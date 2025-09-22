/*	$OpenBSD: packet.c,v 1.23 2024/05/18 11:17:30 jsg Exp $ */

/*
 * Copyright (c) 2004, 2005 Esben Norby <norby@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <arpa/inet.h>

#include <errno.h>
#include <event.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "ospf6d.h"
#include "ospf6.h"
#include "log.h"
#include "ospfe.h"

int		 ospf_hdr_sanity_check(struct ospf_hdr *, u_int16_t,
		    const struct iface *, struct in6_addr *);
struct iface	*find_iface(struct ospfd_conf *, unsigned int,
		    struct in6_addr *);

static u_int8_t	*recv_buf;

int
gen_ospf_hdr(struct ibuf *buf, struct iface *iface, u_int8_t type)
{
	struct ospf_hdr	ospf_hdr;

	bzero(&ospf_hdr, sizeof(ospf_hdr));
	ospf_hdr.version = OSPF6_VERSION;
	ospf_hdr.type = type;
	ospf_hdr.rtr_id = ospfe_router_id();
	if (iface->type != IF_TYPE_VIRTUALLINK)
		ospf_hdr.area_id = iface->area->id.s_addr;
	ospf_hdr.instance = DEFAULT_INSTANCE_ID;
	ospf_hdr.zero = 0;		/* must be zero */

	return (ibuf_add(buf, &ospf_hdr, sizeof(ospf_hdr)));
}

int
upd_ospf_hdr(struct ibuf *buf, struct iface *iface)
{
	/* update length */
	if (ibuf_size(buf) > USHRT_MAX)
		fatalx("upd_ospf_hdr: resulting ospf packet too big");
	if (ibuf_set_n16(buf, offsetof(struct ospf_hdr, len),
	    ibuf_size(buf)) == -1)
		fatalx("upd_ospf_hdr: ibuf_set_n16 failed");

	/* checksum calculated via IPV6_CHECKSUM */
	if (ibuf_set_n16(buf, offsetof(struct ospf_hdr, chksum), 0) == -1)
		fatalx("upd_ospf_hdr: ibuf_set_n16 failed");

	return (0);
}

/* send and receive packets */
int
send_packet(struct iface *iface, struct ibuf *buf,
    struct in6_addr *dst)
{
	struct sockaddr_in6	sa6;

	/* setup sockaddr */
	bzero(&sa6, sizeof(sa6));
	sa6.sin6_family = AF_INET6;
	sa6.sin6_len = sizeof(sa6);
	sa6.sin6_addr = *dst;

	/* don't we all love link local scope and all the needed hacks for it */
	if (IN6_IS_ADDR_LINKLOCAL(dst) || IN6_IS_ADDR_MC_LINKLOCAL(dst))
		sa6.sin6_scope_id = iface->ifindex;

	/* set outgoing interface for multicast traffic */
	if (IN6_IS_ADDR_MULTICAST(dst))
		if (if_set_mcast(iface) == -1) {
			log_warn("send_packet: error setting multicast "
			    "interface, %s", iface->name);
			return (-1);
		}

	if (sendto(iface->fd, ibuf_data(buf), ibuf_size(buf), 0,
	    (struct sockaddr *)&sa6, sizeof(sa6)) == -1) {
		log_warn("send_packet: error sending packet on interface %s",
		    iface->name);
		return (-1);
	}

	return (0);
}

void
recv_packet(int fd, short event, void *bula)
{
	union {
		struct cmsghdr hdr;
		char	buf[CMSG_SPACE(sizeof(struct in6_pktinfo))];
	} cmsgbuf;
	struct msghdr		 msg;
	struct iovec		 iov;
	struct in6_addr		 addr, dest;
	struct sockaddr_in6	 src;
	struct ospfd_conf	*xconf = bula;
	struct ospf_hdr		*ospf_hdr;
	struct iface		*iface;
	struct nbr		*nbr = NULL;
	char			*buf;
	struct cmsghdr		*cmsg;
	ssize_t			 r;
	u_int16_t		 len;
	int			 l;
	unsigned int		 ifindex = 0;

	if (event != EV_READ)
		return;

	if (recv_buf == NULL)
		if ((recv_buf = malloc(READ_BUF_SIZE)) == NULL)
			fatal(__func__);

	/* setup buffer */
	bzero(&msg, sizeof(msg));
	iov.iov_base = buf = recv_buf;
	iov.iov_len = READ_BUF_SIZE;
	msg.msg_name = &src;
	msg.msg_namelen = sizeof(src);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);

	if ((r = recvmsg(fd, &msg, 0)) == -1) {
		if (errno != EAGAIN && errno != EINTR)
			log_debug("recv_packet: read error: %s",
			    strerror(errno));
		return;
	}
	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
	    cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level == IPPROTO_IPV6 &&
		    cmsg->cmsg_type == IPV6_PKTINFO) {
			ifindex = ((struct in6_pktinfo *)
			    CMSG_DATA(cmsg))->ipi6_ifindex;
			dest = ((struct in6_pktinfo *)
			    CMSG_DATA(cmsg))->ipi6_addr;
			break;
		}
	}

	/* find a matching interface */
	if ((iface = find_iface(xconf, ifindex, &src.sin6_addr)) == NULL) {
		/* XXX add a counter here */
		return;
	}
	/*
	 * Packet needs to be sent to AllSPFRouters or AllDRouters
	 * or to the address of the interface itself.
	 * AllDRouters is only valid for DR and BDR but this is checked later.
	 */
	inet_pton(AF_INET6, AllSPFRouters, &addr);
	if (!IN6_ARE_ADDR_EQUAL(&dest, &addr)) {
		inet_pton(AF_INET6, AllDRouters, &addr);
		if (!IN6_ARE_ADDR_EQUAL(&dest, &addr)) {
			struct iface_addr *ia;

			TAILQ_FOREACH(ia, &iface->ifa_list, entry) {
				if (IN6_ARE_ADDR_EQUAL(&dest, &ia->addr))
					break;
			}
			if (ia == NULL) {
				log_debug("recv_packet: packet sent to wrong "
				    "address %s, interface %s",
				    log_in6addr(&dest), iface->name);
				return;
			}
		}
	}

	len = (u_int16_t)r;
	/* OSPF header sanity checks */
	if (len < sizeof(*ospf_hdr)) {
		log_debug("recv_packet: bad packet size");
		return;
	}
	ospf_hdr = (struct ospf_hdr *)buf;

	if ((l = ospf_hdr_sanity_check(ospf_hdr, len, iface, &dest)) == -1)
		return;

	nbr = nbr_find_id(iface, ospf_hdr->rtr_id);
	if (ospf_hdr->type != PACKET_TYPE_HELLO && nbr == NULL) {
		log_debug("recv_packet: unknown neighbor ID");
		return;
	}

	buf += sizeof(*ospf_hdr);
	len = l - sizeof(*ospf_hdr);

	/* switch OSPF packet type */
	switch (ospf_hdr->type) {
	case PACKET_TYPE_HELLO:
		inet_pton(AF_INET6, AllDRouters, &addr);
		if (IN6_ARE_ADDR_EQUAL(&dest, &addr)) {
			log_debug("recv_packet: invalid destination IP "
			     "address");
			break;
		}

		recv_hello(iface, &src.sin6_addr, ospf_hdr->rtr_id, buf, len);
		break;
	case PACKET_TYPE_DD:
		recv_db_description(nbr, buf, len);
		break;
	case PACKET_TYPE_LS_REQUEST:
		recv_ls_req(nbr, buf, len);
		break;
	case PACKET_TYPE_LS_UPDATE:
		recv_ls_update(nbr, buf, len);
		break;
	case PACKET_TYPE_LS_ACK:
		recv_ls_ack(nbr, buf, len);
		break;
	default:
		log_debug("recv_packet: unknown OSPF packet type, interface %s",
		    iface->name);
	}
}

int
ospf_hdr_sanity_check(struct ospf_hdr *ospf_hdr, u_int16_t len,
    const struct iface *iface, struct in6_addr *dst)
{
	struct in6_addr		 addr;
	struct in_addr		 id;

	if (ospf_hdr->version != OSPF6_VERSION) {
		log_debug("recv_packet: invalid OSPF version %d",
		    ospf_hdr->version);
		return (-1);
	}

	if (ntohs(ospf_hdr->len) > len ||
	    len <= sizeof(struct ospf_hdr)) {
		log_debug("recv_packet: invalid OSPF packet length %d",
		    ntohs(ospf_hdr->len));
		return (-1);
	}

	if (iface->type != IF_TYPE_VIRTUALLINK) {
		if (ospf_hdr->area_id != iface->area->id.s_addr) {
			id.s_addr = ospf_hdr->area_id;
			log_debug("recv_packet: invalid area ID %s, "
			    "interface %s", inet_ntoa(id), iface->name);
			return (-1);
		}
	} else {
		if (ospf_hdr->area_id != 0) {
			id.s_addr = ospf_hdr->area_id;
			log_debug("recv_packet: invalid area ID %s, "
			    "interface %s", inet_ntoa(id), iface->name);
			return (-1);
		}
	}

	if (iface->type == IF_TYPE_BROADCAST || iface->type == IF_TYPE_NBMA) {
		if (inet_pton(AF_INET6, AllDRouters, &addr) == 0)
			fatalx("recv_packet: inet_pton");
		if (IN6_ARE_ADDR_EQUAL(dst, &addr) &&
		    (iface->state & IF_STA_DRORBDR) == 0) {
			log_debug("recv_packet: invalid destination IP in "
			    "state %s, interface %s",
			    if_state_name(iface->state), iface->name);
			return (-1);
		}
	}

	return (ntohs(ospf_hdr->len));
}

struct iface *
find_iface(struct ospfd_conf *xconf, unsigned int ifindex, struct in6_addr *src)
{
	struct area	*area;
	struct iface	*iface, *match = NULL;

	/*
	 * Returned interface needs to be active.
	 * Virtual-Links have higher precedence so the full interface
	 * list needs to be scanned for possible matches.
	 */
	LIST_FOREACH(area, &xconf->area_list, entry) {
		LIST_FOREACH(iface, &area->iface_list, entry) {
			switch (iface->type) {
			case IF_TYPE_VIRTUALLINK:
				if (IN6_ARE_ADDR_EQUAL(src, &iface->dst) &&
				    !(iface->cflags & F_IFACE_PASSIVE))
					return (iface);
				break;
			default:
				if (ifindex == iface->ifindex &&
				    !(iface->cflags & F_IFACE_PASSIVE))
					match = iface;
				break;
			}
		}
	}

	return (match);
}
