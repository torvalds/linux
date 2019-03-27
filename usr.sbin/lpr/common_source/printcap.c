/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if 0
#ifndef lint
static char sccsid[] = "@(#)printcap.c	8.2 (Berkeley) 4/28/95";
#endif /* not lint */
#endif

#include "lp.cdefs.h"		/* A cross-platform version of <sys/cdefs.h> */
__FBSDID("$FreeBSD$");

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/param.h>		/* required for lp.h, but not used here */
#include <sys/dirent.h>		/* ditto */
#include "lp.h"
#include "lp.local.h"
#include "pathnames.h"

/*
 * Routines and data used in processing the printcap file.
 */
static char *printcapdb[2] = { _PATH_PRINTCAP, 0 };  /* list for cget* */

static char 	*capdb_canonical_name(const char *_bp);
static int	 capdb_getaltlog(char *_bp, const char *_shrt,
		    const char *_lng);
static int	 capdb_getaltnum(char *_bp, const char *_shrt,
		    const char *_lng, long _dflt, long *_result);
static int	 capdb_getaltstr(char *_bp, const char *_shrt,
		    const char *lng, const char *_dflt, char **_result);
static int	 getprintcap_int(char *_bp, struct printer *_pp);

/*
 * Change the name of the printcap file.  Used by chkprintcap(8),
 * but could be used by other members of the suite with appropriate
 * security measures.
 */
void
setprintcap(char *newfile)
{
	printcapdb[0] = newfile;
}

/*
 * Read the printcap database for printer `printer' into the
 * struct printer pointed by `pp'.  Return values are as for
 * cgetent(3): -1 means we could not find what we wanted, -2
 * means a system error occurred (and errno is set), -3 if a
 * reference (`tc=') loop was detected, and 0 means success.
 *
 * Copied from lpr; should add additional capabilities as they
 * are required by the other programs in the suite so that
 * printcap-reading is consistent across the entire family.
 */
int
getprintcap(const char *printer, struct printer *pp)
{
	int status;
	char *XXX;
	char *bp;

	/*
	 * A bug in the declaration of cgetent(3) means that we have
	 * to hide the constness of its third argument.
	 */
	XXX = (char *)printer;
	if ((status = cgetent(&bp, printcapdb, XXX)) < 0)
		return status;
	status = getprintcap_int(bp, pp);
	free(bp);
	return status;
}

/*
 * Map the status values returned by cgetfirst/cgetnext into those
 * used by cgetent, returning truth if there are more records to
 * examine.  This points out what is arguably a bug in the cget*
 * interface (or at least a nasty wart).
 */
static int
firstnextmap(int *status)
{
	switch (*status) {
	case 0:
		return 0;
	case 1:
		*status = 0;
		return 1;
	case 2:
		*status = 1;
		return 1;
	case -1:
		*status = -2;
		return 0;
	case -2:
		*status = -3;
		return 1;
	default:
		return 0;
	}
}

/*
 * Scan through the database of printers using cgetfirst/cgetnext.
 * Return false of error or end-of-database; else true.
 */
int
firstprinter(struct printer *pp, int *error)
{
	int status;
	char *bp;

	init_printer(pp);
	status = cgetfirst(&bp, printcapdb);
	if (firstnextmap(&status) == 0) {
		if (error)
			*error = status;
		return 0;
	}
	if (error)
		*error = status;
	status = getprintcap_int(bp, pp);
	free(bp);
	if (error && status)
		*error = status;
	return 1;
}

int
nextprinter(struct printer *pp, int *error)
{
	int status;
	char *bp;

	free_printer(pp);
	status = cgetnext(&bp, printcapdb);
	if (firstnextmap(&status) == 0) {
		if (error)
			*error = status;
		return 0;
	}
	if (error)
		*error = status;
	status = getprintcap_int(bp, pp);
	free(bp);
	if (error && status)
		*error = status;
	return 1;
}

void
lastprinter(void)
{
	cgetclose();
}

/*
 * This must match the order of declaration of enum filter in lp.h.
 */
static const char *filters[] = {
	"cf", "df", "gf", "if", "nf", "of", "rf", "tf", "vf"
};

static const char *longfilters[] = {
	"filt.cifplot", "filt.dvi", "filt.plot", "filt.input", "filt.ditroff",
	"filt.output", "filt.fortran", "filt.troff", "filt.raster"
};

/*
 * Internal routine for both getprintcap() and nextprinter().
 * Actually parse the printcap entry using cget* functions.
 * Also attempt to figure out the canonical name of the printer
 * and store a malloced copy of it in pp->printer.
 */
static int
getprintcap_int(char *bp, struct printer *pp)
{
	enum lpd_filters filt;
	char *rp_name;
	int error;

	if ((pp->printer = capdb_canonical_name(bp)) == NULL)
		return PCAPERR_OSERR;

#define CHK(x) do {if ((x) == PCAPERR_OSERR) return PCAPERR_OSERR;}while(0)
	CHK(capdb_getaltstr(bp, "af", "acct.file", 0, &pp->acct_file));
	CHK(capdb_getaltnum(bp, "br", "tty.rate", 0, &pp->baud_rate));
	CHK(capdb_getaltnum(bp, "ct", "remote.timeout", DEFTIMEOUT, 
			    &pp->conn_timeout));
	CHK(capdb_getaltnum(bp, "du", "daemon.user", DEFUID, 
			    &pp->daemon_user));
	CHK(capdb_getaltstr(bp, "ff", "job.formfeed", DEFFF, &pp->form_feed));
	CHK(capdb_getaltstr(bp, "lf", "spool.log", _PATH_CONSOLE, 
			    &pp->log_file));
	CHK(capdb_getaltstr(bp, "lo", "spool.lock", DEFLOCK, &pp->lock_file));
	CHK(capdb_getaltstr(bp, "lp", "tty.device", _PATH_DEFDEVLP, &pp->lp));
	CHK(capdb_getaltnum(bp, "mc", "max.copies", DEFMAXCOPIES, 
			    &pp->max_copies));
	CHK(capdb_getaltstr(bp, "ms", "tty.mode", 0, &pp->mode_set));
	CHK(capdb_getaltnum(bp, "mx", "max.blocks", DEFMX, &pp->max_blocks));
	CHK(capdb_getaltnum(bp, "pc", "acct.price", 0, &pp->price100));
	CHK(capdb_getaltnum(bp, "pl", "page.length", DEFLENGTH,
			    &pp->page_length));
	CHK(capdb_getaltnum(bp, "pw", "page.width", DEFWIDTH, 
			    &pp->page_width));
	CHK(capdb_getaltnum(bp, "px", "page.pwidth", 0, &pp->page_pwidth));
	CHK(capdb_getaltnum(bp, "py", "page.plength", 0, &pp->page_plength));
	CHK(capdb_getaltstr(bp, "rg", "daemon.restrictgrp", 0, 
			    &pp->restrict_grp));
	CHK(capdb_getaltstr(bp, "rm", "remote.host", 0, &pp->remote_host));
	CHK(capdb_getaltstr(bp, "rp", "remote.queue", DEFLP, 
			    &pp->remote_queue));
	CHK(capdb_getaltstr(bp, "sd", "spool.dir", _PATH_DEFSPOOL,
			    &pp->spool_dir));
	CHK(capdb_getaltstr(bp, "sr", "stat.recv", 0, &pp->stat_recv));
	CHK(capdb_getaltstr(bp, "ss", "stat.send", 0, &pp->stat_send));
	CHK(capdb_getaltstr(bp, "st", "spool.status", DEFSTAT,
			    &pp->status_file));
	CHK(capdb_getaltstr(bp, "tr", "job.trailer", 0, &pp->trailer));

	pp->resend_copies = capdb_getaltlog(bp, "rc", "remote.resend_copies");
	pp->restricted = capdb_getaltlog(bp, "rs", "daemon.restricted");
	pp->short_banner = capdb_getaltlog(bp, "sb", "banner.short");
	pp->no_copies = capdb_getaltlog(bp, "sc", "job.no_copies");
	pp->no_formfeed = capdb_getaltlog(bp, "sf", "job.no_formfeed");
	pp->no_header = capdb_getaltlog(bp, "sh", "banner.disable");
	pp->header_last = capdb_getaltlog(bp, "hl", "banner.last");
	pp->rw = capdb_getaltlog(bp, "rw", "tty.rw");
	pp->tof = !capdb_getaltlog(bp, "fo", "job.topofform");
	
	/*
	 * Decide if the remote printer name matches the local printer name.
	 * If no name is given then we assume they mean them to match.
	 * If a name is given see if the rp_name is one of the names for
	 * this printer.
	 */
	pp->rp_matches_local = 1;
	CHK((error = capdb_getaltstr(bp, "rp", "remote.queue", 0, &rp_name)));
	if (error != PCAPERR_NOTFOUND && rp_name != NULL) {
		if (cgetmatch(bp,rp_name) != 0)
			pp->rp_matches_local = 0;
		free(rp_name);
	}

	/*
	 * Filters:
	 */
	for (filt = 0; filt < LPF_COUNT; filt++) {
		CHK(capdb_getaltstr(bp, filters[filt], longfilters[filt], 0,
				    &pp->filters[filt]));
	}

	return 0;
}

/*
 * Decode the error codes returned by cgetent() using the names we
 * made up for them from "lp.h".
 * This would have been much better done with Common Error, >sigh<.
 * Perhaps this can be fixed in the next incarnation of cget*.
 */
const char *
pcaperr(int error)
{
	switch(error) {
	case PCAPERR_TCOPEN:
		return "unresolved tc= expansion";
	case PCAPERR_SUCCESS:
		return "no error";
	case PCAPERR_NOTFOUND:
		return "printer not found";
	case PCAPERR_OSERR:
		return strerror(errno);
	case PCAPERR_TCLOOP:
		return "loop detected in tc= expansion";
	default:
		return "unknown printcap error";
	}
}

/*
 * Initialize a `struct printer' to contain values harmless to
 * the other routines in liblpr.
 */
void
init_printer(struct printer *pp)
{
	static struct printer zero;
	*pp = zero;
}

/*
 * Free the dynamically-allocated strings in a `struct printer'.
 * Idempotent.
 */
void
free_printer(struct printer *pp)
{
	enum lpd_filters filt;
#define	cfree(x)	do { if (x) free(x); } while(0)
	cfree(pp->printer);
	cfree(pp->acct_file);
	for (filt = 0; filt < LPF_COUNT; filt++)
		cfree(pp->filters[filt]);
	cfree(pp->form_feed);
	cfree(pp->log_file);
	cfree(pp->lock_file);
	cfree(pp->lp);
	cfree(pp->restrict_grp);
	cfree(pp->remote_host);
	cfree(pp->remote_queue);
	cfree(pp->spool_dir);
	cfree(pp->stat_recv);
	cfree(pp->stat_send);
	cfree(pp->status_file);
	cfree(pp->trailer);
	cfree(pp->mode_set);

	init_printer(pp);
}


/* 
 * The following routines are part of what would be a sensible library 
 * interface to capability databases.  Maybe someday this will become
 * the default.
 */

/*
 * It provides similar functionality to cgetstr(),
 * except that it provides for both a long and a short
 * capability name and allows for a default to be specified.
 */
static int
capdb_getaltstr(char *bp, const char *shrt, const char *lng, 
    const char *dflt, char **result)
{
	int status;

	status = cgetstr(bp, (char *)/*XXX*/lng, result);
	if (status >= 0 || status == PCAPERR_OSERR)
		return status;
	status = cgetstr(bp, (char *)/*XXX*/shrt, result);
	if (status >= 0 || status == PCAPERR_OSERR)
		return status;
	if (dflt) {
		*result = strdup(dflt);
		if (*result == NULL)
			return PCAPERR_OSERR;
		return strlen(*result);
	}
	return PCAPERR_NOTFOUND;
}

/*
 * The same, only for integers.
 */
static int
capdb_getaltnum(char *bp, const char *shrt, const char *lng, long dflt,
    long *result)
{
	int status;

	status = cgetnum(bp, (char *)/*XXX*/lng, result);
	if (status >= 0)
		return status;
	status = cgetnum(bp, (char *)/*XXX*/shrt, result);
	if (status >= 0)
		return status;
	*result = dflt;
	return 0;
}	

/*
 * Likewise for logical values.  There's no need for a default parameter
 * because the default is always false.
 */
static int
capdb_getaltlog(char *bp, const char *shrt, const char *lng)
{
	if (cgetcap(bp, (char *)/*XXX*/lng, ':'))
		return 1;
	if (cgetcap(bp, (char *)/*XXX*/shrt, ':'))
		return 1;
	return 0;
}

/*
 * Also should be a part of a better cget* library.
 * Given a capdb entry, attempt to figure out what its canonical name
 * is, and return a malloced copy of it.  The canonical name is
 * considered to be the first one listed.
 */
static char *
capdb_canonical_name(const char *bp)
{
	char *retval;	
	const char *nameend;

	nameend = strpbrk(bp, "|:");
	if (nameend == NULL)
		nameend = bp + 1;
	if ((retval = malloc(nameend - bp + 1)) != NULL) {
		retval[0] = '\0';
		strncat(retval, bp, nameend - bp);
	}
	return retval;
}


