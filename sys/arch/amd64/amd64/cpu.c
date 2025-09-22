/*	$OpenBSD: cpu.c,v 1.199 2025/09/19 07:00:32 sf Exp $	*/
/* $NetBSD: cpu.c,v 1.1 2003/04/26 18:39:26 fvdl Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by RedBack Networks Inc.
 *
 * Author: Bill Sommerfeld
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1999 Stefan Grefen
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the NetBSD
 *      Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR AND CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "lapic.h"
#include "ioapic.h"
#include "vmm.h"
#include "pctr.h"
#include "pvbus.h"

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/timeout.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/memrange.h>
#include <sys/atomic.h>
#include <sys/user.h>

#include <uvm/uvm_extern.h>

#include <machine/codepatch.h>
#include <machine/cpu_full.h>
#include <machine/cpufunc.h>
#include <machine/cpuvar.h>
#include <machine/pmap.h>
#include <machine/vmparam.h>
#include <machine/mpbiosvar.h>
#include <machine/pcb.h>
#include <machine/specialreg.h>
#include <machine/segments.h>
#include <machine/gdt.h>
#include <machine/pio.h>
#include <machine/vmmvar.h>

#if NLAPIC > 0
#include <machine/i82489reg.h>
#include <machine/i82489var.h>
#endif

#if NIOAPIC > 0
#include <machine/i82093var.h>
#endif

#if NPCTR > 0
#include <machine/pctr.h>
#endif

#if NPVBUS > 0
#include <dev/pv/pvvar.h>
#endif

#include <dev/ic/mc146818reg.h>
#include <amd64/isa/nvram.h>
#include <dev/isa/isareg.h>

#ifdef HIBERNATE
#include <sys/hibernate.h>
#include <machine/hibernate.h>
#endif /* HIBERNATE */

/* #define CPU_DEBUG */

#ifdef CPU_DEBUG
#define DPRINTF(x...)	do { printf(x); } while(0)
#else
#define DPRINTF(x...)
#endif /* CPU_DEBUG */

int     cpu_match(struct device *, void *, void *);
void    cpu_attach(struct device *, struct device *, void *);
int     cpu_activate(struct device *, int);
void	patinit(struct cpu_info *ci);
#if NVMM > 0
void	cpu_init_vmm(struct cpu_info *ci);
#endif /* NVMM > 0 */

struct cpu_softc {
	struct device sc_dev;		/* device tree glue */
	struct cpu_info *sc_info;	/* pointer to CPU info */
};

void	replacesmap(void);
void	replacemeltdown(void);
void	replacemds(void);

extern long _stac;
extern long _clac;

int cpuid_level = 0;		/* MIN cpuid(0).eax */
char cpu_vendor[16] = { 0 };	/* CPU0's cpuid(0).e[bdc]x, \0 */
int cpu_id = 0;			/* cpuid(1).eax */
int cpu_ebxfeature = 0;		/* cpuid(1).ebx */
int cpu_ecxfeature = 0;		/* INTERSECTION(cpuid(1).ecx) */
int cpu_feature = 0;		/* cpuid(1).edx */
int ecpu_ecxfeature = 0;	/* cpuid(0x80000001).ecx */
int cpu_sev_guestmode = 0;
int cpu_meltdown = 0;
int cpu_use_xsaves = 0;
int need_retpoline = 1;		/* most systems need retpoline */

void
replacesmap(void)
{
	static int replacedone = 0;
	int s;

	if (replacedone)
		return;
	replacedone = 1;

	s = splhigh();

	codepatch_replace(CPTAG_STAC, &_stac, 3);
	codepatch_replace(CPTAG_CLAC, &_clac, 3);

	splx(s);
}

void
replacemeltdown(void)
{
	static int replacedone = 0;
	struct cpu_info *ci = &cpu_info_primary;
	int swapgs_vuln = 0, ibrs = 0, s, ibpb = 0;

	if (ci->ci_vendor == CPUV_INTEL) {
		int family = ci->ci_family;
		int model = ci->ci_model;

		swapgs_vuln = 1;
		if (family == 0x6 &&
		    (model == 0x37 || model == 0x4a || model == 0x4c ||
		     model == 0x4d || model == 0x5a || model == 0x5d ||
		     model == 0x6e || model == 0x65 || model == 0x75)) {
			/* Silvermont, Airmont */
			swapgs_vuln = 0;
		} else if (family == 0x6 && (model == 0x85 || model == 0x57)) {
			/* KnightsLanding */
			swapgs_vuln = 0;
		}
		if ((ci->ci_feature_sefflags_edx & SEFF0EDX_ARCH_CAP) &&
		    (rdmsr(MSR_ARCH_CAPABILITIES) & ARCH_CAP_IBRS_ALL)) {
			ibrs = 2;
		} else if (ci->ci_feature_sefflags_edx & SEFF0EDX_IBRS) {
			ibrs = 1;
		}
		if (ci->ci_feature_sefflags_edx & SEFF0EDX_IBRS)
			ibpb = 1;
        } else if (ci->ci_vendor == CPUV_AMD &&
            ci->ci_pnfeatset >= 0x80000008) {
		if (ci->ci_feature_amdspec_ebx & CPUIDEBX_IBRS_ALWAYSON) {
			ibrs = 2;
		} else if ((ci->ci_feature_amdspec_ebx & CPUIDEBX_IBRS) &&
		    (ci->ci_feature_amdspec_ebx & CPUIDEBX_IBRS_PREF)) {
			ibrs = 1;
		}
		if (ci->ci_feature_amdspec_ebx & CPUIDEBX_IBPB)
			ibpb = 1;
	}

	/* Enhanced IBRS: turn it on once on each CPU and don't touch again */
	if (ibrs == 2)
		wrmsr(MSR_SPEC_CTRL, SPEC_CTRL_IBRS);

	if (replacedone)
		return;
	replacedone = 1;

	s = splhigh();

	/* If we don't have IBRS/IBPB, then don't use IBPB */
	if (ibpb == 0)
		codepatch_nop(CPTAG_IBPB_NOP);

	if (ibrs == 2 || (ci->ci_feature_sefflags_edx & SEFF0EDX_IBT)) {
		extern const char _jmprax, _jmpr11, _jmpr13;
		extern const short _jmprax_len, _jmpr11_len, _jmpr13_len;

		codepatch_replace(CPTAG_RETPOLINE_RAX, &_jmprax, _jmprax_len);
		codepatch_replace(CPTAG_RETPOLINE_R11, &_jmpr11, _jmpr11_len);
		codepatch_replace(CPTAG_RETPOLINE_R13, &_jmpr13, _jmpr13_len);
		need_retpoline = 0;
	}

	if (!cpu_meltdown)
		codepatch_nop(CPTAG_MELTDOWN_NOP);
	else {
		extern long alltraps_kern_meltdown;

		/* eliminate conditional branch in alltraps */
		codepatch_jmp(CPTAG_MELTDOWN_ALLTRAPS, &alltraps_kern_meltdown);

		/* enable reuse of PCID for U-K page tables */
		if (pmap_use_pcid) {
			extern long _pcid_set_reuse;
			DPRINTF("%s: codepatching PCID use\n", __func__);
			codepatch_replace(CPTAG_PCID_SET_REUSE,
			    &_pcid_set_reuse, PCID_SET_REUSE_SIZE);
		}
	}

	/*
	 * CVE-2019-1125: if the CPU has SMAP and it's not vulnerable to
	 * Meltdown, then it's protected both from speculatively mis-skipping
	 * the swapgs during interrupts of userspace and from speculatively
	 * mis-taking a swapgs during interrupts while already in the kernel
	 * as the speculative path will fault from SMAP.  Warning: enabling
	 * WRGSBASE would break this 'protection'.
	 *
	 * Otherwise, if the CPU's swapgs can't be speculated over and it
	 * _is_ vulnerable to Meltdown then the %cr3 change will serialize
	 * user->kern transitions, but we still need to mitigate the
	 * already-in-kernel cases.
	 */
	if (!cpu_meltdown && (ci->ci_feature_sefflags_ebx & SEFF0EBX_SMAP)) {
		codepatch_nop(CPTAG_FENCE_SWAPGS_MIS_TAKEN);
		codepatch_nop(CPTAG_FENCE_NO_SAFE_SMAP);
	} else if (!swapgs_vuln && cpu_meltdown) {
		codepatch_nop(CPTAG_FENCE_SWAPGS_MIS_TAKEN);
	}
	splx(s);
}

void
replacemds(void)
{
	static int replacedone = 0;
	extern long mds_handler_bdw, mds_handler_ivb, mds_handler_skl;
	extern long mds_handler_skl_sse, mds_handler_skl_avx;
	extern long mds_handler_skl_avx512;
	extern long mds_handler_silvermont, mds_handler_knights;
	struct cpu_info *ci = &cpu_info_primary;
	CPU_INFO_ITERATOR cii;
	void *handler = NULL, *vmm_handler = NULL;
	const char *type;
	int use_verw = 0, s;
	uint32_t cap = 0;

	/* ci_mds_tmp must be 64-byte aligned for AVX-512 instructions */
	CTASSERT((offsetof(struct cpu_info, ci_mds_tmp) -
		  offsetof(struct cpu_info, ci_PAGEALIGN)) % 64 == 0);

	if (replacedone)
		return;
	replacedone = 1;

	if (ci->ci_vendor != CPUV_INTEL)
		goto notintel;	/* VERW only needed on Intel */

	if ((ci->ci_feature_sefflags_edx & SEFF0EDX_ARCH_CAP))
		cap = rdmsr(MSR_ARCH_CAPABILITIES);

	if (cap & ARCH_CAP_MDS_NO) {
		/* Unaffected, nop out the handling code */
	} else if (ci->ci_feature_sefflags_edx & SEFF0EDX_MD_CLEAR) {
		/* new firmware, use VERW */
		use_verw = 1;
	} else {
		int family = ci->ci_family;
		int model = ci->ci_model;
		int stepping = CPUID2STEPPING(ci->ci_signature);

		if (family == 0x6 &&
		    (model == 0x2e || model == 0x1e || model == 0x1f ||
		     model == 0x1a || model == 0x2f || model == 0x25 ||
		     model == 0x2c || model == 0x2d || model == 0x2a ||
		     model == 0x3e || model == 0x3a)) {
			/* Nehalem, SandyBridge, IvyBridge */
			handler = vmm_handler = &mds_handler_ivb;
			type = "IvyBridge";
			CPU_INFO_FOREACH(cii, ci) {
				ci->ci_mds_buf = malloc(672, M_DEVBUF,
				    M_WAITOK);
				memset(ci->ci_mds_buf, 0, 16);
			}
		} else if (family == 0x6 &&
		    (model == 0x3f || model == 0x3c || model == 0x45 ||
		     model == 0x46 || model == 0x56 || model == 0x4f ||
		     model == 0x47 || model == 0x3d)) {
			/* Haswell and Broadwell */
			handler = vmm_handler = &mds_handler_bdw;
			type = "Broadwell";
			CPU_INFO_FOREACH(cii, ci) {
				ci->ci_mds_buf = malloc(1536, M_DEVBUF,
				    M_WAITOK);
			}
		} else if (family == 0x6 &&
		    ((model == 0x55 && stepping <= 5) || model == 0x4e ||
		    model == 0x5e || (model == 0x8e && stepping <= 0xb) ||
		    (model == 0x9e && stepping <= 0xc))) {
			/*
			 * Skylake, KabyLake, CoffeeLake, WhiskeyLake,
			 * CascadeLake
			 */
			if (xgetbv(0) & XFEATURE_AVX512) {
				handler = &mds_handler_skl_avx512;
				type = "Skylake AVX-512";
			} else if (xgetbv(0) & XFEATURE_AVX) {
				handler = &mds_handler_skl_avx;
				type = "Skylake AVX";
			} else {
				handler = &mds_handler_skl_sse;
				type = "Skylake SSE";
			}
			vmm_handler = &mds_handler_skl;
			CPU_INFO_FOREACH(cii, ci) {
				vaddr_t b64;
				b64 = (vaddr_t)malloc(6 * 1024 + 64 + 63,
				    M_DEVBUF, M_WAITOK);
				ci->ci_mds_buf = (void *)((b64 + 63) & ~63);
				memset(ci->ci_mds_buf, 0, 64);
			}
		} else if (family == 0x6 &&
		    (model == 0x37 || model == 0x4a || model == 0x4c ||
		     model == 0x4d || model == 0x5a || model == 0x5d ||
		     model == 0x6e || model == 0x65 || model == 0x75)) {
			/* Silvermont, Airmont */
			handler = vmm_handler = &mds_handler_silvermont;
			type = "Silvermont";
			CPU_INFO_FOREACH(cii, ci) {
				ci->ci_mds_buf = malloc(256, M_DEVBUF,
				    M_WAITOK);
				memset(ci->ci_mds_buf, 0, 16);
			}
		} else if (family == 0x6 && (model == 0x85 || model == 0x57)) {
			handler = vmm_handler = &mds_handler_knights;
			type = "KnightsLanding";
			CPU_INFO_FOREACH(cii, ci) {
				vaddr_t b64;
				b64 = (vaddr_t)malloc(1152 + 63, M_DEVBUF,
				    M_WAITOK);
				ci->ci_mds_buf = (void *)((b64 + 63) & ~63);
			}
		}
	}

	/* Register File Data Sampling (RFDS) also has a VERW workaround */
	if ((cap & ARCH_CAP_RFDS_NO) == 0 && (cap & ARCH_CAP_RFDS_CLEAR))
		use_verw = 1;

	if (handler != NULL) {
		printf("cpu0: using %s MDS workaround%s\n", type, "");
		s = splhigh();
		codepatch_call(CPTAG_MDS, handler);
		codepatch_call(CPTAG_MDS_VMM, vmm_handler);
		splx(s);
	} else if (use_verw) {
		/*
		 * The new firmware enhances L1D_FLUSH MSR to flush MDS too,
		 * but keep the verw if affected by RFDS
		 */
		if ((cap & ARCH_CAP_RFDS_NO) == 0 && (cap & ARCH_CAP_RFDS_CLEAR)) {
			type = "";
		} else if (cpu_info_primary.ci_vmm_cap.vcc_vmx.vmx_has_l1_flush_msr == 1) {
			s = splhigh();
			codepatch_nop(CPTAG_MDS_VMM);
			splx(s);
			type = " (except on vmm entry)";
		} else {
			type = "";
		}
		printf("cpu0: using %s MDS workaround%s\n", "VERW", type);
	} else {
notintel:
		s = splhigh();
		codepatch_nop(CPTAG_MDS);
		codepatch_nop(CPTAG_MDS_VMM);
		splx(s);
	}
}

#ifdef MULTIPROCESSOR
int mp_cpu_start(struct cpu_info *);
void mp_cpu_start_cleanup(struct cpu_info *);
struct cpu_functions mp_cpu_funcs = { mp_cpu_start, NULL,
				      mp_cpu_start_cleanup };
#endif /* MULTIPROCESSOR */

const struct cfattach cpu_ca = {
	sizeof(struct cpu_softc), cpu_match, cpu_attach, NULL, cpu_activate
};

struct cfdriver cpu_cd = {
	NULL, "cpu", DV_DULL, CD_COCOVM
};

/*
 * Statically-allocated CPU info for the primary CPU (or the only
 * CPU, on uniprocessors).  The CPU info list is initialized to
 * point at it.
 */
struct cpu_info_full cpu_info_full_primary = { .cif_cpu = {
	.ci_self = &cpu_info_primary,
	.ci_flags = CPUF_PRIMARY,
} };

struct cpu_info *cpu_info_list = &cpu_info_primary;

#ifdef MULTIPROCESSOR
/*
 * Array of CPU info structures.  Must be statically-allocated because
 * curproc, etc. are used early.
 */
struct cpu_info *cpu_info[MAXCPUS] = { &cpu_info_primary };

void    	cpu_hatch(void *);
void    	cpu_boot_secondary(struct cpu_info *ci);
void    	cpu_start_secondary(struct cpu_info *ci);
#endif

int
cpu_match(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;
	struct cpu_attach_args *caa = aux;

	if (strcmp(caa->caa_name, cf->cf_driver->cd_name) != 0)
		return 0;

	if (cf->cf_unit >= MAXCPUS)
		return 0;

	/* XXX We don't support MP with SEV-ES, yet */
	if (ISSET(cpu_sev_guestmode, SEV_STAT_ES_ENABLED) &&
	    cf->cf_unit >= 1)
		return 0;

	return 1;
}

void	cpu_idle_mwait_cycle(void);
void	cpu_init_mwait(struct cpu_softc *, struct cpu_info *);

u_int	cpu_mwait_size, cpu_mwait_states;

void
cpu_idle_mwait_cycle(void)
{
	struct cpu_info *ci = curcpu();

	if ((read_rflags() & PSL_I) == 0)
		panic("idle with interrupts blocked!");

	/* something already queued? */
	if (!cpu_is_idle(ci))
		return;

	/*
	 * About to idle; setting the MWAIT_IN_IDLE bit tells
	 * cpu_unidle() that it can't be a no-op and tells cpu_kick()
	 * that it doesn't need to use an IPI.  We also set the
	 * MWAIT_KEEP_IDLING bit: those routines clear it to stop
	 * the mwait.  Once they're set, we do a final check of the
	 * queue, in case another cpu called setrunqueue() and added
	 * something to the queue and called cpu_unidle() between
	 * the check in sched_idle() and here.
	 */
	atomic_setbits_int(&ci->ci_mwait, MWAIT_IDLING | MWAIT_ONLY);
	if (cpu_is_idle(ci)) {
		monitor(&ci->ci_mwait, 0, 0);
		if ((ci->ci_mwait & MWAIT_IDLING) == MWAIT_IDLING)
			mwait(0, 0);
	}

	/* done idling; let cpu_kick() know that an IPI is required */
	atomic_clearbits_int(&ci->ci_mwait, MWAIT_IDLING);
}

void
cpu_init_mwait(struct cpu_softc *sc, struct cpu_info *ci)
{
	unsigned int smallest, largest, extensions, c_substates;

	if ((cpu_ecxfeature & CPUIDECX_MWAIT) == 0 || ci->ci_cpuid_level < 0x5)
		return;

	/* get the monitor granularity */
	CPUID(0x5, smallest, largest, extensions, cpu_mwait_states);
	smallest &= 0xffff;
	largest  &= 0xffff;

	/* mask out states C6/C7 in 31:24 for CHT45 errata */
	if (ci->ci_vendor == CPUV_INTEL &&
	    ci->ci_family == 0x06 && ci->ci_model == 0x4c)
		cpu_mwait_states &= 0x00ffffff;

	printf("%s: mwait min=%u, max=%u", sc->sc_dev.dv_xname,
	    smallest, largest);
	if (extensions & 0x1) {
		if (cpu_mwait_states > 0) {
			c_substates = cpu_mwait_states;
			printf(", C-substates=%u", 0xf & c_substates);
			while ((c_substates >>= 4) > 0)
				printf(".%u", 0xf & c_substates);
		}
		if (extensions & 0x2)
			printf(", IBE");
	} else {
		/* substates not supported, forge the default: just C1 */
		cpu_mwait_states = 1 << 4;
	}

	/* paranoia: check the values */
	if (smallest < sizeof(int) || largest < smallest ||
	    (largest & (sizeof(int)-1)))
		printf(" (bogus)");
	else
		cpu_mwait_size = largest;
	printf("\n");

	/* enable use of mwait; may be overridden by acpicpu later */
	if (cpu_mwait_size > 0)
		cpu_idle_cycle_fcn = &cpu_idle_mwait_cycle;
}

void
cpu_attach(struct device *parent, struct device *self, void *aux)
{
	struct cpu_softc *sc = (void *) self;
	struct cpu_attach_args *caa = aux;
	struct cpu_info *ci;
#if defined(MULTIPROCESSOR)
	int cpunum = sc->sc_dev.dv_unit;
	vaddr_t kstack;
	struct pcb *pcb;
#endif

	/*
	 * If we're an Application Processor, allocate a cpu_info
	 * structure, otherwise use the primary's.
	 */
	if (caa->cpu_role == CPU_ROLE_AP) {
		struct cpu_info_full *cif;
		
		cif = km_alloc(sizeof *cif, &kv_any, &kp_zero, &kd_waitok);
		ci = &cif->cif_cpu;
#if defined(MULTIPROCESSOR)
		ci->ci_tss = &cif->cif_tss;
		ci->ci_gdt = &cif->cif_gdt;
		memcpy(ci->ci_gdt, cpu_info_primary.ci_gdt, GDT_SIZE);
		cpu_enter_pages(cif);
		if (cpu_info[cpunum] != NULL)
			panic("cpu at apic id %d already attached?", cpunum);
		cpu_info[cpunum] = ci;
#endif
#ifdef TRAPLOG
		ci->ci_tlog_base = malloc(sizeof(struct tlog),
		    M_DEVBUF, M_WAITOK);
#endif
	} else {
		ci = &cpu_info_primary;
#if defined(MULTIPROCESSOR)
		if (caa->cpu_apicid != lapic_cpu_number()) {
			panic("%s: running cpu is at apic %d"
			    " instead of at expected %d",
			    sc->sc_dev.dv_xname, lapic_cpu_number(), caa->cpu_apicid);
		}
#endif
	}

	ci->ci_self = ci;
	sc->sc_info = ci;

	ci->ci_dev = self;
	ci->ci_apicid = caa->cpu_apicid;
	ci->ci_acpi_proc_id = caa->cpu_acpi_proc_id;
#ifdef MULTIPROCESSOR
	ci->ci_cpuid = cpunum;
#else
	ci->ci_cpuid = 0;	/* False for APs, but they're not used anyway */
#endif
	ci->ci_func = caa->cpu_func;
	ci->ci_handled_intr_level = IPL_NONE;

#ifndef SMALL_KERNEL
	strlcpy(ci->ci_sensordev.xname, ci->ci_dev->dv_xname,
	    sizeof(ci->ci_sensordev.xname));
#endif

#if defined(MULTIPROCESSOR)
	/*
	 * Allocate UPAGES contiguous pages for the idle PCB and stack.
	 */
	kstack = (vaddr_t)km_alloc(USPACE, &kv_any, &kp_dirty, &kd_nowait);
	if (kstack == 0) {
		if (caa->cpu_role != CPU_ROLE_AP) {
			panic("cpu_attach: unable to allocate idle stack for"
			    " primary");
		}
		printf("%s: unable to allocate idle stack\n",
		    sc->sc_dev.dv_xname);
		return;
	}
	pcb = ci->ci_idle_pcb = (struct pcb *) kstack;
	memset(pcb, 0, USPACE);

	pcb->pcb_kstack = kstack + USPACE - 16;
	pcb->pcb_rbp = pcb->pcb_rsp = kstack + USPACE - 16;
	pcb->pcb_pmap = pmap_kernel();
	pcb->pcb_cr3 = pcb->pcb_pmap->pm_pdirpa;
#endif

	/* further PCB init done later. */

	printf(": ");

	switch (caa->cpu_role) {
	case CPU_ROLE_SP:
		printf("(uniprocessor)\n");
		atomic_setbits_int(&ci->ci_flags,
		    CPUF_PRESENT | CPUF_SP | CPUF_PRIMARY);
		cpu_intr_init(ci);
		identifycpu(ci);
		cpu_fix_msrs(ci);
#ifdef MTRR
		mem_range_attach();
#endif /* MTRR */
		/* XXX SP fpuinit(ci) is done earlier */
		cpu_init(ci);
		cpu_init_mwait(sc, ci);
		break;

	case CPU_ROLE_BP:
		printf("apid %d (boot processor)\n", caa->cpu_apicid);
		atomic_setbits_int(&ci->ci_flags,
		    CPUF_PRESENT | CPUF_BSP | CPUF_PRIMARY);
		cpu_intr_init(ci);
		identifycpu(ci);
		cpu_fix_msrs(ci);
#ifdef MTRR
		mem_range_attach();
#endif /* MTRR */

#if NLAPIC > 0
		/*
		 * Enable local apic
		 */
		lapic_enable();
		lapic_calibrate_timer(ci);
#endif
		/* XXX BP fpuinit(ci) is done earlier */
		cpu_init(ci);

#if NIOAPIC > 0
		ioapic_bsp_id = caa->cpu_apicid;
#endif
		cpu_init_mwait(sc, ci);
		break;

	case CPU_ROLE_AP:
		/*
		 * report on an AP
		 */
		printf("apid %d (application processor)\n", caa->cpu_apicid);

#if defined(MULTIPROCESSOR)
		cpu_intr_init(ci);
		cpu_start_secondary(ci);
		clockqueue_init(&ci->ci_queue);
		sched_init_cpu(ci);
		ncpus++;
		if (ci->ci_flags & CPUF_PRESENT) {
			ci->ci_next = cpu_info_list->ci_next;
			cpu_info_list->ci_next = ci;
		}
#else
		printf("%s: not started\n", sc->sc_dev.dv_xname);
#endif
		break;

	default:
		panic("unknown processor type??");
	}

#if defined(MULTIPROCESSOR)
	if (mp_verbose) {
		printf("%s: kstack at 0x%lx for %d bytes\n",
		    sc->sc_dev.dv_xname, kstack, USPACE);
		printf("%s: idle pcb at %p, idle sp at 0x%llx\n",
		    sc->sc_dev.dv_xname, pcb, pcb->pcb_rsp);
	}
#endif
#if NVMM > 0
	cpu_init_vmm(ci);
#endif /* NVMM > 0 */

#ifndef SMALL_KERNEL
	if (ci->ci_sensordev.sensors_count > 0)
		sensordev_install(&ci->ci_sensordev);
#endif
}

static void
replacexsave(int xsave_ext)
{
	extern long _xrstor, _xrstors, _xsave, _xsaves, _xsaveopt;
	static int replacedone = 0;
	int s;

	if (replacedone)
		return;
	replacedone = 1;

	s = splhigh();
	codepatch_replace(CPTAG_XRSTORS,
	    (xsave_ext & XSAVE_XSAVES) ? &_xrstors : &_xrstor, 4);
	codepatch_replace(CPTAG_XRSTOR, &_xrstor, 4);
	codepatch_replace(CPTAG_XSAVE,
	    (xsave_ext & XSAVE_XSAVES) ? &_xsaves :
	    (xsave_ext & XSAVE_XSAVEOPT) ? &_xsaveopt : &_xsave, 4);
	splx(s);
}


/*
 * Initialize the processor appropriately.
 */

void
cpu_init(struct cpu_info *ci)
{
	struct savefpu *sfp;
	u_int cr4;

	/* configure the CPU if needed */
	if (ci->cpu_setup != NULL)
		(*ci->cpu_setup)(ci);

	cr4 = rcr4() | CR4_DEFAULT;
	if (ci->ci_feature_sefflags_ebx & SEFF0EBX_SMEP)
		cr4 |= CR4_SMEP;
	if (ci->ci_feature_sefflags_ebx & SEFF0EBX_SMAP)
		cr4 |= CR4_SMAP;
	if (ci->ci_feature_sefflags_ecx & SEFF0ECX_UMIP)
		cr4 |= CR4_UMIP;
	if ((cpu_ecxfeature & CPUIDECX_XSAVE) && ci->ci_cpuid_level >= 0xd)
		cr4 |= CR4_OSXSAVE;
	if (pg_xo)
		cr4 |= CR4_PKE;
	if (pmap_use_pcid)
		cr4 |= CR4_PCIDE;
	lcr4(cr4);

	if ((cpu_ecxfeature & CPUIDECX_XSAVE) && ci->ci_cpuid_level >= 0xd) {
		u_int32_t eax, ebx, ecx, edx;

		xsave_mask = XFEATURE_X87 | XFEATURE_SSE;
		CPUID_LEAF(0xd, 0, eax, ebx, ecx, edx);
		xsave_mask |= eax & XFEATURE_AVX;
		xsave_mask |= eax & XFEATURE_AVX512;
		xsetbv(0, xsave_mask);
		CPUID_LEAF(0xd, 0, eax, ebx, ecx, edx);
		if (CPU_IS_PRIMARY(ci)) {
			fpu_save_len = ebx;
			KASSERT(fpu_save_len <= sizeof(struct savefpu));
		} else {
			KASSERT(ebx == fpu_save_len);
		}

		/* check for xsaves, xsaveopt, and supervisor features */
		CPUID_LEAF(0xd, 1, eax, ebx, ecx, edx);
		/* Disable XSAVES on AMD family 17h due to Erratum 1386 */
		if (ci->ci_vendor == CPUV_AMD &&
		    ci->ci_family == 0x17) {
			eax &= ~XSAVE_XSAVES;
		}
		if (eax & XSAVE_XSAVES) {
#ifndef SMALL_KERNEL
			if (ci->ci_feature_sefflags_edx & SEFF0EDX_IBT)
				xsave_mask |= ecx & XFEATURE_CET_U;
#endif
			if (xsave_mask & XFEATURE_XSS_MASK) {
				wrmsr(MSR_XSS, xsave_mask & XFEATURE_XSS_MASK);
				CPUID_LEAF(0xd, 1, eax, ebx, ecx, edx);
				KASSERT(ebx <= sizeof(struct savefpu));
			}
			if (CPU_IS_PRIMARY(ci))
				cpu_use_xsaves = 1;
		}

		replacexsave(eax);
	}

	if (CPU_IS_PRIMARY(ci)) {
		/* Clean our FPU save area */
		sfp = fpu_cleandata;
		memset(sfp, 0, fpu_save_len);
		sfp->fp_fxsave.fx_fcw = __INITIAL_NPXCW__;
		sfp->fp_fxsave.fx_mxcsr = __INITIAL_MXCSR__;
		xrstor_user(sfp, xsave_mask);
		if (cpu_use_xsaves || !xsave_mask)
			fpusave(sfp);
		else {
			/* must not use xsaveopt here */
			xsave(sfp, xsave_mask);
		}
	} else {
		fpureset();
	}

#ifdef MULTIPROCESSOR
	atomic_setbits_int(&ci->ci_flags, CPUF_RUNNING);
	/*
	 * Big hammer: flush all TLB entries, including ones from PTEs
	 * with the G bit set.  This should only be necessary if TLB
	 * shootdown falls far behind.
	 */
	cr4 = rcr4();
	lcr4(cr4 & ~CR4_PGE);
	lcr4(cr4);

	/* Check if TSC is synchronized. */
	if (cold && !CPU_IS_PRIMARY(ci))
	      tsc_test_sync_ap(ci);
#endif
}

#if NVMM > 0
/*
 * cpu_init_vmm
 *
 * Initializes per-cpu VMM state
 *
 * Parameters:
 *  ci: the cpu for which state is being initialized
 */
void
cpu_init_vmm(struct cpu_info *ci)
{
	uint64_t msr;

	/*
	 * Detect VMX specific features and initialize VMX-related state.
	 */
	if (ci->ci_vmm_flags & CI_VMM_VMX) {
		ci->ci_vmxon_region = (struct vmxon_region *)malloc(PAGE_SIZE,
		    M_DEVBUF, M_WAITOK | M_ZERO);
		if (!pmap_extract(pmap_kernel(), (vaddr_t)ci->ci_vmxon_region,
		    &ci->ci_vmxon_region_pa))
			panic("Can't locate VMXON region in phys mem");

		ci->ci_vmcs_pa = VMX_VMCS_PA_CLEAR;
		rw_init(&ci->ci_vmcs_lock, "vmcslock");

		if (rdmsr_safe(IA32_VMX_EPT_VPID_CAP, &msr) == 0 &&
		    msr & IA32_EPT_VPID_CAP_INVEPT_CONTEXT)
			ci->ci_vmm_cap.vcc_vmx.vmx_invept_mode =
			    IA32_VMX_INVEPT_SINGLE_CTX;
		else
			ci->ci_vmm_cap.vcc_vmx.vmx_invept_mode =
			    IA32_VMX_INVEPT_GLOBAL_CTX;
	}
}
#endif /* NVMM > 0 */

#ifdef MULTIPROCESSOR
void
cpu_boot_secondary_processors(void)
{
	struct cpu_info *ci;
	u_long i;

	for (i=0; i < MAXCPUS; i++) {
		ci = cpu_info[i];
		if (ci == NULL)
			continue;
		if (ci->ci_idle_pcb == NULL)
			continue;
		if ((ci->ci_flags & CPUF_PRESENT) == 0)
			continue;
		if (ci->ci_flags & (CPUF_BSP | CPUF_SP | CPUF_PRIMARY))
			continue;
		ci->ci_randseed = (arc4random() & 0x7fffffff) + 1;
		cpu_boot_secondary(ci);
	}
}

void
cpu_start_secondary(struct cpu_info *ci)
{
	int i;
	u_long s;

	atomic_setbits_int(&ci->ci_flags, CPUF_AP);

	pmap_kenter_pa(MP_TRAMPOLINE, MP_TRAMPOLINE, PROT_READ | PROT_EXEC);
	pmap_kenter_pa(MP_TRAMP_DATA, MP_TRAMP_DATA, PROT_READ | PROT_WRITE);

	CPU_STARTUP(ci);

	/*
	 * wait for it to become ready
	 */
	for (i = 100000; (!(ci->ci_flags & CPUF_PRESENT)) && i>0;i--) {
		delay(10);
	}
	if (! (ci->ci_flags & CPUF_PRESENT)) {
		printf("%s: failed to become ready\n", ci->ci_dev->dv_xname);
#if defined(MPDEBUG) && defined(DDB)
		printf("dropping into debugger; continue from here to resume boot\n");
		db_enter();
#endif
	}

	if ((ci->ci_flags & CPUF_IDENTIFIED) == 0) {
		atomic_setbits_int(&ci->ci_flags, CPUF_IDENTIFY);

		/* wait for it to identify */
		for (i = 2000000; (ci->ci_flags & CPUF_IDENTIFY) && i > 0; i--)
			delay(10);

		if (ci->ci_flags & CPUF_IDENTIFY)
			printf("%s: failed to identify\n",
			    ci->ci_dev->dv_xname);
	}

	if (ci->ci_flags & CPUF_IDENTIFIED) {
		/*
		 * Test if TSCs are synchronized.  Invalidate cache to
		 * minimize possible cache effects.  Disable interrupts to
		 * try to rule out external interference.
		 */
		s = intr_disable();
		wbinvd();
		tsc_test_sync_bp(curcpu());
		intr_restore(s);
	}

	CPU_START_CLEANUP(ci);

	pmap_kremove(MP_TRAMPOLINE, PAGE_SIZE);
	pmap_kremove(MP_TRAMP_DATA, PAGE_SIZE);
}

void
cpu_boot_secondary(struct cpu_info *ci)
{
	int i;
	u_long s;

	atomic_setbits_int(&ci->ci_flags, CPUF_GO);

	for (i = 100000; (!(ci->ci_flags & CPUF_RUNNING)) && i>0;i--) {
		delay(10);
	}
	if (! (ci->ci_flags & CPUF_RUNNING)) {
		printf("cpu failed to start\n");
#if defined(MPDEBUG) && defined(DDB)
		printf("dropping into debugger; continue from here to resume boot\n");
		db_enter();
#endif
	} else if (cold) {
		/* Test if TSCs are synchronized again. */
		s = intr_disable();
		wbinvd();
		tsc_test_sync_bp(curcpu());
		intr_restore(s);
	}
}

/*
 * The CPU ends up here when it's ready to run
 * This is called from code in mptramp.s; at this point, we are running
 * in the idle pcb/idle stack of the new cpu.  When this function returns,
 * this processor will enter the idle loop and start looking for work.
 *
 * XXX should share some of this with init386 in machdep.c
 */
void
cpu_hatch(void *v)
{
	struct cpu_info *ci = (struct cpu_info *)v;
	int s;

	{
		uint32_t vendor[4];
		int level;

		CPUID(0, level, vendor[0], vendor[2], vendor[1]);
		vendor[3] = 0;
		cpu_set_vendor(ci, level, (const char *)vendor);
	}

	cpu_init_msrs(ci);

#ifdef DEBUG
	if (ci->ci_flags & CPUF_PRESENT)
		panic("%s: already running!?", ci->ci_dev->dv_xname);
#endif
	atomic_setbits_int(&ci->ci_flags, CPUF_PRESENT);

	lapic_enable();
	cpu_ucode_apply(ci);
	cpu_tsx_disable(ci);

	if ((ci->ci_flags & CPUF_IDENTIFIED) == 0) {
		/*
		 * We need to wait until we can identify, otherwise dmesg
		 * output will be messy.
		 */
		while ((ci->ci_flags & CPUF_IDENTIFY) == 0)
			delay(10);

		identifycpu(ci);

		/* Prevent identifycpu() from running again */
		atomic_setbits_int(&ci->ci_flags, CPUF_IDENTIFIED);

		/* Signal we're done */
		atomic_clearbits_int(&ci->ci_flags, CPUF_IDENTIFY);
	}

	/* These have to run after identifycpu() */ 
	cpu_fix_msrs(ci);

	/*
	 * Test if our TSC is synchronized for the first time.
	 * Note that interrupts are off at this point.
	 */
	wbinvd();
	tsc_test_sync_ap(ci);

	while ((ci->ci_flags & CPUF_GO) == 0)
		delay(10);
#ifdef HIBERNATE
	if ((ci->ci_flags & CPUF_PARK) != 0) {
		if (ci->ci_feature_sefflags_edx & SEFF0EDX_IBT)
			lcr4(rcr4() & ~CR4_CET);
		atomic_clearbits_int(&ci->ci_flags, CPUF_PARK);
		hibernate_drop_to_real_mode();
	}
#endif /* HIBERNATE */

#ifdef DEBUG
	if (ci->ci_flags & CPUF_RUNNING)
		panic("%s: already running!?", ci->ci_dev->dv_xname);
#endif

	cpu_init_idt();
	lapic_set_lvt();
	gdt_init_cpu(ci);
	fpuinit(ci);

	lldt(0);

	cpu_init(ci);
#if NPVBUS > 0
	pvbus_init_cpu();
#endif

	/* Re-initialise memory range handling on AP */
	if (mem_range_softc.mr_op != NULL)
		mem_range_softc.mr_op->initAP(&mem_range_softc);

	s = splhigh();
	lcr8(0);
	intr_enable();
	splx(s);

	lapic_startclock();

	sched_toidle();
}

#if defined(DDB)

#include <ddb/db_output.h>
#include <machine/db_machdep.h>

/*
 * Dump cpu information from ddb.
 */
void
cpu_debug_dump(void)
{
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;

	db_printf("addr		dev	id	flags	ipis	curproc\n");
	CPU_INFO_FOREACH(cii, ci) {
		db_printf("%p	%s	%u	%x	%x	%10p\n",
		    ci,
		    ci->ci_dev == NULL ? "BOOT" : ci->ci_dev->dv_xname,
		    ci->ci_cpuid,
		    ci->ci_flags, ci->ci_ipis,
		    ci->ci_curproc);
	}
}
#endif

int
mp_cpu_start(struct cpu_info *ci)
{
	unsigned short dwordptr[2];

	/*
	 * "The BSP must initialize CMOS shutdown code to 0Ah ..."
	 */

	outb(IO_RTC, NVRAM_RESET);
	outb(IO_RTC+1, NVRAM_RESET_JUMP);

	/*
	 * "and the warm reset vector (DWORD based at 40:67) to point
	 * to the AP startup code ..."
	 */

	dwordptr[0] = 0;
	dwordptr[1] = MP_TRAMPOLINE >> 4;

	pmap_kenter_pa(0, 0, PROT_READ | PROT_WRITE);
	memcpy((u_int8_t *) 0x467, dwordptr, 4);
	pmap_kremove(0, PAGE_SIZE);

#if NLAPIC > 0
	/*
	 * ... prior to executing the following sequence:"
	 */

	if (ci->ci_flags & CPUF_AP) {
		x86_ipi_init(ci->ci_apicid);

		delay(10000);

		if (cpu_feature & CPUID_APIC) {
			x86_ipi(MP_TRAMPOLINE/PAGE_SIZE, ci->ci_apicid,
			    LAPIC_DLMODE_STARTUP);
			delay(200);

			x86_ipi(MP_TRAMPOLINE/PAGE_SIZE, ci->ci_apicid,
			    LAPIC_DLMODE_STARTUP);
			delay(200);
		}
	}
#endif
	return 0;
}

void
mp_cpu_start_cleanup(struct cpu_info *ci)
{
	/*
	 * Ensure the NVRAM reset byte contains something vaguely sane.
	 */

	outb(IO_RTC, NVRAM_RESET);
	outb(IO_RTC+1, NVRAM_RESET_RST);
}
#endif	/* MULTIPROCESSOR */

typedef void (vector)(void);
extern vector Xsyscall_meltdown, Xsyscall, Xsyscall32;

void
cpu_init_msrs(struct cpu_info *ci)
{
	wrmsr(MSR_STAR,
	    ((uint64_t)GSEL(GCODE_SEL, SEL_KPL) << 32) |
	    ((uint64_t)GSEL(GUDATA_SEL-1, SEL_UPL) << 48));
	wrmsr(MSR_LSTAR, cpu_meltdown ? (uint64_t)Xsyscall_meltdown :
	    (uint64_t)Xsyscall);
	wrmsr(MSR_CSTAR, 0);
	wrmsr(MSR_SFMASK, PSL_NT|PSL_T|PSL_I|PSL_C|PSL_D|PSL_AC);

	wrmsr(MSR_FSBASE, 0);
	wrmsr(MSR_GSBASE, (u_int64_t)ci);
	wrmsr(MSR_KERNELGSBASE, 0);
	patinit(ci);
}

void
cpu_fix_msrs(struct cpu_info *ci)
{
	int family = ci->ci_family;
	uint64_t msr, nmsr;

	if (ci->ci_vendor == CPUV_INTEL) {
		if ((family > 6 || (family == 6 && ci->ci_model >= 0xd)) &&
		    rdmsr_safe(MSR_MISC_ENABLE, &msr) == 0 &&
		    (msr & MISC_ENABLE_FAST_STRINGS) == 0) {
			msr |= MISC_ENABLE_FAST_STRINGS;
			wrmsr(MSR_MISC_ENABLE, msr);
			DPRINTF("%s: enabled fast strings\n", ci->ci_dev->dv_xname);
	
		/*
		 * Attempt to disable Silicon Debug and lock the configuration
		 * if it's enabled and unlocked.
		 */
		if (cpu_ecxfeature & CPUIDECX_SDBG) {
			msr = rdmsr(IA32_DEBUG_INTERFACE);
			if ((msr & IA32_DEBUG_INTERFACE_ENABLE) &&
			    (msr & IA32_DEBUG_INTERFACE_LOCK) == 0) {
				msr &= IA32_DEBUG_INTERFACE_MASK;
				msr |= IA32_DEBUG_INTERFACE_LOCK;
				wrmsr(IA32_DEBUG_INTERFACE, msr);
			} else if (msr & IA32_DEBUG_INTERFACE_ENABLE)
				printf("%s: cannot disable silicon debug\n",
				    ci->ci_dev->dv_xname);
			}
		}
	}

	if (ci->ci_vendor == CPUV_AMD) {
		/* Apply AMD errata */
		amd64_errata(ci);

		/*
		 * "Mitigation G-2" per AMD's Whitepaper "Software Techniques
		 * for Managing Speculation on AMD Processors"
		 *
		 * By setting MSR C001_1029[1]=1, LFENCE becomes a dispatch
		 * serializing instruction.
		 *
		 * This MSR is available on all AMD families >= 10h, except 11h
		 * where LFENCE is always serializing.
		 */
		if (family >= 0x10 && family != 0x11) {
			nmsr = msr = rdmsr(MSR_DE_CFG);
			nmsr |= DE_CFG_SERIALIZE_LFENCE;
			if (msr != nmsr)
				wrmsr(MSR_DE_CFG, nmsr);
		}
		if (family == 0x17 && ci->ci_model >= 0x31 &&
		    (cpu_ecxfeature & CPUIDECX_HV) == 0) {
			nmsr = msr = rdmsr(MSR_DE_CFG);
			nmsr |= DE_CFG_SERIALIZE_9;
			if (msr != nmsr)
				wrmsr(MSR_DE_CFG, nmsr);
		}
	}

#ifndef SMALL_KERNEL
	if (ci->ci_feature_sefflags_edx & SEFF0EDX_IBT) {
		msr = rdmsr(MSR_S_CET);
		wrmsr(MSR_S_CET, (msr & ~MSR_CET_NO_TRACK_EN) | MSR_CET_ENDBR_EN);
		lcr4(rcr4() | CR4_CET);
	}
#endif
}

void
cpu_tsx_disable(struct cpu_info *ci)
{
	uint64_t msr;
	uint32_t dummy, sefflags_edx;

	/* this runs before identifycpu() populates ci_feature_sefflags_edx */
	if (ci->ci_cpuid_level < 0x07)
		return;
	CPUID_LEAF(0x7, 0, dummy, dummy, dummy, sefflags_edx);

	if (ci->ci_vendor == CPUV_INTEL &&
	    (sefflags_edx & SEFF0EDX_ARCH_CAP)) {
		msr = rdmsr(MSR_ARCH_CAPABILITIES);
		if (msr & ARCH_CAP_TSX_CTRL) {
			msr = rdmsr(MSR_TSX_CTRL);
			msr |= TSX_CTRL_RTM_DISABLE | TSX_CTRL_TSX_CPUID_CLEAR;
			wrmsr(MSR_TSX_CTRL, msr);
		}
	}
}

void
patinit(struct cpu_info *ci)
{
	extern int	pmap_pg_wc;
	u_int64_t	reg;

	if ((cpu_feature & CPUID_PAT) == 0)
		return;
	/*
	 * Set up PAT bits.
	 * The default pat table is the following:
	 * WB, WT, UC-, UC, WB, WT, UC-, UC
	 * We change it to:
	 * WB, WC, UC-, UC, WB, WC, UC-, UC
	 * i.e change the WT bit to be WC.
	 */
	reg = PATENTRY(0, PAT_WB) | PATENTRY(1, PAT_WC) |
	    PATENTRY(2, PAT_UCMINUS) | PATENTRY(3, PAT_UC) |
	    PATENTRY(4, PAT_WB) | PATENTRY(5, PAT_WC) |
	    PATENTRY(6, PAT_UCMINUS) | PATENTRY(7, PAT_UC);

	wrmsr(MSR_CR_PAT, reg);
	pmap_pg_wc = PG_WC;
}

struct timeout rdrand_tmo;
void rdrand(void *);

void
rdrand(void *v)
{
	struct timeout *tmo = v;
	extern int	has_rdrand, has_rdseed;
	union {
		uint64_t u64;
		uint32_t u32[2];
	} r, t;
	uint64_t tsc;
	uint8_t valid = 0;

	tsc = rdtsc();
	if (has_rdseed)
		__asm volatile(
		    "rdseed	%0\n\t"
		    "setc	%1\n"
		    : "=r" (r.u64), "=qm" (valid) );
	if (has_rdrand && (has_rdseed == 0 || valid == 0))
		__asm volatile(
		    "rdrand	%0\n\t"
		    "setc	%1\n"
		    : "=r" (r.u64), "=qm" (valid) );

	t.u64 = tsc;
	t.u64 ^= r.u64;
	t.u64 ^= valid;			/* potential rdrand empty */
	if (has_rdrand)
		t.u64 += rdtsc();	/* potential vmexit latency */

	enqueue_randomness(t.u32[0]);
	enqueue_randomness(t.u32[1]);

	if (tmo)
		timeout_add_msec(tmo, 10);
}

int
cpu_activate(struct device *self, int act)
{
	struct cpu_softc *sc = (struct cpu_softc *)self;

	switch (act) {
	case DVACT_RESUME:
		if (sc->sc_info->ci_cpuid == 0)
			rdrand(NULL);
#if NPCTR > 0
		pctr_resume(sc->sc_info);
#endif
		break;
	}

	return (0);
}

/*
 * cpu_enter_pages
 *
 * Requests mapping of various special pages required in the Intel Meltdown
 * case (to be entered into the U-K page table):
 *
 *  1 tss+gdt page for each CPU
 *  1 trampoline stack page for each CPU
 *
 * The cpu_info_full struct for each CPU straddles these pages. The offset into
 * 'cif' is calculated below, for each page. For more information, consult
 * the definition of struct cpu_info_full in cpu_full.h
 *
 * On CPUs unaffected by Meltdown, this function still configures 'cif' but
 * the calls to pmap_enter_special become no-ops.
 *
 * Parameters:
 *  cif : the cpu_info_full structure describing a CPU whose pages are to be
 *    entered into the special meltdown U-K page table.
 */ 
void
cpu_enter_pages(struct cpu_info_full *cif)
{
	vaddr_t va;
	paddr_t pa;

	/* The TSS+GDT need to be readable */
	va = (vaddr_t)cif;
	pmap_extract(pmap_kernel(), va, &pa);
	pmap_enter_special(va, pa, PROT_READ);
	DPRINTF("%s: entered tss+gdt page at va 0x%llx pa 0x%llx\n", __func__,
	   (uint64_t)va, (uint64_t)pa);

	/* The trampoline stack page needs to be read/write */
	va = (vaddr_t)&cif->cif_tramp_stack;
	pmap_extract(pmap_kernel(), va, &pa);
	pmap_enter_special(va, pa, PROT_READ | PROT_WRITE);
	DPRINTF("%s: entered t.stack page at va 0x%llx pa 0x%llx\n", __func__,
	   (uint64_t)va, (uint64_t)pa);

	cif->cif_tss.tss_rsp0 = va + sizeof(cif->cif_tramp_stack) - 16;
	DPRINTF("%s: cif_tss.tss_rsp0 = 0x%llx\n" ,__func__,
	    (uint64_t)cif->cif_tss.tss_rsp0);
	cif->cif_cpu.ci_intr_rsp = cif->cif_tss.tss_rsp0 -
	    sizeof(struct iretq_frame);

#define	SETUP_IST_SPECIAL_STACK(ist, cif, member) do {			\
	(cif)->cif_tss.tss_ist[(ist)] = (vaddr_t)&(cif)->member +	\
	    sizeof((cif)->member) - 16;					\
	(cif)->member[nitems((cif)->member) - 2] = (int64_t)&(cif)->cif_cpu; \
} while (0)

	SETUP_IST_SPECIAL_STACK(0, cif, cif_dblflt_stack);
	SETUP_IST_SPECIAL_STACK(1, cif, cif_nmi_stack);

	/* an empty iomap, by setting its offset to the TSS limit */
	cif->cif_tss.tss_iobase = sizeof(cif->cif_tss);
}

#ifdef MULTIPROCESSOR
int
wbinvd_on_all_cpus(void)
{
	x86_broadcast_ipi(X86_IPI_WBINVD);
	wbinvd();
	return 0;
}

volatile long wbinvd_wait __attribute__((section(".kudata")));

void
wbinvd_on_all_cpus_acked(void)
{
	struct cpu_info *ci, *self = curcpu();;
	CPU_INFO_ITERATOR cii;
	long wait = 0;
	u_int64_t mask = 0;
	int s;

	CPU_INFO_FOREACH(cii, ci) {
		if (ci == self)
			continue;
		mask |= (1ULL << ci->ci_cpuid);
		wait++;
	}

	KASSERT(wait > 0);

	s = splvm();
	while (atomic_cas_ulong(&wbinvd_wait, 0 , wait) != 0) {
		while (wbinvd_wait != 0) {
			CPU_BUSY_CYCLE();
		}
	}

	CPU_INFO_FOREACH(cii, ci) {
		if ((mask & (1ULL << ci->ci_cpuid)) == 0)
			continue;
		if (x86_fast_ipi(ci, LAPIC_IPI_WBINVD) != 0)
			panic("%s: ipi failed", __func__);
	}
	splx(s);

	wbinvd();

	while (wbinvd_wait != 0)
		CPU_BUSY_CYCLE();
}
#endif /* MULTIPROCESSOR */

int cpu_suspended;
int cpu_wakeups;

#ifdef SUSPEND

void
cpu_suspend_cycle(void)
{
	if (cpu_suspend_cycle_fcn)
		cpu_suspend_cycle_fcn();
	else
		cpu_idle_cycle_fcn();
}

int
cpu_suspend_primary(void)
{
	struct cpu_info *ci = curcpu();

	/* Mask clock interrupts. */
	local_pic.pic_hwmask(&local_pic, 0);

	/*
	 * All non-wakeup interrupts should be masked at this point;
	 * re-enable interrupts such that wakeup interrupts actually
	 * wake us up.  Set a flag such that drivers can tell we're
	 * suspended and change their behaviour accordingly.  They can
	 * wake us up by clearing the flag.
	 */
	cpu_suspended = 1;
	ci->ci_ilevel = IPL_NONE;
	intr_enable();

	while (cpu_suspended) {
		cpu_suspend_cycle();
		cpu_wakeups++;
	}

	intr_disable();
	ci->ci_ilevel = IPL_HIGH;

	/* Unmask clock interrupts. */
	local_pic.pic_hwunmask(&local_pic, 0);

	return 0;
}

#endif
