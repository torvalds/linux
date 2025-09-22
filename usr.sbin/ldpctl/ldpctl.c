/*	$OpenBSD: ldpctl.c,v 1.37 2024/11/21 13:38:14 claudio Exp $
 *
 * Copyright (c) 2009 Michele Marchetto <michele@openbsd.org>
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
#include <netmpls/mpls.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "ldp.h"
#include "ldpd.h"
#include "ldpe.h"
#include "log.h"
#include "parser.h"

__dead void	 usage(void);
const char	*fmt_timeframe_core(time_t);
const char	*get_linkstate(uint8_t, int);
int		 show_interface_msg(struct imsg *, struct parse_result *);
int		 show_discovery_msg(struct imsg *, struct parse_result *);
uint64_t	 get_ifms_type(uint8_t);
int		 show_lib_msg(struct imsg *, struct parse_result *);
int		 show_nbr_msg(struct imsg *, struct parse_result *);
void		 show_fib_head(void);
int		 show_fib_msg(struct imsg *, struct parse_result *);
void		 show_interface_head(void);
int		 show_fib_interface_msg(struct imsg *);
int		 show_l2vpn_pw_msg(struct imsg *);
int		 show_l2vpn_binding_msg(struct imsg *);
const char	*get_media_descr(uint64_t);
void		 print_baudrate(uint64_t);

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
	struct kroute		 kr;
	int			 ctl_sock;
	int			 done = 0, verbose = 0;
	int			 n;
	struct ctl_nbr		 nbr;

	/* parse options */
	if ((res = parse(argc - 1, argv + 1)) == NULL)
		exit(1);

	/* connect to ldpd control socket */
	if ((ctl_sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "socket");

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, LDPD_SOCKET, sizeof(sun.sun_path));
	if (connect(ctl_sock, (struct sockaddr *)&sun, sizeof(sun)) == -1)
		err(1, "connect: %s", LDPD_SOCKET);

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
		printf("%-4s %-11s %-6s %-10s %-8s %-12s %3s\n",
		    "AF", "Interface", "State", "Linkstate", "Uptime",
		    "Hello Timers", "ac");
		if (*res->ifname) {
			ifidx = if_nametoindex(res->ifname);
			if (ifidx == 0)
				errx(1, "no such interface %s", res->ifname);
		}
		imsg_compose(ibuf, IMSG_CTL_SHOW_INTERFACE, 0, 0, -1,
		    &ifidx, sizeof(ifidx));
		break;
	case SHOW_DISC:
		printf("%-4s %-15s %-8s %-15s %9s\n",
		    "AF", "ID", "Type", "Source", "Holdtime");
		imsg_compose(ibuf, IMSG_CTL_SHOW_DISCOVERY, 0, 0, -1,
		    NULL, 0);
		break;
	case SHOW_NBR:
		printf("%-4s %-15s %-11s %-15s %8s\n",
		    "AF", "ID", "State", "Remote Address", "Uptime");
		imsg_compose(ibuf, IMSG_CTL_SHOW_NBR, 0, 0, -1, NULL, 0);
		break;
	case SHOW_LIB:
		printf("%-4s %-20s %-15s %-11s %-13s %6s\n", "AF",
		    "Destination", "Nexthop", "Local Label", "Remote Label",
		    "In Use");
		imsg_compose(ibuf, IMSG_CTL_SHOW_LIB, 0, 0, -1, NULL, 0);
		break;
	case SHOW_FIB:
		if (!ldp_addrisset(res->family, &res->addr))
			imsg_compose(ibuf, IMSG_CTL_KROUTE, 0, 0, -1,
			    &res->flags, sizeof(res->flags));
		else {
			memset(&kr, 0, sizeof(kr));
			kr.af = res->family;
			kr.prefix = res->addr;
			imsg_compose(ibuf, IMSG_CTL_KROUTE_ADDR, 0, 0, -1,
			    &kr, sizeof(kr));
		}
		show_fib_head();
		break;
	case SHOW_FIB_IFACE:
		if (*res->ifname)
			imsg_compose(ibuf, IMSG_CTL_IFINFO, 0, 0, -1,
			    res->ifname, sizeof(res->ifname));
		else
			imsg_compose(ibuf, IMSG_CTL_IFINFO, 0, 0, -1, NULL, 0);
		show_interface_head();
		break;
	case SHOW_L2VPN_PW:
		printf("%-11s %-15s %-14s %-10s\n",
		    "Interface", "Neighbor", "PWID", "Status");
		imsg_compose(ibuf, IMSG_CTL_SHOW_L2VPN_PW, 0, 0, -1, NULL, 0);
		break;
	case SHOW_L2VPN_BINDING:
		imsg_compose(ibuf, IMSG_CTL_SHOW_L2VPN_BINDING, 0, 0, -1,
		    NULL, 0);
		break;
	case CLEAR_NBR:
		memset(&nbr, 0, sizeof(nbr));
		nbr.af = res->family;
		memcpy(&nbr.raddr, &res->addr, sizeof(nbr.raddr));
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
			case SHOW_DISC:
				done = show_discovery_msg(&imsg, res);
				break;
			case SHOW_NBR:
				done = show_nbr_msg(&imsg, res);
				break;
			case SHOW_LIB:
				done = show_lib_msg(&imsg, res);
				break;
			case SHOW_FIB:
				done = show_fib_msg(&imsg, res);
				break;
			case SHOW_FIB_IFACE:
				done = show_fib_interface_msg(&imsg);
				break;
			case SHOW_L2VPN_PW:
				done = show_l2vpn_pw_msg(&imsg);
				break;
			case SHOW_L2VPN_BINDING:
				done = show_l2vpn_binding_msg(&imsg);
				break;
			case NONE:
			case CLEAR_NBR:
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
		break;
	case IFT_FDDI:
		return (IFM_FDDI);
		break;
	case IFT_CARP:
		return (IFM_CARP);
		break;
	default:
		return (0);
		break;
	}
}

#define	TF_BUFS	8
#define	TF_LEN	9

const char *
fmt_timeframe_core(time_t t)
{
	char		*buf;
	static char	 tfbuf[TF_BUFS][TF_LEN];	/* ring buffer */
	static int	 idx = 0;
	unsigned int	 sec, min, hrs, day, week;

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
		snprintf(buf, TF_LEN, "%02uw%01ud%02uh", week, day, hrs);
	else if (day > 0)
		snprintf(buf, TF_LEN, "%01ud%02uh%02um", day, hrs, min);
	else
		snprintf(buf, TF_LEN, "%02u:%02u:%02u", hrs, min, sec);

	return (buf);
}

int
show_interface_msg(struct imsg *imsg, struct parse_result *res)
{
	struct ctl_iface	*iface;
	char			*timers;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_INTERFACE:
		iface = imsg->data;

		if (res->family != AF_UNSPEC && res->family != iface->af)
			break;

		if (asprintf(&timers, "%u/%u", iface->hello_interval,
		    iface->hello_holdtime) == -1)
			err(1, NULL);

		printf("%-4s %-11s %-6s %-10s %-8s %-12s %3u\n",
		    af_name(iface->af), iface->name,
		    if_state_name(iface->state), get_linkstate(iface->if_type,
		    iface->linkstate), iface->uptime == 0 ? "00:00:00" :
		    fmt_timeframe_core(iface->uptime), timers, iface->adj_cnt);
		free(timers);
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
show_discovery_msg(struct imsg *imsg, struct parse_result *res)
{
	struct ctl_adj	*adj;
	const char	*addr;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_DISCOVERY:
		adj = imsg->data;

		if (res->family != AF_UNSPEC && res->family != adj->af)
			break;

		printf("%-4s %-15s ", af_name(adj->af), inet_ntoa(adj->id));
		switch(adj->type) {
		case HELLO_LINK:
			printf("%-8s %-15s ", "Link", adj->ifname);
			break;
		case HELLO_TARGETED:
			addr = log_addr(adj->af, &adj->src_addr);

			printf("%-8s %-15s ", "Targeted", addr);
			if (strlen(addr) > 15)
				printf("\n%46s", " ");
			break;
		}
		printf("%9u\n", adj->holdtime);
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
show_lib_msg(struct imsg *imsg, struct parse_result *res)
{
	struct ctl_rt	*rt;
	char		*dstnet;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_LIB:
		rt = imsg->data;

		if (res->family != AF_UNSPEC && res->family != rt->af)
			break;

		if (asprintf(&dstnet, "%s/%d", log_addr(rt->af, &rt->prefix),
		    rt->prefixlen) == -1)
			err(1, NULL);

		printf("%-4s %-20s", af_name(rt->af), dstnet);
		if (strlen(dstnet) > 20)
			printf("\n%25s", " ");
		printf(" %-15s %-11s %-13s %6s\n", inet_ntoa(rt->nexthop),
		    log_label(rt->local_label), log_label(rt->remote_label),
		    rt->in_use ? "yes" : "no");

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
show_nbr_msg(struct imsg *imsg, struct parse_result *res)
{
	struct ctl_nbr	*nbr;
	const char	*addr;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_NBR:
		nbr = imsg->data;

		if (res->family != AF_UNSPEC && res->family != nbr->af)
			break;

		addr = log_addr(nbr->af, &nbr->raddr);

		printf("%-4s %-15s %-11s %-15s",
		    af_name(nbr->af), inet_ntoa(nbr->id),
		    nbr_state_name(nbr->nbr_state), addr);
		if (strlen(addr) > 15)
			printf("\n%48s", " ");
		printf(" %8s\n", nbr->uptime == 0 ? "-" :
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

void
show_fib_head(void)
{
	printf("Flags: C = Connected, S = Static\n");
	printf(" %-4s %-20s %-17s %-17s %s\n", "Prio", "Destination",
	    "Nexthop", "Local Label", "Remote Label");
}

int
show_fib_msg(struct imsg *imsg, struct parse_result *res)
{
	struct kroute	*k;
	char		*p;
	const char	*nexthop;

	switch (imsg->hdr.type) {
	case IMSG_CTL_KROUTE:
		if (imsg->hdr.len < IMSG_HEADER_SIZE + sizeof(struct kroute))
			errx(1, "wrong imsg len");
		k = imsg->data;

		if (res->family != AF_UNSPEC && res->family != k->af)
			break;

		if (k->flags & F_CONNECTED)
			printf("C");
		else if (k->flags & F_STATIC)
			printf("S");
		else
			printf(" ");

		printf(" %3d ", k->priority);
		if (asprintf(&p, "%s/%u", log_addr(k->af, &k->prefix),
		    k->prefixlen) == -1)
			err(1, NULL);
		printf("%-20s ", p);
		if (strlen(p) > 20)
			printf("\n%27s", " ");
		free(p);

		if (ldp_addrisset(k->af, &k->nexthop)) {
			switch (k->af) {
			case AF_INET:
				printf("%-18s", inet_ntoa(k->nexthop.v4));
				break;
			case AF_INET6:
				nexthop = log_in6addr_scope(&k->nexthop.v6,
				    k->ifindex);
				printf("%-18s", nexthop);
				if (strlen(nexthop) > 18)
					printf("\n%45s", " ");
				break;
			default:
				printf("%-18s", " ");
				break;
			}
		} else if (k->flags & F_CONNECTED)
			printf("link#%-13u", k->ifindex);

		printf("%-18s", log_label(k->local_label));
		printf("%s", log_label(k->remote_label));
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

int
show_fib_interface_msg(struct imsg *imsg)
{
	struct kif	*k;
	uint64_t	 ifms_type;

	switch (imsg->hdr.type) {
	case IMSG_CTL_IFINFO:
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
show_l2vpn_pw_msg(struct imsg *imsg)
{
	struct ctl_pw	*pw;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_L2VPN_PW:
		pw = imsg->data;

		printf("%-11s %-15s %-14u %-10s\n", pw->ifname,
		    inet_ntoa(pw->lsr_id), pw->pwid,
		    (pw->status ? "UP" : "DOWN"));
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
show_l2vpn_binding_msg(struct imsg *imsg)
{
	struct ctl_pw	*pw;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_L2VPN_BINDING:
		pw = imsg->data;

		printf("Neighbor: %s - PWID: %u (%s)\n",
		    inet_ntoa(pw->lsr_id), pw->pwid,
		    pw_type_name(pw->type));
		printf("%-12s%-15s%-15s%-10s\n", "", "Label", "Group-ID",
		    "MTU");
		if (pw->local_label != NO_LABEL)
			printf("  %-10s%-15u%-15u%u\n", "Local",
			    pw->local_label, pw->local_gid, pw->local_ifmtu);
		else
			printf("  %-10s%-15s%-15s%s\n", "Local", "-",
			    "-", "-");
		if (pw->remote_label != NO_LABEL)
			printf("  %-10s%-15u%-15u%u\n", "Remote",
			    pw->remote_label, pw->remote_gid,
			    pw->remote_ifmtu);
		else
			printf("  %-10s%-15s%-15s%s\n", "Remote", "-",
			    "-", "-");
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
