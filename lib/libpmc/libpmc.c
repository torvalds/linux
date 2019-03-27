/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2008 Joseph Koshy
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/module.h>
#include <sys/pmc.h>
#include <sys/syscall.h>

#include <ctype.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <pmc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sysexits.h>
#include <unistd.h>

#include "libpmcinternal.h"

/* Function prototypes */
#if defined(__amd64__) || defined(__i386__)
static int k8_allocate_pmc(enum pmc_event _pe, char *_ctrspec,
    struct pmc_op_pmcallocate *_pmc_config);
#endif
#if defined(__amd64__) || defined(__i386__)
static int tsc_allocate_pmc(enum pmc_event _pe, char *_ctrspec,
    struct pmc_op_pmcallocate *_pmc_config);
#endif
#if defined(__arm__)
#if defined(__XSCALE__)
static int xscale_allocate_pmc(enum pmc_event _pe, char *_ctrspec,
    struct pmc_op_pmcallocate *_pmc_config);
#endif
static int armv7_allocate_pmc(enum pmc_event _pe, char *_ctrspec,
    struct pmc_op_pmcallocate *_pmc_config);
#endif
#if defined(__aarch64__)
static int arm64_allocate_pmc(enum pmc_event _pe, char *_ctrspec,
    struct pmc_op_pmcallocate *_pmc_config);
#endif
#if defined(__mips__)
static int mips_allocate_pmc(enum pmc_event _pe, char* ctrspec,
			     struct pmc_op_pmcallocate *_pmc_config);
#endif /* __mips__ */
static int soft_allocate_pmc(enum pmc_event _pe, char *_ctrspec,
    struct pmc_op_pmcallocate *_pmc_config);

#if defined(__powerpc__)
static int powerpc_allocate_pmc(enum pmc_event _pe, char* ctrspec,
			     struct pmc_op_pmcallocate *_pmc_config);
#endif /* __powerpc__ */

#define PMC_CALL(cmd, params)				\
	syscall(pmc_syscall, PMC_OP_##cmd, (params))

/*
 * Event aliases provide a way for the user to ask for generic events
 * like "cache-misses", or "instructions-retired".  These aliases are
 * mapped to the appropriate canonical event descriptions using a
 * lookup table.
 */
struct pmc_event_alias {
	const char	*pm_alias;
	const char	*pm_spec;
};

static const struct pmc_event_alias *pmc_mdep_event_aliases;

/*
 * The pmc_event_descr structure maps symbolic names known to the user
 * to integer codes used by the PMC KLD.
 */
struct pmc_event_descr {
	const char	*pm_ev_name;
	enum pmc_event	pm_ev_code;
};

/*
 * The pmc_class_descr structure maps class name prefixes for
 * event names to event tables and other PMC class data.
 */
struct pmc_class_descr {
	const char	*pm_evc_name;
	size_t		pm_evc_name_size;
	enum pmc_class	pm_evc_class;
	const struct pmc_event_descr *pm_evc_event_table;
	size_t		pm_evc_event_table_size;
	int		(*pm_evc_allocate_pmc)(enum pmc_event _pe,
			    char *_ctrspec, struct pmc_op_pmcallocate *_pa);
};

#define	PMC_TABLE_SIZE(N)	(sizeof(N)/sizeof(N[0]))
#define	PMC_EVENT_TABLE_SIZE(N)	PMC_TABLE_SIZE(N##_event_table)

#undef	__PMC_EV
#define	__PMC_EV(C,N) { #N, PMC_EV_ ## C ## _ ## N },

/*
 * PMC_CLASSDEP_TABLE(NAME, CLASS)
 *
 * Define a table mapping event names and aliases to HWPMC event IDs.
 */
#define	PMC_CLASSDEP_TABLE(N, C)				\
	static const struct pmc_event_descr N##_event_table[] =	\
	{							\
		__PMC_EV_##C()					\
	}

PMC_CLASSDEP_TABLE(iaf, IAF);
PMC_CLASSDEP_TABLE(k8, K8);
PMC_CLASSDEP_TABLE(xscale, XSCALE);
PMC_CLASSDEP_TABLE(armv7, ARMV7);
PMC_CLASSDEP_TABLE(armv8, ARMV8);
PMC_CLASSDEP_TABLE(mips24k, MIPS24K);
PMC_CLASSDEP_TABLE(mips74k, MIPS74K);
PMC_CLASSDEP_TABLE(octeon, OCTEON);
PMC_CLASSDEP_TABLE(ppc7450, PPC7450);
PMC_CLASSDEP_TABLE(ppc970, PPC970);
PMC_CLASSDEP_TABLE(e500, E500);

static struct pmc_event_descr soft_event_table[PMC_EV_DYN_COUNT];

#undef	__PMC_EV_ALIAS
#define	__PMC_EV_ALIAS(N,CODE) 	{ N, PMC_EV_##CODE },

static const struct pmc_event_descr cortex_a8_event_table[] = 
{
	__PMC_EV_ALIAS_ARMV7_CORTEX_A8()
};

static const struct pmc_event_descr cortex_a9_event_table[] = 
{
	__PMC_EV_ALIAS_ARMV7_CORTEX_A9()
};

static const struct pmc_event_descr cortex_a53_event_table[] = 
{
	__PMC_EV_ALIAS_ARMV8_CORTEX_A53()
};

static const struct pmc_event_descr cortex_a57_event_table[] = 
{
	__PMC_EV_ALIAS_ARMV8_CORTEX_A57()
};

/*
 * PMC_MDEP_TABLE(NAME, PRIMARYCLASS, ADDITIONAL_CLASSES...)
 *
 * Map a CPU to the PMC classes it supports.
 */
#define	PMC_MDEP_TABLE(N,C,...)				\
	static const enum pmc_class N##_pmc_classes[] = {	\
		PMC_CLASS_##C, __VA_ARGS__			\
	}

PMC_MDEP_TABLE(k8, K8, PMC_CLASS_SOFT, PMC_CLASS_TSC);
PMC_MDEP_TABLE(xscale, XSCALE, PMC_CLASS_SOFT, PMC_CLASS_XSCALE);
PMC_MDEP_TABLE(cortex_a8, ARMV7, PMC_CLASS_SOFT, PMC_CLASS_ARMV7);
PMC_MDEP_TABLE(cortex_a9, ARMV7, PMC_CLASS_SOFT, PMC_CLASS_ARMV7);
PMC_MDEP_TABLE(cortex_a53, ARMV8, PMC_CLASS_SOFT, PMC_CLASS_ARMV8);
PMC_MDEP_TABLE(cortex_a57, ARMV8, PMC_CLASS_SOFT, PMC_CLASS_ARMV8);
PMC_MDEP_TABLE(mips24k, MIPS24K, PMC_CLASS_SOFT, PMC_CLASS_MIPS24K);
PMC_MDEP_TABLE(mips74k, MIPS74K, PMC_CLASS_SOFT, PMC_CLASS_MIPS74K);
PMC_MDEP_TABLE(octeon, OCTEON, PMC_CLASS_SOFT, PMC_CLASS_OCTEON);
PMC_MDEP_TABLE(ppc7450, PPC7450, PMC_CLASS_SOFT, PMC_CLASS_PPC7450, PMC_CLASS_TSC);
PMC_MDEP_TABLE(ppc970, PPC970, PMC_CLASS_SOFT, PMC_CLASS_PPC970, PMC_CLASS_TSC);
PMC_MDEP_TABLE(e500, E500, PMC_CLASS_SOFT, PMC_CLASS_E500, PMC_CLASS_TSC);
PMC_MDEP_TABLE(generic, SOFT, PMC_CLASS_SOFT);

static const struct pmc_event_descr tsc_event_table[] =
{
	__PMC_EV_TSC()
};

#undef	PMC_CLASS_TABLE_DESC
#define	PMC_CLASS_TABLE_DESC(NAME, CLASS, EVENTS, ALLOCATOR)	\
static const struct pmc_class_descr NAME##_class_table_descr =	\
	{							\
		.pm_evc_name  = #CLASS "-",			\
		.pm_evc_name_size = sizeof(#CLASS "-") - 1,	\
		.pm_evc_class = PMC_CLASS_##CLASS ,		\
		.pm_evc_event_table = EVENTS##_event_table ,	\
		.pm_evc_event_table_size = 			\
			PMC_EVENT_TABLE_SIZE(EVENTS),		\
		.pm_evc_allocate_pmc = ALLOCATOR##_allocate_pmc	\
	}

#if	defined(__i386__) || defined(__amd64__)
PMC_CLASS_TABLE_DESC(k8, K8, k8, k8);
#endif
#if	defined(__i386__) || defined(__amd64__)
PMC_CLASS_TABLE_DESC(tsc, TSC, tsc, tsc);
#endif
#if	defined(__arm__)
#if	defined(__XSCALE__)
PMC_CLASS_TABLE_DESC(xscale, XSCALE, xscale, xscale);
#endif
PMC_CLASS_TABLE_DESC(cortex_a8, ARMV7, cortex_a8, armv7);
PMC_CLASS_TABLE_DESC(cortex_a9, ARMV7, cortex_a9, armv7);
#endif
#if	defined(__aarch64__)
PMC_CLASS_TABLE_DESC(cortex_a53, ARMV8, cortex_a53, arm64);
PMC_CLASS_TABLE_DESC(cortex_a57, ARMV8, cortex_a57, arm64);
#endif
#if defined(__mips__)
PMC_CLASS_TABLE_DESC(mips24k, MIPS24K, mips24k, mips);
PMC_CLASS_TABLE_DESC(mips74k, MIPS74K, mips74k, mips);
PMC_CLASS_TABLE_DESC(octeon, OCTEON, octeon, mips);
#endif /* __mips__ */
#if defined(__powerpc__)
PMC_CLASS_TABLE_DESC(ppc7450, PPC7450, ppc7450, powerpc);
PMC_CLASS_TABLE_DESC(ppc970, PPC970, ppc970, powerpc);
PMC_CLASS_TABLE_DESC(e500, E500, e500, powerpc);
#endif

static struct pmc_class_descr soft_class_table_descr =
{
	.pm_evc_name  = "SOFT-",
	.pm_evc_name_size = sizeof("SOFT-") - 1,
	.pm_evc_class = PMC_CLASS_SOFT,
	.pm_evc_event_table = NULL,
	.pm_evc_event_table_size = 0,
	.pm_evc_allocate_pmc = soft_allocate_pmc
};

#undef	PMC_CLASS_TABLE_DESC

static const struct pmc_class_descr **pmc_class_table;
#define	PMC_CLASS_TABLE_SIZE	cpu_info.pm_nclass

static const enum pmc_class *pmc_mdep_class_list;
static size_t pmc_mdep_class_list_size;

/*
 * Mapping tables, mapping enumeration values to human readable
 * strings.
 */

static const char * pmc_capability_names[] = {
#undef	__PMC_CAP
#define	__PMC_CAP(N,V,D)	#N ,
	__PMC_CAPS()
};

struct pmc_class_map {
	enum pmc_class	pm_class;
	const char	*pm_name;
};

static const struct pmc_class_map pmc_class_names[] = {
#undef	__PMC_CLASS
#define __PMC_CLASS(S,V,D) { .pm_class = PMC_CLASS_##S, .pm_name = #S } ,
	__PMC_CLASSES()
};

struct pmc_cputype_map {
	enum pmc_cputype pm_cputype;
	const char	*pm_name;
};

static const struct pmc_cputype_map pmc_cputype_names[] = {
#undef	__PMC_CPU
#define	__PMC_CPU(S, V, D) { .pm_cputype = PMC_CPU_##S, .pm_name = #S } ,
	__PMC_CPUS()
};

static const char * pmc_disposition_names[] = {
#undef	__PMC_DISP
#define	__PMC_DISP(D)	#D ,
	__PMC_DISPOSITIONS()
};

static const char * pmc_mode_names[] = {
#undef  __PMC_MODE
#define __PMC_MODE(M,N)	#M ,
	__PMC_MODES()
};

static const char * pmc_state_names[] = {
#undef  __PMC_STATE
#define __PMC_STATE(S) #S ,
	__PMC_STATES()
};

/*
 * Filled in by pmc_init().
 */
static int pmc_syscall = -1;
static struct pmc_cpuinfo cpu_info;
static struct pmc_op_getdyneventinfo soft_event_info;

/* Event masks for events */
struct pmc_masks {
	const char	*pm_name;
	const uint64_t	pm_value;
};
#define	PMCMASK(N,V)	{ .pm_name = #N, .pm_value = (V) }
#define	NULLMASK	{ .pm_name = NULL }

#if defined(__amd64__) || defined(__i386__)
static int
pmc_parse_mask(const struct pmc_masks *pmask, char *p, uint64_t *evmask)
{
	const struct pmc_masks *pm;
	char *q, *r;
	int c;

	if (pmask == NULL)	/* no mask keywords */
		return (-1);
	q = strchr(p, '=');	/* skip '=' */
	if (*++q == '\0')	/* no more data */
		return (-1);
	c = 0;			/* count of mask keywords seen */
	while ((r = strsep(&q, "+")) != NULL) {
		for (pm = pmask; pm->pm_name && strcasecmp(r, pm->pm_name);
		    pm++)
			;
		if (pm->pm_name == NULL) /* not found */
			return (-1);
		*evmask |= pm->pm_value;
		c++;
	}
	return (c);
}
#endif

#define	KWMATCH(p,kw)		(strcasecmp((p), (kw)) == 0)
#define	KWPREFIXMATCH(p,kw)	(strncasecmp((p), (kw), sizeof((kw)) - 1) == 0)
#define	EV_ALIAS(N,S)		{ .pm_alias = N, .pm_spec = S }

#if defined(__amd64__) || defined(__i386__)
/*
 * AMD K8 PMCs.
 *
 */

static struct pmc_event_alias k8_aliases[] = {
	EV_ALIAS("branches",		"k8-fr-retired-taken-branches"),
	EV_ALIAS("branch-mispredicts",
	    "k8-fr-retired-taken-branches-mispredicted"),
	EV_ALIAS("cycles",		"tsc"),
	EV_ALIAS("dc-misses",		"k8-dc-miss"),
	EV_ALIAS("ic-misses",		"k8-ic-miss"),
	EV_ALIAS("instructions",	"k8-fr-retired-x86-instructions"),
	EV_ALIAS("interrupts",		"k8-fr-taken-hardware-interrupts"),
	EV_ALIAS("unhalted-cycles",	"k8-bu-cpu-clk-unhalted"),
	EV_ALIAS(NULL, NULL)
};

#define	__K8MASK(N,V) PMCMASK(N,(1 << (V)))

/*
 * Parsing tables
 */

/* fp dispatched fpu ops */
static const struct pmc_masks k8_mask_fdfo[] = {
	__K8MASK(add-pipe-excluding-junk-ops,	0),
	__K8MASK(multiply-pipe-excluding-junk-ops,	1),
	__K8MASK(store-pipe-excluding-junk-ops,	2),
	__K8MASK(add-pipe-junk-ops,		3),
	__K8MASK(multiply-pipe-junk-ops,	4),
	__K8MASK(store-pipe-junk-ops,		5),
	NULLMASK
};

/* ls segment register loads */
static const struct pmc_masks k8_mask_lsrl[] = {
	__K8MASK(es,	0),
	__K8MASK(cs,	1),
	__K8MASK(ss,	2),
	__K8MASK(ds,	3),
	__K8MASK(fs,	4),
	__K8MASK(gs,	5),
	__K8MASK(hs,	6),
	NULLMASK
};

/* ls locked operation */
static const struct pmc_masks k8_mask_llo[] = {
	__K8MASK(locked-instructions,	0),
	__K8MASK(cycles-in-request,	1),
	__K8MASK(cycles-to-complete,	2),
	NULLMASK
};

/* dc refill from {l2,system} and dc copyback */
static const struct pmc_masks k8_mask_dc[] = {
	__K8MASK(invalid,	0),
	__K8MASK(shared,	1),
	__K8MASK(exclusive,	2),
	__K8MASK(owner,		3),
	__K8MASK(modified,	4),
	NULLMASK
};

/* dc one bit ecc error */
static const struct pmc_masks k8_mask_dobee[] = {
	__K8MASK(scrubber,	0),
	__K8MASK(piggyback,	1),
	NULLMASK
};

/* dc dispatched prefetch instructions */
static const struct pmc_masks k8_mask_ddpi[] = {
	__K8MASK(load,	0),
	__K8MASK(store,	1),
	__K8MASK(nta,	2),
	NULLMASK
};

/* dc dcache accesses by locks */
static const struct pmc_masks k8_mask_dabl[] = {
	__K8MASK(accesses,	0),
	__K8MASK(misses,	1),
	NULLMASK
};

/* bu internal l2 request */
static const struct pmc_masks k8_mask_bilr[] = {
	__K8MASK(ic-fill,	0),
	__K8MASK(dc-fill,	1),
	__K8MASK(tlb-reload,	2),
	__K8MASK(tag-snoop,	3),
	__K8MASK(cancelled,	4),
	NULLMASK
};

/* bu fill request l2 miss */
static const struct pmc_masks k8_mask_bfrlm[] = {
	__K8MASK(ic-fill,	0),
	__K8MASK(dc-fill,	1),
	__K8MASK(tlb-reload,	2),
	NULLMASK
};

/* bu fill into l2 */
static const struct pmc_masks k8_mask_bfil[] = {
	__K8MASK(dirty-l2-victim,	0),
	__K8MASK(victim-from-l2,	1),
	NULLMASK
};

/* fr retired fpu instructions */
static const struct pmc_masks k8_mask_frfi[] = {
	__K8MASK(x87,			0),
	__K8MASK(mmx-3dnow,		1),
	__K8MASK(packed-sse-sse2,	2),
	__K8MASK(scalar-sse-sse2,	3),
	NULLMASK
};

/* fr retired fastpath double op instructions */
static const struct pmc_masks k8_mask_frfdoi[] = {
	__K8MASK(low-op-pos-0,		0),
	__K8MASK(low-op-pos-1,		1),
	__K8MASK(low-op-pos-2,		2),
	NULLMASK
};

/* fr fpu exceptions */
static const struct pmc_masks k8_mask_ffe[] = {
	__K8MASK(x87-reclass-microfaults,	0),
	__K8MASK(sse-retype-microfaults,	1),
	__K8MASK(sse-reclass-microfaults,	2),
	__K8MASK(sse-and-x87-microtraps,	3),
	NULLMASK
};

/* nb memory controller page access event */
static const struct pmc_masks k8_mask_nmcpae[] = {
	__K8MASK(page-hit,	0),
	__K8MASK(page-miss,	1),
	__K8MASK(page-conflict,	2),
	NULLMASK
};

/* nb memory controller turnaround */
static const struct pmc_masks k8_mask_nmct[] = {
	__K8MASK(dimm-turnaround,		0),
	__K8MASK(read-to-write-turnaround,	1),
	__K8MASK(write-to-read-turnaround,	2),
	NULLMASK
};

/* nb memory controller bypass saturation */
static const struct pmc_masks k8_mask_nmcbs[] = {
	__K8MASK(memory-controller-hi-pri-bypass,	0),
	__K8MASK(memory-controller-lo-pri-bypass,	1),
	__K8MASK(dram-controller-interface-bypass,	2),
	__K8MASK(dram-controller-queue-bypass,		3),
	NULLMASK
};

/* nb sized commands */
static const struct pmc_masks k8_mask_nsc[] = {
	__K8MASK(nonpostwrszbyte,	0),
	__K8MASK(nonpostwrszdword,	1),
	__K8MASK(postwrszbyte,		2),
	__K8MASK(postwrszdword,		3),
	__K8MASK(rdszbyte,		4),
	__K8MASK(rdszdword,		5),
	__K8MASK(rdmodwr,		6),
	NULLMASK
};

/* nb probe result */
static const struct pmc_masks k8_mask_npr[] = {
	__K8MASK(probe-miss,		0),
	__K8MASK(probe-hit,		1),
	__K8MASK(probe-hit-dirty-no-memory-cancel, 2),
	__K8MASK(probe-hit-dirty-with-memory-cancel, 3),
	NULLMASK
};

/* nb hypertransport bus bandwidth */
static const struct pmc_masks k8_mask_nhbb[] = { /* HT bus bandwidth */
	__K8MASK(command,	0),
	__K8MASK(data,	1),
	__K8MASK(buffer-release, 2),
	__K8MASK(nop,	3),
	NULLMASK
};

#undef	__K8MASK

#define	K8_KW_COUNT	"count"
#define	K8_KW_EDGE	"edge"
#define	K8_KW_INV	"inv"
#define	K8_KW_MASK	"mask"
#define	K8_KW_OS	"os"
#define	K8_KW_USR	"usr"

static int
k8_allocate_pmc(enum pmc_event pe, char *ctrspec,
    struct pmc_op_pmcallocate *pmc_config)
{
	char		*e, *p, *q;
	int		n;
	uint32_t	count;
	uint64_t	evmask;
	const struct pmc_masks	*pm, *pmask;

	pmc_config->pm_caps |= (PMC_CAP_READ | PMC_CAP_WRITE);
	pmc_config->pm_md.pm_amd.pm_amd_config = 0;

	pmask = NULL;
	evmask = 0;

#define	__K8SETMASK(M) pmask = k8_mask_##M

	/* setup parsing tables */
	switch (pe) {
	case PMC_EV_K8_FP_DISPATCHED_FPU_OPS:
		__K8SETMASK(fdfo);
		break;
	case PMC_EV_K8_LS_SEGMENT_REGISTER_LOAD:
		__K8SETMASK(lsrl);
		break;
	case PMC_EV_K8_LS_LOCKED_OPERATION:
		__K8SETMASK(llo);
		break;
	case PMC_EV_K8_DC_REFILL_FROM_L2:
	case PMC_EV_K8_DC_REFILL_FROM_SYSTEM:
	case PMC_EV_K8_DC_COPYBACK:
		__K8SETMASK(dc);
		break;
	case PMC_EV_K8_DC_ONE_BIT_ECC_ERROR:
		__K8SETMASK(dobee);
		break;
	case PMC_EV_K8_DC_DISPATCHED_PREFETCH_INSTRUCTIONS:
		__K8SETMASK(ddpi);
		break;
	case PMC_EV_K8_DC_DCACHE_ACCESSES_BY_LOCKS:
		__K8SETMASK(dabl);
		break;
	case PMC_EV_K8_BU_INTERNAL_L2_REQUEST:
		__K8SETMASK(bilr);
		break;
	case PMC_EV_K8_BU_FILL_REQUEST_L2_MISS:
		__K8SETMASK(bfrlm);
		break;
	case PMC_EV_K8_BU_FILL_INTO_L2:
		__K8SETMASK(bfil);
		break;
	case PMC_EV_K8_FR_RETIRED_FPU_INSTRUCTIONS:
		__K8SETMASK(frfi);
		break;
	case PMC_EV_K8_FR_RETIRED_FASTPATH_DOUBLE_OP_INSTRUCTIONS:
		__K8SETMASK(frfdoi);
		break;
	case PMC_EV_K8_FR_FPU_EXCEPTIONS:
		__K8SETMASK(ffe);
		break;
	case PMC_EV_K8_NB_MEMORY_CONTROLLER_PAGE_ACCESS_EVENT:
		__K8SETMASK(nmcpae);
		break;
	case PMC_EV_K8_NB_MEMORY_CONTROLLER_TURNAROUND:
		__K8SETMASK(nmct);
		break;
	case PMC_EV_K8_NB_MEMORY_CONTROLLER_BYPASS_SATURATION:
		__K8SETMASK(nmcbs);
		break;
	case PMC_EV_K8_NB_SIZED_COMMANDS:
		__K8SETMASK(nsc);
		break;
	case PMC_EV_K8_NB_PROBE_RESULT:
		__K8SETMASK(npr);
		break;
	case PMC_EV_K8_NB_HT_BUS0_BANDWIDTH:
	case PMC_EV_K8_NB_HT_BUS1_BANDWIDTH:
	case PMC_EV_K8_NB_HT_BUS2_BANDWIDTH:
		__K8SETMASK(nhbb);
		break;

	default:
		break;		/* no options defined */
	}

	while ((p = strsep(&ctrspec, ",")) != NULL) {
		if (KWPREFIXMATCH(p, K8_KW_COUNT "=")) {
			q = strchr(p, '=');
			if (*++q == '\0') /* skip '=' */
				return (-1);

			count = strtol(q, &e, 0);
			if (e == q || *e != '\0')
				return (-1);

			pmc_config->pm_caps |= PMC_CAP_THRESHOLD;
			pmc_config->pm_md.pm_amd.pm_amd_config |=
			    AMD_PMC_TO_COUNTER(count);

		} else if (KWMATCH(p, K8_KW_EDGE)) {
			pmc_config->pm_caps |= PMC_CAP_EDGE;
		} else if (KWMATCH(p, K8_KW_INV)) {
			pmc_config->pm_caps |= PMC_CAP_INVERT;
		} else if (KWPREFIXMATCH(p, K8_KW_MASK "=")) {
			if ((n = pmc_parse_mask(pmask, p, &evmask)) < 0)
				return (-1);
			pmc_config->pm_caps |= PMC_CAP_QUALIFIER;
		} else if (KWMATCH(p, K8_KW_OS)) {
			pmc_config->pm_caps |= PMC_CAP_SYSTEM;
		} else if (KWMATCH(p, K8_KW_USR)) {
			pmc_config->pm_caps |= PMC_CAP_USER;
		} else
			return (-1);
	}

	/* other post processing */
	switch (pe) {
	case PMC_EV_K8_FP_DISPATCHED_FPU_OPS:
	case PMC_EV_K8_FP_CYCLES_WITH_NO_FPU_OPS_RETIRED:
	case PMC_EV_K8_FP_DISPATCHED_FPU_FAST_FLAG_OPS:
	case PMC_EV_K8_FR_RETIRED_FASTPATH_DOUBLE_OP_INSTRUCTIONS:
	case PMC_EV_K8_FR_RETIRED_FPU_INSTRUCTIONS:
	case PMC_EV_K8_FR_FPU_EXCEPTIONS:
		/* XXX only available in rev B and later */
		break;
	case PMC_EV_K8_DC_DCACHE_ACCESSES_BY_LOCKS:
		/* XXX only available in rev C and later */
		break;
	case PMC_EV_K8_LS_LOCKED_OPERATION:
		/* XXX CPU Rev A,B evmask is to be zero */
		if (evmask & (evmask - 1)) /* > 1 bit set */
			return (-1);
		if (evmask == 0) {
			evmask = 0x01; /* Rev C and later: #instrs */
			pmc_config->pm_caps |= PMC_CAP_QUALIFIER;
		}
		break;
	default:
		if (evmask == 0 && pmask != NULL) {
			for (pm = pmask; pm->pm_name; pm++)
				evmask |= pm->pm_value;
			pmc_config->pm_caps |= PMC_CAP_QUALIFIER;
		}
	}

	if (pmc_config->pm_caps & PMC_CAP_QUALIFIER)
		pmc_config->pm_md.pm_amd.pm_amd_config =
		    AMD_PMC_TO_UNITMASK(evmask);

	return (0);
}

#endif

#if	defined(__i386__) || defined(__amd64__)
static int
tsc_allocate_pmc(enum pmc_event pe, char *ctrspec,
    struct pmc_op_pmcallocate *pmc_config)
{
	if (pe != PMC_EV_TSC_TSC)
		return (-1);

	/* TSC events must be unqualified. */
	if (ctrspec && *ctrspec != '\0')
		return (-1);

	pmc_config->pm_md.pm_amd.pm_amd_config = 0;
	pmc_config->pm_caps |= PMC_CAP_READ;

	return (0);
}
#endif

static struct pmc_event_alias generic_aliases[] = {
	EV_ALIAS("instructions",		"SOFT-CLOCK.HARD"),
	EV_ALIAS(NULL, NULL)
};

static int
soft_allocate_pmc(enum pmc_event pe, char *ctrspec,
    struct pmc_op_pmcallocate *pmc_config)
{
	(void)ctrspec;
	(void)pmc_config;

	if ((int)pe < PMC_EV_SOFT_FIRST || (int)pe > PMC_EV_SOFT_LAST)
		return (-1);

	pmc_config->pm_caps |= (PMC_CAP_READ | PMC_CAP_WRITE);
	return (0);
}

#if	defined(__arm__)
#if	defined(__XSCALE__)

static struct pmc_event_alias xscale_aliases[] = {
	EV_ALIAS("branches",		"BRANCH_RETIRED"),
	EV_ALIAS("branch-mispredicts",	"BRANCH_MISPRED"),
	EV_ALIAS("dc-misses",		"DC_MISS"),
	EV_ALIAS("ic-misses",		"IC_MISS"),
	EV_ALIAS("instructions",	"INSTR_RETIRED"),
	EV_ALIAS(NULL, NULL)
};
static int
xscale_allocate_pmc(enum pmc_event pe, char *ctrspec __unused,
    struct pmc_op_pmcallocate *pmc_config __unused)
{
	switch (pe) {
	default:
		break;
	}

	return (0);
}
#endif

static struct pmc_event_alias cortex_a8_aliases[] = {
	EV_ALIAS("dc-misses",		"L1_DCACHE_REFILL"),
	EV_ALIAS("ic-misses",		"L1_ICACHE_REFILL"),
	EV_ALIAS("instructions",	"INSTR_EXECUTED"),
	EV_ALIAS(NULL, NULL)
};

static struct pmc_event_alias cortex_a9_aliases[] = {
	EV_ALIAS("dc-misses",		"L1_DCACHE_REFILL"),
	EV_ALIAS("ic-misses",		"L1_ICACHE_REFILL"),
	EV_ALIAS("instructions",	"INSTR_EXECUTED"),
	EV_ALIAS(NULL, NULL)
};

static int
armv7_allocate_pmc(enum pmc_event pe, char *ctrspec __unused,
    struct pmc_op_pmcallocate *pmc_config __unused)
{
	switch (pe) {
	default:
		break;
	}

	return (0);
}
#endif

#if	defined(__aarch64__)
static struct pmc_event_alias cortex_a53_aliases[] = {
	EV_ALIAS(NULL, NULL)
};
static struct pmc_event_alias cortex_a57_aliases[] = {
	EV_ALIAS(NULL, NULL)
};
static int
arm64_allocate_pmc(enum pmc_event pe, char *ctrspec __unused,
    struct pmc_op_pmcallocate *pmc_config __unused)
{
	switch (pe) {
	default:
		break;
	}

	return (0);
}
#endif

#if defined(__mips__)

static struct pmc_event_alias mips24k_aliases[] = {
	EV_ALIAS("instructions",	"INSTR_EXECUTED"),
	EV_ALIAS("branches",		"BRANCH_COMPLETED"),
	EV_ALIAS("branch-mispredicts",	"BRANCH_MISPRED"),
	EV_ALIAS(NULL, NULL)
};

static struct pmc_event_alias mips74k_aliases[] = {
	EV_ALIAS("instructions",	"INSTR_EXECUTED"),
	EV_ALIAS("branches",		"BRANCH_INSNS"),
	EV_ALIAS("branch-mispredicts",	"MISPREDICTED_BRANCH_INSNS"),
	EV_ALIAS(NULL, NULL)
};

static struct pmc_event_alias octeon_aliases[] = {
	EV_ALIAS("instructions",	"RET"),
	EV_ALIAS("branches",		"BR"),
	EV_ALIAS("branch-mispredicts",	"BRMIS"),
	EV_ALIAS(NULL, NULL)
};

#define	MIPS_KW_OS		"os"
#define	MIPS_KW_USR		"usr"
#define	MIPS_KW_ANYTHREAD	"anythread"

static int
mips_allocate_pmc(enum pmc_event pe, char *ctrspec __unused,
		  struct pmc_op_pmcallocate *pmc_config __unused)
{
	char *p;

	(void) pe;

	pmc_config->pm_caps |= (PMC_CAP_READ | PMC_CAP_WRITE);
	
	while ((p = strsep(&ctrspec, ",")) != NULL) {
		if (KWMATCH(p, MIPS_KW_OS))
			pmc_config->pm_caps |= PMC_CAP_SYSTEM;
		else if (KWMATCH(p, MIPS_KW_USR))
			pmc_config->pm_caps |= PMC_CAP_USER;
		else if (KWMATCH(p, MIPS_KW_ANYTHREAD))
			pmc_config->pm_caps |= (PMC_CAP_USER | PMC_CAP_SYSTEM);
		else
			return (-1);
	}

	return (0);
}

#endif /* __mips__ */

#if defined(__powerpc__)

static struct pmc_event_alias ppc7450_aliases[] = {
	EV_ALIAS("instructions",	"INSTR_COMPLETED"),
	EV_ALIAS("branches",		"BRANCHES_COMPLETED"),
	EV_ALIAS("branch-mispredicts",	"MISPREDICTED_BRANCHES"),
	EV_ALIAS(NULL, NULL)
};

static struct pmc_event_alias ppc970_aliases[] = {
	EV_ALIAS("instructions", "INSTR_COMPLETED"),
	EV_ALIAS("cycles",       "CYCLES"),
	EV_ALIAS(NULL, NULL)
};

static struct pmc_event_alias e500_aliases[] = {
	EV_ALIAS("instructions", "INSTR_COMPLETED"),
	EV_ALIAS("cycles",       "CYCLES"),
	EV_ALIAS(NULL, NULL)
};

#define	POWERPC_KW_OS		"os"
#define	POWERPC_KW_USR		"usr"
#define	POWERPC_KW_ANYTHREAD	"anythread"

static int
powerpc_allocate_pmc(enum pmc_event pe, char *ctrspec __unused,
		     struct pmc_op_pmcallocate *pmc_config __unused)
{
	char *p;

	(void) pe;

	pmc_config->pm_caps |= (PMC_CAP_READ | PMC_CAP_WRITE);
	
	while ((p = strsep(&ctrspec, ",")) != NULL) {
		if (KWMATCH(p, POWERPC_KW_OS))
			pmc_config->pm_caps |= PMC_CAP_SYSTEM;
		else if (KWMATCH(p, POWERPC_KW_USR))
			pmc_config->pm_caps |= PMC_CAP_USER;
		else if (KWMATCH(p, POWERPC_KW_ANYTHREAD))
			pmc_config->pm_caps |= (PMC_CAP_USER | PMC_CAP_SYSTEM);
		else
			return (-1);
	}

	return (0);
}

#endif /* __powerpc__ */


/*
 * Match an event name `name' with its canonical form.
 *
 * Matches are case insensitive and spaces, periods, underscores and
 * hyphen characters are considered to match each other.
 *
 * Returns 1 for a match, 0 otherwise.
 */

static int
pmc_match_event_name(const char *name, const char *canonicalname)
{
	int cc, nc;
	const unsigned char *c, *n;

	c = (const unsigned char *) canonicalname;
	n = (const unsigned char *) name;

	for (; (nc = *n) && (cc = *c); n++, c++) {

		if ((nc == ' ' || nc == '_' || nc == '-' || nc == '.') &&
		    (cc == ' ' || cc == '_' || cc == '-' || cc == '.'))
			continue;

		if (toupper(nc) == toupper(cc))
			continue;


		return (0);
	}

	if (*n == '\0' && *c == '\0')
		return (1);

	return (0);
}

/*
 * Match an event name against all the event named supported by a
 * PMC class.
 *
 * Returns an event descriptor pointer on match or NULL otherwise.
 */
static const struct pmc_event_descr *
pmc_match_event_class(const char *name,
    const struct pmc_class_descr *pcd)
{
	size_t n;
	const struct pmc_event_descr *ev;

	ev = pcd->pm_evc_event_table;
	for (n = 0; n < pcd->pm_evc_event_table_size; n++, ev++)
		if (pmc_match_event_name(name, ev->pm_ev_name))
			return (ev);

	return (NULL);
}

static int
pmc_mdep_is_compatible_class(enum pmc_class pc)
{
	size_t n;

	for (n = 0; n < pmc_mdep_class_list_size; n++)
		if (pmc_mdep_class_list[n] == pc)
			return (1);
	return (0);
}

/*
 * API entry points
 */

int
pmc_allocate(const char *ctrspec, enum pmc_mode mode,
    uint32_t flags, int cpu, pmc_id_t *pmcid,
    uint64_t count)
{
	size_t n;
	int retval;
	char *r, *spec_copy;
	const char *ctrname;
	const struct pmc_event_descr *ev;
	const struct pmc_event_alias *alias;
	struct pmc_op_pmcallocate pmc_config;
	const struct pmc_class_descr *pcd;

	spec_copy = NULL;
	retval    = -1;

	if (mode != PMC_MODE_SS && mode != PMC_MODE_TS &&
	    mode != PMC_MODE_SC && mode != PMC_MODE_TC) {
		errno = EINVAL;
		goto out;
	}
	bzero(&pmc_config, sizeof(pmc_config));
	pmc_config.pm_cpu   = cpu;
	pmc_config.pm_mode  = mode;
	pmc_config.pm_flags = flags;
	pmc_config.pm_count = count;
	if (PMC_IS_SAMPLING_MODE(mode))
		pmc_config.pm_caps |= PMC_CAP_INTERRUPT;
	/*
	 * Can we pull this straight from the pmu table?
	 */
	r = spec_copy = strdup(ctrspec);
	ctrname = strsep(&r, ",");
	if (pmc_pmu_enabled()) {
		if (pmc_pmu_pmcallocate(ctrname, &pmc_config) == 0) {
			if (PMC_CALL(PMCALLOCATE, &pmc_config) < 0) {
				goto out;
			}
			retval = 0;
			*pmcid = pmc_config.pm_pmcid;
			goto out;
		}
		errx(EX_USAGE, "ERROR: pmc_pmu_allocate failed, check for ctrname %s\n", ctrname);
	} else {
		free(spec_copy);
		spec_copy = NULL;
	}

	/* replace an event alias with the canonical event specifier */
	if (pmc_mdep_event_aliases)
		for (alias = pmc_mdep_event_aliases; alias->pm_alias; alias++)
			if (!strcasecmp(ctrspec, alias->pm_alias)) {
				spec_copy = strdup(alias->pm_spec);
				break;
			}

	if (spec_copy == NULL)
		spec_copy = strdup(ctrspec);

	r = spec_copy;
	ctrname = strsep(&r, ",");

	/*
	 * If a explicit class prefix was given by the user, restrict the
	 * search for the event to the specified PMC class.
	 */
	ev = NULL;
	for (n = 0; n < PMC_CLASS_TABLE_SIZE; n++) {
		pcd = pmc_class_table[n];
		if (pcd && pmc_mdep_is_compatible_class(pcd->pm_evc_class) &&
		    strncasecmp(ctrname, pcd->pm_evc_name,
				pcd->pm_evc_name_size) == 0) {
			if ((ev = pmc_match_event_class(ctrname +
			    pcd->pm_evc_name_size, pcd)) == NULL) {
				errno = EINVAL;
				goto out;
			}
			break;
		}
	}

	/*
	 * Otherwise, search for this event in all compatible PMC
	 * classes.
	 */
	for (n = 0; ev == NULL && n < PMC_CLASS_TABLE_SIZE; n++) {
		pcd = pmc_class_table[n];
		if (pcd && pmc_mdep_is_compatible_class(pcd->pm_evc_class))
			ev = pmc_match_event_class(ctrname, pcd);
	}

	if (ev == NULL) {
		errno = EINVAL;
		goto out;
	}

	pmc_config.pm_ev    = ev->pm_ev_code;
	pmc_config.pm_class = pcd->pm_evc_class;

 	if (pcd->pm_evc_allocate_pmc(ev->pm_ev_code, r, &pmc_config) < 0) {
		errno = EINVAL;
		goto out;
	}

	if (PMC_CALL(PMCALLOCATE, &pmc_config) < 0)
		goto out;

	*pmcid = pmc_config.pm_pmcid;

	retval = 0;

 out:
	if (spec_copy)
		free(spec_copy);

	return (retval);
}

int
pmc_attach(pmc_id_t pmc, pid_t pid)
{
	struct pmc_op_pmcattach pmc_attach_args;

	pmc_attach_args.pm_pmc = pmc;
	pmc_attach_args.pm_pid = pid;

	return (PMC_CALL(PMCATTACH, &pmc_attach_args));
}

int
pmc_capabilities(pmc_id_t pmcid, uint32_t *caps)
{
	unsigned int i;
	enum pmc_class cl;

	cl = PMC_ID_TO_CLASS(pmcid);
	for (i = 0; i < cpu_info.pm_nclass; i++)
		if (cpu_info.pm_classes[i].pm_class == cl) {
			*caps = cpu_info.pm_classes[i].pm_caps;
			return (0);
		}
	errno = EINVAL;
	return (-1);
}

int
pmc_configure_logfile(int fd)
{
	struct pmc_op_configurelog cla;

	cla.pm_logfd = fd;
	if (PMC_CALL(CONFIGURELOG, &cla) < 0)
		return (-1);
	return (0);
}

int
pmc_cpuinfo(const struct pmc_cpuinfo **pci)
{
	if (pmc_syscall == -1) {
		errno = ENXIO;
		return (-1);
	}

	*pci = &cpu_info;
	return (0);
}

int
pmc_detach(pmc_id_t pmc, pid_t pid)
{
	struct pmc_op_pmcattach pmc_detach_args;

	pmc_detach_args.pm_pmc = pmc;
	pmc_detach_args.pm_pid = pid;
	return (PMC_CALL(PMCDETACH, &pmc_detach_args));
}

int
pmc_disable(int cpu, int pmc)
{
	struct pmc_op_pmcadmin ssa;

	ssa.pm_cpu = cpu;
	ssa.pm_pmc = pmc;
	ssa.pm_state = PMC_STATE_DISABLED;
	return (PMC_CALL(PMCADMIN, &ssa));
}

int
pmc_enable(int cpu, int pmc)
{
	struct pmc_op_pmcadmin ssa;

	ssa.pm_cpu = cpu;
	ssa.pm_pmc = pmc;
	ssa.pm_state = PMC_STATE_FREE;
	return (PMC_CALL(PMCADMIN, &ssa));
}

/*
 * Return a list of events known to a given PMC class.  'cl' is the
 * PMC class identifier, 'eventnames' is the returned list of 'const
 * char *' pointers pointing to the names of the events. 'nevents' is
 * the number of event name pointers returned.
 *
 * The space for 'eventnames' is allocated using malloc(3).  The caller
 * is responsible for freeing this space when done.
 */
int
pmc_event_names_of_class(enum pmc_class cl, const char ***eventnames,
    int *nevents)
{
	int count;
	const char **names;
	const struct pmc_event_descr *ev;

	switch (cl)
	{
	case PMC_CLASS_IAF:
		ev = iaf_event_table;
		count = PMC_EVENT_TABLE_SIZE(iaf);
		break;
	case PMC_CLASS_TSC:
		ev = tsc_event_table;
		count = PMC_EVENT_TABLE_SIZE(tsc);
		break;
	case PMC_CLASS_K8:
		ev = k8_event_table;
		count = PMC_EVENT_TABLE_SIZE(k8);
		break;
	case PMC_CLASS_XSCALE:
		ev = xscale_event_table;
		count = PMC_EVENT_TABLE_SIZE(xscale);
		break;
	case PMC_CLASS_ARMV7:
		switch (cpu_info.pm_cputype) {
		default:
		case PMC_CPU_ARMV7_CORTEX_A8:
			ev = cortex_a8_event_table;
			count = PMC_EVENT_TABLE_SIZE(cortex_a8);
			break;
		case PMC_CPU_ARMV7_CORTEX_A9:
			ev = cortex_a9_event_table;
			count = PMC_EVENT_TABLE_SIZE(cortex_a9);
			break;
		}
		break;
	case PMC_CLASS_ARMV8:
		switch (cpu_info.pm_cputype) {
		default:
		case PMC_CPU_ARMV8_CORTEX_A53:
			ev = cortex_a53_event_table;
			count = PMC_EVENT_TABLE_SIZE(cortex_a53);
			break;
		case PMC_CPU_ARMV8_CORTEX_A57:
			ev = cortex_a57_event_table;
			count = PMC_EVENT_TABLE_SIZE(cortex_a57);
			break;
		}
		break;
	case PMC_CLASS_MIPS24K:
		ev = mips24k_event_table;
		count = PMC_EVENT_TABLE_SIZE(mips24k);
		break;
	case PMC_CLASS_MIPS74K:
		ev = mips74k_event_table;
		count = PMC_EVENT_TABLE_SIZE(mips74k);
		break;
	case PMC_CLASS_OCTEON:
		ev = octeon_event_table;
		count = PMC_EVENT_TABLE_SIZE(octeon);
		break;
	case PMC_CLASS_PPC7450:
		ev = ppc7450_event_table;
		count = PMC_EVENT_TABLE_SIZE(ppc7450);
		break;
	case PMC_CLASS_PPC970:
		ev = ppc970_event_table;
		count = PMC_EVENT_TABLE_SIZE(ppc970);
		break;
	case PMC_CLASS_E500:
		ev = e500_event_table;
		count = PMC_EVENT_TABLE_SIZE(e500);
		break;
	case PMC_CLASS_SOFT:
		ev = soft_event_table;
		count = soft_event_info.pm_nevent;
		break;
	default:
		errno = EINVAL;
		return (-1);
	}

	if ((names = malloc(count * sizeof(const char *))) == NULL)
		return (-1);

	*eventnames = names;
	*nevents = count;

	for (;count--; ev++, names++)
		*names = ev->pm_ev_name;

	return (0);
}

int
pmc_flush_logfile(void)
{
	return (PMC_CALL(FLUSHLOG,0));
}

int
pmc_close_logfile(void)
{
	return (PMC_CALL(CLOSELOG,0));
}

int
pmc_get_driver_stats(struct pmc_driverstats *ds)
{
	struct pmc_op_getdriverstats gms;

	if (PMC_CALL(GETDRIVERSTATS, &gms) < 0)
		return (-1);

	/* copy out fields in the current userland<->library interface */
	ds->pm_intr_ignored    = gms.pm_intr_ignored;
	ds->pm_intr_processed  = gms.pm_intr_processed;
	ds->pm_intr_bufferfull = gms.pm_intr_bufferfull;
	ds->pm_syscalls        = gms.pm_syscalls;
	ds->pm_syscall_errors  = gms.pm_syscall_errors;
	ds->pm_buffer_requests = gms.pm_buffer_requests;
	ds->pm_buffer_requests_failed = gms.pm_buffer_requests_failed;
	ds->pm_log_sweeps      = gms.pm_log_sweeps;
	return (0);
}

int
pmc_get_msr(pmc_id_t pmc, uint32_t *msr)
{
	struct pmc_op_getmsr gm;

	gm.pm_pmcid = pmc;
	if (PMC_CALL(PMCGETMSR, &gm) < 0)
		return (-1);
	*msr = gm.pm_msr;
	return (0);
}

int
pmc_init(void)
{
	int error, pmc_mod_id;
	unsigned int n;
	uint32_t abi_version;
	struct module_stat pmc_modstat;
	struct pmc_op_getcpuinfo op_cpu_info;
#if defined(__amd64__) || defined(__i386__)
	int cpu_has_iaf_counters;
	unsigned int t;
#endif

	if (pmc_syscall != -1) /* already inited */
		return (0);

	/* retrieve the system call number from the KLD */
	if ((pmc_mod_id = modfind(PMC_MODULE_NAME)) < 0)
		return (-1);

	pmc_modstat.version = sizeof(struct module_stat);
	if ((error = modstat(pmc_mod_id, &pmc_modstat)) < 0)
		return (-1);

	pmc_syscall = pmc_modstat.data.intval;

	/* check the kernel module's ABI against our compiled-in version */
	abi_version = PMC_VERSION;
	if (PMC_CALL(GETMODULEVERSION, &abi_version) < 0)
		return (pmc_syscall = -1);

	/* ignore patch & minor numbers for the comparison */
	if ((abi_version & 0xFF000000) != (PMC_VERSION & 0xFF000000)) {
		errno  = EPROGMISMATCH;
		return (pmc_syscall = -1);
	}

	bzero(&op_cpu_info, sizeof(op_cpu_info));
	if (PMC_CALL(GETCPUINFO, &op_cpu_info) < 0)
		return (pmc_syscall = -1);

	cpu_info.pm_cputype = op_cpu_info.pm_cputype;
	cpu_info.pm_ncpu    = op_cpu_info.pm_ncpu;
	cpu_info.pm_npmc    = op_cpu_info.pm_npmc;
	cpu_info.pm_nclass  = op_cpu_info.pm_nclass;
	for (n = 0; n < op_cpu_info.pm_nclass; n++)
		memcpy(&cpu_info.pm_classes[n], &op_cpu_info.pm_classes[n],
		    sizeof(cpu_info.pm_classes[n]));

	pmc_class_table = malloc(PMC_CLASS_TABLE_SIZE *
	    sizeof(struct pmc_class_descr *));

	if (pmc_class_table == NULL)
		return (-1);

	for (n = 0; n < PMC_CLASS_TABLE_SIZE; n++)
		pmc_class_table[n] = NULL;

	/*
	 * Get soft events list.
	 */
	soft_event_info.pm_class = PMC_CLASS_SOFT;
	if (PMC_CALL(GETDYNEVENTINFO, &soft_event_info) < 0)
		return (pmc_syscall = -1);

	/* Map soft events to static list. */
	for (n = 0; n < soft_event_info.pm_nevent; n++) {
		soft_event_table[n].pm_ev_name =
		    soft_event_info.pm_events[n].pm_ev_name;
		soft_event_table[n].pm_ev_code =
		    soft_event_info.pm_events[n].pm_ev_code;
	}
	soft_class_table_descr.pm_evc_event_table_size = \
	    soft_event_info.pm_nevent;
	soft_class_table_descr.pm_evc_event_table = \
	    soft_event_table;

	/*
	 * Fill in the class table.
	 */
	n = 0;

	/* Fill soft events information. */
	pmc_class_table[n++] = &soft_class_table_descr;
#if defined(__amd64__) || defined(__i386__)
	if (cpu_info.pm_cputype != PMC_CPU_GENERIC)
		pmc_class_table[n++] = &tsc_class_table_descr;

	/*
 	 * Check if this CPU has fixed function counters.
	 */
	cpu_has_iaf_counters = 0;
	for (t = 0; t < cpu_info.pm_nclass; t++)
		if (cpu_info.pm_classes[t].pm_class == PMC_CLASS_IAF &&
		    cpu_info.pm_classes[t].pm_num > 0)
			cpu_has_iaf_counters = 1;
#endif

#define	PMC_MDEP_INIT(C) do {					\
		pmc_mdep_event_aliases    = C##_aliases;	\
		pmc_mdep_class_list  = C##_pmc_classes;		\
		pmc_mdep_class_list_size =			\
		    PMC_TABLE_SIZE(C##_pmc_classes);		\
	} while (0)

#define	PMC_MDEP_INIT_INTEL_V2(C) do {					\
		PMC_MDEP_INIT(C);					\
		pmc_class_table[n++] = &iaf_class_table_descr;		\
		if (!cpu_has_iaf_counters) 				\
			pmc_mdep_event_aliases =			\
				C##_aliases_without_iaf;		\
		pmc_class_table[n] = &C##_class_table_descr;		\
	} while (0)

	/* Configure the event name parser. */
	switch (cpu_info.pm_cputype) {
#if defined(__amd64__) || defined(__i386__)
	case PMC_CPU_AMD_K8:
		PMC_MDEP_INIT(k8);
		pmc_class_table[n] = &k8_class_table_descr;
		break;
#endif
	case PMC_CPU_GENERIC:
		PMC_MDEP_INIT(generic);
		break;
#if defined(__arm__)
#if defined(__XSCALE__)
	case PMC_CPU_INTEL_XSCALE:
		PMC_MDEP_INIT(xscale);
		pmc_class_table[n] = &xscale_class_table_descr;
		break;
#endif
	case PMC_CPU_ARMV7_CORTEX_A8:
		PMC_MDEP_INIT(cortex_a8);
		pmc_class_table[n] = &cortex_a8_class_table_descr;
		break;
	case PMC_CPU_ARMV7_CORTEX_A9:
		PMC_MDEP_INIT(cortex_a9);
		pmc_class_table[n] = &cortex_a9_class_table_descr;
		break;
#endif
#if defined(__aarch64__)
	case PMC_CPU_ARMV8_CORTEX_A53:
		PMC_MDEP_INIT(cortex_a53);
		pmc_class_table[n] = &cortex_a53_class_table_descr;
		break;
	case PMC_CPU_ARMV8_CORTEX_A57:
		PMC_MDEP_INIT(cortex_a57);
		pmc_class_table[n] = &cortex_a57_class_table_descr;
		break;
#endif
#if defined(__mips__)
	case PMC_CPU_MIPS_24K:
		PMC_MDEP_INIT(mips24k);
		pmc_class_table[n] = &mips24k_class_table_descr;
		break;
	case PMC_CPU_MIPS_74K:
		PMC_MDEP_INIT(mips74k);
		pmc_class_table[n] = &mips74k_class_table_descr;
		break;
	case PMC_CPU_MIPS_OCTEON:
		PMC_MDEP_INIT(octeon);
		pmc_class_table[n] = &octeon_class_table_descr;
		break;
#endif /* __mips__ */
#if defined(__powerpc__)
	case PMC_CPU_PPC_7450:
		PMC_MDEP_INIT(ppc7450);
		pmc_class_table[n] = &ppc7450_class_table_descr;
		break;
	case PMC_CPU_PPC_970:
		PMC_MDEP_INIT(ppc970);
		pmc_class_table[n] = &ppc970_class_table_descr;
		break;
	case PMC_CPU_PPC_E500:
		PMC_MDEP_INIT(e500);
		pmc_class_table[n] = &e500_class_table_descr;
		break;
#endif
	default:
		/*
		 * Some kind of CPU this version of the library knows nothing
		 * about.  This shouldn't happen since the abi version check
		 * should have caught this.
		 */
#if defined(__amd64__) || defined(__i386__)
		break;
#endif
		errno = ENXIO;
		return (pmc_syscall = -1);
	}

	return (0);
}

const char *
pmc_name_of_capability(enum pmc_caps cap)
{
	int i;

	/*
	 * 'cap' should have a single bit set and should be in
	 * range.
	 */
	if ((cap & (cap - 1)) || cap < PMC_CAP_FIRST ||
	    cap > PMC_CAP_LAST) {
		errno = EINVAL;
		return (NULL);
	}

	i = ffs(cap);
	return (pmc_capability_names[i - 1]);
}

const char *
pmc_name_of_class(enum pmc_class pc)
{
	size_t n;

	for (n = 0; n < PMC_TABLE_SIZE(pmc_class_names); n++)
		if (pc == pmc_class_names[n].pm_class)
			return (pmc_class_names[n].pm_name);

	errno = EINVAL;
	return (NULL);
}

const char *
pmc_name_of_cputype(enum pmc_cputype cp)
{
	size_t n;

	for (n = 0; n < PMC_TABLE_SIZE(pmc_cputype_names); n++)
		if (cp == pmc_cputype_names[n].pm_cputype)
			return (pmc_cputype_names[n].pm_name);

	errno = EINVAL;
	return (NULL);
}

const char *
pmc_name_of_disposition(enum pmc_disp pd)
{
	if ((int) pd >= PMC_DISP_FIRST &&
	    pd <= PMC_DISP_LAST)
		return (pmc_disposition_names[pd]);

	errno = EINVAL;
	return (NULL);
}

const char *
_pmc_name_of_event(enum pmc_event pe, enum pmc_cputype cpu)
{
	const struct pmc_event_descr *ev, *evfence;

	ev = evfence = NULL;
	if (pe >= PMC_EV_K8_FIRST && pe <= PMC_EV_K8_LAST) {
		ev = k8_event_table;
		evfence = k8_event_table + PMC_EVENT_TABLE_SIZE(k8);
	} else if (pe >= PMC_EV_XSCALE_FIRST && pe <= PMC_EV_XSCALE_LAST) {
		ev = xscale_event_table;
		evfence = xscale_event_table + PMC_EVENT_TABLE_SIZE(xscale);
	} else if (pe >= PMC_EV_ARMV7_FIRST && pe <= PMC_EV_ARMV7_LAST) {
		switch (cpu) {
		case PMC_CPU_ARMV7_CORTEX_A8:
			ev = cortex_a8_event_table;
			evfence = cortex_a8_event_table + PMC_EVENT_TABLE_SIZE(cortex_a8);
			break;
		case PMC_CPU_ARMV7_CORTEX_A9:
			ev = cortex_a9_event_table;
			evfence = cortex_a9_event_table + PMC_EVENT_TABLE_SIZE(cortex_a9);
			break;
		default:	/* Unknown CPU type. */
			break;
		}
	} else if (pe >= PMC_EV_ARMV8_FIRST && pe <= PMC_EV_ARMV8_LAST) {
		switch (cpu) {
		case PMC_CPU_ARMV8_CORTEX_A53:
			ev = cortex_a53_event_table;
			evfence = cortex_a53_event_table + PMC_EVENT_TABLE_SIZE(cortex_a53);
			break;
		case PMC_CPU_ARMV8_CORTEX_A57:
			ev = cortex_a57_event_table;
			evfence = cortex_a57_event_table + PMC_EVENT_TABLE_SIZE(cortex_a57);
			break;
		default:	/* Unknown CPU type. */
			break;
		}
	} else if (pe >= PMC_EV_MIPS24K_FIRST && pe <= PMC_EV_MIPS24K_LAST) {
		ev = mips24k_event_table;
		evfence = mips24k_event_table + PMC_EVENT_TABLE_SIZE(mips24k);
	} else if (pe >= PMC_EV_MIPS74K_FIRST && pe <= PMC_EV_MIPS74K_LAST) {
		ev = mips74k_event_table;
		evfence = mips74k_event_table + PMC_EVENT_TABLE_SIZE(mips74k);
	} else if (pe >= PMC_EV_OCTEON_FIRST && pe <= PMC_EV_OCTEON_LAST) {
		ev = octeon_event_table;
		evfence = octeon_event_table + PMC_EVENT_TABLE_SIZE(octeon);
	} else if (pe >= PMC_EV_PPC7450_FIRST && pe <= PMC_EV_PPC7450_LAST) {
		ev = ppc7450_event_table;
		evfence = ppc7450_event_table + PMC_EVENT_TABLE_SIZE(ppc7450);
	} else if (pe >= PMC_EV_PPC970_FIRST && pe <= PMC_EV_PPC970_LAST) {
		ev = ppc970_event_table;
		evfence = ppc970_event_table + PMC_EVENT_TABLE_SIZE(ppc970);
	} else if (pe >= PMC_EV_E500_FIRST && pe <= PMC_EV_E500_LAST) {
		ev = e500_event_table;
		evfence = e500_event_table + PMC_EVENT_TABLE_SIZE(e500);
	} else if (pe == PMC_EV_TSC_TSC) {
		ev = tsc_event_table;
		evfence = tsc_event_table + PMC_EVENT_TABLE_SIZE(tsc);
	} else if ((int)pe >= PMC_EV_SOFT_FIRST && (int)pe <= PMC_EV_SOFT_LAST) {
		ev = soft_event_table;
		evfence = soft_event_table + soft_event_info.pm_nevent;
	}

	for (; ev != evfence; ev++)
		if (pe == ev->pm_ev_code)
			return (ev->pm_ev_name);

	return (NULL);
}

const char *
pmc_name_of_event(enum pmc_event pe)
{
	const char *n;

	if ((n = _pmc_name_of_event(pe, cpu_info.pm_cputype)) != NULL)
		return (n);

	errno = EINVAL;
	return (NULL);
}

const char *
pmc_name_of_mode(enum pmc_mode pm)
{
	if ((int) pm >= PMC_MODE_FIRST &&
	    pm <= PMC_MODE_LAST)
		return (pmc_mode_names[pm]);

	errno = EINVAL;
	return (NULL);
}

const char *
pmc_name_of_state(enum pmc_state ps)
{
	if ((int) ps >= PMC_STATE_FIRST &&
	    ps <= PMC_STATE_LAST)
		return (pmc_state_names[ps]);

	errno = EINVAL;
	return (NULL);
}

int
pmc_ncpu(void)
{
	if (pmc_syscall == -1) {
		errno = ENXIO;
		return (-1);
	}

	return (cpu_info.pm_ncpu);
}

int
pmc_npmc(int cpu)
{
	if (pmc_syscall == -1) {
		errno = ENXIO;
		return (-1);
	}

	if (cpu < 0 || cpu >= (int) cpu_info.pm_ncpu) {
		errno = EINVAL;
		return (-1);
	}

	return (cpu_info.pm_npmc);
}

int
pmc_pmcinfo(int cpu, struct pmc_pmcinfo **ppmci)
{
	int nbytes, npmc;
	struct pmc_op_getpmcinfo *pmci;

	if ((npmc = pmc_npmc(cpu)) < 0)
		return (-1);

	nbytes = sizeof(struct pmc_op_getpmcinfo) +
	    npmc * sizeof(struct pmc_info);

	if ((pmci = calloc(1, nbytes)) == NULL)
		return (-1);

	pmci->pm_cpu  = cpu;

	if (PMC_CALL(GETPMCINFO, pmci) < 0) {
		free(pmci);
		return (-1);
	}

	/* kernel<->library, library<->userland interfaces are identical */
	*ppmci = (struct pmc_pmcinfo *) pmci;
	return (0);
}

int
pmc_read(pmc_id_t pmc, pmc_value_t *value)
{
	struct pmc_op_pmcrw pmc_read_op;

	pmc_read_op.pm_pmcid = pmc;
	pmc_read_op.pm_flags = PMC_F_OLDVALUE;
	pmc_read_op.pm_value = -1;

	if (PMC_CALL(PMCRW, &pmc_read_op) < 0)
		return (-1);

	*value = pmc_read_op.pm_value;
	return (0);
}

int
pmc_release(pmc_id_t pmc)
{
	struct pmc_op_simple	pmc_release_args;

	pmc_release_args.pm_pmcid = pmc;
	return (PMC_CALL(PMCRELEASE, &pmc_release_args));
}

int
pmc_rw(pmc_id_t pmc, pmc_value_t newvalue, pmc_value_t *oldvaluep)
{
	struct pmc_op_pmcrw pmc_rw_op;

	pmc_rw_op.pm_pmcid = pmc;
	pmc_rw_op.pm_flags = PMC_F_NEWVALUE | PMC_F_OLDVALUE;
	pmc_rw_op.pm_value = newvalue;

	if (PMC_CALL(PMCRW, &pmc_rw_op) < 0)
		return (-1);

	*oldvaluep = pmc_rw_op.pm_value;
	return (0);
}

int
pmc_set(pmc_id_t pmc, pmc_value_t value)
{
	struct pmc_op_pmcsetcount sc;

	sc.pm_pmcid = pmc;
	sc.pm_count = value;

	if (PMC_CALL(PMCSETCOUNT, &sc) < 0)
		return (-1);
	return (0);
}

int
pmc_start(pmc_id_t pmc)
{
	struct pmc_op_simple	pmc_start_args;

	pmc_start_args.pm_pmcid = pmc;
	return (PMC_CALL(PMCSTART, &pmc_start_args));
}

int
pmc_stop(pmc_id_t pmc)
{
	struct pmc_op_simple	pmc_stop_args;

	pmc_stop_args.pm_pmcid = pmc;
	return (PMC_CALL(PMCSTOP, &pmc_stop_args));
}

int
pmc_width(pmc_id_t pmcid, uint32_t *width)
{
	unsigned int i;
	enum pmc_class cl;

	cl = PMC_ID_TO_CLASS(pmcid);
	for (i = 0; i < cpu_info.pm_nclass; i++)
		if (cpu_info.pm_classes[i].pm_class == cl) {
			*width = cpu_info.pm_classes[i].pm_width;
			return (0);
		}
	errno = EINVAL;
	return (-1);
}

int
pmc_write(pmc_id_t pmc, pmc_value_t value)
{
	struct pmc_op_pmcrw pmc_write_op;

	pmc_write_op.pm_pmcid = pmc;
	pmc_write_op.pm_flags = PMC_F_NEWVALUE;
	pmc_write_op.pm_value = value;
	return (PMC_CALL(PMCRW, &pmc_write_op));
}

int
pmc_writelog(uint32_t userdata)
{
	struct pmc_op_writelog wl;

	wl.pm_userdata = userdata;
	return (PMC_CALL(WRITELOG, &wl));
}
