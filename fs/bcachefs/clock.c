// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "clock.h"

#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/preempt.h>

static inline bool io_timer_cmp(const void *l, const void *r, void __always_unused *args)
{
	struct io_timer **_l = (struct io_timer **)l;
	struct io_timer **_r = (struct io_timer **)r;

	return (*_l)->expire < (*_r)->expire;
}

static const struct min_heap_callbacks callbacks = {
	.less = io_timer_cmp,
	.swp = NULL,
};

void bch2_io_timer_add(struct io_clock *clock, struct io_timer *timer)
{
	spin_lock(&clock->timer_lock);

	if (time_after_eq64((u64) atomic64_read(&clock->now), timer->expire)) {
		spin_unlock(&clock->timer_lock);
		timer->fn(timer);
		return;
	}

	for (size_t i = 0; i < clock->timers.nr; i++)
		if (clock->timers.data[i] == timer)
			goto out;

	BUG_ON(!min_heap_push(&clock->timers, &timer, &callbacks, NULL));
out:
	spin_unlock(&clock->timer_lock);
}

void bch2_io_timer_del(struct io_clock *clock, struct io_timer *timer)
{
	spin_lock(&clock->timer_lock);

	for (size_t i = 0; i < clock->timers.nr; i++)
		if (clock->timers.data[i] == timer) {
			min_heap_del(&clock->timers, i, &callbacks, NULL);
			break;
		}

	spin_unlock(&clock->timer_lock);
}

struct io_clock_wait {
	struct io_timer		io_timer;
	struct timer_list	cpu_timer;
	struct task_struct	*task;
	int			expired;
};

static void io_clock_wait_fn(struct io_timer *timer)
{
	struct io_clock_wait *wait = container_of(timer,
				struct io_clock_wait, io_timer);

	wait->expired = 1;
	wake_up_process(wait->task);
}

static void io_clock_cpu_timeout(struct timer_list *timer)
{
	struct io_clock_wait *wait = container_of(timer,
				struct io_clock_wait, cpu_timer);

	wait->expired = 1;
	wake_up_process(wait->task);
}

void bch2_io_clock_schedule_timeout(struct io_clock *clock, u64 until)
{
	struct io_clock_wait wait = {
		.io_timer.expire	= until,
		.io_timer.fn		= io_clock_wait_fn,
		.io_timer.fn2		= (void *) _RET_IP_,
		.task			= current,
	};

	bch2_io_timer_add(clock, &wait.io_timer);
	schedule();
	bch2_io_timer_del(clock, &wait.io_timer);
}

void bch2_kthread_io_clock_wait(struct io_clock *clock,
				u64 io_until, unsigned long cpu_timeout)
{
	bool kthread = (current->flags & PF_KTHREAD) != 0;
	struct io_clock_wait wait = {
		.io_timer.expire	= io_until,
		.io_timer.fn		= io_clock_wait_fn,
		.io_timer.fn2		= (void *) _RET_IP_,
		.task			= current,
	};

	bch2_io_timer_add(clock, &wait.io_timer);

	timer_setup_on_stack(&wait.cpu_timer, io_clock_cpu_timeout, 0);

	if (cpu_timeout != MAX_SCHEDULE_TIMEOUT)
		mod_timer(&wait.cpu_timer, cpu_timeout + jiffies);

	do {
		set_current_state(TASK_INTERRUPTIBLE);
		if (kthread && kthread_should_stop())
			break;

		if (wait.expired)
			break;

		schedule();
		try_to_freeze();
	} while (0);

	__set_current_state(TASK_RUNNING);
	timer_delete_sync(&wait.cpu_timer);
	destroy_timer_on_stack(&wait.cpu_timer);
	bch2_io_timer_del(clock, &wait.io_timer);
}

static struct io_timer *get_expired_timer(struct io_clock *clock, u64 now)
{
	struct io_timer *ret = NULL;

	if (clock->timers.nr &&
	    time_after_eq64(now, clock->timers.data[0]->expire)) {
		ret = *min_heap_peek(&clock->timers);
		min_heap_pop(&clock->timers, &callbacks, NULL);
	}

	return ret;
}

void __bch2_increment_clock(struct io_clock *clock, u64 sectors)
{
	struct io_timer *timer;
	u64 now = atomic64_add_return(sectors, &clock->now);

	spin_lock(&clock->timer_lock);
	while ((timer = get_expired_timer(clock, now)))
		timer->fn(timer);
	spin_unlock(&clock->timer_lock);
}

void bch2_io_timers_to_text(struct printbuf *out, struct io_clock *clock)
{
	out->atomic++;
	spin_lock(&clock->timer_lock);
	u64 now = atomic64_read(&clock->now);

	printbuf_tabstop_push(out, 40);
	prt_printf(out, "current time:\t%llu\n", now);

	for (unsigned i = 0; i < clock->timers.nr; i++)
		prt_printf(out, "%ps %ps:\t%llu\n",
		       clock->timers.data[i]->fn,
		       clock->timers.data[i]->fn2,
		       clock->timers.data[i]->expire);
	spin_unlock(&clock->timer_lock);
	--out->atomic;
}

void bch2_io_clock_exit(struct io_clock *clock)
{
	free_heap(&clock->timers);
	free_percpu(clock->pcpu_buf);
}

int bch2_io_clock_init(struct io_clock *clock)
{
	atomic64_set(&clock->now, 0);
	spin_lock_init(&clock->timer_lock);

	clock->max_slop = IO_CLOCK_PCPU_SECTORS * num_possible_cpus();

	clock->pcpu_buf = alloc_percpu(*clock->pcpu_buf);
	if (!clock->pcpu_buf)
		return -BCH_ERR_ENOMEM_io_clock_init;

	if (!init_heap(&clock->timers, NR_IO_TIMERS, GFP_KERNEL))
		return -BCH_ERR_ENOMEM_io_clock_init;

	return 0;
}
