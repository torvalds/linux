/*	$OpenBSD: dvmrpctl.c,v 1.21 2024/11/21 13:38:14 claudio Exp $ */

/*
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004, 2005, 2006 Esben Norby <norby@openbsd.org>
 * Copyright (c) 2003 Henning Brauer <henning@openbsd.org>
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
#include <netinet/ip_mroute.h>
#include <arpa/inet.h>
#include <net/if_types.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "igmp.h"
#include "dvmrp.h"
#include "dvmrpd.h"
#include "dvmrpe.h"
#include "parser.h"
#include "log.h"

__dead void	 usage(void);
int		 show_summary_msg(struct imsg *);
int		 show_interface_msg(struct imsg *);
int		 show_interface_detail_msg(struct imsg *);
int		 show_igmp_msg(struct imsg *);
const char	*print_if_type(enum iface_type type);
const char	*print_nbr_state(int);
const char	*print_link(int);
const char	*fmt_timeframe(time_t t);
const char	*fmt_timeframe_core(time_t t);
int		 show_nbr_msg(struct imsg *);
const char	*print_dvmrp_options(u_int8_t);
int		 show_nbr_detail_msg(struct imsg *);
int		 show_rib_msg(struct imsg *);
int		 show_rib_detail_msg(struct imsg *);
int		 show_mfc_msg(struct imsg *);
int		 show_mfc_detail_msg(struct imsg *);
const char *	 get_linkstate(uint8_t, int);

struct imsgbuf	*ibuf;

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s command [argument ...]\n", __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_un	 sun;
	struct parse_result	*res;
	struct imsg		 imsg;
	unsigned int		 ifidx = 0;
	int			 ctl_sock;
	int			 done = 0, verbose = 0;
	int			 n;

	/* parse options */
	if ((res = parse(argc - 1, argv + 1)) == NULL)
		exit(1);

	/* connect to dvmrpd control socket */
	if ((ctl_sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "socket");

	bzero(&sun, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, DVMRPD_SOCKET, sizeof(sun.sun_path));
	if (connect(ctl_sock, (struct sockaddr *)&sun, sizeof(sun)) == -1)
		err(1, "connect: %s", DVMRPD_SOCKET);

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if ((ibuf = malloc(sizeof(struct imsgbuf))) == NULL)
		fatal(NULL);
	if (imsgbuf_init(ibuf, ctl_sock) == -1)
		fatal(NULL);
	done = 0;

	/* process user request */
	switch (res->action) {
	case NONE:
		usage();
		/* NOTREACHED */
	case SHOW:
	case SHOW_SUM:
		imsg_compose(ibuf, IMSG_CTL_SHOW_SUM, 0, 0, -1, NULL, 0);
		break;
	case SHOW_IFACE:
		printf("%-11s %-18s %-10s %-10s %-10s %-8s %s\n",
		    "Interface", "Address", "State", "ProbeTimer", "Linkstate",
		    "Uptime", "Groups");
		/* FALLTHROUGH */
	case SHOW_IFACE_DTAIL:
		if (*res->ifname) {
			ifidx = if_nametoindex(res->ifname);
			if (ifidx == 0)
				errx(1, "no such interface %s", res->ifname);
		}
		imsg_compose(ibuf, IMSG_CTL_SHOW_IFACE, 0, 0, -1, &ifidx,
		    sizeof(ifidx));
		break;
	case SHOW_IGMP:
		if (*res->ifname) {
			ifidx = if_nametoindex(res->ifname);
			if (ifidx == 0)
				errx(1, "no such interface %s", res->ifname);
		}
		imsg_compose(ibuf, IMSG_CTL_SHOW_IGMP, 0, 0, -1, &ifidx,
		    sizeof(ifidx));
		break;
	case SHOW_NBR:
		printf("%-15s %-10s %-9s %-15s %-11s %-8s\n", "ID", "State",
		    "DeadTime", "Address", "Interface", "Uptime");
		/* FALLTHROUGH */
	case SHOW_NBR_DTAIL:
		imsg_compose(ibuf, IMSG_CTL_SHOW_NBR, 0, 0, -1, NULL, 0);
		break;
	case SHOW_RIB:
		printf("%-20s %-17s %-7s %-10s %-s\n", "Destination", "Nexthop",
		    "Cost", "Uptime", "Expire");
		/* FALLTHROUGH */
	case SHOW_RIB_DTAIL:
		imsg_compose(ibuf, IMSG_CTL_SHOW_RIB, 0, 0, -1, NULL, 0);
		break;
	case SHOW_MFC:
		printf("%-16s %-16s %-9s %-9s %-4s %-10s %-10s\n", "Group",
		    "Origin", "Incoming", "Outgoing", "TTL", "Uptime",
		    "Expire");
		/* FALLTHROUGH */
	case SHOW_MFC_DTAIL:
		imsg_compose(ibuf, IMSG_CTL_SHOW_MFC, 0, 0, -1, NULL, 0);
		break;
	case LOG_VERBOSE:
		verbose = 1;
		/* FALLTHROUGH */
	case LOG_BRIEF:
		imsg_compose(ibuf, IMSG_CTL_LOG_VERBOSE, 0, 0, -1,
		    &verbose, sizeof(verbose));
		printf("logging request sent.\n");
		done = 1;
		break;
	case RELOAD:
		imsg_compose(ibuf, IMSG_CTL_RELOAD, 0, 0, -1, NULL, 0);
		printf("reload request sent.\n");
		done = 1;
		break;
	}

	if (imsgbuf_flush(ibuf) == -1)
		err(1, "write error");

	while (!done) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			err(1, "read error");
		if (n == 0)
			errx(1, "pipe closed");

		while (!done) {
			if ((n = imsg_get(ibuf, &imsg)) == -1)
				errx(1, "imsg_get error");
			if (n == 0)
				break;
			switch (res->action) {
			case SHOW:
			case SHOW_SUM:
				done = show_summary_msg(&imsg);
				break;
			case SHOW_IFACE:
				done = show_interface_msg(&imsg);
				break;
			case SHOW_IFACE_DTAIL:
				done = show_interface_detail_msg(&imsg);
				break;
			case SHOW_IGMP:
				done = show_igmp_msg(&imsg);
				break;
			case SHOW_NBR:
				done = show_nbr_msg(&imsg);
				break;
			case SHOW_NBR_DTAIL:
				done = show_nbr_detail_msg(&imsg);
				break;
			case SHOW_RIB:
				done = show_rib_msg(&imsg);
				break;
			case SHOW_RIB_DTAIL:
				done = show_rib_detail_msg(&imsg);
				break;
			case SHOW_MFC:
				done = show_mfc_msg(&imsg);
				break;
			case SHOW_MFC_DTAIL:
				done = show_mfc_detail_msg(&imsg);
				break;
			case NONE:
			case LOG_VERBOSE:
			case LOG_BRIEF:
			case RELOAD:
				break;
			}
			imsg_free(&imsg);
		}
	}
	close(ctl_sock);
	free(ibuf);

	return (0);
}

int
show_summary_msg(struct imsg *imsg)
{
	struct ctl_sum		*sum;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_SUM:
		sum = imsg->data;
		printf("Router ID: %s\n", inet_ntoa(sum->rtr_id));
		printf("Hold time is %d sec(s)\n", sum->hold_time);
		break;
	case IMSG_CTL_END:
		printf("\n");
		return (1);
	default:
		break;
	}

	return (0);
}

int
show_interface_msg(struct imsg *imsg)
{
	struct ctl_iface	*iface;
	char			*netid;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_IFACE:
		iface = imsg->data;

		if (asprintf(&netid, "%s/%d", inet_ntoa(iface->addr),
		    mask2prefixlen(iface->mask.s_addr)) == -1)
			err(1, NULL);
		printf("%-11s %-18s %-10s %-10s %-10s %-8s %5d\n",
		    iface->name, netid, if_state_name(iface->state),
		    iface->probe_timer == 0 ? "00:00:00" :
		    fmt_timeframe_core(iface->probe_timer),
		    get_linkstate(iface->if_type, iface->linkstate),
		    iface->uptime == 0 ? "00:00:00" :
		    fmt_timeframe_core(iface->uptime), iface->group_cnt);
		free(netid);
		break;
	case IMSG_CTL_END:
		printf("\n");
		return (1);
	default:
		break;
	}

	return (0);
}

int
show_interface_detail_msg(struct imsg *imsg)
{
	struct ctl_iface	*iface;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_IFACE:
		iface = imsg->data;

		printf("\n");
		printf("Interface %s, line protocol is %s\n",
		    iface->name, print_link(iface->flags));
		printf("  Internet address %s/%d\n",
		    inet_ntoa(iface->addr),
		    mask2prefixlen(iface->mask.s_addr));
		printf("  Linkstate %s\n",
		    get_linkstate(iface->if_type, iface->linkstate));
		printf("  Network type %s, cost: %d\n",
		    if_type_name(iface->type), iface->metric);
		printf("  State %s, querier ", if_state_name(iface->state));
		if (iface->state == IF_STA_QUERIER)
			printf("%s\n", inet_ntoa(iface->addr));
		else
			printf("%s\n", inet_ntoa(iface->querier));
		printf("  Generation ID %d\n", iface->gen_id);
		printf("  Timer intervals configured, "
		    "probe %d, dead %d\n", iface->probe_interval,
		    iface->dead_interval);
		if (iface->passive)
			printf("    Passive interface (No Hellos)\n");
		else if (iface->probe_timer < 0)
			printf("    Hello timer not running\n");
		else
			printf("    Hello timer due in %s\n",
			    fmt_timeframe_core(iface->probe_timer));
		printf("    Uptime %s\n", iface->uptime == 0 ?
		    "00:00:00" : fmt_timeframe_core(iface->uptime));
		printf("  Adjacent neighbor count is "
		    "%d\n", iface->adj_cnt);
		break;
	case IMSG_CTL_END:
		printf("\n");
		return (1);
	default:
		break;
	}

	return (0);
}

int
show_igmp_msg(struct imsg *imsg)
{
	struct ctl_iface	*iface;
	struct ctl_group	*group;
	char			*netid;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_IFACE:
		iface = imsg->data;
		if (asprintf(&netid, "%s/%d", inet_ntoa(iface->addr),
		    mask2prefixlen(iface->mask.s_addr)) == -1)
			err(1, NULL);
		printf("\nInterface %s, address %s, state %s, groups %d\n",
		    iface->name, netid, if_state_name(iface->state),
		    iface->group_cnt);
		free(netid);
		printf("  %-16s %-10s %-10s %-10s\n", "Group", "State",
		    "DeadTimer", "Uptime");
		break;
	case IMSG_CTL_SHOW_IGMP:
		group = imsg->data;
		printf("  %-16s %-10s %-10s %-10s\n", inet_ntoa(group->addr),
		    group_state_name(group->state),
		    group->dead_timer == 0 ? "00:00:00" :
		    fmt_timeframe_core(group->dead_timer),
		    group->uptime == 0 ? "00:00:00" :
		    fmt_timeframe_core(group->uptime));
		break;
	case IMSG_CTL_END:
		printf("\n");
		return (1);
	default:
		break;
	}

	return (0);
}

const char *
print_if_type(enum iface_type type)
{
	switch (type) {
	case IF_TYPE_POINTOPOINT:
		return ("POINTOPOINT");
	case IF_TYPE_BROADCAST:
		return ("BROADCAST");
	default:
		return ("UNKNOWN");
	}
}

const char *
print_nbr_state(int state)
{
	switch (state) {
	case NBR_STA_DOWN:
		return ("DOWN");
	case NBR_STA_1_WAY:
		return ("1-WAY");
	case NBR_STA_2_WAY:
		return ("2-WAY");
	default:
		return ("UNKNOWN");
	}
}

const char *
print_link(int state)
{
	if (state & IFF_UP)
		return ("UP");
	else
		return ("DOWN");
}

#define TF_BUFS	8
#define TF_LEN	9

const char *
fmt_timeframe(time_t t)
{
	if (t == 0)
		return ("Never");
	else
		return (fmt_timeframe_core(time(NULL) - t));
}

const char *
fmt_timeframe_core(time_t t)
{
	char		*buf;
	static char	 tfbuf[TF_BUFS][TF_LEN];	/* ring buffer */
	static int	 idx = 0;
	unsigned int	 sec, min, hrs, day;
	unsigned long long	week;

	if (t == 0)
		return ("Stopped");

	buf = tfbuf[idx++];
	if (idx == TF_BUFS)
		idx = 0;

	week = t;

	sec = week % 60;
	week /= 60;
	min = week % 60;
	week /= 60;
	hrs = week % 24;
	week /= 24;
	day = week % 7;
	week /= 7;

	if (week > 0)
		snprintf(buf, TF_LEN, "%02lluw%01ud%02uh", week, day, hrs);
	else if (day > 0)
		snprintf(buf, TF_LEN, "%01ud%02uh%02um", day, hrs, min);
	else
		snprintf(buf, TF_LEN, "%02u:%02u:%02u", hrs, min, sec);

	return (buf);
}

/* prototype defined in dvmrpd.h and shared with the kroute.c version */
u_int8_t
mask2prefixlen(in_addr_t ina)
{
	if (ina == 0)
		return (0);
	else
		return (33 - ffs(ntohl(ina)));
}

int
show_nbr_msg(struct imsg *imsg)
{
	struct ctl_nbr	*nbr;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_NBR:
		nbr = imsg->data;
		printf("%-15s %-10s %-10s", inet_ntoa(nbr->id),
		    print_nbr_state(nbr->state),
		    fmt_timeframe_core(nbr->dead_timer));
		printf("%-15s %-11s %s\n", inet_ntoa(nbr->addr),
		    nbr->name, fmt_timeframe_core(nbr->uptime));
		break;
	case IMSG_CTL_END:
		printf("\n");
		return (1);
	default:
		break;
	}

	return (0);
}

const char *
print_dvmrp_options(u_int8_t opts)
{
	static char	optbuf[32];

	snprintf(optbuf, sizeof(optbuf), "*|*|%s|%s|%s|%s|%s|%s",
	    opts & DVMRP_CAP_NETMASK ? "N" : "-",
	    opts & DVMRP_CAP_SNMP ? "S" : "-",
	    opts & DVMRP_CAP_MTRACE ? "M" : "-",
	    opts & DVMRP_CAP_GENID ? "G" : "-",
	    opts & DVMRP_CAP_PRUNE ? "P" : "-",
	    opts & DVMRP_CAP_LEAF ? "L" : "-");
	return (optbuf);
}

int
show_nbr_detail_msg(struct imsg *imsg)
{
	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_NBR:
		break;
	case IMSG_CTL_END:
		printf("\n");
		return (1);
	default:
		break;
	}

	return (0);
}

int
show_rib_msg(struct imsg *imsg)
{
	struct ctl_rt	*rt;
	char		*dstnet;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_RIB:
		rt = imsg->data;
		if (asprintf(&dstnet, "%s/%d", inet_ntoa(rt->prefix),
		    rt->prefixlen) == -1)
			err(1, NULL);

		printf("%-20s %-17s %-7d %-9s %9s\n", dstnet,
		    inet_ntoa(rt->nexthop),
		    rt->cost, rt->uptime == 0 ? "-" :
		    fmt_timeframe_core(rt->uptime),
		    rt->expire == 0 ? "00:00:00" :
		    fmt_timeframe_core(rt->expire));
		free(dstnet);

		break;
	case IMSG_CTL_END:
		printf("\n");
		return (1);
	default:
		break;
	}

	return (0);
}

int
show_rib_detail_msg(struct imsg *imsg)
{

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_RIB:
		break;
	case IMSG_CTL_END:
		printf("\n");
		return (1);
	default:
		break;
	}

	return (0);
}

int
show_mfc_msg(struct imsg *imsg)
{
	char		 iname[IF_NAMESIZE];
	char		 oname[IF_NAMESIZE] = "-";
	struct ctl_mfc	*mfc;
	int		 i;


	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_MFC:
		mfc = imsg->data;
		if_indextoname(mfc->ifindex, iname);

		/* search for first entry with ttl > 0 */
		for (i = 0; i < MAXVIFS; i++) {
			if (mfc->ttls[i] > 0) {
				if_indextoname(i, oname);
				i++;
				break;
			}
		}

		/* display first entry with uptime */
		printf("%-16s ", inet_ntoa(mfc->group));
		printf("%-16s %-9s %-9s %-4d %-10s %-10s\n",
		    inet_ntoa(mfc->origin), iname, oname, mfc->ttls[i - 1],
		    mfc->uptime == 0 ? "-" : fmt_timeframe_core(mfc->uptime),
		    mfc->expire == 0 ? "-" : fmt_timeframe_core(mfc->expire));

		/* display remaining entries with ttl > 0 */
		for (; i < MAXVIFS; i++) {
			if (mfc->ttls[i] > 0) {
				if_indextoname(i, oname);
				printf("%43s %-9s %-4d\n", " ", oname,
				    mfc->ttls[i]);
			}
		}
		break;
	case IMSG_CTL_END:
		printf("\n");
		return (1);
	default:
		break;
	}

	return (0);
}

int
show_mfc_detail_msg(struct imsg *imsg)
{

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_MFC:
		break;
	case IMSG_CTL_END:
		printf("\n");
		return (1);
	default:
		break;
	}

	return (0);
}

const struct if_status_description
		if_status_descriptions[] = LINK_STATE_DESCRIPTIONS;

const char *
get_linkstate(uint8_t if_type, int link_state)
{
	const struct if_status_description *p;
	static char buf[8];

	for (p = if_status_descriptions; p->ifs_string != NULL; p++) {
		if (LINK_STATE_DESC_MATCH(p, if_type, link_state))
			return (p->ifs_string);
	}
	snprintf(buf, sizeof(buf), "[#%d]", link_state);
	return (buf);
}
