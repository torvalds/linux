/*	$OpenBSD: dhcrelay6.c,v 1.6 2025/05/21 05:09:17 kn Exp $	*/

/*
 * Copyright (c) 2017 Rafael Zalamena <rzalamena@openbsd.org>
 * Copyright (c) 2004 Henning Brauer <henning@cvs.openbsd.org>
 * Copyright (c) 1997, 1998, 1999 The Internet Software Consortium.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The Internet Software Consortium nor the names
 *    of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INTERNET SOFTWARE CONSORTIUM AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE INTERNET SOFTWARE CONSORTIUM OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This software has been written for the Internet Software Consortium
 * by Ted Lemon <mellon@fugue.com> in cooperation with Vixie
 * Enterprises.  To learn more about the Internet Software Consortium,
 * see ``http://www.vix.com/isc''.  To learn more about Vixie
 * Enterprises, see ``http://www.vix.com''.
 */

#include <sys/socket.h>

#include <arpa/inet.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/udp.h>
#include <netinet/if_ether.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "dhcp.h"
#include "dhcpd.h"
#include "log.h"

/*
 * RFC 3315 Section 5.1 Multicast Addresses:
 * All_DHCP_Relay_Agents_and_Servers: FF02::1:2
 * All_DHCP_Servers: FF05::1:3
 */
struct in6_addr		 in6alldhcprelay = {
	{{ 0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0, 0x02 }}
};
struct in6_addr		 in6alldhcp = {
	{{ 0xff, 0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0, 0x03 }}
};

__dead void usage(void);
struct server_list *parse_destination(const char *);
void	 relay6_setup(void);
int	 s6fromaddr(struct sockaddr_in6 *, const char *, const char *, int);
char	*print_hw_addr(int, int, unsigned char *);
const char *dhcp6type2str(uint8_t);
int	 relay6_pushrelaymsg(struct packet_ctx *, struct interface_info *,
	    uint8_t *, size_t *, size_t);
int	 relay6_poprelaymsg(struct packet_ctx *, struct interface_info **,
	    uint8_t *, size_t *);
void	 rai_configure(struct packet_ctx *, struct interface_info *);
void	 relay6_logsrcaddr(struct packet_ctx *, struct interface_info *,
	    uint8_t);
void	 relay6(struct interface_info *, void *, size_t,
	    struct packet_ctx *);
void	 mcast6_recv(struct protocol *);

/* Shared variables */
int			 clientsd;
int			 serversd;
int			 oflag;
time_t			 cur_time;

struct intfq		 intflist;
struct serverq		 svlist;
struct interface_info	*interfaces;
char			*rai_data;
size_t			 rai_datalen;
uint32_t		 enterpriseno = OPENBSD_ENTERPRISENO;
char			*remote_data;
size_t			 remote_datalen;
enum dhcp_relay_mode	 drm = DRM_LAYER3;

__dead void
usage(void)
{
	extern char	*__progname;

	fprintf(stderr, "usage: %s [-dlov] [-E enterprise-number] "
	    "[-I interface-id] [-R remote-id]\n"
	    "\t-i interface destination ...\n",
	    __progname);
	exit(1);
}

struct server_list *
parse_destination(const char *dest)
{
	struct server_list	*sp;
	const char		*ifname;
	char			 buf[128];

	if ((sp = calloc(1, sizeof(*sp))) == NULL)
		fatal("calloc");
	TAILQ_INSERT_HEAD(&svlist, sp, entry);

	/* Detect interface only destinations. */
	if ((sp->intf = iflist_getbyname(dest)) != NULL)
		return sp;

	/* Split address from interface and save it. */
	ifname = strchr(dest, '%');
	if (ifname == NULL)
		fatalx("%s doesn't specify an output interface", dest);

	if (strlcpy(buf, dest, sizeof(buf)) >= sizeof(buf))
		fatalx("%s is an invalid IPv6 address", dest);

	/* Remove '%' from the address string. */
	buf[ifname - dest] = 0;
	if ((sp->intf = iflist_getbyname(ifname + 1)) == NULL)
		fatalx("interface '%s' not found", ifname);
	if (s6fromaddr(ss2sin6(&sp->to), buf,
	    DHCP6_SERVER_PORT_STR, 1) == -1)
		fatalx("%s: unknown host", buf);

	/*
	 * User configured a non-local address, we must require a
	 * proper address to route this.
	 */
	if (!IN6_IS_ADDR_LINKLOCAL(&ss2sin6(&sp->to)->sin6_addr))
		sp->siteglobaladdr = 1;

	return sp;
}

int
main(int argc, char *argv[])
{
	int			 daemonize = 1, debug = 0;
	const char		*errp;
	struct passwd		*pw;
	int			 ch;

	log_init(1, LOG_DAEMON);	/* log to stderr until daemonized */
	log_setverbose(1);

	setup_iflist();

	while ((ch = getopt(argc, argv, "dE:I:i:loR:v")) != -1) {
		switch (ch) {
		case 'd':
			daemonize = 0;
			break;
		case 'E':
			enterpriseno = strtonum(optarg, 1, UINT32_MAX, &errp);
			if (errp != NULL)
				fatalx("invalid enterprise number: %s", errp);
			break;
		case 'I':
			rai_data = optarg;
			rai_datalen = strlen(optarg);
			if (rai_datalen == 0)
				fatalx("can't use empty Interface-ID");
			break;
		case 'i':
			if (interfaces != NULL)
				usage();

			interfaces = iflist_getbyname(optarg);
			if (interfaces == NULL)
				fatalx("interface '%s' not found", optarg);
			break;
		case 'l':
			drm = DRM_LAYER2;
			break;
		case 'o':
			oflag = 1;
			break;
		case 'R':
			remote_data = optarg;
			remote_datalen = strlen(remote_data);
			if (remote_datalen == 0)
				fatalx("can't use empty Remote-ID");
			break;
		case 'v':
			daemonize = 0;
			debug = 1;
			break;

		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;
	while (argc > 0) {
		parse_destination(argv[0]);
		argc--;
		argv++;
	}

	if (interfaces == NULL)
		fatalx("no interface given");
	if (TAILQ_EMPTY(&svlist))
		fatalx("no destination selected");

	relay6_setup();
	bootp_packet_handler = relay6;

	tzset();
	time(&cur_time);

	if ((pw = getpwnam(DHCRELAY6_USER)) == NULL)
		fatalx("user \"%s\" not found", DHCRELAY6_USER);
	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	if (daemonize) {
		if (daemon(0, 0) == -1)
			fatal("daemon");

		log_init(0, LOG_DAEMON);	/* stop logging to stderr */
	}
	log_setverbose(debug);

	if (pledge("stdio inet route", NULL) == -1)
		fatal("pledge");

	dispatch();
	/* not reached */

	exit(0);
}

int
s6fromaddr(struct sockaddr_in6 *sin6, const char *addr, const char *serv,
    int passive)
{
	struct sockaddr_in6	*sin6p;
	struct addrinfo		*aip;
	struct addrinfo		 ai;
	int			 rv;

	memset(&ai, 0, sizeof(ai));
	ai.ai_family = PF_INET6;
	ai.ai_socktype = SOCK_DGRAM;
	ai.ai_protocol = IPPROTO_UDP;
	ai.ai_flags = (passive) ? AI_PASSIVE : 0;
	if ((rv = getaddrinfo(addr, serv, &ai, &aip)) != 0) {
		log_debug("getaddrinfo: %s", gai_strerror(rv));
		return -1;
	}

	sin6p = (struct sockaddr_in6 *)aip->ai_addr;
	*sin6 = *sin6p;

	freeaddrinfo(aip);

	return 0;
}

void
relay6_setup(void)
{
	struct interface_info	*intf;
	struct server_list	*sp;
	int			 flag = 1;
	struct sockaddr_in6	 sin6;
	struct ipv6_mreq	 mreq6;

	/* Don't allow disabled interfaces. */
	TAILQ_FOREACH(sp, &svlist, entry) {
		if (sp->intf == NULL)
			continue;

		if (sp->intf->dead)
			fatalx("interface '%s' is down", sp->intf->name);
	}

	/* Check for layer 2 dependencies. */
	if (drm == DRM_LAYER2) {
		TAILQ_FOREACH(sp, &svlist, entry) {
			sp->intf = register_interface(sp->intf->name,
			    got_one);
			if (sp->intf == NULL)
				fatalx("destination interface "
				    "registration failed");
		}
		interfaces = register_interface(interfaces->name, got_one);
		if (interfaces == NULL)
			fatalx("input interface not configured");

		return;
	}

	/*
	 * Layer 3 requires at least one IPv6 address on all configured
	 * interfaces.
	 */
	TAILQ_FOREACH(sp, &svlist, entry) {
		if (!sp->intf->ipv6)
			fatalx("%s: no IPv6 address configured",
			    sp->intf->name);

		if (sp->siteglobaladdr && !sp->intf->gipv6)
			fatalx("%s: no IPv6 site/global address configured",
			    sp->intf->name);
	}
	if (!interfaces->ipv6)
		fatalx("%s: no IPv6 address configured", interfaces->name);

	/* Setup the client side socket. */
	clientsd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (clientsd == -1)
		fatal("socket");

	if (setsockopt(clientsd, SOL_SOCKET, SO_REUSEPORT, &flag,
	    sizeof(flag)) == -1)
		fatal("setsockopt(SO_REUSEPORT)");

	if (s6fromaddr(&sin6, NULL, DHCP6_SERVER_PORT_STR, 1) == -1)
		fatalx("s6fromaddr");
	if (bind(clientsd, (struct sockaddr *)&sin6, sizeof(sin6)) == -1)
		fatal("bind");

	if (setsockopt(clientsd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &flag,
	    sizeof(flag)) == -1)
		fatal("setsockopt(IPV6_RECVPKTINFO)");

	memset(&mreq6, 0, sizeof(mreq6));
	if (s6fromaddr(&sin6, DHCP6_ADDR_RELAYSERVER, NULL, 0) == -1)
		fatalx("s6fromaddr");
	memcpy(&mreq6.ipv6mr_multiaddr, &sin6.sin6_addr,
	    sizeof(mreq6.ipv6mr_multiaddr));
	TAILQ_FOREACH(intf, &intflist, entry) {
		/* Skip interfaces without IPv6. */
		if (!intf->ipv6)
			continue;

		mreq6.ipv6mr_interface = intf->index;
		if (setsockopt(clientsd, IPPROTO_IPV6, IPV6_JOIN_GROUP,
		    &mreq6, sizeof(mreq6)) == -1)
			fatal("setsockopt(IPV6_JOIN_GROUP)");
	}

	add_protocol("clientsd", clientsd, mcast6_recv, &clientsd);

	/* Setup the server side socket. */
	serversd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (serversd == -1)
		fatal("socket");

	if (setsockopt(serversd, SOL_SOCKET, SO_REUSEPORT, &flag,
	    sizeof(flag)) == -1)
		fatal("setsockopt(SO_REUSEPORT)");

	if (s6fromaddr(&sin6, NULL, DHCP6_SERVER_PORT_STR, 1) == -1)
		fatalx("s6fromaddr");
	if (bind(serversd, (struct sockaddr *)&sin6, sizeof(sin6)) == -1)
		fatal("bind");

	if (setsockopt(serversd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &flag,
	    sizeof(flag)) == -1)
		fatal("setsockopt(IPV6_RECVPKTINFO)");

	add_protocol("serversd", serversd, mcast6_recv, &serversd);
}

char *
print_hw_addr(int htype, int hlen, unsigned char *data)
{
	static char	 habuf[49];
	char		*s = habuf;
	int		 i, j, slen = sizeof(habuf);

	if (htype == 0 || hlen == 0) {
bad:
		strlcpy(habuf, "<null>", sizeof habuf);
		return habuf;
	}

	for (i = 0; i < hlen; i++) {
		j = snprintf(s, slen, "%02x", data[i]);
		if (j <= 0 || j >= slen)
			goto bad;
		j = strlen (s);
		s += j;
		slen -= (j + 1);
		*s++ = ':';
	}
	*--s = '\0';
	return habuf;
}

const char *
v6addr2str(struct in6_addr *addr)
{
	static int	bufpos = 0;
	static char	buf[3][256];

	bufpos = (bufpos + 1) % 3;
	buf[bufpos][0] = '[';
	if (inet_ntop(AF_INET6, addr, &buf[bufpos][1],
	    sizeof(buf[bufpos])) == NULL)
		return "[unknown]";

	strlcat(buf[bufpos], "]", sizeof(buf[bufpos]));

	return buf[bufpos];
}

const char *
dhcp6type2str(uint8_t msgtype)
{
	switch (msgtype) {
	case DHCP6_MT_REQUEST:
		return "REQUEST";
	case DHCP6_MT_RENEW:
		return "RENEW";
	case DHCP6_MT_REBIND:
		return "REBIND";
	case DHCP6_MT_RELEASE:
		return "RELEASE";
	case DHCP6_MT_DECLINE:
		return "DECLINE";
	case DHCP6_MT_INFORMATIONREQUEST:
		return "INFORMATION-REQUEST";
	case DHCP6_MT_SOLICIT:
		return "SOLICIT";
	case DHCP6_MT_ADVERTISE:
		return "ADVERTISE";
	case DHCP6_MT_CONFIRM:
		return "CONFIRM";
	case DHCP6_MT_REPLY:
		return "REPLY";
	case DHCP6_MT_RECONFIGURE:
		return "RECONFIGURE";
	case DHCP6_MT_RELAYREPL:
		return "RELAY-REPLY";
	case DHCP6_MT_RELAYFORW:
		return "RELAY-FORWARD";
	default:
		return "UNKNOWN";
	}
}

int
relay6_pushrelaymsg(struct packet_ctx *pc, struct interface_info *intf,
    uint8_t *p, size_t *plen, size_t ptotal)
{
	struct dhcp6_relay_packet	*dsr;
	struct dhcp6_option		*dso;
	size_t				 rmlen, dhcplen, optoff;
	size_t				 railen, remotelen;

	if (pc->pc_raidata != NULL)
		railen = sizeof(*dso) + pc->pc_raidatalen;
	else
		railen = 0;

	if (pc->pc_remote)
		remotelen = sizeof(*dso) + ENTERPRISENO_LEN +
		    pc->pc_remotelen;
	else
		remotelen = 0;

	/*
	 * Check if message bigger than MTU and log (RFC 6221
	 * Section 5.3.1).
	 */
	dhcplen = sizeof(*dsr) + railen + remotelen + sizeof(*dso) + *plen;
	rmlen = sizeof(struct ether_header) + sizeof(struct ip6_hdr) +
	    sizeof(struct udphdr) + dhcplen;
	if (rmlen > ptotal) {
		log_info("Relay message too big");
		return -1;
	}

	/* Move the DHCP payload to option. */
	optoff = sizeof(*dsr) + railen + remotelen + sizeof(*dso);
	memmove(p + optoff, p, *plen);

	/* Write the new DHCP packet header for relay-message. */
	dsr = (struct dhcp6_relay_packet *)p;
	dsr->dsr_msgtype = DHCP6_MT_RELAYFORW;

	/*
	 * When the destination is All_DHCP_Relay_Agents_and_Servers we
	 * start the hop count from zero, otherwise set it to
	 * DHCP6_HOP_LIMIT to limit the packet to a single network.
	 */
	if (memcmp(&ss2sin6(&pc->pc_dst)->sin6_addr,
	    &in6alldhcprelay, sizeof(in6alldhcprelay)) == 0)
		dsr->dsr_hopcount = 0;
	else
		dsr->dsr_hopcount = DHCP6_HOP_LIMIT;

	/*
	 * XXX RFC 6221 Section 6.1: layer 2 mode does not set
	 * linkaddr, but we'll use our link-local always to identify the
	 * interface where the packet came in so we don't need to keep
	 * the interface addresses updated.
	 */
	dsr->dsr_linkaddr = intf->linklocal;

	memcpy(&dsr->dsr_peer, &ss2sin6(&pc->pc_src)->sin6_addr,
	    sizeof(dsr->dsr_peer));

	/* Append Interface-ID DHCP option to identify this segment. */
	if (railen > 0) {
		dso = dsr->dsr_options;
		dso->dso_code = htons(DHCP6_OPT_INTERFACEID);
		dso->dso_length = htons(pc->pc_raidatalen);
		memcpy(dso->dso_data, pc->pc_raidata, pc->pc_raidatalen);
	}

	/* Append the Remote-ID DHCP option to identify this segment. */
	if (remotelen > 0) {
		dso = (struct dhcp6_option *)
		    ((uint8_t *)dsr->dsr_options + railen);
		dso->dso_code = htons(DHCP6_OPT_REMOTEID);
		dso->dso_length =
		    htons(sizeof(pc->pc_enterpriseno) + pc->pc_remotelen);
		memcpy(dso->dso_data, &pc->pc_enterpriseno,
		    sizeof(pc->pc_enterpriseno));
		memcpy(dso->dso_data + sizeof(pc->pc_enterpriseno),
		    pc->pc_remote, pc->pc_remotelen);
	}

	/* Write the Relay-Message option header. */
	dso = (struct dhcp6_option *)
	    ((uint8_t *)dsr->dsr_options + railen + remotelen);
	dso->dso_code = htons(DHCP6_OPT_RELAY_MSG);
	dso->dso_length = htons(*plen);

	/* Update the packet length. */
	*plen = dhcplen;

	return 0;
}

int
relay6_poprelaymsg(struct packet_ctx *pc, struct interface_info **intf,
    uint8_t *p, size_t *plen)
{
	struct dhcp6_relay_packet	*dsr = (struct dhcp6_relay_packet *)p;
	struct dhcp6_packet		*ds = NULL;
	struct dhcp6_option		*dso;
	struct in6_addr			 linkaddr;
	size_t				 pleft = *plen, ifnamelen = 0;
	size_t				 dsolen, dhcplen = 0;
	uint16_t			 optcode;
	char				 ifname[64];

	*intf = NULL;

	/* Sanity check: this is a relay message of the right type. */
	if (dsr->dsr_msgtype != DHCP6_MT_RELAYREPL) {
		log_debug("Invalid relay-message (%s) to pop",
		    dhcp6type2str(dsr->dsr_msgtype));
		return -1;
	}

	/* Set the client address based on relay message. */
	ss2sin6(&pc->pc_dst)->sin6_addr = dsr->dsr_peer;
	linkaddr = dsr->dsr_linkaddr;

	dso = dsr->dsr_options;
	pleft -= sizeof(*dsr);
	while (pleft > sizeof(*dso)) {
		optcode = ntohs(dso->dso_code);
		dsolen = sizeof(*dso) + ntohs(dso->dso_length);

		/* Sanity check: do we have the payload? */
		if (dsolen > pleft) {
			log_debug("invalid packet: payload greater than "
			    "packet content (%ld, bytes left %ld)",
			    dsolen, pleft);
			return -1;
		}

		/* Use the interface suggested by the packet. */
		if (optcode == DHCP6_OPT_INTERFACEID) {
			ifnamelen = dsolen - sizeof(*dso);
			if (ifnamelen >= sizeof(ifname)) {
				log_info("received interface id with "
				    "truncated interface name");
				ifnamelen = sizeof(ifname) - 1;
			}

			memcpy(ifname, dso->dso_data, ifnamelen);
			ifname[ifnamelen] = 0;

			dso = (struct dhcp6_option *)
			    ((uint8_t *)dso + dsolen);
			pleft -= dsolen;
			continue;
		}

		/* Ignore unsupported options. */
		if (optcode != DHCP6_OPT_RELAY_MSG) {
			log_debug("ignoring option type %d", optcode);
			dso = (struct dhcp6_option *)
			    ((uint8_t *)dso + dsolen);
			pleft -= dsolen;
			continue;
		}

		/* Save the pointer for the DHCP payload. */
		ds = (struct dhcp6_packet *)dso->dso_data;
		dhcplen = ntohs(dso->dso_length);

		dso = (struct dhcp6_option *)((uint8_t *)dso + dsolen);
		pleft -= dsolen;
	}
	if (ds == NULL || dhcplen == 0) {
		log_debug("Could not find relay-message option");
		return -1;
	}

	/* Move the encapsulated DHCP payload. */
	memmove(p, ds, dhcplen);
	*plen = dhcplen;

	/*
	 * If the new message is for the client, we must change the
	 * destination port to the client's, otherwise keep the port
	 * for the next relay.
	 */
	ds = (struct dhcp6_packet *)p;
	if (ds->ds_msgtype != DHCP6_MT_RELAYREPL)
		ss2sin6(&pc->pc_dst)->sin6_port =
		    htons(DHCP6_CLIENT_PORT);

	/* No Interface-ID specified. */
	if (ifnamelen == 0)
		goto use_linkaddr;

	/* Look out for the specified interface, */
	if ((*intf = iflist_getbyname(ifname)) == NULL) {
		log_debug("  Interface-ID found, but no interface matches.");

		/*
		 * Use client interface as fallback, but try
		 * link-address (if any) before giving up.
		 */
		*intf = interfaces;
	}

 use_linkaddr:
	/* Use link-addr to determine output interface if present. */
	if (memcmp(&linkaddr, &in6addr_any, sizeof(linkaddr)) != 0) {
		if ((*intf = iflist_getbyaddr6(&linkaddr)) != NULL)
			return 0;

		log_debug("Could not find interface using "
		    "address %s", v6addr2str(&linkaddr));
	}

	return 0;
}

void
rai_configure(struct packet_ctx *pc, struct interface_info *intf)
{
	if (remote_data != NULL) {
		pc->pc_remote = remote_data;
		pc->pc_remotelen = remote_datalen;
		pc->pc_enterpriseno = htonl(enterpriseno);
	}

	/* Layer-2 must include Interface-ID (Option 18). */
	if (drm == DRM_LAYER2)
		goto select_rai;

	/* User did not configure Interface-ID. */
	if (oflag == 0)
		return;

 select_rai:
	if (rai_data == NULL) {
		pc->pc_raidata = intf->name;
		pc->pc_raidatalen = strlen(intf->name);
	} else {
		pc->pc_raidata = rai_data;
		pc->pc_raidatalen = rai_datalen;
	}
}

void
relay6_logsrcaddr(struct packet_ctx *pc, struct interface_info *intf,
    uint8_t msgtype)
{
	const char		*type;

	type = (msgtype == DHCP6_MT_RELAYREPL) ? "reply" : "forward";
	if (drm == DRM_LAYER2)
		log_info("forwarded relay-%s for %s to %s",
		    type, print_hw_addr(pc->pc_htype, pc->pc_hlen,
		    pc->pc_smac), intf->name);
	else
		log_info("forwarded relay-%s for %s to %s%%%s",
		    type,
		    v6addr2str(&ss2sin6(&pc->pc_srcorig)->sin6_addr),
		    v6addr2str(&ss2sin6(&pc->pc_dst)->sin6_addr),
		    intf->name);
}

void
relay6(struct interface_info *intf, void *p, size_t plen,
    struct packet_ctx *pc)
{
	struct dhcp6_packet		*ds = (struct dhcp6_packet *)p;
	struct dhcp6_relay_packet	*dsr = (struct dhcp6_relay_packet *)p;
	struct interface_info		*dstif = NULL;
	struct server_list		*sp;
	size_t				 buflen = plen;
	int				 clientdir = (intf != interfaces);
	uint8_t				 msgtype, hopcount = 0;

	/* Sanity check: we have at least the DHCP header. */
	if (plen < (int)sizeof(*ds)) {
		log_debug("invalid packet size");
		return;
	}

	/* Set Relay Agent Information fields. */
	rai_configure(pc, intf);

	/*
	 * RFC 3315 section 20 relay messages:
	 * For client messages prepend a new DHCP payload with the
	 * relay-forward, otherwise update the DHCP relay header.
	 */
	msgtype = ds->ds_msgtype;

	log_debug("%s: received %s from %s",
	    intf->name, dhcp6type2str(msgtype),
	    v6addr2str(&ss2sin6(&pc->pc_src)->sin6_addr));

	switch (msgtype) {
	case DHCP6_MT_ADVERTISE:
	case DHCP6_MT_REPLY:
	case DHCP6_MT_RECONFIGURE:
		/*
		 * Don't forward reply packets coming from the client
		 * interface.
		 *
		 * RFC 6221 Section 6.1.1.
		 */
		if (clientdir == 0) {
			log_debug("  dropped reply in opposite direction");
			return;
		}
		/* FALLTHROUGH */

	case DHCP6_MT_REQUEST:
	case DHCP6_MT_RENEW:
	case DHCP6_MT_REBIND:
	case DHCP6_MT_RELEASE:
	case DHCP6_MT_DECLINE:
	case DHCP6_MT_INFORMATIONREQUEST:
	case DHCP6_MT_SOLICIT:
	case DHCP6_MT_CONFIRM:
		/*
		 * Encapsulate the client/server message with the
		 * relay-message header.
		 */
		if (relay6_pushrelaymsg(pc, intf, (uint8_t *)p,
		    &buflen, DHCP_MTU_MAX) == -1) {
			log_debug("  message encapsulation failed");
			return;
		}
		break;

	case DHCP6_MT_RELAYREPL:
		/*
		 * Don't forward reply packets coming from the client
		 * interface.
		 *
		 * RFC 6221 Section 6.1.1.
		 */
		if (clientdir == 0) {
			log_debug("  dropped reply in opposite direction");
			return;
		}

		if (relay6_poprelaymsg(pc, &dstif, (uint8_t *)p,
		    &buflen) == -1) {
			log_debug("  failed to pop relay-message");
			return;
		}

		pc->pc_sd = clientsd;
		break;

	case DHCP6_MT_RELAYFORW:
		/*
		 * We can only have multiple hops when the destination
		 * address is All_DHCP_Relay_Agents_and_Servers, otherwise
		 * drop it.
		 */
		if (memcmp(&ss2sin6(&pc->pc_dst)->sin6_addr,
		    &in6alldhcprelay, sizeof(in6alldhcprelay)) != 0) {
			log_debug("  wrong destination");
			return;
		}

		hopcount = dsr->dsr_hopcount + 1;
		if (hopcount >= DHCP6_HOP_LIMIT) {
			log_debug("  hop limit reached");
			return;
		}

		/* Stack into another relay-message. */
		if (relay6_pushrelaymsg(pc, intf, (uint8_t *)p,
		    &buflen, DHCP_MTU_MAX) == -1) {
			log_debug("  failed to push relay message");
			return;
		}

		dsr = (struct dhcp6_relay_packet *)p;
		dsr->dsr_msgtype = msgtype;
		dsr->dsr_peer = ss2sin6(&pc->pc_src)->sin6_addr;
		dsr->dsr_hopcount = hopcount;
		break;

	default:
		log_debug("  unknown message type %d", ds->ds_msgtype);
		return;
	}

	/* We received an packet with Interface-ID, use it. */
	if (dstif != NULL) {
		relay6_logsrcaddr(pc, dstif, msgtype);
		send_packet(dstif, p, buflen, pc);
		return;
	}

	/* Or send packet to the client. */
	if (clientdir) {
		relay6_logsrcaddr(pc, interfaces, msgtype);
		send_packet(interfaces, p, buflen, pc);
		return;
	}

	/* Otherwise broadcast it to other relays/servers. */
	TAILQ_FOREACH(sp, &svlist, entry) {
		/*
		 * Don't send in the same interface it came in if we are
		 * using multicast.
		 */
		if (sp->intf == intf &&
		    sp->to.ss_family == 0)
			continue;

		/*
		 * When forwarding a packet use the configured address
		 * (if any) instead of multicasting.
		 */
		if (msgtype != DHCP6_MT_REPLY &&
		    sp->to.ss_family == AF_INET6)
			pc->pc_dst = sp->to;

		relay6_logsrcaddr(pc, sp->intf, msgtype);
		send_packet(sp->intf, p, buflen, pc);
	}
}

void
mcast6_recv(struct protocol *l)
{
	struct in6_pktinfo	*ipi6 = NULL;
	struct cmsghdr		*cmsg;
	struct interface_info	*intf;
	int			 sd = *(int *)l->local;
	ssize_t			 recvlen;
	struct packet_ctx	 pc;
	struct msghdr		 msg;
	struct sockaddr_storage	 ss;
	struct iovec		 iov[2];
	uint8_t			 iovbuf[4096];
	uint8_t			 cmsgbuf[
	    CMSG_SPACE(sizeof(struct in6_pktinfo))
	];

	memset(&pc, 0, sizeof(pc));

	iov[0].iov_base = iovbuf;
	iov[0].iov_len = sizeof(iovbuf);

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_control = cmsgbuf;
	msg.msg_controllen = sizeof(cmsgbuf);
	msg.msg_name = &ss;
	msg.msg_namelen = sizeof(ss);
	if ((recvlen = recvmsg(sd, &msg, 0)) == -1) {
		log_warn("%s: recvmsg failed", __func__);
		return;
	}

	/* Sanity check: this is an IPv6 packet. */
	if (ss.ss_family != AF_INET6) {
		log_debug("received non IPv6 packet");
		return;
	}

	/* Drop packets that we sent. */
	if (iflist_getbyaddr6(&ss2sin6(&ss)->sin6_addr) != NULL)
		return;

	/* Save the sender address. */
	pc.pc_srcorig = pc.pc_src = ss;

	/* Pre-configure destination to the default multicast address. */
	ss2sin6(&pc.pc_dst)->sin6_family = AF_INET6;
	ss2sin6(&pc.pc_dst)->sin6_len = sizeof(struct sockaddr_in6);
	ss2sin6(&pc.pc_dst)->sin6_addr = in6alldhcprelay;
	ss2sin6(&pc.pc_dst)->sin6_port = htons(DHCP6_SERVER_PORT);
	pc.pc_sd = serversd;

	/* Find out input interface. */
	for (cmsg = (struct cmsghdr *)CMSG_FIRSTHDR(&msg); cmsg;
	    cmsg = (struct cmsghdr *)CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level != IPPROTO_IPV6)
			continue;

		switch (cmsg->cmsg_type) {
		case IPV6_PKTINFO:
			ipi6 = (struct in6_pktinfo *)CMSG_DATA(cmsg);
			break;
		}
	}
	if (ipi6 == NULL) {
		log_debug("failed to get packet interface");
		return;
	}

	intf = iflist_getbyindex(ipi6->ipi6_ifindex);
	if (intf == NULL) {
		log_debug("failed to find packet interface: %u",
		    ipi6->ipi6_ifindex);
		return;
	}

	/* Pass it to the relay routine. */
	if (bootp_packet_handler)
		(*bootp_packet_handler)(intf, iovbuf, recvlen, &pc);
}
