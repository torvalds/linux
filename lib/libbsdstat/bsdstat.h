/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2007 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

#ifndef	_BSDSTAT_H_
#define	_BSDSTAT_H_
/*
 * Base class for managing+displaying periodically collected statistics.
 */

/*
 * Statistic definition/description.  The are defined
 * for stats that correspond 1-1 w/ a collected stat
 * and for stats that are calculated indirectly.
 */
struct fmt {
	int	width;			/* printed field width */
	const char* name;		/* stat field name referenced by user */
	const char* label;		/* printed header label */
	const char* desc;		/* verbose description */
};

#define	BSDSTAT_DECL_METHODS(_p) \
	/* set the format of the statistics to display */	\
	void (*setfmt)(_p, const char *);			\
	/* collect+store ``current statistics'' */		\
	void (*collect_cur)(_p);				\
	/* collect+store ``total statistics'' */		\
	void (*collect_tot)(_p);				\
	/* update ``total statistics'' if necessary from current */ \
	void (*update_tot)(_p);					\
	/* format a statistic from the current stats */		\
	int (*get_curstat)(_p, int, char [], size_t);		\
	/* format a statistic from the total stats */		\
	int (*get_totstat)(_p, int, char [], size_t);		\
	/* print field headers terminated by a \n */		\
	void (*print_header)(_p, FILE *);			\
	/* print current statistics terminated by a \n */	\
	void (*print_current)(_p, FILE *);			\
	/* print total statistics terminated by a \n */		\
	void (*print_total)(_p, FILE *);			\
	/* print total statistics in a verbose (1 stat/line) format */ \
	void (*print_verbose)(_p, FILE *);			\
	/* print available statistics */			\
	void (*print_fields)(_p, FILE *)

/*
 * Statistics base class.  This class is not usable; only
 * classes derived from it are useful.
 */
struct bsdstat {
	const char *name;		/* statistics name, e.g. wlanstats */
	const struct fmt *stats;	/* statistics in class */
	int nstats;			/* number of stats */
#define	FMTS_IS_STAT	0x80	/* the following two bytes are the stat id */
	unsigned char fmts[4096];	/* private: compiled stats to display */

	BSDSTAT_DECL_METHODS(struct bsdstat *);
};

extern	void bsdstat_init(struct bsdstat *, const char *name,
		    const struct fmt *stats, int nstats);

#define	BSDSTAT_DEFINE_BOUNCE(_t) \
static void _t##_setfmt(struct _t *wf, const char *fmt0)	\
	{ wf->base.setfmt(&wf->base, fmt0); }			\
static void _t##_collect_cur(struct _t *wf)			\
	{ wf->base.collect_cur(&wf->base); }			\
static void _t##_collect_tot(struct _t *wf)			\
	{ wf->base.collect_tot(&wf->base); }			\
static void _t##_update_tot(struct _t *wf)			\
	{ wf->base.update_tot(&wf->base); }			\
static int _t##_get_curstat(struct _t *wf, int s, char b[], size_t bs) \
	{ return wf->base.get_curstat(&wf->base, s, b, bs); }	\
static int _t##_get_totstat(struct _t *wf, int s, char b[], size_t bs) \
	{ return wf->base.get_totstat(&wf->base, s, b, bs); }	\
static void _t##_print_header(struct _t *wf, FILE *fd)		\
	{ wf->base.print_header(&wf->base, fd); }		\
static void _t##_print_current(struct _t *wf, FILE *fd)		\
	{ wf->base.print_current(&wf->base, fd); }		\
static void _t##_print_total(struct _t *wf, FILE *fd)		\
	{ wf->base.print_total(&wf->base, fd); }		\
static void _t##_print_verbose(struct _t *wf, FILE *fd)		\
	{ wf->base.print_verbose(&wf->base, fd); }		\
static void _t##_print_fields(struct _t *wf, FILE *fd)		\
	{ wf->base.print_fields(&wf->base, fd); }

#define	BSDSTAT_BOUNCE(_p, _t) do {				\
	_p->base.setfmt = _t##_setfmt;				\
	_p->base.collect_cur = _t##_collect_cur;		\
	_p->base.collect_tot = _t##_collect_tot;		\
	_p->base.update_tot = _t##_update_tot;			\
	_p->base.get_curstat = _t##_get_curstat;		\
	_p->base.get_totstat = _t##_get_totstat;		\
	_p->base.print_header = _t##_print_header;		\
	_p->base.print_current = _t##_print_current;		\
	_p->base.print_total = _t##_print_total;		\
	_p->base.print_verbose = _t##_print_verbose;		\
	_p->base.print_fields = _t##_print_fields;		\
} while (0)
#endif /* _BSDSTAT_H_ */
