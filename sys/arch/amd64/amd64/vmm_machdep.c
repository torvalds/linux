/* $OpenBSD: vmm_machdep.c,v 1.64 2025/09/14 15:52:28 mlarkin Exp $ */
/*
 * Copyright (c) 2014 Mike Larkin <mlarkin@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/pool.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/refcnt.h>
#include <sys/rwlock.h>
#include <sys/pledge.h>
#include <sys/memrange.h>
#include <sys/tracepoint.h>

#include <uvm/uvm_extern.h>

#include <machine/fpu.h>
#include <machine/pmap.h>
#include <machine/biosvar.h>
#include <machine/segments.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/ghcb.h>
#include <machine/vmmvar.h>

#include <dev/isa/isareg.h>
#include <dev/pv/pvreg.h>

#include <dev/vmm/vmm.h>

#ifdef MP_LOCKDEBUG
#include <ddb/db_output.h>
#endif /* MP_LOCKDEBUG */

void *l1tf_flush_region;

#define DEVNAME(s)  ((s)->sc_dev.dv_xname)

#define CTRL_DUMP(x,y,z) printf("     %s: Can set:%s Can clear:%s\n", #z , \
				vcpu_vmx_check_cap(x, IA32_VMX_##y ##_CTLS, \
				IA32_VMX_##z, 1) ? "Yes" : "No", \
				vcpu_vmx_check_cap(x, IA32_VMX_##y ##_CTLS, \
				IA32_VMX_##z, 0) ? "Yes" : "No");

#define VMX_EXIT_INFO_HAVE_RIP		0x1
#define VMX_EXIT_INFO_HAVE_REASON	0x2
#define VMX_EXIT_INFO_COMPLETE				\
    (VMX_EXIT_INFO_HAVE_RIP | VMX_EXIT_INFO_HAVE_REASON)

void vmx_dump_vmcs_field(uint16_t, const char *);
int vmm_enabled(void);
void vmm_activate_machdep(struct device *, int);
int vmmioctl_machdep(dev_t, u_long, caddr_t, int, struct proc *);
int vmm_quiesce_vmx(void);
int vm_run(struct vm_run_params *);
int vm_intr_pending(struct vm_intr_params *);
int vm_rwregs(struct vm_rwregs_params *, int);
int vm_rwvmparams(struct vm_rwvmparams_params *, int);
int vcpu_readregs_vmx(struct vcpu *, uint64_t, int, struct vcpu_reg_state *);
int vcpu_readregs_svm(struct vcpu *, uint64_t, struct vcpu_reg_state *);
int vcpu_writeregs_vmx(struct vcpu *, uint64_t, int, struct vcpu_reg_state *);
int vcpu_writeregs_svm(struct vcpu *, uint64_t, struct vcpu_reg_state *);
int vcpu_reset_regs(struct vcpu *, struct vcpu_reg_state *);
int vcpu_reset_regs_vmx(struct vcpu *, struct vcpu_reg_state *);
int vcpu_reset_regs_svm(struct vcpu *, struct vcpu_reg_state *);
int vcpu_svm_init_vmsa(struct vcpu *, struct vcpu_reg_state *);
int vcpu_reload_vmcs_vmx(struct vcpu *);
int vcpu_init(struct vcpu *, struct vm_create_params *);
int vcpu_init_vmx(struct vcpu *);
int vcpu_init_svm(struct vcpu *, struct vm_create_params *);
int vcpu_run_vmx(struct vcpu *, struct vm_run_params *);
int vcpu_run_svm(struct vcpu *, struct vm_run_params *);
void vcpu_deinit(struct vcpu *);
void vcpu_deinit_svm(struct vcpu *);
void vcpu_deinit_vmx(struct vcpu *);
int vcpu_vmx_check_cap(struct vcpu *, uint32_t, uint32_t, int);
int vcpu_vmx_compute_ctrl(uint64_t, uint16_t, uint32_t, uint32_t, uint32_t *);
int vmx_get_exit_info(uint64_t *, uint64_t *);
int vmx_load_pdptes(struct vcpu *);
int vmx_handle_exit(struct vcpu *);
int svm_handle_exit(struct vcpu *);
int svm_vmgexit_sync_host(struct vcpu *);
int svm_vmgexit_sync_guest(struct vcpu *);
int svm_handle_vmgexit(struct vcpu *);
int svm_handle_efercr(struct vcpu *, uint64_t);
int svm_get_iflag(struct vcpu *, uint64_t);
int svm_handle_msr(struct vcpu *);
int vmm_handle_xsetbv(struct vcpu *, uint64_t *);
int vmx_handle_xsetbv(struct vcpu *);
int svm_handle_xsetbv(struct vcpu *);
int vmm_handle_cpuid(struct vcpu *);
int vmx_handle_rdmsr(struct vcpu *);
int vmx_handle_wrmsr(struct vcpu *);
int vmx_handle_cr0_write(struct vcpu *, uint64_t);
int vmx_handle_cr4_write(struct vcpu *, uint64_t);
int vmx_handle_cr(struct vcpu *);
int svm_handle_inout(struct vcpu *);
int vmx_handle_inout(struct vcpu *);
int svm_handle_hlt(struct vcpu *);
int vmx_handle_hlt(struct vcpu *);
int vmm_inject_ud(struct vcpu *);
int vmm_inject_gp(struct vcpu *);
int vmm_inject_db(struct vcpu *);
void vmx_handle_intr(struct vcpu *);
void vmx_handle_misc_enable_msr(struct vcpu *);
int vmm_get_guest_memtype(struct vm *, paddr_t);
vaddr_t vmm_translate_gpa(struct vm *, paddr_t);
int vmx_get_guest_faulttype(void);
int svm_get_guest_faulttype(struct vmcb *);
int vmx_get_exit_qualification(uint64_t *);
int vmm_get_guest_cpu_cpl(struct vcpu *);
int vmm_get_guest_cpu_mode(struct vcpu *);
int svm_fault_page(struct vcpu *, paddr_t);
int vmx_fault_page(struct vcpu *, paddr_t);
int vmx_handle_np_fault(struct vcpu *);
int svm_handle_np_fault(struct vcpu *);
int vmm_alloc_vpid_vcpu(uint16_t *, struct vcpu *);
int vmm_alloc_vpid(uint16_t *);
int vmm_alloc_asid(uint16_t *, struct vcpu *);
void vmm_free_vpid(uint16_t);
const char *vcpu_state_decode(u_int);
const char *vmx_exit_reason_decode(uint32_t);
const char *svm_exit_reason_decode(uint32_t);
const char *vmx_instruction_error_decode(uint32_t);
void svm_setmsrbr(struct vcpu *, uint32_t);
void svm_setmsrbw(struct vcpu *, uint32_t);
void svm_setmsrbrw(struct vcpu *, uint32_t);
void vmx_setmsrbr(struct vcpu *, uint32_t);
void vmx_setmsrbw(struct vcpu *, uint32_t);
void vmx_setmsrbrw(struct vcpu *, uint32_t);
void svm_set_clean(struct vcpu *, uint32_t);
void svm_set_dirty(struct vcpu *, uint32_t);
int svm_get_vmsa_pa(uint32_t, uint32_t, uint64_t *);

int vmm_gpa_is_valid(struct vcpu *vcpu, paddr_t gpa, size_t obj_size);
void vmm_init_pvclock(struct vcpu *, paddr_t);
int vmm_update_pvclock(struct vcpu *);
void vmm_pv_wall_clock(struct vcpu *, paddr_t);
int vmm_pat_is_valid(uint64_t);

#ifdef MULTIPROCESSOR
static int vmx_remote_vmclear(struct cpu_info*, struct vcpu *);
#endif

#ifdef VMM_DEBUG
void vmx_vcpu_dump_regs(struct vcpu *);
void vmx_dump_vmcs(struct vcpu *);
const char *msr_name_decode(uint32_t);
void vmm_segment_desc_decode(uint64_t);
void vmm_decode_cr0(uint64_t);
void vmm_decode_cr3(uint64_t);
void vmm_decode_cr4(uint64_t);
void vmm_decode_msr_value(uint64_t, uint64_t);
void vmm_decode_apicbase_msr_value(uint64_t);
void vmm_decode_ia32_fc_value(uint64_t);
void vmm_decode_mtrrcap_value(uint64_t);
void vmm_decode_perf_status_value(uint64_t);
void vmm_decode_perf_ctl_value(uint64_t);
void vmm_decode_mtrrdeftype_value(uint64_t);
void vmm_decode_efer_value(uint64_t);
void vmm_decode_rflags(uint64_t);
void vmm_decode_misc_enable_value(uint64_t);
const char *vmm_decode_cpu_mode(struct vcpu *);

extern int mtrr2mrt(int);

struct vmm_reg_debug_info {
	uint64_t	vrdi_bit;
	const char	*vrdi_present;
	const char	*vrdi_absent;
};
#endif /* VMM_DEBUG */

extern uint64_t tsc_frequency;
extern int tsc_is_invariant;

const char *vmm_hv_signature = VMM_HV_SIGNATURE;
const char *kvm_hv_signature = "KVMKVMKVM\0\0\0";

const struct kmem_pa_mode vmm_kp_contig = {
	.kp_constraint = &no_constraint,
	.kp_maxseg = 1,
	.kp_align = 4096,
	.kp_zero = 1,
};

/*
 * Helper struct to easily get the VMCS field IDs needed in vmread/vmwrite
 * to access the individual fields of the guest segment registers. This
 * struct is indexed by VCPU_REGS_* id.
 */
const struct {
	uint64_t selid;
	uint64_t limitid;
	uint64_t arid;
	uint64_t baseid;
} vmm_vmx_sreg_vmcs_fields[] = {
	{ VMCS_GUEST_IA32_ES_SEL, VMCS_GUEST_IA32_ES_LIMIT,
	  VMCS_GUEST_IA32_ES_AR, VMCS_GUEST_IA32_ES_BASE },
	{ VMCS_GUEST_IA32_CS_SEL, VMCS_GUEST_IA32_CS_LIMIT,
	  VMCS_GUEST_IA32_CS_AR, VMCS_GUEST_IA32_CS_BASE },
	{ VMCS_GUEST_IA32_SS_SEL, VMCS_GUEST_IA32_SS_LIMIT,
	  VMCS_GUEST_IA32_SS_AR, VMCS_GUEST_IA32_SS_BASE },
	{ VMCS_GUEST_IA32_DS_SEL, VMCS_GUEST_IA32_DS_LIMIT,
	  VMCS_GUEST_IA32_DS_AR, VMCS_GUEST_IA32_DS_BASE },
	{ VMCS_GUEST_IA32_FS_SEL, VMCS_GUEST_IA32_FS_LIMIT,
	  VMCS_GUEST_IA32_FS_AR, VMCS_GUEST_IA32_FS_BASE },
	{ VMCS_GUEST_IA32_GS_SEL, VMCS_GUEST_IA32_GS_LIMIT,
	  VMCS_GUEST_IA32_GS_AR, VMCS_GUEST_IA32_GS_BASE },
	{ VMCS_GUEST_IA32_LDTR_SEL, VMCS_GUEST_IA32_LDTR_LIMIT,
	  VMCS_GUEST_IA32_LDTR_AR, VMCS_GUEST_IA32_LDTR_BASE },
	{ VMCS_GUEST_IA32_TR_SEL, VMCS_GUEST_IA32_TR_LIMIT,
	  VMCS_GUEST_IA32_TR_AR, VMCS_GUEST_IA32_TR_BASE }
};

/* IDT information used when populating host state area */
extern vaddr_t idt_vaddr;
extern struct gate_descriptor *idt;

/* Constants used in "CR access exit" */
#define CR_WRITE	0
#define CR_READ		1
#define CR_CLTS		2
#define CR_LMSW		3

/*
 * vmm_enabled
 *
 * Checks if we have at least one CPU with either VMX or SVM.
 * Returns 1 if we have at least one of either type, but not both, 0 otherwise.
 */
int
vmm_enabled(void)
{
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;
	int found_ept = 0, found_svm = 0;

	/* Check if we have at least one CPU with either VMX/EPT or SVM */
	CPU_INFO_FOREACH(cii, ci) {
		if (ci->ci_vmm_flags & CI_VMM_EPT)
			found_ept = 1;
		if (ci->ci_vmm_flags & CI_VMM_SVM)
			found_svm = 1;
	}

	/* Don't support both SVM and VMX/EPT at the same time */
	if (found_ept && found_svm)
		return (0);

	if (found_ept || found_svm)
		return 1;

	return 0;
}

int
vmm_probe_machdep(struct device *parent, void *match, void *aux)
{
	return vmm_enabled();
}

void
vmm_attach_machdep(struct device *parent, struct device *self, void *aux)
{
	struct vmm_softc *sc = (struct vmm_softc *)self;
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;

	sc->sc_md.nr_rvi_cpus = 0;
	sc->sc_md.nr_ept_cpus = 0;

	/* Calculate CPU features */
	CPU_INFO_FOREACH(cii, ci) {
		if (ci->ci_vmm_flags & CI_VMM_RVI)
			sc->sc_md.nr_rvi_cpus++;
		if (ci->ci_vmm_flags & CI_VMM_EPT)
			sc->sc_md.nr_ept_cpus++;
	}

	sc->sc_md.pkru_enabled = 0;
	if (rcr4() & CR4_PKE)
		sc->sc_md.pkru_enabled = 1;

	if (sc->sc_md.nr_ept_cpus) {
		printf(": VMX/EPT");
		sc->mode = VMM_MODE_EPT;
	} else if (sc->sc_md.nr_rvi_cpus) {
		printf(": SVM/RVI");
		sc->mode = VMM_MODE_RVI;
	} else {
		printf(": unknown");
		sc->mode = VMM_MODE_UNKNOWN;
	}

	if (sc->mode == VMM_MODE_EPT) {
		if (!(curcpu()->ci_vmm_cap.vcc_vmx.vmx_has_l1_flush_msr)) {
			l1tf_flush_region = km_alloc(VMX_L1D_FLUSH_SIZE,
			    &kv_any, &vmm_kp_contig, &kd_waitok);
			if (!l1tf_flush_region) {
				printf(" (failing, no memory)");
				sc->mode = VMM_MODE_UNKNOWN;
			} else {
				printf(" (using slow L1TF mitigation)");
				memset(l1tf_flush_region, 0xcc,
				    VMX_L1D_FLUSH_SIZE);
			}
		}
	}

	if (sc->mode == VMM_MODE_RVI) {
		sc->max_vpid = curcpu()->ci_vmm_cap.vcc_svm.svm_max_asid;
	} else {
		sc->max_vpid = 0xFFF;
	}

	bzero(&sc->vpids, sizeof(sc->vpids));
	rw_init(&sc->vpid_lock, "vpid");
}

/*
 * vmm_quiesce_vmx
 *
 * Prepare the host for suspend by flushing all VMCS states.
 */
int
vmm_quiesce_vmx(void)
{
	struct vm		*vm;
	struct vcpu		*vcpu;
	int			 err;

	/*
	 * We should be only called from a quiescing device state so we
	 * don't expect to sleep here. If we can't get all our locks,
	 * something is wrong.
	 */
	if ((err = rw_enter(&vmm_softc->vm_lock, RW_WRITE | RW_NOSLEEP)))
		return (err);

	/* Iterate over each vm... */
	SLIST_FOREACH(vm, &vmm_softc->vm_list, vm_link) {
		/* Iterate over each vcpu... */
		SLIST_FOREACH(vcpu, &vm->vm_vcpu_list, vc_vcpu_link) {
			err = rw_enter(&vcpu->vc_lock, RW_WRITE | RW_NOSLEEP);
			if (err)
				break;

			/* We can skip unlaunched VMCS. Nothing to flush. */
			if (atomic_load_int(&vcpu->vc_vmx_vmcs_state)
			    != VMCS_LAUNCHED) {
				DPRINTF("%s: skipping vcpu %d for vm %d\n",
				    __func__, vcpu->vc_id, vm->vm_id);
				rw_exit_write(&vcpu->vc_lock);
				continue;
			}

#ifdef MULTIPROCESSOR
			if (vcpu->vc_last_pcpu != curcpu()) {
				/* Remote cpu vmclear via ipi. */
				err = vmx_remote_vmclear(vcpu->vc_last_pcpu,
				    vcpu);
				if (err)
					printf("%s: failed to remote vmclear "
					    "vcpu %d of vm %d\n", __func__,
					    vcpu->vc_id, vm->vm_id);
			} else
#endif
			{
				/* Local cpu vmclear instruction. */
				if ((err = vmclear(&vcpu->vc_control_pa)))
					printf("%s: failed to locally vmclear "
					    "vcpu %d of vm %d\n", __func__,
					    vcpu->vc_id, vm->vm_id);
				atomic_swap_uint(&vcpu->vc_vmx_vmcs_state,
				    VMCS_CLEARED);
			}

			rw_exit_write(&vcpu->vc_lock);
			if (err)
				break;
			DPRINTF("%s: cleared vcpu %d for vm %d\n", __func__,
			    vcpu->vc_id, vm->vm_id);
		}
		if (err)
			break;
	}
	rw_exit_write(&vmm_softc->vm_lock);

	if (err)
		return (err);
	return (0);
}

void
vmm_activate_machdep(struct device *self, int act)
{
	struct cpu_info		*ci = curcpu();

	switch (act) {
	case DVACT_QUIESCE:
		/* If we're not in vmm mode, nothing to do. */
		if ((ci->ci_flags & CPUF_VMM) == 0)
			break;

		/* Intel systems need extra steps to sync vcpu state. */
		if (vmm_softc->mode == VMM_MODE_EPT)
			if (vmm_quiesce_vmx())
				DPRINTF("%s: vmx quiesce failed\n", __func__);

		/* Stop virtualization mode on all cpus. */
		vmm_stop();
		break;

	case DVACT_WAKEUP:
		/* Restart virtualization mode on all cpu's. */
		if (vmm_softc->vm_ct > 0)
			vmm_start();
		break;
	}
}

int
vmmioctl_machdep(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	int ret;

	switch (cmd) {
	case VMM_IOC_INTR:
		ret = vm_intr_pending((struct vm_intr_params *)data);
		break;
	default:
		DPRINTF("%s: unknown ioctl code 0x%lx\n", __func__, cmd);
		ret = ENOTTY;
	}

	return (ret);
}

int
pledge_ioctl_vmm_machdep(struct proc *p, long com)
{
	switch (com) {
	case VMM_IOC_INTR:
		return (0);
	}

	return (EPERM);
}

/*
 * vm_intr_pending
 *
 * IOCTL handler routine for VMM_IOC_INTR messages, sent from vmd when an
 * interrupt is pending and needs acknowledgment
 *
 * Parameters:
 *  vip: Describes the vm/vcpu for which the interrupt is pending
 *
 * Return values:
 *  0: if successful
 *  ENOENT: if the VM/VCPU defined by 'vip' cannot be found
 */
int
vm_intr_pending(struct vm_intr_params *vip)
{
	struct vm *vm;
	struct vcpu *vcpu;
#ifdef MULTIPROCESSOR
	struct cpu_info *ci;
#endif
	int error, ret = 0;

	/* Find the desired VM */
	error = vm_find(vip->vip_vm_id, &vm);

	/* Not found? exit. */
	if (error != 0)
		return (error);

	vcpu = vm_find_vcpu(vm, vip->vip_vcpu_id);

	if (vcpu == NULL) {
		ret = ENOENT;
		goto out;
	}

	vcpu->vc_intr = vip->vip_intr;
#ifdef MULTIPROCESSOR
	ci = READ_ONCE(vcpu->vc_curcpu);
	if (ci != NULL)
		x86_send_ipi(ci, X86_IPI_NOP);
#endif

out:
	refcnt_rele_wake(&vm->vm_refcnt);
	return (ret);
}

/*
 * vm_rwvmparams
 *
 * IOCTL handler to read/write the current vmm params like pvclock gpa, pvclock
 * version, etc.
 *
 * Parameters:
 *   vrwp: Describes the VM and VCPU to get/set the params from
 *   dir: 0 for reading, 1 for writing
 *
 * Return values:
 *  0: if successful
 *  ENOENT: if the VM/VCPU defined by 'vpp' cannot be found
 *  EINVAL: if an error occurred reading the registers of the guest
 */
int
vm_rwvmparams(struct vm_rwvmparams_params *vpp, int dir)
{
	struct vm *vm;
	struct vcpu *vcpu;
	int error, ret = 0;

	/* Find the desired VM */
	error = vm_find(vpp->vpp_vm_id, &vm);

	/* Not found? exit. */
	if (error != 0)
		return (error);

	vcpu = vm_find_vcpu(vm, vpp->vpp_vcpu_id);

	if (vcpu == NULL) {
		ret = ENOENT;
		goto out;
	}

	if (dir == 0) {
		if (vpp->vpp_mask & VM_RWVMPARAMS_PVCLOCK_VERSION)
			vpp->vpp_pvclock_version = vcpu->vc_pvclock_version;
		if (vpp->vpp_mask & VM_RWVMPARAMS_PVCLOCK_SYSTEM_GPA)
			vpp->vpp_pvclock_system_gpa = \
			    vcpu->vc_pvclock_system_gpa;
	} else {
		if (vpp->vpp_mask & VM_RWVMPARAMS_PVCLOCK_VERSION)
			vcpu->vc_pvclock_version = vpp->vpp_pvclock_version;
		if (vpp->vpp_mask & VM_RWVMPARAMS_PVCLOCK_SYSTEM_GPA) {
			vmm_init_pvclock(vcpu, vpp->vpp_pvclock_system_gpa);
		}
	}
out:
	refcnt_rele_wake(&vm->vm_refcnt);
	return (ret);
}

/*
 * vm_readregs
 *
 * IOCTL handler to read/write the current register values of a guest VCPU.
 * The VCPU must not be running.
 *
 * Parameters:
 *   vrwp: Describes the VM and VCPU to get/set the registers from. The
 *    register values are returned here as well.
 *   dir: 0 for reading, 1 for writing
 *
 * Return values:
 *  0: if successful
 *  ENOENT: if the VM/VCPU defined by 'vrwp' cannot be found
 *  EINVAL: if an error occurred accessing the registers of the guest
 *  EPERM: if the vm cannot be accessed from the calling process
 */
int
vm_rwregs(struct vm_rwregs_params *vrwp, int dir)
{
	struct vm *vm;
	struct vcpu *vcpu;
	struct vcpu_reg_state *vrs = &vrwp->vrwp_regs;
	int error, ret = 0;

	/* Find the desired VM */
	error = vm_find(vrwp->vrwp_vm_id, &vm);

	/* Not found? exit. */
	if (error != 0)
		return (error);

	vcpu = vm_find_vcpu(vm, vrwp->vrwp_vcpu_id);

	if (vcpu == NULL) {
		ret = ENOENT;
		goto out;
	}

	rw_enter_write(&vcpu->vc_lock);
	if (vmm_softc->mode == VMM_MODE_EPT)
		ret = (dir == 0) ?
		    vcpu_readregs_vmx(vcpu, vrwp->vrwp_mask, 1, vrs) :
		    vcpu_writeregs_vmx(vcpu, vrwp->vrwp_mask, 1, vrs);
	else if (vmm_softc->mode == VMM_MODE_RVI)
		ret = (dir == 0) ?
		    vcpu_readregs_svm(vcpu, vrwp->vrwp_mask, vrs) :
		    vcpu_writeregs_svm(vcpu, vrwp->vrwp_mask, vrs);
	else {
		DPRINTF("%s: unknown vmm mode", __func__);
		ret = EINVAL;
	}
	rw_exit_write(&vcpu->vc_lock);
out:
	refcnt_rele_wake(&vm->vm_refcnt);
	return (ret);
}

/*
 * vmm_start
 *
 * Starts VMM mode on the system
 */
int
vmm_start(void)
{
	int rv = 0;
	struct cpu_info *self = curcpu();
#ifdef MULTIPROCESSOR
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;
#ifdef MP_LOCKDEBUG
	long nticks;
#endif /* MP_LOCKDEBUG */
#endif /* MULTIPROCESSOR */

	rw_enter_write(&vmm_softc->sc_slock);

	/* VMM is already running */
	if (self->ci_flags & CPUF_VMM)
		goto unlock;

	/* Start VMM on this CPU */
	start_vmm_on_cpu(self);
	if (!(self->ci_flags & CPUF_VMM)) {
		printf("%s: failed to enter VMM mode\n",
			self->ci_dev->dv_xname);
		rv = EIO;
		goto unlock;
	}

#ifdef MULTIPROCESSOR
	/* Broadcast start VMM IPI */
	x86_broadcast_ipi(X86_IPI_START_VMM);

	CPU_INFO_FOREACH(cii, ci) {
		if (ci == self)
			continue;
#ifdef MP_LOCKDEBUG
		nticks = __mp_lock_spinout;
#endif /* MP_LOCKDEBUG */
		while (!(ci->ci_flags & CPUF_VMM)) {
			CPU_BUSY_CYCLE();
#ifdef MP_LOCKDEBUG
			if (--nticks <= 0) {
				db_printf("%s: spun out", __func__);
				db_enter();
				nticks = __mp_lock_spinout;
			}
#endif /* MP_LOCKDEBUG */
		}
	}
#endif /* MULTIPROCESSOR */
unlock:
	rw_exit_write(&vmm_softc->sc_slock);
	return (rv);
}

/*
 * vmm_stop
 *
 * Stops VMM mode on the system
 */
int
vmm_stop(void)
{
	int rv = 0;
	struct cpu_info *self = curcpu();
#ifdef MULTIPROCESSOR
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;
#ifdef MP_LOCKDEBUG
	long nticks;
#endif /* MP_LOCKDEBUG */
#endif /* MULTIPROCESSOR */

	rw_enter_write(&vmm_softc->sc_slock);

	/* VMM is not running */
	if (!(self->ci_flags & CPUF_VMM))
		goto unlock;

	/* Stop VMM on this CPU */
	stop_vmm_on_cpu(self);
	if (self->ci_flags & CPUF_VMM) {
		printf("%s: failed to exit VMM mode\n",
			self->ci_dev->dv_xname);
		rv = EIO;
		goto unlock;
	}

#ifdef MULTIPROCESSOR
	/* Stop VMM on other CPUs */
	x86_broadcast_ipi(X86_IPI_STOP_VMM);

	CPU_INFO_FOREACH(cii, ci) {
		if (ci == self)
			continue;
#ifdef MP_LOCKDEBUG
		nticks = __mp_lock_spinout;
#endif /* MP_LOCKDEBUG */
		while ((ci->ci_flags & CPUF_VMM)) {
			CPU_BUSY_CYCLE();
#ifdef MP_LOCKDEBUG
			if (--nticks <= 0) {
				db_printf("%s: spunout", __func__);
				db_enter();
				nticks = __mp_lock_spinout;
			}
#endif /* MP_LOCKDEBUG */
		}
	}
#endif /* MULTIPROCESSOR */
unlock:
	rw_exit_write(&vmm_softc->sc_slock);
	return (0);
}

/*
 * start_vmm_on_cpu
 *
 * Starts VMM mode on 'ci' by executing the appropriate CPU-specific insn
 * sequence to enter VMM mode (eg, VMXON)
 */
void
start_vmm_on_cpu(struct cpu_info *ci)
{
	uint64_t msr;
	uint32_t cr4;
	struct vmx_invept_descriptor vid;

	/* No VMM mode? exit. */
	if ((ci->ci_vmm_flags & CI_VMM_VMX) == 0 &&
	    (ci->ci_vmm_flags & CI_VMM_SVM) == 0)
		return;

	/*
	 * AMD SVM
	 */
	if (ci->ci_vmm_flags & CI_VMM_SVM) {
		msr = rdmsr(MSR_EFER);
		msr |= EFER_SVME;
		wrmsr(MSR_EFER, msr);
	}

	/*
	 * Intel VMX
	 */
	if (ci->ci_vmm_flags & CI_VMM_VMX) {
		if (ci->ci_vmxon_region == 0)
			return;
		else {
			bzero(ci->ci_vmxon_region, PAGE_SIZE);
			ci->ci_vmxon_region->vr_revision =
			    ci->ci_vmm_cap.vcc_vmx.vmx_vmxon_revision;

			/* Enable VMX */
			msr = rdmsr(MSR_IA32_FEATURE_CONTROL);
			if (msr & IA32_FEATURE_CONTROL_LOCK) {
				if (!(msr & IA32_FEATURE_CONTROL_VMX_EN))
					return;
			} else {
				msr |= IA32_FEATURE_CONTROL_VMX_EN |
				    IA32_FEATURE_CONTROL_LOCK;
				wrmsr(MSR_IA32_FEATURE_CONTROL, msr);
			}

			/* Set CR4.VMXE */
			cr4 = rcr4();
			cr4 |= CR4_VMXE;
			lcr4(cr4);

			/* Enter VMX mode and clear EPTs on this cpu */
			if (vmxon((uint64_t *)&ci->ci_vmxon_region_pa))
				panic("vmxon failed");

			memset(&vid, 0, sizeof(vid));
			if (invept(IA32_VMX_INVEPT_GLOBAL_CTX, &vid))
				panic("invept failed");
		}
	}

	atomic_setbits_int(&ci->ci_flags, CPUF_VMM);
}

/*
 * stop_vmm_on_cpu
 *
 * Stops VMM mode on 'ci' by executing the appropriate CPU-specific insn
 * sequence to exit VMM mode (eg, VMXOFF)
 */
void
stop_vmm_on_cpu(struct cpu_info *ci)
{
	uint64_t msr;
	uint32_t cr4;

	if (!(ci->ci_flags & CPUF_VMM))
		return;

	/*
	 * AMD SVM
	 */
	if (ci->ci_vmm_flags & CI_VMM_SVM) {
		msr = rdmsr(MSR_EFER);
		msr &= ~EFER_SVME;
		wrmsr(MSR_EFER, msr);
	}

	/*
	 * Intel VMX
	 */
	if (ci->ci_vmm_flags & CI_VMM_VMX) {
		if (vmxoff())
			panic("VMXOFF failed");

		cr4 = rcr4();
		cr4 &= ~CR4_VMXE;
		lcr4(cr4);
	}

	atomic_clearbits_int(&ci->ci_flags, CPUF_VMM);
}

/*
 * vmclear_on_cpu
 *
 * Flush and clear VMCS on 'ci' by executing vmclear.
 *
 */
void
vmclear_on_cpu(struct cpu_info *ci)
{
	if ((ci->ci_flags & CPUF_VMM) && (ci->ci_vmm_flags & CI_VMM_VMX)) {
		if (vmclear(&ci->ci_vmcs_pa))
			panic("VMCLEAR ipi failed");
		atomic_swap_ulong(&ci->ci_vmcs_pa, VMX_VMCS_PA_CLEAR);
	}
}

#ifdef MULTIPROCESSOR
static int
vmx_remote_vmclear(struct cpu_info *ci, struct vcpu *vcpu)
{
#ifdef MP_LOCKDEBUG
	long nticks = __mp_lock_spinout;
#endif /* MP_LOCKDEBUG */

	rw_enter_write(&ci->ci_vmcs_lock);
	atomic_swap_ulong(&ci->ci_vmcs_pa, vcpu->vc_control_pa);
	x86_send_ipi(ci, X86_IPI_VMCLEAR_VMM);

	while (ci->ci_vmcs_pa != VMX_VMCS_PA_CLEAR) {
		CPU_BUSY_CYCLE();
#ifdef MP_LOCKDEBUG
		if (--nticks <= 0) {
			db_printf("%s: spun out\n", __func__);
			db_enter();
			nticks = __mp_lock_spinout;
		}
#endif /* MP_LOCKDEBUG */
	}
	atomic_swap_uint(&vcpu->vc_vmx_vmcs_state, VMCS_CLEARED);
	rw_exit_write(&ci->ci_vmcs_lock);

	return (0);
}
#endif /* MULTIPROCESSOR */

/*
 * vm_impl_init
 *
 * VM address space initialization routine
 *
 * Parameters:
 *  vm: the VM being initialized
 *   p: vmd process owning the VM
 *
 * Return values:
 *  0: the initialization was successful
 *  EINVAL: unsupported vmm mode
 *  ENOMEM: the initialization failed (lack of resources)
 */
int
vm_impl_init(struct vm *vm, struct proc *p)
{
	/* If not EPT or RVI, nothing to do here */
	switch (vmm_softc->mode) {
	case VMM_MODE_EPT:
		pmap_convert(vm->vm_pmap, PMAP_TYPE_EPT);
		break;
	case VMM_MODE_RVI:
		pmap_convert(vm->vm_pmap, PMAP_TYPE_RVI);
		break;
	default:
		printf("%s: invalid vmm mode %d\n", __func__, vmm_softc->mode);
		return (EINVAL);
	}

	return (0);
}

void
vm_impl_deinit(struct vm *vm)
{
	/* unused */
}

/*
 * vcpu_reload_vmcs_vmx
 *
 * (Re)load the VMCS on the current cpu. Must be called with the VMCS write
 * lock acquired. If the VMCS is determined to be loaded on a remote cpu, an
 * ipi will be used to remotely flush it before loading the VMCS locally.
 *
 * Parameters:
 *  vcpu: Pointer to the vcpu needing its VMCS
 *
 * Return values:
 *  0: if successful
 *  EINVAL: an error occurred during flush or reload
 */
int
vcpu_reload_vmcs_vmx(struct vcpu *vcpu)
{
	struct cpu_info *ci, *last_ci;

	rw_assert_wrlock(&vcpu->vc_lock);

	ci = curcpu();
	last_ci = vcpu->vc_last_pcpu;

	if (last_ci == NULL) {
		/* First launch */
		if (vmclear(&vcpu->vc_control_pa))
				return (EINVAL);
		atomic_swap_uint(&vcpu->vc_vmx_vmcs_state, VMCS_CLEARED);
#ifdef MULTIPROCESSOR
	} else if (last_ci != ci) {
		/* We've moved CPUs at some point, so remote VMCLEAR */
		if (vmx_remote_vmclear(last_ci, vcpu))
			return (EINVAL);
		KASSERT(vcpu->vc_vmx_vmcs_state == VMCS_CLEARED);
#endif /* MULTIPROCESSOR */
	}

	if (vmptrld(&vcpu->vc_control_pa)) {
		printf("%s: vmptrld\n", __func__);
		return (EINVAL);
	}

	return (0);
}

/*
 * vcpu_readregs_vmx
 *
 * Reads 'vcpu's registers
 *
 * Parameters:
 *  vcpu: the vcpu to read register values from
 *  regmask: the types of registers to read
 *  loadvmcs: bit to indicate whether the VMCS has to be loaded first
 *  vrs: output parameter where register values are stored
 *
 * Return values:
 *  0: if successful
 *  EINVAL: an error reading registers occurred
 */
int
vcpu_readregs_vmx(struct vcpu *vcpu, uint64_t regmask, int loadvmcs,
    struct vcpu_reg_state *vrs)
{
	int i, ret = 0;
	uint64_t sel, limit, ar;
	uint64_t *gprs = vrs->vrs_gprs;
	uint64_t *crs = vrs->vrs_crs;
	uint64_t *msrs = vrs->vrs_msrs;
	uint64_t *drs = vrs->vrs_drs;
	struct vcpu_segment_info *sregs = vrs->vrs_sregs;
	struct vmx_msr_store *msr_store;

	if (loadvmcs) {
		if (vcpu_reload_vmcs_vmx(vcpu))
			return (EINVAL);
	}

#ifdef VMM_DEBUG
	/* VMCS should be loaded... */
	paddr_t pa = 0ULL;
	if (vmptrst(&pa))
		panic("%s: vmptrst", __func__);
	KASSERT(pa == vcpu->vc_control_pa);
#endif /* VMM_DEBUG */

	if (regmask & VM_RWREGS_GPRS) {
		gprs[VCPU_REGS_RAX] = vcpu->vc_gueststate.vg_rax;
		gprs[VCPU_REGS_RBX] = vcpu->vc_gueststate.vg_rbx;
		gprs[VCPU_REGS_RCX] = vcpu->vc_gueststate.vg_rcx;
		gprs[VCPU_REGS_RDX] = vcpu->vc_gueststate.vg_rdx;
		gprs[VCPU_REGS_RSI] = vcpu->vc_gueststate.vg_rsi;
		gprs[VCPU_REGS_RDI] = vcpu->vc_gueststate.vg_rdi;
		gprs[VCPU_REGS_R8] = vcpu->vc_gueststate.vg_r8;
		gprs[VCPU_REGS_R9] = vcpu->vc_gueststate.vg_r9;
		gprs[VCPU_REGS_R10] = vcpu->vc_gueststate.vg_r10;
		gprs[VCPU_REGS_R11] = vcpu->vc_gueststate.vg_r11;
		gprs[VCPU_REGS_R12] = vcpu->vc_gueststate.vg_r12;
		gprs[VCPU_REGS_R13] = vcpu->vc_gueststate.vg_r13;
		gprs[VCPU_REGS_R14] = vcpu->vc_gueststate.vg_r14;
		gprs[VCPU_REGS_R15] = vcpu->vc_gueststate.vg_r15;
		gprs[VCPU_REGS_RBP] = vcpu->vc_gueststate.vg_rbp;
		gprs[VCPU_REGS_RIP] = vcpu->vc_gueststate.vg_rip;
		if (vmread(VMCS_GUEST_IA32_RSP, &gprs[VCPU_REGS_RSP]))
			goto errout;
		if (vmread(VMCS_GUEST_IA32_RFLAGS, &gprs[VCPU_REGS_RFLAGS]))
			goto errout;
	}

	if (regmask & VM_RWREGS_SREGS) {
		for (i = 0; i < nitems(vmm_vmx_sreg_vmcs_fields); i++) {
			if (vmread(vmm_vmx_sreg_vmcs_fields[i].selid, &sel))
				goto errout;
			if (vmread(vmm_vmx_sreg_vmcs_fields[i].limitid, &limit))
				goto errout;
			if (vmread(vmm_vmx_sreg_vmcs_fields[i].arid, &ar))
				goto errout;
			if (vmread(vmm_vmx_sreg_vmcs_fields[i].baseid,
			   &sregs[i].vsi_base))
				goto errout;

			sregs[i].vsi_sel = sel;
			sregs[i].vsi_limit = limit;
			sregs[i].vsi_ar = ar;
		}

		if (vmread(VMCS_GUEST_IA32_GDTR_LIMIT, &limit))
			goto errout;
		if (vmread(VMCS_GUEST_IA32_GDTR_BASE,
		    &vrs->vrs_gdtr.vsi_base))
			goto errout;
		vrs->vrs_gdtr.vsi_limit = limit;

		if (vmread(VMCS_GUEST_IA32_IDTR_LIMIT, &limit))
			goto errout;
		if (vmread(VMCS_GUEST_IA32_IDTR_BASE,
		    &vrs->vrs_idtr.vsi_base))
			goto errout;
		vrs->vrs_idtr.vsi_limit = limit;
	}

	if (regmask & VM_RWREGS_CRS) {
		crs[VCPU_REGS_CR2] = vcpu->vc_gueststate.vg_cr2;
		crs[VCPU_REGS_XCR0] = vcpu->vc_gueststate.vg_xcr0;
		if (vmread(VMCS_GUEST_IA32_CR0, &crs[VCPU_REGS_CR0]))
			goto errout;
		if (vmread(VMCS_GUEST_IA32_CR3, &crs[VCPU_REGS_CR3]))
			goto errout;
		if (vmread(VMCS_GUEST_IA32_CR4, &crs[VCPU_REGS_CR4]))
			goto errout;
		if (vmread(VMCS_GUEST_PDPTE0, &crs[VCPU_REGS_PDPTE0]))
			goto errout;
		if (vmread(VMCS_GUEST_PDPTE1, &crs[VCPU_REGS_PDPTE1]))
			goto errout;
		if (vmread(VMCS_GUEST_PDPTE2, &crs[VCPU_REGS_PDPTE2]))
			goto errout;
		if (vmread(VMCS_GUEST_PDPTE3, &crs[VCPU_REGS_PDPTE3]))
			goto errout;
	}

	msr_store = (struct vmx_msr_store *)vcpu->vc_vmx_msr_exit_save_va;

	if (regmask & VM_RWREGS_MSRS) {
		for (i = 0; i < VCPU_REGS_NMSRS; i++) {
			msrs[i] = msr_store[i].vms_data;
		}
	}

	if (regmask & VM_RWREGS_DRS) {
		drs[VCPU_REGS_DR0] = vcpu->vc_gueststate.vg_dr0;
		drs[VCPU_REGS_DR1] = vcpu->vc_gueststate.vg_dr1;
		drs[VCPU_REGS_DR2] = vcpu->vc_gueststate.vg_dr2;
		drs[VCPU_REGS_DR3] = vcpu->vc_gueststate.vg_dr3;
		drs[VCPU_REGS_DR6] = vcpu->vc_gueststate.vg_dr6;
		if (vmread(VMCS_GUEST_IA32_DR7, &drs[VCPU_REGS_DR7]))
			goto errout;
	}

	goto out;

errout:
	ret = EINVAL;
out:
	return (ret);
}

/*
 * vcpu_readregs_svm
 *
 * Reads 'vcpu's registers
 *
 * Parameters:
 *  vcpu: the vcpu to read register values from
 *  regmask: the types of registers to read
 *  vrs: output parameter where register values are stored
 *
 * Return values:
 *  0: if successful
 */
int
vcpu_readregs_svm(struct vcpu *vcpu, uint64_t regmask,
    struct vcpu_reg_state *vrs)
{
	uint64_t *gprs = vrs->vrs_gprs;
	uint64_t *crs = vrs->vrs_crs;
	uint64_t *msrs = vrs->vrs_msrs;
	uint64_t *drs = vrs->vrs_drs;
	uint32_t attr;
	struct vcpu_segment_info *sregs = vrs->vrs_sregs;
	struct vmcb *vmcb = (struct vmcb *)vcpu->vc_control_va;

	if (regmask & VM_RWREGS_GPRS) {
		gprs[VCPU_REGS_RAX] = vmcb->v_rax;
		gprs[VCPU_REGS_RBX] = vcpu->vc_gueststate.vg_rbx;
		gprs[VCPU_REGS_RCX] = vcpu->vc_gueststate.vg_rcx;
		gprs[VCPU_REGS_RDX] = vcpu->vc_gueststate.vg_rdx;
		gprs[VCPU_REGS_RSI] = vcpu->vc_gueststate.vg_rsi;
		gprs[VCPU_REGS_RDI] = vcpu->vc_gueststate.vg_rdi;
		gprs[VCPU_REGS_R8] = vcpu->vc_gueststate.vg_r8;
		gprs[VCPU_REGS_R9] = vcpu->vc_gueststate.vg_r9;
		gprs[VCPU_REGS_R10] = vcpu->vc_gueststate.vg_r10;
		gprs[VCPU_REGS_R11] = vcpu->vc_gueststate.vg_r11;
		gprs[VCPU_REGS_R12] = vcpu->vc_gueststate.vg_r12;
		gprs[VCPU_REGS_R13] = vcpu->vc_gueststate.vg_r13;
		gprs[VCPU_REGS_R14] = vcpu->vc_gueststate.vg_r14;
		gprs[VCPU_REGS_R15] = vcpu->vc_gueststate.vg_r15;
		gprs[VCPU_REGS_RBP] = vcpu->vc_gueststate.vg_rbp;
		gprs[VCPU_REGS_RIP] = vmcb->v_rip;
		gprs[VCPU_REGS_RSP] = vmcb->v_rsp;
		gprs[VCPU_REGS_RFLAGS] = vmcb->v_rflags;
	}

	if (regmask & VM_RWREGS_SREGS) {
		sregs[VCPU_REGS_CS].vsi_sel = vmcb->v_cs.vs_sel;
		sregs[VCPU_REGS_CS].vsi_limit = vmcb->v_cs.vs_lim;
		attr = vmcb->v_cs.vs_attr;
		sregs[VCPU_REGS_CS].vsi_ar = (attr & 0xff) | ((attr << 4) &
		    0xf000);
		sregs[VCPU_REGS_CS].vsi_base = vmcb->v_cs.vs_base;

		sregs[VCPU_REGS_DS].vsi_sel = vmcb->v_ds.vs_sel;
		sregs[VCPU_REGS_DS].vsi_limit = vmcb->v_ds.vs_lim;
		attr = vmcb->v_ds.vs_attr;
		sregs[VCPU_REGS_DS].vsi_ar = (attr & 0xff) | ((attr << 4) &
		    0xf000);
		sregs[VCPU_REGS_DS].vsi_base = vmcb->v_ds.vs_base;

		sregs[VCPU_REGS_ES].vsi_sel = vmcb->v_es.vs_sel;
		sregs[VCPU_REGS_ES].vsi_limit = vmcb->v_es.vs_lim;
		attr = vmcb->v_es.vs_attr;
		sregs[VCPU_REGS_ES].vsi_ar = (attr & 0xff) | ((attr << 4) &
		    0xf000);
		sregs[VCPU_REGS_ES].vsi_base = vmcb->v_es.vs_base;

		sregs[VCPU_REGS_FS].vsi_sel = vmcb->v_fs.vs_sel;
		sregs[VCPU_REGS_FS].vsi_limit = vmcb->v_fs.vs_lim;
		attr = vmcb->v_fs.vs_attr;
		sregs[VCPU_REGS_FS].vsi_ar = (attr & 0xff) | ((attr << 4) &
		    0xf000);
		sregs[VCPU_REGS_FS].vsi_base = vmcb->v_fs.vs_base;

		sregs[VCPU_REGS_GS].vsi_sel = vmcb->v_gs.vs_sel;
		sregs[VCPU_REGS_GS].vsi_limit = vmcb->v_gs.vs_lim;
		attr = vmcb->v_gs.vs_attr;
		sregs[VCPU_REGS_GS].vsi_ar = (attr & 0xff) | ((attr << 4) &
		    0xf000);
		sregs[VCPU_REGS_GS].vsi_base = vmcb->v_gs.vs_base;

		sregs[VCPU_REGS_SS].vsi_sel = vmcb->v_ss.vs_sel;
		sregs[VCPU_REGS_SS].vsi_limit = vmcb->v_ss.vs_lim;
		attr = vmcb->v_ss.vs_attr;
		sregs[VCPU_REGS_SS].vsi_ar = (attr & 0xff) | ((attr << 4) &
		    0xf000);
		sregs[VCPU_REGS_SS].vsi_base = vmcb->v_ss.vs_base;

		sregs[VCPU_REGS_LDTR].vsi_sel = vmcb->v_ldtr.vs_sel;
		sregs[VCPU_REGS_LDTR].vsi_limit = vmcb->v_ldtr.vs_lim;
		attr = vmcb->v_ldtr.vs_attr;
		sregs[VCPU_REGS_LDTR].vsi_ar = (attr & 0xff) | ((attr << 4)
		    & 0xf000);
		sregs[VCPU_REGS_LDTR].vsi_base = vmcb->v_ldtr.vs_base;

		sregs[VCPU_REGS_TR].vsi_sel = vmcb->v_tr.vs_sel;
		sregs[VCPU_REGS_TR].vsi_limit = vmcb->v_tr.vs_lim;
		attr = vmcb->v_tr.vs_attr;
		sregs[VCPU_REGS_TR].vsi_ar = (attr & 0xff) | ((attr << 4) &
		    0xf000);
		sregs[VCPU_REGS_TR].vsi_base = vmcb->v_tr.vs_base;

		vrs->vrs_gdtr.vsi_limit = vmcb->v_gdtr.vs_lim;
		vrs->vrs_gdtr.vsi_base = vmcb->v_gdtr.vs_base;
		vrs->vrs_idtr.vsi_limit = vmcb->v_idtr.vs_lim;
		vrs->vrs_idtr.vsi_base = vmcb->v_idtr.vs_base;
	}

	if (regmask & VM_RWREGS_CRS) {
		crs[VCPU_REGS_CR0] = vmcb->v_cr0;
		crs[VCPU_REGS_CR3] = vmcb->v_cr3;
		crs[VCPU_REGS_CR4] = vmcb->v_cr4;
		crs[VCPU_REGS_CR2] = vcpu->vc_gueststate.vg_cr2;
		crs[VCPU_REGS_XCR0] = vcpu->vc_gueststate.vg_xcr0;
	}

	if (regmask & VM_RWREGS_MSRS) {
		 msrs[VCPU_REGS_EFER] = vmcb->v_efer;
		 msrs[VCPU_REGS_STAR] = vmcb->v_star;
		 msrs[VCPU_REGS_LSTAR] = vmcb->v_lstar;
		 msrs[VCPU_REGS_CSTAR] = vmcb->v_cstar;
		 msrs[VCPU_REGS_SFMASK] = vmcb->v_sfmask;
		 msrs[VCPU_REGS_KGSBASE] = vmcb->v_kgsbase;
	}

	if (regmask & VM_RWREGS_DRS) {
		drs[VCPU_REGS_DR0] = vcpu->vc_gueststate.vg_dr0;
		drs[VCPU_REGS_DR1] = vcpu->vc_gueststate.vg_dr1;
		drs[VCPU_REGS_DR2] = vcpu->vc_gueststate.vg_dr2;
		drs[VCPU_REGS_DR3] = vcpu->vc_gueststate.vg_dr3;
		drs[VCPU_REGS_DR6] = vmcb->v_dr6;
		drs[VCPU_REGS_DR7] = vmcb->v_dr7;
	}

	return (0);
}

/*
 * vcpu_writeregs_vmx
 *
 * Writes VCPU registers
 *
 * Parameters:
 *  vcpu: the vcpu that has to get its registers written to
 *  regmask: the types of registers to write
 *  loadvmcs: bit to indicate whether the VMCS has to be loaded first
 *  vrs: the register values to write
 *
 * Return values:
 *  0: if successful
 *  EINVAL an error writing registers occurred
 */
int
vcpu_writeregs_vmx(struct vcpu *vcpu, uint64_t regmask, int loadvmcs,
    struct vcpu_reg_state *vrs)
{
	int i, ret = 0;
	uint16_t sel;
	uint64_t limit, ar;
	uint64_t *gprs = vrs->vrs_gprs;
	uint64_t *crs = vrs->vrs_crs;
	uint64_t *msrs = vrs->vrs_msrs;
	uint64_t *drs = vrs->vrs_drs;
	struct vcpu_segment_info *sregs = vrs->vrs_sregs;
	struct vmx_msr_store *msr_store;

	if (loadvmcs) {
		if (vcpu_reload_vmcs_vmx(vcpu))
			return (EINVAL);
	}

#ifdef VMM_DEBUG
	/* VMCS should be loaded... */
	paddr_t pa = 0ULL;
	if (vmptrst(&pa))
		panic("%s: vmptrst", __func__);
	KASSERT(pa == vcpu->vc_control_pa);
#endif /* VMM_DEBUG */

	if (regmask & VM_RWREGS_GPRS) {
		vcpu->vc_gueststate.vg_rax = gprs[VCPU_REGS_RAX];
		vcpu->vc_gueststate.vg_rbx = gprs[VCPU_REGS_RBX];
		vcpu->vc_gueststate.vg_rcx = gprs[VCPU_REGS_RCX];
		vcpu->vc_gueststate.vg_rdx = gprs[VCPU_REGS_RDX];
		vcpu->vc_gueststate.vg_rsi = gprs[VCPU_REGS_RSI];
		vcpu->vc_gueststate.vg_rdi = gprs[VCPU_REGS_RDI];
		vcpu->vc_gueststate.vg_r8 = gprs[VCPU_REGS_R8];
		vcpu->vc_gueststate.vg_r9 = gprs[VCPU_REGS_R9];
		vcpu->vc_gueststate.vg_r10 = gprs[VCPU_REGS_R10];
		vcpu->vc_gueststate.vg_r11 = gprs[VCPU_REGS_R11];
		vcpu->vc_gueststate.vg_r12 = gprs[VCPU_REGS_R12];
		vcpu->vc_gueststate.vg_r13 = gprs[VCPU_REGS_R13];
		vcpu->vc_gueststate.vg_r14 = gprs[VCPU_REGS_R14];
		vcpu->vc_gueststate.vg_r15 = gprs[VCPU_REGS_R15];
		vcpu->vc_gueststate.vg_rbp = gprs[VCPU_REGS_RBP];
		vcpu->vc_gueststate.vg_rip = gprs[VCPU_REGS_RIP];
		if (vmwrite(VMCS_GUEST_IA32_RIP, gprs[VCPU_REGS_RIP]))
			goto errout;
		if (vmwrite(VMCS_GUEST_IA32_RSP, gprs[VCPU_REGS_RSP]))
			goto errout;
		if (vmwrite(VMCS_GUEST_IA32_RFLAGS, gprs[VCPU_REGS_RFLAGS]))
			goto errout;
	}

	if (regmask & VM_RWREGS_SREGS) {
		for (i = 0; i < nitems(vmm_vmx_sreg_vmcs_fields); i++) {
			sel = sregs[i].vsi_sel;
			limit = sregs[i].vsi_limit;
			ar = sregs[i].vsi_ar;

			if (vmwrite(vmm_vmx_sreg_vmcs_fields[i].selid, sel))
				goto errout;
			if (vmwrite(vmm_vmx_sreg_vmcs_fields[i].limitid, limit))
				goto errout;
			if (vmwrite(vmm_vmx_sreg_vmcs_fields[i].arid, ar))
				goto errout;
			if (vmwrite(vmm_vmx_sreg_vmcs_fields[i].baseid,
			    sregs[i].vsi_base))
				goto errout;
		}

		if (vmwrite(VMCS_GUEST_IA32_GDTR_LIMIT,
		    vrs->vrs_gdtr.vsi_limit))
			goto errout;
		if (vmwrite(VMCS_GUEST_IA32_GDTR_BASE,
		    vrs->vrs_gdtr.vsi_base))
			goto errout;
		if (vmwrite(VMCS_GUEST_IA32_IDTR_LIMIT,
		    vrs->vrs_idtr.vsi_limit))
			goto errout;
		if (vmwrite(VMCS_GUEST_IA32_IDTR_BASE,
		    vrs->vrs_idtr.vsi_base))
			goto errout;
	}

	if (regmask & VM_RWREGS_CRS) {
		vcpu->vc_gueststate.vg_xcr0 = crs[VCPU_REGS_XCR0];
		if (vmwrite(VMCS_GUEST_IA32_CR0, crs[VCPU_REGS_CR0]))
			goto errout;
		if (vmwrite(VMCS_GUEST_IA32_CR3, crs[VCPU_REGS_CR3]))
			goto errout;
		if (vmwrite(VMCS_GUEST_IA32_CR4, crs[VCPU_REGS_CR4]))
			goto errout;
		if (vmwrite(VMCS_GUEST_PDPTE0, crs[VCPU_REGS_PDPTE0]))
			goto errout;
		if (vmwrite(VMCS_GUEST_PDPTE1, crs[VCPU_REGS_PDPTE1]))
			goto errout;
		if (vmwrite(VMCS_GUEST_PDPTE2, crs[VCPU_REGS_PDPTE2]))
			goto errout;
		if (vmwrite(VMCS_GUEST_PDPTE3, crs[VCPU_REGS_PDPTE3]))
			goto errout;
	}

	msr_store = (struct vmx_msr_store *)vcpu->vc_vmx_msr_exit_save_va;

	if (regmask & VM_RWREGS_MSRS) {
		for (i = 0; i < VCPU_REGS_NMSRS; i++) {
			msr_store[i].vms_data = msrs[i];
		}
	}

	if (regmask & VM_RWREGS_DRS) {
		vcpu->vc_gueststate.vg_dr0 = drs[VCPU_REGS_DR0];
		vcpu->vc_gueststate.vg_dr1 = drs[VCPU_REGS_DR1];
		vcpu->vc_gueststate.vg_dr2 = drs[VCPU_REGS_DR2];
		vcpu->vc_gueststate.vg_dr3 = drs[VCPU_REGS_DR3];
		vcpu->vc_gueststate.vg_dr6 = drs[VCPU_REGS_DR6];
		if (vmwrite(VMCS_GUEST_IA32_DR7, drs[VCPU_REGS_DR7]))
			goto errout;
	}

	goto out;

errout:
	ret = EINVAL;
out:
	if (loadvmcs) {
		if (vmclear(&vcpu->vc_control_pa))
			ret = EINVAL;
		atomic_swap_uint(&vcpu->vc_vmx_vmcs_state, VMCS_CLEARED);
	}
	return (ret);
}

/*
 * vcpu_writeregs_svm
 *
 * Writes 'vcpu's registers
 *
 * Parameters:
 *  vcpu: the vcpu that has to get its registers written to
 *  regmask: the types of registers to write
 *  vrs: the register values to write
 *
 * Return values:
 *  0: if successful
 *  EINVAL an error writing registers occurred
 */
int
vcpu_writeregs_svm(struct vcpu *vcpu, uint64_t regmask,
    struct vcpu_reg_state *vrs)
{
	uint64_t *gprs = vrs->vrs_gprs;
	uint64_t *crs = vrs->vrs_crs;
	uint16_t attr;
	uint64_t *msrs = vrs->vrs_msrs;
	uint64_t *drs = vrs->vrs_drs;
	struct vcpu_segment_info *sregs = vrs->vrs_sregs;
	struct vmcb *vmcb = (struct vmcb *)vcpu->vc_control_va;

	if (regmask & VM_RWREGS_GPRS) {
		vcpu->vc_gueststate.vg_rax = gprs[VCPU_REGS_RAX];
		vcpu->vc_gueststate.vg_rbx = gprs[VCPU_REGS_RBX];
		vcpu->vc_gueststate.vg_rcx = gprs[VCPU_REGS_RCX];
		vcpu->vc_gueststate.vg_rdx = gprs[VCPU_REGS_RDX];
		vcpu->vc_gueststate.vg_rsi = gprs[VCPU_REGS_RSI];
		vcpu->vc_gueststate.vg_rdi = gprs[VCPU_REGS_RDI];
		vcpu->vc_gueststate.vg_r8 = gprs[VCPU_REGS_R8];
		vcpu->vc_gueststate.vg_r9 = gprs[VCPU_REGS_R9];
		vcpu->vc_gueststate.vg_r10 = gprs[VCPU_REGS_R10];
		vcpu->vc_gueststate.vg_r11 = gprs[VCPU_REGS_R11];
		vcpu->vc_gueststate.vg_r12 = gprs[VCPU_REGS_R12];
		vcpu->vc_gueststate.vg_r13 = gprs[VCPU_REGS_R13];
		vcpu->vc_gueststate.vg_r14 = gprs[VCPU_REGS_R14];
		vcpu->vc_gueststate.vg_r15 = gprs[VCPU_REGS_R15];
		vcpu->vc_gueststate.vg_rbp = gprs[VCPU_REGS_RBP];
		vcpu->vc_gueststate.vg_rip = gprs[VCPU_REGS_RIP];

		vmcb->v_rax = gprs[VCPU_REGS_RAX];
		vmcb->v_rip = gprs[VCPU_REGS_RIP];
		vmcb->v_rsp = gprs[VCPU_REGS_RSP];
		vmcb->v_rflags = gprs[VCPU_REGS_RFLAGS];
	}

	if (regmask & VM_RWREGS_SREGS) {
		vmcb->v_cs.vs_sel = sregs[VCPU_REGS_CS].vsi_sel;
		vmcb->v_cs.vs_lim = sregs[VCPU_REGS_CS].vsi_limit;
		attr = sregs[VCPU_REGS_CS].vsi_ar;
		vmcb->v_cs.vs_attr = (attr & 0xff) | ((attr >> 4) & 0xf00);
		vmcb->v_cs.vs_base = sregs[VCPU_REGS_CS].vsi_base;
		vmcb->v_ds.vs_sel = sregs[VCPU_REGS_DS].vsi_sel;
		vmcb->v_ds.vs_lim = sregs[VCPU_REGS_DS].vsi_limit;
		attr = sregs[VCPU_REGS_DS].vsi_ar;
		vmcb->v_ds.vs_attr = (attr & 0xff) | ((attr >> 4) & 0xf00);
		vmcb->v_ds.vs_base = sregs[VCPU_REGS_DS].vsi_base;
		vmcb->v_es.vs_sel = sregs[VCPU_REGS_ES].vsi_sel;
		vmcb->v_es.vs_lim = sregs[VCPU_REGS_ES].vsi_limit;
		attr = sregs[VCPU_REGS_ES].vsi_ar;
		vmcb->v_es.vs_attr = (attr & 0xff) | ((attr >> 4) & 0xf00);
		vmcb->v_es.vs_base = sregs[VCPU_REGS_ES].vsi_base;
		vmcb->v_fs.vs_sel = sregs[VCPU_REGS_FS].vsi_sel;
		vmcb->v_fs.vs_lim = sregs[VCPU_REGS_FS].vsi_limit;
		attr = sregs[VCPU_REGS_FS].vsi_ar;
		vmcb->v_fs.vs_attr = (attr & 0xff) | ((attr >> 4) & 0xf00);
		vmcb->v_fs.vs_base = sregs[VCPU_REGS_FS].vsi_base;
		vmcb->v_gs.vs_sel = sregs[VCPU_REGS_GS].vsi_sel;
		vmcb->v_gs.vs_lim = sregs[VCPU_REGS_GS].vsi_limit;
		attr = sregs[VCPU_REGS_GS].vsi_ar;
		vmcb->v_gs.vs_attr = (attr & 0xff) | ((attr >> 4) & 0xf00);
		vmcb->v_gs.vs_base = sregs[VCPU_REGS_GS].vsi_base;
		vmcb->v_ss.vs_sel = sregs[VCPU_REGS_SS].vsi_sel;
		vmcb->v_ss.vs_lim = sregs[VCPU_REGS_SS].vsi_limit;
		attr = sregs[VCPU_REGS_SS].vsi_ar;
		vmcb->v_ss.vs_attr = (attr & 0xff) | ((attr >> 4) & 0xf00);
		vmcb->v_ss.vs_base = sregs[VCPU_REGS_SS].vsi_base;
		vmcb->v_ldtr.vs_sel = sregs[VCPU_REGS_LDTR].vsi_sel;
		vmcb->v_ldtr.vs_lim = sregs[VCPU_REGS_LDTR].vsi_limit;
		attr = sregs[VCPU_REGS_LDTR].vsi_ar;
		vmcb->v_ldtr.vs_attr = (attr & 0xff) | ((attr >> 4) & 0xf00);
		vmcb->v_ldtr.vs_base = sregs[VCPU_REGS_LDTR].vsi_base;
		vmcb->v_tr.vs_sel = sregs[VCPU_REGS_TR].vsi_sel;
		vmcb->v_tr.vs_lim = sregs[VCPU_REGS_TR].vsi_limit;
		attr = sregs[VCPU_REGS_TR].vsi_ar;
		vmcb->v_tr.vs_attr = (attr & 0xff) | ((attr >> 4) & 0xf00);
		vmcb->v_tr.vs_base = sregs[VCPU_REGS_TR].vsi_base;
		vmcb->v_gdtr.vs_lim = vrs->vrs_gdtr.vsi_limit;
		vmcb->v_gdtr.vs_base = vrs->vrs_gdtr.vsi_base;
		vmcb->v_idtr.vs_lim = vrs->vrs_idtr.vsi_limit;
		vmcb->v_idtr.vs_base = vrs->vrs_idtr.vsi_base;
	}

	if (regmask & VM_RWREGS_CRS) {
		vmcb->v_cr0 = crs[VCPU_REGS_CR0];
		vmcb->v_cr3 = crs[VCPU_REGS_CR3];
		vmcb->v_cr4 = crs[VCPU_REGS_CR4];
		vcpu->vc_gueststate.vg_cr2 = crs[VCPU_REGS_CR2];
		vcpu->vc_gueststate.vg_xcr0 = crs[VCPU_REGS_XCR0];
	}

	if (regmask & VM_RWREGS_MSRS) {
		vmcb->v_efer |= msrs[VCPU_REGS_EFER];
		vmcb->v_star = msrs[VCPU_REGS_STAR];
		vmcb->v_lstar = msrs[VCPU_REGS_LSTAR];
		vmcb->v_cstar = msrs[VCPU_REGS_CSTAR];
		vmcb->v_sfmask = msrs[VCPU_REGS_SFMASK];
		vmcb->v_kgsbase = msrs[VCPU_REGS_KGSBASE];
	}

	if (regmask & VM_RWREGS_DRS) {
		vcpu->vc_gueststate.vg_dr0 = drs[VCPU_REGS_DR0];
		vcpu->vc_gueststate.vg_dr1 = drs[VCPU_REGS_DR1];
		vcpu->vc_gueststate.vg_dr2 = drs[VCPU_REGS_DR2];
		vcpu->vc_gueststate.vg_dr3 = drs[VCPU_REGS_DR3];
		vmcb->v_dr6 = drs[VCPU_REGS_DR6];
		vmcb->v_dr7 = drs[VCPU_REGS_DR7];
	}

	return (0);
}

/*
 * vcpu_reset_regs_svm
 *
 * Initializes 'vcpu's registers to supplied state
 *
 * Parameters:
 *  vcpu: the vcpu whose register state is to be initialized
 *  vrs: the register state to set
 *
 * Return values:
 *  0: registers init'ed successfully
 *  EINVAL: an error occurred setting register state
 */
int
vcpu_reset_regs_svm(struct vcpu *vcpu, struct vcpu_reg_state *vrs)
{
	struct vmcb *vmcb;
	int ret;

	vmcb = (struct vmcb *)vcpu->vc_control_va;

	/*
	 * Intercept controls
	 *
	 * External Interrupt exiting (SVM_INTERCEPT_INTR)
	 * External NMI exiting (SVM_INTERCEPT_NMI)
	 * CPUID instruction (SVM_INTERCEPT_CPUID)
	 * HLT instruction (SVM_INTERCEPT_HLT)
	 * INVLPGA instruction (SVM_INTERCEPT_INVLPGA)
	 * I/O instructions (SVM_INTERCEPT_INOUT)
	 * MSR access (SVM_INTERCEPT_MSR)
	 * shutdown events (SVM_INTERCEPT_SHUTDOWN)
	 * INVLPGA instruction (SVM_INTERCEPT_INVLPGA)
	 *
	 * VMRUN instruction (SVM_INTERCEPT_VMRUN)
	 * VMMCALL instruction (SVM_INTERCEPT_VMMCALL)
	 * VMLOAD instruction (SVM_INTERCEPT_VMLOAD)
	 * VMSAVE instruction (SVM_INTERCEPT_VMSAVE)
	 * STGI instruction (SVM_INTERCEPT_STGI)
	 * CLGI instruction (SVM_INTERCEPT_CLGI)
	 * SKINIT instruction (SVM_INTERCEPT_SKINIT)
	 * ICEBP instruction (SVM_INTERCEPT_ICEBP)
	 * MWAIT instruction (SVM_INTERCEPT_MWAIT_UNCOND)
	 * MWAIT instruction (SVM_INTERCEPT_MWAIT_COND)
	 * MONITOR instruction (SVM_INTERCEPT_MONITOR)
	 * RDTSCP instruction (SVM_INTERCEPT_RDTSCP)
	 * XSETBV instruction (SVM_INTERCEPT_XSETBV) (if available)
	 */
	vmcb->v_intercept1 = SVM_INTERCEPT_INTR | SVM_INTERCEPT_NMI |
	    SVM_INTERCEPT_CPUID | SVM_INTERCEPT_HLT | SVM_INTERCEPT_INOUT |
	    SVM_INTERCEPT_MSR | SVM_INTERCEPT_SHUTDOWN | SVM_INTERCEPT_INVLPGA;

	vmcb->v_intercept2 = SVM_INTERCEPT_VMRUN | SVM_INTERCEPT_VMMCALL |
	    SVM_INTERCEPT_VMLOAD | SVM_INTERCEPT_VMSAVE | SVM_INTERCEPT_STGI |
	    SVM_INTERCEPT_CLGI | SVM_INTERCEPT_SKINIT | SVM_INTERCEPT_ICEBP |
	    SVM_INTERCEPT_MWAIT_UNCOND | SVM_INTERCEPT_MONITOR |
	    SVM_INTERCEPT_MWAIT_COND | SVM_INTERCEPT_RDTSCP;

	/* With SEV-ES we cannot force access XCR0, thus no intercept */
	if (xsave_mask && !vcpu->vc_seves)
		vmcb->v_intercept2 |= SVM_INTERCEPT_XSETBV;

	if (vcpu->vc_seves) {
		/* With SEV-ES also intercept post EFER and CR[04] writes */
		vmcb->v_intercept2 |= SVM_INTERCEPT_EFER_WRITE;
		vmcb->v_intercept2 |= SVM_INTERCEPT_CR0_WRITE_POST;
		vmcb->v_intercept2 |= SVM_INTERCEPT_CR4_WRITE_POST;
	}

	/* Setup I/O bitmap */
	memset((uint8_t *)vcpu->vc_svm_ioio_va, 0xFF, 3 * PAGE_SIZE);
	vmcb->v_iopm_pa = (uint64_t)(vcpu->vc_svm_ioio_pa);

	/* Setup MSR bitmap */
	memset((uint8_t *)vcpu->vc_msr_bitmap_va, 0xFF, 2 * PAGE_SIZE);
	vmcb->v_msrpm_pa = (uint64_t)(vcpu->vc_msr_bitmap_pa);
	svm_setmsrbrw(vcpu, MSR_IA32_FEATURE_CONTROL);
	svm_setmsrbrw(vcpu, MSR_SYSENTER_CS);
	svm_setmsrbrw(vcpu, MSR_SYSENTER_ESP);
	svm_setmsrbrw(vcpu, MSR_SYSENTER_EIP);
	svm_setmsrbrw(vcpu, MSR_STAR);
	svm_setmsrbrw(vcpu, MSR_LSTAR);
	svm_setmsrbrw(vcpu, MSR_CSTAR);
	svm_setmsrbrw(vcpu, MSR_SFMASK);
	svm_setmsrbrw(vcpu, MSR_FSBASE);
	svm_setmsrbrw(vcpu, MSR_GSBASE);
	svm_setmsrbrw(vcpu, MSR_KERNELGSBASE);

	/* allow reading SEV status */
	svm_setmsrbrw(vcpu, MSR_SEV_STATUS);

	if (vcpu->vc_seves) {
		/* Allow read/write GHCB guest physical address */
		svm_setmsrbrw(vcpu, MSR_SEV_GHCB);

		/* Allow reading MSR_XSS; for CPUID Extended State Enum. */
		svm_setmsrbr(vcpu, MSR_XSS);

		/*
		 * With SEV-ES SVME can't be modified by the guest;
		 * host can only intercept post-write (see
		 * SVM_INTERCEPT_EFER_WRITE above).
		 */
		svm_setmsrbrw(vcpu, MSR_EFER);
	} else {
		/* EFER is R/O so we can ensure the guest always has SVME */
		svm_setmsrbr(vcpu, MSR_EFER);
	}

	/* allow reading TSC */
	svm_setmsrbr(vcpu, MSR_TSC);

	/* allow reading HWCR and PSTATEDEF to determine TSC frequency */
	svm_setmsrbr(vcpu, MSR_HWCR);
	svm_setmsrbr(vcpu, MSR_PSTATEDEF(0));

	/* Guest VCPU ASID */
	vmcb->v_asid = vcpu->vc_vpid;

	/* TLB Control - First time in, flush all*/
	vmcb->v_tlb_control = SVM_TLB_CONTROL_FLUSH_ALL;

	/* INTR masking */
	vmcb->v_intr_masking = 1;

	/* PAT */
	vmcb->v_g_pat = PATENTRY(0, PAT_WB) | PATENTRY(1, PAT_WC) |
	    PATENTRY(2, PAT_UCMINUS) | PATENTRY(3, PAT_UC) |
	    PATENTRY(4, PAT_WB) | PATENTRY(5, PAT_WC) |
	    PATENTRY(6, PAT_UCMINUS) | PATENTRY(7, PAT_UC);

	/* NPT */
	vmcb->v_np_enable = SVM_ENABLE_NP;
	vmcb->v_n_cr3 = vcpu->vc_parent->vm_pmap->pm_pdirpa;

	/* SEV */
	if (vcpu->vc_sev)
		vmcb->v_np_enable |= SVM_ENABLE_SEV;

	/* SEV-ES */
	if (vcpu->vc_seves) {
		vmcb->v_np_enable |= SVM_SEVES_ENABLE;
		vmcb->v_lbr_virt_enable |= SVM_LBRVIRT_ENABLE;

		/* Set VMSA. */
		vmcb->v_vmsa_pa = vcpu->vc_svm_vmsa_pa;
	}

	/* Enable SVME in EFER (must always be set) */
	vmcb->v_efer |= EFER_SVME;

	if ((ret = vcpu_writeregs_svm(vcpu, VM_RWREGS_ALL, vrs)) != 0)
		return ret;

	/* xcr0 power on default sets bit 0 (x87 state) */
	vcpu->vc_gueststate.vg_xcr0 = XFEATURE_X87 & xsave_mask;

	vcpu->vc_parent->vm_pmap->eptp = 0;

	ret = vcpu_svm_init_vmsa(vcpu, vrs);

	return ret;
}

/*
 * vcpu_svm_init_vmsa
 *
 * Initialize VMSA with initial VCPU state.
 */
int
vcpu_svm_init_vmsa(struct vcpu *vcpu, struct vcpu_reg_state *vrs)
{
	uint64_t	*gprs = vrs->vrs_gprs;
	struct vmcb	*vmcb = (struct vmcb *)vcpu->vc_control_va;
	struct vmsa	*vmsa;

	if (!vcpu->vc_seves)
		return 0;

	vmsa = (struct vmsa *)vcpu->vc_svm_vmsa_va;
	memcpy(vmsa, &vmcb->vmcb_layout, sizeof(vmcb->vmcb_layout));

	vmsa->v_rax = gprs[VCPU_REGS_RAX];
	vmsa->v_rbx = gprs[VCPU_REGS_RBX];
	vmsa->v_rcx = gprs[VCPU_REGS_RCX];
	vmsa->v_rdx = gprs[VCPU_REGS_RDX];
	vmsa->v_rsp = gprs[VCPU_REGS_RSP];
	vmsa->v_rbp = gprs[VCPU_REGS_RBP];
	vmsa->v_rsi = gprs[VCPU_REGS_RSI];
	vmsa->v_rdi = gprs[VCPU_REGS_RDI];

	vmsa->v_r8 = gprs[VCPU_REGS_R8];
	vmsa->v_r9 = gprs[VCPU_REGS_R9];
	vmsa->v_r10 = gprs[VCPU_REGS_R10];
	vmsa->v_r11 = gprs[VCPU_REGS_R11];
	vmsa->v_r12 = gprs[VCPU_REGS_R12];
	vmsa->v_r13 = gprs[VCPU_REGS_R13];
	vmsa->v_r14 = gprs[VCPU_REGS_R14];
	vmsa->v_r15 = gprs[VCPU_REGS_R15];

	vmsa->v_rip = gprs[VCPU_REGS_RIP];

	vmsa->v_xcr0 = vcpu->vc_gueststate.vg_xcr0;

	/* initialize FPU */
	vmsa->v_x87_fcw = __INITIAL_NPXCW__;
	vmsa->v_mxcsr = __INITIAL_MXCSR__;

	return 0;
}

/*
 * svm_setmsrbr
 *
 * Allow read access to the specified msr on the supplied vcpu.
 *
 * Parameters:
 *  vcpu: the VCPU to allow access
 *  msr: the MSR number to allow access to
 */
void
svm_setmsrbr(struct vcpu *vcpu, uint32_t msr)
{
	uint8_t *msrs;
	uint16_t idx;

	msrs = (uint8_t *)vcpu->vc_msr_bitmap_va;

	/*
	 * MSR Read bitmap layout:
	 * Pentium MSRs (0x0 - 0x1fff) @ 0x0
	 * Gen6 and Syscall MSRs (0xc0000000 - 0xc0001fff) @ 0x800
	 * Gen7 and Gen8 MSRs (0xc0010000 - 0xc0011fff) @ 0x1000
	 *
	 * Read enable bit is low order bit of 2-bit pair
	 * per MSR (eg, MSR 0x0 write bit is at bit 0 @ 0x0)
	 */
	if (msr <= 0x1fff) {
		idx = SVM_MSRIDX(msr);
		msrs[idx] &= ~(SVM_MSRBIT_R(msr));
	} else if (msr >= 0xc0000000 && msr <= 0xc0001fff) {
		idx = SVM_MSRIDX(msr - 0xc0000000) + 0x800;
		msrs[idx] &= ~(SVM_MSRBIT_R(msr - 0xc0000000));
	} else if (msr >= 0xc0010000 && msr <= 0xc0011fff) {
		idx = SVM_MSRIDX(msr - 0xc0010000) + 0x1000;
		msrs[idx] &= ~(SVM_MSRBIT_R(msr - 0xc0010000));
	} else {
		printf("%s: invalid msr 0x%x\n", __func__, msr);
		return;
	}
}

/*
 * svm_setmsrbw
 *
 * Allow write access to the specified msr on the supplied vcpu
 *
 * Parameters:
 *  vcpu: the VCPU to allow access
 *  msr: the MSR number to allow access to
 */
void
svm_setmsrbw(struct vcpu *vcpu, uint32_t msr)
{
	uint8_t *msrs;
	uint16_t idx;

	msrs = (uint8_t *)vcpu->vc_msr_bitmap_va;

	/*
	 * MSR Write bitmap layout:
	 * Pentium MSRs (0x0 - 0x1fff) @ 0x0
	 * Gen6 and Syscall MSRs (0xc0000000 - 0xc0001fff) @ 0x800
	 * Gen7 and Gen8 MSRs (0xc0010000 - 0xc0011fff) @ 0x1000
	 *
	 * Write enable bit is high order bit of 2-bit pair
	 * per MSR (eg, MSR 0x0 write bit is at bit 1 @ 0x0)
	 */
	if (msr <= 0x1fff) {
		idx = SVM_MSRIDX(msr);
		msrs[idx] &= ~(SVM_MSRBIT_W(msr));
	} else if (msr >= 0xc0000000 && msr <= 0xc0001fff) {
		idx = SVM_MSRIDX(msr - 0xc0000000) + 0x800;
		msrs[idx] &= ~(SVM_MSRBIT_W(msr - 0xc0000000));
	} else if (msr >= 0xc0010000 && msr <= 0xc0011fff) {
		idx = SVM_MSRIDX(msr - 0xc0010000) + 0x1000;
		msrs[idx] &= ~(SVM_MSRBIT_W(msr - 0xc0010000));
	} else {
		printf("%s: invalid msr 0x%x\n", __func__, msr);
		return;
	}
}

/*
 * svm_setmsrbrw
 *
 * Allow read/write access to the specified msr on the supplied vcpu
 *
 * Parameters:
 *  vcpu: the VCPU to allow access
 *  msr: the MSR number to allow access to
 */
void
svm_setmsrbrw(struct vcpu *vcpu, uint32_t msr)
{
	svm_setmsrbr(vcpu, msr);
	svm_setmsrbw(vcpu, msr);
}

/*
 * vmx_setmsrbr
 *
 * Allow read access to the specified msr on the supplied vcpu.
 *
 * Parameters:
 *  vcpu: the VCPU to allow access
 *  msr: the MSR number to allow access to
 */
void
vmx_setmsrbr(struct vcpu *vcpu, uint32_t msr)
{
	uint8_t *msrs;
	uint16_t idx;

	msrs = (uint8_t *)vcpu->vc_msr_bitmap_va;

	/*
	 * MSR Read bitmap layout:
	 * "Low" MSRs (0x0 - 0x1fff) @ 0x0
	 * "High" MSRs (0xc0000000 - 0xc0001fff) @ 0x400
	 */
	if (msr <= 0x1fff) {
		idx = VMX_MSRIDX(msr);
		msrs[idx] &= ~(VMX_MSRBIT(msr));
	} else if (msr >= 0xc0000000 && msr <= 0xc0001fff) {
		idx = VMX_MSRIDX(msr - 0xc0000000) + 0x400;
		msrs[idx] &= ~(VMX_MSRBIT(msr - 0xc0000000));
	} else
		printf("%s: invalid msr 0x%x\n", __func__, msr);
}

/*
 * vmx_setmsrbw
 *
 * Allow write access to the specified msr on the supplied vcpu
 *
 * Parameters:
 *  vcpu: the VCPU to allow access
 *  msr: the MSR number to allow access to
 */
void
vmx_setmsrbw(struct vcpu *vcpu, uint32_t msr)
{
	uint8_t *msrs;
	uint16_t idx;

	msrs = (uint8_t *)vcpu->vc_msr_bitmap_va;

	/*
	 * MSR Write bitmap layout:
	 * "Low" MSRs (0x0 - 0x1fff) @ 0x800
	 * "High" MSRs (0xc0000000 - 0xc0001fff) @ 0xc00
	 */
	if (msr <= 0x1fff) {
		idx = VMX_MSRIDX(msr) + 0x800;
		msrs[idx] &= ~(VMX_MSRBIT(msr));
	} else if (msr >= 0xc0000000 && msr <= 0xc0001fff) {
		idx = VMX_MSRIDX(msr - 0xc0000000) + 0xc00;
		msrs[idx] &= ~(VMX_MSRBIT(msr - 0xc0000000));
	} else
		printf("%s: invalid msr 0x%x\n", __func__, msr);
}

/*
 * vmx_setmsrbrw
 *
 * Allow read/write access to the specified msr on the supplied vcpu
 *
 * Parameters:
 *  vcpu: the VCPU to allow access
 *  msr: the MSR number to allow access to
 */
void
vmx_setmsrbrw(struct vcpu *vcpu, uint32_t msr)
{
	vmx_setmsrbr(vcpu, msr);
	vmx_setmsrbw(vcpu, msr);
}

/*
 * svm_set_clean
 *
 * Sets (mark as unmodified) the VMCB clean bit set in 'value'.
 * For example, to set the clean bit for the VMCB intercepts (bit position 0),
 * the caller provides 'SVM_CLEANBITS_I' (0x1) for the 'value' argument.
 * Multiple cleanbits can be provided in 'value' at the same time (eg,
 * "SVM_CLEANBITS_I | SVM_CLEANBITS_TPR").
 *
 * Note that this function does not clear any bits; to clear bits in the
 * vmcb cleanbits bitfield, use 'svm_set_dirty'.
 *
 * Parameters:
 *  vmcs: the VCPU whose VMCB clean value should be set
 *  value: the value(s) to enable in the cleanbits mask
 */
void
svm_set_clean(struct vcpu *vcpu, uint32_t value)
{
	struct vmcb *vmcb;

	/* If no cleanbits support, do nothing */
	if (!curcpu()->ci_vmm_cap.vcc_svm.svm_vmcb_clean)
		return;

	vmcb = (struct vmcb *)vcpu->vc_control_va;

	vmcb->v_vmcb_clean_bits |= value;
}

/*
 * svm_set_dirty
 *
 * Clears (mark as modified) the VMCB clean bit set in 'value'.
 * For example, to clear the bit for the VMCB intercepts (bit position 0)
 * the caller provides 'SVM_CLEANBITS_I' (0x1) for the 'value' argument.
 * Multiple dirty bits can be provided in 'value' at the same time (eg,
 * "SVM_CLEANBITS_I | SVM_CLEANBITS_TPR").
 *
 * Parameters:
 *  vmcs: the VCPU whose VMCB dirty value should be set
 *  value: the value(s) to dirty in the cleanbits mask
 */
void
svm_set_dirty(struct vcpu *vcpu, uint32_t value)
{
	struct vmcb *vmcb;

	/* If no cleanbits support, do nothing */
	if (!curcpu()->ci_vmm_cap.vcc_svm.svm_vmcb_clean)
		return;

	vmcb = (struct vmcb *)vcpu->vc_control_va;

	vmcb->v_vmcb_clean_bits &= ~value;
}

/*
 * vcpu_reset_regs_vmx
 *
 * Initializes 'vcpu's registers to supplied state
 *
 * Parameters:
 *  vcpu: the vcpu whose register state is to be initialized
 *  vrs: the register state to set
 *
 * Return values:
 *  0: registers init'ed successfully
 *  EINVAL: an error occurred setting register state
 */
int
vcpu_reset_regs_vmx(struct vcpu *vcpu, struct vcpu_reg_state *vrs)
{
	int ret = 0, ug = 0;
	uint32_t cr0, cr4;
	uint32_t pinbased, procbased, procbased2, exit, entry;
	uint32_t want1, want0;
	uint64_t ctrlval, cr3, msr_misc_enable;
	uint16_t ctrl;
	struct vmx_msr_store *msr_store;

	rw_assert_wrlock(&vcpu->vc_lock);

	cr0 = vrs->vrs_crs[VCPU_REGS_CR0];

	if (vcpu_reload_vmcs_vmx(vcpu)) {
		DPRINTF("%s: error reloading VMCS\n", __func__);
		ret = EINVAL;
		goto exit;
	}

#ifdef VMM_DEBUG
	/* VMCS should be loaded... */
	paddr_t pa = 0ULL;
	if (vmptrst(&pa))
		panic("%s: vmptrst", __func__);
	KASSERT(pa == vcpu->vc_control_pa);
#endif /* VMM_DEBUG */

	/* Compute Basic Entry / Exit Controls */
	vcpu->vc_vmx_basic = rdmsr(IA32_VMX_BASIC);
	vcpu->vc_vmx_entry_ctls = rdmsr(IA32_VMX_ENTRY_CTLS);
	vcpu->vc_vmx_exit_ctls = rdmsr(IA32_VMX_EXIT_CTLS);
	vcpu->vc_vmx_pinbased_ctls = rdmsr(IA32_VMX_PINBASED_CTLS);
	vcpu->vc_vmx_procbased_ctls = rdmsr(IA32_VMX_PROCBASED_CTLS);

	/* Compute True Entry / Exit Controls (if applicable) */
	if (vcpu->vc_vmx_basic & IA32_VMX_TRUE_CTLS_AVAIL) {
		vcpu->vc_vmx_true_entry_ctls = rdmsr(IA32_VMX_TRUE_ENTRY_CTLS);
		vcpu->vc_vmx_true_exit_ctls = rdmsr(IA32_VMX_TRUE_EXIT_CTLS);
		vcpu->vc_vmx_true_pinbased_ctls =
		    rdmsr(IA32_VMX_TRUE_PINBASED_CTLS);
		vcpu->vc_vmx_true_procbased_ctls =
		    rdmsr(IA32_VMX_TRUE_PROCBASED_CTLS);
	}

	/* Compute Secondary Procbased Controls (if applicable) */
	if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED_CTLS,
	    IA32_VMX_ACTIVATE_SECONDARY_CONTROLS, 1))
		vcpu->vc_vmx_procbased2_ctls = rdmsr(IA32_VMX_PROCBASED2_CTLS);

	/*
	 * Pinbased ctrls
	 *
	 * We must be able to set the following:
	 * IA32_VMX_EXTERNAL_INT_EXITING - exit on host interrupt
	 * IA32_VMX_NMI_EXITING - exit on host NMI
	 */
	want1 = IA32_VMX_EXTERNAL_INT_EXITING |
	    IA32_VMX_NMI_EXITING;
	want0 = 0;

	if (vcpu->vc_vmx_basic & IA32_VMX_TRUE_CTLS_AVAIL) {
		ctrl = IA32_VMX_TRUE_PINBASED_CTLS;
		ctrlval = vcpu->vc_vmx_true_pinbased_ctls;
	} else {
		ctrl = IA32_VMX_PINBASED_CTLS;
		ctrlval = vcpu->vc_vmx_pinbased_ctls;
	}

	if (vcpu_vmx_compute_ctrl(ctrlval, ctrl, want1, want0, &pinbased)) {
		DPRINTF("%s: error computing pinbased controls\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	if (vmwrite(VMCS_PINBASED_CTLS, pinbased)) {
		DPRINTF("%s: error setting pinbased controls\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	/*
	 * Procbased ctrls
	 *
	 * We must be able to set the following:
	 * IA32_VMX_HLT_EXITING - exit on HLT instruction
	 * IA32_VMX_MWAIT_EXITING - exit on MWAIT instruction
	 * IA32_VMX_UNCONDITIONAL_IO_EXITING - exit on I/O instructions
	 * IA32_VMX_USE_MSR_BITMAPS - exit on various MSR accesses
	 * IA32_VMX_CR8_LOAD_EXITING - guest TPR access
	 * IA32_VMX_CR8_STORE_EXITING - guest TPR access
	 * IA32_VMX_USE_TPR_SHADOW - guest TPR access (shadow)
	 * IA32_VMX_MONITOR_EXITING - exit on MONITOR instruction
	 *
	 * If we have EPT, we must be able to clear the following
	 * IA32_VMX_CR3_LOAD_EXITING - don't care about guest CR3 accesses
	 * IA32_VMX_CR3_STORE_EXITING - don't care about guest CR3 accesses
	 */
	want1 = IA32_VMX_HLT_EXITING |
	    IA32_VMX_MWAIT_EXITING |
	    IA32_VMX_UNCONDITIONAL_IO_EXITING |
	    IA32_VMX_USE_MSR_BITMAPS |
	    IA32_VMX_CR8_LOAD_EXITING |
	    IA32_VMX_CR8_STORE_EXITING |
	    IA32_VMX_MONITOR_EXITING |
	    IA32_VMX_USE_TPR_SHADOW;
	want0 = 0;

	want1 |= IA32_VMX_ACTIVATE_SECONDARY_CONTROLS;
	want0 |= IA32_VMX_CR3_LOAD_EXITING | IA32_VMX_CR3_STORE_EXITING;

	if (vcpu->vc_vmx_basic & IA32_VMX_TRUE_CTLS_AVAIL) {
		ctrl = IA32_VMX_TRUE_PROCBASED_CTLS;
		ctrlval = vcpu->vc_vmx_true_procbased_ctls;
	} else {
		ctrl = IA32_VMX_PROCBASED_CTLS;
		ctrlval = vcpu->vc_vmx_procbased_ctls;
	}

	if (vcpu_vmx_compute_ctrl(ctrlval, ctrl, want1, want0, &procbased)) {
		DPRINTF("%s: error computing procbased controls\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	if (vmwrite(VMCS_PROCBASED_CTLS, procbased)) {
		DPRINTF("%s: error setting procbased controls\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	/*
	 * Secondary Procbased ctrls
	 *
	 * We want to be able to set the following, if available:
	 * IA32_VMX_ENABLE_VPID - use VPIDs where available
	 *
	 * If we have EPT, we must be able to set the following:
	 * IA32_VMX_ENABLE_EPT - enable EPT
	 *
	 * If we have unrestricted guest capability, we must be able to set
	 * the following:
	 * IA32_VMX_UNRESTRICTED_GUEST - enable unrestricted guest (if caller
	 *     specified CR0_PG | CR0_PE in %cr0 in the 'vrs' parameter)
	 */
	want1 = IA32_VMX_ENABLE_EPT;

	/* XXX checking for 2ndary controls can be combined here */
	if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED_CTLS,
	    IA32_VMX_ACTIVATE_SECONDARY_CONTROLS, 1)) {
		if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED2_CTLS,
		    IA32_VMX_ENABLE_VPID, 1)) {
			want1 |= IA32_VMX_ENABLE_VPID;
			vcpu->vc_vmx_vpid_enabled = 1;
		}
	}

	if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED_CTLS,
	    IA32_VMX_ACTIVATE_SECONDARY_CONTROLS, 1)) {
		if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED2_CTLS,
		    IA32_VMX_UNRESTRICTED_GUEST, 1)) {
			want1 |= IA32_VMX_UNRESTRICTED_GUEST;
			ug = 1;
		}
	}

	want0 = ~want1;
	ctrlval = vcpu->vc_vmx_procbased2_ctls;
	ctrl = IA32_VMX_PROCBASED2_CTLS;

	if (vcpu_vmx_compute_ctrl(ctrlval, ctrl, want1, want0, &procbased2)) {
		DPRINTF("%s: error computing secondary procbased controls\n",
		     __func__);
		ret = EINVAL;
		goto exit;
	}

	if (vmwrite(VMCS_PROCBASED2_CTLS, procbased2)) {
		DPRINTF("%s: error setting secondary procbased controls\n",
		     __func__);
		ret = EINVAL;
		goto exit;
	}

	/*
	 * Exit ctrls
	 *
	 * We must be able to set the following:
	 * IA32_VMX_SAVE_DEBUG_CONTROLS
	 * IA32_VMX_HOST_SPACE_ADDRESS_SIZE - exit to long mode
	 * IA32_VMX_ACKNOWLEDGE_INTERRUPT_ON_EXIT - ack interrupt on exit
	 */
	want1 = IA32_VMX_HOST_SPACE_ADDRESS_SIZE |
	    IA32_VMX_ACKNOWLEDGE_INTERRUPT_ON_EXIT |
	    IA32_VMX_SAVE_DEBUG_CONTROLS;
	want0 = 0;

	if (vcpu->vc_vmx_basic & IA32_VMX_TRUE_CTLS_AVAIL) {
		ctrl = IA32_VMX_TRUE_EXIT_CTLS;
		ctrlval = vcpu->vc_vmx_true_exit_ctls;
	} else {
		ctrl = IA32_VMX_EXIT_CTLS;
		ctrlval = vcpu->vc_vmx_exit_ctls;
	}

	if (rcr4() & CR4_CET)
		want1 |= IA32_VMX_LOAD_HOST_CET_STATE;
	else
		want0 |= IA32_VMX_LOAD_HOST_CET_STATE;

	if (vcpu_vmx_compute_ctrl(ctrlval, ctrl, want1, want0, &exit)) {
		DPRINTF("%s: error computing exit controls\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	if (vmwrite(VMCS_EXIT_CTLS, exit)) {
		DPRINTF("%s: error setting exit controls\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	/*
	 * Entry ctrls
	 *
	 * We must be able to set the following:
	 * IA32_VMX_IA32E_MODE_GUEST (if no unrestricted guest)
	 * IA32_VMX_LOAD_DEBUG_CONTROLS
	 * We must be able to clear the following:
	 * IA32_VMX_ENTRY_TO_SMM - enter to SMM
	 * IA32_VMX_DEACTIVATE_DUAL_MONITOR_TREATMENT
	 * IA32_VMX_LOAD_IA32_PERF_GLOBAL_CTRL_ON_ENTRY
	 */
	want1 = IA32_VMX_LOAD_DEBUG_CONTROLS;
	if (vrs->vrs_msrs[VCPU_REGS_EFER] & EFER_LMA)
		want1 |= IA32_VMX_IA32E_MODE_GUEST;

	want0 = IA32_VMX_ENTRY_TO_SMM |
	    IA32_VMX_DEACTIVATE_DUAL_MONITOR_TREATMENT |
	    IA32_VMX_LOAD_IA32_PERF_GLOBAL_CTRL_ON_ENTRY;

	if (vcpu->vc_vmx_basic & IA32_VMX_TRUE_CTLS_AVAIL) {
		ctrl = IA32_VMX_TRUE_ENTRY_CTLS;
		ctrlval = vcpu->vc_vmx_true_entry_ctls;
	} else {
		ctrl = IA32_VMX_ENTRY_CTLS;
		ctrlval = vcpu->vc_vmx_entry_ctls;
	}

	if (rcr4() & CR4_CET)
		want1 |= IA32_VMX_LOAD_GUEST_CET_STATE;
	else
		want0 |= IA32_VMX_LOAD_GUEST_CET_STATE;

	if (vcpu_vmx_compute_ctrl(ctrlval, ctrl, want1, want0, &entry)) {
		ret = EINVAL;
		goto exit;
	}

	if (vmwrite(VMCS_ENTRY_CTLS, entry)) {
		ret = EINVAL;
		goto exit;
	}

	if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED_CTLS,
	    IA32_VMX_ACTIVATE_SECONDARY_CONTROLS, 1)) {
		if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED2_CTLS,
		    IA32_VMX_ENABLE_VPID, 1)) {
			if (vmwrite(VMCS_GUEST_VPID, vcpu->vc_vpid)) {
				DPRINTF("%s: error setting guest VPID\n",
				    __func__);
				ret = EINVAL;
				goto exit;
			}
		}
	}

	/*
	 * Determine which bits in CR0 have to be set to a fixed
	 * value as per Intel SDM A.7.
	 * CR0 bits in the vrs parameter must match these.
	 */
	want1 = (curcpu()->ci_vmm_cap.vcc_vmx.vmx_cr0_fixed0) &
	    (curcpu()->ci_vmm_cap.vcc_vmx.vmx_cr0_fixed1);
	want0 = ~(curcpu()->ci_vmm_cap.vcc_vmx.vmx_cr0_fixed0) &
	    ~(curcpu()->ci_vmm_cap.vcc_vmx.vmx_cr0_fixed1);

	/*
	 * CR0_FIXED0 and CR0_FIXED1 may report the CR0_PG and CR0_PE bits as
	 * fixed to 1 even if the CPU supports the unrestricted guest
	 * feature. Update want1 and want0 accordingly to allow
	 * any value for CR0_PG and CR0_PE in vrs->vrs_crs[VCPU_REGS_CR0] if
	 * the CPU has the unrestricted guest capability.
	 */
	if (ug) {
		want1 &= ~(CR0_PG | CR0_PE);
		want0 &= ~(CR0_PG | CR0_PE);
	}

	/*
	 * VMX may require some bits to be set that userland should not have
	 * to care about. Set those here.
	 */
	if (want1 & CR0_NE)
		cr0 |= CR0_NE;

	if ((cr0 & want1) != want1) {
		ret = EINVAL;
		goto exit;
	}

	if ((~cr0 & want0) != want0) {
		ret = EINVAL;
		goto exit;
	}

	vcpu->vc_vmx_cr0_fixed1 = want1;
	vcpu->vc_vmx_cr0_fixed0 = want0;
	/*
	 * Determine which bits in CR4 have to be set to a fixed
	 * value as per Intel SDM A.8.
	 * CR4 bits in the vrs parameter must match these, except
	 * CR4_VMXE - we add that here since it must always be set.
	 */
	want1 = (curcpu()->ci_vmm_cap.vcc_vmx.vmx_cr4_fixed0) &
	    (curcpu()->ci_vmm_cap.vcc_vmx.vmx_cr4_fixed1);
	want0 = ~(curcpu()->ci_vmm_cap.vcc_vmx.vmx_cr4_fixed0) &
	    ~(curcpu()->ci_vmm_cap.vcc_vmx.vmx_cr4_fixed1);

	cr4 = vrs->vrs_crs[VCPU_REGS_CR4] | CR4_VMXE;

	if ((cr4 & want1) != want1) {
		ret = EINVAL;
		goto exit;
	}

	if ((~cr4 & want0) != want0) {
		ret = EINVAL;
		goto exit;
	}

	cr3 = vrs->vrs_crs[VCPU_REGS_CR3];

	/* Restore PDPTEs if 32-bit PAE paging is being used */
	if (cr3 && (cr4 & CR4_PAE) &&
	    !(vrs->vrs_msrs[VCPU_REGS_EFER] & EFER_LMA)) {
		if (vmwrite(VMCS_GUEST_PDPTE0,
		    vrs->vrs_crs[VCPU_REGS_PDPTE0])) {
			ret = EINVAL;
			goto exit;
		}

		if (vmwrite(VMCS_GUEST_PDPTE1,
		    vrs->vrs_crs[VCPU_REGS_PDPTE1])) {
			ret = EINVAL;
			goto exit;
		}

		if (vmwrite(VMCS_GUEST_PDPTE2,
		    vrs->vrs_crs[VCPU_REGS_PDPTE2])) {
			ret = EINVAL;
			goto exit;
		}

		if (vmwrite(VMCS_GUEST_PDPTE3,
		    vrs->vrs_crs[VCPU_REGS_PDPTE3])) {
			ret = EINVAL;
			goto exit;
		}
	}

	vrs->vrs_crs[VCPU_REGS_CR0] = cr0;
	vrs->vrs_crs[VCPU_REGS_CR4] = cr4;

	msr_misc_enable = rdmsr(MSR_MISC_ENABLE);

	/*
	 * Select host MSRs to be loaded on exit
	 */
	msr_store = (struct vmx_msr_store *)vcpu->vc_vmx_msr_exit_load_va;
	msr_store[VCPU_HOST_REGS_EFER].vms_index = MSR_EFER;
	msr_store[VCPU_HOST_REGS_EFER].vms_data = rdmsr(MSR_EFER);
	msr_store[VCPU_HOST_REGS_STAR].vms_index = MSR_STAR;
	msr_store[VCPU_HOST_REGS_STAR].vms_data = rdmsr(MSR_STAR);
	msr_store[VCPU_HOST_REGS_LSTAR].vms_index = MSR_LSTAR;
	msr_store[VCPU_HOST_REGS_LSTAR].vms_data = rdmsr(MSR_LSTAR);
	msr_store[VCPU_HOST_REGS_CSTAR].vms_index = MSR_CSTAR;
	msr_store[VCPU_HOST_REGS_CSTAR].vms_data = 0;
	msr_store[VCPU_HOST_REGS_SFMASK].vms_index = MSR_SFMASK;
	msr_store[VCPU_HOST_REGS_SFMASK].vms_data = rdmsr(MSR_SFMASK);
	msr_store[VCPU_HOST_REGS_KGSBASE].vms_index = MSR_KERNELGSBASE;
	msr_store[VCPU_HOST_REGS_KGSBASE].vms_data = 0;
	msr_store[VCPU_HOST_REGS_MISC_ENABLE].vms_index = MSR_MISC_ENABLE;
	msr_store[VCPU_HOST_REGS_MISC_ENABLE].vms_data = msr_misc_enable;

	/*
	 * Select guest MSRs to be loaded on entry / saved on exit
	 */
	msr_store = (struct vmx_msr_store *)vcpu->vc_vmx_msr_exit_save_va;

	msr_store[VCPU_REGS_EFER].vms_index = MSR_EFER;
	msr_store[VCPU_REGS_STAR].vms_index = MSR_STAR;
	msr_store[VCPU_REGS_LSTAR].vms_index = MSR_LSTAR;
	msr_store[VCPU_REGS_CSTAR].vms_index = MSR_CSTAR;
	msr_store[VCPU_REGS_SFMASK].vms_index = MSR_SFMASK;
	msr_store[VCPU_REGS_KGSBASE].vms_index = MSR_KERNELGSBASE;
	msr_store[VCPU_REGS_MISC_ENABLE].vms_index = MSR_MISC_ENABLE;

	/*
	 * Initialize MSR_MISC_ENABLE as it can't be read and populated from vmd
	 * and some of the content is based on the host.
	 */
	msr_store[VCPU_REGS_MISC_ENABLE].vms_data = msr_misc_enable;
	msr_store[VCPU_REGS_MISC_ENABLE].vms_data &=
	    ~(MISC_ENABLE_TCC | MISC_ENABLE_PERF_MON_AVAILABLE |
	      MISC_ENABLE_EIST_ENABLED | MISC_ENABLE_ENABLE_MONITOR_FSM |
	      MISC_ENABLE_xTPR_MESSAGE_DISABLE);
	msr_store[VCPU_REGS_MISC_ENABLE].vms_data |=
	      MISC_ENABLE_BTS_UNAVAILABLE | MISC_ENABLE_PEBS_UNAVAILABLE;

	/*
	 * Currently we use the same memory for guest MSRs (entry-load and
	 * exit-store) so they have the same count.  We exit-load the same
	 * host MSRs, so same count but different memory.  Those are just
	 * our current choices, not architectural requirements.
	 */
	if (vmwrite(VMCS_EXIT_MSR_STORE_COUNT, VCPU_REGS_NMSRS)) {
		DPRINTF("%s: error setting guest MSR exit store count\n",
		    __func__);
		ret = EINVAL;
		goto exit;
	}

	if (vmwrite(VMCS_EXIT_MSR_LOAD_COUNT, VCPU_HOST_REGS_NMSRS)) {
		DPRINTF("%s: error setting guest MSR exit load count\n",
		    __func__);
		ret = EINVAL;
		goto exit;
	}

	if (vmwrite(VMCS_ENTRY_MSR_LOAD_COUNT, VCPU_REGS_NMSRS)) {
		DPRINTF("%s: error setting guest MSR entry load count\n",
		    __func__);
		ret = EINVAL;
		goto exit;
	}

	if (vmwrite(VMCS_EXIT_STORE_MSR_ADDRESS,
	    vcpu->vc_vmx_msr_exit_save_pa)) {
		DPRINTF("%s: error setting guest MSR exit store address\n",
		    __func__);
		ret = EINVAL;
		goto exit;
	}

	if (vmwrite(VMCS_EXIT_LOAD_MSR_ADDRESS,
	    vcpu->vc_vmx_msr_exit_load_pa)) {
		DPRINTF("%s: error setting guest MSR exit load address\n",
		    __func__);
		ret = EINVAL;
		goto exit;
	}

	if (vmwrite(VMCS_ENTRY_LOAD_MSR_ADDRESS,
	    vcpu->vc_vmx_msr_exit_save_pa)) {
		DPRINTF("%s: error setting guest MSR entry load address\n",
		    __func__);
		ret = EINVAL;
		goto exit;
	}

	if (vmwrite(VMCS_MSR_BITMAP_ADDRESS,
	    vcpu->vc_msr_bitmap_pa)) {
		DPRINTF("%s: error setting guest MSR bitmap address\n",
		    __func__);
		ret = EINVAL;
		goto exit;
	}

	if (vmwrite(VMCS_CR4_MASK, CR4_VMXE)) {
		DPRINTF("%s: error setting guest CR4 mask\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	if (vmwrite(VMCS_CR0_MASK, CR0_NE)) {
		DPRINTF("%s: error setting guest CR0 mask\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	/*
	 * Set up the VMCS for the register state we want during VCPU start.
	 * This matches what the CPU state would be after a bootloader
	 * transition to 'start'.
	 */
	ret = vcpu_writeregs_vmx(vcpu, VM_RWREGS_ALL, 0, vrs);

	/*
	 * Set up the MSR bitmap
	 */
	memset((uint8_t *)vcpu->vc_msr_bitmap_va, 0xFF, PAGE_SIZE);
	vmx_setmsrbrw(vcpu, MSR_IA32_FEATURE_CONTROL);
	vmx_setmsrbrw(vcpu, MSR_SYSENTER_CS);
	vmx_setmsrbrw(vcpu, MSR_SYSENTER_ESP);
	vmx_setmsrbrw(vcpu, MSR_SYSENTER_EIP);
	vmx_setmsrbrw(vcpu, MSR_EFER);
	vmx_setmsrbrw(vcpu, MSR_STAR);
	vmx_setmsrbrw(vcpu, MSR_LSTAR);
	vmx_setmsrbrw(vcpu, MSR_CSTAR);
	vmx_setmsrbrw(vcpu, MSR_SFMASK);
	vmx_setmsrbrw(vcpu, MSR_FSBASE);
	vmx_setmsrbrw(vcpu, MSR_GSBASE);
	vmx_setmsrbrw(vcpu, MSR_KERNELGSBASE);

	vmx_setmsrbr(vcpu, MSR_MISC_ENABLE);
	vmx_setmsrbr(vcpu, MSR_TSC);

	/* If host supports CET, pass through access to the guest. */
	if (rcr4() & CR4_CET)
		vmx_setmsrbrw(vcpu, MSR_S_CET);

	/* XXX CR0 shadow */
	/* XXX CR4 shadow */

	/* xcr0 power on default sets bit 0 (x87 state) */
	vcpu->vc_gueststate.vg_xcr0 = XFEATURE_X87 & xsave_mask;

	/* XXX PAT shadow */
	vcpu->vc_shadow_pat = rdmsr(MSR_CR_PAT);

	/* Flush the VMCS */
	if (vmclear(&vcpu->vc_control_pa)) {
		DPRINTF("%s: vmclear failed\n", __func__);
		ret = EINVAL;
	}
	atomic_swap_uint(&vcpu->vc_vmx_vmcs_state, VMCS_CLEARED);

exit:
	return (ret);
}

/*
 * vcpu_init_vmx
 *
 * Intel VMX specific VCPU initialization routine.
 *
 * This function allocates various per-VCPU memory regions, sets up initial
 * VCPU VMCS controls, and sets initial register values.
 *
 * Parameters:
 *  vcpu: the VCPU structure being initialized
 *
 * Return values:
 *  0: the VCPU was initialized successfully
 *  ENOMEM: insufficient resources
 *  EINVAL: an error occurred during VCPU initialization
 */
int
vcpu_init_vmx(struct vcpu *vcpu)
{
	struct vmcs *vmcs;
	uint64_t msr, eptp;
	uint32_t cr0, cr4;
	int ret = 0;

	/* Allocate a VPID early to avoid km_alloc if we're out of VPIDs. */
	if (vmm_alloc_vpid(&vcpu->vc_vpid))
		return (ENOMEM);

	/* Allocate VMCS VA */
	vcpu->vc_control_va = (vaddr_t)km_alloc(PAGE_SIZE, &kv_page, &kp_zero,
	    &kd_waitok);
	vcpu->vc_vmx_vmcs_state = VMCS_CLEARED;

	if (!vcpu->vc_control_va) {
		ret = ENOMEM;
		goto exit;
	}

	/* Compute VMCS PA */
	if (!pmap_extract(pmap_kernel(), vcpu->vc_control_va,
	    (paddr_t *)&vcpu->vc_control_pa)) {
		ret = ENOMEM;
		goto exit;
	}

	/* Allocate MSR bitmap VA */
	vcpu->vc_msr_bitmap_va = (vaddr_t)km_alloc(PAGE_SIZE, &kv_page, &kp_zero,
	    &kd_waitok);

	if (!vcpu->vc_msr_bitmap_va) {
		ret = ENOMEM;
		goto exit;
	}

	/* Compute MSR bitmap PA */
	if (!pmap_extract(pmap_kernel(), vcpu->vc_msr_bitmap_va,
	    (paddr_t *)&vcpu->vc_msr_bitmap_pa)) {
		ret = ENOMEM;
		goto exit;
	}

	/* Allocate MSR exit load area VA */
	vcpu->vc_vmx_msr_exit_load_va = (vaddr_t)km_alloc(PAGE_SIZE, &kv_page,
	   &kp_zero, &kd_waitok);

	if (!vcpu->vc_vmx_msr_exit_load_va) {
		ret = ENOMEM;
		goto exit;
	}

	/* Compute MSR exit load area PA */
	if (!pmap_extract(pmap_kernel(), vcpu->vc_vmx_msr_exit_load_va,
	    &vcpu->vc_vmx_msr_exit_load_pa)) {
		ret = ENOMEM;
		goto exit;
	}

	/* Allocate MSR exit save area VA */
	vcpu->vc_vmx_msr_exit_save_va = (vaddr_t)km_alloc(PAGE_SIZE, &kv_page,
	   &kp_zero, &kd_waitok);

	if (!vcpu->vc_vmx_msr_exit_save_va) {
		ret = ENOMEM;
		goto exit;
	}

	/* Compute MSR exit save area PA */
	if (!pmap_extract(pmap_kernel(), vcpu->vc_vmx_msr_exit_save_va,
	    &vcpu->vc_vmx_msr_exit_save_pa)) {
		ret = ENOMEM;
		goto exit;
	}

#if 0	/* XXX currently use msr_exit_save for msr_entry_load too */
	/* Allocate MSR entry load area VA */
	vcpu->vc_vmx_msr_entry_load_va = (vaddr_t)km_alloc(PAGE_SIZE, &kv_page,
	   &kp_zero, &kd_waitok);

	if (!vcpu->vc_vmx_msr_entry_load_va) {
		ret = ENOMEM;
		goto exit;
	}

	/* Compute MSR entry load area PA */
	if (!pmap_extract(pmap_kernel(), vcpu->vc_vmx_msr_entry_load_va,
	    &vcpu->vc_vmx_msr_entry_load_pa)) {
		ret = ENOMEM;
		goto exit;
	}
#endif

	vmcs = (struct vmcs *)vcpu->vc_control_va;
	vmcs->vmcs_revision = curcpu()->ci_vmm_cap.vcc_vmx.vmx_vmxon_revision;

	/*
	 * Load the VMCS onto this PCPU so we can write registers
	 */
	if (vmptrld(&vcpu->vc_control_pa)) {
		ret = EINVAL;
		goto exit;
	}

	/* Configure EPT Pointer */
	eptp = vcpu->vc_parent->vm_pmap->pm_pdirpa;
	msr = rdmsr(IA32_VMX_EPT_VPID_CAP);
	if (msr & IA32_EPT_VPID_CAP_PAGE_WALK_4) {
		/* Page walk length 4 supported */
		eptp |= ((IA32_EPT_PAGE_WALK_LENGTH - 1) << 3);
	} else {
		DPRINTF("EPT page walk length 4 not supported\n");
		ret = EINVAL;
		goto exit;
	}
	if (msr & IA32_EPT_VPID_CAP_WB) {
		/* WB cache type supported */
		eptp |= IA32_EPT_PAGING_CACHE_TYPE_WB;
	} else
		DPRINTF("%s: no WB cache type available, guest VM will run "
		    "uncached\n", __func__);

	DPRINTF("Guest EPTP = 0x%llx\n", eptp);
	if (vmwrite(VMCS_GUEST_IA32_EPTP, eptp)) {
		DPRINTF("%s: error setting guest EPTP\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	vcpu->vc_parent->vm_pmap->eptp = eptp;

	/* Host CR0 */
	cr0 = rcr0() & ~CR0_TS;
	if (vmwrite(VMCS_HOST_IA32_CR0, cr0)) {
		DPRINTF("%s: error writing host CR0\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	/* Host CR4 */
	cr4 = rcr4();
	if (vmwrite(VMCS_HOST_IA32_CR4, cr4)) {
		DPRINTF("%s: error writing host CR4\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	/* Host Segment Selectors */
	if (vmwrite(VMCS_HOST_IA32_CS_SEL, GSEL(GCODE_SEL, SEL_KPL))) {
		DPRINTF("%s: error writing host CS selector\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	if (vmwrite(VMCS_HOST_IA32_DS_SEL, GSEL(GDATA_SEL, SEL_KPL))) {
		DPRINTF("%s: error writing host DS selector\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	if (vmwrite(VMCS_HOST_IA32_ES_SEL, GSEL(GDATA_SEL, SEL_KPL))) {
		DPRINTF("%s: error writing host ES selector\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	if (vmwrite(VMCS_HOST_IA32_FS_SEL, GSEL(GDATA_SEL, SEL_KPL))) {
		DPRINTF("%s: error writing host FS selector\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	if (vmwrite(VMCS_HOST_IA32_GS_SEL, GSEL(GDATA_SEL, SEL_KPL))) {
		DPRINTF("%s: error writing host GS selector\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	if (vmwrite(VMCS_HOST_IA32_SS_SEL, GSEL(GDATA_SEL, SEL_KPL))) {
		DPRINTF("%s: error writing host SS selector\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	if (vmwrite(VMCS_HOST_IA32_TR_SEL, GSYSSEL(GPROC0_SEL, SEL_KPL))) {
		DPRINTF("%s: error writing host TR selector\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	/* Host IDTR base */
	if (vmwrite(VMCS_HOST_IA32_IDTR_BASE, idt_vaddr)) {
		DPRINTF("%s: error writing host IDTR base\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	/* VMCS link */
	if (vmwrite(VMCS_LINK_POINTER, VMX_VMCS_PA_CLEAR)) {
		DPRINTF("%s: error writing VMCS link pointer\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	/* Flush the initial VMCS */
	if (vmclear(&vcpu->vc_control_pa)) {
		DPRINTF("%s: vmclear failed\n", __func__);
		ret = EINVAL;
	}

exit:
	if (ret)
		vcpu_deinit_vmx(vcpu);

	return (ret);
}

/*
 * vcpu_reset_regs
 *
 * Resets a vcpu's registers to the provided state
 *
 * Parameters:
 *  vcpu: the vcpu whose registers shall be reset
 *  vrs: the desired register state
 *
 * Return values:
 *  0: the vcpu's registers were successfully reset
 *  !0: the vcpu's registers could not be reset (see arch-specific reset
 *      function for various values that can be returned here)
 */
int
vcpu_reset_regs(struct vcpu *vcpu, struct vcpu_reg_state *vrs)
{
	int ret;

	if (vmm_softc->mode == VMM_MODE_EPT)
		ret = vcpu_reset_regs_vmx(vcpu, vrs);
	else if (vmm_softc->mode == VMM_MODE_RVI)
		ret = vcpu_reset_regs_svm(vcpu, vrs);
	else
		panic("%s: unknown vmm mode: %d", __func__, vmm_softc->mode);

	return (ret);
}

/*
 * vcpu_init_svm
 *
 * AMD SVM specific VCPU initialization routine.
 *
 * This function allocates various per-VCPU memory regions, sets up initial
 * VCPU VMCB controls, and sets initial register values.
 *
 * Parameters:
 *  vcpu: the VCPU structure being initialized
 *  vcp: parameters provided by vmd(8)
 *
 * Return values:
 *  0: the VCPU was initialized successfully
 *  ENOMEM: insufficient resources
 *  EINVAL: an error occurred during VCPU initialization
 */
int
vcpu_init_svm(struct vcpu *vcpu, struct vm_create_params *vcp)
{
	int ret = 0;

	/* Shall we enable SEV/SEV-ES? */
	vcpu->vc_sev = vcp->vcp_sev;
	vcpu->vc_seves = vcp->vcp_seves;

	/* Allocate an ASID early to avoid km_alloc if we're out of ASIDs. */
	if (vmm_alloc_asid(&vcpu->vc_vpid, vcpu))
		return (ENOMEM);

	/* Allocate VMCB VA */
	vcpu->vc_control_va = (vaddr_t)km_alloc(PAGE_SIZE, &kv_page, &kp_zero,
	    &kd_waitok);

	if (!vcpu->vc_control_va) {
		ret = ENOMEM;
		goto exit;
	}

	/* Compute VMCB PA */
	if (!pmap_extract(pmap_kernel(), vcpu->vc_control_va,
	    (paddr_t *)&vcpu->vc_control_pa)) {
		ret = ENOMEM;
		goto exit;
	}

	DPRINTF("%s: VMCB va @ 0x%llx, pa @ 0x%llx\n", __func__,
	    (uint64_t)vcpu->vc_control_va,
	    (uint64_t)vcpu->vc_control_pa);


	/* Allocate MSR bitmap VA (2 pages) */
	vcpu->vc_msr_bitmap_va = (vaddr_t)km_alloc(2 * PAGE_SIZE, &kv_any,
	    &vmm_kp_contig, &kd_waitok);

	if (!vcpu->vc_msr_bitmap_va) {
		ret = ENOMEM;
		goto exit;
	}

	/* Compute MSR bitmap PA */
	if (!pmap_extract(pmap_kernel(), vcpu->vc_msr_bitmap_va,
	    (paddr_t *)&vcpu->vc_msr_bitmap_pa)) {
		ret = ENOMEM;
		goto exit;
	}

	DPRINTF("%s: MSR bitmap va @ 0x%llx, pa @ 0x%llx\n", __func__,
	    (uint64_t)vcpu->vc_msr_bitmap_va,
	    (uint64_t)vcpu->vc_msr_bitmap_pa);

	/* Allocate host state area VA */
	vcpu->vc_svm_hsa_va = (vaddr_t)km_alloc(PAGE_SIZE, &kv_page,
	   &kp_zero, &kd_waitok);

	if (!vcpu->vc_svm_hsa_va) {
		ret = ENOMEM;
		goto exit;
	}

	/* Compute host state area PA */
	if (!pmap_extract(pmap_kernel(), vcpu->vc_svm_hsa_va,
	    &vcpu->vc_svm_hsa_pa)) {
		ret = ENOMEM;
		goto exit;
	}

	DPRINTF("%s: HSA va @ 0x%llx, pa @ 0x%llx\n", __func__,
	    (uint64_t)vcpu->vc_svm_hsa_va,
	    (uint64_t)vcpu->vc_svm_hsa_pa);

	/* Allocate IOIO area VA (3 pages) */
	vcpu->vc_svm_ioio_va = (vaddr_t)km_alloc(3 * PAGE_SIZE, &kv_any,
	   &vmm_kp_contig, &kd_waitok);

	if (!vcpu->vc_svm_ioio_va) {
		ret = ENOMEM;
		goto exit;
	}

	/* Compute IOIO area PA */
	if (!pmap_extract(pmap_kernel(), vcpu->vc_svm_ioio_va,
	    &vcpu->vc_svm_ioio_pa)) {
		ret = ENOMEM;
		goto exit;
	}

	DPRINTF("%s: IOIO va @ 0x%llx, pa @ 0x%llx\n", __func__,
	    (uint64_t)vcpu->vc_svm_ioio_va,
	    (uint64_t)vcpu->vc_svm_ioio_pa);

	if (vcpu->vc_seves) {
		/* Allocate VM save area VA */
		vcpu->vc_svm_vmsa_va = (vaddr_t)km_alloc(PAGE_SIZE, &kv_page,
		   &kp_zero, &kd_waitok);

		if (!vcpu->vc_svm_vmsa_va) {
			ret = ENOMEM;
			goto exit;
		}

		/* Compute VM save area PA */
		if (!pmap_extract(pmap_kernel(), vcpu->vc_svm_vmsa_va,
		    &vcpu->vc_svm_vmsa_pa)) {
			ret = ENOMEM;
			goto exit;
		}

		DPRINTF("%s: VMSA va @ 0x%llx, pa @ 0x%llx\n", __func__,
		    (uint64_t)vcpu->vc_svm_vmsa_va,
		    (uint64_t)vcpu->vc_svm_vmsa_pa);
	}

	/* Inform vmd(8) about ASID and C bit position. */
	vcp->vcp_poscbit = amd64_pos_cbit;
	vcp->vcp_asid[vcpu->vc_id] = vcpu->vc_vpid;

exit:
	if (ret)
		vcpu_deinit_svm(vcpu);

	return (ret);
}

/*
 * vcpu_init
 *
 * Calls the architecture-specific VCPU init routine
 */
int
vcpu_init(struct vcpu *vcpu, struct vm_create_params *vcp)
{
	int ret = 0;

	vcpu->vc_virt_mode = vmm_softc->mode;
	vcpu->vc_state = VCPU_STATE_STOPPED;
	vcpu->vc_vpid = 0;
	vcpu->vc_pvclock_system_gpa = 0;
	vcpu->vc_last_pcpu = NULL;

	rw_init(&vcpu->vc_lock, "vcpu");

	/* Shadow PAT MSR, starting with host's value. */
	vcpu->vc_shadow_pat = rdmsr(MSR_CR_PAT);

	if (vmm_softc->mode == VMM_MODE_EPT)
		ret = vcpu_init_vmx(vcpu);
	else if (vmm_softc->mode == VMM_MODE_RVI)
		ret = vcpu_init_svm(vcpu, vcp);
	else
		panic("%s: unknown vmm mode: %d", __func__, vmm_softc->mode);

	return (ret);
}

/*
 * vcpu_deinit_vmx
 *
 * Deinitializes the vcpu described by 'vcpu'
 *
 * Parameters:
 *  vcpu: the vcpu to be deinited
 */
void
vcpu_deinit_vmx(struct vcpu *vcpu)
{
	if (vcpu->vc_control_va) {
		km_free((void *)vcpu->vc_control_va, PAGE_SIZE,
		    &kv_page, &kp_zero);
		vcpu->vc_control_va = 0;
	}
	if (vcpu->vc_vmx_msr_exit_save_va) {
		km_free((void *)vcpu->vc_vmx_msr_exit_save_va,
		    PAGE_SIZE, &kv_page, &kp_zero);
		vcpu->vc_vmx_msr_exit_save_va = 0;
	}
	if (vcpu->vc_vmx_msr_exit_load_va) {
		km_free((void *)vcpu->vc_vmx_msr_exit_load_va,
		    PAGE_SIZE, &kv_page, &kp_zero);
		vcpu->vc_vmx_msr_exit_load_va = 0;
	}
#if 0
	if (vcpu->vc_vmx_msr_entry_load_va) {
		km_free((void *)vcpu->vc_vmx_msr_entry_load_va,
		    PAGE_SIZE, &kv_page, &kp_zero);
		vcpu->vc_vmx_msr_entry_load_va = 0;
	}
#endif

	vmm_free_vpid(vcpu->vc_vpid);
}

/*
 * vcpu_deinit_svm
 *
 * Deinitializes the vcpu described by 'vcpu'
 *
 * Parameters:
 *  vcpu: the vcpu to be deinited
 */
void
vcpu_deinit_svm(struct vcpu *vcpu)
{
	if (vcpu->vc_control_va) {
		km_free((void *)vcpu->vc_control_va, PAGE_SIZE, &kv_page,
		    &kp_zero);
		vcpu->vc_control_va = 0;
	}
	if (vcpu->vc_msr_bitmap_va) {
		km_free((void *)vcpu->vc_msr_bitmap_va, 2 * PAGE_SIZE, &kv_any,
		    &vmm_kp_contig);
		vcpu->vc_msr_bitmap_va = 0;
	}
	if (vcpu->vc_svm_hsa_va) {
		km_free((void *)vcpu->vc_svm_hsa_va, PAGE_SIZE, &kv_page,
		    &kp_zero);
		vcpu->vc_svm_hsa_va = 0;
	}
	if (vcpu->vc_svm_vmsa_va) {
		km_free((void *)vcpu->vc_svm_vmsa_va, PAGE_SIZE, &kv_page,
		    &kp_zero);
		vcpu->vc_svm_vmsa_va = 0;
	}
	if (vcpu->vc_svm_ioio_va) {
		km_free((void *)vcpu->vc_svm_ioio_va, 3 * PAGE_SIZE, &kv_any,
		    &vmm_kp_contig);
		vcpu->vc_svm_ioio_va = 0;
	}

	vmm_free_vpid(vcpu->vc_vpid);
}

/*
 * vcpu_deinit
 *
 * Calls the architecture-specific VCPU deinit routine
 *
 * Parameters:
 *  vcpu: the vcpu to be deinited
 */
void
vcpu_deinit(struct vcpu *vcpu)
{
	if (vmm_softc->mode == VMM_MODE_EPT)
		vcpu_deinit_vmx(vcpu);
	else if	(vmm_softc->mode == VMM_MODE_RVI)
		vcpu_deinit_svm(vcpu);
	else
		panic("%s: unknown vmm mode: %d", __func__, vmm_softc->mode);
}

/*
 * vcpu_vmx_check_cap
 *
 * Checks if the 'cap' bit in the 'msr' MSR can be set or cleared (set = 1
 * or set = 0, respectively).
 *
 * When considering 'msr', we check to see if true controls are available,
 * and use those if so.
 *
 * Returns 1 of 'cap' can be set/cleared as requested, 0 otherwise.
 */
int
vcpu_vmx_check_cap(struct vcpu *vcpu, uint32_t msr, uint32_t cap, int set)
{
	uint64_t ctl;

	if (vcpu->vc_vmx_basic & IA32_VMX_TRUE_CTLS_AVAIL) {
		switch (msr) {
		case IA32_VMX_PINBASED_CTLS:
			ctl = vcpu->vc_vmx_true_pinbased_ctls;
			break;
		case IA32_VMX_PROCBASED_CTLS:
			ctl = vcpu->vc_vmx_true_procbased_ctls;
			break;
		case IA32_VMX_PROCBASED2_CTLS:
			ctl = vcpu->vc_vmx_procbased2_ctls;
			break;
		case IA32_VMX_ENTRY_CTLS:
			ctl = vcpu->vc_vmx_true_entry_ctls;
			break;
		case IA32_VMX_EXIT_CTLS:
			ctl = vcpu->vc_vmx_true_exit_ctls;
			break;
		default:
			return (0);
		}
	} else {
		switch (msr) {
		case IA32_VMX_PINBASED_CTLS:
			ctl = vcpu->vc_vmx_pinbased_ctls;
			break;
		case IA32_VMX_PROCBASED_CTLS:
			ctl = vcpu->vc_vmx_procbased_ctls;
			break;
		case IA32_VMX_PROCBASED2_CTLS:
			ctl = vcpu->vc_vmx_procbased2_ctls;
			break;
		case IA32_VMX_ENTRY_CTLS:
			ctl = vcpu->vc_vmx_entry_ctls;
			break;
		case IA32_VMX_EXIT_CTLS:
			ctl = vcpu->vc_vmx_exit_ctls;
			break;
		default:
			return (0);
		}
	}

	if (set) {
		/* Check bit 'cap << 32', must be !0 */
		return (ctl & ((uint64_t)cap << 32)) != 0;
	} else {
		/* Check bit 'cap', must be 0 */
		return (ctl & cap) == 0;
	}
}

/*
 * vcpu_vmx_compute_ctrl
 *
 * Computes the appropriate control value, given the supplied parameters
 * and CPU capabilities.
 *
 * Intel has made somewhat of a mess of this computation - it is described
 * using no fewer than three different approaches, spread across many
 * pages of the SDM. Further compounding the problem is the fact that now
 * we have "true controls" for each type of "control", and each needs to
 * be examined to get the calculation right, but only if "true" controls
 * are present on the CPU we're on.
 *
 * Parameters:
 *  ctrlval: the control value, as read from the CPU MSR
 *  ctrl: which control is being set (eg, pinbased, procbased, etc)
 *  want0: the set of desired 0 bits
 *  want1: the set of desired 1 bits
 *  out: (out) the correct value to write into the VMCS for this VCPU,
 *      for the 'ctrl' desired.
 *
 * Returns 0 if successful, or EINVAL if the supplied parameters define
 *     an unworkable control setup.
 */
int
vcpu_vmx_compute_ctrl(uint64_t ctrlval, uint16_t ctrl, uint32_t want1,
	uint32_t want0, uint32_t *out)
{
	int i, set, clear;

	*out = 0;

	/*
	 * The Intel SDM gives three formulae for determining which bits to
	 * set/clear for a given control and desired functionality. Formula
	 * 1 is the simplest but disallows use of newer features that are
	 * enabled by functionality in later CPUs.
	 *
	 * Formulas 2 and 3 allow such extra functionality. We use formula
	 * 2 - this requires us to know the identity of controls in the
	 * "default1" class for each control register, but allows us to not
	 * have to pass along and/or query both sets of capability MSRs for
	 * each control lookup. This makes the code slightly longer,
	 * however.
	 */
	for (i = 0; i < 32; i++) {
		/* Figure out if we can set and / or clear this bit */
		set = (ctrlval & (1ULL << (i + 32))) != 0;
		clear = ((1ULL << i) & ((uint64_t)ctrlval)) == 0;

		/* If the bit can't be set nor cleared, something's wrong */
		if (!set && !clear)
			return (EINVAL);

		/*
		 * Formula 2.c.i - "If the relevant VMX capability MSR
		 * reports that a control has a single setting, use that
		 * setting."
		 */
		if (set && !clear) {
			if (want0 & (1ULL << i))
				return (EINVAL);
			else
				*out |= (1ULL << i);
		} else if (clear && !set) {
			if (want1 & (1ULL << i))
				return (EINVAL);
			else
				*out &= ~(1ULL << i);
		} else {
			/*
			 * 2.c.ii - "If the relevant VMX capability MSR
			 * reports that a control can be set to 0 or 1
			 * and that control's meaning is known to the VMM,
			 * set the control based on the functionality desired."
			 */
			if (want1 & (1ULL << i))
				*out |= (1ULL << i);
			else if (want0 & (1 << i))
				*out &= ~(1ULL << i);
			else {
				/*
				 * ... assuming the control's meaning is not
				 * known to the VMM ...
				 *
				 * 2.c.iii - "If the relevant VMX capability
				 * MSR reports that a control can be set to 0
				 * or 1 and the control is not in the default1
				 * class, set the control to 0."
				 *
				 * 2.c.iv - "If the relevant VMX capability
				 * MSR reports that a control can be set to 0
				 * or 1 and the control is in the default1
				 * class, set the control to 1."
				 */
				switch (ctrl) {
				case IA32_VMX_PINBASED_CTLS:
				case IA32_VMX_TRUE_PINBASED_CTLS:
					/*
					 * A.3.1 - default1 class of pinbased
					 * controls comprises bits 1,2,4
					 */
					switch (i) {
						case 1:
						case 2:
						case 4:
							*out |= (1ULL << i);
							break;
						default:
							*out &= ~(1ULL << i);
							break;
					}
					break;
				case IA32_VMX_PROCBASED_CTLS:
				case IA32_VMX_TRUE_PROCBASED_CTLS:
					/*
					 * A.3.2 - default1 class of procbased
					 * controls comprises bits 1, 4-6, 8,
					 * 13-16, 26
					 */
					switch (i) {
						case 1:
						case 4 ... 6:
						case 8:
						case 13 ... 16:
						case 26:
							*out |= (1ULL << i);
							break;
						default:
							*out &= ~(1ULL << i);
							break;
					}
					break;
					/*
					 * Unknown secondary procbased controls
					 * can always be set to 0
					 */
				case IA32_VMX_PROCBASED2_CTLS:
					*out &= ~(1ULL << i);
					break;
				case IA32_VMX_EXIT_CTLS:
				case IA32_VMX_TRUE_EXIT_CTLS:
					/*
					 * A.4 - default1 class of exit
					 * controls comprises bits 0-8, 10,
					 * 11, 13, 14, 16, 17
					 */
					switch (i) {
						case 0 ... 8:
						case 10 ... 11:
						case 13 ... 14:
						case 16 ... 17:
							*out |= (1ULL << i);
							break;
						default:
							*out &= ~(1ULL << i);
							break;
					}
					break;
				case IA32_VMX_ENTRY_CTLS:
				case IA32_VMX_TRUE_ENTRY_CTLS:
					/*
					 * A.5 - default1 class of entry
					 * controls comprises bits 0-8, 12
					 */
					switch (i) {
						case 0 ... 8:
						case 12:
							*out |= (1ULL << i);
							break;
						default:
							*out &= ~(1ULL << i);
							break;
					}
					break;
				}
			}
		}
	}

	return (0);
}

/*
 * vm_run
 *
 * Run the vm / vcpu specified by 'vrp'
 *
 * Parameters:
 *  vrp: structure defining the VM to run
 *
 * Return value:
 *  ENOENT: the VM defined in 'vrp' could not be located
 *  EBUSY: the VM defined in 'vrp' is already running
 *  EFAULT: error copying data from userspace (vmd) on return from previous
 *      exit.
 *  EAGAIN: help is needed from vmd(8) (device I/O or exit vmm(4) cannot
 *      handle in-kernel.)
 *  0: the run loop exited and no help is needed from vmd(8)
 */
int
vm_run(struct vm_run_params *vrp)
{
	struct vm *vm;
	struct vcpu *vcpu;
	int ret = 0;
	u_int old, next;

	/*
	 * Find desired VM
	 */
	ret = vm_find(vrp->vrp_vm_id, &vm);
	if (ret)
		return (ret);

	vcpu = vm_find_vcpu(vm, vrp->vrp_vcpu_id);
	if (vcpu == NULL) {
		ret = ENOENT;
		goto out;
	}

	/*
	 * Attempt to transition from VCPU_STATE_STOPPED -> VCPU_STATE_RUNNING.
	 * Failure to make the transition indicates the VCPU is busy.
	 */
	rw_enter_write(&vcpu->vc_lock);
	old = VCPU_STATE_STOPPED;
	next = VCPU_STATE_RUNNING;
	if (atomic_cas_uint(&vcpu->vc_state, old, next) != old) {
		ret = EBUSY;
		goto out_unlock;
	}

	/*
	 * We may be returning from userland helping us from the last
	 * exit. Copy in the exit data from vmd. The exit data will be
	 * consumed before the next entry (this typically comprises
	 * VCPU register changes as the result of vmd(8)'s actions).
	 */
	ret = copyin(vrp->vrp_exit, &vcpu->vc_exit, sizeof(struct vm_exit));
	if (ret)
		goto out_unlock;

	vcpu->vc_inject.vie_type = vrp->vrp_inject.vie_type;
	vcpu->vc_inject.vie_vector = vrp->vrp_inject.vie_vector;
	vcpu->vc_inject.vie_errorcode = vrp->vrp_inject.vie_errorcode;

	WRITE_ONCE(vcpu->vc_curcpu, curcpu());
	/* Run the VCPU specified in vrp */
	if (vcpu->vc_virt_mode == VMM_MODE_EPT) {
		ret = vcpu_run_vmx(vcpu, vrp);
	} else if (vcpu->vc_virt_mode == VMM_MODE_RVI) {
		ret = vcpu_run_svm(vcpu, vrp);
	}
	WRITE_ONCE(vcpu->vc_curcpu, NULL);

	if (ret == 0 || ret == EAGAIN) {
		/* If we are exiting, populate exit data so vmd can help. */
		vrp->vrp_exit_reason = (ret == 0) ? VM_EXIT_NONE
		    : vcpu->vc_gueststate.vg_exit_reason;
		vrp->vrp_irqready = vcpu->vc_irqready;
		vcpu->vc_state = VCPU_STATE_STOPPED;

		if (copyout(&vcpu->vc_exit, vrp->vrp_exit,
		    sizeof(struct vm_exit)) == EFAULT) {
			ret = EFAULT;
		} else
			ret = 0;
	} else {
		vrp->vrp_exit_reason = VM_EXIT_TERMINATED;
		vcpu->vc_state = VCPU_STATE_TERMINATED;
	}
out_unlock:
	rw_exit_write(&vcpu->vc_lock);
out:
	refcnt_rele_wake(&vm->vm_refcnt);
	return (ret);
}

/*
 * vmm_fpurestore
 *
 * Restore the guest's FPU state, saving the existing userland thread's
 * FPU context if necessary.  Must be called with interrupts disabled.
 */
int
vmm_fpurestore(struct vcpu *vcpu)
{
	struct cpu_info *ci = curcpu();

	rw_assert_wrlock(&vcpu->vc_lock);

	/* save vmm's FPU state if we haven't already */
	if (ci->ci_pflags & CPUPF_USERXSTATE) {
		ci->ci_pflags &= ~CPUPF_USERXSTATE;
		fpusavereset(&curproc->p_addr->u_pcb.pcb_savefpu);
	}

	if (vcpu->vc_fpuinited)
		xrstor_kern(&vcpu->vc_g_fpu, xsave_mask);

	if (xsave_mask) {
		/* Restore guest %xcr0 */
		if (xsetbv_user(0, vcpu->vc_gueststate.vg_xcr0)) {
			DPRINTF("%s: guest attempted to set invalid bits in "
			    "xcr0 (guest %%xcr0=0x%llx, host %%xcr0=0x%llx)\n",
			    __func__, vcpu->vc_gueststate.vg_xcr0, xsave_mask);
			return EINVAL;
		}
	}

	return 0;
}

/*
 * vmm_fpusave
 *
 * Save the guest's FPU state.  Must be called with interrupts disabled.
 */
void
vmm_fpusave(struct vcpu *vcpu)
{
	rw_assert_wrlock(&vcpu->vc_lock);

	if (xsave_mask) {
		/* Save guest %xcr0 */
		vcpu->vc_gueststate.vg_xcr0 = xgetbv(0);

		/* Restore host %xcr0 */
		xsetbv(0, xsave_mask & XFEATURE_XCR0_MASK);
	}

	/*
	 * Save a copy of FPU state - guest content is always
	 * a subset of host's save area. PKRU is saved separately.
	 */
	fpusavereset(&vcpu->vc_g_fpu);
	vcpu->vc_fpuinited = 1;
}

/*
 * vmm_translate_gva
 *
 * Translates a guest virtual address to a guest physical address by walking
 * the currently active page table (if needed).
 *
 * Note - this function can possibly alter the supplied VCPU state.
 *  Specifically, it may inject exceptions depending on the current VCPU
 *  configuration, and may alter %cr2 on #PF. Consequently, this function
 *  should only be used as part of instruction emulation.
 *
 * Parameters:
 *  vcpu: The VCPU this translation should be performed for (guest MMU settings
 *   are gathered from this VCPU)
 *  va: virtual address to translate
 *  pa: pointer to paddr_t variable that will receive the translated physical
 *   address. 'pa' is unchanged on error.
 *  mode: one of PROT_READ, PROT_WRITE, PROT_EXEC indicating the mode in which
 *   the address should be translated
 *
 * Return values:
 *  0: the address was successfully translated - 'pa' contains the physical
 *     address currently mapped by 'va'.
 *  EFAULT: the PTE for 'VA' is unmapped. A #PF will be injected in this case
 *     and %cr2 set in the vcpu structure.
 *  EINVAL: an error occurred reading paging table structures
 */
int
vmm_translate_gva(struct vcpu *vcpu, uint64_t va, uint64_t *pa, int mode)
{
	int level, shift, pdidx;
	uint64_t pte, pt_paddr, pte_paddr, mask, low_mask, high_mask;
	uint64_t shift_width, pte_size, *hva;
	paddr_t hpa;
	struct vcpu_reg_state vrs;

	level = 0;

	if (vmm_softc->mode == VMM_MODE_EPT) {
		if (vcpu_readregs_vmx(vcpu, VM_RWREGS_ALL, 1, &vrs))
			return (EINVAL);
	} else if (vmm_softc->mode == VMM_MODE_RVI) {
		if (vcpu_readregs_svm(vcpu, VM_RWREGS_ALL, &vrs))
			return (EINVAL);
	} else {
		printf("%s: unknown vmm mode", __func__);
		return (EINVAL);
	}

	DPRINTF("%s: guest %%cr0=0x%llx, %%cr3=0x%llx\n", __func__,
	    vrs.vrs_crs[VCPU_REGS_CR0], vrs.vrs_crs[VCPU_REGS_CR3]);

	if (!(vrs.vrs_crs[VCPU_REGS_CR0] & CR0_PG)) {
		DPRINTF("%s: unpaged, va=pa=0x%llx\n", __func__,
		    va);
		*pa = va;
		return (0);
	}

	pt_paddr = vrs.vrs_crs[VCPU_REGS_CR3];

	if (vrs.vrs_crs[VCPU_REGS_CR0] & CR0_PE) {
		if (vrs.vrs_crs[VCPU_REGS_CR4] & CR4_PAE) {
			pte_size = sizeof(uint64_t);
			shift_width = 9;

			if (vrs.vrs_msrs[VCPU_REGS_EFER] & EFER_LMA) {
				level = 4;
				mask = L4_MASK;
				shift = L4_SHIFT;
			} else {
				level = 3;
				mask = L3_MASK;
				shift = L3_SHIFT;
			}
		} else {
			level = 2;
			shift_width = 10;
			mask = 0xFFC00000;
			shift = 22;
			pte_size = sizeof(uint32_t);
		}
	} else {
		return (EINVAL);
	}

	DPRINTF("%s: pte size=%lld level=%d mask=0x%llx, shift=%d, "
	    "shift_width=%lld\n", __func__, pte_size, level, mask, shift,
	    shift_width);

	/* XXX: Check for R bit in segment selector and set A bit */

	for (;level > 0; level--) {
		pdidx = (va & mask) >> shift;
		pte_paddr = (pt_paddr) + (pdidx * pte_size);

		DPRINTF("%s: read pte level %d @ GPA 0x%llx\n", __func__,
		    level, pte_paddr);
		if (!pmap_extract(vcpu->vc_parent->vm_pmap, pte_paddr,
		    &hpa)) {
			DPRINTF("%s: cannot extract HPA for GPA 0x%llx\n",
			    __func__, pte_paddr);
			return (EINVAL);
		}

		hpa = hpa | (pte_paddr & 0xFFF);
		hva = (uint64_t *)PMAP_DIRECT_MAP(hpa);
		DPRINTF("%s: GPA 0x%llx -> HPA 0x%llx -> HVA 0x%llx\n",
		    __func__, pte_paddr, (uint64_t)hpa, (uint64_t)hva);
		if (pte_size == 8)
			pte = *hva;
		else
			pte = *(uint32_t *)hva;

		DPRINTF("%s: PTE @ 0x%llx = 0x%llx\n", __func__, pte_paddr,
		    pte);

		/* XXX: Set CR2  */
		if (!(pte & PG_V))
			return (EFAULT);

		/* XXX: Check for SMAP */
		if ((mode == PROT_WRITE) && !(pte & PG_RW))
			return (EPERM);

		if ((vcpu->vc_exit.cpl > 0) && !(pte & PG_u))
			return (EPERM);

		pte = pte | PG_U;
		if (mode == PROT_WRITE)
			pte = pte | PG_M;
		*hva = pte;

		/* XXX: EINVAL if in 32bit and PG_PS is 1 but CR4.PSE is 0 */
		if (pte & PG_PS)
			break;

		if (level > 1) {
			pt_paddr = pte & PG_FRAME;
			shift -= shift_width;
			mask = mask >> shift_width;
		}
	}

	low_mask = ((uint64_t)1ULL << shift) - 1;
	high_mask = (((uint64_t)1ULL << ((pte_size * 8) - 1)) - 1) ^ low_mask;
	*pa = (pte & high_mask) | (va & low_mask);

	DPRINTF("%s: final GPA for GVA 0x%llx = 0x%llx\n", __func__,
	    va, *pa);

	return (0);
}


/*
 * vcpu_run_vmx
 *
 * VMX main loop used to run a VCPU.
 *
 * Parameters:
 *  vcpu: The VCPU to run
 *  vrp: run parameters
 *
 * Return values:
 *  0: The run loop exited and no help is needed from vmd
 *  EAGAIN: The run loop exited and help from vmd is needed
 *  EINVAL: an error occurred
 */
int
vcpu_run_vmx(struct vcpu *vcpu, struct vm_run_params *vrp)
{
	int ret = 0, exitinfo;
	struct region_descriptor gdt;
	struct cpu_info *ci = NULL;
	uint64_t exit_reason, cr3, msr, insn_error;
	struct schedstate_percpu *spc;
	struct vmx_msr_store *msr_store;
	struct vmx_invvpid_descriptor vid;
	struct vmx_invept_descriptor vid_ept;
	uint64_t cr0, eii, procbased, int_st;
	u_long s;

	rw_assert_wrlock(&vcpu->vc_lock);

	if (vcpu_reload_vmcs_vmx(vcpu)) {
		printf("%s: failed (re)loading vmcs\n", __func__);
		return (EINVAL);
	}

	/*
	 * If we are returning from userspace (vmd) because we exited
	 * last time, fix up any needed vcpu state first. Which state
	 * needs to be fixed up depends on what vmd populated in the
	 * exit data structure.
	 */
	if (vrp->vrp_intr_pending)
		vcpu->vc_intr = 1;
	else
		vcpu->vc_intr = 0;

	switch (vcpu->vc_gueststate.vg_exit_reason) {
	case VMX_EXIT_IO:
		if (vcpu->vc_exit.vei.vei_dir == VEI_DIR_IN)
			vcpu->vc_gueststate.vg_rax = vcpu->vc_exit.vei.vei_data;
		vcpu->vc_gueststate.vg_rip =
		    vcpu->vc_exit.vrs.vrs_gprs[VCPU_REGS_RIP];
		if (vmwrite(VMCS_GUEST_IA32_RIP, vcpu->vc_gueststate.vg_rip)) {
			printf("%s: failed to update rip\n", __func__);
			return (EINVAL);
		}
		break;
	case VMX_EXIT_EPT_VIOLATION:
		ret = vcpu_writeregs_vmx(vcpu, VM_RWREGS_GPRS, 0,
		    &vcpu->vc_exit.vrs);
		if (ret) {
			printf("%s: vm %d vcpu %d failed to update registers\n",
			    __func__, vcpu->vc_parent->vm_id, vcpu->vc_id);
			return (EINVAL);
		}
		break;
	}
	memset(&vcpu->vc_exit, 0, sizeof(vcpu->vc_exit));

	/* Handle vmd(8) injected interrupts */
	/* Is there an interrupt pending injection? */
	if (vcpu->vc_inject.vie_type == VCPU_INJECT_INTR) {
		if (vmread(VMCS_GUEST_INTERRUPTIBILITY_ST, &int_st)) {
			printf("%s: can't get interruptibility state\n",
			    __func__);
			return (EINVAL);
		}

		/* Interruptibility state 0x3 covers NMIs and STI */
		if (!(int_st & 0x3) && vcpu->vc_irqready) {
			eii = (uint64_t)vcpu->vc_inject.vie_vector;
			eii |= (1ULL << 31);	/* Valid */
			if (vmwrite(VMCS_ENTRY_INTERRUPTION_INFO, eii)) {
				printf("vcpu_run_vmx: can't vector "
				    "interrupt to guest\n");
				return (EINVAL);
			}

			vcpu->vc_inject.vie_type = VCPU_INJECT_NONE;
		}
	} else if (!vcpu->vc_intr) {
		/*
		 * Disable window exiting
		 */
		if (vmread(VMCS_PROCBASED_CTLS, &procbased)) {
			printf("%s: can't read procbased ctls on exit\n",
			    __func__);
			return (EINVAL);
		} else {
			procbased &= ~IA32_VMX_INTERRUPT_WINDOW_EXITING;
			if (vmwrite(VMCS_PROCBASED_CTLS, procbased)) {
				printf("%s: can't write procbased ctls "
				    "on exit\n", __func__);
				return (EINVAL);
			}
		}
	}

	msr_store = (struct vmx_msr_store *)vcpu->vc_vmx_msr_exit_load_va;
	while (ret == 0) {
#ifdef VMM_DEBUG
		paddr_t pa = 0ULL;
		vmptrst(&pa);
		KASSERT(pa == vcpu->vc_control_pa);
#endif /* VMM_DEBUG */

		vmm_update_pvclock(vcpu);

		if (ci != curcpu()) {
			ci = curcpu();
			vcpu->vc_last_pcpu = ci;

			/* We're now using this vcpu's EPT pmap on this cpu. */
			atomic_swap_ptr(&ci->ci_ept_pmap,
			    vcpu->vc_parent->vm_pmap);

			/* Invalidate EPT cache. */
			vid_ept.vid_reserved = 0;
			vid_ept.vid_eptp = vcpu->vc_parent->vm_pmap->eptp;
			if (invept(ci->ci_vmm_cap.vcc_vmx.vmx_invept_mode,
			    &vid_ept)) {
				printf("%s: invept\n", __func__);
				return (EINVAL);
			}

			/* Host CR3 */
			cr3 = rcr3();
			if (vmwrite(VMCS_HOST_IA32_CR3, cr3)) {
				printf("%s: vmwrite(0x%04X, 0x%llx)\n", __func__,
				    VMCS_HOST_IA32_CR3, cr3);
				return (EINVAL);
			}

			setregion(&gdt, ci->ci_gdt, GDT_SIZE - 1);
			if (gdt.rd_base == 0) {
				printf("%s: setregion\n", __func__);
				return (EINVAL);
			}

			/* Host GDTR base */
			if (vmwrite(VMCS_HOST_IA32_GDTR_BASE, gdt.rd_base)) {
				printf("%s: vmwrite(0x%04X, 0x%llx)\n",
				    __func__, VMCS_HOST_IA32_GDTR_BASE,
				    gdt.rd_base);
				return (EINVAL);
			}

			/* Host TR base */
			if (vmwrite(VMCS_HOST_IA32_TR_BASE,
			    (uint64_t)ci->ci_tss)) {
				printf("%s: vmwrite(0x%04X, 0x%llx)\n",
				    __func__, VMCS_HOST_IA32_TR_BASE,
				    (uint64_t)ci->ci_tss);
				return (EINVAL);
			}

			/* Host GS.base (aka curcpu) */
			if (vmwrite(VMCS_HOST_IA32_GS_BASE, (uint64_t)ci)) {
				printf("%s: vmwrite(0x%04X, 0x%llx)\n",
				    __func__, VMCS_HOST_IA32_GS_BASE,
				    (uint64_t)ci);
				return (EINVAL);
			}

			/* Host FS.base */
			msr = rdmsr(MSR_FSBASE);
			if (vmwrite(VMCS_HOST_IA32_FS_BASE, msr)) {
				printf("%s: vmwrite(0x%04X, 0x%llx)\n",
				    __func__, VMCS_HOST_IA32_FS_BASE, msr);
				return (EINVAL);
			}

			/* Host KernelGS.base (userspace GS.base here) */
			msr_store[VCPU_HOST_REGS_KGSBASE].vms_data =
			    rdmsr(MSR_KERNELGSBASE);
		}

		/* Inject event if present */
		if (vcpu->vc_inject.vie_type == VCPU_INJECT_EX) {
			eii = (uint64_t)vcpu->vc_inject.vie_vector;
			eii |= (1ULL << 31);	/* Valid */

			switch (vcpu->vc_inject.vie_vector) {
			case VMM_EX_BP:
			case VMM_EX_OF:
				/* Software Exceptions */
				eii |= (4ULL << 8);
				break;
			case VMM_EX_DF:
			case VMM_EX_TS:
			case VMM_EX_NP:
			case VMM_EX_SS:
			case VMM_EX_GP:
			case VMM_EX_PF:
			case VMM_EX_AC:
				/* Hardware Exceptions */
				eii |= (3ULL << 8);
				cr0 = 0;
				if (vmread(VMCS_GUEST_IA32_CR0, &cr0)) {
					printf("%s: vmread(VMCS_GUEST_IA32_CR0)"
					    "\n", __func__);
					ret = EINVAL;
					break;
				}

				/* Don't set error codes if in real mode. */
				if (ret == EINVAL || !(cr0 & CR0_PE))
					break;
				eii |= (1ULL << 11);

				/* Enforce a 0 error code for #AC. */
				if (vcpu->vc_inject.vie_vector == VMM_EX_AC)
					vcpu->vc_inject.vie_errorcode = 0;
				/*
				 * XXX: Intel SDM says if IA32_VMX_BASIC[56] is
				 * set, error codes can be injected for hw
				 * exceptions with or without error code,
				 * regardless of vector. See Vol 3D. A1. Ignore
				 * this capability for now.
				 */
				if (vmwrite(VMCS_ENTRY_EXCEPTION_ERROR_CODE,
				    vcpu->vc_inject.vie_errorcode)) {
					printf("%s: can't write error code to "
					    "guest\n", __func__);
					ret = EINVAL;
				}
			} /* switch */
			if (ret == EINVAL)
				break;

			if (vmwrite(VMCS_ENTRY_INTERRUPTION_INFO, eii)) {
				printf("%s: can't vector event to guest\n",
				    __func__);
				ret = EINVAL;
				break;
			}
			vcpu->vc_inject.vie_type = VCPU_INJECT_NONE;
		}

		if (vcpu->vc_vmx_vpid_enabled) {
			/* Invalidate old TLB mappings */
			vid.vid_vpid = vcpu->vc_vpid;
			vid.vid_addr = 0;
			invvpid(IA32_VMX_INVVPID_SINGLE_CTX_GLB, &vid);
		}

		/* Start / resume the VCPU */

		/* Disable interrupts and save the current host FPU state. */
		s = intr_disable();
		if ((ret = vmm_fpurestore(vcpu))) {
			intr_restore(s);
			break;
		}

		TRACEPOINT(vmm, guest_enter, vcpu, vrp);

		/*
		 * If we're resuming to a different VCPU and have IBPB,
		 * then use it to prevent cross-VM branch-target injection.
		 */
		if (ci->ci_guest_vcpu != vcpu &&
		    (ci->ci_feature_sefflags_edx & SEFF0EDX_IBRS)) {
			wrmsr(MSR_PRED_CMD, PRED_CMD_IBPB);
			ci->ci_guest_vcpu = vcpu;
		}

		/* Restore any guest PKRU state. */
		if (vmm_softc->sc_md.pkru_enabled)
			wrpkru(0, vcpu->vc_pkru);

		ret = vmx_enter_guest(&vcpu->vc_control_pa,
		    &vcpu->vc_gueststate,
		    (vcpu->vc_vmx_vmcs_state == VMCS_LAUNCHED),
		    ci->ci_vmm_cap.vcc_vmx.vmx_has_l1_flush_msr);

		/* Restore host PKRU state. */
		if (vmm_softc->sc_md.pkru_enabled) {
			vcpu->vc_pkru = rdpkru(0);
			wrpkru(0, PGK_VALUE);
		}

		/*
		 * VM exit restores the GDT and IDT bases, but gives
		 * them high limits.  Reload with the correct limits here.
		 * 'gdt' is set above first time through and reset there
		 * whenever this thread switches CPU.
		 */
		bare_lgdt(&gdt);
		cpu_init_idt();

		/*
		 * On exit, interrupts are disabled, and we are running with
		 * the guest FPU state still possibly on the CPU. Save the FPU
		 * state before re-enabling interrupts.
		 */
		vmm_fpusave(vcpu);
		intr_restore(s);

		atomic_swap_uint(&vcpu->vc_vmx_vmcs_state, VMCS_LAUNCHED);
		exit_reason = VM_EXIT_NONE;

		/* If we exited successfully ... */
		if (ret == 0) {
			exitinfo = vmx_get_exit_info(
			    &vcpu->vc_gueststate.vg_rip, &exit_reason);
			if (!(exitinfo & VMX_EXIT_INFO_HAVE_RIP)) {
				printf("%s: cannot read guest rip\n", __func__);
				ret = EINVAL;
				break;
			}
			if (!(exitinfo & VMX_EXIT_INFO_HAVE_REASON)) {
				printf("%s: can't read exit reason\n",
				    __func__);
				ret = EINVAL;
				break;
			}
			vcpu->vc_gueststate.vg_exit_reason = exit_reason;
			TRACEPOINT(vmm, guest_exit, vcpu, vrp, exit_reason);

			/* Update our state */
			if (vmread(VMCS_GUEST_IA32_RFLAGS,
			    &vcpu->vc_gueststate.vg_rflags)) {
				printf("%s: can't read guest rflags during "
				    "exit\n", __func__);
				ret = EINVAL;
				break;
			}

			/*
			 * Handle the exit. This will alter "ret" to EAGAIN if
			 * the exit handler determines help from vmd is needed.
			 */
			ret = vmx_handle_exit(vcpu);

			if (vcpu->vc_gueststate.vg_rflags & PSL_I)
				vcpu->vc_irqready = 1;
			else
				vcpu->vc_irqready = 0;

			/*
			 * If not ready for interrupts, but interrupts pending,
			 * enable interrupt window exiting.
			 */
			if (vcpu->vc_irqready == 0 && vcpu->vc_intr) {
				if (vmread(VMCS_PROCBASED_CTLS, &procbased)) {
					printf("%s: can't read procbased ctls "
					    "on intwin exit\n", __func__);
					ret = EINVAL;
					break;
				}

				procbased |= IA32_VMX_INTERRUPT_WINDOW_EXITING;
				if (vmwrite(VMCS_PROCBASED_CTLS, procbased)) {
					printf("%s: can't write procbased ctls "
					    "on intwin exit\n", __func__);
					ret = EINVAL;
					break;
				}
			}

			/*
			 * Exit to vmd if we are terminating, failed to enter,
			 * or need help (device I/O)
			 */
			if (ret || vcpu_must_stop(vcpu))
				break;

			if (vcpu->vc_intr && vcpu->vc_irqready) {
				ret = EAGAIN;
				break;
			}

			/* Check if we should yield - don't hog the {p,v}pu */
			spc = &ci->ci_schedstate;
			if (spc->spc_schedflags & SPCF_SHOULDYIELD)
				break;

		} else {
			/*
			 * We failed vmresume or vmlaunch for some reason,
			 * typically due to invalid vmcs state or other
			 * reasons documented in SDM Vol 3C 30.4.
			 */
			switch (ret) {
			case VMX_FAIL_LAUNCH_INVALID_VMCS:
				printf("%s: failed %s with invalid vmcs\n",
				    __func__,
				    (vcpu->vc_vmx_vmcs_state == VMCS_LAUNCHED
					? "vmresume" : "vmlaunch"));
				break;
			case VMX_FAIL_LAUNCH_VALID_VMCS:
				printf("%s: failed %s with valid vmcs\n",
				    __func__,
				    (vcpu->vc_vmx_vmcs_state == VMCS_LAUNCHED
					? "vmresume" : "vmlaunch"));
				break;
			default:
				printf("%s: failed %s for unknown reason\n",
				    __func__,
				    (vcpu->vc_vmx_vmcs_state == VMCS_LAUNCHED
					? "vmresume" : "vmlaunch"));
			}

			ret = EINVAL;

			/* Try to translate a vmfail error code, if possible. */
			if (vmread(VMCS_INSTRUCTION_ERROR, &insn_error)) {
				printf("%s: can't read insn error field\n",
				    __func__);
			} else
				printf("%s: error code = %lld, %s\n", __func__,
				    insn_error,
				    vmx_instruction_error_decode(insn_error));
#ifdef VMM_DEBUG
			vmx_vcpu_dump_regs(vcpu);
			dump_vcpu(vcpu);
#endif /* VMM_DEBUG */
		}
	}

	vcpu->vc_last_pcpu = curcpu();

	/* Copy the VCPU register state to the exit structure */
	if (vcpu_readregs_vmx(vcpu, VM_RWREGS_ALL, 0, &vcpu->vc_exit.vrs))
		ret = EINVAL;
	vcpu->vc_exit.cpl = vmm_get_guest_cpu_cpl(vcpu);

	return (ret);
}

/*
 * vmx_handle_intr
 *
 * Handle host (external) interrupts. We read which interrupt fired by
 * extracting the vector from the VMCS and dispatch the interrupt directly
 * to the host using vmm_dispatch_intr.
 */
void
vmx_handle_intr(struct vcpu *vcpu)
{
	uint8_t vec;
	uint64_t eii;
	struct gate_descriptor *idte;
	vaddr_t handler;

	if (vmread(VMCS_EXIT_INTERRUPTION_INFO, &eii)) {
		printf("%s: can't obtain intr info\n", __func__);
		return;
	}

	vec = eii & 0xFF;

	/* XXX check "error valid" code in eii, abort if 0 */
	idte=&idt[vec];
	handler = idte->gd_looffset + ((uint64_t)idte->gd_hioffset << 16);
	vmm_dispatch_intr(handler);
}

/*
 * svm_handle_hlt
 *
 * Handle HLT exits
 *
 * Parameters
 *  vcpu: The VCPU that executed the HLT instruction
 *
 * Return Values:
 *  EIO: The guest halted with interrupts disabled
 *  EAGAIN: Normal return to vmd - vmd should halt scheduling this VCPU
 *   until a virtual interrupt is ready to inject
 */
int
svm_handle_hlt(struct vcpu *vcpu)
{
	struct vmcb *vmcb = (struct vmcb *)vcpu->vc_control_va;
	uint64_t rflags = vmcb->v_rflags;

	/* All HLT insns are 1 byte */
	vcpu->vc_gueststate.vg_rip += 1;

	if (!svm_get_iflag(vcpu, rflags)) {
		DPRINTF("%s: guest halted with interrupts disabled\n",
		    __func__);
		return (EIO);
	}

	return (EAGAIN);
}

/*
 * vmx_handle_hlt
 *
 * Handle HLT exits. HLTing the CPU with interrupts disabled will terminate
 * the guest (no NMIs handled) by returning EIO to vmd.
 *
 * Parameters:
 *  vcpu: The VCPU that executed the HLT instruction
 *
 * Return Values:
 *  EINVAL: An error occurred extracting information from the VMCS, or an
 *   invalid HLT instruction was encountered
 *  EIO: The guest halted with interrupts disabled
 *  EAGAIN: Normal return to vmd - vmd should halt scheduling this VCPU
 *   until a virtual interrupt is ready to inject
 *
 */
int
vmx_handle_hlt(struct vcpu *vcpu)
{
	uint64_t insn_length, rflags;

	if (vmread(VMCS_INSTRUCTION_LENGTH, &insn_length)) {
		printf("%s: can't obtain instruction length\n", __func__);
		return (EINVAL);
	}

	if (vmread(VMCS_GUEST_IA32_RFLAGS, &rflags)) {
		printf("%s: can't obtain guest rflags\n", __func__);
		return (EINVAL);
	}

	if (insn_length != 1) {
		DPRINTF("%s: HLT with instruction length %lld not supported\n",
		    __func__, insn_length);
		return (EINVAL);
	}

	if (!(rflags & PSL_I)) {
		DPRINTF("%s: guest halted with interrupts disabled\n",
		    __func__);
		return (EIO);
	}

	vcpu->vc_gueststate.vg_rip += insn_length;
	return (EAGAIN);
}

/*
 * vmx_get_exit_info
 *
 * Returns exit information containing the current guest RIP and exit reason
 * in rip and exit_reason. The return value is a bitmask indicating whether
 * reading the RIP and exit reason was successful.
 */
int
vmx_get_exit_info(uint64_t *rip, uint64_t *exit_reason)
{
	int rv = 0;

	if (vmread(VMCS_GUEST_IA32_RIP, rip) == 0) {
		rv |= VMX_EXIT_INFO_HAVE_RIP;
		if (vmread(VMCS_EXIT_REASON, exit_reason) == 0)
			rv |= VMX_EXIT_INFO_HAVE_REASON;
	}
	return (rv);
}

/*
 * svm_handle_exit
 *
 * Handle exits from the VM by decoding the exit reason and calling various
 * subhandlers as needed.
 */
int
svm_handle_exit(struct vcpu *vcpu)
{
	uint64_t exit_reason, rflags;
	int update_rip, ret = 0;
	struct vmcb *vmcb = (struct vmcb *)vcpu->vc_control_va;

	update_rip = 0;
	exit_reason = vcpu->vc_gueststate.vg_exit_reason;
	rflags = vcpu->vc_gueststate.vg_rflags;

	switch (exit_reason) {
	case SVM_VMEXIT_VINTR:
		if (!svm_get_iflag(vcpu, rflags)) {
			DPRINTF("%s: impossible interrupt window exit "
			    "config\n", __func__);
			ret = EINVAL;
			break;
		}

		/*
		 * Guest is now ready for interrupts, so disable interrupt
		 * window exiting.
		 */
		vmcb->v_irq = 0;
		vmcb->v_intr_vector = 0;
		vmcb->v_intercept1 &= ~SVM_INTERCEPT_VINTR;
		svm_set_dirty(vcpu, SVM_CLEANBITS_TPR | SVM_CLEANBITS_I);

		update_rip = 0;
		break;
	case SVM_VMEXIT_INTR:
		update_rip = 0;
		break;
	case SVM_VMEXIT_SHUTDOWN:
		update_rip = 0;
		ret = EAGAIN;
		break;
	case SVM_VMEXIT_NPF:
		ret = svm_handle_np_fault(vcpu);
		break;
	case SVM_VMEXIT_CPUID:
		ret = vmm_handle_cpuid(vcpu);
		update_rip = 1;
		break;
	case SVM_VMEXIT_MSR:
		ret = svm_handle_msr(vcpu);
		update_rip = 1;
		break;
	case SVM_VMEXIT_XSETBV:
		ret = svm_handle_xsetbv(vcpu);
		update_rip = 1;
		break;
	case SVM_VMEXIT_IOIO:
		if (svm_handle_inout(vcpu) == 0)
			ret = EAGAIN;
		break;
	case SVM_VMEXIT_HLT:
		ret = svm_handle_hlt(vcpu);
		update_rip = 1;
		break;
	case SVM_VMEXIT_MWAIT:
	case SVM_VMEXIT_MWAIT_CONDITIONAL:
	case SVM_VMEXIT_MONITOR:
	case SVM_VMEXIT_VMRUN:
	case SVM_VMEXIT_VMMCALL:
	case SVM_VMEXIT_VMLOAD:
	case SVM_VMEXIT_VMSAVE:
	case SVM_VMEXIT_STGI:
	case SVM_VMEXIT_CLGI:
	case SVM_VMEXIT_SKINIT:
	case SVM_VMEXIT_RDTSCP:
	case SVM_VMEXIT_ICEBP:
	case SVM_VMEXIT_INVLPGA:
		ret = vmm_inject_ud(vcpu);
		update_rip = 0;
		break;
	case SVM_VMEXIT_EFER_WRITE_TRAP:
	case SVM_VMEXIT_CR0_WRITE_TRAP:
	case SVM_VMEXIT_CR4_WRITE_TRAP:
		ret = svm_handle_efercr(vcpu, exit_reason);
		update_rip = 0;
		break;
	case SVM_VMEXIT_VMGEXIT:
		ret = svm_handle_vmgexit(vcpu);
		break;
	default:
		DPRINTF("%s: unhandled exit 0x%llx (pa=0x%llx)\n", __func__,
		    exit_reason, (uint64_t)vcpu->vc_control_pa);
		return (EINVAL);
	}

	if (update_rip) {
		vmcb->v_rip = vcpu->vc_gueststate.vg_rip;

		if (rflags & PSL_T) {
			if (vmm_inject_db(vcpu)) {
				printf("%s: can't inject #DB exception to "
				    "guest", __func__);
				return (EINVAL);
			}
		}
	}

	/* Enable SVME in EFER (must always be set) */
	vmcb->v_efer |= EFER_SVME;
	svm_set_dirty(vcpu, SVM_CLEANBITS_CR);

	return (ret);
}

/*
 * sync guest ghcb -> host vmcb/vcpu
 */
int
svm_vmgexit_sync_host(struct vcpu *vcpu)
{
	struct vmcb		*vmcb = (struct vmcb *)vcpu->vc_control_va;
	struct ghcb_sa		*ghcb;
	uint64_t		 svm_sw_exitcode;
	uint8_t			*valid_bm, expected_bm[0x10];

	if (!vcpu->vc_seves)
		return (0);

	if (vcpu->vc_svm_ghcb_va == 0)
		return (0);

	ghcb = (struct ghcb_sa *)vcpu->vc_svm_ghcb_va;
	if (!ghcb_valid(ghcb))
		return (EINVAL);
	valid_bm = ghcb->valid_bitmap;

	/* Always required. */
	memset(expected_bm, 0, sizeof(expected_bm));
	ghcb_valbm_set(expected_bm, GHCB_SW_EXITCODE);
	ghcb_valbm_set(expected_bm, GHCB_SW_EXITINFO1);
	ghcb_valbm_set(expected_bm, GHCB_SW_EXITINFO2);

	svm_sw_exitcode = ghcb->v_sw_exitcode;
	switch (svm_sw_exitcode) {
	case SVM_VMEXIT_CPUID:
		ghcb_valbm_set(expected_bm, GHCB_RAX);
		ghcb_valbm_set(expected_bm, GHCB_RCX);
		break;
	case SVM_VMEXIT_IOIO:
		if (ghcb->v_sw_exitinfo1 & 0x1) {
			/* IN instruction, no registers used */
		} else {
			/* OUT instruction */
			ghcb_valbm_set(expected_bm, GHCB_RAX);
		}
		break;
	case SVM_VMEXIT_MSR:
		if (ghcb->v_sw_exitinfo1 == 1) {
			/* WRMSR */
			ghcb_valbm_set(expected_bm, GHCB_RAX);
			ghcb_valbm_set(expected_bm, GHCB_RCX);
			ghcb_valbm_set(expected_bm, GHCB_RDX);
		} else {
			/* RDMSR */
			ghcb_valbm_set(expected_bm, GHCB_RCX);
		}
		break;
	default:
		return (EINVAL);
	}

	if (ghcb_verify_bm(valid_bm, expected_bm) != 0)
		return (EINVAL);

	/* Always required */
	vmcb->v_exitcode = vcpu->vc_gueststate.vg_exit_reason =
	    ghcb->v_sw_exitcode;
	vmcb->v_exitinfo1 = ghcb->v_sw_exitinfo1;
	vmcb->v_exitinfo2 = ghcb->v_sw_exitinfo2;

	if (ghcb_valbm_isset(expected_bm, GHCB_RAX))
		vmcb->v_rax = vcpu->vc_gueststate.vg_rax = ghcb->v_rax;
	if (ghcb_valbm_isset(expected_bm, GHCB_RBX))
		vcpu->vc_gueststate.vg_rbx = ghcb->v_rbx;
	if (ghcb_valbm_isset(expected_bm, GHCB_RCX))
		vcpu->vc_gueststate.vg_rcx = ghcb->v_rcx;
	if (ghcb_valbm_isset(expected_bm, GHCB_RDX))
		vcpu->vc_gueststate.vg_rdx = ghcb->v_rdx;

	return (0);
}

/*
 * sync host vmcb/vcpu -> guest ghcb
 */
int
svm_vmgexit_sync_guest(struct vcpu *vcpu)
{
	uint64_t		 svm_sw_exitcode;
	uint64_t		 svm_sw_exitinfo1, svm_sw_exitinfo2;
	uint8_t			*valid_bm;
	struct ghcb_sa		*ghcb;

	if (!vcpu->vc_seves)
		return (0);

	if (vcpu->vc_svm_ghcb_va == 0)
		return (0);

	ghcb = (struct ghcb_sa *)vcpu->vc_svm_ghcb_va;
	svm_sw_exitcode = ghcb->v_sw_exitcode;
	svm_sw_exitinfo1 = ghcb->v_sw_exitinfo1;
	svm_sw_exitinfo2 = ghcb->v_sw_exitinfo2;
	ghcb_clear(ghcb);
	valid_bm = ghcb->valid_bitmap;

	switch (svm_sw_exitcode) {
	case SVM_VMEXIT_CPUID:
		ghcb_valbm_set(valid_bm, GHCB_RAX);
		ghcb_valbm_set(valid_bm, GHCB_RBX);
		ghcb_valbm_set(valid_bm, GHCB_RCX);
		ghcb_valbm_set(valid_bm, GHCB_RDX);
		break;
	case SVM_VMEXIT_IOIO:
		if (svm_sw_exitinfo1 & 0x1) {
			/* IN instruction */
			ghcb_valbm_set(valid_bm, GHCB_RAX);
		} else {
			/* OUT instruction, nothing to return */
		}
		break;
	case SVM_VMEXIT_MSR:
		if (svm_sw_exitinfo1 == 1) {
			/* WRMSR, nothing to return */
		} else {
			/* RDMSR */
			ghcb_valbm_set(valid_bm, GHCB_RAX);
			ghcb_valbm_set(valid_bm, GHCB_RDX);
		}
		break;
	default:
		return (EINVAL);
	}

	/* Always required */
	svm_sw_exitinfo1 = 0;
	svm_sw_exitinfo2 = 0;
	ghcb_valbm_set(valid_bm, GHCB_SW_EXITINFO1);
	ghcb_valbm_set(valid_bm, GHCB_SW_EXITINFO2);

	if (ghcb_valbm_isset(valid_bm, GHCB_RAX))
		ghcb->v_rax = vcpu->vc_gueststate.vg_rax;
	if (ghcb_valbm_isset(valid_bm, GHCB_RBX))
		ghcb->v_rbx = vcpu->vc_gueststate.vg_rbx;
	if (ghcb_valbm_isset(valid_bm, GHCB_RCX))
		ghcb->v_rcx = vcpu->vc_gueststate.vg_rcx;
	if (ghcb_valbm_isset(valid_bm, GHCB_RDX))
		ghcb->v_rdx = vcpu->vc_gueststate.vg_rdx;

	if (ghcb_valbm_isset(valid_bm, GHCB_SW_EXITINFO1))
		ghcb->v_sw_exitinfo1 = svm_sw_exitinfo1;
	if (ghcb_valbm_isset(valid_bm, GHCB_SW_EXITINFO2))
		ghcb->v_sw_exitinfo2 = svm_sw_exitinfo2;

	return (0);
}

/*
 * svm_handle_vmgexit
 *
 * Handle exits initiated by the guest due to #VC exceptions generated
 * when SEV-ES is enabled.
 */
int
svm_handle_vmgexit(struct vcpu *vcpu)
{
	struct vmcb		*vmcb = (struct vmcb *)vcpu->vc_control_va;
	struct vm		*vm = vcpu->vc_parent;
	struct ghcb_sa		*ghcb;
	paddr_t			 ghcb_gpa, ghcb_hpa;
	uint32_t		 req, resp;
	uint64_t		 result;
	int			 syncout, error = 0;

	if (vcpu->vc_svm_ghcb_va == 0 && (vmcb->v_ghcb_gpa & ~PG_FRAME) == 0 &&
	    (vmcb->v_ghcb_gpa & PG_FRAME) != 0) {
		/*
		 * Guest provides a valid guest physical address
		 * for GHCB and it is not set yet -> assign it.
		 *
		 * We only accept a GHCB once; we decline re-definition.
		 */
		ghcb_gpa = vmcb->v_ghcb_gpa & PG_FRAME;
		if (!pmap_extract(vm->vm_pmap, ghcb_gpa, &ghcb_hpa))
			return (EINVAL);
		vcpu->vc_svm_ghcb_va = (vaddr_t)PMAP_DIRECT_MAP(ghcb_hpa);
	} else if ((vmcb->v_ghcb_gpa & ~PG_FRAME) != 0) {
		/*
		 * Low bits in use, thus must be a MSR protocol
		 * request.
		 */
		req = (vmcb->v_ghcb_gpa & 0xffffffff);

		/* We only support cpuid and terminate. */
		if ((req & ~PG_FRAME) == MSR_PROTO_TERMINATE) {
			DPRINTF("%s: guest requests termination\n", __func__);
			return (1);
		} else if ((req & ~PG_FRAME) != MSR_PROTO_CPUID_REQ)
			return (EINVAL);

		/* Emulate CPUID */
		vmcb->v_exitcode = SVM_VMEXIT_CPUID;
		vmcb->v_rax = vmcb->v_ghcb_gpa >> 32;
		vcpu->vc_gueststate.vg_rax = 0;
		vcpu->vc_gueststate.vg_rbx = 0;
		vcpu->vc_gueststate.vg_rcx = 0;
		vcpu->vc_gueststate.vg_rdx = 0;
		error = vmm_handle_cpuid(vcpu);
		if (error)
			goto out;

		switch (req >> 30) {
		case 0:	/* eax: emulate cpuid and return eax */
			result = vmcb->v_rax;
			break;
		case 1:	/* return ebx */
			result = vcpu->vc_gueststate.vg_rbx;
			break;
		case 2:	/* return ecx */
			result = vcpu->vc_gueststate.vg_rcx;
			break;
		case 3:	/* return edx */
			result = vcpu->vc_gueststate.vg_rdx;
			break;
		default:
			DPRINTF("%s: unknown request 0x%x\n", __func__, req);
			return (EINVAL);
		}

		/* build response */
		resp = MSR_PROTO_CPUID_RESP | (req & 0xc0000000);
		vmcb->v_ghcb_gpa = (result << 32) | resp;

		return (0);
	}

	/* Verify GHCB and synchronize guest state information. */
	ghcb = (struct ghcb_sa *)vcpu->vc_svm_ghcb_va;
	if (svm_vmgexit_sync_host(vcpu)) {
		error = EINVAL;
		goto out;
	}

	/* Handle GHCB protocol */
	syncout = 0;
	switch (vmcb->v_exitcode) {
	case SVM_VMEXIT_CPUID:
		error = vmm_handle_cpuid(vcpu);
		vmcb->v_rip = vcpu->vc_gueststate.vg_rip;
		vcpu->vc_gueststate.vg_rax = vmcb->v_rax;
		syncout = 1;
		break;
	case SVM_VMEXIT_IOIO:
		if (svm_handle_inout(vcpu) == 0)
			error = EAGAIN;
		break;
	case SVM_VMEXIT_MSR:
		error = svm_handle_msr(vcpu);
		vmcb->v_rip = vcpu->vc_gueststate.vg_rip;
		syncout = 1;
		break;
	default:
		DPRINTF("%s: unknown exit 0x%llx\n", __func__,
		    vmcb->v_exitcode);
		error = EINVAL;
	}

	if (syncout)
		error = svm_vmgexit_sync_guest(vcpu);

out:
	return (error);
}

/*
 * svm_handle_efercr
 *
 * With SEV-ES the hypervisor can not intercept and modify writes
 * to CR and EFER.  However, a post write intercept notifies about
 * the new state of these registers.
 */
int
svm_handle_efercr(struct vcpu *vcpu, uint64_t exit_reason)
{
	struct vmcb	*vmcb = (struct vmcb *)vcpu->vc_control_va;

	switch (exit_reason) {
	case SVM_VMEXIT_EFER_WRITE_TRAP:
		vmcb->v_efer = vmcb->v_exitinfo1;
		break;
	case SVM_VMEXIT_CR0_WRITE_TRAP:
		vmcb->v_cr0 = vmcb->v_exitinfo1;
		break;
	case SVM_VMEXIT_CR4_WRITE_TRAP:
		vmcb->v_cr4 = vmcb->v_exitinfo1;
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

/*
 * svm_get_iflag
 *
 * With SEV-ES the hypervisor has no access to the flags register.
 * Only the state of the PSL_I is provided by v_intr_shadow in
 * the VMCB.
 */
int
svm_get_iflag(struct vcpu *vcpu, uint64_t rflags)
{
	struct vmcb		*vmcb = (struct vmcb *)vcpu->vc_control_va;

	if (vcpu->vc_seves)
		return (vmcb->v_intr_shadow & SMV_GUEST_INTR_MASK);
	return (rflags & PSL_I);
}

/*
 * vmx_handle_exit
 *
 * Handle exits from the VM by decoding the exit reason and calling various
 * subhandlers as needed.
 */
int
vmx_handle_exit(struct vcpu *vcpu)
{
	uint64_t exit_reason, rflags, istate;
	int update_rip, ret = 0;

	update_rip = 0;
	exit_reason = vcpu->vc_gueststate.vg_exit_reason;
	rflags = vcpu->vc_gueststate.vg_rflags;

	switch (exit_reason) {
	case VMX_EXIT_INT_WINDOW:
		if (!(rflags & PSL_I)) {
			DPRINTF("%s: impossible interrupt window exit "
			    "config\n", __func__);
			ret = EINVAL;
			break;
		}

		ret = EAGAIN;
		update_rip = 0;
		break;
	case VMX_EXIT_EPT_VIOLATION:
		ret = vmx_handle_np_fault(vcpu);
		break;
	case VMX_EXIT_CPUID:
		ret = vmm_handle_cpuid(vcpu);
		update_rip = 1;
		break;
	case VMX_EXIT_IO:
		if (vmx_handle_inout(vcpu) == 0)
			ret = EAGAIN;
		break;
	case VMX_EXIT_EXTINT:
		vmx_handle_intr(vcpu);
		update_rip = 0;
		break;
	case VMX_EXIT_CR_ACCESS:
		ret = vmx_handle_cr(vcpu);
		update_rip = 1;
		break;
	case VMX_EXIT_HLT:
		ret = vmx_handle_hlt(vcpu);
		update_rip = 1;
		break;
	case VMX_EXIT_RDMSR:
		ret = vmx_handle_rdmsr(vcpu);
		update_rip = 1;
		break;
	case VMX_EXIT_WRMSR:
		ret = vmx_handle_wrmsr(vcpu);
		update_rip = 1;
		break;
	case VMX_EXIT_XSETBV:
		ret = vmx_handle_xsetbv(vcpu);
		update_rip = 1;
		break;
	case VMX_EXIT_MWAIT:
	case VMX_EXIT_MONITOR:
	case VMX_EXIT_VMXON:
	case VMX_EXIT_VMWRITE:
	case VMX_EXIT_VMREAD:
	case VMX_EXIT_VMLAUNCH:
	case VMX_EXIT_VMRESUME:
	case VMX_EXIT_VMPTRLD:
	case VMX_EXIT_VMPTRST:
	case VMX_EXIT_VMCLEAR:
	case VMX_EXIT_VMCALL:
	case VMX_EXIT_VMFUNC:
	case VMX_EXIT_VMXOFF:
	case VMX_EXIT_INVVPID:
	case VMX_EXIT_INVEPT:
		ret = vmm_inject_ud(vcpu);
		update_rip = 0;
		break;
	case VMX_EXIT_TRIPLE_FAULT:
#ifdef VMM_DEBUG
		DPRINTF("%s: vm %d vcpu %d triple fault\n", __func__,
		    vcpu->vc_parent->vm_id, vcpu->vc_id);
		vmx_vcpu_dump_regs(vcpu);
		dump_vcpu(vcpu);
		vmx_dump_vmcs(vcpu);
#endif /* VMM_DEBUG */
		ret = EAGAIN;
		update_rip = 0;
		break;
	default:
#ifdef VMM_DEBUG
		DPRINTF("%s: unhandled exit 0x%llx (%s)\n", __func__,
		    exit_reason, vmx_exit_reason_decode(exit_reason));
#endif /* VMM_DEBUG */
		return (EINVAL);
	}

	if (update_rip) {
		if (vmwrite(VMCS_GUEST_IA32_RIP,
		    vcpu->vc_gueststate.vg_rip)) {
			printf("%s: can't advance rip\n", __func__);
			return (EINVAL);
		}

		if (vmread(VMCS_GUEST_INTERRUPTIBILITY_ST,
		    &istate)) {
			printf("%s: can't read interruptibility state\n",
			    __func__);
			return (EINVAL);
		}

		/* Interruptibility state 0x3 covers NMIs and STI */
		istate &= ~0x3;

		if (vmwrite(VMCS_GUEST_INTERRUPTIBILITY_ST,
		    istate)) {
			printf("%s: can't write interruptibility state\n",
			    __func__);
			return (EINVAL);
		}

		if (rflags & PSL_T) {
			if (vmm_inject_db(vcpu)) {
				printf("%s: can't inject #DB exception to "
				    "guest", __func__);
				return (EINVAL);
			}
		}
	}

	return (ret);
}

/*
 * vmm_inject_gp
 *
 * Injects an #GP exception into the guest VCPU.
 *
 * Parameters:
 *  vcpu: vcpu to inject into
 *
 * Return values:
 *  Always 0
 */
int
vmm_inject_gp(struct vcpu *vcpu)
{
	DPRINTF("%s: injecting #GP at guest %%rip 0x%llx\n", __func__,
	    vcpu->vc_gueststate.vg_rip);
	vcpu->vc_inject.vie_vector = VMM_EX_GP;
	vcpu->vc_inject.vie_type = VCPU_INJECT_EX;
	vcpu->vc_inject.vie_errorcode = 0;

	return (0);
}

/*
 * vmm_inject_ud
 *
 * Injects an #UD exception into the guest VCPU.
 *
 * Parameters:
 *  vcpu: vcpu to inject into
 *
 * Return values:
 *  Always 0
 */
int
vmm_inject_ud(struct vcpu *vcpu)
{
	DPRINTF("%s: injecting #UD at guest %%rip 0x%llx\n", __func__,
	    vcpu->vc_gueststate.vg_rip);
	vcpu->vc_inject.vie_vector = VMM_EX_UD;
	vcpu->vc_inject.vie_type = VCPU_INJECT_EX;
	vcpu->vc_inject.vie_errorcode = 0;

	return (0);
}

/*
 * vmm_inject_db
 *
 * Injects a #DB exception into the guest VCPU.
 *
 * Parameters:
 *  vcpu: vcpu to inject into
 *
 * Return values:
 *  Always 0
 */
int
vmm_inject_db(struct vcpu *vcpu)
{
	DPRINTF("%s: injecting #DB at guest %%rip 0x%llx\n", __func__,
	    vcpu->vc_gueststate.vg_rip);
	vcpu->vc_inject.vie_vector = VMM_EX_DB;
	vcpu->vc_inject.vie_type = VCPU_INJECT_EX;
	vcpu->vc_inject.vie_errorcode = 0;

	return (0);
}

/*
 * vmm_get_guest_memtype
 *
 * Returns the type of memory 'gpa' refers to in the context of vm 'vm'
 */
int
vmm_get_guest_memtype(struct vm *vm, paddr_t gpa)
{
	int i;
	struct vm_mem_range *vmr;

	/* XXX Use binary search? */
	for (i = 0; i < vm->vm_nmemranges; i++) {
		vmr = &vm->vm_memranges[i];

		/*
		 * vm_memranges are ascending. gpa can no longer be in one of
		 * the memranges
		 */
		if (gpa < vmr->vmr_gpa)
			break;

		if (gpa < vmr->vmr_gpa + vmr->vmr_size) {
			if (vmr->vmr_type == VM_MEM_MMIO)
				return (VMM_MEM_TYPE_MMIO);
			return (VMM_MEM_TYPE_REGULAR);
		}
	}

	DPRINTF("guest memtype @ 0x%llx unknown\n", (uint64_t)gpa);
	return (VMM_MEM_TYPE_UNKNOWN);
}

vaddr_t
vmm_translate_gpa(struct vm *vm, paddr_t gpa)
{
	int i = 0;
	vaddr_t hva = 0;
	struct vm_mem_range *vmr = NULL;

	/*
	 * Translate GPA -> userland HVA in proc p. Find the memory range
	 * and use it to translate to the HVA.
	 */
	for (i = 0; i < vm->vm_nmemranges; i++) {
		vmr = &vm->vm_memranges[i];
		if (gpa >= vmr->vmr_gpa && gpa < vmr->vmr_gpa + vmr->vmr_size) {
			hva = vmr->vmr_va + (gpa - vmr->vmr_gpa);
			break;
		}
	}

	return (hva);
}


/*
 * vmx_get_exit_qualification
 *
 * Return the current VMCS' exit qualification information
 */
int
vmx_get_exit_qualification(uint64_t *exit_qualification)
{
	if (vmread(VMCS_GUEST_EXIT_QUALIFICATION, exit_qualification)) {
		printf("%s: can't extract exit qual\n", __func__);
		return (EINVAL);
	}

	return (0);
}

/*
 * vmx_get_guest_faulttype
 *
 * Determines the type (R/W/X) of the last fault on the VCPU last run on
 * this PCPU.
 */
int
vmx_get_guest_faulttype(void)
{
	uint64_t exit_qual;
	uint64_t presentmask = IA32_VMX_EPT_FAULT_WAS_READABLE |
	    IA32_VMX_EPT_FAULT_WAS_WRITABLE | IA32_VMX_EPT_FAULT_WAS_EXECABLE;
	vm_prot_t prot, was_prot;

	if (vmx_get_exit_qualification(&exit_qual))
		return (-1);

	if ((exit_qual & presentmask) == 0)
		return VM_FAULT_INVALID;

	was_prot = 0;
	if (exit_qual & IA32_VMX_EPT_FAULT_WAS_READABLE)
		was_prot |= PROT_READ;
	if (exit_qual & IA32_VMX_EPT_FAULT_WAS_WRITABLE)
		was_prot |= PROT_WRITE;
	if (exit_qual & IA32_VMX_EPT_FAULT_WAS_EXECABLE)
		was_prot |= PROT_EXEC;

	prot = 0;
	if (exit_qual & IA32_VMX_EPT_FAULT_READ)
		prot = PROT_READ;
	else if (exit_qual & IA32_VMX_EPT_FAULT_WRITE)
		prot = PROT_WRITE;
	else if (exit_qual & IA32_VMX_EPT_FAULT_EXEC)
		prot = PROT_EXEC;

	if ((was_prot & prot) == 0)
		return VM_FAULT_PROTECT;

	return (-1);
}

/*
 * svm_get_guest_faulttype
 *
 * Determines the type (R/W/X) of the last fault on the VCPU last run on
 * this PCPU.
 */
int
svm_get_guest_faulttype(struct vmcb *vmcb)
{
	if (!(vmcb->v_exitinfo1 & 0x1))
		return VM_FAULT_INVALID;
	return VM_FAULT_PROTECT;
}

/*
 * svm_fault_page
 *
 * Request a new page to be faulted into the UVM map of the VM owning 'vcpu'
 * at address 'gpa'.
 */
int
svm_fault_page(struct vcpu *vcpu, paddr_t gpa)
{
	struct proc *p = curproc;
	paddr_t hpa, pa = trunc_page(gpa);
	vaddr_t hva;
	int ret = 1;

	hva = vmm_translate_gpa(vcpu->vc_parent, pa);
	if (hva == 0) {
		printf("%s: unable to translate gpa 0x%llx\n", __func__,
		    (uint64_t)pa);
		return (EINVAL);
	}

	/* If we don't already have a backing page... */
	if (!pmap_extract(p->p_vmspace->vm_map.pmap, hva, &hpa)) {
		/* ...fault a RW page into the p's address space... */
		ret = uvm_fault_wire(&p->p_vmspace->vm_map, hva,
		    hva + PAGE_SIZE, PROT_READ | PROT_WRITE);
		if (ret) {
			printf("%s: uvm_fault failed %d hva=0x%llx\n", __func__,
			    ret, (uint64_t)hva);
			return (ret);
		}

		/* ...and then get the mapping. */
		if (!pmap_extract(p->p_vmspace->vm_map.pmap, hva, &hpa)) {
			printf("%s: failed to extract hpa for hva 0x%llx\n",
			    __func__, (uint64_t)hva);
			return (EINVAL);
		}
	}

	/* Now we insert a RWX mapping into the guest's RVI pmap. */
	ret = pmap_enter(vcpu->vc_parent->vm_pmap, pa, hpa,
	    PROT_READ | PROT_WRITE | PROT_EXEC, 0);
	if (ret) {
		printf("%s: pmap_enter failed pa=0x%llx, hpa=0x%llx\n",
		    __func__, (uint64_t)pa, (uint64_t)hpa);
	}

	return (ret);
}

/*
 * svm_handle_np_fault
 *
 * High level nested paging handler for SVM. Verifies that a fault is for a
 * valid memory region, then faults a page, or aborts otherwise.
 */
int
svm_handle_np_fault(struct vcpu *vcpu)
{
	uint64_t gpa;
	int gpa_memtype, ret = 0;
	struct vmcb *vmcb = (struct vmcb *)vcpu->vc_control_va;
	struct vm_exit_eptviolation *vee = &vcpu->vc_exit.vee;
	struct cpu_info *ci = curcpu();

	memset(vee, 0, sizeof(*vee));

	gpa = vmcb->v_exitinfo2;

	gpa_memtype = vmm_get_guest_memtype(vcpu->vc_parent, gpa);
	switch (gpa_memtype) {
	case VMM_MEM_TYPE_REGULAR:
		vee->vee_fault_type = VEE_FAULT_HANDLED;
		ret = svm_fault_page(vcpu, gpa);
		break;
	case VMM_MEM_TYPE_MMIO:
		vee->vee_fault_type = VEE_FAULT_MMIO_ASSIST;
		if (ci->ci_vmm_cap.vcc_svm.svm_decode_assist) {
			vee->vee_insn_len = vmcb->v_n_bytes_fetched;
			memcpy(&vee->vee_insn_bytes, vmcb->v_guest_ins_bytes,
			    sizeof(vee->vee_insn_bytes));
			vee->vee_insn_info |= VEE_BYTES_VALID;
		}
		ret = EAGAIN;
		break;
	default:
		printf("%s: unknown memory type %d for GPA 0x%llx\n",
		    __func__, gpa_memtype, gpa);
		return (EINVAL);
	}

	return (ret);
}

/*
 * vmx_fault_page
 *
 * Request a new page to be faulted into the UVM map of the VM owning 'vcpu'
 * at address 'gpa'.
 *
 * Parameters:
 *  vcpu: guest VCPU requiring the page to be faulted into the UVM map
 *  gpa: guest physical address that triggered the fault
 *
 * Return Values:
 *  0: if successful
 *  EINVAL: if fault type could not be determined or VMCS reload fails
 *  EAGAIN: if a protection fault occurred, ie writing to a read-only page
 *  errno: if uvm_fault_wire() fails to wire in the page
 */
int
vmx_fault_page(struct vcpu *vcpu, paddr_t gpa)
{
	struct proc *p = curproc;
	int fault_type, ret;
	paddr_t hpa, pa = trunc_page(gpa);
	vaddr_t hva;

	fault_type = vmx_get_guest_faulttype();
	switch (fault_type) {
	case -1:
		printf("%s: invalid fault type\n", __func__);
		return (EINVAL);
	case VM_FAULT_PROTECT:
		vcpu->vc_exit.vee.vee_fault_type = VEE_FAULT_PROTECT;
		return (EAGAIN);
	default:
		vcpu->vc_exit.vee.vee_fault_type = VEE_FAULT_HANDLED;
		break;
	}

	hva = vmm_translate_gpa(vcpu->vc_parent, pa);
	if (hva == 0) {
		printf("%s: unable to translate gpa 0x%llx\n", __func__,
		    (uint64_t)pa);
		return (EINVAL);
	}

	/* If we don't already have a backing page... */
	if (!pmap_extract(p->p_vmspace->vm_map.pmap, hva, &hpa)) {
		/* ...fault a RW page into the p's address space... */
		vcpu->vc_last_pcpu = curcpu(); /* uvm_fault may sleep. */
		ret = uvm_fault_wire(&p->p_vmspace->vm_map, hva,
		    hva + PAGE_SIZE, PROT_READ | PROT_WRITE);
		if (ret) {
			printf("%s: uvm_fault failed %d hva=0x%llx\n", __func__,
			    ret, (uint64_t)hva);
			return (ret);
		}
		if (vcpu_reload_vmcs_vmx(vcpu)) {
			printf("%s: failed to reload vmcs\n", __func__);
			return (EINVAL);
		}

		/* ...and then get the mapping. */
		if (!pmap_extract(p->p_vmspace->vm_map.pmap, hva, &hpa)) {
			printf("%s: failed to extract hpa for hva 0x%llx\n",
			    __func__, (uint64_t)hva);
			return (EINVAL);
		}
	}

	/* Now we insert a RWX mapping into the guest's EPT pmap. */
	ret = pmap_enter(vcpu->vc_parent->vm_pmap, pa, hpa,
	    PROT_READ | PROT_WRITE | PROT_EXEC, 0);
	if (ret) {
		printf("%s: pmap_enter failed pa=0x%llx, hpa=0x%llx\n",
		    __func__, (uint64_t)pa, (uint64_t)hpa);
	}

	return (ret);
}

/*
 * vmx_handle_np_fault
 *
 * High level nested paging handler for VMX. Verifies that a fault is for a
 * valid memory region, then faults a page, or aborts otherwise.
 */
int
vmx_handle_np_fault(struct vcpu *vcpu)
{
	uint64_t insn_len = 0, gpa;
	int gpa_memtype, ret = 0;
	struct vm_exit_eptviolation *vee = &vcpu->vc_exit.vee;

	memset(vee, 0, sizeof(*vee));

	if (vmread(VMCS_GUEST_PHYSICAL_ADDRESS, &gpa)) {
		printf("%s: cannot extract faulting pa\n", __func__);
		return (EINVAL);
	}

	gpa_memtype = vmm_get_guest_memtype(vcpu->vc_parent, gpa);
	switch (gpa_memtype) {
	case VMM_MEM_TYPE_REGULAR:
		vee->vee_fault_type = VEE_FAULT_HANDLED;
		ret = vmx_fault_page(vcpu, gpa);
		break;
	case VMM_MEM_TYPE_MMIO:
		vee->vee_fault_type = VEE_FAULT_MMIO_ASSIST;
		if (vmread(VMCS_INSTRUCTION_LENGTH, &insn_len) ||
		    insn_len == 0 || insn_len > 15) {
			printf("%s: failed to extract instruction length\n",
			    __func__);
			ret = EINVAL;
		} else {
			vee->vee_insn_len = (uint32_t)insn_len;
			vee->vee_insn_info |= VEE_LEN_VALID;
			ret = EAGAIN;
		}
		break;
	default:
		printf("%s: unknown memory type %d for GPA 0x%llx\n",
		    __func__, gpa_memtype, gpa);
		return (EINVAL);
	}

	return (ret);
}

/*
 * vmm_get_guest_cpu_cpl
 *
 * Determines current CPL of 'vcpu'. On VMX/Intel, this is gathered from the
 * VMCS field for the DPL of SS (this seems odd, but is documented that way
 * in the SDM). For SVM/AMD, this is gathered directly from the VMCB's 'cpl'
 * field, as per the APM.
 *
 * Parameters:
 *  vcpu: guest VCPU for which CPL is to be checked
 *
 * Return Values:
 *  -1: the CPL could not be determined
 *  0-3 indicating the current CPL. For real mode operation, 0 is returned.
 */
int
vmm_get_guest_cpu_cpl(struct vcpu *vcpu)
{
	int mode;
	struct vmcb *vmcb;
	uint64_t ss_ar;

	mode = vmm_get_guest_cpu_mode(vcpu);

	if (mode == VMM_CPU_MODE_UNKNOWN)
		return (-1);

	if (mode == VMM_CPU_MODE_REAL)
		return (0);

	if (vmm_softc->mode == VMM_MODE_RVI) {
		vmcb = (struct vmcb *)vcpu->vc_control_va;
		return (vmcb->v_cpl);
	} else if (vmm_softc->mode == VMM_MODE_EPT) {
		if (vmread(VMCS_GUEST_IA32_SS_AR, &ss_ar))
			return (-1);
		return ((ss_ar & 0x60) >> 5);
	} else
		return (-1);
}

/*
 * vmm_get_guest_cpu_mode
 *
 * Determines current CPU mode of 'vcpu'.
 *
 * Parameters:
 *  vcpu: guest VCPU for which mode is to be checked
 *
 * Return Values:
 *  One of VMM_CPU_MODE_*, or VMM_CPU_MODE_UNKNOWN if the mode could not be
 *   ascertained.
 */
int
vmm_get_guest_cpu_mode(struct vcpu *vcpu)
{
	uint64_t cr0, efer, cs_ar;
	uint8_t l, dib;
	struct vmcb *vmcb;
	struct vmx_msr_store *msr_store;

	if (vmm_softc->mode == VMM_MODE_RVI) {
		vmcb = (struct vmcb *)vcpu->vc_control_va;
		cr0 = vmcb->v_cr0;
		efer = vmcb->v_efer;
		cs_ar = vmcb->v_cs.vs_attr;
		cs_ar = (cs_ar & 0xff) | ((cs_ar << 4) & 0xf000);
	} else if (vmm_softc->mode == VMM_MODE_EPT) {
		if (vmread(VMCS_GUEST_IA32_CR0, &cr0))
			return (VMM_CPU_MODE_UNKNOWN);
		if (vmread(VMCS_GUEST_IA32_CS_AR, &cs_ar))
			return (VMM_CPU_MODE_UNKNOWN);
		msr_store =
		    (struct vmx_msr_store *)vcpu->vc_vmx_msr_exit_save_va;
		efer = msr_store[VCPU_REGS_EFER].vms_data;
	} else
		return (VMM_CPU_MODE_UNKNOWN);

	l = (cs_ar & 0x2000) >> 13;
	dib = (cs_ar & 0x4000) >> 14;

	/* Check CR0.PE */
	if (!(cr0 & CR0_PE))
		return (VMM_CPU_MODE_REAL);

	/* Check EFER */
	if (efer & EFER_LMA) {
		/* Could be compat or long mode, check CS.L */
		if (l)
			return (VMM_CPU_MODE_LONG);
		else
			return (VMM_CPU_MODE_COMPAT);
	}

	/* Check prot vs prot32 */
	if (dib)
		return (VMM_CPU_MODE_PROT32);
	else
		return (VMM_CPU_MODE_PROT);
}

/*
 * svm_handle_inout
 *
 * Exit handler for IN/OUT instructions.
 *
 * Parameters:
 *  vcpu: The VCPU where the IN/OUT instruction occurred
 *
 * Return values:
 *  0: if successful
 *  EINVAL: an invalid IN/OUT instruction was encountered
 */
int
svm_handle_inout(struct vcpu *vcpu)
{
	uint64_t insn_length, exit_qual;
	struct vmcb *vmcb = (struct vmcb *)vcpu->vc_control_va;

	insn_length = vmcb->v_exitinfo2 - vmcb->v_rip;
	exit_qual = vmcb->v_exitinfo1;

	/* Bit 0 - direction */
	if (exit_qual & 0x1)
		vcpu->vc_exit.vei.vei_dir = VEI_DIR_IN;
	else
		vcpu->vc_exit.vei.vei_dir = VEI_DIR_OUT;
	/* Bit 2 - string instruction? */
	vcpu->vc_exit.vei.vei_string = (exit_qual & 0x4) >> 2;
	/* Bit 3 - REP prefix? */
	vcpu->vc_exit.vei.vei_rep = (exit_qual & 0x8) >> 3;

	/* Bits 4:6 - size of exit */
	if (exit_qual & 0x10)
		vcpu->vc_exit.vei.vei_size = 1;
	else if (exit_qual & 0x20)
		vcpu->vc_exit.vei.vei_size = 2;
	else if (exit_qual & 0x40)
		vcpu->vc_exit.vei.vei_size = 4;

	/* Bit 16:31 - port */
	vcpu->vc_exit.vei.vei_port = (exit_qual & 0xFFFF0000) >> 16;
	/* Data */
	vcpu->vc_exit.vei.vei_data = vmcb->v_rax;

	vcpu->vc_exit.vei.vei_insn_len = (uint8_t)insn_length;

	TRACEPOINT(vmm, inout, vcpu, vcpu->vc_exit.vei.vei_port,
	    vcpu->vc_exit.vei.vei_dir, vcpu->vc_exit.vei.vei_data);

	return (0);
}

/*
 * vmx_handle_inout
 *
 * Exit handler for IN/OUT instructions.
 *
 * Parameters:
 *  vcpu: The VCPU where the IN/OUT instruction occurred
 *
 * Return values:
 *  0: if successful
 *  EINVAL: invalid IN/OUT instruction or vmread failures occurred
 */
int
vmx_handle_inout(struct vcpu *vcpu)
{
	uint64_t insn_length, exit_qual;

	if (vmread(VMCS_INSTRUCTION_LENGTH, &insn_length)) {
		printf("%s: can't obtain instruction length\n", __func__);
		return (EINVAL);
	}

	if (vmx_get_exit_qualification(&exit_qual)) {
		printf("%s: can't get exit qual\n", __func__);
		return (EINVAL);
	}

	/* Bits 0:2 - size of exit */
	vcpu->vc_exit.vei.vei_size = (exit_qual & 0x7) + 1;
	/* Bit 3 - direction */
	if ((exit_qual & 0x8) >> 3)
		vcpu->vc_exit.vei.vei_dir = VEI_DIR_IN;
	else
		vcpu->vc_exit.vei.vei_dir = VEI_DIR_OUT;
	/* Bit 4 - string instruction? */
	vcpu->vc_exit.vei.vei_string = (exit_qual & 0x10) >> 4;
	/* Bit 5 - REP prefix? */
	vcpu->vc_exit.vei.vei_rep = (exit_qual & 0x20) >> 5;
	/* Bit 6 - Operand encoding */
	vcpu->vc_exit.vei.vei_encoding = (exit_qual & 0x40) >> 6;
	/* Bit 16:31 - port */
	vcpu->vc_exit.vei.vei_port = (exit_qual & 0xFFFF0000) >> 16;
	/* Data */
	vcpu->vc_exit.vei.vei_data = (uint32_t)vcpu->vc_gueststate.vg_rax;

	vcpu->vc_exit.vei.vei_insn_len = (uint8_t)insn_length;

	TRACEPOINT(vmm, inout, vcpu, vcpu->vc_exit.vei.vei_port,
	    vcpu->vc_exit.vei.vei_dir, vcpu->vc_exit.vei.vei_data);

	return (0);
}

/*
 * vmx_load_pdptes
 *
 * Update the PDPTEs in the VMCS with the values currently indicated by the
 * guest CR3. This is used for 32-bit PAE guests when enabling paging.
 *
 * Parameters
 *  vcpu: The vcpu whose PDPTEs should be loaded
 *
 * Return values:
 *  0: if successful
 *  EINVAL: if the PDPTEs could not be loaded
 *  ENOMEM: memory allocation failure
 */
int
vmx_load_pdptes(struct vcpu *vcpu)
{
	uint64_t cr3, cr3_host_phys;
	vaddr_t cr3_host_virt;
	pd_entry_t *pdptes;
	int ret;

	if (vmread(VMCS_GUEST_IA32_CR3, &cr3)) {
		printf("%s: can't read guest cr3\n", __func__);
		return (EINVAL);
	}

	if (!pmap_extract(vcpu->vc_parent->vm_pmap, (vaddr_t)cr3,
	    (paddr_t *)&cr3_host_phys)) {
		DPRINTF("%s: nonmapped guest CR3, setting PDPTEs to 0\n",
		    __func__);
		if (vmwrite(VMCS_GUEST_PDPTE0, 0)) {
			printf("%s: can't write guest PDPTE0\n", __func__);
			return (EINVAL);
		}

		if (vmwrite(VMCS_GUEST_PDPTE1, 0)) {
			printf("%s: can't write guest PDPTE1\n", __func__);
			return (EINVAL);
		}

		if (vmwrite(VMCS_GUEST_PDPTE2, 0)) {
			printf("%s: can't write guest PDPTE2\n", __func__);
			return (EINVAL);
		}

		if (vmwrite(VMCS_GUEST_PDPTE3, 0)) {
			printf("%s: can't write guest PDPTE3\n", __func__);
			return (EINVAL);
		}
		return (0);
	}

	ret = 0;

	/* We may sleep during km_alloc(9), so reload VMCS. */
	vcpu->vc_last_pcpu = curcpu();
	cr3_host_virt = (vaddr_t)km_alloc(PAGE_SIZE, &kv_any, &kp_none,
	    &kd_waitok);
	if (vcpu_reload_vmcs_vmx(vcpu)) {
		printf("%s: failed to reload vmcs\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	if (!cr3_host_virt) {
		printf("%s: can't allocate address for guest CR3 mapping\n",
		    __func__);
		return (ENOMEM);
	}

	pmap_kenter_pa(cr3_host_virt, cr3_host_phys, PROT_READ);

	pdptes = (pd_entry_t *)cr3_host_virt;
	if (vmwrite(VMCS_GUEST_PDPTE0, pdptes[0])) {
		printf("%s: can't write guest PDPTE0\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	if (vmwrite(VMCS_GUEST_PDPTE1, pdptes[1])) {
		printf("%s: can't write guest PDPTE1\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	if (vmwrite(VMCS_GUEST_PDPTE2, pdptes[2])) {
		printf("%s: can't write guest PDPTE2\n", __func__);
		ret = EINVAL;
		goto exit;
	}

	if (vmwrite(VMCS_GUEST_PDPTE3, pdptes[3])) {
		printf("%s: can't write guest PDPTE3\n", __func__);
		ret = EINVAL;
		goto exit;
	}

exit:
	pmap_kremove(cr3_host_virt, PAGE_SIZE);

	/* km_free(9) might sleep, so we need to reload VMCS. */
	vcpu->vc_last_pcpu = curcpu();
	km_free((void *)cr3_host_virt, PAGE_SIZE, &kv_any, &kp_none);
	if (vcpu_reload_vmcs_vmx(vcpu)) {
		printf("%s: failed to reload vmcs after km_free\n", __func__);
		ret = EINVAL;
	}

	return (ret);
}

/*
 * vmx_handle_cr0_write
 *
 * Write handler for CR0. This function ensures valid values are written into
 * CR0 for the cpu/vmm mode in use (cr0 must-be-0 and must-be-1 bits, etc).
 *
 * Parameters
 *  vcpu: The vcpu taking the cr0 write exit
 *     r: The guest's desired (incoming) cr0 value
 *
 * Return values:
 *  0: if successful
 *  EINVAL: if an error occurred
 */
int
vmx_handle_cr0_write(struct vcpu *vcpu, uint64_t r)
{
	struct vmx_msr_store *msr_store;
	struct vmx_invvpid_descriptor vid;
	uint64_t ectls, oldcr0, cr4, mask;
	int ret;

	/* Check must-be-0 bits */
	mask = vcpu->vc_vmx_cr0_fixed1;
	if (~r & mask) {
		/* Inject #GP, let the guest handle it */
		DPRINTF("%s: guest set invalid bits in %%cr0. Zeros "
		    "mask=0x%llx, data=0x%llx\n", __func__,
		    vcpu->vc_vmx_cr0_fixed1, r);
		vmm_inject_gp(vcpu);
		return (0);
	}

	/* Check must-be-1 bits */
	mask = vcpu->vc_vmx_cr0_fixed0;
	if ((r & mask) != mask) {
		/* Inject #GP, let the guest handle it */
		DPRINTF("%s: guest set invalid bits in %%cr0. Ones "
		    "mask=0x%llx, data=0x%llx\n", __func__,
		    vcpu->vc_vmx_cr0_fixed0, r);
		vmm_inject_gp(vcpu);
		return (0);
	}

	if (r & 0xFFFFFFFF00000000ULL) {
		DPRINTF("%s: setting bits 63:32 of %%cr0 is invalid,"
		    " inject #GP, cr0=0x%llx\n", __func__, r);
		vmm_inject_gp(vcpu);
		return (0);
	}

	if ((r & CR0_PG) && (r & CR0_PE) == 0) {
		DPRINTF("%s: PG flag set when the PE flag is clear,"
		    " inject #GP, cr0=0x%llx\n", __func__, r);
		vmm_inject_gp(vcpu);
		return (0);
	}

	if ((r & CR0_NW) && (r & CR0_CD) == 0) {
		DPRINTF("%s: NW flag set when the CD flag is clear,"
		    " inject #GP, cr0=0x%llx\n", __func__, r);
		vmm_inject_gp(vcpu);
		return (0);
	}

	if (vmread(VMCS_GUEST_IA32_CR0, &oldcr0)) {
		printf("%s: can't read guest cr0\n", __func__);
		return (EINVAL);
	}

	/* CR0 must always have NE set */
	r |= CR0_NE;

	if (vmwrite(VMCS_GUEST_IA32_CR0, r)) {
		printf("%s: can't write guest cr0\n", __func__);
		return (EINVAL);
	}

	/* If the guest hasn't enabled paging ... */
	if (!(r & CR0_PG) && (oldcr0 & CR0_PG)) {
		/* Paging was disabled (prev. enabled) - Flush TLB */
		if (vcpu->vc_vmx_vpid_enabled) {
			vid.vid_vpid = vcpu->vc_vpid;
			vid.vid_addr = 0;
			invvpid(IA32_VMX_INVVPID_SINGLE_CTX_GLB, &vid);
		}
	} else if (!(oldcr0 & CR0_PG) && (r & CR0_PG)) {
		/*
		 * Since the guest has enabled paging, then the IA32_VMX_IA32E_MODE_GUEST
		 * control must be set to the same as EFER_LME.
		 */
		msr_store = (struct vmx_msr_store *)vcpu->vc_vmx_msr_exit_save_va;

		if (vmread(VMCS_ENTRY_CTLS, &ectls)) {
			printf("%s: can't read entry controls", __func__);
			return (EINVAL);
		}

		if (msr_store[VCPU_REGS_EFER].vms_data & EFER_LME)
			ectls |= IA32_VMX_IA32E_MODE_GUEST;
		else
			ectls &= ~IA32_VMX_IA32E_MODE_GUEST;

		if (vmwrite(VMCS_ENTRY_CTLS, ectls)) {
			printf("%s: can't write entry controls", __func__);
			return (EINVAL);
		}

		if (vmread(VMCS_GUEST_IA32_CR4, &cr4)) {
			printf("%s: can't read guest cr4\n", __func__);
			return (EINVAL);
		}

		/* Load PDPTEs if PAE guest enabling paging */
		if (cr4 & CR4_PAE) {
			ret = vmx_load_pdptes(vcpu);

			if (ret) {
				printf("%s: updating PDPTEs failed\n", __func__);
				return (ret);
			}
		}
	}

	return (0);
}

/*
 * vmx_handle_cr4_write
 *
 * Write handler for CR4. This function ensures valid values are written into
 * CR4 for the cpu/vmm mode in use (cr4 must-be-0 and must-be-1 bits, etc).
 *
 * Parameters
 *  vcpu: The vcpu taking the cr4 write exit
 *     r: The guest's desired (incoming) cr4 value
 *
 * Return values:
 *  0: if successful
 *  EINVAL: if an error occurred
 */
int
vmx_handle_cr4_write(struct vcpu *vcpu, uint64_t r)
{
	uint64_t mask;

	/* Check must-be-0 bits */
	mask = ~(curcpu()->ci_vmm_cap.vcc_vmx.vmx_cr4_fixed1);
	if (r & mask) {
		/* Inject #GP, let the guest handle it */
		DPRINTF("%s: guest set invalid bits in %%cr4. Zeros "
		    "mask=0x%llx, data=0x%llx\n", __func__,
		    curcpu()->ci_vmm_cap.vcc_vmx.vmx_cr4_fixed1,
		    r);
		vmm_inject_gp(vcpu);
		return (0);
	}

	/* Check must-be-1 bits */
	mask = curcpu()->ci_vmm_cap.vcc_vmx.vmx_cr4_fixed0;
	if ((r & mask) != mask) {
		/* Inject #GP, let the guest handle it */
		DPRINTF("%s: guest set invalid bits in %%cr4. Ones "
		    "mask=0x%llx, data=0x%llx\n", __func__,
		    curcpu()->ci_vmm_cap.vcc_vmx.vmx_cr4_fixed0,
		    r);
		vmm_inject_gp(vcpu);
		return (0);
	}

	/* CR4_VMXE must always be enabled */
	r |= CR4_VMXE;

	if (vmwrite(VMCS_GUEST_IA32_CR4, r)) {
		printf("%s: can't write guest cr4\n", __func__);
		return (EINVAL);
	}

	return (0);
}

/*
 * vmx_handle_cr
 *
 * Handle reads/writes to control registers (except CR3)
 */
int
vmx_handle_cr(struct vcpu *vcpu)
{
	uint64_t insn_length, exit_qual, r;
	uint8_t crnum, dir, reg;

	if (vmread(VMCS_INSTRUCTION_LENGTH, &insn_length)) {
		printf("%s: can't obtain instruction length\n", __func__);
		return (EINVAL);
	}

	if (vmx_get_exit_qualification(&exit_qual)) {
		printf("%s: can't get exit qual\n", __func__);
		return (EINVAL);
	}

	/* Low 4 bits of exit_qual represent the CR number */
	crnum = exit_qual & 0xf;

	/*
	 * Bits 5:4 indicate the direction of operation (or special CR-modifying
	 * instruction)
	 */
	dir = (exit_qual & 0x30) >> 4;

	/* Bits 11:8 encode the source/target register */
	reg = (exit_qual & 0xf00) >> 8;

	switch (dir) {
	case CR_WRITE:
		if (crnum == 0 || crnum == 4) {
			switch (reg) {
			case 0: r = vcpu->vc_gueststate.vg_rax; break;
			case 1: r = vcpu->vc_gueststate.vg_rcx; break;
			case 2: r = vcpu->vc_gueststate.vg_rdx; break;
			case 3: r = vcpu->vc_gueststate.vg_rbx; break;
			case 4: if (vmread(VMCS_GUEST_IA32_RSP, &r)) {
					printf("%s: unable to read guest "
					    "RSP\n", __func__);
					return (EINVAL);
				}
				break;
			case 5: r = vcpu->vc_gueststate.vg_rbp; break;
			case 6: r = vcpu->vc_gueststate.vg_rsi; break;
			case 7: r = vcpu->vc_gueststate.vg_rdi; break;
			case 8: r = vcpu->vc_gueststate.vg_r8; break;
			case 9: r = vcpu->vc_gueststate.vg_r9; break;
			case 10: r = vcpu->vc_gueststate.vg_r10; break;
			case 11: r = vcpu->vc_gueststate.vg_r11; break;
			case 12: r = vcpu->vc_gueststate.vg_r12; break;
			case 13: r = vcpu->vc_gueststate.vg_r13; break;
			case 14: r = vcpu->vc_gueststate.vg_r14; break;
			case 15: r = vcpu->vc_gueststate.vg_r15; break;
			}
			DPRINTF("%s: mov to cr%d @ %llx, data=0x%llx\n",
			    __func__, crnum, vcpu->vc_gueststate.vg_rip, r);
		}

		if (crnum == 0)
			vmx_handle_cr0_write(vcpu, r);

		if (crnum == 4)
			vmx_handle_cr4_write(vcpu, r);

		break;
	case CR_READ:
		DPRINTF("%s: mov from cr%d @ %llx\n", __func__, crnum,
		    vcpu->vc_gueststate.vg_rip);
		break;
	case CR_CLTS:
		DPRINTF("%s: clts instruction @ %llx\n", __func__,
		    vcpu->vc_gueststate.vg_rip);
		break;
	case CR_LMSW:
		DPRINTF("%s: lmsw instruction @ %llx\n", __func__,
		    vcpu->vc_gueststate.vg_rip);
		break;
	default:
		DPRINTF("%s: unknown cr access @ %llx\n", __func__,
		    vcpu->vc_gueststate.vg_rip);
	}

	vcpu->vc_gueststate.vg_rip += insn_length;

	return (0);
}

/*
 * vmx_handle_rdmsr
 *
 * Handler for rdmsr instructions. Bitmap MSRs are allowed implicit access
 * and won't end up here. This handler is primarily intended to catch otherwise
 * unknown MSR access for possible later inclusion in the bitmap list. For
 * each MSR access that ends up here, we log the access (when VMM_DEBUG is
 * enabled)
 *
 * Parameters:
 *  vcpu: vcpu structure containing instruction info causing the exit
 *
 * Return value:
 *  0: The operation was successful
 *  EINVAL: An error occurred
 */
int
vmx_handle_rdmsr(struct vcpu *vcpu)
{
	uint64_t insn_length;
	uint64_t *rax, *rdx;
	uint64_t *rcx;
	int ret;

	if (vmread(VMCS_INSTRUCTION_LENGTH, &insn_length)) {
		printf("%s: can't obtain instruction length\n", __func__);
		return (EINVAL);
	}

	if (insn_length != 2) {
		DPRINTF("%s: RDMSR with instruction length %lld not "
		    "supported\n", __func__, insn_length);
		return (EINVAL);
	}

	rax = &vcpu->vc_gueststate.vg_rax;
	rcx = &vcpu->vc_gueststate.vg_rcx;
	rdx = &vcpu->vc_gueststate.vg_rdx;

	switch (*rcx) {
	case MSR_BIOS_SIGN:
	case MSR_PLATFORM_ID:
		/* Ignored */
		*rax = 0;
		*rdx = 0;
		break;
	case MSR_CR_PAT:
		*rax = (vcpu->vc_shadow_pat & 0xFFFFFFFFULL);
		*rdx = (vcpu->vc_shadow_pat >> 32);
		break;
	default:
		/* Unsupported MSRs causes #GP exception, don't advance %rip */
		DPRINTF("%s: unsupported rdmsr (msr=0x%llx), injecting #GP\n",
		    __func__, *rcx);
		ret = vmm_inject_gp(vcpu);
		return (ret);
	}

	vcpu->vc_gueststate.vg_rip += insn_length;

	return (0);
}

/*
 * vmx_handle_xsetbv
 *
 * VMX-specific part of the xsetbv instruction exit handler
 *
 * Parameters:
 *  vcpu: vcpu structure containing instruction info causing the exit
 *
 * Return value:
 *  0: The operation was successful
 *  EINVAL: An error occurred
 */
int
vmx_handle_xsetbv(struct vcpu *vcpu)
{
	uint64_t insn_length, *rax;
	int ret;

	if (vmread(VMCS_INSTRUCTION_LENGTH, &insn_length)) {
		printf("%s: can't obtain instruction length\n", __func__);
		return (EINVAL);
	}

	/* All XSETBV instructions are 3 bytes */
	if (insn_length != 3) {
		DPRINTF("%s: XSETBV with instruction length %lld not "
		    "supported\n", __func__, insn_length);
		return (EINVAL);
	}

	rax = &vcpu->vc_gueststate.vg_rax;

	ret = vmm_handle_xsetbv(vcpu, rax);

	vcpu->vc_gueststate.vg_rip += insn_length;

	return ret;
}

/*
 * svm_handle_xsetbv
 *
 * SVM-specific part of the xsetbv instruction exit handler
 *
 * Parameters:
 *  vcpu: vcpu structure containing instruction info causing the exit
 *
 * Return value:
 *  0: The operation was successful
 *  EINVAL: An error occurred
 */
int
svm_handle_xsetbv(struct vcpu *vcpu)
{
	uint64_t insn_length, *rax;
	int ret;
	struct vmcb *vmcb = (struct vmcb *)vcpu->vc_control_va;

	/* All XSETBV instructions are 3 bytes */
	insn_length = 3;

	rax = &vmcb->v_rax;

	ret = vmm_handle_xsetbv(vcpu, rax);

	vcpu->vc_gueststate.vg_rip += insn_length;

	return ret;
}

/*
 * vmm_handle_xsetbv
 *
 * Handler for xsetbv instructions. We allow the guest VM to set xcr0 values
 * limited to the xsave_mask in use in the host.
 *
 * Parameters:
 *  vcpu: vcpu structure containing instruction info causing the exit
 *  rax: pointer to guest %rax
 *
 * Return value:
 *  0: The operation was successful
 *  EINVAL: An error occurred
 */
int
vmm_handle_xsetbv(struct vcpu *vcpu, uint64_t *rax)
{
	uint64_t *rdx, *rcx, val, mask = xsave_mask & XFEATURE_XCR0_MASK;

	rcx = &vcpu->vc_gueststate.vg_rcx;
	rdx = &vcpu->vc_gueststate.vg_rdx;

	if (vmm_get_guest_cpu_cpl(vcpu) != 0) {
		DPRINTF("%s: guest cpl not zero\n", __func__);
		return (vmm_inject_gp(vcpu));
	}

	if (*rcx != 0) {
		DPRINTF("%s: guest specified invalid xcr register number "
		    "%lld\n", __func__, *rcx);
		return (vmm_inject_gp(vcpu));
	}

	/* If we're exposing PKRU features, allow guests to set PKRU in xcr0. */
	if (vmm_softc->sc_md.pkru_enabled)
		mask |= XFEATURE_PKRU;

	val = *rax + (*rdx << 32);
	if (val & ~mask) {
		DPRINTF("%s: guest specified xcr0 outside xsave_mask %lld\n",
		    __func__, val);
		return (vmm_inject_gp(vcpu));
	}

	vcpu->vc_gueststate.vg_xcr0 = val;

	return (0);
}

/*
 * vmx_handle_misc_enable_msr
 *
 * Handler for writes to the MSR_MISC_ENABLE (0x1a0) MSR on Intel CPUs. We
 * limit what the guest can write to this MSR (certain hardware-related
 * settings like speedstep, etc).
 *
 * Parameters:
 *  vcpu: vcpu structure containing information about the wrmsr causing this
 *   exit
 */
void
vmx_handle_misc_enable_msr(struct vcpu *vcpu)
{
	uint64_t *rax, *rdx;
	struct vmx_msr_store *msr_store;

	rax = &vcpu->vc_gueststate.vg_rax;
	rdx = &vcpu->vc_gueststate.vg_rdx;
	msr_store = (struct vmx_msr_store *)vcpu->vc_vmx_msr_exit_save_va;

	/* Filter out guest writes to TCC, EIST, and xTPR */
	*rax &= ~(MISC_ENABLE_TCC | MISC_ENABLE_EIST_ENABLED |
	    MISC_ENABLE_xTPR_MESSAGE_DISABLE);

	msr_store[VCPU_REGS_MISC_ENABLE].vms_data = *rax | (*rdx << 32);
}

/*
 * vmx_handle_wrmsr
 *
 * Handler for wrmsr instructions. This handler logs the access, and discards
 * the written data (when VMM_DEBUG is enabled). Any valid wrmsr will not end
 * up here (it will be whitelisted in the MSR bitmap).
 *
 * Parameters:
 *  vcpu: vcpu structure containing instruction info causing the exit
 *
 * Return value:
 *  0: The operation was successful
 *  EINVAL: An error occurred
 */
int
vmx_handle_wrmsr(struct vcpu *vcpu)
{
	uint64_t insn_length, val;
	uint64_t *rax, *rdx, *rcx;
	int ret;

	if (vmread(VMCS_INSTRUCTION_LENGTH, &insn_length)) {
		printf("%s: can't obtain instruction length\n", __func__);
		return (EINVAL);
	}

	if (insn_length != 2) {
		DPRINTF("%s: WRMSR with instruction length %lld not "
		    "supported\n", __func__, insn_length);
		return (EINVAL);
	}

	rax = &vcpu->vc_gueststate.vg_rax;
	rcx = &vcpu->vc_gueststate.vg_rcx;
	rdx = &vcpu->vc_gueststate.vg_rdx;
	val = (*rdx << 32) | (*rax & 0xFFFFFFFFULL);

	switch (*rcx) {
	case MSR_CR_PAT:
		if (!vmm_pat_is_valid(val)) {
			ret = vmm_inject_gp(vcpu);
			return (ret);
		}
		vcpu->vc_shadow_pat = val;
		break;
	case MSR_MISC_ENABLE:
		vmx_handle_misc_enable_msr(vcpu);
		break;
	case MSR_SMM_MONITOR_CTL:
		/*
		 * 34.15.5 - Enabling dual monitor treatment
		 *
		 * Unsupported, so inject #GP and return without
		 * advancing %rip.
		 */
		ret = vmm_inject_gp(vcpu);
		return (ret);
	case KVM_MSR_SYSTEM_TIME:
		vmm_init_pvclock(vcpu,
		    (*rax & 0xFFFFFFFFULL) | (*rdx  << 32));
		break;
	case KVM_MSR_WALL_CLOCK:
		vmm_pv_wall_clock(vcpu,
		    (*rax & 0xFFFFFFFFULL) | (*rdx  << 32));
		break;
#ifdef VMM_DEBUG
	default:
		/*
		 * Log the access, to be able to identify unknown MSRs
		 */
		DPRINTF("%s: wrmsr exit, msr=0x%llx, discarding data "
		    "written from guest=0x%llx:0x%llx\n", __func__,
		    *rcx, *rdx, *rax);
#endif /* VMM_DEBUG */
	}

	vcpu->vc_gueststate.vg_rip += insn_length;

	return (0);
}

/*
 * svm_handle_msr
 *
 * Handler for MSR instructions.
 *
 * Parameters:
 *  vcpu: vcpu structure containing instruction info causing the exit
 *
 * Return value:
 *  Always 0 (successful)
 */
int
svm_handle_msr(struct vcpu *vcpu)
{
	uint64_t insn_length, val;
	uint64_t *rax, *rcx, *rdx;
	struct vmcb *vmcb = (struct vmcb *)vcpu->vc_control_va;
	int ret;

	/* XXX: Validate RDMSR / WRMSR insn_length */
	insn_length = 2;

	rax = &vmcb->v_rax;
	rcx = &vcpu->vc_gueststate.vg_rcx;
	rdx = &vcpu->vc_gueststate.vg_rdx;

	if (vmcb->v_exitinfo1 == 1) {
		/* WRMSR */
		val = (*rdx << 32) | (*rax & 0xFFFFFFFFULL);

		switch (*rcx) {
		case MSR_CR_PAT:
			if (!vmm_pat_is_valid(val)) {
				ret = vmm_inject_gp(vcpu);
				return (ret);
			}
			vcpu->vc_shadow_pat = val;
			break;
		case MSR_EFER:
			vmcb->v_efer = *rax | EFER_SVME;
			break;
		case KVM_MSR_SYSTEM_TIME:
			vmm_init_pvclock(vcpu,
			    (*rax & 0xFFFFFFFFULL) | (*rdx  << 32));
			break;
		case KVM_MSR_WALL_CLOCK:
			vmm_pv_wall_clock(vcpu,
			    (*rax & 0xFFFFFFFFULL) | (*rdx  << 32));
			break;
		default:
			/* Log the access, to be able to identify unknown MSRs */
			DPRINTF("%s: wrmsr exit, msr=0x%llx, discarding data "
			    "written from guest=0x%llx:0x%llx\n", __func__,
			    *rcx, *rdx, *rax);
		}
	} else {
		/* RDMSR */
		switch (*rcx) {
		case MSR_BIOS_SIGN:
		case MSR_INT_PEN_MSG:
		case MSR_PLATFORM_ID:
			/* Ignored */
			*rax = 0;
			*rdx = 0;
			break;
		case MSR_CR_PAT:
			*rax = (vcpu->vc_shadow_pat & 0xFFFFFFFFULL);
			*rdx = (vcpu->vc_shadow_pat >> 32);
			break;
		case MSR_DE_CFG:
			/* LFENCE serializing bit is set by host */
			*rax = DE_CFG_SERIALIZE_LFENCE;
			*rdx = 0;
			break;
		default:
			/*
			 * Unsupported MSRs causes #GP exception, don't advance
			 * %rip
			 */
			DPRINTF("%s: unsupported rdmsr (msr=0x%llx), "
			    "injecting #GP\n", __func__, *rcx);
			ret = vmm_inject_gp(vcpu);
			return (ret);
		}
	}

	vcpu->vc_gueststate.vg_rip += insn_length;

	return (0);
}

/* Handle cpuid(0xd) and its subleafs */
static void
vmm_handle_cpuid_0xd(struct vcpu *vcpu, uint32_t subleaf, uint64_t *rax,
    uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx)
{
	uint64_t xcr0 = vcpu->vc_gueststate.vg_xcr0;

	if (subleaf == 0) {
		/*
		 * CPUID(0xd.0) depends on the value in XCR0 and MSR_XSS.  If
		 * the guest XCR0 isn't the same as the host then set it, redo
		 * the CPUID, and restore it.
		 */
		/*
		 * "ecx enumerates the size required ... for an area
		 *  containing all the ... components supported by this
		 *  processor"
		 * "ebx enumerates the size required ... for an area
		 *  containing all the ... components corresponding to bits
		 *  currently set in xcr0"
		 * So: since the VMM 'processor' is what our base kernel uses,
		 * the VMM ecx is our ebx
		 */
		ecx = ebx;
		if (xcr0 != (xsave_mask & XFEATURE_XCR0_MASK)) {
			uint32_t dummy;
			xsetbv(0, xcr0);
			CPUID_LEAF(0xd, subleaf, eax, ebx, dummy, edx);
			xsetbv(0, xsave_mask & XFEATURE_XCR0_MASK);
		}
		eax = xsave_mask & XFEATURE_XCR0_MASK;
		edx = (xsave_mask & XFEATURE_XCR0_MASK) >> 32;

		/*
		 * Emulate support for the pkru xsave region if the
		 * host has pku enabled. This allow guests to enable
		 * it in xcr0 and use xsave/xrstor on context switches
		 * to save or restore pkru.
		 */
		if (vmm_softc->sc_md.pkru_enabled) {
			eax |= XFEATURE_PKRU;
			ecx = sizeof(struct savefpu) + sizeof(uint64_t);
			if (xcr0 & XFEATURE_PKRU)
				ebx = ecx;
		}
	} else if (subleaf == 1) {
		/* mask out XSAVEC, XSAVES, and XFD support */
		eax &= XSAVE_XSAVEOPT | XSAVE_XGETBV1;
		ebx = 0;	/* no xsavec or xsaves for now */
		ecx = edx = 0;	/* no xsaves for now */
	} else if ((1ULL << subleaf) == XFEATURE_PKRU) {
		if (vmm_softc->sc_md.pkru_enabled) {
			eax = sizeof(uint64_t);		/* size of PKRU area */
			ebx = sizeof(struct savefpu);	/* offset of area */
		} else
			eax = ebx = 0;
		ecx = edx = 0;
	} else if (subleaf >= 63 ||
	    ((1ULL << subleaf) & xsave_mask & XFEATURE_XCR0_MASK) == 0) {
		/* disclaim subleaves of features we don't expose */
		eax = ebx = ecx = edx = 0;
	} else {
		/* disclaim compressed alignment or xfd support */
		ecx = 0;
	}

	*rax = eax;
	vcpu->vc_gueststate.vg_rbx = ebx;
	vcpu->vc_gueststate.vg_rcx = ecx;
	vcpu->vc_gueststate.vg_rdx = edx;
}

/*
 * vmm_handle_cpuid
 *
 * Exit handler for CPUID instruction
 *
 * Parameters:
 *  vcpu: vcpu causing the CPUID exit
 *
 * Return value:
 *  0: the exit was processed successfully
 *  EINVAL: error occurred validating the CPUID instruction arguments
 */
int
vmm_handle_cpuid(struct vcpu *vcpu)
{
	uint64_t insn_length, cr4;
	uint64_t *rax, *rbx, *rcx, *rdx;
	struct vmcb *vmcb;
	uint32_t leaf, subleaf, eax, ebx, ecx, edx;
	struct vmx_msr_store *msr_store;
	int vmm_cpuid_level;

	/* what's the cpuid level we support/advertise? */
	vmm_cpuid_level = cpuid_level;
	if (vmm_cpuid_level < 0x15 && tsc_is_invariant)
		vmm_cpuid_level = 0x15;

	if (vmm_softc->mode == VMM_MODE_EPT) {
		if (vmread(VMCS_INSTRUCTION_LENGTH, &insn_length)) {
			DPRINTF("%s: can't obtain instruction length\n",
			    __func__);
			return (EINVAL);
		}

		if (vmread(VMCS_GUEST_IA32_CR4, &cr4)) {
			DPRINTF("%s: can't obtain cr4\n", __func__);
			return (EINVAL);
		}

		rax = &vcpu->vc_gueststate.vg_rax;

		/*
		 * "CPUID leaves above 02H and below 80000000H are only
		 * visible when IA32_MISC_ENABLE MSR has bit 22 set to its
		 * default value 0"
		 */
		msr_store =
		    (struct vmx_msr_store *)vcpu->vc_vmx_msr_exit_save_va;
		if (msr_store[VCPU_REGS_MISC_ENABLE].vms_data &
		    MISC_ENABLE_LIMIT_CPUID_MAXVAL)
			vmm_cpuid_level = 0x02;
	} else {
		/* XXX: validate insn_length 2 */
		insn_length = 2;
		vmcb = (struct vmcb *)vcpu->vc_control_va;
		rax = &vmcb->v_rax;
		cr4 = vmcb->v_cr4;
	}

	rbx = &vcpu->vc_gueststate.vg_rbx;
	rcx = &vcpu->vc_gueststate.vg_rcx;
	rdx = &vcpu->vc_gueststate.vg_rdx;
	vcpu->vc_gueststate.vg_rip += insn_length;

	leaf = *rax;
	subleaf = *rcx;

	/*
	 * "If a value entered for CPUID.EAX is higher than the maximum input
	 *  value for basic or extended function for that processor then the
	 *  data for the highest basic information leaf is returned."
	 *
	 * "When CPUID returns the highest basic leaf information as a result
	 *  of an invalid input EAX value, any dependence on input ECX value
	 *  in the basic leaf is honored."
	 *
	 * This means if leaf is between vmm_cpuid_level and 0x40000000 (the start
	 * of the hypervisor info leaves), clamp to vmm_cpuid_level, but without
	 * altering subleaf.  Also, if leaf is greater than the extended function
	 * info, clamp also to vmm_cpuid_level.
	 */
	if ((leaf > vmm_cpuid_level && leaf < 0x40000000) ||
	    (leaf > curcpu()->ci_pnfeatset)) {
		DPRINTF("%s: invalid cpuid input leaf 0x%x, guest rip="
		    "0x%llx - resetting to 0x%x\n", __func__, leaf,
		    vcpu->vc_gueststate.vg_rip - insn_length,
		    vmm_cpuid_level);
		leaf = vmm_cpuid_level;
	}

	/* we fake up values in the range (cpuid_level, vmm_cpuid_level] */
	if (leaf <= cpuid_level || leaf > 0x80000000)
		CPUID_LEAF(leaf, subleaf, eax, ebx, ecx, edx);
	else
		eax = ebx = ecx = edx = 0;

	switch (leaf) {
	case 0x00:	/* Max level and vendor ID */
		*rax = vmm_cpuid_level;
		*rbx = *((uint32_t *)&cpu_vendor);
		*rdx = *((uint32_t *)&cpu_vendor + 1);
		*rcx = *((uint32_t *)&cpu_vendor + 2);
		break;
	case 0x01:	/* Version, brand, feature info */
		*rax = cpu_id;
		/* mask off host's APIC ID, reset to vcpu id */
		*rbx = cpu_ebxfeature & 0x0000FFFF;
		*rbx |= (vcpu->vc_id & 0xFF) << 24;
		*rcx = (cpu_ecxfeature | CPUIDECX_HV) & VMM_CPUIDECX_MASK;

		/* Guest CR4.OSXSAVE determines presence of CPUIDECX_OSXSAVE */
		if (cr4 & CR4_OSXSAVE)
			*rcx |= CPUIDECX_OSXSAVE;
		else
			*rcx &= ~CPUIDECX_OSXSAVE;

		*rdx = curcpu()->ci_feature_flags & VMM_CPUIDEDX_MASK;
		break;
	case 0x02:	/* Cache and TLB information */
		*rax = eax;
		*rbx = ebx;
		*rcx = ecx;
		*rdx = edx;
		break;
	case 0x03:	/* Processor serial number (not supported) */
		DPRINTF("%s: function 0x03 (processor serial number) not "
		"supported\n", __func__);
		*rax = 0;
		*rbx = 0;
		*rcx = 0;
		*rdx = 0;
		break;
	case 0x04:	/* Deterministic cache info */
		*rax = eax & VMM_CPUID4_CACHE_TOPOLOGY_MASK;
		*rbx = ebx;
		*rcx = ecx;
		*rdx = edx;
		break;
	case 0x05:	/* MONITOR/MWAIT (not supported) */
		DPRINTF("%s: function 0x05 (monitor/mwait) not supported\n",
		    __func__);
		*rax = 0;
		*rbx = 0;
		*rcx = 0;
		*rdx = 0;
		break;
	case 0x06:	/* Thermal / Power management (not supported) */
		DPRINTF("%s: function 0x06 (thermal/power mgt) not supported\n",
		    __func__);
		*rax = 0;
		*rbx = 0;
		*rcx = 0;
		*rdx = 0;
		break;
	case 0x07:	/* SEFF */
		if (subleaf == 0) {
			*rax = 0;	/* Highest subleaf supported */
			*rbx = curcpu()->ci_feature_sefflags_ebx & VMM_SEFF0EBX_MASK;
			*rcx = curcpu()->ci_feature_sefflags_ecx & VMM_SEFF0ECX_MASK;
			*rdx = curcpu()->ci_feature_sefflags_edx & VMM_SEFF0EDX_MASK;
			/*
			 * Only expose PKU support if we've detected it in use
			 * on the host.
			 */
			if (vmm_softc->sc_md.pkru_enabled)
				*rcx |= SEFF0ECX_PKU;
			else
				*rcx &= ~SEFF0ECX_PKU;

			/* Expose IBT bit if we've enabled CET on the host. */
			if (rcr4() & CR4_CET)
				*rdx |= SEFF0EDX_IBT;
			else
				*rdx &= ~SEFF0EDX_IBT;

		} else {
			/* Unsupported subleaf */
			DPRINTF("%s: function 0x07 (SEFF) unsupported subleaf "
			    "0x%x not supported\n", __func__, subleaf);
			*rax = 0;
			*rbx = 0;
			*rcx = 0;
			*rdx = 0;
		}
		break;
	case 0x09:	/* Direct Cache Access (not supported) */
		DPRINTF("%s: function 0x09 (direct cache access) not "
		    "supported\n", __func__);
		*rax = 0;
		*rbx = 0;
		*rcx = 0;
		*rdx = 0;
		break;
	case 0x0a:	/* Architectural perf monitoring (not supported) */
		DPRINTF("%s: function 0x0a (arch. perf mon) not supported\n",
		    __func__);
		*rax = 0;
		*rbx = 0;
		*rcx = 0;
		*rdx = 0;
		break;
	case 0x0b:	/* Extended topology enumeration (not supported) */
		DPRINTF("%s: function 0x0b (topology enumeration) not "
		    "supported\n", __func__);
		*rax = 0;
		*rbx = 0;
		*rcx = 0;
		*rdx = 0;
		break;
	case 0x0d:	/* Processor ext. state information */
		vmm_handle_cpuid_0xd(vcpu, subleaf, rax, eax, ebx, ecx, edx);
		break;
	case 0x0f:	/* QoS info (not supported) */
		DPRINTF("%s: function 0x0f (QoS info) not supported\n",
		    __func__);
		*rax = 0;
		*rbx = 0;
		*rcx = 0;
		*rdx = 0;
		break;
	case 0x14:	/* Processor Trace info (not supported) */
		DPRINTF("%s: function 0x14 (processor trace info) not "
		    "supported\n", __func__);
		*rax = 0;
		*rbx = 0;
		*rcx = 0;
		*rdx = 0;
		break;
	case 0x15:
		if (cpuid_level >= 0x15) {
			*rax = eax;
			*rbx = ebx;
			*rcx = ecx;
			*rdx = edx;
		} else {
			KASSERT(tsc_is_invariant);
			*rax = 1;
			*rbx = 100;
			*rcx = tsc_frequency / 100;
			*rdx = 0;
		}
		break;
	case 0x16:	/* Processor frequency info */
		*rax = eax;
		*rbx = ebx;
		*rcx = ecx;
		*rdx = edx;
		break;
	case 0x40000000:	/* Hypervisor information */
		*rax = 0;
		*rbx = *((uint32_t *)&vmm_hv_signature[0]);
		*rcx = *((uint32_t *)&vmm_hv_signature[4]);
		*rdx = *((uint32_t *)&vmm_hv_signature[8]);
		break;
	case 0x40000001:	/* KVM hypervisor features */
		*rax = (1 << KVM_FEATURE_CLOCKSOURCE2) |
		    (1 << KVM_FEATURE_CLOCKSOURCE_STABLE_BIT);
		*rbx = 0;
		*rcx = 0;
		*rdx = 0;
		break;
	case 0x40000100:	/* Hypervisor information KVM */
		*rax = 0x40000101;
		*rbx = *((uint32_t *)&kvm_hv_signature[0]);
		*rcx = *((uint32_t *)&kvm_hv_signature[4]);
		*rdx = *((uint32_t *)&kvm_hv_signature[8]);
		break;
	case 0x40000101:	/* KVM hypervisor features */
		*rax = (1 << KVM_FEATURE_CLOCKSOURCE2) |
		    (1 << KVM_FEATURE_CLOCKSOURCE_STABLE_BIT) |
		    (1 << KVM_FEATURE_NOP_IO_DELAY);
		*rbx = 0;
		*rcx = 0;
		*rdx = 0;
		break;
	case 0x80000000:	/* Extended function level */
		/* We don't emulate past 0x8000001f currently. */
		*rax = min(curcpu()->ci_pnfeatset, 0x8000001f);
		*rbx = 0;
		*rcx = 0;
		*rdx = 0;
		break;
	case 0x80000001:	/* Extended function info */
		*rax = curcpu()->ci_efeature_eax;
		*rbx = 0;	/* Reserved */
		*rcx = curcpu()->ci_efeature_ecx & VMM_ECPUIDECX_MASK;
		*rdx = curcpu()->ci_feature_eflags & VMM_FEAT_EFLAGS_MASK;
		break;
	case 0x80000002:	/* Brand string */
		*rax = curcpu()->ci_brand[0];
		*rbx = curcpu()->ci_brand[1];
		*rcx = curcpu()->ci_brand[2];
		*rdx = curcpu()->ci_brand[3];
		break;
	case 0x80000003:	/* Brand string */
		*rax = curcpu()->ci_brand[4];
		*rbx = curcpu()->ci_brand[5];
		*rcx = curcpu()->ci_brand[6];
		*rdx = curcpu()->ci_brand[7];
		break;
	case 0x80000004:	/* Brand string */
		*rax = curcpu()->ci_brand[8];
		*rbx = curcpu()->ci_brand[9];
		*rcx = curcpu()->ci_brand[10];
		*rdx = curcpu()->ci_brand[11];
		break;
	case 0x80000005:	/* Reserved (Intel), cacheinfo (AMD) */
		*rax = eax;
		*rbx = ebx;
		*rcx = ecx;
		*rdx = edx;
		break;
	case 0x80000006:	/* ext. cache info */
		*rax = eax;
		*rbx = ebx;
		*rcx = ecx;
		*rdx = edx;
		break;
	case 0x80000007:	/* apmi */
		*rax = eax;
		*rbx = ebx;
		*rcx = ecx;
		*rdx = edx & VMM_APMI_EDX_INCLUDE_MASK;
		break;
	case 0x80000008:	/* Phys bits info and topology (AMD) */
		*rax = eax;
		*rbx = ebx & VMM_AMDSPEC_EBX_MASK;
		/* Reset %rcx (topology) */
		*rcx = 0;
		*rdx = edx;
		break;
	case 0x8000001d:	/* cache topology (AMD) */
		*rax = eax;
		*rbx = ebx;
		*rcx = ecx;
		*rdx = edx;
		break;
	case 0x8000001f:	/* encryption features (AMD) */
		*rax = eax;
		*rbx = ebx;
		*rcx = ecx;
		*rdx = edx;
		break;
	default:
		DPRINTF("%s: unsupported rax=0x%llx\n", __func__, *rax);
		*rax = 0;
		*rbx = 0;
		*rcx = 0;
		*rdx = 0;
	}


	if (vmm_softc->mode == VMM_MODE_RVI) {
		/*
		 * update %rax. the rest of the registers get updated in
		 * svm_enter_guest
		 */
		vmcb->v_rax = *rax;
	}

	return (0);
}

/*
 * vcpu_run_svm
 *
 * SVM main loop used to run a VCPU.
 *
 * Parameters:
 *  vcpu: The VCPU to run
 *  vrp: run parameters
 *
 * Return values:
 *  0: The run loop exited and no help is needed from vmd
 *  EAGAIN: The run loop exited and help from vmd is needed
 *  EINVAL: an error occurred
 */
int
vcpu_run_svm(struct vcpu *vcpu, struct vm_run_params *vrp)
{
	int ret = 0;
	struct region_descriptor gdt;
	struct cpu_info *ci = NULL;
	uint64_t exit_reason;
	struct schedstate_percpu *spc;
	struct vmcb *vmcb = (struct vmcb *)vcpu->vc_control_va;

	if (vrp->vrp_intr_pending)
		vcpu->vc_intr = 1;
	else
		vcpu->vc_intr = 0;

	/*
	 * If we are returning from userspace (vmd) because we exited
	 * last time, fix up any needed vcpu state first. Which state
	 * needs to be fixed up depends on what vmd populated in the
	 * exit data structure.
	 */
	switch (vcpu->vc_gueststate.vg_exit_reason) {
	case SVM_VMEXIT_IOIO:
		if (vcpu->vc_exit.vei.vei_dir == VEI_DIR_IN) {
			vcpu->vc_gueststate.vg_rax =
			    vcpu->vc_exit.vei.vei_data;
			vmcb->v_rax = vcpu->vc_gueststate.vg_rax;
		}
		vcpu->vc_gueststate.vg_rip =
		    vcpu->vc_exit.vrs.vrs_gprs[VCPU_REGS_RIP];
		vmcb->v_rip = vcpu->vc_gueststate.vg_rip;
		if (svm_vmgexit_sync_guest(vcpu))
			return (EINVAL);
		break;
	case SVM_VMEXIT_NPF:
		ret = vcpu_writeregs_svm(vcpu, VM_RWREGS_GPRS,
		    &vcpu->vc_exit.vrs);
		if (ret) {
			printf("%s: vm %d vcpu %d failed to update "
			    "registers\n", __func__,
			    vcpu->vc_parent->vm_id, vcpu->vc_id);
			return (EINVAL);
		}
		break;
	}
	memset(&vcpu->vc_exit, 0, sizeof(vcpu->vc_exit));

	while (ret == 0) {
		vmm_update_pvclock(vcpu);
		if (ci != curcpu()) {
			/*
			 * We are launching for the first time, or we are
			 * resuming from a different pcpu, so we need to
			 * reset certain pcpu-specific values.
			 */
			ci = curcpu();
			setregion(&gdt, ci->ci_gdt, GDT_SIZE - 1);

			if (ci != vcpu->vc_last_pcpu) {
				/*
				 * Flush TLB by guest ASID if feature
				 * available, flush entire TLB if not.
				 */
				if (ci->ci_vmm_cap.vcc_svm.svm_flush_by_asid)
					vmcb->v_tlb_control =
					    SVM_TLB_CONTROL_FLUSH_ASID;
				else
					vmcb->v_tlb_control =
					    SVM_TLB_CONTROL_FLUSH_ALL;

				svm_set_dirty(vcpu, SVM_CLEANBITS_ALL);
			}

			vcpu->vc_last_pcpu = ci;

			if (gdt.rd_base == 0) {
				ret = EINVAL;
				break;
			}
		}

		/* Handle vmd(8) injected interrupts */
		/* Is there an interrupt pending injection? */
		if (vcpu->vc_inject.vie_type == VCPU_INJECT_INTR &&
		    vcpu->vc_irqready) {
			vmcb->v_eventinj = vcpu->vc_inject.vie_vector |
			    (1U << 31);
			vcpu->vc_inject.vie_type = VCPU_INJECT_NONE;
		}

		/* Inject event if present */
		if (vcpu->vc_inject.vie_type == VCPU_INJECT_EX) {
			vmcb->v_eventinj = vcpu->vc_inject.vie_vector;

			/* Set the "Event Valid" flag for certain vectors */
			switch (vcpu->vc_inject.vie_vector) {
			case VMM_EX_BP:
			case VMM_EX_OF:
			case VMM_EX_DB:
				/*
				 * Software exception.
				 * XXX check nRIP support.
				 */
				vmcb->v_eventinj |= (4ULL << 8);
				break;
			case VMM_EX_AC:
				vcpu->vc_inject.vie_errorcode = 0;
				/* fallthrough */
			case VMM_EX_DF:
			case VMM_EX_TS:
			case VMM_EX_NP:
			case VMM_EX_SS:
			case VMM_EX_GP:
			case VMM_EX_PF:
				/* Hardware exception. */
				vmcb->v_eventinj |= (3ULL << 8);

				if (vmcb->v_cr0 & CR0_PE) {
					/* Error code valid. */
					vmcb->v_eventinj |= (1ULL << 11);
					vmcb->v_eventinj |= (uint64_t)
					    vcpu->vc_inject.vie_errorcode << 32;
				}
				break;
			default:
				printf("%s: unsupported exception vector %u\n",
				    __func__, vcpu->vc_inject.vie_vector);
				ret = EINVAL;
			} /* switch */
			if (ret == EINVAL)
				break;

			/* Event is valid. */
			vmcb->v_eventinj |= (1U << 31);
			vcpu->vc_inject.vie_type = VCPU_INJECT_NONE;
		}

		TRACEPOINT(vmm, guest_enter, vcpu, vrp);

		/* Start / resume the VCPU */
		/* Disable interrupts and save the current host FPU state. */
		clgi();
		if ((ret = vmm_fpurestore(vcpu))) {
			stgi();
			break;
		}

		/*
		 * If we're resuming to a different VCPU and have IBPB,
		 * then use it to prevent cross-VM branch-target injection.
		 */
		if (ci->ci_guest_vcpu != vcpu &&
		    (ci->ci_feature_amdspec_ebx & CPUIDEBX_IBPB)) {
			wrmsr(MSR_PRED_CMD, PRED_CMD_IBPB);
			ci->ci_guest_vcpu = vcpu;
		}

		/* Restore any guest PKRU state. */
		if (vmm_softc->sc_md.pkru_enabled)
			wrpkru(0, vcpu->vc_pkru);

		KASSERT(vmcb->v_intercept1 & SVM_INTERCEPT_INTR);
		wrmsr(MSR_AMD_VM_HSAVE_PA, vcpu->vc_svm_hsa_pa);

		if (vcpu->vc_seves) {
			ret = svm_seves_enter_guest(vcpu->vc_control_pa,
			    vcpu->vc_svm_hsa_va + SVM_HSA_OFFSET, &gdt);
		} else {
			ret = svm_enter_guest(vcpu->vc_control_pa,
			    &vcpu->vc_gueststate, &gdt);
		}

		/* Restore host PKRU state. */
		if (vmm_softc->sc_md.pkru_enabled) {
			vcpu->vc_pkru = rdpkru(0);
			wrpkru(0, PGK_VALUE);
		}

		/*
		 * On exit, interrupts are disabled, and we are running with
		 * the guest FPU state still possibly on the CPU. Save the FPU
		 * state before re-enabling interrupts.
		 */
		vmm_fpusave(vcpu);

		/*
		 * Enable interrupts now. Note that if the exit was due to INTR
		 * (external interrupt), the interrupt will be processed now.
		 */
		stgi();

		vcpu->vc_gueststate.vg_rip = vmcb->v_rip;
		vmcb->v_tlb_control = SVM_TLB_CONTROL_FLUSH_NONE;
		svm_set_clean(vcpu, SVM_CLEANBITS_ALL);

		/* If we exited successfully ... */
		if (ret == 0) {
			exit_reason = vmcb->v_exitcode;
			vcpu->vc_gueststate.vg_exit_reason = exit_reason;
			TRACEPOINT(vmm, guest_exit, vcpu, vrp, exit_reason);

			vcpu->vc_gueststate.vg_rflags = vmcb->v_rflags;

			/*
			 * Handle the exit. This will alter "ret" to EAGAIN if
			 * the exit handler determines help from vmd is needed.
			 */
			ret = svm_handle_exit(vcpu);

			if (svm_get_iflag(vcpu, vcpu->vc_gueststate.vg_rflags))
				vcpu->vc_irqready = 1;
			else
				vcpu->vc_irqready = 0;

			/*
			 * If not ready for interrupts, but interrupts pending,
			 * enable interrupt window exiting.
			 */
			if (vcpu->vc_irqready == 0 && vcpu->vc_intr) {
				vmcb->v_intercept1 |= SVM_INTERCEPT_VINTR;
				vmcb->v_irq = 1;
				vmcb->v_intr_misc = SVM_INTR_MISC_V_IGN_TPR;
				vmcb->v_intr_vector = 0;
				svm_set_dirty(vcpu, SVM_CLEANBITS_TPR |
				    SVM_CLEANBITS_I);
			}

			/*
			 * Exit to vmd if we are terminating, failed to enter,
			 * or need help (device I/O)
			 */
			if (ret || vcpu_must_stop(vcpu))
				break;

			if (vcpu->vc_intr && vcpu->vc_irqready) {
				ret = EAGAIN;
				break;
			}

			/* Check if we should yield - don't hog the cpu */
			spc = &ci->ci_schedstate;
			if (spc->spc_schedflags & SPCF_SHOULDYIELD)
				break;
		}
	}

	/*
	 * We are heading back to userspace (vmd), either because we need help
	 * handling an exit, a guest interrupt is pending, or we failed in some
	 * way to enter the guest. Copy the guest registers to the exit struct
	 * and return to vmd.
	 */
	if (vcpu_readregs_svm(vcpu, VM_RWREGS_ALL, &vcpu->vc_exit.vrs))
		ret = EINVAL;

	return (ret);
}

/*
 * vmm_alloc_vpid_vcpu
 *
 * Sets the memory location pointed to by "vpid" to the next available VPID
 * or ASID. For SEV-ES consider minimum ASID value for non-ES enabled guests.
 *
 * Parameters:
 *  vpid: Pointer to location to receive the next VPID/ASID
 *  vcpu: Pointer to VCPU data structure
 *
 * Return Values:
 *  0: The operation completed successfully
 *  ENOMEM: No VPIDs/ASIDs were available. Content of 'vpid' is unchanged.
 */
int
vmm_alloc_vpid_vcpu(uint16_t *vpid, struct vcpu *vcpu)
{
	uint16_t i, minasid;
	uint8_t idx, bit;
	struct vmm_softc *sc = vmm_softc;

	rw_enter_write(&vmm_softc->vpid_lock);
	if (vcpu == NULL || vcpu->vc_seves || amd64_min_noes_asid == 0)
		minasid = 1;
	else
		minasid = amd64_min_noes_asid;
	for (i = minasid; i <= sc->max_vpid; i++) {
		idx = i / 8;
		bit = i - (idx * 8);

		if (!(sc->vpids[idx] & (1 << bit))) {
			sc->vpids[idx] |= (1 << bit);
			*vpid = i;
			DPRINTF("%s: allocated VPID/ASID %d\n", __func__,
			    i);
			rw_exit_write(&vmm_softc->vpid_lock);
			return 0;
		}
	}

	printf("%s: no available %ss\n", __func__,
	    (sc->mode == VMM_MODE_EPT) ? "VPID" :
	    "ASID");

	rw_exit_write(&vmm_softc->vpid_lock);
	return ENOMEM;
}

int
vmm_alloc_vpid(uint16_t *vpid)
{
	return vmm_alloc_vpid_vcpu(vpid, NULL);
}

int
vmm_alloc_asid(uint16_t *asid, struct vcpu *vcpu)
{
	return vmm_alloc_vpid_vcpu(asid, vcpu);
}

/*
 * vmm_free_vpid
 *
 * Frees the VPID/ASID id supplied in "vpid".
 *
 * Parameters:
 *  vpid: VPID/ASID to free.
 */
void
vmm_free_vpid(uint16_t vpid)
{
	uint8_t idx, bit;
	struct vmm_softc *sc = vmm_softc;

	rw_enter_write(&vmm_softc->vpid_lock);
	idx = vpid / 8;
	bit = vpid - (idx * 8);
	sc->vpids[idx] &= ~(1 << bit);

	DPRINTF("%s: freed VPID/ASID %d\n", __func__, vpid);
	rw_exit_write(&vmm_softc->vpid_lock);
}


/* vmm_gpa_is_valid
 *
 * Check if the given gpa is within guest memory space.
 *
 * Parameters:
 *	vcpu: The virtual cpu we are running on.
 *	gpa: The address to check.
 *	obj_size: The size of the object assigned to gpa
 *
 * Return values:
 *	1: gpa is within the memory ranges allocated for the vcpu
 *	0: otherwise
 */
int
vmm_gpa_is_valid(struct vcpu *vcpu, paddr_t gpa, size_t obj_size)
{
	struct vm *vm = vcpu->vc_parent;
	struct vm_mem_range *vmr;
	size_t i;

	for (i = 0; i < vm->vm_nmemranges; ++i) {
		vmr = &vm->vm_memranges[i];
		if (vmr->vmr_size >= obj_size &&
		    vmr->vmr_gpa <= gpa &&
		    gpa < (vmr->vmr_gpa + vmr->vmr_size - obj_size)) {
		    return 1;
		}
	}
	return 0;
}

void
vmm_init_pvclock(struct vcpu *vcpu, paddr_t gpa)
{
	paddr_t pvclock_gpa = gpa & 0xFFFFFFFFFFFFFFF0;
	if (!vmm_gpa_is_valid(vcpu, pvclock_gpa,
		sizeof(struct pvclock_time_info))) {
		/* XXX: Kill guest? */
		vmm_inject_gp(vcpu);
		return;
	}

	/* XXX: handle case when this struct goes over page boundaries */
	if ((pvclock_gpa & PAGE_MASK) + sizeof(struct pvclock_time_info) >
	    PAGE_SIZE) {
		vmm_inject_gp(vcpu);
		return;
	}

	vcpu->vc_pvclock_system_gpa = gpa;
	if (tsc_frequency > 0)
		vcpu->vc_pvclock_system_tsc_mul =
		    (int) ((1000000000L << 20) / tsc_frequency);
	else
		vcpu->vc_pvclock_system_tsc_mul = 0;
	vmm_update_pvclock(vcpu);
}

int
vmm_update_pvclock(struct vcpu *vcpu)
{
	struct pvclock_time_info *pvclock_ti;
	struct timespec tv;
	struct vm *vm = vcpu->vc_parent;
	paddr_t pvclock_hpa, pvclock_gpa;

	if (vcpu->vc_pvclock_system_gpa & PVCLOCK_SYSTEM_TIME_ENABLE) {
		pvclock_gpa = vcpu->vc_pvclock_system_gpa & 0xFFFFFFFFFFFFFFF0;
		if (!pmap_extract(vm->vm_pmap, pvclock_gpa, &pvclock_hpa))
			return (EINVAL);
		pvclock_ti = (void*) PMAP_DIRECT_MAP(pvclock_hpa);

		/* START next cycle (must be odd) */
		pvclock_ti->ti_version =
		    (++vcpu->vc_pvclock_version << 1) | 0x1;

		pvclock_ti->ti_tsc_timestamp = rdtsc();
		nanouptime(&tv);
		pvclock_ti->ti_system_time =
		    tv.tv_sec * 1000000000L + tv.tv_nsec;
		pvclock_ti->ti_tsc_shift = 12;
		pvclock_ti->ti_tsc_to_system_mul =
		    vcpu->vc_pvclock_system_tsc_mul;
		pvclock_ti->ti_flags = PVCLOCK_FLAG_TSC_STABLE;

		/* END (must be even) */
		pvclock_ti->ti_version &= ~0x1;
	}
	return (0);
}

void
vmm_pv_wall_clock(struct vcpu *vcpu, paddr_t gpa)
{
	struct pvclock_wall_clock *pvclock_wc;
	struct timespec tv;
	struct vm *vm = vcpu->vc_parent;
	paddr_t hpa;

	if (!vmm_gpa_is_valid(vcpu, gpa, sizeof(struct pvclock_wall_clock)))
		goto err;

	/* XXX: handle case when this struct goes over page boundaries */
	if ((gpa & PAGE_MASK) + sizeof(struct pvclock_wall_clock) > PAGE_SIZE)
		goto err;

	if (!pmap_extract(vm->vm_pmap, gpa, &hpa))
		goto err;
	pvclock_wc = (void*) PMAP_DIRECT_MAP(hpa);
	pvclock_wc->wc_version |= 0x1;
	nanoboottime(&tv);
	pvclock_wc->wc_sec = tv.tv_sec;
	pvclock_wc->wc_nsec = tv.tv_nsec;
	pvclock_wc->wc_version += 1;

	return;
err:
	vmm_inject_gp(vcpu);
}


int
vmm_pat_is_valid(uint64_t pat)
{
	int i;
	uint8_t *byte = (uint8_t *)&pat;

	/* Intel SDM Vol 3A, 11.12.2: 0x02, 0x03, and 0x08-0xFF result in #GP */
	for (i = 0; i < 8; i++) {
		if (byte[i] == 0x02 || byte[i] == 0x03 || byte[i] > 0x07) {
			DPRINTF("%s: invalid pat %llx\n", __func__, pat);
			return 0;
		}
	}

	return 1;
}

/*
 * vmx_exit_reason_decode
 *
 * Returns a human readable string describing exit type 'code'
 */
const char *
vmx_exit_reason_decode(uint32_t code)
{
	switch (code) {
	case VMX_EXIT_NMI: return "NMI";
	case VMX_EXIT_EXTINT: return "External interrupt";
	case VMX_EXIT_TRIPLE_FAULT: return "Triple fault";
	case VMX_EXIT_INIT: return "INIT signal";
	case VMX_EXIT_SIPI: return "SIPI signal";
	case VMX_EXIT_IO_SMI: return "I/O SMI";
	case VMX_EXIT_OTHER_SMI: return "other SMI";
	case VMX_EXIT_INT_WINDOW: return "Interrupt window";
	case VMX_EXIT_NMI_WINDOW: return "NMI window";
	case VMX_EXIT_TASK_SWITCH: return "Task switch";
	case VMX_EXIT_CPUID: return "CPUID instruction";
	case VMX_EXIT_GETSEC: return "GETSEC instruction";
	case VMX_EXIT_HLT: return "HLT instruction";
	case VMX_EXIT_INVD: return "INVD instruction";
	case VMX_EXIT_INVLPG: return "INVLPG instruction";
	case VMX_EXIT_RDPMC: return "RDPMC instruction";
	case VMX_EXIT_RDTSC: return "RDTSC instruction";
	case VMX_EXIT_RSM: return "RSM instruction";
	case VMX_EXIT_VMCALL: return "VMCALL instruction";
	case VMX_EXIT_VMCLEAR: return "VMCLEAR instruction";
	case VMX_EXIT_VMLAUNCH: return "VMLAUNCH instruction";
	case VMX_EXIT_VMPTRLD: return "VMPTRLD instruction";
	case VMX_EXIT_VMPTRST: return "VMPTRST instruction";
	case VMX_EXIT_VMREAD: return "VMREAD instruction";
	case VMX_EXIT_VMRESUME: return "VMRESUME instruction";
	case VMX_EXIT_VMWRITE: return "VMWRITE instruction";
	case VMX_EXIT_VMXOFF: return "VMXOFF instruction";
	case VMX_EXIT_VMXON: return "VMXON instruction";
	case VMX_EXIT_CR_ACCESS: return "CR access";
	case VMX_EXIT_MOV_DR: return "MOV DR instruction";
	case VMX_EXIT_IO: return "I/O instruction";
	case VMX_EXIT_RDMSR: return "RDMSR instruction";
	case VMX_EXIT_WRMSR: return "WRMSR instruction";
	case VMX_EXIT_ENTRY_FAILED_GUEST_STATE: return "guest state invalid";
	case VMX_EXIT_ENTRY_FAILED_MSR_LOAD: return "MSR load failed";
	case VMX_EXIT_MWAIT: return "MWAIT instruction";
	case VMX_EXIT_MTF: return "monitor trap flag";
	case VMX_EXIT_MONITOR: return "MONITOR instruction";
	case VMX_EXIT_PAUSE: return "PAUSE instruction";
	case VMX_EXIT_ENTRY_FAILED_MCE: return "MCE during entry";
	case VMX_EXIT_TPR_BELOW_THRESHOLD: return "TPR below threshold";
	case VMX_EXIT_APIC_ACCESS: return "APIC access";
	case VMX_EXIT_VIRTUALIZED_EOI: return "virtualized EOI";
	case VMX_EXIT_GDTR_IDTR: return "GDTR/IDTR access";
	case VMX_EXIT_LDTR_TR: return "LDTR/TR access";
	case VMX_EXIT_EPT_VIOLATION: return "EPT violation";
	case VMX_EXIT_EPT_MISCONFIGURATION: return "EPT misconfiguration";
	case VMX_EXIT_INVEPT: return "INVEPT instruction";
	case VMX_EXIT_RDTSCP: return "RDTSCP instruction";
	case VMX_EXIT_VMX_PREEMPTION_TIMER_EXPIRED:
	    return "preemption timer expired";
	case VMX_EXIT_INVVPID: return "INVVPID instruction";
	case VMX_EXIT_WBINVD: return "WBINVD instruction";
	case VMX_EXIT_XSETBV: return "XSETBV instruction";
	case VMX_EXIT_APIC_WRITE: return "APIC write";
	case VMX_EXIT_RDRAND: return "RDRAND instruction";
	case VMX_EXIT_INVPCID: return "INVPCID instruction";
	case VMX_EXIT_VMFUNC: return "VMFUNC instruction";
	case VMX_EXIT_RDSEED: return "RDSEED instruction";
	case VMX_EXIT_XSAVES: return "XSAVES instruction";
	case VMX_EXIT_XRSTORS: return "XRSTORS instruction";
	default: return "unknown";
	}
}

/*
 * svm_exit_reason_decode
 *
 * Returns a human readable string describing exit type 'code'
 */
const char *
svm_exit_reason_decode(uint32_t code)
{
	switch (code) {
	case SVM_VMEXIT_CR0_READ: return "CR0 read";		/* 0x00 */
	case SVM_VMEXIT_CR1_READ: return "CR1 read";		/* 0x01 */
	case SVM_VMEXIT_CR2_READ: return "CR2 read";		/* 0x02 */
	case SVM_VMEXIT_CR3_READ: return "CR3 read";		/* 0x03 */
	case SVM_VMEXIT_CR4_READ: return "CR4 read";		/* 0x04 */
	case SVM_VMEXIT_CR5_READ: return "CR5 read";		/* 0x05 */
	case SVM_VMEXIT_CR6_READ: return "CR6 read";		/* 0x06 */
	case SVM_VMEXIT_CR7_READ: return "CR7 read";		/* 0x07 */
	case SVM_VMEXIT_CR8_READ: return "CR8 read";		/* 0x08 */
	case SVM_VMEXIT_CR9_READ: return "CR9 read";		/* 0x09 */
	case SVM_VMEXIT_CR10_READ: return "CR10 read";		/* 0x0A */
	case SVM_VMEXIT_CR11_READ: return "CR11 read";		/* 0x0B */
	case SVM_VMEXIT_CR12_READ: return "CR12 read";		/* 0x0C */
	case SVM_VMEXIT_CR13_READ: return "CR13 read";		/* 0x0D */
	case SVM_VMEXIT_CR14_READ: return "CR14 read";		/* 0x0E */
	case SVM_VMEXIT_CR15_READ: return "CR15 read";		/* 0x0F */
	case SVM_VMEXIT_CR0_WRITE: return "CR0 write";		/* 0x10 */
	case SVM_VMEXIT_CR1_WRITE: return "CR1 write";		/* 0x11 */
	case SVM_VMEXIT_CR2_WRITE: return "CR2 write";		/* 0x12 */
	case SVM_VMEXIT_CR3_WRITE: return "CR3 write";		/* 0x13 */
	case SVM_VMEXIT_CR4_WRITE: return "CR4 write";		/* 0x14 */
	case SVM_VMEXIT_CR5_WRITE: return "CR5 write";		/* 0x15 */
	case SVM_VMEXIT_CR6_WRITE: return "CR6 write";		/* 0x16 */
	case SVM_VMEXIT_CR7_WRITE: return "CR7 write";		/* 0x17 */
	case SVM_VMEXIT_CR8_WRITE: return "CR8 write";		/* 0x18 */
	case SVM_VMEXIT_CR9_WRITE: return "CR9 write";		/* 0x19 */
	case SVM_VMEXIT_CR10_WRITE: return "CR10 write";	/* 0x1A */
	case SVM_VMEXIT_CR11_WRITE: return "CR11 write";	/* 0x1B */
	case SVM_VMEXIT_CR12_WRITE: return "CR12 write";	/* 0x1C */
	case SVM_VMEXIT_CR13_WRITE: return "CR13 write";	/* 0x1D */
	case SVM_VMEXIT_CR14_WRITE: return "CR14 write";	/* 0x1E */
	case SVM_VMEXIT_CR15_WRITE: return "CR15 write";	/* 0x1F */
	case SVM_VMEXIT_DR0_READ: return "DR0 read";		/* 0x20 */
	case SVM_VMEXIT_DR1_READ: return "DR1 read";		/* 0x21 */
	case SVM_VMEXIT_DR2_READ: return "DR2 read";		/* 0x22 */
	case SVM_VMEXIT_DR3_READ: return "DR3 read";		/* 0x23 */
	case SVM_VMEXIT_DR4_READ: return "DR4 read";		/* 0x24 */
	case SVM_VMEXIT_DR5_READ: return "DR5 read";		/* 0x25 */
	case SVM_VMEXIT_DR6_READ: return "DR6 read";		/* 0x26 */
	case SVM_VMEXIT_DR7_READ: return "DR7 read";		/* 0x27 */
	case SVM_VMEXIT_DR8_READ: return "DR8 read";		/* 0x28 */
	case SVM_VMEXIT_DR9_READ: return "DR9 read";		/* 0x29 */
	case SVM_VMEXIT_DR10_READ: return "DR10 read";		/* 0x2A */
	case SVM_VMEXIT_DR11_READ: return "DR11 read";		/* 0x2B */
	case SVM_VMEXIT_DR12_READ: return "DR12 read";		/* 0x2C */
	case SVM_VMEXIT_DR13_READ: return "DR13 read";		/* 0x2D */
	case SVM_VMEXIT_DR14_READ: return "DR14 read";		/* 0x2E */
	case SVM_VMEXIT_DR15_READ: return "DR15 read";		/* 0x2F */
	case SVM_VMEXIT_DR0_WRITE: return "DR0 write";		/* 0x30 */
	case SVM_VMEXIT_DR1_WRITE: return "DR1 write";		/* 0x31 */
	case SVM_VMEXIT_DR2_WRITE: return "DR2 write";		/* 0x32 */
	case SVM_VMEXIT_DR3_WRITE: return "DR3 write";		/* 0x33 */
	case SVM_VMEXIT_DR4_WRITE: return "DR4 write";		/* 0x34 */
	case SVM_VMEXIT_DR5_WRITE: return "DR5 write";		/* 0x35 */
	case SVM_VMEXIT_DR6_WRITE: return "DR6 write";		/* 0x36 */
	case SVM_VMEXIT_DR7_WRITE: return "DR7 write";		/* 0x37 */
	case SVM_VMEXIT_DR8_WRITE: return "DR8 write";		/* 0x38 */
	case SVM_VMEXIT_DR9_WRITE: return "DR9 write";		/* 0x39 */
	case SVM_VMEXIT_DR10_WRITE: return "DR10 write";	/* 0x3A */
	case SVM_VMEXIT_DR11_WRITE: return "DR11 write";	/* 0x3B */
	case SVM_VMEXIT_DR12_WRITE: return "DR12 write";	/* 0x3C */
	case SVM_VMEXIT_DR13_WRITE: return "DR13 write";	/* 0x3D */
	case SVM_VMEXIT_DR14_WRITE: return "DR14 write";	/* 0x3E */
	case SVM_VMEXIT_DR15_WRITE: return "DR15 write";	/* 0x3F */
	case SVM_VMEXIT_EXCP0: return "Exception 0x00";		/* 0x40 */
	case SVM_VMEXIT_EXCP1: return "Exception 0x01";		/* 0x41 */
	case SVM_VMEXIT_EXCP2: return "Exception 0x02";		/* 0x42 */
	case SVM_VMEXIT_EXCP3: return "Exception 0x03";		/* 0x43 */
	case SVM_VMEXIT_EXCP4: return "Exception 0x04";		/* 0x44 */
	case SVM_VMEXIT_EXCP5: return "Exception 0x05";		/* 0x45 */
	case SVM_VMEXIT_EXCP6: return "Exception 0x06";		/* 0x46 */
	case SVM_VMEXIT_EXCP7: return "Exception 0x07";		/* 0x47 */
	case SVM_VMEXIT_EXCP8: return "Exception 0x08";		/* 0x48 */
	case SVM_VMEXIT_EXCP9: return "Exception 0x09";		/* 0x49 */
	case SVM_VMEXIT_EXCP10: return "Exception 0x0A";	/* 0x4A */
	case SVM_VMEXIT_EXCP11: return "Exception 0x0B";	/* 0x4B */
	case SVM_VMEXIT_EXCP12: return "Exception 0x0C";	/* 0x4C */
	case SVM_VMEXIT_EXCP13: return "Exception 0x0D";	/* 0x4D */
	case SVM_VMEXIT_EXCP14: return "Exception 0x0E";	/* 0x4E */
	case SVM_VMEXIT_EXCP15: return "Exception 0x0F";	/* 0x4F */
	case SVM_VMEXIT_EXCP16: return "Exception 0x10";	/* 0x50 */
	case SVM_VMEXIT_EXCP17: return "Exception 0x11";	/* 0x51 */
	case SVM_VMEXIT_EXCP18: return "Exception 0x12";	/* 0x52 */
	case SVM_VMEXIT_EXCP19: return "Exception 0x13";	/* 0x53 */
	case SVM_VMEXIT_EXCP20: return "Exception 0x14";	/* 0x54 */
	case SVM_VMEXIT_EXCP21: return "Exception 0x15";	/* 0x55 */
	case SVM_VMEXIT_EXCP22: return "Exception 0x16";	/* 0x56 */
	case SVM_VMEXIT_EXCP23: return "Exception 0x17";	/* 0x57 */
	case SVM_VMEXIT_EXCP24: return "Exception 0x18";	/* 0x58 */
	case SVM_VMEXIT_EXCP25: return "Exception 0x19";	/* 0x59 */
	case SVM_VMEXIT_EXCP26: return "Exception 0x1A";	/* 0x5A */
	case SVM_VMEXIT_EXCP27: return "Exception 0x1B";	/* 0x5B */
	case SVM_VMEXIT_EXCP28: return "Exception 0x1C";	/* 0x5C */
	case SVM_VMEXIT_EXCP29: return "Exception 0x1D";	/* 0x5D */
	case SVM_VMEXIT_EXCP30: return "Exception 0x1E";	/* 0x5E */
	case SVM_VMEXIT_EXCP31: return "Exception 0x1F";	/* 0x5F */
	case SVM_VMEXIT_INTR: return "External interrupt";	/* 0x60 */
	case SVM_VMEXIT_NMI: return "NMI";			/* 0x61 */
	case SVM_VMEXIT_SMI: return "SMI";			/* 0x62 */
	case SVM_VMEXIT_INIT: return "INIT";			/* 0x63 */
	case SVM_VMEXIT_VINTR: return "Interrupt window";	/* 0x64 */
	case SVM_VMEXIT_CR0_SEL_WRITE: return "Sel CR0 write";	/* 0x65 */
	case SVM_VMEXIT_IDTR_READ: return "IDTR read";		/* 0x66 */
	case SVM_VMEXIT_GDTR_READ: return "GDTR read";		/* 0x67 */
	case SVM_VMEXIT_LDTR_READ: return "LDTR read";		/* 0x68 */
	case SVM_VMEXIT_TR_READ: return "TR read";		/* 0x69 */
	case SVM_VMEXIT_IDTR_WRITE: return "IDTR write";	/* 0x6A */
	case SVM_VMEXIT_GDTR_WRITE: return "GDTR write";	/* 0x6B */
	case SVM_VMEXIT_LDTR_WRITE: return "LDTR write";	/* 0x6C */
	case SVM_VMEXIT_TR_WRITE: return "TR write";		/* 0x6D */
	case SVM_VMEXIT_RDTSC: return "RDTSC instruction";	/* 0x6E */
	case SVM_VMEXIT_RDPMC: return "RDPMC instruction";	/* 0x6F */
	case SVM_VMEXIT_PUSHF: return "PUSHF instruction";	/* 0x70 */
	case SVM_VMEXIT_POPF: return "POPF instruction";	/* 0x71 */
	case SVM_VMEXIT_CPUID: return "CPUID instruction";	/* 0x72 */
	case SVM_VMEXIT_RSM: return "RSM instruction";		/* 0x73 */
	case SVM_VMEXIT_IRET: return "IRET instruction";	/* 0x74 */
	case SVM_VMEXIT_SWINT: return "SWINT instruction";	/* 0x75 */
	case SVM_VMEXIT_INVD: return "INVD instruction";	/* 0x76 */
	case SVM_VMEXIT_PAUSE: return "PAUSE instruction";	/* 0x77 */
	case SVM_VMEXIT_HLT: return "HLT instruction";		/* 0x78 */
	case SVM_VMEXIT_INVLPG: return "INVLPG instruction";	/* 0x79 */
	case SVM_VMEXIT_INVLPGA: return "INVLPGA instruction";	/* 0x7A */
	case SVM_VMEXIT_IOIO: return "I/O instruction";		/* 0x7B */
	case SVM_VMEXIT_MSR: return "RDMSR/WRMSR instruction";	/* 0x7C */
	case SVM_VMEXIT_TASK_SWITCH: return "Task switch";	/* 0x7D */
	case SVM_VMEXIT_FERR_FREEZE: return "FERR_FREEZE";	/* 0x7E */
	case SVM_VMEXIT_SHUTDOWN: return "Triple fault";	/* 0x7F */
	case SVM_VMEXIT_VMRUN: return "VMRUN instruction";	/* 0x80 */
	case SVM_VMEXIT_VMMCALL: return "VMMCALL instruction";	/* 0x81 */
	case SVM_VMEXIT_VMLOAD: return "VMLOAD instruction";	/* 0x82 */
	case SVM_VMEXIT_VMSAVE: return "VMSAVE instruction";	/* 0x83 */
	case SVM_VMEXIT_STGI: return "STGI instruction";	/* 0x84 */
	case SVM_VMEXIT_CLGI: return "CLGI instruction";	/* 0x85 */
	case SVM_VMEXIT_SKINIT: return "SKINIT instruction";	/* 0x86 */
	case SVM_VMEXIT_RDTSCP: return "RDTSCP instruction";	/* 0x87 */
	case SVM_VMEXIT_ICEBP: return "ICEBP instruction";	/* 0x88 */
	case SVM_VMEXIT_WBINVD: return "WBINVD instruction";	/* 0x89 */
	case SVM_VMEXIT_MONITOR: return "MONITOR instruction";	/* 0x8A */
	case SVM_VMEXIT_MWAIT: return "MWAIT instruction";	/* 0x8B */
	case SVM_VMEXIT_MWAIT_CONDITIONAL: return "Cond MWAIT";	/* 0x8C */
	case SVM_VMEXIT_NPF: return "NPT violation";		/* 0x400 */
	default: return "unknown";
	}
}

/*
 * vmx_instruction_error_decode
 *
 * Returns a human readable string describing the instruction error in 'code'
 */
const char *
vmx_instruction_error_decode(uint32_t code)
{
	switch (code) {
	case 1: return "VMCALL: unsupported in VMX root";
	case 2: return "VMCLEAR: invalid paddr";
	case 3: return "VMCLEAR: VMXON pointer";
	case 4: return "VMLAUNCH: non-clear VMCS";
	case 5: return "VMRESUME: non-launched VMCS";
	case 6: return "VMRESUME: executed after VMXOFF";
	case 7: return "VM entry: invalid control field(s)";
	case 8: return "VM entry: invalid host state field(s)";
	case 9: return "VMPTRLD: invalid paddr";
	case 10: return "VMPTRLD: VMXON pointer";
	case 11: return "VMPTRLD: incorrect VMCS revid";
	case 12: return "VMREAD/VMWRITE: unsupported VMCS field";
	case 13: return "VMWRITE: RO VMCS field";
	case 15: return "VMXON: unsupported in VMX root";
	case 20: return "VMCALL: invalid VM exit control fields";
	case 26: return "VM entry: blocked by MOV SS";
	case 28: return "Invalid operand to INVEPT/INVVPID";
	case 0x80000021: return "VM entry: invalid guest state";
	case 0x80000022: return "VM entry: failure due to MSR loading";
	case 0x80000029: return "VM entry: machine-check event";
	default: return "unknown";
	}
}

/*
 * vcpu_state_decode
 *
 * Returns a human readable string describing the vcpu state in 'state'.
 */
const char *
vcpu_state_decode(u_int state)
{
	switch (state) {
	case VCPU_STATE_STOPPED: return "stopped";
	case VCPU_STATE_RUNNING: return "running";
	case VCPU_STATE_REQTERM: return "requesting termination";
	case VCPU_STATE_TERMINATED: return "terminated";
	case VCPU_STATE_UNKNOWN: return "unknown";
	default: return "invalid";
	}
}

/*
 * svm_get_vmsa_pa
 *
 * Return physical address of VMSA for specified VCPU.
 */
int
svm_get_vmsa_pa(uint32_t vmid, uint32_t vcpuid, uint64_t *vmsapa)
{
	struct vm	*vm;
	struct vcpu	*vcpu;
	int		 error, ret = 0;

	error = vm_find(vmid, &vm);
	if (error)
		return (error);

	vcpu = vm_find_vcpu(vm, vcpuid);
	if (vcpu == NULL || !vcpu->vc_seves) {
		ret = ENOENT;
		goto out;
	}

	if (vmsapa)
		*vmsapa = vcpu->vc_svm_vmsa_pa;

out:
	refcnt_rele_wake(&vm->vm_refcnt);
	return (ret);
}

#ifdef VMM_DEBUG
/*
 * dump_vcpu
 *
 * Dumps the VMX capabilities of vcpu 'vcpu'
 */
void
dump_vcpu(struct vcpu *vcpu)
{
	printf("vcpu @ %p\n", vcpu);
	printf("    parent vm @ %p\n", vcpu->vc_parent);
	printf("    mode: ");
	if (vcpu->vc_virt_mode == VMM_MODE_EPT) {
		printf("VMX\n");
		printf("    pinbased ctls: 0x%llx\n",
		    vcpu->vc_vmx_pinbased_ctls);
		printf("    true pinbased ctls: 0x%llx\n",
		    vcpu->vc_vmx_true_pinbased_ctls);
		CTRL_DUMP(vcpu, PINBASED, EXTERNAL_INT_EXITING);
		CTRL_DUMP(vcpu, PINBASED, NMI_EXITING);
		CTRL_DUMP(vcpu, PINBASED, VIRTUAL_NMIS);
		CTRL_DUMP(vcpu, PINBASED, ACTIVATE_VMX_PREEMPTION_TIMER);
		CTRL_DUMP(vcpu, PINBASED, PROCESS_POSTED_INTERRUPTS);
		printf("    procbased ctls: 0x%llx\n",
		    vcpu->vc_vmx_procbased_ctls);
		printf("    true procbased ctls: 0x%llx\n",
		    vcpu->vc_vmx_true_procbased_ctls);
		CTRL_DUMP(vcpu, PROCBASED, INTERRUPT_WINDOW_EXITING);
		CTRL_DUMP(vcpu, PROCBASED, USE_TSC_OFFSETTING);
		CTRL_DUMP(vcpu, PROCBASED, HLT_EXITING);
		CTRL_DUMP(vcpu, PROCBASED, INVLPG_EXITING);
		CTRL_DUMP(vcpu, PROCBASED, MWAIT_EXITING);
		CTRL_DUMP(vcpu, PROCBASED, RDPMC_EXITING);
		CTRL_DUMP(vcpu, PROCBASED, RDTSC_EXITING);
		CTRL_DUMP(vcpu, PROCBASED, CR3_LOAD_EXITING);
		CTRL_DUMP(vcpu, PROCBASED, CR3_STORE_EXITING);
		CTRL_DUMP(vcpu, PROCBASED, CR8_LOAD_EXITING);
		CTRL_DUMP(vcpu, PROCBASED, CR8_STORE_EXITING);
		CTRL_DUMP(vcpu, PROCBASED, USE_TPR_SHADOW);
		CTRL_DUMP(vcpu, PROCBASED, NMI_WINDOW_EXITING);
		CTRL_DUMP(vcpu, PROCBASED, MOV_DR_EXITING);
		CTRL_DUMP(vcpu, PROCBASED, UNCONDITIONAL_IO_EXITING);
		CTRL_DUMP(vcpu, PROCBASED, USE_IO_BITMAPS);
		CTRL_DUMP(vcpu, PROCBASED, MONITOR_TRAP_FLAG);
		CTRL_DUMP(vcpu, PROCBASED, USE_MSR_BITMAPS);
		CTRL_DUMP(vcpu, PROCBASED, MONITOR_EXITING);
		CTRL_DUMP(vcpu, PROCBASED, PAUSE_EXITING);
		if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED_CTLS,
		    IA32_VMX_ACTIVATE_SECONDARY_CONTROLS, 1)) {
			printf("    procbased2 ctls: 0x%llx\n",
			    vcpu->vc_vmx_procbased2_ctls);
			CTRL_DUMP(vcpu, PROCBASED2, VIRTUALIZE_APIC);
			CTRL_DUMP(vcpu, PROCBASED2, ENABLE_EPT);
			CTRL_DUMP(vcpu, PROCBASED2, DESCRIPTOR_TABLE_EXITING);
			CTRL_DUMP(vcpu, PROCBASED2, ENABLE_RDTSCP);
			CTRL_DUMP(vcpu, PROCBASED2, VIRTUALIZE_X2APIC_MODE);
			CTRL_DUMP(vcpu, PROCBASED2, ENABLE_VPID);
			CTRL_DUMP(vcpu, PROCBASED2, WBINVD_EXITING);
			CTRL_DUMP(vcpu, PROCBASED2, UNRESTRICTED_GUEST);
			CTRL_DUMP(vcpu, PROCBASED2,
			    APIC_REGISTER_VIRTUALIZATION);
			CTRL_DUMP(vcpu, PROCBASED2,
			    VIRTUAL_INTERRUPT_DELIVERY);
			CTRL_DUMP(vcpu, PROCBASED2, PAUSE_LOOP_EXITING);
			CTRL_DUMP(vcpu, PROCBASED2, RDRAND_EXITING);
			CTRL_DUMP(vcpu, PROCBASED2, ENABLE_INVPCID);
			CTRL_DUMP(vcpu, PROCBASED2, ENABLE_VM_FUNCTIONS);
			CTRL_DUMP(vcpu, PROCBASED2, VMCS_SHADOWING);
			CTRL_DUMP(vcpu, PROCBASED2, ENABLE_ENCLS_EXITING);
			CTRL_DUMP(vcpu, PROCBASED2, RDSEED_EXITING);
			CTRL_DUMP(vcpu, PROCBASED2, ENABLE_PML);
			CTRL_DUMP(vcpu, PROCBASED2, EPT_VIOLATION_VE);
			CTRL_DUMP(vcpu, PROCBASED2, CONCEAL_VMX_FROM_PT);
			CTRL_DUMP(vcpu, PROCBASED2, ENABLE_XSAVES_XRSTORS);
			CTRL_DUMP(vcpu, PROCBASED2, ENABLE_TSC_SCALING);
		}
		printf("    entry ctls: 0x%llx\n",
		    vcpu->vc_vmx_entry_ctls);
		printf("    true entry ctls: 0x%llx\n",
		    vcpu->vc_vmx_true_entry_ctls);
		CTRL_DUMP(vcpu, ENTRY, LOAD_DEBUG_CONTROLS);
		CTRL_DUMP(vcpu, ENTRY, IA32E_MODE_GUEST);
		CTRL_DUMP(vcpu, ENTRY, ENTRY_TO_SMM);
		CTRL_DUMP(vcpu, ENTRY, DEACTIVATE_DUAL_MONITOR_TREATMENT);
		CTRL_DUMP(vcpu, ENTRY, LOAD_IA32_PERF_GLOBAL_CTRL_ON_ENTRY);
		CTRL_DUMP(vcpu, ENTRY, LOAD_IA32_PAT_ON_ENTRY);
		CTRL_DUMP(vcpu, ENTRY, LOAD_IA32_EFER_ON_ENTRY);
		CTRL_DUMP(vcpu, ENTRY, LOAD_IA32_BNDCFGS_ON_ENTRY);
		CTRL_DUMP(vcpu, ENTRY, CONCEAL_VM_ENTRIES_FROM_PT);
		printf("    exit ctls: 0x%llx\n",
		    vcpu->vc_vmx_exit_ctls);
		printf("    true exit ctls: 0x%llx\n",
		    vcpu->vc_vmx_true_exit_ctls);
		CTRL_DUMP(vcpu, EXIT, SAVE_DEBUG_CONTROLS);
		CTRL_DUMP(vcpu, EXIT, HOST_SPACE_ADDRESS_SIZE);
		CTRL_DUMP(vcpu, EXIT, LOAD_IA32_PERF_GLOBAL_CTRL_ON_EXIT);
		CTRL_DUMP(vcpu, EXIT, ACKNOWLEDGE_INTERRUPT_ON_EXIT);
		CTRL_DUMP(vcpu, EXIT, SAVE_IA32_PAT_ON_EXIT);
		CTRL_DUMP(vcpu, EXIT, LOAD_IA32_PAT_ON_EXIT);
		CTRL_DUMP(vcpu, EXIT, SAVE_IA32_EFER_ON_EXIT);
		CTRL_DUMP(vcpu, EXIT, LOAD_IA32_EFER_ON_EXIT);
		CTRL_DUMP(vcpu, EXIT, SAVE_VMX_PREEMPTION_TIMER);
		CTRL_DUMP(vcpu, EXIT, CLEAR_IA32_BNDCFGS_ON_EXIT);
		CTRL_DUMP(vcpu, EXIT, CONCEAL_VM_EXITS_FROM_PT);
	}
}

/*
 * vmx_dump_vmcs_field
 *
 * Debug function to dump the contents of a single VMCS field
 *
 * Parameters:
 *  fieldid: VMCS Field ID
 *  msg: string to display
 */
void
vmx_dump_vmcs_field(uint16_t fieldid, const char *msg)
{
	uint8_t width;
	uint64_t val;


	DPRINTF("%s (0x%04x): ", msg, fieldid);
	if (vmread(fieldid, &val))
		DPRINTF("???? ");
	else {
		/*
		 * Field width encoding : bits 13:14
		 *
		 * 0: 16-bit
		 * 1: 64-bit
		 * 2: 32-bit
		 * 3: natural width
		 */
		width = (fieldid >> 13) & 0x3;
		switch (width) {
			case 0: DPRINTF("0x%04llx ", val); break;
			case 1:
			case 3: DPRINTF("0x%016llx ", val); break;
			case 2: DPRINTF("0x%08llx ", val);
		}
	}
}

/*
 * vmx_dump_vmcs
 *
 * Debug function to dump the contents of the current VMCS.
 */
void
vmx_dump_vmcs(struct vcpu *vcpu)
{
	int has_sec, i;
	uint32_t cr3_tgt_ct;

	/* XXX save and load new vmcs, restore at end */

	DPRINTF("--CURRENT VMCS STATE--\n");
	printf("VMCS launched: %s\n",
	    (vcpu->vc_vmx_vmcs_state == VMCS_LAUNCHED) ? "Yes" : "No");
	DPRINTF("VMXON revision : 0x%x\n",
	    curcpu()->ci_vmm_cap.vcc_vmx.vmx_vmxon_revision);
	DPRINTF("CR0 fixed0: 0x%llx\n",
	    curcpu()->ci_vmm_cap.vcc_vmx.vmx_cr0_fixed0);
	DPRINTF("CR0 fixed1: 0x%llx\n",
	    curcpu()->ci_vmm_cap.vcc_vmx.vmx_cr0_fixed1);
	DPRINTF("CR4 fixed0: 0x%llx\n",
	    curcpu()->ci_vmm_cap.vcc_vmx.vmx_cr4_fixed0);
	DPRINTF("CR4 fixed1: 0x%llx\n",
	    curcpu()->ci_vmm_cap.vcc_vmx.vmx_cr4_fixed1);
	DPRINTF("MSR table size: 0x%x\n",
	    512 * (curcpu()->ci_vmm_cap.vcc_vmx.vmx_msr_table_size + 1));

	has_sec = vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED_CTLS,
	    IA32_VMX_ACTIVATE_SECONDARY_CONTROLS, 1);

	if (has_sec) {
		if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED2_CTLS,
		    IA32_VMX_ENABLE_VPID, 1)) {
			vmx_dump_vmcs_field(VMCS_GUEST_VPID, "VPID");
		}
	}

	if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PINBASED_CTLS,
	    IA32_VMX_PROCESS_POSTED_INTERRUPTS, 1)) {
		vmx_dump_vmcs_field(VMCS_POSTED_INT_NOTIF_VECTOR,
		    "Posted Int Notif Vec");
	}

	if (has_sec) {
		if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED2_CTLS,
		    IA32_VMX_EPT_VIOLATION_VE, 1)) {
			vmx_dump_vmcs_field(VMCS_EPTP_INDEX, "EPTP idx");
		}
	}

	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_ES_SEL, "G.ES");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_CS_SEL, "G.CS");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_SS_SEL, "G.SS");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_DS_SEL, "G.DS");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_FS_SEL, "G.FS");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_GS_SEL, "G.GS");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_LDTR_SEL, "LDTR");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_TR_SEL, "G.TR");

	if (has_sec) {
		if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED2_CTLS,
		    IA32_VMX_VIRTUAL_INTERRUPT_DELIVERY, 1)) {
			vmx_dump_vmcs_field(VMCS_GUEST_INTERRUPT_STATUS,
			    "Int sts");
		}

		if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED2_CTLS,
		    IA32_VMX_ENABLE_PML, 1)) {
			vmx_dump_vmcs_field(VMCS_GUEST_PML_INDEX, "PML Idx");
		}
	}

	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_HOST_IA32_ES_SEL, "H.ES");
	vmx_dump_vmcs_field(VMCS_HOST_IA32_CS_SEL, "H.CS");
	vmx_dump_vmcs_field(VMCS_HOST_IA32_SS_SEL, "H.SS");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_HOST_IA32_DS_SEL, "H.DS");
	vmx_dump_vmcs_field(VMCS_HOST_IA32_FS_SEL, "H.FS");
	vmx_dump_vmcs_field(VMCS_HOST_IA32_GS_SEL, "H.GS");
	DPRINTF("\n");

	vmx_dump_vmcs_field(VMCS_IO_BITMAP_A, "I/O Bitmap A");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_IO_BITMAP_B, "I/O Bitmap B");
	DPRINTF("\n");

	if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED_CTLS,
	    IA32_VMX_USE_MSR_BITMAPS, 1)) {
		vmx_dump_vmcs_field(VMCS_MSR_BITMAP_ADDRESS, "MSR Bitmap");
		DPRINTF("\n");
	}

	vmx_dump_vmcs_field(VMCS_EXIT_STORE_MSR_ADDRESS, "Exit Store MSRs");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_EXIT_LOAD_MSR_ADDRESS, "Exit Load MSRs");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_ENTRY_LOAD_MSR_ADDRESS, "Entry Load MSRs");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_EXECUTIVE_VMCS_POINTER, "Exec VMCS Ptr");
	DPRINTF("\n");

	if (has_sec) {
		if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED2_CTLS,
		    IA32_VMX_ENABLE_PML, 1)) {
			vmx_dump_vmcs_field(VMCS_PML_ADDRESS, "PML Addr");
			DPRINTF("\n");
		}
	}

	vmx_dump_vmcs_field(VMCS_TSC_OFFSET, "TSC Offset");
	DPRINTF("\n");

	if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED_CTLS,
	    IA32_VMX_USE_TPR_SHADOW, 1)) {
		vmx_dump_vmcs_field(VMCS_VIRTUAL_APIC_ADDRESS,
		    "Virtual APIC Addr");
		DPRINTF("\n");
	}

	if (has_sec) {
		if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED2_CTLS,
		    IA32_VMX_VIRTUALIZE_APIC, 1)) {
			vmx_dump_vmcs_field(VMCS_APIC_ACCESS_ADDRESS,
			    "APIC Access Addr");
			DPRINTF("\n");
		}
	}

	if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PINBASED_CTLS,
	    IA32_VMX_PROCESS_POSTED_INTERRUPTS, 1)) {
		vmx_dump_vmcs_field(VMCS_POSTED_INTERRUPT_DESC,
		    "Posted Int Desc Addr");
		DPRINTF("\n");
	}

	if (has_sec) {
		if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED2_CTLS,
		    IA32_VMX_ENABLE_VM_FUNCTIONS, 1)) {
			vmx_dump_vmcs_field(VMCS_VM_FUNCTION_CONTROLS,
			    "VM Function Controls");
			DPRINTF("\n");
		}

		if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED2_CTLS,
		    IA32_VMX_ENABLE_EPT, 1)) {
			vmx_dump_vmcs_field(VMCS_GUEST_IA32_EPTP,
			    "EPT Pointer");
			DPRINTF("\n");
		}

		if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED2_CTLS,
		    IA32_VMX_VIRTUAL_INTERRUPT_DELIVERY, 1)) {
			vmx_dump_vmcs_field(VMCS_EOI_EXIT_BITMAP_0,
			    "EOI Exit Bitmap 0");
			DPRINTF("\n");
			vmx_dump_vmcs_field(VMCS_EOI_EXIT_BITMAP_1,
			    "EOI Exit Bitmap 1");
			DPRINTF("\n");
			vmx_dump_vmcs_field(VMCS_EOI_EXIT_BITMAP_2,
			    "EOI Exit Bitmap 2");
			DPRINTF("\n");
			vmx_dump_vmcs_field(VMCS_EOI_EXIT_BITMAP_3,
			    "EOI Exit Bitmap 3");
			DPRINTF("\n");
		}

		if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED2_CTLS,
		    IA32_VMX_VMCS_SHADOWING, 1)) {
			vmx_dump_vmcs_field(VMCS_VMREAD_BITMAP_ADDRESS,
			    "VMREAD Bitmap Addr");
			DPRINTF("\n");
			vmx_dump_vmcs_field(VMCS_VMWRITE_BITMAP_ADDRESS,
			    "VMWRITE Bitmap Addr");
			DPRINTF("\n");
		}

		if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED2_CTLS,
		    IA32_VMX_EPT_VIOLATION_VE, 1)) {
			vmx_dump_vmcs_field(VMCS_VIRTUALIZATION_EXC_ADDRESS,
			    "#VE Addr");
			DPRINTF("\n");
		}

		if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED2_CTLS,
		    IA32_VMX_ENABLE_XSAVES_XRSTORS, 1)) {
			vmx_dump_vmcs_field(VMCS_XSS_EXITING_BITMAP,
			    "XSS exiting bitmap addr");
			DPRINTF("\n");
		}

		if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED2_CTLS,
		    IA32_VMX_ENABLE_ENCLS_EXITING, 1)) {
			vmx_dump_vmcs_field(VMCS_ENCLS_EXITING_BITMAP,
			    "Encls exiting bitmap addr");
			DPRINTF("\n");
		}

		if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED2_CTLS,
		    IA32_VMX_ENABLE_TSC_SCALING, 1)) {
			vmx_dump_vmcs_field(VMCS_TSC_MULTIPLIER,
			    "TSC scaling factor");
			DPRINTF("\n");
		}

		if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED2_CTLS,
		    IA32_VMX_ENABLE_EPT, 1)) {
			vmx_dump_vmcs_field(VMCS_GUEST_PHYSICAL_ADDRESS,
			    "Guest PA");
			DPRINTF("\n");
		}
	}

	vmx_dump_vmcs_field(VMCS_LINK_POINTER, "VMCS Link Pointer");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_DEBUGCTL, "Guest DEBUGCTL");
	DPRINTF("\n");

	if (vcpu_vmx_check_cap(vcpu, IA32_VMX_ENTRY_CTLS,
	    IA32_VMX_LOAD_IA32_PAT_ON_ENTRY, 1) ||
	    vcpu_vmx_check_cap(vcpu, IA32_VMX_EXIT_CTLS,
	    IA32_VMX_SAVE_IA32_PAT_ON_EXIT, 1)) {
		vmx_dump_vmcs_field(VMCS_GUEST_IA32_PAT,
		    "Guest PAT");
		DPRINTF("\n");
	}

	if (vcpu_vmx_check_cap(vcpu, IA32_VMX_ENTRY_CTLS,
	    IA32_VMX_LOAD_IA32_EFER_ON_ENTRY, 1) ||
	    vcpu_vmx_check_cap(vcpu, IA32_VMX_EXIT_CTLS,
	    IA32_VMX_SAVE_IA32_EFER_ON_EXIT, 1)) {
		vmx_dump_vmcs_field(VMCS_GUEST_IA32_EFER,
		    "Guest EFER");
		DPRINTF("\n");
	}

	if (vcpu_vmx_check_cap(vcpu, IA32_VMX_ENTRY_CTLS,
	    IA32_VMX_LOAD_IA32_PERF_GLOBAL_CTRL_ON_ENTRY, 1)) {
		vmx_dump_vmcs_field(VMCS_GUEST_IA32_PERF_GBL_CTRL,
		    "Guest Perf Global Ctrl");
		DPRINTF("\n");
	}

	if (has_sec) {
		if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED2_CTLS,
		    IA32_VMX_ENABLE_EPT, 1)) {
			vmx_dump_vmcs_field(VMCS_GUEST_PDPTE0, "Guest PDPTE0");
			DPRINTF("\n");
			vmx_dump_vmcs_field(VMCS_GUEST_PDPTE1, "Guest PDPTE1");
			DPRINTF("\n");
			vmx_dump_vmcs_field(VMCS_GUEST_PDPTE2, "Guest PDPTE2");
			DPRINTF("\n");
			vmx_dump_vmcs_field(VMCS_GUEST_PDPTE3, "Guest PDPTE3");
			DPRINTF("\n");
		}
	}

	if (vcpu_vmx_check_cap(vcpu, IA32_VMX_ENTRY_CTLS,
	    IA32_VMX_LOAD_IA32_BNDCFGS_ON_ENTRY, 1) ||
	    vcpu_vmx_check_cap(vcpu, IA32_VMX_EXIT_CTLS,
	    IA32_VMX_CLEAR_IA32_BNDCFGS_ON_EXIT, 1)) {
		vmx_dump_vmcs_field(VMCS_GUEST_IA32_BNDCFGS,
		    "Guest BNDCFGS");
		DPRINTF("\n");
	}

	if (vcpu_vmx_check_cap(vcpu, IA32_VMX_EXIT_CTLS,
	    IA32_VMX_LOAD_IA32_PAT_ON_EXIT, 1)) {
		vmx_dump_vmcs_field(VMCS_HOST_IA32_PAT,
		    "Host PAT");
		DPRINTF("\n");
	}

	if (vcpu_vmx_check_cap(vcpu, IA32_VMX_EXIT_CTLS,
	    IA32_VMX_LOAD_IA32_EFER_ON_EXIT, 1)) {
		vmx_dump_vmcs_field(VMCS_HOST_IA32_EFER,
		    "Host EFER");
		DPRINTF("\n");
	}

	if (vcpu_vmx_check_cap(vcpu, IA32_VMX_EXIT_CTLS,
	    IA32_VMX_LOAD_IA32_PERF_GLOBAL_CTRL_ON_EXIT, 1)) {
		vmx_dump_vmcs_field(VMCS_HOST_IA32_PERF_GBL_CTRL,
		    "Host Perf Global Ctrl");
		DPRINTF("\n");
	}

	vmx_dump_vmcs_field(VMCS_PINBASED_CTLS, "Pinbased Ctrls");
	vmx_dump_vmcs_field(VMCS_PROCBASED_CTLS, "Procbased Ctrls");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_EXCEPTION_BITMAP, "Exception Bitmap");
	vmx_dump_vmcs_field(VMCS_PF_ERROR_CODE_MASK, "#PF Err Code Mask");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_PF_ERROR_CODE_MATCH, "#PF Err Code Match");
	vmx_dump_vmcs_field(VMCS_CR3_TARGET_COUNT, "CR3 Tgt Count");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_EXIT_CTLS, "Exit Ctrls");
	vmx_dump_vmcs_field(VMCS_EXIT_MSR_STORE_COUNT, "Exit MSR Store Ct");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_EXIT_MSR_LOAD_COUNT, "Exit MSR Load Ct");
	vmx_dump_vmcs_field(VMCS_ENTRY_CTLS, "Entry Ctrls");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_ENTRY_MSR_LOAD_COUNT, "Entry MSR Load Ct");
	vmx_dump_vmcs_field(VMCS_ENTRY_INTERRUPTION_INFO, "Entry Int. Info");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_ENTRY_EXCEPTION_ERROR_CODE,
	    "Entry Ex. Err Code");
	vmx_dump_vmcs_field(VMCS_ENTRY_INSTRUCTION_LENGTH, "Entry Insn Len");
	DPRINTF("\n");

	if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED_CTLS,
	    IA32_VMX_USE_TPR_SHADOW, 1)) {
		vmx_dump_vmcs_field(VMCS_TPR_THRESHOLD, "TPR Threshold");
		DPRINTF("\n");
	}

	if (has_sec) {
		vmx_dump_vmcs_field(VMCS_PROCBASED2_CTLS, "2ndary Ctrls");
		DPRINTF("\n");
		if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PROCBASED2_CTLS,
		    IA32_VMX_PAUSE_LOOP_EXITING, 1)) {
			vmx_dump_vmcs_field(VMCS_PLE_GAP, "PLE Gap");
			vmx_dump_vmcs_field(VMCS_PLE_WINDOW, "PLE Window");
		}
		DPRINTF("\n");
	}

	vmx_dump_vmcs_field(VMCS_INSTRUCTION_ERROR, "Insn Error");
	vmx_dump_vmcs_field(VMCS_EXIT_REASON, "Exit Reason");
	DPRINTF("\n");

	vmx_dump_vmcs_field(VMCS_EXIT_INTERRUPTION_INFO, "Exit Int. Info");
	vmx_dump_vmcs_field(VMCS_EXIT_INTERRUPTION_ERR_CODE,
	    "Exit Int. Err Code");
	DPRINTF("\n");

	vmx_dump_vmcs_field(VMCS_IDT_VECTORING_INFO, "IDT vect info");
	vmx_dump_vmcs_field(VMCS_IDT_VECTORING_ERROR_CODE,
	    "IDT vect err code");
	DPRINTF("\n");

	vmx_dump_vmcs_field(VMCS_INSTRUCTION_LENGTH, "Insn Len");
	vmx_dump_vmcs_field(VMCS_EXIT_INSTRUCTION_INFO, "Exit Insn Info");
	DPRINTF("\n");

	vmx_dump_vmcs_field(VMCS_GUEST_IA32_ES_LIMIT, "G. ES Lim");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_CS_LIMIT, "G. CS Lim");
	DPRINTF("\n");

	vmx_dump_vmcs_field(VMCS_GUEST_IA32_SS_LIMIT, "G. SS Lim");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_DS_LIMIT, "G. DS Lim");
	DPRINTF("\n");

	vmx_dump_vmcs_field(VMCS_GUEST_IA32_FS_LIMIT, "G. FS Lim");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_GS_LIMIT, "G. GS Lim");
	DPRINTF("\n");

	vmx_dump_vmcs_field(VMCS_GUEST_IA32_LDTR_LIMIT, "G. LDTR Lim");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_TR_LIMIT, "G. TR Lim");
	DPRINTF("\n");

	vmx_dump_vmcs_field(VMCS_GUEST_IA32_GDTR_LIMIT, "G. GDTR Lim");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_IDTR_LIMIT, "G. IDTR Lim");
	DPRINTF("\n");

	vmx_dump_vmcs_field(VMCS_GUEST_IA32_ES_AR, "G. ES AR");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_CS_AR, "G. CS AR");
	DPRINTF("\n");

	vmx_dump_vmcs_field(VMCS_GUEST_IA32_SS_AR, "G. SS AR");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_DS_AR, "G. DS AR");
	DPRINTF("\n");

	vmx_dump_vmcs_field(VMCS_GUEST_IA32_FS_AR, "G. FS AR");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_GS_AR, "G. GS AR");
	DPRINTF("\n");

	vmx_dump_vmcs_field(VMCS_GUEST_IA32_LDTR_AR, "G. LDTR AR");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_TR_AR, "G. TR AR");
	DPRINTF("\n");

	vmx_dump_vmcs_field(VMCS_GUEST_INTERRUPTIBILITY_ST, "G. Int St.");
	vmx_dump_vmcs_field(VMCS_GUEST_ACTIVITY_STATE, "G. Act St.");
	DPRINTF("\n");

	vmx_dump_vmcs_field(VMCS_GUEST_SMBASE, "G. SMBASE");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_SYSENTER_CS, "G. SYSENTER CS");
	DPRINTF("\n");

	if (vcpu_vmx_check_cap(vcpu, IA32_VMX_PINBASED_CTLS,
	    IA32_VMX_ACTIVATE_VMX_PREEMPTION_TIMER, 1)) {
		vmx_dump_vmcs_field(VMCS_VMX_PREEMPTION_TIMER_VAL,
		    "VMX Preempt Timer");
		DPRINTF("\n");
	}

	vmx_dump_vmcs_field(VMCS_HOST_IA32_SYSENTER_CS, "H. SYSENTER CS");
	DPRINTF("\n");

	vmx_dump_vmcs_field(VMCS_CR0_MASK, "CR0 Mask");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_CR4_MASK, "CR4 Mask");
	DPRINTF("\n");

	vmx_dump_vmcs_field(VMCS_CR0_READ_SHADOW, "CR0 RD Shadow");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_CR4_READ_SHADOW, "CR4 RD Shadow");
	DPRINTF("\n");

	/* We assume all CPUs have the same max CR3 target ct */
	cr3_tgt_ct = curcpu()->ci_vmm_cap.vcc_vmx.vmx_cr3_tgt_count;
	DPRINTF("Max CR3 target count: 0x%x\n", cr3_tgt_ct);
	if (cr3_tgt_ct <= VMX_MAX_CR3_TARGETS) {
		for (i = 0 ; i < cr3_tgt_ct; i++) {
			vmx_dump_vmcs_field(VMCS_CR3_TARGET_0 + (2 * i),
			    "CR3 Target");
			DPRINTF("\n");
		}
	} else {
		DPRINTF("(Bogus CR3 Target Count > %d", VMX_MAX_CR3_TARGETS);
	}

	vmx_dump_vmcs_field(VMCS_GUEST_EXIT_QUALIFICATION, "G. Exit Qual");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_IO_RCX, "I/O RCX");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_IO_RSI, "I/O RSI");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_IO_RDI, "I/O RDI");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_IO_RIP, "I/O RIP");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_LINEAR_ADDRESS, "G. Lin Addr");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_CR0, "G. CR0");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_CR3, "G. CR3");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_CR4, "G. CR4");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_ES_BASE, "G. ES Base");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_CS_BASE, "G. CS Base");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_SS_BASE, "G. SS Base");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_DS_BASE, "G. DS Base");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_FS_BASE, "G. FS Base");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_GS_BASE, "G. GS Base");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_LDTR_BASE, "G. LDTR Base");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_TR_BASE, "G. TR Base");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_GDTR_BASE, "G. GDTR Base");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_IDTR_BASE, "G. IDTR Base");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_DR7, "G. DR7");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_RSP, "G. RSP");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_RIP, "G. RIP");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_RFLAGS, "G. RFLAGS");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_PENDING_DBG_EXC, "G. Pend Dbg Exc");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_SYSENTER_ESP, "G. SYSENTER ESP");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_GUEST_IA32_SYSENTER_EIP, "G. SYSENTER EIP");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_HOST_IA32_CR0, "H. CR0");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_HOST_IA32_CR3, "H. CR3");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_HOST_IA32_CR4, "H. CR4");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_HOST_IA32_FS_BASE, "H. FS Base");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_HOST_IA32_GS_BASE, "H. GS Base");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_HOST_IA32_TR_BASE, "H. TR Base");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_HOST_IA32_GDTR_BASE, "H. GDTR Base");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_HOST_IA32_IDTR_BASE, "H. IDTR Base");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_HOST_IA32_SYSENTER_ESP, "H. SYSENTER ESP");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_HOST_IA32_SYSENTER_EIP, "H. SYSENTER EIP");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_HOST_IA32_RSP, "H. RSP");
	DPRINTF("\n");
	vmx_dump_vmcs_field(VMCS_HOST_IA32_RIP, "H. RIP");
	DPRINTF("\n");
}

/*
 * vmx_vcpu_dump_regs
 *
 * Debug function to print vcpu regs from the current vcpu
 *  note - vmcs for 'vcpu' must be on this pcpu.
 *
 * Parameters:
 *  vcpu - vcpu whose registers should be dumped
 */
void
vmx_vcpu_dump_regs(struct vcpu *vcpu)
{
	uint64_t r;
	int i;
	struct vmx_msr_store *msr_store;

	/* XXX reformat this for 32 bit guest as needed */
	DPRINTF("vcpu @ %p in %s mode\n", vcpu, vmm_decode_cpu_mode(vcpu));
	i = vmm_get_guest_cpu_cpl(vcpu);
	if (i == -1)
		DPRINTF(" CPL=unknown\n");
	else
		DPRINTF(" CPL=%d\n", i);
	DPRINTF(" rax=0x%016llx rbx=0x%016llx rcx=0x%016llx\n",
	    vcpu->vc_gueststate.vg_rax, vcpu->vc_gueststate.vg_rbx,
	    vcpu->vc_gueststate.vg_rcx);
	DPRINTF(" rdx=0x%016llx rbp=0x%016llx rdi=0x%016llx\n",
	    vcpu->vc_gueststate.vg_rdx, vcpu->vc_gueststate.vg_rbp,
	    vcpu->vc_gueststate.vg_rdi);
	DPRINTF(" rsi=0x%016llx  r8=0x%016llx  r9=0x%016llx\n",
	    vcpu->vc_gueststate.vg_rsi, vcpu->vc_gueststate.vg_r8,
	    vcpu->vc_gueststate.vg_r9);
	DPRINTF(" r10=0x%016llx r11=0x%016llx r12=0x%016llx\n",
	    vcpu->vc_gueststate.vg_r10, vcpu->vc_gueststate.vg_r11,
	    vcpu->vc_gueststate.vg_r12);
	DPRINTF(" r13=0x%016llx r14=0x%016llx r15=0x%016llx\n",
	    vcpu->vc_gueststate.vg_r13, vcpu->vc_gueststate.vg_r14,
	    vcpu->vc_gueststate.vg_r15);

	DPRINTF(" rip=0x%016llx rsp=", vcpu->vc_gueststate.vg_rip);
	if (vmread(VMCS_GUEST_IA32_RSP, &r))
		DPRINTF("(error reading)\n");
	else
		DPRINTF("0x%016llx\n", r);

	DPRINTF(" rflags=");
	if (vmread(VMCS_GUEST_IA32_RFLAGS, &r))
		DPRINTF("(error reading)\n");
	else {
		DPRINTF("0x%016llx ", r);
		vmm_decode_rflags(r);
	}

	DPRINTF(" cr0=");
	if (vmread(VMCS_GUEST_IA32_CR0, &r))
		DPRINTF("(error reading)\n");
	else {
		DPRINTF("0x%016llx ", r);
		vmm_decode_cr0(r);
	}

	DPRINTF(" cr2=0x%016llx\n", vcpu->vc_gueststate.vg_cr2);

	DPRINTF(" cr3=");
	if (vmread(VMCS_GUEST_IA32_CR3, &r))
		DPRINTF("(error reading)\n");
	else {
		DPRINTF("0x%016llx ", r);
		vmm_decode_cr3(r);
	}

	DPRINTF(" cr4=");
	if (vmread(VMCS_GUEST_IA32_CR4, &r))
		DPRINTF("(error reading)\n");
	else {
		DPRINTF("0x%016llx ", r);
		vmm_decode_cr4(r);
	}

	DPRINTF(" --Guest Segment Info--\n");

	DPRINTF(" cs=");
	if (vmread(VMCS_GUEST_IA32_CS_SEL, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%04llx rpl=%lld", r, r & 0x3);

	DPRINTF(" base=");
	if (vmread(VMCS_GUEST_IA32_CS_BASE, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%016llx", r);

	DPRINTF(" limit=");
	if (vmread(VMCS_GUEST_IA32_CS_LIMIT, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%016llx", r);

	DPRINTF(" a/r=");
	if (vmread(VMCS_GUEST_IA32_CS_AR, &r))
		DPRINTF("(error reading)\n");
	else {
		DPRINTF("0x%04llx\n  ", r);
		vmm_segment_desc_decode(r);
	}

	DPRINTF(" ds=");
	if (vmread(VMCS_GUEST_IA32_DS_SEL, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%04llx rpl=%lld", r, r & 0x3);

	DPRINTF(" base=");
	if (vmread(VMCS_GUEST_IA32_DS_BASE, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%016llx", r);

	DPRINTF(" limit=");
	if (vmread(VMCS_GUEST_IA32_DS_LIMIT, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%016llx", r);

	DPRINTF(" a/r=");
	if (vmread(VMCS_GUEST_IA32_DS_AR, &r))
		DPRINTF("(error reading)\n");
	else {
		DPRINTF("0x%04llx\n  ", r);
		vmm_segment_desc_decode(r);
	}

	DPRINTF(" es=");
	if (vmread(VMCS_GUEST_IA32_ES_SEL, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%04llx rpl=%lld", r, r & 0x3);

	DPRINTF(" base=");
	if (vmread(VMCS_GUEST_IA32_ES_BASE, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%016llx", r);

	DPRINTF(" limit=");
	if (vmread(VMCS_GUEST_IA32_ES_LIMIT, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%016llx", r);

	DPRINTF(" a/r=");
	if (vmread(VMCS_GUEST_IA32_ES_AR, &r))
		DPRINTF("(error reading)\n");
	else {
		DPRINTF("0x%04llx\n  ", r);
		vmm_segment_desc_decode(r);
	}

	DPRINTF(" fs=");
	if (vmread(VMCS_GUEST_IA32_FS_SEL, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%04llx rpl=%lld", r, r & 0x3);

	DPRINTF(" base=");
	if (vmread(VMCS_GUEST_IA32_FS_BASE, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%016llx", r);

	DPRINTF(" limit=");
	if (vmread(VMCS_GUEST_IA32_FS_LIMIT, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%016llx", r);

	DPRINTF(" a/r=");
	if (vmread(VMCS_GUEST_IA32_FS_AR, &r))
		DPRINTF("(error reading)\n");
	else {
		DPRINTF("0x%04llx\n  ", r);
		vmm_segment_desc_decode(r);
	}

	DPRINTF(" gs=");
	if (vmread(VMCS_GUEST_IA32_GS_SEL, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%04llx rpl=%lld", r, r & 0x3);

	DPRINTF(" base=");
	if (vmread(VMCS_GUEST_IA32_GS_BASE, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%016llx", r);

	DPRINTF(" limit=");
	if (vmread(VMCS_GUEST_IA32_GS_LIMIT, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%016llx", r);

	DPRINTF(" a/r=");
	if (vmread(VMCS_GUEST_IA32_GS_AR, &r))
		DPRINTF("(error reading)\n");
	else {
		DPRINTF("0x%04llx\n  ", r);
		vmm_segment_desc_decode(r);
	}

	DPRINTF(" ss=");
	if (vmread(VMCS_GUEST_IA32_SS_SEL, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%04llx rpl=%lld", r, r & 0x3);

	DPRINTF(" base=");
	if (vmread(VMCS_GUEST_IA32_SS_BASE, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%016llx", r);

	DPRINTF(" limit=");
	if (vmread(VMCS_GUEST_IA32_SS_LIMIT, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%016llx", r);

	DPRINTF(" a/r=");
	if (vmread(VMCS_GUEST_IA32_SS_AR, &r))
		DPRINTF("(error reading)\n");
	else {
		DPRINTF("0x%04llx\n  ", r);
		vmm_segment_desc_decode(r);
	}

	DPRINTF(" tr=");
	if (vmread(VMCS_GUEST_IA32_TR_SEL, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%04llx", r);

	DPRINTF(" base=");
	if (vmread(VMCS_GUEST_IA32_TR_BASE, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%016llx", r);

	DPRINTF(" limit=");
	if (vmread(VMCS_GUEST_IA32_TR_LIMIT, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%016llx", r);

	DPRINTF(" a/r=");
	if (vmread(VMCS_GUEST_IA32_TR_AR, &r))
		DPRINTF("(error reading)\n");
	else {
		DPRINTF("0x%04llx\n  ", r);
		vmm_segment_desc_decode(r);
	}

	DPRINTF(" gdtr base=");
	if (vmread(VMCS_GUEST_IA32_GDTR_BASE, &r))
		DPRINTF("(error reading)   ");
	else
		DPRINTF("0x%016llx", r);

	DPRINTF(" limit=");
	if (vmread(VMCS_GUEST_IA32_GDTR_LIMIT, &r))
		DPRINTF("(error reading)\n");
	else
		DPRINTF("0x%016llx\n", r);

	DPRINTF(" idtr base=");
	if (vmread(VMCS_GUEST_IA32_IDTR_BASE, &r))
		DPRINTF("(error reading)   ");
	else
		DPRINTF("0x%016llx", r);

	DPRINTF(" limit=");
	if (vmread(VMCS_GUEST_IA32_IDTR_LIMIT, &r))
		DPRINTF("(error reading)\n");
	else
		DPRINTF("0x%016llx\n", r);

	DPRINTF(" ldtr=");
	if (vmread(VMCS_GUEST_IA32_LDTR_SEL, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%04llx", r);

	DPRINTF(" base=");
	if (vmread(VMCS_GUEST_IA32_LDTR_BASE, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%016llx", r);

	DPRINTF(" limit=");
	if (vmread(VMCS_GUEST_IA32_LDTR_LIMIT, &r))
		DPRINTF("(error reading)");
	else
		DPRINTF("0x%016llx", r);

	DPRINTF(" a/r=");
	if (vmread(VMCS_GUEST_IA32_LDTR_AR, &r))
		DPRINTF("(error reading)\n");
	else {
		DPRINTF("0x%04llx\n  ", r);
		vmm_segment_desc_decode(r);
	}

	DPRINTF(" --Guest MSRs @ 0x%016llx (paddr: 0x%016llx)--\n",
	    (uint64_t)vcpu->vc_vmx_msr_exit_save_va,
	    (uint64_t)vcpu->vc_vmx_msr_exit_save_pa);

	msr_store = (struct vmx_msr_store *)vcpu->vc_vmx_msr_exit_save_va;

	for (i = 0; i < VCPU_REGS_NMSRS; i++) {
		DPRINTF("  MSR %d @ %p : 0x%08llx (%s), "
		    "value=0x%016llx ",
		    i, &msr_store[i], msr_store[i].vms_index,
		    msr_name_decode(msr_store[i].vms_index),
		    msr_store[i].vms_data);
		vmm_decode_msr_value(msr_store[i].vms_index,
		    msr_store[i].vms_data);
	}
}

/*
 * msr_name_decode
 *
 * Returns a human-readable name for the MSR supplied in 'msr'.
 *
 * Parameters:
 *  msr - The MSR to decode
 *
 * Return value:
 *  NULL-terminated character string containing the name of the MSR requested
 */
const char *
msr_name_decode(uint32_t msr)
{
	/*
	 * Add as needed. Also consider adding a decode function when
	 * adding to this table.
	 */

	switch (msr) {
	case MSR_TSC: return "TSC";
	case MSR_APICBASE: return "APIC base";
	case MSR_IA32_FEATURE_CONTROL: return "IA32 feature control";
	case MSR_PERFCTR0: return "perf counter 0";
	case MSR_PERFCTR1: return "perf counter 1";
	case MSR_TEMPERATURE_TARGET: return "temperature target";
	case MSR_MTRRcap: return "MTRR cap";
	case MSR_PERF_STATUS: return "perf status";
	case MSR_PERF_CTL: return "perf control";
	case MSR_MTRRvarBase: return "MTRR variable base";
	case MSR_MTRRfix64K_00000: return "MTRR fixed 64K";
	case MSR_MTRRfix16K_80000: return "MTRR fixed 16K";
	case MSR_MTRRfix4K_C0000: return "MTRR fixed 4K";
	case MSR_CR_PAT: return "PAT";
	case MSR_MTRRdefType: return "MTRR default type";
	case MSR_EFER: return "EFER";
	case MSR_STAR: return "STAR";
	case MSR_LSTAR: return "LSTAR";
	case MSR_CSTAR: return "CSTAR";
	case MSR_SFMASK: return "SFMASK";
	case MSR_FSBASE: return "FSBASE";
	case MSR_GSBASE: return "GSBASE";
	case MSR_KERNELGSBASE: return "KGSBASE";
	case MSR_MISC_ENABLE: return "Misc Enable";
	default: return "Unknown MSR";
	}
}

/*
 * vmm_segment_desc_decode
 *
 * Debug function to print segment information for supplied descriptor
 *
 * Parameters:
 *  val - The A/R bytes for the segment descriptor to decode
 */
void
vmm_segment_desc_decode(uint64_t val)
{
	uint16_t ar;
	uint8_t g, type, s, dpl, p, dib, l;
	uint32_t unusable;

	/* Exit early on unusable descriptors */
	unusable = val & 0x10000;
	if (unusable) {
		DPRINTF("(unusable)\n");
		return;
	}

	ar = (uint16_t)val;

	g = (ar & 0x8000) >> 15;
	dib = (ar & 0x4000) >> 14;
	l = (ar & 0x2000) >> 13;
	p = (ar & 0x80) >> 7;
	dpl = (ar & 0x60) >> 5;
	s = (ar & 0x10) >> 4;
	type = (ar & 0xf);

	DPRINTF("granularity=%d dib=%d l(64 bit)=%d present=%d sys=%d ",
	    g, dib, l, p, s);

	DPRINTF("type=");
	if (!s) {
		switch (type) {
		case SDT_SYSLDT: DPRINTF("ldt\n"); break;
		case SDT_SYS386TSS: DPRINTF("tss (available)\n"); break;
		case SDT_SYS386BSY: DPRINTF("tss (busy)\n"); break;
		case SDT_SYS386CGT: DPRINTF("call gate\n"); break;
		case SDT_SYS386IGT: DPRINTF("interrupt gate\n"); break;
		case SDT_SYS386TGT: DPRINTF("trap gate\n"); break;
		/* XXX handle 32 bit segment types by inspecting mode */
		default: DPRINTF("unknown");
		}
	} else {
		switch (type + 16) {
		case SDT_MEMRO: DPRINTF("data, r/o\n"); break;
		case SDT_MEMROA: DPRINTF("data, r/o, accessed\n"); break;
		case SDT_MEMRW: DPRINTF("data, r/w\n"); break;
		case SDT_MEMRWA: DPRINTF("data, r/w, accessed\n"); break;
		case SDT_MEMROD: DPRINTF("data, r/o, expand down\n"); break;
		case SDT_MEMRODA: DPRINTF("data, r/o, expand down, "
		    "accessed\n");
			break;
		case SDT_MEMRWD: DPRINTF("data, r/w, expand down\n"); break;
		case SDT_MEMRWDA: DPRINTF("data, r/w, expand down, "
		    "accessed\n");
			break;
		case SDT_MEME: DPRINTF("code, x only\n"); break;
		case SDT_MEMEA: DPRINTF("code, x only, accessed\n");
		case SDT_MEMER: DPRINTF("code, r/x\n"); break;
		case SDT_MEMERA: DPRINTF("code, r/x, accessed\n"); break;
		case SDT_MEMEC: DPRINTF("code, x only, conforming\n"); break;
		case SDT_MEMEAC: DPRINTF("code, x only, conforming, "
		    "accessed\n");
			break;
		case SDT_MEMERC: DPRINTF("code, r/x, conforming\n"); break;
		case SDT_MEMERAC: DPRINTF("code, r/x, conforming, accessed\n");
			break;
		}
	}
}

void
vmm_decode_cr0(uint64_t cr0)
{
	struct vmm_reg_debug_info cr0_info[11] = {
		{ CR0_PG, "PG ", "pg " },
		{ CR0_CD, "CD ", "cd " },
		{ CR0_NW, "NW ", "nw " },
		{ CR0_AM, "AM ", "am " },
		{ CR0_WP, "WP ", "wp " },
		{ CR0_NE, "NE ", "ne " },
		{ CR0_ET, "ET ", "et " },
		{ CR0_TS, "TS ", "ts " },
		{ CR0_EM, "EM ", "em " },
		{ CR0_MP, "MP ", "mp " },
		{ CR0_PE, "PE", "pe" }
	};

	uint8_t i;

	DPRINTF("(");
	for (i = 0; i < nitems(cr0_info); i++)
		if (cr0 & cr0_info[i].vrdi_bit)
			DPRINTF("%s", cr0_info[i].vrdi_present);
		else
			DPRINTF("%s", cr0_info[i].vrdi_absent);

	DPRINTF(")\n");
}

void
vmm_decode_cr3(uint64_t cr3)
{
	struct vmm_reg_debug_info cr3_info[2] = {
		{ CR3_PWT, "PWT ", "pwt "},
		{ CR3_PCD, "PCD", "pcd"}
	};

	uint64_t cr4;
	uint8_t i;

	if (vmread(VMCS_GUEST_IA32_CR4, &cr4)) {
		DPRINTF("(error)\n");
		return;
	}

	/* If CR4.PCIDE = 0, interpret CR3.PWT and CR3.PCD */
	if ((cr4 & CR4_PCIDE) == 0) {
		DPRINTF("(");
		for (i = 0 ; i < nitems(cr3_info) ; i++)
			if (cr3 & cr3_info[i].vrdi_bit)
				DPRINTF("%s", cr3_info[i].vrdi_present);
			else
				DPRINTF("%s", cr3_info[i].vrdi_absent);

		DPRINTF(")\n");
	} else {
		DPRINTF("(pcid=0x%llx)\n", cr3 & 0xFFF);
	}
}

void
vmm_decode_cr4(uint64_t cr4)
{
	struct vmm_reg_debug_info cr4_info[19] = {
		{ CR4_PKE, "PKE ", "pke "},
		{ CR4_SMAP, "SMAP ", "smap "},
		{ CR4_SMEP, "SMEP ", "smep "},
		{ CR4_OSXSAVE, "OSXSAVE ", "osxsave "},
		{ CR4_PCIDE, "PCIDE ", "pcide "},
		{ CR4_FSGSBASE, "FSGSBASE ", "fsgsbase "},
		{ CR4_SMXE, "SMXE ", "smxe "},
		{ CR4_VMXE, "VMXE ", "vmxe "},
		{ CR4_OSXMMEXCPT, "OSXMMEXCPT ", "osxmmexcpt "},
		{ CR4_OSFXSR, "OSFXSR ", "osfxsr "},
		{ CR4_PCE, "PCE ", "pce "},
		{ CR4_PGE, "PGE ", "pge "},
		{ CR4_MCE, "MCE ", "mce "},
		{ CR4_PAE, "PAE ", "pae "},
		{ CR4_PSE, "PSE ", "pse "},
		{ CR4_DE, "DE ", "de "},
		{ CR4_TSD, "TSD ", "tsd "},
		{ CR4_PVI, "PVI ", "pvi "},
		{ CR4_VME, "VME", "vme"}
	};

	uint8_t i;

	DPRINTF("(");
	for (i = 0; i < nitems(cr4_info); i++)
		if (cr4 & cr4_info[i].vrdi_bit)
			DPRINTF("%s", cr4_info[i].vrdi_present);
		else
			DPRINTF("%s", cr4_info[i].vrdi_absent);

	DPRINTF(")\n");
}

void
vmm_decode_apicbase_msr_value(uint64_t apicbase)
{
	struct vmm_reg_debug_info apicbase_info[3] = {
		{ APICBASE_BSP, "BSP ", "bsp "},
		{ APICBASE_ENABLE_X2APIC, "X2APIC ", "x2apic "},
		{ APICBASE_GLOBAL_ENABLE, "GLB_EN", "glb_en"}
	};

	uint8_t i;

	DPRINTF("(");
	for (i = 0; i < nitems(apicbase_info); i++)
		if (apicbase & apicbase_info[i].vrdi_bit)
			DPRINTF("%s", apicbase_info[i].vrdi_present);
		else
			DPRINTF("%s", apicbase_info[i].vrdi_absent);

	DPRINTF(")\n");
}

void
vmm_decode_ia32_fc_value(uint64_t fcr)
{
	struct vmm_reg_debug_info fcr_info[4] = {
		{ IA32_FEATURE_CONTROL_LOCK, "LOCK ", "lock "},
		{ IA32_FEATURE_CONTROL_SMX_EN, "SMX ", "smx "},
		{ IA32_FEATURE_CONTROL_VMX_EN, "VMX ", "vmx "},
		{ IA32_FEATURE_CONTROL_SENTER_EN, "SENTER ", "senter "}
	};

	uint8_t i;

	DPRINTF("(");
	for (i = 0; i < nitems(fcr_info); i++)
		if (fcr & fcr_info[i].vrdi_bit)
			DPRINTF("%s", fcr_info[i].vrdi_present);
		else
			DPRINTF("%s", fcr_info[i].vrdi_absent);

	if (fcr & IA32_FEATURE_CONTROL_SENTER_EN)
		DPRINTF(" [SENTER param = 0x%llx]",
		    (fcr & IA32_FEATURE_CONTROL_SENTER_PARAM_MASK) >> 8);

	DPRINTF(")\n");
}

void
vmm_decode_mtrrcap_value(uint64_t val)
{
	struct vmm_reg_debug_info mtrrcap_info[3] = {
		{ MTRRcap_FIXED, "FIXED ", "fixed "},
		{ MTRRcap_WC, "WC ", "wc "},
		{ MTRRcap_SMRR, "SMRR ", "smrr "}
	};

	uint8_t i;

	DPRINTF("(");
	for (i = 0; i < nitems(mtrrcap_info); i++)
		if (val & mtrrcap_info[i].vrdi_bit)
			DPRINTF("%s", mtrrcap_info[i].vrdi_present);
		else
			DPRINTF("%s", mtrrcap_info[i].vrdi_absent);

	if (val & MTRRcap_FIXED)
		DPRINTF(" [nr fixed ranges = 0x%llx]",
		    (val & 0xff));

	DPRINTF(")\n");
}

void
vmm_decode_perf_status_value(uint64_t val)
{
	DPRINTF("(pstate ratio = 0x%llx)\n", (val & 0xffff));
}

void
vmm_decode_perf_ctl_value(uint64_t val)
{
	DPRINTF("(%s ", (val & PERF_CTL_TURBO) ? "TURBO" : "turbo");
	DPRINTF("pstate req = 0x%llx)\n", (val & 0xfffF));
}

void
vmm_decode_mtrrdeftype_value(uint64_t mtrrdeftype)
{
	struct vmm_reg_debug_info mtrrdeftype_info[2] = {
		{ MTRRdefType_FIXED_ENABLE, "FIXED ", "fixed "},
		{ MTRRdefType_ENABLE, "ENABLED ", "enabled "},
	};

	uint8_t i;
	int type;

	DPRINTF("(");
	for (i = 0; i < nitems(mtrrdeftype_info); i++)
		if (mtrrdeftype & mtrrdeftype_info[i].vrdi_bit)
			DPRINTF("%s", mtrrdeftype_info[i].vrdi_present);
		else
			DPRINTF("%s", mtrrdeftype_info[i].vrdi_absent);

	DPRINTF("type = ");
	type = mtrr2mrt(mtrrdeftype & 0xff);
	switch (type) {
	case MDF_UNCACHEABLE: DPRINTF("UC"); break;
	case MDF_WRITECOMBINE: DPRINTF("WC"); break;
	case MDF_WRITETHROUGH: DPRINTF("WT"); break;
	case MDF_WRITEPROTECT: DPRINTF("RO"); break;
	case MDF_WRITEBACK: DPRINTF("WB"); break;
	case MDF_UNKNOWN:
	default:
		DPRINTF("??");
		break;
	}

	DPRINTF(")\n");
}

void
vmm_decode_efer_value(uint64_t efer)
{
	struct vmm_reg_debug_info efer_info[4] = {
		{ EFER_SCE, "SCE ", "sce "},
		{ EFER_LME, "LME ", "lme "},
		{ EFER_LMA, "LMA ", "lma "},
		{ EFER_NXE, "NXE", "nxe"},
	};

	uint8_t i;

	DPRINTF("(");
	for (i = 0; i < nitems(efer_info); i++)
		if (efer & efer_info[i].vrdi_bit)
			DPRINTF("%s", efer_info[i].vrdi_present);
		else
			DPRINTF("%s", efer_info[i].vrdi_absent);

	DPRINTF(")\n");
}

void
vmm_decode_msr_value(uint64_t msr, uint64_t val)
{
	switch (msr) {
	case MSR_APICBASE: vmm_decode_apicbase_msr_value(val); break;
	case MSR_IA32_FEATURE_CONTROL: vmm_decode_ia32_fc_value(val); break;
	case MSR_MTRRcap: vmm_decode_mtrrcap_value(val); break;
	case MSR_PERF_STATUS: vmm_decode_perf_status_value(val); break;
	case MSR_PERF_CTL: vmm_decode_perf_ctl_value(val); break;
	case MSR_MTRRdefType: vmm_decode_mtrrdeftype_value(val); break;
	case MSR_EFER: vmm_decode_efer_value(val); break;
	case MSR_MISC_ENABLE: vmm_decode_misc_enable_value(val); break;
	default: DPRINTF("\n");
	}
}

void
vmm_decode_rflags(uint64_t rflags)
{
	struct vmm_reg_debug_info rflags_info[16] = {
		{ PSL_C,   "CF ", "cf "},
		{ PSL_PF,  "PF ", "pf "},
		{ PSL_AF,  "AF ", "af "},
		{ PSL_Z,   "ZF ", "zf "},
		{ PSL_N,   "SF ", "sf "},	/* sign flag */
		{ PSL_T,   "TF ", "tf "},
		{ PSL_I,   "IF ", "if "},
		{ PSL_D,   "DF ", "df "},
		{ PSL_V,   "OF ", "of "},	/* overflow flag */
		{ PSL_NT,  "NT ", "nt "},
		{ PSL_RF,  "RF ", "rf "},
		{ PSL_VM,  "VM ", "vm "},
		{ PSL_AC,  "AC ", "ac "},
		{ PSL_VIF, "VIF ", "vif "},
		{ PSL_VIP, "VIP ", "vip "},
		{ PSL_ID,  "ID ", "id "},
	};

	uint8_t i, iopl;

	DPRINTF("(");
	for (i = 0; i < nitems(rflags_info); i++)
		if (rflags & rflags_info[i].vrdi_bit)
			DPRINTF("%s", rflags_info[i].vrdi_present);
		else
			DPRINTF("%s", rflags_info[i].vrdi_absent);

	iopl = (rflags & PSL_IOPL) >> 12;
	DPRINTF("IOPL=%d", iopl);

	DPRINTF(")\n");
}

void
vmm_decode_misc_enable_value(uint64_t misc)
{
	struct vmm_reg_debug_info misc_info[10] = {
		{ MISC_ENABLE_FAST_STRINGS,		"FSE ", "fse "},
		{ MISC_ENABLE_TCC,			"TCC ", "tcc "},
		{ MISC_ENABLE_PERF_MON_AVAILABLE,	"PERF ", "perf "},
		{ MISC_ENABLE_BTS_UNAVAILABLE,		"BTSU ", "btsu "},
		{ MISC_ENABLE_PEBS_UNAVAILABLE,		"PEBSU ", "pebsu "},
		{ MISC_ENABLE_EIST_ENABLED,		"EIST ", "eist "},
		{ MISC_ENABLE_ENABLE_MONITOR_FSM,	"MFSM ", "mfsm "},
		{ MISC_ENABLE_LIMIT_CPUID_MAXVAL,	"CMAX ", "cmax "},
		{ MISC_ENABLE_xTPR_MESSAGE_DISABLE,	"xTPRD ", "xtprd "},
		{ MISC_ENABLE_XD_BIT_DISABLE,		"NXD", "nxd"},
	};

	uint8_t i;

	DPRINTF("(");
	for (i = 0; i < nitems(misc_info); i++)
		if (misc & misc_info[i].vrdi_bit)
			DPRINTF("%s", misc_info[i].vrdi_present);
		else
			DPRINTF("%s", misc_info[i].vrdi_absent);

	DPRINTF(")\n");
}

const char *
vmm_decode_cpu_mode(struct vcpu *vcpu)
{
	int mode = vmm_get_guest_cpu_mode(vcpu);

	switch (mode) {
	case VMM_CPU_MODE_REAL: return "real";
	case VMM_CPU_MODE_PROT: return "16 bit protected";
	case VMM_CPU_MODE_PROT32: return "32 bit protected";
	case VMM_CPU_MODE_COMPAT: return "compatibility";
	case VMM_CPU_MODE_LONG: return "long";
	default: return "unknown";
	}
}
#endif /* VMM_DEBUG */
