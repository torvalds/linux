/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_MMIOWB_H
#define __ASM_GENERIC_MMIOWB_H

/*
 * Generic implementation of mmiowb() tracking for spinlocks.
 *
 * If your architecture doesn't ensure that writes to an I/O peripheral
 * within two spinlocked sections on two different CPUs are seen by the
 * peripheral in the order corresponding to the lock handover, then you
 * need to follow these FIVE easy steps:
 *
 * 	1. Implement mmiowb() (and arch_mmiowb_state() if you're fancy)
 *	   in asm/mmiowb.h, then #include this file
 *	2. Ensure your I/O write accessors call mmiowb_set_pending()
 *	3. Select ARCH_HAS_MMIOWB
 *	4. Untangle the resulting mess of header files
 *	5. Complain to your architects
 */
#ifdef CONFIG_MMIOWB

#include <linux/compiler.h>
#include <asm-generic/mmiowb_types.h>

#ifndef arch_mmiowb_state
#include <asm/percpu.h>
#include <asm/smp.h>

DECLARE_PER_CPU(struct mmiowb_state, __mmiowb_state);
#define __mmiowb_state()	this_cpu_ptr(&__mmiowb_state)
#else
#define __mmiowb_state()	arch_mmiowb_state()
#endif	/* arch_mmiowb_state */

static inline void mmiowb_set_pending(void)
{
	struct mmiowb_state *ms = __mmiowb_state();
	ms->mmiowb_pending = ms->nesting_count;
}

static inline void mmiowb_spin_lock(void)
{
	struct mmiowb_state *ms = __mmiowb_state();
	ms->nesting_count++;
}

static inline void mmiowb_spin_unlock(void)
{
	struct mmiowb_state *ms = __mmiowb_state();

	if (unlikely(ms->mmiowb_pending)) {
		ms->mmiowb_pending = 0;
		mmiowb();
	}

	ms->nesting_count--;
}
#else
#define mmiowb_set_pending()		do { } while (0)
#define mmiowb_spin_lock()		do { } while (0)
#define mmiowb_spin_unlock()		do { } while (0)
#endif	/* CONFIG_MMIOWB */
#endif	/* __ASM_GENERIC_MMIOWB_H */
