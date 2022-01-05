/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_U64_STATS_SYNC_H
#define _LINUX_U64_STATS_SYNC_H

/*
 * Protect against 64-bit values tearing on 32-bit architectures. This is
 * typically used for statistics read/update in different subsystems.
 *
 * Key points :
 *
 * -  Use a seqcount on 32-bit SMP, only disable preemption for 32-bit UP.
 * -  The whole thing is a no-op on 64-bit architectures.
 *
 * Usage constraints:
 *
 * 1) Write side must ensure mutual exclusion, or one seqcount update could
 *    be lost, thus blocking readers forever.
 *
 * 2) Write side must disable preemption, or a seqcount reader can preempt the
 *    writer and also spin forever.
 *
 * 3) Write side must use the _irqsave() variant if other writers, or a reader,
 *    can be invoked from an IRQ context.
 *
 * 4) If reader fetches several counters, there is no guarantee the whole values
 *    are consistent w.r.t. each other (remember point #2: seqcounts are not
 *    used for 64bit architectures).
 *
 * 5) Readers are allowed to sleep or be preempted/interrupted: they perform
 *    pure reads.
 *
 * 6) Readers must use both u64_stats_fetch_{begin,retry}_irq() if the stats
 *    might be updated from a hardirq or softirq context (remember point #1:
 *    seqcounts are not used for UP kernels). 32-bit UP stat readers could read
 *    corrupted 64-bit values otherwise.
 *
 * Usage :
 *
 * Stats producer (writer) should use following template granted it already got
 * an exclusive access to counters (a lock is already taken, or per cpu
 * data is used [in a non preemptable context])
 *
 *   spin_lock_bh(...) or other synchronization to get exclusive access
 *   ...
 *   u64_stats_update_begin(&stats->syncp);
 *   u64_stats_add(&stats->bytes64, len); // non atomic operation
 *   u64_stats_inc(&stats->packets64);    // non atomic operation
 *   u64_stats_update_end(&stats->syncp);
 *
 * While a consumer (reader) should use following template to get consistent
 * snapshot for each variable (but no guarantee on several ones)
 *
 * u64 tbytes, tpackets;
 * unsigned int start;
 *
 * do {
 *         start = u64_stats_fetch_begin(&stats->syncp);
 *         tbytes = u64_stats_read(&stats->bytes64); // non atomic operation
 *         tpackets = u64_stats_read(&stats->packets64); // non atomic operation
 * } while (u64_stats_fetch_retry(&stats->syncp, start));
 *
 *
 * Example of use in drivers/net/loopback.c, using per_cpu containers,
 * in BH disabled context.
 */
#include <linux/seqlock.h>

struct u64_stats_sync {
#if BITS_PER_LONG==32 && defined(CONFIG_SMP)
	seqcount_t	seq;
#endif
};

#if BITS_PER_LONG == 64
#include <asm/local64.h>

typedef struct {
	local64_t	v;
} u64_stats_t ;

static inline u64 u64_stats_read(const u64_stats_t *p)
{
	return local64_read(&p->v);
}

static inline void u64_stats_set(u64_stats_t *p, u64 val)
{
	local64_set(&p->v, val);
}

static inline void u64_stats_add(u64_stats_t *p, unsigned long val)
{
	local64_add(val, &p->v);
}

static inline void u64_stats_inc(u64_stats_t *p)
{
	local64_inc(&p->v);
}

#else

typedef struct {
	u64		v;
} u64_stats_t;

static inline u64 u64_stats_read(const u64_stats_t *p)
{
	return p->v;
}

static inline void u64_stats_set(u64_stats_t *p, u64 val)
{
	p->v = val;
}

static inline void u64_stats_add(u64_stats_t *p, unsigned long val)
{
	p->v += val;
}

static inline void u64_stats_inc(u64_stats_t *p)
{
	p->v++;
}
#endif

#if BITS_PER_LONG == 32 && defined(CONFIG_SMP)
#define u64_stats_init(syncp)	seqcount_init(&(syncp)->seq)
#else
static inline void u64_stats_init(struct u64_stats_sync *syncp)
{
}
#endif

static inline void u64_stats_update_begin(struct u64_stats_sync *syncp)
{
#if BITS_PER_LONG==32 && defined(CONFIG_SMP)
	write_seqcount_begin(&syncp->seq);
#endif
}

static inline void u64_stats_update_end(struct u64_stats_sync *syncp)
{
#if BITS_PER_LONG==32 && defined(CONFIG_SMP)
	write_seqcount_end(&syncp->seq);
#endif
}

static inline unsigned long
u64_stats_update_begin_irqsave(struct u64_stats_sync *syncp)
{
	unsigned long flags = 0;

#if BITS_PER_LONG==32 && defined(CONFIG_SMP)
	local_irq_save(flags);
	write_seqcount_begin(&syncp->seq);
#endif
	return flags;
}

static inline void
u64_stats_update_end_irqrestore(struct u64_stats_sync *syncp,
				unsigned long flags)
{
#if BITS_PER_LONG==32 && defined(CONFIG_SMP)
	write_seqcount_end(&syncp->seq);
	local_irq_restore(flags);
#endif
}

static inline unsigned int __u64_stats_fetch_begin(const struct u64_stats_sync *syncp)
{
#if BITS_PER_LONG==32 && defined(CONFIG_SMP)
	return read_seqcount_begin(&syncp->seq);
#else
	return 0;
#endif
}

static inline unsigned int u64_stats_fetch_begin(const struct u64_stats_sync *syncp)
{
#if BITS_PER_LONG==32 && !defined(CONFIG_SMP)
	preempt_disable();
#endif
	return __u64_stats_fetch_begin(syncp);
}

static inline bool __u64_stats_fetch_retry(const struct u64_stats_sync *syncp,
					 unsigned int start)
{
#if BITS_PER_LONG==32 && defined(CONFIG_SMP)
	return read_seqcount_retry(&syncp->seq, start);
#else
	return false;
#endif
}

static inline bool u64_stats_fetch_retry(const struct u64_stats_sync *syncp,
					 unsigned int start)
{
#if BITS_PER_LONG==32 && !defined(CONFIG_SMP)
	preempt_enable();
#endif
	return __u64_stats_fetch_retry(syncp, start);
}

/*
 * In case irq handlers can update u64 counters, readers can use following helpers
 * - SMP 32bit arches use seqcount protection, irq safe.
 * - UP 32bit must disable irqs.
 * - 64bit have no problem atomically reading u64 values, irq safe.
 */
static inline unsigned int u64_stats_fetch_begin_irq(const struct u64_stats_sync *syncp)
{
#if BITS_PER_LONG==32 && !defined(CONFIG_SMP)
	local_irq_disable();
#endif
	return __u64_stats_fetch_begin(syncp);
}

static inline bool u64_stats_fetch_retry_irq(const struct u64_stats_sync *syncp,
					     unsigned int start)
{
#if BITS_PER_LONG==32 && !defined(CONFIG_SMP)
	local_irq_enable();
#endif
	return __u64_stats_fetch_retry(syncp, start);
}

#endif /* _LINUX_U64_STATS_SYNC_H */
