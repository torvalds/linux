/*-
 * Copyright (c) 2014-2015 Netflix, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <getopt.h>
#include "eval_expr.h"
__FBSDID("$FreeBSD$");

static int max_pmc_counters = 1;
static int run_all = 0;

#define MAX_COUNTER_SLOTS 1024
#define MAX_NLEN 64
#define MAX_CPU 64
static int verbose = 0;

extern char **environ;
extern struct expression *master_exp;
struct expression *master_exp=NULL;

#define PMC_INITIAL_ALLOC 512
extern char **valid_pmcs;
char **valid_pmcs = NULL;
extern int valid_pmc_cnt;
int valid_pmc_cnt=0;
extern int pmc_allocated_cnt;
int pmc_allocated_cnt=0;

/*
 * The following two varients on popen and pclose with
 * the cavet that they get you the PID so that you
 * can supply it to pclose so it can send a SIGTERM 
 *  to the process.
 */
static FILE *
my_popen(const char *command, const char *dir, pid_t *p_pid)
{
	FILE *io_out, *io_in;
	int pdesin[2], pdesout[2];
	char *argv[4];
	pid_t pid;
	char cmd[4];
	char cmd2[1024];
	char arg1[4];

	if ((strcmp(dir, "r") != 0) &&
	    (strcmp(dir, "w") != 0)) {
		errno = EINVAL;
		return(NULL);
	}
	if (pipe(pdesin) < 0)
		return (NULL);

	if (pipe(pdesout) < 0) {
		(void)close(pdesin[0]);
		(void)close(pdesin[1]);
		return (NULL);
	}
	strcpy(cmd, "sh");
	strcpy(arg1, "-c");
	strcpy(cmd2, command);
	argv[0] = cmd;
	argv[1] = arg1;
	argv[2] = cmd2;
	argv[3] = NULL;

	switch (pid = fork()) {
	case -1:			/* Error. */
		(void)close(pdesin[0]);
		(void)close(pdesin[1]);
		(void)close(pdesout[0]);
		(void)close(pdesout[1]);
		return (NULL);
		/* NOTREACHED */
	case 0:				/* Child. */
		/* Close out un-used sides */
		(void)close(pdesin[1]);
		(void)close(pdesout[0]);
		/* Now prepare the stdin of the process */
		close(0);
		(void)dup(pdesin[0]);
		(void)close(pdesin[0]);
		/* Now prepare the stdout of the process */
		close(1);
		(void)dup(pdesout[1]);
		/* And lets do stderr just in case */
		close(2);
		(void)dup(pdesout[1]);
		(void)close(pdesout[1]);
		/* Now run it */
		execve("/bin/sh", argv, environ);
		exit(127);
		/* NOTREACHED */
	}
	/* Parent; assume fdopen can't fail. */
	/* Store the pid */
	*p_pid = pid;
	if (strcmp(dir, "r") != 0) {
		io_out = fdopen(pdesin[1], "w");
		(void)close(pdesin[0]);
		(void)close(pdesout[0]);
		(void)close(pdesout[1]);
		return(io_out);
 	} else {
		/* Prepare the input stream */
		io_in = fdopen(pdesout[0], "r");
		(void)close(pdesout[1]);
		(void)close(pdesin[0]);
		(void)close(pdesin[1]);
		return (io_in);
	}
}

/*
 * pclose --
 *	Pclose returns -1 if stream is not associated with a `popened' command,
 *	if already `pclosed', or waitpid returns an error.
 */
static void
my_pclose(FILE *io, pid_t the_pid)
{
	int pstat;
	pid_t pid;

	/*
	 * Find the appropriate file pointer and remove it from the list.
	 */
	(void)fclose(io);
	/* Die if you are not dead! */
	kill(the_pid, SIGTERM);
	do {
		pid = wait4(the_pid, &pstat, 0, (struct rusage *)0);
	} while (pid == -1 && errno == EINTR);
}

struct counters {
	struct counters *next_cpu;
	char counter_name[MAX_NLEN];		/* Name of counter */
	int cpu;				/* CPU we are on */
	int pos;				/* Index we are filling to. */
	uint64_t vals[MAX_COUNTER_SLOTS];	/* Last 64 entries */
	uint64_t sum;				/* Summary of entries */
};

extern struct counters *glob_cpu[MAX_CPU];
struct counters *glob_cpu[MAX_CPU];

extern struct counters *cnts;
struct counters *cnts=NULL;

extern int ncnts;
int ncnts=0;

extern int (*expression)(struct counters *, int);
int (*expression)(struct counters *, int);

static const char *threshold=NULL;
static const char *command;

struct cpu_entry {
	const char *name;
	const char *thresh;
	const char *command;
	int (*func)(struct counters *, int);
	int counters_required;
};

struct cpu_type {
	char cputype[32];
	int number;
	struct cpu_entry *ents;
	void (*explain)(const char *name);
};
extern struct cpu_type the_cpu;
struct cpu_type the_cpu;

static void
explain_name_sb(const char *name)
{
	const char *mythresh;
	if (strcmp(name, "allocstall1") == 0) {
		printf("Examine PARTIAL_RAT_STALLS.SLOW_LEA_WINDOW / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh > .05";
	} else if (strcmp(name, "allocstall2") == 0) {
		printf("Examine PARTIAL_RAT_STALLS.FLAGS_MERGE_UOP_CYCLES/CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh > .05";
	} else if (strcmp(name, "br_miss") == 0) {
		printf("Examine (20 * BR_MISP_RETIRED.ALL_BRANCHES)/CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .2";
	} else if (strcmp(name, "splitload") == 0) {
		printf("Examine MEM_UOPS_RETIRED.SPLIT_LOADS * 5) / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .1";
	} else if (strcmp(name, "splitstore") == 0) {
		printf("Examine MEM_UOPS_RETIRED.SPLIT_STORES / MEM_UOPS_RETIRED.ALL_STORES\n");
		mythresh = "thresh >= .01";
	} else if (strcmp(name, "contested") == 0) {
		printf("Examine (MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HITM * 60) / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .05";
	} else if (strcmp(name, "blockstorefwd") == 0) {
		printf("Examine (LD_BLOCKS_STORE_FORWARD * 13) / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .05";
	} else if (strcmp(name, "cache2") == 0) {
		printf("Examine ((MEM_LOAD_RETIRED.L3_HIT * 26) + \n");
		printf("         (MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HIT * 43) + \n");
		printf("         (MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HITM * 60)) / CPU_CLK_UNHALTED.THREAD_P\n");
		printf("**Note we have it labeled MEM_LOAD_UOPS_RETIRED.LLC_HIT not MEM_LOAD_RETIRED.L3_HIT\n");
		mythresh = "thresh >= .2";
	} else if (strcmp(name, "cache1") == 0) {
		printf("Examine (MEM_LOAD_UOPS_MISC_RETIRED.LLC_MISS * 180) / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .2";
	} else if (strcmp(name, "dtlbmissload") == 0) {
		printf("Examine (((DTLB_LOAD_MISSES.STLB_HIT * 7) + DTLB_LOAD_MISSES.WALK_DURATION)\n");
		printf("         / CPU_CLK_UNHALTED.THREAD_P)\n");
		mythresh = "thresh >= .1";
	} else if (strcmp(name, "frontendstall") == 0) {
		printf("Examine IDQ_UOPS_NOT_DELIVERED.CORE / (CPU_CLK_UNHALTED.THREAD_P * 4)\n");
		mythresh = "thresh >= .15";
	} else if (strcmp(name, "clears") == 0) {
		printf("Examine ((MACHINE_CLEARS.MEMORY_ORDERING + \n");
		printf("          MACHINE_CLEARS.SMC + \n");
		printf("          MACHINE_CLEARS.MASKMOV ) * 100 ) / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .02";
	} else if (strcmp(name, "microassist") == 0) {
		printf("Examine IDQ.MS_CYCLES / (CPU_CLK_UNHALTED.THREAD_P * 4)\n");
		printf("***We use IDQ.MS_UOPS,cmask=1 to get cycles\n");
		mythresh = "thresh >= .05";
	} else if (strcmp(name, "aliasing_4k") == 0) {
		printf("Examine (LD_BLOCKS_PARTIAL.ADDRESS_ALIAS * 5) / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .1";
	} else if (strcmp(name, "fpassist") == 0) {
		printf("Examine FP_ASSIST.ANY/INST_RETIRED.ANY_P\n");
		mythresh = "look for a excessive value";
	} else if (strcmp(name, "otherassistavx") == 0) {
		printf("Examine (OTHER_ASSISTS.AVX_TO_SSE * 75)/CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "look for a excessive value";
	} else if (strcmp(name, "otherassistsse") == 0) {
		printf("Examine (OTHER_ASSISTS.SSE_TO_AVX * 75)/CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "look for a excessive value";
	} else if (strcmp(name, "eff1") == 0) {
		printf("Examine (UOPS_RETIRED.RETIRE_SLOTS)/(4 *CPU_CLK_UNHALTED.THREAD_P)\n");
		mythresh = "thresh < .9";
	} else if (strcmp(name, "eff2") == 0) {
		printf("Examine CPU_CLK_UNHALTED.THREAD_P/INST_RETIRED.ANY_P\n");
		mythresh = "thresh > 1.0";
	} else if (strcmp(name, "dtlbmissstore") == 0) {
		printf("Examine (((DTLB_STORE_MISSES.STLB_HIT * 7) + DTLB_STORE_MISSES.WALK_DURATION)\n");
		printf("         / CPU_CLK_UNHALTED.THREAD_P)\n");
		mythresh = "thresh >= .05";
	} else {
		printf("Unknown name:%s\n", name);
		mythresh = "unknown entry";
        }
	printf("If the value printed is %s we may have the ability to improve performance\n", mythresh);
}

static void
explain_name_ib(const char *name)
{
	const char *mythresh;
	if (strcmp(name, "br_miss") == 0) {
		printf("Examine ((BR_MISP_RETIRED.ALL_BRANCHES /(BR_MISP_RETIRED.ALL_BRANCHES +\n");
		printf("         MACHINE_CLEAR.COUNT) * ((UOPS_ISSUED.ANY - UOPS_RETIRED.RETIRE_SLOTS + 4 * INT_MISC.RECOVERY_CYCLES)\n");
		printf("/ (4 * CPU_CLK_UNHALTED.THREAD))))\n");
		mythresh = "thresh >= .2";
	} else if (strcmp(name, "eff1") == 0) {
		printf("Examine (UOPS_RETIRED.RETIRE_SLOTS)/(4 *CPU_CLK_UNHALTED.THREAD_P)\n");
		mythresh = "thresh < .9";
	} else if (strcmp(name, "eff2") == 0) {
		printf("Examine CPU_CLK_UNHALTED.THREAD_P/INST_RETIRED.ANY_P\n");
		mythresh = "thresh > 1.0";
	} else if (strcmp(name, "cache1") == 0) {
		printf("Examine (MEM_LOAD_UOPS_LLC_MISS_RETIRED.LOCAL_DRAM * 180) / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .2";
	} else if (strcmp(name, "cache2") == 0) {
		printf("Examine (MEM_LOAD_UOPS_RETIRED.LLC_HIT / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .2";
	} else if (strcmp(name, "itlbmiss") == 0) {
		printf("Examine ITLB_MISSES.WALK_DURATION / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh > .05"; 
	} else if (strcmp(name, "icachemiss") == 0) {
		printf("Examine (ICACHE.IFETCH_STALL - ITLB_MISSES.WALK_DURATION)/ CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh > .05";
	} else if (strcmp(name, "lcpstall") == 0) {
		printf("Examine ILD_STALL.LCP/CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh > .05";
	} else if (strcmp(name, "datashare") == 0) {
		printf("Examine (MEM_LOAD_UOPS_L3_HIT_RETIRED.XSNP_HIT * 43)/CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh > .05";
	} else if (strcmp(name, "blockstorefwd") == 0) {
		printf("Examine (LD_BLOCKS_STORE_FORWARD * 13) / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .05";
	} else if (strcmp(name, "splitload") == 0) {
		printf("Examine  ((L1D_PEND_MISS.PENDING / MEM_LOAD_UOPS_RETIRED.L1_MISS) *\n");
		printf("         LD_BLOCKS.NO_SR)/CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .1";
	} else if (strcmp(name, "splitstore") == 0) {
		printf("Examine MEM_UOPS_RETIRED.SPLIT_STORES / MEM_UOPS_RETIRED.ALL_STORES\n");
		mythresh = "thresh >= .01";
	} else if (strcmp(name, "aliasing_4k") == 0) {
		printf("Examine (LD_BLOCKS_PARTIAL.ADDRESS_ALIAS * 5) / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .1";
	} else if (strcmp(name, "dtlbmissload") == 0) {
		printf("Examine (((DTLB_LOAD_MISSES.STLB_HIT * 7) + DTLB_LOAD_MISSES.WALK_DURATION)\n");
		printf("         / CPU_CLK_UNHALTED.THREAD_P)\n");
		mythresh = "thresh >= .1";
	} else if (strcmp(name, "dtlbmissstore") == 0) {
		printf("Examine (((DTLB_STORE_MISSES.STLB_HIT * 7) + DTLB_STORE_MISSES.WALK_DURATION)\n");
		printf("         / CPU_CLK_UNHALTED.THREAD_P)\n");
		mythresh = "thresh >= .05";
	} else if (strcmp(name, "contested") == 0) {
		printf("Examine (MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HITM * 60) / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .05";
	} else if (strcmp(name, "clears") == 0) {
		printf("Examine ((MACHINE_CLEARS.MEMORY_ORDERING + \n");
		printf("          MACHINE_CLEARS.SMC + \n");
		printf("          MACHINE_CLEARS.MASKMOV ) * 100 ) / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .02";
	} else if (strcmp(name, "microassist") == 0) {
		printf("Examine IDQ.MS_CYCLES / (4 * CPU_CLK_UNHALTED.THREAD_P)\n");
		printf("***We use IDQ.MS_UOPS,cmask=1 to get cycles\n");
		mythresh = "thresh >= .05";
	} else if (strcmp(name, "fpassist") == 0) {
		printf("Examine FP_ASSIST.ANY/INST_RETIRED.ANY_P\n");
		mythresh = "look for a excessive value";
	} else if (strcmp(name, "otherassistavx") == 0) {
		printf("Examine (OTHER_ASSISTS.AVX_TO_SSE * 75)/CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "look for a excessive value";
	} else if (strcmp(name, "otherassistsse") == 0) {
		printf("Examine (OTHER_ASSISTS.SSE_TO_AVX * 75)/CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "look for a excessive value";
	} else {
		printf("Unknown name:%s\n", name);
		mythresh = "unknown entry";
        }
	printf("If the value printed is %s we may have the ability to improve performance\n", mythresh);
}


static void
explain_name_has(const char *name)
{
	const char *mythresh;
	if (strcmp(name, "eff1") == 0) {
		printf("Examine (UOPS_RETIRED.RETIRE_SLOTS)/(4 *CPU_CLK_UNHALTED.THREAD_P)\n");
		mythresh = "thresh < .75";
	} else if (strcmp(name, "eff2") == 0) {
		printf("Examine CPU_CLK_UNHALTED.THREAD_P/INST_RETIRED.ANY_P\n");
		mythresh = "thresh > 1.0";
	} else if (strcmp(name, "itlbmiss") == 0) {
		printf("Examine ITLB_MISSES.WALK_DURATION / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh > .05"; 
	} else if (strcmp(name, "icachemiss") == 0) {
		printf("Examine (36 * ICACHE.MISSES)/ CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh > .05";
	} else if (strcmp(name, "lcpstall") == 0) {
		printf("Examine ILD_STALL.LCP/CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh > .05";
	} else if (strcmp(name, "cache1") == 0) {
		printf("Examine (MEM_LOAD_UOPS_LLC_MISS_RETIRED.LOCAL_DRAM * 180) / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .2";
	} else if (strcmp(name, "cache2") == 0) {
		printf("Examine ((MEM_LOAD_UOPS_RETIRED.LLC_HIT * 36) + \n");
		printf("         (MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HIT * 72) + \n");
		printf("         (MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HITM * 84))\n");
		printf("          / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .2";
	} else if (strcmp(name, "contested") == 0) {
		printf("Examine (MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HITM * 84) / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .05";
	} else if (strcmp(name, "datashare") == 0) {
		printf("Examine (MEM_LOAD_UOPS_L3_HIT_RETIRED.XSNP_HIT * 72)/CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh > .05";
	} else if (strcmp(name, "blockstorefwd") == 0) {
		printf("Examine (LD_BLOCKS_STORE_FORWARD * 13) / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .05";
	} else if (strcmp(name, "splitload") == 0) {
		printf("Examine  (MEM_UOPS_RETIRED.SPLIT_LOADS * 5) / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .1";
	} else if (strcmp(name, "splitstore") == 0) {
		printf("Examine MEM_UOPS_RETIRED.SPLIT_STORES / MEM_UOPS_RETIRED.ALL_STORES\n");
		mythresh = "thresh >= .01";
	} else if (strcmp(name, "aliasing_4k") == 0) {
		printf("Examine (LD_BLOCKS_PARTIAL.ADDRESS_ALIAS * 5) / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .1";
	} else if (strcmp(name, "dtlbmissload") == 0) {
		printf("Examine (((DTLB_LOAD_MISSES.STLB_HIT * 7) + DTLB_LOAD_MISSES.WALK_DURATION)\n");
		printf("         / CPU_CLK_UNHALTED.THREAD_P)\n");
		mythresh = "thresh >= .1";
	} else if (strcmp(name, "br_miss") == 0) {
		printf("Examine (20 * BR_MISP_RETIRED.ALL_BRANCHES)/CPU_CLK_UNHALTED.THREAD\n");
		mythresh = "thresh >= .2";
	} else if (strcmp(name, "clears") == 0) {
		printf("Examine ((MACHINE_CLEARS.MEMORY_ORDERING + \n");
		printf("          MACHINE_CLEARS.SMC + \n");
		printf("          MACHINE_CLEARS.MASKMOV ) * 100 ) / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .02";
	} else if (strcmp(name, "microassist") == 0) {
		printf("Examine IDQ.MS_CYCLES / (4 * CPU_CLK_UNHALTED.THREAD_P)\n");
		printf("***We use IDQ.MS_UOPS,cmask=1 to get cycles\n");
		mythresh = "thresh >= .05";
	} else if (strcmp(name, "fpassist") == 0) {
		printf("Examine FP_ASSIST.ANY/INST_RETIRED.ANY_P\n");
		mythresh = "look for a excessive value";
	} else if (strcmp(name, "otherassistavx") == 0) {
		printf("Examine (OTHER_ASSISTS.AVX_TO_SSE * 75)/CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "look for a excessive value";
	} else if (strcmp(name, "otherassistsse") == 0) {
		printf("Examine (OTHER_ASSISTS.SSE_TO_AVX * 75)/CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "look for a excessive value";
	} else {
		printf("Unknown name:%s\n", name);
		mythresh = "unknown entry";
        }
	printf("If the value printed is %s we may have the ability to improve performance\n", mythresh);
}



static struct counters *
find_counter(struct counters *base, const char *name)
{
	struct counters *at;
	int len;

	at = base;
	len = strlen(name);
	while(at) {
		if (strncmp(at->counter_name, name, len) == 0) {
			return(at);
		}
		at = at->next_cpu;
	}
	printf("Can't find counter %s\n", name);
	printf("We have:\n");
	at = base;
	while(at) {
		printf("- %s\n", at->counter_name);
		at = at->next_cpu;
	}
	exit(-1);
}

static int
allocstall1(struct counters *cpu, int pos)
{
/*  1  - PARTIAL_RAT_STALLS.SLOW_LEA_WINDOW/CPU_CLK_UNHALTED.THREAD_P (thresh > .05)*/
	int ret;
	struct counters *partial;
	struct counters *unhalt;
	double un, par, res;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	partial = find_counter(cpu, "PARTIAL_RAT_STALLS.SLOW_LEA_WINDOW");
	if (pos != -1) {
		par = partial->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		par = partial->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = par/un;
	ret = printf("%1.3f", res);
	return(ret);
}

static int
allocstall2(struct counters *cpu, int pos)
{
/*  2  - PARTIAL_RAT_STALLS.FLAGS_MERGE_UOP_CYCLES/CPU_CLK_UNHALTED.THREAD_P (thresh >.05) */
	int ret;
	struct counters *partial;
	struct counters *unhalt;
	double un, par, res;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	partial = find_counter(cpu, "PARTIAL_RAT_STALLS.FLAGS_MERGE_UOP");
	if (pos != -1) {
		par = partial->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		par = partial->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = par/un;
	ret = printf("%1.3f", res);
	return(ret);
}

static int
br_mispredict(struct counters *cpu, int pos)
{
	struct counters *brctr;
	struct counters *unhalt;
	int ret;
/*  3  - (20 * BR_MISP_RETIRED.ALL_BRANCHES)/CPU_CLK_UNHALTED.THREAD_P (thresh >= .2) */
	double br, un, con, res;
	con = 20.0;
	
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
        brctr = find_counter(cpu, "BR_MISP_RETIRED.ALL_BRANCHES");
	if (pos != -1) {
		br = brctr->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		br = brctr->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = (con * br)/un;
 	ret = printf("%1.3f", res);
	return(ret);
}

static int
br_mispredictib(struct counters *cpu, int pos)
{
	struct counters *brctr;
	struct counters *unhalt;
	struct counters *clear, *clear2, *clear3;
	struct counters *uops;
	struct counters *recv;	
	struct counters *iss;
/*	  "pmcstat -s CPU_CLK_UNHALTED.THREAD_P -s BR_MISP_RETIRED.ALL_BRANCHES -s MACHINE_CLEARS.MEMORY_ORDERING -s MACHINE_CLEARS.SMC -s MACHINE_CLEARS.MASKMOV -s UOPS_ISSUED.ANY -s UOPS_RETIRED.RETIRE_SLOTS -s INT_MISC.RECOVERY_CYCLES -w 1",*/
	int ret;
        /*  
	 * (BR_MISP_RETIRED.ALL_BRANCHES / 
	 *         (BR_MISP_RETIRED.ALL_BRANCHES +
	 *          MACHINE_CLEAR.COUNT) * 
	 *	   ((UOPS_ISSUED.ANY - UOPS_RETIRED.RETIRE_SLOTS + 4 * INT_MISC.RECOVERY_CYCLES) / (4 * CPU_CLK_UNHALTED.THREAD)))
	 *
	 */
	double br, cl, cl2, cl3, uo, re, un, con, res, is;
	con = 4.0;
	
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
        brctr = find_counter(cpu, "BR_MISP_RETIRED.ALL_BRANCHES");
	clear = find_counter(cpu, "MACHINE_CLEARS.MEMORY_ORDERING");
	clear2 = find_counter(cpu, "MACHINE_CLEARS.SMC");
	clear3 = find_counter(cpu, "MACHINE_CLEARS.MASKMOV");
	uops = find_counter(cpu, "UOPS_RETIRED.RETIRE_SLOTS");
	iss = find_counter(cpu, "UOPS_ISSUED.ANY");
	recv = find_counter(cpu, "INT_MISC.RECOVERY_CYCLES");
	if (pos != -1) {
		br = brctr->vals[pos] * 1.0;
		cl = clear->vals[pos] * 1.0;
		cl2 = clear2->vals[pos] * 1.0;
		cl3 = clear3->vals[pos] * 1.0;
		uo = uops->vals[pos] * 1.0;
		re = recv->vals[pos] * 1.0;
		is = iss->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		br = brctr->sum * 1.0;
		cl = clear->sum * 1.0;
		cl2 = clear2->sum * 1.0;
		cl3 = clear3->sum * 1.0;
		uo = uops->sum * 1.0;
		re = recv->sum * 1.0;
		is = iss->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = (br/(br + cl + cl2 + cl3) * ((is - uo + con * re) / (con * un)));
 	ret = printf("%1.3f", res);
	return(ret);
}


static int
br_mispredict_broad(struct counters *cpu, int pos)
{
	struct counters *brctr;
	struct counters *unhalt;
	struct counters *clear;
	struct counters *uops;
	struct counters *uops_ret;
	struct counters *recv;
	int ret;
	double br, cl, uo, uo_r, re, con, un, res;

	con = 4.0;
	
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
        brctr = find_counter(cpu, "BR_MISP_RETIRED.ALL_BRANCHES");
	clear = find_counter(cpu, "MACHINE_CLEARS.CYCLES");
	uops = find_counter(cpu, "UOPS_ISSUED.ANY");
	uops_ret = find_counter(cpu, "UOPS_RETIRED.RETIRE_SLOTS");
	recv = find_counter(cpu, "INT_MISC.RECOVERY_CYCLES");

	if (pos != -1) {
		un = unhalt->vals[pos] * 1.0;
		br = brctr->vals[pos] * 1.0;
		cl = clear->vals[pos] * 1.0;
		uo = uops->vals[pos] * 1.0;
		uo_r = uops_ret->vals[pos] * 1.0;
		re = recv->vals[pos] * 1.0;
	} else {
		un = unhalt->sum * 1.0;
		br = brctr->sum * 1.0;
		cl = clear->sum * 1.0;
		uo = uops->sum * 1.0;
		uo_r = uops_ret->sum * 1.0;
		re = recv->sum * 1.0;
	}
	res = br / (br + cl) * (uo - uo_r + con * re) / (un * con);
 	ret = printf("%1.3f", res);
	return(ret);
}

static int
splitloadib(struct counters *cpu, int pos)
{
	int ret;
	struct counters *mem;
	struct counters *l1d, *ldblock;
	struct counters *unhalt;
	double un, memd, res, l1, ldb;
        /*  
	 * ((L1D_PEND_MISS.PENDING / MEM_LOAD_UOPS_RETIRED.L1_MISS) * LD_BLOCKS.NO_SR) / CPU_CLK_UNHALTED.THREAD_P
	 * "pmcstat -s CPU_CLK_UNHALTED.THREAD_P -s L1D_PEND_MISS.PENDING -s MEM_LOAD_UOPS_RETIRED.L1_MISS -s LD_BLOCKS.NO_SR -w 1",
	 */

	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	mem = find_counter(cpu, "MEM_LOAD_UOPS_RETIRED.L1_MISS");
	l1d = find_counter(cpu, "L1D_PEND_MISS.PENDING");
	ldblock = find_counter(cpu, "LD_BLOCKS.NO_SR");
	if (pos != -1) {
		memd = mem->vals[pos] * 1.0;
		l1 = l1d->vals[pos] * 1.0;
		ldb = ldblock->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		memd = mem->sum * 1.0;
		l1 = l1d->sum * 1.0;
		ldb = ldblock->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = ((l1 / memd) * ldb)/un;
	ret = printf("%1.3f", res);
	return(ret);
}


static int
splitload(struct counters *cpu, int pos)
{
	int ret;
	struct counters *mem;
	struct counters *unhalt;
	double con, un, memd, res;
/*  4  - (MEM_UOPS_RETIRED.SPLIT_LOADS * 5) / CPU_CLK_UNHALTED.THREAD_P (thresh >= .1)*/

	con = 5.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	mem = find_counter(cpu, "MEM_UOPS_RETIRED.SPLIT_LOADS");
	if (pos != -1) {
		memd = mem->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		memd = mem->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = (memd * con)/un;
	ret = printf("%1.3f", res);
	return(ret);
}


static int
splitload_sb(struct counters *cpu, int pos)
{
	int ret;
	struct counters *mem;
	struct counters *unhalt;
	double con, un, memd, res;
/*  4  - (MEM_UOP_RETIRED.SPLIT_LOADS * 5) / CPU_CLK_UNHALTED.THREAD_P (thresh >= .1)*/

	con = 5.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	mem = find_counter(cpu, "MEM_UOP_RETIRED.SPLIT_LOADS");
	if (pos != -1) {
		memd = mem->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		memd = mem->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = (memd * con)/un;
	ret = printf("%1.3f", res);
	return(ret);
}


static int
splitstore_sb(struct counters *cpu, int pos)
{
        /*  5  - MEM_UOP_RETIRED.SPLIT_STORES / MEM_UOP_RETIRED.ALL_STORES (thresh > 0.01) */
	int ret;
	struct counters *mem_split;
	struct counters *mem_stores;
	double memsplit, memstore, res;
	mem_split = find_counter(cpu, "MEM_UOP_RETIRED.SPLIT_STORES");
	mem_stores = find_counter(cpu, "MEM_UOP_RETIRED.ALL_STORES");
	if (pos != -1) {
		memsplit = mem_split->vals[pos] * 1.0;
		memstore = mem_stores->vals[pos] * 1.0;
	} else {
		memsplit = mem_split->sum * 1.0;
		memstore = mem_stores->sum * 1.0;
	}
	res = memsplit/memstore;
	ret = printf("%1.3f", res);
	return(ret);
}



static int
splitstore(struct counters *cpu, int pos)
{
        /*  5  - MEM_UOPS_RETIRED.SPLIT_STORES / MEM_UOPS_RETIRED.ALL_STORES (thresh > 0.01) */
	int ret;
	struct counters *mem_split;
	struct counters *mem_stores;
	double memsplit, memstore, res;
	mem_split = find_counter(cpu, "MEM_UOPS_RETIRED.SPLIT_STORES");
	mem_stores = find_counter(cpu, "MEM_UOPS_RETIRED.ALL_STORES");
	if (pos != -1) {
		memsplit = mem_split->vals[pos] * 1.0;
		memstore = mem_stores->vals[pos] * 1.0;
	} else {
		memsplit = mem_split->sum * 1.0;
		memstore = mem_stores->sum * 1.0;
	}
	res = memsplit/memstore;
	ret = printf("%1.3f", res);
	return(ret);
}


static int
contested(struct counters *cpu, int pos)
{
        /*  6  - (MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HITM * 60) / CPU_CLK_UNHALTED.THREAD_P (thresh >.05) */
	int ret;
	struct counters *mem;
	struct counters *unhalt;
	double con, un, memd, res;

	con = 60.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	mem = find_counter(cpu, "MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HITM");
	if (pos != -1) {
		memd = mem->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		memd = mem->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = (memd * con)/un;
	ret = printf("%1.3f", res);
	return(ret);
}

static int
contested_has(struct counters *cpu, int pos)
{
        /*  6  - (MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HITM * 84) / CPU_CLK_UNHALTED.THREAD_P (thresh >.05) */
	int ret;
	struct counters *mem;
	struct counters *unhalt;
	double con, un, memd, res;

	con = 84.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	mem = find_counter(cpu, "MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HITM");
	if (pos != -1) {
		memd = mem->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		memd = mem->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = (memd * con)/un;
	ret = printf("%1.3f", res);
	return(ret);
}

static int
contestedbroad(struct counters *cpu, int pos)
{
        /*  6  - (MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HITM * 84) / CPU_CLK_UNHALTED.THREAD_P (thresh >.05) */
	int ret;
	struct counters *mem;
	struct counters *mem2;
	struct counters *unhalt;
	double con, un, memd, memtoo, res;

	con = 84.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	mem = find_counter(cpu, "MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HITM");
	mem2 = find_counter(cpu,"MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_MISS");

	if (pos != -1) {
		memd = mem->vals[pos] * 1.0;
		memtoo = mem2->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		memd = mem->sum * 1.0;
		memtoo = mem2->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = ((memd * con) + memtoo)/un;
	ret = printf("%1.3f", res);
	return(ret);
}


static int
blockstoreforward(struct counters *cpu, int pos)
{
        /*  7  - (LD_BLOCKS_STORE_FORWARD * 13) / CPU_CLK_UNHALTED.THREAD_P (thresh >= .05)*/
	int ret;
	struct counters *ldb;
	struct counters *unhalt;
	double con, un, ld, res;

	con = 13.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	ldb = find_counter(cpu, "LD_BLOCKS_STORE_FORWARD");
	if (pos != -1) {
		ld = ldb->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		ld = ldb->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = (ld * con)/un;
	ret = printf("%1.3f", res);
	return(ret);
}

static int
cache2(struct counters *cpu, int pos)
{
	/* ** Suspect ***
	 *  8  - ((MEM_LOAD_RETIRED.L3_HIT * 26) + (MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HIT * 43) +
	 *        (MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HITM * 60)) / CPU_CLK_UNHALTED.THREAD_P (thresh >.2)
	 */
	int ret;
	struct counters *mem1, *mem2, *mem3;
	struct counters *unhalt;
	double con1, con2, con3, un, me_1, me_2, me_3, res;

	con1 = 26.0;
	con2 = 43.0;
	con3 = 60.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
/* Call for MEM_LOAD_RETIRED.L3_HIT possibly MEM_LOAD_UOPS_RETIRED.LLC_HIT ?*/
	mem1 = find_counter(cpu, "MEM_LOAD_UOPS_RETIRED.LLC_HIT");
	mem2 = find_counter(cpu, "MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HIT");
	mem3 = find_counter(cpu, "MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HITM");
	if (pos != -1) {
		me_1 = mem1->vals[pos] * 1.0;
		me_2 = mem2->vals[pos] * 1.0;
		me_3 = mem3->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		me_1 = mem1->sum * 1.0;
		me_2 = mem2->sum * 1.0;
		me_3 = mem3->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = ((me_1 * con1) + (me_2 * con2) + (me_3 * con3))/un;
	ret = printf("%1.3f", res);
	return(ret);
}

static int
datasharing(struct counters *cpu, int pos)
{
	/* 
	 * (MEM_LOAD_UOPS_L3_HIT_RETIRED.XSNP_HIT * 43)/ CPU_CLK_UNHALTED.THREAD_P (thresh >.2)
	 */
	int ret;
	struct counters *mem;
	struct counters *unhalt;
	double con, res, me, un;

	con = 43.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	mem = find_counter(cpu, "MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HIT");
	if (pos != -1) {
		me = mem->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		me = mem->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = (me * con)/un;
	ret = printf("%1.3f", res);
	return(ret);

}


static int
datasharing_has(struct counters *cpu, int pos)
{
	/* 
	 * (MEM_LOAD_UOPS_L3_HIT_RETIRED.XSNP_HIT * 43)/ CPU_CLK_UNHALTED.THREAD_P (thresh >.2)
	 */
	int ret;
	struct counters *mem;
	struct counters *unhalt;
	double con, res, me, un;

	con = 72.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	mem = find_counter(cpu, "MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HIT");
	if (pos != -1) {
		me = mem->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		me = mem->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = (me * con)/un;
	ret = printf("%1.3f", res);
	return(ret);

}


static int
cache2ib(struct counters *cpu, int pos)
{
        /*
	 *  (29 * MEM_LOAD_UOPS_RETIRED.LLC_HIT / CPU_CLK_UNHALTED.THREAD_P (thresh >.2)
	 */
	int ret;
	struct counters *mem;
	struct counters *unhalt;
	double con, un, me, res;

	con = 29.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	mem = find_counter(cpu, "MEM_LOAD_UOPS_RETIRED.LLC_HIT");
	if (pos != -1) {
		me = mem->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		me = mem->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = (con * me)/un; 
	ret = printf("%1.3f", res);
	return(ret);
}

static int
cache2has(struct counters *cpu, int pos)
{
	/*
	 * Examine ((MEM_LOAD_UOPS_RETIRED.LLC_HIT * 36) + \
	 *          (MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HIT * 72) +
	 *          (MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HITM * 84))
	 *           / CPU_CLK_UNHALTED.THREAD_P
	 */
	int ret;
	struct counters *mem1, *mem2, *mem3;
	struct counters *unhalt;
	double con1, con2, con3, un, me1, me2, me3, res;

	con1 = 36.0;
	con2 = 72.0;
	con3 = 84.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	mem1 = find_counter(cpu, "MEM_LOAD_UOPS_RETIRED.LLC_HIT");
	mem2 = find_counter(cpu, "MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HIT");
	mem3 = find_counter(cpu, "MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HITM");
	if (pos != -1) {
		me1 = mem1->vals[pos] * 1.0;
		me2 = mem2->vals[pos] * 1.0;
		me3 = mem3->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		me1 = mem1->sum * 1.0;
		me2 = mem2->sum * 1.0;
		me3 = mem3->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = ((me1 * con1) + (me2 * con2) + (me3 * con3))/un;
	ret = printf("%1.3f", res);
	return(ret);
}


static int
cache2broad(struct counters *cpu, int pos)
{
        /*
	 *  (29 * MEM_LOAD_UOPS_RETIRED.LLC_HIT / CPU_CLK_UNHALTED.THREAD_P (thresh >.2)
	 */
	int ret;
	struct counters *mem;
	struct counters *unhalt;
	double con, un, me, res;

	con = 36.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	mem = find_counter(cpu, "MEM_LOAD_UOPS_RETIRED.L3_HIT");
	if (pos != -1) {
		me = mem->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		me = mem->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = (con * me)/un; 
	ret = printf("%1.3f", res);
	return(ret);
}


static int
cache1(struct counters *cpu, int pos)
{
	/*  9  - (MEM_LOAD_UOPS_MISC_RETIRED.LLC_MISS * 180) / CPU_CLK_UNHALTED.THREAD_P (thresh >= .2) */
	int ret;
	struct counters *mem;
	struct counters *unhalt;
	double con, un, me, res;

	con = 180.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	mem = find_counter(cpu, "MEM_LOAD_UOPS_MISC_RETIRED.LLC_MISS");
	if (pos != -1) {
		me = mem->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		me = mem->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = (me * con)/un;
	ret = printf("%1.3f", res);
	return(ret);
}

static int
cache1ib(struct counters *cpu, int pos)
{
	/*  9  - (MEM_LOAD_UOPS_L3_MISS_RETIRED.LCOAL_DRAM * 180) / CPU_CLK_UNHALTED.THREAD_P (thresh >= .2) */
	int ret;
	struct counters *mem;
	struct counters *unhalt;
	double con, un, me, res;

	con = 180.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	mem = find_counter(cpu, "MEM_LOAD_UOPS_LLC_MISS_RETIRED.LOCAL_DRAM");
	if (pos != -1) {
		me = mem->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		me = mem->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = (me * con)/un;
	ret = printf("%1.3f", res);
	return(ret);
}


static int
cache1broad(struct counters *cpu, int pos)
{
	/*  9  - (MEM_LOAD_UOPS_L3_MISS_RETIRED.LCOAL_DRAM * 180) / CPU_CLK_UNHALTED.THREAD_P (thresh >= .2) */
	int ret;
	struct counters *mem;
	struct counters *unhalt;
	double con, un, me, res;

	con = 180.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	mem = find_counter(cpu, "MEM_LOAD_UOPS_RETIRED.L3_MISS");
	if (pos != -1) {
		me = mem->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		me = mem->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = (me * con)/un;
	ret = printf("%1.3f", res);
	return(ret);
}


static int
dtlb_missload(struct counters *cpu, int pos)
{
	/* 10  - ((DTLB_LOAD_MISSES.STLB_HIT * 7) + DTLB_LOAD_MISSES.WALK_DURATION) / CPU_CLK_UNHALTED.THREAD_P (t >=.1) */
	int ret;
	struct counters *dtlb_m, *dtlb_d;
	struct counters *unhalt;
	double con, un, d1, d2, res;

	con = 7.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	dtlb_m = find_counter(cpu, "DTLB_LOAD_MISSES.STLB_HIT");
	dtlb_d = find_counter(cpu, "DTLB_LOAD_MISSES.WALK_DURATION");
	if (pos != -1) {
		d1 = dtlb_m->vals[pos] * 1.0;
		d2 = dtlb_d->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		d1 = dtlb_m->sum * 1.0;
		d2 = dtlb_d->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = ((d1 * con) + d2)/un;
	ret = printf("%1.3f", res);
	return(ret);
}

static int
dtlb_missstore(struct counters *cpu, int pos)
{
        /* 
	 * ((DTLB_STORE_MISSES.STLB_HIT * 7) + DTLB_STORE_MISSES.WALK_DURATION) / 
	 * CPU_CLK_UNHALTED.THREAD_P (t >= .1) 
	 */
        int ret;
        struct counters *dtsb_m, *dtsb_d;
        struct counters *unhalt;
        double con, un, d1, d2, res;

        con = 7.0;
        unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
        dtsb_m = find_counter(cpu, "DTLB_STORE_MISSES.STLB_HIT");
        dtsb_d = find_counter(cpu, "DTLB_STORE_MISSES.WALK_DURATION");
        if (pos != -1) {
                d1 = dtsb_m->vals[pos] * 1.0;
                d2 = dtsb_d->vals[pos] * 1.0;
                un = unhalt->vals[pos] * 1.0;
        } else {
                d1 = dtsb_m->sum * 1.0;
                d2 = dtsb_d->sum * 1.0;
                un = unhalt->sum * 1.0;
        }
        res = ((d1 * con) + d2)/un;
        ret = printf("%1.3f", res);
        return(ret);
}

static int
itlb_miss(struct counters *cpu, int pos)
{
	/* ITLB_MISSES.WALK_DURATION / CPU_CLK_UNTHREAD_P  IB */
	int ret;
	struct counters *itlb;
	struct counters *unhalt;
	double un, d1, res;

	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	itlb = find_counter(cpu, "ITLB_MISSES.WALK_DURATION");
	if (pos != -1) {
		d1 = itlb->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		d1 = itlb->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = d1/un;
	ret = printf("%1.3f", res);
	return(ret);
}


static int
itlb_miss_broad(struct counters *cpu, int pos)
{
	/* (7 * ITLB_MISSES.STLB_HIT_4K + ITLB_MISSES.WALK_DURATION) / CPU_CLK_UNTHREAD_P   */
	int ret;
	struct counters *itlb;
	struct counters *unhalt;
	struct counters *four_k;
	double un, d1, res, k;

	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	itlb = find_counter(cpu, "ITLB_MISSES.WALK_DURATION");
	four_k = find_counter(cpu, "ITLB_MISSES.STLB_HIT_4K");
	if (pos != -1) {
		d1 = itlb->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
		k = four_k->vals[pos] * 1.0;
	} else {
		d1 = itlb->sum * 1.0;
		un = unhalt->sum * 1.0;
		k = four_k->sum * 1.0;
	}
	res = (7.0 * k + d1)/un;
	ret = printf("%1.3f", res);
	return(ret);
}


static int
icache_miss(struct counters *cpu, int pos)
{
	/* (ICACHE.IFETCH_STALL - ITLB_MISSES.WALK_DURATION) / CPU_CLK_UNHALTED.THREAD_P IB */

	int ret;
	struct counters *itlb, *icache;
	struct counters *unhalt;
	double un, d1, ic, res;

	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	itlb = find_counter(cpu, "ITLB_MISSES.WALK_DURATION");
	icache = find_counter(cpu, "ICACHE.IFETCH_STALL");
	if (pos != -1) {
		d1 = itlb->vals[pos] * 1.0;
		ic = icache->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		d1 = itlb->sum * 1.0;
		ic = icache->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = (ic-d1)/un;
	ret = printf("%1.3f", res);
	return(ret);

}

static int
icache_miss_has(struct counters *cpu, int pos)
{
	/* (36 * ICACHE.MISSES) / CPU_CLK_UNHALTED.THREAD_P */

	int ret;
	struct counters *icache;
	struct counters *unhalt;
	double un, con, ic, res;

	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	icache = find_counter(cpu, "ICACHE.MISSES");
	con = 36.0;
	if (pos != -1) {
		ic = icache->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		ic = icache->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = (con * ic)/un;
	ret = printf("%1.3f", res);
	return(ret);

}

static int
lcp_stall(struct counters *cpu, int pos)
{
         /* ILD_STALL.LCP/CPU_CLK_UNHALTED.THREAD_P IB */
	int ret;
	struct counters *ild;
	struct counters *unhalt;
	double un, d1, res;

	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	ild = find_counter(cpu, "ILD_STALL.LCP");
	if (pos != -1) {
		d1 = ild->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		d1 = ild->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = d1/un;
	ret = printf("%1.3f", res);
	return(ret);

}


static int
frontendstall(struct counters *cpu, int pos)
{
      /* 12  -  IDQ_UOPS_NOT_DELIVERED.CORE / (CPU_CLK_UNHALTED.THREAD_P * 4) (thresh >= .15) */
	int ret;
	struct counters *idq;
	struct counters *unhalt;
	double con, un, id, res;

	con = 4.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	idq = find_counter(cpu, "IDQ_UOPS_NOT_DELIVERED.CORE");
	if (pos != -1) {
		id = idq->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		id = idq->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = id/(un * con);
	ret = printf("%1.3f", res);
	return(ret);
}

static int
clears(struct counters *cpu, int pos)
{
	/* 13  - ((MACHINE_CLEARS.MEMORY_ORDERING + MACHINE_CLEARS.SMC + MACHINE_CLEARS.MASKMOV ) * 100 )  
	 *         / CPU_CLK_UNHALTED.THREAD_P (thresh  >= .02)*/
	
	int ret;
	struct counters *clr1, *clr2, *clr3;
	struct counters *unhalt;
	double con, un, cl1, cl2, cl3, res;

	con = 100.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	clr1 = find_counter(cpu, "MACHINE_CLEARS.MEMORY_ORDERING");
	clr2 = find_counter(cpu, "MACHINE_CLEARS.SMC");
	clr3 = find_counter(cpu, "MACHINE_CLEARS.MASKMOV");
	
	if (pos != -1) {
		cl1 = clr1->vals[pos] * 1.0;
		cl2 = clr2->vals[pos] * 1.0;
		cl3 = clr3->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		cl1 = clr1->sum * 1.0;
		cl2 = clr2->sum * 1.0;
		cl3 = clr3->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = ((cl1 + cl2 + cl3) * con)/un;
	ret = printf("%1.3f", res);
	return(ret);
}



static int
clears_broad(struct counters *cpu, int pos)
{
	int ret;
	struct counters *clr1, *clr2, *clr3, *cyc;
	struct counters *unhalt;
	double con, un, cl1, cl2, cl3, cy, res;

	con = 100.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	clr1 = find_counter(cpu, "MACHINE_CLEARS.MEMORY_ORDERING");
	clr2 = find_counter(cpu, "MACHINE_CLEARS.SMC");
	clr3 = find_counter(cpu, "MACHINE_CLEARS.MASKMOV");
	cyc = find_counter(cpu, "MACHINE_CLEARS.CYCLES");
	if (pos != -1) {
		cl1 = clr1->vals[pos] * 1.0;
		cl2 = clr2->vals[pos] * 1.0;
		cl3 = clr3->vals[pos] * 1.0;
		cy = cyc->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		cl1 = clr1->sum * 1.0;
		cl2 = clr2->sum * 1.0;
		cl3 = clr3->sum * 1.0;
		cy = cyc->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	/* Formula not listed but extrapulated to add the cy ?? */
	res = ((cl1 + cl2 + cl3 + cy) * con)/un;
	ret = printf("%1.3f", res);
	return(ret);
}





static int
microassist(struct counters *cpu, int pos)
{
	/* 14  - IDQ.MS_CYCLES / CPU_CLK_UNHALTED.THREAD_P (thresh > .05) */
	int ret;
	struct counters *idq;
	struct counters *unhalt;
	double un, id, res, con;

	con = 4.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	idq = find_counter(cpu, "IDQ.MS_UOPS");
	if (pos != -1) {
		id = idq->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		id = idq->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = id/(un * con);
	ret = printf("%1.3f", res);
	return(ret);
}


static int
microassist_broad(struct counters *cpu, int pos)
{
	int ret;
	struct counters *idq;
	struct counters *unhalt;
	struct counters *uopiss;
	struct counters *uopret;
	double un, id, res, con, uoi, uor;

	con = 4.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	idq = find_counter(cpu, "IDQ.MS_UOPS");
	uopiss = find_counter(cpu, "UOPS_ISSUED.ANY");
	uopret = find_counter(cpu, "UOPS_RETIRED.RETIRE_SLOTS");
	if (pos != -1) {
		id = idq->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
		uoi = uopiss->vals[pos] * 1.0;
		uor = uopret->vals[pos] * 1.0;
	} else {
		id = idq->sum * 1.0;
		un = unhalt->sum * 1.0;
		uoi = uopiss->sum * 1.0;
		uor = uopret->sum * 1.0;
	}
	res = (uor/uoi) * (id/(un * con));
	ret = printf("%1.3f", res);
	return(ret);
}


static int
aliasing(struct counters *cpu, int pos)
{
	/* 15  - (LD_BLOCKS_PARTIAL.ADDRESS_ALIAS * 5) / CPU_CLK_UNHALTED.THREAD_P (thresh > .1) */
	int ret;	
	struct counters *ld;
	struct counters *unhalt;
	double un, lds, con, res;

	con = 5.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	ld = find_counter(cpu, "LD_BLOCKS_PARTIAL.ADDRESS_ALIAS");
	if (pos != -1) {
		lds = ld->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		lds = ld->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = (lds * con)/un;
	ret = printf("%1.3f", res);
	return(ret);
}

static int
aliasing_broad(struct counters *cpu, int pos)
{
	/* 15  - (LD_BLOCKS_PARTIAL.ADDRESS_ALIAS * 5) / CPU_CLK_UNHALTED.THREAD_P (thresh > .1) */
	int ret;	
	struct counters *ld;
	struct counters *unhalt;
	double un, lds, con, res;

	con = 7.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	ld = find_counter(cpu, "LD_BLOCKS_PARTIAL.ADDRESS_ALIAS");
	if (pos != -1) {
		lds = ld->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		lds = ld->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = (lds * con)/un;
	ret = printf("%1.3f", res);
	return(ret);
}


static int
fpassists(struct counters *cpu, int pos)
{
	/* 16  - FP_ASSIST.ANY/INST_RETIRED.ANY_P */
	int ret;	
	struct counters *fp;
	struct counters *inst;
	double un, fpd, res;

	inst = find_counter(cpu, "INST_RETIRED.ANY_P");
	fp = find_counter(cpu, "FP_ASSIST.ANY");
	if (pos != -1) {
		fpd = fp->vals[pos] * 1.0;
		un = inst->vals[pos] * 1.0;
	} else {
		fpd = fp->sum * 1.0;
		un = inst->sum * 1.0;
	}
	res = fpd/un;
	ret = printf("%1.3f", res);
	return(ret);
}

static int
otherassistavx(struct counters *cpu, int pos)
{
	/* 17  - (OTHER_ASSISTS.AVX_TO_SSE * 75)/CPU_CLK_UNHALTED.THREAD_P thresh  .1*/
	int ret;	
	struct counters *oth;
	struct counters *unhalt;
	double un, ot, con, res;

	con = 75.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	oth = find_counter(cpu, "OTHER_ASSISTS.AVX_TO_SSE");
	if (pos != -1) {
		ot = oth->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		ot = oth->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = (ot * con)/un;
	ret = printf("%1.3f", res);
	return(ret);
}

static int
otherassistsse(struct counters *cpu, int pos)
{

	int ret;	
	struct counters *oth;
	struct counters *unhalt;
	double un, ot, con, res;

	/* 18     (OTHER_ASSISTS.SSE_TO_AVX * 75)/CPU_CLK_UNHALTED.THREAD_P  thresh .1*/
	con = 75.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	oth = find_counter(cpu, "OTHER_ASSISTS.SSE_TO_AVX");
	if (pos != -1) {
		ot = oth->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		ot = oth->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = (ot * con)/un;
	ret = printf("%1.3f", res);
	return(ret);
}

static int
efficiency1(struct counters *cpu, int pos)
{

	int ret;	
	struct counters *uops;
	struct counters *unhalt;
	double un, ot, con, res;

        /* 19 (UOPS_RETIRED.RETIRE_SLOTS/(4*CPU_CLK_UNHALTED.THREAD_P) look if thresh < .9*/
	con = 4.0;
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	uops = find_counter(cpu, "UOPS_RETIRED.RETIRE_SLOTS");
	if (pos != -1) {
		ot = uops->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		ot = uops->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = ot/(con * un);
	ret = printf("%1.3f", res);
	return(ret);
}

static int
efficiency2(struct counters *cpu, int pos)
{

	int ret;	
	struct counters *uops;
	struct counters *unhalt;
	double un, ot, res;

        /* 20  - CPU_CLK_UNHALTED.THREAD_P/INST_RETIRED.ANY_P good if > 1. (comp factor)*/
	unhalt = find_counter(cpu, "CPU_CLK_UNHALTED.THREAD_P");
	uops = find_counter(cpu, "INST_RETIRED.ANY_P");
	if (pos != -1) {
		ot = uops->vals[pos] * 1.0;
		un = unhalt->vals[pos] * 1.0;
	} else {
		ot = uops->sum * 1.0;
		un = unhalt->sum * 1.0;
	}
	res = un/ot;
	ret = printf("%1.3f", res);
	return(ret);
}

#define SANDY_BRIDGE_COUNT 20	
static struct cpu_entry sandy_bridge[SANDY_BRIDGE_COUNT] = {
/*01*/	{ "allocstall1", "thresh > .05", 
	  "pmcstat -s CPU_CLK_UNHALTED.THREAD_P -s PARTIAL_RAT_STALLS.SLOW_LEA_WINDOW -w 1",
	  allocstall1, 2 },
/* -- not defined for SB right (partial-rat_stalls) 02*/
        { "allocstall2", "thresh > .05", 
	  "pmcstat -s CPU_CLK_UNHALTED.THREAD_P -s PARTIAL_RAT_STALLS.FLAGS_MERGE_UOP -w 1",
	  allocstall2, 2 },
/*03*/	{ "br_miss", "thresh >= .2", 
	  "pmcstat -s CPU_CLK_UNHALTED.THREAD_P -s BR_MISP_RETIRED.ALL_BRANCHES -w 1",
	  br_mispredict, 2 },
/*04*/	{ "splitload", "thresh >= .1", 
	  "pmcstat -s CPU_CLK_UNHALTED.THREAD_P -s MEM_UOP_RETIRED.SPLIT_LOADS -w 1",
	  splitload_sb, 2 },
/* 05*/	{ "splitstore", "thresh >= .01", 
	  "pmcstat -s MEM_UOP_RETIRED.SPLIT_STORES -s MEM_UOP_RETIRED.ALL_STORES -w 1",
	  splitstore_sb, 2 },
/*06*/	{ "contested", "thresh >= .05", 
	  "pmcstat -s MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HITM  -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  contested, 2 },
/*07*/	{ "blockstorefwd", "thresh >= .05", 
	  "pmcstat -s LD_BLOCKS_STORE_FORWARD -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  blockstoreforward, 2 },
/*08*/	{ "cache2", "thresh >= .2", 
	  "pmcstat -s MEM_LOAD_UOPS_RETIRED.LLC_HIT -s MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HIT -s MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HITM -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  cache2, 4 },
/*09*/	{ "cache1", "thresh >= .2", 
	  "pmcstat -s MEM_LOAD_UOPS_MISC_RETIRED.LLC_MISS -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  cache1, 2 },
/*10*/	{ "dtlbmissload", "thresh >= .1", 
	  "pmcstat -s DTLB_LOAD_MISSES.STLB_HIT -s DTLB_LOAD_MISSES.WALK_DURATION -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  dtlb_missload, 3 },
/*11*/	{ "dtlbmissstore", "thresh >= .05", 
	  "pmcstat -s DTLB_STORE_MISSES.STLB_HIT -s DTLB_STORE_MISSES.WALK_DURATION -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  dtlb_missstore, 3 },
/*12*/	{ "frontendstall", "thresh >= .15", 
	  "pmcstat -s IDQ_UOPS_NOT_DELIVERED.CORE -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  frontendstall, 2 },
/*13*/	{ "clears", "thresh >= .02", 
	  "pmcstat -s MACHINE_CLEARS.MEMORY_ORDERING -s MACHINE_CLEARS.SMC -s MACHINE_CLEARS.MASKMOV -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  clears, 4 },
/*14*/	{ "microassist", "thresh >= .05", 
	  "pmcstat -s IDQ.MS_UOPS,cmask=1 -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  microassist, 2 },
/*15*/	{ "aliasing_4k", "thresh >= .1", 
	  "pmcstat -s LD_BLOCKS_PARTIAL.ADDRESS_ALIAS -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  aliasing, 2 },
/*16*/	{ "fpassist", "look for a excessive value", 
	  "pmcstat -s FP_ASSIST.ANY -s INST_RETIRED.ANY_P -w 1",
	  fpassists, 2 },
/*17*/	{ "otherassistavx", "look for a excessive value", 
	  "pmcstat -s OTHER_ASSISTS.AVX_TO_SSE -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  otherassistavx, 2},
/*18*/	{ "otherassistsse", "look for a excessive value", 
	  "pmcstat -s OTHER_ASSISTS.SSE_TO_AVX -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  otherassistsse, 2 },
/*19*/	{ "eff1", "thresh < .9", 
	  "pmcstat -s UOPS_RETIRED.RETIRE_SLOTS -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  efficiency1, 2 },
/*20*/	{ "eff2", "thresh > 1.0", 
	  "pmcstat -s INST_RETIRED.ANY_P -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  efficiency2, 2 },
};


#define IVY_BRIDGE_COUNT 21
static struct cpu_entry ivy_bridge[IVY_BRIDGE_COUNT] = {
/*1*/	{ "eff1", "thresh < .75", 
	  "pmcstat -s UOPS_RETIRED.RETIRE_SLOTS -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  efficiency1, 2 },
/*2*/	{ "eff2", "thresh > 1.0", 
	  "pmcstat -s INST_RETIRED.ANY_P -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  efficiency2, 2 },
/*3*/	{ "itlbmiss", "thresh > .05", 
	  "pmcstat -s ITLB_MISSES.WALK_DURATION -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  itlb_miss, 2 },
/*4*/	{ "icachemiss", "thresh > .05", 
	  "pmcstat -s ICACHE.IFETCH_STALL -s ITLB_MISSES.WALK_DURATION -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  icache_miss, 3 },
/*5*/	{ "lcpstall", "thresh > .05", 
	  "pmcstat -s ILD_STALL.LCP -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  lcp_stall, 2 },
/*6*/	{ "cache1", "thresh >= .2", 
	  "pmcstat -s MEM_LOAD_UOPS_LLC_MISS_RETIRED.LOCAL_DRAM -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  cache1ib, 2 },
/*7*/	{ "cache2", "thresh >= .2", 
	  "pmcstat -s MEM_LOAD_UOPS_RETIRED.LLC_HIT -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  cache2ib, 2 },
/*8*/	{ "contested", "thresh >= .05", 
	  "pmcstat -s MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HITM  -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  contested, 2 },
/*9*/	{ "datashare", "thresh >= .05",
	  "pmcstat -s MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HIT -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  datasharing, 2 },
/*10*/	{ "blockstorefwd", "thresh >= .05", 
	  "pmcstat -s LD_BLOCKS_STORE_FORWARD -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  blockstoreforward, 2 },
/*11*/	{ "splitload", "thresh >= .1", 
	  "pmcstat -s CPU_CLK_UNHALTED.THREAD_P -s L1D_PEND_MISS.PENDING -s MEM_LOAD_UOPS_RETIRED.L1_MISS -s LD_BLOCKS.NO_SR -w 1",
	  splitloadib, 4 },
/*12*/	{ "splitstore", "thresh >= .01", 
	  "pmcstat -s MEM_UOPS_RETIRED.SPLIT_STORES -s MEM_UOPS_RETIRED.ALL_STORES -w 1",
	  splitstore, 2 },
/*13*/	{ "aliasing_4k", "thresh >= .1", 
	  "pmcstat -s LD_BLOCKS_PARTIAL.ADDRESS_ALIAS -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  aliasing, 2 },
/*14*/	{ "dtlbmissload", "thresh >= .1", 
	  "pmcstat -s DTLB_LOAD_MISSES.STLB_HIT -s DTLB_LOAD_MISSES.WALK_DURATION -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  dtlb_missload , 3},
/*15*/	{ "dtlbmissstore", "thresh >= .05", 
	  "pmcstat -s DTLB_STORE_MISSES.STLB_HIT -s DTLB_STORE_MISSES.WALK_DURATION -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  dtlb_missstore, 3 },
/*16*/	{ "br_miss", "thresh >= .2", 
	  "pmcstat -s CPU_CLK_UNHALTED.THREAD_P -s BR_MISP_RETIRED.ALL_BRANCHES -s MACHINE_CLEARS.MEMORY_ORDERING -s MACHINE_CLEARS.SMC -s MACHINE_CLEARS.MASKMOV -s UOPS_ISSUED.ANY -s UOPS_RETIRED.RETIRE_SLOTS -s INT_MISC.RECOVERY_CYCLES -w 1",
	  br_mispredictib, 8 },
/*17*/	{ "clears", "thresh >= .02", 
	  "pmcstat -s MACHINE_CLEARS.MEMORY_ORDERING -s MACHINE_CLEARS.SMC -s MACHINE_CLEARS.MASKMOV -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  clears, 4 },
/*18*/	{ "microassist", "thresh >= .05", 
	  "pmcstat -s IDQ.MS_UOPS,cmask=1 -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  microassist, 2 },
/*19*/	{ "fpassist", "look for a excessive value", 
	  "pmcstat -s FP_ASSIST.ANY -s INST_RETIRED.ANY_P -w 1",
	  fpassists, 2 },
/*20*/	{ "otherassistavx", "look for a excessive value", 
	  "pmcstat -s OTHER_ASSISTS.AVX_TO_SSE -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  otherassistavx , 2},
/*21*/	{ "otherassistsse", "look for a excessive value", 
	  "pmcstat -s OTHER_ASSISTS.SSE_TO_AVX -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  otherassistsse, 2 },
};

#define HASWELL_COUNT 20
static struct cpu_entry haswell[HASWELL_COUNT] = {
/*1*/	{ "eff1", "thresh < .75", 
	  "pmcstat -s UOPS_RETIRED.RETIRE_SLOTS -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  efficiency1, 2 },
/*2*/	{ "eff2", "thresh > 1.0", 
	  "pmcstat -s INST_RETIRED.ANY_P -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  efficiency2, 2 },
/*3*/	{ "itlbmiss", "thresh > .05", 
	  "pmcstat -s ITLB_MISSES.WALK_DURATION -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  itlb_miss, 2 },
/*4*/	{ "icachemiss", "thresh > .05", 
	  "pmcstat -s ICACHE.MISSES -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  icache_miss_has, 2 },
/*5*/	{ "lcpstall", "thresh > .05", 
	  "pmcstat -s ILD_STALL.LCP -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  lcp_stall, 2 },
/*6*/	{ "cache1", "thresh >= .2", 
	  "pmcstat -s MEM_LOAD_UOPS_LLC_MISS_RETIRED.LOCAL_DRAM -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  cache1ib, 2 },
/*7*/	{ "cache2", "thresh >= .2", 
	  "pmcstat -s MEM_LOAD_UOPS_RETIRED.LLC_HIT  -s MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HIT -s MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HITM -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  cache2has, 4 },
/*8*/	{ "contested", "thresh >= .05", 
	  "pmcstat -s MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HITM  -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  contested_has, 2 },
/*9*/	{ "datashare", "thresh >= .05",
	  "pmcstat -s MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HIT -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  datasharing_has, 2 },
/*10*/	{ "blockstorefwd", "thresh >= .05", 
	  "pmcstat -s LD_BLOCKS_STORE_FORWARD -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  blockstoreforward, 2 },
/*11*/	{ "splitload", "thresh >= .1", 
	  "pmcstat -s CPU_CLK_UNHALTED.THREAD_P -s MEM_UOPS_RETIRED.SPLIT_LOADS -w 1",
	  splitload , 2},
/*12*/	{ "splitstore", "thresh >= .01", 
	  "pmcstat -s MEM_UOPS_RETIRED.SPLIT_STORES -s MEM_UOPS_RETIRED.ALL_STORES -w 1",
	  splitstore, 2 },
/*13*/	{ "aliasing_4k", "thresh >= .1", 
	  "pmcstat -s LD_BLOCKS_PARTIAL.ADDRESS_ALIAS -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  aliasing, 2 },
/*14*/	{ "dtlbmissload", "thresh >= .1", 
	  "pmcstat -s DTLB_LOAD_MISSES.STLB_HIT -s DTLB_LOAD_MISSES.WALK_DURATION -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  dtlb_missload, 3 },
/*15*/	{ "br_miss", "thresh >= .2", 
	  "pmcstat -s CPU_CLK_UNHALTED.THREAD_P -s BR_MISP_RETIRED.ALL_BRANCHES -w 1",
	  br_mispredict, 2 },
/*16*/	{ "clears", "thresh >= .02", 
	  "pmcstat -s MACHINE_CLEARS.MEMORY_ORDERING -s MACHINE_CLEARS.SMC -s MACHINE_CLEARS.MASKMOV -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  clears, 4 },
/*17*/	{ "microassist", "thresh >= .05", 
	  "pmcstat -s IDQ.MS_UOPS,cmask=1 -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  microassist, 2 },
/*18*/	{ "fpassist", "look for a excessive value", 
	  "pmcstat -s FP_ASSIST.ANY -s INST_RETIRED.ANY_P -w 1",
	  fpassists, 2 },
/*19*/	{ "otherassistavx", "look for a excessive value", 
	  "pmcstat -s OTHER_ASSISTS.AVX_TO_SSE -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  otherassistavx, 2 },
/*20*/	{ "otherassistsse", "look for a excessive value", 
	  "pmcstat -s OTHER_ASSISTS.SSE_TO_AVX -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  otherassistsse, 2 },
};


static void
explain_name_broad(const char *name)
{
	const char *mythresh;
	if (strcmp(name, "eff1") == 0) {
		printf("Examine (UOPS_RETIRED.RETIRE_SLOTS)/(4 *CPU_CLK_UNHALTED.THREAD_P)\n");
		mythresh = "thresh < .75";
	} else if (strcmp(name, "eff2") == 0) {
		printf("Examine CPU_CLK_UNHALTED.THREAD_P/INST_RETIRED.ANY_P\n");
		mythresh = "thresh > 1.0";
	} else if (strcmp(name, "itlbmiss") == 0) {
		printf("Examine (7 * ITLB_MISSES_STLB_HIT_4K + ITLB_MISSES.WALK_DURATION)/ CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh > .05"; 
	} else if (strcmp(name, "icachemiss") == 0) {
		printf("Examine ( 36.0 * ICACHE.MISSES)/ CPU_CLK_UNHALTED.THREAD_P ??? may not be right \n");
		mythresh = "thresh > .05";
	} else if (strcmp(name, "lcpstall") == 0) {
		printf("Examine ILD_STALL.LCP/CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh > .05";
	} else if (strcmp(name, "cache1") == 0) {
		printf("Examine (MEM_LOAD_UOPS_LLC_MISS_RETIRED.LOCAL_DRAM * 180) / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .1";
	} else if (strcmp(name, "cache2") == 0) {
		printf("Examine (36.0 * MEM_LOAD_UOPS_RETIRED.L3_HIT / CPU_CLK_UNHALTED.THREAD_P)\n");
		mythresh = "thresh >= .2";
	} else if (strcmp(name, "contested") == 0) {
		printf("Examine ((MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HITM * 84) +  MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_MISS)/ CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .05";
	} else if (strcmp(name, "datashare") == 0) {
		printf("Examine (MEM_LOAD_UOPS_L3_HIT_RETIRED.XSNP_HIT * 72)/CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh > .05";
	} else if (strcmp(name, "blockstorefwd") == 0) {
		printf("Examine (LD_BLOCKS_STORE_FORWARD * 13) / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .05";
	} else if (strcmp(name, "aliasing_4k") == 0) {
		printf("Examine (LD_BLOCKS_PARTIAL.ADDRESS_ALIAS * 7) / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .1";
	} else if (strcmp(name, "dtlbmissload") == 0) {
		printf("Examine (((DTLB_LOAD_MISSES.STLB_HIT * 7) + DTLB_LOAD_MISSES.WALK_DURATION)\n");
		printf("         / CPU_CLK_UNHALTED.THREAD_P)\n");
		mythresh = "thresh >= .1";

	} else if (strcmp(name, "br_miss") == 0) {
		printf("Examine BR_MISP_RETIRED.ALL_BRANCHS_PS / (BR_MISP_RETIED.ALL_BRANCHES_PS + MACHINE_CLEARS.COUNT) *\n");
		printf(" (UOPS_ISSUEDF.ANY - UOPS_RETIRED.RETIRE_SLOTS + 4 * INT_MISC.RECOVERY_CYCLES) /\n");
		printf("CPU_CLK_UNHALTED.THREAD * 4)\n");
		mythresh = "thresh >= .2";
	} else if (strcmp(name, "clears") == 0) {
		printf("Examine ((MACHINE_CLEARS.MEMORY_ORDERING + \n");
		printf("          MACHINE_CLEARS.SMC + \n");
		printf("          MACHINE_CLEARS.MASKMOV ) * 100 ) / CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "thresh >= .02";
	} else if (strcmp(name, "fpassist") == 0) {
		printf("Examine FP_ASSIST.ANY/INST_RETIRED.ANY_P\n");
		mythresh = "look for a excessive value";
	} else if (strcmp(name, "otherassistavx") == 0) {
		printf("Examine (OTHER_ASSISTS.AVX_TO_SSE * 75)/CPU_CLK_UNHALTED.THREAD_P\n");
		mythresh = "look for a excessive value";
	} else if (strcmp(name, "microassist") == 0) {
		printf("Examine (UOPS_RETIRED.RETIRE_SLOTS/UOPS_ISSUED.ANY) * (IDQ.MS_CYCLES / (4 * CPU_CLK_UNHALTED.THREAD_P)\n");
		printf("***We use IDQ.MS_UOPS,cmask=1 to get cycles\n");
		mythresh = "thresh >= .05";
	} else {
		printf("Unknown name:%s\n", name);
		mythresh = "unknown entry";
        }
	printf("If the value printed is %s we may have the ability to improve performance\n", mythresh);
}


#define BROADWELL_COUNT 17
static struct cpu_entry broadwell[BROADWELL_COUNT] = {
/*1*/	{ "eff1", "thresh < .75", 
	  "pmcstat -s UOPS_RETIRED.RETIRE_SLOTS -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  efficiency1, 2 }, 
/*2*/	{ "eff2", "thresh > 1.0", 
	  "pmcstat -s INST_RETIRED.ANY_P -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  efficiency2, 2 },
/*3*/	{ "itlbmiss", "thresh > .05", 
	  "pmcstat -s ITLB_MISSES.WALK_DURATION -s CPU_CLK_UNHALTED.THREAD_P -s ITLB_MISSES.STLB_HIT_4K -w 1",
	  itlb_miss_broad, 3 },
/*4*/	{ "icachemiss", "thresh > .05", 
	  "pmcstat -s ICACHE.MISSES -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  icache_miss_has, 2 },
/*5*/	{ "lcpstall", "thresh > .05", 
	  "pmcstat -s ILD_STALL.LCP -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  lcp_stall, 2 },
/*6*/	{ "cache1", "thresh >= .1", 
	  "pmcstat -s MEM_LOAD_UOPS_RETIRED.L3_MISS  -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  cache1broad, 2 },
/*7*/	{ "cache2", "thresh >= .2", 
	  "pmcstat -s MEM_LOAD_UOPS_RETIRED.L3_HIT  -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  cache2broad, 2 },
/*8*/	{ "contested", "thresh >= .05", 
	  "pmcstat -s MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HITM  -s CPU_CLK_UNHALTED.THREAD_P  -s MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_MISS -w 1",
	  contestedbroad, 2 },
/*9*/	{ "datashare", "thresh >= .05",
	  "pmcstat -s MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HIT -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  datasharing_has, 2 },
/*10*/	{ "blockstorefwd", "thresh >= .05", 
	  "pmcstat -s LD_BLOCKS_STORE_FORWARD -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  blockstoreforward, 2 },
/*11*/	{ "aliasing_4k", "thresh >= .1", 
	  "pmcstat -s LD_BLOCKS_PARTIAL.ADDRESS_ALIAS -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  aliasing_broad, 2 }, 
/*12*/	{ "dtlbmissload", "thresh >= .1", 
	  "pmcstat -s DTLB_LOAD_MISSES.STLB_HIT_4K -s DTLB_LOAD_MISSES.WALK_DURATION -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  dtlb_missload, 3 },
/*13*/	{ "br_miss", "thresh >= .2", 
	  "pmcstat -s CPU_CLK_UNHALTED.THREAD_P -s BR_MISP_RETIRED.ALL_BRANCHES -s MACHINE_CLEARS.CYCLES -s UOPS_ISSUED.ANY -s UOPS_RETIRED.RETIRE_SLOTS -s INT_MISC.RECOVERY_CYCLES -w 1",
	  br_mispredict_broad, 7 },
/*14*/	{ "clears", "thresh >= .02", 
	  "pmcstat -s MACHINE_CLEARS.CYCLES -s MACHINE_CLEARS.MEMORY_ORDERING -s MACHINE_CLEARS.SMC -s MACHINE_CLEARS.MASKMOV -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  clears_broad, 5 },
/*15*/	{ "fpassist", "look for a excessive value", 
	  "pmcstat -s FP_ASSIST.ANY -s INST_RETIRED.ANY_P -w 1",
	  fpassists, 2 },
/*16*/	{ "otherassistavx", "look for a excessive value", 
	  "pmcstat -s OTHER_ASSISTS.AVX_TO_SSE -s CPU_CLK_UNHALTED.THREAD_P -w 1",
	  otherassistavx, 2 },
/*17*/	{ "microassist", "thresh >= .2", 
	  "pmcstat -s IDQ.MS_UOPS,cmask=1 -s CPU_CLK_UNHALTED.THREAD_P -s UOPS_ISSUED.ANY -s UOPS_RETIRED.RETIRE_SLOTS  -w 1",
	  microassist_broad, 4 },
};


static void
set_sandybridge(void)
{
	strcpy(the_cpu.cputype, "SandyBridge PMC");
	the_cpu.number = SANDY_BRIDGE_COUNT;
	the_cpu.ents = sandy_bridge;
	the_cpu.explain = explain_name_sb;
}

static void
set_ivybridge(void)
{
	strcpy(the_cpu.cputype, "IvyBridge PMC");
	the_cpu.number = IVY_BRIDGE_COUNT;
	the_cpu.ents = ivy_bridge;
	the_cpu.explain = explain_name_ib;
}


static void
set_haswell(void)
{
	strcpy(the_cpu.cputype, "HASWELL PMC");
	the_cpu.number = HASWELL_COUNT;
	the_cpu.ents = haswell;
	the_cpu.explain = explain_name_has;
}


static void
set_broadwell(void)
{
	strcpy(the_cpu.cputype, "HASWELL PMC");
	the_cpu.number = BROADWELL_COUNT;
	the_cpu.ents = broadwell;
	the_cpu.explain = explain_name_broad;
}


static int
set_expression(const char *name)
{
	int found = 0, i;
	for(i=0 ; i< the_cpu.number; i++) {
		if (strcmp(name, the_cpu.ents[i].name) == 0) {
			found = 1;
			expression = the_cpu.ents[i].func;
			command = the_cpu.ents[i].command;
			threshold = the_cpu.ents[i].thresh;
			if  (the_cpu.ents[i].counters_required > max_pmc_counters) {
				printf("Test %s requires that the CPU have %d counters and this CPU has only %d\n",
				       the_cpu.ents[i].name,
				       the_cpu.ents[i].counters_required, max_pmc_counters);
				printf("Sorry this test can not be run\n");
				if (run_all == 0) {
					exit(-1);
				} else {
					return(-1);
				}
			}
			break;
		}
	}
	if (!found) {
		printf("For CPU type %s we have no expression:%s\n",
		       the_cpu.cputype, name);
		exit(-1);
	}
	return(0);
}





static int
validate_expression(char *name) 
{
	int i, found;

	found = 0;
	for(i=0 ; i< the_cpu.number; i++) {
		if (strcmp(name, the_cpu.ents[i].name) == 0) {
			found = 1;
			break;
		}
	}
	if (!found) {
		return(-1);
	}
	return (0);
}

static void
do_expression(struct counters *cpu, int pos)
{
	if (expression == NULL) 
		return;
	(*expression)(cpu, pos);
}

static void
process_header(int idx, char *p)
{
	struct counters *up;
	int i, len, nlen;
	/* 
	 * Given header element idx, at p in
	 * form 's/NN/nameof'
	 * process the entry to pull out the name and
	 * the CPU number.
	 */
	if (strncmp(p, "s/", 2)) {
		printf("Check -- invalid header no s/ in %s\n",
		       p);
		return;
	}
	up = &cnts[idx];
	up->cpu = strtol(&p[2], NULL, 10);
	len = strlen(p);
	for (i=2; i<len; i++) {
		if (p[i] == '/') {
			nlen = strlen(&p[(i+1)]);
			if (nlen < (MAX_NLEN-1)) {
				strcpy(up->counter_name, &p[(i+1)]);
			} else {
				strncpy(up->counter_name, &p[(i+1)], (MAX_NLEN-1));
			}
		}
	}
}

static void
build_counters_from_header(FILE *io)
{
	char buffer[8192], *p;
	int i, len, cnt;
	size_t mlen;

	/* We have a new start, lets 
	 * setup our headers and cpus.
	 */
	if (fgets(buffer, sizeof(buffer), io) == NULL) {
		printf("First line can't be read from file err:%d\n", errno);
		return;
	}
	/*
	 * Ok output is an array of counters. Once
	 * we start to read the values in we must
	 * put them in there slot to match there CPU and 
	 * counter being updated. We create a mass array
	 * of the counters, filling in the CPU and 
	 * counter name. 
	 */
	/* How many do we get? */
	len = strlen(buffer);
	for (i=0, cnt=0; i<len; i++) {
		if (strncmp(&buffer[i], "s/", 2) == 0) {
			cnt++;
			for(;i<len;i++) {
				if (buffer[i] == ' ')
					break;
			}
		}
	}
	mlen = sizeof(struct counters) * cnt;
	cnts = malloc(mlen);
	ncnts = cnt;
	if (cnts == NULL) {
		printf("No memory err:%d\n", errno);
		return;
	}
	memset(cnts, 0, mlen);
	for (i=0, cnt=0; i<len; i++) {
		if (strncmp(&buffer[i], "s/", 2) == 0) {
			p = &buffer[i];
			for(;i<len;i++) {
				if (buffer[i] == ' ') {
					buffer[i] = 0;
					break;
				}
			}
			process_header(cnt, p);
			cnt++;
		}
	}
	if (verbose)
		printf("We have %d entries\n", cnt);	
}
extern int max_to_collect;
int max_to_collect = MAX_COUNTER_SLOTS;

static int
read_a_line(FILE *io) 
{
	char buffer[8192], *p, *stop;	
	int pos, i;

	if (fgets(buffer, sizeof(buffer), io) == NULL) {
		return(0);
	}
	p = buffer;
	for (i=0; i<ncnts; i++) {
		pos = cnts[i].pos;
		cnts[i].vals[pos] = strtol(p, &stop, 0);
		cnts[i].pos++;
		cnts[i].sum += cnts[i].vals[pos];
		p = stop;
	}
	return (1);
}

extern int cpu_count_out;
int cpu_count_out=0;

static void
print_header(void)
{
	int i, cnt, printed_cnt;

	printf("*********************************\n");
	for(i=0, cnt=0; i<MAX_CPU; i++) {
		if (glob_cpu[i]) {
			cnt++;
		}
	}	
	cpu_count_out = cnt;
	for(i=0, printed_cnt=0; i<MAX_CPU; i++) {
		if (glob_cpu[i]) {
			printf("CPU%d", i);
			printed_cnt++;
		}
		if (printed_cnt == cnt) {
			printf("\n");
			break;
		} else {
			printf("\t");
		}
	}
}

static void
lace_cpus_together(void)
{
	int i, j, lace_cpu;
	struct counters *cpat, *at;

	for(i=0; i<ncnts; i++) {
		cpat = &cnts[i];
		if (cpat->next_cpu) {
			/* Already laced in */
			continue;
		}
		lace_cpu = cpat->cpu;
		if (lace_cpu >= MAX_CPU) {
			printf("CPU %d to big\n", lace_cpu);
			continue;
		}
		if (glob_cpu[lace_cpu] == NULL) {
			glob_cpu[lace_cpu] = cpat;
		} else {
			/* Already processed this cpu */
			continue;
		}
		/* Ok look forward for cpu->cpu and link in */
		for(j=(i+1); j<ncnts; j++) {
			at = &cnts[j];
			if (at->next_cpu) {
				continue;
			}
			if (at->cpu == lace_cpu) {
				/* Found one */
				cpat->next_cpu = at;
				cpat = at;
			}
		}
	}
}


static void
process_file(char *filename)
{
	FILE *io;
	int i;
	int line_at, not_done;
	pid_t pid_of_command=0;

	if (filename ==  NULL) {
		io = my_popen(command, "r", &pid_of_command);
	} else {
		io = fopen(filename, "r");
		if (io == NULL) {
			printf("Can't process file %s err:%d\n",
			       filename, errno);
			return;
		}
	}
	build_counters_from_header(io);
	if (cnts == NULL) {
		/* Nothing we can do */
		printf("Nothing to do -- no counters built\n");
		if (io) {
			fclose(io);
		}
		return;
	}
	lace_cpus_together();
	print_header();
	if (verbose) {
		for (i=0; i<ncnts; i++) {
			printf("Counter:%s cpu:%d index:%d\n",
			       cnts[i].counter_name,
			       cnts[i].cpu, i);
		}
	}
	line_at = 0;
	not_done = 1;
	while(not_done) {
		if (read_a_line(io)) {
			line_at++;
		} else {
			break;
		}
		if (line_at >= max_to_collect) {
			not_done = 0;
		}
		if (filename == NULL) {
			int cnt;
			/* For the ones we dynamically open we print now */
			for(i=0, cnt=0; i<MAX_CPU; i++) {
				do_expression(glob_cpu[i], (line_at-1));
				cnt++;
				if (cnt == cpu_count_out) {
					printf("\n");
					break;
				} else {
					printf("\t");
				}
			}
		}
	}
	if (filename) {
		fclose(io);
	} else {
		my_pclose(io, pid_of_command);
	}
}
#if defined(__amd64__)
#define cpuid(in,a,b,c,d)\
  asm("cpuid": "=a" (a), "=b" (b), "=c" (c), "=d" (d) : "a" (in));

static __inline void
do_cpuid(u_int ax, u_int cx, u_int *p)
{
	__asm __volatile("cpuid"
			 : "=a" (p[0]), "=b" (p[1]), "=c" (p[2]), "=d" (p[3])
			 :  "0" (ax), "c" (cx) );
}

#else
#define cpuid(in, a, b, c, d) 
#define do_cpuid(ax, cx, p)
#endif

static void
get_cpuid_set(void)
{
	unsigned long eax, ebx, ecx, edx;
	int model;
	pid_t pid_of_command=0;
	size_t sz, len;
	FILE *io;
	char linebuf[1024], *str;
	u_int reg[4];

	eax = ebx = ecx = edx = 0;

	cpuid(0, eax, ebx, ecx, edx);
	if (ebx == 0x68747541) {
		printf("AMD processors are not supported by this program\n");
		printf("Sorry\n");
		exit(0);
	} else if (ebx == 0x6972794) {
		printf("Cyrix processors are not supported by this program\n");
		printf("Sorry\n");
		exit(0);
	} else if (ebx == 0x756e6547) {
		printf("Genuine Intel\n");
	} else {
		printf("Unknown processor type 0x%lx Only Intel AMD64 types are supported by this routine!\n", ebx);
		exit(0);
	}
	cpuid(1, eax, ebx, ecx, edx);
	model = (((eax & 0xF0000) >> 12) | ((eax & 0xF0) >> 4));
	printf("CPU model is 0x%x id:0x%lx\n", model, eax);
	switch (eax & 0xF00) {
	case 0x500:		/* Pentium family processors */
		printf("Intel Pentium P5\n");
		goto not_supported;
		break;
	case 0x600:		/* Pentium Pro, Celeron, Pentium II & III */
		switch (model) {
		case 0x1:
			printf("Intel Pentium P6\n");
			goto not_supported;
			break;
		case 0x3: 
		case 0x5:
			printf("Intel PII\n");
			goto not_supported;
			break;
		case 0x6: case 0x16:
			printf("Intel CL\n");
			goto not_supported;
			break;
		case 0x7: case 0x8: case 0xA: case 0xB:
			printf("Intel PIII\n");
			goto not_supported;
			break;
		case 0x9: case 0xD:
			printf("Intel PM\n");
			goto not_supported;
			break;
		case 0xE:
			printf("Intel CORE\n");
			goto not_supported;
			break;
		case 0xF:
			printf("Intel CORE2\n");
			goto not_supported;
			break;
		case 0x17:
			printf("Intel CORE2EXTREME\n");
			goto not_supported;
			break;
		case 0x1C:	/* Per Intel document 320047-002. */
			printf("Intel ATOM\n");
			goto not_supported;
			break;
		case 0x1A:
		case 0x1E:	/*
				 * Per Intel document 253669-032 9/2009,
				 * pages A-2 and A-57
				 */
		case 0x1F:	/*
				 * Per Intel document 253669-032 9/2009,
				 * pages A-2 and A-57
				 */
			printf("Intel COREI7\n");
			goto not_supported;
			break;
		case 0x2E:
			printf("Intel NEHALEM\n");
			goto not_supported;
			break;
		case 0x25:	/* Per Intel document 253669-033US 12/2009. */
		case 0x2C:	/* Per Intel document 253669-033US 12/2009. */
			printf("Intel WESTMERE\n");
			goto not_supported;
			break;
		case 0x2F:	/* Westmere-EX, seen in wild */
			printf("Intel WESTMERE\n");
			goto not_supported;
			break;
		case 0x2A:	/* Per Intel document 253669-039US 05/2011. */
			printf("Intel SANDYBRIDGE\n");
			set_sandybridge();
			break;
		case 0x2D:	/* Per Intel document 253669-044US 08/2012. */
			printf("Intel SANDYBRIDGE_XEON\n");
			set_sandybridge();
			break;
		case 0x3A:	/* Per Intel document 253669-043US 05/2012. */
			printf("Intel IVYBRIDGE\n");
			set_ivybridge();
			break;
		case 0x3E:	/* Per Intel document 325462-045US 01/2013. */
			printf("Intel IVYBRIDGE_XEON\n");
			set_ivybridge();
			break;
		case 0x3F:	/* Per Intel document 325462-045US 09/2014. */
			printf("Intel HASWELL (Xeon)\n");
			set_haswell();
			break;
		case 0x3C:	/* Per Intel document 325462-045US 01/2013. */
		case 0x45:
		case 0x46:
			printf("Intel HASWELL\n");
			set_haswell();
			break;

		case 0x4e:
		case 0x5e:
			printf("Intel SKY-LAKE\n");
			goto not_supported;
			break;
		case 0x3D:
		case 0x47:
			printf("Intel BROADWELL\n");
			set_broadwell();
			break;
		case 0x4f:
		case 0x56:
			printf("Intel BROADWEL (Xeon)\n");
			set_broadwell();
			break;

		case 0x4D:
			/* Per Intel document 330061-001 01/2014. */
			printf("Intel ATOM_SILVERMONT\n");
			goto not_supported;
			break;
		default:
			printf("Intel model 0x%x is not known -- sorry\n",
			       model);
			goto not_supported;
			break;
		}
		break;
	case 0xF00:		/* P4 */
		printf("Intel unknown model %d\n", model);
		goto not_supported;
		break;
	}
	do_cpuid(0xa, 0, reg);
	max_pmc_counters = (reg[3] & 0x0000000f) + 1;
	printf("We have %d PMC counters to work with\n", max_pmc_counters);
	/* Ok lets load the list of all known PMC's */
	io = my_popen("/usr/sbin/pmccontrol -L", "r", &pid_of_command);
	if (valid_pmcs == NULL) {
		/* Likely */
		pmc_allocated_cnt = PMC_INITIAL_ALLOC;
		sz = sizeof(char *) * pmc_allocated_cnt;
		valid_pmcs = malloc(sz);
		if (valid_pmcs == NULL) {
			printf("No memory allocation fails at startup?\n");	
			exit(-1);
		}
		memset(valid_pmcs, 0, sz);
	}
	
	while (fgets(linebuf, sizeof(linebuf), io) != NULL) {
		if (linebuf[0] != '\t') {
			/* sometimes headers ;-) */
			continue;
		}
		len = strlen(linebuf);
		if (linebuf[(len-1)] == '\n') {
			/* Likely */
			linebuf[(len-1)] = 0;
		}
		str = &linebuf[1];
		len = strlen(str) + 1;
		valid_pmcs[valid_pmc_cnt] = malloc(len);
		if (valid_pmcs[valid_pmc_cnt] == NULL) {
			printf("No memory2 allocation fails at startup?\n");	
			exit(-1);
		}
		memset(valid_pmcs[valid_pmc_cnt], 0, len);
		strcpy(valid_pmcs[valid_pmc_cnt], str);
		valid_pmc_cnt++;
		if (valid_pmc_cnt >= pmc_allocated_cnt) {
			/* Got to expand -- unlikely */
			char **more;

			sz = sizeof(char *) * (pmc_allocated_cnt * 2);
			more = malloc(sz);
			if (more == NULL) {
				printf("No memory3 allocation fails at startup?\n");	
				exit(-1);
			}
			memset(more, 0, sz);
			memcpy(more, valid_pmcs, sz);
			pmc_allocated_cnt *= 2;
			free(valid_pmcs);
			valid_pmcs = more;
		}
	}
	my_pclose(io, pid_of_command);	
	return;
not_supported:
	printf("Not supported\n");	
	exit(-1);
}

static void
explain_all(void)
{
	int i;
	printf("For CPU's of type %s the following expressions are available:\n",the_cpu.cputype);
	printf("-------------------------------------------------------------\n");
	for(i=0; i<the_cpu.number; i++){
		printf("For -e %s ", the_cpu.ents[i].name);
		(*the_cpu.explain)(the_cpu.ents[i].name);
		printf("----------------------------\n");
	}
}

static void
test_for_a_pmc(const char *pmc, int out_so_far)
{
	FILE *io;
	pid_t pid_of_command=0;	
	char my_command[1024];
	char line[1024];
	char resp[1024];
	int len, llen, i;

	if (out_so_far < 50) {
		len = 50 - out_so_far;
		for(i=0; i<len; i++) {
			printf(" ");
		}
	}
	sprintf(my_command, "/usr/sbin/pmcstat -w .25 -c 0 -s %s", pmc);
	io = my_popen(my_command, "r", &pid_of_command);	
	if (io == NULL) {
		printf("Failed -- popen fails\n");
		return;
	}
	/* Setup what we expect */
	len = sprintf(resp, "%s", pmc);
	if (fgets(line, sizeof(line), io) == NULL) {
		printf("Failed -- no output from pmstat\n");
		goto out;
	}
	llen = strlen(line);
	if (line[(llen-1)] == '\n') {
		line[(llen-1)] = 0;
		llen--;
	}
	for(i=2; i<(llen-len); i++) {
		if (strncmp(&line[i], "ERROR", 5) == 0) {
			printf("Failed %s\n", line);
			goto out;
		} else if (strncmp(&line[i], resp, len) == 0) {
			int j, k;

			if (fgets(line, sizeof(line), io) == NULL) {
				printf("Failed -- no second output from pmstat\n");
				goto out;
			}
			len = strlen(line);
			for (j=0; j<len; j++) {
				if (line[j] == ' ') {
					j++; 
				} else {
					break;
				}
			}
			printf("Pass");
			len = strlen(&line[j]);
			if (len < 20) {
				for(k=0; k<(20-len); k++) {
					printf(" ");
				}
			}
			if (len) {
				printf("%s", &line[j]);
			} else {
				printf("\n");
			}
			goto out;
		}
	}
	printf("Failed -- '%s' not '%s'\n", line, resp);
out:
	my_pclose(io, pid_of_command);		
	
}

static int
add_it_to(char **vars, int cur_cnt, char *name)
{
	int i;
	size_t len;
	for(i=0; i<cur_cnt; i++) {
		if (strcmp(vars[i], name) == 0) {
			/* Already have */
			return(0);
		}
	}
	if (vars[cur_cnt] != NULL) {
		printf("Cur_cnt:%d filled with %s??\n", 
		       cur_cnt, vars[cur_cnt]);
		exit(-1);
	}
	/* Ok its new */
	len = strlen(name) + 1;
	vars[cur_cnt] = malloc(len);
	if (vars[cur_cnt] == NULL) {
		printf("No memory %s\n", __FUNCTION__);
		exit(-1);
	}
	memset(vars[cur_cnt], 0, len);
	strcpy(vars[cur_cnt], name);
	return(1);
}

static char *
build_command_for_exp(struct expression *exp)
{
	/*
	 * Build the pmcstat command to handle
	 * the passed in expression.
	 * /usr/sbin/pmcstat -w 1 -s NNN -s QQQ
	 * where NNN and QQQ represent the PMC's in the expression
	 * uniquely..
	 */
	char forming[1024];
	int cnt_pmc, alloced_pmcs, i;
	struct expression *at;
	char **vars, *cmd;
	size_t mal;

	alloced_pmcs = cnt_pmc = 0;
	/* first how many do we have */
	at = exp;
	while (at) {
		if (at->type == TYPE_VALUE_PMC) {
			cnt_pmc++;
		}
		at = at->next;
	}
	if (cnt_pmc == 0) {
		printf("No PMC's in your expression -- nothing to do!!\n");
		exit(0);
	}
	mal = cnt_pmc * sizeof(char *);
	vars = malloc(mal);
	if (vars == NULL) {
		printf("No memory\n");
		exit(-1);
	}
	memset(vars, 0, mal);
	at = exp;
	while (at) {
		if (at->type == TYPE_VALUE_PMC) {
			if(add_it_to(vars, alloced_pmcs, at->name)) {
				alloced_pmcs++;
			}
		}
		at = at->next;
	}
	/* Now we have a unique list in vars so create our command */
	mal = 23; /*	"/usr/sbin/pmcstat -w 1"  + \0 */
	for(i=0; i<alloced_pmcs; i++) {
		mal += strlen(vars[i]) + 4;	/* var + " -s " */
	}
	cmd = malloc((mal+2));
	if (cmd == NULL) {
		printf("%s out of mem\n", __FUNCTION__);
		exit(-1);
	}
	memset(cmd, 0, (mal+2));
	strcpy(cmd, "/usr/sbin/pmcstat -w 1");
	at = exp;
	for(i=0; i<alloced_pmcs; i++) {
		sprintf(forming, " -s %s", vars[i]);
		strcat(cmd, forming);
		free(vars[i]);
		vars[i] = NULL;
	}
	free(vars);
	return(cmd);
}

static int
user_expr(struct counters *cpu, int pos)
{
	int ret;	
	double res;
	struct counters *var;
	struct expression *at;

	at = master_exp;
	while (at) {
		if (at->type == TYPE_VALUE_PMC) {
			var = find_counter(cpu, at->name);
			if (var == NULL) {
				printf("%s:Can't find counter %s?\n", __FUNCTION__, at->name);
				exit(-1);
			}
			if (pos != -1) {
				at->value = var->vals[pos] * 1.0;
			} else {
				at->value = var->sum * 1.0;
			}
		}
		at = at->next;
	}
	res = run_expr(master_exp, 1, NULL);
	ret = printf("%1.3f", res);
	return(ret);
}


static void
set_manual_exp(struct expression *exp)
{
	expression = user_expr;
	command = build_command_for_exp(exp);
	threshold = "User defined threshold";
}

static void
run_tests(void)
{
	int i, lenout;
	printf("Running tests on %d PMC's this may take some time\n", valid_pmc_cnt);
	printf("------------------------------------------------------------------------\n");
	for(i=0; i<valid_pmc_cnt; i++) {
		lenout = printf("%s", valid_pmcs[i]);
		fflush(stdout);
		test_for_a_pmc(valid_pmcs[i], lenout);
	}
}
static void
list_all(void)
{
	int i, cnt, j;
	printf("PMC                                               Abbreviation\n");
	printf("--------------------------------------------------------------\n");
	for(i=0; i<valid_pmc_cnt; i++) {
		cnt = printf("%s", valid_pmcs[i]);
		for(j=cnt; j<52; j++) {
			printf(" ");
		}
		printf("%%%d\n", i);
	}
}


int
main(int argc, char **argv)
{
	int i, j, cnt;
	char *filename=NULL;
	const char *name=NULL;
	int help_only = 0;
	int test_mode = 0;
	int test_at = 0;

	get_cpuid_set();
	memset(glob_cpu, 0, sizeof(glob_cpu));
	while ((i = getopt(argc, argv, "ALHhvm:i:?e:TE:")) != -1) {
		switch (i) {
		case 'A':
			run_all = 1;
			break;
		case 'L':
			list_all();
			return(0);
		case 'H':
			printf("**********************************\n");
			explain_all();
			printf("**********************************\n");
			return(0);
			break;
		case 'T':
			test_mode = 1;
			break;
		case 'E':
			master_exp = parse_expression(optarg);
			if (master_exp) {
				set_manual_exp(master_exp);
			}
			break;
		case 'e':
			if (validate_expression(optarg)) {
				printf("Unknown expression %s\n", optarg);
				return(0);
			}
			name = optarg;
			set_expression(optarg);
			break;
		case 'm':
			max_to_collect = strtol(optarg, NULL, 0);
			if (max_to_collect > MAX_COUNTER_SLOTS) {
				/* You can't collect more than max in array */
				max_to_collect = MAX_COUNTER_SLOTS;
			}
			break;
		case 'v':
			verbose++;
			break;
		case 'h':
			help_only = 1;
			break;
		case 'i':
			filename = optarg;
			break;
		case '?':
		default:
		use:
			printf("Use %s [ -i inputfile -v -m max_to_collect -e expr -E -h -? -H]\n",
			       argv[0]);
			printf("-i inputfile -- use source as inputfile not stdin (if stdin collect)\n");
			printf("-v -- verbose dump debug type things -- you don't want this\n");
			printf("-m N -- maximum to collect is N measurments\n");
			printf("-e expr-name -- Do expression expr-name\n");
			printf("-E 'your expression' -- Do your expression\n");
			printf("-h -- Don't do the expression I put in -e xxx just explain what it does and exit\n");
			printf("-H -- Don't run anything, just explain all canned expressions\n");
			printf("-T -- Test all PMC's defined by this processor\n");
			printf("-A -- Run all canned tests\n");
			return(0);
			break;
		}
	}
	if ((run_all == 0) && (name == NULL) && (filename == NULL) &&
	    (test_mode == 0) && (master_exp == NULL)) {
		printf("Without setting an expression we cannot dynamically gather information\n");
		printf("you must supply a filename (and you probably want verbosity)\n");
		goto use;
	}
	if (run_all && max_to_collect > 10) {
		max_to_collect = 3;
	}
	if (test_mode) {
		run_tests();
		return(0);
	}
	printf("*********************************\n");
	if ((master_exp == NULL) && name) {
		(*the_cpu.explain)(name);
	} else if (master_exp) {
		printf("Examine your expression ");
		print_exp(master_exp);
		printf("User defined threshold\n");
	}
	if (help_only) {
		return(0);
	}
	if (run_all) {
	more:
		name = the_cpu.ents[test_at].name;
		printf("***Test %s (threshold %s)****\n", name, the_cpu.ents[test_at].thresh);
		test_at++;
		if (set_expression(name) == -1) {
			if (test_at >= the_cpu.number) {
				goto done;
			} else
				goto more;
		}

	}
	process_file(filename);
	if (verbose >= 2) {
		for (i=0; i<ncnts; i++) {
			printf("Counter:%s cpu:%d index:%d\n",
			       cnts[i].counter_name,
			       cnts[i].cpu, i);
			for(j=0; j<cnts[i].pos; j++) {
				printf(" val - %ld\n", (long int)cnts[i].vals[j]);
			}
			printf(" sum - %ld\n", (long int)cnts[i].sum);
		}
	}
	if (expression == NULL) {
		return(0);
	}
	if (max_to_collect > 1) {
		for(i=0, cnt=0; i<MAX_CPU; i++) {
			if (glob_cpu[i]) {
				do_expression(glob_cpu[i], -1);
				cnt++;
				if (cnt == cpu_count_out) {
					printf("\n");
					break;
				} else {
					printf("\t");
				}
			}
		}
	}
	if (run_all && (test_at < the_cpu.number)) {
		memset(glob_cpu, 0, sizeof(glob_cpu));
		ncnts = 0;
		printf("*********************************\n");
		goto more;
	} else if (run_all) {
	done:
		printf("*********************************\n");
	}
	return(0);	
}
