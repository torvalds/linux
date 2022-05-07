/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_ENTRYKVM_H
#define __LINUX_ENTRYKVM_H

#include <linux/entry-common.h>

/* Transfer to guest mode work */
#ifdef CONFIG_KVM_XFER_TO_GUEST_WORK

#ifndef ARCH_XFER_TO_GUEST_MODE_WORK
# define ARCH_XFER_TO_GUEST_MODE_WORK	(0)
#endif

#define XFER_TO_GUEST_MODE_WORK					\
	(_TIF_NEED_RESCHED | _TIF_SIGPENDING |			\
	 _TIF_NOTIFY_RESUME | ARCH_XFER_TO_GUEST_MODE_WORK)

struct kvm_vcpu;

/**
 * arch_xfer_to_guest_mode_handle_work - Architecture specific xfer to guest
 *					 mode work handling function.
 * @vcpu:	Pointer to current's VCPU data
 * @ti_work:	Cached TIF flags gathered in xfer_to_guest_mode_handle_work()
 *
 * Invoked from xfer_to_guest_mode_handle_work(). Defaults to NOOP. Can be
 * replaced by architecture specific code.
 */
static inline int arch_xfer_to_guest_mode_handle_work(struct kvm_vcpu *vcpu,
						      unsigned long ti_work);

#ifndef arch_xfer_to_guest_mode_work
static inline int arch_xfer_to_guest_mode_handle_work(struct kvm_vcpu *vcpu,
						      unsigned long ti_work)
{
	return 0;
}
#endif

/**
 * xfer_to_guest_mode_handle_work - Check and handle pending work which needs
 *				    to be handled before going to guest mode
 * @vcpu:	Pointer to current's VCPU data
 *
 * Returns: 0 or an error code
 */
int xfer_to_guest_mode_handle_work(struct kvm_vcpu *vcpu);

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
	unsigned long ti_work = READ_ONCE(current_thread_info()->flags);

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
#endif /* CONFIG_KVM_XFER_TO_GUEST_WORK */

#endif
