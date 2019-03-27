/*-
 * This file is in the public domain.
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_PMC_MDEP_H_
#define	_MACHINE_PMC_MDEP_H_

#define PMC_MDEP_CLASS_INDEX_POWERPC	1

union pmc_md_op_pmcallocate {
	uint64_t		__pad[4];
};

/* Logging */
#define	PMCLOG_READADDR		PMCLOG_READ32
#define	PMCLOG_EMITADDR		PMCLOG_EMIT32

#define	mtpmr(reg, val)							\
	__asm __volatile("mtpmr %0,%1" : : "K"(reg), "r"(val))
#define	mfpmr(reg)							\
	( { register_t val;						\
	  __asm __volatile("mfpmr %0,%1" : "=r"(val) : "K"(reg));	\
	  val; } )

#define	PMR_PMC0	16
#define	PMR_PMC1	17
#define	PMR_PMC2	18
#define	PMR_PMC3	19
#define	PMR_PMLCa0	144
#define	  PMLCax_FC	  0x80000000
#define	  PMLCax_FCS	  0x40000000
#define	  PMLCax_FCU	  0x20000000
#define	  PMLCax_FCM1	  0x10000000
#define	  PMLCax_FCM0	  0x08000000
#define	  PMLCax_CE	  0x04000000
#define	  PMLCax_EVENT(x) ((x) << 16)
#define	  PMLCax_FCGS1	  0x00000002
#define	  PMLCax_FCGS0	  0x00000001
#define	PMR_PMLCa1	145
#define	PMR_PMLCa2	146
#define	PMR_PMLCa3	147
#define	PMR_PMLCb0	272
#define	  PMLCbx_TRIGONCTL(x)	  ((x) << 28)
#define	  PMLCbx_TRIGOFFCTL(x)	  ((x) << 24)
#define	  PMLCbx_PMCC		  0x00800000
#define	  PMLCbx_PMP(x)		  ((x) << 13)
#define	  PMLCbx_TREHMUL(x)	  ((x) << 8)
#define	  PMLCbx_TRESHOLD(x)	  ((x) << 0)
#define	PMR_PMLCb1	273
#define	PMR_PMLCb2	274
#define	PMR_PMLCb3	275
#define	PMR_PMGC0	400
#define	  PMGC_FAC	  0x80000000
#define	  PMGC_PMIE	  0x40000000
#define	  PMGC_FCECE	  0x20000000
#define	  PMGC_TBSEL(x)	  ((x) << 11)
#define	  PMGC_TBEE	  0x00000100
#define	PMR_UPMC0	0
#define	PMR_UPMC1	1
#define	PMR_UPMC2	2
#define	PMR_UPMC3	3
#define	PMR_UPMLCa0	128
#define	PMR_UPMLCa1	129
#define	PMR_UPMLCa2	130
#define	PMR_UPMLCa3	131
#define	PMR_UPMLCb0	256
#define	PMR_UPMLCb1	257
#define	PMR_UPMLCb2	258
#define	PMR_UPMLCb3	259
#define	PMR_UPMGC0	384

#if	_KERNEL

struct pmc_md_powerpc_pmc {
	uint32_t	pm_powerpc_evsel;
};

union pmc_md_pmc {
	struct pmc_md_powerpc_pmc	pm_powerpc;
};

#define	PMC_TRAPFRAME_TO_PC(TF)	((TF)->srr0)
#define	PMC_TRAPFRAME_TO_FP(TF)	((TF)->fixreg[1])
#define	PMC_TRAPFRAME_TO_SP(TF)	(0)

#endif

#endif /* !_MACHINE_PMC_MDEP_H_ */
