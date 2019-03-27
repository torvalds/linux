/*-
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
#include <sys/cpuset.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/pmc.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <pmc.h>
#include <pmclog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sysexits.h>
#include <unistd.h>

#include "libpmcstat.h"

/*
 * Get PMC record by id, apply merge policy.
 */

static struct pmcstat_pmcrecord *
pmcstat_lookup_pmcid(pmc_id_t pmcid, int pmcstat_mergepmc)
{
	struct pmcstat_pmcrecord *pr;

	LIST_FOREACH(pr, &pmcstat_pmcs, pr_next) {
		if (pr->pr_pmcid == pmcid) {
			if (pmcstat_mergepmc)
				return pr->pr_merge;
			return pr;
		}
	}

	return NULL;
}

/*
 * Add a {pmcid,name} mapping.
 */

static void
pmcstat_pmcid_add(pmc_id_t pmcid, pmcstat_interned_string ps,
    struct pmcstat_args *args, struct pmc_plugins *plugins,
    int *pmcstat_npmcs)
{
	struct pmcstat_pmcrecord *pr, *prm;

	/* Replace an existing name for the PMC. */
	prm = NULL;
	LIST_FOREACH(pr, &pmcstat_pmcs, pr_next)
		if (pr->pr_pmcid == pmcid) {
			pr->pr_pmcname = ps;
			return;
		} else if (pr->pr_pmcname == ps)
			prm = pr;

	/*
	 * Otherwise, allocate a new descriptor and call the
	 * plugins hook.
	 */
	if ((pr = malloc(sizeof(*pr))) == NULL)
		err(EX_OSERR, "ERROR: Cannot allocate pmc record");

	pr->pr_pmcid = pmcid;
	pr->pr_pmcname = ps;
	pr->pr_pmcin = (*pmcstat_npmcs)++;
	pr->pr_samples = 0;
	pr->pr_dubious_frames = 0;
	pr->pr_merge = prm == NULL ? pr : prm;

	LIST_INSERT_HEAD(&pmcstat_pmcs, pr, pr_next);

	if (plugins[args->pa_pplugin].pl_newpmc != NULL)
		plugins[args->pa_pplugin].pl_newpmc(ps, pr);
	if (plugins[args->pa_plugin].pl_newpmc != NULL)
		plugins[args->pa_plugin].pl_newpmc(ps, pr);
}

/*
 * Unmap images in the range [start..end) associated with process
 * 'pp'.
 */

static void
pmcstat_image_unmap(struct pmcstat_process *pp, uintfptr_t start,
    uintfptr_t end)
{
	struct pmcstat_pcmap *pcm, *pcmtmp, *pcmnew;

	assert(pp != NULL);
	assert(start < end);

	/*
	 * Cases:
	 * - we could have the range completely in the middle of an
	 *   existing pcmap; in this case we have to split the pcmap
	 *   structure into two (i.e., generate a 'hole').
	 * - we could have the range covering multiple pcmaps; these
	 *   will have to be removed.
	 * - we could have either 'start' or 'end' falling in the
	 *   middle of a pcmap; in this case shorten the entry.
	 */
	TAILQ_FOREACH_SAFE(pcm, &pp->pp_map, ppm_next, pcmtmp) {
		assert(pcm->ppm_lowpc < pcm->ppm_highpc);
		if (pcm->ppm_highpc <= start)
			continue;
		if (pcm->ppm_lowpc >= end)
			return;
		if (pcm->ppm_lowpc >= start && pcm->ppm_highpc <= end) {
			/*
			 * The current pcmap is completely inside the
			 * unmapped range: remove it entirely.
			 */
			TAILQ_REMOVE(&pp->pp_map, pcm, ppm_next);
			free(pcm);
		} else if (pcm->ppm_lowpc < start && pcm->ppm_highpc > end) {
			/*
			 * Split this pcmap into two; curtail the
			 * current map to end at [start-1], and start
			 * the new one at [end].
			 */
			if ((pcmnew = malloc(sizeof(*pcmnew))) == NULL)
				err(EX_OSERR,
				    "ERROR: Cannot split a map entry");

			pcmnew->ppm_image = pcm->ppm_image;

			pcmnew->ppm_lowpc = end;
			pcmnew->ppm_highpc = pcm->ppm_highpc;

			pcm->ppm_highpc = start;

			TAILQ_INSERT_AFTER(&pp->pp_map, pcm, pcmnew, ppm_next);

			return;
		} else if (pcm->ppm_lowpc < start && pcm->ppm_highpc <= end)
			pcm->ppm_highpc = start;
		else if (pcm->ppm_lowpc >= start && pcm->ppm_highpc > end)
			pcm->ppm_lowpc = end;
		else
			assert(0);
	}
}

/*
 * Convert a hwpmc(4) log to profile information.  A system-wide
 * callgraph is generated if FLAG_DO_CALLGRAPHS is set.  gmon.out
 * files usable by gprof(1) are created if FLAG_DO_GPROF is set.
 */
int
pmcstat_analyze_log(struct pmcstat_args *args,
    struct pmc_plugins *plugins,
    struct pmcstat_stats *pmcstat_stats,
    struct pmcstat_process *pmcstat_kernproc,
    int pmcstat_mergepmc,
    int *pmcstat_npmcs,
    int *ps_samples_period)
{
	uint32_t cpu, cpuflags;
	pid_t pid;
	struct pmcstat_image *image;
	struct pmcstat_process *pp, *ppnew;
	struct pmcstat_pcmap *ppm, *ppmtmp;
	struct pmclog_ev ev;
	struct pmcstat_pmcrecord *pmcr;
	pmcstat_interned_string image_path;

	assert(args->pa_flags & FLAG_DO_ANALYSIS);

	if (elf_version(EV_CURRENT) == EV_NONE)
		err(EX_UNAVAILABLE, "Elf library initialization failed");

	while (pmclog_read(args->pa_logparser, &ev) == 0) {
		assert(ev.pl_state == PMCLOG_OK);

		switch (ev.pl_type) {
		case PMCLOG_TYPE_INITIALIZE:
			if ((ev.pl_u.pl_i.pl_version & 0xFF000000) !=
			    PMC_VERSION_MAJOR << 24 && args->pa_verbosity > 0)
				warnx(
"WARNING: Log version 0x%x does not match compiled version 0x%x.",
				    ev.pl_u.pl_i.pl_version, PMC_VERSION_MAJOR);
			break;

		case PMCLOG_TYPE_MAP_IN:
			/*
			 * Introduce an address range mapping for a
			 * userland process or the kernel (pid == -1).
			 *
			 * We always allocate a process descriptor so
			 * that subsequent samples seen for this
			 * address range are mapped to the current
			 * object being mapped in.
			 */
			pid = ev.pl_u.pl_mi.pl_pid;
			if (pid == -1)
				pp = pmcstat_kernproc;
			else
				pp = pmcstat_process_lookup(pid,
				    PMCSTAT_ALLOCATE);

			assert(pp != NULL);

			image_path = pmcstat_string_intern(ev.pl_u.pl_mi.
			    pl_pathname);
			image = pmcstat_image_from_path(image_path, pid == -1,
			    args, plugins);
			if (image->pi_type == PMCSTAT_IMAGE_UNKNOWN)
				pmcstat_image_determine_type(image, args);
			if (image->pi_type != PMCSTAT_IMAGE_INDETERMINABLE)
				pmcstat_image_link(pp, image,
				    ev.pl_u.pl_mi.pl_start);
			break;

		case PMCLOG_TYPE_MAP_OUT:
			/*
			 * Remove an address map.
			 */
			pid = ev.pl_u.pl_mo.pl_pid;
			if (pid == -1)
				pp = pmcstat_kernproc;
			else
				pp = pmcstat_process_lookup(pid, 0);

			if (pp == NULL)	/* unknown process */
				break;

			pmcstat_image_unmap(pp, ev.pl_u.pl_mo.pl_start,
			    ev.pl_u.pl_mo.pl_end);
			break;

		case PMCLOG_TYPE_CALLCHAIN:
			pmcstat_stats->ps_samples_total++;
			*ps_samples_period += 1;

			cpuflags = ev.pl_u.pl_cc.pl_cpuflags;
			cpu = PMC_CALLCHAIN_CPUFLAGS_TO_CPU(cpuflags);

			if ((args->pa_flags & FLAG_FILTER_THREAD_ID) &&
				args->pa_tid != ev.pl_u.pl_cc.pl_tid) {
				pmcstat_stats->ps_samples_skipped++;
				break;
			}
			/* Filter on the CPU id. */
			if (!CPU_ISSET(cpu, &(args->pa_cpumask))) {
				pmcstat_stats->ps_samples_skipped++;
				break;
			}

			pp = pmcstat_process_lookup(ev.pl_u.pl_cc.pl_pid,
			    PMCSTAT_ALLOCATE);

			/* Get PMC record. */
			pmcr = pmcstat_lookup_pmcid(ev.pl_u.pl_cc.pl_pmcid, pmcstat_mergepmc);
			assert(pmcr != NULL);
			pmcr->pr_samples++;

			/*
			 * Call the plugins processing
			 */

			if (plugins[args->pa_pplugin].pl_process != NULL)
				plugins[args->pa_pplugin].pl_process(
				    pp, pmcr,
				    ev.pl_u.pl_cc.pl_npc,
				    ev.pl_u.pl_cc.pl_pc,
				    PMC_CALLCHAIN_CPUFLAGS_TO_USERMODE(cpuflags),
				    cpu);
			plugins[args->pa_plugin].pl_process(
			    pp, pmcr,
			    ev.pl_u.pl_cc.pl_npc,
			    ev.pl_u.pl_cc.pl_pc,
			    PMC_CALLCHAIN_CPUFLAGS_TO_USERMODE(cpuflags),
			    cpu);
			break;

		case PMCLOG_TYPE_PMCALLOCATE:
			/*
			 * Record the association pmc id between this
			 * PMC and its name.
			 */
			pmcstat_pmcid_add(ev.pl_u.pl_a.pl_pmcid,
			    pmcstat_string_intern(ev.pl_u.pl_a.pl_evname),
			    args, plugins, pmcstat_npmcs);
			break;

		case PMCLOG_TYPE_PMCALLOCATEDYN:
			/*
			 * Record the association pmc id between this
			 * PMC and its name.
			 */
			pmcstat_pmcid_add(ev.pl_u.pl_ad.pl_pmcid,
			    pmcstat_string_intern(ev.pl_u.pl_ad.pl_evname),
			    args, plugins, pmcstat_npmcs);
			break;

		case PMCLOG_TYPE_PROCEXEC:
			/*
			 * Change the executable image associated with
			 * a process.
			 */
			pp = pmcstat_process_lookup(ev.pl_u.pl_x.pl_pid,
			    PMCSTAT_ALLOCATE);

			/* delete the current process map */
			TAILQ_FOREACH_SAFE(ppm, &pp->pp_map, ppm_next, ppmtmp) {
				TAILQ_REMOVE(&pp->pp_map, ppm, ppm_next);
				free(ppm);
			}

			/*
			 * Associate this process image.
			 */
			image_path = pmcstat_string_intern(
				ev.pl_u.pl_x.pl_pathname);
			assert(image_path != NULL);
			pmcstat_process_exec(pp, image_path,
			    ev.pl_u.pl_x.pl_entryaddr, args,
			    plugins, pmcstat_stats);
			break;

		case PMCLOG_TYPE_PROCEXIT:

			/*
			 * Due to the way the log is generated, the
			 * last few samples corresponding to a process
			 * may appear in the log after the process
			 * exit event is recorded.  Thus we keep the
			 * process' descriptor and associated data
			 * structures around, but mark the process as
			 * having exited.
			 */
			pp = pmcstat_process_lookup(ev.pl_u.pl_e.pl_pid, 0);
			if (pp == NULL)
				break;
			pp->pp_isactive = 0;	/* mark as a zombie */
			break;

		case PMCLOG_TYPE_SYSEXIT:
			pp = pmcstat_process_lookup(ev.pl_u.pl_se.pl_pid, 0);
			if (pp == NULL)
				break;
			pp->pp_isactive = 0;	/* make a zombie */
			break;

		case PMCLOG_TYPE_PROCFORK:

			/*
			 * Allocate a process descriptor for the new
			 * (child) process.
			 */
			ppnew =
			    pmcstat_process_lookup(ev.pl_u.pl_f.pl_newpid,
				PMCSTAT_ALLOCATE);

			/*
			 * If we had been tracking the parent, clone
			 * its address maps.
			 */
			pp = pmcstat_process_lookup(ev.pl_u.pl_f.pl_oldpid, 0);
			if (pp == NULL)
				break;
			TAILQ_FOREACH(ppm, &pp->pp_map, ppm_next)
			    pmcstat_image_link(ppnew, ppm->ppm_image,
				ppm->ppm_lowpc);
			break;

		default:	/* other types of entries are not relevant */
			break;
		}
	}

	if (ev.pl_state == PMCLOG_EOF)
		return (PMCSTAT_FINISHED);
	else if (ev.pl_state == PMCLOG_REQUIRE_DATA)
		return (PMCSTAT_RUNNING);

	err(EX_DATAERR,
	    "ERROR: event parsing failed state: %d type: %d (record %jd, offset 0x%jx)",
	    ev.pl_state, ev.pl_type, (uintmax_t) ev.pl_count + 1, ev.pl_offset);
}

/*
 * Open a log file, for reading or writing.
 *
 * The function returns the fd of a successfully opened log or -1 in
 * case of failure.
 */

int
pmcstat_open_log(const char *path, int mode)
{
	int error, fd, cfd;
	size_t hlen;
	const char *p, *errstr;
	struct addrinfo hints, *res, *res0;
	char hostname[MAXHOSTNAMELEN];

	errstr = NULL;
	fd = -1;

	/*
	 * If 'path' is "-" then open one of stdin or stdout depending
	 * on the value of 'mode'.
	 *
	 * If 'path' contains a ':' and does not start with a '/' or '.',
	 * and is being opened for writing, treat it as a "host:port"
	 * specification and open a network socket.
	 *
	 * Otherwise, treat 'path' as a file name and open that.
	 */
	if (path[0] == '-' && path[1] == '\0')
		fd = (mode == PMCSTAT_OPEN_FOR_READ) ? 0 : 1;
	else if (path[0] != '/' &&
	    path[0] != '.' && strchr(path, ':') != NULL) {

		p = strrchr(path, ':');
		hlen = p - path;
		if (p == path || hlen >= sizeof(hostname)) {
			errstr = strerror(EINVAL);
			goto done;
		}

		assert(hlen < sizeof(hostname));
		(void) strncpy(hostname, path, hlen);
		hostname[hlen] = '\0';

		(void) memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		if ((error = getaddrinfo(hostname, p+1, &hints, &res0)) != 0) {
			errstr = gai_strerror(error);
			goto done;
		}

		fd = -1;
		for (res = res0; res; res = res->ai_next) {
			if ((fd = socket(res->ai_family, res->ai_socktype,
			    res->ai_protocol)) < 0) {
				errstr = strerror(errno);
				continue;
			}
			if (mode == PMCSTAT_OPEN_FOR_READ) {
				if (bind(fd, res->ai_addr, res->ai_addrlen) < 0) {
					errstr = strerror(errno);
					(void) close(fd);
					fd = -1;
					continue;
				}
				listen(fd, 1);
				cfd = accept(fd, NULL, NULL);
				(void) close(fd);
				if (cfd < 0) {
					errstr = strerror(errno);
					fd = -1;
					break;
				}
				fd = cfd;
			} else {
				if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
					errstr = strerror(errno);
					(void) close(fd);
					fd = -1;
					continue;
				}
			}
			errstr = NULL;
			break;
		}
		freeaddrinfo(res0);

	} else if ((fd = open(path, mode == PMCSTAT_OPEN_FOR_READ ?
		    O_RDONLY : (O_WRONLY|O_CREAT|O_TRUNC),
		    S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)) < 0)
			errstr = strerror(errno);

  done:
	if (errstr)
		errx(EX_OSERR, "ERROR: Cannot open \"%s\" for %s: %s.", path,
		    (mode == PMCSTAT_OPEN_FOR_READ ? "reading" : "writing"),
		    errstr);

	return (fd);
}

/*
 * Close a logfile, after first flushing all in-module queued data.
 */

int
pmcstat_close_log(struct pmcstat_args *args)
{
	/* If a local logfile is configured ask the kernel to stop
	 * and flush data. Kernel will close the file when data is flushed
	 * so keep the status to EXITING.
	 */
	if (args->pa_logfd != -1) {
		if (pmc_close_logfile() < 0)
			err(EX_OSERR, "ERROR: logging failed");
	}

	return (args->pa_flags & FLAG_HAS_PIPE ? PMCSTAT_EXITING :
	    PMCSTAT_FINISHED);
}

/*
 * Initialize module.
 */

void
pmcstat_initialize_logging(struct pmcstat_process **pmcstat_kernproc,
    struct pmcstat_args *args, struct pmc_plugins *plugins,
    int *pmcstat_npmcs, int *pmcstat_mergepmc)
{
	struct pmcstat_process *pmcstat_kp;
	int i;

	/* use a convenient format for 'ldd' output */
	if (setenv("LD_TRACE_LOADED_OBJECTS_FMT1","%o \"%p\" %x\n",1) != 0)
		err(EX_OSERR, "ERROR: Cannot setenv");

	/* Initialize hash tables */
	pmcstat_string_initialize();
	for (i = 0; i < PMCSTAT_NHASH; i++) {
		LIST_INIT(&pmcstat_image_hash[i]);
		LIST_INIT(&pmcstat_process_hash[i]);
	}

	/*
	 * Create a fake 'process' entry for the kernel with pid -1.
	 * hwpmc(4) will subsequently inform us about where the kernel
	 * and any loaded kernel modules are mapped.
	 */
	if ((pmcstat_kp = pmcstat_process_lookup((pid_t) -1,
	    PMCSTAT_ALLOCATE)) == NULL)
		err(EX_OSERR, "ERROR: Cannot initialize logging");

	*pmcstat_kernproc = pmcstat_kp;

	/* PMC count. */
	*pmcstat_npmcs = 0;

	/* Merge PMC with same name. */
	*pmcstat_mergepmc = args->pa_mergepmc;

	/*
	 * Initialize plugins
	 */

	if (plugins[args->pa_pplugin].pl_init != NULL)
		plugins[args->pa_pplugin].pl_init();
	if (plugins[args->pa_plugin].pl_init != NULL)
		plugins[args->pa_plugin].pl_init();
}

/*
 * Shutdown module.
 */

void
pmcstat_shutdown_logging(struct pmcstat_args *args,
    struct pmc_plugins *plugins,
    struct pmcstat_stats *pmcstat_stats)
{
	struct pmcstat_image *pi, *pitmp;
	struct pmcstat_process *pp, *pptmp;
	struct pmcstat_pcmap *ppm, *ppmtmp;
	FILE *mf;
	int i;

	/* determine where to send the map file */
	mf = NULL;
	if (args->pa_mapfilename != NULL)
		mf = (strcmp(args->pa_mapfilename, "-") == 0) ?
		    args->pa_printfile : fopen(args->pa_mapfilename, "w");

	if (mf == NULL && args->pa_flags & FLAG_DO_GPROF &&
	    args->pa_verbosity >= 2)
		mf = args->pa_printfile;

	if (mf)
		(void) fprintf(mf, "MAP:\n");

	/*
	 * Shutdown the plugins
	 */

	if (plugins[args->pa_plugin].pl_shutdown != NULL)
		plugins[args->pa_plugin].pl_shutdown(mf);
	if (plugins[args->pa_pplugin].pl_shutdown != NULL)
		plugins[args->pa_pplugin].pl_shutdown(mf);

	for (i = 0; i < PMCSTAT_NHASH; i++) {
		LIST_FOREACH_SAFE(pi, &pmcstat_image_hash[i], pi_next,
		    pitmp) {
			if (plugins[args->pa_plugin].pl_shutdownimage != NULL)
				plugins[args->pa_plugin].pl_shutdownimage(pi);
			if (plugins[args->pa_pplugin].pl_shutdownimage != NULL)
				plugins[args->pa_pplugin].pl_shutdownimage(pi);

			free(pi->pi_symbols);
			if (pi->pi_addr2line != NULL)
				pclose(pi->pi_addr2line);
			LIST_REMOVE(pi, pi_next);
			free(pi);
		}

		LIST_FOREACH_SAFE(pp, &pmcstat_process_hash[i], pp_next,
		    pptmp) {
			TAILQ_FOREACH_SAFE(ppm, &pp->pp_map, ppm_next, ppmtmp) {
				TAILQ_REMOVE(&pp->pp_map, ppm, ppm_next);
				free(ppm);
			}
			LIST_REMOVE(pp, pp_next);
			free(pp);
		}
	}

	pmcstat_string_shutdown();

	/*
	 * Print errors unless -q was specified.  Print all statistics
	 * if verbosity > 1.
	 */
#define	PRINT(N,V) do {							\
		if (pmcstat_stats->ps_##V || args->pa_verbosity >= 2)	\
			(void) fprintf(args->pa_printfile, " %-40s %d\n",\
			    N, pmcstat_stats->ps_##V);			\
	} while (0)

	if (args->pa_verbosity >= 1 && (args->pa_flags & FLAG_DO_ANALYSIS)) {
		(void) fprintf(args->pa_printfile, "CONVERSION STATISTICS:\n");
		PRINT("#exec/a.out", exec_aout);
		PRINT("#exec/elf", exec_elf);
		PRINT("#exec/unknown", exec_indeterminable);
		PRINT("#exec handling errors", exec_errors);
		PRINT("#samples/total", samples_total);
		PRINT("#samples/unclaimed", samples_unknown_offset);
		PRINT("#samples/unknown-object", samples_indeterminable);
		PRINT("#samples/unknown-function", samples_unknown_function);
		PRINT("#callchain/dubious-frames", callchain_dubious_frames);
	}

	if (mf)
		(void) fclose(mf);
}
