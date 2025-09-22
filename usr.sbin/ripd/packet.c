/*	$OpenBSD: packet.c,v 1.17 2021/01/19 16:02:22 claudio Exp $ */

/*
 * Copyright (c) 2006 Michele Marchetto <mydecay@openbeer.it>
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
#include <netinet/udp.h>
#include <arpa/inet.h>

#include <net/if_dl.h>

#include <errno.h>
#include <event.h>
#include <stdlib.h>
#include <string.h>

#include "ripd.h"
#include "rip.h"
#include "log.h"
#include "ripe.h"

int		 rip_hdr_sanity_check(struct rip_hdr *);
struct iface	*find_iface(struct ripd_conf *, unsigned int, struct in_addr);

static u_int8_t	*recv_buf;

int
gen_rip_hdr(struct ibuf *buf, u_int8_t command)
{
	struct rip_hdr	rip_hdr;

	bzero(&rip_hdr, sizeof(rip_hdr));
	rip_hdr.version = RIP_VERSION;
	rip_hdr.command = command;

	return (ibuf_add(buf, &rip_hdr, sizeof(rip_hdr)));
}

/* send and receive packets */
int
send_packet(struct iface *iface, void *pkt, size_t len, struct sockaddr_in *dst)
{
	/* set outgoing interface for multicast traffic */
	if (IN_MULTICAST(ntohl(dst->sin_addr.s_addr)))
		if (if_set_mcast(iface) == -1) {
			log_warn("send_packet: error setting multicast "
			    "interface, %s", iface->name);
			return (-1);
		}

	if (sendto(iface->fd, pkt, len, 0,
	    (struct sockaddr *)dst, sizeof(*dst)) == -1) {
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
		char	buf[CMSG_SPACE(sizeof(struct sockaddr_dl))];
	} cmsgbuf;
	struct sockaddr_in	 src;
	struct iovec		 iov;
	struct msghdr		 msg;
	struct cmsghdr		*cmsg;
	struct sockaddr_dl	*dst = NULL;
	struct nbr_failed	*nbr_failed = NULL;
	struct ripd_conf	*xconf = bula;
	struct iface		*iface;
	struct rip_hdr		*rip_hdr;
	struct nbr		*nbr;
	u_int8_t		*buf;
	ssize_t			 r;
	u_int16_t		 len, srcport;
	u_int32_t		 auth_crypt_num = 0;

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
		if (errno != EINTR && errno != EAGAIN)
			log_debug("recv_packet: read error: %s",
			    strerror(errno));
		return;
	}

	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
	    cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level == IPPROTO_IP &&
		    cmsg->cmsg_type == IP_RECVIF) {
			dst = (struct sockaddr_dl *)CMSG_DATA(cmsg);
			break;
		}
	}

	if (dst == NULL)
		return;

	len = (u_int16_t)r;

	/* Check the packet is not from one of the local interfaces */
	LIST_FOREACH(iface, &xconf->iface_list, entry) {
		if (iface->addr.s_addr == src.sin_addr.s_addr)
			return;
	}

	/* find a matching interface */
	if ((iface = find_iface(xconf, dst->sdl_index, src.sin_addr)) == NULL) {
		log_debug("recv_packet: cannot find a matching interface");
		return;
	}

	srcport = ntohs(src.sin_port);

	/* RIP header sanity checks */
	if (len < RIP_HDR_LEN) {
		log_debug("recv_packet: bad packet size");
		return;
	}
	rip_hdr = (struct rip_hdr *)buf;

	if (rip_hdr_sanity_check(rip_hdr) == -1)
		return;

	nbr = nbr_find_ip(iface, src.sin_addr.s_addr);

	if (nbr == NULL && iface->auth_type == AUTH_CRYPT)
		nbr_failed = nbr_failed_find(iface, src.sin_addr.s_addr);

	/* switch RIP command */
	switch (rip_hdr->command) {
	case COMMAND_REQUEST:
		/* Requests don't create a real neighbor, just a temporary
		 * one to build the response.
		 */
		if ((msg.msg_flags & MSG_MCAST) == 0 && srcport == RIP_PORT)
			return;

		if (nbr == NULL) {
			nbr = nbr_new(src.sin_addr.s_addr, iface);
			nbr->addr = src.sin_addr;
		}
		nbr->port = srcport;
		nbr_fsm(nbr, NBR_EVT_REQUEST_RCVD);

		buf += RIP_HDR_LEN;
		len -= RIP_HDR_LEN;

		recv_request(iface, nbr, buf, len);
		break;
	case COMMAND_RESPONSE:
		if (srcport != RIP_PORT)
			return;

		if (auth_validate(&buf, &len, iface, nbr, nbr_failed,
		    &auth_crypt_num)) {
			log_warnx("recv_packet: authentication error, "
			    "interface %s", iface->name);
			return;
		}

		if (nbr == NULL) {
			nbr = nbr_new(src.sin_addr.s_addr, iface);
			if (nbr_failed != NULL)
				nbr_failed_delete(nbr_failed);
			nbr->addr = src.sin_addr;
		}
		nbr->auth_seq_num = auth_crypt_num;
		nbr_fsm(nbr, NBR_EVT_RESPONSE_RCVD);

		recv_response(iface, nbr, buf, len);
		break;
	default:
		log_debug("recv_packet: unknown RIP command, interface %s",
		    iface->name);
	}
}

int
rip_hdr_sanity_check(struct rip_hdr *rip_hdr)
{
	if (rip_hdr->version != RIP_VERSION) {
		log_debug("rip_hdr_sanity_check: invalid RIP version %d",
		    rip_hdr->version);
		return (-1);
	}

	return (0);
}

struct iface *
find_iface(struct ripd_conf *xconf, unsigned int ifindex, struct in_addr src)
{
	struct iface	*iface = NULL;

	/* returned interface needs to be active */
	LIST_FOREACH(iface, &xconf->iface_list, entry) {
		if (ifindex == 0 || ifindex != iface->ifindex)
			continue;

		if (iface->passive)
			continue;

		if ((iface->addr.s_addr & iface->mask.s_addr) ==
		    (src.s_addr & iface->mask.s_addr))
			return (iface);

		if (iface->dst.s_addr && iface->dst.s_addr == src.s_addr)
			return (iface);
	}

	return (NULL);
}
