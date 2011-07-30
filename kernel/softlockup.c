/*
 * Detect Soft Lockups
 *
 * started by Ingo Molnar, Copyright (C) 2005, 2006 Red Hat, Inc.
 *
 * this code detects soft lockups: incidents in where on a CPU
 * the kernel does not reschedule for 10 seconds or more.
 */
#include <linux/mm.h>
#include <linux/cpu.h>
#include <linux/nmi.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/lockdep.h>
#include <linux/notifier.h>
#include <linux/module.h>
#include <linux/sysctl.h>

#include <asm/irq_regs.h>

static DEFINE_SPINLOCK(print_lock);

static DEFINE_PER_CPU(unsigned long, touch_timestamp);
static DEFINE_PER_CPU(unsigned long, print_timestamp);
static DEFINE_PER_CPU(struct task_struct *, watchdog_task);

static int __read_mostly did_panic;
int __read_mostly softlockup_thresh = 60;

/*
 * Should we panic (and reboot, if panic_timeout= is set) when a
 * soft-lockup occurs:
 */
unsigned int __read_mostly softlockup_panic =
				CONFIG_BOOTPARAM_SOFTLOCKUP_PANIC_VALUE;

static int __init softlockup_panic_setup(char *str)
{
	softlockup_panic = simple_strtoul(str, NULL, 0);

	return 1;
}
__setup("softlockup_panic=", softlockup_panic_setup);

static int
softlock_panic(struct notifier_block *this, unsigned long event, void *ptr)
{
	did_panic = 1;

	return NOTIFY_DONE;
}

static struct notifier_block panic_block = {
	.notifier_call = softlock_panic,
};

/*
 * Returns seconds, approximately.  We don't need nanosecond
 * resolution, and we don't need to waste time with a big divide when
 * 2^30ns == 1.074s.
 */
static unsigned long get_timestamp(int this_cpu)
{
	return cpu_clock(this_cpu) >> 30LL;  /* 2^30 ~= 10^9 */
}

static void __touch_softlockup_watchdog(void)
{
	int this_cpu = raw_smp_processor_id();

	__raw_get_cpu_var(touch_timestamp) = get_timestamp(this_cpu);
}

void touch_softlockup_watchdog(void)
{
	__raw_get_cpu_var(touch_timestamp) = 0;
}
EXPORT_SYMBOL(touch_softlockup_watchdog);

void touch_all_softlockup_watchdogs(void)
{
	int cpu;

	/* Cause each CPU to re-update its timestamp rather than complain */
	for_each_online_cpu(cpu)
		per_cpu(touch_timestamp, cpu) = 0;
}
EXPORT_SYMBOL(touch_all_softlockup_watchdogs);

int proc_dosoftlockup_thresh(struct ctl_table *table, int write,
			     void __user *buffer,
			     size_t *lenp, loff_t *ppos)
{
	touch_all_softlockup_watchdogs();
	return proc_dointvec_minmax(table, write, buffer, lenp, ppos);
}

/*
 * This callback runs from the timer interrupt, and checks
 * whether the watchdog thread has hung or not:
 */
void softlockup_tick(void)
{
	int this_cpu = smp_processor_id();
	unsigned long touch_timestamp = per_cpu(touch_timestamp, this_cpu);
	unsigned long print_timestamp;
	struct pt_regs *regs = get_irq_regs();
	unsigned long now;

	/* Is detection switched off? */
	if (!per_cpu(watchdog_task, this_cpu) || softlockup_thresh <= 0) {
		/* Be sure we don't false trigger if switched back on */
		if (touch_timestamp)
			per_cpu(touch_timestamp, this_cpu) = 0;
		return;
	}

	if (touch_timestamp == 0) {
		__touch_softlockup_watchdog();
		return;
	}

	print_timestamp = per_cpu(print_timestamp, this_cpu);

	/* report at most once a second */
	if (print_timestamp == touch_timestamp || did_panic)
		return;

	/* do not print during early bootup: */
	if (unlikely(system_state != SYSTEM_RUNNING)) {
		__touch_softlockup_watchdog();
		return;
	}

	now = get_timestamp(this_cpu);

	/*
	 * Wake up the high-prio watchdog task twice per
	 * threshold timespan.
	 */
	if (now > touch_timestamp + softlockup_thresh/2)
		wake_up_process(per_cpu(watchdog_task, this_cpu));

	/* Warn about unreasonable delays: */
	if (now <= (touch_timestamp + softlockup_thresh))
		return;

	per_cpu(print_timestamp, this_cpu) = touch_timestamp;

	spin_lock(&print_lock);
	printk(KERN_ERR "BUG: soft lockup - CPU#%d stuck for %lus! [%s:%d]\n",
			this_cpu, now - touch_timestamp,
			current->comm, task_pid_nr(current));
	print_modules();
	print_irqtrace_events(current);
	if (regs)
		show_regs(regs);
	else
		dump_stack();
	spin_unlock(&print_lock);

	if (softlockup_panic)
		panic("softlockup: hung tasks");
}

/*
 * The watchdog thread - runs every second and touches the timestamp.
 */
static int watchdog(void *__bind_cpu)
{
	struct sched_param param = { .sched_priority = MAX_RT_PRIO-1 };

	sched_setscheduler(current, SCHED_FIFO, &param);

	/* initialize timestamp */
	__touch_softlockup_watchdog();

	set_current_state(TASK_INTERRUPTIBLE);
	/*
	 * Run briefly once per second to reset the softlockup timestamp.
	 * If this gets delayed for more than 60 seconds then the
	 * debug-printout triggers in softlockup_tick().
	 */
	while (!kthread_should_stop()) {
		__touch_softlockup_watchdog();
		schedule();

		if (kthread_should_stop())
			break;

		set_current_state(TASK_INTERRUPTIBLE);
	}
	__set_current_state(TASK_RUNNING);

	return 0;
}

/*
 * Create/destroy watchdog threads as CPUs come and go:
 */
static int __cpuinit
cpu_callback(struct notifier_block *nfb, unsigned long action, void *hcpu)
{
	int hotcpu = (unsigned long)hcpu;
	struct task_struct *p;

	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		BUG_ON(per_cpu(watchdog_task, hotcpu));
		p = kthread_create(watchdog, hcpu, "watchdog/%d", hotcpu);
		if (IS_ERR(p)) {
			printk(KERN_ERR "watchdog for %i failed\n", hotcpu);
			return NOTIFY_BAD;
		}
		per_cpu(touch_timestamp, hotcpu) = 0;
		per_cpu(watchdog_task, hotcpu) = p;
		kthread_bind(p, hotcpu);
		break;
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		wake_up_process(per_cpu(watchdog_task, hotcpu));
		break;
#ifdef CONFIG_HOTPLUG_CPU
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
		if (!per_cpu(watchdog_task, hotcpu))
			break;
		/* Unbind so it can run.  Fall thru. */
		kthread_bind(per_cpu(watchdog_task, hotcpu),
			     cpumask_any(cpu_online_mask));
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		p = per_cpu(watchdog_task, hotcpu);
		per_cpu(watchdog_task, hotcpu) = NULL;
		kthread_stop(p);
		break;
#endif /* CONFIG_HOTPLUG_CPU */
	}
	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata cpu_nfb = {
	.notifier_call = cpu_callback
};

static int __initdata nosoftlockup;

static int __init nosoftlockup_setup(char *str)
{
	nosoftlockup = 1;
	return 1;
}
__setup("nosoftlockup", nosoftlockup_setup);

static int __init spawn_softlockup_task(void)
{
	void *cpu = (void *)(long)smp_processor_id();
	int err;

	if (nosoftlockup)
		return 0;

	err = cpu_callback(&cpu_nfb, CPU_UP_PREPARE, cpu);
	if (err == NOTIFY_BAD) {
		BUG();
		return 1;
	}
	cpu_callback(&cpu_nfb, CPU_ONLINE, cpu);
	register_cpu_notifier(&cpu_nfb);

	atomic_notifier_chain_register(&panic_notifier_list, &panic_block);

	return 0;
}
early_initcall(spawn_softlockup_task);
