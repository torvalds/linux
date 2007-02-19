#ifndef __LINUX_KVM_PARA_H
#define __LINUX_KVM_PARA_H

/*
 * Guest OS interface for KVM paravirtualization
 *
 * Note: this interface is totally experimental, and is certain to change
 *       as we make progress.
 */

/*
 * Per-VCPU descriptor area shared between guest and host. Writable to
 * both guest and host. Registered with the host by the guest when
 * a guest acknowledges paravirtual mode.
 *
 * NOTE: all addresses are guest-physical addresses (gpa), to make it
 * easier for the hypervisor to map between the various addresses.
 */
struct kvm_vcpu_para_state {
	/*
	 * API version information for compatibility. If there's any support
	 * mismatch (too old host trying to execute too new guest) then
	 * the host will deny entry into paravirtual mode. Any other
	 * combination (new host + old guest and new host + new guest)
	 * is supposed to work - new host versions will support all old
	 * guest API versions.
	 */
	u32 guest_version;
	u32 host_version;
	u32 size;
	u32 ret;

	/*
	 * The address of the vm exit instruction (VMCALL or VMMCALL),
	 * which the host will patch according to the CPU model the
	 * VM runs on:
	 */
	u64 hypercall_gpa;

} __attribute__ ((aligned(PAGE_SIZE)));

#define KVM_PARA_API_VERSION 1

/*
 * This is used for an RDMSR's ECX parameter to probe for a KVM host.
 * Hopefully no CPU vendor will use up this number. This is placed well
 * out of way of the typical space occupied by CPU vendors' MSR indices,
 * and we think (or at least hope) it wont be occupied in the future
 * either.
 */
#define MSR_KVM_API_MAGIC 0x87655678

#define KVM_EINVAL 1

#endif
