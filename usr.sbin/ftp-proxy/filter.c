/*	$OpenBSD: filter.c,v 1.21 2015/01/21 21:50:33 deraadt Exp $ */

/*
 * Copyright (c) 2004, 2005 Camiel Dobbelaar, <cd@sentia.nl>
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

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/pfvar.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "filter.h"

/* From netinet/in.h, but only _KERNEL_ gets them. */
#define satosin(sa)	((struct sockaddr_in *)(sa))
#define satosin6(sa)	((struct sockaddr_in6 *)(sa))

int add_addr(struct sockaddr *, struct pf_pool *);
int prepare_rule(u_int32_t, struct sockaddr *, struct sockaddr *,
    u_int16_t);

static struct pfioc_rule	pfr;
static struct pfioc_trans	pft;
static struct pfioc_trans_e	pfte;
static int dev, rule_log;
static char *qname, *tagname;

int
add_addr(struct sockaddr *addr, struct pf_pool *pfp)
{
	if (addr->sa_family == AF_INET) {
		memcpy(&pfp->addr.v.a.addr.v4,
		    &satosin(addr)->sin_addr.s_addr, 4);
		memset(&pfp->addr.v.a.mask.addr8, 255, 4);
	} else {
		memcpy(&pfp->addr.v.a.addr.v6,
		    &satosin6(addr)->sin6_addr.s6_addr, 16);
		memset(&pfp->addr.v.a.mask.addr8, 255, 16);
	}
	pfp->addr.type = PF_ADDR_ADDRMASK;
	return (0);
}

int
add_nat(u_int32_t id, struct sockaddr *src, int s_rd, struct sockaddr *dst,
    u_int16_t d_port, struct sockaddr *nat, u_int16_t nat_range_low,
    u_int16_t nat_range_high)
{
	if (!src || !dst || !d_port || !nat || !nat_range_low ||
	    !nat_range_high || (src->sa_family != nat->sa_family)) {
		errno = EINVAL;
		return (-1);
	}

	if (prepare_rule(id, src, dst, d_port) == -1)
		return (-1);

	if (add_addr(nat, &pfr.rule.nat) == -1)
		return (-1);

	pfr.rule.direction = PF_OUT;
	pfr.rule.onrdomain = s_rd;
	pfr.rule.rtableid = -1;
	pfr.rule.nat.proxy_port[0] = nat_range_low;
	pfr.rule.nat.proxy_port[1] = nat_range_high;
	if (ioctl(dev, DIOCADDRULE, &pfr) == -1)
		return (-1);

	return (0);
}

int
add_rdr(u_int32_t id, struct sockaddr *src, int s_rd, struct sockaddr *dst,
    u_int16_t d_port, struct sockaddr *rdr, u_int16_t rdr_port, int d_rd)
{
	if (!src || !dst || !d_port || !rdr || !rdr_port ||
	    (src->sa_family != rdr->sa_family)) {
		errno = EINVAL;
		return (-1);
	}

	if (prepare_rule(id, src, dst, d_port) == -1)
		return (-1);

	if (add_addr(rdr, &pfr.rule.rdr) == -1)
		return (-1);

	pfr.rule.direction = PF_IN;
	pfr.rule.onrdomain = s_rd;
	pfr.rule.rtableid = d_rd;
	pfr.rule.rdr.proxy_port[0] = rdr_port;
	if (ioctl(dev, DIOCADDRULE, &pfr) == -1)
		return (-1);

	return (0);
}

int
do_commit(void)
{
	if (ioctl(dev, DIOCXCOMMIT, &pft) == -1)
		return (-1);

	return (0);
}

int
do_rollback(void)
{
	if (ioctl(dev, DIOCXROLLBACK, &pft) == -1)
		return (-1);

	return (0);
}

void
init_filter(char *opt_qname, char *opt_tagname, int opt_verbose)
{
	struct pf_status status;

	qname = opt_qname;
	tagname = opt_tagname;

	if (opt_verbose == 1)
		rule_log = PF_LOG;
	else if (opt_verbose == 2)
		rule_log = PF_LOG_ALL;

	dev = open("/dev/pf", O_RDWR);
	if (dev == -1)
		err(1, "open /dev/pf");
	if (ioctl(dev, DIOCGETSTATUS, &status) == -1)
		err(1, "DIOCGETSTATUS");
	if (!status.running)
		errx(1, "pf is disabled");
}

int
prepare_commit(u_int32_t id)
{
	char an[PF_ANCHOR_NAME_SIZE];

	memset(&pft, 0, sizeof pft);
	pft.size = 1;
	pft.esize = sizeof pfte;
	pft.array = &pfte;

	snprintf(an, PF_ANCHOR_NAME_SIZE, "%s/%d.%d", FTP_PROXY_ANCHOR,
	    getpid(), id);
	memset(&pfte, 0, sizeof pfte);
	strlcpy(pfte.anchor, an, PF_ANCHOR_NAME_SIZE);
	pfte.type = PF_TRANS_RULESET;

	if (ioctl(dev, DIOCXBEGIN, &pft) == -1)
		return (-1);

	return (0);
}

int
prepare_rule(u_int32_t id, struct sockaddr *src,
    struct sockaddr *dst, u_int16_t d_port)
{
	char an[PF_ANCHOR_NAME_SIZE];

	if ((src->sa_family != AF_INET && src->sa_family != AF_INET6) ||
	    (src->sa_family != dst->sa_family)) {
		errno = EPROTONOSUPPORT;
		return (-1);
	}

	memset(&pfr, 0, sizeof pfr);
	snprintf(an, PF_ANCHOR_NAME_SIZE, "%s/%d.%d", FTP_PROXY_ANCHOR,
	    getpid(), id);
	strlcpy(pfr.anchor, an, PF_ANCHOR_NAME_SIZE);

	pfr.ticket = pfte.ticket;

	/* Generic for all rule types. */
	pfr.rule.af = src->sa_family;
	pfr.rule.proto = IPPROTO_TCP;
	pfr.rule.src.addr.type = PF_ADDR_ADDRMASK;
	pfr.rule.dst.addr.type = PF_ADDR_ADDRMASK;
	pfr.rule.nat.addr.type = PF_ADDR_NONE;
	pfr.rule.rdr.addr.type = PF_ADDR_NONE;

	if (src->sa_family == AF_INET) {
		memcpy(&pfr.rule.src.addr.v.a.addr.v4,
		    &satosin(src)->sin_addr.s_addr, 4);
		memset(&pfr.rule.src.addr.v.a.mask.addr8, 255, 4);
		memcpy(&pfr.rule.dst.addr.v.a.addr.v4,
		    &satosin(dst)->sin_addr.s_addr, 4);
		memset(&pfr.rule.dst.addr.v.a.mask.addr8, 255, 4);
	} else {
		memcpy(&pfr.rule.src.addr.v.a.addr.v6,
		    &satosin6(src)->sin6_addr.s6_addr, 16);
		memset(&pfr.rule.src.addr.v.a.mask.addr8, 255, 16);
		memcpy(&pfr.rule.dst.addr.v.a.addr.v6,
		    &satosin6(dst)->sin6_addr.s6_addr, 16);
		memset(&pfr.rule.dst.addr.v.a.mask.addr8, 255, 16);
	}
	pfr.rule.dst.port_op = PF_OP_EQ;
	pfr.rule.dst.port[0] = htons(d_port);

	/*
	 * pass [quick] [log] inet[6] proto tcp \
	 *     from $src to $dst port = $d_port flags S/SA keep state
	 *     (max 1) [queue qname] [tag tagname]
	 */
	if (tagname != NULL)
		pfr.rule.action = PF_MATCH;
	else
		pfr.rule.action = PF_PASS;
	pfr.rule.quick = 1;
	pfr.rule.log = rule_log;
	pfr.rule.keep_state = 1;
	pfr.rule.flags = TH_SYN;
	pfr.rule.flagset = (TH_SYN|TH_ACK);
	pfr.rule.max_states = 1;
	if (qname != NULL)
		strlcpy(pfr.rule.qname, qname, sizeof pfr.rule.qname);
	if (tagname != NULL) {
		pfr.rule.quick = 0;
		strlcpy(pfr.rule.tagname, tagname,
                               sizeof pfr.rule.tagname);
	}

	return (0);
}
