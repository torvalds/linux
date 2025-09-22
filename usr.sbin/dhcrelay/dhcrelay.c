/*	$OpenBSD: dhcrelay.c,v 1.69 2025/05/21 05:05:33 kn Exp $ */

/*
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

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#include <net/if.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "dhcp.h"
#include "dhcpd.h"
#include "log.h"

void	 usage(void);
void	 relay(struct interface_info *, struct dhcp_packet *, int,
	    struct packet_ctx *);
void	 l2relay(struct interface_info *, struct dhcp_packet *, int,
	    struct packet_ctx *);
char	*print_hw_addr(int, int, unsigned char *);
void	 got_response(struct protocol *);
int	 get_rdomain(char *);

void	 relay_agentinfo(struct packet_ctx *, struct interface_info *, int);

int	 relay_agentinfo_cmp(struct packet_ctx *pc, uint8_t *, int);
ssize_t	 relay_agentinfo_append(struct packet_ctx *, struct dhcp_packet *,
	    size_t);
ssize_t	 relay_agentinfo_remove(struct packet_ctx *, struct dhcp_packet *,
	    size_t);

time_t cur_time;

struct interface_info *interfaces = NULL;
struct server_list *servers;
struct iflist intflist;
int server_fd;
int oflag;

enum dhcp_relay_mode	 drm = DRM_UNKNOWN;
const char		*rai_circuit = NULL;
const char		*rai_remote = NULL;
int			 rai_replace = 0;

int
main(int argc, char *argv[])
{
	int			 ch, daemonize = 1, opt, rdomain;
	struct server_list	*sp = NULL;
	struct passwd		*pw;
	struct sockaddr_in	 laddr;
	int			 optslen;

	log_init(1, LOG_DAEMON);	/* log to stderr until daemonized */

	setup_iflist();

	while ((ch = getopt(argc, argv, "aC:di:oR:r")) != -1) {
		switch (ch) {
		case 'C':
			rai_circuit = optarg;
			break;
		case 'd':
			daemonize = 0;
			break;
		case 'i':
			if (interfaces != NULL)
				usage();

			interfaces = iflist_getbyname(optarg);
			if (interfaces == NULL)
				fatalx("interface '%s' not found", optarg);
			break;
		case 'o':
			/* add the relay agent information option */
			oflag++;
			break;
		case 'R':
			rai_remote = optarg;
			break;
		case 'r':
			rai_replace = 1;
			break;

		default:
			usage();
			/* not reached */
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1)
		usage();

	if (rai_remote != NULL && rai_circuit == NULL)
		fatalx("you must specify a circuit-id with a remote-id");

	/* Validate that we have space for all suboptions. */
	if (rai_circuit != NULL) {
		optslen = 2 + strlen(rai_circuit);
		if (rai_remote != NULL)
			optslen += 2 + strlen(rai_remote);

		if (optslen > DHCP_OPTION_MAXLEN)
			fatalx("relay agent information is too long");
	}

	while (argc > 0) {
		struct addrinfo hints, *res;
		struct in_addr		 ia, *iap = NULL;

		if ((sp = calloc(1, sizeof(*sp))) == NULL)
			fatalx("calloc");

		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;

		if ((sp->intf = register_interface(argv[0], got_one,
		    1)) != NULL) {
			if (drm == DRM_LAYER3)
				fatalx("don't mix interfaces with hosts");

			if (sp->intf->hw_address.htype == HTYPE_IPSEC_TUNNEL)
				fatalx("can't use IPsec with layer 2");

			sp->next = servers;
			servers = sp;

			drm = DRM_LAYER2;
			argc--;
			argv++;
			continue;
		}

		if (getaddrinfo(argv[0], NULL, &hints, &res) != 0)
			log_warnx("%s: host unknown", argv[0]);
		else {
			ia = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
			iap = &ia;
			freeaddrinfo(res);
		}

		if (iap) {
			if (drm == DRM_LAYER2)
				fatalx("don't mix interfaces with hosts");

			drm = DRM_LAYER3;
			sp->next = servers;
			servers = sp;
			memcpy(&ss2sin(&sp->to)->sin_addr, iap, sizeof(*iap));
		} else
			free(sp);

		argc--;
		argv++;
	}

	if (interfaces == NULL ||
	    register_interface(interfaces->name, got_one, 0) == NULL)
		fatalx("no interface given");

	/* We need an address for running layer 3 mode. */
	if (drm == DRM_LAYER3 &&
	    (interfaces->hw_address.htype != HTYPE_IPSEC_TUNNEL &&
	    interfaces->primary_address.s_addr == 0))
		fatalx("interface '%s' does not have an address",
		    interfaces->name);

	/* We need at least one server. */
	if (!sp)
		usage();

	rdomain = get_rdomain(interfaces->name);

	/* Enable the relay agent option by default for enc0 */
	if (interfaces->hw_address.htype == HTYPE_IPSEC_TUNNEL)
		oflag++;

	bzero(&laddr, sizeof laddr);
	laddr.sin_len = sizeof laddr;
	laddr.sin_family = AF_INET;
	laddr.sin_port = htons(SERVER_PORT);
	laddr.sin_addr.s_addr = interfaces->primary_address.s_addr;
	/* Set up the server sockaddrs. */
	for (sp = servers; sp; sp = sp->next) {
		if (sp->intf != NULL)
			break;

		ss2sin(&sp->to)->sin_port = htons(SERVER_PORT);
		ss2sin(&sp->to)->sin_family = AF_INET;
		ss2sin(&sp->to)->sin_len = sizeof(struct sockaddr_in);
		sp->fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (sp->fd == -1)
			fatal("socket");
		opt = 1;
		if (setsockopt(sp->fd, SOL_SOCKET, SO_REUSEPORT,
		    &opt, sizeof(opt)) == -1)
			fatal("setsockopt");
		if (setsockopt(sp->fd, SOL_SOCKET, SO_RTABLE, &rdomain,
		    sizeof(rdomain)) == -1)
			fatal("setsockopt");
		if (bind(sp->fd, (struct sockaddr *)&laddr, sizeof laddr) ==
		    -1)
			fatal("bind");
		if (connect(sp->fd, (struct sockaddr *)&sp->to,
		    sp->to.ss_len) == -1)
			fatal("connect");
		add_protocol("server", sp->fd, got_response, sp);
	}

	/* Socket used to forward packets to the DHCP client */
	if (interfaces->hw_address.htype == HTYPE_IPSEC_TUNNEL) {
		laddr.sin_addr.s_addr = INADDR_ANY;
		server_fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (server_fd == -1)
			fatal("socket");
		opt = 1;
		if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT,
		    &opt, sizeof(opt)) == -1)
			fatal("setsockopt");
		if (setsockopt(server_fd, SOL_SOCKET, SO_RTABLE, &rdomain,
		    sizeof(rdomain)) == -1)
			fatal("setsockopt");
		if (bind(server_fd, (struct sockaddr *)&laddr,
		    sizeof(laddr)) == -1)
			fatal("bind");
	}

	tzset();

	time(&cur_time);
	if (drm == DRM_LAYER3)
		bootp_packet_handler = relay;
	else
		bootp_packet_handler = l2relay;

	if ((pw = getpwnam("_dhcp")) == NULL)
		fatalx("user \"_dhcp\" not found");
	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	if (daemonize) {
		if (daemon(0, 0) == -1)
			fatal("daemon");

		log_init(0, LOG_DAEMON);	/* stop logging to stderr */
	}

	if (pledge("stdio route", NULL) == -1)
		fatal("pledge");

	dispatch();
	/* not reached */

	exit(0);
}

void
relay(struct interface_info *ip, struct dhcp_packet *packet, int length,
    struct packet_ctx *pc)
{
	struct server_list	*sp;
	struct sockaddr_in	 to;

	if (packet->hlen > sizeof packet->chaddr) {
		log_info("Discarding packet with invalid hlen.");
		return;
	}

	/* If it's a bootreply, forward it to the client. */
	if (packet->op == BOOTREPLY) {
		/* Filter packet that were not meant for us. */
		if (packet->giaddr.s_addr !=
		    interfaces->primary_address.s_addr)
			return;

		bzero(&to, sizeof(to));
		if (!(packet->flags & htons(BOOTP_BROADCAST))) {
			to.sin_addr = packet->yiaddr;
			to.sin_port = htons(CLIENT_PORT);
		} else {
			to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
			to.sin_port = htons(CLIENT_PORT);
		}
		to.sin_family = AF_INET;
		to.sin_len = sizeof to;
		*ss2sin(&pc->pc_dst) = to;

		/*
		 * Set up the hardware destination address.  If it's a reply
		 * with the BROADCAST flag set, we should send an L2 broad-
		 * cast as well.
		 */
		if (!(packet->flags & htons(BOOTP_BROADCAST))) {
			pc->pc_hlen = packet->hlen;
			if (pc->pc_hlen > CHADDR_SIZE)
				pc->pc_hlen = CHADDR_SIZE;
			memcpy(pc->pc_dmac, packet->chaddr, pc->pc_hlen);
			pc->pc_htype = packet->htype;
		} else {
			memset(pc->pc_dmac, 0xff, sizeof(pc->pc_dmac));
		}

		relay_agentinfo(pc, interfaces, packet->op);
		if ((length = relay_agentinfo_remove(pc, packet,
		    length)) == -1) {
			log_info("ignoring BOOTREPLY with invalid "
			    "relay agent information");
			return;
		}

		/*
		 * VMware PXE "ROMs" confuse the DHCP gateway address
		 * with the IP gateway address. This is a problem if your
		 * DHCP relay is running on something that's not your
		 * network gateway.
		 *
		 * It is purely informational from the relay to the client
		 * so we can safely clear it.
		 */
		packet->giaddr.s_addr = 0x0;

		ss2sin(&pc->pc_src)->sin_addr = interfaces->primary_address;
		if (send_packet(interfaces, packet, length, pc) != -1)
			log_debug("forwarded BOOTREPLY for %s to %s",
			    print_hw_addr(packet->htype, packet->hlen,
			    packet->chaddr), inet_ntoa(to.sin_addr));
		return;
	}

	if (ip == NULL) {
		log_info("ignoring non BOOTREPLY from server");
		return;
	}

	if (packet->hops > 16) {
		log_info("ignoring BOOTREQUEST with hop count of %d",
		    packet->hops);
		return;
	}
	packet->hops++;

	/*
	 * Set the giaddr so the server can figure out what net it's
	 * from and so that we can later forward the response to the
	 * correct net.  The RFC specifies that we have to keep the
	 * initial giaddr (in case we relay over multiple hops).
	 */
	if (!packet->giaddr.s_addr)
		packet->giaddr = ip->primary_address;

	relay_agentinfo(pc, interfaces, packet->op);
	if ((length = relay_agentinfo_append(pc, packet, length)) == -1) {
		log_info("ignoring BOOTREQUEST with invalid "
		    "relay agent information");
		return;
	}

	/* Otherwise, it's a BOOTREQUEST, so forward it to all the
	   servers. */
	for (sp = servers; sp; sp = sp->next) {
		if (send(sp->fd, packet, length, 0) != -1) {
			log_debug("forwarded BOOTREQUEST for %s to %s",
			    print_hw_addr(packet->htype, packet->hlen,
			    packet->chaddr),
			    inet_ntoa(ss2sin(&sp->to)->sin_addr));
		}
	}

}

void
usage(void)
{
	extern char	*__progname;

	fprintf(stderr, "usage: %s [-dor] [-C circuit-id] [-R remote-id] "
	    "-i interface\n\tdestination ...\n",
	    __progname);
	exit(1);
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

void
got_response(struct protocol *l)
{
	struct packet_ctx pc;
	ssize_t result;
	union {
		/*
		 * Packet input buffer.  Must be as large as largest
		 * possible MTU.
		 */
		unsigned char packbuf[4095];
		struct dhcp_packet packet;
	} u;
	struct server_list *sp = l->local;

	memset(&u, DHO_END, sizeof(u));
	if ((result = recv(l->fd, u.packbuf, sizeof(u), 0)) == -1 &&
	    errno != ECONNREFUSED) {
		/*
		 * Ignore ECONNREFUSED as too many dhcp servers send a bogus
		 * icmp unreach for every request.
		 */
		log_warn("recv failed for %s",
		    inet_ntoa(ss2sin(&sp->to)->sin_addr));
		return;
	}
	if (result == -1 && errno == ECONNREFUSED)
		return;

	if (result == 0)
		return;

	if (result < BOOTP_MIN_LEN) {
		log_info("Discarding packet with invalid size.");
		return;
	}

	memset(&pc, 0, sizeof(pc));
	pc.pc_src.ss_family = AF_INET;
	pc.pc_src.ss_len = sizeof(struct sockaddr_in);
	memcpy(&ss2sin(&pc.pc_src)->sin_addr, &ss2sin(&sp->to)->sin_addr,
	    sizeof(ss2sin(&pc.pc_src)->sin_addr));
	ss2sin(&pc.pc_src)->sin_port = htons(SERVER_PORT);

	pc.pc_dst.ss_family = AF_INET;
	pc.pc_dst.ss_len = sizeof(struct sockaddr_in);
	ss2sin(&pc.pc_dst)->sin_port = htons(CLIENT_PORT);

	if (bootp_packet_handler)
		(*bootp_packet_handler)(NULL, &u.packet, result, &pc);
}

void
relay_agentinfo(struct packet_ctx *pc, struct interface_info *intf,
    int bootop)
{
	static u_int8_t		 buf[8];
	struct sockaddr_in	*sin;

	if (oflag == 0)
		return;

	if (rai_remote != NULL) {
		pc->pc_remote = rai_remote;
		pc->pc_remotelen = strlen(rai_remote);
	} else
		pc->pc_remotelen = 0;

	if (rai_circuit == NULL) {
		buf[0] = (uint8_t)(intf->index << 8);
		buf[1] = intf->index & 0xff;
		pc->pc_circuit = buf;
		pc->pc_circuitlen = 2;

		if (rai_remote == NULL) {
			if (bootop == BOOTREPLY)
				sin = ss2sin(&pc->pc_dst);
			else
				sin = ss2sin(&pc->pc_src);

			pc->pc_remote =
			    (uint8_t *)&sin->sin_addr;
			pc->pc_remotelen =
			    sizeof(sin->sin_addr);
		}
	} else {
		pc->pc_circuit = rai_circuit;
		pc->pc_circuitlen = strlen(rai_circuit);
	}
}

int
relay_agentinfo_cmp(struct packet_ctx *pc, uint8_t *p, int plen)
{
	int		 len;
	char		 buf[256];

	if (oflag == 0)
		return (-1);

	len = *(p + 1);
	if (len > plen)
		return (-1);

	switch (*p) {
	case RAI_CIRCUIT_ID:
		if (pc->pc_circuit == NULL)
			return (-1);
		if (pc->pc_circuitlen != len)
			return (-1);

		memcpy(buf, p + DHCP_OPTION_HDR_LEN, len);
		return (memcmp(pc->pc_circuit, buf, len));

	case RAI_REMOTE_ID:
		if (pc->pc_remote == NULL)
			return (-1);
		if (pc->pc_remotelen != len)
			return (-1);

		memcpy(buf, p + DHCP_OPTION_HDR_LEN, len);
		return (memcmp(pc->pc_remote, buf, len));

	default:
		/* Unmatched type */
		log_info("unmatched relay info %d", *p);
		return (0);
	}
}

ssize_t
relay_agentinfo_append(struct packet_ctx *pc, struct dhcp_packet *dp,
    size_t dplen)
{
	uint8_t		*p, *startp;
	ssize_t		 newtotal = dplen;
	int		 opttotal, optlen, i, hasinfo = 0;
	int		 maxlen, neededlen;

	/* Only append when enabled. */
	if (oflag == 0)
		return (dplen);

	startp = (uint8_t *)dp;
	p = (uint8_t *)&dp->options;
	if (memcmp(p, DHCP_OPTIONS_COOKIE, DHCP_OPTIONS_COOKIE_LEN)) {
		log_info("invalid dhcp options cookie");
		return (-1);
	}

	p += DHCP_OPTIONS_COOKIE_LEN;
	opttotal = dplen - DHCP_FIXED_NON_UDP - DHCP_OPTIONS_COOKIE_LEN;
	maxlen = DHCP_MTU_MAX - DHCP_FIXED_LEN - DHCP_OPTIONS_COOKIE_LEN - 1;
	if (maxlen < 1 || opttotal < 1)
		return (dplen);

	for (i = 0; i < opttotal && *p != DHO_END;) {
		if (*p == DHO_PAD)
			optlen = 1;
		else
			optlen = p[1] + DHCP_OPTION_HDR_LEN;

		if ((i + optlen) > opttotal) {
			log_info("truncated dhcp options");
			return (-1);
		}

		if (*p == DHO_RELAY_AGENT_INFORMATION) {
			if (rai_replace) {
				memmove(p, p + optlen, opttotal - i);
				opttotal -= optlen;
				optlen = 0;
			} else
				hasinfo = 1;
		}

		p += optlen;
		i += optlen;

		/* We reached the end, append the relay agent info. */
		if (i < opttotal && *p == DHO_END) {
			/* We already have the Relay Agent Info, skip it. */
			if (hasinfo)
				continue;

			/* Calculate needed length to append new data. */
			neededlen = newtotal + DHCP_OPTION_HDR_LEN;
			if (pc->pc_circuitlen > 0)
				neededlen += DHCP_OPTION_HDR_LEN +
				    pc->pc_circuitlen;
			if (pc->pc_remotelen > 0)
				neededlen += DHCP_OPTION_HDR_LEN +
				    pc->pc_remotelen;

			/* Save one byte for DHO_END. */
			neededlen += 1;

			/* Check if we have space for the new options. */
			if (neededlen > maxlen) {
				log_warnx("no space for relay agent info");
				return (newtotal);
			}

			/* New option header: 2 bytes. */
			newtotal += DHCP_OPTION_HDR_LEN;

			*p++ = DHO_RELAY_AGENT_INFORMATION;
			*p = 0;
			if (pc->pc_circuitlen > 0) {
				newtotal += DHCP_OPTION_HDR_LEN +
				    pc->pc_circuitlen;
				*p = (*p) + DHCP_OPTION_HDR_LEN +
				    pc->pc_circuitlen;
			}

			if (pc->pc_remotelen > 0) {
				newtotal += DHCP_OPTION_HDR_LEN +
				    pc->pc_remotelen;
				*p = (*p) + DHCP_OPTION_HDR_LEN +
				    pc->pc_remotelen;
			}

			p++;

			/* Sub-option circuit-id header plus value. */
			if (pc->pc_circuitlen > 0) {
				*p++ = RAI_CIRCUIT_ID;
				*p++ = pc->pc_circuitlen;
				memcpy(p, pc->pc_circuit, pc->pc_circuitlen);

				p += pc->pc_circuitlen;
			}

			/* Sub-option remote-id header plus value. */
			if (pc->pc_remotelen > 0) {
				*p++ = RAI_REMOTE_ID;
				*p++ = pc->pc_remotelen;
				memcpy(p, pc->pc_remote, pc->pc_remotelen);

				p += pc->pc_remotelen;
			}

			*p = DHO_END;
		}
	}

	/* Zero the padding so we don't leak anything. */
	p++;
	if (p < (startp + maxlen))
		memset(p, 0, (startp + maxlen) - p);

	return (newtotal);
}

ssize_t
relay_agentinfo_remove(struct packet_ctx *pc, struct dhcp_packet *dp,
    size_t dplen)
{
	uint8_t		*p, *np, *startp, *endp;
	int		 opttotal, optleft;
	int		 suboptlen, optlen, i;
	int		 maxlen, remaining, matched = 0;

	startp = (uint8_t *)dp;
	p = (uint8_t *)&dp->options;
	if (memcmp(p, DHCP_OPTIONS_COOKIE, DHCP_OPTIONS_COOKIE_LEN)) {
		log_info("invalid dhcp options cookie");
		return (-1);
	}

	maxlen = DHCP_MTU_MAX - DHCP_FIXED_LEN - DHCP_OPTIONS_COOKIE_LEN - 1;
	opttotal = dplen - DHCP_FIXED_NON_UDP - DHCP_OPTIONS_COOKIE_LEN;
	optleft = opttotal;

	p += DHCP_OPTIONS_COOKIE_LEN;
	endp = p + opttotal;

	for (i = 0; i < opttotal && *p != DHO_END;) {
		if (*p == DHO_PAD)
			optlen = 1;
		else
			optlen = p[1] + DHCP_OPTION_HDR_LEN;

		if ((i + optlen) > opttotal) {
			log_info("truncated dhcp options");
			return (-1);
		}

		if (*p == DHO_RELAY_AGENT_INFORMATION) {
			/* Fast case: there is no next option. */
			np = p + optlen;
			if (*np == DHO_END) {
				*p = *np;
				endp = p + 1;
				/* Zero the padding so we don't leak data. */
				if (endp < (startp + maxlen))
					memset(endp, 0,
					    (startp + maxlen) - endp);

				return (dplen);
			}

			remaining = optlen;
			while (remaining > 0) {
				suboptlen = *(p + 1);
				remaining -= DHCP_OPTION_HDR_LEN + suboptlen;

				matched = 1;
				if (relay_agentinfo_cmp(pc, p, suboptlen) == 0)
					continue;

				matched = 0;
				break;
			}
			/* It is not ours Relay Agent Info, don't remove it. */
			if (matched == 0)
				break;

			/* Move the other options on top of this one. */
			optleft -= optlen;
			endp -= optlen;

			/* Replace the old agent relay info. */
			memmove(p, dp, optleft);

			endp++;
			/* Zero the padding so we don't leak data. */
			if (endp < (startp + maxlen))
				memset(endp, 0,
				    (startp + maxlen) - endp);

			return (endp - startp);
		}

		p += optlen;
		i += optlen;
		optleft -= optlen;
	}

	return (endp - startp);
}

int
get_rdomain(char *name)
{
	int rv = 0, s;
	struct  ifreq ifr;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		fatal("get_rdomain socket");

	bzero(&ifr, sizeof(ifr));
	strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFRDOMAIN, (caddr_t)&ifr) != -1)
		rv = ifr.ifr_rdomainid;

	close(s);
	return rv;
}

void
l2relay(struct interface_info *ip, struct dhcp_packet *dp, int length,
    struct packet_ctx *pc)
{
	struct server_list	*sp;
	ssize_t			 dplen;

	if (dp->hlen > sizeof(dp->chaddr)) {
		log_info("Discarding packet with invalid hlen.");
		return;
	}

	relay_agentinfo(pc, ip, dp->op);

	switch (dp->op) {
	case BOOTREQUEST:
		/* Add the relay agent info asked by the user. */
		if ((dplen = relay_agentinfo_append(pc, dp, length)) == -1)
			return;

		/*
		 * Re-send the packet to every interface except the one
		 * it came in.
		 */
		for (sp = servers; sp != NULL; sp = sp->next) {
			if (sp->intf == ip)
				continue;

			log_debug("forwarded BOOTREQUEST for %s to %s",
			    print_hw_addr(pc->pc_htype, pc->pc_hlen,
			    pc->pc_smac), sp->intf->name);

			send_packet(sp->intf, dp, dplen, pc);
		}
		if (ip != interfaces) {
			log_debug("forwarded BOOTREQUEST for %s to %s",
			    print_hw_addr(pc->pc_htype, pc->pc_hlen,
			    pc->pc_smac), interfaces->name);

			send_packet(interfaces, dp, dplen, pc);
		}
		break;

	case BOOTREPLY:
		/* Remove relay agent info on offer. */
		if ((dplen = relay_agentinfo_remove(pc, dp, length)) == -1)
			return;

		if (ip != interfaces) {
			log_debug("forwarded BOOTREPLY for %s to %s",
			    print_hw_addr(pc->pc_htype, pc->pc_hlen,
			    pc->pc_dmac), interfaces->name);
			send_packet(interfaces, dp, dplen, pc);
		}
		break;

	default:
		log_debug("invalid operation type '%d'", dp->op);
		return;
	}
}
