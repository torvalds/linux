/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 *
 * 	From: @(#)lp.h	8.2 (Berkeley) 4/28/95
 * $FreeBSD$
 */

#include <sys/queue.h>
#include <time.h>
#include <netdb.h>

/*
 * All this information used to be in global static variables shared
 * mysteriously by various parts of the lpr/lpd suite.
 * This structure attempts to centralize all these declarations in the
 * hope that they can later be made more dynamic.
 */
enum	lpd_filters { LPF_CIFPLOT, LPF_DVI, LPF_GRAPH, LPF_INPUT,
		      LPF_DITROFF, LPF_OUTPUT, LPF_FORTRAN, LPF_TROFF,
		      LPF_RASTER, LPF_COUNT };
/* NB: there is a table in common.c giving the mapping from capability names */

struct	printer {
	char	*printer;	/* printer name */
	int	 remote;	/* true if RM points to a remote host */
	int	 rp_matches_local; /* true if rp has same name as us */
	int	 tof;		/* true if we are at top-of-form */
	/* ------------------------------------------------------ */
	char	*acct_file;	/* AF: accounting file */
	long	 baud_rate;	/* BR: baud rate if lp is a tty */
	char	*filters[LPF_COUNT]; /* CF, DF, GF, IF, NF, OF, RF, TF, VF */
	long	 conn_timeout;	/* CT: TCP connection timeout */
	long	 daemon_user;	/* DU: daemon user id -- XXX belongs ???? */
	char	*form_feed;	/* FF: form feed */
	long	 header_last;	/* HL: print header last */
	char	*log_file;	/* LF: log file */
	char	*lock_file;	/* LO: lock file */
	char	*lp;		/* LP: device name or network address */
	long	 max_copies;	/* MC: maximum number of copies allowed */
	long	 max_blocks;	/* MX: maximum number of blocks to copy */
	long	 price100;	/* PC: price per 100 units of output */
	long	 page_length;	/* PL: page length */
	long	 page_width;	/* PW: page width */
	long	 page_pwidth;	/* PX: page width in pixels */
	long	 page_plength;	/* PY: page length in pixels */
	long	 resend_copies;	/* RC: resend copies to remote host */
	char	*restrict_grp;	/* RG: restricted group */
	char	*remote_host;	/* RM: remote machine name */
	char	*remote_queue;	/* RP: remote printer name */
	long	 restricted;	/* RS: restricted to those with local accts */
	long	 rw;		/* RW: open LP for reading and writing */
	long	 short_banner;	/* SB: short banner */
	long	 no_copies;	/* SC: suppress multiple copies */
	char	*spool_dir;	/* SD: spool directory */
	long	 no_formfeed;	/* SF: suppress FF on each print job */
	long	 no_header;	/* SH: suppress header page */
	char	*stat_recv;	/* SR: statistics file, receiving jobs */
	char	*stat_send;	/* SS: statistics file, sending jobs */
	char	*status_file;	/* ST: status file name */
	char	*trailer;	/* TR: trailer string send when Q empties */
	char	*mode_set;	/* MS: mode set, a la stty */

	/* variables used by trstat*() to keep statistics on file transfers */
#define JOBNUM_SIZE   8
	char 	 jobnum[JOBNUM_SIZE];
	long	 jobdfnum;	/* current datafile number within job */
	struct timespec tr_start, tr_done;
#define TIMESTR_SIZE 40		/* holds result from LPD_TIMESTAMP_PATTERN */
	char	 tr_timestr[TIMESTR_SIZE];
#define DIFFTIME_TS(endTS,startTS) \
		((double)(endTS.tv_sec - startTS.tv_sec) \
		+ (endTS.tv_nsec - startTS.tv_nsec) * 1.0e-9)
};

/*
 * Lists of user names and job numbers, for the benefit of the structs
 * defined below.  We use TAILQs so that requests don't get mysteriously
 * reversed in process.
 */
struct	req_user {
	TAILQ_ENTRY(req_user)	ru_link; /* macro glue */
	char	ru_uname[1];	/* name of user */
};
TAILQ_HEAD(req_user_head, req_user);

struct	req_file {
	TAILQ_ENTRY(req_file)	rf_link; /* macro glue */
	char	 rf_type;	/* type (lowercase cf file letter) of file */
	char	*rf_prettyname;	/* user-visible name of file */
	char	 rf_fname[1];	/* name of file */
};
TAILQ_HEAD(req_file_head, req_file);

struct	req_jobid {
	TAILQ_ENTRY(req_jobid)	rj_link; /* macro glue */
	int	rj_job;		/* job number */
};
TAILQ_HEAD(req_jobid_head, req_jobid);

/*
 * Encapsulate all the information relevant to a request in the
 * lpr/lpd protocol.
 */
enum	req_type { REQ_START, REQ_RECVJOB, REQ_LIST, REQ_DELETE };

struct	request {
	enum	 req_type type;	/* what sort of request is this? */
	struct	 printer prtr;	/* which printer is it for? */
	int	 remote;	/* did request arrive over network? */
	char	*logname;	/* login name of requesting user */
	char	*authname;	/* authenticated identity of requesting user */
	char	*prettyname;	/* ``pretty'' name of requesting user */
	int	 privileged;	/* was the request from a privileged user? */
	void	*authinfo;	/* authentication information */
	int	 authentic;	/* was the request securely authenticated? */

	/* Information for queries and deletes... */
	int	 nusers;	/* length of following list... */
	struct	 req_user_head users; /* list of users to query/delete */
	int	 njobids;	/* length of following list... */
	struct	 req_jobid_head jobids;	/* list of jobids to query/delete */
};

/*
 * Global definitions for the line printer system.
 */
extern char	line[BUFSIZ];
extern const char	*progname;	/* program name (lpr, lpq, etc) */

    /*
     * 'local_host' is the name of the machine that lpd (lpr, whatever)
     * is actually running on.
     *
     * 'from_host' will point to the 'host' variable when receiving a job
     * from a user on the same host, or "somewhere else" when receiving a
     * job from a remote host.  If 'from_host != local_host', then 'from_ip'
     * is the character representation of the IP address of from_host (note
     * that string could be an IPv6 address).
     *
     * Also note that when 'from_host' is not pointing at 'local_host', the
     * string it is pointing at may be as long as NI_MAXHOST (which is very
     * likely to be much longer than MAXHOSTNAMELEN).
     */
extern char	 local_host[MAXHOSTNAMELEN];
extern const char	*from_host;	/* client's machine name */
extern const char	*from_ip;	/* client machine's IP address */

extern int	requ[];		/* job number of spool entries */
extern int	requests;	/* # of spool requests */
extern char	*user[];        /* users to process */
extern int	users;		/* # of users in user array */
extern char	*person;	/* name of person doing lprm */
extern u_char	family;		/* address family */

/*
 * Structure used for building a sorted list of control files.
 * The job_processed value can be used by callers of getq(), to keep
 * track of whatever processing they are doing.
 */
struct jobqueue {
	time_t	job_time;		/* last-mod time of cf-file */
	int	job_matched;		/* used by match_jobspec() */
	int	job_processed;		/* set to zero by getq() */
	char	job_cfname[MAXNAMLEN+1];	/* control file name */
};

/* lpr/lpd generates readable timestamps for logfiles, etc.  Have all those
 * timestamps be in the same format wrt strftime().  This is ISO 8601 format,
 * with the addition of an easy-readable day-of-the-week field.  Note that
 * '%T' = '%H:%M:%S', and that '%z' is not available on all platforms.
 */
#define LPD_TIMESTAMP_PATTERN    "%Y-%m-%dT%T%z %a"

/*
 * Codes to indicate which statistic records trstat_write should write.
 */
typedef enum { TR_SENDING, TR_RECVING, TR_PRINTING } tr_sendrecv;

/*
 * Error codes for our mini printcap library.
 */
#define	PCAPERR_TCLOOP		(-3)
#define	PCAPERR_OSERR		(-2)
#define	PCAPERR_NOTFOUND	(-1)
#define	PCAPERR_SUCCESS		0
#define	PCAPERR_TCOPEN		1

/*
 * File modes for the various status files maintained by lpd.
 */
#define	LOCK_FILE_MODE	(S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH)
#define	LFM_PRINT_DIS	(S_IXUSR)
#define	LFM_QUEUE_DIS	(S_IXGRP)
#define	LFM_RESET_QUE	(S_IXOTH)

#define	STAT_FILE_MODE	(S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH)
#define	LOG_FILE_MODE	(S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH)
#define	TEMP_FILE_MODE	(S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH)

/*
 * Bit-flags for set_qstate() actions, followed by the return values.
 */
#define SQS_DISABLEQ	0x01	/* Disable the queuing of new jobs */
#define SQS_STOPP	0x02	/* Stop the printing of jobs */
#define SQS_ENABLEQ	0x10	/* Enable the queuing of new jobs */
#define SQS_STARTP	0x20	/* Start the printing of jobs */
#define SQS_QCHANGED	0x80	/* The queue has changed (new jobs, etc) */

#define SQS_PARMERR	-9	/* Invalid parameters from caller */
#define SQS_CREFAIL	-3	/* File did not exist, and create failed */
#define SQS_CHGFAIL	-2	/* File exists, but unable to change state */
#define SQS_STATFAIL	-1	/* Unable to stat() the lock file */
#define SQS_CHGOK	1	/* File existed, and the state was changed */
#define SQS_CREOK	2	/* File did not exist, but was created OK */
#define SQS_SKIPCREOK	3	/* File did not exist, and there was */
				/* no need to create it */

/*
 * Command codes used in the protocol.
 */
#define	CMD_CHECK_QUE	'\1'
#define	CMD_TAKE_THIS	'\2'
#define	CMD_SHOWQ_SHORT	'\3'
#define	CMD_SHOWQ_LONG	'\4'
#define	CMD_RMJOB	'\5'

/*
 * seteuid() macros.
*/

extern uid_t	uid, euid;

#define PRIV_START { \
    if (seteuid(euid) != 0) err(1, "seteuid failed"); \
}
#define PRIV_END { \
    if (seteuid(uid) != 0) err(1, "seteuid failed"); \
}


#include "lp.cdefs.h"		/* A cross-platform version of <sys/cdefs.h> */

__BEGIN_DECLS
struct	 dirent;

void	 blankfill(int _tocol);
int	 calc_jobnum(const char *_cfname, const char **_hostpp);
char	*checkremote(struct printer *_pp);
int	 chk(char *_file);
void	 closeallfds(int _start);
void	 delay(int _millisec);
void	 displayq(struct printer *_pp, int _format);
void	 dump(const char *_nfile, const char *_datafile, int _copies);
void	 fatal(const struct printer *_pp, const char *_msg, ...)
	    __printflike(2, 3);
int	 firstprinter(struct printer *_pp, int *_error);
void	 free_printer(struct printer *_pp);
void	 free_request(struct request *_rp);
int	 get_line(FILE *_cfp);
int	 getport(const struct printer *_pp, const char *_rhost, int _rport);
int	 getprintcap(const char *_printer, struct printer *_pp);
int	 getq(const struct printer *_pp, struct jobqueue *(*_namelist[]));
void	 header(void);
void	 inform(const struct printer *_pp, char *_cf);
void	 init_printer(struct printer *_pp);
void	 init_request(struct request *_rp);
int	 inlist(char *_uname, char *_cfile);
int	 iscf(const struct dirent *_d);
void	 ldump(const char *_nfile, const char *_datafile, int _copies);
void	 lastprinter(void);
int	 lockchk(struct printer *_pp, char *_slockf);
char	*lock_file_name(const struct printer *_pp, char *_buf, size_t _len);
void	 lpd_gettime(struct timespec *_tsp, char *_strp, size_t _strsize);
int	 nextprinter(struct printer *_pp, int *_error);
const
char	*pcaperr(int _error);
void	 prank(int _n);
void	 process(const struct printer *_pp, char *_file);
void	 rmjob(const char *_printer);
void	 rmremote(const struct printer *_pp);
void	 setprintcap(char *_newfile);
int	 set_qstate(int _action, const char *_lfname);
void	 show(const char *_nfile, const char *_datafile, int _copies);
int	 startdaemon(const struct printer *_pp);
char	*status_file_name(const struct printer *_pp, char *_buf,
	    size_t _len);
void	 trstat_init(struct printer *_pp, const char *_fname, int _filenum);
void	 trstat_write(struct printer *_pp, tr_sendrecv _sendrecv,
	    size_t _bytecnt, const char *_userid, const char *_otherhost,
	    const char *_orighost);
ssize_t	 writel(int _strm, ...);
__END_DECLS
