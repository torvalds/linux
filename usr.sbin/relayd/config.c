/*	$OpenBSD: config.c,v 1.47 2024/10/31 13:37:44 claudio Exp $	*/

/*
 * Copyright (c) 2011 - 2014 Reyk Floeter <reyk@openbsd.org>
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
#include <sys/time.h>
#include <sys/uio.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <imsg.h>

#include "relayd.h"

int
config_init(struct relayd *env)
{
	struct privsep	*ps = env->sc_ps;
	u_int		 what;

	/* Global configuration */
	if (privsep_process == PROC_PARENT) {
		env->sc_conf.timeout.tv_sec = CHECK_TIMEOUT / 1000;
		env->sc_conf.timeout.tv_usec = (CHECK_TIMEOUT % 1000) * 1000;
		env->sc_conf.interval.tv_sec = CHECK_INTERVAL;
		env->sc_conf.interval.tv_usec = 0;
		env->sc_conf.prefork_relay = RELAY_NUMPROC;
		env->sc_conf.statinterval.tv_sec = RELAY_STATINTERVAL;
		env->sc_ps->ps_csock.cs_name = RELAYD_SOCKET;
	}

	ps->ps_what[PROC_PARENT] = CONFIG_ALL;
	ps->ps_what[PROC_PFE] = CONFIG_ALL & ~(CONFIG_PROTOS|CONFIG_CERTS);
	ps->ps_what[PROC_HCE] = CONFIG_TABLES;
	ps->ps_what[PROC_CA] = CONFIG_RELAYS|CONFIG_CERTS;
	ps->ps_what[PROC_RELAY] = CONFIG_RELAYS|CONFIG_CERTS|
	    CONFIG_TABLES|CONFIG_PROTOS|CONFIG_CA_ENGINE;

	/* Other configuration */
	what = ps->ps_what[privsep_process];
	if (what & CONFIG_TABLES) {
		if ((env->sc_tables =
		    calloc(1, sizeof(*env->sc_tables))) == NULL)
			return (-1);
		TAILQ_INIT(env->sc_tables);

		memset(&env->sc_empty_table, 0, sizeof(env->sc_empty_table));
		env->sc_empty_table.conf.id = EMPTY_TABLE;
		env->sc_empty_table.conf.flags |= F_DISABLE;
		(void)strlcpy(env->sc_empty_table.conf.name, "empty",
		    sizeof(env->sc_empty_table.conf.name));

	}
	if (what & CONFIG_RDRS) {
		if ((env->sc_rdrs =
		    calloc(1, sizeof(*env->sc_rdrs))) == NULL)
			return (-1);
		TAILQ_INIT(env->sc_rdrs);

	}
	if (what & CONFIG_RELAYS) {
		if ((env->sc_relays =
		    calloc(1, sizeof(*env->sc_relays))) == NULL)
			return (-1);
		TAILQ_INIT(env->sc_relays);

		if ((env->sc_certs =
		    calloc(1, sizeof(*env->sc_certs))) == NULL)
			return (-1);
		TAILQ_INIT(env->sc_certs);

		if ((env->sc_pkeys =
		    calloc(1, sizeof(*env->sc_pkeys))) == NULL)
			return (-1);
		TAILQ_INIT(env->sc_pkeys);
	}
	if (what & CONFIG_PROTOS) {
		if ((env->sc_protos =
		    calloc(1, sizeof(*env->sc_protos))) == NULL)
			return (-1);
		TAILQ_INIT(env->sc_protos);

		bzero(&env->sc_proto_default, sizeof(env->sc_proto_default));
		env->sc_proto_default.id = EMPTY_ID;
		env->sc_proto_default.flags = F_USED;
		env->sc_proto_default.tcpflags = TCPFLAG_DEFAULT;
		env->sc_proto_default.tcpbacklog = RELAY_BACKLOG;
		env->sc_proto_default.tlsflags = TLSFLAG_DEFAULT;
		TAILQ_INIT(&env->sc_proto_default.tlscerts);
		(void)strlcpy(env->sc_proto_default.tlsciphers,
		    TLSCIPHERS_DEFAULT,
		    sizeof(env->sc_proto_default.tlsciphers));
		(void)strlcpy(env->sc_proto_default.tlsecdhecurves,
		    TLSECDHECURVES_DEFAULT,
		    sizeof(env->sc_proto_default.tlsecdhecurves));
		(void)strlcpy(env->sc_proto_default.tlsdhparams,
		    TLSDHPARAM_DEFAULT,
		    sizeof(env->sc_proto_default.tlsdhparams));
		env->sc_proto_default.type = RELAY_PROTO_TCP;
		(void)strlcpy(env->sc_proto_default.name, "default",
		    sizeof(env->sc_proto_default.name));
	}
	if (what & CONFIG_RTS) {
		if ((env->sc_rts =
		    calloc(1, sizeof(*env->sc_rts))) == NULL)
			return (-1);
		TAILQ_INIT(env->sc_rts);
	}
	if (what & CONFIG_ROUTES) {
		if ((env->sc_routes =
		    calloc(1, sizeof(*env->sc_routes))) == NULL)
			return (-1);
		TAILQ_INIT(env->sc_routes);
	}

	return (0);
}

void
config_purge(struct relayd *env, u_int reset)
{
	struct privsep		*ps = env->sc_ps;
	struct table		*table;
	struct rdr		*rdr;
	struct address		*virt;
	struct protocol		*proto;
	struct relay_rule	*rule;
	struct relay		*rlay;
	struct netroute		*nr;
	struct router		*rt;
	struct ca_pkey		*pkey;
	struct keyname		*keyname;
	u_int			 what;

	what = ps->ps_what[privsep_process] & reset;

	if (what & CONFIG_TABLES && env->sc_tables != NULL) {
		while ((table = TAILQ_FIRST(env->sc_tables)) != NULL)
			purge_table(env, env->sc_tables, table);
		env->sc_tablecount = 0;
	}
	if (what & CONFIG_RDRS && env->sc_rdrs != NULL) {
		while ((rdr = TAILQ_FIRST(env->sc_rdrs)) != NULL) {
			TAILQ_REMOVE(env->sc_rdrs, rdr, entry);
			while ((virt = TAILQ_FIRST(&rdr->virts)) != NULL) {
				TAILQ_REMOVE(&rdr->virts, virt, entry);
				free(virt);
			}
			free(rdr);
		}
		env->sc_rdrcount = 0;
	}
	if (what & CONFIG_RELAYS && env->sc_pkeys != NULL) {
		while ((pkey = TAILQ_FIRST(env->sc_pkeys)) != NULL) {
			TAILQ_REMOVE(env->sc_pkeys, pkey, pkey_entry);
			free(pkey);
		}
	}
	if (what & CONFIG_RELAYS && env->sc_relays != NULL) {
		while ((rlay = TAILQ_FIRST(env->sc_relays)) != NULL)
			purge_relay(env, rlay);
		env->sc_relaycount = 0;
	}
	if (what & CONFIG_PROTOS && env->sc_protos != NULL) {
		while ((proto = TAILQ_FIRST(env->sc_protos)) != NULL) {
			TAILQ_REMOVE(env->sc_protos, proto, entry);
			while ((rule = TAILQ_FIRST(&proto->rules)) != NULL)
				rule_delete(&proto->rules, rule);
			proto->rulecount = 0;
		}
	}
	if (what & CONFIG_PROTOS && env->sc_protos != NULL) {
		while ((proto = TAILQ_FIRST(env->sc_protos)) != NULL) {
			TAILQ_REMOVE(env->sc_protos, proto, entry);
			free(proto->style);
			free(proto->tlscapass);
			while ((keyname =
			    TAILQ_FIRST(&proto->tlscerts)) != NULL) {
				TAILQ_REMOVE(&proto->tlscerts, keyname, entry);
				free(keyname->name);
				free(keyname);
			}
			free(proto);
		}
		env->sc_protocount = 0;
	}
	if (what & CONFIG_RTS && env->sc_rts != NULL) {
		while ((rt = TAILQ_FIRST(env->sc_rts)) != NULL) {
			TAILQ_REMOVE(env->sc_rts, rt, rt_entry);
			while ((nr = TAILQ_FIRST(&rt->rt_netroutes)) != NULL) {
				TAILQ_REMOVE(&rt->rt_netroutes, nr, nr_entry);
				TAILQ_REMOVE(env->sc_routes, nr, nr_route);
				free(nr);
				env->sc_routecount--;
			}
			free(rt);
		}
		env->sc_routercount = 0;
	}
	if (what & CONFIG_ROUTES && env->sc_routes != NULL) {
		while ((nr = TAILQ_FIRST(env->sc_routes)) != NULL) {
			if ((rt = nr->nr_router) != NULL)
				TAILQ_REMOVE(&rt->rt_netroutes, nr, nr_entry);
			TAILQ_REMOVE(env->sc_routes, nr, nr_route);
			free(nr);
		}
		env->sc_routecount = 0;
	}
}

int
config_setreset(struct relayd *env, u_int reset)
{
	struct privsep	*ps = env->sc_ps;
	int		 id;

	for (id = 0; id < PROC_MAX; id++) {
		if ((reset & ps->ps_what[id]) == 0 ||
		    id == privsep_process)
			continue;
		proc_compose(ps, id, IMSG_CTL_RESET, &reset, sizeof(reset));

		/*
		 * XXX Make sure that the reset message is sent
		 * immediately by flushing the imsg output buffer, before
		 * sending any other imsg that potentially include an fd.
		 * This should better be fixed in the imsg API itself.
		 */
		proc_flush_imsg(ps, id, -1);
	}

	return (0);
}

int
config_getreset(struct relayd *env, struct imsg *imsg)
{
	u_int		 mode;

	IMSG_SIZE_CHECK(imsg, &mode);
	memcpy(&mode, imsg->data, sizeof(mode));

	config_purge(env, mode);

	return (0);
}

int
config_getcfg(struct relayd *env, struct imsg *imsg)
{
	struct privsep		*ps = env->sc_ps;
	struct table		*tb;
	struct host		*h, *ph;
	u_int			 what;

	if (IMSG_DATA_SIZE(imsg) != sizeof(struct relayd_config))
		return (0); /* ignore */

	/* Update runtime flags */
	memcpy(&env->sc_conf, imsg->data, sizeof(env->sc_conf));

	what = ps->ps_what[privsep_process];

	if (what & CONFIG_TABLES) {
		/* Update the tables */
		TAILQ_FOREACH(tb, env->sc_tables, entry) {
			TAILQ_FOREACH(h, &tb->hosts, entry) {
				if (h->conf.parentid && (ph = host_find(env,
				    h->conf.parentid)) != NULL) {
					SLIST_INSERT_HEAD(&ph->children,
					    h, child);
				}
			}
		}
	}

	if (env->sc_conf.flags & (F_TLS|F_TLSCLIENT)) {
		if (what & CONFIG_CA_ENGINE)
			ca_engine_init(env);
	}

	if (privsep_process != PROC_PARENT)
		proc_compose(env->sc_ps, PROC_PARENT, IMSG_CFG_DONE, NULL, 0);

	return (0);
}

int
config_settable(struct relayd *env, struct table *tb)
{
	struct privsep	*ps = env->sc_ps;
	struct host	*host;
	int		 id, c;
	struct iovec	 iov[2];

	for (id = 0; id < PROC_MAX; id++) {
		if ((ps->ps_what[id] & CONFIG_TABLES) == 0 ||
		    id == privsep_process)
			continue;

		/* XXX need to send table to pfe for control socket */
		if (id == PROC_HCE && tb->conf.check == CHECK_NOCHECK)
			continue;

		DPRINTF("%s: sending table %s %d to %s", __func__,
		    tb->conf.name, tb->conf.id, env->sc_ps->ps_title[id]);

		c = 0;
		iov[c].iov_base = &tb->conf;
		iov[c++].iov_len = sizeof(tb->conf);
		if (tb->sendbuf != NULL) {
			iov[c].iov_base = tb->sendbuf;
			iov[c++].iov_len = strlen(tb->sendbuf);
		}

		proc_composev(ps, id, IMSG_CFG_TABLE, iov, c);

		TAILQ_FOREACH(host, &tb->hosts, entry) {
			proc_compose(ps, id, IMSG_CFG_HOST,
			    &host->conf, sizeof(host->conf));
		}
	}

	return (0);
}

int
config_gettable(struct relayd *env, struct imsg *imsg)
{
	struct table		*tb;
	size_t			 sb;
	u_int8_t		*p = imsg->data;
	size_t			 s;

	if ((tb = calloc(1, sizeof(*tb))) == NULL)
		return (-1);

	IMSG_SIZE_CHECK(imsg, &tb->conf);
	memcpy(&tb->conf, p, sizeof(tb->conf));
	s = sizeof(tb->conf);

	sb = IMSG_DATA_SIZE(imsg) - s;
	if (sb > 0) {
		if ((tb->sendbuf = get_string(p + s, sb)) == NULL) {
			free(tb);
			return (-1);
		}
	}
	if (tb->conf.check == CHECK_BINSEND_EXPECT) {
		tb->sendbinbuf = string2binary(tb->sendbuf);
		if (tb->sendbinbuf == NULL) {
			free(tb);
			return (-1);
		}
	}

	TAILQ_INIT(&tb->hosts);
	TAILQ_INSERT_TAIL(env->sc_tables, tb, entry);

	env->sc_tablecount++;

	DPRINTF("%s: %s %d received table %d (%s)", __func__,
	    env->sc_ps->ps_title[privsep_process], env->sc_ps->ps_instance,
	    tb->conf.id, tb->conf.name);

	return (0);
}

int
config_gethost(struct relayd *env, struct imsg *imsg)
{
	struct table		*tb;
	struct host		*host;

	if ((host = calloc(1, sizeof(*host))) == NULL)
		return (-1);

	IMSG_SIZE_CHECK(imsg, &host->conf);
	memcpy(&host->conf, imsg->data, sizeof(host->conf));

	if (host_find(env, host->conf.id) != NULL) {
		log_debug("%s: host %d already exists",
		    __func__, host->conf.id);
		free(host);
		return (-1);
	}

	if ((tb = table_find(env, host->conf.tableid)) == NULL) {
		log_debug("%s: "
		    "received host for unknown table %d", __func__,
		    host->conf.tableid);
		free(host);
		return (-1);
	}

	host->tablename = tb->conf.name;
	host->cte.s = -1;

	SLIST_INIT(&host->children);
	TAILQ_INSERT_TAIL(&tb->hosts, host, entry);
	TAILQ_INSERT_TAIL(&env->sc_hosts, host, globalentry);

	DPRINTF("%s: %s %d received host %s for table %s", __func__,
	    env->sc_ps->ps_title[privsep_process], env->sc_ps->ps_instance,
	    host->conf.name, tb->conf.name);

	return (0);
}

int
config_setrdr(struct relayd *env, struct rdr *rdr)
{
	struct privsep	*ps = env->sc_ps;
	struct address	*virt;
	int		 id;

	for (id = 0; id < PROC_MAX; id++) {
		if ((ps->ps_what[id] & CONFIG_RDRS) == 0 ||
		    id == privsep_process)
			continue;

		DPRINTF("%s: sending rdr %s to %s", __func__,
		    rdr->conf.name, ps->ps_title[id]);

		proc_compose(ps, id, IMSG_CFG_RDR,
		    &rdr->conf, sizeof(rdr->conf));

		TAILQ_FOREACH(virt, &rdr->virts, entry) {
			virt->rdrid = rdr->conf.id;
			proc_compose(ps, id, IMSG_CFG_VIRT,
			    virt, sizeof(*virt));
		}
	}

	return (0);
}

int
config_getrdr(struct relayd *env, struct imsg *imsg)
{
	struct rdr		*rdr;

	if ((rdr = calloc(1, sizeof(*rdr))) == NULL)
		return (-1);

	IMSG_SIZE_CHECK(imsg, &rdr->conf);
	memcpy(&rdr->conf, imsg->data, sizeof(rdr->conf));

	if ((rdr->table = table_find(env, rdr->conf.table_id)) == NULL) {
		log_debug("%s: table not found", __func__);
		free(rdr);
		return (-1);
	}
	if ((rdr->backup = table_find(env, rdr->conf.backup_id)) == NULL) {
		rdr->conf.backup_id = EMPTY_TABLE;
		rdr->backup = &env->sc_empty_table;
	}

	TAILQ_INIT(&rdr->virts);
	TAILQ_INSERT_TAIL(env->sc_rdrs, rdr, entry);

	env->sc_rdrcount++;

	DPRINTF("%s: %s %d received rdr %s", __func__,
	    env->sc_ps->ps_title[privsep_process], env->sc_ps->ps_instance,
	    rdr->conf.name);

	return (0);
}

int
config_getvirt(struct relayd *env, struct imsg *imsg)
{
	struct rdr	*rdr;
	struct address	*virt;

	IMSG_SIZE_CHECK(imsg, virt);

	if ((virt = calloc(1, sizeof(*virt))) == NULL)
		return (-1);
	memcpy(virt, imsg->data, sizeof(*virt));

	if ((rdr = rdr_find(env, virt->rdrid)) == NULL) {
		log_debug("%s: rdr not found", __func__);
		free(virt);
		return (-1);
	}

	TAILQ_INSERT_TAIL(&rdr->virts, virt, entry);

	DPRINTF("%s: %s %d received address for rdr %s", __func__,
	    env->sc_ps->ps_title[privsep_process], env->sc_ps->ps_instance,
	    rdr->conf.name);

	return (0);
}

int
config_setrt(struct relayd *env, struct router *rt)
{
	struct privsep	*ps = env->sc_ps;
	struct netroute	*nr;
	int		 id;

	for (id = 0; id < PROC_MAX; id++) {
		if ((ps->ps_what[id] & CONFIG_RTS) == 0 ||
		    id == privsep_process)
			continue;

		DPRINTF("%s: sending router %s to %s tbl %d", __func__,
		    rt->rt_conf.name, ps->ps_title[id], rt->rt_conf.gwtable);

		proc_compose(ps, id, IMSG_CFG_ROUTER,
		    &rt->rt_conf, sizeof(rt->rt_conf));

		TAILQ_FOREACH(nr, &rt->rt_netroutes, nr_entry) {
			proc_compose(ps, id, IMSG_CFG_ROUTE,
			    &nr->nr_conf, sizeof(nr->nr_conf));
		}
	}

	return (0);
}

int
config_getrt(struct relayd *env, struct imsg *imsg)
{
	struct router		*rt;

	if ((rt = calloc(1, sizeof(*rt))) == NULL)
		return (-1);

	IMSG_SIZE_CHECK(imsg, &rt->rt_conf);
	memcpy(&rt->rt_conf, imsg->data, sizeof(rt->rt_conf));

	if ((rt->rt_gwtable = table_find(env, rt->rt_conf.gwtable)) == NULL) {
		log_debug("%s: table not found", __func__);
		free(rt);
		return (-1);
	}

	TAILQ_INIT(&rt->rt_netroutes);
	TAILQ_INSERT_TAIL(env->sc_rts, rt, rt_entry);

	env->sc_routercount++;

	DPRINTF("%s: %s %d received router %s", __func__,
	    env->sc_ps->ps_title[privsep_process], env->sc_ps->ps_instance,
	    rt->rt_conf.name);

	return (0);
}

int
config_getroute(struct relayd *env, struct imsg *imsg)
{
	struct router		*rt;
	struct netroute		*nr;

	if ((nr = calloc(1, sizeof(*nr))) == NULL)
		return (-1);

	IMSG_SIZE_CHECK(imsg, &nr->nr_conf);
	memcpy(&nr->nr_conf, imsg->data, sizeof(nr->nr_conf));

	if (route_find(env, nr->nr_conf.id) != NULL) {
		log_debug("%s: route %d already exists",
		    __func__, nr->nr_conf.id);
		free(nr);
		return (-1);
	}

	if ((rt = router_find(env, nr->nr_conf.routerid)) == NULL) {
		log_debug("%s: received route for unknown router", __func__);
		free(nr);
		return (-1);
	}

	nr->nr_router = rt;

	TAILQ_INSERT_TAIL(env->sc_routes, nr, nr_route);
	TAILQ_INSERT_TAIL(&rt->rt_netroutes, nr, nr_entry);

	env->sc_routecount++;

	DPRINTF("%s: %s %d received route %d for router %s", __func__,
	    env->sc_ps->ps_title[privsep_process], env->sc_ps->ps_instance,
	    nr->nr_conf.id, rt->rt_conf.name);

	return (0);
}

int
config_setproto(struct relayd *env, struct protocol *proto)
{
	struct privsep		*ps = env->sc_ps;
	int			 id;
	struct iovec		 iov[IOV_MAX];
	size_t			 c;

	for (id = 0; id < PROC_MAX; id++) {
		if ((ps->ps_what[id] & CONFIG_PROTOS) == 0 ||
		    id == privsep_process)
			continue;

		DPRINTF("%s: sending protocol %s to %s", __func__,
		    proto->name, ps->ps_title[id]);

		c = 0;
		iov[c].iov_base = proto;
		iov[c++].iov_len = sizeof(*proto);

		if (proto->style != NULL) {
			iov[c].iov_base = proto->style;
			iov[c++].iov_len = strlen(proto->style) + 1;
		}

		proc_composev(ps, id, IMSG_CFG_PROTO, iov, c);
	}

	return (0);
}

int
config_setrule(struct relayd *env, struct protocol *proto)
{
	struct privsep		*ps = env->sc_ps;
	struct relay_rule	*rule;
	struct iovec		 iov[IOV_MAX];
	int			 id;
	size_t			 c, i;

	for (id = 0; id < PROC_MAX; id++) {
		if ((ps->ps_what[id] & CONFIG_PROTOS) == 0 ||
		    id == privsep_process)
			continue;

		DPRINTF("%s: sending rules %s to %s", __func__,
		    proto->name, ps->ps_title[id]);

		/* Now send all the rules */
		TAILQ_FOREACH(rule, &proto->rules, rule_entry) {
			rule->rule_protoid = proto->id;
			bzero(&rule->rule_ctl, sizeof(rule->rule_ctl));
			c = 0;
			iov[c].iov_base = rule;
			iov[c++].iov_len = sizeof(*rule);
			for (i = 1; i < KEY_TYPE_MAX; i++) {
				if (rule->rule_kv[i].kv_key != NULL) {
					rule->rule_ctl.kvlen[i].key =
					    strlen(rule->rule_kv[i].kv_key);
					iov[c].iov_base =
					    rule->rule_kv[i].kv_key;
					iov[c++].iov_len =
					    rule->rule_ctl.kvlen[i].key;
				} else
					rule->rule_ctl.kvlen[i].key = -1;
				if (rule->rule_kv[i].kv_value != NULL) {
					rule->rule_ctl.kvlen[i].value =
					    strlen(rule->rule_kv[i].kv_value);
					iov[c].iov_base =
					    rule->rule_kv[i].kv_value;
					iov[c++].iov_len =
					    rule->rule_ctl.kvlen[i].value;
				} else
					rule->rule_ctl.kvlen[i].value = -1;
			}

			proc_composev(ps, id, IMSG_CFG_RULE, iov, c);
		}
	}

	return (0);
}

int
config_getproto(struct relayd *env, struct imsg *imsg)
{
	struct protocol		*proto;
	size_t			 styl;
	size_t			 s;
	u_int8_t		*p = imsg->data;

	if ((proto = calloc(1, sizeof(*proto))) == NULL)
		return (-1);

	IMSG_SIZE_CHECK(imsg, proto);
	memcpy(proto, p, sizeof(*proto));
	s = sizeof(*proto);

	styl = IMSG_DATA_SIZE(imsg) - s;
	proto->style = NULL;
	if (styl > 0) {
		if ((proto->style = get_string(p + s, styl - 1)) == NULL) {
			free(proto);
			return (-1);
		}
	}

	TAILQ_INIT(&proto->rules);
	TAILQ_INIT(&proto->tlscerts);
	proto->tlscapass = NULL;

	TAILQ_INSERT_TAIL(env->sc_protos, proto, entry);

	env->sc_protocount++;

	DPRINTF("%s: %s %d received protocol %s", __func__,
	    env->sc_ps->ps_title[privsep_process], env->sc_ps->ps_instance,
	    proto->name);

	return (0);
}

int
config_getrule(struct relayd *env, struct imsg *imsg)
{
	struct protocol		*proto;
	struct relay_rule	*rule;
	size_t			 s, i;
	u_int8_t		*p = imsg->data;
	ssize_t			 len;

	if ((rule = calloc(1, sizeof(*rule))) == NULL)
		return (-1);

	IMSG_SIZE_CHECK(imsg, rule);
	memcpy(rule, p, sizeof(*rule));
	s = sizeof(*rule);
	len = IMSG_DATA_SIZE(imsg) - s;

	if ((proto = proto_find(env, rule->rule_protoid)) == NULL) {
		free(rule);
		return (-1);
	}

#define GETKV(_n, _f)	{						\
	if (rule->rule_ctl.kvlen[_n]._f >= 0) {				\
		/* Also accept "empty" 0-length strings */		\
		if ((len < rule->rule_ctl.kvlen[_n]._f) ||		\
		    (rule->rule_kv[_n].kv_##_f =			\
		    get_string(p + s,					\
		    rule->rule_ctl.kvlen[_n]._f)) == NULL) {		\
			free(rule);					\
			return (-1);					\
		}							\
		s += rule->rule_ctl.kvlen[_n]._f;			\
		len -= rule->rule_ctl.kvlen[_n]._f;			\
									\
		DPRINTF("%s: %s %s (len %ld, option %d): %s", __func__,	\
		    #_n, #_f, rule->rule_ctl.kvlen[_n]._f,		\
		    rule->rule_kv[_n].kv_option,			\
		    rule->rule_kv[_n].kv_##_f);				\
	}								\
}

	memset(&rule->rule_kv[0], 0, sizeof(struct kv));
	for (i = 1; i < KEY_TYPE_MAX; i++) {
		TAILQ_INIT(&rule->rule_kv[i].kv_children);
		GETKV(i, key);
		GETKV(i, value);
	}

	if (rule->rule_labelname[0])
		rule->rule_label = label_name2id(rule->rule_labelname);

	if (rule->rule_tagname[0])
		rule->rule_tag = tag_name2id(rule->rule_tagname);

	if (rule->rule_taggedname[0])
		rule->rule_tagged = tag_name2id(rule->rule_taggedname);

	rule->rule_id = proto->rulecount++;

	TAILQ_INSERT_TAIL(&proto->rules, rule, rule_entry);

	DPRINTF("%s: %s %d received rule %u for protocol %s", __func__,
	    env->sc_ps->ps_title[privsep_process], env->sc_ps->ps_instance,
	    rule->rule_id, proto->name);

	return (0);
}

static int
config_setrelayfd(struct privsep *ps, int id, int n,
    objid_t obj_id, objid_t rlay_id, enum fd_type type, int ofd)
{
	struct ctl_relayfd	rfd;
	int			fd;

	rfd.id = obj_id;
	rfd.relayid = rlay_id;
	rfd.type = type;

	if ((fd = dup(ofd)) == -1)
		return (-1);
	if (proc_compose_imsg(ps, id, n, IMSG_CFG_RELAY_FD, -1, fd,
	    &rfd, sizeof(rfd)) != 0)
		return (-1);

	return (0);
}

int
config_setrelay(struct relayd *env, struct relay *rlay)
{
	struct privsep		*ps = env->sc_ps;
	struct ctl_relaytable	 crt;
	struct relay_table	*rlt;
	struct relay_config	 rl;
	struct relay_cert	*cert;
	int			 id;
	int			 fd, n, m;
	struct iovec		 iov[6];
	size_t			 c;
	u_int			 what;

	/* opens listening sockets etc. */
	if (relay_privinit(rlay) == -1)
		return (-1);

	for (id = 0; id < PROC_MAX; id++) {
		what = ps->ps_what[id];

		if ((what & CONFIG_RELAYS) == 0 || id == privsep_process)
			continue;

		DPRINTF("%s: sending relay %s to %s fd %d", __func__,
		    rlay->rl_conf.name, ps->ps_title[id], rlay->rl_s);

		memcpy(&rl, &rlay->rl_conf, sizeof(rl));

		c = 0;
		iov[c].iov_base = &rl;
		iov[c++].iov_len = sizeof(rl);

		if ((what & CONFIG_CA_ENGINE) == 0 &&
		    rl.tls_cakey_len) {
			iov[c].iov_base = rlay->rl_tls_cakey;
			iov[c++].iov_len = rl.tls_cakey_len;
		} else
			rl.tls_cakey_len = 0;

		if (id == PROC_RELAY) {
			/* XXX imsg code will close the fd after 1st call */
			n = -1;
			proc_range(ps, id, &n, &m);
			for (n = 0; n < m; n++) {
				if ((fd = dup(rlay->rl_s)) == -1)
					return (-1);
				if (proc_composev_imsg(ps, id, n,
				    IMSG_CFG_RELAY, -1, fd, iov, c) != 0) {
					log_warn("%s: failed to compose "
					    "IMSG_CFG_RELAY imsg for `%s'",
					    __func__, rlay->rl_conf.name);
					return (-1);
				}
				/* Prevent fd exhaustion in the parent. */
				if (proc_flush_imsg(ps, id, n) == -1) {
					log_warn("%s: failed to flush "
					    "IMSG_CFG_RELAY imsg for `%s'",
					    __func__, rlay->rl_conf.name);
					return (-1);
				}
			}
		} else {
			if (proc_composev(ps, id,
			    IMSG_CFG_RELAY, iov, c) != 0) {
				log_warn("%s: failed to compose "
				    "IMSG_CFG_RELAY imsg for `%s'",
				    __func__, rlay->rl_conf.name);
				return (-1);
			}
		}

		/* cert keypairs */
		TAILQ_FOREACH(cert, env->sc_certs, cert_entry) {
			if (cert->cert_relayid != rlay->rl_conf.id)
				continue;
			n = -1;
			proc_range(ps, id, &n, &m);
			for (n = 0; (what & CONFIG_CERTS) && n < m; n++) {
				if (cert->cert_fd != -1 &&
				    config_setrelayfd(ps, id, n,
				    cert->cert_id, cert->cert_relayid,
				    RELAY_FD_CERT, cert->cert_fd) == -1) {
					log_warn("%s: fd passing failed for "
					    "`%s'", __func__,
					    rlay->rl_conf.name);
					return (-1);
				}
				if (id == PROC_RELAY &&
				    cert->cert_ocsp_fd != -1 &&
				    config_setrelayfd(ps, id, n,
				    cert->cert_id, cert->cert_relayid,
				    RELAY_FD_OCSP, cert->cert_ocsp_fd) == -1) {
					log_warn("%s: fd passing failed for "
					    "`%s'", __func__,
					    rlay->rl_conf.name);
					return (-1);
				}
				if (id == PROC_CA &&
				    cert->cert_key_fd != -1 &&
				    config_setrelayfd(ps, id, n,
				    cert->cert_id, cert->cert_relayid,
				    RELAY_FD_KEY, cert->cert_key_fd) == -1) {
					log_warn("%s: fd passing failed for "
					    "`%s'", __func__,
					    rlay->rl_conf.name);
					return (-1);
				}
			}
		}

		/* CA certs */
		if (what & CONFIG_CERTS) {
			n = -1;
			proc_range(ps, id, &n, &m);
			for (n = 0; n < m; n++) {
				if (rlay->rl_tls_ca_fd != -1 &&
				    config_setrelayfd(ps, id, n, 0,
				    rlay->rl_conf.id, RELAY_FD_CACERT,
				    rlay->rl_tls_ca_fd) == -1) {
					log_warn("%s: fd passing failed for "
					    "`%s'", __func__,
					    rlay->rl_conf.name);
					return (-1);
				}
				if (rlay->rl_tls_cacert_fd != -1 &&
				    config_setrelayfd(ps, id, n, 0,
				    rlay->rl_conf.id, RELAY_FD_CAFILE,
				    rlay->rl_tls_cacert_fd) == -1) {
					log_warn("%s: fd passing failed for "
					    "`%s'", __func__,
					    rlay->rl_conf.name);
					return (-1);
				}
				if (rlay->rl_tls_client_ca_fd != -1 &&
				    config_setrelayfd(ps, id, n, 0,
				    rlay->rl_conf.id, RELAY_FD_CLIENTCACERT,
				    rlay->rl_tls_client_ca_fd) == -1) {
					log_warn("%s: fd passing failed for "
					    "`%s'", __func__,
					    rlay->rl_conf.name);
					return (-1);
				}
				/* Prevent fd exhaustion in the parent. */
				if (proc_flush_imsg(ps, id, n) == -1) {
					log_warn("%s: failed to flush "
					    "IMSG_CFG_RELAY imsg for `%s'",
					    __func__, rlay->rl_conf.name);
					return (-1);
				}
			}
		}

		if ((what & CONFIG_TABLES) == 0)
			continue;

		/* Now send the tables associated to this relay */
		TAILQ_FOREACH(rlt, &rlay->rl_tables, rlt_entry) {
			crt.id = rlt->rlt_table->conf.id;
			crt.relayid = rlay->rl_conf.id;
			crt.mode = rlt->rlt_mode;
			crt.flags = rlt->rlt_flags;

			c = 0;
			iov[c].iov_base = &crt;
			iov[c++].iov_len = sizeof(crt);

			proc_composev(ps, id, IMSG_CFG_RELAY_TABLE, iov, c);
		}
	}

	/* Close server socket early to prevent fd exhaustion in the parent. */
	if (rlay->rl_s != -1) {
		close(rlay->rl_s);
		rlay->rl_s = -1;
	}
	if (rlay->rl_tls_client_ca_fd != -1) {
		close(rlay->rl_tls_client_ca_fd);
		rlay->rl_tls_client_ca_fd = -1;
	}
	if (rlay->rl_tls_cacert_fd != -1) {
		close(rlay->rl_tls_cacert_fd);
		rlay->rl_tls_cacert_fd = -1;
	}
	if (rlay->rl_tls_ca_fd != -1) {
		close(rlay->rl_tls_ca_fd);
		rlay->rl_tls_ca_fd = -1;
	}
	TAILQ_FOREACH(cert, env->sc_certs, cert_entry) {
		if (cert->cert_relayid != rlay->rl_conf.id)
			continue;

		if (cert->cert_fd != -1) {
			close(cert->cert_fd);
			cert->cert_fd = -1;
		}
		if (cert->cert_key_fd != -1) {
			close(cert->cert_key_fd);
			cert->cert_key_fd = -1;
		}
		if (cert->cert_ocsp_fd != -1) {
			close(cert->cert_ocsp_fd);
			cert->cert_ocsp_fd = -1;
		}
	}
	if (rlay->rl_tls_client_ca_fd != -1) {
		close(rlay->rl_tls_client_ca_fd);
		rlay->rl_tls_client_ca_fd = -1;
	}

	return (0);
}

int
config_getrelay(struct relayd *env, struct imsg *imsg)
{
	struct privsep		*ps = env->sc_ps;
	struct relay		*rlay;
	u_int8_t		*p = imsg->data;
	size_t			 s;

	if ((rlay = calloc(1, sizeof(*rlay))) == NULL)
		return (-1);

	IMSG_SIZE_CHECK(imsg, &rlay->rl_conf);
	memcpy(&rlay->rl_conf, p, sizeof(rlay->rl_conf));
	s = sizeof(rlay->rl_conf);

	rlay->rl_s = imsg_get_fd(imsg);
	rlay->rl_tls_ca_fd = -1;
	rlay->rl_tls_cacert_fd = -1;
	rlay->rl_tls_client_ca_fd = -1;

	if (ps->ps_what[privsep_process] & CONFIG_PROTOS) {
		if (rlay->rl_conf.proto == EMPTY_ID)
			rlay->rl_proto = &env->sc_proto_default;
		else if ((rlay->rl_proto =
		    proto_find(env, rlay->rl_conf.proto)) == NULL) {
			log_debug("%s: unknown protocol", __func__);
			goto fail;
		}
	}

	if ((off_t)(IMSG_DATA_SIZE(imsg) - s) <
	    (rlay->rl_conf.tls_cakey_len)) {
		log_debug("%s: invalid message length", __func__);
		goto fail;
	}

	if (rlay->rl_conf.tls_cakey_len) {
		if ((rlay->rl_tls_cakey = get_data(p + s,
		    rlay->rl_conf.tls_cakey_len)) == NULL)
			goto fail;
		s += rlay->rl_conf.tls_cakey_len;
	}

	TAILQ_INIT(&rlay->rl_tables);
	TAILQ_INSERT_TAIL(env->sc_relays, rlay, rl_entry);

	env->sc_relaycount++;

	DPRINTF("%s: %s %d received relay %s", __func__,
	    ps->ps_title[privsep_process], ps->ps_instance,
	    rlay->rl_conf.name);

	return (0);

 fail:
	free(rlay->rl_tls_cakey);
	close(rlay->rl_s);
	free(rlay);
	return (-1);
}

int
config_getrelaytable(struct relayd *env, struct imsg *imsg)
{
	struct relay_table	*rlt = NULL;
	struct ctl_relaytable	 crt;
	struct relay		*rlay;
	struct table		*table;
	u_int8_t		*p = imsg->data;

	IMSG_SIZE_CHECK(imsg, &crt);
	memcpy(&crt, p, sizeof(crt));

	if ((rlay = relay_find(env, crt.relayid)) == NULL) {
		log_debug("%s: unknown relay", __func__);
		goto fail;
	}

	if ((table = table_find(env, crt.id)) == NULL) {
		log_debug("%s: unknown table", __func__);
		goto fail;
	}

	if ((rlt = calloc(1, sizeof(*rlt))) == NULL)
		goto fail;

	rlt->rlt_table = table;
	rlt->rlt_mode = crt.mode;
	rlt->rlt_flags = crt.flags;

	TAILQ_INSERT_TAIL(&rlay->rl_tables, rlt, rlt_entry);

	DPRINTF("%s: %s %d received relay table %s for relay %s", __func__,
	    env->sc_ps->ps_title[privsep_process], env->sc_ps->ps_instance,
	    table->conf.name, rlay->rl_conf.name);

	return (0);

 fail:
	free(rlt);
	return (-1);
}

int
config_getrelayfd(struct relayd *env, struct imsg *imsg)
{
	struct ctl_relayfd	 crfd;
	struct relay		*rlay = NULL;
	struct relay_cert	*cert;
	u_int8_t		*p = imsg->data;

	IMSG_SIZE_CHECK(imsg, &crfd);
	memcpy(&crfd, p, sizeof(crfd));

	switch (crfd.type) {
	case RELAY_FD_CERT:
	case RELAY_FD_KEY:
	case RELAY_FD_OCSP:
		if ((cert = cert_find(env, crfd.id)) == NULL) {
			if ((cert = cert_add(env, crfd.id)) == NULL)
				return (-1);
			cert->cert_relayid = crfd.relayid;
		}
		/* FALLTHROUGH */
	default:
		if ((rlay = relay_find(env, crfd.relayid)) == NULL) {
			log_debug("%s: unknown relay", __func__);
			return (-1);
		}
		break;
	}

	switch (crfd.type) {
	case RELAY_FD_CERT:
		cert->cert_fd = imsg_get_fd(imsg);
		break;
	case RELAY_FD_KEY:
		cert->cert_key_fd = imsg_get_fd(imsg);
		break;
	case RELAY_FD_OCSP:
		cert->cert_ocsp_fd = imsg_get_fd(imsg);
		break;
	case RELAY_FD_CACERT:
		rlay->rl_tls_ca_fd = imsg_get_fd(imsg);
		break;
	case RELAY_FD_CAFILE:
		rlay->rl_tls_cacert_fd = imsg_get_fd(imsg);
		break;
	case RELAY_FD_CLIENTCACERT:
		rlay->rl_tls_client_ca_fd = imsg_get_fd(imsg);
		break;
	}

	DPRINTF("%s: %s %d received relay fd %d type %d for relay %s", __func__,
	    env->sc_ps->ps_title[privsep_process], env->sc_ps->ps_instance,
	    imsg->fd, crfd.type, rlay->rl_conf.name);

	return (0);
}
