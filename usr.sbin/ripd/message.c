/*	$OpenBSD: message.c,v 1.18 2024/08/21 14:58:14 florian Exp $ */

/*
 * Copyright (c) 2006 Michele Marchetto <mydecay@openbeer.it>
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
#include <netinet/udp.h>

#include <stdlib.h>
#include <string.h>

#include "ripd.h"
#include "rip.h"
#include "ripe.h"
#include "log.h"

extern struct ripd_conf	*oeconf;

void	 delete_entry(struct rip_route *);

/* timers */
void
report_timer(int fd, short event, void *arg)
{
	struct timeval	 tv;

	ripe_imsg_compose_rde(IMSG_FULL_RESPONSE, 0, 0, NULL, 0);

	/* restart report timer */
	timerclear(&tv);
	tv.tv_sec = KEEPALIVE + arc4random_uniform(OFFSET);
	evtimer_add(&oeconf->report_timer, &tv);
}

int
start_report_timer(void)
{
	struct timeval	 tv;

	timerclear(&tv);
	tv.tv_sec = KEEPALIVE + arc4random_uniform(OFFSET);
	return (evtimer_add(&oeconf->report_timer, &tv));
}

/* list handlers */
void
add_entry(struct packet_head *r_list, struct rip_route *rr)
{
	struct packet_entry	*re;

	if (rr == NULL)
		fatalx("add_entry: no route report");

	if ((re = calloc(1, sizeof(*re))) == NULL)
		fatal("add_entry");

	TAILQ_INSERT_TAIL(r_list, re, entry);
	re->rr = rr;
	rr->refcount++;
}

void
delete_entry(struct rip_route *rr)
{
	if (--rr->refcount == 0)
		free(rr);
}

void
clear_list(struct packet_head *r_list)
{
	struct packet_entry	*re;

	while ((re = TAILQ_FIRST(r_list)) != NULL) {
		TAILQ_REMOVE(r_list, re, entry);
		delete_entry(re->rr);
		free(re);
	}
}

/* communications */
int
send_triggered_update(struct iface *iface, struct rip_route *rr)
{
	struct sockaddr_in	 dst;
	struct ibuf		*buf;
	u_int16_t		 afi, route_tag;
	u_int32_t		 address, netmask, nexthop, metric;

	if (iface->passive)
		return (0);

	inet_pton(AF_INET, ALL_RIP_ROUTERS, &dst.sin_addr);

	dst.sin_port = htons(RIP_PORT);
	dst.sin_family = AF_INET;
	dst.sin_len = sizeof(struct sockaddr_in);

	if ((buf = ibuf_open(iface->mtu - sizeof(struct ip) -
	    sizeof(struct udphdr))) == NULL)
		fatal("send_triggered_update");

	gen_rip_hdr(buf, COMMAND_RESPONSE);

	afi = htons(AF_INET);
	route_tag = 0;

	address = rr->address.s_addr;
	netmask = rr->mask.s_addr;
	nexthop = rr->nexthop.s_addr;
	metric = htonl(rr->metric);

	ibuf_add(buf, &afi, sizeof(afi));
	ibuf_add(buf, &route_tag, sizeof(route_tag));
	ibuf_add(buf, &address, sizeof(address));
	ibuf_add(buf, &netmask, sizeof(netmask));
	ibuf_add(buf, &nexthop, sizeof(nexthop));
	ibuf_add(buf, &metric, sizeof(metric));

	send_packet(iface, ibuf_data(buf), ibuf_size(buf), &dst);
	ibuf_free(buf);

	return (0);
}

int
send_request(struct packet_head *r_list, struct iface *i, struct nbr *nbr)
{
	struct ibuf		*buf;
	struct iface		*iface;
	struct packet_entry	*entry;
	struct sockaddr_in	 dst;
	u_int8_t		 nentries;
	u_int8_t		 single_entry = 0;
	u_int32_t		 address, netmask, nexthop;
	u_int16_t		 port, afi, route_tag;
	u_int32_t		 metric;

	if (i == NULL) {
		/* directly to a nbr */
		iface = nbr->iface;
		dst.sin_addr = nbr->addr;
		port = htons(nbr->port);
	} else {
		/* multicast on interface */
		iface = i;
		inet_pton(AF_INET, ALL_RIP_ROUTERS, &dst.sin_addr);
		port = htons(RIP_PORT);
	}

	if (iface->passive) {
		clear_list(r_list);
		return (0);
	}

	dst.sin_port = port;
	dst.sin_family = AF_INET;
	dst.sin_len = sizeof(struct sockaddr_in);

	while (!TAILQ_EMPTY(r_list)) {
		if ((buf = ibuf_open(iface->mtu - sizeof(struct ip) -
		    sizeof(struct udphdr))) == NULL)
			fatal("send_request");

		gen_rip_hdr(buf, COMMAND_REQUEST);

		route_tag = 0;
		nentries = 0;

		if (TAILQ_FIRST(r_list) == TAILQ_LAST(r_list, packet_head))
			single_entry = 1;
		while (((entry = TAILQ_FIRST(r_list)) != NULL) &&
		    nentries < MAX_RIP_ENTRIES) {
			afi = htons(AF_INET);

			address = entry->rr->address.s_addr;
			netmask = entry->rr->mask.s_addr;
			nexthop = entry->rr->nexthop.s_addr;
			metric = htonl(entry->rr->metric);

			if (metric == htonl(INFINITY) && single_entry)
				afi = AF_UNSPEC;

			ibuf_add(buf, &afi, sizeof(afi));
			ibuf_add(buf, &route_tag, sizeof(route_tag));
			ibuf_add(buf, &address, sizeof(address));
			ibuf_add(buf, &netmask, sizeof(netmask));
			ibuf_add(buf, &nexthop, sizeof(nexthop));
			ibuf_add(buf, &metric, sizeof(metric));
			nentries++;

			TAILQ_REMOVE(r_list, entry, entry);
			delete_entry(entry->rr);
			free(entry);
		}
		send_packet(iface, ibuf_data(buf), ibuf_size(buf), &dst);
		ibuf_free(buf);
	}

	return (0);
}

int
send_response(struct packet_head *r_list, struct iface *i, struct nbr *nbr)
{
	struct ibuf		*buf;
	struct iface		*iface;
	struct packet_entry	*entry;
	struct sockaddr_in	 dst;
	u_int8_t		 nentries;
	u_int16_t		 port, afi, route_tag;
	u_int32_t		 address, netmask, nexthop;
	u_int32_t		 metric;

	if (i == NULL) {
		/* directly to a nbr */
		iface = nbr->iface;
		dst.sin_addr = nbr->addr;
		port = htons(nbr->port);
	} else {
		/* multicast on interface */
		iface = i;
		inet_pton(AF_INET, ALL_RIP_ROUTERS, &dst.sin_addr);
		port = htons(RIP_PORT);
	}

	if (iface->passive) {
		clear_list(r_list);
		return (0);
	}

	dst.sin_port = port;
	dst.sin_family = AF_INET;
	dst.sin_len = sizeof(struct sockaddr_in);

	while (!TAILQ_EMPTY(r_list)) {
		if ((buf = ibuf_open(iface->mtu - sizeof(struct ip) -
		    sizeof(struct udphdr))) == NULL)
			fatal("send_response");

		gen_rip_hdr(buf, COMMAND_RESPONSE);

		afi = htons(AF_INET);
		route_tag = 0;
		nentries = 0;

		if (iface->auth_type != AUTH_NONE) {
			if (auth_gen(buf, iface) == -1) {
				ibuf_free(buf);
				return (-1);
			}
			nentries++;
		}

		while ((entry = TAILQ_FIRST(r_list)) != NULL &&
		    nentries < MAX_RIP_ENTRIES) {
			address = entry->rr->address.s_addr;
			netmask = entry->rr->mask.s_addr;
			nexthop = entry->rr->nexthop.s_addr;
			metric = htonl(entry->rr->metric);

			if (entry->rr->ifindex == iface->ifindex) {
				if (oeconf->options & OPT_SPLIT_HORIZON)
					goto free;
				else if (oeconf->options & OPT_SPLIT_POISONED)
					metric = htonl(INFINITY);
			}

			/* If the nexthop is not reachable through the
			 * outgoing interface set it to INADDR_ANY */
			if ((nexthop & iface->mask.s_addr) !=
			    (iface->addr.s_addr & iface->mask.s_addr))
				nexthop = INADDR_ANY;

			ibuf_add(buf, &afi, sizeof(afi));
			ibuf_add(buf, &route_tag, sizeof(route_tag));
			ibuf_add(buf, &address, sizeof(address));
			ibuf_add(buf, &netmask, sizeof(netmask));
			ibuf_add(buf, &nexthop, sizeof(nexthop));
			ibuf_add(buf, &metric, sizeof(metric));
			nentries++;
free:
			TAILQ_REMOVE(r_list, entry, entry);
			delete_entry(entry->rr);
			free(entry);
		}

		if (iface->auth_type == AUTH_CRYPT)
			auth_add_trailer(buf, iface);

		send_packet(iface, ibuf_data(buf), ibuf_size(buf), &dst);
		ibuf_free(buf);
	}

	return (0);
}

void
recv_request(struct iface *i, struct nbr *nbr, u_int8_t *buf, u_int16_t len)
{
	struct rip_entry	*e;
	struct rip_route	 rr;
	int			 l = len;

	bzero(&rr, sizeof(rr));

	if (len < RIP_ENTRY_LEN) {
		log_debug("recv_request: bad packet size, interface %s",
		    i->name);
		return;
	}

	/*
	 * XXX is it guaranteed that bus is properly aligned.
	 * If not this will bomb on strict alignment archs.
	 * */
	e = (struct rip_entry *)buf;

	if (len > RIP_ENTRY_LEN * MAX_RIP_ENTRIES) {
		log_debug("recv_request: packet too long\n");
		return;
	}

	l -= RIP_ENTRY_LEN;

	/*
	 * If there is exactly one entry in the request, and it has
	 * an address family identifier of zero and a metric of
	 * infinity (i.e., 16), then this is a request to send the
	 * entire routing table.
	 */
	if (e->AFI == 0 && e->metric == ntohl(INFINITY) && l == 0) {
		ripe_imsg_compose_rde(IMSG_FULL_RESPONSE, nbr->peerid,
		    0, NULL, 0);
		return;
	}

	for ( ; l >= 0; l -= RIP_ENTRY_LEN) {
		if (e->AFI != AF_INET) {
			log_debug("recv_request: AFI %d not supported\n",
			    e->AFI);
			return;
		}
		rr.address.s_addr = e->address;
		rr.mask.s_addr = e->mask;
		rr.nexthop.s_addr = e->nexthop;
		rr.metric = e->metric;
		rr.ifindex = i->ifindex;

		ripe_imsg_compose_rde(IMSG_ROUTE_REQUEST, nbr->peerid,
		    0, &rr, sizeof(rr));

		e++;
	}

	ripe_imsg_compose_rde(IMSG_ROUTE_REQUEST_END, nbr->peerid,
	    0, NULL, 0);
}

void
recv_response(struct iface *i, struct nbr *nbr, u_int8_t *buf, u_int16_t len)
{
	struct rip_route	 r;
	struct rip_entry	*e;
	int			 l;

	if (len < RIP_ENTRY_LEN) {
		log_debug("recv_response: bad packet size, interface %s",
		    i->name);
		return;
	}

	/* We must double check the length, because the only entry
	 * can be stripped off by authentication code
	 */
	if (len < RIP_ENTRY_LEN) {
		/* If there are no entries, our work is finished here */
		return;
	}

	/* XXX again */
	e = (struct rip_entry *)buf;

	if (len > RIP_ENTRY_LEN * MAX_RIP_ENTRIES) {
		log_debug("recv_response: packet too long\n");
		return;
	}

	l = len - sizeof(*e);

	for ( ; l >= 0; l -= RIP_ENTRY_LEN) {
		if (ntohs(e->AFI) != AF_INET) {
			log_debug("recv_response: AFI %d not supported\n",
			    e->AFI);
			return;
		}

		r.address.s_addr = e->address;
		r.mask.s_addr = e->mask;

		if (e->nexthop == INADDR_ANY ||
		    ((i->addr.s_addr & i->mask.s_addr) !=
		    (e->nexthop & i->mask.s_addr)))
			r.nexthop.s_addr = nbr->addr.s_addr;
		else
			r.nexthop.s_addr = e->nexthop;

		r.metric = ntohl(e->metric);
		r.ifindex = i->ifindex;

		ripe_imsg_compose_rde(IMSG_ROUTE_FEED, 0, 0, &r, sizeof(r));

		e++;
	}
}
