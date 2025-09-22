/*
 * Copyright (c) 1989 Jan-Simon Pendry
 * Copyright (c) 1989 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
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
 *	from: @(#)amd.c	8.1 (Berkeley) 6/6/93
 *	$Id: amd.c,v 1.25 2023/07/05 18:45:14 guenther Exp $
 */

/*
 * Automounter
 */

#include "am.h"
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <setjmp.h>
#include <endian.h>

#include <rpc/rpc.h>
#include <rpcsvc/ypclnt.h>
#include <rpcsvc/yp_prot.h>

#if BYTE_ORDER == LITTLE_ENDIAN
#define ARCH_ENDIAN "little"
#elif BYTE_ORDER == BIG_ENDIAN
#define ARCH_ENDIAN "big"
#else
#error "unknown endian"
#endif

char pid_fsname[16 + HOST_NAME_MAX+1];	/* "kiska.southseas.nz:(pid%d)" */
#ifdef HAS_HOST
#ifdef HOST_EXEC
char *host_helper;
#endif /* HOST_EXEC */
#endif /* HAS_HOST */
char *auto_dir = "/tmp_mnt";
char *hostdomain = "unknown.domain";
char hostname[HOST_NAME_MAX+1] = "localhost"; /* Hostname */
char hostd[2*(HOST_NAME_MAX+1)];	/* Host+domain */
char *op_sys = "bsd44";			/* Name of current op_sys */
char *arch = ARCH_REP;			/* Name of current architecture */
char *endian = ARCH_ENDIAN;		/* Big or Little endian */
char *wire;
int foreground = 1;			/* This is the top-level server */
pid_t mypid;				/* Current process id */
volatile sig_atomic_t immediate_abort;	/* Should close-down unmounts be retried */
struct in_addr myipaddr;		/* (An) IP address of this host */
serv_state amd_state;
struct amd_stats amd_stats;		/* Server statistics */
time_t do_mapc_reload = 0;		/* mapc_reload() call required? */
jmp_buf select_intr;
int select_intr_valid;
int orig_umask;

/*
 * Signal handler:
 * SIGINT - tells amd to do a full shutdown, including unmounting all filesystem.
 * SIGTERM - tells amd to shutdown now.  Just unmounts the automount nodes.
 */
static void
sigterm(int sig)
{

	switch (sig) {
	case SIGINT:
		immediate_abort = 15;
		break;

	case SIGTERM:
		immediate_abort = -1;
		/* fall through... */

	default:
		plog(XLOG_WARNING, "WARNING: automounter going down on signal %d", sig);
		break;
	}
	if (select_intr_valid)
		longjmp(select_intr, sig);
}

/*
 * Hook for cache reload.
 * When a SIGHUP arrives it schedules a call to mapc_reload
 */
static void
sighup(int sig)
{

#ifdef DEBUG
	if (sig != SIGHUP)
		dlog("spurious call to sighup");
#endif /* DEBUG */
	/*
	 * Force a reload by zero'ing the timer
	 */
	if (amd_state == Run)
		do_mapc_reload = 0;
}

static void
parent_exit(int sig)
{
	_exit(0);
}

static pid_t
daemon_mode(void)
{
	pid_t bgpid;

	signal(SIGQUIT, parent_exit);
	bgpid = background();

	if (bgpid != 0) {
		if (print_pid) {
			printf("%ld\n", (long)bgpid);
			fflush(stdout);
		}
		/*
		 * Now wait for the automount points to
		 * complete.
		 */
		for (;;)
			pause();
	}

	signal(SIGQUIT, SIG_DFL);

	/*
	 * Pretend we are in the foreground again
	 */
	foreground = 1;

#ifdef TIOCNOTTY
	{
		int t = open("/dev/tty", O_RDWR);
		if (t == -1) {
			if (errno != ENXIO)	/* not an error if already no controlling tty */
				plog(XLOG_WARNING, "Could not open controlling tty: %m");
		} else {
			if (ioctl(t, TIOCNOTTY, 0) == -1 && errno != ENOTTY)
				plog(XLOG_WARNING, "Could not disassociate tty (TIOCNOTTY): %m");
			(void) close(t);
		}
	}
#else
	(void) setpgrp();
#endif /* TIOCNOTTY */

	return getppid();
}

int
main(int argc, char *argv[])
{
	char *domdot;
	pid_t ppid = 0;
	int error;

	logfp = stderr;		/* Log errors to stderr initially */

	/*
	 * Make sure some built-in assumptions are true before we start
	 */
	assert(sizeof(nfscookie) >= sizeof (unsigned int));
	assert(sizeof(int) >= 4);

	/*
	 * Set processing status.
	 */
	amd_state = Start;

	/*
	 * Initialise process id.  This is kept
	 * cached since it is used for generating
	 * and using file handles.
	 */
	mypid = getpid();

	/*
	 * Get local machine name
	 */
	if (gethostname(hostname, sizeof(hostname)) == -1) {
		plog(XLOG_FATAL, "gethostname: %m");
		going_down(1);
	}
	/*
	 * Check it makes sense
	 */
	if (!*hostname) {
		plog(XLOG_FATAL, "host name is not set");
		going_down(1);
	}
	/*
	 * Partially initialise hostd[].  This
	 * is completed in get_args().
	 */
	if ((domdot = strchr(hostname, '.'))) {
		/*
		 * Hostname already contains domainname.
		 * Split out hostname and domainname
		 * components
		 */
		*domdot++ = '\0';
		hostdomain = domdot;
	}
	strlcpy(hostd, hostname, sizeof hostd);

	/*
	 * Trap interrupts for shutdowns.
	 */
	(void) signal(SIGINT, sigterm);

	/*
	 * Hangups tell us to reload the cache
	 */
	(void) signal(SIGHUP, sighup);

	/*
	 * Trap Terminate so that we can shutdown gracefully (some chance)
	 */
	(void) signal(SIGTERM, sigterm);
	/*
	 * Trap Death-of-a-child.  These allow us to
	 * pick up the exit status of backgrounded mounts.
	 * See "sched.c".
	 */
	(void) signal(SIGCHLD, sigchld);

	/*
	 * Fix-up any umask problems.  Most systems default
	 * to 002 which is not too convenient for our purposes
	 */
	orig_umask = umask(0);

	/*
	 * Figure out primary network name
	 */
	wire = getwire();

	/*
	 * Determine command-line arguments
	 */
	get_args(argc, argv);

	if (mkdir(auto_dir, 0755) == -1) {
		if (errno != EEXIST)
			plog(XLOG_FATAL, "mkdir(autodir = %s: %m", auto_dir);
	}

	/*
	 * Get our own IP address so that we
	 * can mount the automounter.
	 */
	{ struct sockaddr_in sin;
	  get_myaddress(&sin);
	  myipaddr.s_addr = sin.sin_addr.s_addr;
	}

	/*
	 * Now check we are root.
	 */
	if (geteuid() != 0) {
		plog(XLOG_FATAL, "Must be root to mount filesystems (euid = %u)", geteuid());
		going_down(1);
	}

	/*
	 * If the domain was specified then bind it here
	 * to circumvent any default bindings that may
	 * be done in the C library.
	 */
	if (domain && yp_bind(domain)) {
		plog(XLOG_FATAL, "Can't bind to domain \"%s\"", domain);
		going_down(1);
	}

#ifdef DEBUG
	Debug(D_DAEMON)
#endif /* DEBUG */
	ppid = daemon_mode();

	snprintf(pid_fsname, sizeof(pid_fsname), "%s:(pid%ld)", hostname, (long)mypid);

	do_mapc_reload = clocktime() + ONE_HOUR;

	/*
	 * Register automounter with system
	 */
	error = mount_automounter(ppid);
	if (error && ppid)
		kill(ppid, SIGALRM);
	going_down(error);

	abort();
}
