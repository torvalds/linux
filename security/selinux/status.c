// SPDX-License-Identifier: GPL-2.0-only
/*
 * mmap based event notifications for SELinux
 *
 * Author: KaiGai Kohei <kaigai@ak.jp.nec.com>
 *
 * Copyright (C) 2010 NEC corporation
 */
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include "avc.h"
#include "security.h"

/*
 * The selinux_status_page shall be exposed to userspace applications
 * using mmap interface on /selinux/status.
 * It enables to notify applications a few events that will cause reset
 * of userspace access vector without context switching.
 *
 * The selinux_kernel_status structure on the head of status page is
 * protected from concurrent accesses using seqlock logic, so userspace
 * application should reference the status page according to the seqlock
 * logic.
 *
 * Typically, application checks status->sequence at the head of access
 * control routine. If it is odd-number, kernel is updating the status,
 * so please wait for a moment. If it is changed from the last sequence
 * number, it means something happen, so application will reset userspace
 * avc, if needed.
 * In most cases, application shall confirm the kernel status is not
 * changed without any system call invocations.
 */

/*
 * selinux_kernel_status_page
 *
 * It returns a reference to selinux_status_page. If the status page is
 * not allocated yet, it also tries to allocate it at the first time.
 */
struct page *selinux_kernel_status_page(void)
{
	struct selinux_kernel_status   *status;
	struct page		       *result = NULL;

	mutex_lock(&selinux_state.status_lock);
	if (!selinux_state.status_page) {
		selinux_state.status_page = alloc_page(GFP_KERNEL|__GFP_ZERO);

		if (selinux_state.status_page) {
			status = page_address(selinux_state.status_page);

			status->version = SELINUX_KERNEL_STATUS_VERSION;
			status->sequence = 0;
			status->enforcing = enforcing_enabled();
			/*
			 * NOTE: the next policyload event shall set
			 * a positive value on the status->policyload,
			 * although it may not be 1, but never zero.
			 * So, application can know it was updated.
			 */
			status->policyload = 0;
			status->deny_unknown =
				!security_get_allow_unknown();
		}
	}
	result = selinux_state.status_page;
	mutex_unlock(&selinux_state.status_lock);

	return result;
}

/*
 * selinux_status_update_setenforce
 *
 * It updates status of the current enforcing/permissive mode.
 */
void selinux_status_update_setenforce(bool enforcing)
{
	struct selinux_kernel_status   *status;

	mutex_lock(&selinux_state.status_lock);
	if (selinux_state.status_page) {
		status = page_address(selinux_state.status_page);

		status->sequence++;
		smp_wmb();

		status->enforcing = enforcing ? 1 : 0;

		smp_wmb();
		status->sequence++;
	}
	mutex_unlock(&selinux_state.status_lock);
}

/*
 * selinux_status_update_policyload
 *
 * It updates status of the times of policy reloaded, and current
 * setting of deny_unknown.
 */
void selinux_status_update_policyload(u32 seqno)
{
	struct selinux_kernel_status   *status;

	mutex_lock(&selinux_state.status_lock);
	if (selinux_state.status_page) {
		status = page_address(selinux_state.status_page);

		status->sequence++;
		smp_wmb();

		status->policyload = seqno;
		status->deny_unknown = !security_get_allow_unknown();

		smp_wmb();
		status->sequence++;
	}
	mutex_unlock(&selinux_state.status_lock);
}
