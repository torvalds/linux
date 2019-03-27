/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) KATO Takenori, 1997, 1998.
 * 
 * All rights reserved.  Unpublished rights reserved under the copyright
 * laws of Japan.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
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

#include "opt_cpu.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/pcpu.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>

#include <vm/vm.h>
#include <vm/pmap.h>

static int	hw_instruction_sse;
SYSCTL_INT(_hw, OID_AUTO, instruction_sse, CTLFLAG_RD,
    &hw_instruction_sse, 0, "SIMD/MMX2 instructions available in CPU");
static int	lower_sharedpage_init;
int		hw_lower_amd64_sharedpage;
SYSCTL_INT(_hw, OID_AUTO, lower_amd64_sharedpage, CTLFLAG_RDTUN,
    &hw_lower_amd64_sharedpage, 0,
   "Lower sharedpage to work around Ryzen issue with executing code near the top of user memory");
/*
 * -1: automatic (default)
 *  0: keep enable CLFLUSH
 *  1: force disable CLFLUSH
 */
static int	hw_clflush_disable = -1;

static void
init_amd(void)
{
	uint64_t msr;

	/*
	 * Work around Erratum 721 for Family 10h and 12h processors.
	 * These processors may incorrectly update the stack pointer
	 * after a long series of push and/or near-call instructions,
	 * or a long series of pop and/or near-return instructions.
	 *
	 * http://support.amd.com/us/Processor_TechDocs/41322_10h_Rev_Gd.pdf
	 * http://support.amd.com/us/Processor_TechDocs/44739_12h_Rev_Gd.pdf
	 *
	 * Hypervisors do not provide access to the errata MSR,
	 * causing #GP exception on attempt to apply the errata.  The
	 * MSR write shall be done on host and persist globally
	 * anyway, so do not try to do it when under virtualization.
	 */
	switch (CPUID_TO_FAMILY(cpu_id)) {
	case 0x10:
	case 0x12:
		if ((cpu_feature2 & CPUID2_HV) == 0)
			wrmsr(0xc0011029, rdmsr(0xc0011029) | 1);
		break;
	}

	/*
	 * BIOS may fail to set InitApicIdCpuIdLo to 1 as it should per BKDG.
	 * So, do it here or otherwise some tools could be confused by
	 * Initial Local APIC ID reported with CPUID Function 1 in EBX.
	 */
	if (CPUID_TO_FAMILY(cpu_id) == 0x10) {
		if ((cpu_feature2 & CPUID2_HV) == 0) {
			msr = rdmsr(MSR_NB_CFG1);
			msr |= (uint64_t)1 << 54;
			wrmsr(MSR_NB_CFG1, msr);
		}
	}

	/*
	 * BIOS may configure Family 10h processors to convert WC+ cache type
	 * to CD.  That can hurt performance of guest VMs using nested paging.
	 * The relevant MSR bit is not documented in the BKDG,
	 * the fix is borrowed from Linux.
	 */
	if (CPUID_TO_FAMILY(cpu_id) == 0x10) {
		if ((cpu_feature2 & CPUID2_HV) == 0) {
			msr = rdmsr(0xc001102a);
			msr &= ~((uint64_t)1 << 24);
			wrmsr(0xc001102a, msr);
		}
	}

	/*
	 * Work around Erratum 793: Specific Combination of Writes to Write
	 * Combined Memory Types and Locked Instructions May Cause Core Hang.
	 * See Revision Guide for AMD Family 16h Models 00h-0Fh Processors,
	 * revision 3.04 or later, publication 51810.
	 */
	if (CPUID_TO_FAMILY(cpu_id) == 0x16 && CPUID_TO_MODEL(cpu_id) <= 0xf) {
		if ((cpu_feature2 & CPUID2_HV) == 0) {
			msr = rdmsr(0xc0011020);
			msr |= (uint64_t)1 << 15;
			wrmsr(0xc0011020, msr);
		}
	}

	/* Ryzen erratas. */
	if (CPUID_TO_FAMILY(cpu_id) == 0x17 && CPUID_TO_MODEL(cpu_id) == 0x1 &&
	    (cpu_feature2 & CPUID2_HV) == 0) {
		/* 1021 */
		msr = rdmsr(0xc0011029);
		msr |= 0x2000;
		wrmsr(0xc0011029, msr);

		/* 1033 */
		msr = rdmsr(0xc0011020);
		msr |= 0x10;
		wrmsr(0xc0011020, msr);

		/* 1049 */
		msr = rdmsr(0xc0011028);
		msr |= 0x10;
		wrmsr(0xc0011028, msr);

		/* 1095 */
		msr = rdmsr(0xc0011020);
		msr |= 0x200000000000000;
		wrmsr(0xc0011020, msr);
	}

	/*
	 * Work around a problem on Ryzen that is triggered by executing
	 * code near the top of user memory, in our case the signal
	 * trampoline code in the shared page on amd64.
	 *
	 * This function is executed once for the BSP before tunables take
	 * effect so the value determined here can be overridden by the
	 * tunable.  This function is then executed again for each AP and
	 * also on resume.  Set a flag the first time so that value set by
	 * the tunable is not overwritten.
	 *
	 * The stepping and/or microcode versions should be checked after
	 * this issue is fixed by AMD so that we don't use this mode if not
	 * needed.
	 */
	if (lower_sharedpage_init == 0) {
		lower_sharedpage_init = 1;
		if (CPUID_TO_FAMILY(cpu_id) == 0x17) {
			hw_lower_amd64_sharedpage = 1;
		}
	}
}

/*
 * Initialize special VIA features
 */
static void
init_via(void)
{
	u_int regs[4], val;

	/*
	 * Check extended CPUID for PadLock features.
	 *
	 * http://www.via.com.tw/en/downloads/whitepapers/initiatives/padlock/programming_guide.pdf
	 */
	do_cpuid(0xc0000000, regs);
	if (regs[0] >= 0xc0000001) {
		do_cpuid(0xc0000001, regs);
		val = regs[3];
	} else
		return;

	/* Enable RNG if present. */
	if ((val & VIA_CPUID_HAS_RNG) != 0) {
		via_feature_rng = VIA_HAS_RNG;
		wrmsr(0x110B, rdmsr(0x110B) | VIA_CPUID_DO_RNG);
	}

	/* Enable PadLock if present. */
	if ((val & VIA_CPUID_HAS_ACE) != 0)
		via_feature_xcrypt |= VIA_HAS_AES;
	if ((val & VIA_CPUID_HAS_ACE2) != 0)
		via_feature_xcrypt |= VIA_HAS_AESCTR;
	if ((val & VIA_CPUID_HAS_PHE) != 0)
		via_feature_xcrypt |= VIA_HAS_SHA;
	if ((val & VIA_CPUID_HAS_PMM) != 0)
		via_feature_xcrypt |= VIA_HAS_MM;
	if (via_feature_xcrypt != 0)
		wrmsr(0x1107, rdmsr(0x1107) | (1 << 28));
}

/*
 * Initialize CPU control registers
 */
void
initializecpu(void)
{
	uint64_t msr;
	uint32_t cr4;

	cr4 = rcr4();
	if ((cpu_feature & CPUID_XMM) && (cpu_feature & CPUID_FXSR)) {
		cr4 |= CR4_FXSR | CR4_XMM;
		cpu_fxsr = hw_instruction_sse = 1;
	}
	if (cpu_stdext_feature & CPUID_STDEXT_FSGSBASE)
		cr4 |= CR4_FSGSBASE;

	if (cpu_stdext_feature2 & CPUID_STDEXT2_PKU)
		cr4 |= CR4_PKE;

	/*
	 * Postpone enabling the SMEP on the boot CPU until the page
	 * tables are switched from the boot loader identity mapping
	 * to the kernel tables.  The boot loader enables the U bit in
	 * its tables.
	 */
	if (!IS_BSP()) {
		if (cpu_stdext_feature & CPUID_STDEXT_SMEP)
			cr4 |= CR4_SMEP;
		if (cpu_stdext_feature & CPUID_STDEXT_SMAP)
			cr4 |= CR4_SMAP;
	}
	load_cr4(cr4);
	if (IS_BSP() && (amd_feature & AMDID_NX) != 0) {
		msr = rdmsr(MSR_EFER) | EFER_NXE;
		wrmsr(MSR_EFER, msr);
		pg_nx = PG_NX;
	}
	hw_ibrs_recalculate();
	hw_ssb_recalculate(false);
	amd64_syscall_ret_flush_l1d_recalc();
	switch (cpu_vendor_id) {
	case CPU_VENDOR_AMD:
		init_amd();
		break;
	case CPU_VENDOR_CENTAUR:
		init_via();
		break;
	}

	if ((amd_feature & AMDID_RDTSCP) != 0 ||
	    (cpu_stdext_feature2 & CPUID_STDEXT2_RDPID) != 0)
		wrmsr(MSR_TSC_AUX, PCPU_GET(cpuid));
}

void
initializecpucache(void)
{

	/*
	 * CPUID with %eax = 1, %ebx returns
	 * Bits 15-8: CLFLUSH line size
	 * 	(Value * 8 = cache line size in bytes)
	 */
	if ((cpu_feature & CPUID_CLFSH) != 0)
		cpu_clflush_line_size = ((cpu_procinfo >> 8) & 0xff) * 8;
	/*
	 * XXXKIB: (temporary) hack to work around traps generated
	 * when CLFLUSHing APIC register window under virtualization
	 * environments.  These environments tend to disable the
	 * CPUID_SS feature even though the native CPU supports it.
	 */
	TUNABLE_INT_FETCH("hw.clflush_disable", &hw_clflush_disable);
	if (vm_guest != VM_GUEST_NO && hw_clflush_disable == -1) {
		cpu_feature &= ~CPUID_CLFSH;
		cpu_stdext_feature &= ~CPUID_STDEXT_CLFLUSHOPT;
	}

	/*
	 * The kernel's use of CLFLUSH{,OPT} can be disabled manually
	 * by setting the hw.clflush_disable tunable.
	 */
	if (hw_clflush_disable == 1) {
		cpu_feature &= ~CPUID_CLFSH;
		cpu_stdext_feature &= ~CPUID_STDEXT_CLFLUSHOPT;
	}
}
