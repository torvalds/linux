/*	$OpenBSD: pfe_filter.c,v 1.66 2024/06/17 08:02:57 sashan Exp $	*/

/*
 * Copyright (c) 2006 Pierre-Yves Ritschard <pyr@openbsd.org>
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
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <net/pfvar.h>

#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))

#include "relayd.h"

int	 transaction_init(struct relayd *, const char *);
int	 transaction_commit(struct relayd *);
void	 kill_tables(struct relayd *);
int	 kill_srcnodes(struct relayd *, struct table *);

void
init_tables(struct relayd *env)
{
	int			 i;
	struct rdr		*rdr;
	struct pfr_table	*tables;
	struct pfioc_table	 io;

	if (!(env->sc_conf.flags & F_NEEDPF))
		return;

	if ((tables = calloc(env->sc_rdrcount, sizeof(*tables))) == NULL)
		fatal("calloc");
	i = 0;

	TAILQ_FOREACH(rdr, env->sc_rdrs, entry) {
		if (strlcpy(tables[i].pfrt_anchor, RELAYD_ANCHOR "/",
		    sizeof(tables[i].pfrt_anchor)) >= PF_ANCHOR_NAME_SIZE)
			goto toolong;
		if (strlcat(tables[i].pfrt_anchor, rdr->conf.name,
		    sizeof(tables[i].pfrt_anchor)) >= PF_ANCHOR_NAME_SIZE)
			goto toolong;
		if (strlcpy(tables[i].pfrt_name, rdr->conf.name,
		    sizeof(tables[i].pfrt_name)) >=
		    sizeof(tables[i].pfrt_name))
			goto toolong;
		tables[i].pfrt_flags |= PFR_TFLAG_PERSIST;
		i++;
	}
	if (i != env->sc_rdrcount)
		fatalx("%s: table count modified", __func__);

	memset(&io, 0, sizeof(io));
	io.pfrio_size = env->sc_rdrcount;
	io.pfrio_esize = sizeof(*tables);
	io.pfrio_buffer = tables;

	if (ioctl(env->sc_pf->dev, DIOCRADDTABLES, &io) == -1)
		fatal("%s: cannot create tables", __func__);
	log_debug("%s: created %d tables", __func__, io.pfrio_nadd);

	free(tables);

	if (io.pfrio_nadd == env->sc_rdrcount)
		return;

	/*
	 * clear all tables, since some already existed
	 */
	TAILQ_FOREACH(rdr, env->sc_rdrs, entry)
		flush_table(env, rdr);

	return;

 toolong:
	fatal("%s: name too long", __func__);
}

void
kill_tables(struct relayd *env)
{
	struct pfioc_table	 io;
	struct rdr		*rdr;
	int			 cnt = 0;

	if (!(env->sc_conf.flags & F_NEEDPF))
		return;

	TAILQ_FOREACH(rdr, env->sc_rdrs, entry) {
		memset(&io, 0, sizeof(io));
		if (strlcpy(io.pfrio_table.pfrt_anchor, RELAYD_ANCHOR "/",
		    sizeof(io.pfrio_table.pfrt_anchor)) >= PF_ANCHOR_NAME_SIZE)
			goto toolong;
		if (strlcat(io.pfrio_table.pfrt_anchor, rdr->conf.name,
		    sizeof(io.pfrio_table.pfrt_anchor)) >= PF_ANCHOR_NAME_SIZE)
			goto toolong;
		if (ioctl(env->sc_pf->dev, DIOCRCLRTABLES, &io) == -1)
			fatal("%s: ioctl failed", __func__);
		cnt += io.pfrio_ndel;
	}
	log_debug("%s: deleted %d tables", __func__, cnt);
	return;

 toolong:
	fatal("%s: name too long", __func__);
}

void
sync_table(struct relayd *env, struct rdr *rdr, struct table *table)
{
	int			 i, cnt = 0;
	struct pfioc_table	 io;
	struct pfr_addr		*addlist;
	struct sockaddr_in	*sain;
	struct sockaddr_in6	*sain6;
	struct host		*host;

	if (!(env->sc_conf.flags & F_NEEDPF))
		return;

	if (table == NULL)
		return;

	if (table->up == 0) {
		flush_table(env, rdr);
		return;
	}

	if ((addlist = calloc(table->up, sizeof(*addlist))) == NULL)
		fatal("calloc");

	memset(&io, 0, sizeof(io));
	io.pfrio_esize = sizeof(struct pfr_addr);
	io.pfrio_size = table->up;
	io.pfrio_size2 = 0;
	io.pfrio_buffer = addlist;
	if (strlcpy(io.pfrio_table.pfrt_anchor, RELAYD_ANCHOR "/",
	    sizeof(io.pfrio_table.pfrt_anchor)) >= PF_ANCHOR_NAME_SIZE)
		goto toolong;
	if (strlcat(io.pfrio_table.pfrt_anchor, rdr->conf.name,
	    sizeof(io.pfrio_table.pfrt_anchor)) >= PF_ANCHOR_NAME_SIZE)
		goto toolong;
	if (strlcpy(io.pfrio_table.pfrt_name, rdr->conf.name,
	    sizeof(io.pfrio_table.pfrt_name)) >=
	    sizeof(io.pfrio_table.pfrt_name))
		goto toolong;

	i = 0;
	TAILQ_FOREACH(host, &table->hosts, entry) {
		if (host->up != HOST_UP)
			continue;
		memset(&(addlist[i]), 0, sizeof(addlist[i]));
		switch (host->conf.ss.ss_family) {
		case AF_INET:
			sain = (struct sockaddr_in *)&host->conf.ss;
			addlist[i].pfra_af = AF_INET;
			memcpy(&(addlist[i].pfra_ip4addr), &sain->sin_addr,
			    sizeof(sain->sin_addr));
			addlist[i].pfra_net = 32;
			break;
		case AF_INET6:
			sain6 = (struct sockaddr_in6 *)&host->conf.ss;
			addlist[i].pfra_af = AF_INET6;
			memcpy(&(addlist[i].pfra_ip6addr), &sain6->sin6_addr,
			    sizeof(sain6->sin6_addr));
			addlist[i].pfra_net = 128;
			break;
		default:
			fatalx("%s: unknown address family", __func__);
			break;
		}
		i++;
	}
	if (i != table->up)
		fatalx("%s: desynchronized", __func__);

	if (ioctl(env->sc_pf->dev, DIOCRSETADDRS, &io) == -1)
		fatal("%s: cannot set address list", __func__);
	if (rdr->conf.flags & F_STICKY)
		cnt = kill_srcnodes(env, table);
	free(addlist);

	if (env->sc_conf.opts & RELAYD_OPT_LOGUPDATE)
		log_info("table %s: %d added, %d deleted, "
		    "%d changed, %d killed", io.pfrio_table.pfrt_name,
		    io.pfrio_nadd, io.pfrio_ndel, io.pfrio_nchange, cnt);
	return;

 toolong:
	fatal("%s: name too long", __func__);
}

int
kill_srcnodes(struct relayd *env, struct table *table)
{
	struct host			*host;
	struct pfioc_src_node_kill	 psnk;
	int				 cnt = 0;
	struct sockaddr_in		*sain;
	struct sockaddr_in6		*sain6;

	bzero(&psnk, sizeof(psnk));

	/* Only match the destination address, source mask will be zero */
	memset(&psnk.psnk_dst.addr.v.a.mask, 0xff,
	    sizeof(psnk.psnk_dst.addr.v.a.mask));

	TAILQ_FOREACH(host, &table->hosts, entry) {
		if (host->up != HOST_DOWN)
			continue;

		switch (host->conf.ss.ss_family) {
		case AF_INET:
		sain = (struct sockaddr_in *)&host->conf.ss;
			bcopy(&sain->sin_addr,
			    &psnk.psnk_dst.addr.v.a.addr.v4,
			    sizeof(psnk.psnk_dst.addr.v.a.addr.v4));
			break;
		case AF_INET6:
			sain6 = (struct sockaddr_in6 *)&host->conf.ss;
			bcopy(&sain6->sin6_addr,
			    &psnk.psnk_dst.addr.v.a.addr.v6,
			    sizeof(psnk.psnk_dst.addr.v.a.addr.v6));
			break;
		default:
			fatalx("%s: unknown address family", __func__);
			break;
		}

		psnk.psnk_af = host->conf.ss.ss_family;
		psnk.psnk_killed = 0;

		if (ioctl(env->sc_pf->dev,
		    DIOCKILLSRCNODES, &psnk) == -1)
			fatal("%s: cannot kill src nodes", __func__);
		cnt += psnk.psnk_killed;
	}

	return (cnt);
}

void
flush_table(struct relayd *env, struct rdr *rdr)
{
	struct pfioc_table	io;

	if (!(env->sc_conf.flags & F_NEEDPF))
		return;

	memset(&io, 0, sizeof(io));
	if (strlcpy(io.pfrio_table.pfrt_anchor, RELAYD_ANCHOR "/",
	    sizeof(io.pfrio_table.pfrt_anchor)) >= PF_ANCHOR_NAME_SIZE)
		goto toolong;
	if (strlcat(io.pfrio_table.pfrt_anchor, rdr->conf.name,
	    sizeof(io.pfrio_table.pfrt_anchor)) >= PF_ANCHOR_NAME_SIZE)
		goto toolong;
	if (strlcpy(io.pfrio_table.pfrt_name, rdr->conf.name,
	    sizeof(io.pfrio_table.pfrt_name)) >=
	    sizeof(io.pfrio_table.pfrt_name))
		goto toolong;
	if (ioctl(env->sc_pf->dev, DIOCRCLRADDRS, &io) == -1)
		fatal("%s: cannot flush table addresses", __func__);

	io.pfrio_esize = sizeof(io.pfrio_table);
	io.pfrio_size = 1;
	io.pfrio_buffer = &io.pfrio_table;
	if (ioctl(env->sc_pf->dev, DIOCRCLRTSTATS, &io) == -1)
		fatal("%s: cannot flush table stats", __func__);

	log_debug("%s: flushed table %s", __func__, rdr->conf.name);
	return;

 toolong:
	fatal("%s: name too long", __func__);
}

int
transaction_init(struct relayd *env, const char *anchor)
{
	env->sc_pf->pft.size = 1;
	env->sc_pf->pft.esize = sizeof(env->sc_pf->pfte);
	env->sc_pf->pft.array = &env->sc_pf->pfte;

	bzero(&env->sc_pf->pfte, sizeof(env->sc_pf->pfte));
	(void)strlcpy(env->sc_pf->pfte.anchor,
	    anchor, PF_ANCHOR_NAME_SIZE);
	env->sc_pf->pfte.type = PF_TRANS_RULESET;

	if (ioctl(env->sc_pf->dev, DIOCXBEGIN,
	    &env->sc_pf->pft) == -1)
		return (-1);

	return (0);
}

int
transaction_commit(struct relayd *env)
{
	if (ioctl(env->sc_pf->dev, DIOCXCOMMIT,
	    &env->sc_pf->pft) == -1)
		return (-1);

	return (0);
}

void
sync_ruleset(struct relayd *env, struct rdr *rdr, int enable)
{
	struct pfioc_rule	 rio;
	struct sockaddr_in	*sain;
	struct sockaddr_in6	*sain6;
	struct address		*address;
	char			 anchor[PF_ANCHOR_NAME_SIZE];
	struct table		*t = rdr->table;

	if ((env->sc_conf.flags & F_NEEDPF) == 0)
		return;

	bzero(anchor, sizeof(anchor));
	if (strlcpy(anchor, RELAYD_ANCHOR "/", sizeof(anchor)) >=
	    PF_ANCHOR_NAME_SIZE)
		goto toolong;
	if (strlcat(anchor, rdr->conf.name, sizeof(anchor)) >=
	    PF_ANCHOR_NAME_SIZE)
		goto toolong;
	if (transaction_init(env, anchor) == -1) {
		log_warn("%s: transaction init failed", __func__);
		return;
	}

	if (!enable) {
		if (transaction_commit(env) == -1)
			log_warn("%s: remove rules transaction failed",
			    __func__);
		else
			log_debug("%s: rules removed", __func__);
		return;
	}

	TAILQ_FOREACH(address, &rdr->virts, entry) {
		memset(&rio, 0, sizeof(rio));
		(void)strlcpy(rio.anchor, anchor, sizeof(rio.anchor));

		if (rdr->conf.flags & F_MATCH) {
			rio.rule.action = PF_MATCH;
			rio.rule.quick = 0;
		} else {
			rio.rule.action = PF_PASS;
			rio.rule.quick = 1; /* force first match */
		}
		rio.rule.direction = PF_IN;
		rio.rule.keep_state = PF_STATE_NORMAL;

		if (rdr->conf.flags & F_PFLOG)
			rio.rule.log = 1;
		else
			rio.rule.log = 0; /* allow change via reload */

		switch (t->conf.fwdmode) {
		case FWD_NORMAL:
			/* traditional redirection */
			if (address->ipproto == IPPROTO_TCP) {
				rio.rule.flags = TH_SYN;
				rio.rule.flagset = (TH_SYN|TH_ACK);
			}
			break;
		case FWD_ROUTE:
			/* re-route with pf for DSR (direct server return) */
			rio.rule.rt = PF_ROUTETO;

			/* Use sloppy state handling for half connections */
			rio.rule.rule_flag = PFRULE_STATESLOPPY;
			break;
		default:
			fatalx("%s: invalid forward mode", __func__);
			/* NOTREACHED */
		}

		rio.ticket = env->sc_pf->pfte.ticket;

		rio.rule.af = address->ss.ss_family;
		rio.rule.proto = address->ipproto;
		rio.rule.src.addr.type = PF_ADDR_ADDRMASK;
		rio.rule.dst.addr.type = PF_ADDR_ADDRMASK;
		rio.rule.dst.port_op = address->port.op;
		rio.rule.dst.port[0] = address->port.val[0];
		rio.rule.dst.port[1] = address->port.val[1];
		rio.rule.rtableid = -1; /* stay in the main routing table */
		rio.rule.onrdomain = env->sc_rtable;
		DPRINTF("%s rtable %d",__func__,env->sc_rtable);

		if (rio.rule.proto == IPPROTO_TCP)
			rio.rule.timeout[PFTM_TCP_ESTABLISHED] =
			    (u_int32_t)MINIMUM(rdr->conf.timeout.tv_sec,
			    INT_MAX);

		if (strlen(rdr->conf.tag))
			(void)strlcpy(rio.rule.tagname, rdr->conf.tag,
			    sizeof(rio.rule.tagname));
		if (strlen(address->ifname))
			(void)strlcpy(rio.rule.ifname, address->ifname,
			    sizeof(rio.rule.ifname));

		if (address->ss.ss_family == AF_INET) {
			sain = (struct sockaddr_in *)&address->ss;

			rio.rule.dst.addr.v.a.addr.addr32[0] =
			    sain->sin_addr.s_addr;
			rio.rule.dst.addr.v.a.mask.addr32[0] = 0xffffffff;
		} else {
			sain6 = (struct sockaddr_in6 *)&address->ss;

			memcpy(&rio.rule.dst.addr.v.a.addr.v6,
			    &sain6->sin6_addr.s6_addr,
			    sizeof(sain6->sin6_addr.s6_addr));
			memset(&rio.rule.dst.addr.v.a.mask.addr8, 0xff, 16);
		}

		rio.rule.nat.addr.type = PF_ADDR_NONE;
		rio.rule.rdr.addr.type = PF_ADDR_TABLE;
		if (strlen(t->conf.ifname))
			(void)strlcpy(rio.rule.rdr.ifname, t->conf.ifname,
			    sizeof(rio.rule.rdr.ifname));
		if (strlcpy(rio.rule.rdr.addr.v.tblname, rdr->conf.name,
		    sizeof(rio.rule.rdr.addr.v.tblname)) >=
		    sizeof(rio.rule.rdr.addr.v.tblname))
			fatal("%s: table name too long", __func__);

		if (address->port.op == PF_OP_EQ ||
		    rdr->table->conf.flags & F_PORT) {
			rio.rule.rdr.proxy_port[0] =
			    ntohs(rdr->table->conf.port);
			rio.rule.rdr.port_op = PF_OP_EQ;
		}

		switch (rdr->conf.mode) {
		case RELAY_DSTMODE_RANDOM:
			rio.rule.rdr.opts = PF_POOL_RANDOM;
			break;
		case RELAY_DSTMODE_ROUNDROBIN:
			rio.rule.rdr.opts = PF_POOL_ROUNDROBIN;
			break;
		case RELAY_DSTMODE_SRCHASH:
			rio.rule.rdr.opts = PF_POOL_SRCHASH;
			break;
		case RELAY_DSTMODE_LEASTSTATES:
			rio.rule.rdr.opts = PF_POOL_LEASTSTATES;
			break;
		default:
			fatalx("%s: unsupported mode", __func__);
			/* NOTREACHED */
		}
		if (rdr->conf.flags & F_STICKY)
			rio.rule.rdr.opts |= PF_POOL_STICKYADDR;
		if (rdr->conf.flags & F_HASHKEY)
			memcpy(rio.rule.rdr.key.key32, rdr->conf.key.data,
			    sizeof(rio.rule.rdr.key.key32));

		if (rio.rule.rt == PF_ROUTETO) {
			memcpy(&rio.rule.route, &rio.rule.rdr,
			    sizeof(rio.rule.route));
			rio.rule.rdr.addr.type = PF_ADDR_NONE;
		}

		if (ioctl(env->sc_pf->dev, DIOCADDRULE, &rio) == -1)
			fatal("cannot add rule");
		log_debug("%s: rule added to anchor \"%s\"", __func__, anchor);
	}
	if (transaction_commit(env) == -1)
		log_warn("%s: add rules transaction failed", __func__);
	return;

 toolong:
	fatal("%s: name too long", __func__);
}

void
flush_rulesets(struct relayd *env)
{
	struct rdr	*rdr;
	char		 anchor[PF_ANCHOR_NAME_SIZE];

	if (!(env->sc_conf.flags & F_NEEDPF))
		return;

	kill_tables(env);
	TAILQ_FOREACH(rdr, env->sc_rdrs, entry) {
		if (strlcpy(anchor, RELAYD_ANCHOR "/", sizeof(anchor)) >=
		    PF_ANCHOR_NAME_SIZE)
			goto toolong;
		if (strlcat(anchor, rdr->conf.name, sizeof(anchor)) >=
		    PF_ANCHOR_NAME_SIZE)
			goto toolong;
		if (transaction_init(env, anchor) == -1 ||
		    transaction_commit(env) == -1)
			log_warn("%s: transaction for %s/ failed", __func__,
			    RELAYD_ANCHOR);
	}
	if (strlcpy(anchor, RELAYD_ANCHOR, sizeof(anchor)) >=
	    PF_ANCHOR_NAME_SIZE)
		goto toolong;
	if (transaction_init(env, anchor) == -1 ||
	    transaction_commit(env) == -1)
		log_warn("%s: transaction for %s failed", __func__,
		    RELAYD_ANCHOR);
	log_debug("%s: flushed rules", __func__);
	return;

 toolong:
	fatal("%s: name too long", __func__);
}

int
natlook(struct relayd *env, struct ctl_natlook *cnl)
{
	struct pfioc_natlook	 pnl;
	struct sockaddr_in	*in, *out;
	struct sockaddr_in6	*in6, *out6;
	char			 ibuf[BUFSIZ], obuf[BUFSIZ];

	if (!(env->sc_conf.flags & F_NEEDPF))
		return (0);

	bzero(&pnl, sizeof(pnl));

	if ((pnl.af = cnl->src.ss_family) != cnl->dst.ss_family)
		fatalx("%s: illegal address families", __func__);
	switch (pnl.af) {
	case AF_INET:
		in = (struct sockaddr_in *)&cnl->src;
		out = (struct sockaddr_in *)&cnl->dst;
		bcopy(&in->sin_addr, &pnl.saddr.v4, sizeof(pnl.saddr.v4));
		pnl.sport = in->sin_port;
		bcopy(&out->sin_addr, &pnl.daddr.v4, sizeof(pnl.daddr.v4));
		pnl.dport = out->sin_port;
		break;
	case AF_INET6:
		in6 = (struct sockaddr_in6 *)&cnl->src;
		out6 = (struct sockaddr_in6 *)&cnl->dst;
		bcopy(&in6->sin6_addr, &pnl.saddr.v6, sizeof(pnl.saddr.v6));
		pnl.sport = in6->sin6_port;
		bcopy(&out6->sin6_addr, &pnl.daddr.v6, sizeof(pnl.daddr.v6));
		pnl.dport = out6->sin6_port;
	}
	pnl.proto = cnl->proto;
	pnl.direction = PF_IN;
	cnl->in = 1;

	if (ioctl(env->sc_pf->dev, DIOCNATLOOK, &pnl) == -1) {
		pnl.direction = PF_OUT;
		cnl->in = 0;
		if (ioctl(env->sc_pf->dev, DIOCNATLOOK, &pnl) == -1) {
			log_debug("%s: ioctl: %s", __func__, strerror(errno));
			return (-1);
		}
	}

	inet_ntop(pnl.af, &pnl.rsaddr, ibuf, sizeof(ibuf));
	inet_ntop(pnl.af, &pnl.rdaddr, obuf, sizeof(obuf));
	log_debug("%s: %s %s:%d -> %s:%d", __func__,
	    pnl.direction == PF_IN ? "in" : "out",
	    ibuf, ntohs(pnl.rsport), obuf, ntohs(pnl.rdport));

	switch (pnl.af) {
	case AF_INET:
		in = (struct sockaddr_in *)&cnl->rsrc;
		out = (struct sockaddr_in *)&cnl->rdst;
		bcopy(&pnl.rsaddr.v4, &in->sin_addr, sizeof(in->sin_addr));
		in->sin_port = pnl.rsport;
		bcopy(&pnl.rdaddr.v4, &out->sin_addr, sizeof(out->sin_addr));
		out->sin_port = pnl.rdport;
		break;
	case AF_INET6:
		in6 = (struct sockaddr_in6 *)&cnl->rsrc;
		out6 = (struct sockaddr_in6 *)&cnl->rdst;
		bcopy(&pnl.rsaddr.v6, &in6->sin6_addr, sizeof(in6->sin6_addr));
		bcopy(&pnl.rdaddr.v6, &out6->sin6_addr,
		    sizeof(out6->sin6_addr));
		break;
	}
	cnl->rsrc.ss_family = pnl.af;
	cnl->rdst.ss_family = pnl.af;
	cnl->rsport = pnl.rsport;
	cnl->rdport = pnl.rdport;

	return (0);
}

u_int64_t
check_table(struct relayd *env, struct rdr *rdr, struct table *table)
{
	struct pfioc_table	 io;
	struct pfr_tstats	 tstats;

	if (table == NULL)
		return (0);

	bzero(&io, sizeof(io));
	io.pfrio_esize = sizeof(struct pfr_tstats);
	io.pfrio_size = 1;
	io.pfrio_buffer = &tstats;
	if (strlcpy(io.pfrio_table.pfrt_anchor, RELAYD_ANCHOR "/",
	    sizeof(io.pfrio_table.pfrt_anchor)) >= PF_ANCHOR_NAME_SIZE)
		goto toolong;
	if (strlcat(io.pfrio_table.pfrt_anchor, rdr->conf.name,
	    sizeof(io.pfrio_table.pfrt_anchor)) >= PF_ANCHOR_NAME_SIZE)
		goto toolong;
	if (strlcpy(io.pfrio_table.pfrt_name, rdr->conf.name,
	    sizeof(io.pfrio_table.pfrt_name)) >=
	    sizeof(io.pfrio_table.pfrt_name))
		goto toolong;

	if (ioctl(env->sc_pf->dev, DIOCRGETTSTATS, &io) == -1)
		fatal("%s: cannot get table stats for %s@%s", __func__,
		    io.pfrio_table.pfrt_name, io.pfrio_table.pfrt_anchor);

	return (tstats.pfrts_match);

 toolong:
	fatal("%s: name too long", __func__);
	return (0);
}
