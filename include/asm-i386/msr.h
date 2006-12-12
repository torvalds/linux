#ifndef __ASM_MSR_H
#define __ASM_MSR_H

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else

/*
 * Access to machine-specific registers (available on 586 and better only)
 * Note: the rd* operations modify the parameters directly (without using
 * pointer indirection), this allows gcc to optimize better
 */

#define rdmsr(msr,val1,val2) \
	__asm__ __volatile__("rdmsr" \
			  : "=a" (val1), "=d" (val2) \
			  : "c" (msr))

#define wrmsr(msr,val1,val2) \
	__asm__ __volatile__("wrmsr" \
			  : /* no outputs */ \
			  : "c" (msr), "a" (val1), "d" (val2))

#define rdmsrl(msr,val) do { \
	unsigned long l__,h__; \
	rdmsr (msr, l__, h__);  \
	val = l__;  \
	val |= ((u64)h__<<32);  \
} while(0)

static inline void wrmsrl (unsigned long msr, unsigned long long val)
{
	unsigned long lo, hi;
	lo = (unsigned long) val;
	hi = val >> 32;
	wrmsr (msr, lo, hi);
}

/* wrmsr with exception handling */
#define wrmsr_safe(msr,a,b) ({ int ret__;						\
	asm volatile("2: wrmsr ; xorl %0,%0\n"						\
		     "1:\n\t"								\
		     ".section .fixup,\"ax\"\n\t"					\
		     "3:  movl %4,%0 ; jmp 1b\n\t"					\
		     ".previous\n\t"							\
 		     ".section __ex_table,\"a\"\n"					\
		     "   .align 4\n\t"							\
		     "   .long 	2b,3b\n\t"						\
		     ".previous"							\
		     : "=a" (ret__)							\
		     : "c" (msr), "0" (a), "d" (b), "i" (-EFAULT));\
	ret__; })

/* rdmsr with exception handling */
#define rdmsr_safe(msr,a,b) ({ int ret__;						\
	asm volatile("2: rdmsr ; xorl %0,%0\n"						\
		     "1:\n\t"								\
		     ".section .fixup,\"ax\"\n\t"					\
		     "3:  movl %4,%0 ; jmp 1b\n\t"					\
		     ".previous\n\t"							\
 		     ".section __ex_table,\"a\"\n"					\
		     "   .align 4\n\t"							\
		     "   .long 	2b,3b\n\t"						\
		     ".previous"							\
		     : "=r" (ret__), "=a" (*(a)), "=d" (*(b))				\
		     : "c" (msr), "i" (-EFAULT));\
	ret__; })

#define rdtsc(low,high) \
     __asm__ __volatile__("rdtsc" : "=a" (low), "=d" (high))

#define rdtscl(low) \
     __asm__ __volatile__("rdtsc" : "=a" (low) : : "edx")

#define rdtscll(val) \
     __asm__ __volatile__("rdtsc" : "=A" (val))

#define write_tsc(val1,val2) wrmsr(0x10, val1, val2)

#define rdpmc(counter,low,high) \
     __asm__ __volatile__("rdpmc" \
			  : "=a" (low), "=d" (high) \
			  : "c" (counter))
#endif	/* !CONFIG_PARAVIRT */

/* symbolic names for some interesting MSRs */
/* Intel defined MSRs. */
#define MSR_IA32_P5_MC_ADDR		0
#define MSR_IA32_P5_MC_TYPE		1
#define MSR_IA32_PLATFORM_ID		0x17
#define MSR_IA32_EBL_CR_POWERON		0x2a

#define MSR_IA32_APICBASE		0x1b
#define MSR_IA32_APICBASE_BSP		(1<<8)
#define MSR_IA32_APICBASE_ENABLE	(1<<11)
#define MSR_IA32_APICBASE_BASE		(0xfffff<<12)

#define MSR_IA32_UCODE_WRITE		0x79
#define MSR_IA32_UCODE_REV		0x8b

#define MSR_P6_PERFCTR0		0xc1
#define MSR_P6_PERFCTR1		0xc2
#define MSR_FSB_FREQ		0xcd


#define MSR_IA32_BBL_CR_CTL		0x119

#define MSR_IA32_SYSENTER_CS		0x174
#define MSR_IA32_SYSENTER_ESP		0x175
#define MSR_IA32_SYSENTER_EIP		0x176

#define MSR_IA32_MCG_CAP		0x179
#define MSR_IA32_MCG_STATUS		0x17a
#define MSR_IA32_MCG_CTL		0x17b

/* P4/Xeon+ specific */
#define MSR_IA32_MCG_EAX		0x180
#define MSR_IA32_MCG_EBX		0x181
#define MSR_IA32_MCG_ECX		0x182
#define MSR_IA32_MCG_EDX		0x183
#define MSR_IA32_MCG_ESI		0x184
#define MSR_IA32_MCG_EDI		0x185
#define MSR_IA32_MCG_EBP		0x186
#define MSR_IA32_MCG_ESP		0x187
#define MSR_IA32_MCG_EFLAGS		0x188
#define MSR_IA32_MCG_EIP		0x189
#define MSR_IA32_MCG_RESERVED		0x18A

#define MSR_P6_EVNTSEL0			0x186
#define MSR_P6_EVNTSEL1			0x187

#define MSR_IA32_PERF_STATUS		0x198
#define MSR_IA32_PERF_CTL		0x199

#define MSR_IA32_MPERF			0xE7
#define MSR_IA32_APERF			0xE8

#define MSR_IA32_THERM_CONTROL		0x19a
#define MSR_IA32_THERM_INTERRUPT	0x19b
#define MSR_IA32_THERM_STATUS		0x19c
#define MSR_IA32_MISC_ENABLE		0x1a0

#define MSR_IA32_DEBUGCTLMSR		0x1d9
#define MSR_IA32_LASTBRANCHFROMIP	0x1db
#define MSR_IA32_LASTBRANCHTOIP		0x1dc
#define MSR_IA32_LASTINTFROMIP		0x1dd
#define MSR_IA32_LASTINTTOIP		0x1de

#define MSR_IA32_MC0_CTL		0x400
#define MSR_IA32_MC0_STATUS		0x401
#define MSR_IA32_MC0_ADDR		0x402
#define MSR_IA32_MC0_MISC		0x403

#define MSR_IA32_PEBS_ENABLE		0x3f1
#define MSR_IA32_DS_AREA		0x600
#define MSR_IA32_PERF_CAPABILITIES	0x345

/* Pentium IV performance counter MSRs */
#define MSR_P4_BPU_PERFCTR0 		0x300
#define MSR_P4_BPU_PERFCTR1 		0x301
#define MSR_P4_BPU_PERFCTR2 		0x302
#define MSR_P4_BPU_PERFCTR3 		0x303
#define MSR_P4_MS_PERFCTR0 		0x304
#define MSR_P4_MS_PERFCTR1 		0x305
#define MSR_P4_MS_PERFCTR2 		0x306
#define MSR_P4_MS_PERFCTR3 		0x307
#define MSR_P4_FLAME_PERFCTR0 		0x308
#define MSR_P4_FLAME_PERFCTR1 		0x309
#define MSR_P4_FLAME_PERFCTR2 		0x30a
#define MSR_P4_FLAME_PERFCTR3 		0x30b
#define MSR_P4_IQ_PERFCTR0 		0x30c
#define MSR_P4_IQ_PERFCTR1 		0x30d
#define MSR_P4_IQ_PERFCTR2 		0x30e
#define MSR_P4_IQ_PERFCTR3 		0x30f
#define MSR_P4_IQ_PERFCTR4 		0x310
#define MSR_P4_IQ_PERFCTR5 		0x311
#define MSR_P4_BPU_CCCR0 		0x360
#define MSR_P4_BPU_CCCR1 		0x361
#define MSR_P4_BPU_CCCR2 		0x362
#define MSR_P4_BPU_CCCR3 		0x363
#define MSR_P4_MS_CCCR0 		0x364
#define MSR_P4_MS_CCCR1 		0x365
#define MSR_P4_MS_CCCR2 		0x366
#define MSR_P4_MS_CCCR3 		0x367
#define MSR_P4_FLAME_CCCR0 		0x368
#define MSR_P4_FLAME_CCCR1 		0x369
#define MSR_P4_FLAME_CCCR2 		0x36a
#define MSR_P4_FLAME_CCCR3 		0x36b
#define MSR_P4_IQ_CCCR0 		0x36c
#define MSR_P4_IQ_CCCR1 		0x36d
#define MSR_P4_IQ_CCCR2 		0x36e
#define MSR_P4_IQ_CCCR3 		0x36f
#define MSR_P4_IQ_CCCR4 		0x370
#define MSR_P4_IQ_CCCR5 		0x371
#define MSR_P4_ALF_ESCR0 		0x3ca
#define MSR_P4_ALF_ESCR1 		0x3cb
#define MSR_P4_BPU_ESCR0 		0x3b2
#define MSR_P4_BPU_ESCR1 		0x3b3
#define MSR_P4_BSU_ESCR0 		0x3a0
#define MSR_P4_BSU_ESCR1 		0x3a1
#define MSR_P4_CRU_ESCR0 		0x3b8
#define MSR_P4_CRU_ESCR1 		0x3b9
#define MSR_P4_CRU_ESCR2 		0x3cc
#define MSR_P4_CRU_ESCR3 		0x3cd
#define MSR_P4_CRU_ESCR4 		0x3e0
#define MSR_P4_CRU_ESCR5 		0x3e1
#define MSR_P4_DAC_ESCR0 		0x3a8
#define MSR_P4_DAC_ESCR1 		0x3a9
#define MSR_P4_FIRM_ESCR0 		0x3a4
#define MSR_P4_FIRM_ESCR1 		0x3a5
#define MSR_P4_FLAME_ESCR0 		0x3a6
#define MSR_P4_FLAME_ESCR1 		0x3a7
#define MSR_P4_FSB_ESCR0 		0x3a2
#define MSR_P4_FSB_ESCR1 		0x3a3
#define MSR_P4_IQ_ESCR0 		0x3ba
#define MSR_P4_IQ_ESCR1 		0x3bb
#define MSR_P4_IS_ESCR0 		0x3b4
#define MSR_P4_IS_ESCR1 		0x3b5
#define MSR_P4_ITLB_ESCR0 		0x3b6
#define MSR_P4_ITLB_ESCR1 		0x3b7
#define MSR_P4_IX_ESCR0 		0x3c8
#define MSR_P4_IX_ESCR1 		0x3c9
#define MSR_P4_MOB_ESCR0 		0x3aa
#define MSR_P4_MOB_ESCR1 		0x3ab
#define MSR_P4_MS_ESCR0 		0x3c0
#define MSR_P4_MS_ESCR1 		0x3c1
#define MSR_P4_PMH_ESCR0 		0x3ac
#define MSR_P4_PMH_ESCR1 		0x3ad
#define MSR_P4_RAT_ESCR0 		0x3bc
#define MSR_P4_RAT_ESCR1 		0x3bd
#define MSR_P4_SAAT_ESCR0 		0x3ae
#define MSR_P4_SAAT_ESCR1 		0x3af
#define MSR_P4_SSU_ESCR0 		0x3be
#define MSR_P4_SSU_ESCR1 		0x3bf    /* guess: not defined in manual */
#define MSR_P4_TBPU_ESCR0 		0x3c2
#define MSR_P4_TBPU_ESCR1 		0x3c3
#define MSR_P4_TC_ESCR0 		0x3c4
#define MSR_P4_TC_ESCR1 		0x3c5
#define MSR_P4_U2L_ESCR0 		0x3b0
#define MSR_P4_U2L_ESCR1 		0x3b1

/* AMD Defined MSRs */
#define MSR_K6_EFER			0xC0000080
#define MSR_K6_STAR			0xC0000081
#define MSR_K6_WHCR			0xC0000082
#define MSR_K6_UWCCR			0xC0000085
#define MSR_K6_EPMR			0xC0000086
#define MSR_K6_PSOR			0xC0000087
#define MSR_K6_PFIR			0xC0000088

#define MSR_K7_EVNTSEL0			0xC0010000
#define MSR_K7_EVNTSEL1			0xC0010001
#define MSR_K7_EVNTSEL2			0xC0010002
#define MSR_K7_EVNTSEL3			0xC0010003
#define MSR_K7_PERFCTR0			0xC0010004
#define MSR_K7_PERFCTR1			0xC0010005
#define MSR_K7_PERFCTR2			0xC0010006
#define MSR_K7_PERFCTR3			0xC0010007
#define MSR_K7_HWCR			0xC0010015
#define MSR_K7_CLK_CTL			0xC001001b
#define MSR_K7_FID_VID_CTL		0xC0010041
#define MSR_K7_FID_VID_STATUS		0xC0010042

/* extended feature register */
#define MSR_EFER 			0xc0000080

/* EFER bits: */

/* Execute Disable enable */
#define _EFER_NX			11
#define EFER_NX				(1<<_EFER_NX)

/* Centaur-Hauls/IDT defined MSRs. */
#define MSR_IDT_FCR1			0x107
#define MSR_IDT_FCR2			0x108
#define MSR_IDT_FCR3			0x109
#define MSR_IDT_FCR4			0x10a

#define MSR_IDT_MCR0			0x110
#define MSR_IDT_MCR1			0x111
#define MSR_IDT_MCR2			0x112
#define MSR_IDT_MCR3			0x113
#define MSR_IDT_MCR4			0x114
#define MSR_IDT_MCR5			0x115
#define MSR_IDT_MCR6			0x116
#define MSR_IDT_MCR7			0x117
#define MSR_IDT_MCR_CTRL		0x120

/* VIA Cyrix defined MSRs*/
#define MSR_VIA_FCR			0x1107
#define MSR_VIA_LONGHAUL		0x110a
#define MSR_VIA_RNG			0x110b
#define MSR_VIA_BCR2			0x1147

/* Transmeta defined MSRs */
#define MSR_TMTA_LONGRUN_CTRL		0x80868010
#define MSR_TMTA_LONGRUN_FLAGS		0x80868011
#define MSR_TMTA_LRTI_READOUT		0x80868018
#define MSR_TMTA_LRTI_VOLT_MHZ		0x8086801a

/* Intel Core-based CPU performance counters */
#define MSR_CORE_PERF_FIXED_CTR0	0x309
#define MSR_CORE_PERF_FIXED_CTR1	0x30a
#define MSR_CORE_PERF_FIXED_CTR2	0x30b
#define MSR_CORE_PERF_FIXED_CTR_CTRL	0x38d
#define MSR_CORE_PERF_GLOBAL_STATUS	0x38e
#define MSR_CORE_PERF_GLOBAL_CTRL	0x38f
#define MSR_CORE_PERF_GLOBAL_OVF_CTRL	0x390

#endif /* __ASM_MSR_H */
