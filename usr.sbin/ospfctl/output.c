/*
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004, 2005 Esben Norby <norby@openbsd.org>
 * Copyright (c) 2003 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2020 Richard Chivers <r.chivers@zengenti.com>
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
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ospf.h"
#include "ospfd.h"
#include "ospfctl.h"
#include "ospfe.h"
#include "parser.h"

static void
show_head(struct parse_result *res)
{
	switch (res->action) {
	case SHOW_IFACE:
		printf("%-11s %-18s %-6s %-10s %-10s %-8s %3s %3s\n",
		    "Interface", "Address", "State", "HelloTimer", "Linkstate",
		    "Uptime", "nc", "ac");
		break;
	case SHOW_FIB:
		printf("flags: * = valid, O = OSPF, C = Connected, "
		    "S = Static\n");
		printf("%-6s %-4s %-20s %-17s\n", "Flags", "Prio",
		    "Destination", "Nexthop");
		break;
	case SHOW_FIB_IFACE:
		printf("%-15s%-15s%s\n", "Interface", "Flags", "Link state");
		break;
	case SHOW_NBR:
		printf("%-15s %-3s %-12s %-8s %-15s %-9s %s\n", "ID", "Pri",
		    "State", "DeadTime", "Address", "Iface","Uptime");
		break;
	case SHOW_RIB:
		printf("%-20s %-17s %-12s %-9s %-7s %-8s\n", "Destination",
		    "Nexthop", "Path Type", "Type", "Cost", "Uptime");
		break;
	default:
		break;
	}
}

static void
show_summary(struct ctl_sum *sum)
{
	printf("Router ID: %s\n", inet_ntoa(sum->rtr_id));
	printf("Uptime: %s\n", fmt_timeframe_core(sum->uptime));
	printf("RFC1583 compatibility flag is ");
	if (sum->rfc1583compat)
		printf("enabled\n");
	else
		printf("disabled\n");

	printf("SPF delay is %d msec(s), hold time between two SPFs "
	    "is %d msec(s)\n", sum->spf_delay, sum->spf_hold_time);
	printf("Number of external LSA(s) %d (Checksum sum 0x%x)\n",
	    sum->num_ext_lsa, sum->ext_lsa_cksum);
	printf("Number of areas attached to this router: %d\n",
	    sum->num_area);
}

static void
show_summary_area(struct ctl_sum_area *sumarea)
{
	printf("\nArea ID: %s\n", inet_ntoa(sumarea->area));
	printf("  Number of interfaces in this area: %d\n",
	    sumarea->num_iface);
	printf("  Number of fully adjacent neighbors in this "
	    "area: %d\n", sumarea->num_adj_nbr);
	printf("  SPF algorithm executed %d time(s)\n",
	    sumarea->num_spf_calc);
	printf("  Number LSA(s) %d (Checksum sum 0x%x)\n",
	    sumarea->num_lsa, sumarea->lsa_cksum);
}

static void
show_rib_head(struct in_addr aid, u_int8_t d_type, u_int8_t p_type)
{
	char	*header, *format, *format2;

	switch (p_type) {
	case PT_INTRA_AREA:
	case PT_INTER_AREA:
		switch (d_type) {
		case DT_NET:
			format = "Network Routing Table";
			format2 = "";
			break;
		case DT_RTR:
			format = "Router Routing Table";
			format2 = "Type";
			break;
		default:
			errx(1, "unknown route type");
		}
		break;
	case PT_TYPE1_EXT:
	case PT_TYPE2_EXT:
		format = NULL;
		format2 = "Cost 2";
		if ((header = strdup("External Routing Table")) == NULL)
			err(1, NULL);
		break;
	default:
		errx(1, "unknown route type");
	}

	if (p_type != PT_TYPE1_EXT && p_type != PT_TYPE2_EXT)
		if (asprintf(&header, "%s (Area %s)", format,
		    inet_ntoa(aid)) == -1)
			err(1, NULL);

	printf("\n%-18s %s\n", "", header);
	free(header);

	printf("\n%-18s %-15s %-15s %-12s %-7s %-7s\n", "Destination",
	    "Nexthop", "Adv Router", "Path type", "Cost", format2);
}

static void
show_interface(struct ctl_iface	*iface, int detail)
{
	char	*netid;

	/* XXX This wasn't previously executed on detail call */
	if (asprintf(&netid, "%s/%d", inet_ntoa(iface->addr),
	    mask2prefixlen(iface->mask.s_addr)) == -1)
			err(1, NULL);

	if (detail) {
		printf("\n");
		printf("Interface %s, line protocol is %s\n",
		    iface->name, print_link(iface->flags));
		printf("  Internet address %s/%d, ",
		    inet_ntoa(iface->addr),
		    mask2prefixlen(iface->mask.s_addr));
		printf("Area %s\n", inet_ntoa(iface->area));
		printf("  Linkstate %s,",
		    get_linkstate(iface->if_type, iface->linkstate));
		printf(" mtu %d\n", iface->mtu);
		printf("  Router ID %s, network type %s, cost: %d\n",
		    inet_ntoa(iface->rtr_id),
		    if_type_name(iface->type), iface->metric);
		if (iface->dependon[0] != '\0') {
			printf("  Depends on %s, %s\n", iface->dependon,
			    iface->depend_ok ? "up" : "down");
		}
		printf("  Transmit delay is %d sec(s), state %s, priority %d\n",
		    iface->transmit_delay, if_state_name(iface->state),
		    iface->priority);
		printf("  Designated Router (ID) %s, ",
		    inet_ntoa(iface->dr_id));
		printf("interface address %s\n", inet_ntoa(iface->dr_addr));
		printf("  Backup Designated Router (ID) %s, ",
		    inet_ntoa(iface->bdr_id));
		printf("interface address %s\n", inet_ntoa(iface->bdr_addr));
		if (iface->dead_interval == FAST_RTR_DEAD_TIME) {
			printf("  Timer intervals configured, "
			    "hello %d msec, dead %d, wait %d, retransmit %d\n",
			    iface->fast_hello_interval, iface->dead_interval,
			    iface->dead_interval, iface->rxmt_interval);

		} else {
			printf("  Timer intervals configured, "
			    "hello %d, dead %d, wait %d, retransmit %d\n",
			    iface->hello_interval, iface->dead_interval,
			    iface->dead_interval, iface->rxmt_interval);
		}

		if (iface->passive)
			printf("    Passive interface (No Hellos)\n");
		else if (iface->hello_timer.tv_sec < 0)
			printf("    Hello timer not running\n");
		else
			printf("    Hello timer due in %s+%ldmsec\n",
			    fmt_timeframe_core(iface->hello_timer.tv_sec),
			    iface->hello_timer.tv_usec / 1000);
		printf("    Uptime %s\n", fmt_timeframe_core(iface->uptime));
		printf("  Neighbor count is %d, adjacent neighbor count is "
		    "%d\n", iface->nbr_cnt, iface->adj_cnt);

		if (iface->auth_type > 0) {
			switch (iface->auth_type) {
			case AUTH_SIMPLE:
				printf("  Simple password authentication "
				    "enabled\n");
				break;
			case AUTH_CRYPT:
				printf("  Message digest authentication "
				    "enabled\n");
				printf("    Primary key id is %d\n",
				    iface->auth_keyid);
				break;
			default:
				break;
			}
		}
	} else {
		printf("%-11s %-18s %-6s %-10s %-10s %s %3d %3d\n",
		    iface->name, netid, if_state_name(iface->state),
		    iface->hello_timer.tv_sec < 0 ? "-" :
		    fmt_timeframe_core(iface->hello_timer.tv_sec),
		    get_linkstate(iface->if_type, iface->linkstate),
		    fmt_timeframe_core(iface->uptime),
		    iface->nbr_cnt, iface->adj_cnt);
	}
	free(netid);
}


static void
show_neighbor(struct ctl_nbr *nbr, int detail)
{
	char	*state;

	if (asprintf(&state, "%s/%s", nbr_state_name(nbr->nbr_state),
	    if_state_name(nbr->iface_state)) == -1)
		err(1, NULL);

	if (detail) {
		printf("\nNeighbor %s, ", inet_ntoa(nbr->id));
		printf("interface address %s\n", inet_ntoa(nbr->addr));
		printf("  Area %s, interface %s\n", inet_ntoa(nbr->area),
		    nbr->name);
		printf("  Neighbor priority is %d, "
		    "State is %s, %d state changes\n",
		    nbr->priority, nbr_state_name(nbr->nbr_state),
		    nbr->state_chng_cnt);
		printf("  DR is %s, ", inet_ntoa(nbr->dr));
		printf("BDR is %s\n", inet_ntoa(nbr->bdr));
		printf("  Options %s\n", print_ospf_options(nbr->options));
		printf("  Dead timer due in %s\n",
		    fmt_timeframe_core(nbr->dead_timer));
		printf("  Uptime %s\n", fmt_timeframe_core(nbr->uptime));
		printf("  Database Summary List %d\n", nbr->db_sum_lst_cnt);
		printf("  Link State Request List %d\n", nbr->ls_req_lst_cnt);
		printf("  Link State Retransmission List %d\n",
		    nbr->ls_retrans_lst_cnt);
	} else {
		printf("%-15s %-3d %-12s %-9s", inet_ntoa(nbr->id),
		    nbr->priority, state, fmt_timeframe_core(nbr->dead_timer));
		printf("%-15s %-9s %s\n", inet_ntoa(nbr->addr), nbr->name,
		    nbr->uptime == 0 ? "-" : fmt_timeframe_core(nbr->uptime));
	}
	free(state);
}

static void
show_rib(struct ctl_rt *rt, int detail)
{
	char		*dstnet;
	static u_int8_t	 lasttype;

	if (detail) {
		switch (rt->p_type) {
		case PT_INTRA_AREA:
		case PT_INTER_AREA:
			switch (rt->d_type) {
			case DT_NET:
				if (lasttype != RIB_NET)
					show_rib_head(rt->area, rt->d_type,
					    rt->p_type);
				if (asprintf(&dstnet, "%s/%d",
				    inet_ntoa(rt->prefix), rt->prefixlen) == -1)
					err(1, NULL);
				lasttype = RIB_NET;
				break;
			case DT_RTR:
				if (lasttype != RIB_RTR)
					show_rib_head(rt->area, rt->d_type,
					    rt->p_type);
				if (asprintf(&dstnet, "%s",
					inet_ntoa(rt->prefix)) == -1)
					err(1, NULL);
				lasttype = RIB_RTR;
				break;
			default:
				errx(1, "unknown route type");
			}
			printf("%-18s %-15s ", dstnet, inet_ntoa(rt->nexthop));
			printf("%-15s %-12s %-7d", inet_ntoa(rt->adv_rtr),
			    path_type_name(rt->p_type), rt->cost);
			free(dstnet);

			if (rt->d_type == DT_RTR)
				printf(" %-7s", print_ospf_rtr_flags(rt->flags));

			printf("\n");
			break;
		case PT_TYPE1_EXT:
		case PT_TYPE2_EXT:
			if (lasttype != RIB_EXT)
				show_rib_head(rt->area, rt->d_type, rt->p_type);

			if (asprintf(&dstnet, "%s/%d", inet_ntoa(rt->prefix),
			    rt->prefixlen) == -1)
				err(1, NULL);

			printf("%-18s %-15s ", dstnet, inet_ntoa(rt->nexthop));
			printf("%-15s %-12s %-7d %-7d\n", inet_ntoa(rt->adv_rtr),
			    path_type_name(rt->p_type), rt->cost, rt->cost2);

			free(dstnet);

			lasttype = RIB_EXT;
			break;
		default:
			errx(1, "unknown route type");
		}
	} else {
		switch (rt->d_type) {
		case DT_NET:
			if (asprintf(&dstnet, "%s/%d", inet_ntoa(rt->prefix),
			    rt->prefixlen) == -1)
				err(1, NULL);
			break;
		case DT_RTR:
			if (asprintf(&dstnet, "%s",
			    inet_ntoa(rt->prefix)) == -1)
				err(1, NULL);
			break;
		default:
			errx(1, "Invalid route type");
		}

		printf("%-20s %-16s%s %-12s %-9s %-7d %s\n", dstnet,
		    inet_ntoa(rt->nexthop), rt->connected ? "C" : " ",
		    path_type_name(rt->p_type),
		    dst_type_name(rt->d_type), rt->cost,
		    rt->uptime == 0 ? "-" : fmt_timeframe_core(rt->uptime));

		free(dstnet);
	}
}

static void
show_fib(struct kroute *k)
{
	char	*p;

	if (k->flags & F_DOWN)
		printf(" ");
	else
		printf("*");

	if (!(k->flags & F_KERNEL))
		printf("O");
	else if (k->flags & F_CONNECTED)
		printf("C");
	else if (k->flags & F_STATIC)
		printf("S");
	else
		printf(" ");

	printf("     ");
	printf("%4d ", k->priority);
	if (asprintf(&p, "%s/%u", inet_ntoa(k->prefix), k->prefixlen) == -1)
		err(1, NULL);

	printf("%-20s ", p);
	free(p);

	if (k->nexthop.s_addr)
		printf("%s", inet_ntoa(k->nexthop));
	else if (k->flags & F_CONNECTED)
		printf("link#%u", k->ifindex);

	printf("\n");
}

static void
show_fib_interface(struct kif *k)
{
	uint64_t	ifms_type;

	printf("%-15s", k->ifname);
	printf("%-15s", k->flags & IFF_UP ? "UP" : "");
	ifms_type = get_ifms_type(k->if_type);
	if (ifms_type)
		printf("%s, ", get_media_descr(ifms_type));

	printf("%s", get_linkstate(k->if_type, k->link_state));

	if (k->link_state != LINK_STATE_DOWN && k->baudrate > 0) {
		printf(", ");
		printf("%s", print_baudrate(k->baudrate));
	}
	printf("\n");
}

static void
show_database_head(struct in_addr aid, char *ifname, u_int8_t type)
{
	char	*header, *format;
	int	 cleanup = 0;

	switch (type) {
	case LSA_TYPE_ROUTER:
		format = "Router Link States";
		break;
	case LSA_TYPE_NETWORK:
		format = "Net Link States";
		break;
	case LSA_TYPE_SUM_NETWORK:
		format = "Summary Net Link States";
		break;
	case LSA_TYPE_SUM_ROUTER:
		format = "Summary Router Link States";
		break;
	case LSA_TYPE_EXTERNAL:
		format = NULL;
		if ((header = strdup("Type-5 AS External Link States")) == NULL)
			err(1, NULL);
		break;
	case LSA_TYPE_LINK_OPAQ:
		format = "Type-9 Link Local Opaque Link States";
		break;
	case LSA_TYPE_AREA_OPAQ:
		format = "Type-10 Area Local Opaque Link States";
		break;
	case LSA_TYPE_AS_OPAQ:
		format = NULL;
		if ((header = strdup("Type-11 AS Wide Opaque Link States")) ==
			NULL)
			err(1, NULL);
		break;
	default:
		if (asprintf(&format, "LSA type %x", ntohs(type)) == -1)
			err(1, NULL);
		cleanup = 1;
		break;
	}
	if (type == LSA_TYPE_LINK_OPAQ) {
		if (asprintf(&header, "%s (Area %s Interface %s)", format,
		    inet_ntoa(aid), ifname) == -1)
			err(1, NULL);
	} else if (type != LSA_TYPE_EXTERNAL && type != LSA_TYPE_AS_OPAQ)
		if (asprintf(&header, "%s (Area %s)", format,
		    inet_ntoa(aid)) == -1)
			err(1, NULL);

	printf("\n%-15s %s\n\n", "", header);
	free(header);
	if (cleanup)
		free(format);
}

static void
show_db_hdr_msg_detail(struct lsa_hdr *lsa)
{
	printf("LS age: %d\n", ntohs(lsa->age));
	printf("Options: %s\n", print_ospf_options(lsa->opts));
	printf("LS Type: %s\n", print_ls_type(lsa->type));

	switch (lsa->type) {
	case LSA_TYPE_ROUTER:
		printf("Link State ID: %s\n", log_id(lsa->ls_id));
		break;
	case LSA_TYPE_NETWORK:
		printf("Link State ID: %s (address of Designated Router)\n",
		    log_id(lsa->ls_id));
		break;
	case LSA_TYPE_SUM_NETWORK:
		printf("Link State ID: %s (Network ID)\n", log_id(lsa->ls_id));
		break;
	case LSA_TYPE_SUM_ROUTER:
		printf("Link State ID: %s (ASBR Router ID)\n",
		    log_id(lsa->ls_id));
		break;
	case LSA_TYPE_EXTERNAL:
		printf("Link State ID: %s (External Network Number)\n",
		    log_id(lsa->ls_id));
		break;
	case LSA_TYPE_LINK_OPAQ:
	case LSA_TYPE_AREA_OPAQ:
	case LSA_TYPE_AS_OPAQ:
		printf("Link State ID: %s Type %d ID %d\n", log_id(lsa->ls_id),
		    LSA_24_GETHI(ntohl(lsa->ls_id)),
		    LSA_24_GETLO(ntohl(lsa->ls_id)));
		break;
	}

	printf("Advertising Router: %s\n", log_adv_rtr(lsa->adv_rtr));
	printf("LS Seq Number: 0x%08x\n", ntohl(lsa->seq_num));
	printf("Checksum: 0x%04x\n", ntohs(lsa->ls_chksum));
	printf("Length: %d\n", ntohs(lsa->len));
}

static void
show_db_simple(struct lsa_hdr *lsa, struct in_addr area_id, u_int8_t lasttype,
    char *ifname)
{
	if (lsa->type != lasttype) {
		show_database_head(area_id, ifname, lsa->type);
		printf("%-15s %-15s %-4s %-10s %-8s\n", "Link ID",
		    "Adv Router", "Age", "Seq#", "Checksum");
	}
	printf("%-15s %-15s %-4d 0x%08x 0x%04x\n",
	    log_id(lsa->ls_id), log_adv_rtr(lsa->adv_rtr),
	    ntohs(lsa->age), ntohl(lsa->seq_num),
	    ntohs(lsa->ls_chksum));
}
static void
show_db(struct lsa *lsa, struct in_addr	area_id, u_int8_t lasttype,
	char *ifname)
{
	struct in_addr		 addr, data;
	struct lsa_asext	*asext;
	struct lsa_rtr_link	*rtr_link;
	u_int16_t		 i, nlinks, off;

	if (lsa->hdr.type != lasttype)
		show_database_head(area_id, ifname, lsa->hdr.type);
	show_db_hdr_msg_detail(&lsa->hdr);

	switch (lsa->hdr.type) {
	case LSA_TYPE_EXTERNAL:
		addr.s_addr = lsa->data.asext.mask;
		printf("Network Mask: %s\n", inet_ntoa(addr));

		asext = (struct lsa_asext *)((char *)lsa + sizeof(lsa->hdr));

		printf("    Metric type: ");
		if (ntohl(lsa->data.asext.metric) & LSA_ASEXT_E_FLAG)
			printf("2\n");
		else
			printf("1\n");
		printf("    Metric: %d\n", ntohl(asext->metric) &
		    LSA_METRIC_MASK);
		addr.s_addr = asext->fw_addr;
		printf("    Forwarding Address: %s\n", inet_ntoa(addr));
		printf("    External Route Tag: %d\n\n", ntohl(asext->ext_tag));
		break;
	case LSA_TYPE_NETWORK:
		addr.s_addr = lsa->data.net.mask;
		printf("Network Mask: %s\n", inet_ntoa(addr));

		nlinks = (ntohs(lsa->hdr.len) - sizeof(struct lsa_hdr)
		    - sizeof(u_int32_t)) / sizeof(struct lsa_net_link);
		off = sizeof(lsa->hdr) + sizeof(u_int32_t);
		printf("Number of Routers: %d\n", nlinks);

		for (i = 0; i < nlinks; i++) {
			addr.s_addr = lsa->data.net.att_rtr[i];
			printf("    Attached Router: %s\n", inet_ntoa(addr));
		}

		printf("\n");
		break;
	case LSA_TYPE_ROUTER:
		printf("Flags: %s\n", print_ospf_flags(lsa->data.rtr.flags));
		nlinks = ntohs(lsa->data.rtr.nlinks);
		printf("Number of Links: %d\n\n", nlinks);

		off = sizeof(lsa->hdr) + sizeof(struct lsa_rtr);

		for (i = 0; i < nlinks; i++) {
			rtr_link = (struct lsa_rtr_link *)((char *)lsa + off);

			printf("    Link connected to: %s\n",
			    print_rtr_link_type(rtr_link->type));

			addr.s_addr = rtr_link->id;
			data.s_addr = rtr_link->data;

			switch (rtr_link->type) {
			case LINK_TYPE_POINTTOPOINT:
			case LINK_TYPE_VIRTUAL:
				printf("    Link ID (Neighbors Router ID): "
				    "%s\n", inet_ntoa(addr));
				printf("    Link Data (Router Interface "
				    "address): %s\n", inet_ntoa(data));
				break;
			case LINK_TYPE_TRANSIT_NET:
				printf("    Link ID (Designated Router "
				    "address): %s\n", inet_ntoa(addr));
				printf("    Link Data (Router Interface "
				    "address): %s\n", inet_ntoa(data));
				break;
			case LINK_TYPE_STUB_NET:
				printf("    Link ID (Network ID): %s\n",
				    inet_ntoa(addr));
				printf("    Link Data (Network Mask): %s\n",
				    inet_ntoa(data));
				break;
			default:
				printf("    Link ID (Unknown): %s\n",
				    inet_ntoa(addr));
				printf("    Link Data (Unknown): %s\n",
				    inet_ntoa(data));
				break;
			}

			printf("    Metric: %d\n\n", ntohs(rtr_link->metric));

			off += sizeof(struct lsa_rtr_link) +
			    rtr_link->num_tos * sizeof(u_int32_t);
		}
		break;
	case LSA_TYPE_SUM_ROUTER:
		if (lsa->hdr.type != lasttype)
			show_database_head(area_id, ifname, lsa->hdr.type);

		show_db_hdr_msg_detail(&lsa->hdr);
		addr.s_addr = lsa->data.sum.mask;
		printf("Network Mask: %s\n", inet_ntoa(addr));
		printf("Metric: %d\n\n", ntohl(lsa->data.sum.metric) &
		    LSA_METRIC_MASK);
		break;
	case LSA_TYPE_LINK_OPAQ:
	case LSA_TYPE_AREA_OPAQ:
	case LSA_TYPE_AS_OPAQ:
		if (lsa->hdr.type != lasttype)
			show_database_head(area_id, ifname, lsa->hdr.type);

		show_db_hdr_msg_detail(&lsa->hdr);
		break;
	}
}

static void
show_tail(void)
{
	/* nothing */
}

const struct output show_output = {
	.head = show_head,
	.summary = show_summary,
	.summary_area = show_summary_area,
	.interface = show_interface,
	.neighbor = show_neighbor,
	.rib = show_rib,
	.fib = show_fib,
	.fib_interface = show_fib_interface,
	.db = show_db,
	.db_simple = show_db_simple,
	.tail = show_tail
};
