// SPDX-License-Identifier: GPL-2.0

#include <linux/entry-kvm.h>
#include <linux/kvm_host.h>

/**
 * xfer_to_guest_mode_work - Handle work for transitioning to guest mode
 * @vcpu: Pointer to the virtual CPU structure
 * @ti_work: Thread flags indicating the state of the vCPU
 *
 * This function processes various flags that indicate work to be done
 * when transitioning a virtual CPU to guest mode. It handles signals,
 * rescheduling, and architecture-specific work.
 *
 * Returns: 0 on success, or a negative error code on failure.
 */
static int xfer_to_guest_mode_work(struct kvm_vcpu *vcpu, unsigned long ti_work)
{
    do {
        int ret;

        // Check for pending signals and handle them
        if (ti_work & (_TIF_SIGPENDING | _TIF_NOTIFY_SIGNAL)) {
            kvm_handle_signal_exit(vcpu);
            return -EINTR; // Interrupted by a signal
        }

        // Check if rescheduling is needed
        if (ti_work & (_TIF_NEED_RESCHED | _TIF_NEED_RESCHED_LAZY))
            schedule();

        // Resume user mode work if notified
        if (ti_work & _TIF_NOTIFY_RESUME)
            resume_user_mode_work(NULL);

        // Handle architecture-specific work
        ret = arch_xfer_to_guest_mode_handle_work(vcpu, ti_work);
        if (ret)
            return ret; // Return on error

        // Read thread flags for the next iteration
        ti_work = read_thread_flags();
    } while (ti_work & XFER_TO_GUEST_MODE_WORK); // Continue if more work is pending

    return 0; // Success
}

/**
 * xfer_to_guest_mode_handle_work - Check and handle guest mode work
 * @vcpu: Pointer to the virtual CPU structure
 *
 * This function checks if there is any work pending for the vCPU
 * before invoking xfer_to_guest_mode_work. It ensures that the work
 * is only processed if the relevant flags are set.
 *
 * Returns: 0 if no work is pending, or the result of
 *          xfer_to_guest_mode_work on success.
 */
int xfer_to_guest_mode_handle_work(struct kvm_vcpu *vcpu)
{
    unsigned long ti_work;

    // Read thread flags to check for pending work
    ti_work = read_thread_flags();
    if (!(ti_work & XFER_TO_GUEST_MODE_WORK))
        return 0; // No work to do

    return xfer_to_guest_mode_work(vcpu, ti_work); // Delegate to work function
}
EXPORT_SYMBOL_GPL(xfer_to_guest_mode_handle_work);
