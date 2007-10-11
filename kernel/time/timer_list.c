/*
 * kernel/time/timer_list.c
 *
 * List pending timers
 *
 * Copyright(C) 2006, Red Hat, Inc., Ingo Molnar
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/kallsyms.h>
#include <linux/tick.h>

#include <asm/uaccess.h>

typedef void (*print_fn_t)(struct seq_file *m, unsigned int *classes);

DECLARE_PER_CPU(struct hrtimer_cpu_base, hrtimer_bases);

/*
 * This allows printing both to /proc/timer_list and
 * to the console (on SysRq-Q):
 */
#define SEQ_printf(m, x...)			\
 do {						\
	if (m)					\
		seq_printf(m, x);		\
	else					\
		printk(x);			\
 } while (0)

static void print_name_offset(struct seq_file *m, void *sym)
{
	char symname[KSYM_NAME_LEN];

	if (lookup_symbol_name((unsigned long)sym, symname) < 0)
		SEQ_printf(m, "<%p>", sym);
	else
		SEQ_printf(m, "%s", symname);
}

static void
print_timer(struct seq_file *m, struct hrtimer *timer, int idx, u64 now)
{
#ifdef CONFIG_TIMER_STATS
	char tmp[TASK_COMM_LEN + 1];
#endif
	SEQ_printf(m, " #%d: ", idx);
	print_name_offset(m, timer);
	SEQ_printf(m, ", ");
	print_name_offset(m, timer->function);
	SEQ_printf(m, ", S:%02lx", timer->state);
#ifdef CONFIG_TIMER_STATS
	SEQ_printf(m, ", ");
	print_name_offset(m, timer->start_site);
	memcpy(tmp, timer->start_comm, TASK_COMM_LEN);
	tmp[TASK_COMM_LEN] = 0;
	SEQ_printf(m, ", %s/%d", tmp, timer->start_pid);
#endif
	SEQ_printf(m, "\n");
	SEQ_printf(m, " # expires at %Lu nsecs [in %Lu nsecs]\n",
		(unsigned long long)ktime_to_ns(timer->expires),
		(unsigned long long)(ktime_to_ns(timer->expires) - now));
}

static void
print_active_timers(struct seq_file *m, struct hrtimer_clock_base *base,
		    u64 now)
{
	struct hrtimer *timer, tmp;
	unsigned long next = 0, i;
	struct rb_node *curr;
	unsigned long flags;

next_one:
	i = 0;
	spin_lock_irqsave(&base->cpu_base->lock, flags);

	curr = base->first;
	/*
	 * Crude but we have to do this O(N*N) thing, because
	 * we have to unlock the base when printing:
	 */
	while (curr && i < next) {
		curr = rb_next(curr);
		i++;
	}

	if (curr) {

		timer = rb_entry(curr, struct hrtimer, node);
		tmp = *timer;
		spin_unlock_irqrestore(&base->cpu_base->lock, flags);

		print_timer(m, &tmp, i, now);
		next++;
		goto next_one;
	}
	spin_unlock_irqrestore(&base->cpu_base->lock, flags);
}

static void
print_base(struct seq_file *m, struct hrtimer_clock_base *base, u64 now)
{
	SEQ_printf(m, "  .index:      %d\n",
			base->index);
	SEQ_printf(m, "  .resolution: %Lu nsecs\n",
			(unsigned long long)ktime_to_ns(base->resolution));
	SEQ_printf(m,   "  .get_time:   ");
	print_name_offset(m, base->get_time);
	SEQ_printf(m,   "\n");
#ifdef CONFIG_HIGH_RES_TIMERS
	SEQ_printf(m, "  .offset:     %Lu nsecs\n",
		   (unsigned long long) ktime_to_ns(base->offset));
#endif
	SEQ_printf(m,   "active timers:\n");
	print_active_timers(m, base, now);
}

static void print_cpu(struct seq_file *m, int cpu, u64 now)
{
	struct hrtimer_cpu_base *cpu_base = &per_cpu(hrtimer_bases, cpu);
	int i;

	SEQ_printf(m, "\n");
	SEQ_printf(m, "cpu: %d\n", cpu);
	for (i = 0; i < HRTIMER_MAX_CLOCK_BASES; i++) {
		SEQ_printf(m, " clock %d:\n", i);
		print_base(m, cpu_base->clock_base + i, now);
	}
#define P(x) \
	SEQ_printf(m, "  .%-15s: %Lu\n", #x, \
		   (unsigned long long)(cpu_base->x))
#define P_ns(x) \
	SEQ_printf(m, "  .%-15s: %Lu nsecs\n", #x, \
		   (unsigned long long)(ktime_to_ns(cpu_base->x)))

#ifdef CONFIG_HIGH_RES_TIMERS
	P_ns(expires_next);
	P(hres_active);
	P(nr_events);
#endif
#undef P
#undef P_ns

#ifdef CONFIG_TICK_ONESHOT
# define P(x) \
	SEQ_printf(m, "  .%-15s: %Lu\n", #x, \
		   (unsigned long long)(ts->x))
# define P_ns(x) \
	SEQ_printf(m, "  .%-15s: %Lu nsecs\n", #x, \
		   (unsigned long long)(ktime_to_ns(ts->x)))
	{
		struct tick_sched *ts = tick_get_tick_sched(cpu);
		P(nohz_mode);
		P_ns(idle_tick);
		P(tick_stopped);
		P(idle_jiffies);
		P(idle_calls);
		P(idle_sleeps);
		P_ns(idle_entrytime);
		P_ns(idle_sleeptime);
		P(last_jiffies);
		P(next_jiffies);
		P_ns(idle_expires);
		SEQ_printf(m, "jiffies: %Lu\n",
			   (unsigned long long)jiffies);
	}
#endif

#undef P
#undef P_ns
}

#ifdef CONFIG_GENERIC_CLOCKEVENTS
static void
print_tickdevice(struct seq_file *m, struct tick_device *td)
{
	struct clock_event_device *dev = td->evtdev;

	SEQ_printf(m, "\n");
	SEQ_printf(m, "Tick Device: mode:     %d\n", td->mode);

	SEQ_printf(m, "Clock Event Device: ");
	if (!dev) {
		SEQ_printf(m, "<NULL>\n");
		return;
	}
	SEQ_printf(m, "%s\n", dev->name);
	SEQ_printf(m, " max_delta_ns:   %lu\n", dev->max_delta_ns);
	SEQ_printf(m, " min_delta_ns:   %lu\n", dev->min_delta_ns);
	SEQ_printf(m, " mult:           %lu\n", dev->mult);
	SEQ_printf(m, " shift:          %d\n", dev->shift);
	SEQ_printf(m, " mode:           %d\n", dev->mode);
	SEQ_printf(m, " next_event:     %Ld nsecs\n",
		   (unsigned long long) ktime_to_ns(dev->next_event));

	SEQ_printf(m, " set_next_event: ");
	print_name_offset(m, dev->set_next_event);
	SEQ_printf(m, "\n");

	SEQ_printf(m, " set_mode:       ");
	print_name_offset(m, dev->set_mode);
	SEQ_printf(m, "\n");

	SEQ_printf(m, " event_handler:  ");
	print_name_offset(m, dev->event_handler);
	SEQ_printf(m, "\n");
}

static void timer_list_show_tickdevices(struct seq_file *m)
{
	int cpu;

#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
	print_tickdevice(m, tick_get_broadcast_device());
	SEQ_printf(m, "tick_broadcast_mask: %08lx\n",
		   tick_get_broadcast_mask()->bits[0]);
#ifdef CONFIG_TICK_ONESHOT
	SEQ_printf(m, "tick_broadcast_oneshot_mask: %08lx\n",
		   tick_get_broadcast_oneshot_mask()->bits[0]);
#endif
	SEQ_printf(m, "\n");
#endif
	for_each_online_cpu(cpu)
		   print_tickdevice(m, tick_get_device(cpu));
	SEQ_printf(m, "\n");
}
#else
static void timer_list_show_tickdevices(struct seq_file *m) { }
#endif

static int timer_list_show(struct seq_file *m, void *v)
{
	u64 now = ktime_to_ns(ktime_get());
	int cpu;

	SEQ_printf(m, "Timer List Version: v0.3\n");
	SEQ_printf(m, "HRTIMER_MAX_CLOCK_BASES: %d\n", HRTIMER_MAX_CLOCK_BASES);
	SEQ_printf(m, "now at %Ld nsecs\n", (unsigned long long)now);

	for_each_online_cpu(cpu)
		print_cpu(m, cpu, now);

	SEQ_printf(m, "\n");
	timer_list_show_tickdevices(m);

	return 0;
}

void sysrq_timer_list_show(void)
{
	timer_list_show(NULL, NULL);
}

static int timer_list_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, timer_list_show, NULL);
}

static struct file_operations timer_list_fops = {
	.open		= timer_list_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init init_timer_list_procfs(void)
{
	struct proc_dir_entry *pe;

	pe = create_proc_entry("timer_list", 0644, NULL);
	if (!pe)
		return -ENOMEM;

	pe->proc_fops = &timer_list_fops;

	return 0;
}
__initcall(init_timer_list_procfs);
