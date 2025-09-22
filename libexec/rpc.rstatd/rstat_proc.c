/*	$OpenBSD: rstat_proc.c,v 1.38 2023/03/08 04:43:05 guenther Exp $	*/

/*
 * Copyright (c) 2010, Oracle America, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the "Oracle America, Inc." nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * rstat service:  built with rstat.x and derived from rpc.rstatd.c
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/sched.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <net/if.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <ifaddrs.h>
#include "dkstats.h"

#undef FSHIFT			 /* Use protocol's shift and scale values */
#undef FSCALE
#undef DK_NDRIVE
#undef CPUSTATES
#undef if_ipackets
#undef if_ierrors
#undef if_opackets
#undef if_oerrors
#undef if_collisions
#include <rpcsvc/rstat.h>

int	cp_xlat[CPUSTATES] = { CP_USER, CP_NICE, CP_SYS, CP_IDLE };

extern int dk_ndrive;		/* from dkstats.c */
extern struct _disk cur, last;
char *memf = NULL, *nlistf = NULL;
int hz;

extern int from_inetd;
int sincelastreq = 0;		/* number of alarms since last request */
extern int closedown;

union {
	struct stats s1;
	struct statsswtch s2;
	struct statstime s3;
} stats_all;

void	updatestat(void);
void	updatestatsig(int sig);
void	setup(void);

volatile sig_atomic_t wantupdatestat;

static int stat_is_init = 0;

#ifndef FSCALE
#define FSCALE (1 << 8)
#endif

static void
stat_init(void)
{
	stat_is_init = 1;
	setup();
	updatestat();
	(void) signal(SIGALRM, updatestatsig);
	alarm(1);
}

statstime *
rstatproc_stats_3_svc(void *arg, struct svc_req *rqstp)
{
	if (!stat_is_init)
		stat_init();
	sincelastreq = 0;
	return (&stats_all.s3);
}

statsswtch *
rstatproc_stats_2_svc(void *arg, struct svc_req *rqstp)
{
	if (!stat_is_init)
		stat_init();
	sincelastreq = 0;
	return (&stats_all.s2);
}

stats *
rstatproc_stats_1_svc(void *arg, struct svc_req *rqstp)
{
	if (!stat_is_init)
		stat_init();
	sincelastreq = 0;
	return (&stats_all.s1);
}

u_int *
rstatproc_havedisk_3_svc(void *arg, struct svc_req *rqstp)
{
	static u_int have;

	if (!stat_is_init)
		stat_init();
	sincelastreq = 0;
	have = dk_ndrive != 0;
	return (&have);
}

u_int *
rstatproc_havedisk_2_svc(void *arg, struct svc_req *rqstp)
{
	return (rstatproc_havedisk_3_svc(arg, rqstp));
}

u_int *
rstatproc_havedisk_1_svc(void *arg, struct svc_req *rqstp)
{
	return (rstatproc_havedisk_3_svc(arg, rqstp));
}

void
updatestatsig(int sig)
{
	wantupdatestat = 1;
}

void
updatestat(void)
{
	int i, mib[2], save_errno = errno;
	struct uvmexp uvmexp;
	size_t len;
	struct if_data *ifdp;
	struct ifaddrs *ifaddrs, *ifa;
	double avrun[3];
	struct timeval tm, btm;
	long *cp_time = cur.cp_time;

#ifdef DEBUG
	syslog(LOG_DEBUG, "entering updatestat");
#endif
	if (sincelastreq >= closedown) {
#ifdef DEBUG
		syslog(LOG_DEBUG, "about to closedown");
#endif
		if (from_inetd)
			_exit(0);
		else {
			stat_is_init = 0;
			errno = save_errno;
			return;
		}
	}
	sincelastreq++;

	/*
	 * dkreadstats reads in the "disk_count" as well as the "disklist"
	 * statistics.  It also retrieves "hz" and the "cp_time" array.
	 */
	dkreadstats();
	memset(stats_all.s1.dk_xfer, '\0', sizeof(stats_all.s1.dk_xfer));
	for (i = 0; i < dk_ndrive && i < DK_NDRIVE; i++)
		stats_all.s1.dk_xfer[i] = cur.dk_rxfer[i] + cur.dk_wxfer[i];

	for (i = 0; i < CPUSTATES; i++)
		stats_all.s1.cp_time[i] = cp_time[cp_xlat[i]];
	(void)getloadavg(avrun, sizeof(avrun) / sizeof(avrun[0]));
	stats_all.s2.avenrun[0] = avrun[0] * FSCALE;
	stats_all.s2.avenrun[1] = avrun[1] * FSCALE;
	stats_all.s2.avenrun[2] = avrun[2] * FSCALE;
	mib[0] = CTL_KERN;
	mib[1] = KERN_BOOTTIME;
	len = sizeof(btm);
	if (sysctl(mib, 2, &btm, &len, NULL, 0) == -1) {
		syslog(LOG_ERR, "can't sysctl kern.boottime: %m");
		_exit(1);
	}
	stats_all.s2.boottime.tv_sec = btm.tv_sec;
	stats_all.s2.boottime.tv_usec = btm.tv_usec;


#ifdef DEBUG
	syslog(LOG_DEBUG, "%d %d %d %d", stats_all.s1.cp_time[0],
	    stats_all.s1.cp_time[1], stats_all.s1.cp_time[2],
	    stats_all.s1.cp_time[3]);
#endif

	mib[0] = CTL_VM;
	mib[1] = VM_UVMEXP;
	len = sizeof(uvmexp);
	if (sysctl(mib, 2, &uvmexp, &len, NULL, 0) == -1) {
		syslog(LOG_ERR, "can't sysctl vm.uvmexp: %m");
		_exit(1);
	}
	stats_all.s1.v_pgpgin = uvmexp.fltanget;
	stats_all.s1.v_pgpgout = uvmexp.pdpageouts;
	stats_all.s1.v_pswpin = 0;
	stats_all.s1.v_pswpout = 0;
	stats_all.s1.v_intr = uvmexp.intrs;
	stats_all.s2.v_swtch = uvmexp.swtch;
	gettimeofday(&tm, NULL);
	stats_all.s1.v_intr -= hz*(tm.tv_sec - btm.tv_sec) +
	    hz*(tm.tv_usec - btm.tv_usec)/1000000;
	stats_all.s1.if_ipackets = 0;
	stats_all.s1.if_opackets = 0;
	stats_all.s1.if_ierrors = 0;
	stats_all.s1.if_oerrors = 0;
	stats_all.s1.if_collisions = 0;
	if (getifaddrs(&ifaddrs) == -1) {
		syslog(LOG_ERR, "can't getifaddrs: %m");
		_exit(1);
	}
	for (ifa = ifaddrs; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr->sa_family != AF_LINK)
			continue;
		ifdp = (struct if_data *)ifa->ifa_data;
		stats_all.s1.if_ipackets += ifdp->ifi_ipackets;
		stats_all.s1.if_opackets += ifdp->ifi_opackets;
		stats_all.s1.if_ierrors += ifdp->ifi_ierrors;
		stats_all.s1.if_oerrors += ifdp->ifi_oerrors;
		stats_all.s1.if_collisions += ifdp->ifi_collisions;
	}
	freeifaddrs(ifaddrs);
	stats_all.s3.curtime.tv_sec = tm.tv_sec;
	stats_all.s3.curtime.tv_usec = tm.tv_usec;

	alarm(1);
	errno = save_errno;
}

void
setup(void)
{
	dkinit(0);
}

void	rstat_service(struct svc_req *, SVCXPRT *);

void
rstat_service(struct svc_req *rqstp, SVCXPRT *transp)
{
	char *(*local)(void *, struct svc_req *);
	xdrproc_t xdr_argument, xdr_result;
	union {
		int fill;
	} argument;
	char *result;

	switch (rqstp->rq_proc) {
	case NULLPROC:
		(void)svc_sendreply(transp, xdr_void, (char *)NULL);
		return;

	case RSTATPROC_STATS:
		xdr_argument = (xdrproc_t)xdr_void;
		xdr_result = (xdrproc_t)xdr_statstime;
		switch (rqstp->rq_vers) {
		case RSTATVERS_ORIG:
			local = (char *(*)(void *, struct svc_req *))
				rstatproc_stats_1_svc;
			break;
		case RSTATVERS_SWTCH:
			local = (char *(*)(void *, struct svc_req *))
				rstatproc_stats_2_svc;
			break;
		case RSTATVERS_TIME:
			local = (char *(*)(void *, struct svc_req *))
				rstatproc_stats_3_svc;
			break;
		default:
			svcerr_progvers(transp, RSTATVERS_ORIG, RSTATVERS_TIME);
			return;
		}
		break;

	case RSTATPROC_HAVEDISK:
		xdr_argument = (xdrproc_t)xdr_void;
		xdr_result = (xdrproc_t)xdr_u_int;
		switch (rqstp->rq_vers) {
		case RSTATVERS_ORIG:
			local = (char *(*)(void *, struct svc_req *))
				rstatproc_havedisk_1_svc;
			break;
		case RSTATVERS_SWTCH:
			local = (char *(*)(void *, struct svc_req *))
				rstatproc_havedisk_2_svc;
			break;
		case RSTATVERS_TIME:
			local = (char *(*)(void *, struct svc_req *))
				rstatproc_havedisk_3_svc;
			break;
		default:
			svcerr_progvers(transp, RSTATVERS_ORIG, RSTATVERS_TIME);
			return;
		}
		break;

	default:
		svcerr_noproc(transp);
		return;
	}
	memset(&argument, 0, sizeof(argument));
	if (!svc_getargs(transp, xdr_argument, (caddr_t)&argument)) {
		svcerr_decode(transp);
		return;
	}
	result = (*local)(&argument, rqstp);
	if (result != NULL && !svc_sendreply(transp, xdr_result, result)) {
		svcerr_systemerr(transp);
	}
	if (!svc_freeargs(transp, xdr_argument, (caddr_t)&argument)) {
		syslog(LOG_ERR, "unable to free arguments");
		exit(1);
	}
}
