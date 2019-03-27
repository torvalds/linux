/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999 Berkeley Software Design, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Berkeley Software Design Inc's name may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN INC BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	From BSDI: daemon.c,v 1.2 1996/08/15 01:11:09 jch Exp
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <libutil.h>
#include <login_cap.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#define SYSLOG_NAMES
#include <syslog.h>
#include <time.h>
#include <assert.h>

#define LBUF_SIZE 4096

struct log_params {
	int dosyslog;
	int logpri;
	int noclose;
	int outfd;
};

static void restrict_process(const char *);
static void handle_term(int);
static void handle_chld(int);
static int  listen_child(int, struct log_params *);
static int  get_log_mapping(const char *, const CODE *);
static void open_pid_files(const char *, const char *, struct pidfh **,
			   struct pidfh **);
static void do_output(const unsigned char *, size_t, struct log_params *);
static void daemon_sleep(time_t, long);
static void usage(void);

static volatile sig_atomic_t terminate = 0, child_gone = 0, pid = 0;

int
main(int argc, char *argv[])
{
	const char *pidfile, *ppidfile, *title, *user, *outfn, *logtag;
	int ch, nochdir, noclose, restart, dosyslog, child_eof;
	sigset_t mask_susp, mask_orig, mask_read, mask_term;
	struct log_params logpar;
	int pfd[2] = { -1, -1 }, outfd = -1;
	int stdmask, logpri, logfac;
	struct pidfh *ppfh, *pfh;
	char *p;

	memset(&logpar, 0, sizeof(logpar));
	stdmask = STDOUT_FILENO | STDERR_FILENO;
	ppidfile = pidfile = user = NULL;
	nochdir = noclose = 1;
	logpri = LOG_NOTICE;
	logfac = LOG_DAEMON;
	logtag = "daemon";
	restart = 0;
	dosyslog = 0;
	outfn = NULL;
	title = NULL;
	while ((ch = getopt(argc, argv, "cfSp:P:ru:o:s:l:t:l:m:R:T:")) != -1) {
		switch (ch) {
		case 'c':
			nochdir = 0;
			break;
		case 'f':
			noclose = 0;
			break;
		case 'l':
			logfac = get_log_mapping(optarg, facilitynames);
			if (logfac == -1)
				errx(5, "unrecognized syslog facility");
			dosyslog = 1;
			break;
		case 'm':
			stdmask = strtol(optarg, &p, 10);
			if (p == optarg || stdmask < 0 || stdmask > 3)
				errx(6, "unrecognized listening mask");
			break;
		case 'o':
			outfn = optarg;
			break;
		case 'p':
			pidfile = optarg;
			break;
		case 'P':
			ppidfile = optarg;
			break;
		case 'r':
			restart = 1;
			break;
		case 'R':
			restart = strtol(optarg, &p, 0);
			if (p == optarg || restart < 1)
				errx(6, "invalid restart delay");
			break;
		case 's':
			logpri = get_log_mapping(optarg, prioritynames);
			if (logpri == -1)
				errx(4, "unrecognized syslog priority");
			dosyslog = 1;
			break;
		case 'S':
			dosyslog = 1;
			break;
		case 't':
			title = optarg;
			break;
		case 'T':
			logtag = optarg;
			dosyslog = 1;
			break;
		case 'u':
			user = optarg;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0)
		usage();

	if (!title)
		title = argv[0];

	if (outfn) {
		outfd = open(outfn, O_CREAT | O_WRONLY | O_APPEND | O_CLOEXEC, 0600);
		if (outfd == -1)
			err(7, "open");
	}
	
	if (dosyslog)
		openlog(logtag, LOG_PID | LOG_NDELAY, logfac);

	ppfh = pfh = NULL;
	/*
	 * Try to open the pidfile before calling daemon(3),
	 * to be able to report the error intelligently
	 */
	open_pid_files(pidfile, ppidfile, &pfh, &ppfh);
	if (daemon(nochdir, noclose) == -1) {
		warn("daemon");
		goto exit;
	}
	/* Write out parent pidfile if needed. */
	pidfile_write(ppfh);
	/*
	 * If the pidfile or restart option is specified the daemon
	 * executes the command in a forked process and wait on child
	 * exit to remove the pidfile or restart the command. Normally
	 * we don't want the monitoring daemon to be terminated
	 * leaving the running process and the stale pidfile, so we
	 * catch SIGTERM and forward it to the children expecting to
	 * get SIGCHLD eventually. We also must fork() to obtain a
	 * readable pipe with the child for writing to a log file
	 * and syslog.
	 */
	pid = -1;
	if (pidfile || ppidfile || restart || outfd != -1 || dosyslog) {
		struct sigaction act_term, act_chld;

		/* Avoid PID racing with SIGCHLD and SIGTERM. */
		memset(&act_term, 0, sizeof(act_term));
		act_term.sa_handler = handle_term;
		sigemptyset(&act_term.sa_mask);
		sigaddset(&act_term.sa_mask, SIGCHLD);

		memset(&act_chld, 0, sizeof(act_chld));
		act_chld.sa_handler = handle_chld;
		sigemptyset(&act_chld.sa_mask);
		sigaddset(&act_chld.sa_mask, SIGTERM);

		/* Block these when avoiding racing before sigsuspend(). */
		sigemptyset(&mask_susp);
		sigaddset(&mask_susp, SIGTERM);
		sigaddset(&mask_susp, SIGCHLD);
		/* Block SIGTERM when we lack a valid child PID. */
		sigemptyset(&mask_term);
		sigaddset(&mask_term, SIGTERM);
		/*
		 * When reading, we wish to avoid SIGCHLD. SIGTERM
		 * has to be caught, otherwise we'll be stuck until
		 * the read() returns - if it returns.
		 */
		sigemptyset(&mask_read);
		sigaddset(&mask_read, SIGCHLD);
		/* Block SIGTERM to avoid racing until we have forked. */
		if (sigprocmask(SIG_BLOCK, &mask_term, &mask_orig)) {
			warn("sigprocmask");
			goto exit;
		}
		if (sigaction(SIGTERM, &act_term, NULL) == -1) {
			warn("sigaction");
			goto exit;
		}
		if (sigaction(SIGCHLD, &act_chld, NULL) == -1) {
			warn("sigaction");
			goto exit;
		}
		/*
		 * Try to protect against pageout kill. Ignore the
		 * error, madvise(2) will fail only if a process does
		 * not have superuser privileges.
		 */
		(void)madvise(NULL, 0, MADV_PROTECT);
		logpar.outfd = outfd;
		logpar.dosyslog = dosyslog;
		logpar.logpri = logpri;
		logpar.noclose = noclose;
restart:
		if (pipe(pfd))
			err(1, "pipe");
		/*
		 * Spawn a child to exec the command.
		 */
		child_gone = 0;
		pid = fork();
		if (pid == -1) {
			warn("fork");
			goto exit;
		} else if (pid > 0) {
			/*
			 * Unblock SIGTERM after we know we have a valid
			 * child PID to signal.
			 */
			if (sigprocmask(SIG_UNBLOCK, &mask_term, NULL)) {
				warn("sigprocmask");
				goto exit;
			}
			close(pfd[1]);
			pfd[1] = -1;
		}
	}
	if (pid <= 0) {
		/* Now that we are the child, write out the pid. */
		pidfile_write(pfh);

		if (user != NULL)
			restrict_process(user);
		/*
		 * When forking, the child gets the original sigmask,
		 * and dup'd pipes.
		 */
		if (pid == 0) {
			close(pfd[0]);
			if (sigprocmask(SIG_SETMASK, &mask_orig, NULL))
				err(1, "sigprogmask");
			if (stdmask & STDERR_FILENO) {
				if (dup2(pfd[1], STDERR_FILENO) == -1)
					err(1, "dup2");
			}
			if (stdmask & STDOUT_FILENO) {
				if (dup2(pfd[1], STDOUT_FILENO) == -1)
					err(1, "dup2");
			}
			if (pfd[1] != STDERR_FILENO &&
			    pfd[1] != STDOUT_FILENO)
				close(pfd[1]);
		}
		execvp(argv[0], argv);
		/*
		 * execvp() failed -- report the error. The child is
		 * now running, so the exit status doesn't matter.
		 */
		err(1, "%s", argv[0]);
	}
	setproctitle("%s[%d]", title, (int)pid);
	/*
	 * As we have closed the write end of pipe for parent process,
	 * we might detect the child's exit by reading EOF. The child
	 * might have closed its stdout and stderr, so we must wait for
	 * the SIGCHLD to ensure that the process is actually gone.
	 */
	child_eof = 0;
	for (;;) {
		/*
		 * We block SIGCHLD when listening, but SIGTERM we accept
		 * so the read() won't block if we wish to depart.
		 *
		 * Upon receiving SIGTERM, we have several options after
		 * sending the SIGTERM to our child:
		 * - read until EOF
		 * - read until EOF but only for a while
		 * - bail immediately
		 *
		 * We go for the third, as otherwise we have no guarantee
		 * that we won't block indefinitely if the child refuses
		 * to depart. To handle the second option, a different
		 * approach would be needed (procctl()?)
		 */
		if (child_gone && child_eof) {
			break;
		} else if (terminate) {
			goto exit;
		} else if (!child_eof) {
			if (sigprocmask(SIG_BLOCK, &mask_read, NULL)) {
				warn("sigprocmask");
				goto exit;
			}
			child_eof = !listen_child(pfd[0], &logpar);
			if (sigprocmask(SIG_UNBLOCK, &mask_read, NULL)) {
				warn("sigprocmask");
				goto exit;
			}
		} else {
			if (sigprocmask(SIG_BLOCK, &mask_susp, NULL)) {
				warn("sigprocmask");
	 			goto exit;
			}
			while (!terminate && !child_gone)
				sigsuspend(&mask_orig);
			if (sigprocmask(SIG_UNBLOCK, &mask_susp, NULL)) {
				warn("sigprocmask");
				goto exit;
			}
		}
	}
	if (sigprocmask(SIG_BLOCK, &mask_term, NULL)) {
		warn("sigprocmask");
		goto exit;
	}
	if (restart && !terminate) {
		daemon_sleep(restart, 0);
		close(pfd[0]);
		pfd[0] = -1;
		goto restart;
	}
exit:
	close(outfd);
	close(pfd[0]);
	close(pfd[1]);
	if (dosyslog)
		closelog();
	pidfile_remove(pfh);
	pidfile_remove(ppfh);
	exit(1); /* If daemon(3) succeeded exit status does not matter. */
}

static void
daemon_sleep(time_t secs, long nsecs)
{
	struct timespec ts = { secs, nsecs };
	while (nanosleep(&ts, &ts) == -1) {
		if (errno != EINTR)
			err(1, "nanosleep");
	}
}

static void
open_pid_files(const char *pidfile, const char *ppidfile,
	       struct pidfh **pfh, struct pidfh **ppfh)
{
	pid_t fpid;
	int serrno;

	if (pidfile) {
		*pfh = pidfile_open(pidfile, 0600, &fpid);
		if (*pfh == NULL) {
			if (errno == EEXIST) {
				errx(3, "process already running, pid: %d",
				    fpid);
			}
			err(2, "pidfile ``%s''", pidfile);
		}
	}
	/* Do the same for the actual daemon process. */
	if (ppidfile) {
		*ppfh = pidfile_open(ppidfile, 0600, &fpid);
		if (*ppfh == NULL) {
			serrno = errno;
			pidfile_remove(*pfh);
			errno = serrno;
			if (errno == EEXIST) {
				errx(3, "process already running, pid: %d",
				     fpid);
			}
			err(2, "ppidfile ``%s''", ppidfile);
		}
	}
}

static int
get_log_mapping(const char *str, const CODE *c)
{
	const CODE *cp;
	for (cp = c; cp->c_name; cp++)
		if (strcmp(cp->c_name, str) == 0)
			return cp->c_val;
	return -1;
}

static void
restrict_process(const char *user)
{
	struct passwd *pw = NULL;

	pw = getpwnam(user);
	if (pw == NULL)
		errx(1, "unknown user: %s", user);

	if (setusercontext(NULL, pw, pw->pw_uid, LOGIN_SETALL) != 0)
		errx(1, "failed to set user environment");
}

/*
 * We try to collect whole lines terminated by '\n'. Otherwise we collect a
 * full buffer, and then output it.
 *
 * Return value of 0 is assumed to mean EOF or error, and 1 indicates to
 * continue reading.
 */
static int
listen_child(int fd, struct log_params *logpar)
{
	static unsigned char buf[LBUF_SIZE];
	static size_t bytes_read = 0;
	int rv;

	assert(logpar);
	assert(bytes_read < LBUF_SIZE - 1);

	rv = read(fd, buf + bytes_read, LBUF_SIZE - bytes_read - 1);
	if (rv > 0) {
		unsigned char *cp;

		bytes_read += rv;
		assert(bytes_read <= LBUF_SIZE - 1);
		/* Always NUL-terminate just in case. */
		buf[LBUF_SIZE - 1] = '\0';
		/*
		 * Chomp line by line until we run out of buffer.
		 * This does not take NUL characters into account.
		 */
		while ((cp = memchr(buf, '\n', bytes_read)) != NULL) {
			size_t bytes_line = cp - buf + 1;
			assert(bytes_line <= bytes_read);
			do_output(buf, bytes_line, logpar);
			bytes_read -= bytes_line;
			memmove(buf, cp + 1, bytes_read);
		}
		/* Wait until the buffer is full. */
		if (bytes_read < LBUF_SIZE - 1)
			return 1;
		do_output(buf, bytes_read, logpar);
		bytes_read = 0;
		return 1;
	} else if (rv == -1) {
		/* EINTR should trigger another read. */
		if (errno == EINTR) {
			return 1;
		} else {
			warn("read");
			return 0;
		}
	}
	/* Upon EOF, we have to flush what's left of the buffer. */
	if (bytes_read > 0) {
		do_output(buf, bytes_read, logpar);
		bytes_read = 0;
	}
	return 0;
}

/*
 * The default behavior is to stay silent if the user wants to redirect
 * output to a file and/or syslog. If neither are provided, then we bounce
 * everything back to parent's stdout.
 */
static void
do_output(const unsigned char *buf, size_t len, struct log_params *logpar)
{
	assert(len <= LBUF_SIZE);
	assert(logpar);

	if (len < 1)
		return;
	if (logpar->dosyslog)
		syslog(logpar->logpri, "%.*s", (int)len, buf);
	if (logpar->outfd != -1) {
		if (write(logpar->outfd, buf, len) == -1)
			warn("write");
	}
	if (logpar->noclose && !logpar->dosyslog && logpar->outfd == -1)
		printf("%.*s", (int)len, buf);
}

/*
 * We use the global PID acquired directly from fork. If there is no valid
 * child pid, the handler should be blocked and/or child_gone == 1.
 */
static void
handle_term(int signo)
{
	if (pid > 0 && !child_gone)
		kill(pid, signo);
	terminate = 1;
}

static void
handle_chld(int signo)
{
	(void)signo;
	for (;;) {
		int rv = waitpid(-1, NULL, WNOHANG);
		if (pid == rv) {
			child_gone = 1;
			break;
		} else if (rv == -1 && errno != EINTR) {
			warn("waitpid");
			return;
		}
	}
}

static void
usage(void)
{
	(void)fprintf(stderr,
	    "usage: daemon [-cfrS] [-p child_pidfile] [-P supervisor_pidfile]\n"
	    "              [-u user] [-o output_file] [-t title]\n"
	    "              [-l syslog_facility] [-s syslog_priority]\n"
	    "              [-T syslog_tag] [-m output_mask] [-R restart_delay_secs]\n"
	    "command arguments ...\n");
	exit(1);
}
