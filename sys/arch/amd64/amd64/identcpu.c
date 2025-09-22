/*	$OpenBSD: identcpu.c,v 1.152 2025/09/14 15:52:28 mlarkin Exp $	*/
/*	$NetBSD: identcpu.c,v 1.1 2003/04/26 18:39:28 fvdl Exp $	*/

/*
 * Copyright (c) 2003 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Frank van der Linden for Wasabi Systems, Inc.
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
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/proc.h>
#include <sys/sysctl.h>

#include "vmm.h"
#include "pvbus.h"

#include <machine/cpu.h>
#include <machine/cpufunc.h>

#if NPVBUS > 0
#include <dev/pv/pvvar.h>
#endif

void	replacesmap(void);
void	replacemeltdown(void);
uint64_t cpu_freq(struct cpu_info *);
void	tsc_identify(struct cpu_info *);
void	tsc_timecounter_init(struct cpu_info *, uint64_t);
#if NVMM > 0
void	cpu_check_vmm_cap(struct cpu_info *);
#endif /* NVMM > 0 */

/* sysctl wants this. */
char cpu_model[48];
int cpuspeed;

int amd64_has_xcrypt;
int amd64_pos_cbit;	/* C bit position for SEV */
int amd64_min_noes_asid;
int has_rdrand;
int has_rdseed;

int
cpu_amd64speed(int *freq)
{
	*freq = cpuspeed;
	return (0);
}

#ifndef SMALL_KERNEL
void	intelcore_update_sensor(void *);
void	cpu_hz_update_sensor(void *);

/*
 * Temperature read on the CPU is relative to the maximum
 * temperature supported by the CPU, Tj(Max).
 * Refer to:
 * 64-ia-32-architectures-software-developer-vol-3c-part-3-manual.pdf
 * Section 35 and
 * http://www.intel.com/content/dam/www/public/us/en/documents/
 * white-papers/cpu-monitoring-dts-peci-paper.pdf
 *
 * The temperature on Intel CPUs can be between 70 and 105 degC, since
 * Westmere we can read the TJmax from the die. For older CPUs we have
 * to guess or use undocumented MSRs. Then we subtract the temperature
 * portion of thermal status from max to get current temperature.
 */
void
intelcore_update_sensor(void *args)
{
	struct cpu_info *ci = (struct cpu_info *) args;
	u_int64_t msr;
	int max = 100;

	/* Only some Core family chips have MSR_TEMPERATURE_TARGET. */
	if (ci->ci_model == 0x0e &&
	    (rdmsr(MSR_TEMPERATURE_TARGET_UNDOCUMENTED) &
	     MSR_TEMPERATURE_TARGET_LOW_BIT_UNDOCUMENTED))
		max = 85;

	/*
	 * Newer CPUs can tell you what their max temperature is.
	 * See: '64-ia-32-architectures-software-developer-
	 * vol-3c-part-3-manual.pdf'
	 */
	if (ci->ci_model > 0x17 && ci->ci_model != 0x1c &&
	    ci->ci_model != 0x26 && ci->ci_model != 0x27 &&
	    ci->ci_model != 0x35 && ci->ci_model != 0x36)
		max = MSR_TEMPERATURE_TARGET_TJMAX(
		    rdmsr(MSR_TEMPERATURE_TARGET));

	msr = rdmsr(MSR_THERM_STATUS);
	if (msr & MSR_THERM_STATUS_VALID_BIT) {
		ci->ci_sensor.value = max - MSR_THERM_STATUS_TEMP(msr);
		/* micro degrees */
		ci->ci_sensor.value *= 1000000;
		/* kelvin */
		ci->ci_sensor.value += 273150000;
		ci->ci_sensor.flags &= ~SENSOR_FINVALID;
	} else {
		ci->ci_sensor.value = 0;
		ci->ci_sensor.flags |= SENSOR_FINVALID;
	}
}

/*
 * Effective CPU frequency measurement
 *
 * Refer to:
 *   64-ia-32-architectures-software-developer-vol-3b-part-2-manual.pdf
 *   Section 14.2 and
 *   OSRR for AMD Family 17h processors Section 2.1.2
 * Round to 50Mhz which is the accuracy of this measurement.
 */
#define FREQ_50MHZ	(50ULL * 1000000ULL * 1000000ULL)
void
cpu_hz_update_sensor(void *args)
{
	extern uint64_t	 tsc_frequency;
	struct cpu_info	*ci = args;
	uint64_t	 mperf, aperf, mdelta, adelta, val;
	unsigned long	 s;

	sched_peg_curproc(ci);

	s = intr_disable();
	mperf = rdmsr(MSR_MPERF);
	aperf = rdmsr(MSR_APERF);
	intr_restore(s);

	mdelta = mperf - ci->ci_hz_mperf;
	adelta = aperf - ci->ci_hz_aperf;
	ci->ci_hz_mperf = mperf;
	ci->ci_hz_aperf = aperf;

	if (mdelta > 0) {
		val = (adelta * 1000000) / mdelta * tsc_frequency;
		val = ((val + FREQ_50MHZ / 2) / FREQ_50MHZ) * FREQ_50MHZ;
		ci->ci_hz_sensor.value = val;
	}

	sched_unpeg_curproc();
}
#endif

void (*setperf_setup)(struct cpu_info *);

void via_nano_setup(struct cpu_info *ci);

void cpu_topology(struct cpu_info *ci);

void
via_nano_setup(struct cpu_info *ci)
{
	u_int32_t regs[4], val;
	u_int64_t msreg;
	int model = (ci->ci_signature >> 4) & 15;

	if (model >= 9) {
		CPUID(0xC0000000, regs[0], regs[1], regs[2], regs[3]);
		val = regs[0];
		if (val >= 0xC0000001) {
			CPUID(0xC0000001, regs[0], regs[1], regs[2], regs[3]);
			val = regs[3];
		} else
			val = 0;

		if (val & (C3_CPUID_HAS_RNG | C3_CPUID_HAS_ACE))
			printf("%s:", ci->ci_dev->dv_xname);

		/* Enable RNG if present and disabled */
		if (val & C3_CPUID_HAS_RNG) {
			extern int viac3_rnd_present;

			if (!(val & C3_CPUID_DO_RNG)) {
				msreg = rdmsr(0x110B);
				msreg |= 0x40;
				wrmsr(0x110B, msreg);
			}
			viac3_rnd_present = 1;
			printf(" RNG");
		}

		/* Enable AES engine if present and disabled */
		if (val & C3_CPUID_HAS_ACE) {
#ifdef CRYPTO
			if (!(val & C3_CPUID_DO_ACE)) {
				msreg = rdmsr(0x1107);
				msreg |= (0x01 << 28);
				wrmsr(0x1107, msreg);
			}
			amd64_has_xcrypt |= C3_HAS_AES;
#endif /* CRYPTO */
			printf(" AES");
		}

		/* Enable ACE2 engine if present and disabled */
		if (val & C3_CPUID_HAS_ACE2) {
#ifdef CRYPTO
			if (!(val & C3_CPUID_DO_ACE2)) {
				msreg = rdmsr(0x1107);
				msreg |= (0x01 << 28);
				wrmsr(0x1107, msreg);
			}
			amd64_has_xcrypt |= C3_HAS_AESCTR;
#endif /* CRYPTO */
			printf(" AES-CTR");
		}

		/* Enable SHA engine if present and disabled */
		if (val & C3_CPUID_HAS_PHE) {
#ifdef CRYPTO
			if (!(val & C3_CPUID_DO_PHE)) {
				msreg = rdmsr(0x1107);
				msreg |= (0x01 << 28/**/);
				wrmsr(0x1107, msreg);
			}
			amd64_has_xcrypt |= C3_HAS_SHA;
#endif /* CRYPTO */
			printf(" SHA1 SHA256");
		}

		/* Enable MM engine if present and disabled */
		if (val & C3_CPUID_HAS_PMM) {
#ifdef CRYPTO
			if (!(val & C3_CPUID_DO_PMM)) {
				msreg = rdmsr(0x1107);
				msreg |= (0x01 << 28/**/);
				wrmsr(0x1107, msreg);
			}
			amd64_has_xcrypt |= C3_HAS_MM;
#endif /* CRYPTO */
			printf(" RSA");
		}

		printf("\n");
	}
}

#ifndef SMALL_KERNEL
void via_update_sensor(void *args);
void
via_update_sensor(void *args)
{
	struct cpu_info *ci = (struct cpu_info *) args;
	u_int64_t msr;

	msr = rdmsr(MSR_CENT_TMTEMPERATURE);
	ci->ci_sensor.value = (msr & 0xffffff);
	/* micro degrees */
	ci->ci_sensor.value *= 1000000;
	ci->ci_sensor.value += 273150000;
	ci->ci_sensor.flags &= ~SENSOR_FINVALID;
}
#endif

uint64_t
cpu_freq_ctr(struct cpu_info *ci, uint32_t cpu_perf_eax,
    uint32_t cpu_perf_edx)
{
	uint64_t count, last_count, msr;

	if ((ci->ci_flags & CPUF_CONST_TSC) == 0 ||
	    (cpu_perf_eax & CPUIDEAX_VERID) <= 1 ||
	    CPUIDEDX_NUM_FC(cpu_perf_edx) <= 1)
		return (0);

	msr = rdmsr(MSR_PERF_FIXED_CTR_CTRL);
	if (msr & MSR_PERF_FIXED_CTR_FC(1, MSR_PERF_FIXED_CTR_FC_MASK)) {
		/* some hypervisor is dicking us around */
		return (0);
	}

	msr |= MSR_PERF_FIXED_CTR_FC(1, MSR_PERF_FIXED_CTR_FC_1);
	wrmsr(MSR_PERF_FIXED_CTR_CTRL, msr);

	msr = rdmsr(MSR_PERF_GLOBAL_CTRL) | MSR_PERF_GLOBAL_CTR1_EN;
	wrmsr(MSR_PERF_GLOBAL_CTRL, msr);

	last_count = rdmsr(MSR_PERF_FIXED_CTR1);
	delay(100000);
	count = rdmsr(MSR_PERF_FIXED_CTR1);

	msr = rdmsr(MSR_PERF_FIXED_CTR_CTRL);
	msr &= MSR_PERF_FIXED_CTR_FC(1, MSR_PERF_FIXED_CTR_FC_MASK);
	wrmsr(MSR_PERF_FIXED_CTR_CTRL, msr);

	msr = rdmsr(MSR_PERF_GLOBAL_CTRL);
	msr &= ~MSR_PERF_GLOBAL_CTR1_EN;
	wrmsr(MSR_PERF_GLOBAL_CTRL, msr);

	return ((count - last_count) * 10);
}

uint64_t
cpu_freq(struct cpu_info *ci)
{
	uint64_t last_count, count;

	last_count = rdtsc();
	delay(100000);
	count = rdtsc();

	return ((count - last_count) * 10);
}

/* print flags from one cpuid for cpu0 */
static inline void
pcpu0id3(const char *id, char reg1, uint32_t val1, const char *bits1,
    char reg2, uint32_t val2, const char *bits2,
    char reg3, uint32_t val3, const char *bits3)
{
	if (val1 || val2 || val3) {
		printf("\ncpu0: cpuid %s", id);
		if (val1)
			printf(" e%cx=%b", reg1, val1, bits1);
		if (val2)
			printf(" e%cx=%b", reg2, val2, bits2);
		if (val3)
			printf(" e%cx=%b", reg3, val3, bits3);
	}
}

/* print flags from one, 32-bit MSR for cpu0 */
static inline void
pmsr032(uint32_t msr, uint32_t value, const char *bits)
{
	if (value)
		printf("\ncpu0: msr %x=%b", msr, value, bits);
}

static void
pbitdiff(uint32_t value, uint32_t base_value, const char *bits)
{
	uint32_t minus;
	if (value == base_value)
		return;
	minus = base_value & ~value;
	value &= ~base_value;
	if (minus)
		printf("-%b", minus, bits);
	if (value)
		printf("+%b", value, bits);
}

static inline void
pcpuid(struct cpu_info *ci, const char *id, char reg, uint32_t val,
    uint32_t prev_val, const char *bits)
{
	if (CPU_IS_PRIMARY(ci))
		pcpu0id3(id, reg, val, bits, 0, 0, NULL, 0, 0, NULL);
	else if (val != prev_val) {
		printf("\n%s: cpuid %s e%cx=", ci->ci_dev->dv_xname, id, reg);
		pbitdiff(val, prev_val, bits);
	}
}

static inline void
pcpuid2(struct cpu_info *ci, const char *id,
    char reg1, uint32_t val1, uint32_t prev_val1, const char *bits1,
    char reg2, uint32_t val2, uint32_t prev_val2, const char *bits2)
{
	if (CPU_IS_PRIMARY(ci))
		pcpu0id3(id, reg1, val1, bits1, reg2, val2, bits2, 0, 0,
		    NULL);
	else if (val1 != prev_val1 || val2 != prev_val2) {
		printf("\n%s: cpuid %s", ci->ci_dev->dv_xname, id);
		if (val1 != prev_val1) {
			printf(" e%cx=", reg1);
			pbitdiff(val1, prev_val1, bits1);
		}
		if (val2 != prev_val2) {
			printf(" e%cx=", reg2);
			pbitdiff(val2, prev_val2, bits2);
		}
	}
}

static inline void
pcpuid3(struct cpu_info *ci, const char *id,
    char reg1, uint32_t val1, uint32_t prev_val1, const char *bits1,
    char reg2, uint32_t val2, uint32_t prev_val2, const char *bits2,
    char reg3, uint32_t val3, uint32_t prev_val3, const char *bits3)
{
	if (CPU_IS_PRIMARY(ci))
		pcpu0id3(id, reg1, val1, bits1, reg2, val2, bits2, reg3, val3,
		    bits3);
	else if (val1 != prev_val1 || val2 != prev_val2 || val3 != prev_val3) {
		printf("\n%s: cpuid %s", ci->ci_dev->dv_xname, id);
		if (val1 != prev_val1) {
			printf(" e%cx=", reg1);
			pbitdiff(val1, prev_val1, bits1);
		}
		if (val2 != prev_val2) {
			printf(" e%cx=", reg2);
			pbitdiff(val2, prev_val2, bits2);
		}
		if (val3 != prev_val3) {
			printf(" e%cx=", reg3);
			pbitdiff(val3, prev_val3, bits3);
		}
	}
}

static inline void
pmsr32(struct cpu_info *ci, uint32_t msr, uint32_t value, uint32_t prev_value,
    const char *bits)
{
	if (CPU_IS_PRIMARY(ci))
		pmsr032(msr, value, bits);
	else if (value != prev_value) {
		printf("\n%s: msr %x=", ci->ci_dev->dv_xname, msr);
		pbitdiff(value, prev_value, bits);
	}
}

#ifdef MULTIPROCESSOR
static uint32_t prevcpu_perf_eax;
static uint32_t prevcpu_perf_edx;
#endif

static inline void
print_perf_cpuid(struct cpu_info *ci, uint32_t cpu_perf_eax,
    uint32_t cpu_perf_edx)
{
	uint32_t version;

	if (CPU_IS_PRIMARY(ci)) {
		version = cpu_perf_eax & CPUIDEAX_VERID;
		if (version == 0)
			return;
	}
#ifdef MULTIPROCESSOR
	else {
		/* if no difference on the bits we care about, say nothing */
		if (((cpu_perf_eax ^ prevcpu_perf_eax) & 0x00ffffff) == 0 &&
		    ((cpu_perf_edx ^ prevcpu_perf_edx) & 0x00001fff) == 0)
			return;
		version = cpu_perf_eax & CPUIDEAX_VERID;
	}
	prevcpu_perf_eax = cpu_perf_eax;
	prevcpu_perf_edx = cpu_perf_edx;
#endif

	printf("\n%s: cpuid a vers=%d", ci->ci_dev->dv_xname, version);
	if (version) {
		printf(", gp=%d, gpwidth=%d", CPUIDEAX_NUM_GC(cpu_perf_eax),
		    CPUIDEAX_BIT_GC(cpu_perf_eax));
		if (version > 1) {
			printf(", ff=%d, ffwidth=%d",
			    CPUIDEDX_NUM_FC(cpu_perf_edx),
			    CPUIDEDX_BIT_FC(cpu_perf_edx));
		}
	}
}

void
identifycpu(struct cpu_info *ci)
{
	static uint32_t prevcpu_1_ecx, prevcpu_tpm_ecxflags, prevcpu_d_1_eax;
	static uint32_t prevcpu_apmi_edx, prevcpu_arch_capa;
	static struct cpu_info *prevci = &cpu_info_primary;
#define CPUID_MEMBER(member)	ci->member, prevci->member
	uint32_t cflushsz, curcpu_1_ecx, curcpu_apmi_edx = 0;
	uint32_t curcpu_perf_eax = 0, curcpu_perf_edx = 0;
	uint32_t curcpu_tpm_ecxflags = 0, curcpu_d_1_eax = 0;
	uint64_t freq = 0;
	u_int32_t dummy;
	char mycpu_model[48];
	char *brandstr_from, *brandstr_to;
	int skipspace;

	CPUID(0x80000000, ci->ci_pnfeatset, dummy, dummy, dummy);
	CPUID(0x80000001, ci->ci_efeature_eax, dummy, ci->ci_efeature_ecx,
	    ci->ci_feature_eflags);

	if (CPU_IS_PRIMARY(ci)) {
		ci->ci_signature = cpu_id;
		ci->ci_feature_flags = cpu_feature & ~CPUID_NXE;
		cflushsz = cpu_ebxfeature;
		curcpu_1_ecx = cpu_ecxfeature;
		ecpu_ecxfeature = ci->ci_efeature_ecx;
	} else {
		CPUID(1, ci->ci_signature, cflushsz, curcpu_1_ecx,
		    ci->ci_feature_flags);
		/* Let cpu_feature be the common bits */
		cpu_feature &= ci->ci_feature_flags |
		    (ci->ci_feature_eflags & CPUID_NXE);
		cpu_ecxfeature &= curcpu_1_ecx;
	}
	/* cflush cacheline size is equal to bits 15-8 of ebx * 8 */
	ci->ci_cflushsz = ((cflushsz >> 8) & 0xff) * 8;

	CPUID(0x80000002, ci->ci_brand[0],
	    ci->ci_brand[1], ci->ci_brand[2], ci->ci_brand[3]);
	CPUID(0x80000003, ci->ci_brand[4],
	    ci->ci_brand[5], ci->ci_brand[6], ci->ci_brand[7]);
	CPUID(0x80000004, ci->ci_brand[8],
	    ci->ci_brand[9], ci->ci_brand[10], ci->ci_brand[11]);
	strlcpy(mycpu_model, (char *)ci->ci_brand, sizeof(mycpu_model));

	/* Remove leading, trailing and duplicated spaces from mycpu_model */
	brandstr_from = brandstr_to = mycpu_model;
	skipspace = 1;
	while (*brandstr_from != '\0') {
		if (!skipspace || *brandstr_from != ' ') {
			skipspace = 0;
			*(brandstr_to++) = *brandstr_from;
		}
		if (*brandstr_from == ' ')
			skipspace = 1;
		brandstr_from++;
	}
	if (skipspace && brandstr_to > mycpu_model)
		brandstr_to--;
	*brandstr_to = '\0';

	if (mycpu_model[0] == 0)
		strlcpy(mycpu_model, "Opteron or Athlon 64",
		    sizeof(mycpu_model));

	/* If primary cpu, fill in the global cpu_model used by sysctl */
	if (CPU_IS_PRIMARY(ci))
		strlcpy(cpu_model, mycpu_model, sizeof(cpu_model));

	ci->ci_family = (ci->ci_signature >> 8) & 0x0f;
	ci->ci_model = (ci->ci_signature >> 4) & 0x0f;
	if (ci->ci_family == 0x6 || ci->ci_family == 0xf) {
		ci->ci_family += (ci->ci_signature >> 20) & 0xff;
		ci->ci_model += ((ci->ci_signature >> 16) & 0x0f) << 4;
	}

#if NPVBUS > 0
	/* Detect hypervisors early, attach the paravirtual bus later */
	if (CPU_IS_PRIMARY(ci) && cpu_ecxfeature & CPUIDECX_HV)
		pvbus_identify();
#endif

	if (ci->ci_pnfeatset >= 0x80000007)
		CPUID(0x80000007, dummy, dummy, dummy, curcpu_apmi_edx);

	if (ci->ci_feature_flags && ci->ci_feature_flags & CPUID_TSC) {
		/* Has TSC, check if it's constant */
		if (ci->ci_vendor == CPUV_INTEL) {
			if ((ci->ci_family == 0x0f && ci->ci_model >= 0x03) ||
			    (ci->ci_family == 0x06 && ci->ci_model >= 0x0e)) {
				atomic_setbits_int(&ci->ci_flags, CPUF_CONST_TSC);
			}
		} else if (ci->ci_vendor == CPUV_VIA) {
			/* VIA */
			if (ci->ci_model >= 0x0f) {
				atomic_setbits_int(&ci->ci_flags, CPUF_CONST_TSC);
			}
		} else if (ci->ci_vendor == CPUV_AMD) {
			if (curcpu_apmi_edx & CPUIDEDX_ITSC) {
				/* Invariant TSC indicates constant TSC on AMD */
				atomic_setbits_int(&ci->ci_flags, CPUF_CONST_TSC);
			}
		}

		/* Check if it's an invariant TSC */
		if (curcpu_apmi_edx & CPUIDEDX_ITSC)
			atomic_setbits_int(&ci->ci_flags, CPUF_INVAR_TSC);

		tsc_identify(ci);
	}

	if (ci->ci_cpuid_level >= 0xa) {
		CPUID(0xa, curcpu_perf_eax, dummy, dummy, curcpu_perf_edx);

		freq = cpu_freq_ctr(ci, curcpu_perf_eax, curcpu_perf_edx);
	}
	if (freq == 0)
		freq = cpu_freq(ci);

	if (ci->ci_cpuid_level >= 0x07) {
		/* "Structured Extended Feature Flags" */
		CPUID_LEAF(0x7, 0, dummy, ci->ci_feature_sefflags_ebx,
		    ci->ci_feature_sefflags_ecx, ci->ci_feature_sefflags_edx);
		/* SEFF0ECX_OSPKE is set late on AP */
		ci->ci_feature_sefflags_ecx &= ~SEFF0ECX_OSPKE;
	}

	printf("%s: %s", ci->ci_dev->dv_xname, mycpu_model);

	if (freq != 0)
		printf(", %llu.%02llu MHz", (freq + 4999) / 1000000,
		    ((freq + 4999) / 10000) % 100);

	if (CPU_IS_PRIMARY(ci)) {
		cpuspeed = (freq + 4999) / 1000000;
		cpu_cpuspeed = cpu_amd64speed;
	}

	printf(", %02x-%02x-%02x", ci->ci_family, ci->ci_model,
	    ci->ci_signature & 0x0f);

	if ((cpu_ecxfeature & CPUIDECX_HV) == 0) {
		uint64_t level = 0;
		uint32_t dummy;

		if (ci->ci_vendor == CPUV_AMD) {
			level = rdmsr(MSR_PATCH_LEVEL);
		} else if (ci->ci_vendor == CPUV_INTEL) {
			wrmsr(MSR_BIOS_SIGN, 0);
			CPUID(1, dummy, dummy, dummy, dummy);
			level = rdmsr(MSR_BIOS_SIGN) >> 32;
		}
		if (level != 0)
			printf(", patch %08llx", level);
	}

	if (ci->ci_cpuid_level >= 0x06)
		CPUID(0x06, ci->ci_feature_tpmflags, dummy,
		    curcpu_tpm_ecxflags, dummy);
	if (ci->ci_vendor == CPUV_AMD && ci->ci_family >= 0x12)
		ci->ci_feature_tpmflags |= TPM_ARAT;

	/* xsave subfeatures */
	if (ci->ci_cpuid_level >= 0xd)
		CPUID_LEAF(0xd, 1, curcpu_d_1_eax, dummy, dummy, dummy);

	pcpuid2(ci, "1", 'd', CPUID_MEMBER(ci_feature_flags), CPUID_EDX_BITS,
	    'c', curcpu_1_ecx, prevcpu_1_ecx, CPUID_ECX_BITS);
	pcpuid2(ci, "6", 'a', CPUID_MEMBER(ci_feature_tpmflags), TPM_EAX_BITS,
	    'c', curcpu_tpm_ecxflags, prevcpu_tpm_ecxflags, TPM_ECX_BITS);
	pcpuid3(ci, "7.0",
	    'b', CPUID_MEMBER(ci_feature_sefflags_ebx), SEFF0_EBX_BITS,
	    'c', CPUID_MEMBER(ci_feature_sefflags_ecx), SEFF0_ECX_BITS,
	    'd', CPUID_MEMBER(ci_feature_sefflags_edx), SEFF0_EDX_BITS);
	print_perf_cpuid(ci, curcpu_perf_eax, curcpu_perf_edx);
	pcpuid(ci, "d.1", 'a', curcpu_d_1_eax, prevcpu_d_1_eax, XSAVE_BITS);
	pcpuid2(ci, "80000001",
	    'd', CPUID_MEMBER(ci_feature_eflags), CPUIDE_EDX_BITS,
	    'c', CPUID_MEMBER(ci_efeature_ecx), CPUIDE_ECX_BITS);
	pcpuid(ci, "80000007", 'd', curcpu_apmi_edx, prevcpu_apmi_edx,
	    CPUID_APMI_EDX_BITS);
#ifdef MULTIPROCESSOR
	prevcpu_1_ecx = curcpu_1_ecx;
	prevcpu_tpm_ecxflags = curcpu_tpm_ecxflags;
	prevcpu_d_1_eax = curcpu_d_1_eax;
	prevcpu_apmi_edx = curcpu_apmi_edx;
#endif

	/* speculation control features */
	if (ci->ci_vendor == CPUV_AMD) {
		if (ci->ci_pnfeatset >= 0x80000008) {
			CPUID(0x80000008, dummy, ci->ci_feature_amdspec_ebx,
			    dummy, dummy);
			pcpuid(ci, "80000008", 'b',
			    CPUID_MEMBER(ci_feature_amdspec_ebx),
			    CPUID_AMDSPEC_EBX_BITS);
		}
	} else if (ci->ci_vendor == CPUV_INTEL) {
		if (ci->ci_feature_sefflags_edx & SEFF0EDX_ARCH_CAP) {
			uint32_t msr = rdmsr(MSR_ARCH_CAPABILITIES);

			pmsr32(ci, MSR_ARCH_CAPABILITIES, msr,
			    prevcpu_arch_capa, ARCH_CAP_MSR_BITS);
			prevcpu_arch_capa = msr;
			if (!CPU_IS_PRIMARY(ci) && cpu_meltdown &&
			    (msr & ARCH_CAP_RDCL_NO))
				printf("\n%s: -MELTDOWN", ci->ci_dev->dv_xname);
		}
		if (cpu_meltdown && CPU_IS_PRIMARY(ci))
			printf("\n%s: MELTDOWN", ci->ci_dev->dv_xname);
	}

	/* AMD secure memory encryption and encrypted virtualization features */
	if (ci->ci_vendor == CPUV_AMD &&
	    ci->ci_pnfeatset >= CPUID_AMD_SEV_CAP) {
		CPUID(CPUID_AMD_SEV_CAP, ci->ci_feature_amdsev_eax,
		    ci->ci_feature_amdsev_ebx, ci->ci_feature_amdsev_ecx,
		    ci->ci_feature_amdsev_edx);
		pcpuid3(ci, "8000001F",
		    'a', CPUID_MEMBER(ci_feature_amdsev_eax),
		    CPUID_AMDSEV_EAX_BITS,
		    'c', CPUID_MEMBER(ci_feature_amdsev_ecx),
		    CPUID_AMDSEV_ECX_BITS,
		    'd', CPUID_MEMBER(ci_feature_amdsev_edx),
		    CPUID_AMDSEV_EDX_BITS);
		amd64_pos_cbit = (ci->ci_feature_amdsev_ebx & 0x3f);
		amd64_min_noes_asid = ci->ci_feature_amdsev_edx;
		if (cpu_sev_guestmode && CPU_IS_PRIMARY(ci))
			printf("\n%s: SEV%s guest mode", ci->ci_dev->dv_xname,
			    ISSET(cpu_sev_guestmode, SEV_STAT_ES_ENABLED) ?
			    "-ES" : "");
	}

	printf("\n");

	replacemeltdown();
	x86_print_cacheinfo(ci);

	if (CPU_IS_PRIMARY(ci)) {
#ifndef SMALL_KERNEL
		if (ci->ci_vendor == CPUV_AMD &&
		    ci->ci_pnfeatset >= 0x80000007) {
			if (curcpu_apmi_edx & 0x06) {
				if ((ci->ci_signature & 0xF00) == 0xF00)
					setperf_setup = k8_powernow_init;
			}
			if (ci->ci_family >= 0x10)
				setperf_setup = k1x_init;
		}

		if (cpu_ecxfeature & CPUIDECX_EST)
			setperf_setup = est_init;
#endif

		if (cpu_ecxfeature & CPUIDECX_RDRAND)
			has_rdrand = 1;

		if (ci->ci_feature_sefflags_ebx & SEFF0EBX_RDSEED)
			has_rdseed = 1;

		if (ci->ci_feature_sefflags_ebx & SEFF0EBX_SMAP)
			replacesmap();
	}

#ifndef SMALL_KERNEL
	if (CPU_IS_PRIMARY(ci) && (ci->ci_feature_tpmflags & TPM_SENSOR) &&
	    ci->ci_vendor == CPUV_INTEL) {
		ci->ci_sensor.type = SENSOR_TEMP;
		sensor_task_register(ci, intelcore_update_sensor, 5);
		sensor_attach(&ci->ci_sensordev, &ci->ci_sensor);
	}
#endif

	if (CPU_IS_PRIMARY(ci) && ci->ci_vendor == CPUV_VIA) {
		ci->cpu_setup = via_nano_setup;
#ifndef SMALL_KERNEL
		ci->ci_sensor.type = SENSOR_TEMP;
		sensor_task_register(ci, via_update_sensor, 5);
		sensor_attach(&ci->ci_sensordev, &ci->ci_sensor);
#endif
	}

	tsc_timecounter_init(ci, freq);

	cpu_topology(ci);
#if NVMM > 0
	cpu_check_vmm_cap(ci);
#endif /* NVMM > 0 */

	/* Check for effective frequency via MPERF, APERF */
	if ((curcpu_tpm_ecxflags & TPM_EFFFREQ) && ci->ci_smt_id == 0) {
#ifndef SMALL_KERNEL
		ci->ci_hz_sensor.type = SENSOR_FREQ;
		sensor_task_register(ci, cpu_hz_update_sensor, 1);
		sensor_attach(&ci->ci_sensordev, &ci->ci_hz_sensor);
#endif
	}
	prevci = ci;
}

#ifndef SMALL_KERNEL
/*
 * Base 2 logarithm of an int. returns 0 for 0 (yeye, I know).
 */
static int
log2(unsigned int i)
{
	int ret = 0;

	while (i >>= 1)
		ret++;

	return (ret);
}

static int
mask_width(u_int x)
{
	int bit;
	int mask;
	int powerof2;

	powerof2 = ((x - 1) & x) == 0;
	mask = (x << (1 - powerof2)) - 1;

	/* fls */
	if (mask == 0)
		return (0);
	for (bit = 1; mask != 1; bit++)
		mask = (unsigned int)mask >> 1;

	return (bit);
}
#endif

/*
 * Build up cpu topology for given cpu, must run on the core itself.
 */
void
cpu_topology(struct cpu_info *ci)
{
#ifndef SMALL_KERNEL
	u_int32_t eax, ebx, ecx, edx;
	u_int32_t apicid, max_apicid = 0, max_coreid = 0;
	u_int32_t smt_bits = 0, core_bits, pkg_bits = 0;
	u_int32_t smt_mask = 0, core_mask, pkg_mask = 0;

	/* We need at least apicid at CPUID 1 */
	if (ci->ci_cpuid_level < 1)
		goto no_topology;

	/* Initial apicid */
	CPUID(1, eax, ebx, ecx, edx);
	apicid = (ebx >> 24) & 0xff;

	if (ci->ci_vendor == CPUV_AMD) {
		uint32_t nthreads = 1; /* per core */
		uint32_t thread_id; /* within a package */

		/* We need at least apicid at CPUID 0x80000008 */
		if (ci->ci_pnfeatset < 0x80000008)
			goto no_topology;

		CPUID(0x80000008, eax, ebx, ecx, edx);
		core_bits = (ecx >> 12) & 0xf;

		if (ci->ci_pnfeatset >= 0x8000001e) {
			CPUID(0x8000001e, eax, ebx, ecx, edx);
			nthreads = ((ebx >> 8) & 0xf) + 1;
		}

		/* Shift the core_bits off to get at the pkg bits */
		ci->ci_pkg_id = apicid >> core_bits;

		/* Get rid of the package bits */
		core_mask = (1U << core_bits) - 1;
		thread_id = apicid & core_mask;

		/* Cut logical thread_id into core id, and smt id in a core */
		ci->ci_core_id = thread_id / nthreads;
		ci->ci_smt_id = thread_id % nthreads;
	} else if (ci->ci_vendor == CPUV_INTEL) {
		/* We only support leaf 1/4 detection */
		if (ci->ci_cpuid_level < 4)
			goto no_topology;
		/* Get max_apicid */
		CPUID(1, eax, ebx, ecx, edx);
		max_apicid = (ebx >> 16) & 0xff;
		/* Get max_coreid */
		CPUID_LEAF(4, 0, eax, ebx, ecx, edx);
		max_coreid = ((eax >> 26) & 0x3f) + 1;
		/* SMT */
		smt_bits = mask_width(max_apicid / max_coreid);
		smt_mask = (1U << smt_bits) - 1;
		/* Core */
		core_bits = log2(max_coreid);
		core_mask = (1U << (core_bits + smt_bits)) - 1;
		core_mask ^= smt_mask;
		/* Pkg */
		pkg_bits = core_bits + smt_bits;
		pkg_mask = ~0U << core_bits;

		ci->ci_smt_id = apicid & smt_mask;
		ci->ci_core_id = (apicid & core_mask) >> smt_bits;
		ci->ci_pkg_id = (apicid & pkg_mask) >> pkg_bits;
	} else
		goto no_topology;
#ifdef DEBUG
	printf("cpu%d: smt %u, core %u, pkg %u "
		"(apicid 0x%x, max_apicid 0x%x, max_coreid 0x%x, smt_bits 0x%x, smt_mask 0x%x, "
		"core_bits 0x%x, core_mask 0x%x, pkg_bits 0x%x, pkg_mask 0x%x)\n",
		ci->ci_cpuid, ci->ci_smt_id, ci->ci_core_id, ci->ci_pkg_id,
		apicid, max_apicid, max_coreid, smt_bits, smt_mask, core_bits,
		core_mask, pkg_bits, pkg_mask);
#else
	printf("cpu%d: smt %u, core %u, package %u\n", ci->ci_cpuid,
		ci->ci_smt_id, ci->ci_core_id, ci->ci_pkg_id);

#endif
	return;
	/* We can't map, so consider ci_core_id as ci_cpuid */
no_topology:
#endif
	ci->ci_smt_id  = 0;
	ci->ci_core_id = ci->ci_cpuid;
	ci->ci_pkg_id  = 0;
}

#if NVMM > 0
/*
 * cpu_check_vmm_cap
 *
 * Checks for VMM capabilities for 'ci'. Initializes certain per-cpu VMM
 * state in 'ci' if virtualization extensions are found.
 *
 * Parameters:
 *  ci: the cpu being checked
 */
void
cpu_check_vmm_cap(struct cpu_info *ci)
{
	uint64_t msr;
	uint32_t cap, dummy, edx;

	/*
	 * Check for workable VMX
	 */
	if (cpu_ecxfeature & CPUIDECX_VMX) {
		msr = rdmsr(MSR_IA32_FEATURE_CONTROL);

		if (!(msr & IA32_FEATURE_CONTROL_LOCK))
			ci->ci_vmm_flags |= CI_VMM_VMX;
		else {
			if (msr & IA32_FEATURE_CONTROL_VMX_EN)
				ci->ci_vmm_flags |= CI_VMM_VMX;
			else
				ci->ci_vmm_flags |= CI_VMM_DIS;
		}
	}

	/*
	 * Check for EPT (Intel Nested Paging) and other secondary
	 * controls
	 */
	if (ci->ci_vmm_flags & CI_VMM_VMX) {
		/* Secondary controls available? */
		/* XXX should we check true procbased ctls here if avail? */
		msr = rdmsr(IA32_VMX_PROCBASED_CTLS);
		if (msr & (IA32_VMX_ACTIVATE_SECONDARY_CONTROLS) << 32) {
			msr = rdmsr(IA32_VMX_PROCBASED2_CTLS);
			/* EPT and UG available? */
			if ((msr & (IA32_VMX_ENABLE_EPT) << 32) &&
			    (msr & (IA32_VMX_UNRESTRICTED_GUEST) << 32))
				ci->ci_vmm_flags |= CI_VMM_EPT;
		}
	}

	/*
	 * Check startup config (VMX)
	 */
	if (ci->ci_vmm_flags & CI_VMM_VMX) {
		/* CR0 fixed and flexible bits */
		msr = rdmsr(IA32_VMX_CR0_FIXED0);
		ci->ci_vmm_cap.vcc_vmx.vmx_cr0_fixed0 = msr;
		msr = rdmsr(IA32_VMX_CR0_FIXED1);
		ci->ci_vmm_cap.vcc_vmx.vmx_cr0_fixed1 = msr;

		/* CR4 fixed and flexible bits */
		msr = rdmsr(IA32_VMX_CR4_FIXED0);
		ci->ci_vmm_cap.vcc_vmx.vmx_cr4_fixed0 = msr;
		msr = rdmsr(IA32_VMX_CR4_FIXED1);
		ci->ci_vmm_cap.vcc_vmx.vmx_cr4_fixed1 = msr;

		/* VMXON region revision ID (bits 30:0 of IA32_VMX_BASIC) */
		msr = rdmsr(IA32_VMX_BASIC);
		ci->ci_vmm_cap.vcc_vmx.vmx_vmxon_revision =
			(uint32_t)(msr & 0x7FFFFFFF);

		/* MSR save / load table size */
		msr = rdmsr(IA32_VMX_MISC);
		ci->ci_vmm_cap.vcc_vmx.vmx_msr_table_size =
			(uint32_t)(msr & IA32_VMX_MSR_LIST_SIZE_MASK) >> 25;

		/* CR3 target count size */
		ci->ci_vmm_cap.vcc_vmx.vmx_cr3_tgt_count =
			(uint32_t)(msr & IA32_VMX_CR3_TGT_SIZE_MASK) >> 16;
	}

	/*
	 * Check for workable SVM
	 */
	if (ecpu_ecxfeature & CPUIDECX_SVM) {
		msr = rdmsr(MSR_AMD_VM_CR);

		if (!(msr & AMD_SVMDIS))
			ci->ci_vmm_flags |= CI_VMM_SVM;

		CPUID(CPUID_AMD_SVM_CAP, dummy,
		    ci->ci_vmm_cap.vcc_svm.svm_max_asid, dummy, edx);

		if (ci->ci_vmm_cap.vcc_svm.svm_max_asid > 0xFFF)
			ci->ci_vmm_cap.vcc_svm.svm_max_asid = 0xFFF;

		if (edx & AMD_SVM_FLUSH_BY_ASID_CAP)
			ci->ci_vmm_cap.vcc_svm.svm_flush_by_asid = 1;

		if (edx & AMD_SVM_VMCB_CLEAN_CAP)
			ci->ci_vmm_cap.vcc_svm.svm_vmcb_clean = 1;

		if (edx & AMD_SVM_DECODE_ASSIST_CAP)
			ci->ci_vmm_cap.vcc_svm.svm_decode_assist = 1;
	}

	/*
	 * Check for SVM Nested Paging
	 */
	if ((ci->ci_vmm_flags & CI_VMM_SVM) &&
	    ci->ci_pnfeatset >= CPUID_AMD_SVM_CAP) {
		CPUID(CPUID_AMD_SVM_CAP, dummy, dummy, dummy, cap);
		if (cap & AMD_SVM_NESTED_PAGING_CAP)
			ci->ci_vmm_flags |= CI_VMM_RVI;
	}

	/*
	 * Check "L1 flush on VM entry" (Intel L1TF vuln) semantics
	 * Full details can be found here:
	 * https://software.intel.com/security-software-guidance/insights/deep-dive-intel-analysis-l1-terminal-fault
	 */
	if (ci->ci_vendor == CPUV_INTEL) {
		if (ci->ci_feature_sefflags_edx & SEFF0EDX_L1DF)
			ci->ci_vmm_cap.vcc_vmx.vmx_has_l1_flush_msr = 1;
		else
			ci->ci_vmm_cap.vcc_vmx.vmx_has_l1_flush_msr = 0;

		/*
		 * Certain CPUs may have the vulnerability remedied in
		 * hardware (RDCL_NO), or we may be nested in an VMM that
		 * is doing flushes (SKIP_L1DFL_VMENTRY) using the MSR.
		 * In either case no mitigation at all is necessary.
		 */
		if (ci->ci_feature_sefflags_edx & SEFF0EDX_ARCH_CAP) {
			msr = rdmsr(MSR_ARCH_CAPABILITIES);
			if ((msr & ARCH_CAP_RDCL_NO) ||
			    ((msr & ARCH_CAP_SKIP_L1DFL_VMENTRY) &&
			    ci->ci_vmm_cap.vcc_vmx.vmx_has_l1_flush_msr))
				ci->ci_vmm_cap.vcc_vmx.vmx_has_l1_flush_msr =
				    VMX_SKIP_L1D_FLUSH;
		}
	}
}
#endif /* NVMM > 0 */
