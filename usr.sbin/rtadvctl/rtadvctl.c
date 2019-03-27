/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2011 Hiroki Sato <hrs@FreeBSD.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet6/in6_var.h>
#include <netinet6/nd6.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <syslog.h>
#include <time.h>
#include <err.h>

#include "pathnames.h"
#include "rtadvd.h"
#include "if.h"
#include "timer_subr.h"
#include "timer.h"
#include "control.h"
#include "control_client.h"

#define RA_IFSTATUS_INACTIVE	0
#define RA_IFSTATUS_RA_RECV	1
#define RA_IFSTATUS_RA_SEND	2

static int vflag = LOG_ERR;

static void	usage(void);

static int	action_propset(char *);
static int	action_propget(char *, struct ctrl_msg_pl *);
static int	action_plgeneric(int, char *, char *);

static int	action_enable(int, char **);
static int	action_disable(int, char **);
static int	action_reload(int, char **);
static int	action_echo(int, char **);
static int	action_version(int, char **);
static int	action_shutdown(int, char **);

static int	action_show(int, char **);
static int	action_show_prefix(struct prefix *);
static int	action_show_rtinfo(struct rtinfo *);
static int	action_show_rdnss(void *);
static int	action_show_dnssl(void *);

static int	csock_client_open(struct sockinfo *);
static size_t	dname_labeldec(char *, size_t, const char *);
static void	mysyslog(int, const char *, ...);

static const char *rtpref_str[] = {
	"medium",		/* 00 */
	"high",			/* 01 */
	"rsv",			/* 10 */
	"low"			/* 11 */
};

static struct dispatch_table {
	const char	*dt_comm;
	int (*dt_act)(int, char **);
} dtable[] = {
	{ "show", action_show },
	{ "reload", action_reload },
	{ "shutdown", action_shutdown },
	{ "enable", action_enable },
	{ "disable", action_disable },
	{ NULL, NULL },
	{ "echo", action_echo },
	{ "version", action_version },
	{ NULL, NULL },
};

static char errmsgbuf[1024];
static char *errmsg = NULL;

static void
mysyslog(int priority, const char * restrict fmt, ...)
{
	va_list ap;

	if (vflag >= priority) {
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		fprintf(stderr, "\n");
		va_end(ap);
	}
}

static void
usage(void)
{
	int i;

	for (i = 0; (size_t)i < sizeof(dtable)/sizeof(dtable[0]); i++) {
		if (dtable[i].dt_comm == NULL)
			break;
		printf("%s\n", dtable[i].dt_comm);
	}

	exit(1);
}

int
main(int argc, char *argv[])
{
	int i;
	int ch;
	int (*action)(int, char **) = NULL;
	int error;

	while ((ch = getopt(argc, argv, "Dv")) != -1) {
		switch (ch) {
		case 'D':
			vflag = LOG_DEBUG;
			break;
		case 'v':
			vflag++;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0)
		usage();

	for (i = 0; (size_t)i < sizeof(dtable)/sizeof(dtable[0]); i++) {
		if (dtable[i].dt_comm == NULL ||
		    strcmp(dtable[i].dt_comm, argv[0]) == 0) {
			action = dtable[i].dt_act;
			break;
		}
	}

	if (action == NULL)
		usage();

	error = (dtable[i].dt_act)(--argc, ++argv);
	if (error) {
		fprintf(stderr, "%s failed", dtable[i].dt_comm);
		if (errmsg != NULL)
			fprintf(stderr, ": %s", errmsg);
		fprintf(stderr, ".\n");
	}

	return (error);
}

static int
csock_client_open(struct sockinfo *s)
{
	struct sockaddr_un sun;

	if ((s->si_fd = socket(PF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "cannot open control socket.");

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	sun.sun_len = sizeof(sun);
	strlcpy(sun.sun_path, s->si_name, sizeof(sun.sun_path));

	if (connect(s->si_fd, (struct sockaddr *)&sun, sizeof(sun)) == -1)
		err(1, "connect: %s", s->si_name);

	mysyslog(LOG_DEBUG,
	    "<%s> connected to %s", __func__, sun.sun_path);

	return (0);
}

static int
action_plgeneric(int action, char *plstr, char *buf)
{
	struct ctrl_msg_hdr *cm;
	struct ctrl_msg_pl cp;
	struct sockinfo *s;
	char *msg;
	char *p;
	char *q;

	s = &ctrlsock;
	csock_client_open(s);

	cm = (struct ctrl_msg_hdr *)buf;
	msg = (char *)buf + sizeof(*cm);

	cm->cm_version = CM_VERSION;
	cm->cm_type = action;
	cm->cm_len = sizeof(*cm);

	if (plstr != NULL) {
		memset(&cp, 0, sizeof(cp));
		p = strchr(plstr, ':');
		q = strchr(plstr, '=');
		if (p != NULL && q != NULL && p > q)
			return (1);

		if (p == NULL) {		/* No : */
			cp.cp_ifname = NULL;
			cp.cp_key = plstr;
		} else if  (p == plstr) {	/* empty */
			cp.cp_ifname = NULL;
			cp.cp_key = plstr + 1;
		} else {
			*p++ = '\0';
			cp.cp_ifname = plstr;
			cp.cp_key = p;
		}
		if (q == NULL)
			cp.cp_val = NULL;
		else {
			*q++ = '\0';
			cp.cp_val = q;
		}
		cm->cm_len += cm_pl2bin(msg, &cp);

		mysyslog(LOG_DEBUG, "<%s> key=%s, val_len=%d, ifname=%s",
		    __func__,cp.cp_key, cp.cp_val_len, cp.cp_ifname);
	}

	return (cm_handler_client(s->si_fd, CM_STATE_MSG_DISPATCH, buf));
}

static int
action_propget(char *argv, struct ctrl_msg_pl *cp)
{
	int error;
	struct ctrl_msg_hdr *cm;
	char buf[CM_MSG_MAXLEN];
	char *msg;

	memset(cp, 0, sizeof(*cp));
	cm = (struct ctrl_msg_hdr *)buf;
	msg = (char *)buf + sizeof(*cm);

	error = action_plgeneric(CM_TYPE_REQ_GET_PROP, argv, buf);
	if (error || cm->cm_len <= sizeof(*cm))
		return (1);

	cm_bin2pl(msg, cp);
	mysyslog(LOG_DEBUG, "<%s> type=%d, len=%d",
	    __func__, cm->cm_type, cm->cm_len);
	mysyslog(LOG_DEBUG, "<%s> key=%s, val_len=%d, ifname=%s",
	    __func__,cp->cp_key, cp->cp_val_len, cp->cp_ifname);

	return (0);
}

static int
action_propset(char *argv)
{
	char buf[CM_MSG_MAXLEN];

	return (action_plgeneric(CM_TYPE_REQ_SET_PROP, argv, buf));
}

static int
action_disable(int argc, char **argv)
{
	char *action_argv;
	char argv_disable[IFNAMSIZ + sizeof(":disable=")];
	int i;
	int error;

	if (argc < 1)
		return (1);

	error = 0;
	for (i = 0; i < argc; i++) {
		sprintf(argv_disable, "%s:disable=", argv[i]);
		action_argv = argv_disable;
		error += action_propset(action_argv);
	}

	return (error);
}

static int
action_enable(int argc, char **argv)
{
	char *action_argv;
	char argv_enable[IFNAMSIZ + sizeof(":enable=")];
	int i;
	int error;

	if (argc < 1)
		return (1);

	error = 0;
	for (i = 0; i < argc; i++) {
		sprintf(argv_enable, "%s:enable=", argv[i]);
		action_argv = argv_enable;
		error += action_propset(action_argv);
	}

	return (error);
}

static int
action_reload(int argc, char **argv)
{
	char *action_argv;
	char argv_reload[IFNAMSIZ + sizeof(":reload=")];
	int i;
	int error;

	if (argc == 0) {
		action_argv = strdup(":reload=");
		return (action_propset(action_argv));
	}

	error = 0;
	for (i = 0; i < argc; i++) {
		sprintf(argv_reload, "%s:reload=", argv[i]);
		action_argv = argv_reload;
		error += action_propset(action_argv);
	}

	return (error);
}

static int
action_echo(int argc __unused, char **argv __unused)
{
	char *action_argv;

	action_argv = strdup("echo");
	return (action_propset(action_argv));
}

static int
action_shutdown(int argc __unused, char **argv __unused)
{
	char *action_argv;

	action_argv = strdup("shutdown");
	return (action_propset(action_argv));
}

/* XXX */
static int
action_version(int argc __unused, char **argv __unused)
{
	char *action_argv;
	struct ctrl_msg_pl cp;
	int error;

	action_argv = strdup(":version=");
	error = action_propget(action_argv, &cp);
	if (error)
		return (error);

	printf("version=%s\n", cp.cp_val);
	return (0);
}

static int
action_show(int argc, char **argv)
{
	char *action_argv;
	char argv_ifilist[sizeof(":ifilist=")] = ":ifilist=";
	char argv_ifi[IFNAMSIZ + sizeof(":ifi=")];
	char argv_rai[IFNAMSIZ + sizeof(":rai=")];
	char argv_rti[IFNAMSIZ + sizeof(":rti=")];
	char argv_pfx[IFNAMSIZ + sizeof(":pfx=")];
	char argv_ifi_ra_timer[IFNAMSIZ + sizeof(":ifi_ra_timer=")];
	char argv_rdnss[IFNAMSIZ + sizeof(":rdnss=")];
	char argv_dnssl[IFNAMSIZ + sizeof(":dnssl=")];
	char ssbuf[SSBUFLEN];

	struct timespec now, ts0, ts;
	struct ctrl_msg_pl cp;
	struct ifinfo *ifi;
	TAILQ_HEAD(, ifinfo) ifl = TAILQ_HEAD_INITIALIZER(ifl);
	char *endp;
	char *p;
	int error;
	int i;
	int len;

	if (argc == 0) {
		action_argv = argv_ifilist;
		error = action_propget(action_argv, &cp);
		if (error)
			return (error);

		p = cp.cp_val;
		endp = p + cp.cp_val_len;
		while (p < endp) {
			ifi = malloc(sizeof(*ifi));
			if (ifi == NULL)
				return (1);
			memset(ifi, 0, sizeof(*ifi));

			strcpy(ifi->ifi_ifname, p);
			ifi->ifi_ifindex = if_nametoindex(ifi->ifi_ifname);
			TAILQ_INSERT_TAIL(&ifl, ifi, ifi_next);
			p += strlen(ifi->ifi_ifname) + 1;
		}
	} else {
		for (i = 0; i < argc; i++) {
			ifi = malloc(sizeof(*ifi));
			if (ifi == NULL)
				return (1);
			memset(ifi, 0, sizeof(*ifi));

			strcpy(ifi->ifi_ifname, argv[i]);
			ifi->ifi_ifindex = if_nametoindex(ifi->ifi_ifname);
			if (ifi->ifi_ifindex == 0) {
				sprintf(errmsgbuf, "invalid interface %s",
				    ifi->ifi_ifname);
				errmsg = errmsgbuf;
				return (1);
			}

			TAILQ_INSERT_TAIL(&ifl, ifi, ifi_next);
		}
	}

	clock_gettime(CLOCK_REALTIME_FAST, &now);
	clock_gettime(CLOCK_MONOTONIC_FAST, &ts);
	TS_SUB(&now, &ts, &ts0);

	TAILQ_FOREACH(ifi, &ifl, ifi_next) {
		struct ifinfo *ifi_s;
		struct rtadvd_timer *rat;
		struct rainfo *rai;
		struct rtinfo *rti;
		struct prefix *pfx;
		int c;
		int ra_ifstatus;

		sprintf(argv_ifi, "%s:ifi=", ifi->ifi_ifname);
		action_argv = argv_ifi;
		error = action_propget(action_argv, &cp);
		if (error)
			return (error);
		ifi_s = (struct ifinfo *)cp.cp_val;

		if (!(ifi_s->ifi_persist) && vflag < LOG_NOTICE)
			continue;

		printf("%s: flags=<", ifi->ifi_ifname);

		c = 0;
		if (ifi_s->ifi_ifindex == 0)
			c += printf("NONEXISTENT");
		else
			c += printf("%s", (ifi_s->ifi_flags & IFF_UP) ?
			    "UP" : "DOWN");
		switch (ifi_s->ifi_state) {
		case IFI_STATE_CONFIGURED:
			c += printf("%s%s", (c) ? "," : "", "CONFIGURED");
			break;
		case IFI_STATE_TRANSITIVE:
			c += printf("%s%s", (c) ? "," : "", "TRANSITIVE");
			break;
		}
		if (ifi_s->ifi_persist)
			c += printf("%s%s", (c) ? "," : "", "PERSIST");
		printf(">");

		ra_ifstatus = RA_IFSTATUS_INACTIVE;
		if ((ifi_s->ifi_flags & IFF_UP) &&
		    ((ifi_s->ifi_state == IFI_STATE_CONFIGURED) ||
			(ifi_s->ifi_state == IFI_STATE_TRANSITIVE))) {
#if (__FreeBSD_version < 900000)
			/*
			 * RA_RECV: !ip6.forwarding && ip6.accept_rtadv
			 * RA_SEND: ip6.forwarding
			 */
			if (getinet6sysctl(IPV6CTL_FORWARDING) == 0) {
				if (getinet6sysctl(IPV6CTL_ACCEPT_RTADV))
					ra_ifstatus = RA_IFSTATUS_RA_RECV;
				else
					ra_ifstatus = RA_IFSTATUS_INACTIVE;
			} else
				ra_ifstatus = RA_IFSTATUS_RA_SEND;
#else
			/*
			 * RA_RECV: ND6_IFF_ACCEPT_RTADV
			 * RA_SEND: ip6.forwarding
			 */
			if (ifi_s->ifi_nd_flags & ND6_IFF_ACCEPT_RTADV)
				ra_ifstatus = RA_IFSTATUS_RA_RECV;
			else if (getinet6sysctl(IPV6CTL_FORWARDING))
				ra_ifstatus = RA_IFSTATUS_RA_SEND;
			else
				ra_ifstatus = RA_IFSTATUS_INACTIVE;
#endif
		}

		c = 0;
		printf(" status=<");
		if (ra_ifstatus == RA_IFSTATUS_INACTIVE)
			printf("%s%s", (c) ? "," : "", "INACTIVE");
		else if (ra_ifstatus == RA_IFSTATUS_RA_RECV)
			printf("%s%s", (c) ? "," : "", "RA_RECV");
		else if (ra_ifstatus == RA_IFSTATUS_RA_SEND)
			printf("%s%s", (c) ? "," : "", "RA_SEND");
		printf("> ");

		switch (ifi_s->ifi_state) {
		case IFI_STATE_CONFIGURED:
		case IFI_STATE_TRANSITIVE:
			break;
		default:
			printf("\n");
			continue;
		}

		printf("mtu %d\n", ifi_s->ifi_phymtu);

		sprintf(argv_rai, "%s:rai=", ifi->ifi_ifname);
		action_argv = argv_rai;

		error = action_propget(action_argv, &cp);
		if (error)
			continue;

		rai = (struct rainfo *)cp.cp_val;

		printf("\tDefaultLifetime: %s",
		    sec2str(rai->rai_lifetime, ssbuf));
		if (ra_ifstatus != RA_IFSTATUS_RA_SEND &&
		    rai->rai_lifetime == 0)
			printf(" (RAs will be sent with zero lifetime)");

		printf("\n");

		printf("\tMinAdvInterval/MaxAdvInterval: ");
		printf("%s/", sec2str(rai->rai_mininterval, ssbuf));
		printf("%s\n", sec2str(rai->rai_maxinterval, ssbuf));
		if (rai->rai_linkmtu)
			printf("\tAdvLinkMTU: %d", rai->rai_linkmtu);
		else
			printf("\tAdvLinkMTU: <none>");

		printf(", ");

		printf("Flags: ");
		if (rai->rai_managedflg || rai->rai_otherflg) {
			printf("%s", rai->rai_managedflg ? "M" : "");
			printf("%s", rai->rai_otherflg ? "O" : "");
		} else
			printf("<none>");

		printf(", ");

		printf("Preference: %s\n",
		    rtpref_str[(rai->rai_rtpref >> 3) & 0xff]);

		printf("\tReachableTime: %s, ",
		    sec2str(rai->rai_reachabletime, ssbuf));
		printf("RetransTimer: %s, "
		    "CurHopLimit: %d\n",
		    sec2str(rai->rai_retranstimer, ssbuf),
		    rai->rai_hoplimit);
		printf("\tAdvIfPrefixes: %s\n",
		    rai->rai_advifprefix ? "yes" : "no");

		/* RA timer */
		rat = NULL;
		if (ifi_s->ifi_ra_timer != NULL) {
			sprintf(argv_ifi_ra_timer, "%s:ifi_ra_timer=",
			    ifi->ifi_ifname);
			action_argv = argv_ifi_ra_timer;

			error = action_propget(action_argv, &cp);
			if (error)
				return (error);

			rat = (struct rtadvd_timer *)cp.cp_val;
		}
		printf("\tNext RA send: ");
		if (rat == NULL)
			printf("never\n");
		else {
			ts.tv_sec = rat->rat_tm.tv_sec + ts0.tv_sec;
			printf("%s", ctime(&ts.tv_sec));
		}
		printf("\tLast RA send: ");
		if (ifi_s->ifi_ra_lastsent.tv_sec == 0)
			printf("never\n");
		else {
			ts.tv_sec = ifi_s->ifi_ra_lastsent.tv_sec + ts0.tv_sec;
			printf("%s", ctime(&ts.tv_sec));
		}
		if (rai->rai_clockskew)
			printf("\tClock skew: %" PRIu16 "sec\n",
			    rai->rai_clockskew);

		if (vflag < LOG_WARNING)
			continue;

		/* route information */
		sprintf(argv_rti, "%s:rti=", ifi->ifi_ifname);
		action_argv = argv_rti;
		error = action_propget(action_argv, &cp);
		if (error)
			return (error);

		rti = (struct rtinfo *)cp.cp_val;
		len = cp.cp_val_len / sizeof(*rti);
		if (len > 0) {
			printf("\tRoute Info:\n");

			for (i = 0; i < len; i++)
				action_show_rtinfo(&rti[i]);
		}

		/* prefix information */
		sprintf(argv_pfx, "%s:pfx=", ifi->ifi_ifname);
		action_argv = argv_pfx;

		error = action_propget(action_argv, &cp);
		if (error)
			continue;

		pfx = (struct prefix *)cp.cp_val;
		len = cp.cp_val_len / sizeof(*pfx);

		if (len > 0) {
			printf("\tPrefixes (%d):\n", len);

			for (i = 0; i < len; i++)
				action_show_prefix(&pfx[i]);
		}

		/* RDNSS information */
		sprintf(argv_rdnss, "%s:rdnss=", ifi->ifi_ifname);
		action_argv = argv_rdnss;

		error = action_propget(action_argv, &cp);
		if (error)
			continue;

		len = *((uint16_t *)cp.cp_val);

		if (len > 0) {
			printf("\tRDNSS entries:\n");
			action_show_rdnss(cp.cp_val);
		}

		/* DNSSL information */
		sprintf(argv_dnssl, "%s:dnssl=", ifi->ifi_ifname);
		action_argv = argv_dnssl;

		error = action_propget(action_argv, &cp);
		if (error)
			continue;

		len = *((uint16_t *)cp.cp_val);

		if (len > 0) {
			printf("\tDNSSL entries:\n");
			action_show_dnssl(cp.cp_val);
		}

		if (vflag < LOG_NOTICE)
			continue;

		printf("\n");

		printf("\tCounters\n"
		    "\t RA burst counts: %" PRIu16 " (interval: %s)\n"
		    "\t RS wait counts: %" PRIu16 "\n",
		    ifi_s->ifi_burstcount,
		    sec2str(ifi_s->ifi_burstinterval, ssbuf),
		    ifi_s->ifi_rs_waitcount);

		printf("\tOutputs\n"
		    "\t RA: %" PRIu64 "\n", ifi_s->ifi_raoutput);

		printf("\tInputs\n"
		    "\t RA: %" PRIu64 " (normal)\n"
		    "\t RA: %" PRIu64 " (inconsistent)\n"
		    "\t RS: %" PRIu64 "\n",
		    ifi_s->ifi_rainput,
		    ifi_s->ifi_rainconsistent,
		    ifi_s->ifi_rsinput);

		printf("\n");

#if 0	/* Not implemented yet */
		printf("\tReceived RAs:\n");
#endif
	}

	return (0);
}

static int
action_show_rtinfo(struct rtinfo *rti)
{
	char ntopbuf[INET6_ADDRSTRLEN];
	char ssbuf[SSBUFLEN];

	printf("\t  %s/%d (pref: %s, ltime: %s)\n",
	    inet_ntop(AF_INET6, &rti->rti_prefix,
		ntopbuf, sizeof(ntopbuf)),
	    rti->rti_prefixlen,
	    rtpref_str[0xff & (rti->rti_rtpref >> 3)],
	    (rti->rti_ltime == ND6_INFINITE_LIFETIME) ?
	    "infinity" : sec2str(rti->rti_ltime, ssbuf));

	return (0);
}

static int
action_show_prefix(struct prefix *pfx)
{
	char ntopbuf[INET6_ADDRSTRLEN];
	char ssbuf[SSBUFLEN];
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC_FAST, &now);
	printf("\t  %s/%d", inet_ntop(AF_INET6, &pfx->pfx_prefix,
		ntopbuf, sizeof(ntopbuf)), pfx->pfx_prefixlen);

	printf(" (");
	switch (pfx->pfx_origin) {
	case PREFIX_FROM_KERNEL:
		printf("KERNEL");
		break;
	case PREFIX_FROM_CONFIG:
		printf("CONFIG");
		break;
	case PREFIX_FROM_DYNAMIC:
		printf("DYNAMIC");
		break;
	}

	printf(",");

	printf(" vltime=%s",
	    (pfx->pfx_validlifetime == ND6_INFINITE_LIFETIME) ?
	    "infinity" : sec2str(pfx->pfx_validlifetime, ssbuf));

	if (pfx->pfx_vltimeexpire > 0)
		printf("(expire: %s)",
		    ((long)pfx->pfx_vltimeexpire > now.tv_sec) ?
		    sec2str(pfx->pfx_vltimeexpire - now.tv_sec, ssbuf) :
		    "0");

	printf(",");

	printf(" pltime=%s",
	    (pfx->pfx_preflifetime == ND6_INFINITE_LIFETIME) ?
	    "infinity" : sec2str(pfx->pfx_preflifetime, ssbuf));

	if (pfx->pfx_pltimeexpire > 0)
		printf("(expire %s)",
		    ((long)pfx->pfx_pltimeexpire > now.tv_sec) ?
		    sec2str(pfx->pfx_pltimeexpire - now.tv_sec, ssbuf) :
		    "0");

	printf(",");

	printf(" flags=");
	if (pfx->pfx_onlinkflg || pfx->pfx_autoconfflg) {
		printf("%s", pfx->pfx_onlinkflg ? "L" : "");
		printf("%s", pfx->pfx_autoconfflg ? "A" : "");
	} else
		printf("<none>");

	if (pfx->pfx_timer) {
		struct timespec *rest;

		rest = rtadvd_timer_rest(pfx->pfx_timer);
		if (rest) { /* XXX: what if not? */
			printf(" expire=%s", sec2str(rest->tv_sec, ssbuf));
		}
	}

	printf(")\n");

	return (0);
}

static int
action_show_rdnss(void *msg)
{
	struct rdnss *rdn;
	struct rdnss_addr *rda;
	uint16_t *rdn_cnt;
	uint16_t *rda_cnt;
	int i;
	int j;
	char *p;
	uint32_t	ltime;
	char ntopbuf[INET6_ADDRSTRLEN];
	char ssbuf[SSBUFLEN];

	p = msg;
	rdn_cnt = (uint16_t *)p;
	p += sizeof(*rdn_cnt);

	if (*rdn_cnt > 0) {
		for (i = 0; i < *rdn_cnt; i++) {
			rdn = (struct rdnss *)p;
			ltime = rdn->rd_ltime;
			p += sizeof(*rdn);

			rda_cnt = (uint16_t *)p;
			p += sizeof(*rda_cnt);
			if (*rda_cnt > 0)
				for (j = 0; j < *rda_cnt; j++) {
					rda = (struct rdnss_addr *)p;
					printf("\t  %s (ltime=%s)\n",
					    inet_ntop(AF_INET6,
						&rda->ra_dns,
						ntopbuf,
						sizeof(ntopbuf)),
					    sec2str(ltime, ssbuf));
					p += sizeof(*rda);
				}
		}
	}

	return (0);
}

static int
action_show_dnssl(void *msg)
{
	struct dnssl *dns;
	struct dnssl_addr *dna;
	uint16_t *dns_cnt;
	uint16_t *dna_cnt;
	int i;
	int j;
	char *p;
	uint32_t ltime;
	char hbuf[NI_MAXHOST];
	char ssbuf[SSBUFLEN];

	p = msg;
	dns_cnt = (uint16_t *)p;
	p += sizeof(*dns_cnt);

	if (*dns_cnt > 0) {
		for (i = 0; i < *dns_cnt; i++) {
			dns = (struct dnssl *)p;
			ltime = dns->dn_ltime;
			p += sizeof(*dns);

			dna_cnt = (uint16_t *)p;
			p += sizeof(*dna_cnt);
			if (*dna_cnt > 0)
				for (j = 0; j < *dna_cnt; j++) {
					dna = (struct dnssl_addr *)p;
					dname_labeldec(hbuf, sizeof(hbuf),
					    dna->da_dom);
					printf("\t  %s (ltime=%s)\n",
					    hbuf, sec2str(ltime, ssbuf));
					p += sizeof(*dna);
				}
		}
	}

	return (0);
}

/* Decode domain name label encoding in RFC 1035 Section 3.1 */
static size_t
dname_labeldec(char *dst, size_t dlen, const char *src)
{
	size_t len;
	const char *src_origin;
	const char *src_last;
	const char *dst_origin;

	src_origin = src;
	src_last = strchr(src, '\0');
	dst_origin = dst;
	memset(dst, '\0', dlen);
	while (src && (len = (uint8_t)(*src++) & 0x3f) &&
	    (src + len) <= src_last) {
		if (dst != dst_origin)
			*dst++ = '.';
		mysyslog(LOG_DEBUG, "<%s> labellen = %zd", __func__, len);
		memcpy(dst, src, len);
		src += len;
		dst += len;
	}
	*dst = '\0';

	return (src - src_origin);
}
