/*
 * Detect Hard Lockups using the NMI
 *
 * started by Don Zickus, Copyright (C) 2010 Red Hat, Inc.
 *
 * this code detects hard lockups: incidents in where on a CPU
 * the kernel does not respond to anything except NMI.
 *
 * Note: Most of this code is borrowed heavily from softlockup.c,
 * so thanks to Ingo for the initial implementation.
 * Some chunks also taken from arch/x86/kernel/apic/nmi.c, thanks
 * to those contributors as well.
 */

#include <linux/mm.h>
#include <linux/cpu.h>
#include <linux/nmi.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/lockdep.h>
#include <linux/notifier.h>
#include <linux/module.h>
#include <linux/sysctl.h>

#include <asm/irq_regs.h>
#include <linux/perf_event.h>

static DEFINE_PER_CPU(struct perf_event *, nmi_watchdog_ev);
static DEFINE_PER_CPU(int, nmi_watchdog_touch);
static DEFINE_PER_CPU(long, alert_counter);

static int panic_on_timeout;

void touch_nmi_watchdog(void)
{
	__raw_get_cpu_var(nmi_watchdog_touch) = 1;
	touch_softlockup_watchdog();
}
EXPORT_SYMBOL(touch_nmi_watchdog);

void touch_all_nmi_watchdog(void)
{
	int cpu;

	for_each_online_cpu(cpu)
		per_cpu(nmi_watchdog_touch, cpu) = 1;
	touch_softlockup_watchdog();
}

static int __init setup_nmi_watchdog(char *str)
{
	if (!strncmp(str, "panic", 5)) {
		panic_on_timeout = 1;
		str = strchr(str, ',');
		if (!str)
			return 1;
		++str;
	}
	return 1;
}
__setup("nmi_watchdog=", setup_nmi_watchdog);

struct perf_event_attr wd_hw_attr = {
	.type		= PERF_TYPE_HARDWARE,
	.config		= PERF_COUNT_HW_CPU_CYCLES,
	.size		= sizeof(struct perf_event_attr),
	.pinned		= 1,
	.disabled	= 1,
};

struct perf_event_attr wd_sw_attr = {
	.type		= PERF_TYPE_SOFTWARE,
	.config		= PERF_COUNT_SW_CPU_CLOCK,
	.size		= sizeof(struct perf_event_attr),
	.pinned		= 1,
	.disabled	= 1,
};

void wd_overflow(struct perf_event *event, int nmi,
		 struct perf_sample_data *data,
		 struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	int touched = 0;

	if (__get_cpu_var(nmi_watchdog_touch)) {
		per_cpu(nmi_watchdog_touch, cpu) = 0;
		touched = 1;
	}

	/* check to see if the cpu is doing anything */
	if (!touched && hw_nmi_is_cpu_stuck(regs)) {
		/*
		 * Ayiee, looks like this CPU is stuck ...
		 * wait a few IRQs (5 seconds) before doing the oops ...
		 */
		per_cpu(alert_counter, cpu) += 1;
		if (per_cpu(alert_counter, cpu) == 5) {
			if (panic_on_timeout)
				panic("NMI Watchdog detected LOCKUP on cpu %d", cpu);
			else
				WARN(1, "NMI Watchdog detected LOCKUP on cpu %d", cpu);
		}
	} else {
		per_cpu(alert_counter, cpu) = 0;
	}

	return;
}

static int enable_nmi_watchdog(int cpu)
{
	struct perf_event *event;
	struct perf_event_attr *wd_attr;

	event = per_cpu(nmi_watchdog_ev, cpu);
	if (event && event->state > PERF_EVENT_STATE_OFF)
		return 0;

	if (event == NULL) {
		/* Try to register using hardware perf events first */
		wd_attr = &wd_hw_attr;
		wd_attr->sample_period = hw_nmi_get_sample_period();
		event = perf_event_create_kernel_counter(wd_attr, cpu, -1, wd_overflow);
		if (IS_ERR(event)) {
			/* hardware doesn't exist or not supported, fallback to software events */
			printk(KERN_INFO "nmi_watchdog: hardware not available, trying software events\n");
			wd_attr = &wd_sw_attr;
			wd_attr->sample_period = NSEC_PER_SEC;
			event = perf_event_create_kernel_counter(wd_attr, cpu, -1, wd_overflow);
			if (IS_ERR(event)) {
				printk(KERN_ERR "nmi watchdog failed to create perf event on %i: %p\n", cpu, event);
				return -1;
			}
		}
		per_cpu(nmi_watchdog_ev, cpu) = event;
	}
	perf_event_enable(per_cpu(nmi_watchdog_ev, cpu));
	return 0;
}

static void disable_nmi_watchdog(int cpu)
{
	struct perf_event *event;

	event = per_cpu(nmi_watchdog_ev, cpu);
	if (event) {
		perf_event_disable(per_cpu(nmi_watchdog_ev, cpu));
		per_cpu(nmi_watchdog_ev, cpu) = NULL;
		perf_event_release_kernel(event);
	}
}

#ifdef CONFIG_SYSCTL
/*
 * proc handler for /proc/sys/kernel/nmi_watchdog
 */
int nmi_watchdog_enabled;

int proc_nmi_enabled(struct ctl_table *table, int write,
		     void __user *buffer, size_t *length, loff_t *ppos)
{
	int cpu;

	if (!write) {
		struct perf_event *event;
		for_each_online_cpu(cpu) {
			event = per_cpu(nmi_watchdog_ev, cpu);
			if (event && event->state > PERF_EVENT_STATE_OFF) {
				nmi_watchdog_enabled = 1;
				break;
			}
		}
		proc_dointvec(table, write, buffer, length, ppos);
		return 0;
	}

	touch_all_nmi_watchdog();
	proc_dointvec(table, write, buffer, length, ppos);
	if (nmi_watchdog_enabled) {
		for_each_online_cpu(cpu)
			if (enable_nmi_watchdog(cpu)) {
				printk(KERN_ERR "NMI watchdog failed configuration, "
					" can not be enabled\n");
			}
	} else {
		for_each_online_cpu(cpu)
			disable_nmi_watchdog(cpu);
	}
	return 0;
}

#endif /* CONFIG_SYSCTL */

/*
 * Create/destroy watchdog threads as CPUs come and go:
 */
static int __cpuinit
cpu_callback(struct notifier_block *nfb, unsigned long action, void *hcpu)
{
	int hotcpu = (unsigned long)hcpu;

	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		per_cpu(nmi_watchdog_touch, hotcpu) = 0;
		break;
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		if (enable_nmi_watchdog(hotcpu))
			return NOTIFY_BAD;
		break;
#ifdef CONFIG_HOTPLUG_CPU
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
		disable_nmi_watchdog(hotcpu);
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		break;
#endif /* CONFIG_HOTPLUG_CPU */
	}
	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata cpu_nfb = {
	.notifier_call = cpu_callback
};

static int __initdata nonmi_watchdog;

static int __init nonmi_watchdog_setup(char *str)
{
	nonmi_watchdog = 1;
	return 1;
}
__setup("nonmi_watchdog", nonmi_watchdog_setup);

static int __init spawn_nmi_watchdog_task(void)
{
	void *cpu = (void *)(long)smp_processor_id();
	int err;

	if (nonmi_watchdog)
		return 0;

	printk(KERN_INFO "NMI watchdog enabled, takes one hw-pmu counter.\n");

	err = cpu_callback(&cpu_nfb, CPU_UP_PREPARE, cpu);
	if (err == NOTIFY_BAD) {
		BUG();
		return 1;
	}
	cpu_callback(&cpu_nfb, CPU_ONLINE, cpu);
	register_cpu_notifier(&cpu_nfb);

	return 0;
}
early_initcall(spawn_nmi_watchdog_task);
