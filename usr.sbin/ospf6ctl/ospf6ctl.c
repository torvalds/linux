/*	$OpenBSD: ospf6ctl.c,v 1.59 2024/11/21 13:38:14 claudio Exp $ */

/*
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

#include "ospf6.h"
#include "ospf6d.h"
#include "ospfe.h"
#include "parser.h"
#include "log.h"

__dead void	 usage(void);
int		 show_summary_msg(struct imsg *);
uint64_t	 get_ifms_type(uint8_t);
int		 show_interface_msg(struct imsg *);
int		 show_interface_detail_msg(struct imsg *);
const char	*print_link(int);
const char	*fmt_timeframe(time_t t);
const char	*fmt_timeframe_core(time_t t);
const char	*log_id(u_int32_t );
const char	*log_adv_rtr(u_int32_t);
void		 show_database_head(struct in_addr, char *, u_int16_t);
int		 show_database_msg(struct imsg *);
char		*print_ls_type(u_int16_t);
void		 show_db_hdr_msg_detail(struct lsa_hdr *);
char		*print_rtr_link_type(u_int8_t);
const char	*print_ospf_flags(u_int8_t);
const char	*print_asext_flags(u_int32_t);
const char	*print_prefix_opt(u_int8_t);
int		 show_db_msg_detail(struct imsg *imsg);
int		 show_nbr_msg(struct imsg *);
const char	*print_ospf_options(u_int32_t);
int		 show_nbr_detail_msg(struct imsg *);
int		 show_rib_msg(struct imsg *);
void		 show_rib_head(struct in_addr, u_int8_t, u_int8_t);
const char	*print_ospf_rtr_flags(u_int8_t);
int		 show_rib_detail_msg(struct imsg *);
void		 show_fib_head(void);
int		 show_fib_msg(struct imsg *);
const char *	 get_media_descr(uint64_t);
const char *	 get_linkstate(uint8_t, int);
void		 print_baudrate(u_int64_t);

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
	struct sockaddr_un	 sun;
	struct parse_result	*res;
	struct imsg		 imsg;
	unsigned int		 ifidx = 0;
	int			 ctl_sock, r;
	int			 done = 0, verbose = 0;
	int			 n;
	int			 ch;
	char			*sockname;

	r = getrtable();
	if (asprintf(&sockname, "%s.%d", OSPF6D_SOCKET, r) == -1)
		err(1, "asprintf");

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

	/* connect to ospf6d control socket */
	if ((ctl_sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "socket");

	bzero(&sun, sizeof(sun));
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
	case SHOW_SUM:
		imsg_compose(ibuf, IMSG_CTL_SHOW_SUM, 0, 0, -1, NULL, 0);
		break;
	case SHOW_IFACE:
		printf("%-11s %-29s %-6s %-10s %-10s %-8s\n",
		    "Interface", "Address", "State", "HelloTimer", "Linkstate",
		    "Uptime");
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
		printf("%-15s %-3s %-12s %-9s %-11s %s\n", "ID", "Pri",
		    "State", "DeadTime", "Iface","Uptime");
		/*FALLTHROUGH*/
	case SHOW_NBR_DTAIL:
		imsg_compose(ibuf, IMSG_CTL_SHOW_NBR, 0, 0, -1, NULL, 0);
		break;
	case SHOW_DB:
		imsg_compose(ibuf, IMSG_CTL_SHOW_DATABASE, 0, 0, -1, NULL, 0);
		break;
	case SHOW_DBBYAREA:
		imsg_compose(ibuf, IMSG_CTL_SHOW_DATABASE, 0, 0, -1,
		    &res->area, sizeof(res->area));
		break;
	case SHOW_DBEXT:
		imsg_compose(ibuf, IMSG_CTL_SHOW_DB_EXT, 0, 0, -1, NULL, 0);
		break;
	case SHOW_DBLINK:
		imsg_compose(ibuf, IMSG_CTL_SHOW_DB_LINK, 0, 0, -1, NULL, 0);
		break;
	case SHOW_DBNET:
		imsg_compose(ibuf, IMSG_CTL_SHOW_DB_NET, 0, 0, -1, NULL, 0);
		break;
	case SHOW_DBRTR:
		imsg_compose(ibuf, IMSG_CTL_SHOW_DB_RTR, 0, 0, -1, NULL, 0);
		break;
	case SHOW_DBINTRA:
		imsg_compose(ibuf, IMSG_CTL_SHOW_DB_INTRA, 0, 0, -1, NULL, 0);
		break;
	case SHOW_DBSELF:
		imsg_compose(ibuf, IMSG_CTL_SHOW_DB_SELF, 0, 0, -1, NULL, 0);
		break;
	case SHOW_DBSUM:
		imsg_compose(ibuf, IMSG_CTL_SHOW_DB_SUM, 0, 0, -1, NULL, 0);
		break;
	case SHOW_DBASBR:
		imsg_compose(ibuf, IMSG_CTL_SHOW_DB_ASBR, 0, 0, -1, NULL, 0);
		break;
	case SHOW_RIB:
		printf("%-20s %-17s %-12s %-9s %-7s %-8s\n", "Destination",
		    "Nexthop", "Path Type", "Type", "Cost", "Uptime");
		/*FALLTHROUGH*/
	case SHOW_RIB_DTAIL:
		imsg_compose(ibuf, IMSG_CTL_SHOW_RIB, 0, 0, -1, NULL, 0);
		break;
	case SHOW_FIB:
		if (IN6_IS_ADDR_UNSPECIFIED(&res->addr))
			imsg_compose(ibuf, IMSG_CTL_KROUTE, 0, 0, -1,
			    &res->flags, sizeof(res->flags));
		else
			imsg_compose(ibuf, IMSG_CTL_KROUTE_ADDR, 0, 0, -1,
			    &res->addr, sizeof(res->addr));
		show_fib_head();
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
	case FIB_RELOAD:
		imsg_compose(ibuf, IMSG_CTL_FIB_RELOAD, 0, 0, -1, NULL, 0);
		printf("reload request sent.\n");
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
#ifdef notyet
		imsg_compose(ibuf, IMSG_CTL_RELOAD, 0, 0, -1, NULL, 0);
		printf("reload request sent.\n");
		done = 1;
		break;
#else
		errx(1, "reload not supported");
#endif
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
			case SHOW_NBR:
				done = show_nbr_msg(&imsg);
				break;
			case SHOW_NBR_DTAIL:
				done = show_nbr_detail_msg(&imsg);
				break;
			case SHOW_DB:
			case SHOW_DBBYAREA:
			case SHOW_DBSELF:
				done = show_database_msg(&imsg);
				break;
			case SHOW_DBEXT:
			case SHOW_DBLINK:
			case SHOW_DBNET:
			case SHOW_DBRTR:
			case SHOW_DBINTRA:
			case SHOW_DBSUM:
			case SHOW_DBASBR:
				done = show_db_msg_detail(&imsg);
				break;
			case SHOW_RIB:
				done = show_rib_msg(&imsg);
				break;
			case SHOW_RIB_DTAIL:
				done = show_rib_detail_msg(&imsg);
				break;
			case SHOW_FIB:
				done = show_fib_msg(&imsg);
				break;
			case NONE:
			case FIB:
			case FIB_COUPLE:
			case FIB_DECOUPLE:
			case FIB_RELOAD:
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
	struct ctl_sum_area	*sumarea;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_SUM:
		sum = imsg->data;
		printf("Router ID: %s\n", inet_ntoa(sum->rtr_id));
		printf("Uptime: %s\n", fmt_timeframe_core(sum->uptime));

		printf("SPF delay is %d sec(s), hold time between two SPFs "
		    "is %d sec(s)\n", sum->spf_delay, sum->spf_hold_time);
		printf("Number of external LSA(s) %d\n", sum->num_ext_lsa);
		printf("Number of areas attached to this router: %d\n",
		    sum->num_area);
		break;
	case IMSG_CTL_SHOW_SUM_AREA:
		sumarea = imsg->data;
		printf("\nArea ID: %s\n", inet_ntoa(sumarea->area));
		printf("  Number of interfaces in this area: %d\n",
		    sumarea->num_iface);
		printf("  Number of fully adjacent neighbors in this "
		    "area: %d\n", sumarea->num_adj_nbr);
		printf("  SPF algorithm executed %d time(s)\n",
		    sumarea->num_spf_calc);
		printf("  Number LSA(s) %d\n", sumarea->num_lsa);
		break;
	case IMSG_CTL_END:
		printf("\n");
		return (1);
	default:
		break;
	}

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
show_interface_msg(struct imsg *imsg)
{
	struct ctl_iface	*iface;
	char			*netid;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_INTERFACE:
		iface = imsg->data;

		if (asprintf(&netid, "%s", log_in6addr(&iface->addr)) == -1)
			err(1, NULL);
		printf("%-11s %-29s %-6s %-10s %-10s %s\n",
		    iface->name, netid, if_state_name(iface->state),
		    iface->hello_timer < 0 ? "-" :
		    fmt_timeframe_core(iface->hello_timer),
		    get_linkstate(iface->if_type, iface->linkstate),
		    fmt_timeframe_core(iface->uptime));
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
	case IMSG_CTL_SHOW_INTERFACE:
		iface = imsg->data;
		printf("\n");
		printf("Interface %s, line protocol is %s\n",
		    iface->name, print_link(iface->flags));
		printf("  Internet address %s Area %s\n",
		    log_in6addr(&iface->addr), inet_ntoa(iface->area));
		printf("  Link type %s, state %s, mtu %d",
		    get_media_descr(get_ifms_type(iface->if_type)),
		    get_linkstate(iface->if_type, iface->linkstate),
		    iface->mtu);
		if (iface->linkstate != LINK_STATE_DOWN &&
		    iface->baudrate > 0) {
		    printf(", ");
		    print_baudrate(iface->baudrate);
		}
		printf("\n");
		printf("  Router ID %s, network type %s, cost: %d\n",
		    inet_ntoa(iface->rtr_id),
		    if_type_name(iface->type), iface->metric);
		printf("  Transmit delay is %d sec(s), state %s, priority %d\n",
		    iface->transmit_delay, if_state_name(iface->state),
		    iface->priority);
		printf("  Designated Router (ID) %s\n",
		    inet_ntoa(iface->dr_id));
		printf("    Interface address %s\n",
		    log_in6addr(&iface->dr_addr));
		printf("  Backup Designated Router (ID) %s\n",
		    inet_ntoa(iface->bdr_id));
		printf("    Interface address %s\n",
		    log_in6addr(&iface->bdr_addr));
		printf("  Timer intervals configured, "
		    "hello %d, dead %d, wait %d, retransmit %d\n",
		     iface->hello_interval, iface->dead_interval,
		     iface->dead_interval, iface->rxmt_interval);
		if (iface->passive)
			printf("    Passive interface (No Hellos)\n");
		else if (iface->hello_timer < 0)
			printf("    Hello timer not running\n");
		else
			printf("    Hello timer due in %s\n",
			    fmt_timeframe_core(iface->hello_timer));
		printf("    Uptime %s\n", fmt_timeframe_core(iface->uptime));
		printf("  Neighbor count is %d, adjacent neighbor count is "
		    "%d\n", iface->nbr_cnt, iface->adj_cnt);
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
	unsigned int	 sec, min, hrs, day, week;

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
		snprintf(buf, TF_LEN, "%02uw%01ud%02uh", week, day, hrs);
	else if (day > 0)
		snprintf(buf, TF_LEN, "%01ud%02uh%02um", day, hrs, min);
	else
		snprintf(buf, TF_LEN, "%02u:%02u:%02u", hrs, min, sec);

	return (buf);
}

const char *
log_id(u_int32_t id)
{
	static char	buf[48];
	struct in_addr	addr;

	addr.s_addr = id;

	if (inet_ntop(AF_INET, &addr, buf, sizeof(buf)) == NULL)
		return ("?");
	else
		return (buf);
}

const char *
log_adv_rtr(u_int32_t adv_rtr)
{
	static char	buf[48];
	struct in_addr	addr;

	addr.s_addr = adv_rtr;

	if (inet_ntop(AF_INET, &addr, buf, sizeof(buf)) == NULL)
		return ("?");
	else
		return (buf);
}

void
show_database_head(struct in_addr aid, char *ifname, u_int16_t type)
{
	char	*header, *format;
	int	cleanup = 0;

	switch (ntohs(type)) {
	case LSA_TYPE_LINK:
		format = "Link (Type-8) Link States";
		break;
	case LSA_TYPE_ROUTER:
		format = "Router Link States";
		break;
	case LSA_TYPE_NETWORK:
		format = "Net Link States";
		break;
	case LSA_TYPE_INTER_A_PREFIX:
		format = "Inter Area Prefix Link States";
		break;
	case LSA_TYPE_INTER_A_ROUTER:
		format = "Inter Area Router Link States";
		break;
	case LSA_TYPE_INTRA_A_PREFIX:
		format = "Intra Area Prefix Link States";
		break;
	case LSA_TYPE_EXTERNAL:
		printf("\n%-15s %s\n\n", "", "Type-5 AS External Link States");
		return;
	default:
		if (asprintf(&format, "LSA type %x", ntohs(type)) == -1)
			err(1, NULL);
		cleanup = 1;
		break;
	}
	if (LSA_IS_SCOPE_AREA(ntohs(type))) {
		if (asprintf(&header, "%s (Area %s)", format,
		    inet_ntoa(aid)) == -1)
			err(1, NULL);
	} else if (LSA_IS_SCOPE_LLOCAL(ntohs(type))) {
		if (asprintf(&header, "%s (Area %s Interface %s)", format,
		    inet_ntoa(aid), ifname) == -1)
			err(1, NULL);
	} else {
		if (asprintf(&header, "%s", format) == -1)
			err(1, NULL);
	}

	printf("\n%-15s %s\n\n", "", header);
	free(header);
	if (cleanup)
		free(format);
}

int
show_database_msg(struct imsg *imsg)
{
	static struct in_addr	 area_id;
	static char		 ifname[IF_NAMESIZE];
	static u_int16_t	 lasttype;
	struct area		*area;
	struct iface		*iface;
	struct lsa_hdr		*lsa;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_DATABASE:
	case IMSG_CTL_SHOW_DB_SELF:
		lsa = imsg->data;
		if (lsa->type != lasttype) {
			show_database_head(area_id, ifname, lsa->type);
			printf("%-15s %-15s %-4s %-10s %-8s\n", "Link ID",
			    "Adv Router", "Age", "Seq#", "Checksum");
		}
		printf("%-15s %-15s %-4d 0x%08x 0x%04x\n",
		    log_id(lsa->ls_id), log_adv_rtr(lsa->adv_rtr),
		    ntohs(lsa->age), ntohl(lsa->seq_num),
		    ntohs(lsa->ls_chksum));
		lasttype = lsa->type;
		break;
	case IMSG_CTL_AREA:
		area = imsg->data;
		area_id = area->id;
		lasttype = 0;
		break;
	case IMSG_CTL_IFACE:
		iface = imsg->data;
		strlcpy(ifname, iface->name, sizeof(ifname));
		lasttype = 0;
		break;
	case IMSG_CTL_END:
		printf("\n");
		return (1);
	default:
		break;
	}

	return (0);
}

char *
print_ls_type(u_int16_t type)
{
	switch (ntohs(type)) {
	case LSA_TYPE_LINK:
		return ("Link");
	case LSA_TYPE_ROUTER:
		return ("Router");
	case LSA_TYPE_NETWORK:
		return ("Network");
	case LSA_TYPE_INTER_A_PREFIX:
		return ("Inter Area (Prefix)");
	case LSA_TYPE_INTER_A_ROUTER:
		return ("Inter Area (Router)");
	case LSA_TYPE_INTRA_A_PREFIX:
		return ("Intra Area (Prefix)");
	case LSA_TYPE_EXTERNAL:
		return ("AS External");
	default:
		return ("Unknown");
	}
}

void
show_db_hdr_msg_detail(struct lsa_hdr *lsa)
{
	printf("LS age: %d\n", ntohs(lsa->age));
	printf("LS Type: %s\n", print_ls_type(lsa->type));

	switch (ntohs(lsa->type)) {
	case LSA_TYPE_ROUTER:
	case LSA_TYPE_INTER_A_PREFIX:
	case LSA_TYPE_INTER_A_ROUTER:
	case LSA_TYPE_INTRA_A_PREFIX:
	case LSA_TYPE_EXTERNAL:
		printf("Link State ID: %s\n", log_id(lsa->ls_id));
		break;
	case LSA_TYPE_LINK:
		printf("Link State ID: %s (Interface ID of Advertising "
		    "Router)\n", log_id(lsa->ls_id));
		break;
	case LSA_TYPE_NETWORK:
		printf("Link State ID: %s (Interface ID of Designated "
		    "Router)\n", log_id(lsa->ls_id));
		break;
	}

	printf("Advertising Router: %s\n", log_adv_rtr(lsa->adv_rtr));
	printf("LS Seq Number: 0x%08x\n", ntohl(lsa->seq_num));
	printf("Checksum: 0x%04x\n", ntohs(lsa->ls_chksum));
	printf("Length: %d\n", ntohs(lsa->len));
}

char *
print_rtr_link_type(u_int8_t type)
{
	switch (type) {
	case LINK_TYPE_POINTTOPOINT:
		return ("Point-to-Point");
	case LINK_TYPE_TRANSIT_NET:
		return ("Transit Network");
	case LINK_TYPE_RESERVED:
		return ("Reserved");
	case LINK_TYPE_VIRTUAL:
		return ("Virtual Link");
	default:
		return ("Unknown");
	}
}

const char *
print_ospf_flags(u_int8_t opts)
{
	static char	optbuf[32];

	snprintf(optbuf, sizeof(optbuf), "*|*|*|*|*|%s|%s|%s",
	    opts & OSPF_RTR_V ? "V" : "-",
	    opts & OSPF_RTR_E ? "E" : "-",
	    opts & OSPF_RTR_B ? "B" : "-");
	return (optbuf);
}

const char *
print_asext_flags(u_int32_t opts)
{
	static char	optbuf[32];

	snprintf(optbuf, sizeof(optbuf), "*|*|*|*|*|%s|%s|%s",
	    opts & LSA_ASEXT_E_FLAG ? "E" : "-",
	    opts & LSA_ASEXT_F_FLAG ? "F" : "-",
	    opts & LSA_ASEXT_T_FLAG ? "T" : "-");
	return (optbuf);
}

const char *
print_prefix_opt(u_int8_t opts)
{
	static char	optbuf[32];

	if (opts) {
		snprintf(optbuf, sizeof(optbuf),
		    " Options: *|*|*|%s|%s|x|%s|%s",
		    opts & OSPF_PREFIX_DN ? "DN" : "-",
		    opts & OSPF_PREFIX_P ? "P" : "-",
		    opts & OSPF_PREFIX_LA ? "LA" : "-",
		    opts & OSPF_PREFIX_NU ? "NU" : "-");
		return (optbuf);
	}
	return ("");
}

int
show_db_msg_detail(struct imsg *imsg)
{
	static struct in_addr	 area_id;
	static char		 ifname[IF_NAMESIZE];
	static u_int16_t	 lasttype;
	struct in6_addr		 ia6;
	struct in_addr		 addr, data;
	struct area		*area;
	struct iface		*iface;
	struct lsa		*lsa;
	struct lsa_rtr_link	*rtr_link;
	struct lsa_net_link	*net_link;
	struct lsa_prefix	*prefix;
	struct lsa_asext	*asext;
	u_int32_t		 ext_tag;
	u_int16_t		 i, nlinks, off;

	/* XXX sanity checks! */

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_DB_EXT:
		lsa = imsg->data;
		if (lsa->hdr.type != lasttype)
			show_database_head(area_id, ifname, lsa->hdr.type);
		show_db_hdr_msg_detail(&lsa->hdr);

		asext = (struct lsa_asext *)((char *)lsa + sizeof(lsa->hdr));

		printf("    Flags: %s\n",
		    print_asext_flags(ntohl(lsa->data.asext.metric)));
		printf("    Metric: %d Type: ", ntohl(asext->metric)
		    & LSA_METRIC_MASK);
		if (ntohl(lsa->data.asext.metric) & LSA_ASEXT_E_FLAG)
			printf("2\n");
		else
			printf("1\n");

		prefix = &asext->prefix;
		bzero(&ia6, sizeof(ia6));
		bcopy(prefix + 1, &ia6, LSA_PREFIXSIZE(prefix->prefixlen));
		printf("    Prefix: %s/%d%s\n", log_in6addr(&ia6),
		    prefix->prefixlen, print_prefix_opt(prefix->options));

		off = sizeof(*asext) + LSA_PREFIXSIZE(prefix->prefixlen);
		if (ntohl(lsa->data.asext.metric) & LSA_ASEXT_F_FLAG) {
			bcopy((char *)asext + off, &ia6, sizeof(ia6));
			printf("    Forwarding Address: %s\n",
			    log_in6addr(&ia6));
			off += sizeof(ia6);
		}
		if (ntohl(lsa->data.asext.metric) & LSA_ASEXT_T_FLAG) {
			bcopy((char *)asext + off, &ext_tag, sizeof(ext_tag));
			printf("    External Route Tag: %d\n", ntohl(ext_tag));
		}
		printf("\n");
		lasttype = lsa->hdr.type;
		break;
	case IMSG_CTL_SHOW_DB_LINK:
		lsa = imsg->data;
		if (lsa->hdr.type != lasttype)
			show_database_head(area_id, ifname, lsa->hdr.type);
		show_db_hdr_msg_detail(&lsa->hdr);
		printf("Options: %s\n", print_ospf_options(LSA_24_GETLO(
		    ntohl(lsa->data.link.opts))));
		printf("Link Local Address: %s\n",
		    log_in6addr(&lsa->data.link.lladdr));

		nlinks = ntohl(lsa->data.link.numprefix);
		printf("Number of Prefixes: %d\n", nlinks);
		off = sizeof(lsa->hdr) + sizeof(struct lsa_link);

		for (i = 0; i < nlinks; i++) {
			prefix = (struct lsa_prefix *)((char *)lsa + off);
			bzero(&ia6, sizeof(ia6));
			bcopy(prefix + 1, &ia6,
			    LSA_PREFIXSIZE(prefix->prefixlen));

			printf("    Prefix: %s/%d%s\n", log_in6addr(&ia6),
			    prefix->prefixlen,
			    print_prefix_opt(prefix->options));

			off += sizeof(struct lsa_prefix)
			    + LSA_PREFIXSIZE(prefix->prefixlen);
		}

		printf("\n");
		lasttype = lsa->hdr.type;
		break;
	case IMSG_CTL_SHOW_DB_NET:
		lsa = imsg->data;
		if (lsa->hdr.type != lasttype)
			show_database_head(area_id, ifname, lsa->hdr.type);
		show_db_hdr_msg_detail(&lsa->hdr);
		printf("Options: %s\n",
		    print_ospf_options(LSA_24_GETLO(ntohl(lsa->data.net.opts))));

		nlinks = (ntohs(lsa->hdr.len) - sizeof(struct lsa_hdr) -
		    sizeof(struct lsa_net)) / sizeof(struct lsa_net_link);
		net_link = (struct lsa_net_link *)((char *)lsa +
		    sizeof(lsa->hdr) + sizeof(lsa->data.net));
		printf("Number of Routers: %d\n", nlinks);

		for (i = 0; i < nlinks; i++) {
			addr.s_addr = net_link->att_rtr;
			printf("    Attached Router: %s\n", inet_ntoa(addr));
			net_link++;
		}

		printf("\n");
		lasttype = lsa->hdr.type;
		break;
	case IMSG_CTL_SHOW_DB_RTR:
		lsa = imsg->data;
		if (lsa->hdr.type != lasttype)
			show_database_head(area_id, ifname, lsa->hdr.type);
		show_db_hdr_msg_detail(&lsa->hdr);
		printf("Flags: %s\n",
		    print_ospf_flags(LSA_24_GETHI(ntohl(lsa->data.rtr.opts))));
		printf("Options: %s\n",
		    print_ospf_options(LSA_24_GETLO(ntohl(lsa->data.rtr.opts))));

		nlinks = (ntohs(lsa->hdr.len) - sizeof(struct lsa_hdr)
		    - sizeof(u_int32_t)) / sizeof(struct lsa_rtr_link);
		printf("Number of Links: %d\n\n", nlinks);

		off = sizeof(lsa->hdr) + sizeof(struct lsa_rtr);

		for (i = 0; i < nlinks; i++) {
			rtr_link = (struct lsa_rtr_link *)((char *)lsa + off);

			printf("    Link (Interface ID %s) connected to: %s\n",
			    log_id(rtr_link->iface_id),
			    print_rtr_link_type(rtr_link->type));

			addr.s_addr = rtr_link->nbr_rtr_id;
			data.s_addr = rtr_link->nbr_iface_id;

			switch (rtr_link->type) {
			case LINK_TYPE_POINTTOPOINT:
			case LINK_TYPE_VIRTUAL:
				printf("    Router ID: %s\n", inet_ntoa(addr));
				printf("    Interface ID: %s\n",
				    inet_ntoa(data));
				break;
			case LINK_TYPE_TRANSIT_NET:
				printf("    Designated Router ID: %s\n",
				    inet_ntoa(addr));
				printf("    DR Interface ID: %s\n",
				    inet_ntoa(data));
				break;
			default:
				printf("    Link ID (Unknown type %d): %s\n",
				    rtr_link->type, inet_ntoa(addr));
				printf("    Link Data (Unknown): %s\n",
				    inet_ntoa(data));
				break;
			}

			printf("    Metric: %d\n\n", ntohs(rtr_link->metric));

			off += sizeof(struct lsa_rtr_link);
		}

		lasttype = lsa->hdr.type;
		break;
	case IMSG_CTL_SHOW_DB_INTRA:
		lsa = imsg->data;
		if (lsa->hdr.type != lasttype)
			show_database_head(area_id, ifname, lsa->hdr.type);
		show_db_hdr_msg_detail(&lsa->hdr);
		printf("Referenced LS Type: %s\n",
		    print_ls_type(lsa->data.pref_intra.ref_type));
		addr.s_addr = lsa->data.pref_intra.ref_ls_id;
		printf("Referenced Link State ID: %s\n", inet_ntoa(addr));
		addr.s_addr = lsa->data.pref_intra.ref_adv_rtr;
		printf("Referenced Advertising Router: %s\n", inet_ntoa(addr));
		nlinks = ntohs(lsa->data.pref_intra.numprefix);
		printf("Number of Prefixes: %d\n", nlinks);

		off = sizeof(lsa->hdr) + sizeof(struct lsa_intra_prefix);

		for (i = 0; i < nlinks; i++) {
			prefix = (struct lsa_prefix *)((char *)lsa + off);
			bzero(&ia6, sizeof(ia6));
			bcopy(prefix + 1, &ia6,
			    LSA_PREFIXSIZE(prefix->prefixlen));

			printf("    Prefix: %s/%d%s Metric: %d\n",
			    log_in6addr(&ia6), prefix->prefixlen,
			    print_prefix_opt(prefix->options),
			    ntohs(prefix->metric));

			off += sizeof(struct lsa_prefix)
			    + LSA_PREFIXSIZE(prefix->prefixlen);
		}

		printf("\n");
		lasttype = lsa->hdr.type;
		break;
	case IMSG_CTL_SHOW_DB_SUM:
		lsa = imsg->data;
		if (lsa->hdr.type != lasttype)
			show_database_head(area_id, ifname, lsa->hdr.type);
		show_db_hdr_msg_detail(&lsa->hdr);
		printf("Prefix: XXX\n");
		printf("Metric: %d\n", ntohl(lsa->data.pref_sum.metric) &
		    LSA_METRIC_MASK);
		lasttype = lsa->hdr.type;
		break;
	case IMSG_CTL_SHOW_DB_ASBR:
		lsa = imsg->data;
		if (lsa->hdr.type != lasttype)
			show_database_head(area_id, ifname, lsa->hdr.type);
		show_db_hdr_msg_detail(&lsa->hdr);

		addr.s_addr = lsa->data.rtr_sum.dest_rtr_id;
		printf("Destination Router ID: %s\n", inet_ntoa(addr));
		printf("Options: %s\n",
		    print_ospf_options(ntohl(lsa->data.rtr_sum.opts)));
		printf("Metric: %d\n\n", ntohl(lsa->data.rtr_sum.metric) &
		    LSA_METRIC_MASK);
	case IMSG_CTL_AREA:
		area = imsg->data;
		area_id = area->id;
		lasttype = 0;
		break;
	case IMSG_CTL_IFACE:
		iface = imsg->data;
		strlcpy(ifname, iface->name, sizeof(ifname));
		lasttype = 0;
		break;
	case IMSG_CTL_END:
		return (1);
	default:
		break;
	}

	return (0);
}

int
show_nbr_msg(struct imsg *imsg)
{
	struct ctl_nbr	*nbr;
	char		*state;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_NBR:
		nbr = imsg->data;
		if (asprintf(&state, "%s/%s", nbr_state_name(nbr->nbr_state),
		    if_state_name(nbr->iface_state)) == -1)
			err(1, NULL);
		printf("%-15s %-3d %-12s %-10s", inet_ntoa(nbr->id),
		    nbr->priority, state, fmt_timeframe_core(nbr->dead_timer));
		printf("%-11s %s\n", nbr->name,
		    nbr->uptime == 0 ? "-" : fmt_timeframe_core(nbr->uptime));
		free(state);
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
print_ospf_options(u_int32_t opts)
{
	static char	optbuf[32];

	snprintf(optbuf, sizeof(optbuf), "*|*|%s|%s|%s|*|%s|%s",
	    opts & OSPF_OPTION_DC ? "DC" : "-",
	    opts & OSPF_OPTION_R ? "R" : "-",
	    opts & OSPF_OPTION_N ? "N" : "-",
	    opts & OSPF_OPTION_E ? "E" : "-",
	    opts & OSPF_OPTION_V6 ? "V6" : "-");
	return (optbuf);
}

int
show_nbr_detail_msg(struct imsg *imsg)
{
	struct ctl_nbr	*nbr;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_NBR:
		nbr = imsg->data;
		printf("\nNeighbor %s, ", inet_ntoa(nbr->id));
		printf("interface address %s\n", log_in6addr(&nbr->addr));
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
		switch (rt->d_type) {
		case DT_NET:
			if (asprintf(&dstnet, "%s/%d", log_in6addr(&rt->prefix),
			    rt->prefixlen) == -1)
				err(1, NULL);
			break;
		case DT_RTR:
			if (asprintf(&dstnet, "%s",
			    log_in6addr(&rt->prefix)) == -1)
				err(1, NULL);
			break;
		default:
			errx(1, "Invalid route type");
		}

		printf("%-20s %-16s%s %-12s %-9s %-7d %s\n", dstnet,
		    log_in6addr_scope(&rt->nexthop, rt->ifindex),
		    rt->connected ? "C" : " ", path_type_name(rt->p_type),
		    dst_type_name(rt->d_type), rt->cost,
		    rt->uptime == 0 ? "-" : fmt_timeframe_core(rt->uptime));
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

void
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

const char *
print_ospf_rtr_flags(u_int8_t opts)
{
	static char	optbuf[32];

	snprintf(optbuf, sizeof(optbuf), "%s%s%s",
	    opts & OSPF_RTR_E ? "AS" : "",
	    opts & OSPF_RTR_E && opts & OSPF_RTR_B ? "+" : "",
	    opts & OSPF_RTR_B ? "ABR" : "");
	return (optbuf);
}

int
show_rib_detail_msg(struct imsg *imsg)
{
	struct ctl_rt		*rt;
	char			*dstnet;
	static u_int8_t		 lasttype;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_RIB:
		rt = imsg->data;

		switch (rt->p_type) {
		case PT_INTRA_AREA:
		case PT_INTER_AREA:
			switch (rt->d_type) {
			case DT_NET:
				if (lasttype != RIB_NET)
					show_rib_head(rt->area, rt->d_type,
					     rt->p_type);
				if (asprintf(&dstnet, "%s/%d",
				    log_in6addr(&rt->prefix),
				    rt->prefixlen) == -1)
					err(1, NULL);
				lasttype = RIB_NET;
				break;
			case DT_RTR:
				if (lasttype != RIB_RTR)
					show_rib_head(rt->area, rt->d_type,
					     rt->p_type);
				if (asprintf(&dstnet, "%s",
				    log_in6addr(&rt->prefix)) == -1)
					err(1, NULL);
				lasttype = RIB_RTR;
				break;
			default:
				errx(1, "unknown route type");
			}
			printf("%-18s %-15s ", dstnet,
			    log_in6addr_scope(&rt->nexthop, rt->ifindex));
			printf("%-15s %-12s %-7d", inet_ntoa(rt->adv_rtr),
			    path_type_name(rt->p_type), rt->cost);
			free(dstnet);

			if (rt->d_type == DT_RTR)
				printf(" %-7s",
				    print_ospf_rtr_flags(rt->flags));

			printf("\n");
			break;
		case PT_TYPE1_EXT:
		case PT_TYPE2_EXT:
			if (lasttype != RIB_EXT)
				show_rib_head(rt->area, rt->d_type, rt->p_type);

			if (asprintf(&dstnet, "%s/%d",
			    log_in6addr(&rt->prefix), rt->prefixlen) == -1)
				err(1, NULL);

			printf("%-18s %-15s ", dstnet,
			    log_in6addr_scope(&rt->nexthop, rt->ifindex));
			printf("%-15s %-12s %-7d %-7d\n",
			    inet_ntoa(rt->adv_rtr), path_type_name(rt->p_type),
			    rt->cost, rt->cost2);
			free(dstnet);

			lasttype = RIB_EXT;
			break;
		default:
			errx(1, "unknown route type");
		}
		break;
	case IMSG_CTL_AREA:
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
show_fib_head(void)
{
	printf("flags: * = valid, O = OSPF, C = Connected, S = Static\n");
	printf("%-6s %-4s %-20s %-17s\n",
	    "Flags", "Prio", "Destination", "Nexthop");
}

int
show_fib_msg(struct imsg *imsg)
{
	struct kroute		*k;
	char			*p;

	switch (imsg->hdr.type) {
	case IMSG_CTL_KROUTE:
		if (imsg->hdr.len < IMSG_HEADER_SIZE + sizeof(struct kroute))
			errx(1, "wrong imsg len");
		k = imsg->data;

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
		if (asprintf(&p, "%s/%u", log_in6addr(&k->prefix),
		    k->prefixlen) == -1)
			err(1, NULL);
		printf("%-20s ", p);
		free(p);

		if (!IN6_IS_ADDR_UNSPECIFIED(&k->nexthop))
			printf("%s", log_in6addr_scope(&k->nexthop, k->scope));
		else if (k->flags & F_CONNECTED)
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
print_baudrate(u_int64_t baudrate)
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
