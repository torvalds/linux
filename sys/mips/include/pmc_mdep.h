/*-
 * This file is in the public domain.
 *
 *	from: src/sys/alpha/include/pmc_mdep.h,v 1.2 2005/06/09 19:45:06 jkoshy
 * $FreeBSD$
 */

#ifndef _MACHINE_PMC_MDEP_H_
#define	_MACHINE_PMC_MDEP_H_

#define	PMC_MDEP_CLASS_INDEX_MIPS	1

union pmc_md_op_pmcallocate {
	uint64_t	__pad[4];
};

/* Logging */
#if defined(__mips_n64)
#define	PMCLOG_READADDR		PMCLOG_READ64
#define	PMCLOG_EMITADDR		PMCLOG_EMIT64
#else
#define	PMCLOG_READADDR		PMCLOG_READ32
#define	PMCLOG_EMITADDR		PMCLOG_EMIT32
#endif

#if	_KERNEL

/*
 * MIPS event codes are encoded with a select bit.  The
 * select bit is used when writing to CP0 so that we 
 * can select either counter 0/2 or 1/3.  The cycle
 * and instruction counters are special in that they
 * can be counted on either 0/2 or 1/3.
 */

#define MIPS_CTR_ALL	255 /* Count events in any counter. */
#define MIPS_CTR_0	0 /* Counter 0 Event */
#define MIPS_CTR_1	1 /* Counter 1 Event */

struct mips_event_code_map {
	uint32_t	pe_ev;       /* enum value */
	uint8_t         pe_counter;  /* Which counter this can be counted in. */
	uint8_t		pe_code;     /* numeric code */
};

struct mips_pmc_spec {
	uint32_t	ps_cpuclass;
	uint32_t	ps_cputype;
	uint32_t	ps_capabilities;
	int		ps_counter_width;
};

union pmc_md_pmc {
	uint32_t	pm_mips_evsel;
};

#define	PMC_TRAPFRAME_TO_PC(TF)	((TF)->pc)

extern const struct mips_event_code_map mips_event_codes[];
extern const int mips_event_codes_size;
extern int mips_npmcs;
extern struct mips_pmc_spec mips_pmc_spec;

/*
 * Prototypes
 */
struct pmc_mdep *pmc_mips_initialize(void);
void		pmc_mips_finalize(struct pmc_mdep *_md);

/*
 * CPU-specific functions
 */

uint32_t	mips_get_perfctl(int cpu, int ri, uint32_t event, uint32_t caps);
uint64_t	mips_pmcn_read(unsigned int pmc);
uint64_t	mips_pmcn_write(unsigned int pmc, uint64_t v);

#endif /* _KERNEL */

#endif /* !_MACHINE_PMC_MDEP_H_ */
