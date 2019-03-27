/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014, Neel Natu (neel@freebsd.org)
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
#include <sys/errno.h>
#include <sys/systm.h>

#include <machine/cpufunc.h>
#include <machine/specialreg.h>
#include <machine/vmm.h>

#include "svm.h"
#include "vmcb.h"
#include "svm_softc.h"
#include "svm_msr.h"

#ifndef MSR_AMDK8_IPM
#define	MSR_AMDK8_IPM	0xc0010055
#endif

enum {
	IDX_MSR_LSTAR,
	IDX_MSR_CSTAR,
	IDX_MSR_STAR,
	IDX_MSR_SF_MASK,
	HOST_MSR_NUM		/* must be the last enumeration */
};

static uint64_t host_msrs[HOST_MSR_NUM];

void
svm_msr_init(void)
{
	/* 
	 * It is safe to cache the values of the following MSRs because they
	 * don't change based on curcpu, curproc or curthread.
	 */
	host_msrs[IDX_MSR_LSTAR] = rdmsr(MSR_LSTAR);
	host_msrs[IDX_MSR_CSTAR] = rdmsr(MSR_CSTAR);
	host_msrs[IDX_MSR_STAR] = rdmsr(MSR_STAR);
	host_msrs[IDX_MSR_SF_MASK] = rdmsr(MSR_SF_MASK);
}

void
svm_msr_guest_init(struct svm_softc *sc, int vcpu)
{
	/*
	 * All the MSRs accessible to the guest are either saved/restored by
	 * hardware on every #VMEXIT/VMRUN (e.g., G_PAT) or are saved/restored
	 * by VMSAVE/VMLOAD (e.g., MSR_GSBASE).
	 *
	 * There are no guest MSRs that are saved/restored "by hand" so nothing
	 * more to do here.
	 */
	return;
}

void
svm_msr_guest_enter(struct svm_softc *sc, int vcpu)
{
	/*
	 * Save host MSRs (if any) and restore guest MSRs (if any).
	 */
}

void
svm_msr_guest_exit(struct svm_softc *sc, int vcpu)
{
	/*
	 * Save guest MSRs (if any) and restore host MSRs.
	 */
	wrmsr(MSR_LSTAR, host_msrs[IDX_MSR_LSTAR]);
	wrmsr(MSR_CSTAR, host_msrs[IDX_MSR_CSTAR]);
	wrmsr(MSR_STAR, host_msrs[IDX_MSR_STAR]);
	wrmsr(MSR_SF_MASK, host_msrs[IDX_MSR_SF_MASK]);

	/* MSR_KGSBASE will be restored on the way back to userspace */
}

int
svm_rdmsr(struct svm_softc *sc, int vcpu, u_int num, uint64_t *result,
    bool *retu)
{
	int error = 0;

	switch (num) {
	case MSR_MCG_CAP:
	case MSR_MCG_STATUS:
		*result = 0;
		break;
	case MSR_MTRRcap:
	case MSR_MTRRdefType:
	case MSR_MTRR4kBase ... MSR_MTRR4kBase + 8:
	case MSR_MTRR16kBase ... MSR_MTRR16kBase + 1:
	case MSR_MTRR64kBase:
	case MSR_SYSCFG:
	case MSR_AMDK8_IPM:
	case MSR_EXTFEATURES:
		*result = 0;
		break;
	default:
		error = EINVAL;
		break;
	}

	return (error);
}

int
svm_wrmsr(struct svm_softc *sc, int vcpu, u_int num, uint64_t val, bool *retu)
{
	int error = 0;

	switch (num) {
	case MSR_MCG_CAP:
	case MSR_MCG_STATUS:
		break;		/* ignore writes */
	case MSR_MTRRcap:
		vm_inject_gp(sc->vm, vcpu);
		break;
	case MSR_MTRRdefType:
	case MSR_MTRR4kBase ... MSR_MTRR4kBase + 8:
	case MSR_MTRR16kBase ... MSR_MTRR16kBase + 1:
	case MSR_MTRR64kBase:
	case MSR_SYSCFG:
		break;		/* Ignore writes */
	case MSR_AMDK8_IPM:
		/*
		 * Ignore writes to the "Interrupt Pending Message" MSR.
		 */
		break;
	case MSR_K8_UCODE_UPDATE:
		/*
		 * Ignore writes to microcode update register.
		 */
		break;
	case MSR_EXTFEATURES:
		break;
	default:
		error = EINVAL;
		break;
	}

	return (error);
}
