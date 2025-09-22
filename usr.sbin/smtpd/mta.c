/*	$OpenBSD: mta.c,v 1.248 2024/04/23 13:34:51 jsg Exp $	*/

/*
 * Copyright (c) 2008 Pierre-Yves Ritschard <pyr@openbsd.org>
 * Copyright (c) 2008 Gilles Chehade <gilles@poolp.org>
 * Copyright (c) 2009 Jacek Masiulaniec <jacekm@dobremiasto.net>
 * Copyright (c) 2012 Eric Faurot <eric@openbsd.org>
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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tls.h>

#include "smtpd.h"
#include "log.h"
#include "ssl.h"

#define MAXERROR_PER_ROUTE	4

#define DELAY_CHECK_SOURCE	1
#define DELAY_CHECK_SOURCE_SLOW	10
#define DELAY_CHECK_SOURCE_FAST 0
#define DELAY_CHECK_LIMIT	5

#define	DELAY_QUADRATIC		1
#define DELAY_ROUTE_BASE	15
#define DELAY_ROUTE_MAX		3600

#define RELAY_ONHOLD		0x01
#define RELAY_HOLDQ		0x02

static void mta_setup_dispatcher(struct dispatcher *);
static void mta_handle_envelope(struct envelope *, const char *);
static void mta_query_smarthost(struct envelope *);
static void mta_on_smarthost(struct envelope *, const char *);
static void mta_query_mx(struct mta_relay *);
static void mta_query_secret(struct mta_relay *);
static void mta_query_preference(struct mta_relay *);
static void mta_query_source(struct mta_relay *);
static void mta_on_mx(void *, void *, void *);
static void mta_on_secret(struct mta_relay *, const char *);
static void mta_on_preference(struct mta_relay *, int);
static void mta_on_source(struct mta_relay *, struct mta_source *);
static void mta_on_timeout(struct runq *, void *);
static void mta_connect(struct mta_connector *);
static void mta_route_enable(struct mta_route *);
static void mta_route_disable(struct mta_route *, int, int);
static void mta_drain(struct mta_relay *);
static void mta_delivery_flush_event(int, short, void *);
static void mta_flush(struct mta_relay *, int, const char *);
static struct mta_route *mta_find_route(struct mta_connector *, time_t, int*,
    time_t*, struct mta_mx **);
static void mta_log(const struct mta_envelope *, const char *, const char *,
    const char *, const char *);

SPLAY_HEAD(mta_relay_tree, mta_relay);
static struct mta_relay *mta_relay(struct envelope *, struct relayhost *);
static void mta_relay_ref(struct mta_relay *);
static void mta_relay_unref(struct mta_relay *);
static void mta_relay_show(struct mta_relay *, struct mproc *, uint32_t, time_t);
static int mta_relay_cmp(const struct mta_relay *, const struct mta_relay *);
SPLAY_PROTOTYPE(mta_relay_tree, mta_relay, entry, mta_relay_cmp);

SPLAY_HEAD(mta_host_tree, mta_host);
static struct mta_host *mta_host(const struct sockaddr *);
static void mta_host_ref(struct mta_host *);
static void mta_host_unref(struct mta_host *);
static int mta_host_cmp(const struct mta_host *, const struct mta_host *);
SPLAY_PROTOTYPE(mta_host_tree, mta_host, entry, mta_host_cmp);

SPLAY_HEAD(mta_domain_tree, mta_domain);
static struct mta_domain *mta_domain(char *, int);
#if 0
static void mta_domain_ref(struct mta_domain *);
#endif
static void mta_domain_unref(struct mta_domain *);
static int mta_domain_cmp(const struct mta_domain *, const struct mta_domain *);
SPLAY_PROTOTYPE(mta_domain_tree, mta_domain, entry, mta_domain_cmp);

SPLAY_HEAD(mta_source_tree, mta_source);
static struct mta_source *mta_source(const struct sockaddr *);
static void mta_source_ref(struct mta_source *);
static void mta_source_unref(struct mta_source *);
static const char *mta_source_to_text(struct mta_source *);
static int mta_source_cmp(const struct mta_source *, const struct mta_source *);
SPLAY_PROTOTYPE(mta_source_tree, mta_source, entry, mta_source_cmp);

static struct mta_connector *mta_connector(struct mta_relay *,
    struct mta_source *);
static void mta_connector_free(struct mta_connector *);
static const char *mta_connector_to_text(struct mta_connector *);

SPLAY_HEAD(mta_route_tree, mta_route);
static struct mta_route *mta_route(struct mta_source *, struct mta_host *);
static void mta_route_ref(struct mta_route *);
static void mta_route_unref(struct mta_route *);
static const char *mta_route_to_text(struct mta_route *);
static int mta_route_cmp(const struct mta_route *, const struct mta_route *);
SPLAY_PROTOTYPE(mta_route_tree, mta_route, entry, mta_route_cmp);

struct mta_block {
	SPLAY_ENTRY(mta_block)	 entry;
	struct mta_source	*source;
	char			*domain;
};

SPLAY_HEAD(mta_block_tree, mta_block);
void mta_block(struct mta_source *, char *);
void mta_unblock(struct mta_source *, char *);
int mta_is_blocked(struct mta_source *, char *);
static int mta_block_cmp(const struct mta_block *, const struct mta_block *);
SPLAY_PROTOTYPE(mta_block_tree, mta_block, entry, mta_block_cmp);

/*
 * This function is not publicy exported because it is a hack until libtls
 * has a proper privsep setup
 */
void tls_config_use_fake_private_key(struct tls_config *config);

static struct mta_relay_tree		relays;
static struct mta_domain_tree		domains;
static struct mta_host_tree		hosts;
static struct mta_source_tree		sources;
static struct mta_route_tree		routes;
static struct mta_block_tree		blocks;

static struct tree wait_mx;
static struct tree wait_preference;
static struct tree wait_secret;
static struct tree wait_smarthost;
static struct tree wait_source;
static struct tree flush_evp;
static struct event ev_flush_evp;

static struct runq *runq_relay;
static struct runq *runq_connector;
static struct runq *runq_route;
static struct runq *runq_hoststat;

static time_t	max_seen_conndelay_route;
static time_t	max_seen_discdelay_route;

#define	HOSTSTAT_EXPIRE_DELAY	(4 * 3600)
struct hoststat {
	char			 name[HOST_NAME_MAX+1];
	time_t			 tm;
	char			 error[LINE_MAX];
	struct tree		 deferred;
};
static struct dict hoststat;

void mta_hoststat_update(const char *, const char *);
void mta_hoststat_cache(const char *, uint64_t);
void mta_hoststat_uncache(const char *, uint64_t);
void mta_hoststat_reschedule(const char *);
static void mta_hoststat_remove_entry(struct hoststat *);

void
mta_imsg(struct mproc *p, struct imsg *imsg)
{
	struct mta_relay	*relay;
	struct mta_domain	*domain;
	struct mta_host		*host;
	struct mta_route	*route;
	struct mta_block	*block;
	struct mta_mx		*mx, *imx;
	struct mta_source	*source;
	struct hoststat		*hs;
	struct sockaddr_storage	 ss;
	struct envelope		 evp, *e;
	struct msg		 m;
	const char		*secret;
	const char		*hostname;
	const char		*dom;
	const char		*smarthost;
	uint64_t		 reqid;
	time_t			 t;
	char			 buf[LINE_MAX];
	int			 dnserror, preference, v, status;
	void			*iter;
	uint64_t		 u64;

	switch (imsg->hdr.type) {
	case IMSG_QUEUE_TRANSFER:
		m_msg(&m, imsg);
		m_get_envelope(&m, &evp);
		m_end(&m);
		mta_handle_envelope(&evp, NULL);
		return;

	case IMSG_MTA_OPEN_MESSAGE:
		mta_session_imsg(p, imsg);
		return;

	case IMSG_MTA_LOOKUP_CREDENTIALS:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_string(&m, &secret);
		m_end(&m);
		relay = tree_xpop(&wait_secret, reqid);
		mta_on_secret(relay, secret[0] ? secret : NULL);
		return;

	case IMSG_MTA_LOOKUP_SOURCE:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_int(&m, &status);
		if (status == LKA_OK)
			m_get_sockaddr(&m, (struct sockaddr*)&ss);
		m_end(&m);

		relay = tree_xpop(&wait_source, reqid);
		mta_on_source(relay, (status == LKA_OK) ?
		    mta_source((struct sockaddr *)&ss) : NULL);
		return;

	case IMSG_MTA_LOOKUP_SMARTHOST:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_int(&m, &status);
		smarthost = NULL;
		if (status == LKA_OK)
			m_get_string(&m, &smarthost);
		m_end(&m);

		e = tree_xpop(&wait_smarthost, reqid);
		mta_on_smarthost(e, smarthost);
		return;

	case IMSG_MTA_LOOKUP_HELO:
		mta_session_imsg(p, imsg);
		return;

	case IMSG_MTA_DNS_HOST:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_string(&m, &hostname);
		m_get_sockaddr(&m, (struct sockaddr*)&ss);
		m_get_int(&m, &preference);
		m_end(&m);
		domain = tree_xget(&wait_mx, reqid);
		mx = xcalloc(1, sizeof *mx);
		mx->mxname = xstrdup(hostname);
		mx->host = mta_host((struct sockaddr*)&ss);
		mx->preference = preference;
		TAILQ_FOREACH(imx, &domain->mxs, entry) {
			if (imx->preference > mx->preference) {
				TAILQ_INSERT_BEFORE(imx, mx, entry);
				return;
			}
		}
		TAILQ_INSERT_TAIL(&domain->mxs, mx, entry);
		return;

	case IMSG_MTA_DNS_HOST_END:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_int(&m, &dnserror);
		m_end(&m);
		domain = tree_xpop(&wait_mx, reqid);
		domain->mxstatus = dnserror;
		if (domain->mxstatus == DNS_OK) {
			log_debug("debug: MXs for domain %s:",
			    domain->name);
			TAILQ_FOREACH(mx, &domain->mxs, entry)
				log_debug("	%s preference %d",
				    sa_to_text(mx->host->sa),
				    mx->preference);
		}
		else {
			log_debug("debug: Failed MX query for %s:",
			    domain->name);
		}
		domain->lastmxquery = time(NULL);
		waitq_run(&domain->mxs, domain);
		return;

	case IMSG_MTA_DNS_MX_PREFERENCE:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_int(&m, &dnserror);
		if (dnserror == 0)
			m_get_int(&m, &preference);
		m_end(&m);

		relay = tree_xpop(&wait_preference, reqid);
		if (dnserror) {
			log_warnx("warn: Couldn't find backup "
			    "preference for %s: error %d",
			    mta_relay_to_text(relay), dnserror);
			preference = INT_MAX;
		}
		mta_on_preference(relay, preference);
		return;

	case IMSG_CTL_RESUME_ROUTE:
		u64 = *((uint64_t *)imsg->data);
		if (u64)
			log_debug("resuming route: %llu",
			    (unsigned long long)u64);
		else
			log_debug("resuming all routes");
		SPLAY_FOREACH(route, mta_route_tree, &routes) {
			if (u64 && route->id != u64)
				continue;

			if (route->flags & ROUTE_DISABLED) {
				log_info("smtp-out: Enabling route %s per admin request",
				    mta_route_to_text(route));
				if (!runq_cancel(runq_route, route)) {
					log_warnx("warn: route not on runq");
					fatalx("exiting");
				}
				route->flags &= ~ROUTE_DISABLED;
				route->flags |= ROUTE_NEW;
				route->nerror = 0;
				route->penalty = 0;
				mta_route_unref(route); /* from mta_route_disable */
			}

			if (u64)
				break;
		}
		return;

	case IMSG_CTL_MTA_SHOW_HOSTS:
		t = time(NULL);
		SPLAY_FOREACH(host, mta_host_tree, &hosts) {
			(void)snprintf(buf, sizeof(buf),
			    "%s %s refcount=%d nconn=%zu lastconn=%s",
			    sockaddr_to_text(host->sa),
			    host->ptrname,
			    host->refcount,
			    host->nconn,
			    host->lastconn ? duration_to_text(t - host->lastconn) : "-");
			m_compose(p, IMSG_CTL_MTA_SHOW_HOSTS,
			    imsg->hdr.peerid, 0, -1,
			    buf, strlen(buf) + 1);
		}
		m_compose(p, IMSG_CTL_MTA_SHOW_HOSTS, imsg->hdr.peerid,
		    0, -1, NULL, 0);
		return;

	case IMSG_CTL_MTA_SHOW_RELAYS:
		t = time(NULL);
		SPLAY_FOREACH(relay, mta_relay_tree, &relays)
			mta_relay_show(relay, p, imsg->hdr.peerid, t);
		m_compose(p, IMSG_CTL_MTA_SHOW_RELAYS, imsg->hdr.peerid,
		    0, -1, NULL, 0);
		return;

	case IMSG_CTL_MTA_SHOW_ROUTES:
		SPLAY_FOREACH(route, mta_route_tree, &routes) {
			v = runq_pending(runq_route, route, &t);
			(void)snprintf(buf, sizeof(buf),
			    "%llu. %s %c%c%c%c nconn=%zu nerror=%d penalty=%d timeout=%s",
			    (unsigned long long)route->id,
			    mta_route_to_text(route),
			    route->flags & ROUTE_NEW ? 'N' : '-',
			    route->flags & ROUTE_DISABLED ? 'D' : '-',
			    route->flags & ROUTE_RUNQ ? 'Q' : '-',
			    route->flags & ROUTE_KEEPALIVE ? 'K' : '-',
			    route->nconn,
			    route->nerror,
			    route->penalty,
			    v ? duration_to_text(t - time(NULL)) : "-");
			m_compose(p, IMSG_CTL_MTA_SHOW_ROUTES,
			    imsg->hdr.peerid, 0, -1,
			    buf, strlen(buf) + 1);
		}
		m_compose(p, IMSG_CTL_MTA_SHOW_ROUTES, imsg->hdr.peerid,
		    0, -1, NULL, 0);
		return;

	case IMSG_CTL_MTA_SHOW_HOSTSTATS:
		iter = NULL;
		while (dict_iter(&hoststat, &iter, &hostname,
			(void **)&hs)) {
			(void)snprintf(buf, sizeof(buf),
			    "%s|%llu|%s",
			    hostname, (unsigned long long) hs->tm,
			    hs->error);
			m_compose(p, IMSG_CTL_MTA_SHOW_HOSTSTATS,
			    imsg->hdr.peerid, 0, -1,
			    buf, strlen(buf) + 1);
		}
		m_compose(p, IMSG_CTL_MTA_SHOW_HOSTSTATS,
		    imsg->hdr.peerid,
		    0, -1, NULL, 0);
		return;

	case IMSG_CTL_MTA_BLOCK:
		m_msg(&m, imsg);
		m_get_sockaddr(&m, (struct sockaddr*)&ss);
		m_get_string(&m, &dom);
		m_end(&m);
		source = mta_source((struct sockaddr*)&ss);
		if (*dom != '\0') {
			if (!(strlcpy(buf, dom, sizeof(buf))
				>= sizeof(buf)))
				mta_block(source, buf);
		}
		else
			mta_block(source, NULL);
		mta_source_unref(source);
		m_compose(p, IMSG_CTL_OK, imsg->hdr.peerid, 0, -1, NULL, 0);
		return;

	case IMSG_CTL_MTA_UNBLOCK:
		m_msg(&m, imsg);
		m_get_sockaddr(&m, (struct sockaddr*)&ss);
		m_get_string(&m, &dom);
		m_end(&m);
		source = mta_source((struct sockaddr*)&ss);
		if (*dom != '\0') {
			if (!(strlcpy(buf, dom, sizeof(buf))
				>= sizeof(buf)))
				mta_unblock(source, buf);
		}
		else
			mta_unblock(source, NULL);
		mta_source_unref(source);
		m_compose(p, IMSG_CTL_OK, imsg->hdr.peerid, 0, -1, NULL, 0);
		return;

	case IMSG_CTL_MTA_SHOW_BLOCK:
		SPLAY_FOREACH(block, mta_block_tree, &blocks) {
			(void)snprintf(buf, sizeof(buf), "%s -> %s",
			    mta_source_to_text(block->source),
			    block->domain ? block->domain : "*");
			m_compose(p, IMSG_CTL_MTA_SHOW_BLOCK,
			    imsg->hdr.peerid, 0, -1, buf, strlen(buf) + 1);
		}
		m_compose(p, IMSG_CTL_MTA_SHOW_BLOCK, imsg->hdr.peerid,
		    0, -1, NULL, 0);
		return;
	}

	fatalx("mta_imsg: unexpected %s imsg", imsg_to_str(imsg->hdr.type));
}

void
mta_postfork(void)
{
	struct dispatcher *dispatcher;
	const char *key;
	void *iter;

	iter = NULL;
	while (dict_iter(env->sc_dispatchers, &iter, &key, (void **)&dispatcher)) {
		log_debug("%s: %s", __func__, key);
		mta_setup_dispatcher(dispatcher);
	}
}

static void
mta_setup_dispatcher(struct dispatcher *dispatcher)
{
	struct dispatcher_remote *remote;
	static const char *dheparams[] = { "none", "auto", "legacy" };
	struct tls_config *config;
	struct pki *pki;
	struct ca *ca;
	const char *ciphers;
	uint32_t protos;

	if (dispatcher->type != DISPATCHER_REMOTE)
		return;

	remote = &dispatcher->u.remote;

	if ((config = tls_config_new()) == NULL)
		fatal("smtpd: tls_config_new");

	ciphers = env->sc_tls_ciphers;
	if (remote->tls_ciphers)
		ciphers = remote->tls_ciphers;
	if (ciphers && tls_config_set_ciphers(config, ciphers) == -1)
		fatalx("%s", tls_config_error(config));

	if (remote->tls_protocols) {
		if (tls_config_parse_protocols(&protos,
		    remote->tls_protocols) == -1)
			fatalx("failed to parse protocols \"%s\"",
			    remote->tls_protocols);
		if (tls_config_set_protocols(config, protos) == -1)
			fatalx("%s", tls_config_error(config));
	}

	if (remote->pki) {
		pki = dict_get(env->sc_pki_dict, remote->pki);
		if (pki == NULL)
			fatalx("client pki \"%s\" not found", remote->pki);

		tls_config_set_dheparams(config, dheparams[pki->pki_dhe]);
		tls_config_use_fake_private_key(config);
		if (tls_config_set_keypair_mem(config, pki->pki_cert,
		    pki->pki_cert_len, NULL, 0) == -1)
			fatalx("tls_config_set_keypair_mem: %s",
			    tls_config_error(config));
	}

	if (remote->ca) {
		ca = dict_get(env->sc_ca_dict, remote->ca);
		if (tls_config_set_ca_mem(config, ca->ca_cert, ca->ca_cert_len)
		    == -1)
			fatalx("tls_config_set_ca_mem: %s",
			    tls_config_error(config));
	}
	else if (tls_config_set_ca_file(config, tls_default_ca_cert_file())
	    == -1)
		fatalx("tls_config_set_ca_file: %s",
		    tls_config_error(config));

	if (remote->tls_verify) {
		tls_config_verify(config);
	} else {
		tls_config_insecure_noverifycert(config);
		tls_config_insecure_noverifyname(config);
		tls_config_insecure_noverifytime(config);
	}

	remote->tls_config = config;
}

void
mta_postprivdrop(void)
{
	SPLAY_INIT(&relays);
	SPLAY_INIT(&domains);
	SPLAY_INIT(&hosts);
	SPLAY_INIT(&sources);
	SPLAY_INIT(&routes);
	SPLAY_INIT(&blocks);

	tree_init(&wait_secret);
	tree_init(&wait_smarthost);
	tree_init(&wait_mx);
	tree_init(&wait_preference);
	tree_init(&wait_source);
	tree_init(&flush_evp);
	dict_init(&hoststat);

	evtimer_set(&ev_flush_evp, mta_delivery_flush_event, NULL);

	runq_init(&runq_relay, mta_on_timeout);
	runq_init(&runq_connector, mta_on_timeout);
	runq_init(&runq_route, mta_on_timeout);
	runq_init(&runq_hoststat, mta_on_timeout);
}


/*
 * Local error on the given source.
 */
void
mta_source_error(struct mta_relay *relay, struct mta_route *route, const char *e)
{
	struct mta_connector	*c;

	/*
	 * Remember the source as broken for this connector.
	 */
	c = mta_connector(relay, route->src);
	if (!(c->flags & CONNECTOR_ERROR_SOURCE))
		log_info("smtp-out: Error on %s: %s",
		    mta_route_to_text(route), e);
	c->flags |= CONNECTOR_ERROR_SOURCE;
}

void
mta_route_error(struct mta_relay *relay, struct mta_route *route)
{
#if 0
	route->nerror += 1;

	if (route->nerror > MAXERROR_PER_ROUTE) {
		log_info("smtp-out: Too many errors on %s: "
		    "disabling for a while", mta_route_to_text(route));
		mta_route_disable(route, 2, ROUTE_DISABLED_SMTP);
	}
#endif
}

void
mta_route_ok(struct mta_relay *relay, struct mta_route *route)
{
	struct mta_connector	*c;

	if (!(route->flags & ROUTE_NEW))
		return;

	log_debug("debug: mta-routing: route %s is now valid.",
	    mta_route_to_text(route));

	route->nerror = 0;
	route->flags &= ~ROUTE_NEW;

	c = mta_connector(relay, route->src);
	mta_connect(c);
}

void
mta_route_down(struct mta_relay *relay, struct mta_route *route)
{
#if 0
	mta_route_disable(route, 2, ROUTE_DISABLED_SMTP);
#endif
}

void
mta_route_collect(struct mta_relay *relay, struct mta_route *route)
{
	struct mta_connector	*c;

	log_debug("debug: mta_route_collect(%s)",
	    mta_route_to_text(route));

	relay->nconn -= 1;
	relay->domain->nconn -= 1;
	route->nconn -= 1;
	route->src->nconn -= 1;
	route->dst->nconn -= 1;
	route->lastdisc = time(NULL);

	/* First connection failed */
	if (route->flags & ROUTE_NEW)
		mta_route_disable(route, 1, ROUTE_DISABLED_NET);

	c = mta_connector(relay, route->src);
	c->nconn -= 1;
	mta_connect(c);
	mta_route_unref(route); /* from mta_find_route() */
	mta_relay_unref(relay); /* from mta_connect() */
}

struct mta_task *
mta_route_next_task(struct mta_relay *relay, struct mta_route *route)
{
	struct mta_task	*task;

	if ((task = TAILQ_FIRST(&relay->tasks))) {
		TAILQ_REMOVE(&relay->tasks, task, entry);
		relay->ntask -= 1;
		task->relay = NULL;

		/* When the number of tasks is down to lowat, query some evp */
		if (relay->ntask == (size_t)relay->limits->task_lowat) {
			if (relay->state & RELAY_ONHOLD) {
				log_info("smtp-out: back to lowat on %s: releasing",
				    mta_relay_to_text(relay));
				relay->state &= ~RELAY_ONHOLD;
			}
			if (relay->state & RELAY_HOLDQ) {
				m_create(p_queue, IMSG_MTA_HOLDQ_RELEASE, 0, 0, -1);
				m_add_id(p_queue, relay->id);
				m_add_int(p_queue, relay->limits->task_release);
				m_close(p_queue);
			}
		}
		else if (relay->ntask == 0 && relay->state & RELAY_HOLDQ) {
			m_create(p_queue, IMSG_MTA_HOLDQ_RELEASE, 0, 0, -1);
			m_add_id(p_queue, relay->id);
			m_add_int(p_queue, 0);
			m_close(p_queue);
		}
	}

	return (task);
}

static void
mta_handle_envelope(struct envelope *evp, const char *smarthost)
{
	struct mta_relay	*relay;
	struct mta_task		*task;
	struct mta_envelope	*e;
	struct dispatcher	*dispatcher;
	struct mailaddr		 maddr;
	struct relayhost	 relayh;
	char			 buf[LINE_MAX];

	dispatcher = dict_xget(env->sc_dispatchers, evp->dispatcher);
	if (dispatcher->u.remote.smarthost && smarthost == NULL) {
		mta_query_smarthost(evp);
		return;
	}

	memset(&relayh, 0, sizeof(relayh));
	relayh.tls = RELAY_TLS_OPPORTUNISTIC;
	if (smarthost && !text_to_relayhost(&relayh, smarthost)) {
		log_warnx("warn: Failed to parse smarthost %s", smarthost);
		m_create(p_queue, IMSG_MTA_DELIVERY_TEMPFAIL, 0, 0, -1);
		m_add_evpid(p_queue, evp->id);
		m_add_string(p_queue, "Cannot parse smarthost");
		m_add_int(p_queue, ESC_OTHER_STATUS);
		m_close(p_queue);
		return;
	}

	if (relayh.flags & RELAY_AUTH && dispatcher->u.remote.auth == NULL) {
		log_warnx("warn: No auth table on action \"%s\" for relay %s",
		    evp->dispatcher, smarthost);
		m_create(p_queue, IMSG_MTA_DELIVERY_TEMPFAIL, 0, 0, -1);
		m_add_evpid(p_queue, evp->id);
		m_add_string(p_queue, "No auth table for relaying");
		m_add_int(p_queue, ESC_OTHER_STATUS);
		m_close(p_queue);
		return;
	}

	if (dispatcher->u.remote.tls_required) {
		/* Reject relay if smtp+notls:// is requested */
		if (relayh.tls == RELAY_TLS_NO) {
			log_warnx("warn: TLS required for action \"%s\"",
			    evp->dispatcher);
			m_create(p_queue, IMSG_MTA_DELIVERY_TEMPFAIL, 0, 0, -1);
			m_add_evpid(p_queue, evp->id);
			m_add_string(p_queue, "TLS required for relaying");
			m_add_int(p_queue, ESC_OTHER_STATUS);
			m_close(p_queue);
			return;
		}
		/* Update smtp:// to smtp+tls:// */
		if (relayh.tls == RELAY_TLS_OPPORTUNISTIC)
			relayh.tls = RELAY_TLS_STARTTLS;
	}

	relay = mta_relay(evp, &relayh);
	/* ignore if we don't know the limits yet */
	if (relay->limits &&
	    relay->ntask >= (size_t)relay->limits->task_hiwat) {
		if (!(relay->state & RELAY_ONHOLD)) {
			log_info("smtp-out: hiwat reached on %s: holding envelopes",
			    mta_relay_to_text(relay));
			relay->state |= RELAY_ONHOLD;
		}
	}

	/*
	 * If the relay has too many pending tasks, tell the
	 * scheduler to hold it until further notice
	 */
	if (relay->state & RELAY_ONHOLD) {
		relay->state |= RELAY_HOLDQ;
		m_create(p_queue, IMSG_MTA_DELIVERY_HOLD, 0, 0, -1);
		m_add_evpid(p_queue, evp->id);
		m_add_id(p_queue, relay->id);
		m_close(p_queue);
		mta_relay_unref(relay); /* from here */
		return;
	}

	task = NULL;
	TAILQ_FOREACH(task, &relay->tasks, entry)
		if (task->msgid == evpid_to_msgid(evp->id))
			break;

	if (task == NULL) {
		task = xmalloc(sizeof *task);
		TAILQ_INIT(&task->envelopes);
		task->relay = relay;
		relay->ntask += 1;
		TAILQ_INSERT_TAIL(&relay->tasks, task, entry);
		task->msgid = evpid_to_msgid(evp->id);
		if (evp->sender.user[0] || evp->sender.domain[0])
			(void)snprintf(buf, sizeof buf, "%s@%s",
			    evp->sender.user, evp->sender.domain);
		else
			buf[0] = '\0';

		if (dispatcher->u.remote.mail_from && evp->sender.user[0]) {
			memset(&maddr, 0, sizeof (maddr));
			if (text_to_mailaddr(&maddr,
				dispatcher->u.remote.mail_from)) {
				(void)snprintf(buf, sizeof buf, "%s@%s",
				    maddr.user[0] ? maddr.user : evp->sender.user,
				    maddr.domain[0] ? maddr.domain : evp->sender.domain);
			}
		}

		task->sender = xstrdup(buf);
		stat_increment("mta.task", 1);
	}

	e = xcalloc(1, sizeof *e);
	e->id = evp->id;
	e->creation = evp->creation;
	e->smtpname = xstrdup(evp->smtpname);
	(void)snprintf(buf, sizeof buf, "%s@%s",
	    evp->dest.user, evp->dest.domain);
	e->dest = xstrdup(buf);
	(void)snprintf(buf, sizeof buf, "%s@%s",
	    evp->rcpt.user, evp->rcpt.domain);
	if (strcmp(buf, e->dest))
		e->rcpt = xstrdup(buf);
	e->task = task;
	if (evp->dsn_orcpt[0] != '\0')
		e->dsn_orcpt = xstrdup(evp->dsn_orcpt);
	(void)strlcpy(e->dsn_envid, evp->dsn_envid,
	    sizeof e->dsn_envid);
	e->dsn_notify = evp->dsn_notify;
	e->dsn_ret = evp->dsn_ret;

	TAILQ_INSERT_TAIL(&task->envelopes, e, entry);
	log_debug("debug: mta: received evp:%016" PRIx64
	    " for <%s>", e->id, e->dest);

	stat_increment("mta.envelope", 1);

	mta_drain(relay);
	mta_relay_unref(relay); /* from here */
}

static void
mta_delivery_flush_event(int fd, short event, void *arg)
{
	struct mta_envelope	*e;
	struct timeval		 tv;

	if (tree_poproot(&flush_evp, NULL, (void**)(&e))) {

		if (e->delivery == IMSG_MTA_DELIVERY_OK) {
			m_create(p_queue, IMSG_MTA_DELIVERY_OK, 0, 0, -1);
			m_add_evpid(p_queue, e->id);
			m_add_int(p_queue, e->ext);
			m_close(p_queue);
		} else if (e->delivery == IMSG_MTA_DELIVERY_TEMPFAIL) {
			m_create(p_queue, IMSG_MTA_DELIVERY_TEMPFAIL, 0, 0, -1);
			m_add_evpid(p_queue, e->id);
			m_add_string(p_queue, e->status);
			m_add_int(p_queue, ESC_OTHER_STATUS);
			m_close(p_queue);
		}
		else if (e->delivery == IMSG_MTA_DELIVERY_PERMFAIL) {
			m_create(p_queue, IMSG_MTA_DELIVERY_PERMFAIL, 0, 0, -1);
			m_add_evpid(p_queue, e->id);
			m_add_string(p_queue, e->status);
			m_add_int(p_queue, ESC_OTHER_STATUS);
			m_close(p_queue);
		}
		else if (e->delivery == IMSG_MTA_DELIVERY_LOOP) {
			m_create(p_queue, IMSG_MTA_DELIVERY_LOOP, 0, 0, -1);
			m_add_evpid(p_queue, e->id);
			m_close(p_queue);
		}
		else {
			log_warnx("warn: bad delivery type %d for %016" PRIx64,
			    e->delivery, e->id);
			fatalx("aborting");
		}

		log_debug("debug: mta: flush for %016"PRIx64" (-> %s)", e->id, e->dest);

		free(e->smtpname);
		free(e->dest);
		free(e->rcpt);
		free(e->dsn_orcpt);
		free(e);

		tv.tv_sec = 0;
		tv.tv_usec = 0;
		evtimer_add(&ev_flush_evp, &tv);
	}
}

void
mta_delivery_log(struct mta_envelope *e, const char *source, const char *relay,
    int delivery, const char *status)
{
	if (delivery == IMSG_MTA_DELIVERY_OK)
		mta_log(e, "Ok", source, relay, status);
	else if (delivery == IMSG_MTA_DELIVERY_TEMPFAIL)
		mta_log(e, "TempFail", source, relay, status);
	else if (delivery == IMSG_MTA_DELIVERY_PERMFAIL)
		mta_log(e, "PermFail", source, relay, status);
	else if (delivery == IMSG_MTA_DELIVERY_LOOP)
		mta_log(e, "PermFail", source, relay, "Loop detected");
	else {
		log_warnx("warn: bad delivery type %d for %016" PRIx64,
		    delivery, e->id);
		fatalx("aborting");
	}

	e->delivery = delivery;
	if (status)
		(void)strlcpy(e->status, status, sizeof(e->status));
}

void
mta_delivery_notify(struct mta_envelope *e)
{
	struct timeval	tv;

	tree_xset(&flush_evp, e->id, e);
	if (tree_count(&flush_evp) == 1) {
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		evtimer_add(&ev_flush_evp, &tv);
	}
}

static void
mta_query_mx(struct mta_relay *relay)
{
	uint64_t	id;

	if (relay->status & RELAY_WAIT_MX)
		return;

	log_debug("debug: mta: querying MX for %s...",
	    mta_relay_to_text(relay));

	if (waitq_wait(&relay->domain->mxs, mta_on_mx, relay)) {
		id = generate_uid();
		tree_xset(&wait_mx, id, relay->domain);
		if (relay->domain->as_host)
			m_create(p_lka,  IMSG_MTA_DNS_HOST, 0, 0, -1);
		else
			m_create(p_lka,  IMSG_MTA_DNS_MX, 0, 0, -1);
		m_add_id(p_lka, id);
		m_add_string(p_lka, relay->domain->name);
		m_close(p_lka);
	}
	relay->status |= RELAY_WAIT_MX;
	mta_relay_ref(relay);
}

static void
mta_query_limits(struct mta_relay *relay)
{
	if (relay->status & RELAY_WAIT_LIMITS)
		return;

	relay->limits = dict_get(env->sc_limits_dict, relay->domain->name);
	if (relay->limits == NULL)
		relay->limits = dict_get(env->sc_limits_dict, "default");

	if (max_seen_conndelay_route < relay->limits->conndelay_route)
		max_seen_conndelay_route = relay->limits->conndelay_route;
	if (max_seen_discdelay_route < relay->limits->discdelay_route)
		max_seen_discdelay_route = relay->limits->discdelay_route;
}

static void
mta_query_secret(struct mta_relay *relay)
{
	if (relay->status & RELAY_WAIT_SECRET)
		return;

	log_debug("debug: mta: querying secret for %s...",
	    mta_relay_to_text(relay));

	tree_xset(&wait_secret, relay->id, relay);
	relay->status |= RELAY_WAIT_SECRET;

	m_create(p_lka, IMSG_MTA_LOOKUP_CREDENTIALS, 0, 0, -1);
	m_add_id(p_lka, relay->id);
	m_add_string(p_lka, relay->authtable);
	m_add_string(p_lka, relay->authlabel);
	m_close(p_lka);

	mta_relay_ref(relay);
}

static void
mta_query_smarthost(struct envelope *evp0)
{
	struct dispatcher *dispatcher;
	struct envelope *evp;

	evp = malloc(sizeof(*evp));
	memmove(evp, evp0, sizeof(*evp));

	dispatcher = dict_xget(env->sc_dispatchers, evp->dispatcher);

	log_debug("debug: mta: querying smarthost for %s:%s...",
	    evp->dispatcher, dispatcher->u.remote.smarthost);

	tree_xset(&wait_smarthost, evp->id, evp);

	m_create(p_lka, IMSG_MTA_LOOKUP_SMARTHOST, 0, 0, -1);
	m_add_id(p_lka, evp->id);
	if (dispatcher->u.remote.smarthost_domain)
		m_add_string(p_lka, evp->dest.domain);
	else
		m_add_string(p_lka, NULL);
	m_add_string(p_lka, dispatcher->u.remote.smarthost);
	m_close(p_lka);

	log_debug("debug: mta: querying smarthost");
}

static void
mta_query_preference(struct mta_relay *relay)
{
	if (relay->status & RELAY_WAIT_PREFERENCE)
		return;

	log_debug("debug: mta: querying preference for %s...",
	    mta_relay_to_text(relay));

	tree_xset(&wait_preference, relay->id, relay);
	relay->status |= RELAY_WAIT_PREFERENCE;

	m_create(p_lka,  IMSG_MTA_DNS_MX_PREFERENCE, 0, 0, -1);
	m_add_id(p_lka, relay->id);
	m_add_string(p_lka, relay->domain->name);
	m_add_string(p_lka, relay->backupname);
	m_close(p_lka);

	mta_relay_ref(relay);
}

static void
mta_query_source(struct mta_relay *relay)
{
	log_debug("debug: mta: querying source for %s...",
	    mta_relay_to_text(relay));

	relay->sourceloop += 1;

	if (relay->sourcetable == NULL) {
		/*
		 * This is a recursive call, but it only happens once, since
		 * another source will not be queried immediately.
		 */
		mta_relay_ref(relay);
		mta_on_source(relay, mta_source(NULL));
		return;
	}

	m_create(p_lka, IMSG_MTA_LOOKUP_SOURCE, 0, 0, -1);
	m_add_id(p_lka, relay->id);
	m_add_string(p_lka, relay->sourcetable);
	m_close(p_lka);

	tree_xset(&wait_source, relay->id, relay);
	relay->status |= RELAY_WAIT_SOURCE;
	mta_relay_ref(relay);
}

static void
mta_on_mx(void *tag, void *arg, void *data)
{
	struct mta_domain	*domain = data;
	struct mta_relay	*relay = arg;

	log_debug("debug: mta: ... got mx (%p, %s, %s)",
	    tag, domain->name, mta_relay_to_text(relay));

	switch (domain->mxstatus) {
	case DNS_OK:
		break;
	case DNS_RETRY:
		relay->fail = IMSG_MTA_DELIVERY_TEMPFAIL;
		relay->failstr = "Temporary failure in MX lookup";
		break;
	case DNS_EINVAL:
		relay->fail = IMSG_MTA_DELIVERY_PERMFAIL;
		relay->failstr = "Invalid domain name";
		break;
	case DNS_ENONAME:
		relay->fail = IMSG_MTA_DELIVERY_PERMFAIL;
		relay->failstr = "Domain does not exist";
		break;
	case DNS_ENOTFOUND:
		relay->fail = IMSG_MTA_DELIVERY_TEMPFAIL;
		if (relay->domain->as_host)
			relay->failstr = "Host not found";
		else
			relay->failstr = "No MX found for domain";
		break;
	case DNS_NULLMX:
		relay->fail = IMSG_MTA_DELIVERY_PERMFAIL;
		relay->failstr = "Domain does not accept mail";
		break;
	default:
		fatalx("bad DNS lookup error code");
		break;
	}

	if (domain->mxstatus)
		log_info("smtp-out: Failed to resolve MX for %s: %s",
		    mta_relay_to_text(relay), relay->failstr);

	relay->status &= ~RELAY_WAIT_MX;
	mta_drain(relay);
	mta_relay_unref(relay); /* from mta_drain() */
}

static void
mta_on_secret(struct mta_relay *relay, const char *secret)
{
	log_debug("debug: mta: ... got secret for %s: %s",
	    mta_relay_to_text(relay), secret);

	if (secret)
		relay->secret = strdup(secret);

	if (relay->secret == NULL) {
		log_warnx("warn: Failed to retrieve secret "
			    "for %s", mta_relay_to_text(relay));
		relay->fail = IMSG_MTA_DELIVERY_TEMPFAIL;
		relay->failstr = "Could not retrieve credentials";
	}

	relay->status &= ~RELAY_WAIT_SECRET;
	mta_drain(relay);
	mta_relay_unref(relay); /* from mta_query_secret() */
}

static void
mta_on_smarthost(struct envelope *evp, const char *smarthost)
{
	if (smarthost == NULL) {
		log_warnx("warn: Failed to retrieve smarthost "
			    "for envelope %"PRIx64, evp->id);
		m_create(p_queue, IMSG_MTA_DELIVERY_TEMPFAIL, 0, 0, -1);
		m_add_evpid(p_queue, evp->id);
		m_add_string(p_queue, "Cannot retrieve smarthost");
		m_add_int(p_queue, ESC_OTHER_STATUS);
		m_close(p_queue);
		return;
	}

	log_debug("debug: mta: ... got smarthost for %016"PRIx64": %s",
	    evp->id, smarthost);
	mta_handle_envelope(evp, smarthost);
	free(evp);
}

static void
mta_on_preference(struct mta_relay *relay, int preference)
{
	log_debug("debug: mta: ... got preference for %s: %d",
	    mta_relay_to_text(relay), preference);

	relay->backuppref = preference;

	relay->status &= ~RELAY_WAIT_PREFERENCE;
	mta_drain(relay);
	mta_relay_unref(relay); /* from mta_query_preference() */
}

static void
mta_on_source(struct mta_relay *relay, struct mta_source *source)
{
	struct mta_connector	*c;
	void			*iter;
	int			 delay, errmask;

	log_debug("debug: mta: ... got source for %s: %s",
	    mta_relay_to_text(relay), source ? mta_source_to_text(source) : "NULL");

	relay->lastsource = time(NULL);
	delay = DELAY_CHECK_SOURCE_SLOW;

	if (source) {
		c = mta_connector(relay, source);
		if (c->flags & CONNECTOR_NEW) {
			c->flags &= ~CONNECTOR_NEW;
			delay = DELAY_CHECK_SOURCE;
		}
		mta_connect(c);
		if ((c->flags & CONNECTOR_ERROR) == 0)
			relay->sourceloop = 0;
		else
			delay = DELAY_CHECK_SOURCE_FAST;
		mta_source_unref(source); /* from constructor */
	}
	else {
		log_warnx("warn: Failed to get source address for %s",
		    mta_relay_to_text(relay));
	}

	if (tree_count(&relay->connectors) == 0) {
		relay->fail = IMSG_MTA_DELIVERY_TEMPFAIL;
		relay->failstr = "Could not retrieve source address";
	}
	if (tree_count(&relay->connectors) < relay->sourceloop) {
		relay->fail = IMSG_MTA_DELIVERY_TEMPFAIL;
		relay->failstr = "No valid route to remote MX";

		errmask = 0;
		iter = NULL;
		while (tree_iter(&relay->connectors, &iter, NULL, (void **)&c))
			errmask |= c->flags;

		if (errmask & CONNECTOR_ERROR_ROUTE_SMTP)
			relay->failstr = "Destination seem to reject all mails";
		else if (errmask & CONNECTOR_ERROR_ROUTE_NET)
			relay->failstr = "Network error on destination MXs";
		else if (errmask & CONNECTOR_ERROR_MX)
			relay->failstr = "No MX found for destination";
		else if (errmask & CONNECTOR_ERROR_FAMILY)
			relay->failstr = "Address family mismatch on destination MXs";
		else if (errmask & CONNECTOR_ERROR_BLOCKED)
			relay->failstr = "All routes to destination blocked";
		else
			relay->failstr = "No valid route to destination";
	}

	relay->nextsource = relay->lastsource + delay;
	relay->status &= ~RELAY_WAIT_SOURCE;
	mta_drain(relay);
	mta_relay_unref(relay); /* from mta_query_source() */
}

static void
mta_connect(struct mta_connector *c)
{
	struct mta_route	*route;
	struct mta_mx		*mx;
	struct mta_limits	*l = c->relay->limits;
	int			 limits;
	time_t			 nextconn, now;

	/* toggle the block flag */
	if (mta_is_blocked(c->source, c->relay->domain->name))
		c->flags |= CONNECTOR_ERROR_BLOCKED;
	else
		c->flags &= ~CONNECTOR_ERROR_BLOCKED;

    again:

	log_debug("debug: mta: connecting with %s", mta_connector_to_text(c));

	/* Do not connect if this connector has an error. */
	if (c->flags & CONNECTOR_ERROR) {
		log_debug("debug: mta: connector error");
		return;
	}

	if (c->flags & CONNECTOR_WAIT) {
		log_debug("debug: mta: cancelling connector timeout");
		runq_cancel(runq_connector, c);
		c->flags &= ~CONNECTOR_WAIT;
	}

	/* No job. */
	if (c->relay->ntask == 0) {
		log_debug("debug: mta: no task for connector");
		return;
	}

	/* Do not create more connections than necessary */
	if ((c->relay->nconn_ready >= c->relay->ntask) ||
	    (c->relay->nconn > 2 && c->relay->nconn >= c->relay->ntask / 2)) {
		log_debug("debug: mta: enough connections already");
		return;
	}

	limits = 0;
	nextconn = now = time(NULL);

	if (c->relay->domain->lastconn + l->conndelay_domain > nextconn) {
		log_debug("debug: mta: cannot use domain %s before %llus",
		    c->relay->domain->name,
		    (unsigned long long) c->relay->domain->lastconn + l->conndelay_domain - now);
		nextconn = c->relay->domain->lastconn + l->conndelay_domain;
	}
	if (c->relay->domain->nconn >= l->maxconn_per_domain) {
		log_debug("debug: mta: hit domain limit");
		limits |= CONNECTOR_LIMIT_DOMAIN;
	}

	if (c->source->lastconn + l->conndelay_source > nextconn) {
		log_debug("debug: mta: cannot use source %s before %llus",
		    mta_source_to_text(c->source),
		    (unsigned long long) c->source->lastconn + l->conndelay_source - now);
		nextconn = c->source->lastconn + l->conndelay_source;
	}
	if (c->source->nconn >= l->maxconn_per_source) {
		log_debug("debug: mta: hit source limit");
		limits |= CONNECTOR_LIMIT_SOURCE;
	}

	if (c->lastconn + l->conndelay_connector > nextconn) {
		log_debug("debug: mta: cannot use %s before %llus",
		    mta_connector_to_text(c),
		    (unsigned long long) c->lastconn + l->conndelay_connector - now);
		nextconn = c->lastconn + l->conndelay_connector;
	}
	if (c->nconn >= l->maxconn_per_connector) {
		log_debug("debug: mta: hit connector limit");
		limits |= CONNECTOR_LIMIT_CONN;
	}

	if (c->relay->lastconn + l->conndelay_relay > nextconn) {
		log_debug("debug: mta: cannot use %s before %llus",
		    mta_relay_to_text(c->relay),
		    (unsigned long long) c->relay->lastconn + l->conndelay_relay - now);
		nextconn = c->relay->lastconn + l->conndelay_relay;
	}
	if (c->relay->nconn >= l->maxconn_per_relay) {
		log_debug("debug: mta: hit relay limit");
		limits |= CONNECTOR_LIMIT_RELAY;
	}

	/* We can connect now, find a route */
	if (!limits && nextconn <= now)
		route = mta_find_route(c, now, &limits, &nextconn, &mx);
	else
		route = NULL;

	/* No route */
	if (route == NULL) {

		if (c->flags & CONNECTOR_ERROR) {
			/* XXX we might want to clear this flag later */
			log_debug("debug: mta-routing: no route available for %s: errors on connector",
			    mta_connector_to_text(c));
			return;
		}
		else if (limits) {
			log_debug("debug: mta-routing: no route available for %s: limits reached",
			    mta_connector_to_text(c));
			nextconn = now + DELAY_CHECK_LIMIT;
		}
		else {
			log_debug("debug: mta-routing: no route available for %s: must wait a bit",
			    mta_connector_to_text(c));
		}
		log_debug("debug: mta: retrying to connect on %s in %llus...",
		    mta_connector_to_text(c),
		    (unsigned long long) nextconn - time(NULL));
		c->flags |= CONNECTOR_WAIT;
		runq_schedule_at(runq_connector, nextconn, c);
		return;
	}

	log_debug("debug: mta-routing: spawning new connection on %s",
		    mta_route_to_text(route));

	c->nconn += 1;
	c->lastconn = time(NULL);

	c->relay->nconn += 1;
	c->relay->lastconn = c->lastconn;
	c->relay->domain->nconn += 1;
	c->relay->domain->lastconn = c->lastconn;
	route->nconn += 1;
	route->lastconn = c->lastconn;
	route->src->nconn += 1;
	route->src->lastconn = c->lastconn;
	route->dst->nconn += 1;
	route->dst->lastconn = c->lastconn;

	mta_session(c->relay, route, mx->mxname);	/* this never fails synchronously */
	mta_relay_ref(c->relay);

	goto again;
}

static void
mta_on_timeout(struct runq *runq, void *arg)
{
	struct mta_connector	*connector = arg;
	struct mta_relay	*relay = arg;
	struct mta_route	*route = arg;
	struct hoststat		*hs = arg;

	if (runq == runq_relay) {
		log_debug("debug: mta: ... timeout for %s",
		    mta_relay_to_text(relay));
		relay->status &= ~RELAY_WAIT_CONNECTOR;
		mta_drain(relay);
		mta_relay_unref(relay); /* from mta_drain() */
	}
	else if (runq == runq_connector) {
		log_debug("debug: mta: ... timeout for %s",
		    mta_connector_to_text(connector));
		connector->flags &= ~CONNECTOR_WAIT;
		mta_connect(connector);
	}
	else if (runq == runq_route) {
		route->flags &= ~ROUTE_RUNQ;
		mta_route_enable(route);
		mta_route_unref(route);
	}
	else if (runq == runq_hoststat) {
		log_debug("debug: mta: ... timeout for hoststat %s",
			hs->name);
		mta_hoststat_remove_entry(hs);
		free(hs);
	}
}

static void
mta_route_disable(struct mta_route *route, int penalty, int reason)
{
	unsigned long long	delay;

	route->penalty += penalty;
	route->lastpenalty = time(NULL);
	delay = (unsigned long long)DELAY_ROUTE_BASE * route->penalty * route->penalty;
	if (delay > DELAY_ROUTE_MAX)
		delay = DELAY_ROUTE_MAX;
#if 0
	delay = 60;
#endif

	log_info("smtp-out: Disabling route %s for %llus",
	    mta_route_to_text(route), delay);

	if (route->flags & ROUTE_DISABLED)
		runq_cancel(runq_route, route);
	else
		mta_route_ref(route);

	route->flags |= reason & ROUTE_DISABLED;
	runq_schedule(runq_route, delay, route);
}

static void
mta_route_enable(struct mta_route *route)
{
	if (route->flags & ROUTE_DISABLED) {
		log_info("smtp-out: Enabling route %s",
		    mta_route_to_text(route));
		route->flags &= ~ROUTE_DISABLED;
		route->flags |= ROUTE_NEW;
		route->nerror = 0;
	}

	if (route->penalty) {
#if DELAY_QUADRATIC
		route->penalty -= 1;
		route->lastpenalty = time(NULL);
#else
		route->penalty = 0;
#endif
	}
}

static void
mta_drain(struct mta_relay *r)
{
	char			 buf[64];

	log_debug("debug: mta: draining %s "
	    "refcount=%d, ntask=%zu, nconnector=%zu, nconn=%zu",
	    mta_relay_to_text(r),
	    r->refcount, r->ntask, tree_count(&r->connectors), r->nconn);

	/*
	 * All done.
	 */
	if (r->ntask == 0) {
		log_debug("debug: mta: all done for %s", mta_relay_to_text(r));
		return;
	}

	/*
	 * If we know that this relay is failing flush the tasks.
	 */
	if (r->fail) {
		mta_flush(r, r->fail, r->failstr);
		return;
	}

	/* Query secret if needed. */
	if (r->flags & RELAY_AUTH && r->secret == NULL)
		mta_query_secret(r);

	/* Query our preference if needed. */
	if (r->backupname && r->backuppref == -1)
		mta_query_preference(r);

	/* Query the domain MXs if needed. */
	if (r->domain->lastmxquery == 0)
		mta_query_mx(r);

	/* Query the limits if needed. */
	if (r->limits == NULL)
		mta_query_limits(r);

	/* Wait until we are ready to proceed. */
	if (r->status & RELAY_WAITMASK) {
		buf[0] = '\0';
		if (r->status & RELAY_WAIT_MX)
			(void)strlcat(buf, " MX", sizeof buf);
		if (r->status & RELAY_WAIT_PREFERENCE)
			(void)strlcat(buf, " preference", sizeof buf);
		if (r->status & RELAY_WAIT_SECRET)
			(void)strlcat(buf, " secret", sizeof buf);
		if (r->status & RELAY_WAIT_SOURCE)
			(void)strlcat(buf, " source", sizeof buf);
		if (r->status & RELAY_WAIT_CONNECTOR)
			(void)strlcat(buf, " connector", sizeof buf);
		log_debug("debug: mta: %s waiting for%s",
		    mta_relay_to_text(r), buf);
		return;
	}

	/*
	 * We have pending task, and it's maybe time too try a new source.
	 */
	if (r->nextsource <= time(NULL))
		mta_query_source(r);
	else {
		log_debug("debug: mta: scheduling relay %s in %llus...",
		    mta_relay_to_text(r),
		    (unsigned long long) r->nextsource - time(NULL));
		runq_schedule_at(runq_relay, r->nextsource, r);
		r->status |= RELAY_WAIT_CONNECTOR;
		mta_relay_ref(r);
	}
}

static void
mta_flush(struct mta_relay *relay, int fail, const char *error)
{
	struct mta_envelope	*e;
	struct mta_task		*task;
	const char     		*domain;
	void			*iter;
	struct mta_connector	*c;
	size_t			 n, r;

	log_debug("debug: mta_flush(%s, %d, \"%s\")",
	    mta_relay_to_text(relay), fail, error);

	if (fail != IMSG_MTA_DELIVERY_TEMPFAIL && fail != IMSG_MTA_DELIVERY_PERMFAIL)
		fatalx("unexpected delivery status %d", fail);

	n = 0;
	while ((task = TAILQ_FIRST(&relay->tasks))) {
		TAILQ_REMOVE(&relay->tasks, task, entry);
		while ((e = TAILQ_FIRST(&task->envelopes))) {
			TAILQ_REMOVE(&task->envelopes, e, entry);

			/*
			 * host was suspended, cache envelope id in hoststat tree
			 * so that it can be retried when a delivery succeeds for
			 * that domain.
			 */
			domain = strchr(e->dest, '@');
			if (fail == IMSG_MTA_DELIVERY_TEMPFAIL && domain) {
				r = 0;
				iter = NULL;
				while (tree_iter(&relay->connectors, &iter,
					NULL, (void **)&c)) {
					if (c->flags & CONNECTOR_ERROR_ROUTE)
						r++;
				}
				if (tree_count(&relay->connectors) == r)
					mta_hoststat_cache(domain+1, e->id);
			}

			mta_delivery_log(e, NULL, relay->domain->name, fail, error);
			mta_delivery_notify(e);

			n++;
		}
		free(task->sender);
		free(task);
	}

	stat_decrement("mta.task", relay->ntask);
	stat_decrement("mta.envelope", n);
	relay->ntask = 0;

	/* release all waiting envelopes for the relay */
	if (relay->state & RELAY_HOLDQ) {
		m_create(p_queue, IMSG_MTA_HOLDQ_RELEASE, 0, 0, -1);
		m_add_id(p_queue, relay->id);
		m_add_int(p_queue, -1);
		m_close(p_queue);
	}
}

/*
 * Find a route to use for this connector
 */
static struct mta_route *
mta_find_route(struct mta_connector *c, time_t now, int *limits,
    time_t *nextconn, struct mta_mx **pmx)
{
	struct mta_route	*route, *best;
	struct mta_limits	*l = c->relay->limits;
	struct mta_mx		*mx;
	int			 level, limit_host, limit_route;
	int			 family_mismatch, seen, suspended_route;
	time_t			 tm;

	log_debug("debug: mta-routing: searching new route for %s...",
	    mta_connector_to_text(c));

	tm = 0;
	limit_host = 0;
	limit_route = 0;
	suspended_route = 0;
	family_mismatch = 0;
	level = -1;
	best = NULL;
	seen = 0;

	TAILQ_FOREACH(mx, &c->relay->domain->mxs, entry) {
		/*
		 * New preference level
		 */
		if (mx->preference > level) {
#ifndef IGNORE_MX_PREFERENCE
			/*
			 * Use the current best MX if found.
			 */
			if (best)
				break;

			/*
			 * No candidate found.  There are valid MXs at this
			 * preference level but they reached their limit, or
			 * we can't connect yet.
			 */
			if (limit_host || limit_route || tm)
				break;

			/*
			 *  If we are a backup MX, do not relay to MXs with
			 *  a greater preference value.
			 */
			if (c->relay->backuppref >= 0 &&
			    mx->preference >= c->relay->backuppref)
				break;

			/*
			 * Start looking at MXs on this preference level.
			 */
#endif
			level = mx->preference;
		}

		if (mx->host->flags & HOST_IGNORE)
			continue;

		/* Found a possibly valid mx */
		seen++;

		if ((c->source->sa &&
		     c->source->sa->sa_family != mx->host->sa->sa_family) ||
		    (l->family && l->family != mx->host->sa->sa_family)) {
			log_debug("debug: mta-routing: skipping host %s: AF mismatch",
			    mta_host_to_text(mx->host));
			family_mismatch = 1;
			continue;
		}

		if (mx->host->nconn >= l->maxconn_per_host) {
			log_debug("debug: mta-routing: skipping host %s: too many connections",
			    mta_host_to_text(mx->host));
			limit_host = 1;
			continue;
		}

		if (mx->host->lastconn + l->conndelay_host > now) {
			log_debug("debug: mta-routing: skipping host %s: cannot use before %llus",
			    mta_host_to_text(mx->host),
			    (unsigned long long) mx->host->lastconn + l->conndelay_host - now);
			if (tm == 0 || mx->host->lastconn + l->conndelay_host < tm)
				tm = mx->host->lastconn + l->conndelay_host;
			continue;
		}

		route = mta_route(c->source, mx->host);

		if (route->flags & ROUTE_DISABLED) {
			log_debug("debug: mta-routing: skipping route %s: suspend",
			    mta_route_to_text(route));
			suspended_route |= route->flags & ROUTE_DISABLED;
			mta_route_unref(route); /* from here */
			continue;
		}

		if (route->nconn && (route->flags & ROUTE_NEW)) {
			log_debug("debug: mta-routing: skipping route %s: not validated yet",
			    mta_route_to_text(route));
			limit_route = 1;
			mta_route_unref(route); /* from here */
			continue;
		}

		if (route->nconn >= l->maxconn_per_route) {
			log_debug("debug: mta-routing: skipping route %s: too many connections",
			    mta_route_to_text(route));
			limit_route = 1;
			mta_route_unref(route); /* from here */
			continue;
		}

		if (route->lastconn + l->conndelay_route > now) {
			log_debug("debug: mta-routing: skipping route %s: cannot use before %llus (delay after connect)",
			    mta_route_to_text(route),
			    (unsigned long long) route->lastconn + l->conndelay_route - now);
			if (tm == 0 || route->lastconn + l->conndelay_route < tm)
				tm = route->lastconn + l->conndelay_route;
			mta_route_unref(route); /* from here */
			continue;
		}

		if (route->lastdisc + l->discdelay_route > now) {
			log_debug("debug: mta-routing: skipping route %s: cannot use before %llus (delay after disconnect)",
			    mta_route_to_text(route),
			    (unsigned long long) route->lastdisc + l->discdelay_route - now);
			if (tm == 0 || route->lastdisc + l->discdelay_route < tm)
				tm = route->lastdisc + l->discdelay_route;
			mta_route_unref(route); /* from here */
			continue;
		}

		/* Use the route with the lowest number of connections. */
		if (best && route->nconn >= best->nconn) {
			log_debug("debug: mta-routing: skipping route %s: current one is better",
			    mta_route_to_text(route));
			mta_route_unref(route); /* from here */
			continue;
		}

		if (best)
			mta_route_unref(best); /* from here */
		best = route;
		*pmx = mx;
		log_debug("debug: mta-routing: selecting candidate route %s",
		    mta_route_to_text(route));
	}

	if (best)
		return (best);

	/* Order is important */
	if (seen == 0) {
		log_info("smtp-out: No MX found for %s",
		    mta_connector_to_text(c));
		c->flags |= CONNECTOR_ERROR_MX;
	}
	else if (limit_route) {
		log_debug("debug: mta: hit route limit");
		*limits |= CONNECTOR_LIMIT_ROUTE;
	}
	else if (limit_host) {
		log_debug("debug: mta: hit host limit");
		*limits |= CONNECTOR_LIMIT_HOST;
	}
	else if (tm) {
		if (tm > *nextconn)
			*nextconn = tm;
	}
	else if (family_mismatch) {
		log_info("smtp-out: Address family mismatch on %s",
		    mta_connector_to_text(c));
		c->flags |= CONNECTOR_ERROR_FAMILY;
	}
	else if (suspended_route) {
		log_info("smtp-out: No valid route for %s",
		    mta_connector_to_text(c));
		if (suspended_route & ROUTE_DISABLED_NET)
			c->flags |= CONNECTOR_ERROR_ROUTE_NET;
		if (suspended_route & ROUTE_DISABLED_SMTP)
			c->flags |= CONNECTOR_ERROR_ROUTE_SMTP;
	}

	return (NULL);
}

static void
mta_log(const struct mta_envelope *evp, const char *prefix, const char *source,
    const char *relay, const char *status)
{
	log_info("%016"PRIx64" mta delivery evpid=%016"PRIx64" "
	    "from=<%s> to=<%s> rcpt=<%s> source=\"%s\" "
	    "relay=\"%s\" delay=%s result=\"%s\" stat=\"%s\"",
	    evp->session,
	    evp->id,
	    evp->task->sender,
	    evp->dest,
	    evp->rcpt ? evp->rcpt : "-",
	    source ? source : "-",
	    relay,
	    duration_to_text(time(NULL) - evp->creation),
	    prefix,
	    status);
}

static struct mta_relay *
mta_relay(struct envelope *e, struct relayhost *relayh)
{
	struct dispatcher	*dispatcher;
	struct mta_relay	 key, *r;

	dispatcher = dict_xget(env->sc_dispatchers, e->dispatcher);

	memset(&key, 0, sizeof key);

	key.pki_name = dispatcher->u.remote.pki;
	key.ca_name = dispatcher->u.remote.ca;
	key.authtable = dispatcher->u.remote.auth;
	key.sourcetable = dispatcher->u.remote.source;
	key.helotable = dispatcher->u.remote.helo_source;
	key.heloname = dispatcher->u.remote.helo;
	key.srs = dispatcher->u.remote.srs;

	if (relayh->hostname[0]) {
		key.domain = mta_domain(relayh->hostname, 1);
	}
	else {
		key.domain = mta_domain(e->dest.domain, 0);
		if (dispatcher->u.remote.backup) {
			key.backupname = dispatcher->u.remote.backupmx;
			if (key.backupname == NULL)
				key.backupname = e->smtpname;
		}
	}

	key.tls = relayh->tls;
	key.flags |= relayh->flags;
	key.port = relayh->port;
	key.authlabel = relayh->authlabel;
	if (!key.authlabel[0])
		key.authlabel = NULL;

	if ((r = SPLAY_FIND(mta_relay_tree, &relays, &key)) == NULL) {
		r = xcalloc(1, sizeof *r);
		TAILQ_INIT(&r->tasks);
		r->id = generate_uid();
		r->dispatcher = dispatcher;
		r->tls = key.tls;
		r->flags = key.flags;
		r->domain = key.domain;
		r->backupname = key.backupname ?
		    xstrdup(key.backupname) : NULL;
		r->backuppref = -1;
		r->port = key.port;
		r->pki_name = key.pki_name ? xstrdup(key.pki_name) : NULL;
		r->ca_name = key.ca_name ? xstrdup(key.ca_name) : NULL;
		if (key.authtable)
			r->authtable = xstrdup(key.authtable);
		if (key.authlabel)
			r->authlabel = xstrdup(key.authlabel);
		if (key.sourcetable)
			r->sourcetable = xstrdup(key.sourcetable);
		if (key.helotable)
			r->helotable = xstrdup(key.helotable);
		if (key.heloname)
			r->heloname = xstrdup(key.heloname);
		r->srs = key.srs;
		SPLAY_INSERT(mta_relay_tree, &relays, r);
		stat_increment("mta.relay", 1);
	} else {
		mta_domain_unref(key.domain); /* from here */
	}

	r->refcount++;
	return (r);
}

static void
mta_relay_ref(struct mta_relay *r)
{
	r->refcount++;
}

static void
mta_relay_unref(struct mta_relay *relay)
{
	struct mta_connector	*c;

	if (--relay->refcount)
		return;

	/* Make sure they are no envelopes held for this relay */
	if (relay->state & RELAY_HOLDQ) {
		m_create(p_queue, IMSG_MTA_HOLDQ_RELEASE, 0, 0, -1);
		m_add_id(p_queue, relay->id);
		m_add_int(p_queue, 0);
		m_close(p_queue);
	}

	log_debug("debug: mta: freeing %s", mta_relay_to_text(relay));
	SPLAY_REMOVE(mta_relay_tree, &relays, relay);

	while ((tree_poproot(&relay->connectors, NULL, (void**)&c)))
		mta_connector_free(c);

	free(relay->authlabel);
	free(relay->authtable);
	free(relay->backupname);
	free(relay->pki_name);
	free(relay->ca_name);
	free(relay->helotable);
	free(relay->heloname);
	free(relay->secret);
	free(relay->sourcetable);

	mta_domain_unref(relay->domain); /* from constructor */
	free(relay);
	stat_decrement("mta.relay", 1);
}

const char *
mta_relay_to_text(struct mta_relay *relay)
{
	static char	 buf[1024];
	char		 tmp[32];
	const char	*sep = ",";

	(void)snprintf(buf, sizeof buf, "[relay:%s", relay->domain->name);

	if (relay->port) {
		(void)strlcat(buf, sep, sizeof buf);
		(void)snprintf(tmp, sizeof tmp, "port=%d", (int)relay->port);
		(void)strlcat(buf, tmp, sizeof buf);
	}

	(void)strlcat(buf, sep, sizeof buf);
	switch(relay->tls) {
	case RELAY_TLS_OPPORTUNISTIC:
		(void)strlcat(buf, "smtp", sizeof buf);
		break;
	case RELAY_TLS_STARTTLS:
		(void)strlcat(buf, "smtp+tls", sizeof buf);
		break;
	case RELAY_TLS_SMTPS:
		(void)strlcat(buf, "smtps", sizeof buf);
		break;
	case RELAY_TLS_NO:
		if (relay->flags & RELAY_LMTP)
			(void)strlcat(buf, "lmtp", sizeof buf);
		else
			(void)strlcat(buf, "smtp+notls", sizeof buf);
		break;
	default:
		(void)strlcat(buf, "???", sizeof buf);
	}

	if (relay->flags & RELAY_AUTH) {
		(void)strlcat(buf, sep, sizeof buf);
		(void)strlcat(buf, "auth=", sizeof buf);
		(void)strlcat(buf, relay->authtable, sizeof buf);
		(void)strlcat(buf, ":", sizeof buf);
		(void)strlcat(buf, relay->authlabel, sizeof buf);
	}

	if (relay->pki_name) {
		(void)strlcat(buf, sep, sizeof buf);
		(void)strlcat(buf, "pki_name=", sizeof buf);
		(void)strlcat(buf, relay->pki_name, sizeof buf);
	}

	if (relay->domain->as_host) {
		(void)strlcat(buf, sep, sizeof buf);
		(void)strlcat(buf, "mx", sizeof buf);
	}

	if (relay->backupname) {
		(void)strlcat(buf, sep, sizeof buf);
		(void)strlcat(buf, "backup=", sizeof buf);
		(void)strlcat(buf, relay->backupname, sizeof buf);
	}

	if (relay->sourcetable) {
		(void)strlcat(buf, sep, sizeof buf);
		(void)strlcat(buf, "sourcetable=", sizeof buf);
		(void)strlcat(buf, relay->sourcetable, sizeof buf);
	}

	if (relay->helotable) {
		(void)strlcat(buf, sep, sizeof buf);
		(void)strlcat(buf, "helotable=", sizeof buf);
		(void)strlcat(buf, relay->helotable, sizeof buf);
	}

	if (relay->heloname) {
		(void)strlcat(buf, sep, sizeof buf);
		(void)strlcat(buf, "heloname=", sizeof buf);
		(void)strlcat(buf, relay->heloname, sizeof buf);
	}

	(void)strlcat(buf, "]", sizeof buf);

	return (buf);
}

static void
mta_relay_show(struct mta_relay *r, struct mproc *p, uint32_t id, time_t t)
{
	struct mta_connector	*c;
	void			*iter;
	char			 buf[1024], flags[1024], dur[64];
	time_t			 to;

	flags[0] = '\0';

#define SHOWSTATUS(f, n) do {							\
		if (r->status & (f)) {						\
			if (flags[0])						\
				(void)strlcat(flags, ",", sizeof(flags));	\
			(void)strlcat(flags, (n), sizeof(flags));		\
		}								\
	} while(0)

	SHOWSTATUS(RELAY_WAIT_MX, "MX");
	SHOWSTATUS(RELAY_WAIT_PREFERENCE, "preference");
	SHOWSTATUS(RELAY_WAIT_SECRET, "secret");
	SHOWSTATUS(RELAY_WAIT_LIMITS, "limits");
	SHOWSTATUS(RELAY_WAIT_SOURCE, "source");
	SHOWSTATUS(RELAY_WAIT_CONNECTOR, "connector");
#undef SHOWSTATUS

	if (runq_pending(runq_relay, r, &to))
		(void)snprintf(dur, sizeof(dur), "%s", duration_to_text(to - t));
	else
		(void)strlcpy(dur, "-", sizeof(dur));

	(void)snprintf(buf, sizeof(buf), "%s refcount=%d ntask=%zu nconn=%zu lastconn=%s timeout=%s wait=%s%s",
	    mta_relay_to_text(r),
	    r->refcount,
	    r->ntask,
	    r->nconn,
	    r->lastconn ? duration_to_text(t - r->lastconn) : "-",
	    dur,
	    flags,
	    (r->state & RELAY_ONHOLD) ? "ONHOLD" : "");
	m_compose(p, IMSG_CTL_MTA_SHOW_RELAYS, id, 0, -1, buf, strlen(buf) + 1);

	iter = NULL;
	while (tree_iter(&r->connectors, &iter, NULL, (void **)&c)) {

		if (runq_pending(runq_connector, c, &to))
			(void)snprintf(dur, sizeof(dur), "%s", duration_to_text(to - t));
		else
			(void)strlcpy(dur, "-", sizeof(dur));

		flags[0] = '\0';

#define SHOWFLAG(f, n) do {							\
		if (c->flags & (f)) {						\
			if (flags[0])						\
				(void)strlcat(flags, ",", sizeof(flags));	\
			(void)strlcat(flags, (n), sizeof(flags));		\
		}								\
	} while(0)

		SHOWFLAG(CONNECTOR_NEW,		"NEW");
		SHOWFLAG(CONNECTOR_WAIT,	"WAIT");

		SHOWFLAG(CONNECTOR_ERROR_FAMILY,	"ERROR_FAMILY");
		SHOWFLAG(CONNECTOR_ERROR_SOURCE,	"ERROR_SOURCE");
		SHOWFLAG(CONNECTOR_ERROR_MX,		"ERROR_MX");
		SHOWFLAG(CONNECTOR_ERROR_ROUTE_NET,	"ERROR_ROUTE_NET");
		SHOWFLAG(CONNECTOR_ERROR_ROUTE_SMTP,	"ERROR_ROUTE_SMTP");
		SHOWFLAG(CONNECTOR_ERROR_BLOCKED,	"ERROR_BLOCKED");

		SHOWFLAG(CONNECTOR_LIMIT_HOST,		"LIMIT_HOST");
		SHOWFLAG(CONNECTOR_LIMIT_ROUTE,		"LIMIT_ROUTE");
		SHOWFLAG(CONNECTOR_LIMIT_SOURCE,	"LIMIT_SOURCE");
		SHOWFLAG(CONNECTOR_LIMIT_RELAY,		"LIMIT_RELAY");
		SHOWFLAG(CONNECTOR_LIMIT_CONN,		"LIMIT_CONN");
		SHOWFLAG(CONNECTOR_LIMIT_DOMAIN,	"LIMIT_DOMAIN");
#undef SHOWFLAG

		(void)snprintf(buf, sizeof(buf),
		    "  connector %s refcount=%d nconn=%zu lastconn=%s timeout=%s flags=%s",
		    mta_source_to_text(c->source),
		    c->refcount,
		    c->nconn,
		    c->lastconn ? duration_to_text(t - c->lastconn) : "-",
		    dur,
		    flags);
		m_compose(p, IMSG_CTL_MTA_SHOW_RELAYS, id, 0, -1, buf,
		    strlen(buf) + 1);


	}
}

static int
mta_relay_cmp(const struct mta_relay *a, const struct mta_relay *b)
{
	int	r;

	if (a->domain < b->domain)
		return (-1);
	if (a->domain > b->domain)
		return (1);

	if (a->tls < b->tls)
		return (-1);
	if (a->tls > b->tls)
		return (1);

	if (a->flags < b->flags)
		return (-1);
	if (a->flags > b->flags)
		return (1);

	if (a->port < b->port)
		return (-1);
	if (a->port > b->port)
		return (1);

	if (a->authtable == NULL && b->authtable)
		return (-1);
	if (a->authtable && b->authtable == NULL)
		return (1);
	if (a->authtable && ((r = strcmp(a->authtable, b->authtable))))
		return (r);
	if (a->authlabel == NULL && b->authlabel)
		return (-1);
	if (a->authlabel && b->authlabel == NULL)
		return (1);
	if (a->authlabel && ((r = strcmp(a->authlabel, b->authlabel))))
		return (r);
	if (a->sourcetable == NULL && b->sourcetable)
		return (-1);
	if (a->sourcetable && b->sourcetable == NULL)
		return (1);
	if (a->sourcetable && ((r = strcmp(a->sourcetable, b->sourcetable))))
		return (r);
	if (a->helotable == NULL && b->helotable)
		return (-1);
	if (a->helotable && b->helotable == NULL)
		return (1);
	if (a->helotable && ((r = strcmp(a->helotable, b->helotable))))
		return (r);
	if (a->heloname == NULL && b->heloname)
		return (-1);
	if (a->heloname && b->heloname == NULL)
		return (1);
	if (a->heloname && ((r = strcmp(a->heloname, b->heloname))))
		return (r);

	if (a->pki_name == NULL && b->pki_name)
		return (-1);
	if (a->pki_name && b->pki_name == NULL)
		return (1);
	if (a->pki_name && ((r = strcmp(a->pki_name, b->pki_name))))
		return (r);

	if (a->ca_name == NULL && b->ca_name)
		return (-1);
	if (a->ca_name && b->ca_name == NULL)
		return (1);
	if (a->ca_name && ((r = strcmp(a->ca_name, b->ca_name))))
		return (r);

	if (a->backupname == NULL && b->backupname)
		return (-1);
	if (a->backupname && b->backupname == NULL)
		return (1);
	if (a->backupname && ((r = strcmp(a->backupname, b->backupname))))
		return (r);

	if (a->srs < b->srs)
		return (-1);
	if (a->srs > b->srs)
		return (1);

	return (0);
}

SPLAY_GENERATE(mta_relay_tree, mta_relay, entry, mta_relay_cmp);

static struct mta_host *
mta_host(const struct sockaddr *sa)
{
	struct mta_host		key, *h;
	struct sockaddr_storage	ss;

	memmove(&ss, sa, sa->sa_len);
	key.sa = (struct sockaddr*)&ss;
	h = SPLAY_FIND(mta_host_tree, &hosts, &key);

	if (h == NULL) {
		h = xcalloc(1, sizeof(*h));
		h->sa = xmemdup(sa, sa->sa_len);
		SPLAY_INSERT(mta_host_tree, &hosts, h);
		stat_increment("mta.host", 1);
	}

	h->refcount++;
	return (h);
}

static void
mta_host_ref(struct mta_host *h)
{
	h->refcount++;
}

static void
mta_host_unref(struct mta_host *h)
{
	if (--h->refcount)
		return;

	SPLAY_REMOVE(mta_host_tree, &hosts, h);
	free(h->sa);
	free(h->ptrname);
	free(h);
	stat_decrement("mta.host", 1);
}

const char *
mta_host_to_text(struct mta_host *h)
{
	static char buf[1024];

	if (h->ptrname)
		(void)snprintf(buf, sizeof buf, "%s (%s)",
		    sa_to_text(h->sa), h->ptrname);
	else
		(void)snprintf(buf, sizeof buf, "%s", sa_to_text(h->sa));

	return (buf);
}

static int
mta_host_cmp(const struct mta_host *a, const struct mta_host *b)
{
	if (a->sa->sa_len < b->sa->sa_len)
		return (-1);
	if (a->sa->sa_len > b->sa->sa_len)
		return (1);
	return (memcmp(a->sa, b->sa, a->sa->sa_len));
}

SPLAY_GENERATE(mta_host_tree, mta_host, entry, mta_host_cmp);

static struct mta_domain *
mta_domain(char *name, int as_host)
{
	struct mta_domain	key, *d;

	key.name = name;
	key.as_host = as_host;
	d = SPLAY_FIND(mta_domain_tree, &domains, &key);

	if (d == NULL) {
		d = xcalloc(1, sizeof(*d));
		d->name = xstrdup(name);
		d->as_host = as_host;
		TAILQ_INIT(&d->mxs);
		SPLAY_INSERT(mta_domain_tree, &domains, d);
		stat_increment("mta.domain", 1);
	}

	d->refcount++;
	return (d);
}

#if 0
static void
mta_domain_ref(struct mta_domain *d)
{
	d->refcount++;
}
#endif

static void
mta_domain_unref(struct mta_domain *d)
{
	struct mta_mx	*mx;

	if (--d->refcount)
		return;

	while ((mx = TAILQ_FIRST(&d->mxs))) {
		TAILQ_REMOVE(&d->mxs, mx, entry);
		mta_host_unref(mx->host); /* from IMSG_DNS_HOST */
		free(mx->mxname);
		free(mx);
	}

	SPLAY_REMOVE(mta_domain_tree, &domains, d);
	free(d->name);
	free(d);
	stat_decrement("mta.domain", 1);
}

static int
mta_domain_cmp(const struct mta_domain *a, const struct mta_domain *b)
{
	if (a->as_host < b->as_host)
		return (-1);
	if (a->as_host > b->as_host)
		return (1);
	return (strcasecmp(a->name, b->name));
}

SPLAY_GENERATE(mta_domain_tree, mta_domain, entry, mta_domain_cmp);

static struct mta_source *
mta_source(const struct sockaddr *sa)
{
	struct mta_source	key, *s;
	struct sockaddr_storage	ss;

	if (sa) {
		memmove(&ss, sa, sa->sa_len);
		key.sa = (struct sockaddr*)&ss;
	} else
		key.sa = NULL;
	s = SPLAY_FIND(mta_source_tree, &sources, &key);

	if (s == NULL) {
		s = xcalloc(1, sizeof(*s));
		if (sa)
			s->sa = xmemdup(sa, sa->sa_len);
		SPLAY_INSERT(mta_source_tree, &sources, s);
		stat_increment("mta.source", 1);
	}

	s->refcount++;
	return (s);
}

static void
mta_source_ref(struct mta_source *s)
{
	s->refcount++;
}

static void
mta_source_unref(struct mta_source *s)
{
	if (--s->refcount)
		return;

	SPLAY_REMOVE(mta_source_tree, &sources, s);
	free(s->sa);
	free(s);
	stat_decrement("mta.source", 1);
}

static const char *
mta_source_to_text(struct mta_source *s)
{
	static char buf[1024];

	if (s->sa == NULL)
		return "[]";
	(void)snprintf(buf, sizeof buf, "%s", sa_to_text(s->sa));
	return (buf);
}

static int
mta_source_cmp(const struct mta_source *a, const struct mta_source *b)
{
	if (a->sa == NULL)
		return ((b->sa == NULL) ? 0 : -1);
	if (b->sa == NULL)
		return (1);
	if (a->sa->sa_len < b->sa->sa_len)
		return (-1);
	if (a->sa->sa_len > b->sa->sa_len)
		return (1);
	return (memcmp(a->sa, b->sa, a->sa->sa_len));
}

SPLAY_GENERATE(mta_source_tree, mta_source, entry, mta_source_cmp);

static struct mta_connector *
mta_connector(struct mta_relay *relay, struct mta_source *source)
{
	struct mta_connector	*c;

	c = tree_get(&relay->connectors, (uintptr_t)(source));
	if (c == NULL) {
		c = xcalloc(1, sizeof(*c));
		c->relay = relay;
		c->source = source;
		c->flags |= CONNECTOR_NEW;
		mta_source_ref(source);
		tree_xset(&relay->connectors, (uintptr_t)(source), c);
		stat_increment("mta.connector", 1);
		log_debug("debug: mta: new %s", mta_connector_to_text(c));
	}

	return (c);
}

static void
mta_connector_free(struct mta_connector *c)
{
	log_debug("debug: mta: freeing %s",
	    mta_connector_to_text(c));

	if (c->flags & CONNECTOR_WAIT) {
		log_debug("debug: mta: cancelling timeout for %s",
		    mta_connector_to_text(c));
		runq_cancel(runq_connector, c);
	}
	mta_source_unref(c->source); /* from constructor */
	free(c);

	stat_decrement("mta.connector", 1);
}

static const char *
mta_connector_to_text(struct mta_connector *c)
{
	static char buf[1024];

	(void)snprintf(buf, sizeof buf, "[connector:%s->%s,0x%x]",
	    mta_source_to_text(c->source),
	    mta_relay_to_text(c->relay),
	    c->flags);
	return (buf);
}

static struct mta_route *
mta_route(struct mta_source *src, struct mta_host *dst)
{
	struct mta_route	key, *r;
	static uint64_t		rid = 0;

	key.src = src;
	key.dst = dst;
	r = SPLAY_FIND(mta_route_tree, &routes, &key);

	if (r == NULL) {
		r = xcalloc(1, sizeof(*r));
		r->src = src;
		r->dst = dst;
		r->flags |= ROUTE_NEW;
		r->id = ++rid;
		SPLAY_INSERT(mta_route_tree, &routes, r);
		mta_source_ref(src);
		mta_host_ref(dst);
		stat_increment("mta.route", 1);
	}
	else if (r->flags & ROUTE_RUNQ) {
		log_debug("debug: mta: mta_route_ref(): cancelling runq for route %s",
		    mta_route_to_text(r));
		r->flags &= ~(ROUTE_RUNQ | ROUTE_KEEPALIVE);
		runq_cancel(runq_route, r);
		r->refcount--; /* from mta_route_unref() */
	}

	r->refcount++;
	return (r);
}

static void
mta_route_ref(struct mta_route *r)
{
	r->refcount++;
}

static void
mta_route_unref(struct mta_route *r)
{
	time_t	sched, now;
	int	delay;

	if (--r->refcount)
		return;

	/*
	 * Nothing references this route, but we might want to keep it alive
	 * for a while.
	 */
	now = time(NULL);
	sched = 0;

	if (r->penalty) {
#if DELAY_QUADRATIC
		delay = DELAY_ROUTE_BASE * r->penalty * r->penalty;
#else
		delay = 15 * 60;
#endif
		if (delay > DELAY_ROUTE_MAX)
			delay = DELAY_ROUTE_MAX;
		sched = r->lastpenalty + delay;
		log_debug("debug: mta: mta_route_unref(): keeping route %s alive for %llus (penalty %d)",
		    mta_route_to_text(r), (unsigned long long) sched - now, r->penalty);
	} else if (!(r->flags & ROUTE_KEEPALIVE)) {
		if (r->lastconn + max_seen_conndelay_route > now)
			sched = r->lastconn + max_seen_conndelay_route;
		if (r->lastdisc + max_seen_discdelay_route > now &&
		    r->lastdisc + max_seen_discdelay_route < sched)
			sched = r->lastdisc + max_seen_discdelay_route;

		if (sched > now)
			log_debug("debug: mta: mta_route_unref(): keeping route %s alive for %llus (imposed delay)",
			    mta_route_to_text(r), (unsigned long long) sched - now);
	}

	if (sched > now) {
		r->flags |= ROUTE_RUNQ;
		runq_schedule_at(runq_route, sched, r);
		r->refcount++;
		return;
	}

	log_debug("debug: mta: mta_route_unref(): really discarding route %s",
	    mta_route_to_text(r));

	SPLAY_REMOVE(mta_route_tree, &routes, r);
	mta_source_unref(r->src); /* from constructor */
	mta_host_unref(r->dst); /* from constructor */
	free(r);
	stat_decrement("mta.route", 1);
}

static const char *
mta_route_to_text(struct mta_route *r)
{
	static char	buf[1024];

	(void)snprintf(buf, sizeof buf, "%s <-> %s",
	    mta_source_to_text(r->src),
	    mta_host_to_text(r->dst));

	return (buf);
}

static int
mta_route_cmp(const struct mta_route *a, const struct mta_route *b)
{
	if (a->src < b->src)
		return (-1);
	if (a->src > b->src)
		return (1);

	if (a->dst < b->dst)
		return (-1);
	if (a->dst > b->dst)
		return (1);

	return (0);
}

SPLAY_GENERATE(mta_route_tree, mta_route, entry, mta_route_cmp);

void
mta_block(struct mta_source *src, char *dom)
{
	struct mta_block key, *b;

	key.source = src;
	key.domain = dom;

	b = SPLAY_FIND(mta_block_tree, &blocks, &key);
	if (b != NULL)
		return;

	b = xcalloc(1, sizeof(*b));
	if (dom)
		b->domain = xstrdup(dom);
	b->source = src;
	mta_source_ref(src);
	SPLAY_INSERT(mta_block_tree, &blocks, b);
}

void
mta_unblock(struct mta_source *src, char *dom)
{
	struct mta_block key, *b;

	key.source = src;
	key.domain = dom;

	b = SPLAY_FIND(mta_block_tree, &blocks, &key);
	if (b == NULL)
		return;

	SPLAY_REMOVE(mta_block_tree, &blocks, b);

	mta_source_unref(b->source);
	free(b->domain);
	free(b);
}

int
mta_is_blocked(struct mta_source *src, char *dom)
{
	struct mta_block key;

	key.source = src;
	key.domain = dom;

	if (SPLAY_FIND(mta_block_tree, &blocks, &key))
		return (1);

	return (0);
}

static
int
mta_block_cmp(const struct mta_block *a, const struct mta_block *b)
{
	if (a->source < b->source)
		return (-1);
	if (a->source > b->source)
		return (1);
	if (!a->domain && b->domain)
		return (-1);
	if (a->domain && !b->domain)
		return (1);
	if (a->domain == b->domain)
		return (0);
	return (strcasecmp(a->domain, b->domain));
}

SPLAY_GENERATE(mta_block_tree, mta_block, entry, mta_block_cmp);



/* hoststat errors are not critical, we do best effort */
void
mta_hoststat_update(const char *host, const char *error)
{
	struct hoststat	*hs = NULL;
	char		 buf[HOST_NAME_MAX+1];

	if (!lowercase(buf, host, sizeof buf))
		return;

	hs = dict_get(&hoststat, buf);
	if (hs == NULL) {
		if ((hs = calloc(1, sizeof *hs)) == NULL)
			return;
		tree_init(&hs->deferred);
		runq_schedule(runq_hoststat, HOSTSTAT_EXPIRE_DELAY, hs);
	}
	(void)strlcpy(hs->name, buf, sizeof hs->name);
	(void)strlcpy(hs->error, error, sizeof hs->error);
	hs->tm = time(NULL);
	dict_set(&hoststat, buf, hs);

	runq_cancel(runq_hoststat, hs);
	runq_schedule(runq_hoststat, HOSTSTAT_EXPIRE_DELAY, hs);
}

void
mta_hoststat_cache(const char *host, uint64_t evpid)
{
	struct hoststat	*hs = NULL;
	char buf[HOST_NAME_MAX+1];

	if (!lowercase(buf, host, sizeof buf))
		return;

	hs = dict_get(&hoststat, buf);
	if (hs == NULL)
		return;

	if (tree_count(&hs->deferred) >= env->sc_mta_max_deferred)
		return;

	tree_set(&hs->deferred, evpid, NULL);
}

void
mta_hoststat_uncache(const char *host, uint64_t evpid)
{
	struct hoststat	*hs = NULL;
	char buf[HOST_NAME_MAX+1];

	if (!lowercase(buf, host, sizeof buf))
		return;

	hs = dict_get(&hoststat, buf);
	if (hs == NULL)
		return;

	tree_pop(&hs->deferred, evpid);
}

void
mta_hoststat_reschedule(const char *host)
{
	struct hoststat	*hs = NULL;
	char		 buf[HOST_NAME_MAX+1];
	uint64_t	 evpid;

	if (!lowercase(buf, host, sizeof buf))
		return;

	hs = dict_get(&hoststat, buf);
	if (hs == NULL)
		return;

	while (tree_poproot(&hs->deferred, &evpid, NULL)) {
		m_compose(p_queue, IMSG_MTA_SCHEDULE, 0, 0, -1,
		    &evpid, sizeof evpid);
	}
}

static void
mta_hoststat_remove_entry(struct hoststat *hs)
{
	while (tree_poproot(&hs->deferred, NULL, NULL))
		;
	dict_pop(&hoststat, hs->name);
	runq_cancel(runq_hoststat, hs);
}
