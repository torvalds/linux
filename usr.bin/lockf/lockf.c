/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 1997 John D. Polstra.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY JOHN D. POLSTRA AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL JOHN D. POLSTRA OR CONTRIBUTORS BE LIABLE
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
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>

static int acquire_lock(const char *name, int flags);
static void cleanup(void);
static void killed(int sig);
static void timeout(int sig);
static void usage(void);
static void wait_for_lock(const char *name);

static const char *lockname;
static int lockfd = -1;
static int keep;
static volatile sig_atomic_t timed_out;

/*
 * Execute an arbitrary command while holding a file lock.
 */
int
main(int argc, char **argv)
{
	int ch, flags, silent, status, waitsec;
	pid_t child;

	silent = keep = 0;
	flags = O_CREAT;
	waitsec = -1;	/* Infinite. */
	while ((ch = getopt(argc, argv, "sknt:")) != -1) {
		switch (ch) {
		case 'k':
			keep = 1;
			break;
		case 'n':
			flags &= ~O_CREAT;
			break;
		case 's':
			silent = 1;
			break;
		case 't':
		{
			char *endptr;
			waitsec = strtol(optarg, &endptr, 0);
			if (*optarg == '\0' || *endptr != '\0' || waitsec < 0)
				errx(EX_USAGE,
				    "invalid timeout \"%s\"", optarg);
		}
			break;
		default:
			usage();
		}
	}
	if (argc - optind < 2)
		usage();
	lockname = argv[optind++];
	argc -= optind;
	argv += optind;
	if (waitsec > 0) {		/* Set up a timeout. */
		struct sigaction act;

		act.sa_handler = timeout;
		sigemptyset(&act.sa_mask);
		act.sa_flags = 0;	/* Note that we do not set SA_RESTART. */
		sigaction(SIGALRM, &act, NULL);
		alarm(waitsec);
	}
	/*
	 * If the "-k" option is not given, then we must not block when
	 * acquiring the lock.  If we did, then the lock holder would
	 * unlink the file upon releasing the lock, and we would acquire
	 * a lock on a file with no directory entry.  Then another
	 * process could come along and acquire the same lock.  To avoid
	 * this problem, we separate out the actions of waiting for the
	 * lock to be available and of actually acquiring the lock.
	 *
	 * That approach produces behavior that is technically correct;
	 * however, it causes some performance & ordering problems for
	 * locks that have a lot of contention.  First, it is unfair in
	 * the sense that a released lock isn't necessarily granted to
	 * the process that has been waiting the longest.  A waiter may
	 * be starved out indefinitely.  Second, it creates a thundering
	 * herd situation each time the lock is released.
	 *
	 * When the "-k" option is used, the unlink race no longer
	 * exists.  In that case we can block while acquiring the lock,
	 * avoiding the separate step of waiting for the lock.  This
	 * yields fairness and improved performance.
	 */
	lockfd = acquire_lock(lockname, flags | O_NONBLOCK);
	while (lockfd == -1 && !timed_out && waitsec != 0) {
		if (keep)
			lockfd = acquire_lock(lockname, flags);
		else {
			wait_for_lock(lockname);
			lockfd = acquire_lock(lockname, flags | O_NONBLOCK);
		}
	}
	if (waitsec > 0)
		alarm(0);
	if (lockfd == -1) {		/* We failed to acquire the lock. */
		if (silent)
			exit(EX_TEMPFAIL);
		errx(EX_TEMPFAIL, "%s: already locked", lockname);
	}
	/* At this point, we own the lock. */
	if (atexit(cleanup) == -1)
		err(EX_OSERR, "atexit failed");
	if ((child = fork()) == -1)
		err(EX_OSERR, "cannot fork");
	if (child == 0) {	/* The child process. */
		close(lockfd);
		execvp(argv[0], argv);
		warn("%s", argv[0]);
		_exit(1);
	}
	/* This is the parent process. */
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGTERM, killed);
	if (waitpid(child, &status, 0) == -1)
		err(EX_OSERR, "waitpid failed");
	return (WIFEXITED(status) ? WEXITSTATUS(status) : EX_SOFTWARE);
}

/*
 * Try to acquire a lock on the given file, creating the file if
 * necessary.  The flags argument is O_NONBLOCK or 0, depending on
 * whether we should wait for the lock.  Returns an open file descriptor
 * on success, or -1 on failure.
 */
static int
acquire_lock(const char *name, int flags)
{
	int fd;

	if ((fd = open(name, O_RDONLY|O_EXLOCK|flags, 0666)) == -1) {
		if (errno == EAGAIN || errno == EINTR)
			return (-1);
		else if (errno == ENOENT && (flags & O_CREAT) == 0)
			err(EX_UNAVAILABLE, "%s", name);
		err(EX_CANTCREAT, "cannot open %s", name);
	}
	return (fd);
}

/*
 * Remove the lock file.
 */
static void
cleanup(void)
{

	if (keep)
		flock(lockfd, LOCK_UN);
	else
		unlink(lockname);
}

/*
 * Signal handler for SIGTERM.  Cleans up the lock file, then re-raises
 * the signal.
 */
static void
killed(int sig)
{

	cleanup();
	signal(sig, SIG_DFL);
	if (kill(getpid(), sig) == -1)
		err(EX_OSERR, "kill failed");
}

/*
 * Signal handler for SIGALRM.
 */
static void
timeout(int sig __unused)
{

	timed_out = 1;
}

static void
usage(void)
{

	fprintf(stderr,
	    "usage: lockf [-kns] [-t seconds] file command [arguments]\n");
	exit(EX_USAGE);
}

/*
 * Wait until it might be possible to acquire a lock on the given file.
 * If the file does not exist, return immediately without creating it.
 */
static void
wait_for_lock(const char *name)
{
	int fd;

	if ((fd = open(name, O_RDONLY|O_EXLOCK, 0666)) == -1) {
		if (errno == ENOENT || errno == EINTR)
			return;
		err(EX_CANTCREAT, "cannot open %s", name);
	}
	close(fd);
}
