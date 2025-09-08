/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_THREAD_INFO_TIF_H_
#define _ASM_GENERIC_THREAD_INFO_TIF_H_

#include <vdso/bits.h>

/* Bits 16-31 are reserved for architecture specific purposes */

#define TIF_NOTIFY_RESUME	0	// callback before returning to user
#define _TIF_NOTIFY_RESUME	BIT(TIF_NOTIFY_RESUME)

#define TIF_SIGPENDING		1	// signal pending
#define _TIF_SIGPENDING		BIT(TIF_SIGPENDING)

#define TIF_NOTIFY_SIGNAL	2	// signal notifications exist
#define _TIF_NOTIFY_SIGNAL	BIT(TIF_NOTIFY_SIGNAL)

#define TIF_MEMDIE		3	// is terminating due to OOM killer
#define _TIF_MEMDIE		BIT(TIF_MEMDIE)

#define TIF_NEED_RESCHED	4	// rescheduling necessary
#define _TIF_NEED_RESCHED	BIT(TIF_NEED_RESCHED)

#ifdef HAVE_TIF_NEED_RESCHED_LAZY
# define TIF_NEED_RESCHED_LAZY	5	// Lazy rescheduling needed
# define _TIF_NEED_RESCHED_LAZY	BIT(TIF_NEED_RESCHED_LAZY)
#endif

#ifdef HAVE_TIF_POLLING_NRFLAG
# define TIF_POLLING_NRFLAG	6	// idle is polling for TIF_NEED_RESCHED
# define _TIF_POLLING_NRFLAG	BIT(TIF_POLLING_NRFLAG)
#endif

#define TIF_USER_RETURN_NOTIFY	7	// notify kernel of userspace return
#define _TIF_USER_RETURN_NOTIFY	BIT(TIF_USER_RETURN_NOTIFY)

#define TIF_UPROBE		8	// breakpointed or singlestepping
#define _TIF_UPROBE		BIT(TIF_UPROBE)

#define TIF_PATCH_PENDING	9	// pending live patching update
#define _TIF_PATCH_PENDING	BIT(TIF_PATCH_PENDING)

#ifdef HAVE_TIF_RESTORE_SIGMASK
# define TIF_RESTORE_SIGMASK	10	// Restore signal mask in do_signal() */
# define _TIF_RESTORE_SIGMASK	BIT(TIF_RESTORE_SIGMASK)
#endif

#endif /* _ASM_GENERIC_THREAD_INFO_TIF_H_ */
