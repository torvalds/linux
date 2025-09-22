/*	$OpenBSD: report.c,v 1.13 2024/08/21 09:18:47 florian Exp $ */

/*
 * Copyright (c) 2005, 2006 Esben Norby <norby@openbsd.org>
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
#include <netinet/ip.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <string.h>

#include "igmp.h"
#include "dvmrpd.h"
#include "dvmrp.h"
#include "dvmrpe.h"
#include "log.h"

extern struct dvmrpd_conf	*deconf;

void	 rr_list_remove(struct route_report *);

/* DVMRP report packet handling */
int
send_report(struct iface *iface, struct in_addr addr, void *data, int len)
{
	struct sockaddr_in	 dst;
	struct ibuf		*buf;
	int			 ret = 0;

	log_debug("send_report: interface %s addr %s",
	    iface->name, inet_ntoa(addr));

	if (iface->passive)
		return (0);

	if ((buf = ibuf_open(iface->mtu - sizeof(struct ip))) == NULL)
		fatal("send_report");

	/* DVMRP header */
	if (gen_dvmrp_hdr(buf, iface, DVMRP_CODE_REPORT))
		goto fail;

	ibuf_add(buf, data, len);

	dst.sin_family = AF_INET;
	dst.sin_len = sizeof(struct sockaddr_in);
	dst.sin_addr.s_addr = addr.s_addr;

	ret = send_packet(iface, buf, &dst);
	ibuf_free(buf);
	return (ret);
fail:
	log_warn("send_report");
	ibuf_free(buf);
	return (-1);
}

void
recv_report(struct nbr *nbr, char *buf, u_int16_t len)
{
	struct route_report	 rr;
	u_int32_t		 netid, netmask;
	u_int8_t		 metric, netid_len, prefixlen;

	log_debug("recv_report: neighbor ID %s", inet_ntoa(nbr->id));

	if ((nbr->state != NBR_STA_2_WAY) && (!nbr->compat)) {
		log_warnx("recv_report: neighbor %s not in state %s",
		    inet_ntoa(nbr->id), "2-WAY");
		return;
	}

	/* parse route report */
	do {
		/*
		 * get netmask
		 *
		 * The netmask in a DVMRP report is only represented by 3 bytes,
		 * to cope with that we read 4 bytes and shift 8 bits.
		 * The most significant part of the mask is always 255.
		 */

		/* read four bytes */
		memcpy(&netmask, buf, sizeof(netmask));
		/* ditch one byte, since we only need three */
		netmask = ntohl(netmask) >> 8;
		netmask = htonl(netmask);

		/* set the highest byte to 255 */
		netmask |= htonl(0xff000000);
		buf += 3;
		len -= 3;

		prefixlen = mask2prefixlen(netmask);
		netid_len = PREFIX_SIZE(prefixlen);

		do {
			/*
			 * get netid
			 *
			 * The length of the netid is depending on the above
			 * netmask.
			 * Read 4 bytes and use the netmask from above to
			 * determine the netid.
			 */
			memcpy(&netid, buf, sizeof(netid));
			netid &= netmask;

			buf += netid_len;
			len -= netid_len;

			/* get metric */
			memcpy(&metric, buf, sizeof(metric));
			buf += sizeof(metric);
			len -= sizeof(metric);

			rr.net.s_addr = netid;
			rr.mask.s_addr = netmask;
			rr.nexthop = nbr->id;
			rr.metric = (metric & METRIC_MASK);

			/* ifindex */
			rr.ifindex = nbr->iface->ifindex;

			/* send route report to RDE */
			dvmrpe_imsg_compose_rde(IMSG_ROUTE_REPORT, nbr->peerid,
			    0, &rr, sizeof(rr));

		} while (!(metric & LAST_MASK) && (len > 0));
	} while (len > 0);

	return;
}

/* timers */
void
report_timer(int fd, short event, void *arg)
{
	struct timeval		 tv;

	/* request full route report */
	dvmrpe_imsg_compose_rde(IMSG_FULL_ROUTE_REPORT, 0, 0, NULL, 0);

	/* restart report timer */
	timerclear(&tv);
	tv.tv_sec = ROUTE_REPORT_INTERVAL;
	evtimer_add(&deconf->report_timer, &tv);
}

int
start_report_timer(void)
{
	struct timeval	tv;

	timerclear(&tv);
	tv.tv_sec = MIN_FLASH_UPDATE_INTERVAL;	/* XXX safe?? */
	return (evtimer_add(&deconf->report_timer, &tv));
}

int
stop_report_timer(void)
{
	return (evtimer_del(&deconf->report_timer));
}

/* route report list */
void
rr_list_add(struct rr_head *rr_list, struct route_report *rr)
{
	struct rr_entry		*le;

	if (rr == NULL)
		fatalx("rr_list_add: no route report");

	if ((le = calloc(1, sizeof(*le))) == NULL)
		fatal("rr_list_add");

	TAILQ_INSERT_TAIL(rr_list, le, entry);
	le->re = rr;
	rr->refcount++;
}

void
rr_list_remove(struct route_report *rr)
{
	if (--rr->refcount == 0)
		free(rr);
}

void
rr_list_clr(struct rr_head *rr_list)
{
	struct rr_entry		*le;

	while ((le = TAILQ_FIRST(rr_list)) != NULL) {
		TAILQ_REMOVE(rr_list, le, entry);
		rr_list_remove(le->re);
		free(le);
	}
}

void
rr_list_send(struct rr_head *rr_list, struct iface *xiface, struct nbr *nbr)
{
	struct rr_entry		*le, *le2;
	struct ibuf		*buf;
	struct iface		*iface;
	struct in_addr		 addr;
	u_int32_t		 netid, netmask;
	u_int8_t		 metric, netid_len, prefixlen;

	/* set destination */
	if (xiface == NULL) {
		/* directly to a nbr */
		iface = nbr->iface;
		addr = nbr->addr;
	} else {
		/* multicast on interface */
		iface = xiface;
		inet_pton(AF_INET, AllDVMRPRouters, &addr);
	}

	while (!TAILQ_EMPTY(rr_list)) {
		if ((buf = ibuf_open(iface->mtu - sizeof(struct ip))) == NULL)
			fatal("rr_list_send");

		prefixlen = 0;
		while (((le = TAILQ_FIRST(rr_list)) != NULL) &&
		    (ibuf_size(buf) < 1000)) {
			/* netmask */
			netmask = le->re->mask.s_addr;
			if (prefixlen != mask2prefixlen(netmask)) {
				prefixlen = mask2prefixlen(netmask);
				netmask = ntohl(netmask) << 8;
				netmask = htonl(netmask);
				ibuf_add(buf, &netmask, 3);
			}
			netid_len = PREFIX_SIZE(prefixlen);

			/* netid */
			netid = le->re->net.s_addr;
			ibuf_add(buf, &netid, netid_len);

			/* metric */
			if (iface->ifindex == le->re->ifindex)
				/* poison reverse */
				metric = le->re->metric + INFINITY_METRIC;
			else
				metric = le->re->metric;

			/*
			 * determine if we need to flag last entry with current
			 * netmask.
			 */
			le2 = TAILQ_NEXT(le, entry);
			if (le2 != NULL) {
				if (mask2prefixlen(le2->re->mask.s_addr) !=
				    prefixlen)
					metric = metric | LAST_MASK;
			} else {
				metric = metric | LAST_MASK;
			}

			ibuf_add(buf, &metric, sizeof(metric));

			TAILQ_REMOVE(rr_list, le, entry);
			rr_list_remove(le->re);
			free(le);
		}
		send_report(iface, addr, ibuf_data(buf), ibuf_size(buf));
		ibuf_free(buf);
	}
}
