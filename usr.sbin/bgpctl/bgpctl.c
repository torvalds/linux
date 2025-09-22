/*	$OpenBSD: bgpctl.c,v 1.317 2025/03/10 14:08:25 claudio Exp $ */

/*
 * Copyright (c) 2003 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2004-2019 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2016 Job Snijders <job@instituut.net>
 * Copyright (c) 2016 Peter Hessler <phessler@openbsd.org>
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

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <arpa/inet.h>

#include <endian.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <util.h>

#include "bgpd.h"
#include "session.h"
#include "rde.h"
#include "version.h"

#include "bgpctl.h"
#include "parser.h"
#include "mrtparser.h"

int		 main(int, char *[]);
int		 show(struct imsg *, struct parse_result *);
void		 send_filterset(struct imsgbuf *, struct filter_set_head *);
void		 show_mrt_dump_neighbors(struct mrt_rib *, struct mrt_peer *,
		    void *);
void		 show_mrt_dump(struct mrt_rib *, struct mrt_peer *, void *);
void		 network_mrt_dump(struct mrt_rib *, struct mrt_peer *, void *);
void		 show_mrt_state(struct mrt_bgp_state *, void *);
void		 show_mrt_msg(struct mrt_bgp_msg *, void *);
const char	*msg_type(uint8_t);
void		 network_bulk(struct parse_result *);
int		 match_aspath(void *, uint16_t, struct filter_as *);
struct flowspec	*res_to_flowspec(struct parse_result *);

struct imsgbuf	*imsgbuf;
struct mrt_parser show_mrt = { show_mrt_dump, show_mrt_state, show_mrt_msg };
struct mrt_parser net_mrt = { network_mrt_dump, NULL, NULL };
const struct output	*output = &show_output;
int tableid;
int nodescr;

__dead void
usage(void)
{
	extern char	*__progname;

	fprintf(stderr, "usage: %s [-jnV] [-s socket] command [argument ...]\n",
	    __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_un	 sa_un;
	int			 fd, n, done, numdone, ch, verbose = 0;
	struct imsg		 imsg;
	struct network_config	 net;
	struct parse_result	*res;
	struct ctl_neighbor	 neighbor;
	struct ctl_show_rib_request	ribreq;
	struct flowspec		*f;
	char			*sockname;
	enum imsg_type		 type;

	if (pledge("stdio rpath wpath cpath unix inet dns", NULL) == -1)
		err(1, "pledge");

	tableid = getrtable();
	if (asprintf(&sockname, "%s.%d", SOCKET_NAME, tableid) == -1)
		err(1, "asprintf");

	while ((ch = getopt(argc, argv, "jns:V")) != -1) {
		switch (ch) {
		case 'n':
			if (++nodescr > 1)
				usage();
			break;
		case 'j':
			output = &json_output;
			break;
		case 's':
			sockname = optarg;
			break;
		case 'V':
			fprintf(stderr, "OpenBGPD %s\n", BGPD_VERSION);
			return 0;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if ((res = parse(argc, argv)) == NULL)
		exit(1);

	memcpy(&neighbor.addr, &res->peeraddr, sizeof(neighbor.addr));
	strlcpy(neighbor.descr, res->peerdesc, sizeof(neighbor.descr));
	neighbor.is_group = res->is_group;
	strlcpy(neighbor.reason, res->reason, sizeof(neighbor.reason));

	switch (res->action) {
	case SHOW_MRT:
		if (pledge("stdio", NULL) == -1)
			err(1, "pledge");

		memset(&ribreq, 0, sizeof(ribreq));
		if (res->as.type != AS_UNDEF)
			ribreq.as = res->as;
		if (res->addr.aid) {
			ribreq.prefix = res->addr;
			ribreq.prefixlen = res->prefixlen;
		}
		/* XXX currently no communities support */
		ribreq.neighbor = neighbor;
		ribreq.aid = res->aid;
		ribreq.flags = res->flags;
		ribreq.validation_state = res->validation_state;
		show_mrt.arg = &ribreq;
		if (res->flags & F_CTL_NEIGHBORS)
			show_mrt.dump = show_mrt_dump_neighbors;
		else
			output->head(res);
		mrt_parse(res->mrtfd, &show_mrt, 1);
		exit(0);
	default:
		break;
	}

	if (pledge("stdio unix", NULL) == -1)
		err(1, "pledge");

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "control_init: socket");

	memset(&sa_un, 0, sizeof(sa_un));
	sa_un.sun_family = AF_UNIX;
	if (strlcpy(sa_un.sun_path, sockname, sizeof(sa_un.sun_path)) >=
	    sizeof(sa_un.sun_path))
		errx(1, "socket name too long");
	if (connect(fd, (struct sockaddr *)&sa_un, sizeof(sa_un)) == -1)
		err(1, "connect: %s", sockname);

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if ((imsgbuf = malloc(sizeof(struct imsgbuf))) == NULL)
		err(1, NULL);
	if (imsgbuf_init(imsgbuf, fd) == -1 ||
	    imsgbuf_set_maxsize(imsgbuf, MAX_BGPD_IMSGSIZE) == -1)
		err(1, NULL);
	done = 0;

	switch (res->action) {
	case NONE:
	case SHOW_MRT:
		usage();
		/* NOTREACHED */
	case SHOW:
	case SHOW_SUMMARY:
		imsg_compose(imsgbuf, IMSG_CTL_SHOW_NEIGHBOR, 0, 0, -1,
		    NULL, 0);
		break;
	case SHOW_SUMMARY_TERSE:
		imsg_compose(imsgbuf, IMSG_CTL_SHOW_TERSE, 0, 0, -1, NULL, 0);
		break;
	case SHOW_FIB:
		if (!res->addr.aid) {
			struct ctl_kroute_req	req = { 0 };

			req.af = aid2af(res->aid);
			req.flags = res->flags;

			imsg_compose(imsgbuf, IMSG_CTL_KROUTE, res->rtableid,
			    0, -1, &req, sizeof(req));
		} else
			imsg_compose(imsgbuf, IMSG_CTL_KROUTE_ADDR,
			    res->rtableid, 0, -1,
			    &res->addr, sizeof(res->addr));
		break;
	case SHOW_FIB_TABLES:
		imsg_compose(imsgbuf, IMSG_CTL_SHOW_FIB_TABLES, 0, 0, -1,
		    NULL, 0);
		break;
	case SHOW_NEXTHOP:
		imsg_compose(imsgbuf, IMSG_CTL_SHOW_NEXTHOP, res->rtableid,
		    0, -1, NULL, 0);
		break;
	case SHOW_INTERFACE:
		imsg_compose(imsgbuf, IMSG_CTL_SHOW_INTERFACE, 0, 0, -1,
		    NULL, 0);
		break;
	case SHOW_SET:
		imsg_compose(imsgbuf, IMSG_CTL_SHOW_SET, 0, 0, -1, NULL, 0);
		break;
	case SHOW_RTR:
		imsg_compose(imsgbuf, IMSG_CTL_SHOW_RTR, 0, 0, -1, NULL, 0);
		break;
	case SHOW_NEIGHBOR:
	case SHOW_NEIGHBOR_TIMERS:
	case SHOW_NEIGHBOR_TERSE:
		neighbor.show_timers = (res->action == SHOW_NEIGHBOR_TIMERS);
		if (res->peeraddr.aid || res->peerdesc[0])
			imsg_compose(imsgbuf, IMSG_CTL_SHOW_NEIGHBOR, 0, 0, -1,
			    &neighbor, sizeof(neighbor));
		else
			imsg_compose(imsgbuf, IMSG_CTL_SHOW_NEIGHBOR, 0, 0, -1,
			    NULL, 0);
		break;
	case SHOW_RIB:
		memset(&ribreq, 0, sizeof(ribreq));
		type = IMSG_CTL_SHOW_RIB;
		if (res->addr.aid) {
			ribreq.prefix = res->addr;
			ribreq.prefixlen = res->prefixlen;
			type = IMSG_CTL_SHOW_RIB_PREFIX;
		}
		if (res->as.type != AS_UNDEF)
			ribreq.as = res->as;
		if (res->community.flags != 0)
			ribreq.community = res->community;
		ribreq.neighbor = neighbor;
		strlcpy(ribreq.rib, res->rib, sizeof(ribreq.rib));
		ribreq.aid = res->aid;
		ribreq.path_id = res->pathid;
		ribreq.flags = res->flags;
		imsg_compose(imsgbuf, type, 0, 0, -1, &ribreq, sizeof(ribreq));
		break;
	case SHOW_RIB_MEM:
		imsg_compose(imsgbuf, IMSG_CTL_SHOW_RIB_MEM, 0, 0, -1, NULL, 0);
		break;
	case SHOW_METRICS:
		output = &ometric_output;
		numdone = 2;
		imsg_compose(imsgbuf, IMSG_CTL_SHOW_NEIGHBOR, 0, 0, -1,
		    NULL, 0);
		imsg_compose(imsgbuf, IMSG_CTL_SHOW_RIB_MEM, 0, 0, -1, NULL, 0);
		break;
	case RELOAD:
		imsg_compose(imsgbuf, IMSG_CTL_RELOAD, 0, 0, -1,
		    res->reason, sizeof(res->reason));
		if (res->reason[0])
			printf("reload request sent: %s\n", res->reason);
		else
			printf("reload request sent.\n");
		break;
	case FIB:
		errx(1, "action==FIB");
		break;
	case FIB_COUPLE:
		imsg_compose(imsgbuf, IMSG_CTL_FIB_COUPLE, res->rtableid, 0, -1,
		    NULL, 0);
		printf("couple request sent.\n");
		done = 1;
		break;
	case FIB_DECOUPLE:
		imsg_compose(imsgbuf, IMSG_CTL_FIB_DECOUPLE, res->rtableid,
		    0, -1, NULL, 0);
		printf("decouple request sent.\n");
		done = 1;
		break;
	case NEIGHBOR:
		errx(1, "action==NEIGHBOR");
		break;
	case NEIGHBOR_UP:
		imsg_compose(imsgbuf, IMSG_CTL_NEIGHBOR_UP, 0, 0, -1,
		    &neighbor, sizeof(neighbor));
		break;
	case NEIGHBOR_DOWN:
		imsg_compose(imsgbuf, IMSG_CTL_NEIGHBOR_DOWN, 0, 0, -1,
		    &neighbor, sizeof(neighbor));
		break;
	case NEIGHBOR_CLEAR:
		imsg_compose(imsgbuf, IMSG_CTL_NEIGHBOR_CLEAR, 0, 0, -1,
		    &neighbor, sizeof(neighbor));
		break;
	case NEIGHBOR_RREFRESH:
		imsg_compose(imsgbuf, IMSG_CTL_NEIGHBOR_RREFRESH, 0, 0, -1,
		    &neighbor, sizeof(neighbor));
		break;
	case NEIGHBOR_DESTROY:
		imsg_compose(imsgbuf, IMSG_CTL_NEIGHBOR_DESTROY, 0, 0, -1,
		    &neighbor, sizeof(neighbor));
		break;
	case NETWORK_BULK_ADD:
	case NETWORK_BULK_REMOVE:
		network_bulk(res);
		printf("requests sent.\n");
		done = 1;
		break;
	case NETWORK_ADD:
	case NETWORK_REMOVE:
		memset(&net, 0, sizeof(net));
		net.prefix = res->addr;
		net.prefixlen = res->prefixlen;
		net.rd = res->rd;
		/* attribute sets are not supported */
		if (res->action == NETWORK_ADD) {
			imsg_compose(imsgbuf, IMSG_NETWORK_ADD, 0, 0, -1,
			    &net, sizeof(net));
			send_filterset(imsgbuf, &res->set);
			imsg_compose(imsgbuf, IMSG_NETWORK_DONE, 0, 0, -1,
			    NULL, 0);
		} else
			imsg_compose(imsgbuf, IMSG_NETWORK_REMOVE, 0, 0, -1,
			    &net, sizeof(net));
		printf("request sent.\n");
		done = 1;
		break;
	case NETWORK_FLUSH:
		imsg_compose(imsgbuf, IMSG_NETWORK_FLUSH, 0, 0, -1, NULL, 0);
		printf("request sent.\n");
		done = 1;
		break;
	case NETWORK_SHOW:
		memset(&ribreq, 0, sizeof(ribreq));
		ribreq.aid = res->aid;
		strlcpy(ribreq.rib, res->rib, sizeof(ribreq.rib));
		imsg_compose(imsgbuf, IMSG_CTL_SHOW_NETWORK, 0, 0, -1,
		    &ribreq, sizeof(ribreq));
		break;
	case NETWORK_MRT:
		memset(&ribreq, 0, sizeof(ribreq));
		if (res->as.type != AS_UNDEF)
			ribreq.as = res->as;
		if (res->addr.aid) {
			ribreq.prefix = res->addr;
			ribreq.prefixlen = res->prefixlen;
		}
		/* XXX currently no community support */
		ribreq.neighbor = neighbor;
		ribreq.aid = res->aid;
		ribreq.flags = res->flags;
		net_mrt.arg = &ribreq;
		mrt_parse(res->mrtfd, &net_mrt, 1);
		done = 1;
		break;
	case FLOWSPEC_ADD:
	case FLOWSPEC_REMOVE:
		f = res_to_flowspec(res);
		/* attribute sets are not supported */
		if (res->action == FLOWSPEC_ADD) {
			imsg_compose(imsgbuf, IMSG_FLOWSPEC_ADD, 0, 0, -1,
			    f, FLOWSPEC_SIZE + f->len);
			send_filterset(imsgbuf, &res->set);
			imsg_compose(imsgbuf, IMSG_FLOWSPEC_DONE, 0, 0, -1,
			    NULL, 0);
		} else
			imsg_compose(imsgbuf, IMSG_FLOWSPEC_REMOVE, 0, 0, -1,
			    f, FLOWSPEC_SIZE + f->len);
		printf("request sent.\n");
		done = 1;
		break;
	case FLOWSPEC_FLUSH:
		imsg_compose(imsgbuf, IMSG_FLOWSPEC_FLUSH, 0, 0, -1, NULL, 0);
		printf("request sent.\n");
		done = 1;
		break;
	case FLOWSPEC_SHOW:
		memset(&ribreq, 0, sizeof(ribreq));
		switch (res->aid) {
		case AID_INET:
			ribreq.aid = AID_FLOWSPECv4;
			break;
		case AID_INET6:
			ribreq.aid = AID_FLOWSPECv6;
			break;
		case AID_UNSPEC:
			ribreq.aid = res->aid;
			break;
		default:
			errx(1, "flowspec family %s currently not supported",
			    aid2str(res->aid));
		}
		strlcpy(ribreq.rib, res->rib, sizeof(ribreq.rib));
		imsg_compose(imsgbuf, IMSG_CTL_SHOW_FLOWSPEC, 0, 0, -1,
		    &ribreq, sizeof(ribreq));
		break;
	case LOG_VERBOSE:
		verbose = 1;
		/* FALLTHROUGH */
	case LOG_BRIEF:
		imsg_compose(imsgbuf, IMSG_CTL_LOG_VERBOSE, 0, 0, -1,
		    &verbose, sizeof(verbose));
		printf("logging request sent.\n");
		done = 1;
		break;
	}

	output->head(res);

 again:
	if (imsgbuf_flush(imsgbuf) == -1)
		err(1, "write error");

	while (!done) {
		while (!done) {
			if ((n = imsg_get(imsgbuf, &imsg)) == -1)
				err(1, "imsg_get error");
			if (n == 0)
				break;

			done = show(&imsg, res);
			imsg_free(&imsg);
		}

		if (done)
			break;

		if ((n = imsgbuf_read(imsgbuf)) == -1)
			err(1, "read error");
		if (n == 0)
			errx(1, "pipe closed");

	}

	if (res->action == SHOW_METRICS && --numdone > 0) {
		done = 0;
		goto again;
	}

	output->tail();

	close(fd);
	free(imsgbuf);

	exit(0);
}

int
show(struct imsg *imsg, struct parse_result *res)
{
	struct peer		 p;
	struct ctl_timer	 t;
	struct ctl_show_interface iface;
	struct ctl_show_nexthop	 nh;
	struct ctl_show_set	 set;
	struct ctl_show_rtr	 rtr;
	struct kroute_full	 kf;
	struct ktable		 kt;
	struct flowspec		 f;
	struct ctl_show_rib	 rib;
	struct rde_memstats	 stats;
	struct ibuf		 ibuf;
	u_int			 rescode;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_NEIGHBOR:
		if (output->neighbor == NULL)
			break;
		if (imsg_get_data(imsg, &p, sizeof(p)) == -1)
			err(1, "imsg_get_data");
		output->neighbor(&p, res);
		break;
	case IMSG_CTL_SHOW_TIMER:
		if (output->timer == NULL)
			break;
		if (imsg_get_data(imsg, &t, sizeof(t)) == -1)
			err(1, "imsg_get_data");
		if (t.type > 0 && t.type < Timer_Max)
			output->timer(&t);
		break;
	case IMSG_CTL_SHOW_INTERFACE:
		if (output->interface == NULL)
			break;
		if (imsg_get_data(imsg, &iface, sizeof(iface)) == -1)
			err(1, "imsg_get_data");
		output->interface(&iface);
		break;
	case IMSG_CTL_SHOW_NEXTHOP:
		if (output->nexthop == NULL)
			break;
		if (imsg_get_data(imsg, &nh, sizeof(nh)) == -1)
			err(1, "imsg_get_data");
		output->nexthop(&nh);
		break;
	case IMSG_CTL_KROUTE:
	case IMSG_CTL_SHOW_NETWORK:
		if (output->fib == NULL)
			break;
		if (imsg_get_data(imsg, &kf, sizeof(kf)) == -1)
			err(1, "imsg_get_data");
		output->fib(&kf);
		break;
	case IMSG_CTL_SHOW_FLOWSPEC:
		if (output->flowspec == NULL)
			break;
		if (imsg_get_data(imsg, &f, sizeof(f)) == -1)
			err(1, "imsg_get_data");
		output->flowspec(&f);
		break;
	case IMSG_CTL_SHOW_FIB_TABLES:
		if (output->fib_table == NULL)
			break;
		if (imsg_get_data(imsg, &kt, sizeof(kt)) == -1)
			err(1, "imsg_get_data");
		output->fib_table(&kt);
		break;
	case IMSG_CTL_SHOW_RIB:
		if (output->rib == NULL)
			break;
		if (imsg_get_ibuf(imsg, &ibuf) == -1)
			err(1, "imsg_get_ibuf");
		if (ibuf_get(&ibuf, &rib, sizeof(rib)) == -1)
			err(1, "imsg_get_ibuf");
		output->rib(&rib, &ibuf, res);
		break;
	case IMSG_CTL_SHOW_RIB_COMMUNITIES:
		if (output->communities == NULL)
			break;
		if (imsg_get_ibuf(imsg, &ibuf) == -1)
			err(1, "imsg_get_ibuf");
		output->communities(&ibuf, res);
		break;
	case IMSG_CTL_SHOW_RIB_ATTR:
		if (output->attr == NULL)
			break;
		if (imsg_get_ibuf(imsg, &ibuf) == -1)
			err(1, "imsg_get_ibuf");
		output->attr(&ibuf, res->flags, 0);
		break;
	case IMSG_CTL_SHOW_RIB_MEM:
		if (output->rib_mem == NULL)
			break;
		if (imsg_get_data(imsg, &stats, sizeof(stats)) == -1)
			err(1, "imsg_get_data");
		output->rib_mem(&stats);
		return (1);
	case IMSG_CTL_SHOW_SET:
		if (output->set == NULL)
			break;
		if (imsg_get_data(imsg, &set, sizeof(set)) == -1)
			err(1, "imsg_get_data");
		output->set(&set);
		break;
	case IMSG_CTL_SHOW_RTR:
		if (output->rtr == NULL)
			break;
		if (imsg_get_data(imsg, &rtr, sizeof(rtr)) == -1)
			err(1, "imsg_get_data");
		output->rtr(&rtr);
		break;
	case IMSG_CTL_RESULT:
		if (output->result == NULL)
			break;
		if (imsg_get_data(imsg, &rescode, sizeof(rescode)) == -1)
			err(1, "imsg_get_data");
		output->result(rescode);
		return (1);
	case IMSG_CTL_END:
		return (1);
	default:
		warnx("unknown imsg %d received", imsg->hdr.type);
		break;
	}

	return (0);
}

time_t
get_rel_monotime(monotime_t t)
{
	monotime_t now;

	if (!monotime_valid(t))
		return 0;
	now = getmonotime();
	return monotime_to_sec(monotime_sub(now, t));
}

char *
fmt_peer(const char *descr, const struct bgpd_addr *remote_addr,
    int masklen)
{
	const char	*ip;
	char		*p;

	if (descr && descr[0] && !nodescr) {
		if ((p = strdup(descr)) == NULL)
			err(1, NULL);
		return (p);
	}

	ip = log_addr(remote_addr);
	if (masklen != -1 && ((remote_addr->aid == AID_INET && masklen != 32) ||
	    (remote_addr->aid == AID_INET6 && masklen != 128))) {
		if (asprintf(&p, "%s/%u", ip, masklen) == -1)
			err(1, NULL);
	} else {
		if ((p = strdup(ip)) == NULL)
			err(1, NULL);
	}

	return (p);
}

const char *
fmt_auth_method(enum auth_method method)
{
	switch (method) {
	case AUTH_MD5SIG:
		return ", using md5sig";
	case AUTH_IPSEC_MANUAL_ESP:
		return ", using ipsec manual esp";
	case AUTH_IPSEC_MANUAL_AH:
		return ", using ipsec manual ah";
	case AUTH_IPSEC_IKE_ESP:
		return ", using ipsec ike esp";
	case AUTH_IPSEC_IKE_AH:
		return ", using ipsec ike ah";
	case AUTH_NONE:	/* FALLTHROUGH */
	default:
		return "";
	}
}

#define TF_LEN	16

static const char *
fmt_timeframe(time_t t)
{
	static char		buf[TF_LEN];
	unsigned long long	week;
	unsigned int		sec, min, hrs, day;
	const char		*due = "";

	if (t < 0) {
		due = "due in ";
		t = -t;
	}
	week = t;

	sec = week % 60;
	week /= 60;
	min = week % 60;
	week /= 60;
	hrs = week % 24;
	week /= 24;
	day = week % 7;
	week /= 7;

	if (week >= 1000)
		snprintf(buf, sizeof(buf), "%s%02lluw", due, week);
	else if (week > 0)
		snprintf(buf, sizeof(buf), "%s%02lluw%01ud%02uh",
		    due, week, day, hrs);
	else if (day > 0)
		snprintf(buf, sizeof(buf), "%s%01ud%02uh%02um",
		    due, day, hrs, min);
	else
		snprintf(buf, sizeof(buf), "%s%02u:%02u:%02u",
		    due, hrs, min, sec);

	return (buf);
}

const char *
fmt_monotime(monotime_t mt)
{
	time_t t;

	if (!monotime_valid(mt))
		return ("Never");

	t = get_rel_monotime(mt);
	return (fmt_timeframe(t));
}

const char *
fmt_fib_flags(uint16_t flags)
{
	static char buf[8];

	if (flags & F_BGPD)
		strlcpy(buf, "B", sizeof(buf));
	else if (flags & F_CONNECTED)
		strlcpy(buf, "C", sizeof(buf));
	else if (flags & F_STATIC)
		strlcpy(buf, "S", sizeof(buf));
	else
		strlcpy(buf, " ", sizeof(buf));

	if (flags & F_NEXTHOP)
		strlcat(buf, "N", sizeof(buf));
	else
		strlcat(buf, " ", sizeof(buf));

	if (flags & F_REJECT && flags & F_BLACKHOLE)
		strlcat(buf, "f", sizeof(buf));
	else if (flags & F_REJECT)
		strlcat(buf, "r", sizeof(buf));
	else if (flags & F_BLACKHOLE)
		strlcat(buf, "b", sizeof(buf));
	else
		strlcat(buf, " ", sizeof(buf));

	return buf;
}

const char *
fmt_origin(uint8_t origin, int sum)
{
	switch (origin) {
	case ORIGIN_IGP:
		return (sum ? "i" : "IGP");
	case ORIGIN_EGP:
		return (sum ? "e" : "EGP");
	case ORIGIN_INCOMPLETE:
		return (sum ? "?" : "incomplete");
	default:
		return (sum ? "X" : "bad origin");
	}
}

const char *
fmt_flags(uint32_t flags, int sum)
{
	static char buf[80];
	char	 flagstr[5];
	char	*p = flagstr;

	if (sum) {
		if (flags & F_PREF_FILTERED)
			*p++ = 'F';
		if (flags & F_PREF_INVALID)
			*p++ = 'E';
		if (flags & F_PREF_OTC_LEAK)
			*p++ = 'L';
		if (flags & F_PREF_ANNOUNCE)
			*p++ = 'A';
		if (flags & F_PREF_INTERNAL)
			*p++ = 'I';
		if (flags & F_PREF_STALE)
			*p++ = 'S';
		if (flags & F_PREF_ELIGIBLE)
			*p++ = '*';
		if (flags & F_PREF_BEST)
			*p++ = '>';
		if (flags & F_PREF_ECMP)
			*p++ = 'm';
		if (flags & F_PREF_AS_WIDE)
			*p++ = 'w';
		*p = '\0';
		snprintf(buf, sizeof(buf), "%-5s", flagstr);
	} else {
		if (flags & F_PREF_INTERNAL)
			strlcpy(buf, "internal", sizeof(buf));
		else
			strlcpy(buf, "external", sizeof(buf));

		if (flags & F_PREF_FILTERED)
			strlcat(buf, ", filtered", sizeof(buf));
		if (flags & F_PREF_INVALID)
			strlcat(buf, ", invalid", sizeof(buf));
		if (flags & F_PREF_OTC_LEAK)
			strlcat(buf, ", otc leak", sizeof(buf));
		if (flags & F_PREF_STALE)
			strlcat(buf, ", stale", sizeof(buf));
		if (flags & F_PREF_ELIGIBLE)
			strlcat(buf, ", valid", sizeof(buf));
		if (flags & F_PREF_BEST)
			strlcat(buf, ", best", sizeof(buf));
		if (flags & F_PREF_ECMP)
			strlcat(buf, ", ecmp", sizeof(buf));
		if (flags & F_PREF_AS_WIDE)
			strlcat(buf, ", as-wide", sizeof(buf));
		if (flags & F_PREF_ANNOUNCE)
			strlcat(buf, ", announced", sizeof(buf));
		if (strlen(buf) >= sizeof(buf) - 1)
			errx(1, "%s buffer too small", __func__);
	}

	return buf;
}

const char *
fmt_ovs(uint8_t validation_state, int sum)
{
	switch (validation_state) {
	case ROA_INVALID:
		return (sum ? "!" : "invalid");
	case ROA_VALID:
		return (sum ? "V" : "valid");
	default:
		return (sum ? "N" : "not-found");
	}
}

const char *
fmt_avs(uint8_t validation_state, int sum)
{
	switch (validation_state) {
	case ASPA_INVALID:
		return (sum ? "!" : "invalid");
	case ASPA_VALID:
		return (sum ? "V" : "valid");
	default:
		return (sum ? "?" : "unknown");
	}
}

const char *
fmt_mem(long long num)
{
	static char	buf[16];

	if (fmt_scaled(num, buf) == -1)
		snprintf(buf, sizeof(buf), "%lldB", num);

	return (buf);
}

const char *
fmt_errstr(uint8_t errcode, uint8_t subcode)
{
	static char	 errbuf[256];
	const char	*errstr = NULL;
	const char	*suberr = NULL;
	int		 uk = 0;

	if (errcode == 0)	/* no error */
		return NULL;

	if (errcode < sizeof(errnames)/sizeof(char *))
		errstr = errnames[errcode];

	switch (errcode) {
	case ERR_HEADER:
		if (subcode < sizeof(suberr_header_names)/sizeof(char *))
			suberr = suberr_header_names[subcode];
		else
			uk = 1;
		break;
	case ERR_OPEN:
		if (subcode < sizeof(suberr_open_names)/sizeof(char *))
			suberr = suberr_open_names[subcode];
		else
			uk = 1;
		break;
	case ERR_UPDATE:
		if (subcode < sizeof(suberr_update_names)/sizeof(char *))
			suberr = suberr_update_names[subcode];
		else
			uk = 1;
		break;
	case ERR_HOLDTIMEREXPIRED:
		if (subcode != 0)
			uk = 1;
		break;
	case ERR_FSM:
		if (subcode < sizeof(suberr_fsm_names)/sizeof(char *))
			suberr = suberr_fsm_names[subcode];
		else
			uk = 1;
		break;
	case ERR_CEASE:
		if (subcode < sizeof(suberr_cease_names)/sizeof(char *))
			suberr = suberr_cease_names[subcode];
		else
			uk = 1;
		break;
	default:
		snprintf(errbuf, sizeof(errbuf),
		    "unknown error code %u subcode %u", errcode, subcode);
		return (errbuf);
	}

	if (uk)
		snprintf(errbuf, sizeof(errbuf),
		    "%s, unknown subcode %u", errstr, subcode);
	else if (suberr == NULL)
		return (errstr);
	else
		snprintf(errbuf, sizeof(errbuf),
		    "%s, %s", errstr, suberr);

	return (errbuf);
}

const char *
fmt_attr(uint8_t type, int flags)
{
#define CHECK_FLAGS(s, t, m)	\
	if (((s) & ~(ATTR_DEFMASK | (m))) != (t)) pflags = 1

	static char cstr[48];
	int pflags = 0;

	switch (type) {
	case ATTR_ORIGIN:
		CHECK_FLAGS(flags, ATTR_WELL_KNOWN, 0);
		strlcpy(cstr, "Origin", sizeof(cstr));
		break;
	case ATTR_ASPATH:
		CHECK_FLAGS(flags, ATTR_WELL_KNOWN, 0);
		strlcpy(cstr, "AS-Path", sizeof(cstr));
		break;
	case ATTR_AS4_PATH:
		CHECK_FLAGS(flags, ATTR_WELL_KNOWN, 0);
		strlcpy(cstr, "AS4-Path", sizeof(cstr));
		break;
	case ATTR_NEXTHOP:
		CHECK_FLAGS(flags, ATTR_WELL_KNOWN, 0);
		strlcpy(cstr, "Nexthop", sizeof(cstr));
		break;
	case ATTR_MED:
		CHECK_FLAGS(flags, ATTR_OPTIONAL, 0);
		strlcpy(cstr, "Med", sizeof(cstr));
		break;
	case ATTR_LOCALPREF:
		CHECK_FLAGS(flags, ATTR_WELL_KNOWN, 0);
		strlcpy(cstr, "Localpref", sizeof(cstr));
		break;
	case ATTR_ATOMIC_AGGREGATE:
		CHECK_FLAGS(flags, ATTR_WELL_KNOWN, 0);
		strlcpy(cstr, "Atomic Aggregate", sizeof(cstr));
		break;
	case ATTR_AGGREGATOR:
		CHECK_FLAGS(flags, ATTR_OPTIONAL|ATTR_TRANSITIVE, ATTR_PARTIAL);
		strlcpy(cstr, "Aggregator", sizeof(cstr));
		break;
	case ATTR_AS4_AGGREGATOR:
		CHECK_FLAGS(flags, ATTR_OPTIONAL|ATTR_TRANSITIVE, ATTR_PARTIAL);
		strlcpy(cstr, "AS4-Aggregator", sizeof(cstr));
		break;
	case ATTR_COMMUNITIES:
		CHECK_FLAGS(flags, ATTR_OPTIONAL|ATTR_TRANSITIVE, ATTR_PARTIAL);
		strlcpy(cstr, "Communities", sizeof(cstr));
		break;
	case ATTR_ORIGINATOR_ID:
		CHECK_FLAGS(flags, ATTR_OPTIONAL, 0);
		strlcpy(cstr, "Originator Id", sizeof(cstr));
		break;
	case ATTR_CLUSTER_LIST:
		CHECK_FLAGS(flags, ATTR_OPTIONAL, 0);
		strlcpy(cstr, "Cluster Id List", sizeof(cstr));
		break;
	case ATTR_MP_REACH_NLRI:
		CHECK_FLAGS(flags, ATTR_OPTIONAL, 0);
		strlcpy(cstr, "MP Reach NLRI", sizeof(cstr));
		break;
	case ATTR_MP_UNREACH_NLRI:
		CHECK_FLAGS(flags, ATTR_OPTIONAL, 0);
		strlcpy(cstr, "MP Unreach NLRI", sizeof(cstr));
		break;
	case ATTR_EXT_COMMUNITIES:
		CHECK_FLAGS(flags, ATTR_OPTIONAL|ATTR_TRANSITIVE, ATTR_PARTIAL);
		strlcpy(cstr, "Ext. Communities", sizeof(cstr));
		break;
	case ATTR_LARGE_COMMUNITIES:
		CHECK_FLAGS(flags, ATTR_OPTIONAL|ATTR_TRANSITIVE, ATTR_PARTIAL);
		strlcpy(cstr, "Large Communities", sizeof(cstr));
		break;
	case ATTR_OTC:
		CHECK_FLAGS(flags, ATTR_OPTIONAL|ATTR_TRANSITIVE, ATTR_PARTIAL);
		strlcpy(cstr, "OTC", sizeof(cstr));
		break;
	default:
		/* ignore unknown attributes */
		snprintf(cstr, sizeof(cstr), "Unknown Attribute #%u", type);
		pflags = 1;
		break;
	}
	if (flags != -1 && pflags) {
		strlcat(cstr, " flags [", sizeof(cstr));
		if (flags & ATTR_OPTIONAL)
			strlcat(cstr, "O", sizeof(cstr));
		if (flags & ATTR_TRANSITIVE)
			strlcat(cstr, "T", sizeof(cstr));
		if (flags & ATTR_PARTIAL)
			strlcat(cstr, "P", sizeof(cstr));
		strlcat(cstr, "]", sizeof(cstr));
	}
	return (cstr);

#undef CHECK_FLAGS
}

const char *
fmt_community(uint16_t a, uint16_t v)
{
	static char buf[12];

	if (a == COMMUNITY_WELLKNOWN)
		switch (v) {
		case COMMUNITY_GRACEFUL_SHUTDOWN:
			return "GRACEFUL_SHUTDOWN";
		case COMMUNITY_NO_EXPORT:
			return "NO_EXPORT";
		case COMMUNITY_NO_ADVERTISE:
			return "NO_ADVERTISE";
		case COMMUNITY_NO_EXPSUBCONFED:
			return "NO_EXPORT_SUBCONFED";
		case COMMUNITY_NO_PEER:
			return "NO_PEER";
		case COMMUNITY_BLACKHOLE:
			return "BLACKHOLE";
		default:
			break;
		}

	snprintf(buf, sizeof(buf), "%hu:%hu", a, v);
	return buf;
}

const char *
fmt_large_community(uint32_t d1, uint32_t d2, uint32_t d3)
{
	static char buf[33];

	snprintf(buf, sizeof(buf), "%u:%u:%u", d1, d2, d3);
	return buf;
}

const char *
fmt_ext_community(uint64_t ext)
{
	static char	buf[32];
	struct in_addr	ip;
	uint32_t	as4, u32;
	uint16_t	as2, u16;
	uint8_t		type, subtype;

	type = ext >> 56;
	subtype = ext >> 48;

	switch (type) {
	case EXT_COMMUNITY_TRANS_TWO_AS:
	case EXT_COMMUNITY_GEN_TWO_AS:
		as2 = ext >> 32;
		u32 = ext;
		snprintf(buf, sizeof(buf), "%s %s:%u",
		    log_ext_subtype(type, subtype), log_as(as2), u32);
		return buf;
	case EXT_COMMUNITY_TRANS_IPV4:
	case EXT_COMMUNITY_GEN_IPV4:
		ip.s_addr = htonl(ext >> 16);
		u16 = ext;
		snprintf(buf, sizeof(buf), "%s %s:%hu",
		    log_ext_subtype(type, subtype), inet_ntoa(ip), u16);
		return buf;
	case EXT_COMMUNITY_TRANS_FOUR_AS:
	case EXT_COMMUNITY_GEN_FOUR_AS:
		as4 = ext >> 16;
		u16 = ext;
		snprintf(buf, sizeof(buf), "%s %s:%hu",
		    log_ext_subtype(type, subtype), log_as(as4), u16);
		return buf;
	case EXT_COMMUNITY_TRANS_OPAQUE:
	case EXT_COMMUNITY_TRANS_EVPN:
		ext &= 0xffffffffffffULL;
		snprintf(buf, sizeof(buf), "%s 0x%llx",
		    log_ext_subtype(type, subtype), (unsigned long long)ext);
		return buf;
	case EXT_COMMUNITY_NON_TRANS_OPAQUE:
		ext &= 0xffffffffffffULL;
		if (subtype == EXT_COMMUNITY_SUBTYPE_OVS) {
			switch (ext) {
			case EXT_COMMUNITY_OVS_VALID:
				snprintf(buf, sizeof(buf), "%s valid",
				    log_ext_subtype(type, subtype));
				return buf;
			case EXT_COMMUNITY_OVS_NOTFOUND:
				snprintf(buf, sizeof(buf), "%s not-found",
				    log_ext_subtype(type, subtype));
				return buf;
			case EXT_COMMUNITY_OVS_INVALID:
				snprintf(buf, sizeof(buf), "%s invalid",
				    log_ext_subtype(type, subtype));
				return buf;
			default:
				snprintf(buf, sizeof(buf), "%s 0x%llx",
				    log_ext_subtype(type, subtype),
				    (unsigned long long)ext);
				return buf;
			}
		} else {
			snprintf(buf, sizeof(buf), "%s 0x%llx",
			    log_ext_subtype(type, subtype),
			    (unsigned long long)ext);
			return buf;
		}
		break;
	default:
		snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)ext);
		return buf;
	}
}

const char *
fmt_set_type(struct ctl_show_set *set)
{
	switch (set->type) {
	case ASPA_SET:
		return "ASPA";
	case ROA_SET:
		return "ROA";
	case PREFIX_SET:
		return "PREFIX";
	case ORIGIN_SET:
		return "ORIGIN";
	case ASNUM_SET:
		return "ASNUM";
	default:
		return "BULA";
	}
}

void
send_filterset(struct imsgbuf *i, struct filter_set_head *set)
{
	struct filter_set	*s;

	while ((s = TAILQ_FIRST(set)) != NULL) {
		imsg_compose(i, IMSG_FILTER_SET, 0, 0, -1, s,
		    sizeof(struct filter_set));
		TAILQ_REMOVE(set, s, entry);
		free(s);
	}
}

void
network_bulk(struct parse_result *res)
{
	struct network_config net;
	struct filter_set *s = NULL;
	struct bgpd_addr h;
	char *line = NULL;
	size_t linesize = 0;
	ssize_t linelen;
	uint8_t len;
	FILE *f;

	if ((f = fdopen(STDIN_FILENO, "r")) == NULL)
		err(1, "Failed to open stdin\n");

	while ((linelen = getline(&line, &linesize, f)) != -1) {
		char *b, *buf = line;
		while ((b = strsep(&buf, " \t\n")) != NULL) {
			if (*b == '\0')	/* skip empty tokens */
				continue;
			/* Stop processing after a comment */
			if (*b == '#')
				break;
			memset(&net, 0, sizeof(net));
			if (parse_prefix(b, strlen(b), &h, &len) != 1)
				errx(1, "bad prefix: %s", b);
			net.prefix = h;
			net.prefixlen = len;
			net.rd = res->rd;

			if (res->action == NETWORK_BULK_ADD) {
				imsg_compose(imsgbuf, IMSG_NETWORK_ADD,
				    0, 0, -1, &net, sizeof(net));
				/*
				 * can't use send_filterset since that
				 * would free the set.
				 */
				TAILQ_FOREACH(s, &res->set, entry) {
					imsg_compose(imsgbuf,
					    IMSG_FILTER_SET,
					    0, 0, -1, s, sizeof(*s));
				}
				imsg_compose(imsgbuf, IMSG_NETWORK_DONE,
				    0, 0, -1, NULL, 0);
			} else
				imsg_compose(imsgbuf, IMSG_NETWORK_REMOVE,
				     0, 0, -1, &net, sizeof(net));
		}
	}
	free(line);
	if (ferror(f))
		err(1, "getline");
	fclose(f);
}

void
show_mrt_dump_neighbors(struct mrt_rib *mr, struct mrt_peer *mp, void *arg)
{
	struct mrt_peer_entry *p;
	struct in_addr ina;
	uint16_t i;

	ina.s_addr = htonl(mp->bgp_id);
	printf("view: %s BGP ID: %s Number of peers: %u\n\n",
	    mp->view, inet_ntoa(ina), mp->npeers);
	printf("%-30s %8s %15s\n", "Neighbor", "AS", "BGP ID");
	for (i = 0; i < mp->npeers; i++) {
		p = &mp->peers[i];
		ina.s_addr = htonl(p->bgp_id);
		printf("%-30s %8u %15s\n", log_addr(&p->addr), p->asnum,
		    inet_ntoa(ina));
	}
	/* we only print the first message */
	exit(0);
}

void
show_mrt_dump(struct mrt_rib *mr, struct mrt_peer *mp, void *arg)
{
	struct ctl_show_rib		 ctl;
	struct parse_result		 res;
	struct ctl_show_rib_request	*req = arg;
	struct mrt_rib_entry		*mre;
	struct ibuf			 ibuf;
	uint16_t			 i, j;

	memset(&res, 0, sizeof(res));
	res.flags = req->flags;

	for (i = 0; i < mr->nentries; i++) {
		mre = &mr->entries[i];
		memset(&ctl, 0, sizeof(ctl));
		ctl.prefix = mr->prefix;
		ctl.prefixlen = mr->prefixlen;
		ctl.lastchange = time_to_monotime(mre->originated);
		ctl.true_nexthop = mre->nexthop;
		ctl.exit_nexthop = mre->nexthop;
		ctl.origin = mre->origin;
		ctl.local_pref = mre->local_pref;
		ctl.med = mre->med;
		/* weight is not part of the mrt dump so it can't be set */
		if (mr->add_path) {
			ctl.flags |= F_PREF_PATH_ID;
			ctl.path_id = mre->path_id;
		}

		if (mre->peer_idx < mp->npeers) {
			ctl.remote_addr = mp->peers[mre->peer_idx].addr;
			ctl.remote_id = mp->peers[mre->peer_idx].bgp_id;
		}

		/* filter by neighbor */
		if (req->neighbor.addr.aid != AID_UNSPEC &&
		    memcmp(&req->neighbor.addr, &ctl.remote_addr,
		    sizeof(ctl.remote_addr)) != 0)
			continue;
		/* filter by AF */
		if (req->aid && req->aid != ctl.prefix.aid)
			return;
		/* filter by prefix */
		if (req->prefix.aid != AID_UNSPEC) {
			if (req->flags & F_LONGER) {
				if (req->prefixlen > ctl.prefixlen)
					return;
				if (prefix_compare(&req->prefix, &ctl.prefix,
				    req->prefixlen))
					return;
			} else if (req->flags & F_SHORTER) {
				if (req->prefixlen < ctl.prefixlen)
					return;
				if (prefix_compare(&req->prefix, &ctl.prefix,
				    ctl.prefixlen))
					return;
			} else {
				if (req->prefixlen != ctl.prefixlen)
					return;
				if (prefix_compare(&req->prefix, &ctl.prefix,
				    req->prefixlen))
					return;
			}
		}
		/* filter by AS */
		if (req->as.type != AS_UNDEF &&
		    !match_aspath(mre->aspath, mre->aspath_len, &req->as))
			continue;

		ibuf_from_buffer(&ibuf, mre->aspath, mre->aspath_len);
		output->rib(&ctl, &ibuf, &res);
		if (req->flags & F_CTL_DETAIL) {
			for (j = 0; j < mre->nattrs; j++) {
				ibuf_from_buffer(&ibuf, mre->attrs[j].attr,
				    mre->attrs[j].attr_len);
				output->attr(&ibuf, req->flags, 0);
			}
		}
	}
}

void
network_mrt_dump(struct mrt_rib *mr, struct mrt_peer *mp, void *arg)
{
	struct ctl_show_rib		 ctl;
	struct network_config		 net;
	struct ctl_show_rib_request	*req = arg;
	struct mrt_rib_entry		*mre;
	struct ibuf			*msg;
	uint16_t			 i, j;

	/* can't announce more than one path so ignore add-path */
	if (mr->add_path)
		return;

	for (i = 0; i < mr->nentries; i++) {
		mre = &mr->entries[i];
		memset(&ctl, 0, sizeof(ctl));
		ctl.prefix = mr->prefix;
		ctl.prefixlen = mr->prefixlen;
		ctl.lastchange = time_to_monotime(mre->originated);
		ctl.true_nexthop = mre->nexthop;
		ctl.exit_nexthop = mre->nexthop;
		ctl.origin = mre->origin;
		ctl.local_pref = mre->local_pref;
		ctl.med = mre->med;

		if (mre->peer_idx < mp->npeers) {
			ctl.remote_addr = mp->peers[mre->peer_idx].addr;
			ctl.remote_id = mp->peers[mre->peer_idx].bgp_id;
		}

		/* filter by neighbor */
		if (req->neighbor.addr.aid != AID_UNSPEC &&
		    memcmp(&req->neighbor.addr, &ctl.remote_addr,
		    sizeof(ctl.remote_addr)) != 0)
			continue;
		/* filter by AF */
		if (req->aid && req->aid != ctl.prefix.aid)
			return;
		/* filter by prefix */
		if (req->prefix.aid != AID_UNSPEC) {
			if (!prefix_compare(&req->prefix, &ctl.prefix,
			    req->prefixlen)) {
				if (req->flags & F_LONGER) {
					if (req->prefixlen > ctl.prefixlen)
						return;
				} else if (req->prefixlen != ctl.prefixlen)
					return;
			} else
				return;
		}
		/* filter by AS */
		if (req->as.type != AS_UNDEF &&
		    !match_aspath(mre->aspath, mre->aspath_len, &req->as))
			continue;

		memset(&net, 0, sizeof(net));
		net.prefix = ctl.prefix;
		net.prefixlen = ctl.prefixlen;
		net.type = NETWORK_MRTCLONE;
		/* XXX rd can't be set and will be 0 */

		imsg_compose(imsgbuf, IMSG_NETWORK_ADD, 0, 0, -1,
		    &net, sizeof(net));
		if ((msg = imsg_create(imsgbuf, IMSG_NETWORK_ASPATH,
		    0, 0, sizeof(ctl) + mre->aspath_len)) == NULL)
			errx(1, "imsg_create failure");
		if (imsg_add(msg, &ctl, sizeof(ctl)) == -1 ||
		    imsg_add(msg, mre->aspath, mre->aspath_len) == -1)
			errx(1, "imsg_add failure");
		imsg_close(imsgbuf, msg);
		for (j = 0; j < mre->nattrs; j++)
			imsg_compose(imsgbuf, IMSG_NETWORK_ATTR, 0, 0, -1,
			    mre->attrs[j].attr, mre->attrs[j].attr_len);
		imsg_compose(imsgbuf, IMSG_NETWORK_DONE, 0, 0, -1, NULL, 0);

		if (imsgbuf_flush(imsgbuf) == -1)
			err(1, "write error");
	}
}

static const char *
fmt_time(struct timespec *t)
{
	static char timebuf[32];
	static struct timespec prevtime;
	struct timespec temp;

	timespecsub(t, &prevtime, &temp);
	snprintf(timebuf, sizeof(timebuf), "%lld.%06ld",
	    (long long)temp.tv_sec, temp.tv_nsec / 1000);
	prevtime = *t;
	return (timebuf);
}

void
show_mrt_state(struct mrt_bgp_state *ms, void *arg)
{
	printf("%s %s[%u] -> ", fmt_time(&ms->time),
	    log_addr(&ms->src), ms->src_as);
	printf("%s[%u]: %s -> %s\n", log_addr(&ms->dst), ms->dst_as,
	    statenames[ms->old_state], statenames[ms->new_state]);
}

static void
print_afi(struct ibuf *b)
{
	uint16_t afi;
	uint8_t safi, aid;

	if (ibuf_get_n16(b, &afi) == -1 ||	/* afi, 2 byte */
	    ibuf_skip(b, 1) == -1 ||		/* reserved, 1 byte */
	    ibuf_get_n8(b, &safi) == -1 ||	/* safi, 1 byte */
	    ibuf_size(b) != 0) {
		printf("bad length");
		return;
	}

	if (afi2aid(afi, safi, &aid) == -1)
		printf("unknown afi %u safi %u", afi, safi);
	else
		printf("%s", aid2str(aid));
}

static void
print_capability(uint8_t capa_code, struct ibuf *b)
{
	uint32_t as;

	switch (capa_code) {
	case CAPA_MP:
		printf("multiprotocol capability: ");
		print_afi(b);
		break;
	case CAPA_REFRESH:
		printf("route refresh capability");
		break;
	case CAPA_RESTART:
		printf("graceful restart capability");
		/* XXX there is more needed here */
		break;
	case CAPA_AS4BYTE:
		printf("4-byte AS num capability: ");
		if (ibuf_get_n32(b, &as) == -1 ||
		    ibuf_size(b) != 0)
			printf("bad length");
		else
			printf("AS %u", as);
		break;
	case CAPA_ADD_PATH:
		printf("add-path capability");
		/* XXX there is more needed here */
		break;
	case CAPA_ENHANCED_RR:
		printf("enhanced route refresh capability");
		break;
	case CAPA_EXT_MSG:
		printf("extended message capability");
		break;
	default:
		printf("unknown capability %u length %zu",
		    capa_code, ibuf_size(b));
		break;
	}
}

static void
print_notification(uint8_t errcode, uint8_t subcode)
{
	const char *suberrname = NULL;
	int uk = 0;

	switch (errcode) {
	case ERR_HEADER:
		if (subcode >= sizeof(suberr_header_names)/sizeof(char *))
			uk = 1;
		else
			suberrname = suberr_header_names[subcode];
		break;
	case ERR_OPEN:
		if (subcode >= sizeof(suberr_open_names)/sizeof(char *))
			uk = 1;
		else
			suberrname = suberr_open_names[subcode];
		break;
	case ERR_UPDATE:
		if (subcode >= sizeof(suberr_update_names)/sizeof(char *))
			uk = 1;
		else
			suberrname = suberr_update_names[subcode];
		break;
	case ERR_CEASE:
		if (subcode >= sizeof(suberr_cease_names)/sizeof(char *))
			uk = 1;
		else
			suberrname = suberr_cease_names[subcode];
		break;
	case ERR_HOLDTIMEREXPIRED:
		if (subcode != 0)
			uk = 1;
		break;
	case ERR_FSM:
		if (subcode >= sizeof(suberr_fsm_names)/sizeof(char *))
			uk = 1;
		else
			suberrname = suberr_fsm_names[subcode];
		break;
	default:
		printf("unknown errcode %u, subcode %u",
		    errcode, subcode);
		return;
	}

	if (uk)
		printf("%s, unknown subcode %u", errnames[errcode], subcode);
	else {
		if (suberrname == NULL)
			printf("%s", errnames[errcode]);
		else
			printf("%s, %s", errnames[errcode], suberrname);
	}
}

static int
show_mrt_capabilities(struct ibuf *b)
{
	uint8_t capa_code, capa_len;
	struct ibuf cbuf;

	while (ibuf_size(b) > 0) {
		if (ibuf_get_n8(b, &capa_code) == -1 ||
		    ibuf_get_n8(b, &capa_len) == -1 ||
		    ibuf_get_ibuf(b, capa_len, &cbuf) == -1) {
			printf("truncated capabilities");
			return (-1);
		}
		printf("\n        ");
		print_capability(capa_code, &cbuf);
	}
	return (0);
}

static void
show_mrt_open(struct ibuf *b)
{
	struct in_addr ina;
	uint32_t bgpid;
	uint16_t short_as, holdtime;
	uint8_t version, optparamlen;

	/* length check up to optparamlen already happened */
	if (ibuf_get_n8(b, &version) == -1 ||
	    ibuf_get_n16(b, &short_as) == -1 ||
	    ibuf_get_n16(b, &holdtime) == -1 ||
	    ibuf_get_n32(b, &bgpid) == -1 ||
	    ibuf_get_n8(b, &optparamlen) == -1) {
 trunc:
		printf("truncated message");
		return;
	}

	printf("\n    ");
	ina.s_addr = htonl(bgpid);
	printf("Version: %d AS: %u Holdtime: %u BGP Id: %s Paramlen: %u",
	    version, short_as, holdtime, inet_ntoa(ina), optparamlen);
	if (optparamlen != ibuf_size(b)) {
		/* XXX missing support for RFC9072 */
		printf("optional parameter length mismatch");
		return;
	}
	while (ibuf_size(b) > 0) {
		uint8_t op_type, op_len;

		if (ibuf_get_n8(b, &op_type) == -1 ||
		    ibuf_get_n8(b, &op_len) == -1)
			goto trunc;

		printf("\n    ");
		switch (op_type) {
		case OPT_PARAM_CAPABILITIES:
			printf("Capabilities: %u bytes", op_len);
			if (show_mrt_capabilities(b) == -1)
				return;
			break;
		case OPT_PARAM_AUTH:
		default:
			printf("unsupported optional parameter: type %u",
			    op_type);
			return;
		}
	}
}

static void
show_mrt_notification(struct ibuf *b)
{
	char reason[REASON_LEN];
	uint8_t errcode, subcode, reason_len, c;
	size_t i, len;

	if (ibuf_get_n8(b, &errcode) == -1 ||
	    ibuf_get_n8(b, &subcode) == -1) {
 trunc:
		printf("truncated message");
		return;
	}

	printf("\n    ");
	print_notification(errcode, subcode);

	if (errcode == ERR_CEASE && (subcode == ERR_CEASE_ADMIN_DOWN ||
	    subcode == ERR_CEASE_ADMIN_RESET)) {
		if (ibuf_size(b) > 1) {
			if (ibuf_get_n8(b, &reason_len) == -1)
				goto trunc;
			if (ibuf_get(b, reason, reason_len) == -1)
				goto trunc;
			reason[reason_len] = '\0';
			printf("shutdown reason: \"%s\"",
			    log_reason(reason));
		}
	}
	if (errcode == ERR_OPEN && subcode == ERR_OPEN_CAPA) {
		if (show_mrt_capabilities(b) == -1)
			return;
	}

	if (ibuf_size(b) > 0) {
		len = ibuf_size(b);
		printf("\n    additional data, %zu bytes", len);
		for (i = 0; i < len; i++) {
			if (i % 16 == 0)
				printf("\n    ");
			if (i % 8 == 0)
				printf("   ");
			if (ibuf_get_n8(b, &c) == -1)
				goto trunc;
			printf(" %02X", c);
		}
	}
}

/* XXX this function does not handle JSON output */
static void
show_mrt_update(struct ibuf *b, int reqflags, int addpath)
{
	struct bgpd_addr prefix;
	struct ibuf wbuf, abuf;
	uint32_t pathid;
	uint16_t wlen, alen;
	uint8_t prefixlen;

	if (ibuf_get_n16(b, &wlen) == -1 ||
	    ibuf_get_ibuf(b, wlen, &wbuf) == -1)
		goto trunc;

	if (wlen > 0) {
		printf("\n     Withdrawn prefixes:");
		while (ibuf_size(&wbuf) > 0) {
			if (addpath)
				if (ibuf_get_n32(&wbuf, &pathid) == -1)
					goto trunc;
			if (nlri_get_prefix(&wbuf, &prefix, &prefixlen) == -1)
				goto trunc;

			printf(" %s/%u", log_addr(&prefix), prefixlen);
			if (addpath)
				printf(" path-id %u", pathid);
		}
	}

	if (ibuf_get_n16(b, &alen) == -1 ||
	    ibuf_get_ibuf(b, alen, &abuf) == -1)
		goto trunc;

	printf("\n");
	/* alen attributes here */
	while (ibuf_size(&abuf) > 0) {
		struct ibuf attrbuf;
		uint16_t attrlen;
		uint8_t flags;

		ibuf_from_ibuf(&abuf, &attrbuf);
		if (ibuf_get_n8(&attrbuf, &flags) == -1 ||
		    ibuf_skip(&attrbuf, 1) == -1)
			goto trunc;

		/* get the attribute length */
		if (flags & ATTR_EXTLEN) {
			if (ibuf_get_n16(&attrbuf, &attrlen) == -1)
				goto trunc;
		} else {
			uint8_t tmp;
			if (ibuf_get_n8(&attrbuf, &tmp) == -1)
				goto trunc;
			attrlen = tmp;
		}
		if (ibuf_truncate(&attrbuf, attrlen) == -1)
			goto trunc;
		ibuf_rewind(&attrbuf);
		if (ibuf_skip(&abuf, ibuf_size(&attrbuf)) == -1)
			goto trunc;

		output->attr(&attrbuf, reqflags, addpath);
	}

	if (ibuf_size(b) > 0) {
		printf("    NLRI prefixes:");
		while (ibuf_size(b) > 0) {
			if (addpath)
				if (ibuf_get_n32(b, &pathid) == -1)
					goto trunc;
			if (nlri_get_prefix(b, &prefix, &prefixlen) == -1)
				goto trunc;

			printf(" %s/%u", log_addr(&prefix), prefixlen);
			if (addpath)
				printf(" path-id %u", pathid);
		}
	}
	return;

 trunc:
	printf("truncated message");
}

void
show_mrt_msg(struct mrt_bgp_msg *mm, void *arg)
{
	static const uint8_t marker[MSGSIZE_HEADER_MARKER] = {
	    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	uint8_t m[MSGSIZE_HEADER_MARKER];
	struct ibuf *b;
	uint16_t len;
	uint8_t type;
	struct ctl_show_rib_request *req = arg;

	printf("%s %s[%u] -> ", fmt_time(&mm->time),
	    log_addr(&mm->src), mm->src_as);
	printf("%s[%u]: size %zu%s ", log_addr(&mm->dst), mm->dst_as,
	    ibuf_size(&mm->msg), mm->add_path ? " addpath" : "");
	b = &mm->msg;

	if (ibuf_get(b, m, sizeof(m)) == -1) {
		printf("bad message: short header\n");
		return;
	}

	/* parse BGP message header */
	if (memcmp(m, marker, sizeof(marker))) {
		printf("incorrect marker in BGP message\n");
		return;
	}

	if (ibuf_get_n16(b, &len) == -1 ||
	    ibuf_get_n8(b, &type) == -1) {
		printf("bad message: short header\n");
		return;
	}

	if (len < MSGSIZE_HEADER || len > MAX_PKTSIZE) {
		printf("illegal header length: %u byte\n", len);
		return;
	}

	switch (type) {
	case BGP_OPEN:
		printf("%s ", msgtypenames[type]);
		if (len < MSGSIZE_OPEN_MIN) {
			printf("bad length: %u bytes\n", len);
			return;
		}
		show_mrt_open(b);
		break;
	case BGP_NOTIFICATION:
		printf("%s ", msgtypenames[type]);
		if (len < MSGSIZE_NOTIFICATION_MIN) {
			printf("bad length: %u bytes\n", len);
			return;
		}
		show_mrt_notification(b);
		break;
	case BGP_UPDATE:
		printf("%s ", msgtypenames[type]);
		if (len < MSGSIZE_UPDATE_MIN) {
			printf("bad length: %u bytes\n", len);
			return;
		}
		show_mrt_update(b, req->flags, mm->add_path);
		break;
	case BGP_KEEPALIVE:
		printf("%s ", msgtypenames[type]);
		if (len != MSGSIZE_KEEPALIVE) {
			printf("bad length: %u bytes\n", len);
			return;
		}
		/* nothing */
		break;
	case BGP_RREFRESH:
		printf("%s ", msgtypenames[type]);
		if (len != MSGSIZE_RREFRESH) {
			printf("bad length: %u bytes\n", len);
			return;
		}
		print_afi(b);
		break;
	default:
		printf("unknown type %u\n", type);
		return;
	}
	printf("\n");
}

const char *
msg_type(uint8_t type)
{
	if (type >= sizeof(msgtypenames)/sizeof(msgtypenames[0]))
		return "BAD";
	return (msgtypenames[type]);
}

int
match_aspath(void *data, uint16_t len, struct filter_as *f)
{
	uint8_t		*seg;
	int		 final;
	uint16_t	 seg_size;
	uint8_t		 i, seg_len;
	uint32_t	 as = 0;

	if (f->type == AS_EMPTY) {
		if (len == 0)
			return (1);
		else
			return (0);
	}

	seg = data;

	/* just check the leftmost AS */
	if (f->type == AS_PEER && len >= 6) {
		as = aspath_extract(seg, 0);
		if (f->as_min == as)
			return (1);
		else
			return (0);
	}

	for (; len >= 6; len -= seg_size, seg += seg_size) {
		seg_len = seg[1];
		seg_size = 2 + sizeof(uint32_t) * seg_len;

		final = (len == seg_size);

		if (f->type == AS_SOURCE) {
			/*
			 * Just extract the rightmost AS
			 * but if that segment is an AS_SET then the rightmost
			 * AS of a previous AS_SEQUENCE segment should be used.
			 * Because of that just look at AS_SEQUENCE segments.
			 */
			if (seg[0] == AS_SEQUENCE)
				as = aspath_extract(seg, seg_len - 1);
			/* not yet in the final segment */
			if (!final)
				continue;
			if (f->as_min == as)
				return (1);
			else
				return (0);
		}
		/* AS_TRANSIT or AS_ALL */
		for (i = 0; i < seg_len; i++) {
			/*
			 * the source (rightmost) AS is excluded from
			 * AS_TRANSIT matches.
			 */
			if (final && i == seg_len - 1 && f->type == AS_TRANSIT)
				return (0);
			as = aspath_extract(seg, i);
			if (f->as_min == as)
				return (1);
		}
	}
	return (0);
}

static void
component_finish(int type, uint8_t *data, int len)
{
	uint8_t *last;
	int i;

	switch (type) {
	case FLOWSPEC_TYPE_DEST:
	case FLOWSPEC_TYPE_SOURCE:
		/* nothing todo */
		return;
	default:
		break;
	}

	i = 0;
	do {
		last = data + i;
		i += FLOWSPEC_OP_LEN(*last) + 1;
	} while (i < len);
	*last |= FLOWSPEC_OP_EOL;
}

static void
push_prefix(struct parse_result *r, int type, struct bgpd_addr *addr,
    uint8_t len)
{
	void *data;
	uint8_t *comp;
	int complen, l;

	switch (addr->aid) {
	case AID_UNSPEC:
		return;
	case AID_INET:
		complen = PREFIX_SIZE(len);
		data = &addr->v4;
		break;
	case AID_INET6:
		/* IPv6 includes an offset byte */
		complen = PREFIX_SIZE(len) + 1;
		data = &addr->v6;
		break;
	default:
		errx(1, "unsupported address family for flowspec address");
	}
	comp = malloc(complen);
	if (comp == NULL)
		err(1, NULL);

	l = 0;
	comp[l++] = len;
	if (addr->aid == AID_INET6)
		comp[l++] = 0;
	memcpy(comp + l, data, complen - l);

	r->flow.complen[type] = complen;
	r->flow.components[type] = comp;
}


struct flowspec *
res_to_flowspec(struct parse_result *r)
{
	struct flowspec *f;
	int i, len = 0;
	uint8_t aid;

	switch (r->aid) {
	case AID_INET:
		aid = AID_FLOWSPECv4;
		break;
	case AID_INET6:
		aid = AID_FLOWSPECv6;
		break;
	default:
		errx(1, "unsupported AFI %s for flowspec rule",
		    aid2str(r->aid));
	}

	push_prefix(r, FLOWSPEC_TYPE_DEST, &r->flow.dst, r->flow.dstlen);
	push_prefix(r, FLOWSPEC_TYPE_SOURCE, &r->flow.src, r->flow.srclen);

	for (i = FLOWSPEC_TYPE_MIN; i < FLOWSPEC_TYPE_MAX; i++)
		if (r->flow.components[i] != NULL)
			len += r->flow.complen[i] + 1;

	if (len == 0)
		errx(1, "no flowspec rule defined");

	f = malloc(FLOWSPEC_SIZE + len);
	if (f == NULL)
		err(1, NULL);
	memset(f, 0, FLOWSPEC_SIZE);

	f->aid = aid;
	f->len = len;

	len = 0;
	for (i = FLOWSPEC_TYPE_MIN; i < FLOWSPEC_TYPE_MAX; i++)
		if (r->flow.components[i] != NULL) {
			f->data[len++] = i;
			component_finish(i, r->flow.components[i],
			    r->flow.complen[i]);
			memcpy(f->data + len, r->flow.components[i],
			    r->flow.complen[i]);
			len += r->flow.complen[i];
		}

	return f;
}
