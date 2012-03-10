#ifndef _ASM_GENERIC_KVM_PARA_H
#define _ASM_GENERIC_KVM_PARA_H


/*
 * This function is used by architectures that support kvm to avoid issuing
 * false soft lockup messages.
 */
static inline bool kvm_check_and_clear_guest_paused(void)
{
	return false;
}

#endif
