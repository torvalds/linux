/*	$OpenBSD: igmp.c,v 1.6 2024/08/21 09:18:47 florian Exp $ */

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
#include <arpa/inet.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <event.h>

#include "igmp.h"
#include "dvmrpd.h"
#include "dvmrp.h"
#include "log.h"
#include "dvmrpe.h"

int	 igmp_chksum(struct igmp_hdr *);

/* IGMP packet handling */
int
send_igmp_query(struct iface *iface, struct group *group)
{
	struct igmp_hdr		 igmp_hdr;
	struct sockaddr_in	 dst;
	struct ibuf		*buf;
	int			 ret = 0;

	log_debug("send_igmp_query: interface %s", iface->name);

	if (iface->passive)
		return (0);

	if ((buf = ibuf_open(iface->mtu - sizeof(struct ip))) == NULL)
		fatal("send_igmp_query");

	/* IGMP header */
	memset(&igmp_hdr, 0, sizeof(igmp_hdr));
	igmp_hdr.type = PKT_TYPE_MEMBER_QUERY;

	if (group == NULL) {
		/* general query - version is configured */
		igmp_hdr.grp_addr = 0;

		switch (iface->igmp_version) {
		case 1:
			break;
		case 2:
			igmp_hdr.max_resp_time = iface->query_resp_interval;
			break;
		default:
			fatal("send_igmp_query: invalid igmp version");
		}
	} else {
		/* group specific query - only version 2 */
		igmp_hdr.grp_addr = group->addr.s_addr;
		igmp_hdr.max_resp_time = iface->last_member_query_interval;
	}

	ibuf_add(buf, &igmp_hdr, sizeof(igmp_hdr));

	/* set destination address */
	dst.sin_family = AF_INET;
	dst.sin_len = sizeof(struct sockaddr_in);
	inet_pton(AF_INET, AllSystems, &dst.sin_addr);

	ret = send_packet(iface, buf, &dst);
	ibuf_free(buf);
	return (ret);
}

void
recv_igmp_query(struct iface *iface, struct in_addr src, char *buf,
    u_int16_t len)
{
	struct igmp_hdr	 igmp_hdr;
	struct group	*group;

	log_debug("recv_igmp_query: interface %s", iface->name);

	if (len < sizeof(igmp_hdr)) {
		log_debug("recv_igmp_query: invalid IGMP report, interface %s",
		    iface->name);
		return;
	}

	memcpy(&igmp_hdr, buf, sizeof(igmp_hdr));
	iface->recv_query_resp_interval = igmp_hdr.max_resp_time;

	/* verify chksum */
	if (igmp_chksum(&igmp_hdr) == -1) {
		log_debug("recv_igmp_query: invalid chksum, interface %s",
		    iface->name);
		return;
	}

	if (src.s_addr < iface->addr.s_addr && igmp_hdr.grp_addr == 0) {
		/* we received a general query and we lost the election */
		if_fsm(iface, IF_EVT_QRECVD);
		/* remember who is querier */
		iface->querier = src;
		return;
	}

	if (iface->state == IF_STA_NONQUERIER && igmp_hdr.grp_addr != 0) {
		/* validate group id */
		if (!IN_MULTICAST(ntohl(igmp_hdr.grp_addr))) {
			log_debug("recv_igmp_query: invalid group, "
			    "interface %s", iface->name);
			return;
		}

		if ((group = group_list_add(iface, igmp_hdr.grp_addr))
		    != NULL)
			group_fsm(group, GRP_EVT_QUERY_RCVD);
	}
}

void
recv_igmp_report(struct iface *iface, struct in_addr src, char *buf,
    u_int16_t len, u_int8_t type)
{
	struct igmp_hdr	 igmp_hdr;
	struct group	*group;

	log_debug("recv_igmp_report: interface %s", iface->name);

	if (len < sizeof(igmp_hdr)) {
		log_debug("recv_igmp_report: invalid IGMP report, interface %s",
		    iface->name);
		return;
	}

	memcpy(&igmp_hdr, buf, sizeof(igmp_hdr));

	/* verify chksum */
	if (igmp_chksum(&igmp_hdr) == -1) {
		log_debug("recv_igmp_report: invalid chksum, interface %s",
		    iface->name);
		return;
	}

	/* validate group id */
	if (!IN_MULTICAST(ntohl(igmp_hdr.grp_addr))) {
		log_debug("recv_igmp_report: invalid group, interface %s",
		    iface->name);
		return;
	}

	if ((group = group_list_add(iface, igmp_hdr.grp_addr)) == NULL)
		return;

	if (iface->state == IF_STA_QUERIER) {
		/* querier */
		switch (type) {
		case PKT_TYPE_MEMBER_REPORTv1:
			group_fsm(group, GRP_EVT_V1_REPORT_RCVD);
			break;
		case PKT_TYPE_MEMBER_REPORTv2:
			group_fsm(group, GRP_EVT_V2_REPORT_RCVD);
			break;
		default:
			fatalx("recv_igmp_report: unknown IGMP report type");
		}
	} else {
		/* non querier */
		group_fsm(group, GRP_EVT_REPORT_RCVD);
	}
}

void
recv_igmp_leave(struct iface *iface, struct in_addr src, char *buf,
    u_int16_t len)
{
	struct igmp_hdr	 igmp_hdr;
	struct group	*group;

	log_debug("recv_igmp_leave: interface %s", iface->name);

	if (iface->state != IF_STA_QUERIER)
		return;

	if (len < sizeof(igmp_hdr)) {
		log_debug("recv_igmp_leave: invalid IGMP leave, interface %s",
		    iface->name);
		return;
	}

	memcpy(&igmp_hdr, buf, sizeof(igmp_hdr));

	/* verify chksum */
	if (igmp_chksum(&igmp_hdr) == -1) {
		log_debug("recv_igmp_leave: invalid chksum, interface %s",
		    iface->name);
		return;
	}

	/* validate group id */
	if (!IN_MULTICAST(ntohl(igmp_hdr.grp_addr))) {
		log_debug("recv_igmp_leave: invalid group, interface %s",
		    iface->name);
		return;
	}

	if ((group = group_list_find(iface, igmp_hdr.grp_addr)) != NULL) {
		group_fsm(group, GRP_EVT_LEAVE_RCVD);
	}
}

int
igmp_chksum(struct igmp_hdr *igmp_hdr)
{
	u_int16_t	chksum;

	chksum = igmp_hdr->chksum;
	igmp_hdr->chksum = 0;

	if (chksum != in_cksum(igmp_hdr, sizeof(*igmp_hdr)))
		return (-1);

	return (0);
}
