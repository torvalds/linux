/*	$OpenBSD: pfe.c,v 1.91 2024/06/17 08:36:56 sashan Exp $	*/

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
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/pfvar.h>

#include <event.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <imsg.h>

#include "relayd.h"

void	 pfe_init(struct privsep *, struct privsep_proc *p, void *);
void	 pfe_shutdown(void);
void	 pfe_setup_events(void);
void	 pfe_disable_events(void);
void	 pfe_sync(void);
void	 pfe_statistics(int, short, void *);

int	 pfe_dispatch_parent(int, struct privsep_proc *, struct imsg *);
int	 pfe_dispatch_hce(int, struct privsep_proc *, struct imsg *);
int	 pfe_dispatch_relay(int, struct privsep_proc *, struct imsg *);

static struct relayd		*env = NULL;

static struct privsep_proc procs[] = {
	{ "parent",	PROC_PARENT,	pfe_dispatch_parent },
	{ "relay",	PROC_RELAY,	pfe_dispatch_relay },
	{ "hce",	PROC_HCE,	pfe_dispatch_hce }
};

void
pfe(struct privsep *ps, struct privsep_proc *p)
{
	int			s;
	struct pf_status	status;

	env = ps->ps_env;

	if ((s = open(PF_SOCKET, O_RDWR)) == -1) {
		fatal("%s: cannot open pf socket", __func__);
	}
	if (env->sc_pf == NULL) {
		if ((env->sc_pf = calloc(1, sizeof(*(env->sc_pf)))) == NULL)
			fatal("calloc");
		env->sc_pf->dev = s;
	}
	if (ioctl(env->sc_pf->dev, DIOCGETSTATUS, &status) == -1)
		fatal("%s: DIOCGETSTATUS", __func__);
	if (!status.running)
		fatalx("%s: pf is disabled", __func__);
	log_debug("%s: filter init done", __func__);

	proc_run(ps, p, procs, nitems(procs), pfe_init, NULL);
}

void
pfe_init(struct privsep *ps, struct privsep_proc *p, void *arg)
{
	if (config_init(ps->ps_env) == -1)
		fatal("failed to initialize configuration");

	if (pledge("stdio recvfd unix pf", NULL) == -1)
		fatal("pledge");

	p->p_shutdown = pfe_shutdown;
}

void
pfe_shutdown(void)
{
	flush_rulesets(env);
	config_purge(env, CONFIG_ALL);
}

void
pfe_setup_events(void)
{
	struct timeval	 tv;

	/* Schedule statistics timer */
	if (!event_initialized(&env->sc_statev)) {
		evtimer_set(&env->sc_statev, pfe_statistics, NULL);
		bcopy(&env->sc_conf.statinterval, &tv, sizeof(tv));
		evtimer_add(&env->sc_statev, &tv);
	}
}

void
pfe_disable_events(void)
{
	event_del(&env->sc_statev);
}

int
pfe_dispatch_hce(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	struct host		*host;
	struct table		*table;
	struct ctl_status	 st;

	control_imsg_forward(p->p_ps, imsg);

	switch (imsg->hdr.type) {
	case IMSG_HOST_STATUS:
		IMSG_SIZE_CHECK(imsg, &st);
		memcpy(&st, imsg->data, sizeof(st));
		if ((host = host_find(env, st.id)) == NULL)
			fatalx("%s: invalid host id", __func__);
		host->he = st.he;
		if (host->flags & F_DISABLE)
			break;
		host->retry_cnt = st.retry_cnt;
		if (st.up != HOST_UNKNOWN) {
			host->check_cnt++;
			if (st.up == HOST_UP)
				host->up_cnt++;
		}
		if (host->check_cnt != st.check_cnt) {
			log_debug("%s: host %d => %d", __func__,
			    host->conf.id, host->up);
			fatalx("%s: desynchronized", __func__);
		}

		if (host->up == st.up)
			break;

		/* Forward to relay engine(s) */
		proc_compose(env->sc_ps, PROC_RELAY,
		    IMSG_HOST_STATUS, &st, sizeof(st));

		if ((table = table_find(env, host->conf.tableid))
		    == NULL)
			fatalx("%s: invalid table id", __func__);

		log_debug("%s: state %d for host %u %s", __func__,
		    st.up, host->conf.id, host->conf.name);

/* XXX Readd hosttrap code later */
#if 0
		snmp_hosttrap(env, table, host);
#endif

		/*
		 * Do not change the table state when the host
		 * state switches between UNKNOWN and DOWN.
		 */
		if (HOST_ISUP(st.up)) {
			table->conf.flags |= F_CHANGED;
			table->up++;
			host->flags |= F_ADD;
			host->flags &= ~(F_DEL);
		} else if (HOST_ISUP(host->up)) {
			table->up--;
			table->conf.flags |= F_CHANGED;
			host->flags |= F_DEL;
			host->flags &= ~(F_ADD);
			host->up = st.up;
			pfe_sync();
		}

		host->up = st.up;
		break;
	case IMSG_SYNC:
		pfe_sync();
		break;
	default:
		return (-1);
	}

	return (0);
}

int
pfe_dispatch_parent(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	switch (imsg->hdr.type) {
	case IMSG_CFG_TABLE:
		config_gettable(env, imsg);
		break;
	case IMSG_CFG_HOST:
		config_gethost(env, imsg);
		break;
	case IMSG_CFG_RDR:
		config_getrdr(env, imsg);
		break;
	case IMSG_CFG_VIRT:
		config_getvirt(env, imsg);
		break;
	case IMSG_CFG_ROUTER:
		config_getrt(env, imsg);
		break;
	case IMSG_CFG_ROUTE:
		config_getroute(env, imsg);
		break;
	case IMSG_CFG_PROTO:
		config_getproto(env, imsg);
		break;
	case IMSG_CFG_RELAY:
		config_getrelay(env, imsg);
		break;
	case IMSG_CFG_RELAY_TABLE:
		config_getrelaytable(env, imsg);
		break;
	case IMSG_CFG_DONE:
		config_getcfg(env, imsg);
		init_tables(env);
		agentx_init(env);
		break;
	case IMSG_CTL_START:
		pfe_setup_events();
		pfe_sync();
		break;
	case IMSG_CTL_RESET:
		config_getreset(env, imsg);
		break;
	case IMSG_AGENTXSOCK:
		agentx_getsock(imsg);
		break;
	default:
		return (-1);
	}

	return (0);
}

int
pfe_dispatch_relay(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	struct ctl_natlook	 cnl;
	struct ctl_stats	 crs;
	struct relay		*rlay;
	struct ctl_conn		*c;
	struct rsession		 con, *s, *t;
	int			 cid;
	objid_t			 sid;

	switch (imsg->hdr.type) {
	case IMSG_NATLOOK:
		IMSG_SIZE_CHECK(imsg, &cnl);
		bcopy(imsg->data, &cnl, sizeof(cnl));
		if (cnl.proc > env->sc_conf.prefork_relay)
			fatalx("%s: invalid relay proc", __func__);
		if (natlook(env, &cnl) != 0)
			cnl.in = -1;
		proc_compose_imsg(env->sc_ps, PROC_RELAY, cnl.proc,
		    IMSG_NATLOOK, -1, -1, &cnl, sizeof(cnl));
		break;
	case IMSG_STATISTICS:
		IMSG_SIZE_CHECK(imsg, &crs);
		bcopy(imsg->data, &crs, sizeof(crs));
		if (crs.proc > env->sc_conf.prefork_relay)
			fatalx("%s: invalid relay proc", __func__);
		if ((rlay = relay_find(env, crs.id)) == NULL)
			fatalx("%s: invalid relay id", __func__);
		bcopy(&crs, &rlay->rl_stats[crs.proc], sizeof(crs));
		rlay->rl_stats[crs.proc].interval =
		    env->sc_conf.statinterval.tv_sec;
		break;
	case IMSG_CTL_SESSION:
		IMSG_SIZE_CHECK(imsg, &con);
		memcpy(&con, imsg->data, sizeof(con));
		if ((c = control_connbyfd(con.se_cid)) == NULL) {
			log_debug("%s: control connection %d not found",
			    __func__, con.se_cid);
			return (0);
		}
		imsg_compose_event(&c->iev,
		    IMSG_CTL_SESSION, 0, 0, -1,
		    &con, sizeof(con));
		break;
	case IMSG_CTL_END:
		IMSG_SIZE_CHECK(imsg, &cid);
		memcpy(&cid, imsg->data, sizeof(cid));
		if ((c = control_connbyfd(cid)) == NULL) {
			log_debug("%s: control connection %d not found",
			    __func__, cid);
			return (0);
		}
		if (c->waiting == 0) {
			log_debug("%s: no pending control requests", __func__);
			return (0);
		} else if (--c->waiting == 0) {
			/* Last ack for a previous request */
			imsg_compose_event(&c->iev, IMSG_CTL_END,
			    0, 0, -1, NULL, 0);
		}
		break;
	case IMSG_SESS_PUBLISH:
		IMSG_SIZE_CHECK(imsg, s);
		if ((s = calloc(1, sizeof(*s))) == NULL)
			return (0);		/* XXX */
		memcpy(s, imsg->data, sizeof(*s));
		TAILQ_FOREACH(t, &env->sc_sessions, se_entry) {
			/* duplicate registration */
			if (t->se_id == s->se_id) {
				free(s);
				return (0);
			}
			if (t->se_id > s->se_id)
				break;
		}
		if (t)
			TAILQ_INSERT_BEFORE(t, s, se_entry);
		else
			TAILQ_INSERT_TAIL(&env->sc_sessions, s, se_entry);
		break;
	case IMSG_SESS_UNPUBLISH:
		IMSG_SIZE_CHECK(imsg, &sid);
		memcpy(&sid, imsg->data, sizeof(sid));
		TAILQ_FOREACH(s, &env->sc_sessions, se_entry)
			if (s->se_id == sid)
				break;
		if (s) {
			TAILQ_REMOVE(&env->sc_sessions, s, se_entry);
			free(s);
		} else {
			DPRINTF("removal of unpublished session %i", sid);
		}
		break;
	default:
		return (-1);
	}

	return (0);
}

void
show(struct ctl_conn *c)
{
	struct rdr		*rdr;
	struct host		*host;
	struct relay		*rlay;
	struct router		*rt;
	struct netroute		*nr;
	struct relay_table	*rlt;

	if (env->sc_rdrs == NULL)
		goto relays;
	TAILQ_FOREACH(rdr, env->sc_rdrs, entry) {
		imsg_compose_event(&c->iev, IMSG_CTL_RDR, 0, 0, -1,
		    rdr, sizeof(*rdr));
		if (rdr->conf.flags & F_DISABLE)
			continue;

		imsg_compose_event(&c->iev, IMSG_CTL_RDR_STATS, 0, 0, -1,
		    &rdr->stats, sizeof(rdr->stats));

		imsg_compose_event(&c->iev, IMSG_CTL_TABLE, 0, 0, -1,
		    rdr->table, sizeof(*rdr->table));
		if (!(rdr->table->conf.flags & F_DISABLE))
			TAILQ_FOREACH(host, &rdr->table->hosts, entry)
				imsg_compose_event(&c->iev, IMSG_CTL_HOST,
				    0, 0, -1, host, sizeof(*host));

		if (rdr->backup->conf.id == EMPTY_TABLE)
			continue;
		imsg_compose_event(&c->iev, IMSG_CTL_TABLE, 0, 0, -1,
		    rdr->backup, sizeof(*rdr->backup));
		if (!(rdr->backup->conf.flags & F_DISABLE))
			TAILQ_FOREACH(host, &rdr->backup->hosts, entry)
				imsg_compose_event(&c->iev, IMSG_CTL_HOST,
				    0, 0, -1, host, sizeof(*host));
	}
relays:
	if (env->sc_relays == NULL)
		goto routers;
	TAILQ_FOREACH(rlay, env->sc_relays, rl_entry) {
		rlay->rl_stats[env->sc_conf.prefork_relay].id = EMPTY_ID;
		imsg_compose_event(&c->iev, IMSG_CTL_RELAY, 0, 0, -1,
		    rlay, sizeof(*rlay));
		imsg_compose_event(&c->iev, IMSG_CTL_RELAY_STATS, 0, 0, -1,
		    &rlay->rl_stats, sizeof(rlay->rl_stats));

		TAILQ_FOREACH(rlt, &rlay->rl_tables, rlt_entry) {
			imsg_compose_event(&c->iev, IMSG_CTL_TABLE, 0, 0, -1,
			    rlt->rlt_table, sizeof(*rlt->rlt_table));
			if (!(rlt->rlt_table->conf.flags & F_DISABLE))
				TAILQ_FOREACH(host,
				    &rlt->rlt_table->hosts, entry)
					imsg_compose_event(&c->iev,
					    IMSG_CTL_HOST, 0, 0, -1,
					    host, sizeof(*host));
		}
	}

routers:
	if (env->sc_rts == NULL)
		goto end;
	TAILQ_FOREACH(rt, env->sc_rts, rt_entry) {
		imsg_compose_event(&c->iev, IMSG_CTL_ROUTER, 0, 0, -1,
		    rt, sizeof(*rt));
		if (rt->rt_conf.flags & F_DISABLE)
			continue;

		TAILQ_FOREACH(nr, &rt->rt_netroutes, nr_entry)
			imsg_compose_event(&c->iev, IMSG_CTL_NETROUTE,
			    0, 0, -1, nr, sizeof(*nr));
		imsg_compose_event(&c->iev, IMSG_CTL_TABLE, 0, 0, -1,
		    rt->rt_gwtable, sizeof(*rt->rt_gwtable));
		if (!(rt->rt_gwtable->conf.flags & F_DISABLE))
			TAILQ_FOREACH(host, &rt->rt_gwtable->hosts, entry)
				imsg_compose_event(&c->iev, IMSG_CTL_HOST,
				    0, 0, -1, host, sizeof(*host));
	}

end:
	imsg_compose_event(&c->iev, IMSG_CTL_END, 0, 0, -1, NULL, 0);
}

void
show_sessions(struct ctl_conn *c)
{
	int			 proc, cid;

	for (proc = 0; proc < env->sc_conf.prefork_relay; proc++) {
		cid = c->iev.ibuf.fd;

		/*
		 * Request all the running sessions from the process
		 */
		proc_compose_imsg(env->sc_ps, PROC_RELAY, proc,
		    IMSG_CTL_SESSION, -1, -1, &cid, sizeof(cid));
		c->waiting++;
	}
}

int
disable_rdr(struct ctl_conn *c, struct ctl_id *id)
{
	struct rdr	*rdr;

	if (id->id == EMPTY_ID)
		rdr = rdr_findbyname(env, id->name);
	else
		rdr = rdr_find(env, id->id);
	if (rdr == NULL)
		return (-1);
	id->id = rdr->conf.id;

	if (rdr->conf.flags & F_DISABLE)
		return (0);

	rdr->conf.flags |= F_DISABLE;
	rdr->conf.flags &= ~(F_ADD);
	rdr->conf.flags |= F_DEL;
	rdr->table->conf.flags |= F_DISABLE;
	log_debug("%s: redirect %d", __func__, rdr->conf.id);
	pfe_sync();
	return (0);
}

int
enable_rdr(struct ctl_conn *c, struct ctl_id *id)
{
	struct rdr	*rdr;
	struct ctl_id	 eid;

	if (id->id == EMPTY_ID)
		rdr = rdr_findbyname(env, id->name);
	else
		rdr = rdr_find(env, id->id);
	if (rdr == NULL)
		return (-1);
	id->id = rdr->conf.id;

	if (!(rdr->conf.flags & F_DISABLE))
		return (0);

	rdr->conf.flags &= ~(F_DISABLE);
	rdr->conf.flags &= ~(F_DEL);
	rdr->conf.flags |= F_ADD;
	log_debug("%s: redirect %d", __func__, rdr->conf.id);

	bzero(&eid, sizeof(eid));

	/* XXX: we're syncing twice */
	eid.id = rdr->table->conf.id;
	if (enable_table(c, &eid) == -1)
		return (-1);
	if (rdr->backup->conf.id == EMPTY_ID)
		return (0);
	eid.id = rdr->backup->conf.id;
	if (enable_table(c, &eid) == -1)
		return (-1);
	return (0);
}

int
disable_table(struct ctl_conn *c, struct ctl_id *id)
{
	struct table	*table;
	struct host	*host;

	if (id->id == EMPTY_ID)
		table = table_findbyname(env, id->name);
	else
		table = table_find(env, id->id);
	if (table == NULL)
		return (-1);
	id->id = table->conf.id;
	if (table->conf.rdrid > 0 && rdr_find(env, table->conf.rdrid) == NULL)
		fatalx("%s: desynchronised", __func__);

	if (table->conf.flags & F_DISABLE)
		return (0);
	table->conf.flags |= (F_DISABLE|F_CHANGED);
	table->up = 0;
	TAILQ_FOREACH(host, &table->hosts, entry)
		host->up = HOST_UNKNOWN;
	proc_compose(env->sc_ps, PROC_HCE, IMSG_TABLE_DISABLE,
	    &table->conf.id, sizeof(table->conf.id));

	/* Forward to relay engine(s) */
	proc_compose(env->sc_ps, PROC_RELAY, IMSG_TABLE_DISABLE,
	    &table->conf.id, sizeof(table->conf.id));

	log_debug("%s: table %d", __func__, table->conf.id);
	pfe_sync();
	return (0);
}

int
enable_table(struct ctl_conn *c, struct ctl_id *id)
{
	struct table	*table;
	struct host	*host;

	if (id->id == EMPTY_ID)
		table = table_findbyname(env, id->name);
	else
		table = table_find(env, id->id);
	if (table == NULL)
		return (-1);
	id->id = table->conf.id;

	if (table->conf.rdrid > 0 && rdr_find(env, table->conf.rdrid) == NULL)
		fatalx("%s: desynchronised", __func__);

	if (!(table->conf.flags & F_DISABLE))
		return (0);
	table->conf.flags &= ~(F_DISABLE);
	table->conf.flags |= F_CHANGED;
	table->up = 0;
	TAILQ_FOREACH(host, &table->hosts, entry)
		host->up = HOST_UNKNOWN;
	proc_compose(env->sc_ps, PROC_HCE, IMSG_TABLE_ENABLE,
	    &table->conf.id, sizeof(table->conf.id));

	/* Forward to relay engine(s) */
	proc_compose(env->sc_ps, PROC_RELAY, IMSG_TABLE_ENABLE,
	    &table->conf.id, sizeof(table->conf.id));

	log_debug("%s: table %d", __func__, table->conf.id);
	pfe_sync();
	return (0);
}

int
disable_host(struct ctl_conn *c, struct ctl_id *id, struct host *host)
{
	struct host	*h;
	struct table	*table, *t;
	int	 host_byname = 0;

	if (host == NULL) {
		if (id->id == EMPTY_ID) {
			host = host_findbyname(env, id->name);
			host_byname = 1;
		}
		else
			host = host_find(env, id->id);
		if (host == NULL || host->conf.parentid)
			return (-1);
	}
	id->id = host->conf.id;

	if (host->flags & F_DISABLE)
		return (0);

	if (host->up == HOST_UP) {
		if ((table = table_find(env, host->conf.tableid)) == NULL)
			fatalx("%s: invalid table id", __func__);
		table->up--;
		table->conf.flags |= F_CHANGED;
	}

	host->up = HOST_UNKNOWN;
	host->flags |= F_DISABLE;
	host->flags |= F_DEL;
	host->flags &= ~(F_ADD);
	host->check_cnt = 0;
	host->up_cnt = 0;

	proc_compose(env->sc_ps, PROC_HCE, IMSG_HOST_DISABLE,
	    &host->conf.id, sizeof(host->conf.id));

	/* Forward to relay engine(s) */
	proc_compose(env->sc_ps, PROC_RELAY, IMSG_HOST_DISABLE,
	    &host->conf.id, sizeof(host->conf.id));
	log_debug("%s: host %d", __func__, host->conf.id);

	if (!host->conf.parentid) {
		/* Disable all children */
		SLIST_FOREACH(h, &host->children, child)
			disable_host(c, id, h);

		/* Disable hosts with same name on all tables */
		if (host_byname)
			TAILQ_FOREACH(t, env->sc_tables, entry)
				TAILQ_FOREACH(h, &t->hosts, entry)
					if (strcmp(h->conf.name,
					    host->conf.name) == 0 &&
					    h->conf.id != host->conf.id &&
					    !h->conf.parentid)
						disable_host(c, id, h);
		pfe_sync();
	}
	return (0);
}

int
enable_host(struct ctl_conn *c, struct ctl_id *id, struct host *host)
{
	struct host	*h;
	struct table	*t;
	int	 host_byname = 0;


	if (host == NULL) {
		if (id->id == EMPTY_ID) {
			host = host_findbyname(env, id->name);
			host_byname = 1;
		}
		else
			host = host_find(env, id->id);
		if (host == NULL || host->conf.parentid)
			return (-1);
	}
	id->id = host->conf.id;

	if (!(host->flags & F_DISABLE))
		return (0);

	host->up = HOST_UNKNOWN;
	host->flags &= ~(F_DISABLE);
	host->flags &= ~(F_DEL);
	host->flags &= ~(F_ADD);

	proc_compose(env->sc_ps, PROC_HCE, IMSG_HOST_ENABLE,
	    &host->conf.id, sizeof (host->conf.id));

	/* Forward to relay engine(s) */
	proc_compose(env->sc_ps, PROC_RELAY, IMSG_HOST_ENABLE,
	    &host->conf.id, sizeof(host->conf.id));

	log_debug("%s: host %d", __func__, host->conf.id);

	if (!host->conf.parentid) {
		/* Enable all children */
		SLIST_FOREACH(h, &host->children, child)
			enable_host(c, id, h);

		/* Enable hosts with same name on all tables */
		if (host_byname)
			TAILQ_FOREACH(t, env->sc_tables, entry)
				TAILQ_FOREACH(h, &t->hosts, entry)
					if (strcmp(h->conf.name,
					    host->conf.name) == 0 &&
					    h->conf.id != host->conf.id &&
					    !h->conf.parentid)
						enable_host(c, id, h);
		pfe_sync();
	}
	return (0);
}

void
pfe_sync(void)
{
	struct rdr		*rdr;
	struct table		*active;
	struct table		*table;
	struct ctl_id		 id;
	struct imsg		 imsg;
	struct ctl_demote	 demote;
	struct router		*rt;

	bzero(&id, sizeof(id));
	bzero(&imsg, sizeof(imsg));
	TAILQ_FOREACH(rdr, env->sc_rdrs, entry) {
		rdr->conf.flags &= ~(F_BACKUP);
		rdr->conf.flags &= ~(F_DOWN);

		if (rdr->conf.flags & F_DISABLE ||
		    (rdr->table->up == 0 && rdr->backup->up == 0)) {
			rdr->conf.flags |= F_DOWN;
			active = NULL;
		} else if (rdr->table->up == 0 && rdr->backup->up > 0) {
			rdr->conf.flags |= F_BACKUP;
			active = rdr->backup;
			active->conf.flags |=
			    rdr->table->conf.flags & F_CHANGED;
			active->conf.flags |=
			    rdr->backup->conf.flags & F_CHANGED;
		} else
			active = rdr->table;

		if (active != NULL && active->conf.flags & F_CHANGED) {
			id.id = active->conf.id;
			imsg.hdr.type = IMSG_CTL_TABLE_CHANGED;
			imsg.hdr.len = sizeof(id) + IMSG_HEADER_SIZE;
			imsg.data = &id;
			sync_table(env, rdr, active);
			control_imsg_forward(env->sc_ps, &imsg);
		}

		if (rdr->conf.flags & F_DOWN) {
			if (rdr->conf.flags & F_ACTIVE_RULESET) {
				flush_table(env, rdr);
				log_debug("%s: disabling ruleset", __func__);
				rdr->conf.flags &= ~(F_ACTIVE_RULESET);
				id.id = rdr->conf.id;
				imsg.hdr.type = IMSG_CTL_PULL_RULESET;
				imsg.hdr.len = sizeof(id) + IMSG_HEADER_SIZE;
				imsg.data = &id;
				sync_ruleset(env, rdr, 0);
				control_imsg_forward(env->sc_ps, &imsg);
			}
		} else if (!(rdr->conf.flags & F_ACTIVE_RULESET)) {
			log_debug("%s: enabling ruleset", __func__);
			rdr->conf.flags |= F_ACTIVE_RULESET;
			id.id = rdr->conf.id;
			imsg.hdr.type = IMSG_CTL_PUSH_RULESET;
			imsg.hdr.len = sizeof(id) + IMSG_HEADER_SIZE;
			imsg.data = &id;
			sync_ruleset(env, rdr, 1);
			control_imsg_forward(env->sc_ps, &imsg);
		}
	}

	TAILQ_FOREACH(rt, env->sc_rts, rt_entry) {
		rt->rt_conf.flags &= ~(F_BACKUP);
		rt->rt_conf.flags &= ~(F_DOWN);

		if ((rt->rt_gwtable->conf.flags & F_CHANGED))
			sync_routes(env, rt);
	}

	TAILQ_FOREACH(table, env->sc_tables, entry) {
		if (table->conf.check == CHECK_NOCHECK)
			continue;

		/*
		 * clean up change flag.
		 */
		table->conf.flags &= ~(F_CHANGED);

		/*
		 * handle demotion.
		 */
		if ((table->conf.flags & F_DEMOTE) == 0)
			continue;
		demote.level = 0;
		if (table->up && table->conf.flags & F_DEMOTED) {
			demote.level = -1;
			table->conf.flags &= ~F_DEMOTED;
		}
		else if (!table->up && !(table->conf.flags & F_DEMOTED)) {
			demote.level = 1;
			table->conf.flags |= F_DEMOTED;
		}
		if (demote.level == 0)
			continue;
		log_debug("%s: demote %d table '%s' group '%s'", __func__,
		    demote.level, table->conf.name, table->conf.demote_group);
		(void)strlcpy(demote.group, table->conf.demote_group,
		    sizeof(demote.group));
		proc_compose(env->sc_ps, PROC_PARENT, IMSG_DEMOTE,
		    &demote, sizeof(demote));
	}
}

void
pfe_statistics(int fd, short events, void *arg)
{
	struct rdr		*rdr;
	struct ctl_stats	*cur;
	struct timeval		 tv, tv_now;
	int			 resethour, resetday;
	u_long			 cnt;

	timerclear(&tv);
	getmonotime(&tv_now);

	TAILQ_FOREACH(rdr, env->sc_rdrs, entry) {
		cnt = check_table(env, rdr, rdr->table);
		if (rdr->conf.backup_id != EMPTY_TABLE)
			cnt += check_table(env, rdr, rdr->backup);

		resethour = resetday = 0;

		cur = &rdr->stats;
		cur->last = cnt > cur->cnt ? cnt - cur->cnt : 0;

		cur->cnt = cnt;
		cur->tick++;
		cur->avg = (cur->last + cur->avg) / 2;
		cur->last_hour += cur->last;
		if ((cur->tick %
		    (3600 / env->sc_conf.statinterval.tv_sec)) == 0) {
			cur->avg_hour = (cur->last_hour + cur->avg_hour) / 2;
			resethour++;
		}
		cur->last_day += cur->last;
		if ((cur->tick %
		    (86400 / env->sc_conf.statinterval.tv_sec)) == 0) {
			cur->avg_day = (cur->last_day + cur->avg_day) / 2;
			resethour++;
		}
		if (resethour)
			cur->last_hour = 0;
		if (resetday)
			cur->last_day = 0;

		rdr->stats.interval = env->sc_conf.statinterval.tv_sec;
	}

	/* Schedule statistics timer */
	evtimer_set(&env->sc_statev, pfe_statistics, NULL);
	bcopy(&env->sc_conf.statinterval, &tv, sizeof(tv));
	evtimer_add(&env->sc_statev, &tv);
}
