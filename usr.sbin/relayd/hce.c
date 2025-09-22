/*	$OpenBSD: hce.c,v 1.82 2024/05/18 06:34:46 jsg Exp $	*/

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
#include <sys/time.h>
#include <sys/uio.h>

#include <event.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <imsg.h>

#include "relayd.h"

void	 hce_init(struct privsep *, struct privsep_proc *p, void *);
void	 hce_launch_checks(int, short, void *);
void	 hce_setup_events(void);
void	 hce_disable_events(void);

int	 hce_dispatch_parent(int, struct privsep_proc *, struct imsg *);
int	 hce_dispatch_pfe(int, struct privsep_proc *, struct imsg *);
int	 hce_dispatch_relay(int, struct privsep_proc *, struct imsg *);

static struct relayd *env = NULL;
int			 running = 0;

static struct privsep_proc procs[] = {
	{ "parent",	PROC_PARENT,	hce_dispatch_parent },
	{ "pfe",	PROC_PFE,	hce_dispatch_pfe },
	{ "relay",	PROC_RELAY,	hce_dispatch_relay },
};

void
hce(struct privsep *ps, struct privsep_proc *p)
{
	env = ps->ps_env;

	/* this is needed for icmp tests */
	icmp_init(env);

	proc_run(ps, p, procs, nitems(procs), hce_init, NULL);
}

void
hce_init(struct privsep *ps, struct privsep_proc *p, void *arg)
{
	if (config_init(ps->ps_env) == -1)
		fatal("failed to initialize configuration");

	env->sc_id = getpid() & 0xffff;

	/* Allow maximum available sockets for TCP checks */
	socket_rlimit(-1);

	if (pledge("stdio recvfd inet", NULL) == -1)
		fatal("%s: pledge", __func__);
}

void
hce_setup_events(void)
{
	struct timeval	 tv;
	struct table	*table;

	if (!event_initialized(&env->sc_ev)) {
		evtimer_set(&env->sc_ev, hce_launch_checks, env);
		bzero(&tv, sizeof(tv));
		evtimer_add(&env->sc_ev, &tv);
	}

	if (env->sc_conf.flags & F_TLS) {
		TAILQ_FOREACH(table, env->sc_tables, entry) {
			if (!(table->conf.flags & F_TLS) ||
			    table->tls_cfg != NULL)
				continue;
			table->tls_cfg = tls_config_new();
			if (table->tls_cfg == NULL)
				fatalx("%s: tls_config_new", __func__);
			tls_config_insecure_noverifycert(table->tls_cfg);
			tls_config_insecure_noverifyname(table->tls_cfg);
		}
	}
}

void
hce_disable_events(void)
{
	struct table	*table;
	struct host	*host;

	evtimer_del(&env->sc_ev);
	TAILQ_FOREACH(table, env->sc_tables, entry) {
		TAILQ_FOREACH(host, &table->hosts, entry) {
			host->he = HCE_ABORT;
			if (event_initialized(&host->cte.ev)) {
				event_del(&host->cte.ev);
				close(host->cte.s);
			}
		}
	}
	if (env->sc_has_icmp) {
		event_del(&env->sc_icmp_send.ev);
		event_del(&env->sc_icmp_recv.ev);
	}
	if (env->sc_has_icmp6) {
		event_del(&env->sc_icmp6_send.ev);
		event_del(&env->sc_icmp6_recv.ev);
	}
}

void
hce_launch_checks(int fd, short event, void *arg)
{
	struct host		*host;
	struct table		*table;
	struct timeval		 tv;

	/*
	 * notify pfe checks are done and schedule next check
	 */
	proc_compose(env->sc_ps, PROC_PFE, IMSG_SYNC, NULL, 0);
	TAILQ_FOREACH(table, env->sc_tables, entry) {
		TAILQ_FOREACH(host, &table->hosts, entry) {
			if ((host->flags & F_CHECK_DONE) == 0)
				host->he = HCE_INTERVAL_TIMEOUT;
			if (event_initialized(&host->cte.ev)) {
				event_del(&host->cte.ev);
				close(host->cte.s);
			}
			host->cte.s = -1;
		}
	}

	getmonotime(&tv);

	TAILQ_FOREACH(table, env->sc_tables, entry) {
		if (table->conf.flags & F_DISABLE)
			continue;
		if (table->conf.skip_cnt) {
			if (table->skipped++ > table->conf.skip_cnt)
				table->skipped = 0;
			if (table->skipped != 1)
				continue;
		}
		if (table->conf.check == CHECK_NOCHECK)
			fatalx("%s: unknown check type", __func__);

		TAILQ_FOREACH(host, &table->hosts, entry) {
			if (host->flags & F_DISABLE || host->conf.parentid)
				continue;
			bcopy(&tv, &host->cte.tv_start,
			    sizeof(host->cte.tv_start));
			switch (table->conf.check) {
			case CHECK_ICMP:
				schedule_icmp(env, host);
				break;
			case CHECK_SCRIPT:
				check_script(env, host);
				break;
			default:
				/* Any other TCP-style checks */
				host->last_up = host->up;
				host->cte.host = host;
				host->cte.table = table;
				check_tcp(&host->cte);
				break;
			}
		}
	}
	check_icmp(env, &tv);

	bcopy(&env->sc_conf.interval, &tv, sizeof(tv));
	evtimer_add(&env->sc_ev, &tv);
}

void
hce_notify_done(struct host *host, enum host_error he)
{
	struct table		*table;
	struct ctl_status	 st;
	struct timeval		 tv_now, tv_dur;
	u_long			 duration;
	u_int			 logopt = RELAYD_OPT_LOGHOSTCHECK;
	struct host		*h, *hostnst;
	int			 hostup;
	const char		*msg;
	char			*codemsg = NULL;

	if ((hostnst = host_find(env, host->conf.id)) == NULL)
		fatalx("%s: desynchronized", __func__);

	if ((table = table_find(env, host->conf.tableid)) == NULL)
		fatalx("%s: invalid table id", __func__);

	if (hostnst->flags & F_DISABLE) {
		if (env->sc_conf.opts & RELAYD_OPT_LOGUPDATE) {
			log_info("host %s, check %s%s (ignoring result, "
			    "host disabled)",
			    host->conf.name, table_check(table->conf.check),
			    (table->conf.flags & F_TLS) ? " use tls" : "");
		}
		host->flags |= (F_CHECK_SENT|F_CHECK_DONE);
		return;
	}

	hostup = host->up;
	host->he = he;

	if (host->up == HOST_DOWN && host->retry_cnt) {
		log_debug("%s: host %s retry %d", __func__,
		    host->conf.name, host->retry_cnt);
		host->up = host->last_up;
		host->retry_cnt--;
	} else
		host->retry_cnt = host->conf.retry;
	if (host->up != HOST_UNKNOWN) {
		host->check_cnt++;
		if (host->up == HOST_UP)
			host->up_cnt++;
	}
	st.id = host->conf.id;
	st.up = host->up;
	st.check_cnt = host->check_cnt;
	st.retry_cnt = host->retry_cnt;
	st.he = he;
	host->flags |= (F_CHECK_SENT|F_CHECK_DONE);
	msg = host_error(he);
	if (msg)
		log_debug("%s: %s (%s)", __func__, host->conf.name, msg);

	proc_compose(env->sc_ps, PROC_PFE, IMSG_HOST_STATUS, &st, sizeof(st));
	if (host->up != host->last_up)
		logopt = RELAYD_OPT_LOGUPDATE;

	getmonotime(&tv_now);
	timersub(&tv_now, &host->cte.tv_start, &tv_dur);
	if (timercmp(&host->cte.tv_start, &tv_dur, >))
		duration = (tv_dur.tv_sec * 1000) + (tv_dur.tv_usec / 1000.0);
	else
		duration = 0;

	if (env->sc_conf.opts & logopt) {
		if (host->code > 0)
		    asprintf(&codemsg, ",%d", host->code);
		log_info("host %s, check %s%s (%lums,%s%s), state %s -> %s, "
		    "availability %s",
		    host->conf.name, table_check(table->conf.check),
		    (table->conf.flags & F_TLS) ? " use tls" : "", duration,
		    msg, (codemsg != NULL) ? codemsg : "",
		    host_status(host->last_up), host_status(host->up),
		    print_availability(host->check_cnt, host->up_cnt));
		free(codemsg);
	}

	host->last_up = host->up;

	if (SLIST_EMPTY(&host->children))
		return;

	/* Notify for all other hosts that inherit the state from this one */
	SLIST_FOREACH(h, &host->children, child) {
		h->up = hostup;
		hce_notify_done(h, he);
	}
}

int
hce_dispatch_pfe(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	objid_t			 id;
	struct host		*host;
	struct table		*table;

	switch (imsg->hdr.type) {
	case IMSG_HOST_DISABLE:
		memcpy(&id, imsg->data, sizeof(id));
		if ((host = host_find(env, id)) == NULL)
			fatalx("%s: desynchronized", __func__);
		host->flags |= F_DISABLE;
		host->up = HOST_UNKNOWN;
		host->check_cnt = 0;
		host->up_cnt = 0;
		host->he = HCE_NONE;
		break;
	case IMSG_HOST_ENABLE:
		memcpy(&id, imsg->data, sizeof(id));
		if ((host = host_find(env, id)) == NULL)
			fatalx("%s: desynchronized", __func__);
		host->flags &= ~(F_DISABLE);
		host->up = HOST_UNKNOWN;
		host->he = HCE_NONE;
		break;
	case IMSG_TABLE_DISABLE:
		memcpy(&id, imsg->data, sizeof(id));
		if ((table = table_find(env, id)) == NULL)
			fatalx("%s: desynchronized", __func__);
		table->conf.flags |= F_DISABLE;
		TAILQ_FOREACH(host, &table->hosts, entry)
			host->up = HOST_UNKNOWN;
		break;
	case IMSG_TABLE_ENABLE:
		memcpy(&id, imsg->data, sizeof(id));
		if ((table = table_find(env, id)) == NULL)
			fatalx("%s: desynchronized", __func__);
		table->conf.flags &= ~(F_DISABLE);
		TAILQ_FOREACH(host, &table->hosts, entry)
			host->up = HOST_UNKNOWN;
		break;
	case IMSG_CTL_POLL:
		evtimer_del(&env->sc_ev);
		TAILQ_FOREACH(table, env->sc_tables, entry)
			table->skipped = 0;
		hce_launch_checks(-1, EV_TIMEOUT, env);
		break;
	default:
		return (-1);
	}

	return (0);
}

int
hce_dispatch_parent(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	struct ctl_script	 scr;

	switch (imsg->hdr.type) {
	case IMSG_SCRIPT:
		IMSG_SIZE_CHECK(imsg, &scr);
		bcopy(imsg->data, &scr, sizeof(scr));
		script_done(env, &scr);
		break;
	case IMSG_CFG_TABLE:
		config_gettable(env, imsg);
		break;
	case IMSG_CFG_HOST:
		config_gethost(env, imsg);
		break;
	case IMSG_CFG_DONE:
		config_getcfg(env, imsg);
		break;
	case IMSG_CTL_START:
		hce_setup_events();
		break;
	case IMSG_CTL_RESET:
		config_getreset(env, imsg);
		break;
	default:
		return (-1);
	}

	return (0);
}

int
hce_dispatch_relay(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	switch (imsg->hdr.type) {
	default:
		break;
	}

	return (-1);
}
