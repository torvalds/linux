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
#include <sys/event.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/module.h>
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
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sysexits.h>
#include <unistd.h>

#include "libpmcstat.h"

/*
 * Associate an AOUT image with a process.
 */

void
pmcstat_process_aout_exec(struct pmcstat_process *pp,
    struct pmcstat_image *image, uintfptr_t entryaddr)
{
	(void) pp;
	(void) image;
	(void) entryaddr;
	/* TODO Implement a.out handling */
}

/*
 * Associate an ELF image with a process.
 */

void
pmcstat_process_elf_exec(struct pmcstat_process *pp,
    struct pmcstat_image *image, uintfptr_t entryaddr,
    struct pmcstat_args *args, struct pmc_plugins *plugins,
    struct pmcstat_stats *pmcstat_stats)
{
	uintmax_t libstart;
	struct pmcstat_image *rtldimage;

	assert(image->pi_type == PMCSTAT_IMAGE_ELF32 ||
	    image->pi_type == PMCSTAT_IMAGE_ELF64);

	/* Create a map entry for the base executable. */
	pmcstat_image_link(pp, image, image->pi_vaddr);

	/*
	 * For dynamically linked executables we need to determine
	 * where the dynamic linker was mapped to for this process,
	 * Subsequent executable objects that are mapped in by the
	 * dynamic linker will be tracked by log events of type
	 * PMCLOG_TYPE_MAP_IN.
	 */

	if (image->pi_isdynamic) {

		/*
		 * The runtime loader gets loaded just after the maximum
		 * possible heap address.  Like so:
		 *
		 * [  TEXT DATA BSS HEAP -->*RTLD  SHLIBS   <--STACK]
		 * ^					            ^
		 * 0				   VM_MAXUSER_ADDRESS

		 *
		 * The exact address where the loader gets mapped in
		 * will vary according to the size of the executable
		 * and the limits on the size of the process'es data
		 * segment at the time of exec().  The entry address
		 * recorded at process exec time corresponds to the
		 * 'start' address inside the dynamic linker.  From
		 * this we can figure out the address where the
		 * runtime loader's file object had been mapped to.
		 */
		rtldimage = pmcstat_image_from_path(image->pi_dynlinkerpath,
		    0, args, plugins);
		if (rtldimage == NULL) {
			warnx("WARNING: Cannot find image for \"%s\".",
			    pmcstat_string_unintern(image->pi_dynlinkerpath));
			pmcstat_stats->ps_exec_errors++;
			return;
		}

		if (rtldimage->pi_type == PMCSTAT_IMAGE_UNKNOWN)
			pmcstat_image_get_elf_params(rtldimage, args);

		if (rtldimage->pi_type != PMCSTAT_IMAGE_ELF32 &&
		    rtldimage->pi_type != PMCSTAT_IMAGE_ELF64) {
			warnx("WARNING: rtld not an ELF object \"%s\".",
			    pmcstat_string_unintern(image->pi_dynlinkerpath));
			return;
		}

		libstart = entryaddr - rtldimage->pi_entry;
		pmcstat_image_link(pp, rtldimage, libstart);
	}
}

/*
 * Associate an image and a process.
 */

void
pmcstat_process_exec(struct pmcstat_process *pp,
    pmcstat_interned_string path, uintfptr_t entryaddr,
    struct pmcstat_args *args, struct pmc_plugins *plugins,
    struct pmcstat_stats *pmcstat_stats)
{
	struct pmcstat_image *image;

	if ((image = pmcstat_image_from_path(path, 0,
	    args, plugins)) == NULL) {
		pmcstat_stats->ps_exec_errors++;
		return;
	}

	if (image->pi_type == PMCSTAT_IMAGE_UNKNOWN)
		pmcstat_image_determine_type(image, args);

	assert(image->pi_type != PMCSTAT_IMAGE_UNKNOWN);

	switch (image->pi_type) {
	case PMCSTAT_IMAGE_ELF32:
	case PMCSTAT_IMAGE_ELF64:
		pmcstat_stats->ps_exec_elf++;
		pmcstat_process_elf_exec(pp, image, entryaddr,
		    args, plugins, pmcstat_stats);
		break;

	case PMCSTAT_IMAGE_AOUT:
		pmcstat_stats->ps_exec_aout++;
		pmcstat_process_aout_exec(pp, image, entryaddr);
		break;

	case PMCSTAT_IMAGE_INDETERMINABLE:
		pmcstat_stats->ps_exec_indeterminable++;
		break;

	default:
		err(EX_SOFTWARE,
		    "ERROR: Unsupported executable type for \"%s\"",
		    pmcstat_string_unintern(path));
	}
}

/*
 * Find the map entry associated with process 'p' at PC value 'pc'.
 */

struct pmcstat_pcmap *
pmcstat_process_find_map(struct pmcstat_process *p, uintfptr_t pc)
{
	struct pmcstat_pcmap *ppm;

	TAILQ_FOREACH(ppm, &p->pp_map, ppm_next) {
		if (pc >= ppm->ppm_lowpc && pc < ppm->ppm_highpc)
			return (ppm);
		if (pc < ppm->ppm_lowpc)
			return (NULL);
	}

	return (NULL);
}

/*
 * Find the process descriptor corresponding to a PID.  If 'allocate'
 * is zero, we return a NULL if a pid descriptor could not be found or
 * a process descriptor process.  If 'allocate' is non-zero, then we
 * will attempt to allocate a fresh process descriptor.  Zombie
 * process descriptors are only removed if a fresh allocation for the
 * same PID is requested.
 */

struct pmcstat_process *
pmcstat_process_lookup(pid_t pid, int allocate)
{
	uint32_t hash;
	struct pmcstat_pcmap *ppm, *ppmtmp;
	struct pmcstat_process *pp, *pptmp;

	hash = (uint32_t) pid & PMCSTAT_HASH_MASK;	/* simplicity wins */

	LIST_FOREACH_SAFE(pp, &pmcstat_process_hash[hash], pp_next, pptmp)
		if (pp->pp_pid == pid) {
			/* Found a descriptor, check and process zombies */
			if (allocate && pp->pp_isactive == 0) {
				/* remove maps */
				TAILQ_FOREACH_SAFE(ppm, &pp->pp_map, ppm_next,
				    ppmtmp) {
					TAILQ_REMOVE(&pp->pp_map, ppm,
					    ppm_next);
					free(ppm);
				}
				/* remove process entry */
				LIST_REMOVE(pp, pp_next);
				free(pp);
				break;
			}
			return (pp);
		}

	if (!allocate)
		return (NULL);

	if ((pp = malloc(sizeof(*pp))) == NULL)
		err(EX_OSERR, "ERROR: Cannot allocate pid descriptor");

	pp->pp_pid = pid;
	pp->pp_isactive = 1;

	TAILQ_INIT(&pp->pp_map);

	LIST_INSERT_HEAD(&pmcstat_process_hash[hash], pp, pp_next);
	return (pp);
}

void
pmcstat_create_process(int *pmcstat_sockpair, struct pmcstat_args *args,
    int pmcstat_kq)
{
	char token;
	pid_t pid;
	struct kevent kev;
	struct pmcstat_target *pt;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, pmcstat_sockpair) < 0)
		err(EX_OSERR, "ERROR: cannot create socket pair");

	switch (pid = fork()) {
	case -1:
		err(EX_OSERR, "ERROR: cannot fork");
		/*NOTREACHED*/

	case 0:		/* child */
		(void) close(pmcstat_sockpair[PARENTSOCKET]);

		/* Write a token to tell our parent we've started executing. */
		if (write(pmcstat_sockpair[CHILDSOCKET], "+", 1) != 1)
			err(EX_OSERR, "ERROR (child): cannot write token");

		/* Wait for our parent to signal us to start. */
		if (read(pmcstat_sockpair[CHILDSOCKET], &token, 1) < 0)
			err(EX_OSERR, "ERROR (child): cannot read token");
		(void) close(pmcstat_sockpair[CHILDSOCKET]);

		/* exec() the program requested */
		execvp(*args->pa_argv, args->pa_argv);
		/* and if that fails, notify the parent */
		kill(getppid(), SIGCHLD);
		err(EX_OSERR, "ERROR: execvp \"%s\" failed", *args->pa_argv);
		/*NOTREACHED*/

	default:	/* parent */
		(void) close(pmcstat_sockpair[CHILDSOCKET]);
		break;
	}

	/* Ask to be notified via a kevent when the target process exits. */
	EV_SET(&kev, pid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0,
	    NULL);
	if (kevent(pmcstat_kq, &kev, 1, NULL, 0, NULL) < 0)
		err(EX_OSERR, "ERROR: cannot monitor child process %d", pid);

	if ((pt = malloc(sizeof(*pt))) == NULL)
		errx(EX_SOFTWARE, "ERROR: Out of memory.");

	pt->pt_pid = pid;
	SLIST_INSERT_HEAD(&args->pa_targets, pt, pt_next);

	/* Wait for the child to signal that its ready to go. */
	if (read(pmcstat_sockpair[PARENTSOCKET], &token, 1) < 0)
		err(EX_OSERR, "ERROR (parent): cannot read token");

	return;
}

/*
 * Do process profiling
 *
 * If a pid was specified, attach each allocated PMC to the target
 * process.  Otherwise, fork a child and attach the PMCs to the child,
 * and have the child exec() the target program.
 */

void
pmcstat_start_process(int *pmcstat_sockpair)
{
	/* Signal the child to proceed. */
	if (write(pmcstat_sockpair[PARENTSOCKET], "!", 1) != 1)
		err(EX_OSERR, "ERROR (parent): write of token failed");

	(void) close(pmcstat_sockpair[PARENTSOCKET]);
}

void
pmcstat_attach_pmcs(struct pmcstat_args *args)
{
	struct pmcstat_ev *ev;
	struct pmcstat_target *pt;
	int count;

	/* Attach all process PMCs to target processes. */
	count = 0;
	STAILQ_FOREACH(ev, &args->pa_events, ev_next) {
		if (PMC_IS_SYSTEM_MODE(ev->ev_mode))
			continue;
		SLIST_FOREACH(pt, &args->pa_targets, pt_next) {
			if (pmc_attach(ev->ev_pmcid, pt->pt_pid) == 0)
				count++;
			else if (errno != ESRCH)
				err(EX_OSERR,
"ERROR: cannot attach pmc \"%s\" to process %d",
				    ev->ev_name, (int)pt->pt_pid);
		}
	}

	if (count == 0)
		errx(EX_DATAERR, "ERROR: No processes were attached to.");
}
