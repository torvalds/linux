/*	$OpenBSD: lp.h,v 1.1.1.1 2018/04/27 16:14:36 eric Exp $	*/

/*
 * Copyright (c) 2017 Eric Faurot <eric@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/stat.h>

#include <paths.h>
#include <stdio.h>

#define _PATH_PRINTCAP	"/etc/printcap"
#define _PATH_HOSTSLPD	"/etc/hosts.lpd"

#define DEFAULT_FF	"\f"
#define DEFAULT_LF	_PATH_CONSOLE
#define DEFAULT_LO	"lock"
#define DEFAULT_LP	"/dev/lp"
#define DEFAULT_PC	200
#define DEFAULT_PL	66
#define DEFAULT_PW	132
#define DEFAULT_RP	"lp"
#define DEFAULT_SD	"/var/spool/output"
#define DEFAULT_ST	"status"

#define LP_MAXALIASES	32
#define LP_MAXUSERS	50
#define LP_MAXREQUESTS	50

#define LPR_ACK		0
#define LPR_NACK	1	/* only for sending */

#define PRN_LOCAL	0	/* local printer */
#define PRN_NET		1	/* printer listening directly on a port */
#define PRN_LPR		2	/* some lpr daemon */

#define LPQ_PRINTER_DOWN 	0x1
#define LPQ_QUEUE_OFF		0x2
#define LPQ_QUEUE_UPDATED	0x4

#define LP_FF(p) (((p)->lp_ff) ? ((p)->lp_ff) : DEFAULT_FF)
#define LP_LF(p) (((p)->lp_lf) ? ((p)->lp_lf) : DEFAULT_LF)
#define LP_LO(p) (((p)->lp_lo) ? ((p)->lp_lo) : DEFAULT_LO)
#define LP_LP(p) (((p)->lp_lp) ? ((p)->lp_lp) : DEFAULT_LP)
#define LP_RP(p) (((p)->lp_rp) ? ((p)->lp_rp) : DEFAULT_RP)
#define LP_SD(p) (((p)->lp_sd) ? ((p)->lp_sd) : DEFAULT_SD)
#define LP_ST(p) (((p)->lp_st) ? ((p)->lp_st) : DEFAULT_ST)

#define LP_JOBNUM(cf) (100*((cf)[3]-'0') + 10*((cf)[4]-'0') + ((cf)[5]-'0'))
#define LP_JOBHOST(cf) ((cf) + 6)

struct lp_printer {
	int	 lp_type;
	char	*lp_name;
	char	*lp_aliases[LP_MAXALIASES];
	int	 lp_aliascount;

	char	*lp_host; /* if remote */
	char	*lp_port;

	FILE	*lp_lock; /* currently held lock file */

	char	*lp_af;	/* name of accounting file */
	long	 lp_br;	/* if lp is a tty, set baud rate (ioctl(2) call) */
	char	*lp_cf;	/* cifplot data filter */
	char	*lp_df;	/* tex data filter (DVI format) */
	char	*lp_ff;	/* string to send for a form feed */
	short	 lp_fo;	/* print a form feed when device is opened */
	char	*lp_gf;	/* graph data filter (plot(3) format) */
	short	 lp_hl;	/* print the burst header page last */
	short	 lp_ic;	/* supports non-standard ioctl to indent printout */
	char	*lp_if;	/* name of text filter which does accounting */
	char	*lp_lf;	/* error logging file name */
	char	*lp_lo;	/* name of lock file */
	char	*lp_lp;	/* local printer device, or port@host for remote */
	long	 lp_mc;	/* maximum number of copies allowed; 0=unlimited */
	char	*lp_ms;	/* if lp is a tty, a comma-separated, stty(1)-like list
			   describing the tty modes */
	long	 lp_mx;	/* max file size (in BUFSIZ blocks); 0=unlimited */
	char	*lp_nd;	/* next directory for queues list (unimplemented) */
	char	*lp_nf;	/* ditroff data filter (device independent troff) */
	char	*lp_of;	/* name of output filtering program */
	long	 lp_pc;	/* price per foot or page in hundredths of cents */
	long	 lp_pl;	/* page length (in lines) */
	long	 lp_pw;	/* page width (in characters) */
	long	 lp_px;	/* page width in pixels (horizontal) */
	long	 lp_py;	/* page length in pixels (vertical) */
	char	*lp_rf;	/* filter for printing FORTRAN style text files */
	char	*lp_rg;	/* restricted group-only group members can access */
	char	*lp_rm;	/* machine name for remote printer */
	char	*lp_rp;	/* remote printer name argument */
	short	 lp_rs;	/* remote users must have a local account */
	short	 lp_rw;	/* open printer device for reading and writing */
	short	 lp_sb;	/* short banner (one line only) */
	short	 lp_sc;	/* suppress multiple copies */
	char	*lp_sd;	/* spool directory */
	short	 lp_sf;	/* suppress form feeds */
	short	 lp_sh;	/* suppress printing of burst page header */
	char	*lp_st;	/* status file name */
	char	*lp_tf;	/* troff data filter (cat phototypesetter) */
	char	*lp_tr;	/* trailer string to print when queue empties */
	char	*lp_vf;	/* raster image filter */
};

struct lp_queue {
	int	  count;
	char	**cfname;
};

struct lp_jobfilter {
	const char	*hostfrom;
	int		 nuser;
	const char	*users[LP_MAXUSERS];
	int		 njob;
	int		 jobs[LP_MAXREQUESTS];
};

extern char *lpd_hostname;

/* lp.c */
int   lp_getprinter(struct lp_printer *, const char *);
int   lp_scanprinters(struct lp_printer *);
void  lp_clearprinter(struct lp_printer *);
int   lp_readqueue(struct lp_printer *, struct lp_queue *);
void  lp_clearqueue(struct lp_queue *);
FILE* lp_fopen(struct lp_printer *, const char *);
int   lp_stat(struct lp_printer *, const char *, struct stat *);
int   lp_unlink(struct lp_printer *, const char *);
int   lp_lock(struct lp_printer *);
void  lp_unlock(struct lp_printer *);
int   lp_getqueuestate(struct lp_printer *, int, int *);
int   lp_getcurrtask(struct lp_printer *, pid_t *, char *, size_t);
void  lp_setcurrtask(struct lp_printer *, const char *);
int   lp_getstatus(struct lp_printer *, char *, size_t);
void  lp_setstatus(struct lp_printer *, const char *, ...)
	__attribute__((__format__ (printf, 2, 3)));
int   lp_validfilename(const char *, int);
int   lp_create(struct lp_printer *, int, size_t, const char *);
int   lp_commit(struct lp_printer *, const char *);

/* lp_banner.c */
int   lp_banner(int, char *, int);

/* lp_displayq.c */
void  lp_displayq(int, struct lp_printer *, int, struct lp_jobfilter *);

/* lp_rmjob */
int   lp_rmjob(int, struct lp_printer *, const char *, struct lp_jobfilter *);

/* lp_stty.c */
void  lp_stty(struct lp_printer *, int);
