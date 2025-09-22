/*	$OpenBSD: packet.c,v 1.9 2024/08/21 09:18:47 florian Exp $ */

/*
 * Copyright (c) 2004, 2005, 2006 Esben Norby <norby@openbsd.org>
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
#include <sys/time.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_mroute.h>
#include <arpa/inet.h>

#include <errno.h>
#include <event.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "igmp.h"
#include "dvmrpd.h"
#include "dvmrp.h"
#include "log.h"
#include "dvmrpe.h"

int		 ip_hdr_sanity_check(const struct ip *, u_int16_t);
int		 dvmrp_hdr_sanity_check(const struct ip *, struct dvmrp_hdr *,
		     u_int16_t, const struct iface *);
struct iface	*find_iface(struct dvmrpd_conf *, struct in_addr);

static u_int8_t	*recv_buf;

int
gen_dvmrp_hdr(struct ibuf *buf, struct iface *iface, u_int8_t code)
{
	struct dvmrp_hdr	dvmrp_hdr;

	memset(&dvmrp_hdr, 0, sizeof(dvmrp_hdr));
	dvmrp_hdr.type = PKT_TYPE_DVMRP;
	dvmrp_hdr.code = code;
	dvmrp_hdr.chksum = 0;				/* updated later */
	dvmrp_hdr.capabilities = DVMRP_CAP_DEFAULT;	/* XXX update */
	dvmrp_hdr.minor_version = DVMRP_MINOR_VERSION;
	dvmrp_hdr.major_version = DVMRP_MAJOR_VERSION;

	return (ibuf_add(buf, &dvmrp_hdr, sizeof(dvmrp_hdr)));
}

/* send and receive packets */
int
send_packet(struct iface *iface, struct ibuf *pkt, struct sockaddr_in *dst)
{
	u_int16_t	chksum;

	if (iface->passive) {
		log_warnx("send_packet: cannot send packet on passive "
		    "interface %s", iface->name);
		return (-1);
	}

	/* set outgoing interface for multicast traffic */
	if (IN_MULTICAST(ntohl(dst->sin_addr.s_addr)))
		if (if_set_mcast(iface) == -1) {
			log_warn("send_packet: error setting multicast "
			    "interface, %s", iface->name);
			return (-1);
		}

	/* update chksum */
	chksum = in_cksum(ibuf_data(pkt), ibuf_size(pkt));
	if (ibuf_set(pkt, offsetof(struct dvmrp_hdr, chksum),
	    &chksum, sizeof(chksum)) == -1) {
		log_warn("send_packet: failed to update checksum");
		return (-1);
	}

	if (sendto(iface->fd, ibuf_data(pkt), ibuf_size(pkt), 0,
	    (struct sockaddr *)dst, sizeof(*dst)) == -1 ) {
		log_warn("send_packet: error sending packet on interface %s",
		    iface->name);
		return (-1);
	}

	return (0);
}

void
recv_packet(int fd, short event, void *bula)
{
	struct dvmrpd_conf	*xconf = bula;
	struct ip		 ip_hdr;
	struct dvmrp_hdr	*dvmrp_hdr;
	struct iface		*iface;
	struct nbr		*nbr = NULL;
	struct in_addr		 addr;
	char			*buf;
	ssize_t			 r;
	u_int16_t		 len;
	int			 l;

	if (event != EV_READ)
		return;

	if (recv_buf == NULL)
		if ((recv_buf = malloc(READ_BUF_SIZE)) == NULL)
			fatal(__func__);

	buf = recv_buf;
	if ((r = recvfrom(fd, buf, READ_BUF_SIZE, 0, NULL, NULL)) == -1) {
		if (errno != EAGAIN && errno != EINTR)
			log_debug("recv_packet: error receiving packet");
		return;
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
	if ((iface = find_iface(xconf, ip_hdr.ip_src)) == NULL) {
		log_debug("recv_packet: cannot find valid interface, ip src %s",
		    inet_ntoa(ip_hdr.ip_src));
		return;
	}

	/* header sanity checks */
	if (len < sizeof(*dvmrp_hdr)) {
		log_warnx("recv_packet: bad packet size");
		return;
	}
	dvmrp_hdr = (struct dvmrp_hdr *)buf;

	switch (dvmrp_hdr->type) {
	/* DVMRP */
	case PKT_TYPE_DVMRP:
		if ((l = dvmrp_hdr_sanity_check(&ip_hdr, dvmrp_hdr, len,
		    iface)) == -1)
			return;

		/*
		 * mrouted compat
		 *
		 * Old mrouted versions, send route reports before establishing
		 * 2-WAY neighbor relationships.
		 */
		if ((nbr_find_ip(iface, ip_hdr.ip_src.s_addr) == NULL) &&
		    (dvmrp_hdr->code == DVMRP_CODE_REPORT)) {
			log_debug("recv_packet: route report from neighbor"
			    " ID %s, compat", inet_ntoa(ip_hdr.ip_src));
			nbr = nbr_new(ip_hdr.ip_src.s_addr, iface, 0);
			nbr_fsm(nbr, NBR_EVT_PROBE_RCVD);
			nbr->compat = 1;
			nbr->addr = ip_hdr.ip_src;
		}

		if ((dvmrp_hdr->type == PKT_TYPE_DVMRP) &&
		    (dvmrp_hdr->code != DVMRP_CODE_PROBE))
			/* find neighbor */
			if ((nbr = nbr_find_ip(iface, ip_hdr.ip_src.s_addr))
			    == NULL) {
				log_debug("recv_packet: unknown neighbor ID");
				return;
			}

		buf += sizeof(*dvmrp_hdr);
		len = l - sizeof(*dvmrp_hdr);

		inet_pton(AF_INET, AllDVMRPRouters, &addr);
		if ((ip_hdr.ip_dst.s_addr != addr.s_addr) &&
		    (ip_hdr.ip_dst.s_addr != iface->addr.s_addr)) {
			log_debug("recv_packet: interface %s, invalid"
			    " destination IP address %s", iface->name,
			    inet_ntoa(ip_hdr.ip_dst));
			break;
		}

		switch (dvmrp_hdr->code) {
		case DVMRP_CODE_PROBE:
			recv_probe(iface, ip_hdr.ip_src, ip_hdr.ip_src.s_addr,
			    dvmrp_hdr->capabilities, buf, len);
			break;
		case DVMRP_CODE_REPORT:
			recv_report(nbr, buf, len);
			break;
		case DVMRP_CODE_ASK_NBRS2:
			recv_ask_nbrs2(nbr, buf,len);
			break;
		case DVMRP_CODE_NBRS2:
			recv_nbrs2(nbr, buf,len);
			break;
		case DVMRP_CODE_PRUNE:
			recv_prune(nbr, buf, len);
			break;
		case DVMRP_CODE_GRAFT:
			recv_graft(nbr, buf,len);
			break;
		case DVMRP_CODE_GRAFT_ACK:
			recv_graft_ack(nbr, buf,len);
			break;
		default:
			log_debug("recv_packet: unknown DVMRP packet type, "
			    "interface %s", iface->name);
		}
		break;
	/* IGMP */
	case PKT_TYPE_MEMBER_QUERY:
		recv_igmp_query(iface, ip_hdr.ip_src, buf, len);
		break;
	case PKT_TYPE_MEMBER_REPORTv1:
	case PKT_TYPE_MEMBER_REPORTv2:
		recv_igmp_report(iface, ip_hdr.ip_src, buf, len,
		    dvmrp_hdr->type);
		break;
	case PKT_TYPE_LEAVE_GROUPv2:
		recv_igmp_leave(iface, ip_hdr.ip_src, buf, len);
		break;
	default:
		log_debug("recv_packet: unknown IGMP packet type, interface %s",
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

	if (ip_hdr->ip_p != IPPROTO_IGMP)
		/* this is enforced by the socket itself */
		fatalx("recv_packet: invalid IP proto");

	return (ip_hdr->ip_hl << 2);
}

int
dvmrp_hdr_sanity_check(const struct ip *ip_hdr, struct dvmrp_hdr *dvmrp_hdr,
    u_int16_t len, const struct iface *iface)
{
	/* we only support DVMRPv3 */
	if (dvmrp_hdr->major_version != DVMRP_MAJOR_VERSION) {
		log_debug("recv_packet: invalid DVMRP version");
		return (-1);
	}

	/* XXX enforce minor version as well, but not yet */

	/* XXX chksum */

	return (len);
}

struct iface *
find_iface(struct dvmrpd_conf *xconf, struct in_addr src)
{
	struct iface	*iface = NULL;

	/* returned interface needs to be active */
	LIST_FOREACH(iface, &xconf->iface_list, entry) {
		if (iface->fd > 0 &&
		    (iface->type == IF_TYPE_POINTOPOINT) &&
		    (iface->dst.s_addr == src.s_addr) &&
		    !iface->passive)
			return (iface);

		if (iface->fd > 0 && (iface->addr.s_addr &
		    iface->mask.s_addr) == (src.s_addr &
		    iface->mask.s_addr) && !iface->passive)
			return (iface);
	}

	return (NULL);
}
