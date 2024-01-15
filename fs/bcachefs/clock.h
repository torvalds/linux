/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_CLOCK_H
#define _BCACHEFS_CLOCK_H

void bch2_io_timer_add(struct io_clock *, struct io_timer *);
void bch2_io_timer_del(struct io_clock *, struct io_timer *);
void bch2_kthread_io_clock_wait(struct io_clock *, unsigned long,
				unsigned long);

void __bch2_increment_clock(struct io_clock *, unsigned);

static inline void bch2_increment_clock(struct bch_fs *c, unsigned sectors,
					int rw)
{
	struct io_clock *clock = &c->io_clock[rw];

	if (unlikely(this_cpu_add_return(*clock->pcpu_buf, sectors) >=
		   IO_CLOCK_PCPU_SECTORS))
		__bch2_increment_clock(clock, this_cpu_xchg(*clock->pcpu_buf, 0));
}

void bch2_io_clock_schedule_timeout(struct io_clock *, unsigned long);

#define bch2_kthread_wait_event_ioclock_timeout(condition, clock, timeout)\
({									\
	long __ret = timeout;						\
	might_sleep();							\
	if (!___wait_cond_timeout(condition))				\
		__ret = __wait_event_timeout(wq, condition, timeout);	\
	__ret;								\
})

void bch2_io_timers_to_text(struct printbuf *, struct io_clock *);

void bch2_io_clock_exit(struct io_clock *);
int bch2_io_clock_init(struct io_clock *);

#endif /* _BCACHEFS_CLOCK_H */
