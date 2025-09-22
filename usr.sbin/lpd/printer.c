/*	$OpenBSD: printer.c,v 1.4 2022/12/28 21:30:17 jmc Exp $	*/

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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/tree.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <vis.h>

#include "lpd.h"
#include "lp.h"
#include "log.h"

#define RETRY_MAX	5

#define JOB_OK		0
#define JOB_AGAIN	1
#define JOB_IGNORE	2
#define JOB_ERROR	3

enum {
	OK = 0,
	ERR_TRANSIENT,	/* transient error */
	ERR_ACCOUNT,	/* account required on the local machine */
	ERR_ACCESS,	/* cannot read file */
	ERR_INODE,	/* inode changed */
	ERR_NOIMPL,	/* unimplemented feature */
	ERR_REJECTED,	/* remote server rejected a job */
	ERR_ERROR,	/* filter report an error */
	ERR_FILTER,	/* filter return invalid status */
};

struct job {
	char	*class;
	char	*host;
	char	*literal;
	char	*mail;
	char	*name;
	char	*person;
	char	*statinfo;
	char	*title;
	int	 indent;
	int	 pagewidth;
};

struct prnstate {
	int	 pfd;		/* printer fd */
	int	 ofilter;	/* use output filter when printing */
	int	 ofd;		/* output filter fd */
	pid_t	 opid;		/* output filter process */
	int	 tof;		/* true if at top of form */
	int	 count;		/* number of printed files */
	char	 efile[64];	/* filename for filter stderr */
};

static void sighandler(int);
static char *xstrdup(const char *);

static int openfile(const char *, const char *, struct stat *, FILE **);
static int printjob(const char *, int);
static void printbanner(struct job *);
static int printfile(struct job *, int, const char *, const char *);
static int sendjob(const char *, int);
static int sendcmd(const char *, ...);
static int sendfile(int, const char *, const char *);
static int recvack(void);
static void mailreport(struct job *, int);

static void prn_open(void);
static int prn_connect(void);
static void prn_close(void);
static int prn_fstart(void);
static void prn_fsuspend(void);
static void prn_fresume(void);
static void prn_fclose(void);
static int prn_formfeed(void);
static int prn_write(const char *, size_t);
static int prn_writefile(FILE *);
static int prn_puts(const char *);
static ssize_t prn_read(char *, size_t);

static struct lp_printer *lp;
static struct prnstate *prn;

void
printer(int debug, int verbose, const char *name)
{
	struct sigaction sa;
	struct passwd *pw;
	struct lp_queue q;
	int fd, jobidx, qstate, r, reload, retry;
	char buf[64], curr[1024];

	/* Early initialisation. */
	log_init(debug, LOG_LPR);
	log_setverbose(verbose);
	snprintf(buf, sizeof(buf), "printer:%s", name);
	log_procinit(buf);
	setproctitle("%s", buf);

	if ((lpd_hostname = malloc(HOST_NAME_MAX+1)) == NULL)
		fatal("%s: malloc", __func__);
	gethostname(lpd_hostname, HOST_NAME_MAX+1);

	/* Detach from lpd session if not in debug mode. */
	if (!debug)
		if (setsid() == -1)
			fatal("%s: setsid", __func__);

	/* Read printer config. */
	if ((lp = calloc(1, sizeof(*lp))) == NULL)
		fatal("%s: calloc", __func__);
	if (lp_getprinter(lp, name) == -1)
		exit(1);

	/*
	 * Redirect stderr if not in debug mode.
	 * This must be done before dropping privileges.
	 */
	if (!debug) {
		fd = open(LP_LF(lp), O_WRONLY|O_APPEND);
		if (fd == -1)
			fatal("%s: open: %s", __func__, LP_LF(lp));
		if (fd != STDERR_FILENO) {
			if (dup2(fd, STDERR_FILENO) == -1)
				fatalx("%s: dup2", __func__);
			(void)close(fd);
		}
	}

	/* Drop privileges. */
	if ((pw = getpwnam(LPD_USER)) == NULL)
		fatalx("unknown user " LPD_USER);

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("cannot drop privileges");

	/* Initialize the printer state. */
	if ((prn = calloc(1, sizeof(*prn))) == NULL)
		fatal("%s: calloc", __func__);
	prn->pfd = -1;
	prn->ofd = -1;

	/* Setup signals */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sighandler;
	sa.sa_flags = SA_RESTART;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGINT);	/* for kill() in sighandler */
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	/* Grab lock file. */
	if (lp_lock(lp) == -1) {
		if (errno == EWOULDBLOCK) {
			log_debug("already locked");
			exit(0);
		}
		fatalx("cannot open lock file");
	}

	/* Pledge. */
	switch (lp->lp_type) {
	case PRN_LOCAL:
		pledge("stdio rpath wpath cpath flock getpw tty proc exec",
		    NULL);
		break;

	case PRN_NET:
		pledge("stdio rpath wpath cpath inet flock dns getpw proc exec",
		    NULL);
		break;

	case PRN_LPR:
		pledge("stdio rpath wpath cpath inet flock dns getpw", NULL);
		break;
	}

	/* Start processing the queue. */
	memset(&q, 0, sizeof(q));
	jobidx = 0;
	reload = 1;
	retry = 0;
	curr[0] = '\0';

	for (;;) {

		/* Check the queue state. */
		if (lp_getqueuestate(lp, 1, &qstate) == -1)
			fatalx("cannot get queue state");
		if (qstate & LPQ_PRINTER_DOWN) {
			log_debug("printing disabled");
			break;
		}
		if (qstate & LPQ_QUEUE_UPDATED) {
			log_debug("queue updated");
			if (reload == 0)
				lp_clearqueue(&q);
			reload = 1;
		}

		/* Read the queue if needed. */
		if (reload || q.count == 0) {
			if (lp_readqueue(lp, &q) == -1)
				fatalx("cannot read queue");
			jobidx = 0;
			reload = 0;
		}

		/* If the queue is empty, all done */
		if (q.count <= jobidx) {
			log_debug("queue empty");
			break;
		}

		/* Open the printer if needed. */
		if (prn->pfd == -1) {
			prn_open();
			/*
			 * Opening the printer might take some time.
			 * Re-read the queue in case its state has changed.
			 */
			lp_clearqueue(&q);
			reload = 1;
			continue;
		}

		if (strcmp(curr, q.cfname[jobidx]))
			retry = 0;
		else
			strlcpy(curr, q.cfname[jobidx], sizeof(curr));

		lp_setcurrtask(lp, q.cfname[jobidx]);
		if (lp->lp_type == PRN_LPR)
			r = sendjob(q.cfname[jobidx], retry);
		else
			r = printjob(q.cfname[jobidx], retry);
		lp_setcurrtask(lp, NULL);

		switch (r) {
		case JOB_OK:
			log_info("job %s %s successfully", q.cfname[jobidx],
			    (lp->lp_type == PRN_LPR) ? "relayed" : "printed");
			break;
		case JOB_AGAIN:
			retry++;
			continue;
		case JOB_IGNORE:
			break;
		case JOB_ERROR:
			log_warnx("job %s could not be printed",
			    q.cfname[jobidx]);
			break;
		}
		curr[0] = '\0';
		jobidx++;
		retry = 0;
	}

	if (prn->pfd != -1) {
		if (prn->count) {
			prn_formfeed();
			if (lp->lp_tr)
				prn_puts(lp->lp_tr);
		}
		prn_close();
	}

	exit(0);
}

static void
sighandler(int code)
{
	log_info("got signal %d", code);

	exit(0);
}

static char *
xstrdup(const char *s)
{
	char *r;

	if ((r = strdup(s)) == NULL)
		fatal("strdup");

	return r;
}

/*
 * Open control/data file, and check that the inode information is valid.
 * On success, fill the "st" structure and set "fpp" and return 0 (OK).
 * Return an error code on error.
 */
static int
openfile(const char *fname, const char *inodeinfo, struct stat *st, FILE **fpp)
{
	FILE *fp;
	char buf[64];

	if (inodeinfo) {
		log_warnx("cannot open %s: symlink not implemented", fname);
		return ERR_NOIMPL;
	}
	else {
		if ((fp = lp_fopen(lp, fname)) == NULL) {
			log_warn("cannot open %s", fname);
			return ERR_ACCESS;
		}
	}

	if (fstat(fileno(fp), st) == -1) {
		log_warn("%s: fstat: %s", __func__, fname);
		fclose(fp);
		return ERR_ACCESS;
	}

	if (inodeinfo) {
		snprintf(buf, sizeof(buf), "%d %llu", st->st_dev, st->st_ino);
		if (strcmp(inodeinfo, buf)) {
			log_warnx("inode changed for %s", fname);
			fclose(fp);
			return ERR_INODE;
		}
	}

	*fpp = fp;

	return OK;
}

/*
 * Print the job described by the control file.
 */
static int
printjob(const char *cfname, int retry)
{
	struct job job;
	FILE *fp;
	ssize_t len;
	size_t linesz = 0;
	char *line = NULL;
	const char *errstr;
	long long num;
	int r, ret = JOB_OK;

	log_debug("printing job %s...", cfname);

	prn->efile[0] = '\0';
	memset(&job, 0, sizeof(job));
	job.pagewidth = lp->lp_pw;

	if ((fp = lp_fopen(lp, cfname)) == NULL) {
		if (errno == ENOENT) {
			log_info("missing control file %s", cfname);
			return JOB_IGNORE;
		}
		/* XXX no fatal? */
		fatal("cannot open %s", cfname);
	}

	/* First pass: setup the job structure, print banner and print data. */
	while ((len = getline(&line, &linesz, fp)) != -1) {
		if (line[len-1] == '\n')
			line[len-1] = '\0';

		switch (line[0]) {
		case 'C':		/* Classification */
			if (line[1]) {
				free(job.class);
				job.class = xstrdup(line + 1);
			}
			else if (job.class == NULL)
				job.class = xstrdup(lpd_hostname);
			break;

		case 'H':		 /* Host name */
			free(job.host);
			job.host = xstrdup(line + 1);
			if (job.class == NULL)
				job.class = xstrdup(line + 1);
			break;

		case 'I':		 /* Indent */
			errstr = NULL;
			num = strtonum(line + 1, 0, INT_MAX, &errstr);
			if (errstr == NULL)
				job.indent = num;
			else
				log_warnx("strtonum: %s", errstr);
			break;

		case 'J':		 /* Job Name */
			free(job.name);
			if (line[1])
				job.name = strdup(line + 1);
			else
				job.name = strdup(" ");
			break;

		case 'L':		 /* Literal */
			free(job.literal);
			job.literal = xstrdup(line + 1);
			if (!lp->lp_sh && !lp->lp_hl)
				printbanner(&job);
			break;

		case 'M':		/* Send mail to the specified user */
			free(job.mail);
			job.mail = xstrdup(line + 1);
			break;

		case 'N':	 	/* Filename */
			break;

		case 'P':		 /* Person */
			free(job.person);
			job.person = xstrdup(line + 1);
			if (lp->lp_rs && getpwnam(job.person) == NULL) {
				mailreport(&job, ERR_ACCOUNT);
				ret = JOB_ERROR;
				goto remove;
			}
			break;

		case 'S':		 /* Stat info for symlink protection */
			job.statinfo = xstrdup(line + 1);
			break;

		case 'T':		/* Title for pr	*/
			job.title = xstrdup(line + 1);
			break;

		case 'U':		 /* Unlink */
			break;

		case 'W':		 /* Width */
			errstr = NULL;
			num = strtonum(line + 1, 0, INT_MAX, &errstr);
			if (errstr == NULL)
				job.pagewidth = num;
			else
				log_warnx("strtonum: %s", errstr);
			break;

		case '1':		/* troff fonts */
		case '2':
		case '3':
		case '4':
			/* XXX not implemented */
			break;

		default:
			if (line[0] < 'a' || line[0] > 'z')
				break;

			r = printfile(&job, line[0], line+1, job.statinfo);
			free(job.statinfo);
			job.statinfo = NULL;
			free(job.title);
			job.title = NULL;
			if (r) {
				if (r == ERR_TRANSIENT && retry < RETRY_MAX) {
					ret = JOB_AGAIN;
					goto done;
				}
				mailreport(&job, r);
				ret = JOB_ERROR;
				goto remove;
			}
		}
	}

    remove:
	if (lp_unlink(lp, cfname) == -1)
		log_warn("cannot unlink %s", cfname);

	/* Second pass: print trailing banner, mail report, and remove files. */
	rewind(fp);
	while ((len = getline(&line, &linesz, fp)) != -1) {
		if (line[len-1] == '\n')
			line[len-1] = '\0';

		switch (line[0]) {
		case 'L':		/* Literal */
			if (ret != JOB_OK)
				break;
			if (!lp->lp_sh && lp->lp_hl)
				printbanner(&job);
			break;

		case 'M':		/* Send mail to the specified user */
			if (ret == JOB_OK)
				mailreport(&job, ret);
			break;

		case 'U':		/* Unlink */
			if (lp_unlink(lp, line + 1) == -1)
				log_warn("cannot unlink %s", line + 1);
			break;
		}
	}

    done:
	if (prn->efile[0])
		unlink(prn->efile);
	(void)fclose(fp);
	free(job.class);
	free(job.host);
	free(job.literal);
	free(job.mail);
	free(job.name);
	free(job.person);
	free(job.statinfo);
	free(job.title);
	return ret;
}

static void
printbanner(struct job *job)
{
	time_t t;

        time(&t);

	prn_formfeed();

        if (lp->lp_sb) {
		if (job->class) {
			prn_puts(job->class);
			prn_puts(":");
		}
		prn_puts(job->literal);
		prn_puts("  Job: ");
		prn_puts(job->name);
		prn_puts("  Date: ");
		prn_puts(ctime(&t));
		prn_puts("\n");
	} else {
		prn_puts("\n\n\n");
		lp_banner(prn->pfd, job->literal, lp->lp_pw);
		prn_puts("\n\n");
		lp_banner(prn->pfd, job->name, lp->lp_pw);
		if (job->class) {
			prn_puts("\n\n\n");
			lp_banner(prn->pfd, job->class, lp->lp_pw);
		}
		prn_puts("\n\n\n\n\t\t\t\t\tJob:  ");
		prn_puts(job->name);
		prn_puts("\n\t\t\t\t\tDate: ");
		prn_puts(ctime(&t));
		prn_puts("\n");
	}

	prn_formfeed();
}

static int
printfile(struct job *job, int fmt, const char *fname, const char *inodeinfo)
{
	pid_t pid;
	struct stat st;
	FILE *fp;
	size_t n;
	int ret, argc, efd, status;
	char *argv[16], *prog, width[16], length[16], indent[16], tmp[512];

	log_debug("printing file %s...", fname);

	switch (fmt) {
	case 'f':	/* print file as-is */
	case 'o':	/* print postscript file */
	case 'l':	/* print file as-is but pass control chars */
		break;

	case 'p':	/* print using pr(1) */
	case 'r':	/* print fortran text file */
	case 't':	/* print troff output */
	case 'n':	/* print ditroff output */
	case 'd':	/* print tex output */
	case 'c':	/* print cifplot output */
	case 'g':	/* print plot output */
	case 'v':	/* print raster output */
	default:
		log_warn("unrecognized output format '%c'", fmt);
		return ERR_NOIMPL;
	}

	if ((ret = openfile(fname, inodeinfo, &st, &fp)) != OK)
		return ret;

	prn_formfeed();

	/*
	 * No input filter, just write the raw file.
	 */
	if (!lp->lp_if) {
		if (prn_writefile(fp) == -1)
			ret = ERR_TRANSIENT;
		else
			ret = OK;
		(void)fclose(fp);
		return ret;
	}

	/*
	 * Otherwise, run the input filter with proper plumbing.
	 */

	/* Prepare filter arguments. */
	snprintf(width, sizeof(width), "-w%d", job->pagewidth);
	snprintf(length, sizeof(length), "-l%ld", lp->lp_pl);
	snprintf(indent, sizeof(indent), "-i%d", job->indent);
	prog = strrchr(lp->lp_if, '/');

	argc = 0;
	argv[argc++] = 	prog ? (prog + 1) : lp->lp_if;
	if (fmt == 'l')
		argv[argc++] = "-c";
	argv[argc++] = width;
	argv[argc++] = length;
	argv[argc++] = indent;
	argv[argc++] = "-n";
	argv[argc++] = job->person;
	if (job->name) {
		argv[argc++] = "-j";
		argv[argc++]= job->name;
	}
	argv[argc++] = "-h";
	argv[argc++] = job->host;
	argv[argc++] = lp->lp_af;
	argv[argc++] = NULL;

	/* Open the stderr file. */
	strlcpy(prn->efile, "/tmp/prn.XXXXXXXX", sizeof(prn->efile));
	if ((efd = mkstemp(prn->efile)) == -1) {
		log_warn("%s: mkstemp", __func__);
		(void)fclose(fp);
		return ERR_TRANSIENT;
	}

	/* Disable output filter. */
	prn_fsuspend();

	/* Run input filter */
	switch ((pid = fork())) {
	case -1:
		log_warn("%s: fork", __func__);
		close(efd);
		prn_fresume();
		return ERR_TRANSIENT;

	case 0:
		if (dup2(fileno(fp), STDIN_FILENO) == -1)
			fatal("%s:, dup2", __func__);
		if (dup2(prn->pfd, STDOUT_FILENO) == -1)
			fatal("%s:, dup2", __func__);
		if (dup2(efd, STDERR_FILENO) == -1)
			fatal("%s:, dup2", __func__);
		if (closefrom(3) == -1)
			fatal("%s:, closefrom", __func__);
		execv(lp->lp_if, argv);
		log_warn("%s:, execv", __func__);
		exit(2);

	default:
		break;
	}

	log_debug("waiting for ifilter...");

	/* Wait for input filter to finish. */
	while (waitpid(pid, &status, 0) == -1)
		log_warn("%s: waitpid", __func__);

	log_debug("ifilter done, status %d", status);

	/* Resume output filter */
	prn_fresume();
	prn->tof = 0;

	/* Copy efd to stderr */
	if (lseek(efd, 0, SEEK_SET) == -1)
		log_warn("%s: lseek", __func__);
	while ((n = read(efd, tmp, sizeof(tmp))) > 0)
		(void)write(STDERR_FILENO, tmp, n);
	close(efd);

	if (!WIFEXITED(status)) {
		log_warn("filter terminated (termsig=%d)", WTERMSIG(status));
		return ERR_FILTER;
	}

	switch (WEXITSTATUS(status)) {
	case 0:
		prn->tof = 1;
		return OK;

	case 1:
		return ERR_TRANSIENT;

	case 2:
		return ERR_ERROR;

	default:
		log_warn("filter exited (exitstatus=%d)", WEXITSTATUS(status));
		return ERR_FILTER;
        }
}

static int
sendjob(const char *cfname, int retry)
{
	struct job job;
	FILE *fp;
	ssize_t len;
	size_t linesz = 0;
	char *line = NULL;
	int ret = JOB_OK, r;

	log_debug("sending job %s...", cfname);

	memset(&job, 0, sizeof(job));

	if ((fp = lp_fopen(lp, cfname)) == NULL) {
		if (errno == ENOENT) {
			log_info("missing control file %s", cfname);
			return JOB_IGNORE;
		}
		/* XXX no fatal? */
		fatal("cannot open %s", cfname);
	}

	/* First pass: setup the job structure, and forward data files. */
	while ((len = getline(&line, &linesz, fp)) != -1) {
		if (line[len-1] == '\n')
			line[len-1] = '\0';

		switch (line[0]) {
		case 'P':
			free(job.person);
			job.person = xstrdup(line + 1);
			break;

		case 'S':
			free(job.statinfo);
			job.statinfo = xstrdup(line + 1);
			break;

		default:
			if (line[0] < 'a' || line[0] > 'z')
				break;

			r = sendfile('\3', line+1, job.statinfo);
			free(job.statinfo);
			job.statinfo = NULL;
			if (r) {
				if (r == ERR_TRANSIENT && retry < RETRY_MAX) {
					ret = JOB_AGAIN;
					goto done;
				}
				mailreport(&job, r);
				ret = JOB_ERROR;
				goto remove;
			}
		}
	}

	/* Send the control file. */
	if ((r = sendfile('\2', cfname, ""))) {
		if (r == ERR_TRANSIENT && retry < RETRY_MAX) {
			ret = JOB_AGAIN;
			goto done;
		}
		mailreport(&job, r);
		ret = JOB_ERROR;
	}

    remove:
	if (lp_unlink(lp, cfname) == -1)
		log_warn("cannot unlink %s", cfname);

	/* Second pass: remove files. */
	rewind(fp);
	while ((len = getline(&line, &linesz, fp)) != -1) {
		if (line[len-1] == '\n')
			line[len-1] = '\0';

		switch (line[0]) {
		case 'U':
			if (lp_unlink(lp, line + 1) == -1)
				log_warn("cannot unlink %s", line + 1);
			break;
		}
	}

    done:
	(void)fclose(fp);
	free(line);
	free(job.person);
	free(job.statinfo);
	return ret;
}

/*
 * Send a LPR command to the remote lpd server and return the ack.
 * Return 0 for ack, 1 or nack, -1 and set errno on error.
 */
static int
sendcmd(const char *fmt, ...)
{
	va_list	ap;
	unsigned char line[1024];
	int len;

	va_start(ap, fmt);
	len = vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);

	if (len < 0) {
		log_warn("%s: vsnprintf", __func__);
		return -1;
	}

	if (prn_puts(line) == -1)
		return -1;

	return recvack();
}

static int
sendfile(int type, const char *fname, const char *inodeinfo)
{
	struct stat st;
	FILE *fp = NULL;
	int ret;

	log_debug("sending file %s...", fname);

	if ((ret = openfile(fname, inodeinfo, &st, &fp)) != OK)
		return ret;

	ret = ERR_TRANSIENT;
	if (sendcmd("%c%lld %s\n", type, (long long)st.st_size, fname)) {
		if (errno == 0)
			ret = ERR_REJECTED;
		goto fail;
	}

	lp_setstatus(lp, "sending %s to %s", fname, lp->lp_rm);
	if (prn_writefile(fp) == -1 || prn_write("\0", 1) == -1)
		goto fail;
	if (recvack()) {
		if (errno == 0)
			ret = ERR_REJECTED;
		goto fail;
	}

	ret = OK;

    fail:
	(void)fclose(fp);

	if (ret == ERR_REJECTED)
		log_warnx("%s rejected by remote host", fname);

	return ret;
}

/*
 * Read a ack response from the server.
 * Return 0 for ack, 1 or nack, -1 and set errno on error.
 */
static int
recvack(void)
{
	char visbuf[256 * 4 + 1];
	unsigned char line[1024];
	ssize_t n;

	if ((n = prn_read(line, sizeof(line))) == -1)
		return -1;

	if (n == 1) {
		errno = 0;
		if (line[0])
			log_warnx("%s: \\%d", lp->lp_host, line[0]);
		return line[0] ? 1 : 0;
	}

	if (n > 256)
		n = 256;
	line[n] = '\0';
	if (line[n-1] == '\n')
		line[--n] = '\0';

	strvisx(visbuf, line, n, VIS_NL | VIS_CSTYLE);
	log_warnx("%s: %s", lp->lp_host, visbuf);

	errno = 0;
	return -1;
}

static void
mailreport(struct job *job, int result)
{
	struct stat st;
	FILE *fp = NULL, *efp;
	const char *user;
	char *cp;
	int p[2], c;

	if (job->mail)
		user = 	job->mail;
	else
		user = 	job->person;
	if (user == NULL) {
		log_warnx("no user to send report to");
		return;
	}

	if (pipe(p) == -1) {
		log_warn("pipe");
		return;
	}

	switch (fork()) {
	case -1:
		(void)close(p[0]);
		(void)close(p[1]);
		log_warn("fork");
		return;

	case 0:
		if (dup2(p[0], 0) == -1)
			fatal("%s: dup2", __func__);
		(void)closefrom(3);
		if ((cp = strrchr(_PATH_SENDMAIL, '/')))
			cp++;
		else
			cp = _PATH_SENDMAIL;
		execl(_PATH_SENDMAIL, cp, "-t", (char *)NULL);
		fatal("%s: execl: %s", __func__, _PATH_SENDMAIL);

	default:
		(void)close(p[0]);
		if ((fp = fdopen(p[1], "w")) == NULL) {
			(void)close(p[1]);
			log_warn("fdopen");
			return;
		}
	}

	fprintf(fp, "Auto-Submitted: auto-generated\n");
	fprintf(fp, "To: %s@%s\n", user, job->host);
	fprintf(fp, "Subject: %s printer job \"%s\"\n", lp->lp_name,
	    job->name ? job->name : "<unknown>");
	fprintf(fp, "Reply-To: root@%s\n\n", lpd_hostname);
	fprintf(fp, "Your printer job ");
	if (job->name)
		fprintf(fp, " (%s) ", job->name);

	fprintf(fp, "\n");

	switch (result) {
	case OK:
		fprintf(fp, "completed successfully");
		break;

	case ERR_ACCOUNT:
		fprintf(fp, "could not be printed without an account on %s",
		    lpd_hostname);
		break;

	case ERR_ACCESS:
		fprintf(fp, "could not be printed because the file could "
		    " not be read");
		break;

	case ERR_INODE:
		fprintf(fp, "was not printed because it was not linked to"
		    " the original file");
		break;

	case ERR_NOIMPL:
		fprintf(fp, "was not printed because some feature is missing");
		break;

	case ERR_FILTER:
		efp = fopen(prn->efile, "r");
		if (efp && fstat(fileno(efp), &st) == 0 && st.st_size) {
			fprintf(fp,
			    "had the following errors and may not have printed:\n");
			while ((c = getc(efp)) != EOF)
				putc(c, fp);
		}
		else
			fprintf(fp,
			    "had some errors and may not have printed\n");

		if (efp)
			fclose(efp);
		break;

	default:
		printf("could not be printed");
		break;
	}

	fprintf(fp, "\n");
	fclose(fp);

	wait(NULL);
}

static void
prn_open(void)
{
	const char *status, *oldstatus;
	int i;

	switch (lp->lp_type) {
	case PRN_LOCAL:
		lp_setstatus(lp, "opening %s", LP_LP(lp));
		break;

	case PRN_NET:
	case PRN_LPR:
		lp_setstatus(lp, "connecting to %s:%s", lp->lp_host,
		    lp->lp_port ? lp->lp_port : "printer");
		break;
	}

	status = oldstatus = NULL;
	for (i = 0; prn->pfd == -1; i += (i < 6) ? 1 : 0) {

		if (status != oldstatus) {
			lp_setstatus(lp, "%s", status);
			oldstatus = status;
		}

		if (i)
			sleep(1 << i);

		if ((prn->pfd = prn_connect()) == -1) {
			status = "waiting for printer to come up";
			continue;
		}

		if (lp->lp_type == PRN_LPR) {
			/* Send a recvjob request. */
			if (sendcmd("\2%s\n", LP_RP(lp))) {
				if (errno == 0)
					log_warnx("remote queue is disabled");
				(void)close(prn->pfd);
				prn->pfd = -1;
				status = "waiting for queue to be enabled";
			}
		}
	}

	switch (lp->lp_type) {
	case PRN_LOCAL:
		lp_setstatus(lp, "printing to %s", LP_LP(lp));
		break;

	case PRN_NET:
		lp_setstatus(lp, "printing to %s:%s", lp->lp_host, lp->lp_port);
		break;

	case PRN_LPR:
		lp_setstatus(lp, "sending to %s", lp->lp_host);
		break;
	}

	prn->tof = lp->lp_fo ? 0 : 1;
	prn->count = 0;

	prn_fstart();
}

/*
 * Open the printer device, or connect to the remote host.
 * Return the printer file descriptor, or -1 on error.
 */
static int
prn_connect(void)
{
	struct addrinfo hints, *res, *res0;
	int save_errno;
	int fd, e, mode;
	const char *cause = NULL, *host, *port;

	if (lp->lp_type == PRN_LOCAL) {
		mode = lp->lp_rw ? O_RDWR : O_WRONLY;
		if ((fd = open(LP_LP(lp), mode)) == -1) {
			log_warn("failed to open %s", LP_LP(lp));
			return -1;
		}

		if (isatty(fd)) {
			lp_stty(lp, fd);
			return -1;
		}

		return fd;
	}

	host = lp->lp_host;
	port = lp->lp_port ? lp->lp_port : "printer";

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if ((e = getaddrinfo(host, port, &hints, &res0))) {
		log_warnx("%s:%s: %s", host, port, gai_strerror(e));
		return -1;
	}

	fd = -1;
	for (res = res0; res && fd == -1; res = res->ai_next) {
		fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (fd == -1)
			cause = "socket";
		else if (connect(fd, res->ai_addr, res->ai_addrlen) == -1) {
			cause = "connect";
			save_errno = errno;
			(void)close(fd);
			errno = save_errno;
			fd = -1;
		}
	}

	if (fd == -1)
		log_warn("%s", cause);
	else
		log_debug("connected to %s:%s", host, port);

	freeaddrinfo(res0);
	return fd;
}

static void
prn_close(void)
{
	prn_fclose();

	(void)close(prn->pfd);
	prn->pfd = -1;
}

/*
 * Fork the output filter process if needed.
 */
static int
prn_fstart(void)
{
	char width[32], length[32], *cp;
	int fildes[2], i;

	if (lp->lp_type == PRN_LPR || (!lp->lp_of))
		return 0;

	pipe(fildes);

	for (i = 0; i < 20; i++) {
		if (i)
			sleep(i);
		if ((prn->opid = fork()) != -1)
			break;
		log_warn("%s: fork", __func__);
	}

	if (prn->opid == -1) {
		log_warnx("cannot fork output filter");
		return -1;
	}

	if (prn->opid == 0) {
		/* child */
		dup2(fildes[0], 0);
		dup2(prn->pfd, 1);
		(void)closefrom(3);
		cp = strrchr(lp->lp_of, '/');
		if (cp)
			cp += 1;
		else
			cp = lp->lp_of;
		snprintf(width, sizeof(width), "-w%ld", lp->lp_pw);
		snprintf(length, sizeof(length), "-l%ld", lp->lp_pl);
		execl(lp->lp_of, cp, width, length, (char *)NULL);
		log_warn("%s: execl", __func__);
		exit(1);
	}

	close(fildes[0]);
	prn->ofd = fildes[1];
	prn->ofilter = 1;

	return 0;
}

/*
 * Suspend the output filter process.
 */
static void
prn_fsuspend(void)
{
	pid_t pid;
	int status;

	if (prn->opid == 0)
		return;

	prn_puts("\031\1");
	while ((pid = waitpid(WAIT_ANY, &status, WUNTRACED)) && pid != prn->opid)
		;

	prn->ofilter = 0;
	if (!WIFSTOPPED(status)) {
		log_warn("output filter died (exitstatus=%d termsig=%d)",
		    WEXITSTATUS(status), WTERMSIG(status));
		prn->opid = 0;
		prn_fclose();
	}
}

/*
 * Resume the output filter process.
 */
static void
prn_fresume(void)
{
	if (prn->opid == 0)
		return;

	if (kill(prn->opid, SIGCONT) == -1)
		fatal("cannot restart output filter");
	prn->ofilter = 1;
}

/*
 * Close the output filter socket and wait for the process to terminate
 * if currently running.
 */
static void
prn_fclose(void)
{
	pid_t pid;

	close(prn->ofd);
	prn->ofd = -1;

	while (prn->opid) {
		pid = wait(NULL);
		if (pid == -1)
			log_warn("%s: wait", __func__);
		else if (pid == prn->opid)
			prn->opid = 0;
	}
}

/*
 * Write a form-feed if the printer cap requires it, and if not currently
 * at top of form. Return 0 on success, or -1 on error and set errno.
 */
static int
prn_formfeed(void)
{
	if (!lp->lp_sf && !prn->tof)
		if (prn_puts(LP_FF(lp)) == -1)
			return -1;
	prn->tof = 1;
	return 0;
}

/*
 * Write data to the printer (or output filter process).
 * Return 0 on success, or -1 and set errno.
 */
static int
prn_write(const char *buf, size_t len)
{
	ssize_t n;
	int fd;

	fd = prn->ofilter ? prn->ofd : prn->pfd;

	log_debug("prn_write(fd=%d len=%zu, of=%d pfd=%d ofd=%d)", fd, len,
	    prn->ofilter, prn->pfd, prn->ofd);

	if (fd == -1) {
		log_warnx("printer socket not opened");
		errno = EPIPE;
		return -1;
	}

	while (len) {
		if ((n = write(fd, buf, len)) == -1) {
			if (errno == EINTR)
				continue;
			log_warn("%s: write", __func__);
			/* XXX close the printer */
			return -1;
		}
		len -= n;
		buf += n;
		prn->tof = 0;
	}

	return 0;
}

/*
 * Write a string to the printer (or output filter process).
 * Return 0 on success, or -1 and set errno.
 */
static int
prn_puts(const char *buf)
{
	return prn_write(buf, strlen(buf));
}

/*
 * Write the FILE content to the printer (or output filter process).
 * Return 0 on success, or -1 and set errno.
 */
static int
prn_writefile(FILE *fp)
{
	char buf[BUFSIZ];
	size_t r;

	while (!feof(fp)) {
		r = fread(buf, 1, sizeof(buf), fp);
		if (ferror(fp)) {
			log_warn("%s: fread", __func__);
			return -1;
		}
		if (r && (prn_write(buf, r) == -1))
			return -1;
	}

	return 0;
}

/*
 * Read data from the printer socket into the given buffer.
 * Return 0 on success, or -1 and set errno.
 */
static ssize_t
prn_read(char *buf, size_t sz)
{
	ssize_t n;

	for (;;) {
		if ((n = read(prn->pfd, buf, sz)) == 0) {
			errno = ECONNRESET;
			n = -1;
		}
		if (n == -1) {
			if (errno == EINTR)
				continue;
			/* XXX close printer? */
			log_warn("%s: read", __func__);
			return -1;
		}
		return n;
	}
}
