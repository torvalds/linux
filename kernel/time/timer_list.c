// SPDX-License-Identifier: GPL-2.0
/*
 * List pending timers
 *
 * Copyright(C) 2006, Red Hat, Inc., Ingo Molnar
 */

#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/kallsyms.h>
#include <linux/nmi.h>

#include <linux/uaccess.h>

#include "tick-internal.h"

struct timer_list_iter {
	int cpu;
	bool second_pass;
	u64 now;
};

/*
 * This allows printing both to /proc/timer_list and
 * to the console (on SysRq-Q):
 */
__printf(2, 3)
static void SEQ_printf(struct seq_file *m, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);

	if (m)
		seq_vprintf(m, fmt, args);
	else
		vprintk(fmt, args);

	va_end(args);
}

static void
print_timer(struct seq_file *m, struct hrtimer *taddr, struct hrtimer *timer,
	    int idx, u64 now)
{
	SEQ_printf(m, " #%d: <%p>, %ps", idx, taddr, timer->function);
	SEQ_printf(m, ", S:%02x", timer->state);
	SEQ_printf(m, "\n");
	SEQ_printf(m, " # expires at %Lu-%Lu nsecs [in %Ld to %Ld nsecs]\n",
		(unsigned long long)ktime_to_ns(hrtimer_get_softexpires(timer)),
		(unsigned long long)ktime_to_ns(hrtimer_get_expires(timer)),
		(long long)(ktime_to_ns(hrtimer_get_softexpires(timer)) - now),
		(long long)(ktime_to_ns(hrtimer_get_expires(timer)) - now));
}

static void
print_active_timers(struct seq_file *m, struct hrtimer_clock_base *base,
		    u64 now)
{
	struct hrtimer *timer, tmp;
	unsigned long next = 0, i;
	struct timerqueue_node *curr;
	unsigned long flags;

next_one:
	i = 0;

	touch_nmi_watchdog();

	raw_spin_lock_irqsave(&base->cpu_base->lock, flags);

	curr = timerqueue_getnext(&base->active);
	/*
	 * Crude but we have to do this O(N*N) thing, because
	 * we have to unlock the base when printing:
	 */
	while (curr && i < next) {
		curr = timerqueue_iterate_next(curr);
		i++;
	}

	if (curr) {

		timer = container_of(curr, struct hrtimer, node);
		tmp = *timer;
		raw_spin_unlock_irqrestore(&base->cpu_base->lock, flags);

		print_timer(m, timer, &tmp, i, now);
		next++;
		goto next_one;
	}
	raw_spin_unlock_irqrestore(&base->cpu_base->lock, flags);
}

static void
print_base(struct seq_file *m, struct hrtimer_clock_base *base, u64 now)
{
	SEQ_printf(m, "  .base:       %p\n", base);
	SEQ_printf(m, "  .index:      %d\n", base->index);

	SEQ_printf(m, "  .resolution: %u nsecs\n", hrtimer_resolution);

	SEQ_printf(m,   "  .get_time:   %ps\n", base->get_time);
#ifdef CONFIG_HIGH_RES_TIMERS
	SEQ_printf(m, "  .offset:     %Lu nsecs\n",
		   (unsigned long long) ktime_to_ns(base->offset));
#endif
	SEQ_printf(m,   "active timers:\n");
	print_active_timers(m, base, now + ktime_to_ns(base->offset));
}

static void print_cpu(struct seq_file *m, int cpu, u64 now)
{
	struct hrtimer_cpu_base *cpu_base = &per_cpu(hrtimer_bases, cpu);
	int i;

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
	P(nr_retries);
	P(nr_hangs);
	P(max_hang_time);
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
# define P_flag(x, f)			    \
	SEQ_printf(m, "  .%-15s: %d\n", #x, !!(ts->flags & (f)))

	{
		struct tick_sched *ts = tick_get_tick_sched(cpu);
		P_flag(nohz, TS_FLAG_NOHZ);
		P_flag(highres, TS_FLAG_HIGHRES);
		P_ns(last_tick);
		P_flag(tick_stopped, TS_FLAG_STOPPED);
		P(idle_jiffies);
		P(idle_calls);
		P(idle_sleeps);
		P_ns(idle_entrytime);
		P_ns(idle_waketime);
		P_ns(idle_exittime);
		P_ns(idle_sleeptime);
		P_ns(iowait_sleeptime);
		P(last_jiffies);
		P(next_timer);
		P_ns(idle_expires);
		SEQ_printf(m, "jiffies: %Lu\n",
			   (unsigned long long)jiffies);
	}
#endif

#undef P
#undef P_ns
	SEQ_printf(m, "\n");
}

#ifdef CONFIG_GENERIC_CLOCKEVENTS
static void
print_tickdevice(struct seq_file *m, struct tick_device *td, int cpu)
{
	struct clock_event_device *dev = td->evtdev;

	touch_nmi_watchdog();

	SEQ_printf(m, "Tick Device: mode:     %d\n", td->mode);
	if (cpu < 0)
		SEQ_printf(m, "Broadcast device\n");
	else
		SEQ_printf(m, "Per CPU device: %d\n", cpu);

	SEQ_printf(m, "Clock Event Device: ");
	if (!dev) {
		SEQ_printf(m, "<NULL>\n");
		return;
	}
	SEQ_printf(m, "%s\n", dev->name);
	SEQ_printf(m, " max_delta_ns:   %llu\n",
		   (unsigned long long) dev->max_delta_ns);
	SEQ_printf(m, " min_delta_ns:   %llu\n",
		   (unsigned long long) dev->min_delta_ns);
	SEQ_printf(m, " mult:           %u\n", dev->mult);
	SEQ_printf(m, " shift:          %u\n", dev->shift);
	SEQ_printf(m, " mode:           %d\n", clockevent_get_state(dev));
	SEQ_printf(m, " next_event:     %Ld nsecs\n",
		   (unsigned long long) ktime_to_ns(dev->next_event));

	SEQ_printf(m, " set_next_event: %ps\n", dev->set_next_event);

	if (dev->set_state_shutdown)
		SEQ_printf(m, " shutdown:       %ps\n",
			dev->set_state_shutdown);

	if (dev->set_state_periodic)
		SEQ_printf(m, " periodic:       %ps\n",
			dev->set_state_periodic);

	if (dev->set_state_oneshot)
		SEQ_printf(m, " oneshot:        %ps\n",
			dev->set_state_oneshot);

	if (dev->set_state_oneshot_stopped)
		SEQ_printf(m, " oneshot stopped: %ps\n",
			dev->set_state_oneshot_stopped);

	if (dev->tick_resume)
		SEQ_printf(m, " resume:         %ps\n",
			dev->tick_resume);

	SEQ_printf(m, " event_handler:  %ps\n", dev->event_handler);
	SEQ_printf(m, "\n");
	SEQ_printf(m, " retries:        %lu\n", dev->retries);

#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
	if (cpu >= 0) {
		const struct clock_event_device *wd = tick_get_wakeup_device(cpu);

		SEQ_printf(m, "Wakeup Device: %s\n", wd ? wd->name : "<NULL>");
	}
#endif
	SEQ_printf(m, "\n");
}

static void timer_list_show_tickdevices_header(struct seq_file *m)
{
#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
	print_tickdevice(m, tick_get_broadcast_device(), -1);
	SEQ_printf(m, "tick_broadcast_mask: %*pb\n",
		   cpumask_pr_args(tick_get_broadcast_mask()));
#ifdef CONFIG_TICK_ONESHOT
	SEQ_printf(m, "tick_broadcast_oneshot_mask: %*pb\n",
		   cpumask_pr_args(tick_get_broadcast_oneshot_mask()));
#endif
	SEQ_printf(m, "\n");
#endif
}
#endif

static inline void timer_list_header(struct seq_file *m, u64 now)
{
	SEQ_printf(m, "Timer List Version: v0.10\n");
	SEQ_printf(m, "HRTIMER_MAX_CLOCK_BASES: %d\n", HRTIMER_MAX_CLOCK_BASES);
	SEQ_printf(m, "now at %Ld nsecs\n", (unsigned long long)now);
	SEQ_printf(m, "\n");
}

void sysrq_timer_list_show(void)
{
	u64 now = ktime_to_ns(ktime_get());
	int cpu;

	timer_list_header(NULL, now);

	for_each_online_cpu(cpu)
		print_cpu(NULL, cpu, now);

#ifdef CONFIG_GENERIC_CLOCKEVENTS
	timer_list_show_tickdevices_header(NULL);
	for_each_online_cpu(cpu)
		print_tickdevice(NULL, tick_get_device(cpu), cpu);
#endif
	return;
}

#ifdef CONFIG_PROC_FS
static int timer_list_show(struct seq_file *m, void *v)
{
	struct timer_list_iter *iter = v;

	if (iter->cpu == -1 && !iter->second_pass)
		timer_list_header(m, iter->now);
	else if (!iter->second_pass)
		print_cpu(m, iter->cpu, iter->now);
#ifdef CONFIG_GENERIC_CLOCKEVENTS
	else if (iter->cpu == -1 && iter->second_pass)
		timer_list_show_tickdevices_header(m);
	else
		print_tickdevice(m, tick_get_device(iter->cpu), iter->cpu);
#endif
	return 0;
}

static void *move_iter(struct timer_list_iter *iter, loff_t offset)
{
	for (; offset; offset--) {
		iter->cpu = cpumask_next(iter->cpu, cpu_online_mask);
		if (iter->cpu >= nr_cpu_ids) {
#ifdef CONFIG_GENERIC_CLOCKEVENTS
			if (!iter->second_pass) {
				iter->cpu = -1;
				iter->second_pass = true;
			} else
				return NULL;
#else
			return NULL;
#endif
		}
	}
	return iter;
}

static void *timer_list_start(struct seq_file *file, loff_t *offset)
{
	struct timer_list_iter *iter = file->private;

	if (!*offset)
		iter->now = ktime_to_ns(ktime_get());
	iter->cpu = -1;
	iter->second_pass = false;
	return move_iter(iter, *offset);
}

static void *timer_list_next(struct seq_file *file, void *v, loff_t *offset)
{
	struct timer_list_iter *iter = file->private;
	++*offset;
	return move_iter(iter, 1);
}

static void timer_list_stop(struct seq_file *seq, void *v)
{
}

static const struct seq_operations timer_list_sops = {
	.start = timer_list_start,
	.next = timer_list_next,
	.stop = timer_list_stop,
	.show = timer_list_show,
};

static int __init init_timer_list_procfs(void)
{
	struct proc_dir_entry *pe;

	pe = proc_create_seq_private("timer_list", 0400, NULL, &timer_list_sops,
			sizeof(struct timer_list_iter), NULL);
	if (!pe)
		return -ENOMEM;
	return 0;
}
__initcall(init_timer_list_procfs);
#endif
