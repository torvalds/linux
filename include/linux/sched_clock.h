/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sched_clock.h: support for extending counters to full 64-bit ns counter
 */
#ifndef LINUX_SCHED_CLOCK
#define LINUX_SCHED_CLOCK

#ifdef CONFIG_GENERIC_SCHED_CLOCK
/**
 * struct clock_read_data - data required to read from sched_clock()
 *
 * @epoch_ns:		sched_clock() value at last update
 * @epoch_cyc:		Clock cycle value at last update.
 * @sched_clock_mask:   Bitmask for two's complement subtraction of non 64bit
 *			clocks.
 * @read_sched_clock:	Current clock source (or dummy source when suspended).
 * @mult:		Multipler for scaled math conversion.
 * @shift:		Shift value for scaled math conversion.
 *
 * Care must be taken when updating this structure; it is read by
 * some very hot code paths. It occupies <=40 bytes and, when combined
 * with the seqcount used to synchronize access, comfortably fits into
 * a 64 byte cache line.
 */
struct clock_read_data {
	u64 epoch_ns;
	u64 epoch_cyc;
	u64 sched_clock_mask;
	u64 (*read_sched_clock)(void);
	u32 mult;
	u32 shift;
};

extern struct clock_read_data *sched_clock_read_begin(unsigned int *seq);
extern int sched_clock_read_retry(unsigned int seq);

extern void generic_sched_clock_init(void);

extern void sched_clock_register(u64 (*read)(void), int bits,
				 unsigned long rate);
#else
static inline void generic_sched_clock_init(void) { }

static inline void sched_clock_register(u64 (*read)(void), int bits,
					unsigned long rate)
{
}
#endif

#endif
