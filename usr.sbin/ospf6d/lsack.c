/*	$OpenBSD: lsack.c,v 1.10 2023/03/08 04:43:14 guenther Exp $ */

/*
 * Copyright (c) 2004, 2005, 2007 Esben Norby <norby@openbsd.org>
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
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <string.h>

#include "ospf6d.h"
#include "ospf6.h"
#include "log.h"
#include "ospfe.h"

int		 send_ls_ack(struct iface *, struct in6_addr, struct ibuf *);
struct ibuf	*prepare_ls_ack(struct iface *);
void		 start_ls_ack_tx_timer_now(struct iface *);

/* link state acknowledgement packet handling */
struct ibuf *
prepare_ls_ack(struct iface *iface)
{
	struct ibuf	*buf;

	if ((buf = ibuf_open(iface->mtu - sizeof(struct ip6_hdr))) == NULL) {
		log_warn("prepare_ls_ack");
		return (NULL);
	}

	/* OSPF header */
	if (gen_ospf_hdr(buf, iface, PACKET_TYPE_LS_ACK)) {
		log_warn("prepare_ls_ack");
		ibuf_free(buf);
		return (NULL);
	}

	return (buf);
}

int
send_ls_ack(struct iface *iface, struct in6_addr addr, struct ibuf *buf)
{
	/* calculate checksum */
	if (upd_ospf_hdr(buf, iface)) {
		log_warn("send_ls_ack");
		return (-1);
	}

	if (send_packet(iface, buf, &addr) == -1) {
		log_warn("send_ls_ack");
		return (-1);
	}
	return (0);
}

int
send_direct_ack(struct iface *iface, struct in6_addr addr, void *d, size_t len)
{
	struct ibuf	*buf;
	int		 ret;

	if ((buf = prepare_ls_ack(iface)) == NULL)
		return (-1);

	/* LS ack(s) */
	if (ibuf_add(buf, d, len)) {
		log_warn("send_direct_ack");
		ibuf_free(buf);
		return (-1);
	}

	ret = send_ls_ack(iface, addr, buf);
	ibuf_free(buf);
	return (ret);
}

void
recv_ls_ack(struct nbr *nbr, char *buf, u_int16_t len)
{
	struct lsa_hdr	 lsa_hdr;

	switch (nbr->state) {
	case NBR_STA_DOWN:
	case NBR_STA_ATTEMPT:
	case NBR_STA_INIT:
	case NBR_STA_2_WAY:
	case NBR_STA_XSTRT:
	case NBR_STA_SNAP:
		log_debug("recv_ls_ack: packet ignored in state %s, "
		    "neighbor ID %s (%s)", nbr_state_name(nbr->state),
		    inet_ntoa(nbr->id), nbr->iface->name);
		break;
	case NBR_STA_XCHNG:
	case NBR_STA_LOAD:
	case NBR_STA_FULL:
		while (len >= sizeof(lsa_hdr)) {
			memcpy(&lsa_hdr, buf, sizeof(lsa_hdr));

			if (lsa_hdr_check(nbr, &lsa_hdr)) {
				/* try both list in case of DROTHER */
				if (nbr->iface->state & IF_STA_DROTHER)
					(void)ls_retrans_list_del(
					    nbr->iface->self, &lsa_hdr);
				(void)ls_retrans_list_del(nbr, &lsa_hdr);
			}

			buf += sizeof(lsa_hdr);
			len -= sizeof(lsa_hdr);
		}
		if (len > 0) {
			log_warnx("recv_ls_ack: bad packet size, "
			    "neighbor ID %s (%s)", inet_ntoa(nbr->id),
			    nbr->iface->name);
			return;
		}
		break;
	default:
		fatalx("recv_ls_ack: unknown neighbor state");
	}
}

int
lsa_hdr_check(struct nbr *nbr, struct lsa_hdr *lsa_hdr)
{
	/* invalid age */
	if ((ntohs(lsa_hdr->age) < 1) || (ntohs(lsa_hdr->age) > MAX_AGE)) {
		log_debug("lsa_hdr_check: invalid age, neighbor ID %s (%s)",
		     inet_ntoa(nbr->id), nbr->iface->name);
		return (0);
	}

	/* invalid type */
	switch (ntohs(lsa_hdr->type)) {
	case LSA_TYPE_LINK:
	case LSA_TYPE_ROUTER:
	case LSA_TYPE_NETWORK:
	case LSA_TYPE_INTER_A_PREFIX:
	case LSA_TYPE_INTER_A_ROUTER:
	case LSA_TYPE_INTRA_A_PREFIX:
	case LSA_TYPE_EXTERNAL:
		break;
	default:
		log_debug("lsa_hdr_check: invalid LSA type %d, "
		    "neighbor ID %s (%s)",
		    lsa_hdr->type, inet_ntoa(nbr->id), nbr->iface->name);
		return (0);
	}

	/* invalid sequence number */
	if (ntohl(lsa_hdr->seq_num) == RESV_SEQ_NUM) {
		log_debug("ls_hdr_check: invalid seq num, "
		    "neighbor ID %s (%s)", inet_ntoa(nbr->id),
		    nbr->iface->name);
		return (0);
	}

	return (1);
}

/* link state ack list */
void
ls_ack_list_add(struct iface *iface, struct lsa_hdr *lsa)
{
	struct lsa_entry	*le;

	if (lsa == NULL)
		fatalx("ls_ack_list_add: no LSA header");

	if ((le = calloc(1, sizeof(*le))) == NULL)
		fatal("ls_ack_list_add");

	if (ls_ack_list_empty(iface))
		start_ls_ack_tx_timer(iface);

	TAILQ_INSERT_TAIL(&iface->ls_ack_list, le, entry);
	le->le_lsa = lsa;
	iface->ls_ack_cnt++;

	/* reschedule now if we have enough for a full packet */
	if (iface->ls_ack_cnt >
	    ((iface->mtu - PACKET_HDR) / sizeof(struct lsa_hdr))) {
		start_ls_ack_tx_timer_now(iface);
	}
}

void
ls_ack_list_free(struct iface *iface, struct lsa_entry *le)
{
	TAILQ_REMOVE(&iface->ls_ack_list, le, entry);
	free(le->le_lsa);
	free(le);

	iface->ls_ack_cnt--;
}

void
ls_ack_list_clr(struct iface *iface)
{
	struct lsa_entry	*le;

	while ((le = TAILQ_FIRST(&iface->ls_ack_list)) != NULL) {
		TAILQ_REMOVE(&iface->ls_ack_list, le, entry);
		free(le->le_lsa);
		free(le);
	}
	iface->ls_ack_cnt = 0;
}

int
ls_ack_list_empty(struct iface *iface)
{
	return (TAILQ_EMPTY(&iface->ls_ack_list));
}

/* timers */
void
ls_ack_tx_timer(int fd, short event, void *arg)
{
	struct in6_addr		 addr;
	struct iface		*iface = arg;
	struct lsa_entry	*le, *nle;
	struct nbr		*nbr;
	struct ibuf		*buf;
	int			 cnt;

	while (!ls_ack_list_empty(iface)) {
		if ((buf = prepare_ls_ack(iface)) == NULL)
			fatal("ls_ack_tx_timer");
		cnt = 0;

		for (le = TAILQ_FIRST(&iface->ls_ack_list); le != NULL;
		    le = nle) {
			nle = TAILQ_NEXT(le, entry);
			if (ibuf_left(buf) < sizeof(struct lsa_hdr))
				break;
			if (ibuf_add(buf, le->le_lsa, sizeof(struct lsa_hdr)))
				break;
			ls_ack_list_free(iface, le);
			cnt++;
		}
		if (cnt == 0) {
			log_warnx("ls_ack_tx_timer: lost in space");
			ibuf_free(buf);
			return;
		}

		/* send LS ack(s) but first set correct destination */
		switch (iface->type) {
		case IF_TYPE_POINTOPOINT:
			inet_pton(AF_INET6, AllSPFRouters, &addr);
			send_ls_ack(iface, addr, buf);
			break;
		case IF_TYPE_BROADCAST:
			if (iface->state & IF_STA_DRORBDR)
				inet_pton(AF_INET6, AllSPFRouters, &addr);
			else
				inet_pton(AF_INET6, AllDRouters, &addr);
			send_ls_ack(iface, addr, buf);
			break;
		case IF_TYPE_NBMA:
		case IF_TYPE_POINTOMULTIPOINT:
		case IF_TYPE_VIRTUALLINK:
			LIST_FOREACH(nbr, &iface->nbr_list, entry) {
				if (nbr == iface->self)
					continue;
				if (!(nbr->state & NBR_STA_FLOOD))
					continue;
				send_ls_ack(iface, nbr->addr, buf);
			}
			break;
		default:
			fatalx("lsa_ack_tx_timer: unknown interface type");
		}
		ibuf_free(buf);
	}
}

void
start_ls_ack_tx_timer(struct iface *iface)
{
	struct timeval tv;

	timerclear(&tv);
	tv.tv_sec = iface->rxmt_interval / 2;

	if (evtimer_add(&iface->lsack_tx_timer, &tv) == -1)
		fatal("start_ls_ack_tx_timer");
}

void
start_ls_ack_tx_timer_now(struct iface *iface)
{
	struct timeval tv;

	timerclear(&tv);
	if (evtimer_add(&iface->lsack_tx_timer, &tv) == -1)
		fatal("start_ls_ack_tx_timer_now");
}

void
stop_ls_ack_tx_timer(struct iface *iface)
{
	if (evtimer_del(&iface->lsack_tx_timer) == -1)
		fatal("stop_ls_ack_tx_timer");
}
