/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2007, Joseph Koshy
 * Copyright (c) 2007 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by A. Joseph Koshy under
 * sponsorship from the FreeBSD Foundation and Google, Inc.
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

/*
 * Transform a hwpmc(4) log into human readable form, and into
 * gprof(1) compatible profiles.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/cpuset.h>
#include <sys/gmon.h>
#include <sys/imgact_aout.h>
#include <sys/imgact_elf.h>
#include <sys/mman.h>
#include <sys/pmc.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <netinet/in.h>

#include <assert.h>
#include <curses.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <gelf.h>
#include <libgen.h>
#include <limits.h>
#include <netdb.h>
#include <pmc.h>
#include <pmclog.h>
#include <sysexits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pmcstat.h"
#include "pmcstat_log.h"
#include "pmcstat_top.h"

/*
 * PUBLIC INTERFACES
 *
 * pmcstat_initialize_logging()	initialize this module, called first
 * pmcstat_shutdown_logging()		orderly shutdown, called last
 * pmcstat_open_log()			open an eventlog for processing
 * pmcstat_process_log()		print/convert an event log
 * pmcstat_display_log()		top mode display for the log
 * pmcstat_close_log()			finish processing an event log
 *
 * IMPLEMENTATION NOTES
 *
 * We correlate each 'callchain' or 'sample' entry seen in the event
 * log back to an executable object in the system. Executable objects
 * include:
 * 	- program executables,
 *	- shared libraries loaded by the runtime loader,
 *	- dlopen()'ed objects loaded by the program,
 *	- the runtime loader itself,
 *	- the kernel and kernel modules.
 *
 * Each process that we know about is treated as a set of regions that
 * map to executable objects.  Processes are described by
 * 'pmcstat_process' structures.  Executable objects are tracked by
 * 'pmcstat_image' structures.  The kernel and kernel modules are
 * common to all processes (they reside at the same virtual addresses
 * for all processes).  Individual processes can have their text
 * segments and shared libraries loaded at process-specific locations.
 *
 * A given executable object can be in use by multiple processes
 * (e.g., libc.so) and loaded at a different address in each.
 * pmcstat_pcmap structures track per-image mappings.
 *
 * The sample log could have samples from multiple PMCs; we
 * generate one 'gmon.out' profile per PMC.
 *
 * IMPLEMENTATION OF GMON OUTPUT
 *
 * Each executable object gets one 'gmon.out' profile, per PMC in
 * use.  Creation of 'gmon.out' profiles is done lazily.  The
 * 'gmon.out' profiles generated for a given sampling PMC are
 * aggregates of all the samples for that particular executable
 * object.
 *
 * IMPLEMENTATION OF SYSTEM-WIDE CALLGRAPH OUTPUT
 *
 * Each active pmcid has its own callgraph structure, described by a
 * 'struct pmcstat_callgraph'.  Given a process id and a list of pc
 * values, we map each pc value to a tuple (image, symbol), where
 * 'image' denotes an executable object and 'symbol' is the closest
 * symbol that precedes the pc value.  Each pc value in the list is
 * also given a 'rank' that reflects its depth in the call stack.
 */

struct pmcstat_pmcs pmcstat_pmcs = LIST_HEAD_INITIALIZER(pmcstat_pmcs);

/*
 * All image descriptors are kept in a hash table.
 */
struct pmcstat_image_hash_list pmcstat_image_hash[PMCSTAT_NHASH];

/*
 * All process descriptors are kept in a hash table.
 */
struct pmcstat_process_hash_list pmcstat_process_hash[PMCSTAT_NHASH];

struct pmcstat_stats pmcstat_stats; /* statistics */
static int ps_samples_period; /* samples count between top refresh. */

struct pmcstat_process *pmcstat_kernproc; /* kernel 'process' */

#include "pmcpl_gprof.h"
#include "pmcpl_callgraph.h"
#include "pmcpl_annotate.h"
#include "pmcpl_annotate_cg.h"
#include "pmcpl_calltree.h"

static struct pmc_plugins plugins[] = {
	{
		.pl_name		= "none",
	},
	{
		.pl_name		= "callgraph",
		.pl_init		= pmcpl_cg_init,
		.pl_shutdown		= pmcpl_cg_shutdown,
		.pl_process		= pmcpl_cg_process,
		.pl_topkeypress		= pmcpl_cg_topkeypress,
		.pl_topdisplay		= pmcpl_cg_topdisplay
	},
	{
		.pl_name		= "gprof",
		.pl_shutdown		= pmcpl_gmon_shutdown,
		.pl_process		= pmcpl_gmon_process,
		.pl_initimage		= pmcpl_gmon_initimage,
		.pl_shutdownimage	= pmcpl_gmon_shutdownimage,
		.pl_newpmc		= pmcpl_gmon_newpmc
	},
	{
		.pl_name		= "annotate",
		.pl_process		= pmcpl_annotate_process
	},
	{
		.pl_name		= "calltree",
		.pl_configure		= pmcpl_ct_configure,
		.pl_init		= pmcpl_ct_init,
		.pl_shutdown		= pmcpl_ct_shutdown,
		.pl_process		= pmcpl_ct_process,
		.pl_topkeypress		= pmcpl_ct_topkeypress,
		.pl_topdisplay		= pmcpl_ct_topdisplay
	},
	{
		.pl_name		= "annotate_cg",
		.pl_process		= pmcpl_annotate_cg_process
	},

	{
		.pl_name		= NULL
	}
};

static int pmcstat_mergepmc;

int pmcstat_pmcinfilter = 0; /* PMC filter for top mode. */
float pmcstat_threshold = 0.5; /* Cost filter for top mode. */

/*
 * Prototypes
 */

static void pmcstat_stats_reset(int _reset_global);

/*
 * PMC count.
 */
int pmcstat_npmcs;

/*
 * PMC Top mode pause state.
 */
static int pmcstat_pause;

static void
pmcstat_stats_reset(int reset_global)
{
	struct pmcstat_pmcrecord *pr;

	/* Flush PMCs stats. */
	LIST_FOREACH(pr, &pmcstat_pmcs, pr_next) {
		pr->pr_samples = 0;
		pr->pr_dubious_frames = 0;
	}
	ps_samples_period = 0;

	/* Flush global stats. */
	if (reset_global)
		bzero(&pmcstat_stats, sizeof(struct pmcstat_stats));
}

/*
 * Resolve file name and line number for the given address.
 */
int
pmcstat_image_addr2line(struct pmcstat_image *image, uintfptr_t addr,
    char *sourcefile, size_t sourcefile_len, unsigned *sourceline,
    char *funcname, size_t funcname_len)
{
	static int addr2line_warn = 0;

	char *sep, cmdline[PATH_MAX], imagepath[PATH_MAX];
	unsigned l;
	int fd;

	if (image->pi_addr2line == NULL) {
		/* Try default debug file location. */
		snprintf(imagepath, sizeof(imagepath),
		    "/usr/lib/debug/%s%s.debug",
		    args.pa_fsroot,
		    pmcstat_string_unintern(image->pi_fullpath));
		fd = open(imagepath, O_RDONLY);
		if (fd < 0) {
			/* Old kernel symbol path. */
			snprintf(imagepath, sizeof(imagepath), "%s%s.symbols",
			    args.pa_fsroot,
			    pmcstat_string_unintern(image->pi_fullpath));
			fd = open(imagepath, O_RDONLY);
			if (fd < 0) {
				snprintf(imagepath, sizeof(imagepath), "%s%s",
				    args.pa_fsroot,
				    pmcstat_string_unintern(
				        image->pi_fullpath));
			}
		}
		if (fd >= 0)
			close(fd);
		/*
		 * New addr2line support recursive inline function with -i
		 * but the format does not add a marker when no more entries
		 * are available.
		 */
		snprintf(cmdline, sizeof(cmdline), "addr2line -Cfe \"%s\"",
		    imagepath);
		image->pi_addr2line = popen(cmdline, "r+");
		if (image->pi_addr2line == NULL) {
			if (!addr2line_warn) {
				addr2line_warn = 1;
				warnx(
"WARNING: addr2line is needed for source code information."
				    );
			}
			return (0);
		}
	}

	if (feof(image->pi_addr2line) || ferror(image->pi_addr2line)) {
		warnx("WARNING: addr2line pipe error");
		pclose(image->pi_addr2line);
		image->pi_addr2line = NULL;
		return (0);
	}

	fprintf(image->pi_addr2line, "%p\n", (void *)addr);

	if (fgets(funcname, funcname_len, image->pi_addr2line) == NULL) {
		warnx("WARNING: addr2line function name read error");
		return (0);
	}
	sep = strchr(funcname, '\n');
	if (sep != NULL)
		*sep = '\0';

	if (fgets(sourcefile, sourcefile_len, image->pi_addr2line) == NULL) {
		warnx("WARNING: addr2line source file read error");
		return (0);
	}
	sep = strchr(sourcefile, ':');
	if (sep == NULL) {
		warnx("WARNING: addr2line source line separator missing");
		return (0);
	}
	*sep = '\0';
	l = atoi(sep+1);
	if (l == 0)
		return (0);
	*sourceline = l;
	return (1);
}

/*
 * Given a pmcid in use, find its human-readable name.
 */

const char *
pmcstat_pmcid_to_name(pmc_id_t pmcid)
{
	struct pmcstat_pmcrecord *pr;

	LIST_FOREACH(pr, &pmcstat_pmcs, pr_next)
	    if (pr->pr_pmcid == pmcid)
		    return (pmcstat_string_unintern(pr->pr_pmcname));

	return NULL;
}

/*
 * Convert PMC index to name.
 */

const char *
pmcstat_pmcindex_to_name(int pmcin)
{
	struct pmcstat_pmcrecord *pr;

	LIST_FOREACH(pr, &pmcstat_pmcs, pr_next)
		if (pr->pr_pmcin == pmcin)
			return pmcstat_string_unintern(pr->pr_pmcname);

	return NULL;
}

/*
 * Return PMC record with given index.
 */

struct pmcstat_pmcrecord *
pmcstat_pmcindex_to_pmcr(int pmcin)
{
	struct pmcstat_pmcrecord *pr;

	LIST_FOREACH(pr, &pmcstat_pmcs, pr_next)
		if (pr->pr_pmcin == pmcin)
			return pr;

	return NULL;
}

/*
 * Print log entries as text.
 */

static int
pmcstat_print_log(void)
{
	struct pmclog_ev ev;
	uint32_t npc;

	while (pmclog_read(args.pa_logparser, &ev) == 0) {
		assert(ev.pl_state == PMCLOG_OK);
		switch (ev.pl_type) {
		case PMCLOG_TYPE_CALLCHAIN:
			PMCSTAT_PRINT_ENTRY("callchain",
			    "%d 0x%x %d %d %c", ev.pl_u.pl_cc.pl_pid,
			    ev.pl_u.pl_cc.pl_pmcid,
			    PMC_CALLCHAIN_CPUFLAGS_TO_CPU(ev.pl_u.pl_cc. \
				pl_cpuflags), ev.pl_u.pl_cc.pl_npc,
			    PMC_CALLCHAIN_CPUFLAGS_TO_USERMODE(ev.pl_u.pl_cc.\
			        pl_cpuflags) ? 'u' : 's');
			for (npc = 0; npc < ev.pl_u.pl_cc.pl_npc; npc++)
				PMCSTAT_PRINT_ENTRY("...", "%p",
				    (void *) ev.pl_u.pl_cc.pl_pc[npc]);
			break;
		case PMCLOG_TYPE_CLOSELOG:
			PMCSTAT_PRINT_ENTRY("closelog",);
			break;
		case PMCLOG_TYPE_DROPNOTIFY:
			PMCSTAT_PRINT_ENTRY("drop",);
			break;
		case PMCLOG_TYPE_INITIALIZE:
			PMCSTAT_PRINT_ENTRY("initlog","0x%x \"%s\"",
			    ev.pl_u.pl_i.pl_version,
			    pmc_name_of_cputype(ev.pl_u.pl_i.pl_arch));
			if ((ev.pl_u.pl_i.pl_version & 0xFF000000) !=
			    PMC_VERSION_MAJOR << 24)
				warnx(
"WARNING: Log version 0x%x != expected version 0x%x.",
				    ev.pl_u.pl_i.pl_version, PMC_VERSION);
			break;
		case PMCLOG_TYPE_MAP_IN:
			PMCSTAT_PRINT_ENTRY("map-in","%d %p \"%s\"",
			    ev.pl_u.pl_mi.pl_pid,
			    (void *) ev.pl_u.pl_mi.pl_start,
			    ev.pl_u.pl_mi.pl_pathname);
			break;
		case PMCLOG_TYPE_MAP_OUT:
			PMCSTAT_PRINT_ENTRY("map-out","%d %p %p",
			    ev.pl_u.pl_mo.pl_pid,
			    (void *) ev.pl_u.pl_mo.pl_start,
			    (void *) ev.pl_u.pl_mo.pl_end);
			break;
		case PMCLOG_TYPE_PMCALLOCATE:
			PMCSTAT_PRINT_ENTRY("allocate","0x%x \"%s\" 0x%x",
			    ev.pl_u.pl_a.pl_pmcid,
			    ev.pl_u.pl_a.pl_evname,
			    ev.pl_u.pl_a.pl_flags);
			break;
		case PMCLOG_TYPE_PMCALLOCATEDYN:
			PMCSTAT_PRINT_ENTRY("allocatedyn","0x%x \"%s\" 0x%x",
			    ev.pl_u.pl_ad.pl_pmcid,
			    ev.pl_u.pl_ad.pl_evname,
			    ev.pl_u.pl_ad.pl_flags);
			break;
		case PMCLOG_TYPE_PMCATTACH:
			PMCSTAT_PRINT_ENTRY("attach","0x%x %d \"%s\"",
			    ev.pl_u.pl_t.pl_pmcid,
			    ev.pl_u.pl_t.pl_pid,
			    ev.pl_u.pl_t.pl_pathname);
			break;
		case PMCLOG_TYPE_PMCDETACH:
			PMCSTAT_PRINT_ENTRY("detach","0x%x %d",
			    ev.pl_u.pl_d.pl_pmcid,
			    ev.pl_u.pl_d.pl_pid);
			break;
		case PMCLOG_TYPE_PROCCSW:
			PMCSTAT_PRINT_ENTRY("cswval","0x%x %d %jd",
			    ev.pl_u.pl_c.pl_pmcid,
			    ev.pl_u.pl_c.pl_pid,
			    ev.pl_u.pl_c.pl_value);
			break;
		case PMCLOG_TYPE_PROCEXEC:
			PMCSTAT_PRINT_ENTRY("exec","0x%x %d %p \"%s\"",
			    ev.pl_u.pl_x.pl_pmcid,
			    ev.pl_u.pl_x.pl_pid,
			    (void *) ev.pl_u.pl_x.pl_entryaddr,
			    ev.pl_u.pl_x.pl_pathname);
			break;
		case PMCLOG_TYPE_PROCEXIT:
			PMCSTAT_PRINT_ENTRY("exitval","0x%x %d %jd",
			    ev.pl_u.pl_e.pl_pmcid,
			    ev.pl_u.pl_e.pl_pid,
			    ev.pl_u.pl_e.pl_value);
			break;
		case PMCLOG_TYPE_PROCFORK:
			PMCSTAT_PRINT_ENTRY("fork","%d %d",
			    ev.pl_u.pl_f.pl_oldpid,
			    ev.pl_u.pl_f.pl_newpid);
			break;
		case PMCLOG_TYPE_USERDATA:
			PMCSTAT_PRINT_ENTRY("userdata","0x%x",
			    ev.pl_u.pl_u.pl_userdata);
			break;
		case PMCLOG_TYPE_SYSEXIT:
			PMCSTAT_PRINT_ENTRY("exit","%d",
			    ev.pl_u.pl_se.pl_pid);
			break;
		default:
			fprintf(args.pa_printfile, "unknown event (type %d).\n",
			    ev.pl_type);
		}
	}

	if (ev.pl_state == PMCLOG_EOF)
		return (PMCSTAT_FINISHED);
	else if (ev.pl_state == PMCLOG_REQUIRE_DATA)
		return (PMCSTAT_RUNNING);

	errx(EX_DATAERR,
	    "ERROR: event parsing failed (record %jd, offset 0x%jx).",
	    (uintmax_t) ev.pl_count + 1, ev.pl_offset);
	/*NOTREACHED*/
}

/*
 * Public Interfaces.
 */

/*
 * Process a log file in offline analysis mode.
 */

int
pmcstat_process_log(void)
{

	/*
	 * If analysis has not been asked for, just print the log to
	 * the current output file.
	 */
	if (args.pa_flags & FLAG_DO_PRINT)
		return (pmcstat_print_log());
	else
		return (pmcstat_analyze_log(&args, plugins, &pmcstat_stats, pmcstat_kernproc,
		    pmcstat_mergepmc, &pmcstat_npmcs, &ps_samples_period));
}

/*
 * Refresh top display.
 */

static void
pmcstat_refresh_top(void)
{
	int v_attrs;
	float v;
	char pmcname[40];
	struct pmcstat_pmcrecord *pmcpr;

	/* If in pause mode do not refresh display. */
	if (pmcstat_pause)
		return;

	/* Wait until PMC pop in the log. */
	pmcpr = pmcstat_pmcindex_to_pmcr(pmcstat_pmcinfilter);
	if (pmcpr == NULL)
		return;

	/* Format PMC name. */
	if (pmcstat_mergepmc)
		snprintf(pmcname, sizeof(pmcname), "[%s]",
		    pmcstat_string_unintern(pmcpr->pr_pmcname));
	else
		snprintf(pmcname, sizeof(pmcname), "%s.%d",
		    pmcstat_string_unintern(pmcpr->pr_pmcname),
		    pmcstat_pmcinfilter);

	/* Format samples count. */
	if (ps_samples_period > 0)
		v = (pmcpr->pr_samples * 100.0) / ps_samples_period;
	else
		v = 0.;
	v_attrs = PMCSTAT_ATTRPERCENT(v);

	PMCSTAT_PRINTBEGIN();
	PMCSTAT_PRINTW("PMC: %s Samples: %u ",
	    pmcname,
	    pmcpr->pr_samples);
	PMCSTAT_ATTRON(v_attrs);
	PMCSTAT_PRINTW("(%.1f%%) ", v);
	PMCSTAT_ATTROFF(v_attrs);
	PMCSTAT_PRINTW(", %u unresolved\n\n",
	    pmcpr->pr_dubious_frames);
	if (plugins[args.pa_plugin].pl_topdisplay != NULL)
		plugins[args.pa_plugin].pl_topdisplay();
	PMCSTAT_PRINTEND();
}

/*
 * Find the next pmc index to display.
 */

static void
pmcstat_changefilter(void)
{
	int pmcin;
	struct pmcstat_pmcrecord *pmcr;

	/*
	 * Find the next merge target.
	 */
	if (pmcstat_mergepmc) {
		pmcin = pmcstat_pmcinfilter;

		do {
			pmcr = pmcstat_pmcindex_to_pmcr(pmcstat_pmcinfilter);
			if (pmcr == NULL || pmcr == pmcr->pr_merge)
				break;

			pmcstat_pmcinfilter++;
			if (pmcstat_pmcinfilter >= pmcstat_npmcs)
				pmcstat_pmcinfilter = 0;

		} while (pmcstat_pmcinfilter != pmcin);
	}
}

/*
 * Top mode keypress.
 */

int
pmcstat_keypress_log(void)
{
	int c, ret = 0;
	WINDOW *w;

	w = newwin(1, 0, 1, 0);
	c = wgetch(w);
	wprintw(w, "Key: %c => ", c);
	switch (c) {
	case 'c':
		wprintw(w, "enter mode 'd' or 'a' => ");
		c = wgetch(w);
		if (c == 'd') {
			args.pa_topmode = PMCSTAT_TOP_DELTA;
			wprintw(w, "switching to delta mode");
		} else {
			args.pa_topmode = PMCSTAT_TOP_ACCUM;
			wprintw(w, "switching to accumulation mode");
		}
		break;
	case 'm':
		pmcstat_mergepmc = !pmcstat_mergepmc;
		/*
		 * Changing merge state require data reset.
		 */
		if (plugins[args.pa_plugin].pl_shutdown != NULL)
			plugins[args.pa_plugin].pl_shutdown(NULL);
		pmcstat_stats_reset(0);
		if (plugins[args.pa_plugin].pl_init != NULL)
			plugins[args.pa_plugin].pl_init();

		/* Update filter to be on a merge target. */
		pmcstat_changefilter();
		wprintw(w, "merge PMC %s", pmcstat_mergepmc ? "on" : "off");
		break;
	case 'n':
		/* Close current plugin. */
		if (plugins[args.pa_plugin].pl_shutdown != NULL)
			plugins[args.pa_plugin].pl_shutdown(NULL);

		/* Find next top display available. */
		do {
			args.pa_plugin++;
			if (plugins[args.pa_plugin].pl_name == NULL)
				args.pa_plugin = 0;
		} while (plugins[args.pa_plugin].pl_topdisplay == NULL);

		/* Open new plugin. */
		pmcstat_stats_reset(0);
		if (plugins[args.pa_plugin].pl_init != NULL)
			plugins[args.pa_plugin].pl_init();
		wprintw(w, "switching to plugin %s",
		    plugins[args.pa_plugin].pl_name);
		break;
	case 'p':
		pmcstat_pmcinfilter++;
		if (pmcstat_pmcinfilter >= pmcstat_npmcs)
			pmcstat_pmcinfilter = 0;
		pmcstat_changefilter();
		wprintw(w, "switching to PMC %s.%d",
		    pmcstat_pmcindex_to_name(pmcstat_pmcinfilter),
		    pmcstat_pmcinfilter);
		break;
	case ' ':
		pmcstat_pause = !pmcstat_pause;
		if (pmcstat_pause)
			wprintw(w, "pause => press space again to continue");
		break;
	case 'q':
		wprintw(w, "exiting...");
		ret = 1;
		break;
	default:
		if (plugins[args.pa_plugin].pl_topkeypress != NULL)
			if (plugins[args.pa_plugin].pl_topkeypress(c, (void *)w))
				ret = 1;
	}

	wrefresh(w);
	delwin(w);
	return ret;
}


/*
 * Top mode display.
 */

void
pmcstat_display_log(void)
{

	pmcstat_refresh_top();

	/* Reset everythings if delta mode. */
	if (args.pa_topmode == PMCSTAT_TOP_DELTA) {
		if (plugins[args.pa_plugin].pl_shutdown != NULL)
			plugins[args.pa_plugin].pl_shutdown(NULL);
		pmcstat_stats_reset(0);
		if (plugins[args.pa_plugin].pl_init != NULL)
			plugins[args.pa_plugin].pl_init();
	}
}

/*
 * Configure a plugins.
 */

void
pmcstat_pluginconfigure_log(char *opt)
{

	if (strncmp(opt, "threshold=", 10) == 0) {
		pmcstat_threshold = atof(opt+10);
	} else {
		if (plugins[args.pa_plugin].pl_configure != NULL) {
			if (!plugins[args.pa_plugin].pl_configure(opt))
				err(EX_USAGE,
				    "ERROR: unknown option <%s>.", opt);
		}
	}
}

void
pmcstat_log_shutdown_logging(void)
{

	pmcstat_shutdown_logging(&args, plugins, &pmcstat_stats);
}

void
pmcstat_log_initialize_logging(void)
{

	pmcstat_initialize_logging(&pmcstat_kernproc,
	    &args, plugins, &pmcstat_npmcs, &pmcstat_mergepmc);
}
