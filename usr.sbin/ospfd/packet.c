/*	$OpenBSD: packet.c,v 1.38 2024/08/21 15:18:00 florian Exp $ */

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
#include <arpa/inet.h>
#include <net/if_dl.h>

#include <errno.h>
#include <event.h>
#include <stdlib.h>
#include <string.h>

#include "ospfd.h"
#include "ospf.h"
#include "log.h"
#include "ospfe.h"

int		 ip_hdr_sanity_check(const struct ip *, u_int16_t);
int		 ospf_hdr_sanity_check(const struct ip *,
		    struct ospf_hdr *, u_int16_t, const struct iface *);
struct iface	*find_iface(struct ospfd_conf *, unsigned int, struct in_addr);

static u_int8_t	*recv_buf;

int
gen_ospf_hdr(struct ibuf *buf, struct iface *iface, u_int8_t type)
{
	struct ospf_hdr	ospf_hdr;

	bzero(&ospf_hdr, sizeof(ospf_hdr));
	ospf_hdr.version = OSPF_VERSION;
	ospf_hdr.type = type;
	ospf_hdr.rtr_id = ospfe_router_id();
	if (iface->type != IF_TYPE_VIRTUALLINK)
		ospf_hdr.area_id = iface->area->id.s_addr;
	ospf_hdr.auth_type = htons(iface->auth_type);

	return (ibuf_add(buf, &ospf_hdr, sizeof(ospf_hdr)));
}

/* send and receive packets */
int
send_packet(struct iface *iface, struct ibuf *buf, struct sockaddr_in *dst)
{
	struct msghdr		 msg;
	struct iovec		 iov[2];
	struct ip		 ip_hdr;

	/* setup IP hdr */
	bzero(&ip_hdr, sizeof(ip_hdr));
	ip_hdr.ip_v = IPVERSION;
	ip_hdr.ip_hl = sizeof(ip_hdr) >> 2;
	ip_hdr.ip_tos = IPTOS_PREC_INTERNETCONTROL;
	ip_hdr.ip_len = htons(ibuf_size(buf) + sizeof(ip_hdr));
	ip_hdr.ip_id = 0;  /* 0 means kernel set appropriate value */
	ip_hdr.ip_off = 0;
	ip_hdr.ip_ttl = iface->type != IF_TYPE_VIRTUALLINK ?
	    IP_DEFAULT_MULTICAST_TTL : MAXTTL;
	ip_hdr.ip_p = IPPROTO_OSPF;
	ip_hdr.ip_sum = 0;
	ip_hdr.ip_src = iface->addr;
	ip_hdr.ip_dst = dst->sin_addr;

	/* setup buffer */
	bzero(&msg, sizeof(msg));
	iov[0].iov_base = &ip_hdr;
	iov[0].iov_len = sizeof(ip_hdr);
	iov[1].iov_base = ibuf_data(buf);
	iov[1].iov_len = ibuf_size(buf);
	msg.msg_name = dst;
	msg.msg_namelen = sizeof(*dst);
	msg.msg_iov = iov;
	msg.msg_iovlen = 2;

	/* set outgoing interface for multicast traffic */
	if (IN_MULTICAST(ntohl(dst->sin_addr.s_addr)))
		if (if_set_mcast(iface) == -1)
			return (-1);

	if (sendmsg(iface->fd, &msg, 0) == -1) {
		log_warn("%s: error sending packet to %s on interface %s",
		    __func__, inet_ntoa(ip_hdr.ip_dst), iface->name);
		return (-1);
	}

	return (0);
}

void
recv_packet(int fd, short event, void *bula)
{
	union {
		struct cmsghdr hdr;
		char	buf[CMSG_SPACE(sizeof(struct sockaddr_dl))];
	} cmsgbuf;
	struct msghdr		 msg;
	struct iovec		 iov;
	struct ip		 ip_hdr;
	struct in_addr		 addr;
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
		if (cmsg->cmsg_level == IPPROTO_IP &&
		    cmsg->cmsg_type == IP_RECVIF) {
			ifindex = ((struct sockaddr_dl *)
			    CMSG_DATA(cmsg))->sdl_index;
			break;
		}
	}

	len = (u_int16_t)r;

	/* IP header sanity checks */
	if (len < sizeof(ip_hdr)) {
		log_warnx("recv_packet: bad packet size");
		return;
	}
	memcpy(&ip_hdr, buf, sizeof(ip_hdr));
	if ((l = ip_hdr_sanity_check(&ip_hdr, len)) == -1)
		return;
	buf += l;
	len -= l;

	/* find a matching interface */
	if ((iface = find_iface(xconf, ifindex, ip_hdr.ip_src)) == NULL) {
		/* XXX add a counter here */
		return;
	}

	/*
	 * Packet needs to be sent to AllSPFRouters or AllDRouters
	 * or to the address of the interface itself.
	 * AllDRouters is only valid for DR and BDR but this is checked later.
	 */
	inet_pton(AF_INET, AllSPFRouters, &addr);
	if (ip_hdr.ip_dst.s_addr != addr.s_addr) {
		inet_pton(AF_INET, AllDRouters, &addr);
		if (ip_hdr.ip_dst.s_addr != addr.s_addr) {
			if (ip_hdr.ip_dst.s_addr != iface->addr.s_addr) {
				log_debug("recv_packet: packet sent to wrong "
				    "address %s, interface %s",
				    inet_ntoa(ip_hdr.ip_dst), iface->name);
				return;
			}
		}
	}

	/* OSPF header sanity checks */
	if (len < sizeof(*ospf_hdr)) {
		log_debug("recv_packet: bad packet size");
		return;
	}
	ospf_hdr = (struct ospf_hdr *)buf;

	if ((l = ospf_hdr_sanity_check(&ip_hdr, ospf_hdr, len, iface)) == -1)
		return;

	nbr = nbr_find_id(iface, ospf_hdr->rtr_id);
	if (ospf_hdr->type != PACKET_TYPE_HELLO && nbr == NULL) {
		log_debug("recv_packet: unknown neighbor ID");
		return;
	}

	if (auth_validate(buf, len, iface, nbr)) {
		if (nbr == NULL)
			log_warnx("recv_packet: authentication error, "
			    "interface %s", iface->name);
		else
			log_warnx("recv_packet: authentication error, "
			    "neighbor ID %s interface %s",
			    inet_ntoa(nbr->id), iface->name);
		return;
	}

	buf += sizeof(*ospf_hdr);
	len = l - sizeof(*ospf_hdr);

	/* switch OSPF packet type */
	switch (ospf_hdr->type) {
	case PACKET_TYPE_HELLO:
		inet_pton(AF_INET, AllSPFRouters, &addr);
		if (iface->type == IF_TYPE_BROADCAST ||
		    iface->type == IF_TYPE_POINTOPOINT)
			if (ip_hdr.ip_dst.s_addr != addr.s_addr) {
				log_warnx("%s: hello ignored on interface %s, "
				    "invalid destination IP address %s",
				    __func__, iface->name,
				    inet_ntoa(ip_hdr.ip_dst));
				break;
			}

		recv_hello(iface, ip_hdr.ip_src, ospf_hdr->rtr_id, buf, len);
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
ip_hdr_sanity_check(const struct ip *ip_hdr, u_int16_t len)
{
	if (ntohs(ip_hdr->ip_len) != len) {
		log_debug("recv_packet: invalid IP packet length %u",
		    ntohs(ip_hdr->ip_len));
		return (-1);
	}

	if (ip_hdr->ip_p != IPPROTO_OSPF)
		/* this is enforced by the socket itself */
		fatalx("recv_packet: invalid IP proto");

	return (ip_hdr->ip_hl << 2);
}

int
ospf_hdr_sanity_check(const struct ip *ip_hdr, struct ospf_hdr *ospf_hdr,
    u_int16_t len, const struct iface *iface)
{
	struct in_addr		 addr;

	if (ospf_hdr->version != OSPF_VERSION) {
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
			addr.s_addr = ospf_hdr->area_id;
			log_debug("recv_packet: invalid area ID %s, "
			    "interface %s", inet_ntoa(addr), iface->name);
			return (-1);
		}
	} else {
		if (ospf_hdr->area_id != 0) {
			addr.s_addr = ospf_hdr->area_id;
			log_debug("recv_packet: invalid area ID %s, "
			    "interface %s", inet_ntoa(addr), iface->name);
			return (-1);
		}
	}

	if (iface->type == IF_TYPE_BROADCAST || iface->type == IF_TYPE_NBMA) {
		inet_pton(AF_INET, AllDRouters, &addr);
		if (ip_hdr->ip_dst.s_addr == addr.s_addr &&
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
find_iface(struct ospfd_conf *xconf, unsigned int ifindex, struct in_addr src)
{
	struct area	*area = NULL;
	struct iface	*iface = NULL;

	/* returned interface needs to be active */
	LIST_FOREACH(area, &xconf->area_list, entry) {
		LIST_FOREACH(iface, &area->iface_list, entry) {
			switch (iface->type) {
			case IF_TYPE_VIRTUALLINK:
				if ((src.s_addr == iface->dst.s_addr) &&
				    !iface->passive)
					return (iface);
				break;
			case IF_TYPE_POINTOPOINT:
				if (ifindex == iface->ifindex &&
				    !iface->passive)
					return (iface);
				break;
			default:
				if (ifindex == iface->ifindex &&
				    (iface->addr.s_addr & iface->mask.s_addr) ==
				    (src.s_addr & iface->mask.s_addr) &&
				    !iface->passive)
					return (iface);
				break;
			}
		}
	}

	return (NULL);
}
