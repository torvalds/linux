/*	$OpenBSD: packet.c,v 1.23 2023/12/14 10:02:27 claudio Exp $ */

/*
 * Copyright (c) 2015 Renato Westphal <renato@openbsd.org>
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
#include <net/if_dl.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <arpa/inet.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eigrpd.h"
#include "eigrpe.h"
#include "log.h"

static int	 send_packet_v4(struct iface *, struct nbr *, struct ibuf *);
static int	 send_packet_v6(struct iface *, struct nbr *, struct ibuf *);
static int	 recv_packet_nbr(struct nbr *, struct eigrp_hdr *,
		    struct seq_addr_head *, struct tlv_mcast_seq *);
static void	 recv_packet_eigrp(int, union eigrpd_addr *,
		    union eigrpd_addr *, struct iface *, struct eigrp_hdr *,
		    char *, uint16_t);
static int	 eigrp_hdr_sanity_check(int, union eigrpd_addr *,
		    struct eigrp_hdr *, uint16_t, const struct iface *);
static struct iface *find_iface(unsigned int, int, union eigrpd_addr *);

int
gen_eigrp_hdr(struct ibuf *buf, uint16_t opcode, uint8_t flags,
    uint32_t seq_num, uint16_t as)
{
	struct eigrp_hdr	eigrp_hdr;

	memset(&eigrp_hdr, 0, sizeof(eigrp_hdr));
	eigrp_hdr.version = EIGRP_VERSION;
	eigrp_hdr.opcode = opcode;
	/* chksum will be set later */
	eigrp_hdr.flags = htonl(flags);
	eigrp_hdr.seq_num = htonl(seq_num);
	/* ack_num will be set later */
	eigrp_hdr.vrid = htons(EIGRP_VRID_UNICAST_AF);
	eigrp_hdr.as = htons(as);

	return (ibuf_add(buf, &eigrp_hdr, sizeof(eigrp_hdr)));
}

/* send and receive packets */
static int
send_packet_v4(struct iface *iface, struct nbr *nbr, struct ibuf *buf)
{
	struct sockaddr_in	 dst;
	struct msghdr		 msg;
	struct iovec		 iov[2];
	struct ip		 ip_hdr;

	/* setup sockaddr */
	dst.sin_family = AF_INET;
	dst.sin_len = sizeof(struct sockaddr_in);
	if (nbr)
		dst.sin_addr = nbr->addr.v4;
	else
		dst.sin_addr = global.mcast_addr_v4;

	/* setup IP hdr */
	memset(&ip_hdr, 0, sizeof(ip_hdr));
	ip_hdr.ip_v = IPVERSION;
	ip_hdr.ip_hl = sizeof(ip_hdr) >> 2;
	ip_hdr.ip_tos = IPTOS_PREC_INTERNETCONTROL;
	ip_hdr.ip_len = htons(ibuf_size(buf) + sizeof(ip_hdr));
	ip_hdr.ip_id = 0;  /* 0 means kernel set appropriate value */
	ip_hdr.ip_off = 0;
	ip_hdr.ip_ttl = EIGRP_IP_TTL;
	ip_hdr.ip_p = IPPROTO_EIGRP;
	ip_hdr.ip_sum = 0;
	ip_hdr.ip_src.s_addr = if_primary_addr(iface);
	ip_hdr.ip_dst = dst.sin_addr;

	/* setup buffer */
	memset(&msg, 0, sizeof(msg));
	iov[0].iov_base = &ip_hdr;
	iov[0].iov_len = sizeof(ip_hdr);
	iov[1].iov_base = ibuf_data(buf);
	iov[1].iov_len = ibuf_size(buf);
	msg.msg_name = &dst;
	msg.msg_namelen = sizeof(dst);
	msg.msg_iov = iov;
	msg.msg_iovlen = 2;

	/* set outgoing interface for multicast traffic */
	if (IN_MULTICAST(ntohl(dst.sin_addr.s_addr)))
		if (if_set_ipv4_mcast(iface) == -1) {
			log_warn("%s: error setting multicast interface, %s",
			    __func__, iface->name);
			return (-1);
		}

	if (sendmsg(global.eigrp_socket_v4, &msg, 0) == -1) {
		log_warn("%s: error sending packet on interface %s",
		    __func__, iface->name);
		return (-1);
	}

	return (0);
}

static int
send_packet_v6(struct iface *iface, struct nbr *nbr, struct ibuf *buf)
{
	struct sockaddr_in6	 sa6;

	/* setup sockaddr */
	memset(&sa6, 0, sizeof(sa6));
	sa6.sin6_family = AF_INET6;
	sa6.sin6_len = sizeof(struct sockaddr_in6);
	if (nbr) {
		sa6.sin6_addr = nbr->addr.v6;
		addscope(&sa6, iface->ifindex);
	} else
		sa6.sin6_addr = global.mcast_addr_v6;

	/* set outgoing interface for multicast traffic */
	if (IN6_IS_ADDR_MULTICAST(&sa6.sin6_addr))
		if (if_set_ipv6_mcast(iface) == -1) {
			log_warn("%s: error setting multicast interface, %s",
			    __func__, iface->name);
			return (-1);
		}

	if (sendto(global.eigrp_socket_v6, ibuf_data(buf), ibuf_size(buf), 0,
	    (struct sockaddr *)&sa6, sizeof(sa6)) == -1) {
		log_warn("%s: error sending packet on interface %s",
		    __func__, iface->name);
		return (-1);
	}

	return (0);
}

int
send_packet(struct eigrp_iface *ei, struct nbr *nbr, uint32_t flags,
    struct ibuf *buf)
{
	struct eigrp		*eigrp = ei->eigrp;
	struct iface		*iface = ei->iface;
	struct ibuf		 ebuf;
	struct eigrp_hdr	 eigrp_hdr;

	if (!(iface->flags & IFF_UP) || !LINK_STATE_IS_UP(iface->linkstate))
		return (-1);

	/* update ack number, flags and checksum */
	if (nbr) {
		if (ibuf_set_n32(buf, offsetof(struct eigrp_hdr, ack_num),
		    nbr->recv_seq) == -1)
			fatalx("send_packet: set of ack_num failed");
		rtp_ack_stop_timer(nbr);
	}

	ibuf_from_ibuf(buf, &ebuf);
	if (ibuf_get(&ebuf, &eigrp_hdr, sizeof(eigrp_hdr)) == -1)
		fatalx("send_packet: get hdr failed");

	if (flags) {
		flags |= ntohl(eigrp_hdr.flags);
		if (ibuf_set_n32(buf, offsetof(struct eigrp_hdr, flags),
		    flags) == -1)
			fatalx("send_packet: set of flags failed");
	}

	if (ibuf_set_n16(buf, offsetof(struct eigrp_hdr, chksum), 0) == -1)
		fatalx("send_packet: set of chksum failed");
	if (ibuf_set_n16(buf, offsetof(struct eigrp_hdr, chksum),
	    in_cksum(ibuf_data(buf), ibuf_size(buf))) == -1)
		fatalx("send_packet: set of chksum failed");

	/* log packet being sent */
	if (eigrp_hdr.opcode != EIGRP_OPC_HELLO) {
		char	buffer[64];

		if (nbr)
			snprintf(buffer, sizeof(buffer), "nbr %s",
			    log_addr(eigrp->af, &nbr->addr));
		else
			snprintf(buffer, sizeof(buffer), "(multicast)");

		log_debug("%s: type %s iface %s %s AS %u seq %u ack %u",
		    __func__, opcode_name(eigrp_hdr.opcode), iface->name,
		    buffer, ntohs(eigrp_hdr.as), ntohl(eigrp_hdr.seq_num),
		    ntohl(eigrp_hdr.ack_num));
	}

	switch (eigrp->af) {
	case AF_INET:
		if (send_packet_v4(iface, nbr, buf) < 0)
			return (-1);
		break;
	case AF_INET6:
		if (send_packet_v6(iface, nbr, buf) < 0)
			return (-1);
		break;
	default:
		fatalx("send_packet: unknown af");
	}

	switch (eigrp_hdr.opcode) {
	case EIGRP_OPC_HELLO:
		if (ntohl(eigrp_hdr.ack_num) == 0)
			ei->eigrp->stats.hellos_sent++;
		else
			ei->eigrp->stats.acks_sent++;
		break;
	case EIGRP_OPC_UPDATE:
		ei->eigrp->stats.updates_sent++;
		break;
	case EIGRP_OPC_QUERY:
		ei->eigrp->stats.queries_sent++;
		break;
	case EIGRP_OPC_REPLY:
		ei->eigrp->stats.replies_sent++;
		break;
	case EIGRP_OPC_SIAQUERY:
		ei->eigrp->stats.squeries_sent++;
		break;
	case EIGRP_OPC_SIAREPLY:
		ei->eigrp->stats.sreplies_sent++;
		break;
	default:
		break;
	}

	return (0);
}

static int
recv_packet_nbr(struct nbr *nbr, struct eigrp_hdr *eigrp_hdr,
    struct seq_addr_head *seq_addr_list, struct tlv_mcast_seq *tm)
{
	uint32_t		 seq, ack;
	struct seq_addr_entry	*sa;

	seq = ntohl(eigrp_hdr->seq_num);
	ack = ntohl(eigrp_hdr->ack_num);

	/*
	 * draft-savage-eigrp-04 - Section 5.3.1:
	 * "In addition to the HELLO packet, if any packet is received within
	 * the hold time period, then the Hold Time period will be reset."
	 */
	nbr_start_timeout(nbr);

	/* handle the sequence tlv */
	if (eigrp_hdr->opcode == EIGRP_OPC_HELLO &&
	    !TAILQ_EMPTY(seq_addr_list)) {
		nbr->flags |= F_EIGRP_NBR_CR_MODE;

		TAILQ_FOREACH(sa, seq_addr_list, entry) {
			switch (sa->af) {
			case AF_INET:
				if (sa->addr.v4.s_addr ==
				    if_primary_addr(nbr->ei->iface)) {
					nbr->flags &= ~F_EIGRP_NBR_CR_MODE;
					break;
				}
				break;
			case AF_INET6:
				if (IN6_ARE_ADDR_EQUAL(&sa->addr.v6,
				    &nbr->ei->iface->linklocal)) {
					nbr->flags &= ~F_EIGRP_NBR_CR_MODE;
					break;
				}
				break;
			default:
				break;
			}
		}
		if (tm)
			nbr->next_mcast_seq = ntohl(tm->seq);
	}

	if ((ntohl(eigrp_hdr->flags) & EIGRP_HDR_FLAG_CR)) {
		if (!(nbr->flags & F_EIGRP_NBR_CR_MODE))
			return (-1);
		nbr->flags &= ~F_EIGRP_NBR_CR_MODE;
		if (ntohl(eigrp_hdr->seq_num) != nbr->next_mcast_seq)
			return (-1);
	}

	/* ack processing */
	if (ack != 0)
		rtp_process_ack(nbr, ack);
	if (seq != 0) {
		/* check for sequence wraparound */
		if (nbr->recv_seq >= seq &&
		   !(nbr->recv_seq == UINT32_MAX && seq == 1)) {
			log_debug("%s: duplicate packet", __func__);
			rtp_send_ack(nbr);
			return (-1);
		}
		nbr->recv_seq = seq;
	}

	return (0);
}

static void
recv_packet_eigrp(int af, union eigrpd_addr *src, union eigrpd_addr *dest,
    struct iface *iface, struct eigrp_hdr *eigrp_hdr, char *buf, uint16_t len)
{
	struct eigrp_iface	*ei;
	struct nbr		*nbr;
	struct tlv_parameter	*tp = NULL;
	struct tlv_sw_version	*tv = NULL;
	struct tlv_mcast_seq	*tm = NULL;
	struct rinfo		 ri;
	struct rinfo_entry	*re;
	struct seq_addr_head	 seq_addr_list;
	struct rinfo_head	 rinfo_list;

	/* EIGRP header sanity checks */
	if (eigrp_hdr_sanity_check(af, dest, eigrp_hdr, len, iface) == -1)
		return;

	buf += sizeof(*eigrp_hdr);
	len -= sizeof(*eigrp_hdr);

	TAILQ_INIT(&seq_addr_list);
	TAILQ_INIT(&rinfo_list);
	while (len > 0) {
		struct tlv 	tlv;
		uint16_t	tlv_type;

		if (len < sizeof(tlv)) {
			log_debug("%s: malformed packet (bad length)",
			    __func__);
			goto error;
		}

		memcpy(&tlv, buf, sizeof(tlv));
		if (ntohs(tlv.length) > len) {
			log_debug("%s: malformed packet (bad length)",
			    __func__);
			goto error;
		}

		tlv_type = ntohs(tlv.type);
		switch (tlv_type) {
		case TLV_TYPE_PARAMETER:
			if ((tp = tlv_decode_parameter(&tlv, buf)) == NULL)
				goto error;
			break;
		case TLV_TYPE_SEQ:
			if (tlv_decode_seq(af, &tlv, buf, &seq_addr_list) < 0)
				goto error;
			break;
		case TLV_TYPE_SW_VERSION:
			if ((tv = tlv_decode_sw_version(&tlv, buf)) == NULL)
				goto error;
			break;
		case TLV_TYPE_MCAST_SEQ:
			if ((tm = tlv_decode_mcast_seq(&tlv, buf)) == NULL)
				goto error;
			break;
		case TLV_TYPE_IPV4_INTERNAL:
		case TLV_TYPE_IPV4_EXTERNAL:
		case TLV_TYPE_IPV6_INTERNAL:
		case TLV_TYPE_IPV6_EXTERNAL:
			/* silently ignore TLV from different address-family */
			if ((tlv_type & TLV_PROTO_MASK) == TLV_PROTO_IPV4 &&
			    af != AF_INET)
				break;
			if ((tlv_type & TLV_PROTO_MASK) == TLV_PROTO_IPV6 &&
			    af != AF_INET6)
				break;

			if (tlv_decode_route(af, &tlv, buf, &ri) < 0)
				goto error;
			if ((re = calloc(1, sizeof(*re))) == NULL)
				fatal("recv_packet_eigrp");
			re->rinfo = ri;
			TAILQ_INSERT_TAIL(&rinfo_list, re, entry);
			break;
		case TLV_TYPE_AUTH:
		case TLV_TYPE_PEER_TERM:
			/*
			 * XXX There is no enough information in the draft
			 * to implement these TLVs properly.
			 */
		case TLV_TYPE_IPV4_COMMUNITY:
		case TLV_TYPE_IPV6_COMMUNITY:
			/* TODO */
		default:
			/* ignore unknown tlv */
			break;
		}
		buf += ntohs(tlv.length);
		len -= ntohs(tlv.length);
	}

	ei = eigrp_if_lookup(iface, af, ntohs(eigrp_hdr->as));
	if (ei == NULL || ei->passive)
		goto error;

	nbr = nbr_find(ei, src);
	if (nbr == NULL && (eigrp_hdr->opcode != EIGRP_OPC_HELLO ||
	    ntohl(eigrp_hdr->ack_num) != 0)) {
		log_debug("%s: unknown neighbor", __func__);
		goto error;
	} else if (nbr && recv_packet_nbr(nbr, eigrp_hdr, &seq_addr_list,
	    tm) < 0)
		goto error;

	/* log packet being received */
	if (eigrp_hdr->opcode != EIGRP_OPC_HELLO)
		log_debug("%s: type %s nbr %s AS %u seq %u ack %u", __func__,
		    opcode_name(eigrp_hdr->opcode), log_addr(af, &nbr->addr),
		    ei->eigrp->as, ntohl(eigrp_hdr->seq_num),
		    ntohl(eigrp_hdr->ack_num));

	/* switch EIGRP packet type */
	switch (eigrp_hdr->opcode) {
	case EIGRP_OPC_HELLO:
		if (ntohl(eigrp_hdr->ack_num) == 0) {
			recv_hello(ei, src, nbr, tp);
			ei->eigrp->stats.hellos_recv++;
		} else
			ei->eigrp->stats.acks_recv++;
		break;
	case EIGRP_OPC_UPDATE:
		recv_update(nbr, &rinfo_list, ntohl(eigrp_hdr->flags));
		ei->eigrp->stats.updates_recv++;
		break;
	case EIGRP_OPC_QUERY:
		recv_query(nbr, &rinfo_list, 0);
		ei->eigrp->stats.queries_recv++;
		break;
	case EIGRP_OPC_REPLY:
		recv_reply(nbr, &rinfo_list, 0);
		ei->eigrp->stats.replies_recv++;
		break;
	case EIGRP_OPC_SIAQUERY:
		recv_query(nbr, &rinfo_list, 1);
		ei->eigrp->stats.squeries_recv++;
		break;
	case EIGRP_OPC_SIAREPLY:
		recv_reply(nbr, &rinfo_list, 1);
		ei->eigrp->stats.sreplies_recv++;
		break;
	default:
		log_debug("%s: unknown EIGRP packet type, interface %s",
		    __func__, iface->name);
	}

error:
	/* free rinfo tlvs */
	message_list_clr(&rinfo_list);
	/* free seq addresses tlvs */
	seq_addr_list_clr(&seq_addr_list);
}

#define CMSG_MAXLEN max(sizeof(struct sockaddr_dl), sizeof(struct in6_pktinfo))
void
recv_packet(int fd, short event, void *bula)
{
	union {
		struct	cmsghdr hdr;
		char	buf[CMSG_SPACE(CMSG_MAXLEN)];
	} cmsgbuf;
	struct msghdr		 msg;
	struct sockaddr_storage	 from;
	struct iovec		 iov;
	struct ip		 ip_hdr;
	char 			 pkt[READ_BUF_SIZE];
	char			*buf;
	struct cmsghdr		*cmsg;
	ssize_t			 r;
	uint16_t		 len;
	int			 af;
	union eigrpd_addr	 src, dest;
	unsigned int		 ifindex = 0;
	struct iface		*iface;
	struct eigrp_hdr	*eigrp_hdr;

	if (event != EV_READ)
		return;

	/* setup buffer */
	memset(&msg, 0, sizeof(msg));
	iov.iov_base = buf = pkt;
	iov.iov_len = READ_BUF_SIZE;
	msg.msg_name = &from;
	msg.msg_namelen = sizeof(from);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);

	if ((r = recvmsg(fd, &msg, 0)) == -1) {
		if (errno != EAGAIN && errno != EINTR)
			log_debug("%s: read error: %s", __func__,
			    strerror(errno));
		return;
	}
	len = (uint16_t)r;

	sa2addr((struct sockaddr *)&from, &af, &src);
	if (bad_addr(af, &src)) {
		log_debug("%s: invalid source address: %s", __func__,
		    log_addr(af, &src));
		return;
	}

	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
	    cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (af == AF_INET && cmsg->cmsg_level == IPPROTO_IP &&
		    cmsg->cmsg_type == IP_RECVIF) {
			ifindex = ((struct sockaddr_dl *)
			    CMSG_DATA(cmsg))->sdl_index;
			break;
		}
		if (af == AF_INET6 && cmsg->cmsg_level == IPPROTO_IPV6 &&
		    cmsg->cmsg_type == IPV6_PKTINFO) {
			ifindex = ((struct in6_pktinfo *)
			    CMSG_DATA(cmsg))->ipi6_ifindex;
			dest.v6 = ((struct in6_pktinfo *)
			    CMSG_DATA(cmsg))->ipi6_addr;
			break;
		}
	}

	/* find a matching interface */
	if ((iface = find_iface(ifindex, af, &src)) == NULL)
		return;

	/* the IPv4 raw sockets API gives us direct access to the IP header */
	if (af == AF_INET) {
		if (len < sizeof(ip_hdr)) {
			log_debug("%s: bad packet size", __func__);
			return;
		}
		memcpy(&ip_hdr, buf, sizeof(ip_hdr));
		if (ntohs(ip_hdr.ip_len) != len) {
			log_debug("%s: invalid IP packet length %u", __func__,
			    ntohs(ip_hdr.ip_len));
			return;
		}
		buf += ip_hdr.ip_hl << 2;
		len -= ip_hdr.ip_hl << 2;
		dest.v4 = ip_hdr.ip_dst;
	}

	/* validate destination address */
	switch (af) {
	case AF_INET:
		/*
		 * Packet needs to be sent to 224.0.0.10 or to one of the
		 * interface addresses.
		 */
		if (dest.v4.s_addr != global.mcast_addr_v4.s_addr) {
			struct if_addr	*if_addr;
			int		 found = 0;

			TAILQ_FOREACH(if_addr, &iface->addr_list, entry)
				if (if_addr->af == AF_INET &&
				    dest.v4.s_addr == if_addr->addr.v4.s_addr) {
					found = 1;
					break;
				}
			if (found == 0) {
				log_debug("%s: packet sent to wrong address "
				    "%s, interface %s", __func__,
				    inet_ntoa(dest.v4), iface->name);
				return;
			}
		}
		break;
	case AF_INET6:
		/*
		 * Packet needs to be sent to ff02::a or to the link local
		 * address of the interface.
		 */
		if (!IN6_ARE_ADDR_EQUAL(&dest.v6, &global.mcast_addr_v6) &&
		    !IN6_ARE_ADDR_EQUAL(&dest.v6, &iface->linklocal)) {
			log_debug("%s: packet sent to wrong address %s, "
			    "interface %s", __func__, log_in6addr(&dest.v6),
			    iface->name);
			return;
		}
		break;
	default:
		fatalx("recv_packet: unknown af");
		break;
	}

	if (len < sizeof(*eigrp_hdr)) {
		log_debug("%s: bad packet size", __func__);
		return;
	}
	eigrp_hdr = (struct eigrp_hdr *)buf;

	recv_packet_eigrp(af, &src, &dest, iface, eigrp_hdr, buf, len);
}

static int
eigrp_hdr_sanity_check(int af, union eigrpd_addr *addr,
    struct eigrp_hdr *eigrp_hdr, uint16_t len, const struct iface *iface)
{
	if (in_cksum(eigrp_hdr, len)) {
		log_debug("%s: invalid checksum, interface %s", __func__,
		    iface->name);
		return (-1);
	}

	if (eigrp_hdr->version != EIGRP_HEADER_VERSION) {
		log_debug("%s: invalid EIGRP version %d, interface %s",
		    __func__, eigrp_hdr->version, iface->name);
		return (-1);
	}

	if (ntohs(eigrp_hdr->vrid) != EIGRP_VRID_UNICAST_AF) {
		log_debug("%s: unknown or unsupported vrid %u, interface %s",
		    __func__, ntohs(eigrp_hdr->vrid), iface->name);
		return (-1);
	}

	if (eigrp_hdr->opcode == EIGRP_OPC_HELLO &&
	    eigrp_hdr->ack_num != 0) {
		switch (af) {
		case AF_INET:
			if (IN_MULTICAST(addr->v4.s_addr)) {
				log_debug("%s: multicast ack (ipv4), "
				    "interface %s", __func__, iface->name);
				return (-1);
			}
			break;
		case AF_INET6:
			if (IN6_IS_ADDR_MULTICAST(&addr->v6)) {
				log_debug("%s: multicast ack (ipv6), "
				    "interface %s", __func__, iface->name);
				return (-1);
			}
			break;
		default:
			fatalx("eigrp_hdr_sanity_check: unknown af");
		}
	}

	return (0);
}

static struct iface *
find_iface(unsigned int ifindex, int af, union eigrpd_addr *src)
{
	struct iface	*iface;
	struct if_addr	*if_addr;
	in_addr_t	 mask;

	iface = if_lookup(econf, ifindex);
	if (iface == NULL)
		return (NULL);

	switch (af) {
	case AF_INET:
		/*
		 * From CCNP ROUTE 642-902 OCG:
		 * "EIGRP's rules about neighbor IP addresses being in the same
		 * subnet are less exact than OSPF. OSPF requires matching
		 * subnet numbers and masks. EIGRP just asks the question of
		 * whether the neighbor's IP address is in the range of
		 * addresses for the subnet as known to the local router."
		 */
		TAILQ_FOREACH(if_addr, &iface->addr_list, entry) {
			if (if_addr->af == AF_INET) {
				mask = prefixlen2mask(if_addr->prefixlen);

				if ((if_addr->addr.v4.s_addr & mask) ==
				    (src->v4.s_addr & mask))
					return (iface);
			}
		}
		break;
	case AF_INET6:
		/*
		 * draft-savage-eigrp-04 - Section 10.1:
		 * "EIGRP IPv6 will check that a received HELLO contains a valid
		 * IPv6 link-local source address."
		 */
		if (IN6_IS_ADDR_LINKLOCAL(&src->v6))
			return (iface);
		break;
	default:
		fatalx("find_iface: unknown af");
	}

	return (NULL);
}
