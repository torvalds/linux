/*-
 * Copyright (c) 2002-2007 Sam Leffler, Errno Consulting
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
 * Monitor 802.11 events using a routing socket.
 * Code liberaly swiped from route(8).
 */
#include <sys/param.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <net/if.h>
#include <net/route.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#ifdef __NetBSD__
#include <net80211/ieee80211_netbsd.h>
#elif __FreeBSD__
#include <net80211/ieee80211_freebsd.h>
#else
#error	"No support for your operating system!"
#endif
#include <arpa/inet.h>
#include <netdb.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <ifaddrs.h>

/* XXX */
enum ieee80211_notify_cac_event {
	IEEE80211_NOTIFY_CAC_START  = 0, /* CAC timer started */
	IEEE80211_NOTIFY_CAC_STOP   = 1, /* CAC intentionally stopped */
	IEEE80211_NOTIFY_CAC_RADAR  = 2, /* CAC stopped due to radar detectio */
	IEEE80211_NOTIFY_CAC_EXPIRE = 3, /* CAC expired w/o radar */
};

static	void print_rtmsg(struct rt_msghdr *rtm, int msglen);

int	nflag = 0;

int
main(int argc, char *argv[])
{
	int n, s;
	char msg[2048];

	s = socket(PF_ROUTE, SOCK_RAW, 0);
	if (s < 0)
		err(EX_OSERR, "socket");
	for(;;) {
		n = read(s, msg, 2048);
		print_rtmsg((struct rt_msghdr *)msg, n);
	}
	return 0;
}

static void
bprintf(FILE *fp, int b, char *s)
{
	int i;
	int gotsome = 0;

	if (b == 0)
		return;
	while ((i = *s++) != 0) {
		if (b & (1 << (i-1))) {
			if (gotsome == 0)
				i = '<';
			else
				i = ',';
			(void) putc(i, fp);
			gotsome = 1;
			for (; (i = *s) > 32; s++)
				(void) putc(i, fp);
		} else
			while (*s > 32)
				s++;
	}
	if (gotsome)
		putc('>', fp);
}

char metricnames[] =
"\011pksent\010rttvar\7rtt\6ssthresh\5sendpipe\4recvpipe\3expire\2hopcount"
"\1mtu";
char routeflags[] =
"\1UP\2GATEWAY\3HOST\4REJECT\5DYNAMIC\6MODIFIED\7DONE\010MASK_PRESENT"
"\011CLONING\012XRESOLVE\013LLINFO\014STATIC\015BLACKHOLE\016b016"
"\017PROTO2\020PROTO1\021PRCLONING\022WASCLONED\023PROTO3\024CHAINDELETE"
"\025PINNED\026LOCAL\027BROADCAST\030MULTICAST";
char ifnetflags[] =
"\1UP\2BROADCAST\3DEBUG\4LOOPBACK\5PTP\6b6\7RUNNING\010NOARP"
"\011PPROMISC\012ALLMULTI\013OACTIVE\014SIMPLEX\015LINK0\016LINK1"
"\017LINK2\020MULTICAST";
char addrnames[] =
"\1DST\2GATEWAY\3NETMASK\4GENMASK\5IFP\6IFA\7AUTHOR\010BRD";

static const char *
routename(struct sockaddr *sa)
{
	char *cp;
	static char line[MAXHOSTNAMELEN + 1];
	struct hostent *hp;
	static char domain[MAXHOSTNAMELEN + 1];
	static int first = 1, n;

	if (first) {
		first = 0;
		if (gethostname(domain, MAXHOSTNAMELEN) == 0 &&
		    (cp = strchr(domain, '.'))) {
			domain[MAXHOSTNAMELEN] = '\0';
			(void) strcpy(domain, cp + 1);
		} else
			domain[0] = 0;
	}

	if (sa->sa_len == 0)
		strcpy(line, "default");
	else switch (sa->sa_family) {

	case AF_INET:
	    {	struct in_addr in;
		in = ((struct sockaddr_in *)sa)->sin_addr;

		cp = 0;
		if (in.s_addr == INADDR_ANY || sa->sa_len < 4)
			cp = "default";
		if (cp == 0 && !nflag) {
			hp = gethostbyaddr((char *)&in, sizeof (struct in_addr),
				AF_INET);
			if (hp) {
				if ((cp = strchr(hp->h_name, '.')) &&
				    !strcmp(cp + 1, domain))
					*cp = 0;
				cp = hp->h_name;
			}
		}
		if (cp) {
			strncpy(line, cp, sizeof(line) - 1);
			line[sizeof(line) - 1] = '\0';
		} else
			(void) sprintf(line, "%s", inet_ntoa(in));
		break;
	    }

#ifdef INET6
	case AF_INET6:
	{
		struct sockaddr_in6 sin6; /* use static var for safety */
		int niflags = 0;
#ifdef NI_WITHSCOPEID
		niflags = NI_WITHSCOPEID;
#endif

		memset(&sin6, 0, sizeof(sin6));
		memcpy(&sin6, sa, sa->sa_len);
		sin6.sin6_len = sizeof(struct sockaddr_in6);
		sin6.sin6_family = AF_INET6;
#ifdef __KAME__
		if (sa->sa_len == sizeof(struct sockaddr_in6) &&
		    (IN6_IS_ADDR_LINKLOCAL(&sin6.sin6_addr) ||
		     IN6_IS_ADDR_MC_LINKLOCAL(&sin6.sin6_addr)) &&
		    sin6.sin6_scope_id == 0) {
			sin6.sin6_scope_id =
			    ntohs(*(u_int16_t *)&sin6.sin6_addr.s6_addr[2]);
			sin6.sin6_addr.s6_addr[2] = 0;
			sin6.sin6_addr.s6_addr[3] = 0;
		}
#endif
		if (nflag)
			niflags |= NI_NUMERICHOST;
		if (getnameinfo((struct sockaddr *)&sin6, sin6.sin6_len,
		    line, sizeof(line), NULL, 0, niflags) != 0)
			strncpy(line, "invalid", sizeof(line));

		return(line);
	}
#endif

	case AF_LINK:
		return (link_ntoa((struct sockaddr_dl *)sa));

	default:
	    {	u_short *s = (u_short *)sa;
		u_short *slim = s + ((sa->sa_len + 1) >> 1);
		char *cp = line + sprintf(line, "(%d)", sa->sa_family);
		char *cpe = line + sizeof(line);

		while (++s < slim && cp < cpe) /* start with sa->sa_data */
			if ((n = snprintf(cp, cpe - cp, " %x", *s)) > 0)
				cp += n;
			else
				*cp = '\0';
		break;
	    }
	}
	return (line);
}

#ifndef SA_SIZE
/*
 * This macro returns the size of a struct sockaddr when passed
 * through a routing socket. Basically we round up sa_len to
 * a multiple of sizeof(long), with a minimum of sizeof(long).
 * The check for a NULL pointer is just a convenience, probably never used.
 * The case sa_len == 0 should only apply to empty structures.
 */
#define SA_SIZE(sa)						\
    (  (!(sa) || ((struct sockaddr *)(sa))->sa_len == 0) ?	\
	sizeof(long)		:				\
	1 + ( (((struct sockaddr *)(sa))->sa_len - 1) | (sizeof(long) - 1) ) )
#endif

static void
pmsg_addrs(char *cp, int addrs)
{
	struct sockaddr *sa;
	int i;

	if (addrs == 0) {
		(void) putchar('\n');
		return;
	}
	printf("\nsockaddrs: ");
	bprintf(stdout, addrs, addrnames);
	putchar('\n');
	for (i = 1; i; i <<= 1)
		if (i & addrs) {
			sa = (struct sockaddr *)cp;
			printf(" %s", routename(sa));
			cp += SA_SIZE(sa);
		}
	putchar('\n');
}

static const char *
ether_sprintf(const uint8_t mac[6])
{
	static char buf[32];

	snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return buf;
}

static void
print_rtmsg(struct rt_msghdr *rtm, int msglen)
{
	struct if_msghdr *ifm;
	struct if_announcemsghdr *ifan;
	time_t now = time(NULL);
	char *cnow = ctime(&now);

	if (rtm->rtm_version != RTM_VERSION) {
		(void) printf("routing message version %d not understood\n",
		    rtm->rtm_version);
		return;
	}
	switch (rtm->rtm_type) {
	case RTM_IFINFO:
		ifm = (struct if_msghdr *)rtm;
		printf("%.19s RTM_IFINFO: if# %d, ",
			cnow, ifm->ifm_index);
		switch (ifm->ifm_data.ifi_link_state) {
		case LINK_STATE_DOWN:
			printf("link: down, flags:");
			break;
		case LINK_STATE_UP:
			printf("link: up, flags:");
			break;
		default:
			printf("link: unknown<%d>, flags:",
			    ifm->ifm_data.ifi_link_state);
			break;
		}
		bprintf(stdout, ifm->ifm_flags, ifnetflags);
		pmsg_addrs((char *)(ifm + 1), ifm->ifm_addrs);
		fflush(stdout);
		break;
	case RTM_IFANNOUNCE:
		ifan = (struct if_announcemsghdr *)rtm;
		printf("%.19s RTM_IFANNOUNCE: if# %d, what: ",
			cnow, ifan->ifan_index);
		switch (ifan->ifan_what) {
		case IFAN_ARRIVAL:
			printf("arrival");
			break;
		case IFAN_DEPARTURE:
			printf("departure");
			break;
		default:
			printf("#%d", ifan->ifan_what);
			break;
		}
		printf("\n");
		fflush(stdout);
		break;
	case RTM_IEEE80211:
#define	V(type)	((struct type *)(&ifan[1]))
		ifan = (struct if_announcemsghdr *)rtm;
		printf("%.19s RTM_IEEE80211: if# %d, ", cnow, ifan->ifan_index);
		switch (ifan->ifan_what) {
		case RTM_IEEE80211_ASSOC:
			printf("associate with %s",
			    ether_sprintf(V(ieee80211_join_event)->iev_addr));
			break;
		case RTM_IEEE80211_REASSOC:
			printf("reassociate with %s",
			    ether_sprintf(V(ieee80211_join_event)->iev_addr));
			break;
		case RTM_IEEE80211_DISASSOC:
			printf("disassociate");
			break;
		case RTM_IEEE80211_JOIN:
		case RTM_IEEE80211_REJOIN:
			printf("%s station %sjoin",
			    ether_sprintf(V(ieee80211_join_event)->iev_addr),
			    ifan->ifan_what == RTM_IEEE80211_REJOIN ? "re" : ""
			);
			break;
		case RTM_IEEE80211_LEAVE:
			printf("%s station leave",
			    ether_sprintf(V(ieee80211_leave_event)->iev_addr));
			break;
		case RTM_IEEE80211_SCAN:
			printf("scan complete");
			break;
		case RTM_IEEE80211_REPLAY:
			printf("replay failure: src %s "
			    , ether_sprintf(V(ieee80211_replay_event)->iev_src)
			);
			printf("dst %s cipher %u keyix %u keyrsc %llu rsc %llu"
			    , ether_sprintf(V(ieee80211_replay_event)->iev_dst)
			    , V(ieee80211_replay_event)->iev_cipher
			    , V(ieee80211_replay_event)->iev_keyix
			    , V(ieee80211_replay_event)->iev_keyrsc
			    , V(ieee80211_replay_event)->iev_rsc
			);
			break;
		case RTM_IEEE80211_MICHAEL:
			printf("michael failure: src %s "
			    , ether_sprintf(V(ieee80211_michael_event)->iev_src)
			);
			printf("dst %s cipher %u keyix %u"
			    , ether_sprintf(V(ieee80211_michael_event)->iev_dst)
			    , V(ieee80211_michael_event)->iev_cipher
			    , V(ieee80211_michael_event)->iev_keyix
			);
			break;
		case RTM_IEEE80211_WDS:
			printf("%s wds discovery",
			    ether_sprintf(V(ieee80211_wds_event)->iev_addr));
			break;
		case RTM_IEEE80211_CSA:
			printf("channel switch announcement: channel %u (%u MHz flags 0x%x) mode %d count %d"
			    , V(ieee80211_csa_event)->iev_ieee
			    , V(ieee80211_csa_event)->iev_freq
			    , V(ieee80211_csa_event)->iev_flags
			    , V(ieee80211_csa_event)->iev_mode
			    , V(ieee80211_csa_event)->iev_count
			);
			break;
		case RTM_IEEE80211_CAC:
			printf("channel availability check "
			    "(channel %u, %u MHz flags 0x%x) "
			    , V(ieee80211_cac_event)->iev_ieee
			    , V(ieee80211_cac_event)->iev_freq
			    , V(ieee80211_cac_event)->iev_flags
			);
			switch (V(ieee80211_cac_event)->iev_type) {
			case IEEE80211_NOTIFY_CAC_START:
				printf("start timer");
				break;
			case IEEE80211_NOTIFY_CAC_STOP:
				printf("stop timer");
				break;
			case IEEE80211_NOTIFY_CAC_EXPIRE:
				printf("timer expired");
				break;
			case IEEE80211_NOTIFY_CAC_RADAR:
				printf("radar detected");
				break;
			default:
				printf("unknown type %d",
				   V(ieee80211_cac_event)->iev_type);
				break;
			}
			break;
		case RTM_IEEE80211_DEAUTH:
			printf("%s wds deauth",
			    ether_sprintf(V(ieee80211_deauth_event)->iev_addr));
			break;
		case RTM_IEEE80211_AUTH:
			printf("%s node authenticate",
			    ether_sprintf(V(ieee80211_auth_event)->iev_addr));
			break;
		case RTM_IEEE80211_COUNTRY:
			printf("%s adopt country code '%c%c'",
			    ether_sprintf(V(ieee80211_country_event)->iev_addr),
			    V(ieee80211_country_event)->iev_cc[0],
			    V(ieee80211_country_event)->iev_cc[1]);
			break;
		case RTM_IEEE80211_RADIO:
			printf("radio %s",
			    V(ieee80211_radio_event)->iev_state ? "ON" : "OFF");
			break;
		default:
			printf("what: #%d", ifan->ifan_what);
			break;
		}
		printf("\n");
		fflush(stdout);
		break;
#undef V
	}
}
