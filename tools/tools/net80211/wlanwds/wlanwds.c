/*-
 * Copyright (c) 2006-2009 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

/*
 * Test app to demonstrate how to handle dynamic WDS links:
 * o monitor 802.11 events for wds discovery events
 * o create wds vap's in response to wds discovery events
 *   and launch a script to handle adding the vap to the
 *   bridge, etc.
 * o destroy wds vap's when station leaves
 */
#include <sys/param.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <net/if.h>
#include "net/if_media.h"
#include <net/route.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include "net80211/ieee80211_ioctl.h"
#include "net80211/ieee80211_freebsd.h"
#include <arpa/inet.h>
#include <netdb.h>

#include <net/if.h>
#include <net/if_types.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <paths.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <syslog.h>
#include <unistd.h>
#include <ifaddrs.h>

#define	IEEE80211_ADDR_EQ(a1,a2)	(memcmp(a1,a2,IEEE80211_ADDR_LEN) == 0)
#define	IEEE80211_ADDR_COPY(dst,src)	memcpy(dst,src,IEEE80211_ADDR_LEN)

struct wds {
	struct wds *next;
	uint8_t	bssid[IEEE80211_ADDR_LEN];	/* bssid of associated sta */
	char	ifname[IFNAMSIZ];		/* vap interface name */
};
static struct wds *wds;

static	const char *script = NULL;
static	char **ifnets;
static	int nifnets = 0;
static	int verbose = 0;
static	int discover_on_join = 0;

static	void scanforvaps(int s);
static	void handle_rtmsg(struct rt_msghdr *rtm, ssize_t msglen);
static	void wds_discovery(const char *ifname,
		const uint8_t bssid[IEEE80211_ADDR_LEN]);
static	void wds_destroy(const char *ifname);
static	void wds_leave(const uint8_t bssid[IEEE80211_ADDR_LEN]);
static	int wds_vap_create(const char *ifname, uint8_t macaddr[ETHER_ADDR_LEN],
	    struct wds *);
static	int wds_vap_destroy(const char *ifname);

static void
usage(const char *progname)
{
	fprintf(stderr, "usage: %s [-efjtv] [-P pidfile] [-s <set_scriptname>] [ifnet0 ... | any]\n",
		progname);
	exit(-1);
}

int
main(int argc, char *argv[])
{
	const char *progname = argv[0];
	const char *pidfile = NULL;
	int s, c, logmask, bg = 1;
	char msg[2048];
	int log_stderr = 0;

	logmask = LOG_UPTO(LOG_INFO);
	while ((c = getopt(argc, argv, "efjP:s:tv")) != -1)
		switch (c) {
		case 'e':
			log_stderr = LOG_PERROR;
			break;
		case 'f':
			bg = 0;
			break;
		case 'j':
			discover_on_join = 1;
			break;
		case 'P':
			pidfile = optarg;
			break;
		case 's':
			script = optarg;
			break;
		case 't':
			logmask = LOG_UPTO(LOG_ERR);
			break;
		case 'v':
			logmask = LOG_UPTO(LOG_DEBUG);
			break;
		case '?':
			usage(progname);
			/*NOTREACHED*/
		}
	argc -= optind, argv += optind;
	if (argc == 0) {
		fprintf(stderr, "%s: no ifnet's specified to monitor\n",
		    progname);
		usage(progname);
	}
	ifnets = argv;
	nifnets = argc;

	s = socket(PF_ROUTE, SOCK_RAW, 0);
	if (s < 0)
		err(EX_OSERR, "socket");
	/*
	 * Scan for inherited state.
	 */
	scanforvaps(s);

	/* XXX what directory to work in? */
	if (bg && daemon(0, 0) < 0)
		err(EX_OSERR, "daemon");

	openlog("wlanwds", log_stderr | LOG_PID | LOG_CONS, LOG_DAEMON);
	setlogmask(logmask);

	for (;;) {
		ssize_t n = read(s, msg, sizeof(msg));
		handle_rtmsg((struct rt_msghdr *)msg, n);
	}
	return 0;
}

static const char *
ether_sprintf(const uint8_t mac[IEEE80211_ADDR_LEN])
{
	static char buf[32];

	snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return buf;
}

/*
 * Fetch a vap's parent ifnet name.
 */
static int
getparent(const char *ifname, char parent[IFNAMSIZ+1])
{
	char oid[256];
	size_t parentlen;

	/* fetch parent interface name */
	snprintf(oid, sizeof(oid), "net.wlan.%s.%%parent", ifname+4);
	parentlen = IFNAMSIZ;
	if (sysctlbyname(oid, parent, &parentlen, NULL, 0) < 0)
		return -1;
	parent[parentlen] = '\0';
	return 0;
}

/*
 * Check if the specified ifnet is one we're supposed to monitor.
 * The ifnet is assumed to be a vap; we find it's parent and check
 * it against the set of ifnet's specified on the command line.
 *
 * TODO: extend this to also optionally allow the specific DWDS
 * VAP to be monitored, instead of assuming all VAPs on a parent
 * physical interface are being monitored by this instance of
 * wlanwds.
 */
static int
checkifnet(const char *ifname, int complain)
{
	char parent[256];
	int i;

	if (getparent(ifname, parent) < 0) {
		if (complain)
			syslog(LOG_ERR,
			   "%s: no pointer to parent interface: %m", ifname);
		return 0;
	}

	for (i = 0; i < nifnets; i++)
		if (strcasecmp(ifnets[i], "any") == 0 ||
		    strcmp(ifnets[i], parent) == 0)
			return 1;
	syslog(LOG_DEBUG, "%s: parent %s not being monitored", ifname, parent);
	return 0;
}

/*
 * Return 1 if the specified ifnet is a WDS vap.
 */
static int
iswdsvap(int s, const char *ifname)
{
	struct ifmediareq ifmr;

	memset(&ifmr, 0, sizeof(ifmr));
	strncpy(ifmr.ifm_name, ifname, sizeof(ifmr.ifm_name));
	if (ioctl(s, SIOCGIFMEDIA, (caddr_t)&ifmr) < 0)
		err(-1, "%s: cannot get media", ifname);
	return (ifmr.ifm_current & IFM_IEEE80211_WDS) != 0;
}

/*
 * Fetch the bssid for an ifnet.  The caller is assumed
 * to have already verified this is possible.
 */
static void
getbssid(int s, const char *ifname, uint8_t bssid[IEEE80211_ADDR_LEN])
{
	struct ieee80211req ireq;

	memset(&ireq, 0, sizeof(ireq));
	strncpy(ireq.i_name, ifname, sizeof(ireq.i_name));
	ireq.i_type = IEEE80211_IOC_BSSID;
	ireq.i_data = bssid;
	ireq.i_len = IEEE80211_ADDR_LEN;
	if (ioctl(s, SIOCG80211, &ireq) < 0)
		err(-1, "%s: cannot fetch bssid", ifname);
}

/*
 * Fetch the mac address configured for a given ifnet.
 * (Note - the current link level address, NOT hwaddr.)
 *
 * This is currently, sigh, O(n) because there's no current kernel
 * API that will do it for a single interface.
 *
 * Return 0 if successful, -1 if failure.
 */
static int
getlladdr(const char *ifname, uint8_t macaddr[ETHER_ADDR_LEN])
{
	struct ifaddrs *ifap, *ifa;
	struct sockaddr_dl *sdl;

	if (getifaddrs(&ifap) < 0) {
		warn("%s: getifaddrs", __func__);
		return (-1);
	}

	/* Look for a matching interface */
	for (ifa = ifap; ifa != NULL; ifa++) {
		if (strcmp(ifname, ifa->ifa_name) != 0)
			continue;

		/* Found it - check if there's an ifa_addr */
		if (ifa->ifa_addr == NULL) {
			syslog(LOG_CRIT, "%s: ifname %s; ifa_addr is NULL\n",
			    __func__, ifname);
			goto err;
		}

		/* Check address family */
		sdl = (struct sockaddr_dl *) ifa->ifa_addr;
		if (sdl->sdl_type != IFT_ETHER) {
			syslog(LOG_CRIT, "%s: %s: unknown aftype (%d)\n",
			    __func__,
			    ifname,
			    sdl->sdl_type);
			goto err;
		}
		if (sdl->sdl_alen != ETHER_ADDR_LEN) {
			syslog(LOG_CRIT, "%s: %s: aflen too short (%d)\n",
			    __func__,
			    ifname,
			    sdl->sdl_alen);
			goto err;
		}

		/* Ok, found it */
		memcpy(macaddr, (void *) LLADDR(sdl), ETHER_ADDR_LEN);
		goto ok;
	}
	syslog(LOG_CRIT, "%s: couldn't find ifname %s\n", __func__, ifname);
	/* FALLTHROUGH */
err:
	freeifaddrs(ifap);
	return (-1);

ok:
	freeifaddrs(ifap);
	return (0);
}

/*
 * Scan the system for WDS vaps associated with the ifnet's we're
 * supposed to monitor.  Any vaps are added to our internal table
 * so we can find them (and destroy them) on station leave.
 */
static void
scanforvaps(int s)
{
	char ifname[IFNAMSIZ+1];
	uint8_t bssid[IEEE80211_ADDR_LEN];
	int i;

	/* XXX brutal; should just walk sysctl tree */
	for (i = 0; i < 128; i++) {
		snprintf(ifname, sizeof(ifname), "wlan%d", i);
		if (checkifnet(ifname, 0) && iswdsvap(s, ifname)) {
			struct wds *p = malloc(sizeof(struct wds));
			if (p == NULL)
				err(-1, "%s: malloc failed", __func__);
			strlcpy(p->ifname, ifname, IFNAMSIZ);
			getbssid(s, ifname, p->bssid);
			p->next = wds;
			wds = p;

			syslog(LOG_INFO, "[%s] discover wds vap %s",
			    ether_sprintf(bssid), ifname);
		}
	}
}

/*
 * Process a routing socket message.  We handle messages related
 * to dynamic WDS:
 * o on WDS discovery (rx of a 4-address frame with DWDS enabled)
 *   we create a WDS vap for the specified mac address
 * o on station leave we destroy any associated WDS vap
 * o on ifnet destroy we update state if this is manual destroy of
 *   a WDS vap in our table
 * o if the -j option is supplied on the command line we create
 *   WDS vaps on station join/rejoin, this is useful for some setups
 *   where a WDS vap is required for 4-address traffic to flow
 */
static void
handle_rtmsg(struct rt_msghdr *rtm, ssize_t msglen)
{
	struct if_announcemsghdr *ifan;

	if (rtm->rtm_version != RTM_VERSION) {
		syslog(LOG_ERR, "routing message version %d not understood",
		    rtm->rtm_version);
		return;
	}
	switch (rtm->rtm_type) {
	case RTM_IFANNOUNCE:
		ifan = (struct if_announcemsghdr *)rtm;
		switch (ifan->ifan_what) {
		case IFAN_ARRIVAL:
			syslog(LOG_DEBUG,
			    "RTM_IFANNOUNCE: if# %d, what: arrival",
			    ifan->ifan_index);
			break;
		case IFAN_DEPARTURE:
			syslog(LOG_DEBUG,
			    "RTM_IFANNOUNCE: if# %d, what: departure",
			    ifan->ifan_index);
			/* NB: ok to call w/ unmonitored ifnets */
			wds_destroy(ifan->ifan_name);
			break;
		}
		break;
	case RTM_IEEE80211:
#define	V(type)	((struct type *)(&ifan[1]))
		ifan = (struct if_announcemsghdr *)rtm;
		switch (ifan->ifan_what) {
		case RTM_IEEE80211_DISASSOC:
			if (!discover_on_join)
				break;
			/* fall thru... */
		case RTM_IEEE80211_LEAVE:
			if (!checkifnet(ifan->ifan_name, 1))
				break;
			syslog(LOG_INFO, "[%s] station leave",
			    ether_sprintf(V(ieee80211_leave_event)->iev_addr));
			wds_leave(V(ieee80211_leave_event)->iev_addr);
			break;
		case RTM_IEEE80211_JOIN:
		case RTM_IEEE80211_REJOIN:
		case RTM_IEEE80211_ASSOC:
		case RTM_IEEE80211_REASSOC:
			if (!discover_on_join)
				break;
			/* fall thru... */
		case RTM_IEEE80211_WDS:
			syslog(LOG_INFO, "[%s] wds discovery",
			    ether_sprintf(V(ieee80211_wds_event)->iev_addr));
			if (!checkifnet(ifan->ifan_name, 1))
				break;
			wds_discovery(ifan->ifan_name,
			    V(ieee80211_wds_event)->iev_addr);
			break;
		}
		break;
#undef V
	}
}

/*
 * Handle WDS discovery; create a WDS vap for the specified bssid.
 * If a vap already exists then do nothing (can happen when a flood
 * of 4-address frames causes multiple events to be queued before
 * we create a vap).
 */
static void
wds_discovery(const char *ifname, const uint8_t bssid[IEEE80211_ADDR_LEN])
{
	struct wds *p;
	char parent[256];
	char cmd[1024];
	uint8_t macaddr[ETHER_ADDR_LEN];
	int status;

	for (p = wds; p != NULL; p = p->next)
		if (IEEE80211_ADDR_EQ(p->bssid, bssid)) {
			syslog(LOG_INFO, "[%s] wds vap already created (%s)",
			    ether_sprintf(bssid), ifname);
			return;
		}
	if (getparent(ifname, parent) < 0) {
		syslog(LOG_ERR, "%s: no pointer to parent interface: %m",
		    ifname);
		return;
	}

	if (getlladdr(ifname, macaddr) < 0) {
		syslog(LOG_ERR, "%s: couldn't get lladdr for parent interface: %m",
		    ifname);
		return;
	}

	p = malloc(sizeof(struct wds));
	if (p == NULL) {
		syslog(LOG_ERR, "%s: malloc failed: %m", __func__);
		return;
	}
	IEEE80211_ADDR_COPY(p->bssid, bssid);
	if (wds_vap_create(parent, macaddr, p) < 0) {
		free(p);
		return;
	}
	/*
	 * Add to table and launch setup script.
	 */
	p->next = wds;
	wds = p;
	syslog(LOG_INFO, "[%s] create wds vap %s, parent %s (%s)",
	    ether_sprintf(bssid),
	    p->ifname,
	    ifname,
	    parent);
	if (script != NULL) {
		snprintf(cmd, sizeof(cmd), "%s %s", script, p->ifname);
		status = system(cmd);
		if (status)
			syslog(LOG_ERR, "vap setup script %s exited with "
			    "status %d", script, status);
	}
}

/* 
 * Destroy a WDS vap (if known).
 */
static void
wds_destroy(const char *ifname)
{
	struct wds *p, **pp;

	for (pp = &wds; (p = *pp) != NULL; pp = &p->next)
		if (strncmp(p->ifname, ifname, IFNAMSIZ) == 0)
			break;
	if (p != NULL) {
		*pp = p->next;
		/* NB: vap already destroyed */
		free(p);
		return;
	}
}

/*
 * Handle a station leave event; destroy any associated WDS vap.
 */
static void
wds_leave(const uint8_t bssid[IEEE80211_ADDR_LEN])
{
	struct wds *p, **pp;

	for (pp = &wds; (p = *pp) != NULL; pp = &p->next)
		if (IEEE80211_ADDR_EQ(p->bssid, bssid))
			break;
	if (p != NULL) {
		*pp = p->next;
		if (wds_vap_destroy(p->ifname) >= 0)
			syslog(LOG_INFO, "[%s] wds vap %s destroyed",
			    ether_sprintf(bssid), p->ifname);
		free(p);
	}
}

static int
wds_vap_create(const char *parent, uint8_t macaddr[ETHER_ADDR_LEN],
    struct wds *p)
{
	struct ieee80211_clone_params cp;
	struct ifreq ifr;
	int s, status;
	char bssid_str[32], macaddr_str[32];

	memset(&cp, 0, sizeof(cp));

	/* Parent interface */
	strncpy(cp.icp_parent, parent, IFNAMSIZ);

	/* WDS interface */
	cp.icp_opmode = IEEE80211_M_WDS;

	/* BSSID for the current node */
	IEEE80211_ADDR_COPY(cp.icp_bssid, p->bssid);

	/*
	 * Set the MAC address to match the actual interface
	 * that we received the discovery event from.
	 * That way we can run WDS on any VAP rather than
	 * only the first VAP and then correctly set the
	 * MAC address.
	 */
	cp.icp_flags |= IEEE80211_CLONE_MACADDR;
	IEEE80211_ADDR_COPY(cp.icp_macaddr, macaddr);

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, "wlan", IFNAMSIZ);
	ifr.ifr_data = (void *) &cp;

	status = -1;
	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s >= 0) {
		if (ioctl(s, SIOCIFCREATE2, &ifr) >= 0) {
			strlcpy(p->ifname, ifr.ifr_name, IFNAMSIZ);
			status = 0;
		} else {
			syslog(LOG_ERR, "SIOCIFCREATE2("
			    "mode %u flags 0x%x parent %s bssid %s macaddr %s): %m",
			    cp.icp_opmode, cp.icp_flags, parent,
			    ether_ntoa_r((void *) cp.icp_bssid, bssid_str),
			    ether_ntoa_r((void *) cp.icp_macaddr, macaddr_str));
		}
		close(s);
	} else
		syslog(LOG_ERR, "socket(SOCK_DRAGM): %m");
	return status;
}

static int
wds_vap_destroy(const char *ifname)
{
	struct ieee80211req ifr;
	int s, status;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0) {
		syslog(LOG_ERR, "socket(SOCK_DRAGM): %m");
		return -1;
	}
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.i_name, ifname, IFNAMSIZ);
	if (ioctl(s, SIOCIFDESTROY, &ifr) < 0) {
		syslog(LOG_ERR, "ioctl(SIOCIFDESTROY): %m");
		status = -1;
	} else
		status = 0;
	close(s);
	return status;
}
