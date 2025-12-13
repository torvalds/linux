/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_ENTRYVIRT_H
#define __LINUX_ENTRYVIRT_H

#include <linux/static_call_types.h>
#include <linux/resume_user_mode.h>
#include <linux/syscalls.h>
#include <linux/seccomp.h>
#include <linux/sched.h>
#include <linux/tick.h>

/* Transfer to guest mode work */
#ifdef CONFIG_VIRT_XFER_TO_GUEST_WORK

#ifndef ARCH_XFER_TO_GUEST_MODE_WORK
# define ARCH_XFER_TO_GUEST_MODE_WORK	(0)
#endif

#define XFER_TO_GUEST_MODE_WORK						\
	(_TIF_NEED_RESCHED | _TIF_NEED_RESCHED_LAZY | _TIF_SIGPENDING | \
	 _TIF_NOTIFY_SIGNAL | _TIF_NOTIFY_RESUME |			\
	 ARCH_XFER_TO_GUEST_MODE_WORK)

/**
 * arch_xfer_to_guest_mode_handle_work - Architecture specific xfer to guest
 *					 mode work handling function.
 * @vcpu:	Pointer to current's VCPU data
 * @ti_work:	Cached TIF flags gathered in xfer_to_guest_mode_handle_work()
 *
 * Invoked from xfer_to_guest_mode_handle_work(). Defaults to NOOP. Can be
 * replaced by architecture specific code.
 */
static inline int arch_xfer_to_guest_mode_handle_work(unsigned long ti_work);

#ifndef arch_xfer_to_guest_mode_handle_work
static inline int arch_xfer_to_guest_mode_handle_work(unsigned long ti_work)
{
	return 0;
}
#endif

/**
 * xfer_to_guest_mode_handle_work - Check and handle pending work which needs
 *				    to be handled before going to guest mode
 *
 * Returns: 0 or an error code
 */
int xfer_to_guest_mode_handle_work(void);

/**
 * xfer_to_guest_mode_prepare - Perform last minute preparation work that
 *				need to be handled while IRQs are disabled
 *				upon entering to guest.
 *
 * Has to be invoked with interrupts disabled before the last call
 * to xfer_to_guest_mode_work_pending().
 */
static inline void xfer_to_guest_mode_prepare(void)
{
	lockdep_assert_irqs_disabled();
	tick_nohz_user_enter_prepare();
}

/**
 * __xfer_to_guest_mode_work_pending - Check if work is pending
 *
 * Returns: True if work pending, False otherwise.
 *
 * Bare variant of xfer_to_guest_mode_work_pending(). Can be called from
 * interrupt enabled code for racy quick checks with care.
 */
static inline bool __xfer_to_guest_mode_work_pending(void)
{
	unsigned long ti_work = read_thread_flags();

	return !!(ti_work & XFER_TO_GUEST_MODE_WORK);
}

/**
 * xfer_to_guest_mode_work_pending - Check if work is pending which needs to be
 *				     handled before returning to guest mode
 *
 * Returns: True if work pending, False otherwise.
 *
 * Has to be invoked with interrupts disabled before the transition to
 * guest mode.
 */
static inline bool xfer_to_guest_mode_work_pending(void)
{
	lockdep_assert_irqs_disabled();
	return __xfer_to_guest_mode_work_pending();
}
#endif /* CONFIG_VIRT_XFER_TO_GUEST_WORK */

#endif
