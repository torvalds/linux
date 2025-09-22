/*
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990, 1993
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
 *	from: @(#)nfs_start.c	8.1 (Berkeley) 6/6/93
 *	$Id: nfs_start.c,v 1.20 2015/08/20 22:46:32 deraadt Exp $
 */

#include "am.h"
#include "amq.h"
#include <signal.h>
#include <unistd.h>
#include <setjmp.h>

extern jmp_buf select_intr;
extern int select_intr_valid;

#ifdef HAS_TFS
/*
 * Use replacement for RPC/UDP transport
 * so that we do NFS gatewaying.
 */
#define	svcudp_create svcudp2_create
extern SVCXPRT *svcudp2_create(int);
#endif /* HAS_TFS */

unsigned short nfs_port;
SVCXPRT *nfsxprt, *lnfsxprt;
SVCXPRT *amqp, *lamqp;

extern int fwd_sock;
int max_fds = -1;

#ifdef DEBUG
/*
 * Check that we are not burning resources
 */
static void
checkup(void)
{
	static int max_fd = 0;
	static char *max_mem = 0;

	int next_fd = dup(0);
	extern caddr_t sbrk(int);
	caddr_t next_mem = sbrk(0);
	close(next_fd);

	/*if (max_fd < 0) {
		max_fd = next_fd;
	} else*/ if (max_fd < next_fd) {
		dlog("%d new fds allocated; total is %d",
			next_fd - max_fd, next_fd);
		max_fd = next_fd;
	}

	/*if (max_mem == 0) {
		max_mem = next_mem;
	} else*/ if (max_mem < next_mem) {
		dlog("%#lx bytes of memory allocated; total is %#lx (%ld pages)",
			(unsigned long)(next_mem - max_mem),
			(unsigned long)next_mem,
			((unsigned long)next_mem+getpagesize()-1)/getpagesize());
		max_mem = next_mem;
	}
}
#endif /* DEBUG */

static int
do_select(sigset_t *mask, sigset_t *omask, int fds, fd_set *fdp,
    struct timeval *tvp)
{
	int sig;
	int nsel;

	if ((sig = setjmp(select_intr))) {
		select_intr_valid = 0;
		/* Got a signal */
		switch (sig) {
		case SIGINT:
		case SIGTERM:
			amd_state = Finishing;
			reschedule_timeout_mp();
			break;
		}
		nsel = -1;
		errno = EINTR;
	} else {
		select_intr_valid = 1;
		/*
		 * Invalidate the current clock value
		 */
		clock_valid = 0;
		/*
		 * Allow interrupts.  If a signal
		 * occurs, then it will cause a longjmp
		 * up above.
		 */
		sigprocmask(SIG_SETMASK, omask, NULL);
		/*
		 * Wait for input
		 */
		nsel = select(fds, fdp, NULL, NULL,
		    tvp->tv_sec ? tvp : (struct timeval *) 0);

	}

	sigprocmask(SIG_BLOCK, mask, NULL);

	/*
	 * Perhaps reload the cache?
	 */
	if (do_mapc_reload < clocktime()) {
		mapc_reload();
		do_mapc_reload = clocktime() + ONE_HOUR;
	}
	return nsel;
}

/*
 * Determine whether anything is left in
 * the RPC input queue.
 */
static int
rpc_pending_now(void)
{
	struct timeval tvv;
	int nsel;
	fd_set *fdsp;
	int fdsn;

	fdsn = howmany(max_fds+1, NFDBITS) * sizeof(fd_mask);
	if ((fdsp = malloc(fdsn)) == NULL)
		return(0);
	memset(fdsp, 0, fdsn);
	FD_SET(fwd_sock, fdsp);

	tvv.tv_sec = tvv.tv_usec = 0;
	nsel = select(max_fds+1, fdsp, NULL, NULL, &tvv);
	if (nsel < 1) {
		free(fdsp);
		return(0);
	}
	if (FD_ISSET(fwd_sock, fdsp)) {
		free(fdsp);
		return(1);
	}
	free(fdsp);
	return(0);
}

static serv_state
run_rpc(void)
{
	sigset_t mask, omask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGCHLD);
	sigaddset(&mask, SIGHUP);
	sigprocmask(SIG_BLOCK, &mask, &omask);

	next_softclock = clocktime();

	amd_state = Run;

	/*
	 * Keep on trucking while we are in Run mode.  This state
	 * is switched to Quit after all the file systems have
	 * been unmounted.
	 */
	while ((int)amd_state <= (int)Finishing) {
		struct timeval tvv;
		int nsel;
		time_t now;
#ifdef __OpenBSD__
		extern int __svc_fdsetsize;
		extern fd_set *__svc_fdset;
		fd_set *fdsp;
		int fdsn = __svc_fdsetsize;
		int bytes;

		if (fwd_sock > fdsn)
			fdsn = fwd_sock;
		bytes = howmany(fdsn, NFDBITS) * sizeof(fd_mask);

		fdsp = malloc(bytes);
		memset(fdsp, 0, bytes);
		memcpy(fdsp, __svc_fdset, bytes);
		FD_SET(fwd_sock, fdsp);
#else
		fd_set *fdsp;
		int fdsn = FDSETSIZE;
		bytes = howmany(fdsn, NFDBITS) * sizeof(fd_mask);
		fdsp = malloc(bytes);
		memcpy(fdsp, &svc_fdset, bytes);
		FD_SET(fwd_sock, fdsp);
#endif

#ifdef DEBUG
		checkup();
#endif /* DEBUG */

		/*
		 * If the full timeout code is not called,
		 * then recompute the time delta manually.
		 */
		now = clocktime();

		if (next_softclock <= now) {
			if (amd_state == Finishing)
				umount_exported();
			tvv.tv_sec = softclock();
		} else {
			tvv.tv_sec = next_softclock - now;
		}
		tvv.tv_usec = 0;

		if (amd_state == Finishing && last_used_map < 0) {
			flush_mntfs();
			amd_state = Quit;
			break;
		}

#ifdef DEBUG
		if (tvv.tv_sec)
			dlog("Select waits for %llds", (long long)tvv.tv_sec);
		else
			dlog("Select waits for Godot");
#endif /* DEBUG */

		nsel = do_select(&mask, &omask, fdsn + 1, fdsp, &tvv);


		switch (nsel) {
		case -1:
			if (errno == EINTR) {
#ifdef DEBUG
				dlog("select interrupted");
#endif /* DEBUG */
				continue;
			}
			perror("select");
			break;

		case 0:
#ifdef DEBUG
			/*dlog("select returned 0");*/
#endif /* DEBUG */
			break;

		default:
			/* Read all pending NFS responses at once to avoid
			   having responses queue up as a consequence of
			   retransmissions. */
			if (FD_ISSET(fwd_sock, fdsp)) {
				FD_CLR(fwd_sock, fdsp);
				--nsel;
				do {
					fwd_reply();
				} while (rpc_pending_now() > 0);
			}

			if (nsel) {
				/*
				 * Anything left must be a normal
				 * RPC request.
				 */
#ifdef __OpenBSD__
				svc_getreqset2(fdsp, fdsn);
#else
				svc_getreqset(fdsp);
#endif
			}
			break;
		}
		free(fdsp);
	}

	sigprocmask(SIG_SETMASK, &omask, NULL);

	if (amd_state == Quit)
		amd_state = Done;

	return amd_state;
}

static int
bindnfs_port(int so)
{
	unsigned short port;
	int error = bind_resv_port(so, &port);
	if (error == 0)
		nfs_port = port;
	return error;
}

void
unregister_amq(void)
{
#ifdef DEBUG
	Debug(D_AMQ)
#endif /* DEBUG */
	(void) pmap_unset(AMQ_PROGRAM, AMQ_VERSION);
}

int
mount_automounter(pid_t ppid)
{
	struct sockaddr_in sin;
	int so, so2, nmount;
	int sinlen;
	int on = 1;

	so = socket(AF_INET, SOCK_DGRAM, 0);

	if (so < 0 || bindnfs_port(so) < 0) {
		perror("Can't create privileged nfs port");
		return 1;
	}

	if ((nfsxprt = svcudp_create(so)) == NULL ||
	    (amqp = svcudp_create(so)) == NULL) {
		plog(XLOG_FATAL, "cannot create rpc/udp service");
		return 2;
	}

	sinlen = sizeof sin;
	if (getsockname(so, (struct sockaddr *)&sin, &sinlen) == -1) {
		perror("Can't get information on socket");
		return 1;
	}

	so2 = socket(AF_INET, SOCK_DGRAM, 0);
	if (so2 < 0) {
		perror("Can't create 2nd socket");
		return 1;
	}

	setsockopt(so2, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);

	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(so2, (struct sockaddr *)&sin, sizeof sin) == -1) {
		perror("Can't bind 2nd socket");
		return 1;
	}

	if ((lnfsxprt = svcudp_create(so2)) == NULL ||
	    (lamqp = svcudp_create(so2)) == NULL) {
		plog(XLOG_FATAL, "cannot create rpc/udp service");
		return 2;
	}

	if (!svc_register(nfsxprt, NFS_PROGRAM, NFS_VERSION, nfs_program_2, 0)) {
		plog(XLOG_FATAL, "unable to register (NFS_PROGRAM, NFS_VERSION, 0)");
		return 3;
	}

	/*
	 * Start RPC forwarding
	 */
	if (fwd_init() != 0)
		return 3;

	/*
	 * One or other of so, fwd_sock
	 * must be the highest fd on
	 * which to select.
	 */
	if (so > max_fds)
		max_fds = so;
	if (so2 > max_fds)
		max_fds = so2;
	if (fwd_sock > max_fds)
		max_fds = fwd_sock;

	/*
	 * Construct the root automount node
	 */
	make_root_node();

	/*
	 * Pick up the pieces from a previous run
	 * This is likely to (indirectly) need the rpc_fwd package
	 * so it *must* come after the call to fwd_init().
	 */
	if (restart_existing_mounts)
		restart();

	/*
	 * Mount the top-level auto-mountpoints
	 */
	nmount = mount_exported();

	/*
	 * Now safe to tell parent that we are up and running
	 */
	if (ppid)
		kill(ppid, SIGQUIT);

	if (nmount == 0) {
		plog(XLOG_FATAL, "No work to do - quitting");
		amd_state = Done;
		return 0;
	}

#ifdef DEBUG
	Debug(D_AMQ) {
#endif /* DEBUG */
	/*
	 * Register with amq
	 */
	unregister_amq();

	if (!svc_register(amqp, AMQ_PROGRAM, AMQ_VERSION, amq_program_57, IPPROTO_UDP)) {
		plog(XLOG_FATAL, "unable to register (AMQ_PROGRAM, AMQ_VERSION, udp)");
		return 3;
	}
#ifdef DEBUG
	}
#endif /* DEBUG */

	/*
	 * Start timeout_mp rolling
	 */
	reschedule_timeout_mp();

	/*
	 * Start the server
	 */
	if (run_rpc() != Done) {
		plog(XLOG_FATAL, "run_rpc failed");
		amd_state = Done;
	}

	return 0;
}
