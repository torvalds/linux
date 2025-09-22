/*	$OpenBSD: lockd.c,v 1.16 2023/03/08 04:43:15 guenther Exp $	*/

/*
 * Copyright (c) 1995
 *	A.R. Gordon (andrew.gordon@net-tel.co.uk).  All rights reserved.
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
 *	This product includes software developed for the FreeBSD project
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ANDREW GORDON AND CONTRIBUTORS ``AS IS'' AND
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
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include <rpcsvc/sm_inter.h>
#include "nlm_prot.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <syslog.h>
#include <stdlib.h>
#include <err.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>

#include "lockd.h"

int debug_level = 0;	/* 0 = no debugging syslog() calls */
int _rpcsvcdirty = 0;
int grace_expired;

void nlm_prog_0(struct svc_req *, SVCXPRT *);
void nlm_prog_1(struct svc_req *, SVCXPRT *);
void nlm_prog_3(struct svc_req *, SVCXPRT *);
void nlm_prog_4(struct svc_req *, SVCXPRT *);

static void sigalarm_handler(int);
static void usage(void);

int
main(int argc, char *argv[])
{
	SVCXPRT *transp;
	const char *errstr;
	int ch;
	struct sigaction sigchild, sigalarm;
	int grace_period = 30;

	while ((ch = getopt(argc, argv, "d:g:")) != (-1)) {
		switch (ch) {
		case 'd':
			debug_level = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr) {
				usage();
				/* NOTREACHED */
			}
			break;
		case 'g':
			grace_period = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr) {
				usage();
				/* NOTREACHED */
			}
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	(void) pmap_unset(NLM_PROG, NLM_SM);
	(void) pmap_unset(NLM_PROG, NLM_VERS);
	(void) pmap_unset(NLM_PROG, NLM_VERSX);
	(void) pmap_unset(NLM_PROG, NLM_VERS4);

	transp = svcudp_create(RPC_ANYSOCK);
	if (transp == NULL) {
		fprintf(stderr, "cannot create udp service.\n");
		exit(1);
	}
	if (!svc_register(transp, NLM_PROG, NLM_SM,
	    (void (*) (struct svc_req *, SVCXPRT *)) nlm_prog_0, IPPROTO_UDP)) {
		fprintf(stderr, "unable to register (NLM_PROG, NLM_SM, udp).\n");
		exit(1);
	}
	if (!svc_register(transp, NLM_PROG, NLM_VERS,
	    (void (*) (struct svc_req *, SVCXPRT *)) nlm_prog_1, IPPROTO_UDP)) {
		fprintf(stderr, "unable to register (NLM_PROG, NLM_VERS, udp).\n");
		exit(1);
	}
	if (!svc_register(transp, NLM_PROG, NLM_VERSX,
	    (void (*) (struct svc_req *, SVCXPRT *)) nlm_prog_3, IPPROTO_UDP)) {
		fprintf(stderr, "unable to register (NLM_PROG, NLM_VERSX, udp).\n");
		exit(1);
	}
	if (!svc_register(transp, NLM_PROG, NLM_VERS4,
	    (void (*) (struct svc_req *, SVCXPRT *)) nlm_prog_4, IPPROTO_UDP)) {
		fprintf(stderr, "unable to register (NLM_PROG, NLM_VERS4, udp).\n");
		exit(1);
	}
	transp = svctcp_create(RPC_ANYSOCK, 0, 0);
	if (transp == NULL) {
		fprintf(stderr, "cannot create tcp service.\n");
		exit(1);
	}
	if (!svc_register(transp, NLM_PROG, NLM_VERS,
	    (void (*) (struct svc_req *, SVCXPRT *)) nlm_prog_1, IPPROTO_TCP)) {
		fprintf(stderr, "unable to register (NLM_PROG, NLM_VERS, tcp).\n");
		exit(1);
	}
	if (!svc_register(transp, NLM_PROG, NLM_VERSX,
	    (void (*) (struct svc_req *, SVCXPRT *)) nlm_prog_3, IPPROTO_TCP)) {
		fprintf(stderr, "unable to register (NLM_PROG, NLM_VERSX, tcp).\n");
		exit(1);
	}
	if (!svc_register(transp, NLM_PROG, NLM_VERS4,
	    (void (*) (struct svc_req *, SVCXPRT *)) nlm_prog_4, IPPROTO_TCP)) {
		fprintf(stderr, "unable to register (NLM_PROG, NLM_VERS4, tcp).\n");
		exit(1);
	}

	/*
	 * Note that it is NOT sensible to run this program from inetd - the
	 * protocol assumes that it will run immediately at boot time.
	 */
	if (daemon(0, 0) == -1) {
		err(1, "cannot fork");
		/* NOTREACHED */
	}

	openlog("rpc.lockd", 0, LOG_DAEMON);
	if (debug_level)
		syslog(LOG_INFO, "Starting, debug level %d", debug_level);
	else
		syslog(LOG_INFO, "Starting");

	sigchild.sa_handler = sigchild_handler;
	sigemptyset(&sigchild.sa_mask);
	sigchild.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sigchild, NULL) != 0) {
		syslog(LOG_WARNING, "sigaction(SIGCHLD) failed (%m)");
		exit(1);
	}
	sigalarm.sa_handler = sigalarm_handler;
	sigemptyset(&sigalarm.sa_mask);
	sigalarm.sa_flags = SA_RESETHAND; /* should only happen once */
	sigalarm.sa_flags |= SA_RESTART;
	if (sigaction(SIGALRM, &sigalarm, NULL) != 0) {
		syslog(LOG_WARNING, "sigaction(SIGALRM) failed (%m)");
		exit(1);
	}
	grace_expired = 0;
	if (alarm(10) == (unsigned int)-1) {
		syslog(LOG_WARNING, "alarm failed (%m)");
		exit(1);
	}

	svc_run();		/* Should never return */
	return 1;
}

static void
sigalarm_handler(int s)
{
	grace_expired = 1;
}

static void
usage()
{
	errx(1, "usage: rpc.lockd [-d [debug_level]] [-g grace_period]");
}
