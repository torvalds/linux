#ifndef _TOOLS_LINUX_RING_BUFFER_H_
#define _TOOLS_LINUX_RING_BUFFER_H_

#include <asm/barrier.h>

/*
 * Contract with kernel for walking the perf ring buffer from
 * user space requires the following barrier pairing (quote
 * from kernel/events/ring_buffer.c):
 *
 *   Since the mmap() consumer (userspace) can run on a
 *   different CPU:
 *
 *   kernel                             user
 *
 *   if (LOAD ->data_tail) {            LOAD ->data_head
 *                      (A)             smp_rmb()       (C)
 *      STORE $data                     LOAD $data
 *      smp_wmb()       (B)             smp_mb()        (D)
 *      STORE ->data_head               STORE ->data_tail
 *   }
 *
 *   Where A pairs with D, and B pairs with C.
 *
 *   In our case A is a control dependency that separates the
 *   load of the ->data_tail and the stores of $data. In case
 *   ->data_tail indicates there is no room in the buffer to
 *   store $data we do not.
 *
 *   D needs to be a full barrier since it separates the data
 *   READ from the tail WRITE.
 *
 *   For B a WMB is sufficient since it separates two WRITEs,
 *   and for C an RMB is sufficient since it separates two READs.
 *
 * Note, instead of B, C, D we could also use smp_store_release()
 * in B and D as well as smp_load_acquire() in C.
 *
 * However, this optimization does not make sense for all kernel
 * supported architectures since for a fair number it would
 * resolve into READ_ONCE() + smp_mb() pair for smp_load_acquire(),
 * and smp_mb() + WRITE_ONCE() pair for smp_store_release().
 *
 * Thus for those smp_wmb() in B and smp_rmb() in C would still
 * be less expensive. For the case of D this has either the same
 * cost or is less expensive, for example, due to TSO x86 can
 * avoid the CPU barrier entirely.
 */

static inline u64 ring_buffer_read_head(struct perf_event_mmap_page *base)
{
/*
 * Architectures where smp_load_acquire() does not fallback to
 * READ_ONCE() + smp_mb() pair.
 */
#if defined(__x86_64__) || defined(__aarch64__) || defined(__powerpc64__) || \
    defined(__ia64__) || defined(__sparc__) && defined(__arch64__)
	return smp_load_acquire(&base->data_head);
#else
	u64 head = READ_ONCE(base->data_head);

	smp_rmb();
	return head;
#endif
}

static inline void ring_buffer_write_tail(struct perf_event_mmap_page *base,
					  u64 tail)
{
	smp_store_release(&base->data_tail, tail);
}

#endif /* _TOOLS_LINUX_RING_BUFFER_H_ */
