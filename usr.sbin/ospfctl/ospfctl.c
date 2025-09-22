/*	$OpenBSD: ospfctl.c,v 1.73 2024/11/21 13:38:14 claudio Exp $ */

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

#include "ospf.h"
#include "ospfd.h"
#include "ospfctl.h"
#include "ospfe.h"
#include "parser.h"

__dead void	 usage(void);

int show(struct imsg *, struct parse_result *);

struct imsgbuf		*ibuf;
const struct output	*output = &show_output;

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
	int			 done = 0;
	int			 n, verbose = 0;
	int			 ch;
	char			*sockname;

	r = getrtable();
	if (asprintf(&sockname, "%s.%d", OSPFD_SOCKET, r) == -1)
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

	/* connect to ospfd control socket */
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
	case SHOW_NBR_DTAIL:
		imsg_compose(ibuf, IMSG_CTL_SHOW_NBR, 0, 0, -1, NULL, 0);
		break;
	case SHOW_DB:
		imsg_compose(ibuf, IMSG_CTL_SHOW_DATABASE, 0, 0, -1, NULL, 0);
		break;
	case SHOW_DBBYAREA:
		imsg_compose(ibuf, IMSG_CTL_SHOW_DATABASE, 0, 0, -1,
		    &res->addr, sizeof(res->addr));
		break;
	case SHOW_DBEXT:
		imsg_compose(ibuf, IMSG_CTL_SHOW_DB_EXT, 0, 0, -1, NULL, 0);
		break;
	case SHOW_DBNET:
		imsg_compose(ibuf, IMSG_CTL_SHOW_DB_NET, 0, 0, -1, NULL, 0);
		break;
	case SHOW_DBRTR:
		imsg_compose(ibuf, IMSG_CTL_SHOW_DB_RTR, 0, 0, -1, NULL, 0);
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
	case SHOW_DBOPAQ:
		imsg_compose(ibuf, IMSG_CTL_SHOW_DB_OPAQ, 0, 0, -1, NULL, 0);
		break;
	case SHOW_RIB:
	case SHOW_RIB_DTAIL:
		imsg_compose(ibuf, IMSG_CTL_SHOW_RIB, 0, 0, -1, NULL, 0);
		break;
	case SHOW_FIB:
		if (!res->addr.s_addr)
			imsg_compose(ibuf, IMSG_CTL_KROUTE, 0, 0, -1,
			    &res->flags, sizeof(res->flags));
		else
			imsg_compose(ibuf, IMSG_CTL_KROUTE_ADDR, 0, 0, -1,
			    &res->addr, sizeof(res->addr));
		break;
	case SHOW_FIB_IFACE:
		if (*res->ifname)
			imsg_compose(ibuf, IMSG_CTL_IFINFO, 0, 0, -1,
			    res->ifname, sizeof(res->ifname));
		else
			imsg_compose(ibuf, IMSG_CTL_IFINFO, 0, 0, -1, NULL, 0);
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
		imsg_compose(ibuf, IMSG_CTL_RELOAD, 0, 0, -1, NULL, 0);
		printf("reload request sent.\n");
		done = 1;
		break;
	}

	if (imsgbuf_flush(ibuf) == -1)
		err(1, "write error");

	/* no output for certain commands such as log verbose */
	if (!done) {
		output->head(res);

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

				done = show(&imsg, res);
				imsg_free(&imsg);
			}
		}

		output->tail();
	}

	close(ctl_sock);
	free(ibuf);

	return (0);
}

int
show(struct imsg *imsg, struct parse_result *res)
{
	struct ctl_sum		*sum;
	struct ctl_sum_area	*sumarea;
	struct ctl_iface	*ctliface;
	struct ctl_nbr		*nbr;
	struct ctl_rt		*rt;
	struct kroute		*k;
	struct kif		*kif;
	static struct in_addr	 area_id;
	struct area		*area;
	static u_int8_t		 lasttype;
	static char		 ifname[IF_NAMESIZE];
	struct iface		*iface;
	struct lsa		*lsa;
	struct lsa_hdr		*lsa_hdr;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_SUM:
		sum = imsg->data;
		output->summary(sum);
		break;
	case IMSG_CTL_SHOW_SUM_AREA:
		sumarea = imsg->data;
		output->summary_area(sumarea);
		break;
	case IMSG_CTL_SHOW_INTERFACE:
		ctliface = imsg->data;
		if(res->action == SHOW_IFACE_DTAIL)
			output->interface(ctliface, 1);
		else
			output->interface(ctliface, 0);
		break;
	case IMSG_CTL_SHOW_NBR:
		nbr = imsg->data;
		if(res->action == SHOW_NBR_DTAIL)
			output->neighbor(nbr, 1);
		else
			output->neighbor(nbr, 0);
		break;
	case IMSG_CTL_SHOW_RIB:
		rt = imsg->data;
		if(res->action == SHOW_RIB_DTAIL)
			output->rib(rt, 1);
		else
			output->rib(rt, 0);
		break;
	case IMSG_CTL_KROUTE:
		if (imsg->hdr.len < IMSG_HEADER_SIZE + sizeof(struct kroute))
			errx(1, "wrong imsg len");
		k = imsg->data;
		output->fib(k);
		break;
	case IMSG_CTL_IFINFO:
		kif = imsg->data;
		output->fib_interface(kif);
		break;
	case IMSG_CTL_SHOW_DB_EXT:
	case IMSG_CTL_SHOW_DB_NET:
	case IMSG_CTL_SHOW_DB_RTR:
	case IMSG_CTL_SHOW_DB_SUM:
	case IMSG_CTL_SHOW_DB_ASBR:
	case IMSG_CTL_SHOW_DB_OPAQ:
		lsa = imsg->data;
		output->db(lsa, area_id, lasttype, ifname);
		lasttype = lsa->hdr.type;
		break;
	case IMSG_CTL_SHOW_DATABASE:
	case IMSG_CTL_SHOW_DB_SELF:
		lsa_hdr = imsg->data;
		output->db_simple(lsa_hdr, area_id, lasttype, ifname);
		lasttype = lsa_hdr->type;
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
		return (1);
	default:
		warnx("unknown imsg %d received", imsg->hdr.type);
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
	char			*buf;
	static char		 tfbuf[TF_BUFS][TF_LEN];/* ring buffer */
	static int		 idx = 0;
	unsigned int		 sec, min, hrs, day;
	unsigned long long	 week;

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

/* prototype defined in ospfd.h and shared with the kroute.c version */
u_int8_t
mask2prefixlen(in_addr_t ina)
{
	if (ina == 0)
		return (0);
	else
		return (33 - ffs(ntohl(ina)));
}

char *
print_ls_type(u_int8_t type)
{
	switch (type) {
	case LSA_TYPE_ROUTER:
		return ("Router");
	case LSA_TYPE_NETWORK:
		return ("Network");
	case LSA_TYPE_SUM_NETWORK:
		return ("Summary (Network)");
	case LSA_TYPE_SUM_ROUTER:
		return ("Summary (Router)");
	case LSA_TYPE_EXTERNAL:
		return ("AS External");
	case LSA_TYPE_LINK_OPAQ:
		return ("Type-9 Opaque");
	case LSA_TYPE_AREA_OPAQ:
		return ("Type-10 Opaque");
	case LSA_TYPE_AS_OPAQ:
		return ("Type-11 Opaque");
	default:
		return ("Unknown");
	}
}

char *
print_rtr_link_type(u_int8_t type)
{
	switch (type) {
	case LINK_TYPE_POINTTOPOINT:
		return ("Point-to-Point");
	case LINK_TYPE_TRANSIT_NET:
		return ("Transit Network");
	case LINK_TYPE_STUB_NET:
		return ("Stub Network");
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
print_ospf_options(u_int8_t opts)
{
	static char	optbuf[32];

	snprintf(optbuf, sizeof(optbuf), "%s|%s|%s|%s|%s|%s|%s|%s",
	    opts & OSPF_OPTION_DN ? "DN" : "-",
	    opts & OSPF_OPTION_O ? "O" : "-",
	    opts & OSPF_OPTION_DC ? "DC" : "-",
	    opts & OSPF_OPTION_EA ? "EA" : "-",
	    opts & OSPF_OPTION_NP ? "N/P" : "-",
	    opts & OSPF_OPTION_MC ? "MC" : "-",
	    opts & OSPF_OPTION_E ? "E" : "-",
	    opts & OSPF_OPTION_MT ? "MT" : "-");
	return (optbuf);
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

const char *
print_baudrate(u_int64_t baudrate)
{
	static char	buf[32];

	if (baudrate > IF_Gbps(1))
		snprintf(buf, sizeof(buf), "%llu GBit/s", baudrate / IF_Gbps(1));
	else if (baudrate > IF_Mbps(1))
		snprintf(buf, sizeof(buf), "%llu MBit/s", baudrate / IF_Mbps(1));
	else if (baudrate > IF_Kbps(1))
		snprintf(buf, sizeof(buf), "%llu KBit/s", baudrate / IF_Kbps(1));
	else
		snprintf(buf, sizeof(buf), "%llu Bit/s", baudrate);
	return (buf);
}
