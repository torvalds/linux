/*
 * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
 * unrestricted use provided that this legend is included on all tape
 * media and as a part of the software program in whole or part.  Users
 * may copy or modify Sun RPC without charge, but are not authorized
 * to license or distribute it to anyone else except as part of a product or
 * program developed by the user.
 *
 * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
 * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 *
 * Sun RPC is provided with no support and without any obligation on the
 * part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 *
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
 * OR ANY PART THEREOF.
 *
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 *
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 */

#ifndef lint
#if 0
static char sccsid[] = "from: @(#)rpc.rstatd.c 1.1 86/09/25 Copyr 1984 Sun Micro";
static char sccsid[] = "from: @(#)rstat_proc.c	2.2 88/08/01 4.0 RPCSRC";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif

/*
 * rstat service:  built with rstat.x and derived from rpc.rstatd.c
 *
 * Copyright (c) 1984 by Sun Microsystems, Inc.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/param.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <devstat.h>

#include <net/if.h>
#include <net/if_mib.h>

#undef FSHIFT			 /* Use protocol's shift and scale values */
#undef FSCALE
#undef if_ipackets
#undef if_ierrors
#undef if_opackets
#undef if_oerrors
#undef if_collisions
#include <rpcsvc/rstat.h>

int haveadisk(void);
void updatexfers(int, int *);
int stats_service(void);

extern int from_inetd;
int sincelastreq = 0;		/* number of alarms since last request */
extern int closedown;

union {
    struct stats s1;
    struct statsswtch s2;
    struct statstime s3;
} stats_all;

void updatestat();
static int stat_is_init = 0;

static int	cp_time_xlat[RSTAT_CPUSTATES] = { CP_USER, CP_NICE, CP_SYS,
							CP_IDLE };
static long	bsd_cp_time[CPUSTATES];


#ifndef FSCALE
#define FSCALE (1 << 8)
#endif

void
stat_init(void)
{
    stat_is_init = 1;
    alarm(0);
    updatestat();
    (void) signal(SIGALRM, updatestat);
    alarm(1);
}

statstime *
rstatproc_stats_3_svc(void *argp, struct svc_req *rqstp)
{
    if (! stat_is_init)
        stat_init();
    sincelastreq = 0;
    return(&stats_all.s3);
}

statsswtch *
rstatproc_stats_2_svc(void *argp, struct svc_req *rqstp)
{
    if (! stat_is_init)
        stat_init();
    sincelastreq = 0;
    return(&stats_all.s2);
}

stats *
rstatproc_stats_1_svc(void *argp, struct svc_req *rqstp)
{
    if (! stat_is_init)
        stat_init();
    sincelastreq = 0;
    return(&stats_all.s1);
}

u_int *
rstatproc_havedisk_3_svc(void *argp, struct svc_req *rqstp)
{
    static u_int have;

    if (! stat_is_init)
        stat_init();
    sincelastreq = 0;
    have = haveadisk();
	return(&have);
}

u_int *
rstatproc_havedisk_2_svc(void *argp, struct svc_req *rqstp)
{
    return(rstatproc_havedisk_3_svc(argp, rqstp));
}

u_int *
rstatproc_havedisk_1_svc(void *argp, struct svc_req *rqstp)
{
    return(rstatproc_havedisk_3_svc(argp, rqstp));
}

void
updatestat(void)
{
	int i, hz;
	struct clockinfo clockrate;
	struct ifmibdata ifmd;
	double avrun[3];
	struct timeval tm, btm;
	int mib[6];
	size_t len;
	uint64_t val;
	int ifcount;

#ifdef DEBUG
	fprintf(stderr, "entering updatestat\n");
#endif
	if (sincelastreq >= closedown) {
#ifdef DEBUG
                fprintf(stderr, "about to closedown\n");
#endif
                if (from_inetd)
                        exit(0);
                else {
                        stat_is_init = 0;
                        return;
                }
	}
	sincelastreq++;

	mib[0] = CTL_KERN;
	mib[1] = KERN_CLOCKRATE;
	len = sizeof clockrate;
	if (sysctl(mib, 2, &clockrate, &len, 0, 0) < 0) {
		syslog(LOG_ERR, "sysctl(kern.clockrate): %m");
		exit(1);
	}
	hz = clockrate.hz;

	len = sizeof(bsd_cp_time);
	if (sysctlbyname("kern.cp_time", bsd_cp_time, &len, 0, 0) < 0) {
		syslog(LOG_ERR, "sysctl(kern.cp_time): %m");
		exit(1);
	}
	for(i = 0; i < RSTAT_CPUSTATES ; i++)
		stats_all.s1.cp_time[i] = bsd_cp_time[cp_time_xlat[i]];

        (void)getloadavg(avrun, sizeof(avrun) / sizeof(avrun[0]));

	stats_all.s2.avenrun[0] = avrun[0] * FSCALE;
	stats_all.s2.avenrun[1] = avrun[1] * FSCALE;
	stats_all.s2.avenrun[2] = avrun[2] * FSCALE;

	mib[0] = CTL_KERN;
	mib[1] = KERN_BOOTTIME;
	len = sizeof btm;
	if (sysctl(mib, 2, &btm, &len, 0, 0) < 0) {
		syslog(LOG_ERR, "sysctl(kern.boottime): %m");
		exit(1);
	}

	stats_all.s2.boottime.tv_sec = btm.tv_sec;
	stats_all.s2.boottime.tv_usec = btm.tv_usec;


#ifdef DEBUG
	fprintf(stderr, "%d %d %d %d\n", stats_all.s1.cp_time[0],
	    stats_all.s1.cp_time[1], stats_all.s1.cp_time[2], stats_all.s1.cp_time[3]);
#endif

#define	FETCH_CNT(stat, cnt) do {					\
	len = sizeof(uint64_t);						\
	if (sysctlbyname("vm.stats." #cnt , &val, &len, NULL, 0) < 0) {	\
		syslog(LOG_ERR, "sysctl(vm.stats." #cnt "): %m");	\
		exit(1);						\
	}								\
	stat = val;							\
} while (0)

	FETCH_CNT(stats_all.s1.v_pgpgin, vm.v_vnodepgsin);
	FETCH_CNT(stats_all.s1.v_pgpgout, vm.v_vnodepgsout);
	FETCH_CNT(stats_all.s1.v_pswpin, vm.v_swappgsin);
	FETCH_CNT(stats_all.s1.v_pswpout, vm.v_swappgsout);
	FETCH_CNT(stats_all.s1.v_intr, sys.v_intr);
	FETCH_CNT(stats_all.s2.v_swtch, sys.v_swtch);
	(void)gettimeofday(&tm, NULL);
	stats_all.s1.v_intr -= hz*(tm.tv_sec - btm.tv_sec) +
	    hz*(tm.tv_usec - btm.tv_usec)/1000000;

	/* update disk transfers */
	updatexfers(RSTAT_DK_NDRIVE, stats_all.s1.dk_xfer);

	mib[0] = CTL_NET;
	mib[1] = PF_LINK;
	mib[2] = NETLINK_GENERIC;
	mib[3] = IFMIB_SYSTEM;
	mib[4] = IFMIB_IFCOUNT;
	len = sizeof ifcount;
	if (sysctl(mib, 5, &ifcount, &len, 0, 0) < 0) {
		syslog(LOG_ERR, "sysctl(net.link.generic.system.ifcount): %m");
		exit(1);
	}

	stats_all.s1.if_ipackets = 0;
	stats_all.s1.if_opackets = 0;
	stats_all.s1.if_ierrors = 0;
	stats_all.s1.if_oerrors = 0;
	stats_all.s1.if_collisions = 0;
	for (i = 1; i <= ifcount; i++) {
		len = sizeof ifmd;
		mib[3] = IFMIB_IFDATA;
		mib[4] = i;
		mib[5] = IFDATA_GENERAL;
		if (sysctl(mib, 6, &ifmd, &len, 0, 0) < 0) {
			if (errno == ENOENT)
				continue;

			syslog(LOG_ERR, "sysctl(net.link.ifdata.%d.general)"
			       ": %m", i);
			exit(1);
		}

		stats_all.s1.if_ipackets += ifmd.ifmd_data.ifi_ipackets;
		stats_all.s1.if_opackets += ifmd.ifmd_data.ifi_opackets;
		stats_all.s1.if_ierrors += ifmd.ifmd_data.ifi_ierrors;
		stats_all.s1.if_oerrors += ifmd.ifmd_data.ifi_oerrors;
		stats_all.s1.if_collisions += ifmd.ifmd_data.ifi_collisions;
	}
	(void)gettimeofday(&tm, NULL);
	stats_all.s3.curtime.tv_sec = tm.tv_sec;
	stats_all.s3.curtime.tv_usec = tm.tv_usec;
	alarm(1);
}

/*
 * returns true if have a disk
 */
int
haveadisk(void)
{
	register int i;
	struct statinfo stats;
	int num_devices, retval = 0;

	if ((num_devices = devstat_getnumdevs(NULL)) < 0) {
		syslog(LOG_ERR, "rstatd: can't get number of devices: %s",
		       devstat_errbuf);
		exit(1);
	}

	if (devstat_checkversion(NULL) < 0) {
		syslog(LOG_ERR, "rstatd: %s", devstat_errbuf);
		exit(1);
	}

	stats.dinfo = (struct devinfo *)malloc(sizeof(struct devinfo));
	bzero(stats.dinfo, sizeof(struct devinfo));

	if (devstat_getdevs(NULL, &stats) == -1) {
		syslog(LOG_ERR, "rstatd: can't get device list: %s",
		       devstat_errbuf);
		exit(1);
	}
	for (i = 0; i < stats.dinfo->numdevs; i++) {
		if (((stats.dinfo->devices[i].device_type
		      & DEVSTAT_TYPE_MASK) == DEVSTAT_TYPE_DIRECT)
		 && ((stats.dinfo->devices[i].device_type
		      & DEVSTAT_TYPE_PASS) == 0)) {
			retval = 1;
			break;
		}
	}

	if (stats.dinfo->mem_ptr)
		free(stats.dinfo->mem_ptr);

	free(stats.dinfo);
	return(retval);
}

void
updatexfers(int numdevs, int *devs)
{
	register int i, j, k, t;
	struct statinfo stats;
	int num_devices = 0;
	u_int64_t total_transfers;

	if ((num_devices = devstat_getnumdevs(NULL)) < 0) {
		syslog(LOG_ERR, "rstatd: can't get number of devices: %s",
		       devstat_errbuf);
		exit(1);
	}

	if (devstat_checkversion(NULL) < 0) {
		syslog(LOG_ERR, "rstatd: %s", devstat_errbuf);
		exit(1);
	}

	stats.dinfo = (struct devinfo *)malloc(sizeof(struct devinfo));
	bzero(stats.dinfo, sizeof(struct devinfo));

	if (devstat_getdevs(NULL, &stats) == -1) {
		syslog(LOG_ERR, "rstatd: can't get device list: %s",
		       devstat_errbuf);
		exit(1);
	}

	for (i = 0, j = 0; i < stats.dinfo->numdevs && j < numdevs; i++) {
		if (((stats.dinfo->devices[i].device_type
		      & DEVSTAT_TYPE_MASK) == DEVSTAT_TYPE_DIRECT)
		 && ((stats.dinfo->devices[i].device_type
		      & DEVSTAT_TYPE_PASS) == 0)) {
			total_transfers = 0;
			for (k = 0; k < DEVSTAT_N_TRANS_FLAGS; k++)
				total_transfers +=
				    stats.dinfo->devices[i].operations[k];
			/*
			 * XXX KDM If the total transfers for this device
			 * are greater than the amount we can fit in a
			 * signed integer, just set them to the maximum
			 * amount we can fit in a signed integer.  I have a
			 * feeling that the rstat protocol assumes 32-bit
			 * integers, so this could well break on a 64-bit
			 * architecture like the Alpha.
			 */
			if (total_transfers > INT_MAX)
				t = INT_MAX;
			else
				t = total_transfers;
			devs[j] = t;
			j++;
		}
	}

	if (stats.dinfo->mem_ptr)
		free(stats.dinfo->mem_ptr);

	free(stats.dinfo);
}

void
rstat_service(struct svc_req *rqstp, SVCXPRT *transp)
{
	union {
		int fill;
	} argument;
	char *result;
	bool_t (*xdr_argument)(), (*xdr_result)();
	char *(*local)();

	switch (rqstp->rq_proc) {
	case NULLPROC:
		(void)svc_sendreply(transp, (xdrproc_t)xdr_void, NULL);
		goto leave;

	case RSTATPROC_STATS:
		xdr_argument = xdr_void;
		xdr_result = xdr_statstime;
                switch (rqstp->rq_vers) {
                case RSTATVERS_ORIG:
                        local = (char *(*)()) rstatproc_stats_1_svc;
                        break;
                case RSTATVERS_SWTCH:
                        local = (char *(*)()) rstatproc_stats_2_svc;
                        break;
                case RSTATVERS_TIME:
                        local = (char *(*)()) rstatproc_stats_3_svc;
                        break;
                default:
                        svcerr_progvers(transp, RSTATVERS_ORIG, RSTATVERS_TIME);
                        goto leave;
                        /*NOTREACHED*/
                }
		break;

	case RSTATPROC_HAVEDISK:
		xdr_argument = xdr_void;
		xdr_result = xdr_u_int;
                switch (rqstp->rq_vers) {
                case RSTATVERS_ORIG:
                        local = (char *(*)()) rstatproc_havedisk_1_svc;
                        break;
                case RSTATVERS_SWTCH:
                        local = (char *(*)()) rstatproc_havedisk_2_svc;
                        break;
                case RSTATVERS_TIME:
                        local = (char *(*)()) rstatproc_havedisk_3_svc;
                        break;
                default:
                        svcerr_progvers(transp, RSTATVERS_ORIG, RSTATVERS_TIME);
                        goto leave;
                        /*NOTREACHED*/
                }
		break;

	default:
		svcerr_noproc(transp);
		goto leave;
	}
	bzero((char *)&argument, sizeof(argument));
	if (!svc_getargs(transp, (xdrproc_t)xdr_argument, &argument)) {
		svcerr_decode(transp);
		goto leave;
	}
	result = (*local)(&argument, rqstp);
	if (result != NULL &&
	    !svc_sendreply(transp, (xdrproc_t)xdr_result, result)) {
		svcerr_systemerr(transp);
	}
	if (!svc_freeargs(transp, (xdrproc_t)xdr_argument, &argument))
		errx(1, "unable to free arguments");
leave:
        if (from_inetd)
                exit(0);
}
