/*	$OpenBSD: eigrpctl.c,v 1.14 2024/11/21 13:38:14 claudio Exp $ */

/*
 * Copyright (c) 2015 Renato Westphal <renato@openbsd.org>
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004, 2005 Esben Norby <norby@openbsd.org>
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
#include <arpa/inet.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "eigrp.h"
#include "eigrpd.h"
#include "eigrpe.h"
#include "rde.h"
#include "log.h"
#include "parser.h"

__dead void	 usage(void);
uint64_t	 get_ifms_type(uint8_t);
int		 show_interface_msg(struct imsg *, struct parse_result *);
int		 show_interface_detail_msg(struct imsg *,
    struct parse_result *);
const char	*print_link(int);
const char	*fmt_timeframe_core(time_t);
int		 show_nbr_msg(struct imsg *, struct parse_result *);
int		 show_topology_msg(struct imsg *, struct parse_result *);
int		 show_topology_detail_msg(struct imsg *,
    struct parse_result *);
void		 show_fib_head(void);
int		 show_fib_msg(struct imsg *, struct parse_result *);
void		 show_interface_head(void);
const char *	 get_media_descr(uint64_t);
const char *	 get_linkstate(uint8_t, int);
void		 print_baudrate(uint64_t);
int		 show_fib_interface_msg(struct imsg *);
int		 show_stats_msg(struct imsg *, struct parse_result *);

struct imsgbuf	*ibuf;

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-s socket] command [argument ...]\n",
	    __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_un		 sun;
	struct parse_result		*res;
	struct imsg			 imsg;
	unsigned int			 ifidx = 0;
	int				 ctl_sock;
	int				 done = 0;
	int				 n, verbose = 0;
	int				 ch;
	char				*sockname;
	struct ctl_show_topology_req	 treq;
	struct ctl_nbr			 nbr;

	sockname = EIGRPD_SOCKET;
	while ((ch = getopt(argc, argv, "s:")) != -1) {
		switch (ch) {
		case 's':
			sockname = optarg;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	/* parse options */
	if ((res = parse(argc, argv)) == NULL)
		exit(1);

	/* connect to eigrpd control socket */
	if ((ctl_sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "socket");

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, sockname, sizeof(sun.sun_path));
	if (connect(ctl_sock, (struct sockaddr *)&sun, sizeof(sun)) == -1)
		err(1, "connect: %s", sockname);

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if ((ibuf = malloc(sizeof(struct imsgbuf))) == NULL)
		err(1, NULL);
	if (imsgbuf_init(ibuf, ctl_sock) == -1)
		err(1, NULL);
	done = 0;

	/* process user request */
	switch (res->action) {
	case NONE:
		usage();
		/* not reached */
	case SHOW:
	case SHOW_IFACE:
		printf("%-4s %-5s %-11s %-18s %-10s %-8s %3s\n",
		    "AF", "AS", "Interface", "Address", "Linkstate",
		    "Uptime", "nc");
		/*FALLTHROUGH*/
	case SHOW_IFACE_DTAIL:
		if (*res->ifname) {
			ifidx = if_nametoindex(res->ifname);
			if (ifidx == 0)
				errx(1, "no such interface %s", res->ifname);
		}
		imsg_compose(ibuf, IMSG_CTL_SHOW_INTERFACE, 0, 0, -1,
		    &ifidx, sizeof(ifidx));
		break;
	case SHOW_NBR:
		printf("%-4s %-5s %-18s %-11s %-10s %8s\n", "AF", "AS",
		    "Address", "Iface", "Holdtime", "Uptime");
		imsg_compose(ibuf, IMSG_CTL_SHOW_NBR, 0, 0, -1, NULL, 0);
		break;
	case SHOW_TOPOLOGY:
		memset(&treq, 0, sizeof(treq));
		treq.af = res->family;
		memcpy(&treq.prefix, &res->addr, sizeof(res->addr));
		treq.prefixlen = res->prefixlen;
		treq.flags = res->flags;

		if (!eigrp_addrisset(res->family, &res->addr))
			printf("  %-4s %-5s %-18s %-15s %-12s %s\n",
			    "AF", "AS", "Destination", "Nexthop", "Interface",
			    "Distance");
		imsg_compose(ibuf, IMSG_CTL_SHOW_TOPOLOGY, 0, 0, -1,
		    &treq, sizeof(treq));
		break;
	case SHOW_FIB:
		show_fib_head();
		imsg_compose(ibuf, IMSG_CTL_KROUTE, 0, 0, -1,
		    &res->flags, sizeof(res->flags));
		break;
	case SHOW_FIB_IFACE:
		if (*res->ifname)
			imsg_compose(ibuf, IMSG_CTL_IFINFO, 0, 0, -1,
			    res->ifname, sizeof(res->ifname));
		else
			imsg_compose(ibuf, IMSG_CTL_IFINFO, 0, 0, -1, NULL, 0);
		show_interface_head();
		break;
	case SHOW_STATS:
		imsg_compose(ibuf, IMSG_CTL_SHOW_STATS, 0, 0, -1, NULL, 0);
		break;
	case CLEAR_NBR:
		memset(&nbr, 0, sizeof(nbr));
		nbr.af = res->family;
		nbr.as = res->as;
		memcpy(&nbr.addr, &res->addr, sizeof(res->addr));
		imsg_compose(ibuf, IMSG_CTL_CLEAR_NBR, 0, 0, -1, &nbr,
		    sizeof(nbr));
		done = 1;
		break;
	case FIB:
		errx(1, "fib couple|decouple");
		break;
	case FIB_COUPLE:
		imsg_compose(ibuf, IMSG_CTL_FIB_COUPLE, 0, 0, -1, NULL, 0);
		printf("couple request sent.\n");
		done = 1;
		break;
	case FIB_DECOUPLE:
		imsg_compose(ibuf, IMSG_CTL_FIB_DECOUPLE, 0, 0, -1, NULL, 0);
		printf("decouple request sent.\n");
		done = 1;
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
			case SHOW_IFACE:
				done = show_interface_msg(&imsg, res);
				break;
			case SHOW_IFACE_DTAIL:
				done = show_interface_detail_msg(&imsg, res);
				break;
			case SHOW_NBR:
				done = show_nbr_msg(&imsg, res);
				break;
			case SHOW_TOPOLOGY:
				if (eigrp_addrisset(res->family, &res->addr))
					done = show_topology_detail_msg(&imsg,
					    res);
				else
					done = show_topology_msg(&imsg, res);
				break;
			case SHOW_FIB:
				done = show_fib_msg(&imsg, res);
				break;
			case SHOW_FIB_IFACE:
				done = show_fib_interface_msg(&imsg);
				break;
			case SHOW_STATS:
				done = show_stats_msg(&imsg, res);
				break;
			case CLEAR_NBR:
			case NONE:
			case FIB:
			case FIB_COUPLE:
			case FIB_DECOUPLE:
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

uint64_t
get_ifms_type(uint8_t if_type)
{
	switch (if_type) {
	case IFT_ETHER:
		return (IFM_ETHER);
	case IFT_FDDI:
		return (IFM_FDDI);
	case IFT_CARP:
		return (IFM_CARP);
	case IFT_PPP:
		return (IFM_TDM);
	default:
		return (0);
	}
}

int
show_interface_msg(struct imsg *imsg, struct parse_result *res)
{
	struct ctl_iface	*iface;
	char			*addr;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_INTERFACE:
		if (imsg->hdr.len < IMSG_HEADER_SIZE +
		    sizeof(struct ctl_iface))
			errx(1, "wrong imsg len");
		iface = imsg->data;

		if (res->family != AF_UNSPEC && res->family != iface->af)
			break;
		if (res->as != 0 && res->as != iface->as)
			break;

		if (asprintf(&addr, "%s/%d", log_addr(iface->af, &iface->addr),
		    iface->prefixlen) == -1)
			err(1, NULL);

		printf("%-4s %-5u %-11s %-18s", af_name(iface->af), iface->as,
		    iface->name, addr);
		if (strlen(addr) > 18)
			printf("\n%41s", " ");
		printf(" %-10s %-8s %3u\n", get_linkstate(iface->if_type,
		    iface->linkstate), fmt_timeframe_core(iface->uptime),
		    iface->nbr_cnt);
		free(addr);
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
show_interface_detail_msg(struct imsg *imsg, struct parse_result *res)
{
	struct ctl_iface	*iface;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_INTERFACE:
		if (imsg->hdr.len < IMSG_HEADER_SIZE +
		    sizeof(struct ctl_iface))
			errx(1, "wrong imsg len");
		iface = imsg->data;

		if (res->family != AF_UNSPEC && res->family != iface->af)
			break;
		if (res->as != 0 && res->as != iface->as)
			break;

		printf("\n");
		printf("Interface %s, line protocol is %s\n",
		    iface->name, print_link(iface->flags));
		printf("  Autonomous System %u, Address Family %s\n",
		    iface->as, af_name(iface->af));
		printf("  Internet address %s/%d\n",
		    log_addr(iface->af, &iface->addr), iface->prefixlen);
		printf("  Linkstate %s, network type %s\n",
		    get_linkstate(iface->if_type, iface->linkstate),
		    if_type_name(iface->type));
		printf("  Delay %u usec, Bandwidth %u Kbit/sec\n",
		    iface->delay, iface->bandwidth);
		if (iface->passive)
			printf("  Passive interface (No Hellos)\n");
		else {
			printf("  Hello interval %u, Hello holdtime %u\n",
			    iface->hello_interval, iface->hello_holdtime);
			printf("  Split-horizon %s\n",
			    (iface->splithorizon) ? "enabled" : "disabled");
			printf("  Neighbor count is %d\n", iface->nbr_cnt);
		}
		printf("  Uptime %s\n", fmt_timeframe_core(iface->uptime));
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
fmt_timeframe_core(time_t t)
{
	char		*buf;
	static char	 tfbuf[TF_BUFS][TF_LEN];	/* ring buffer */
	static int	 idx = 0;
	unsigned int	 sec, min, hrs, day;
	unsigned long long	week;

	if (t == 0)
		return ("00:00:00");

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

int
show_nbr_msg(struct imsg *imsg, struct parse_result *res)
{
	struct ctl_nbr	*nbr;
	const char	*addr;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_NBR:
		if (imsg->hdr.len < IMSG_HEADER_SIZE + sizeof(struct ctl_nbr))
			errx(1, "wrong imsg len");
		nbr = imsg->data;

		if (res->family != AF_UNSPEC && res->family != nbr->af)
			break;
		if (res->as != 0 && res->as != nbr->as)
			break;

		addr = log_addr(nbr->af, &nbr->addr);

		printf("%-4s %-5u %-18s", af_name(nbr->af), nbr->as, addr);
		if (strlen(addr) > 18)
			printf("\n%29s", " ");
		printf(" %-11s %-10u %8s\n", nbr->ifname, nbr->hello_holdtime,
		    fmt_timeframe_core(nbr->uptime));
		break;
	case IMSG_CTL_END:
		printf("\n");
		return (1);
	default:
		break;
	}

	return (0);
}

static int
connected_check(int af, union eigrpd_addr *addr)
{
	switch (af) {
	case AF_INET:
		if (addr->v4.s_addr == INADDR_ANY)
			return (1);
		break;
	case AF_INET6:
		if (IN6_IS_ADDR_UNSPECIFIED(&addr->v6))
			return (1);
		break;
	default:
		break;
	}

	return (0);
}

int
show_topology_msg(struct imsg *imsg, struct parse_result *res)
{
	struct ctl_rt	*rt;
	char		*dstnet, *nexthop, *rdistance;
	char		 flag;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_TOPOLOGY:
		if (imsg->hdr.len < IMSG_HEADER_SIZE + sizeof(struct ctl_rt))
			errx(1, "wrong imsg len");
		rt = imsg->data;

		if (res->family != AF_UNSPEC && res->family != rt->af)
			break;
		if (res->as != 0 && res->as != rt->as)
			break;

		if (rt->state & DUAL_STA_ACTIVE_ALL)
			flag = 'A';
		else if (rt->flags & F_CTL_RT_SUCCESSOR)
			flag = 'S';
		else if (rt->flags & F_CTL_RT_FSUCCESSOR)
			flag = 'F';
		else
			flag = ' ';

		if (asprintf(&dstnet, "%s/%d", log_addr(rt->af, &rt->prefix),
		    rt->prefixlen) == -1)
			err(1, NULL);

		if (connected_check(rt->af, &rt->nexthop)) {
			if (asprintf(&nexthop, "Connected") == -1)
				err(1, NULL);
			if (asprintf(&rdistance, "-") == -1)
				err(1, NULL);
		} else {
			if (asprintf(&nexthop, "%s", log_addr(rt->af,
			    &rt->nexthop)) == -1)
				err(1, NULL);
			if (asprintf(&rdistance, "%u", rt->rdistance) == -1)
				err(1, NULL);
		}

		printf("%c %-4s %-5u %-18s", flag, af_name(rt->af), rt->as,
		    dstnet);
		if (strlen(dstnet) > 18)
			printf("\n%31s", " ");
		printf(" %-15s", nexthop);
		if (strlen(nexthop) > 15)
			printf("\n%47s", " ");
		printf(" %-12s %u/%s\n", rt->ifname, rt->distance, rdistance);
		free(dstnet);
		free(nexthop);
		free(rdistance);
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
show_topology_detail_msg(struct imsg *imsg, struct parse_result *res)
{
	struct ctl_rt	*rt;
	char		*dstnet = NULL, *state = NULL, *type, *nexthop;
	struct in_addr	 addr;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_TOPOLOGY:
		if (imsg->hdr.len < IMSG_HEADER_SIZE + sizeof(struct ctl_rt))
			errx(1, "wrong imsg len");
		rt = imsg->data;

		if (res->family != AF_UNSPEC && res->family != rt->af)
			break;
		if (res->as != 0 && res->as != rt->as)
			break;

		if (rt->flags & F_CTL_RT_FIRST) {
			if (asprintf(&dstnet, "%s/%d", log_addr(rt->af,
			    &rt->prefix), rt->prefixlen) == -1)
				err(1, NULL);

			if (rt->state & DUAL_STA_ACTIVE_ALL) {
				if (asprintf(&state, "Active") == -1)
					err(1, NULL);
			} else {
				if (asprintf(&state, "Passive") == -1)
					err(1, NULL);
			}
		}

		if (rt->type == EIGRP_ROUTE_INTERNAL) {
			if (asprintf(&type, "Internal") == -1)
				err(1, NULL);
		} else {
			if (asprintf(&type, "External") == -1)
				err(1, NULL);
		}

		if (connected_check(rt->af, &rt->nexthop)) {
			if (asprintf(&nexthop, "Connected") == -1)
				err(1, NULL);
		} else {
			if (asprintf(&nexthop, "Neighbor %s", log_addr(rt->af,
			    &rt->nexthop)) == -1)
				err(1, NULL);
		}

		if (rt->flags & F_CTL_RT_FIRST) {
			printf("Network %s\n", dstnet);
			printf("Autonomous System %u, Address Family %s\n",
			    rt->as, af_name(rt->af));
			printf("DUAL State: %s, Feasible Distance: %u\n", state,
			    rt->fdistance);
			printf("Routes:\n");
		}
		printf("  Interface %s - %s\n", rt->ifname, nexthop);
		printf("    Distance: %u", rt->distance);
		if (!connected_check(rt->af, &rt->nexthop))
			printf(", Reported Distance: %u", rt->rdistance);
		printf(", route is %s\n", type);
		printf("    Vector metric:\n");
		printf("      Minimum bandwidth is %u Kbit\n",
		    rt->metric.bandwidth);
		printf("      Total delay is %u microseconds\n",
		    rt->metric.delay);
		printf("      Reliability is %u/255\n", rt->metric.reliability);
		printf("      Load is %u/255\n", rt->metric.load);
		printf("      Minimum MTU is %u\n", rt->metric.mtu);
		printf("      Hop count is %u\n", rt->metric.hop_count);
		if (rt->type == EIGRP_ROUTE_EXTERNAL) {
			addr.s_addr = htonl(rt->emetric.routerid);
			printf("    External data:\n");
			printf("      Originating router is %s\n",
			    inet_ntoa(addr));
			printf("      AS number of route is %u\n",
			    rt->emetric.as);
			printf("      External protocol is %s\n",
			    ext_proto_name(rt->emetric.protocol));
			printf("      External metric is %u\n",
			    rt->emetric.metric);
			printf("      Administrator tag is %u\n",
			    rt->emetric.tag);
		}

		printf("\n");
		free(dstnet);
		free(state);
		free(type);
		free(nexthop);
		break;
	case IMSG_CTL_END:
		return (1);
	default:
		break;
	}

	return (0);
}

void
show_fib_head(void)
{
	printf("flags: * = valid, D = EIGRP, C = Connected, S = Static\n");
	printf("%-6s %-4s %-20s %-17s\n", "Flags", "Prio", "Destination",
	    "Nexthop");
}

int
show_fib_msg(struct imsg *imsg, struct parse_result *res)
{
	struct kroute		*k;
	char			*p;

	switch (imsg->hdr.type) {
	case IMSG_CTL_KROUTE:
		if (imsg->hdr.len < IMSG_HEADER_SIZE + sizeof(struct kroute))
			errx(1, "wrong imsg len");
		k = imsg->data;

		if (res->family != AF_UNSPEC && res->family != k->af)
			break;

		if (k->flags & F_DOWN)
			printf(" ");
		else
			printf("*");

		if (!(k->flags & F_KERNEL))
			printf("D");
		else if (k->flags & F_CONNECTED)
			printf("C");
		else if (k->flags & F_STATIC)
			printf("S");
		else
			printf(" ");

		printf("%-5s", (k->flags & F_CTL_EXTERNAL) ? " EX" : "");
		printf("%4d ", k->priority);
		if (asprintf(&p, "%s/%u", log_addr(k->af, &k->prefix),
		    k->prefixlen) == -1)
			err(1, NULL);
		printf("%-20s ", p);
		if (strlen(p) > 20)
			printf("\n%33s", " ");
		free(p);

		if (eigrp_addrisset(k->af, &k->nexthop)) {
			switch (k->af) {
			case AF_INET:
				printf("%s", log_addr(k->af, &k->nexthop));
				break;
			case AF_INET6:
				printf("%s", log_in6addr_scope(&k->nexthop.v6,
				    k->ifindex));
				break;
			default:
				break;
			}

		} else if (k->flags & F_CONNECTED)
			printf("link#%u", k->ifindex);
		printf("\n");

		break;
	case IMSG_CTL_END:
		printf("\n");
		return (1);
	default:
		break;
	}

	return (0);
}

void
show_interface_head(void)
{
	printf("%-15s%-15s%s\n", "Interface", "Flags",
	    "Link state");
}

const struct if_status_description
		if_status_descriptions[] = LINK_STATE_DESCRIPTIONS;
const struct ifmedia_description
		ifm_type_descriptions[] = IFM_TYPE_DESCRIPTIONS;

const char *
get_media_descr(uint64_t media_type)
{
	const struct ifmedia_description	*p;

	for (p = ifm_type_descriptions; p->ifmt_string != NULL; p++)
		if (media_type == p->ifmt_word)
			return (p->ifmt_string);

	return ("unknown");
}

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

void
print_baudrate(uint64_t baudrate)
{
	if (baudrate > IF_Gbps(1))
		printf("%llu GBit/s", baudrate / IF_Gbps(1));
	else if (baudrate > IF_Mbps(1))
		printf("%llu MBit/s", baudrate / IF_Mbps(1));
	else if (baudrate > IF_Kbps(1))
		printf("%llu KBit/s", baudrate / IF_Kbps(1));
	else
		printf("%llu Bit/s", baudrate);
}

int
show_fib_interface_msg(struct imsg *imsg)
{
	struct kif	*k;
	uint64_t	 ifms_type;

	switch (imsg->hdr.type) {
	case IMSG_CTL_IFINFO:
		if (imsg->hdr.len < IMSG_HEADER_SIZE + sizeof(struct kif))
			errx(1, "wrong imsg len");
		k = imsg->data;
		printf("%-15s", k->ifname);
		printf("%-15s", k->flags & IFF_UP ? "UP" : "");
		ifms_type = get_ifms_type(k->if_type);
		if (ifms_type)
			printf("%s, ", get_media_descr(ifms_type));

		printf("%s", get_linkstate(k->if_type, k->link_state));

		if (k->link_state != LINK_STATE_DOWN && k->baudrate > 0) {
			printf(", ");
			print_baudrate(k->baudrate);
		}
		printf("\n");
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
show_stats_msg(struct imsg *imsg, struct parse_result *res)
{
	struct ctl_stats	*cs;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_STATS:
		if (imsg->hdr.len < IMSG_HEADER_SIZE + sizeof(struct ctl_stats))
			errx(1, "wrong imsg len");
		cs = imsg->data;

		if (res->family != AF_UNSPEC && res->family != cs->af)
			break;
		if (res->as != 0 && res->as != cs->as)
			break;

		printf("Address Family %s, Autonomous System %u\n",
		    af_name(cs->af), cs->as);
		printf("  Hellos sent/received: %u/%u\n",
		    cs->stats.hellos_sent, cs->stats.hellos_recv);
		printf("  Updates sent/received: %u/%u\n",
		    cs->stats.updates_sent, cs->stats.updates_recv);
		printf("  Queries sent/received: %u/%u\n",
		    cs->stats.queries_sent, cs->stats.queries_recv);
		printf("  Replies sent/received: %u/%u\n",
		    cs->stats.replies_sent, cs->stats.replies_recv);
		printf("  Acks sent/received: %u/%u\n",
		    cs->stats.acks_sent, cs->stats.acks_recv);
		printf("  SIA-Queries sent/received: %u/%u\n",
		    cs->stats.squeries_sent, cs->stats.squeries_recv);
		printf("  SIA-Replies sent/received: %u/%u\n",
		    cs->stats.sreplies_sent, cs->stats.sreplies_recv);
		break;
	case IMSG_CTL_END:
		printf("\n");
		return (1);
	default:
		break;
	}

	return (0);
}
