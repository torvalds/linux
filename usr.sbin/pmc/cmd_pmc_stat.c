/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018, Matthew Macy
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
#include <sys/cpuset.h>
#include <sys/event.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/ttycom.h>
#include <sys/user.h>
#include <sys/wait.h>

#include <assert.h>
#include <curses.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <kvm.h>
#include <libgen.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <pmc.h>
#include <pmclog.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <libpmcstat.h>
#include "cmd_pmc.h"

/*
 * Return the frequency of the kernel's statistics clock.
 */
static int
getstathz(void)
{
	int mib[2];
	size_t size;
	struct clockinfo clockrate;

	mib[0] = CTL_KERN;
	mib[1] = KERN_CLOCKRATE;
	size = sizeof clockrate;
	if (sysctl(mib, 2, &clockrate, &size, NULL, 0) == -1)
		err(1, "sysctl kern.clockrate");
	return clockrate.stathz;
}

#define STAT_MODE_NPMCS 6
#define FIXED_MODE_NPMCS 2
static struct timespec before_ts;
#define CYCLES		0
#define INST		1
#define BR		2
#define IAP_START	BR
#define BR_MISS	3
#define CACHE	4
#define CACHE_MISS	5
static const char *pmc_stat_mode_names[] = {
	"cycles",
	"instructions",
	"branches",
	"branch-misses",
	"cache-references",
	"cache-misses",
};

static int pmcstat_sockpair[NSOCKPAIRFD];

static void __dead2
usage(void)
{
	errx(EX_USAGE,
	    "\t get basic stats from command line program\n"
	    "\t -j <eventlist>, --events <eventlist> comma-delimited list of event specifiers\n"
	    );
}

static void
showtime(FILE *out, struct timespec *before, struct timespec *after,
    struct rusage *ru)
{
	char decimal_point;
	uint64_t real, user, sys;

	(void)setlocale(LC_NUMERIC, "");
	decimal_point = localeconv()->decimal_point[0];

	after->tv_sec -= before->tv_sec;
	after->tv_nsec -= before->tv_nsec;
	if (after->tv_nsec < 0) {
		after->tv_sec--;
		after->tv_nsec += 1000000000;
	}

	real = (after->tv_sec * 1000000000 + after->tv_nsec) / 1000;
	user = ru->ru_utime.tv_sec * 1000000 + ru->ru_utime.tv_usec;
	sys = ru->ru_stime.tv_sec * 1000000 + ru->ru_stime.tv_usec;
	fprintf(out, "%13jd%c%02ld  real\t\t\t#\t%2.02f%% cpu\n",
	    (intmax_t)after->tv_sec, decimal_point,
	    after->tv_nsec / 10000000, 100 * (double)(sys + user + 1) / (double)(real + 1));
	fprintf(out, "%13jd%c%02ld  user\t\t\t#\t%2.2f%% cpu\n",
	    (intmax_t)ru->ru_utime.tv_sec, decimal_point,
	    ru->ru_utime.tv_usec / 10000, 100 * (double)(user + 1) / (double)(real + 1));
	fprintf(out, "%13jd%c%02ld  sys\t\t\t#\t%2.02f%% cpu\n",
	    (intmax_t)ru->ru_stime.tv_sec, decimal_point,
	    ru->ru_stime.tv_usec / 10000, 100 * (double)(sys + 1) / (double)(real + 1));
}

static const char *stat_mode_cntrs[STAT_MODE_NPMCS];
static const char *stat_mode_names[STAT_MODE_NPMCS];

static void
pmc_stat_setup_stat(int system_mode, const char *arg)
{
	const char *new_cntrs[STAT_MODE_NPMCS];
	static const char **pmc_stat_mode_cntrs;
	struct pmcstat_ev *ev;
	char *counters, *counter;
	int i, c, start, newcnt;
	cpuset_t cpumask, rootmask;

	if (cpuset_getaffinity(CPU_LEVEL_ROOT, CPU_WHICH_PID, -1,
	    sizeof(rootmask), &rootmask) == -1)
		err(EX_OSERR, "ERROR: Cannot determine the root set of CPUs");
	CPU_COPY(&rootmask, &cpumask);

	if (pmc_pmu_stat_mode(&pmc_stat_mode_cntrs) != 0)
		errx(EX_USAGE, "ERROR: hwmpc.ko not loaded or stat not supported on host.");
	if (system_mode && geteuid() != 0)
		errx(EX_USAGE, "ERROR: system mode counters can only be used as root");
	counters = NULL;
	for (i = 0; i < STAT_MODE_NPMCS; i++) {
		stat_mode_cntrs[i] = pmc_stat_mode_cntrs[i];
		stat_mode_names[i] = pmc_stat_mode_names[i];
	}
	if (arg) {
		counters = strdup(arg);
		newcnt = 0;
		while ((counter = strsep(&counters, ",")) != NULL &&
		    newcnt < STAT_MODE_NPMCS - IAP_START) {
			new_cntrs[newcnt++] = counter;
			if (pmc_pmu_sample_rate_get(counter) == DEFAULT_SAMPLE_COUNT)
				errx(EX_USAGE, "ERROR: %s not recognized on host", counter);
		}
		start = IAP_START + STAT_MODE_NPMCS - FIXED_MODE_NPMCS - newcnt;
		for (i = 0; i < newcnt; i++) {
			stat_mode_cntrs[start + i] = new_cntrs[i];
			stat_mode_names[start + i] = new_cntrs[i];
		}
	}
	if (system_mode)
		pmc_args.pa_flags |= FLAG_HAS_SYSTEM_PMCS;
	else
		pmc_args.pa_flags |= FLAG_HAS_PROCESS_PMCS;
	pmc_args.pa_flags |= FLAG_HAS_COUNTING_PMCS;
	pmc_args.pa_flags |= FLAG_HAS_COMMANDLINE | FLAG_HAS_TARGET;
	pmc_args.pa_flags |= FLAG_HAS_PIPE;
	pmc_args.pa_required |= FLAG_HAS_COMMANDLINE | FLAG_HAS_TARGET | FLAG_HAS_OUTPUT_LOGFILE;
	pmc_args.pa_outputpath = strdup("/dev/null");
	pmc_args.pa_logfd = pmcstat_open_log(pmc_args.pa_outputpath,
	    PMCSTAT_OPEN_FOR_WRITE);
	for (i = 0; i < STAT_MODE_NPMCS; i++) {
		if ((ev = malloc(sizeof(*ev))) == NULL)
			errx(EX_SOFTWARE, "ERROR: Out of memory.");
		if (system_mode)
			ev->ev_mode = PMC_MODE_SC;
		else
			ev->ev_mode = PMC_MODE_TC;
		ev->ev_spec = strdup(stat_mode_cntrs[i]);
		if (ev->ev_spec == NULL)
			errx(EX_SOFTWARE, "ERROR: Out of memory.");
		c = strcspn(strdup(stat_mode_cntrs[i]), ", \t");
		ev->ev_name = malloc(c + 1);
		if (ev->ev_name == NULL)
			errx(EX_SOFTWARE, "ERROR: Out of memory.");
		(void)strncpy(ev->ev_name, stat_mode_cntrs[i], c);
		*(ev->ev_name + c) = '\0';

		ev->ev_count = -1;
		ev->ev_flags = 0;
		ev->ev_flags |= PMC_F_DESCENDANTS;
		ev->ev_cumulative = 1;

		ev->ev_saved = 0LL;
		ev->ev_pmcid = PMC_ID_INVALID;
		STAILQ_INSERT_TAIL(&pmc_args.pa_events, ev, ev_next);
		if (system_mode) {
			ev->ev_cpu = CPU_FFS(&cpumask) - 1;
			CPU_CLR(ev->ev_cpu, &cpumask);
			pmcstat_clone_event_descriptor(ev, &cpumask, &pmc_args);
			CPU_SET(ev->ev_cpu, &cpumask);
		} else
			ev->ev_cpu = PMC_CPU_ANY;

	}
	if (clock_gettime(CLOCK_MONOTONIC, &before_ts))
		err(1, "clock_gettime");
}

static void
pmc_stat_print_stat(struct rusage *ru)
{
	struct pmcstat_ev *ev;
	struct timespec after;
	uint64_t cvals[STAT_MODE_NPMCS];
	uint64_t ticks, value;
	int hz, i;

	if (ru) {
		hz = getstathz();
		ticks = hz * (ru->ru_utime.tv_sec + ru->ru_stime.tv_sec) +
			hz * (ru->ru_utime.tv_usec + ru->ru_stime.tv_usec) / 1000000;
		if (clock_gettime(CLOCK_MONOTONIC, &after))
			err(1, "clock_gettime");
		/*
		 * If our round-off on the tick calculation still puts us at 0,
		 * then always assume at least one tick.
		 */
		if (ticks == 0)
			ticks = 1;
		fprintf(pmc_args.pa_printfile, "%16ld  %s\t\t#\t%02.03f M/sec\n",
			ru->ru_minflt, "page faults", ((double)ru->ru_minflt / (double)ticks) / hz);
		fprintf(pmc_args.pa_printfile, "%16ld  %s\t\t#\t%02.03f M/sec\n",
			ru->ru_nvcsw, "voluntary csw", ((double)ru->ru_nvcsw / (double)ticks) / hz);
		fprintf(pmc_args.pa_printfile, "%16ld  %s\t#\t%02.03f M/sec\n",
			ru->ru_nivcsw, "involuntary csw", ((double)ru->ru_nivcsw / (double)ticks) / hz);
	}

	bzero(&cvals, sizeof(cvals));
	STAILQ_FOREACH(ev, &pmc_args.pa_events, ev_next) {
		if (pmc_read(ev->ev_pmcid, &value) < 0)
			err(EX_OSERR, "ERROR: Cannot read pmc \"%s\"",
			    ev->ev_name);
		for (i = 0; i < STAT_MODE_NPMCS; i++)
			if (strcmp(ev->ev_name, stat_mode_cntrs[i]) == 0)
				cvals[i] += value;
	}

	fprintf(pmc_args.pa_printfile, "%16jd  %s\n", (uintmax_t)cvals[CYCLES], stat_mode_names[CYCLES]);
	fprintf(pmc_args.pa_printfile, "%16jd  %s\t\t#\t%01.03f inst/cycle\n", (uintmax_t)cvals[INST], stat_mode_names[INST],
	    (double)cvals[INST] / cvals[CYCLES]);
	fprintf(pmc_args.pa_printfile, "%16jd  %s\n", (uintmax_t)cvals[BR], stat_mode_names[BR]);
	if (stat_mode_names[BR_MISS] == pmc_stat_mode_names[BR_MISS])
		fprintf(pmc_args.pa_printfile, "%16jd  %s\t\t#\t%.03f%%\n",
		    (uintmax_t)cvals[BR_MISS], stat_mode_names[BR_MISS],
		    100 * ((double)cvals[BR_MISS] / cvals[BR]));
	else
		fprintf(pmc_args.pa_printfile, "%16jd  %s\n",
		    (uintmax_t)cvals[BR_MISS], stat_mode_names[BR_MISS]);
	fprintf(pmc_args.pa_printfile, "%16jd  %s%s", (uintmax_t)cvals[CACHE], stat_mode_names[CACHE],
	    stat_mode_names[CACHE] != pmc_stat_mode_names[CACHE] ? "\n" : "");
	if (stat_mode_names[CACHE] == pmc_stat_mode_names[CACHE])
		fprintf(pmc_args.pa_printfile, "\t#\t%.03f refs/inst\n",
		    ((double)cvals[CACHE] / cvals[INST]));
	fprintf(pmc_args.pa_printfile, "%16jd  %s%s", (uintmax_t)cvals[CACHE_MISS], stat_mode_names[CACHE_MISS],
	    stat_mode_names[CACHE_MISS] != pmc_stat_mode_names[CACHE_MISS] ? "\n" : "");
	if (stat_mode_names[CACHE_MISS] == pmc_stat_mode_names[CACHE_MISS])
		fprintf(pmc_args.pa_printfile, "\t\t#\t%.03f%%\n",
		    100 * ((double)cvals[CACHE_MISS] / cvals[CACHE]));

	if (ru)
		showtime(pmc_args.pa_printfile, &before_ts, &after, ru);
}

static struct option longopts[] = {
	{"events", required_argument, NULL, 'j'},
	{NULL, 0, NULL, 0}
};

static int
pmc_stat_internal(int argc, char **argv, int system_mode)
{
	char *event, *r;
	struct sigaction sa;
	struct kevent kev;
	struct rusage ru;
	struct winsize ws;
	struct pmcstat_ev *ev;
	int c, option, runstate;
	int waitstatus, ru_valid, do_debug;

	do_debug = ru_valid = 0;
	r = event = NULL;
	while ((option = getopt_long(argc, argv, "dj:", longopts, NULL)) != -1) {
		switch (option) {
		case 'j':
			r = event = strdup(optarg);
			break;
		case 'd':
			do_debug = 1;
			break;
		case '?':
		default:
			usage();
		}
	}
	pmc_args.pa_argc = (argc -= optind);
	pmc_args.pa_argv = (argv += optind);
	if (argc == 0)
		usage();
	pmc_args.pa_flags |= FLAG_HAS_COMMANDLINE;
	pmc_stat_setup_stat(system_mode, event);
	free(r);
	bzero(&ru, sizeof(ru));
	EV_SET(&kev, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
	if (kevent(pmc_kq, &kev, 1, NULL, 0, NULL) < 0)
		err(EX_OSERR, "ERROR: Cannot register kevent for SIGINT");

	EV_SET(&kev, SIGIO, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
	if (kevent(pmc_kq, &kev, 1, NULL, 0, NULL) < 0)
		err(EX_OSERR, "ERROR: Cannot register kevent for SIGIO");
	EV_SET(&kev, 0, EVFILT_TIMER, EV_ADD, 0, 1000, NULL);
	if (kevent(pmc_kq, &kev, 1, NULL, 0, NULL) < 0)
		err(EX_OSERR,
			"ERROR: Cannot register kevent for timer");

	STAILQ_FOREACH(ev, &pmc_args.pa_events, ev_next) {
		if (pmc_allocate(ev->ev_spec, ev->ev_mode,
		    ev->ev_flags, ev->ev_cpu, &ev->ev_pmcid, ev->ev_count) < 0)
			err(EX_OSERR,
			    "ERROR: Cannot allocate %s-mode pmc with specification \"%s\"",
			    PMC_IS_SYSTEM_MODE(ev->ev_mode) ?
			    "system" : "process", ev->ev_spec);

		if (PMC_IS_SAMPLING_MODE(ev->ev_mode) &&
		    pmc_set(ev->ev_pmcid, ev->ev_count) < 0)
			err(EX_OSERR,
			    "ERROR: Cannot set sampling count for PMC \"%s\"",
			    ev->ev_name);
	}

	/*
	 * An exec() failure of a forked child is signalled by the
	 * child sending the parent a SIGCHLD.  We don't register an
	 * actual signal handler for SIGCHLD, but instead use our
	 * kqueue to pick up the signal.
	 */
	EV_SET(&kev, SIGCHLD, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
	if (kevent(pmc_kq, &kev, 1, NULL, 0, NULL) < 0)
		err(EX_OSERR, "ERROR: Cannot register kevent for SIGCHLD");

	pmcstat_create_process(pmcstat_sockpair, &pmc_args, pmc_kq);

	if (SLIST_EMPTY(&pmc_args.pa_targets))
		errx(EX_DATAERR,
		    "ERROR: No matching target processes.");
	if (pmc_args.pa_flags & FLAG_HAS_PROCESS_PMCS)
		pmcstat_attach_pmcs(&pmc_args);

	/* start the pmcs */
	pmc_util_start_pmcs(&pmc_args);

	/* start the (commandline) process if needed */
	pmcstat_start_process(pmcstat_sockpair);

	/* Handle SIGINT using the kqueue loop */
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	(void)sigemptyset(&sa.sa_mask);

	if (sigaction(SIGINT, &sa, NULL) < 0)
		err(EX_OSERR, "ERROR: Cannot install signal handler");

	/*
 * loop till either the target process (if any) exits, or we
 * are killed by a SIGINT or we reached the time duration.
 */
	runstate = PMCSTAT_RUNNING;
	do {
		if ((c = kevent(pmc_kq, NULL, 0, &kev, 1, NULL)) <= 0) {
			if (errno != EINTR)
				err(EX_OSERR, "ERROR: kevent failed");
			else
				continue;
		}
		if (kev.flags & EV_ERROR)
			errc(EX_OSERR, kev.data, "ERROR: kevent failed");

		switch (kev.filter) {
		case EVFILT_PROC:	/* target has exited */
			if (wait4(pmc_util_get_pid(&pmc_args), &waitstatus, 0, &ru) > 0) {
				getrusage(RUSAGE_CHILDREN, &ru);
				ru_valid = 1;
			}
			break;

		case EVFILT_READ:	/* log file data is present */
			break;
		case EVFILT_TIMER:
			if (do_debug)
				pmc_stat_print_stat(NULL);
			break;
		case EVFILT_SIGNAL:
			if (kev.ident == SIGCHLD) {
				/*
				 * The child process sends us a
				 * SIGCHLD if its exec() failed.  We
				 * wait for it to exit and then exit
				 * ourselves.
				 */
				(void)wait(&c);
				runstate = PMCSTAT_FINISHED;
			} else if (kev.ident == SIGIO) {
				/*
				 * We get a SIGIO if a PMC loses all
				 * of its targets, or if logfile
				 * writes encounter an error.
				 */
				if (wait4(pmc_util_get_pid(&pmc_args), &waitstatus, 0, &ru) > 0) {
					getrusage(RUSAGE_CHILDREN, &ru);
					ru_valid = 1;
				}
				runstate = pmcstat_close_log(&pmc_args);
			} else if (kev.ident == SIGINT) {
				/* Kill the child process if we started it */
				if (pmc_args.pa_flags & FLAG_HAS_COMMANDLINE)
					pmc_util_kill_process(&pmc_args);
				runstate = pmcstat_close_log(&pmc_args);
			} else if (kev.ident == SIGWINCH) {
				if (ioctl(fileno(pmc_args.pa_printfile),
				    TIOCGWINSZ, &ws) < 0)
					err(EX_OSERR,
					    "ERROR: Cannot determine window size");
				pmc_displayheight = ws.ws_row - 1;
				pmc_displaywidth = ws.ws_col - 1;
			} else
				assert(0);

			break;
		}
	} while (runstate != PMCSTAT_FINISHED);
	if (!ru_valid)
		warnx("couldn't get rusage");
	pmc_stat_print_stat(&ru);
	pmc_util_cleanup(&pmc_args);
	return (0);
}

int
cmd_pmc_stat(int argc, char **argv)
{
	return (pmc_stat_internal(argc, argv, 0));
}

int
cmd_pmc_stat_system(int argc, char **argv)
{
	return (pmc_stat_internal(argc, argv, 1));
}
