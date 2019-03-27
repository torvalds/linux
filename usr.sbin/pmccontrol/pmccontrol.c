/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003,2004 Joseph Koshy
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/cpuset.h>
#include <sys/sysctl.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pmc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

/* Compile time defaults */

#define	PMCC_PRINT_USAGE	0
#define	PMCC_PRINT_EVENTS	1
#define	PMCC_LIST_STATE 	2
#define	PMCC_ENABLE_DISABLE	3
#define	PMCC_SHOW_STATISTICS	4

#define	PMCC_CPU_ALL		-1
#define	PMCC_CPU_WILDCARD	'*'

#define	PMCC_PMC_ALL		-1
#define	PMCC_PMC_WILDCARD	'*'

#define	PMCC_OP_IGNORE		0
#define	PMCC_OP_DISABLE		1
#define	PMCC_OP_ENABLE		2

#define	PMCC_PROGRAM_NAME	"pmccontrol"

static STAILQ_HEAD(pmcc_op_list, pmcc_op) head = STAILQ_HEAD_INITIALIZER(head);

struct pmcc_op {
	char	op_cpu;
	char	op_pmc;
	char	op_op;
	STAILQ_ENTRY(pmcc_op) op_next;
};

/* Function Prototypes */
#if	DEBUG
static void	pmcc_init_debug(void);
#endif

static int	pmcc_do_list_state(void);
static int	pmcc_do_enable_disable(struct pmcc_op_list *);
static int	pmcc_do_list_events(void);

/* Globals */

static char usage_message[] =
	"Usage:\n"
	"       " PMCC_PROGRAM_NAME " -L\n"
	"       " PMCC_PROGRAM_NAME " -l\n"
	"       " PMCC_PROGRAM_NAME " -s\n"
	"       " PMCC_PROGRAM_NAME " [-e pmc | -d pmc | -c cpu] ...";

#if DEBUG
static FILE *debug_stream = NULL;
#endif

#if DEBUG
#define DEBUG_MSG(...)					                \
	(void) fprintf(debug_stream, "[pmccontrol] " __VA_ARGS__);
#else
#define DEBUG_MSG(m)		/*  */
#endif /* !DEBUG */

#if DEBUG
/* log debug messages to a separate file */
static void
pmcc_init_debug(void)
{
	char *fn;

	fn = getenv("PMCCONTROL_DEBUG");
	if (fn != NULL)
	{
		debug_stream = fopen(fn, "w");
		if (debug_stream == NULL)
			debug_stream = stderr;
	} else
		debug_stream = stderr;
}
#endif

static int
pmcc_do_enable_disable(struct pmcc_op_list *op_list)
{
	int c, error, i, j, ncpu, npmc, t;
	struct pmcc_op *np;
	unsigned char *map;
	unsigned char op;
	int cpu, pmc;

	if ((ncpu = pmc_ncpu()) < 0)
		err(EX_OSERR, "Unable to determine the number of cpus");

	/* Determine the maximum number of PMCs in any CPU. */
	npmc = 0;
	for (c = 0; c < ncpu; c++) {
		if ((t = pmc_npmc(c)) < 0)
			err(EX_OSERR,
			    "Unable to determine the number of PMCs in CPU %d",
			    c);
		npmc = MAX(t, npmc);
	}

	if (npmc == 0)
		errx(EX_CONFIG, "No PMCs found");

	if ((map = calloc(npmc, ncpu)) == NULL)
		err(EX_SOFTWARE, "Out of memory");

	error = 0;
	STAILQ_FOREACH(np, op_list, op_next) {

		cpu = np->op_cpu;
		pmc = np->op_pmc;
		op  = np->op_op;

		if (cpu >= ncpu)
			errx(EX_DATAERR, "CPU id too large: \"%d\"", cpu);

		if (pmc >= npmc)
			errx(EX_DATAERR, "PMC id too large: \"%d\"", pmc);

#define MARKMAP(M,C,P,V)	do {				\
		*((M) + (C)*npmc + (P)) = (V);			\
} while (0)

#define	SET_PMCS(C,P,V)		do {				\
		if ((P) == PMCC_PMC_ALL) {			\
			for (j = 0; j < npmc; j++)		\
				MARKMAP(map, (C), j, (V));	\
		} else						\
			MARKMAP(map, (C), (P), (V));		\
} while (0)

#define MAP(M,C,P)	(*((M) + (C)*npmc + (P)))

		if (cpu == PMCC_CPU_ALL)
			for (i = 0; i < ncpu; i++) {
				SET_PMCS(i, pmc, op);
			}
		else
			SET_PMCS(cpu, pmc, op);
	}

	/* Configure PMCS */
	for (i = 0; i < ncpu; i++)
		for (j = 0; j < npmc; j++) {
			unsigned char b;

			b = MAP(map, i, j);

			error = 0;

			if (b == PMCC_OP_ENABLE)
				error = pmc_enable(i, j);
			else if (b == PMCC_OP_DISABLE)
				error = pmc_disable(i, j);

			if (error < 0)
				err(EX_OSERR, "%s of PMC %d on CPU %d failed",
				    b == PMCC_OP_ENABLE ? "Enable" : "Disable",
				    j, i);
		}

	return error;
}

static int
pmcc_do_list_state(void)
{
	cpuset_t logical_cpus_mask;
	long cpusetsize;
	size_t setsize;
	int c, cpu, n, npmc, ncpu;
	struct pmc_info *pd;
	struct pmc_pmcinfo *pi;
	const struct pmc_cpuinfo *pc;

	if (pmc_cpuinfo(&pc) != 0)
		err(EX_OSERR, "Unable to determine CPU information");

	printf("%d %s CPUs present, with %d PMCs per CPU\n", pc->pm_ncpu, 
	       pmc_name_of_cputype(pc->pm_cputype),
		pc->pm_npmc);

	/* Determine the set of logical CPUs. */
	cpusetsize = sysconf(_SC_CPUSET_SIZE);
	if (cpusetsize == -1 || (u_long)cpusetsize > sizeof(cpuset_t))
		err(EX_OSERR, "Cannot determine which CPUs are logical");
	CPU_ZERO(&logical_cpus_mask);
	setsize = (size_t)cpusetsize;
	if (sysctlbyname("machdep.logical_cpus_mask", &logical_cpus_mask,
	    &setsize, NULL, 0) < 0)
		CPU_ZERO(&logical_cpus_mask);

	ncpu = pc->pm_ncpu;

	for (c = cpu = 0; cpu < ncpu; cpu++) {
#if	defined(__i386__) || defined(__amd64__)
		if (pc->pm_cputype == PMC_CPU_INTEL_PIV &&
		    CPU_ISSET(cpu, &logical_cpus_mask))
			continue; /* skip P4-style 'logical' cpus */
#endif
		if (pmc_pmcinfo(cpu, &pi) < 0) {
			if (errno == ENXIO)
				continue;
			err(EX_OSERR, "Unable to get PMC status for CPU %d",
			    cpu);
		}

		printf("#CPU %d:\n", c++);
		npmc = pmc_npmc(cpu);
		printf("#N  NAME             CLASS  STATE    ROW-DISP\n");

		for (n = 0; n < npmc; n++) {
			pd = &pi->pm_pmcs[n];

			printf(" %-2d %-16s %-6s %-8s %-10s",
			    n,
			    pd->pm_name,
			    pmc_name_of_class(pd->pm_class),
			    pd->pm_enabled ? "ENABLED" : "DISABLED",
			    pmc_name_of_disposition(pd->pm_rowdisp));

			if (pd->pm_ownerpid != -1) {
			        printf(" (pid %d)", pd->pm_ownerpid);
				printf(" %-32s",
				    pmc_name_of_event(pd->pm_event));
				if (PMC_IS_SAMPLING_MODE(pd->pm_mode))
					printf(" (reload count %jd)",
					    pd->pm_reloadcount);
			}
			printf("\n");
		}
		free(pi);
	}
	return 0;
}

#if defined(__i386__) || defined(__amd64__)
static int
pmcc_do_list_events(void)
{
	pmc_pmu_print_counters(NULL);
	return (0);
}
#else
static int
pmcc_do_list_events(void)
{
	enum pmc_class c;
	unsigned int i, j, nevents;
	const char **eventnamelist;
	const struct pmc_cpuinfo *ci;

	if (pmc_cpuinfo(&ci) != 0)
		err(EX_OSERR, "Unable to determine CPU information");

	eventnamelist = NULL;

	for (i = 0; i < ci->pm_nclass; i++) {
		c = ci->pm_classes[i].pm_class;

		printf("%s\n", pmc_name_of_class(c));
		if (pmc_event_names_of_class(c, &eventnamelist, &nevents) < 0)
			err(EX_OSERR,
"ERROR: Cannot find information for event class \"%s\"",
			    pmc_name_of_class(c));

		for (j = 0; j < nevents; j++)
			printf("\t%s\n", eventnamelist[j]);

		free(eventnamelist);
	}
	return 0;
}
#endif

static int
pmcc_show_statistics(void)
{

	struct pmc_driverstats gms;

	if (pmc_get_driver_stats(&gms) < 0)
		err(EX_OSERR, "ERROR: cannot retrieve driver statistics");

	/*
	 * Print statistics.
	 */

#define	PRINT(N,V)	(void) printf("%-40s %d\n", (N), gms.pm_##V)
	PRINT("interrupts processed:", intr_processed);
	PRINT("non-PMC interrupts:", intr_ignored);
	PRINT("sampling stalls due to space shortages:", intr_bufferfull);
	PRINT("system calls:", syscalls);
	PRINT("system calls with errors:", syscall_errors);
	PRINT("buffer requests:", buffer_requests);
	PRINT("buffer requests failed:", buffer_requests_failed);
	PRINT("sampling log sweeps:", log_sweeps);

	return 0;
}

/*
 * Main
 */

int
main(int argc, char **argv)
{
	int error, command, currentcpu, option, pmc;
	char *dummy;
	struct pmcc_op *p;

#if DEBUG
	pmcc_init_debug();
#endif

	/* parse args */

	currentcpu = PMCC_CPU_ALL;
	command    = PMCC_PRINT_USAGE;
	error      = 0;

	STAILQ_INIT(&head);

	while ((option = getopt(argc, argv, ":c:d:e:lLs")) != -1)
		switch (option) {
		case 'L':
			if (command != PMCC_PRINT_USAGE) {
				error = 1;
				break;
			}
			command = PMCC_PRINT_EVENTS;
			break;

		case 'c':
			if (command != PMCC_PRINT_USAGE &&
			    command != PMCC_ENABLE_DISABLE) {
				error = 1;
				break;
			}
			command = PMCC_ENABLE_DISABLE;

			if (*optarg == PMCC_CPU_WILDCARD)
				currentcpu = PMCC_CPU_ALL;
			else {
				currentcpu = strtoul(optarg, &dummy, 0);
				if (*dummy != '\0' || currentcpu < 0)
					errx(EX_DATAERR,
					    "\"%s\" is not a valid CPU id",
					    optarg);
			}
			break;

		case 'd':
		case 'e':
			if (command != PMCC_PRINT_USAGE &&
			    command != PMCC_ENABLE_DISABLE) {
				error = 1;
				break;
			}
			command = PMCC_ENABLE_DISABLE;

			if (*optarg == PMCC_PMC_WILDCARD)
				pmc = PMCC_PMC_ALL;
			else {
				pmc = strtoul(optarg, &dummy, 0);
				if (*dummy != '\0' || pmc < 0)
					errx(EX_DATAERR,
					    "\"%s\" is not a valid PMC id",
					    optarg);
			}

			if ((p = malloc(sizeof(*p))) == NULL)
				err(EX_SOFTWARE, "Out of memory");

			p->op_cpu = currentcpu;
			p->op_pmc = pmc;
			p->op_op  = option == 'd' ? PMCC_OP_DISABLE :
			    PMCC_OP_ENABLE;

			STAILQ_INSERT_TAIL(&head, p, op_next);
			break;

		case 'l':
			if (command != PMCC_PRINT_USAGE) {
				error = 1;
				break;
			}
			command = PMCC_LIST_STATE;
			break;

		case 's':
			if (command != PMCC_PRINT_USAGE) {
				error = 1;
				break;
			}
			command = PMCC_SHOW_STATISTICS;
			break;

		case ':':
			errx(EX_USAGE,
			    "Missing argument to option '-%c'", optopt);
			break;

		case '?':
			warnx("Unrecognized option \"-%c\"", optopt);
			errx(EX_USAGE, "%s", usage_message);
			break;

		default:
			error = 1;
			break;

		}

	if (command == PMCC_PRINT_USAGE)
		(void) errx(EX_USAGE, "%s", usage_message);

	if (error)
		exit(EX_USAGE);

	if (pmc_init() < 0)
		err(EX_UNAVAILABLE,
		    "Initialization of the pmc(3) library failed");

	switch (command) {
	case PMCC_LIST_STATE:
		error = pmcc_do_list_state();
		break;
	case PMCC_PRINT_EVENTS:
		error = pmcc_do_list_events();
		break;
	case PMCC_SHOW_STATISTICS:
		error = pmcc_show_statistics();
		break;
	case PMCC_ENABLE_DISABLE:
		if (STAILQ_EMPTY(&head))
			errx(EX_USAGE,
			    "No PMCs specified to enable or disable");
		error = pmcc_do_enable_disable(&head);
		break;
	default:
		assert(0);

	}

	if (error != 0)
		err(EX_OSERR, "Command failed");
	exit(0);
}
