/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013, Anish Gupta (akgupt3@gmail.com)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/smp.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/cpufunc.h>
#include <machine/psl.h>
#include <machine/md_var.h>
#include <machine/reg.h>
#include <machine/specialreg.h>
#include <machine/smp.h>
#include <machine/vmm.h>
#include <machine/vmm_dev.h>
#include <machine/vmm_instruction_emul.h>

#include "vmm_lapic.h"
#include "vmm_stat.h"
#include "vmm_ktr.h"
#include "vmm_ioport.h"
#include "vatpic.h"
#include "vlapic.h"
#include "vlapic_priv.h"

#include "x86.h"
#include "vmcb.h"
#include "svm.h"
#include "svm_softc.h"
#include "svm_msr.h"
#include "npt.h"

SYSCTL_DECL(_hw_vmm);
SYSCTL_NODE(_hw_vmm, OID_AUTO, svm, CTLFLAG_RW, NULL, NULL);

/*
 * SVM CPUID function 0x8000_000A, edx bit decoding.
 */
#define AMD_CPUID_SVM_NP		BIT(0)  /* Nested paging or RVI */
#define AMD_CPUID_SVM_LBR		BIT(1)  /* Last branch virtualization */
#define AMD_CPUID_SVM_SVML		BIT(2)  /* SVM lock */
#define AMD_CPUID_SVM_NRIP_SAVE		BIT(3)  /* Next RIP is saved */
#define AMD_CPUID_SVM_TSC_RATE		BIT(4)  /* TSC rate control. */
#define AMD_CPUID_SVM_VMCB_CLEAN	BIT(5)  /* VMCB state caching */
#define AMD_CPUID_SVM_FLUSH_BY_ASID	BIT(6)  /* Flush by ASID */
#define AMD_CPUID_SVM_DECODE_ASSIST	BIT(7)  /* Decode assist */
#define AMD_CPUID_SVM_PAUSE_INC		BIT(10) /* Pause intercept filter. */
#define AMD_CPUID_SVM_PAUSE_FTH		BIT(12) /* Pause filter threshold */
#define	AMD_CPUID_SVM_AVIC		BIT(13)	/* AVIC present */

#define	VMCB_CACHE_DEFAULT	(VMCB_CACHE_ASID 	|	\
				VMCB_CACHE_IOPM		|	\
				VMCB_CACHE_I		|	\
				VMCB_CACHE_TPR		|	\
				VMCB_CACHE_CR2		|	\
				VMCB_CACHE_CR		|	\
				VMCB_CACHE_DR		|	\
				VMCB_CACHE_DT		|	\
				VMCB_CACHE_SEG		|	\
				VMCB_CACHE_NP)

static uint32_t vmcb_clean = VMCB_CACHE_DEFAULT;
SYSCTL_INT(_hw_vmm_svm, OID_AUTO, vmcb_clean, CTLFLAG_RDTUN, &vmcb_clean,
    0, NULL);

static MALLOC_DEFINE(M_SVM, "svm", "svm");
static MALLOC_DEFINE(M_SVM_VLAPIC, "svm-vlapic", "svm-vlapic");

/* Per-CPU context area. */
extern struct pcpu __pcpu[];

static uint32_t svm_feature = ~0U;	/* AMD SVM features. */
SYSCTL_UINT(_hw_vmm_svm, OID_AUTO, features, CTLFLAG_RDTUN, &svm_feature, 0,
    "SVM features advertised by CPUID.8000000AH:EDX");

static int disable_npf_assist;
SYSCTL_INT(_hw_vmm_svm, OID_AUTO, disable_npf_assist, CTLFLAG_RWTUN,
    &disable_npf_assist, 0, NULL);

/* Maximum ASIDs supported by the processor */
static uint32_t nasid;
SYSCTL_UINT(_hw_vmm_svm, OID_AUTO, num_asids, CTLFLAG_RDTUN, &nasid, 0,
    "Number of ASIDs supported by this processor");

/* Current ASID generation for each host cpu */
static struct asid asid[MAXCPU];

/* 
 * SVM host state saved area of size 4KB for each core.
 */
static uint8_t hsave[MAXCPU][PAGE_SIZE] __aligned(PAGE_SIZE);

static VMM_STAT_AMD(VCPU_EXITINTINFO, "VM exits during event delivery");
static VMM_STAT_AMD(VCPU_INTINFO_INJECTED, "Events pending at VM entry");
static VMM_STAT_AMD(VMEXIT_VINTR, "VM exits due to interrupt window");

static int svm_setreg(void *arg, int vcpu, int ident, uint64_t val);

static __inline int
flush_by_asid(void)
{

	return (svm_feature & AMD_CPUID_SVM_FLUSH_BY_ASID);
}

static __inline int
decode_assist(void)
{

	return (svm_feature & AMD_CPUID_SVM_DECODE_ASSIST);
}

static void
svm_disable(void *arg __unused)
{
	uint64_t efer;

	efer = rdmsr(MSR_EFER);
	efer &= ~EFER_SVM;
	wrmsr(MSR_EFER, efer);
}

/*
 * Disable SVM on all CPUs.
 */
static int
svm_cleanup(void)
{

	smp_rendezvous(NULL, svm_disable, NULL, NULL);
	return (0);
}

/*
 * Verify that all the features required by bhyve are available.
 */
static int
check_svm_features(void)
{
	u_int regs[4];

	/* CPUID Fn8000_000A is for SVM */
	do_cpuid(0x8000000A, regs);
	svm_feature &= regs[3];

	/*
	 * The number of ASIDs can be configured to be less than what is
	 * supported by the hardware but not more.
	 */
	if (nasid == 0 || nasid > regs[1])
		nasid = regs[1];
	KASSERT(nasid > 1, ("Insufficient ASIDs for guests: %#x", nasid));

	/* bhyve requires the Nested Paging feature */
	if (!(svm_feature & AMD_CPUID_SVM_NP)) {
		printf("SVM: Nested Paging feature not available.\n");
		return (ENXIO);
	}

	/* bhyve requires the NRIP Save feature */
	if (!(svm_feature & AMD_CPUID_SVM_NRIP_SAVE)) {
		printf("SVM: NRIP Save feature not available.\n");
		return (ENXIO);
	}

	return (0);
}

static void
svm_enable(void *arg __unused)
{
	uint64_t efer;

	efer = rdmsr(MSR_EFER);
	efer |= EFER_SVM;
	wrmsr(MSR_EFER, efer);

	wrmsr(MSR_VM_HSAVE_PA, vtophys(hsave[curcpu]));
}

/*
 * Return 1 if SVM is enabled on this processor and 0 otherwise.
 */
static int
svm_available(void)
{
	uint64_t msr;

	/* Section 15.4 Enabling SVM from APM2. */
	if ((amd_feature2 & AMDID2_SVM) == 0) {
		printf("SVM: not available.\n");
		return (0);
	}

	msr = rdmsr(MSR_VM_CR);
	if ((msr & VM_CR_SVMDIS) != 0) {
		printf("SVM: disabled by BIOS.\n");
		return (0);
	}

	return (1);
}

static int
svm_init(int ipinum)
{
	int error, cpu;

	if (!svm_available())
		return (ENXIO);

	error = check_svm_features();
	if (error)
		return (error);

	vmcb_clean &= VMCB_CACHE_DEFAULT;

	for (cpu = 0; cpu < MAXCPU; cpu++) {
		/*
		 * Initialize the host ASIDs to their "highest" valid values.
		 *
		 * The next ASID allocation will rollover both 'gen' and 'num'
		 * and start off the sequence at {1,1}.
		 */
		asid[cpu].gen = ~0UL;
		asid[cpu].num = nasid - 1;
	}

	svm_msr_init();
	svm_npt_init(ipinum);

	/* Enable SVM on all CPUs */
	smp_rendezvous(NULL, svm_enable, NULL, NULL);

	return (0);
}

static void
svm_restore(void)
{

	svm_enable(NULL);
}		

/* Pentium compatible MSRs */
#define MSR_PENTIUM_START 	0	
#define MSR_PENTIUM_END 	0x1FFF
/* AMD 6th generation and Intel compatible MSRs */
#define MSR_AMD6TH_START 	0xC0000000UL	
#define MSR_AMD6TH_END 		0xC0001FFFUL	
/* AMD 7th and 8th generation compatible MSRs */
#define MSR_AMD7TH_START 	0xC0010000UL	
#define MSR_AMD7TH_END 		0xC0011FFFUL	

/*
 * Get the index and bit position for a MSR in permission bitmap.
 * Two bits are used for each MSR: lower bit for read and higher bit for write.
 */
static int
svm_msr_index(uint64_t msr, int *index, int *bit)
{
	uint32_t base, off;

	*index = -1;
	*bit = (msr % 4) * 2;
	base = 0;

	if (msr >= MSR_PENTIUM_START && msr <= MSR_PENTIUM_END) {
		*index = msr / 4;
		return (0);
	}

	base += (MSR_PENTIUM_END - MSR_PENTIUM_START + 1); 
	if (msr >= MSR_AMD6TH_START && msr <= MSR_AMD6TH_END) {
		off = (msr - MSR_AMD6TH_START); 
		*index = (off + base) / 4;
		return (0);
	} 

	base += (MSR_AMD6TH_END - MSR_AMD6TH_START + 1);
	if (msr >= MSR_AMD7TH_START && msr <= MSR_AMD7TH_END) {
		off = (msr - MSR_AMD7TH_START);
		*index = (off + base) / 4;
		return (0);
	}

	return (EINVAL);
}

/*
 * Allow vcpu to read or write the 'msr' without trapping into the hypervisor.
 */
static void
svm_msr_perm(uint8_t *perm_bitmap, uint64_t msr, bool read, bool write)
{
	int index, bit, error;

	error = svm_msr_index(msr, &index, &bit);
	KASSERT(error == 0, ("%s: invalid msr %#lx", __func__, msr));
	KASSERT(index >= 0 && index < SVM_MSR_BITMAP_SIZE,
	    ("%s: invalid index %d for msr %#lx", __func__, index, msr));
	KASSERT(bit >= 0 && bit <= 6, ("%s: invalid bit position %d "
	    "msr %#lx", __func__, bit, msr));

	if (read)
		perm_bitmap[index] &= ~(1UL << bit);

	if (write)
		perm_bitmap[index] &= ~(2UL << bit);
}

static void
svm_msr_rw_ok(uint8_t *perm_bitmap, uint64_t msr)
{

	svm_msr_perm(perm_bitmap, msr, true, true);
}

static void
svm_msr_rd_ok(uint8_t *perm_bitmap, uint64_t msr)
{

	svm_msr_perm(perm_bitmap, msr, true, false);
}

static __inline int
svm_get_intercept(struct svm_softc *sc, int vcpu, int idx, uint32_t bitmask)
{
	struct vmcb_ctrl *ctrl;

	KASSERT(idx >=0 && idx < 5, ("invalid intercept index %d", idx));

	ctrl = svm_get_vmcb_ctrl(sc, vcpu);
	return (ctrl->intercept[idx] & bitmask ? 1 : 0);
}

static __inline void
svm_set_intercept(struct svm_softc *sc, int vcpu, int idx, uint32_t bitmask,
    int enabled)
{
	struct vmcb_ctrl *ctrl;
	uint32_t oldval;

	KASSERT(idx >=0 && idx < 5, ("invalid intercept index %d", idx));

	ctrl = svm_get_vmcb_ctrl(sc, vcpu);
	oldval = ctrl->intercept[idx];

	if (enabled)
		ctrl->intercept[idx] |= bitmask;
	else
		ctrl->intercept[idx] &= ~bitmask;

	if (ctrl->intercept[idx] != oldval) {
		svm_set_dirty(sc, vcpu, VMCB_CACHE_I);
		VCPU_CTR3(sc->vm, vcpu, "intercept[%d] modified "
		    "from %#x to %#x", idx, oldval, ctrl->intercept[idx]);
	}
}

static __inline void
svm_disable_intercept(struct svm_softc *sc, int vcpu, int off, uint32_t bitmask)
{

	svm_set_intercept(sc, vcpu, off, bitmask, 0);
}

static __inline void
svm_enable_intercept(struct svm_softc *sc, int vcpu, int off, uint32_t bitmask)
{

	svm_set_intercept(sc, vcpu, off, bitmask, 1);
}

static void
vmcb_init(struct svm_softc *sc, int vcpu, uint64_t iopm_base_pa,
    uint64_t msrpm_base_pa, uint64_t np_pml4)
{
	struct vmcb_ctrl *ctrl;
	struct vmcb_state *state;
	uint32_t mask;
	int n;

	ctrl = svm_get_vmcb_ctrl(sc, vcpu);
	state = svm_get_vmcb_state(sc, vcpu);

	ctrl->iopm_base_pa = iopm_base_pa;
	ctrl->msrpm_base_pa = msrpm_base_pa;

	/* Enable nested paging */
	ctrl->np_enable = 1;
	ctrl->n_cr3 = np_pml4;

	/*
	 * Intercept accesses to the control registers that are not shadowed
	 * in the VMCB - i.e. all except cr0, cr2, cr3, cr4 and cr8.
	 */
	for (n = 0; n < 16; n++) {
		mask = (BIT(n) << 16) | BIT(n);
		if (n == 0 || n == 2 || n == 3 || n == 4 || n == 8)
			svm_disable_intercept(sc, vcpu, VMCB_CR_INTCPT, mask);
		else
			svm_enable_intercept(sc, vcpu, VMCB_CR_INTCPT, mask);
	}


	/*
	 * Intercept everything when tracing guest exceptions otherwise
	 * just intercept machine check exception.
	 */
	if (vcpu_trace_exceptions(sc->vm, vcpu)) {
		for (n = 0; n < 32; n++) {
			/*
			 * Skip unimplemented vectors in the exception bitmap.
			 */
			if (n == 2 || n == 9) {
				continue;
			}
			svm_enable_intercept(sc, vcpu, VMCB_EXC_INTCPT, BIT(n));
		}
	} else {
		svm_enable_intercept(sc, vcpu, VMCB_EXC_INTCPT, BIT(IDT_MC));
	}

	/* Intercept various events (for e.g. I/O, MSR and CPUID accesses) */
	svm_enable_intercept(sc, vcpu, VMCB_CTRL1_INTCPT, VMCB_INTCPT_IO);
	svm_enable_intercept(sc, vcpu, VMCB_CTRL1_INTCPT, VMCB_INTCPT_MSR);
	svm_enable_intercept(sc, vcpu, VMCB_CTRL1_INTCPT, VMCB_INTCPT_CPUID);
	svm_enable_intercept(sc, vcpu, VMCB_CTRL1_INTCPT, VMCB_INTCPT_INTR);
	svm_enable_intercept(sc, vcpu, VMCB_CTRL1_INTCPT, VMCB_INTCPT_INIT);
	svm_enable_intercept(sc, vcpu, VMCB_CTRL1_INTCPT, VMCB_INTCPT_NMI);
	svm_enable_intercept(sc, vcpu, VMCB_CTRL1_INTCPT, VMCB_INTCPT_SMI);
	svm_enable_intercept(sc, vcpu, VMCB_CTRL1_INTCPT, VMCB_INTCPT_SHUTDOWN);
	svm_enable_intercept(sc, vcpu, VMCB_CTRL1_INTCPT,
	    VMCB_INTCPT_FERR_FREEZE);

	svm_enable_intercept(sc, vcpu, VMCB_CTRL2_INTCPT, VMCB_INTCPT_MONITOR);
	svm_enable_intercept(sc, vcpu, VMCB_CTRL2_INTCPT, VMCB_INTCPT_MWAIT);

	/*
	 * From section "Canonicalization and Consistency Checks" in APMv2
	 * the VMRUN intercept bit must be set to pass the consistency check.
	 */
	svm_enable_intercept(sc, vcpu, VMCB_CTRL2_INTCPT, VMCB_INTCPT_VMRUN);

	/*
	 * The ASID will be set to a non-zero value just before VMRUN.
	 */
	ctrl->asid = 0;

	/*
	 * Section 15.21.1, Interrupt Masking in EFLAGS
	 * Section 15.21.2, Virtualizing APIC.TPR
	 *
	 * This must be set for %rflag and %cr8 isolation of guest and host.
	 */
	ctrl->v_intr_masking = 1;

	/* Enable Last Branch Record aka LBR for debugging */
	ctrl->lbr_virt_en = 1;
	state->dbgctl = BIT(0);

	/* EFER_SVM must always be set when the guest is executing */
	state->efer = EFER_SVM;

	/* Set up the PAT to power-on state */
	state->g_pat = PAT_VALUE(0, PAT_WRITE_BACK)	|
	    PAT_VALUE(1, PAT_WRITE_THROUGH)	|
	    PAT_VALUE(2, PAT_UNCACHED)		|
	    PAT_VALUE(3, PAT_UNCACHEABLE)	|
	    PAT_VALUE(4, PAT_WRITE_BACK)	|
	    PAT_VALUE(5, PAT_WRITE_THROUGH)	|
	    PAT_VALUE(6, PAT_UNCACHED)		|
	    PAT_VALUE(7, PAT_UNCACHEABLE);

	/* Set up DR6/7 to power-on state */
	state->dr6 = DBREG_DR6_RESERVED1;
	state->dr7 = DBREG_DR7_RESERVED1;
}

/*
 * Initialize a virtual machine.
 */
static void *
svm_vminit(struct vm *vm, pmap_t pmap)
{
	struct svm_softc *svm_sc;
	struct svm_vcpu *vcpu;
	vm_paddr_t msrpm_pa, iopm_pa, pml4_pa;
	int i;

	svm_sc = malloc(sizeof (*svm_sc), M_SVM, M_WAITOK | M_ZERO);
	if (((uintptr_t)svm_sc & PAGE_MASK) != 0)
		panic("malloc of svm_softc not aligned on page boundary");

	svm_sc->msr_bitmap = contigmalloc(SVM_MSR_BITMAP_SIZE, M_SVM,
	    M_WAITOK, 0, ~(vm_paddr_t)0, PAGE_SIZE, 0);
	if (svm_sc->msr_bitmap == NULL)
		panic("contigmalloc of SVM MSR bitmap failed");
	svm_sc->iopm_bitmap = contigmalloc(SVM_IO_BITMAP_SIZE, M_SVM,
	    M_WAITOK, 0, ~(vm_paddr_t)0, PAGE_SIZE, 0);
	if (svm_sc->iopm_bitmap == NULL)
		panic("contigmalloc of SVM IO bitmap failed");

	svm_sc->vm = vm;
	svm_sc->nptp = (vm_offset_t)vtophys(pmap->pm_pml4);

	/*
	 * Intercept read and write accesses to all MSRs.
	 */
	memset(svm_sc->msr_bitmap, 0xFF, SVM_MSR_BITMAP_SIZE);

	/*
	 * Access to the following MSRs is redirected to the VMCB when the
	 * guest is executing. Therefore it is safe to allow the guest to
	 * read/write these MSRs directly without hypervisor involvement.
	 */
	svm_msr_rw_ok(svm_sc->msr_bitmap, MSR_GSBASE);
	svm_msr_rw_ok(svm_sc->msr_bitmap, MSR_FSBASE);
	svm_msr_rw_ok(svm_sc->msr_bitmap, MSR_KGSBASE);

	svm_msr_rw_ok(svm_sc->msr_bitmap, MSR_STAR);
	svm_msr_rw_ok(svm_sc->msr_bitmap, MSR_LSTAR);
	svm_msr_rw_ok(svm_sc->msr_bitmap, MSR_CSTAR);
	svm_msr_rw_ok(svm_sc->msr_bitmap, MSR_SF_MASK);
	svm_msr_rw_ok(svm_sc->msr_bitmap, MSR_SYSENTER_CS_MSR);
	svm_msr_rw_ok(svm_sc->msr_bitmap, MSR_SYSENTER_ESP_MSR);
	svm_msr_rw_ok(svm_sc->msr_bitmap, MSR_SYSENTER_EIP_MSR);
	svm_msr_rw_ok(svm_sc->msr_bitmap, MSR_PAT);

	svm_msr_rd_ok(svm_sc->msr_bitmap, MSR_TSC);

	/*
	 * Intercept writes to make sure that the EFER_SVM bit is not cleared.
	 */
	svm_msr_rd_ok(svm_sc->msr_bitmap, MSR_EFER);

	/* Intercept access to all I/O ports. */
	memset(svm_sc->iopm_bitmap, 0xFF, SVM_IO_BITMAP_SIZE);

	iopm_pa = vtophys(svm_sc->iopm_bitmap);
	msrpm_pa = vtophys(svm_sc->msr_bitmap);
	pml4_pa = svm_sc->nptp;
	for (i = 0; i < VM_MAXCPU; i++) {
		vcpu = svm_get_vcpu(svm_sc, i);
		vcpu->nextrip = ~0;
		vcpu->lastcpu = NOCPU;
		vcpu->vmcb_pa = vtophys(&vcpu->vmcb);
		vmcb_init(svm_sc, i, iopm_pa, msrpm_pa, pml4_pa);
		svm_msr_guest_init(svm_sc, i);
	}
	return (svm_sc);
}

/*
 * Collateral for a generic SVM VM-exit.
 */
static void
vm_exit_svm(struct vm_exit *vme, uint64_t code, uint64_t info1, uint64_t info2)
{

	vme->exitcode = VM_EXITCODE_SVM;
	vme->u.svm.exitcode = code;
	vme->u.svm.exitinfo1 = info1;
	vme->u.svm.exitinfo2 = info2;
}

static int
svm_cpl(struct vmcb_state *state)
{

	/*
	 * From APMv2:
	 *   "Retrieve the CPL from the CPL field in the VMCB, not
	 *    from any segment DPL"
	 */
	return (state->cpl);
}

static enum vm_cpu_mode
svm_vcpu_mode(struct vmcb *vmcb)
{
	struct vmcb_segment seg;
	struct vmcb_state *state;
	int error;

	state = &vmcb->state;

	if (state->efer & EFER_LMA) {
		error = vmcb_seg(vmcb, VM_REG_GUEST_CS, &seg);
		KASSERT(error == 0, ("%s: vmcb_seg(cs) error %d", __func__,
		    error));

		/*
		 * Section 4.8.1 for APM2, check if Code Segment has
		 * Long attribute set in descriptor.
		 */
		if (seg.attrib & VMCB_CS_ATTRIB_L)
			return (CPU_MODE_64BIT);
		else
			return (CPU_MODE_COMPATIBILITY);
	} else  if (state->cr0 & CR0_PE) {
		return (CPU_MODE_PROTECTED);
	} else {
		return (CPU_MODE_REAL);
	}
}

static enum vm_paging_mode
svm_paging_mode(uint64_t cr0, uint64_t cr4, uint64_t efer)
{

	if ((cr0 & CR0_PG) == 0)
		return (PAGING_MODE_FLAT);
	if ((cr4 & CR4_PAE) == 0)
		return (PAGING_MODE_32);
	if (efer & EFER_LME)
		return (PAGING_MODE_64);
	else
		return (PAGING_MODE_PAE);
}

/*
 * ins/outs utility routines
 */
static uint64_t
svm_inout_str_index(struct svm_regctx *regs, int in)
{
	uint64_t val;

	val = in ? regs->sctx_rdi : regs->sctx_rsi;

	return (val);
}

static uint64_t
svm_inout_str_count(struct svm_regctx *regs, int rep)
{
	uint64_t val;

	val = rep ? regs->sctx_rcx : 1;

	return (val);
}

static void
svm_inout_str_seginfo(struct svm_softc *svm_sc, int vcpu, int64_t info1,
    int in, struct vm_inout_str *vis)
{
	int error, s;

	if (in) {
		vis->seg_name = VM_REG_GUEST_ES;
	} else {
		/* The segment field has standard encoding */
		s = (info1 >> 10) & 0x7;
		vis->seg_name = vm_segment_name(s);
	}

	error = vmcb_getdesc(svm_sc, vcpu, vis->seg_name, &vis->seg_desc);
	KASSERT(error == 0, ("%s: svm_getdesc error %d", __func__, error));
}

static int
svm_inout_str_addrsize(uint64_t info1)
{
        uint32_t size;

        size = (info1 >> 7) & 0x7;
        switch (size) {
        case 1:
                return (2);     /* 16 bit */
        case 2:
                return (4);     /* 32 bit */
        case 4:
                return (8);     /* 64 bit */
        default:
                panic("%s: invalid size encoding %d", __func__, size);
        }
}

static void
svm_paging_info(struct vmcb *vmcb, struct vm_guest_paging *paging)
{
	struct vmcb_state *state;

	state = &vmcb->state;
	paging->cr3 = state->cr3;
	paging->cpl = svm_cpl(state);
	paging->cpu_mode = svm_vcpu_mode(vmcb);
	paging->paging_mode = svm_paging_mode(state->cr0, state->cr4,
	    state->efer);
}

#define	UNHANDLED 0

/*
 * Handle guest I/O intercept.
 */
static int
svm_handle_io(struct svm_softc *svm_sc, int vcpu, struct vm_exit *vmexit)
{
	struct vmcb_ctrl *ctrl;
	struct vmcb_state *state;
	struct svm_regctx *regs;
	struct vm_inout_str *vis;
	uint64_t info1;
	int inout_string;

	state = svm_get_vmcb_state(svm_sc, vcpu);
	ctrl  = svm_get_vmcb_ctrl(svm_sc, vcpu);
	regs  = svm_get_guest_regctx(svm_sc, vcpu);

	info1 = ctrl->exitinfo1;
	inout_string = info1 & BIT(2) ? 1 : 0;

	/*
	 * The effective segment number in EXITINFO1[12:10] is populated
	 * only if the processor has the DecodeAssist capability.
	 *
	 * XXX this is not specified explicitly in APMv2 but can be verified
	 * empirically.
	 */
	if (inout_string && !decode_assist())
		return (UNHANDLED);

	vmexit->exitcode 	= VM_EXITCODE_INOUT;
	vmexit->u.inout.in 	= (info1 & BIT(0)) ? 1 : 0;
	vmexit->u.inout.string 	= inout_string;
	vmexit->u.inout.rep 	= (info1 & BIT(3)) ? 1 : 0;
	vmexit->u.inout.bytes 	= (info1 >> 4) & 0x7;
	vmexit->u.inout.port 	= (uint16_t)(info1 >> 16);
	vmexit->u.inout.eax 	= (uint32_t)(state->rax);

	if (inout_string) {
		vmexit->exitcode = VM_EXITCODE_INOUT_STR;
		vis = &vmexit->u.inout_str;
		svm_paging_info(svm_get_vmcb(svm_sc, vcpu), &vis->paging);
		vis->rflags = state->rflags;
		vis->cr0 = state->cr0;
		vis->index = svm_inout_str_index(regs, vmexit->u.inout.in);
		vis->count = svm_inout_str_count(regs, vmexit->u.inout.rep);
		vis->addrsize = svm_inout_str_addrsize(info1);
		svm_inout_str_seginfo(svm_sc, vcpu, info1,
		    vmexit->u.inout.in, vis);
	}

	return (UNHANDLED);
}

static int
npf_fault_type(uint64_t exitinfo1)
{

	if (exitinfo1 & VMCB_NPF_INFO1_W)
		return (VM_PROT_WRITE);
	else if (exitinfo1 & VMCB_NPF_INFO1_ID)
		return (VM_PROT_EXECUTE);
	else
		return (VM_PROT_READ);
}

static bool
svm_npf_emul_fault(uint64_t exitinfo1)
{
	
	if (exitinfo1 & VMCB_NPF_INFO1_ID) {
		return (false);
	}

	if (exitinfo1 & VMCB_NPF_INFO1_GPT) {
		return (false);
	}

	if ((exitinfo1 & VMCB_NPF_INFO1_GPA) == 0) {
		return (false);
	}

	return (true);	
}

static void
svm_handle_inst_emul(struct vmcb *vmcb, uint64_t gpa, struct vm_exit *vmexit)
{
	struct vm_guest_paging *paging;
	struct vmcb_segment seg;
	struct vmcb_ctrl *ctrl;
	char *inst_bytes;
	int error, inst_len;

	ctrl = &vmcb->ctrl;
	paging = &vmexit->u.inst_emul.paging;

	vmexit->exitcode = VM_EXITCODE_INST_EMUL;
	vmexit->u.inst_emul.gpa = gpa;
	vmexit->u.inst_emul.gla = VIE_INVALID_GLA;
	svm_paging_info(vmcb, paging);

	error = vmcb_seg(vmcb, VM_REG_GUEST_CS, &seg);
	KASSERT(error == 0, ("%s: vmcb_seg(CS) error %d", __func__, error));

	switch(paging->cpu_mode) {
	case CPU_MODE_REAL:
		vmexit->u.inst_emul.cs_base = seg.base;
		vmexit->u.inst_emul.cs_d = 0;
		break;
	case CPU_MODE_PROTECTED:
	case CPU_MODE_COMPATIBILITY:
		vmexit->u.inst_emul.cs_base = seg.base;

		/*
		 * Section 4.8.1 of APM2, Default Operand Size or D bit.
		 */
		vmexit->u.inst_emul.cs_d = (seg.attrib & VMCB_CS_ATTRIB_D) ?
		    1 : 0;
		break;
	default:
		vmexit->u.inst_emul.cs_base = 0;
		vmexit->u.inst_emul.cs_d = 0;
		break;	
	}

	/*
	 * Copy the instruction bytes into 'vie' if available.
	 */
	if (decode_assist() && !disable_npf_assist) {
		inst_len = ctrl->inst_len;
		inst_bytes = ctrl->inst_bytes;
	} else {
		inst_len = 0;
		inst_bytes = NULL;
	}
	vie_init(&vmexit->u.inst_emul.vie, inst_bytes, inst_len);
}

#ifdef KTR
static const char *
intrtype_to_str(int intr_type)
{
	switch (intr_type) {
	case VMCB_EVENTINJ_TYPE_INTR:
		return ("hwintr");
	case VMCB_EVENTINJ_TYPE_NMI:
		return ("nmi");
	case VMCB_EVENTINJ_TYPE_INTn:
		return ("swintr");
	case VMCB_EVENTINJ_TYPE_EXCEPTION:
		return ("exception");
	default:
		panic("%s: unknown intr_type %d", __func__, intr_type);
	}
}
#endif

/*
 * Inject an event to vcpu as described in section 15.20, "Event injection".
 */
static void
svm_eventinject(struct svm_softc *sc, int vcpu, int intr_type, int vector,
		 uint32_t error, bool ec_valid)
{
	struct vmcb_ctrl *ctrl;

	ctrl = svm_get_vmcb_ctrl(sc, vcpu);

	KASSERT((ctrl->eventinj & VMCB_EVENTINJ_VALID) == 0,
	    ("%s: event already pending %#lx", __func__, ctrl->eventinj));

	KASSERT(vector >=0 && vector <= 255, ("%s: invalid vector %d",
	    __func__, vector));

	switch (intr_type) {
	case VMCB_EVENTINJ_TYPE_INTR:
	case VMCB_EVENTINJ_TYPE_NMI:
	case VMCB_EVENTINJ_TYPE_INTn:
		break;
	case VMCB_EVENTINJ_TYPE_EXCEPTION:
		if (vector >= 0 && vector <= 31 && vector != 2)
			break;
		/* FALLTHROUGH */
	default:
		panic("%s: invalid intr_type/vector: %d/%d", __func__,
		    intr_type, vector);
	}
	ctrl->eventinj = vector | (intr_type << 8) | VMCB_EVENTINJ_VALID;
	if (ec_valid) {
		ctrl->eventinj |= VMCB_EVENTINJ_EC_VALID;
		ctrl->eventinj |= (uint64_t)error << 32;
		VCPU_CTR3(sc->vm, vcpu, "Injecting %s at vector %d errcode %#x",
		    intrtype_to_str(intr_type), vector, error);
	} else {
		VCPU_CTR2(sc->vm, vcpu, "Injecting %s at vector %d",
		    intrtype_to_str(intr_type), vector);
	}
}

static void
svm_update_virqinfo(struct svm_softc *sc, int vcpu)
{
	struct vm *vm;
	struct vlapic *vlapic;
	struct vmcb_ctrl *ctrl;

	vm = sc->vm;
	vlapic = vm_lapic(vm, vcpu);
	ctrl = svm_get_vmcb_ctrl(sc, vcpu);

	/* Update %cr8 in the emulated vlapic */
	vlapic_set_cr8(vlapic, ctrl->v_tpr);

	/* Virtual interrupt injection is not used. */
	KASSERT(ctrl->v_intr_vector == 0, ("%s: invalid "
	    "v_intr_vector %d", __func__, ctrl->v_intr_vector));
}

static void
svm_save_intinfo(struct svm_softc *svm_sc, int vcpu)
{
	struct vmcb_ctrl *ctrl;
	uint64_t intinfo;

	ctrl  = svm_get_vmcb_ctrl(svm_sc, vcpu);
	intinfo = ctrl->exitintinfo;	
	if (!VMCB_EXITINTINFO_VALID(intinfo))
		return;

	/*
	 * From APMv2, Section "Intercepts during IDT interrupt delivery"
	 *
	 * If a #VMEXIT happened during event delivery then record the event
	 * that was being delivered.
	 */
	VCPU_CTR2(svm_sc->vm, vcpu, "SVM:Pending INTINFO(0x%lx), vector=%d.\n",
		intinfo, VMCB_EXITINTINFO_VECTOR(intinfo));
	vmm_stat_incr(svm_sc->vm, vcpu, VCPU_EXITINTINFO, 1);
	vm_exit_intinfo(svm_sc->vm, vcpu, intinfo);
}

#ifdef INVARIANTS
static __inline int
vintr_intercept_enabled(struct svm_softc *sc, int vcpu)
{

	return (svm_get_intercept(sc, vcpu, VMCB_CTRL1_INTCPT,
	    VMCB_INTCPT_VINTR));
}
#endif

static __inline void
enable_intr_window_exiting(struct svm_softc *sc, int vcpu)
{
	struct vmcb_ctrl *ctrl;

	ctrl = svm_get_vmcb_ctrl(sc, vcpu);

	if (ctrl->v_irq && ctrl->v_intr_vector == 0) {
		KASSERT(ctrl->v_ign_tpr, ("%s: invalid v_ign_tpr", __func__));
		KASSERT(vintr_intercept_enabled(sc, vcpu),
		    ("%s: vintr intercept should be enabled", __func__));
		return;
	}

	VCPU_CTR0(sc->vm, vcpu, "Enable intr window exiting");
	ctrl->v_irq = 1;
	ctrl->v_ign_tpr = 1;
	ctrl->v_intr_vector = 0;
	svm_set_dirty(sc, vcpu, VMCB_CACHE_TPR);
	svm_enable_intercept(sc, vcpu, VMCB_CTRL1_INTCPT, VMCB_INTCPT_VINTR);
}

static __inline void
disable_intr_window_exiting(struct svm_softc *sc, int vcpu)
{
	struct vmcb_ctrl *ctrl;

	ctrl = svm_get_vmcb_ctrl(sc, vcpu);

	if (!ctrl->v_irq && ctrl->v_intr_vector == 0) {
		KASSERT(!vintr_intercept_enabled(sc, vcpu),
		    ("%s: vintr intercept should be disabled", __func__));
		return;
	}

	VCPU_CTR0(sc->vm, vcpu, "Disable intr window exiting");
	ctrl->v_irq = 0;
	ctrl->v_intr_vector = 0;
	svm_set_dirty(sc, vcpu, VMCB_CACHE_TPR);
	svm_disable_intercept(sc, vcpu, VMCB_CTRL1_INTCPT, VMCB_INTCPT_VINTR);
}

static int
svm_modify_intr_shadow(struct svm_softc *sc, int vcpu, uint64_t val)
{
	struct vmcb_ctrl *ctrl;
	int oldval, newval;

	ctrl = svm_get_vmcb_ctrl(sc, vcpu);
	oldval = ctrl->intr_shadow;
	newval = val ? 1 : 0;
	if (newval != oldval) {
		ctrl->intr_shadow = newval;
		VCPU_CTR1(sc->vm, vcpu, "Setting intr_shadow to %d", newval);
	}
	return (0);
}

static int
svm_get_intr_shadow(struct svm_softc *sc, int vcpu, uint64_t *val)
{
	struct vmcb_ctrl *ctrl;

	ctrl = svm_get_vmcb_ctrl(sc, vcpu);
	*val = ctrl->intr_shadow;
	return (0);
}

/*
 * Once an NMI is injected it blocks delivery of further NMIs until the handler
 * executes an IRET. The IRET intercept is enabled when an NMI is injected to
 * to track when the vcpu is done handling the NMI.
 */
static int
nmi_blocked(struct svm_softc *sc, int vcpu)
{
	int blocked;

	blocked = svm_get_intercept(sc, vcpu, VMCB_CTRL1_INTCPT,
	    VMCB_INTCPT_IRET);
	return (blocked);
}

static void
enable_nmi_blocking(struct svm_softc *sc, int vcpu)
{

	KASSERT(!nmi_blocked(sc, vcpu), ("vNMI already blocked"));
	VCPU_CTR0(sc->vm, vcpu, "vNMI blocking enabled");
	svm_enable_intercept(sc, vcpu, VMCB_CTRL1_INTCPT, VMCB_INTCPT_IRET);
}

static void
clear_nmi_blocking(struct svm_softc *sc, int vcpu)
{
	int error;

	KASSERT(nmi_blocked(sc, vcpu), ("vNMI already unblocked"));
	VCPU_CTR0(sc->vm, vcpu, "vNMI blocking cleared");
	/*
	 * When the IRET intercept is cleared the vcpu will attempt to execute
	 * the "iret" when it runs next. However, it is possible to inject
	 * another NMI into the vcpu before the "iret" has actually executed.
	 *
	 * For e.g. if the "iret" encounters a #NPF when accessing the stack
	 * it will trap back into the hypervisor. If an NMI is pending for
	 * the vcpu it will be injected into the guest.
	 *
	 * XXX this needs to be fixed
	 */
	svm_disable_intercept(sc, vcpu, VMCB_CTRL1_INTCPT, VMCB_INTCPT_IRET);

	/*
	 * Set 'intr_shadow' to prevent an NMI from being injected on the
	 * immediate VMRUN.
	 */
	error = svm_modify_intr_shadow(sc, vcpu, 1);
	KASSERT(!error, ("%s: error %d setting intr_shadow", __func__, error));
}

#define	EFER_MBZ_BITS	0xFFFFFFFFFFFF0200UL

static int
svm_write_efer(struct svm_softc *sc, int vcpu, uint64_t newval, bool *retu)
{
	struct vm_exit *vme;
	struct vmcb_state *state;
	uint64_t changed, lma, oldval;
	int error;

	state = svm_get_vmcb_state(sc, vcpu);

	oldval = state->efer;
	VCPU_CTR2(sc->vm, vcpu, "wrmsr(efer) %#lx/%#lx", oldval, newval);

	newval &= ~0xFE;		/* clear the Read-As-Zero (RAZ) bits */
	changed = oldval ^ newval;

	if (newval & EFER_MBZ_BITS)
		goto gpf;

	/* APMv2 Table 14-5 "Long-Mode Consistency Checks" */
	if (changed & EFER_LME) {
		if (state->cr0 & CR0_PG)
			goto gpf;
	}

	/* EFER.LMA = EFER.LME & CR0.PG */
	if ((newval & EFER_LME) != 0 && (state->cr0 & CR0_PG) != 0)
		lma = EFER_LMA;
	else
		lma = 0;

	if ((newval & EFER_LMA) != lma)
		goto gpf;

	if (newval & EFER_NXE) {
		if (!vm_cpuid_capability(sc->vm, vcpu, VCC_NO_EXECUTE))
			goto gpf;
	}

	/*
	 * XXX bhyve does not enforce segment limits in 64-bit mode. Until
	 * this is fixed flag guest attempt to set EFER_LMSLE as an error.
	 */
	if (newval & EFER_LMSLE) {
		vme = vm_exitinfo(sc->vm, vcpu);
		vm_exit_svm(vme, VMCB_EXIT_MSR, 1, 0);
		*retu = true;
		return (0);
	}

	if (newval & EFER_FFXSR) {
		if (!vm_cpuid_capability(sc->vm, vcpu, VCC_FFXSR))
			goto gpf;
	}

	if (newval & EFER_TCE) {
		if (!vm_cpuid_capability(sc->vm, vcpu, VCC_TCE))
			goto gpf;
	}

	error = svm_setreg(sc, vcpu, VM_REG_GUEST_EFER, newval);
	KASSERT(error == 0, ("%s: error %d updating efer", __func__, error));
	return (0);
gpf:
	vm_inject_gp(sc->vm, vcpu);
	return (0);
}

static int
emulate_wrmsr(struct svm_softc *sc, int vcpu, u_int num, uint64_t val,
    bool *retu)
{
	int error;

	if (lapic_msr(num))
		error = lapic_wrmsr(sc->vm, vcpu, num, val, retu);
	else if (num == MSR_EFER)
		error = svm_write_efer(sc, vcpu, val, retu);
	else
		error = svm_wrmsr(sc, vcpu, num, val, retu);

	return (error);
}

static int
emulate_rdmsr(struct svm_softc *sc, int vcpu, u_int num, bool *retu)
{
	struct vmcb_state *state;
	struct svm_regctx *ctx;
	uint64_t result;
	int error;

	if (lapic_msr(num))
		error = lapic_rdmsr(sc->vm, vcpu, num, &result, retu);
	else
		error = svm_rdmsr(sc, vcpu, num, &result, retu);

	if (error == 0) {
		state = svm_get_vmcb_state(sc, vcpu);
		ctx = svm_get_guest_regctx(sc, vcpu);
		state->rax = result & 0xffffffff;
		ctx->sctx_rdx = result >> 32;
	}

	return (error);
}

#ifdef KTR
static const char *
exit_reason_to_str(uint64_t reason)
{
	static char reasonbuf[32];

	switch (reason) {
	case VMCB_EXIT_INVALID:
		return ("invalvmcb");
	case VMCB_EXIT_SHUTDOWN:
		return ("shutdown");
	case VMCB_EXIT_NPF:
		return ("nptfault");
	case VMCB_EXIT_PAUSE:
		return ("pause");
	case VMCB_EXIT_HLT:
		return ("hlt");
	case VMCB_EXIT_CPUID:
		return ("cpuid");
	case VMCB_EXIT_IO:
		return ("inout");
	case VMCB_EXIT_MC:
		return ("mchk");
	case VMCB_EXIT_INTR:
		return ("extintr");
	case VMCB_EXIT_NMI:
		return ("nmi");
	case VMCB_EXIT_VINTR:
		return ("vintr");
	case VMCB_EXIT_MSR:
		return ("msr");
	case VMCB_EXIT_IRET:
		return ("iret");
	case VMCB_EXIT_MONITOR:
		return ("monitor");
	case VMCB_EXIT_MWAIT:
		return ("mwait");
	default:
		snprintf(reasonbuf, sizeof(reasonbuf), "%#lx", reason);
		return (reasonbuf);
	}
}
#endif	/* KTR */

/*
 * From section "State Saved on Exit" in APMv2: nRIP is saved for all #VMEXITs
 * that are due to instruction intercepts as well as MSR and IOIO intercepts
 * and exceptions caused by INT3, INTO and BOUND instructions.
 *
 * Return 1 if the nRIP is valid and 0 otherwise.
 */
static int
nrip_valid(uint64_t exitcode)
{
	switch (exitcode) {
	case 0x00 ... 0x0F:	/* read of CR0 through CR15 */
	case 0x10 ... 0x1F:	/* write of CR0 through CR15 */
	case 0x20 ... 0x2F:	/* read of DR0 through DR15 */
	case 0x30 ... 0x3F:	/* write of DR0 through DR15 */
	case 0x43:		/* INT3 */
	case 0x44:		/* INTO */
	case 0x45:		/* BOUND */
	case 0x65 ... 0x7C:	/* VMEXIT_CR0_SEL_WRITE ... VMEXIT_MSR */
	case 0x80 ... 0x8D:	/* VMEXIT_VMRUN ... VMEXIT_XSETBV */
		return (1);
	default:
		return (0);
	}
}

static int
svm_vmexit(struct svm_softc *svm_sc, int vcpu, struct vm_exit *vmexit)
{
	struct vmcb *vmcb;
	struct vmcb_state *state;
	struct vmcb_ctrl *ctrl;
	struct svm_regctx *ctx;
	uint64_t code, info1, info2, val;
	uint32_t eax, ecx, edx;
	int error, errcode_valid, handled, idtvec, reflect;
	bool retu;

	ctx = svm_get_guest_regctx(svm_sc, vcpu);
	vmcb = svm_get_vmcb(svm_sc, vcpu);
	state = &vmcb->state;
	ctrl = &vmcb->ctrl;

	handled = 0;
	code = ctrl->exitcode;
	info1 = ctrl->exitinfo1;
	info2 = ctrl->exitinfo2;

	vmexit->exitcode = VM_EXITCODE_BOGUS;
	vmexit->rip = state->rip;
	vmexit->inst_length = nrip_valid(code) ? ctrl->nrip - state->rip : 0;

	vmm_stat_incr(svm_sc->vm, vcpu, VMEXIT_COUNT, 1);

	/*
	 * #VMEXIT(INVALID) needs to be handled early because the VMCB is
	 * in an inconsistent state and can trigger assertions that would
	 * never happen otherwise.
	 */
	if (code == VMCB_EXIT_INVALID) {
		vm_exit_svm(vmexit, code, info1, info2);
		return (0);
	}

	KASSERT((ctrl->eventinj & VMCB_EVENTINJ_VALID) == 0, ("%s: event "
	    "injection valid bit is set %#lx", __func__, ctrl->eventinj));

	KASSERT(vmexit->inst_length >= 0 && vmexit->inst_length <= 15,
	    ("invalid inst_length %d: code (%#lx), info1 (%#lx), info2 (%#lx)",
	    vmexit->inst_length, code, info1, info2));

	svm_update_virqinfo(svm_sc, vcpu);
	svm_save_intinfo(svm_sc, vcpu);

	switch (code) {
	case VMCB_EXIT_IRET:
		/*
		 * Restart execution at "iret" but with the intercept cleared.
		 */
		vmexit->inst_length = 0;
		clear_nmi_blocking(svm_sc, vcpu);
		handled = 1;
		break;
	case VMCB_EXIT_VINTR:	/* interrupt window exiting */
		vmm_stat_incr(svm_sc->vm, vcpu, VMEXIT_VINTR, 1);
		handled = 1;
		break;
	case VMCB_EXIT_INTR:	/* external interrupt */
		vmm_stat_incr(svm_sc->vm, vcpu, VMEXIT_EXTINT, 1);
		handled = 1;
		break;
	case VMCB_EXIT_NMI:	/* external NMI */
		handled = 1;
		break;
	case 0x40 ... 0x5F:
		vmm_stat_incr(svm_sc->vm, vcpu, VMEXIT_EXCEPTION, 1);
		reflect = 1;
		idtvec = code - 0x40;
		switch (idtvec) {
		case IDT_MC:
			/*
			 * Call the machine check handler by hand. Also don't
			 * reflect the machine check back into the guest.
			 */
			reflect = 0;
			VCPU_CTR0(svm_sc->vm, vcpu, "Vectoring to MCE handler");
			__asm __volatile("int $18");
			break;
		case IDT_PF:
			error = svm_setreg(svm_sc, vcpu, VM_REG_GUEST_CR2,
			    info2);
			KASSERT(error == 0, ("%s: error %d updating cr2",
			    __func__, error));
			/* fallthru */
		case IDT_NP:
		case IDT_SS:
		case IDT_GP:
		case IDT_AC:
		case IDT_TS:
			errcode_valid = 1;
			break;

		case IDT_DF:
			errcode_valid = 1;
			info1 = 0;
			break;

		case IDT_BP:
		case IDT_OF:
		case IDT_BR:
			/*
			 * The 'nrip' field is populated for INT3, INTO and
			 * BOUND exceptions and this also implies that
			 * 'inst_length' is non-zero.
			 *
			 * Reset 'inst_length' to zero so the guest %rip at
			 * event injection is identical to what it was when
			 * the exception originally happened.
			 */
			VCPU_CTR2(svm_sc->vm, vcpu, "Reset inst_length from %d "
			    "to zero before injecting exception %d",
			    vmexit->inst_length, idtvec);
			vmexit->inst_length = 0;
			/* fallthru */
		default:
			errcode_valid = 0;
			info1 = 0;
			break;
		}
		KASSERT(vmexit->inst_length == 0, ("invalid inst_length (%d) "
		    "when reflecting exception %d into guest",
		    vmexit->inst_length, idtvec));

		if (reflect) {
			/* Reflect the exception back into the guest */
			VCPU_CTR2(svm_sc->vm, vcpu, "Reflecting exception "
			    "%d/%#x into the guest", idtvec, (int)info1);
			error = vm_inject_exception(svm_sc->vm, vcpu, idtvec,
			    errcode_valid, info1, 0);
			KASSERT(error == 0, ("%s: vm_inject_exception error %d",
			    __func__, error));
		}
		handled = 1;
		break;
	case VMCB_EXIT_MSR:	/* MSR access. */
		eax = state->rax;
		ecx = ctx->sctx_rcx;
		edx = ctx->sctx_rdx;
		retu = false;	

		if (info1) {
			vmm_stat_incr(svm_sc->vm, vcpu, VMEXIT_WRMSR, 1);
			val = (uint64_t)edx << 32 | eax;
			VCPU_CTR2(svm_sc->vm, vcpu, "wrmsr %#x val %#lx",
			    ecx, val);
			if (emulate_wrmsr(svm_sc, vcpu, ecx, val, &retu)) {
				vmexit->exitcode = VM_EXITCODE_WRMSR;
				vmexit->u.msr.code = ecx;
				vmexit->u.msr.wval = val;
			} else if (!retu) {
				handled = 1;
			} else {
				KASSERT(vmexit->exitcode != VM_EXITCODE_BOGUS,
				    ("emulate_wrmsr retu with bogus exitcode"));
			}
		} else {
			VCPU_CTR1(svm_sc->vm, vcpu, "rdmsr %#x", ecx);
			vmm_stat_incr(svm_sc->vm, vcpu, VMEXIT_RDMSR, 1);
			if (emulate_rdmsr(svm_sc, vcpu, ecx, &retu)) {
				vmexit->exitcode = VM_EXITCODE_RDMSR;
				vmexit->u.msr.code = ecx;
			} else if (!retu) {
				handled = 1;
			} else {
				KASSERT(vmexit->exitcode != VM_EXITCODE_BOGUS,
				    ("emulate_rdmsr retu with bogus exitcode"));
			}
		}
		break;
	case VMCB_EXIT_IO:
		handled = svm_handle_io(svm_sc, vcpu, vmexit);
		vmm_stat_incr(svm_sc->vm, vcpu, VMEXIT_INOUT, 1);
		break;
	case VMCB_EXIT_CPUID:
		vmm_stat_incr(svm_sc->vm, vcpu, VMEXIT_CPUID, 1);
		handled = x86_emulate_cpuid(svm_sc->vm, vcpu,
		    (uint32_t *)&state->rax,
		    (uint32_t *)&ctx->sctx_rbx,
		    (uint32_t *)&ctx->sctx_rcx,
		    (uint32_t *)&ctx->sctx_rdx);
		break;
	case VMCB_EXIT_HLT:
		vmm_stat_incr(svm_sc->vm, vcpu, VMEXIT_HLT, 1);
		vmexit->exitcode = VM_EXITCODE_HLT;
		vmexit->u.hlt.rflags = state->rflags;
		break;
	case VMCB_EXIT_PAUSE:
		vmexit->exitcode = VM_EXITCODE_PAUSE;
		vmm_stat_incr(svm_sc->vm, vcpu, VMEXIT_PAUSE, 1);
		break;
	case VMCB_EXIT_NPF:
		/* EXITINFO2 contains the faulting guest physical address */
		if (info1 & VMCB_NPF_INFO1_RSV) {
			VCPU_CTR2(svm_sc->vm, vcpu, "nested page fault with "
			    "reserved bits set: info1(%#lx) info2(%#lx)",
			    info1, info2);
		} else if (vm_mem_allocated(svm_sc->vm, vcpu, info2)) {
			vmexit->exitcode = VM_EXITCODE_PAGING;
			vmexit->u.paging.gpa = info2;
			vmexit->u.paging.fault_type = npf_fault_type(info1);
			vmm_stat_incr(svm_sc->vm, vcpu, VMEXIT_NESTED_FAULT, 1);
			VCPU_CTR3(svm_sc->vm, vcpu, "nested page fault "
			    "on gpa %#lx/%#lx at rip %#lx",
			    info2, info1, state->rip);
		} else if (svm_npf_emul_fault(info1)) {
			svm_handle_inst_emul(vmcb, info2, vmexit);
			vmm_stat_incr(svm_sc->vm, vcpu, VMEXIT_INST_EMUL, 1);
			VCPU_CTR3(svm_sc->vm, vcpu, "inst_emul fault "
			    "for gpa %#lx/%#lx at rip %#lx",
			    info2, info1, state->rip);
		}
		break;
	case VMCB_EXIT_MONITOR:
		vmexit->exitcode = VM_EXITCODE_MONITOR;
		break;
	case VMCB_EXIT_MWAIT:
		vmexit->exitcode = VM_EXITCODE_MWAIT;
		break;
	default:
		vmm_stat_incr(svm_sc->vm, vcpu, VMEXIT_UNKNOWN, 1);
		break;
	}	

	VCPU_CTR4(svm_sc->vm, vcpu, "%s %s vmexit at %#lx/%d",
	    handled ? "handled" : "unhandled", exit_reason_to_str(code),
	    vmexit->rip, vmexit->inst_length);

	if (handled) {
		vmexit->rip += vmexit->inst_length;
		vmexit->inst_length = 0;
		state->rip = vmexit->rip;
	} else {
		if (vmexit->exitcode == VM_EXITCODE_BOGUS) {
			/*
			 * If this VM exit was not claimed by anybody then
			 * treat it as a generic SVM exit.
			 */
			vm_exit_svm(vmexit, code, info1, info2);
		} else {
			/*
			 * The exitcode and collateral have been populated.
			 * The VM exit will be processed further in userland.
			 */
		}
	}
	return (handled);
}

static void
svm_inj_intinfo(struct svm_softc *svm_sc, int vcpu)
{
	uint64_t intinfo;

	if (!vm_entry_intinfo(svm_sc->vm, vcpu, &intinfo))
		return;

	KASSERT(VMCB_EXITINTINFO_VALID(intinfo), ("%s: entry intinfo is not "
	    "valid: %#lx", __func__, intinfo));

	svm_eventinject(svm_sc, vcpu, VMCB_EXITINTINFO_TYPE(intinfo),
		VMCB_EXITINTINFO_VECTOR(intinfo),
		VMCB_EXITINTINFO_EC(intinfo),
		VMCB_EXITINTINFO_EC_VALID(intinfo));
	vmm_stat_incr(svm_sc->vm, vcpu, VCPU_INTINFO_INJECTED, 1);
	VCPU_CTR1(svm_sc->vm, vcpu, "Injected entry intinfo: %#lx", intinfo);
}

/*
 * Inject event to virtual cpu.
 */
static void
svm_inj_interrupts(struct svm_softc *sc, int vcpu, struct vlapic *vlapic)
{
	struct vmcb_ctrl *ctrl;
	struct vmcb_state *state;
	struct svm_vcpu *vcpustate;
	uint8_t v_tpr;
	int vector, need_intr_window;
	int extint_pending;

	state = svm_get_vmcb_state(sc, vcpu);
	ctrl  = svm_get_vmcb_ctrl(sc, vcpu);
	vcpustate = svm_get_vcpu(sc, vcpu);

	need_intr_window = 0;

	if (vcpustate->nextrip != state->rip) {
		ctrl->intr_shadow = 0;
		VCPU_CTR2(sc->vm, vcpu, "Guest interrupt blocking "
		    "cleared due to rip change: %#lx/%#lx",
		    vcpustate->nextrip, state->rip);
	}

	/*
	 * Inject pending events or exceptions for this vcpu.
	 *
	 * An event might be pending because the previous #VMEXIT happened
	 * during event delivery (i.e. ctrl->exitintinfo).
	 *
	 * An event might also be pending because an exception was injected
	 * by the hypervisor (e.g. #PF during instruction emulation).
	 */
	svm_inj_intinfo(sc, vcpu);

	/* NMI event has priority over interrupts. */
	if (vm_nmi_pending(sc->vm, vcpu)) {
		if (nmi_blocked(sc, vcpu)) {
			/*
			 * Can't inject another NMI if the guest has not
			 * yet executed an "iret" after the last NMI.
			 */
			VCPU_CTR0(sc->vm, vcpu, "Cannot inject NMI due "
			    "to NMI-blocking");
		} else if (ctrl->intr_shadow) {
			/*
			 * Can't inject an NMI if the vcpu is in an intr_shadow.
			 */
			VCPU_CTR0(sc->vm, vcpu, "Cannot inject NMI due to "
			    "interrupt shadow");
			need_intr_window = 1;
			goto done;
		} else if (ctrl->eventinj & VMCB_EVENTINJ_VALID) {
			/*
			 * If there is already an exception/interrupt pending
			 * then defer the NMI until after that.
			 */
			VCPU_CTR1(sc->vm, vcpu, "Cannot inject NMI due to "
			    "eventinj %#lx", ctrl->eventinj);

			/*
			 * Use self-IPI to trigger a VM-exit as soon as
			 * possible after the event injection is completed.
			 *
			 * This works only if the external interrupt exiting
			 * is at a lower priority than the event injection.
			 *
			 * Although not explicitly specified in APMv2 the
			 * relative priorities were verified empirically.
			 */
			ipi_cpu(curcpu, IPI_AST);	/* XXX vmm_ipinum? */
		} else {
			vm_nmi_clear(sc->vm, vcpu);

			/* Inject NMI, vector number is not used */
			svm_eventinject(sc, vcpu, VMCB_EVENTINJ_TYPE_NMI,
			    IDT_NMI, 0, false);

			/* virtual NMI blocking is now in effect */
			enable_nmi_blocking(sc, vcpu);

			VCPU_CTR0(sc->vm, vcpu, "Injecting vNMI");
		}
	}

	extint_pending = vm_extint_pending(sc->vm, vcpu);
	if (!extint_pending) {
		if (!vlapic_pending_intr(vlapic, &vector))
			goto done;
		KASSERT(vector >= 16 && vector <= 255,
		    ("invalid vector %d from local APIC", vector));
	} else {
		/* Ask the legacy pic for a vector to inject */
		vatpic_pending_intr(sc->vm, &vector);
		KASSERT(vector >= 0 && vector <= 255,
		    ("invalid vector %d from INTR", vector));
	}

	/*
	 * If the guest has disabled interrupts or is in an interrupt shadow
	 * then we cannot inject the pending interrupt.
	 */
	if ((state->rflags & PSL_I) == 0) {
		VCPU_CTR2(sc->vm, vcpu, "Cannot inject vector %d due to "
		    "rflags %#lx", vector, state->rflags);
		need_intr_window = 1;
		goto done;
	}

	if (ctrl->intr_shadow) {
		VCPU_CTR1(sc->vm, vcpu, "Cannot inject vector %d due to "
		    "interrupt shadow", vector);
		need_intr_window = 1;
		goto done;
	}

	if (ctrl->eventinj & VMCB_EVENTINJ_VALID) {
		VCPU_CTR2(sc->vm, vcpu, "Cannot inject vector %d due to "
		    "eventinj %#lx", vector, ctrl->eventinj);
		need_intr_window = 1;
		goto done;
	}

	svm_eventinject(sc, vcpu, VMCB_EVENTINJ_TYPE_INTR, vector, 0, false);

	if (!extint_pending) {
		vlapic_intr_accepted(vlapic, vector);
	} else {
		vm_extint_clear(sc->vm, vcpu);
		vatpic_intr_accepted(sc->vm, vector);
	}

	/*
	 * Force a VM-exit as soon as the vcpu is ready to accept another
	 * interrupt. This is done because the PIC might have another vector
	 * that it wants to inject. Also, if the APIC has a pending interrupt
	 * that was preempted by the ExtInt then it allows us to inject the
	 * APIC vector as soon as possible.
	 */
	need_intr_window = 1;
done:
	/*
	 * The guest can modify the TPR by writing to %CR8. In guest mode
	 * the processor reflects this write to V_TPR without hypervisor
	 * intervention.
	 *
	 * The guest can also modify the TPR by writing to it via the memory
	 * mapped APIC page. In this case, the write will be emulated by the
	 * hypervisor. For this reason V_TPR must be updated before every
	 * VMRUN.
	 */
	v_tpr = vlapic_get_cr8(vlapic);
	KASSERT(v_tpr <= 15, ("invalid v_tpr %#x", v_tpr));
	if (ctrl->v_tpr != v_tpr) {
		VCPU_CTR2(sc->vm, vcpu, "VMCB V_TPR changed from %#x to %#x",
		    ctrl->v_tpr, v_tpr);
		ctrl->v_tpr = v_tpr;
		svm_set_dirty(sc, vcpu, VMCB_CACHE_TPR);
	}

	if (need_intr_window) {
		/*
		 * We use V_IRQ in conjunction with the VINTR intercept to
		 * trap into the hypervisor as soon as a virtual interrupt
		 * can be delivered.
		 *
		 * Since injected events are not subject to intercept checks
		 * we need to ensure that the V_IRQ is not actually going to
		 * be delivered on VM entry. The KASSERT below enforces this.
		 */
		KASSERT((ctrl->eventinj & VMCB_EVENTINJ_VALID) != 0 ||
		    (state->rflags & PSL_I) == 0 || ctrl->intr_shadow,
		    ("Bogus intr_window_exiting: eventinj (%#lx), "
		    "intr_shadow (%u), rflags (%#lx)",
		    ctrl->eventinj, ctrl->intr_shadow, state->rflags));
		enable_intr_window_exiting(sc, vcpu);
	} else {
		disable_intr_window_exiting(sc, vcpu);
	}
}

static __inline void
restore_host_tss(void)
{
	struct system_segment_descriptor *tss_sd;

	/*
	 * The TSS descriptor was in use prior to launching the guest so it
	 * has been marked busy.
	 *
	 * 'ltr' requires the descriptor to be marked available so change the
	 * type to "64-bit available TSS".
	 */
	tss_sd = PCPU_GET(tss);
	tss_sd->sd_type = SDT_SYSTSS;
	ltr(GSEL(GPROC0_SEL, SEL_KPL));
}

static void
check_asid(struct svm_softc *sc, int vcpuid, pmap_t pmap, u_int thiscpu)
{
	struct svm_vcpu *vcpustate;
	struct vmcb_ctrl *ctrl;
	long eptgen;
	bool alloc_asid;

	KASSERT(CPU_ISSET(thiscpu, &pmap->pm_active), ("%s: nested pmap not "
	    "active on cpu %u", __func__, thiscpu));

	vcpustate = svm_get_vcpu(sc, vcpuid);
	ctrl = svm_get_vmcb_ctrl(sc, vcpuid);

	/*
	 * The TLB entries associated with the vcpu's ASID are not valid
	 * if either of the following conditions is true:
	 *
	 * 1. The vcpu's ASID generation is different than the host cpu's
	 *    ASID generation. This happens when the vcpu migrates to a new
	 *    host cpu. It can also happen when the number of vcpus executing
	 *    on a host cpu is greater than the number of ASIDs available.
	 *
	 * 2. The pmap generation number is different than the value cached in
	 *    the 'vcpustate'. This happens when the host invalidates pages
	 *    belonging to the guest.
	 *
	 *	asidgen		eptgen	      Action
	 *	mismatch	mismatch
	 *	   0		   0		(a)
	 *	   0		   1		(b1) or (b2)
	 *	   1		   0		(c)
	 *	   1		   1		(d)
	 *
	 * (a) There is no mismatch in eptgen or ASID generation and therefore
	 *     no further action is needed.
	 *
	 * (b1) If the cpu supports FlushByAsid then the vcpu's ASID is
	 *      retained and the TLB entries associated with this ASID
	 *      are flushed by VMRUN.
	 *
	 * (b2) If the cpu does not support FlushByAsid then a new ASID is
	 *      allocated.
	 *
	 * (c) A new ASID is allocated.
	 *
	 * (d) A new ASID is allocated.
	 */

	alloc_asid = false;
	eptgen = pmap->pm_eptgen;
	ctrl->tlb_ctrl = VMCB_TLB_FLUSH_NOTHING;

	if (vcpustate->asid.gen != asid[thiscpu].gen) {
		alloc_asid = true;	/* (c) and (d) */
	} else if (vcpustate->eptgen != eptgen) {
		if (flush_by_asid())
			ctrl->tlb_ctrl = VMCB_TLB_FLUSH_GUEST;	/* (b1) */
		else
			alloc_asid = true;			/* (b2) */
	} else {
		/*
		 * This is the common case (a).
		 */
		KASSERT(!alloc_asid, ("ASID allocation not necessary"));
		KASSERT(ctrl->tlb_ctrl == VMCB_TLB_FLUSH_NOTHING,
		    ("Invalid VMCB tlb_ctrl: %#x", ctrl->tlb_ctrl));
	}

	if (alloc_asid) {
		if (++asid[thiscpu].num >= nasid) {
			asid[thiscpu].num = 1;
			if (++asid[thiscpu].gen == 0)
				asid[thiscpu].gen = 1;
			/*
			 * If this cpu does not support "flush-by-asid"
			 * then flush the entire TLB on a generation
			 * bump. Subsequent ASID allocation in this
			 * generation can be done without a TLB flush.
			 */
			if (!flush_by_asid())
				ctrl->tlb_ctrl = VMCB_TLB_FLUSH_ALL;
		}
		vcpustate->asid.gen = asid[thiscpu].gen;
		vcpustate->asid.num = asid[thiscpu].num;

		ctrl->asid = vcpustate->asid.num;
		svm_set_dirty(sc, vcpuid, VMCB_CACHE_ASID);
		/*
		 * If this cpu supports "flush-by-asid" then the TLB
		 * was not flushed after the generation bump. The TLB
		 * is flushed selectively after every new ASID allocation.
		 */
		if (flush_by_asid())
			ctrl->tlb_ctrl = VMCB_TLB_FLUSH_GUEST;
	}
	vcpustate->eptgen = eptgen;

	KASSERT(ctrl->asid != 0, ("Guest ASID must be non-zero"));
	KASSERT(ctrl->asid == vcpustate->asid.num,
	    ("ASID mismatch: %u/%u", ctrl->asid, vcpustate->asid.num));
}

static __inline void
disable_gintr(void)
{

	__asm __volatile("clgi");
}

static __inline void
enable_gintr(void)
{

        __asm __volatile("stgi");
}

static __inline void
svm_dr_enter_guest(struct svm_regctx *gctx)
{

	/* Save host control debug registers. */
	gctx->host_dr7 = rdr7();
	gctx->host_debugctl = rdmsr(MSR_DEBUGCTLMSR);

	/*
	 * Disable debugging in DR7 and DEBUGCTL to avoid triggering
	 * exceptions in the host based on the guest DRx values.  The
	 * guest DR6, DR7, and DEBUGCTL are saved/restored in the
	 * VMCB.
	 */
	load_dr7(0);
	wrmsr(MSR_DEBUGCTLMSR, 0);

	/* Save host debug registers. */
	gctx->host_dr0 = rdr0();
	gctx->host_dr1 = rdr1();
	gctx->host_dr2 = rdr2();
	gctx->host_dr3 = rdr3();
	gctx->host_dr6 = rdr6();

	/* Restore guest debug registers. */
	load_dr0(gctx->sctx_dr0);
	load_dr1(gctx->sctx_dr1);
	load_dr2(gctx->sctx_dr2);
	load_dr3(gctx->sctx_dr3);
}

static __inline void
svm_dr_leave_guest(struct svm_regctx *gctx)
{

	/* Save guest debug registers. */
	gctx->sctx_dr0 = rdr0();
	gctx->sctx_dr1 = rdr1();
	gctx->sctx_dr2 = rdr2();
	gctx->sctx_dr3 = rdr3();

	/*
	 * Restore host debug registers.  Restore DR7 and DEBUGCTL
	 * last.
	 */
	load_dr0(gctx->host_dr0);
	load_dr1(gctx->host_dr1);
	load_dr2(gctx->host_dr2);
	load_dr3(gctx->host_dr3);
	load_dr6(gctx->host_dr6);
	wrmsr(MSR_DEBUGCTLMSR, gctx->host_debugctl);
	load_dr7(gctx->host_dr7);
}

/*
 * Start vcpu with specified RIP.
 */
static int
svm_vmrun(void *arg, int vcpu, register_t rip, pmap_t pmap, 
	struct vm_eventinfo *evinfo)
{
	struct svm_regctx *gctx;
	struct svm_softc *svm_sc;
	struct svm_vcpu *vcpustate;
	struct vmcb_state *state;
	struct vmcb_ctrl *ctrl;
	struct vm_exit *vmexit;
	struct vlapic *vlapic;
	struct vm *vm;
	uint64_t vmcb_pa;
	int handled;
	uint16_t ldt_sel;

	svm_sc = arg;
	vm = svm_sc->vm;

	vcpustate = svm_get_vcpu(svm_sc, vcpu);
	state = svm_get_vmcb_state(svm_sc, vcpu);
	ctrl = svm_get_vmcb_ctrl(svm_sc, vcpu);
	vmexit = vm_exitinfo(vm, vcpu);
	vlapic = vm_lapic(vm, vcpu);

	gctx = svm_get_guest_regctx(svm_sc, vcpu);
	vmcb_pa = svm_sc->vcpu[vcpu].vmcb_pa;

	if (vcpustate->lastcpu != curcpu) {
		/*
		 * Force new ASID allocation by invalidating the generation.
		 */
		vcpustate->asid.gen = 0;

		/*
		 * Invalidate the VMCB state cache by marking all fields dirty.
		 */
		svm_set_dirty(svm_sc, vcpu, 0xffffffff);

		/*
		 * XXX
		 * Setting 'vcpustate->lastcpu' here is bit premature because
		 * we may return from this function without actually executing
		 * the VMRUN  instruction. This could happen if a rendezvous
		 * or an AST is pending on the first time through the loop.
		 *
		 * This works for now but any new side-effects of vcpu
		 * migration should take this case into account.
		 */
		vcpustate->lastcpu = curcpu;
		vmm_stat_incr(vm, vcpu, VCPU_MIGRATIONS, 1);
	}

	svm_msr_guest_enter(svm_sc, vcpu);

	/* Update Guest RIP */
	state->rip = rip;

	do {
		/*
		 * Disable global interrupts to guarantee atomicity during
		 * loading of guest state. This includes not only the state
		 * loaded by the "vmrun" instruction but also software state
		 * maintained by the hypervisor: suspended and rendezvous
		 * state, NPT generation number, vlapic interrupts etc.
		 */
		disable_gintr();

		if (vcpu_suspended(evinfo)) {
			enable_gintr();
			vm_exit_suspended(vm, vcpu, state->rip);
			break;
		}

		if (vcpu_rendezvous_pending(evinfo)) {
			enable_gintr();
			vm_exit_rendezvous(vm, vcpu, state->rip);
			break;
		}

		if (vcpu_reqidle(evinfo)) {
			enable_gintr();
			vm_exit_reqidle(vm, vcpu, state->rip);
			break;
		}

		/* We are asked to give the cpu by scheduler. */
		if (vcpu_should_yield(vm, vcpu)) {
			enable_gintr();
			vm_exit_astpending(vm, vcpu, state->rip);
			break;
		}

		if (vcpu_debugged(vm, vcpu)) {
			enable_gintr();
			vm_exit_debug(vm, vcpu, state->rip);
			break;
		}

		/*
		 * #VMEXIT resumes the host with the guest LDTR, so
		 * save the current LDT selector so it can be restored
		 * after an exit.  The userspace hypervisor probably
		 * doesn't use a LDT, but save and restore it to be
		 * safe.
		 */
		ldt_sel = sldt();

		svm_inj_interrupts(svm_sc, vcpu, vlapic);

		/* Activate the nested pmap on 'curcpu' */
		CPU_SET_ATOMIC_ACQ(curcpu, &pmap->pm_active);

		/*
		 * Check the pmap generation and the ASID generation to
		 * ensure that the vcpu does not use stale TLB mappings.
		 */
		check_asid(svm_sc, vcpu, pmap, curcpu);

		ctrl->vmcb_clean = vmcb_clean & ~vcpustate->dirty;
		vcpustate->dirty = 0;
		VCPU_CTR1(vm, vcpu, "vmcb clean %#x", ctrl->vmcb_clean);

		/* Launch Virtual Machine. */
		VCPU_CTR1(vm, vcpu, "Resume execution at %#lx", state->rip);
		svm_dr_enter_guest(gctx);
		svm_launch(vmcb_pa, gctx, &__pcpu[curcpu]);
		svm_dr_leave_guest(gctx);

		CPU_CLR_ATOMIC(curcpu, &pmap->pm_active);

		/*
		 * The host GDTR and IDTR is saved by VMRUN and restored
		 * automatically on #VMEXIT. However, the host TSS needs
		 * to be restored explicitly.
		 */
		restore_host_tss();

		/* Restore host LDTR. */
		lldt(ldt_sel);

		/* #VMEXIT disables interrupts so re-enable them here. */ 
		enable_gintr();

		/* Update 'nextrip' */
		vcpustate->nextrip = state->rip;

		/* Handle #VMEXIT and if required return to user space. */
		handled = svm_vmexit(svm_sc, vcpu, vmexit);
	} while (handled);

	svm_msr_guest_exit(svm_sc, vcpu);

	return (0);
}

static void
svm_vmcleanup(void *arg)
{
	struct svm_softc *sc = arg;

	contigfree(sc->iopm_bitmap, SVM_IO_BITMAP_SIZE, M_SVM);
	contigfree(sc->msr_bitmap, SVM_MSR_BITMAP_SIZE, M_SVM);
	free(sc, M_SVM);
}

static register_t *
swctx_regptr(struct svm_regctx *regctx, int reg)
{

	switch (reg) {
	case VM_REG_GUEST_RBX:
		return (&regctx->sctx_rbx);
	case VM_REG_GUEST_RCX:
		return (&regctx->sctx_rcx);
	case VM_REG_GUEST_RDX:
		return (&regctx->sctx_rdx);
	case VM_REG_GUEST_RDI:
		return (&regctx->sctx_rdi);
	case VM_REG_GUEST_RSI:
		return (&regctx->sctx_rsi);
	case VM_REG_GUEST_RBP:
		return (&regctx->sctx_rbp);
	case VM_REG_GUEST_R8:
		return (&regctx->sctx_r8);
	case VM_REG_GUEST_R9:
		return (&regctx->sctx_r9);
	case VM_REG_GUEST_R10:
		return (&regctx->sctx_r10);
	case VM_REG_GUEST_R11:
		return (&regctx->sctx_r11);
	case VM_REG_GUEST_R12:
		return (&regctx->sctx_r12);
	case VM_REG_GUEST_R13:
		return (&regctx->sctx_r13);
	case VM_REG_GUEST_R14:
		return (&regctx->sctx_r14);
	case VM_REG_GUEST_R15:
		return (&regctx->sctx_r15);
	case VM_REG_GUEST_DR0:
		return (&regctx->sctx_dr0);
	case VM_REG_GUEST_DR1:
		return (&regctx->sctx_dr1);
	case VM_REG_GUEST_DR2:
		return (&regctx->sctx_dr2);
	case VM_REG_GUEST_DR3:
		return (&regctx->sctx_dr3);
	default:
		return (NULL);
	}
}

static int
svm_getreg(void *arg, int vcpu, int ident, uint64_t *val)
{
	struct svm_softc *svm_sc;
	register_t *reg;

	svm_sc = arg;

	if (ident == VM_REG_GUEST_INTR_SHADOW) {
		return (svm_get_intr_shadow(svm_sc, vcpu, val));
	}

	if (vmcb_read(svm_sc, vcpu, ident, val) == 0) {
		return (0);
	}

	reg = swctx_regptr(svm_get_guest_regctx(svm_sc, vcpu), ident);

	if (reg != NULL) {
		*val = *reg;
		return (0);
	}

	VCPU_CTR1(svm_sc->vm, vcpu, "svm_getreg: unknown register %#x", ident);
	return (EINVAL);
}

static int
svm_setreg(void *arg, int vcpu, int ident, uint64_t val)
{
	struct svm_softc *svm_sc;
	register_t *reg;

	svm_sc = arg;

	if (ident == VM_REG_GUEST_INTR_SHADOW) {
		return (svm_modify_intr_shadow(svm_sc, vcpu, val));
	}

	if (vmcb_write(svm_sc, vcpu, ident, val) == 0) {
		return (0);
	}

	reg = swctx_regptr(svm_get_guest_regctx(svm_sc, vcpu), ident);

	if (reg != NULL) {
		*reg = val;
		return (0);
	}

	/*
	 * XXX deal with CR3 and invalidate TLB entries tagged with the
	 * vcpu's ASID. This needs to be treated differently depending on
	 * whether 'running' is true/false.
	 */

	VCPU_CTR1(svm_sc->vm, vcpu, "svm_setreg: unknown register %#x", ident);
	return (EINVAL);
}

static int
svm_setcap(void *arg, int vcpu, int type, int val)
{
	struct svm_softc *sc;
	int error;

	sc = arg;
	error = 0;
	switch (type) {
	case VM_CAP_HALT_EXIT:
		svm_set_intercept(sc, vcpu, VMCB_CTRL1_INTCPT,
		    VMCB_INTCPT_HLT, val);
		break;
	case VM_CAP_PAUSE_EXIT:
		svm_set_intercept(sc, vcpu, VMCB_CTRL1_INTCPT,
		    VMCB_INTCPT_PAUSE, val);
		break;
	case VM_CAP_UNRESTRICTED_GUEST:
		/* Unrestricted guest execution cannot be disabled in SVM */
		if (val == 0)
			error = EINVAL;
		break;
	default:
		error = ENOENT;
		break;
	}
	return (error);
}

static int
svm_getcap(void *arg, int vcpu, int type, int *retval)
{
	struct svm_softc *sc;
	int error;

	sc = arg;
	error = 0;

	switch (type) {
	case VM_CAP_HALT_EXIT:
		*retval = svm_get_intercept(sc, vcpu, VMCB_CTRL1_INTCPT,
		    VMCB_INTCPT_HLT);
		break;
	case VM_CAP_PAUSE_EXIT:
		*retval = svm_get_intercept(sc, vcpu, VMCB_CTRL1_INTCPT,
		    VMCB_INTCPT_PAUSE);
		break;
	case VM_CAP_UNRESTRICTED_GUEST:
		*retval = 1;	/* unrestricted guest is always enabled */
		break;
	default:
		error = ENOENT;
		break;
	}
	return (error);
}

static struct vlapic *
svm_vlapic_init(void *arg, int vcpuid)
{
	struct svm_softc *svm_sc;
	struct vlapic *vlapic;

	svm_sc = arg;
	vlapic = malloc(sizeof(struct vlapic), M_SVM_VLAPIC, M_WAITOK | M_ZERO);
	vlapic->vm = svm_sc->vm;
	vlapic->vcpuid = vcpuid;
	vlapic->apic_page = (struct LAPIC *)&svm_sc->apic_page[vcpuid];

	vlapic_init(vlapic);

	return (vlapic);
}

static void
svm_vlapic_cleanup(void *arg, struct vlapic *vlapic)
{

        vlapic_cleanup(vlapic);
        free(vlapic, M_SVM_VLAPIC);
}

struct vmm_ops vmm_ops_amd = {
	svm_init,
	svm_cleanup,
	svm_restore,
	svm_vminit,
	svm_vmrun,
	svm_vmcleanup,
	svm_getreg,
	svm_setreg,
	vmcb_getdesc,
	vmcb_setdesc,
	svm_getcap,
	svm_setcap,
	svm_npt_alloc,
	svm_npt_free,
	svm_vlapic_init,
	svm_vlapic_cleanup	
};
