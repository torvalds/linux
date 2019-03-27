/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 NetApp, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>

#include <machine/clock.h>
#include <machine/cpufunc.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/specialreg.h>
#include <machine/vmm.h>

#include "vmx.h"
#include "vmx_msr.h"

static boolean_t
vmx_ctl_allows_one_setting(uint64_t msr_val, int bitpos)
{

	if (msr_val & (1UL << (bitpos + 32)))
		return (TRUE);
	else
		return (FALSE);
}

static boolean_t
vmx_ctl_allows_zero_setting(uint64_t msr_val, int bitpos)
{

	if ((msr_val & (1UL << bitpos)) == 0)
		return (TRUE);
	else
		return (FALSE);
}

uint32_t
vmx_revision(void)
{

	return (rdmsr(MSR_VMX_BASIC) & 0xffffffff);
}

/*
 * Generate a bitmask to be used for the VMCS execution control fields.
 *
 * The caller specifies what bits should be set to one in 'ones_mask'
 * and what bits should be set to zero in 'zeros_mask'. The don't-care
 * bits are set to the default value. The default values are obtained
 * based on "Algorithm 3" in Section 27.5.1 "Algorithms for Determining
 * VMX Capabilities".
 *
 * Returns zero on success and non-zero on error.
 */
int
vmx_set_ctlreg(int ctl_reg, int true_ctl_reg, uint32_t ones_mask,
	       uint32_t zeros_mask, uint32_t *retval)
{
	int i;
	uint64_t val, trueval;
	boolean_t true_ctls_avail, one_allowed, zero_allowed;

	/* We cannot ask the same bit to be set to both '1' and '0' */
	if ((ones_mask ^ zeros_mask) != (ones_mask | zeros_mask))
		return (EINVAL);

	if (rdmsr(MSR_VMX_BASIC) & (1UL << 55))
		true_ctls_avail = TRUE;
	else
		true_ctls_avail = FALSE;

	val = rdmsr(ctl_reg);
	if (true_ctls_avail)
		trueval = rdmsr(true_ctl_reg);		/* step c */
	else
		trueval = val;				/* step a */

	for (i = 0; i < 32; i++) {
		one_allowed = vmx_ctl_allows_one_setting(trueval, i);
		zero_allowed = vmx_ctl_allows_zero_setting(trueval, i);

		KASSERT(one_allowed || zero_allowed,
			("invalid zero/one setting for bit %d of ctl 0x%0x, "
			 "truectl 0x%0x\n", i, ctl_reg, true_ctl_reg));

		if (zero_allowed && !one_allowed) {		/* b(i),c(i) */
			if (ones_mask & (1 << i))
				return (EINVAL);
			*retval &= ~(1 << i);
		} else if (one_allowed && !zero_allowed) {	/* b(i),c(i) */
			if (zeros_mask & (1 << i))
				return (EINVAL);
			*retval |= 1 << i;
		} else {
			if (zeros_mask & (1 << i))	/* b(ii),c(ii) */
				*retval &= ~(1 << i);
			else if (ones_mask & (1 << i)) /* b(ii), c(ii) */
				*retval |= 1 << i;
			else if (!true_ctls_avail)
				*retval &= ~(1 << i);	/* b(iii) */
			else if (vmx_ctl_allows_zero_setting(val, i))/* c(iii)*/
				*retval &= ~(1 << i);
			else if (vmx_ctl_allows_one_setting(val, i)) /* c(iv) */
				*retval |= 1 << i;
			else {
				panic("vmx_set_ctlreg: unable to determine "
				      "correct value of ctl bit %d for msr "
				      "0x%0x and true msr 0x%0x", i, ctl_reg,
				      true_ctl_reg);
			}
		}
	}

	return (0);
}

void
msr_bitmap_initialize(char *bitmap)
{

	memset(bitmap, 0xff, PAGE_SIZE);
}

int
msr_bitmap_change_access(char *bitmap, u_int msr, int access)
{
	int byte, bit;

	if (msr <= 0x00001FFF)
		byte = msr / 8;
	else if (msr >= 0xC0000000 && msr <= 0xC0001FFF)
		byte = 1024 + (msr - 0xC0000000) / 8;
	else
		return (EINVAL);

	bit = msr & 0x7;

	if (access & MSR_BITMAP_ACCESS_READ)
		bitmap[byte] &= ~(1 << bit);
	else
		bitmap[byte] |= 1 << bit;

	byte += 2048;
	if (access & MSR_BITMAP_ACCESS_WRITE)
		bitmap[byte] &= ~(1 << bit);
	else
		bitmap[byte] |= 1 << bit;

	return (0);
}

static uint64_t misc_enable;
static uint64_t platform_info;
static uint64_t turbo_ratio_limit;
static uint64_t host_msrs[GUEST_MSR_NUM];

static bool
nehalem_cpu(void)
{
	u_int family, model;

	/*
	 * The family:model numbers belonging to the Nehalem microarchitecture
	 * are documented in Section 35.5, Intel SDM dated Feb 2014.
	 */
	family = CPUID_TO_FAMILY(cpu_id);
	model = CPUID_TO_MODEL(cpu_id);
	if (family == 0x6) {
		switch (model) {
		case 0x1A:
		case 0x1E:
		case 0x1F:
		case 0x2E:
			return (true);
		default:
			break;
		}
	}
	return (false);
}

static bool
westmere_cpu(void)
{
	u_int family, model;

	/*
	 * The family:model numbers belonging to the Westmere microarchitecture
	 * are documented in Section 35.6, Intel SDM dated Feb 2014.
	 */
	family = CPUID_TO_FAMILY(cpu_id);
	model = CPUID_TO_MODEL(cpu_id);
	if (family == 0x6) {
		switch (model) {
		case 0x25:
		case 0x2C:
			return (true);
		default:
			break;
		}
	}
	return (false);
}

static bool
pat_valid(uint64_t val)
{
	int i, pa;

	/*
	 * From Intel SDM: Table "Memory Types That Can Be Encoded With PAT"
	 *
	 * Extract PA0 through PA7 and validate that each one encodes a
	 * valid memory type.
	 */
	for (i = 0; i < 8; i++) {
		pa = (val >> (i * 8)) & 0xff;
		if (pa == 2 || pa == 3 || pa >= 8)
			return (false);
	}
	return (true);
}

void
vmx_msr_init(void)
{
	uint64_t bus_freq, ratio;
	int i;

	/*
	 * It is safe to cache the values of the following MSRs because
	 * they don't change based on curcpu, curproc or curthread.
	 */
	host_msrs[IDX_MSR_LSTAR] = rdmsr(MSR_LSTAR);
	host_msrs[IDX_MSR_CSTAR] = rdmsr(MSR_CSTAR);
	host_msrs[IDX_MSR_STAR] = rdmsr(MSR_STAR);
	host_msrs[IDX_MSR_SF_MASK] = rdmsr(MSR_SF_MASK);

	/*
	 * Initialize emulated MSRs
	 */
	misc_enable = rdmsr(MSR_IA32_MISC_ENABLE);
	/*
	 * Set mandatory bits
	 *  11:   branch trace disabled
	 *  12:   PEBS unavailable
	 * Clear unsupported features
	 *  16:   SpeedStep enable
	 *  18:   enable MONITOR FSM
	 */
	misc_enable |= (1 << 12) | (1 << 11);
	misc_enable &= ~((1 << 18) | (1 << 16));

	if (nehalem_cpu() || westmere_cpu())
		bus_freq = 133330000;		/* 133Mhz */
	else
		bus_freq = 100000000;		/* 100Mhz */

	/*
	 * XXXtime
	 * The ratio should really be based on the virtual TSC frequency as
	 * opposed to the host TSC.
	 */
	ratio = (tsc_freq / bus_freq) & 0xff;

	/*
	 * The register definition is based on the micro-architecture
	 * but the following bits are always the same:
	 * [15:8]  Maximum Non-Turbo Ratio
	 * [28]    Programmable Ratio Limit for Turbo Mode
	 * [29]    Programmable TDC-TDP Limit for Turbo Mode
	 * [47:40] Maximum Efficiency Ratio
	 *
	 * The other bits can be safely set to 0 on all
	 * micro-architectures up to Haswell.
	 */
	platform_info = (ratio << 8) | (ratio << 40);

	/*
	 * The number of valid bits in the MSR_TURBO_RATIO_LIMITx register is
	 * dependent on the maximum cores per package supported by the micro-
	 * architecture. For e.g., Westmere supports 6 cores per package and
	 * uses the low 48 bits. Sandybridge support 8 cores per package and
	 * uses up all 64 bits.
	 *
	 * However, the unused bits are reserved so we pretend that all bits
	 * in this MSR are valid.
	 */
	for (i = 0; i < 8; i++)
		turbo_ratio_limit = (turbo_ratio_limit << 8) | ratio;
}

void
vmx_msr_guest_init(struct vmx *vmx, int vcpuid)
{
	uint64_t *guest_msrs;

	guest_msrs = vmx->guest_msrs[vcpuid];

	/*
	 * The permissions bitmap is shared between all vcpus so initialize it
	 * once when initializing the vBSP.
	 */
	if (vcpuid == 0) {
		guest_msr_rw(vmx, MSR_LSTAR);
		guest_msr_rw(vmx, MSR_CSTAR);
		guest_msr_rw(vmx, MSR_STAR);
		guest_msr_rw(vmx, MSR_SF_MASK);
		guest_msr_rw(vmx, MSR_KGSBASE);
	}

	/*
	 * Initialize guest IA32_PAT MSR with default value after reset.
	 */
	guest_msrs[IDX_MSR_PAT] = PAT_VALUE(0, PAT_WRITE_BACK) |
	    PAT_VALUE(1, PAT_WRITE_THROUGH)	|
	    PAT_VALUE(2, PAT_UNCACHED)		|
	    PAT_VALUE(3, PAT_UNCACHEABLE)	|
	    PAT_VALUE(4, PAT_WRITE_BACK)	|
	    PAT_VALUE(5, PAT_WRITE_THROUGH)	|
	    PAT_VALUE(6, PAT_UNCACHED)		|
	    PAT_VALUE(7, PAT_UNCACHEABLE);

	return;
}

void
vmx_msr_guest_enter(struct vmx *vmx, int vcpuid)
{
	uint64_t *guest_msrs = vmx->guest_msrs[vcpuid];

	/* Save host MSRs (in particular, KGSBASE) and restore guest MSRs */
	update_pcb_bases(curpcb);
	wrmsr(MSR_LSTAR, guest_msrs[IDX_MSR_LSTAR]);
	wrmsr(MSR_CSTAR, guest_msrs[IDX_MSR_CSTAR]);
	wrmsr(MSR_STAR, guest_msrs[IDX_MSR_STAR]);
	wrmsr(MSR_SF_MASK, guest_msrs[IDX_MSR_SF_MASK]);
	wrmsr(MSR_KGSBASE, guest_msrs[IDX_MSR_KGSBASE]);
}

void
vmx_msr_guest_exit(struct vmx *vmx, int vcpuid)
{
	uint64_t *guest_msrs = vmx->guest_msrs[vcpuid];

	/* Save guest MSRs */
	guest_msrs[IDX_MSR_LSTAR] = rdmsr(MSR_LSTAR);
	guest_msrs[IDX_MSR_CSTAR] = rdmsr(MSR_CSTAR);
	guest_msrs[IDX_MSR_STAR] = rdmsr(MSR_STAR);
	guest_msrs[IDX_MSR_SF_MASK] = rdmsr(MSR_SF_MASK);
	guest_msrs[IDX_MSR_KGSBASE] = rdmsr(MSR_KGSBASE);

	/* Restore host MSRs */
	wrmsr(MSR_LSTAR, host_msrs[IDX_MSR_LSTAR]);
	wrmsr(MSR_CSTAR, host_msrs[IDX_MSR_CSTAR]);
	wrmsr(MSR_STAR, host_msrs[IDX_MSR_STAR]);
	wrmsr(MSR_SF_MASK, host_msrs[IDX_MSR_SF_MASK]);

	/* MSR_KGSBASE will be restored on the way back to userspace */
}

int
vmx_rdmsr(struct vmx *vmx, int vcpuid, u_int num, uint64_t *val, bool *retu)
{
	const uint64_t *guest_msrs;
	int error;

	guest_msrs = vmx->guest_msrs[vcpuid];
	error = 0;

	switch (num) {
	case MSR_MCG_CAP:
	case MSR_MCG_STATUS:
		*val = 0;
		break;
	case MSR_MTRRcap:
	case MSR_MTRRdefType:
	case MSR_MTRR4kBase ... MSR_MTRR4kBase + 8:
	case MSR_MTRR16kBase ... MSR_MTRR16kBase + 1:
	case MSR_MTRR64kBase:
		*val = 0;
		break;
	case MSR_IA32_MISC_ENABLE:
		*val = misc_enable;
		break;
	case MSR_PLATFORM_INFO:
		*val = platform_info;
		break;
	case MSR_TURBO_RATIO_LIMIT:
	case MSR_TURBO_RATIO_LIMIT1:
		*val = turbo_ratio_limit;
		break;
	case MSR_PAT:
		*val = guest_msrs[IDX_MSR_PAT];
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
}

int
vmx_wrmsr(struct vmx *vmx, int vcpuid, u_int num, uint64_t val, bool *retu)
{
	uint64_t *guest_msrs;
	uint64_t changed;
	int error;
	
	guest_msrs = vmx->guest_msrs[vcpuid];
	error = 0;

	switch (num) {
	case MSR_MCG_CAP:
	case MSR_MCG_STATUS:
		break;		/* ignore writes */
	case MSR_MTRRcap:
		vm_inject_gp(vmx->vm, vcpuid);
		break;
	case MSR_MTRRdefType:
	case MSR_MTRR4kBase ... MSR_MTRR4kBase + 8:
	case MSR_MTRR16kBase ... MSR_MTRR16kBase + 1:
	case MSR_MTRR64kBase:
		break;		/* Ignore writes */
	case MSR_IA32_MISC_ENABLE:
		changed = val ^ misc_enable;
		/*
		 * If the host has disabled the NX feature then the guest
		 * also cannot use it. However, a Linux guest will try to
		 * enable the NX feature by writing to the MISC_ENABLE MSR.
		 *
		 * This can be safely ignored because the memory management
		 * code looks at CPUID.80000001H:EDX.NX to check if the
		 * functionality is actually enabled.
		 */
		changed &= ~(1UL << 34);

		/*
		 * Punt to userspace if any other bits are being modified.
		 */
		if (changed)
			error = EINVAL;

		break;
	case MSR_PAT:
		if (pat_valid(val))
			guest_msrs[IDX_MSR_PAT] = val;
		else
			vm_inject_gp(vmx->vm, vcpuid);
		break;
	case MSR_TSC:
		error = vmx_set_tsc_offset(vmx, vcpuid, val - rdtsc());
		break;
	default:
		error = EINVAL;
		break;
	}

	return (error);
}
