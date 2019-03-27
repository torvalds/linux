/*-
 * ------+---------+---------+-------- + --------+---------+---------+---------*
 * This file includes significant modifications done by:
 * Copyright (c) 2003, 2004  - Garance Alistair Drosehn <gad@FreeBSD.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
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
 * ------+---------+---------+-------- + --------+---------+---------+---------*
 */

/*
 * This file contains changes from the Open Software Foundation.
 */

/*
 * Copyright 1988, 1989 by the Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the names of M.I.T. and the M.I.T. S.I.P.B. not be
 * used in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission. M.I.T. and the M.I.T.
 * S.I.P.B. make no representations about the suitability of this software
 * for any purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 */

/*
 * newsyslog - roll over selected logs at the appropriate time, keeping the a
 * specified number of backup files around.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define	OSF

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <glob.h>
#include <grp.h>
#include <paths.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "pathnames.h"
#include "extern.h"

/*
 * Compression types
 */
#define	COMPRESS_TYPES  5	/* Number of supported compression types */

#define	COMPRESS_NONE	0
#define	COMPRESS_GZIP	1
#define	COMPRESS_BZIP2	2
#define	COMPRESS_XZ	3
#define COMPRESS_ZSTD	4

/*
 * Bit-values for the 'flags' parsed from a config-file entry.
 */
#define	CE_BINARY	0x0008	/* Logfile is in binary, do not add status */
				/*    messages to logfile(s) when rotating. */
#define	CE_NOSIGNAL	0x0010	/* There is no process to signal when */
				/*    trimming this file. */
#define	CE_TRIMAT	0x0020	/* trim file at a specific time. */
#define	CE_GLOB		0x0040	/* name of the log is file name pattern. */
#define	CE_SIGNALGROUP	0x0080	/* Signal a process-group instead of a single */
				/*    process when trimming this file. */
#define	CE_CREATE	0x0100	/* Create the log file if it does not exist. */
#define	CE_NODUMP	0x0200	/* Set 'nodump' on newly created log file. */
#define	CE_PID2CMD	0x0400	/* Replace PID file with a shell command.*/
#define	CE_PLAIN0	0x0800	/* Do not compress zero'th history file */
#define	CE_RFC5424	0x1000	/* Use RFC5424 format rotation message */

#define	MIN_PID         5	/* Don't touch pids lower than this */
#define	MAX_PID		99999	/* was lower, see /usr/include/sys/proc.h */

#define	kbytes(size)  (((size) + 1023) >> 10)

#define	DEFAULT_MARKER	"<default>"
#define	DEBUG_MARKER	"<debug>"
#define	INCLUDE_MARKER	"<include>"
#define	DEFAULT_TIMEFNAME_FMT	"%Y%m%dT%H%M%S"

#define	MAX_OLDLOGS 65536	/* Default maximum number of old logfiles */

struct compress_types {
	const char *flag;	/* Flag in configuration file */
	const char *suffix;	/* Compression suffix */
	const char *path;	/* Path to compression program */
	const char **flags;	/* Compression program flags */
	int nflags;		/* Program flags count */
};

static const char *gzip_flags[] = { "-f" };
#define bzip2_flags gzip_flags
#define xz_flags gzip_flags
static const char *zstd_flags[] = { "-q", "--rm" };

static const struct compress_types compress_type[COMPRESS_TYPES] = {
	{ "", "", "", NULL, 0 },
	{ "Z", ".gz", _PATH_GZIP, gzip_flags, nitems(gzip_flags) },
	{ "J", ".bz2", _PATH_BZIP2, bzip2_flags, nitems(bzip2_flags) },
	{ "X", ".xz", _PATH_XZ, xz_flags, nitems(xz_flags) },
	{ "Y", ".zst", _PATH_ZSTD, zstd_flags, nitems(zstd_flags) }
};

struct conf_entry {
	STAILQ_ENTRY(conf_entry) cf_nextp;
	char *log;		/* Name of the log */
	char *pid_cmd_file;		/* PID or command file */
	char *r_reason;		/* The reason this file is being rotated */
	int firstcreate;	/* Creating log for the first time (-C). */
	int rotate;		/* Non-zero if this file should be rotated */
	int fsize;		/* size found for the log file */
	uid_t uid;		/* Owner of log */
	gid_t gid;		/* Group of log */
	int numlogs;		/* Number of logs to keep */
	int trsize;		/* Size cutoff to trigger trimming the log */
	int hours;		/* Hours between log trimming */
	struct ptime_data *trim_at;	/* Specific time to do trimming */
	unsigned int permissions;	/* File permissions on the log */
	int flags;		/* CE_BINARY */
	int compress;		/* Compression */
	int sig;		/* Signal to send */
	int def_cfg;		/* Using the <default> rule for this file */
};

struct sigwork_entry {
	SLIST_ENTRY(sigwork_entry) sw_nextp;
	int	 sw_signum;		/* the signal to send */
	int	 sw_pidok;		/* true if pid value is valid */
	pid_t	 sw_pid;		/* the process id from the PID file */
	const char *sw_pidtype;		/* "daemon" or "process group" */
	int	 sw_runcmd;		/* run command or send PID to signal */
	char	 sw_fname[1];		/* file the PID was read from or shell cmd */
};

struct zipwork_entry {
	SLIST_ENTRY(zipwork_entry) zw_nextp;
	const struct conf_entry *zw_conf;	/* for chown/perm/flag info */
	const struct sigwork_entry *zw_swork;	/* to know success of signal */
	int	 zw_fsize;		/* size of the file to compress */
	char	 zw_fname[1];		/* the file to compress */
};

struct include_entry {
	STAILQ_ENTRY(include_entry) inc_nextp;
	const char *file;	/* Name of file to process */
};

struct oldlog_entry {
	char *fname;		/* Filename of the log file */
	time_t t;		/* Parsed timestamp of the logfile */
};

typedef enum {
	FREE_ENT, KEEP_ENT
}	fk_entry;

STAILQ_HEAD(cflist, conf_entry);
static SLIST_HEAD(swlisthead, sigwork_entry) swhead =
    SLIST_HEAD_INITIALIZER(swhead);
static SLIST_HEAD(zwlisthead, zipwork_entry) zwhead =
    SLIST_HEAD_INITIALIZER(zwhead);
STAILQ_HEAD(ilist, include_entry);

int dbg_at_times;		/* -D Show details of 'trim_at' code */

static int archtodir = 0;	/* Archive old logfiles to other directory */
static int createlogs;		/* Create (non-GLOB) logfiles which do not */
				/*    already exist.  1=='for entries with */
				/*    C flag', 2=='for all entries'. */
int verbose = 0;		/* Print out what's going on */
static int needroot = 1;	/* Root privs are necessary */
int noaction = 0;		/* Don't do anything, just show it */
static int norotate = 0;	/* Don't rotate */
static int nosignal;		/* Do not send any signals */
static int enforcepid = 0;	/* If PID file does not exist or empty, do nothing */
static int force = 0;		/* Force the trim no matter what */
static int rotatereq = 0;	/* -R = Always rotate the file(s) as given */
				/*    on the command (this also requires   */
				/*    that a list of files *are* given on  */
				/*    the run command). */
static char *requestor;		/* The name given on a -R request */
static char *timefnamefmt = NULL;/* Use time based filenames instead of .0 */
static char *archdirname;	/* Directory path to old logfiles archive */
static char *destdir = NULL;	/* Directory to treat at root for logs */
static const char *conf;	/* Configuration file to use */

struct ptime_data *dbg_timenow;	/* A "timenow" value set via -D option */
static struct ptime_data *timenow; /* The time to use for checking at-fields */

#define	DAYTIME_LEN	16
static char daytime[DAYTIME_LEN];/* The current time in human readable form,
				  * used for rotation-tracking messages. */

/* Another buffer to hold the current time in RFC5424 format. Fractional
 * seconds are allowed by the RFC, but are not included in the
 * rotation-tracking messages written by newsyslog and so are not accounted for
 * in the length below.
 */
#define	DAYTIME_RFC5424_LEN	sizeof("YYYY-MM-DDTHH:MM:SS+00:00")
static char daytime_rfc5424[DAYTIME_RFC5424_LEN];

static char hostname[MAXHOSTNAMELEN]; /* hostname */
static size_t hostname_shortlen;

static const char *path_syslogpid = _PATH_SYSLOGPID;

static struct cflist *get_worklist(char **files);
static void parse_file(FILE *cf, struct cflist *work_p, struct cflist *glob_p,
		    struct conf_entry **defconf, struct ilist *inclist);
static void add_to_queue(const char *fname, struct ilist *inclist);
static char *sob(char *p);
static char *son(char *p);
static int isnumberstr(const char *);
static int isglobstr(const char *);
static char *missing_field(char *p, char *errline);
static void	 change_attrs(const char *, const struct conf_entry *);
static const char *get_logfile_suffix(const char *logfile);
static fk_entry	 do_entry(struct conf_entry *);
static fk_entry	 do_rotate(const struct conf_entry *);
static void	 do_sigwork(struct sigwork_entry *);
static void	 do_zipwork(struct zipwork_entry *);
static struct sigwork_entry *
		 save_sigwork(const struct conf_entry *);
static struct zipwork_entry *
		 save_zipwork(const struct conf_entry *, const struct
		    sigwork_entry *, int, const char *);
static void	 set_swpid(struct sigwork_entry *, const struct conf_entry *);
static int	 sizefile(const char *);
static void expand_globs(struct cflist *work_p, struct cflist *glob_p);
static void free_clist(struct cflist *list);
static void free_entry(struct conf_entry *ent);
static struct conf_entry *init_entry(const char *fname,
		struct conf_entry *src_entry);
static void parse_args(int argc, char **argv);
static int parse_doption(const char *doption);
static void usage(void);
static int log_trim(const char *logname, const struct conf_entry *log_ent);
static int age_old_log(const char *file);
static void savelog(char *from, char *to);
static void createdir(const struct conf_entry *ent, char *dirpart);
static void createlog(const struct conf_entry *ent);
static int parse_signal(const char *str);

/*
 * All the following take a parameter of 'int', but expect values in the
 * range of unsigned char.  Define wrappers which take values of type 'char',
 * whether signed or unsigned, and ensure they end up in the right range.
 */
#define	isdigitch(Anychar) isdigit((u_char)(Anychar))
#define	isprintch(Anychar) isprint((u_char)(Anychar))
#define	isspacech(Anychar) isspace((u_char)(Anychar))
#define	tolowerch(Anychar) tolower((u_char)(Anychar))

int
main(int argc, char **argv)
{
	struct cflist *worklist;
	struct conf_entry *p;
	struct sigwork_entry *stmp;
	struct zipwork_entry *ztmp;

	SLIST_INIT(&swhead);
	SLIST_INIT(&zwhead);

	parse_args(argc, argv);
	argc -= optind;
	argv += optind;

	if (needroot && getuid() && geteuid())
		errx(1, "must have root privs");
	worklist = get_worklist(argv);

	/*
	 * Rotate all the files which need to be rotated.  Note that
	 * some users have *hundreds* of entries in newsyslog.conf!
	 */
	while (!STAILQ_EMPTY(worklist)) {
		p = STAILQ_FIRST(worklist);
		STAILQ_REMOVE_HEAD(worklist, cf_nextp);
		if (do_entry(p) == FREE_ENT)
			free_entry(p);
	}

	/*
	 * Send signals to any processes which need a signal to tell
	 * them to close and re-open the log file(s) we have rotated.
	 * Note that zipwork_entries include pointers to these
	 * sigwork_entry's, so we can not free the entries here.
	 */
	if (!SLIST_EMPTY(&swhead)) {
		if (noaction || verbose)
			printf("Signal all daemon process(es)...\n");
		SLIST_FOREACH(stmp, &swhead, sw_nextp)
			do_sigwork(stmp);
		if (!(rotatereq && nosignal)) {
			if (noaction)
				printf("\tsleep 10\n");
			else {
				if (verbose)
					printf("Pause 10 seconds to allow "
					    "daemon(s) to close log file(s)\n");
				sleep(10);
			}
		}
	}
	/*
	 * Compress all files that we're expected to compress, now
	 * that all processes should have closed the files which
	 * have been rotated.
	 */
	if (!SLIST_EMPTY(&zwhead)) {
		if (noaction || verbose)
			printf("Compress all rotated log file(s)...\n");
		while (!SLIST_EMPTY(&zwhead)) {
			ztmp = SLIST_FIRST(&zwhead);
			do_zipwork(ztmp);
			SLIST_REMOVE_HEAD(&zwhead, zw_nextp);
			free(ztmp);
		}
	}
	/* Now free all the sigwork entries. */
	while (!SLIST_EMPTY(&swhead)) {
		stmp = SLIST_FIRST(&swhead);
		SLIST_REMOVE_HEAD(&swhead, sw_nextp);
		free(stmp);
	}

	while (wait(NULL) > 0 || errno == EINTR)
		;
	return (0);
}

static struct conf_entry *
init_entry(const char *fname, struct conf_entry *src_entry)
{
	struct conf_entry *tempwork;

	if (verbose > 4)
		printf("\t--> [creating entry for %s]\n", fname);

	tempwork = malloc(sizeof(struct conf_entry));
	if (tempwork == NULL)
		err(1, "malloc of conf_entry for %s", fname);

	if (destdir == NULL || fname[0] != '/')
		tempwork->log = strdup(fname);
	else
		asprintf(&tempwork->log, "%s%s", destdir, fname);
	if (tempwork->log == NULL)
		err(1, "strdup for %s", fname);

	if (src_entry != NULL) {
		tempwork->pid_cmd_file = NULL;
		if (src_entry->pid_cmd_file)
			tempwork->pid_cmd_file = strdup(src_entry->pid_cmd_file);
		tempwork->r_reason = NULL;
		tempwork->firstcreate = 0;
		tempwork->rotate = 0;
		tempwork->fsize = -1;
		tempwork->uid = src_entry->uid;
		tempwork->gid = src_entry->gid;
		tempwork->numlogs = src_entry->numlogs;
		tempwork->trsize = src_entry->trsize;
		tempwork->hours = src_entry->hours;
		tempwork->trim_at = NULL;
		if (src_entry->trim_at != NULL)
			tempwork->trim_at = ptime_init(src_entry->trim_at);
		tempwork->permissions = src_entry->permissions;
		tempwork->flags = src_entry->flags;
		tempwork->compress = src_entry->compress;
		tempwork->sig = src_entry->sig;
		tempwork->def_cfg = src_entry->def_cfg;
	} else {
		/* Initialize as a "do-nothing" entry */
		tempwork->pid_cmd_file = NULL;
		tempwork->r_reason = NULL;
		tempwork->firstcreate = 0;
		tempwork->rotate = 0;
		tempwork->fsize = -1;
		tempwork->uid = (uid_t)-1;
		tempwork->gid = (gid_t)-1;
		tempwork->numlogs = 1;
		tempwork->trsize = -1;
		tempwork->hours = -1;
		tempwork->trim_at = NULL;
		tempwork->permissions = 0;
		tempwork->flags = 0;
		tempwork->compress = COMPRESS_NONE;
		tempwork->sig = SIGHUP;
		tempwork->def_cfg = 0;
	}

	return (tempwork);
}

static void
free_entry(struct conf_entry *ent)
{

	if (ent == NULL)
		return;

	if (ent->log != NULL) {
		if (verbose > 4)
			printf("\t--> [freeing entry for %s]\n", ent->log);
		free(ent->log);
		ent->log = NULL;
	}

	if (ent->pid_cmd_file != NULL) {
		free(ent->pid_cmd_file);
		ent->pid_cmd_file = NULL;
	}

	if (ent->r_reason != NULL) {
		free(ent->r_reason);
		ent->r_reason = NULL;
	}

	if (ent->trim_at != NULL) {
		ptime_free(ent->trim_at);
		ent->trim_at = NULL;
	}

	free(ent);
}

static void
free_clist(struct cflist *list)
{
	struct conf_entry *ent;

	while (!STAILQ_EMPTY(list)) {
		ent = STAILQ_FIRST(list);
		STAILQ_REMOVE_HEAD(list, cf_nextp);
		free_entry(ent);
	}

	free(list);
	list = NULL;
}

static fk_entry
do_entry(struct conf_entry * ent)
{
#define	REASON_MAX	80
	int modtime;
	fk_entry free_or_keep;
	double diffsecs;
	char temp_reason[REASON_MAX];
	int oversized;

	free_or_keep = FREE_ENT;
	if (verbose)
		printf("%s <%d%s>: ", ent->log, ent->numlogs,
		    compress_type[ent->compress].flag);
	ent->fsize = sizefile(ent->log);
	oversized = ((ent->trsize > 0) && (ent->fsize >= ent->trsize));
	modtime = age_old_log(ent->log);
	ent->rotate = 0;
	ent->firstcreate = 0;
	if (ent->fsize < 0) {
		/*
		 * If either the C flag or the -C option was specified,
		 * and if we won't be creating the file, then have the
		 * verbose message include a hint as to why the file
		 * will not be created.
		 */
		temp_reason[0] = '\0';
		if (createlogs > 1)
			ent->firstcreate = 1;
		else if ((ent->flags & CE_CREATE) && createlogs)
			ent->firstcreate = 1;
		else if (ent->flags & CE_CREATE)
			strlcpy(temp_reason, " (no -C option)", REASON_MAX);
		else if (createlogs)
			strlcpy(temp_reason, " (no C flag)", REASON_MAX);

		if (ent->firstcreate) {
			if (verbose)
				printf("does not exist -> will create.\n");
			createlog(ent);
		} else if (verbose) {
			printf("does not exist, skipped%s.\n", temp_reason);
		}
	} else {
		if (ent->flags & CE_TRIMAT && !force && !rotatereq &&
		    !oversized) {
			diffsecs = ptimeget_diff(timenow, ent->trim_at);
			if (diffsecs < 0.0) {
				/* trim_at is some time in the future. */
				if (verbose) {
					ptime_adjust4dst(ent->trim_at,
					    timenow);
					printf("--> will trim at %s",
					    ptimeget_ctime(ent->trim_at));
				}
				return (free_or_keep);
			} else if (diffsecs >= 3600.0) {
				/*
				 * trim_at is more than an hour in the past,
				 * so find the next valid trim_at time, and
				 * tell the user what that will be.
				 */
				if (verbose && dbg_at_times)
					printf("\n\t--> prev trim at %s\t",
					    ptimeget_ctime(ent->trim_at));
				if (verbose) {
					ptimeset_nxtime(ent->trim_at);
					printf("--> will trim at %s",
					    ptimeget_ctime(ent->trim_at));
				}
				return (free_or_keep);
			} else if (verbose && noaction && dbg_at_times) {
				/*
				 * If we are just debugging at-times, then
				 * a detailed message is helpful.  Also
				 * skip "doing" any commands, since they
				 * would all be turned off by no-action.
				 */
				printf("\n\t--> timematch at %s",
				    ptimeget_ctime(ent->trim_at));
				return (free_or_keep);
			} else if (verbose && ent->hours <= 0) {
				printf("--> time is up\n");
			}
		}
		if (verbose && (ent->trsize > 0))
			printf("size (Kb): %d [%d] ", ent->fsize, ent->trsize);
		if (verbose && (ent->hours > 0))
			printf(" age (hr): %d [%d] ", modtime, ent->hours);

		/*
		 * Figure out if this logfile needs to be rotated.
		 */
		temp_reason[0] = '\0';
		if (rotatereq) {
			ent->rotate = 1;
			snprintf(temp_reason, REASON_MAX, " due to -R from %s",
			    requestor);
		} else if (force) {
			ent->rotate = 1;
			snprintf(temp_reason, REASON_MAX, " due to -F request");
		} else if (oversized) {
			ent->rotate = 1;
			snprintf(temp_reason, REASON_MAX, " due to size>%dK",
			    ent->trsize);
		} else if (ent->hours <= 0 && (ent->flags & CE_TRIMAT)) {
			ent->rotate = 1;
		} else if ((ent->hours > 0) && ((modtime >= ent->hours) ||
		    (modtime < 0))) {
			ent->rotate = 1;
		}

		/*
		 * If the file needs to be rotated, then rotate it.
		 */
		if (ent->rotate && !norotate) {
			if (temp_reason[0] != '\0')
				ent->r_reason = strdup(temp_reason);
			if (verbose)
				printf("--> trimming log....\n");
			if (noaction && !verbose)
				printf("%s <%d%s>: trimming\n", ent->log,
				    ent->numlogs,
				    compress_type[ent->compress].flag);
			free_or_keep = do_rotate(ent);
		} else {
			if (verbose)
				printf("--> skipping\n");
		}
	}
	return (free_or_keep);
#undef REASON_MAX
}

static void
parse_args(int argc, char **argv)
{
	int ch;
	char *p;

	timenow = ptime_init(NULL);
	ptimeset_time(timenow, time(NULL));
	strlcpy(daytime, ptimeget_ctime(timenow) + 4, DAYTIME_LEN);
	ptimeget_ctime_rfc5424(timenow, daytime_rfc5424, DAYTIME_RFC5424_LEN);

	/* Let's get our hostname */
	(void)gethostname(hostname, sizeof(hostname));
	hostname_shortlen = strcspn(hostname, ".");

	/* Parse command line options. */
	while ((ch = getopt(argc, argv, "a:d:f:nrst:vCD:FNPR:S:")) != -1)
		switch (ch) {
		case 'a':
			archtodir++;
			archdirname = optarg;
			break;
		case 'd':
			destdir = optarg;
			break;
		case 'f':
			conf = optarg;
			break;
		case 'n':
			noaction++;
			/* FALLTHROUGH */
		case 'r':
			needroot = 0;
			break;
		case 's':
			nosignal = 1;
			break;
		case 't':
			if (optarg[0] == '\0' ||
			    strcmp(optarg, "DEFAULT") == 0)
				timefnamefmt = strdup(DEFAULT_TIMEFNAME_FMT);
			else
				timefnamefmt = strdup(optarg);
			break;
		case 'v':
			verbose++;
			break;
		case 'C':
			/* Useful for things like rc.diskless... */
			createlogs++;
			break;
		case 'D':
			/*
			 * Set some debugging option.  The specific option
			 * depends on the value of optarg.  These options
			 * may come and go without notice or documentation.
			 */
			if (parse_doption(optarg))
				break;
			usage();
			/* NOTREACHED */
		case 'F':
			force++;
			break;
		case 'N':
			norotate++;
			break;
		case 'P':
			enforcepid++;
			break;
		case 'R':
			rotatereq++;
			requestor = strdup(optarg);
			break;
		case 'S':
			path_syslogpid = optarg;
			break;
		case 'm':	/* Used by OpenBSD for "monitor mode" */
		default:
			usage();
			/* NOTREACHED */
		}

	if (force && norotate) {
		warnx("Only one of -F and -N may be specified.");
		usage();
		/* NOTREACHED */
	}

	if (rotatereq) {
		if (optind == argc) {
			warnx("At least one filename must be given when -R is specified.");
			usage();
			/* NOTREACHED */
		}
		/* Make sure "requestor" value is safe for a syslog message. */
		for (p = requestor; *p != '\0'; p++) {
			if (!isprintch(*p) && (*p != '\t'))
				*p = '.';
		}
	}

	if (dbg_timenow) {
		/*
		 * Note that the 'daytime' variable is not changed.
		 * That is only used in messages that track when a
		 * logfile is rotated, and if a file *is* rotated,
		 * then it will still rotated at the "real now" time.
		 */
		ptime_free(timenow);
		timenow = dbg_timenow;
		fprintf(stderr, "Debug: Running as if TimeNow is %s",
		    ptimeget_ctime(dbg_timenow));
	}

}

/*
 * These debugging options are mainly meant for developer use, such
 * as writing regression-tests.  They would not be needed by users
 * during normal operation of newsyslog...
 */
static int
parse_doption(const char *doption)
{
	const char TN[] = "TN=";
	int res;

	if (strncmp(doption, TN, sizeof(TN) - 1) == 0) {
		/*
		 * The "TimeNow" debugging option.  This might be off
		 * by an hour when crossing a timezone change.
		 */
		dbg_timenow = ptime_init(NULL);
		res = ptime_relparse(dbg_timenow, PTM_PARSE_ISO8601,
		    time(NULL), doption + sizeof(TN) - 1);
		if (res == -2) {
			warnx("Non-existent time specified on -D %s", doption);
			return (0);			/* failure */
		} else if (res < 0) {
			warnx("Malformed time given on -D %s", doption);
			return (0);			/* failure */
		}
		return (1);			/* successfully parsed */

	}

	if (strcmp(doption, "ats") == 0) {
		dbg_at_times++;
		return (1);			/* successfully parsed */
	}

	/* XXX - This check could probably be dropped. */
	if ((strcmp(doption, "neworder") == 0) || (strcmp(doption, "oldorder")
	    == 0)) {
		warnx("NOTE: newsyslog always uses 'neworder'.");
		return (1);			/* successfully parsed */
	}

	warnx("Unknown -D (debug) option: '%s'", doption);
	return (0);				/* failure */
}

static void
usage(void)
{

	fprintf(stderr,
	    "usage: newsyslog [-CFNPnrsv] [-a directory] [-d directory] [-f config_file]\n"
	    "                 [-S pidfile] [-t timefmt] [[-R tagname] file ...]\n");
	exit(1);
}

/*
 * Parse a configuration file and return a linked list of all the logs
 * which should be processed.
 */
static struct cflist *
get_worklist(char **files)
{
	FILE *f;
	char **given;
	struct cflist *cmdlist, *filelist, *globlist;
	struct conf_entry *defconf, *dupent, *ent;
	struct ilist inclist;
	struct include_entry *inc;
	int gmatch, fnres;

	defconf = NULL;
	STAILQ_INIT(&inclist);

	filelist = malloc(sizeof(struct cflist));
	if (filelist == NULL)
		err(1, "malloc of filelist");
	STAILQ_INIT(filelist);
	globlist = malloc(sizeof(struct cflist));
	if (globlist == NULL)
		err(1, "malloc of globlist");
	STAILQ_INIT(globlist);

	inc = malloc(sizeof(struct include_entry));
	if (inc == NULL)
		err(1, "malloc of inc");
	inc->file = conf;
	if (inc->file == NULL)
		inc->file = _PATH_CONF;
	STAILQ_INSERT_TAIL(&inclist, inc, inc_nextp);

	STAILQ_FOREACH(inc, &inclist, inc_nextp) {
		if (strcmp(inc->file, "-") != 0)
			f = fopen(inc->file, "r");
		else {
			f = stdin;
			inc->file = "<stdin>";
		}
		if (!f)
			err(1, "%s", inc->file);

		if (verbose)
			printf("Processing %s\n", inc->file);
		parse_file(f, filelist, globlist, &defconf, &inclist);
		(void) fclose(f);
	}

	/*
	 * All config-file information has been read in and turned into
	 * a filelist and a globlist.  If there were no specific files
	 * given on the run command, then the only thing left to do is to
	 * call a routine which finds all files matched by the globlist
	 * and adds them to the filelist.  Then return the worklist.
	 */
	if (*files == NULL) {
		expand_globs(filelist, globlist);
		free_clist(globlist);
		if (defconf != NULL)
			free_entry(defconf);
		return (filelist);
	}

	/*
	 * If newsyslog was given a specific list of files to process,
	 * it may be that some of those files were not listed in any
	 * config file.  Those unlisted files should get the default
	 * rotation action.  First, create the default-rotation action
	 * if none was found in a system config file.
	 */
	if (defconf == NULL) {
		defconf = init_entry(DEFAULT_MARKER, NULL);
		defconf->numlogs = 3;
		defconf->trsize = 50;
		defconf->permissions = S_IRUSR|S_IWUSR;
	}

	/*
	 * If newsyslog was run with a list of specific filenames,
	 * then create a new worklist which has only those files in
	 * it, picking up the rotation-rules for those files from
	 * the original filelist.
	 *
	 * XXX - Note that this will copy multiple rules for a single
	 *	logfile, if multiple entries are an exact match for
	 *	that file.  That matches the historic behavior, but do
	 *	we want to continue to allow it?  If so, it should
	 *	probably be handled more intelligently.
	 */
	cmdlist = malloc(sizeof(struct cflist));
	if (cmdlist == NULL)
		err(1, "malloc of cmdlist");
	STAILQ_INIT(cmdlist);

	for (given = files; *given; ++given) {
		/*
		 * First try to find exact-matches for this given file.
		 */
		gmatch = 0;
		STAILQ_FOREACH(ent, filelist, cf_nextp) {
			if (strcmp(ent->log, *given) == 0) {
				gmatch++;
				dupent = init_entry(*given, ent);
				STAILQ_INSERT_TAIL(cmdlist, dupent, cf_nextp);
			}
		}
		if (gmatch) {
			if (verbose > 2)
				printf("\t+ Matched entry %s\n", *given);
			continue;
		}

		/*
		 * There was no exact-match for this given file, so look
		 * for a "glob" entry which does match.
		 */
		gmatch = 0;
		if (verbose > 2)
			printf("\t+ Checking globs for %s\n", *given);
		STAILQ_FOREACH(ent, globlist, cf_nextp) {
			fnres = fnmatch(ent->log, *given, FNM_PATHNAME);
			if (verbose > 2)
				printf("\t+    = %d for pattern %s\n", fnres,
				    ent->log);
			if (fnres == 0) {
				gmatch++;
				dupent = init_entry(*given, ent);
				/* This new entry is not a glob! */
				dupent->flags &= ~CE_GLOB;
				STAILQ_INSERT_TAIL(cmdlist, dupent, cf_nextp);
				/* Only allow a match to one glob-entry */
				break;
			}
		}
		if (gmatch) {
			if (verbose > 2)
				printf("\t+ Matched %s via %s\n", *given,
				    ent->log);
			continue;
		}

		/*
		 * This given file was not found in any config file, so
		 * add a worklist item based on the default entry.
		 */
		if (verbose > 2)
			printf("\t+ No entry matched %s  (will use %s)\n",
			    *given, DEFAULT_MARKER);
		dupent = init_entry(*given, defconf);
		/* Mark that it was *not* found in a config file */
		dupent->def_cfg = 1;
		STAILQ_INSERT_TAIL(cmdlist, dupent, cf_nextp);
	}

	/*
	 * Free all the entries in the original work list, the list of
	 * glob entries, and the default entry.
	 */
	free_clist(filelist);
	free_clist(globlist);
	free_entry(defconf);

	/* And finally, return a worklist which matches the given files. */
	return (cmdlist);
}

/*
 * Expand the list of entries with filename patterns, and add all files
 * which match those glob-entries onto the worklist.
 */
static void
expand_globs(struct cflist *work_p, struct cflist *glob_p)
{
	int gmatch, gres;
	size_t i;
	char *mfname;
	struct conf_entry *dupent, *ent, *globent;
	glob_t pglob;
	struct stat st_fm;

	/*
	 * The worklist contains all fully-specified (non-GLOB) names.
	 *
	 * Now expand the list of filename-pattern (GLOB) entries into
	 * a second list, which (by definition) will only match files
	 * that already exist.  Do not add a glob-related entry for any
	 * file which already exists in the fully-specified list.
	 */
	STAILQ_FOREACH(globent, glob_p, cf_nextp) {
		gres = glob(globent->log, GLOB_NOCHECK, NULL, &pglob);
		if (gres != 0) {
			warn("cannot expand pattern (%d): %s", gres,
			    globent->log);
			continue;
		}

		if (verbose > 2)
			printf("\t+ Expanding pattern %s\n", globent->log);
		for (i = 0; i < pglob.gl_matchc; i++) {
			mfname = pglob.gl_pathv[i];

			/* See if this file already has a specific entry. */
			gmatch = 0;
			STAILQ_FOREACH(ent, work_p, cf_nextp) {
				if (strcmp(mfname, ent->log) == 0) {
					gmatch++;
					break;
				}
			}
			if (gmatch)
				continue;

			/* Make sure the named matched is a file. */
			gres = lstat(mfname, &st_fm);
			if (gres != 0) {
				/* Error on a file that glob() matched?!? */
				warn("Skipping %s - lstat() error", mfname);
				continue;
			}
			if (!S_ISREG(st_fm.st_mode)) {
				/* We only rotate files! */
				if (verbose > 2)
					printf("\t+  . skipping %s (!file)\n",
					    mfname);
				continue;
			}

			if (verbose > 2)
				printf("\t+  . add file %s\n", mfname);
			dupent = init_entry(mfname, globent);
			/* This new entry is not a glob! */
			dupent->flags &= ~CE_GLOB;

			/* Add to the worklist. */
			STAILQ_INSERT_TAIL(work_p, dupent, cf_nextp);
		}
		globfree(&pglob);
		if (verbose > 2)
			printf("\t+ Done with pattern %s\n", globent->log);
	}
}

/*
 * Parse a configuration file and update a linked list of all the logs to
 * process.
 */
static void
parse_file(FILE *cf, struct cflist *work_p, struct cflist *glob_p,
    struct conf_entry **defconf_p, struct ilist *inclist)
{
	char line[BUFSIZ], *parse, *q;
	char *cp, *errline, *group;
	struct conf_entry *working;
	struct passwd *pwd;
	struct group *grp;
	glob_t pglob;
	int eol, ptm_opts, res, special;
	size_t i;

	errline = NULL;
	while (fgets(line, BUFSIZ, cf)) {
		if ((line[0] == '\n') || (line[0] == '#') ||
		    (strlen(line) == 0))
			continue;
		if (errline != NULL)
			free(errline);
		errline = strdup(line);
		for (cp = line + 1; *cp != '\0'; cp++) {
			if (*cp != '#')
				continue;
			if (*(cp - 1) == '\\') {
				strcpy(cp - 1, cp);
				cp--;
				continue;
			}
			*cp = '\0';
			break;
		}

		q = parse = missing_field(sob(line), errline);
		parse = son(line);
		if (!*parse)
			errx(1, "malformed line (missing fields):\n%s",
			    errline);
		*parse = '\0';

		/*
		 * Allow people to set debug options via the config file.
		 * (NOTE: debug options are undocumented, and may disappear
		 * at any time, etc).
		 */
		if (strcasecmp(DEBUG_MARKER, q) == 0) {
			q = parse = missing_field(sob(parse + 1), errline);
			parse = son(parse);
			if (!*parse)
				warnx("debug line specifies no option:\n%s",
				    errline);
			else {
				*parse = '\0';
				parse_doption(q);
			}
			continue;
		} else if (strcasecmp(INCLUDE_MARKER, q) == 0) {
			if (verbose)
				printf("Found: %s", errline);
			q = parse = missing_field(sob(parse + 1), errline);
			parse = son(parse);
			if (!*parse) {
				warnx("include line missing argument:\n%s",
				    errline);
				continue;
			}

			*parse = '\0';

			if (isglobstr(q)) {
				res = glob(q, GLOB_NOCHECK, NULL, &pglob);
				if (res != 0) {
					warn("cannot expand pattern (%d): %s",
					    res, q);
					continue;
				}

				if (verbose > 2)
					printf("\t+ Expanding pattern %s\n", q);

				for (i = 0; i < pglob.gl_matchc; i++)
					add_to_queue(pglob.gl_pathv[i],
					    inclist);
				globfree(&pglob);
			} else
				add_to_queue(q, inclist);
			continue;
		}

		special = 0;
		working = init_entry(q, NULL);
		if (strcasecmp(DEFAULT_MARKER, q) == 0) {
			special = 1;
			if (*defconf_p != NULL) {
				warnx("Ignoring duplicate entry for %s!", q);
				free_entry(working);
				continue;
			}
			*defconf_p = working;
		}

		q = parse = missing_field(sob(parse + 1), errline);
		parse = son(parse);
		if (!*parse)
			errx(1, "malformed line (missing fields):\n%s",
			    errline);
		*parse = '\0';
		if ((group = strchr(q, ':')) != NULL ||
		    (group = strrchr(q, '.')) != NULL) {
			*group++ = '\0';
			if (*q) {
				if (!(isnumberstr(q))) {
					if ((pwd = getpwnam(q)) == NULL)
						errx(1,
				     "error in config file; unknown user:\n%s",
						    errline);
					working->uid = pwd->pw_uid;
				} else
					working->uid = atoi(q);
			} else
				working->uid = (uid_t)-1;

			q = group;
			if (*q) {
				if (!(isnumberstr(q))) {
					if ((grp = getgrnam(q)) == NULL)
						errx(1,
				    "error in config file; unknown group:\n%s",
						    errline);
					working->gid = grp->gr_gid;
				} else
					working->gid = atoi(q);
			} else
				working->gid = (gid_t)-1;

			q = parse = missing_field(sob(parse + 1), errline);
			parse = son(parse);
			if (!*parse)
				errx(1, "malformed line (missing fields):\n%s",
				    errline);
			*parse = '\0';
		} else {
			working->uid = (uid_t)-1;
			working->gid = (gid_t)-1;
		}

		if (!sscanf(q, "%o", &working->permissions))
			errx(1, "error in config file; bad permissions:\n%s",
			    errline);
		if ((working->permissions & ~DEFFILEMODE) != 0) {
			warnx("File mode bits 0%o changed to 0%o in line:\n%s",
			    working->permissions,
			    working->permissions & DEFFILEMODE, errline);
			working->permissions &= DEFFILEMODE;
		}

		q = parse = missing_field(sob(parse + 1), errline);
		parse = son(parse);
		if (!*parse)
			errx(1, "malformed line (missing fields):\n%s",
			    errline);
		*parse = '\0';
		if (!sscanf(q, "%d", &working->numlogs) || working->numlogs < 0)
			errx(1, "error in config file; bad value for count of logs to save:\n%s",
			    errline);

		q = parse = missing_field(sob(parse + 1), errline);
		parse = son(parse);
		if (!*parse)
			errx(1, "malformed line (missing fields):\n%s",
			    errline);
		*parse = '\0';
		if (isdigitch(*q))
			working->trsize = atoi(q);
		else if (strcmp(q, "*") == 0)
			working->trsize = -1;
		else {
			warnx("Invalid value of '%s' for 'size' in line:\n%s",
			    q, errline);
			working->trsize = -1;
		}

		working->flags = 0;
		working->compress = COMPRESS_NONE;
		q = parse = missing_field(sob(parse + 1), errline);
		parse = son(parse);
		eol = !*parse;
		*parse = '\0';
		{
			char *ep;
			u_long ul;

			ul = strtoul(q, &ep, 10);
			if (ep == q)
				working->hours = 0;
			else if (*ep == '*')
				working->hours = -1;
			else if (ul > INT_MAX)
				errx(1, "interval is too large:\n%s", errline);
			else
				working->hours = ul;

			if (*ep == '\0' || strcmp(ep, "*") == 0)
				goto no_trimat;
			if (*ep != '@' && *ep != '$')
				errx(1, "malformed interval/at:\n%s", errline);

			working->flags |= CE_TRIMAT;
			working->trim_at = ptime_init(NULL);
			ptm_opts = PTM_PARSE_ISO8601;
			if (*ep == '$')
				ptm_opts = PTM_PARSE_DWM;
			ptm_opts |= PTM_PARSE_MATCHDOM;
			res = ptime_relparse(working->trim_at, ptm_opts,
			    ptimeget_secs(timenow), ep + 1);
			if (res == -2)
				errx(1, "nonexistent time for 'at' value:\n%s",
				    errline);
			else if (res < 0)
				errx(1, "malformed 'at' value:\n%s", errline);
		}
no_trimat:

		if (eol)
			q = NULL;
		else {
			q = parse = sob(parse + 1);	/* Optional field */
			parse = son(parse);
			if (!*parse)
				eol = 1;
			*parse = '\0';
		}

		for (; q && *q && !isspacech(*q); q++) {
			switch (tolowerch(*q)) {
			case 'b':
				working->flags |= CE_BINARY;
				break;
			case 'c':
				working->flags |= CE_CREATE;
				break;
			case 'd':
				working->flags |= CE_NODUMP;
				break;
			case 'g':
				working->flags |= CE_GLOB;
				break;
			case 'j':
				working->compress = COMPRESS_BZIP2;
				break;
			case 'n':
				working->flags |= CE_NOSIGNAL;
				break;
			case 'p':
				working->flags |= CE_PLAIN0;
				break;
			case 'r':
				working->flags |= CE_PID2CMD;
				break;
			case 't':
				working->flags |= CE_RFC5424;
				break;
			case 'u':
				working->flags |= CE_SIGNALGROUP;
				break;
			case 'w':
				/* Deprecated flag - keep for compatibility purposes */
				break;
			case 'x':
				working->compress = COMPRESS_XZ;
				break;
			case 'y':
				working->compress = COMPRESS_ZSTD;
				break;
			case 'z':
				working->compress = COMPRESS_GZIP;
				break;
			case '-':
				break;
			case 'f':	/* Used by OpenBSD for "CE_FOLLOW" */
			case 'm':	/* Used by OpenBSD for "CE_MONITOR" */
			default:
				errx(1, "illegal flag in config file -- %c",
				    *q);
			}
		}

		if (eol)
			q = NULL;
		else {
			q = parse = sob(parse + 1);	/* Optional field */
			parse = son(parse);
			if (!*parse)
				eol = 1;
			*parse = '\0';
		}

		working->pid_cmd_file = NULL;
		if (q && *q) {
			if (*q == '/')
				working->pid_cmd_file = strdup(q);
			else if (isalnum(*q))
				goto got_sig;
			else {
				errx(1,
			"illegal pid file or signal in config file:\n%s",
				    errline);
			}
		}
		if (eol)
			q = NULL;
		else {
			q = parse = sob(parse + 1);	/* Optional field */
			parse = son(parse);
			*parse = '\0';
		}

		working->sig = SIGHUP;
		if (q && *q) {
got_sig:
			working->sig = parse_signal(q);
			if (working->sig < 1 || working->sig >= sys_nsig) {
				errx(1,
				    "illegal signal in config file:\n%s",
				    errline);
			}
		}

		/*
		 * Finish figuring out what pid-file to use (if any) in
		 * later processing if this logfile needs to be rotated.
		 */
		if ((working->flags & CE_NOSIGNAL) == CE_NOSIGNAL) {
			/*
			 * This config-entry specified 'n' for nosignal,
			 * see if it also specified an explicit pid_cmd_file.
			 * This would be a pretty pointless combination.
			 */
			if (working->pid_cmd_file != NULL) {
				warnx("Ignoring '%s' because flag 'n' was specified in line:\n%s",
				    working->pid_cmd_file, errline);
				free(working->pid_cmd_file);
				working->pid_cmd_file = NULL;
			}
		} else if (working->pid_cmd_file == NULL) {
			/*
			 * This entry did not specify the 'n' flag, which
			 * means it should signal syslogd unless it had
			 * specified some other pid-file (and obviously the
			 * syslog pid-file will not be for a process-group).
			 * Also, we should only try to notify syslog if we
			 * are root.
			 */
			if (working->flags & CE_SIGNALGROUP) {
				warnx("Ignoring flag 'U' in line:\n%s",
				    errline);
				working->flags &= ~CE_SIGNALGROUP;
			}
			if (needroot)
				working->pid_cmd_file = strdup(path_syslogpid);
		}

		/*
		 * Add this entry to the appropriate list of entries, unless
		 * it was some kind of special entry (eg: <default>).
		 */
		if (special) {
			;			/* Do not add to any list */
		} else if (working->flags & CE_GLOB) {
			STAILQ_INSERT_TAIL(glob_p, working, cf_nextp);
		} else {
			STAILQ_INSERT_TAIL(work_p, working, cf_nextp);
		}
	}
	if (errline != NULL)
		free(errline);
}

static char *
missing_field(char *p, char *errline)
{

	if (!p || !*p)
		errx(1, "missing field in config file:\n%s", errline);
	return (p);
}

/*
 * In our sort we return it in the reverse of what qsort normally
 * would do, as we want the newest files first.  If we have two
 * entries with the same time we don't really care about order.
 *
 * Support function for qsort() in delete_oldest_timelog().
 */
static int
oldlog_entry_compare(const void *a, const void *b)
{
	const struct oldlog_entry *ola = a, *olb = b;

	if (ola->t > olb->t)
		return (-1);
	else if (ola->t < olb->t)
		return (1);
	else
		return (0);
}

/*
 * Check whether the file corresponding to dp is an archive of the logfile
 * logfname, based on the timefnamefmt format string. Return true and fill out
 * tm if this is the case; otherwise return false.
 */
static int
validate_old_timelog(int fd, const struct dirent *dp, const char *logfname,
    struct tm *tm)
{
	struct stat sb;
	size_t logfname_len;
	char *s;
	int c;

	logfname_len = strlen(logfname);

	if (dp->d_type != DT_REG) {
		/*
		 * Some filesystems (e.g. NFS) don't fill out the d_type field
		 * and leave it set to DT_UNKNOWN; in this case we must obtain
		 * the file type ourselves.
		 */
		if (dp->d_type != DT_UNKNOWN ||
		    fstatat(fd, dp->d_name, &sb, AT_SYMLINK_NOFOLLOW) != 0 ||
		    !S_ISREG(sb.st_mode))
			return (0);
	}
	/* Ignore everything but files with our logfile prefix. */
	if (strncmp(dp->d_name, logfname, logfname_len) != 0)
		return (0);
	/* Ignore the actual non-rotated logfile. */
	if (dp->d_namlen == logfname_len)
		return (0);

	/*
	 * Make sure we created have found a logfile, so the
	 * postfix is valid, IE format is: '.<time>(.[bgx]z)?'.
	 */
	if (dp->d_name[logfname_len] != '.') {
		if (verbose)
			printf("Ignoring %s which has unexpected "
			    "extension '%s'\n", dp->d_name,
			    &dp->d_name[logfname_len]);
		return (0);
	}
	memset(tm, 0, sizeof(*tm));
	if ((s = strptime(&dp->d_name[logfname_len + 1],
	    timefnamefmt, tm)) == NULL) {
		/*
		 * We could special case "old" sequentially named logfiles here,
		 * but we do not as that would require special handling to
		 * decide which one was the oldest compared to "new" time based
		 * logfiles.
		 */
		if (verbose)
			printf("Ignoring %s which does not "
			    "match time format\n", dp->d_name);
		return (0);
	}

	for (c = 0; c < COMPRESS_TYPES; c++)
		if (strcmp(s, compress_type[c].suffix) == 0)
			/* We're done. */
			return (1);

	if (verbose)
		printf("Ignoring %s which has unexpected extension '%s'\n",
		    dp->d_name, s);

	return (0);
}

/*
 * Delete the oldest logfiles, when using time based filenames.
 */
static void
delete_oldest_timelog(const struct conf_entry *ent, const char *archive_dir)
{
	char *basebuf, *dirbuf, errbuf[80];
	const char *base, *dir;
	int dir_fd, i, logcnt, max_logcnt;
	struct oldlog_entry *oldlogs;
	struct dirent *dp;
	struct tm tm;
	DIR *dirp;

	oldlogs = malloc(MAX_OLDLOGS * sizeof(struct oldlog_entry));
	max_logcnt = MAX_OLDLOGS;
	logcnt = 0;

	if (archive_dir != NULL && archive_dir[0] != '\0') {
		dirbuf = NULL;
		dir = archive_dir;
	} else {
		if ((dirbuf = strdup(ent->log)) == NULL)
			err(1, "strdup()");
		dir = dirname(dirbuf);
	}

	if ((basebuf = strdup(ent->log)) == NULL)
		err(1, "strdup()");
	base = basename(basebuf);
	if (strcmp(base, "/") == 0)
		errx(1, "Invalid log filename - became '/'");

	if (verbose > 2)
		printf("Searching for old logs in %s\n", dir);

	/* First we create a 'list' of all archived logfiles */
	if ((dirp = opendir(dir)) == NULL)
		err(1, "Cannot open log directory '%s'", dir);
	dir_fd = dirfd(dirp);
	while ((dp = readdir(dirp)) != NULL) {
		if (validate_old_timelog(dir_fd, dp, base, &tm) == 0)
			continue;

		/*
		 * We should now have old an old rotated logfile, so
		 * add it to the 'list'.
		 */
		if ((oldlogs[logcnt].t = timegm(&tm)) == -1)
			err(1, "Could not convert time string to time value");
		if ((oldlogs[logcnt].fname = strdup(dp->d_name)) == NULL)
			err(1, "strdup()");
		logcnt++;

		/*
		 * It is very unlikely we ever run out of space in the
		 * logfile array from the default size, but lets
		 * handle it anyway...
		 */
		if (logcnt >= max_logcnt) {
			max_logcnt *= 4;
			/* Detect integer overflow */
			if (max_logcnt < logcnt)
				errx(1, "Too many old logfiles found");
			oldlogs = realloc(oldlogs,
			    max_logcnt * sizeof(struct oldlog_entry));
			if (oldlogs == NULL)
				err(1, "realloc()");
		}
	}

	/* Second, if needed we delete oldest archived logfiles */
	if (logcnt > 0 && logcnt >= ent->numlogs && ent->numlogs > 1) {
		oldlogs = realloc(oldlogs, logcnt *
		    sizeof(struct oldlog_entry));
		if (oldlogs == NULL)
			err(1, "realloc()");

		/*
		 * We now sort the logs in the order of newest to
		 * oldest.  That way we can simply skip over the
		 * number of records we want to keep.
		 */
		qsort(oldlogs, logcnt, sizeof(struct oldlog_entry),
		    oldlog_entry_compare);
		for (i = ent->numlogs - 1; i < logcnt; i++) {
			if (noaction)
				printf("\trm -f %s/%s\n", dir,
				    oldlogs[i].fname);
			else if (unlinkat(dir_fd, oldlogs[i].fname, 0) != 0) {
				snprintf(errbuf, sizeof(errbuf),
				    "Could not delete old logfile '%s'",
				    oldlogs[i].fname);
				perror(errbuf);
			}
		}
	} else if (verbose > 1)
		printf("No old logs to delete for logfile %s\n", ent->log);

	/* Third, cleanup */
	closedir(dirp);
	for (i = 0; i < logcnt; i++) {
		assert(oldlogs[i].fname != NULL);
		free(oldlogs[i].fname);
	}
	free(oldlogs);
	free(dirbuf);
	free(basebuf);
}

/*
 * Generate a log filename, when using classic filenames.
 */
static void
gen_classiclog_fname(char *fname, size_t fname_sz, const char *archive_dir,
    const char *namepart, int numlogs_c)
{

	if (archive_dir[0] != '\0')
		(void) snprintf(fname, fname_sz, "%s/%s.%d", archive_dir,
		    namepart, numlogs_c);
	else
		(void) snprintf(fname, fname_sz, "%s.%d", namepart, numlogs_c);
}

/*
 * Delete a rotated logfile, when using classic filenames.
 */
static void
delete_classiclog(const char *archive_dir, const char *namepart, int numlog_c)
{
	char file1[MAXPATHLEN], zfile1[MAXPATHLEN];
	int c;

	gen_classiclog_fname(file1, sizeof(file1), archive_dir, namepart,
	    numlog_c);

	for (c = 0; c < COMPRESS_TYPES; c++) {
		(void) snprintf(zfile1, sizeof(zfile1), "%s%s", file1,
		    compress_type[c].suffix);
		if (noaction)
			printf("\trm -f %s\n", zfile1);
		else
			(void) unlink(zfile1);
	}
}

/*
 * Only add to the queue if the file hasn't already been added. This is
 * done to prevent circular include loops.
 */
static void
add_to_queue(const char *fname, struct ilist *inclist)
{
	struct include_entry *inc;

	STAILQ_FOREACH(inc, inclist, inc_nextp) {
		if (strcmp(fname, inc->file) == 0) {
			warnx("duplicate include detected: %s", fname);
			return;
		}
	}

	inc = malloc(sizeof(struct include_entry));
	if (inc == NULL)
		err(1, "malloc of inc");
	inc->file = strdup(fname);

	if (verbose > 2)
		printf("\t+ Adding %s to the processing queue.\n", fname);

	STAILQ_INSERT_TAIL(inclist, inc, inc_nextp);
}

/*
 * Search for logfile and return its compression suffix (if supported)
 * The suffix detection is first-match in the order of compress_types
 *
 * Note: if logfile without suffix exists (uncompressed, COMPRESS_NONE)
 * a zero-length string is returned
 */
static const char *
get_logfile_suffix(const char *logfile)
{
	struct stat st;
	char zfile[MAXPATHLEN];
	int c;

	for (c = 0; c < COMPRESS_TYPES; c++) {
		(void) strlcpy(zfile, logfile, MAXPATHLEN);
		(void) strlcat(zfile, compress_type[c].suffix, MAXPATHLEN);
		if (lstat(zfile, &st) == 0)
			return (compress_type[c].suffix);
	}
	return (NULL);
}

static fk_entry
do_rotate(const struct conf_entry *ent)
{
	char dirpart[MAXPATHLEN], namepart[MAXPATHLEN];
	char file1[MAXPATHLEN], file2[MAXPATHLEN];
	char zfile1[MAXPATHLEN], zfile2[MAXPATHLEN];
	const char *logfile_suffix;
	char datetimestr[30];
	int flags, numlogs_c;
	fk_entry free_or_keep;
	struct sigwork_entry *swork;
	struct stat st;
	struct tm tm;
	time_t now;

	flags = ent->flags;
	free_or_keep = FREE_ENT;

	if (archtodir) {
		char *p;

		/* build complete name of archive directory into dirpart */
		if (*archdirname == '/') {	/* absolute */
			strlcpy(dirpart, archdirname, sizeof(dirpart));
		} else {	/* relative */
			/* get directory part of logfile */
			strlcpy(dirpart, ent->log, sizeof(dirpart));
			if ((p = strrchr(dirpart, '/')) == NULL)
				dirpart[0] = '\0';
			else
				*(p + 1) = '\0';
			strlcat(dirpart, archdirname, sizeof(dirpart));
		}

		/* check if archive directory exists, if not, create it */
		if (lstat(dirpart, &st))
			createdir(ent, dirpart);

		/* get filename part of logfile */
		if ((p = strrchr(ent->log, '/')) == NULL)
			strlcpy(namepart, ent->log, sizeof(namepart));
		else
			strlcpy(namepart, p + 1, sizeof(namepart));
	} else {
		/*
		 * Tell utility functions we are not using an archive
		 * dir.
		 */
		dirpart[0] = '\0';
		strlcpy(namepart, ent->log, sizeof(namepart));
	}

	/* Delete old logs */
	if (timefnamefmt != NULL)
		delete_oldest_timelog(ent, dirpart);
	else {
		/*
		 * Handle cleaning up after legacy newsyslog where we
		 * kept ent->numlogs + 1 files.  This code can go away
		 * at some point in the future.
		 */
		delete_classiclog(dirpart, namepart, ent->numlogs);

		if (ent->numlogs > 0)
			delete_classiclog(dirpart, namepart, ent->numlogs - 1);

	}

	if (timefnamefmt != NULL) {
		/* If time functions fails we can't really do any sensible */
		if (time(&now) == (time_t)-1 ||
		    localtime_r(&now, &tm) == NULL)
			bzero(&tm, sizeof(tm));

		strftime(datetimestr, sizeof(datetimestr), timefnamefmt, &tm);
		if (archtodir)
			(void) snprintf(file1, sizeof(file1), "%s/%s.%s",
			    dirpart, namepart, datetimestr);
		else
			(void) snprintf(file1, sizeof(file1), "%s.%s",
			    ent->log, datetimestr);

		/* Don't run the code to move down logs */
		numlogs_c = -1;
	} else {
		gen_classiclog_fname(file1, sizeof(file1), dirpart, namepart,
		    ent->numlogs - 1);
		numlogs_c = ent->numlogs - 2;		/* copy for countdown */
	}

	/* Move down log files */
	for (; numlogs_c >= 0; numlogs_c--) {
		(void) strlcpy(file2, file1, sizeof(file2));

		gen_classiclog_fname(file1, sizeof(file1), dirpart, namepart,
		    numlogs_c);

		logfile_suffix = get_logfile_suffix(file1);
		if (logfile_suffix == NULL)
			continue;
		(void) strlcpy(zfile1, file1, MAXPATHLEN);
		(void) strlcpy(zfile2, file2, MAXPATHLEN);
		(void) strlcat(zfile1, logfile_suffix, MAXPATHLEN);
		(void) strlcat(zfile2, logfile_suffix, MAXPATHLEN);

		if (noaction)
			printf("\tmv %s %s\n", zfile1, zfile2);
		else {
			/* XXX - Ought to be checking for failure! */
			(void)rename(zfile1, zfile2);
			change_attrs(zfile2, ent);
			if (ent->compress && !strlen(logfile_suffix)) {
				/* compress old rotation */
				struct zipwork_entry zwork;

				memset(&zwork, 0, sizeof(zwork));
				zwork.zw_conf = ent;
				zwork.zw_fsize = sizefile(zfile2);
				strcpy(zwork.zw_fname, zfile2);
				do_zipwork(&zwork);
			}
		}
	}

	if (ent->numlogs > 0) {
		if (noaction) {
			/*
			 * Note that savelog() may succeed with using link()
			 * for the archtodir case, but there is no good way
			 * of knowing if it will when doing "noaction", so
			 * here we claim that it will have to do a copy...
			 */
			if (archtodir)
				printf("\tcp %s %s\n", ent->log, file1);
			else
				printf("\tln %s %s\n", ent->log, file1);
			printf("\ttouch %s\t\t"
			    "# Update mtime for 'when'-interval processing\n",
			    file1);
		} else {
			if (!(flags & CE_BINARY)) {
				/* Report the trimming to the old log */
				log_trim(ent->log, ent);
			}
			savelog(ent->log, file1);
			/*
			 * Interval-based rotations are done using the mtime of
			 * the most recently archived log, so make sure it gets
			 * updated during a rotation.
			 */
			utimes(file1, NULL);
		}
		change_attrs(file1, ent);
	}

	/* Create the new log file and move it into place */
	if (noaction)
		printf("Start new log...\n");
	createlog(ent);

	/*
	 * Save all signalling and file-compression to be done after log
	 * files from all entries have been rotated.  This way any one
	 * process will not be sent the same signal multiple times when
	 * multiple log files had to be rotated.
	 */
	swork = NULL;
	if (ent->pid_cmd_file != NULL)
		swork = save_sigwork(ent);
	if (ent->numlogs > 0 && ent->compress > COMPRESS_NONE) {
		if (!(ent->flags & CE_PLAIN0) ||
		    strcmp(&file1[strlen(file1) - 2], ".0") != 0) {
			/*
			 * The zipwork_entry will include a pointer to this
			 * conf_entry, so the conf_entry should not be freed.
			 */
			free_or_keep = KEEP_ENT;
			save_zipwork(ent, swork, ent->fsize, file1);
		}
	}

	return (free_or_keep);
}

static void
do_sigwork(struct sigwork_entry *swork)
{
	struct sigwork_entry *nextsig;
	int kres, secs;
	char *tmp;

	if (swork->sw_runcmd == 0 && (!(swork->sw_pidok) || swork->sw_pid == 0))
		return;			/* no work to do... */

	/*
	 * If nosignal (-s) was specified, then do not signal any process.
	 * Note that a nosignal request triggers a warning message if the
	 * rotated logfile needs to be compressed, *unless* -R was also
	 * specified.  We assume that an `-sR' request came from a process
	 * which writes to the logfile, and as such, we assume that process
	 * has already made sure the logfile is not presently in use.  This
	 * just sets swork->sw_pidok to a special value, and do_zipwork
	 * will print any necessary warning(s).
	 */
	if (nosignal) {
		if (!rotatereq)
			swork->sw_pidok = -1;
		return;
	}

	/*
	 * Compute the pause between consecutive signals.  Use a longer
	 * sleep time if we will be sending two signals to the same
	 * daemon or process-group.
	 */
	secs = 0;
	nextsig = SLIST_NEXT(swork, sw_nextp);
	if (nextsig != NULL) {
		if (swork->sw_pid == nextsig->sw_pid)
			secs = 10;
		else
			secs = 1;
	}

	if (noaction) {
		if (swork->sw_runcmd)
			printf("\tsh -c '%s %d'\n", swork->sw_fname,
			    swork->sw_signum);
		else {
			printf("\tkill -%d %d \t\t# %s\n", swork->sw_signum,
			    (int)swork->sw_pid, swork->sw_fname);
			if (secs > 0)
				printf("\tsleep %d\n", secs);
		}
		return;
	}

	if (swork->sw_runcmd) {
		asprintf(&tmp, "%s %d", swork->sw_fname, swork->sw_signum);
		if (tmp == NULL) {
			warn("can't allocate memory to run %s",
			    swork->sw_fname);
			return;
		}
		if (verbose)
			printf("Run command: %s\n", tmp);
		kres = system(tmp);
		if (kres) {
			warnx("%s: returned non-zero exit code: %d",
			    tmp, kres);
		}
		free(tmp);
		return;
	}

	kres = kill(swork->sw_pid, swork->sw_signum);
	if (kres != 0) {
		/*
		 * Assume that "no such process" (ESRCH) is something
		 * to warn about, but is not an error.  Presumably the
		 * process which writes to the rotated log file(s) is
		 * gone, in which case we should have no problem with
		 * compressing the rotated log file(s).
		 */
		if (errno != ESRCH)
			swork->sw_pidok = 0;
		warn("can't notify %s, pid %d = %s", swork->sw_pidtype,
		    (int)swork->sw_pid, swork->sw_fname);
	} else {
		if (verbose)
			printf("Notified %s pid %d = %s\n", swork->sw_pidtype,
			    (int)swork->sw_pid, swork->sw_fname);
		if (secs > 0) {
			if (verbose)
				printf("Pause %d second(s) between signals\n",
				    secs);
			sleep(secs);
		}
	}
}

static void
do_zipwork(struct zipwork_entry *zwork)
{
	const struct compress_types *ct;
	struct sbuf *command;
	pid_t pidzip, wpid;
	int c, errsav, fcount, zstatus;
	const char **args, *pgm_name, *pgm_path;
	char *zresult;

	assert(zwork != NULL);
	assert(zwork->zw_conf != NULL);
	assert(zwork->zw_conf->compress > COMPRESS_NONE);
	assert(zwork->zw_conf->compress < COMPRESS_TYPES);

	if (zwork->zw_swork != NULL && zwork->zw_swork->sw_runcmd == 0 &&
	    zwork->zw_swork->sw_pidok <= 0) {
		warnx(
		    "log %s not compressed because daemon(s) not notified",
		    zwork->zw_fname);
		change_attrs(zwork->zw_fname, zwork->zw_conf);
		return;
	}

	ct = &compress_type[zwork->zw_conf->compress];

	/*
	 * execv will be called with the array [ program, flags ... ,
	 * filename, NULL ] so allocate nflags+3 elements for the array.
	 */
	args = calloc(ct->nflags + 3, sizeof(*args));
	if (args == NULL)
		err(1, "calloc");

	pgm_path = ct->path;
	pgm_name = strrchr(pgm_path, '/');
	if (pgm_name == NULL)
		pgm_name = pgm_path;
	else
		pgm_name++;

	/* Build the argument array. */
	args[0] = pgm_name;
	for (c = 0; c < ct->nflags; c++)
		args[c + 1] = ct->flags[c];
	args[c + 1] = zwork->zw_fname;

	/* Also create a space-delimited version if we need to print it. */
	if ((command = sbuf_new_auto()) == NULL)
		errx(1, "sbuf_new");
	sbuf_cpy(command, pgm_path);
	for (c = 1; args[c] != NULL; c++) {
		sbuf_putc(command, ' ');
		sbuf_cat(command, args[c]);
	}
	if (sbuf_finish(command) == -1)
		err(1, "sbuf_finish");

	/* Determine the filename of the compressed file. */
	asprintf(&zresult, "%s%s", zwork->zw_fname, ct->suffix);
	if (zresult == NULL)
		errx(1, "asprintf");

	if (verbose)
		printf("Executing: %s\n", sbuf_data(command));

	if (noaction) {
		printf("\t%s %s\n", pgm_name, zwork->zw_fname);
		change_attrs(zresult, zwork->zw_conf);
		goto out;
	}

	fcount = 1;
	pidzip = fork();
	while (pidzip < 0) {
		/*
		 * The fork failed.  If the failure was due to a temporary
		 * problem, then wait a short time and try it again.
		 */
		errsav = errno;
		warn("fork() for `%s %s'", pgm_name, zwork->zw_fname);
		if (errsav != EAGAIN || fcount > 5)
			errx(1, "Exiting...");
		sleep(fcount * 12);
		fcount++;
		pidzip = fork();
	}
	if (!pidzip) {
		/* The child process executes the compression command */
		execv(pgm_path, __DECONST(char *const*, args));
		err(1, "execv(`%s')", sbuf_data(command));
	}

	wpid = waitpid(pidzip, &zstatus, 0);
	if (wpid == -1) {
		/* XXX - should this be a fatal error? */
		warn("%s: waitpid(%d)", pgm_path, pidzip);
		goto out;
	}
	if (!WIFEXITED(zstatus)) {
		warnx("`%s' did not terminate normally", sbuf_data(command));
		goto out;
	}
	if (WEXITSTATUS(zstatus)) {
		warnx("`%s' terminated with a non-zero status (%d)",
		    sbuf_data(command), WEXITSTATUS(zstatus));
		goto out;
	}

	/* Compression was successful, set file attributes on the result. */
	change_attrs(zresult, zwork->zw_conf);

out:
	sbuf_delete(command);
	free(args);
	free(zresult);
}

/*
 * Save information on any process we need to signal.  Any single
 * process may need to be sent different signal-values for different
 * log files, but usually a single signal-value will cause the process
 * to close and re-open all of its log files.
 */
static struct sigwork_entry *
save_sigwork(const struct conf_entry *ent)
{
	struct sigwork_entry *sprev, *stmp;
	int ndiff;
	size_t tmpsiz;

	sprev = NULL;
	ndiff = 1;
	SLIST_FOREACH(stmp, &swhead, sw_nextp) {
		ndiff = strcmp(ent->pid_cmd_file, stmp->sw_fname);
		if (ndiff > 0)
			break;
		if (ndiff == 0) {
			if (ent->sig == stmp->sw_signum)
				break;
			if (ent->sig > stmp->sw_signum) {
				ndiff = 1;
				break;
			}
		}
		sprev = stmp;
	}
	if (stmp != NULL && ndiff == 0)
		return (stmp);

	tmpsiz = sizeof(struct sigwork_entry) + strlen(ent->pid_cmd_file) + 1;
	stmp = malloc(tmpsiz);

	stmp->sw_runcmd = 0;
	/* If this is a command to run we just set the flag and run command */
	if (ent->flags & CE_PID2CMD) {
		stmp->sw_pid = -1;
		stmp->sw_pidok = 0;
		stmp->sw_runcmd = 1;
	} else {
		set_swpid(stmp, ent);
	}
	stmp->sw_signum = ent->sig;
	strcpy(stmp->sw_fname, ent->pid_cmd_file);
	if (sprev == NULL)
		SLIST_INSERT_HEAD(&swhead, stmp, sw_nextp);
	else
		SLIST_INSERT_AFTER(sprev, stmp, sw_nextp);
	return (stmp);
}

/*
 * Save information on any file we need to compress.  We may see the same
 * file multiple times, so check the full list to avoid duplicates.  The
 * list itself is sorted smallest-to-largest, because that's the order we
 * want to compress the files.  If the partition is very low on disk space,
 * then the smallest files are the most likely to compress, and compressing
 * them first will free up more space for the larger files.
 */
static struct zipwork_entry *
save_zipwork(const struct conf_entry *ent, const struct sigwork_entry *swork,
    int zsize, const char *zipfname)
{
	struct zipwork_entry *zprev, *ztmp;
	int ndiff;
	size_t tmpsiz;

	/* Compute the size if the caller did not know it. */
	if (zsize < 0)
		zsize = sizefile(zipfname);

	zprev = NULL;
	ndiff = 1;
	SLIST_FOREACH(ztmp, &zwhead, zw_nextp) {
		ndiff = strcmp(zipfname, ztmp->zw_fname);
		if (ndiff == 0)
			break;
		if (zsize > ztmp->zw_fsize)
			zprev = ztmp;
	}
	if (ztmp != NULL && ndiff == 0)
		return (ztmp);

	tmpsiz = sizeof(struct zipwork_entry) + strlen(zipfname) + 1;
	ztmp = malloc(tmpsiz);
	ztmp->zw_conf = ent;
	ztmp->zw_swork = swork;
	ztmp->zw_fsize = zsize;
	strcpy(ztmp->zw_fname, zipfname);
	if (zprev == NULL)
		SLIST_INSERT_HEAD(&zwhead, ztmp, zw_nextp);
	else
		SLIST_INSERT_AFTER(zprev, ztmp, zw_nextp);
	return (ztmp);
}

/* Send a signal to the pid specified by pidfile */
static void
set_swpid(struct sigwork_entry *swork, const struct conf_entry *ent)
{
	FILE *f;
	long minok, maxok, rval;
	char *endp, *linep, line[BUFSIZ];

	minok = MIN_PID;
	maxok = MAX_PID;
	swork->sw_pidok = 0;
	swork->sw_pid = 0;
	swork->sw_pidtype = "daemon";
	if (ent->flags & CE_SIGNALGROUP) {
		/*
		 * If we are expected to signal a process-group when
		 * rotating this logfile, then the value read in should
		 * be the negative of a valid process ID.
		 */
		minok = -MAX_PID;
		maxok = -MIN_PID;
		swork->sw_pidtype = "process-group";
	}

	f = fopen(ent->pid_cmd_file, "r");
	if (f == NULL) {
		if (errno == ENOENT && enforcepid == 0) {
			/*
			 * Warn if the PID file doesn't exist, but do
			 * not consider it an error.  Most likely it
			 * means the process has been terminated,
			 * so it should be safe to rotate any log
			 * files that the process would have been using.
			 */
			swork->sw_pidok = 1;
			warnx("pid file doesn't exist: %s", ent->pid_cmd_file);
		} else
			warn("can't open pid file: %s", ent->pid_cmd_file);
		return;
	}

	if (fgets(line, BUFSIZ, f) == NULL) {
		/*
		 * Warn if the PID file is empty, but do not consider
		 * it an error.  Most likely it means the process has
		 * has terminated, so it should be safe to rotate any
		 * log files that the process would have been using.
		 */
		if (feof(f) && enforcepid == 0) {
			swork->sw_pidok = 1;
			warnx("pid/cmd file is empty: %s", ent->pid_cmd_file);
		} else
			warn("can't read from pid file: %s", ent->pid_cmd_file);
		(void)fclose(f);
		return;
	}
	(void)fclose(f);

	errno = 0;
	linep = line;
	while (*linep == ' ')
		linep++;
	rval = strtol(linep, &endp, 10);
	if (*endp != '\0' && !isspacech(*endp)) {
		warnx("pid file does not start with a valid number: %s",
		    ent->pid_cmd_file);
	} else if (rval < minok || rval > maxok) {
		warnx("bad value '%ld' for process number in %s",
		    rval, ent->pid_cmd_file);
		if (verbose)
			warnx("\t(expecting value between %ld and %ld)",
			    minok, maxok);
	} else {
		swork->sw_pidok = 1;
		swork->sw_pid = rval;
	}

	return;
}

/* Log the fact that the logs were turned over */
static int
log_trim(const char *logname, const struct conf_entry *log_ent)
{
	FILE *f;
	const char *xtra;

	if ((f = fopen(logname, "a")) == NULL)
		return (-1);
	xtra = "";
	if (log_ent->def_cfg)
		xtra = " using <default> rule";
	if (log_ent->flags & CE_RFC5424) {
		if (log_ent->firstcreate) {
			fprintf(f, "<%d>1 %s %s newsyslog %d - - %s%s\n",
			    LOG_MAKEPRI(LOG_USER, LOG_INFO),
			    daytime_rfc5424, hostname, getpid(),
			    "logfile first created", xtra);
		} else if (log_ent->r_reason != NULL) {
			fprintf(f, "<%d>1 %s %s newsyslog %d - - %s%s%s\n",
			    LOG_MAKEPRI(LOG_USER, LOG_INFO),
			    daytime_rfc5424, hostname, getpid(),
			    "logfile turned over", log_ent->r_reason, xtra);
		} else {
			fprintf(f, "<%d>1 %s %s newsyslog %d - - %s%s\n",
			    LOG_MAKEPRI(LOG_USER, LOG_INFO),
			    daytime_rfc5424, hostname, getpid(),
			    "logfile turned over", xtra);
		}
	} else {
		if (log_ent->firstcreate)
			fprintf(f,
			    "%s %.*s newsyslog[%d]: logfile first created%s\n",
			    daytime, (int)hostname_shortlen, hostname, getpid(),
			    xtra);
		else if (log_ent->r_reason != NULL)
			fprintf(f,
			    "%s %.*s newsyslog[%d]: logfile turned over%s%s\n",
			    daytime, (int)hostname_shortlen, hostname, getpid(),
			    log_ent->r_reason, xtra);
		else
			fprintf(f,
			    "%s %.*s newsyslog[%d]: logfile turned over%s\n",
			    daytime, (int)hostname_shortlen, hostname, getpid(),
			    xtra);
	}
	if (fclose(f) == EOF)
		err(1, "log_trim: fclose");
	return (0);
}

/* Return size in kilobytes of a file */
static int
sizefile(const char *file)
{
	struct stat sb;

	if (stat(file, &sb) < 0)
		return (-1);
	return (kbytes(sb.st_size));
}

/*
 * Return the mtime of the most recent archive of the logfile, using timestamp
 * based filenames.
 */
static time_t
mtime_old_timelog(const char *file)
{
	struct stat sb;
	struct tm tm;
	int dir_fd;
	time_t t;
	struct dirent *dp;
	DIR *dirp;
	char *logfname, *logfnamebuf, *dir, *dirbuf;

	t = -1;

	if ((dirbuf = strdup(file)) == NULL) {
		warn("strdup() of '%s'", file);
		return (t);
	}
	dir = dirname(dirbuf);
	if ((logfnamebuf = strdup(file)) == NULL) {
		warn("strdup() of '%s'", file);
		free(dirbuf);
		return (t);
	}
	logfname = basename(logfnamebuf);
	if (logfname[0] == '/') {
		warnx("Invalid log filename '%s'", logfname);
		goto out;
	}

	if ((dirp = opendir(dir)) == NULL) {
		warn("Cannot open log directory '%s'", dir);
		goto out;
	}
	dir_fd = dirfd(dirp);
	/* Open the archive dir and find the most recent archive of logfname. */
	while ((dp = readdir(dirp)) != NULL) {
		if (validate_old_timelog(dir_fd, dp, logfname, &tm) == 0)
			continue;

		if (fstatat(dir_fd, dp->d_name, &sb, AT_SYMLINK_NOFOLLOW) == -1) {
			warn("Cannot stat '%s'", file);
			continue;
		}
		if (t < sb.st_mtime)
			t = sb.st_mtime;
	}
	closedir(dirp);

out:
	free(dirbuf);
	free(logfnamebuf);
	return (t);
}

/* Return the age in hours of the most recent archive of the logfile. */
static int
age_old_log(const char *file)
{
	struct stat sb;
	const char *logfile_suffix;
	static unsigned int suffix_maxlen = 0;
	char *tmp;
	size_t tmpsiz;
	time_t mtime;
	int c;

	if (suffix_maxlen == 0) {
		for (c = 0; c < COMPRESS_TYPES; c++)
			suffix_maxlen = MAX(suffix_maxlen,
			    strlen(compress_type[c].suffix));
	}

	tmpsiz = MAXPATHLEN + sizeof(".0") + suffix_maxlen + 1;
	tmp = alloca(tmpsiz);

	if (archtodir) {
		char *p;

		/* build name of archive directory into tmp */
		if (*archdirname == '/') {	/* absolute */
			strlcpy(tmp, archdirname, tmpsiz);
		} else {	/* relative */
			/* get directory part of logfile */
			strlcpy(tmp, file, tmpsiz);
			if ((p = strrchr(tmp, '/')) == NULL)
				tmp[0] = '\0';
			else
				*(p + 1) = '\0';
			strlcat(tmp, archdirname, tmpsiz);
		}

		strlcat(tmp, "/", tmpsiz);

		/* get filename part of logfile */
		if ((p = strrchr(file, '/')) == NULL)
			strlcat(tmp, file, tmpsiz);
		else
			strlcat(tmp, p + 1, tmpsiz);
	} else {
		(void) strlcpy(tmp, file, tmpsiz);
	}

	if (timefnamefmt != NULL) {
		mtime = mtime_old_timelog(tmp);
		if (mtime == -1)
			return (-1);
	} else {
		strlcat(tmp, ".0", tmpsiz);
		logfile_suffix = get_logfile_suffix(tmp);
		if (logfile_suffix == NULL)
			return (-1);
		(void) strlcat(tmp, logfile_suffix, tmpsiz);
		if (stat(tmp, &sb) < 0)
			return (-1);
		mtime = sb.st_mtime;
	}

	return ((int)(ptimeget_secs(timenow) - mtime + 1800) / 3600);
}

/* Skip Over Blanks */
static char *
sob(char *p)
{
	while (p && *p && isspace(*p))
		p++;
	return (p);
}

/* Skip Over Non-Blanks */
static char *
son(char *p)
{
	while (p && *p && !isspace(*p))
		p++;
	return (p);
}

/* Check if string is actually a number */
static int
isnumberstr(const char *string)
{
	while (*string) {
		if (!isdigitch(*string++))
			return (0);
	}
	return (1);
}

/* Check if string contains a glob */
static int
isglobstr(const char *string)
{
	char chr;

	while ((chr = *string++)) {
		if (chr == '*' || chr == '?' || chr == '[')
			return (1);
	}
	return (0);
}

/*
 * Save the active log file under a new name.  A link to the new name
 * is the quick-and-easy way to do this.  If that fails (which it will
 * if the destination is on another partition), then make a copy of
 * the file to the new location.
 */
static void
savelog(char *from, char *to)
{
	FILE *src, *dst;
	int c, res;

	res = link(from, to);
	if (res == 0)
		return;

	if ((src = fopen(from, "r")) == NULL)
		err(1, "can't fopen %s for reading", from);
	if ((dst = fopen(to, "w")) == NULL)
		err(1, "can't fopen %s for writing", to);

	while ((c = getc(src)) != EOF) {
		if ((putc(c, dst)) == EOF)
			err(1, "error writing to %s", to);
	}

	if (ferror(src))
		err(1, "error reading from %s", from);
	if ((fclose(src)) != 0)
		err(1, "can't fclose %s", to);
	if ((fclose(dst)) != 0)
		err(1, "can't fclose %s", from);
}

/* create one or more directory components of a path */
static void
createdir(const struct conf_entry *ent, char *dirpart)
{
	int res;
	char *s, *d;
	char mkdirpath[MAXPATHLEN];
	struct stat st;

	s = dirpart;
	d = mkdirpath;

	for (;;) {
		*d++ = *s++;
		if (*s != '/' && *s != '\0')
			continue;
		*d = '\0';
		res = lstat(mkdirpath, &st);
		if (res != 0) {
			if (noaction) {
				printf("\tmkdir %s\n", mkdirpath);
			} else {
				res = mkdir(mkdirpath, 0755);
				if (res != 0)
					err(1, "Error on mkdir(\"%s\") for -a",
					    mkdirpath);
			}
		}
		if (*s == '\0')
			break;
	}
	if (verbose) {
		if (ent->firstcreate)
			printf("Created directory '%s' for new %s\n",
			    dirpart, ent->log);
		else
			printf("Created directory '%s' for -a\n", dirpart);
	}
}

/*
 * Create a new log file, destroying any currently-existing version
 * of the log file in the process.  If the caller wants a backup copy
 * of the file to exist, they should call 'link(logfile,logbackup)'
 * before calling this routine.
 */
void
createlog(const struct conf_entry *ent)
{
	int fd, failed;
	struct stat st;
	char *realfile, *slash, tempfile[MAXPATHLEN];

	fd = -1;
	realfile = ent->log;

	/*
	 * If this log file is being created for the first time (-C option),
	 * then it may also be true that the parent directory does not exist
	 * yet.  Check, and create that directory if it is missing.
	 */
	if (ent->firstcreate) {
		strlcpy(tempfile, realfile, sizeof(tempfile));
		slash = strrchr(tempfile, '/');
		if (slash != NULL) {
			*slash = '\0';
			failed = stat(tempfile, &st);
			if (failed && errno != ENOENT)
				err(1, "Error on stat(%s)", tempfile);
			if (failed)
				createdir(ent, tempfile);
			else if (!S_ISDIR(st.st_mode))
				errx(1, "%s exists but is not a directory",
				    tempfile);
		}
	}

	/*
	 * First create an unused filename, so it can be chown'ed and
	 * chmod'ed before it is moved into the real location.  mkstemp
	 * will create the file mode=600 & owned by us.  Note that all
	 * temp files will have a suffix of '.z<something>'.
	 */
	strlcpy(tempfile, realfile, sizeof(tempfile));
	strlcat(tempfile, ".zXXXXXX", sizeof(tempfile));
	if (noaction)
		printf("\tmktemp %s\n", tempfile);
	else {
		fd = mkstemp(tempfile);
		if (fd < 0)
			err(1, "can't mkstemp logfile %s", tempfile);

		/*
		 * Add status message to what will become the new log file.
		 */
		if (!(ent->flags & CE_BINARY)) {
			if (log_trim(tempfile, ent))
				err(1, "can't add status message to log");
		}
	}

	/* Change the owner/group, if we are supposed to */
	if (ent->uid != (uid_t)-1 || ent->gid != (gid_t)-1) {
		if (noaction)
			printf("\tchown %u:%u %s\n", ent->uid, ent->gid,
			    tempfile);
		else {
			failed = fchown(fd, ent->uid, ent->gid);
			if (failed)
				err(1, "can't fchown temp file %s", tempfile);
		}
	}

	/* Turn on NODUMP if it was requested in the config-file. */
	if (ent->flags & CE_NODUMP) {
		if (noaction)
			printf("\tchflags nodump %s\n", tempfile);
		else {
			failed = fchflags(fd, UF_NODUMP);
			if (failed) {
				warn("log_trim: fchflags(NODUMP)");
			}
		}
	}

	/*
	 * Note that if the real logfile still exists, and if the call
	 * to rename() fails, then "neither the old file nor the new
	 * file shall be changed or created" (to quote the standard).
	 * If the call succeeds, then the file will be replaced without
	 * any window where some other process might find that the file
	 * did not exist.
	 * XXX - ? It may be that for some error conditions, we could
	 *	retry by first removing the realfile and then renaming.
	 */
	if (noaction) {
		printf("\tchmod %o %s\n", ent->permissions, tempfile);
		printf("\tmv %s %s\n", tempfile, realfile);
	} else {
		failed = fchmod(fd, ent->permissions);
		if (failed)
			err(1, "can't fchmod temp file '%s'", tempfile);
		failed = rename(tempfile, realfile);
		if (failed)
			err(1, "can't mv %s to %s", tempfile, realfile);
	}

	if (fd >= 0)
		close(fd);
}

/*
 * Change the attributes of a given filename to what was specified in
 * the newsyslog.conf entry.  This routine is only called for files
 * that newsyslog expects that it has created, and thus it is a fatal
 * error if this routine finds that the file does not exist.
 */
static void
change_attrs(const char *fname, const struct conf_entry *ent)
{
	int failed;

	if (noaction) {
		printf("\tchmod %o %s\n", ent->permissions, fname);

		if (ent->uid != (uid_t)-1 || ent->gid != (gid_t)-1)
			printf("\tchown %u:%u %s\n",
			    ent->uid, ent->gid, fname);

		if (ent->flags & CE_NODUMP)
			printf("\tchflags nodump %s\n", fname);
		return;
	}

	failed = chmod(fname, ent->permissions);
	if (failed) {
		if (errno != EPERM)
			err(1, "chmod(%s) in change_attrs", fname);
		warn("change_attrs couldn't chmod(%s)", fname);
	}

	if (ent->uid != (uid_t)-1 || ent->gid != (gid_t)-1) {
		failed = chown(fname, ent->uid, ent->gid);
		if (failed)
			warn("can't chown %s", fname);
	}

	if (ent->flags & CE_NODUMP) {
		failed = chflags(fname, UF_NODUMP);
		if (failed)
			warn("can't chflags %s NODUMP", fname);
	}
}

/*
 * Parse a signal number or signal name. Returns the signal number parsed or -1
 * on failure.
 */
static int
parse_signal(const char *str)
{
	int sig, i;
	const char *errstr;

	sig = strtonum(str, 1, sys_nsig - 1, &errstr);

	if (errstr == NULL)
		return (sig);
	if (strncasecmp(str, "SIG", 3) == 0)
		str += 3;

	for (i = 1; i < sys_nsig; i++) {
		if (strcasecmp(str, sys_signame[i]) == 0)
			return (i);
	}

	return (-1);
}
