/*	$OpenBSD: relay.c,v 1.260 2024/10/28 19:56:18 tb Exp $	*/

/*
 * Copyright (c) 2006 - 2014 Reyk Floeter <reyk@openbsd.org>
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
#include <sys/socket.h>
#include <sys/tree.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <event.h>
#include <siphash.h>
#include <imsg.h>

#include <tls.h>

#include "relayd.h"

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))

void		 relay_statistics(int, short, void *);
int		 relay_dispatch_parent(int, struct privsep_proc *,
		    struct imsg *);
int		 relay_dispatch_pfe(int, struct privsep_proc *,
		    struct imsg *);
int		 relay_dispatch_ca(int, struct privsep_proc *,
		    struct imsg *);
int		 relay_dispatch_hce(int, struct privsep_proc *,
		    struct imsg *);
void		 relay_shutdown(void);

void		 relay_protodebug(struct relay *);
void		 relay_ruledebug(struct relay_rule *);
void		 relay_init(struct privsep *, struct privsep_proc *p, void *);
void		 relay_launch(void);
int		 relay_socket(struct sockaddr_storage *, in_port_t,
		    struct protocol *, int, int);
int		 relay_socket_listen(struct sockaddr_storage *, in_port_t,
		    struct protocol *);
int		 relay_socket_connect(struct sockaddr_storage *, in_port_t,
		    struct protocol *, int);

void		 relay_accept(int, short, void *);
void		 relay_input(struct rsession *);

void		 relay_hash_addr(SIPHASH_CTX *, struct sockaddr_storage *, int);

int		 relay_tls_ctx_create(struct relay *);
void		 relay_tls_transaction(struct rsession *,
		    struct ctl_relay_event *);
void		 relay_tls_handshake(int, short, void *);
void		 relay_tls_connected(struct ctl_relay_event *);
void		 relay_tls_readcb(int, short, void *);
void		 relay_tls_writecb(int, short, void *);

void		 relay_connect_retry(int, short, void *);
void		 relay_connect_state(struct rsession *,
		    struct ctl_relay_event *, enum relay_state);

extern void	 bufferevent_read_pressure_cb(struct evbuffer *, size_t,
		    size_t, void *);

volatile int relay_sessions;
volatile int relay_inflight = 0;
objid_t relay_conid;

static struct relayd		*env = NULL;

static struct privsep_proc procs[] = {
	{ "parent",	PROC_PARENT,	relay_dispatch_parent },
	{ "pfe",	PROC_PFE,	relay_dispatch_pfe },
	{ "ca",		PROC_CA,	relay_dispatch_ca },
	{ "hce",	PROC_HCE,	relay_dispatch_hce },
};

void
relay(struct privsep *ps, struct privsep_proc *p)
{
	env = ps->ps_env;
	proc_run(ps, p, procs, nitems(procs), relay_init, NULL);
	relay_http(env);
}

void
relay_shutdown(void)
{
	config_purge(env, CONFIG_ALL);
	usleep(200);	/* XXX relay needs to shutdown last */
}

void
relay_ruledebug(struct relay_rule *rule)
{
	struct kv	*kv = NULL;
	u_int		 i;
	char		 buf[NI_MAXHOST];

	fprintf(stderr, "\t\t");

	switch (rule->rule_action) {
	case RULE_ACTION_MATCH:
		fprintf(stderr, "match ");
		break;
	case RULE_ACTION_BLOCK:
		fprintf(stderr, "block ");
		break;
	case RULE_ACTION_PASS:
		fprintf(stderr, "pass ");
		break;
	}

	switch (rule->rule_dir) {
	case RELAY_DIR_ANY:
		break;
	case RELAY_DIR_REQUEST:
		fprintf(stderr, "request ");
		break;
	case RELAY_DIR_RESPONSE:
		fprintf(stderr, "response ");
		break;
	default:
		return;
		/* NOTREACHED */
		break;
	}

	if (rule->rule_flags & RULE_FLAG_QUICK)
		fprintf(stderr, "quick ");

	switch (rule->rule_af) {
	case AF_INET:
		fprintf(stderr, "inet ");
		break;
	case AF_INET6:
		fprintf(stderr, "inet6 ");
		break;
	}

	if (rule->rule_src.addr.ss_family != AF_UNSPEC)
		fprintf(stderr, "from %s/%d ",
		    print_host(&rule->rule_src.addr, buf, sizeof(buf)),
		    rule->rule_src.addr_mask);

	if (rule->rule_dst.addr.ss_family != AF_UNSPEC)
		fprintf(stderr, "to %s/%d ",
		    print_host(&rule->rule_dst.addr, buf, sizeof(buf)),
		    rule->rule_dst.addr_mask);

	for (i = 1; i < KEY_TYPE_MAX; i++) {
		kv = &rule->rule_kv[i];
		if (kv->kv_type != i)
			continue;

		switch (kv->kv_type) {
		case KEY_TYPE_COOKIE:
			fprintf(stderr, "cookie ");
			break;
		case KEY_TYPE_HEADER:
			fprintf(stderr, "header ");
			break;
		case KEY_TYPE_PATH:
			fprintf(stderr, "path ");
			break;
		case KEY_TYPE_QUERY:
			fprintf(stderr, "query ");
			break;
		case KEY_TYPE_URL:
			fprintf(stderr, "url ");
			break;
		default:
			continue;
		}

		switch (kv->kv_option) {
		case KEY_OPTION_APPEND:
			fprintf(stderr, "append ");
			break;
		case KEY_OPTION_SET:
			fprintf(stderr, "set ");
			break;
		case KEY_OPTION_REMOVE:
			fprintf(stderr, "remove ");
			break;
		case KEY_OPTION_HASH:
			fprintf(stderr, "hash ");
			break;
		case KEY_OPTION_LOG:
			fprintf(stderr, "log ");
			break;
		case KEY_OPTION_STRIP:
			fprintf(stderr, "strip ");
			break;
		case KEY_OPTION_NONE:
			break;
		}

		switch (kv->kv_digest) {
		case DIGEST_SHA1:
		case DIGEST_MD5:
			fprintf(stderr, "digest ");
			break;
		default:
			break;
		}

		int kvv = (kv->kv_option == KEY_OPTION_STRIP ||
		     kv->kv_value == NULL);
		fprintf(stderr, "%s%s%s%s%s%s ",
		    kv->kv_key == NULL ? "" : "\"",
		    kv->kv_key == NULL ? "" : kv->kv_key,
		    kv->kv_key == NULL ? "" : "\"",
		    kvv ? "" : " value \"",
		    kv->kv_value == NULL ? "" : kv->kv_value,
		    kvv ? "" : "\"");
	}

	if (rule->rule_tablename[0])
		fprintf(stderr, "forward to <%s> ", rule->rule_tablename);

	if (rule->rule_tag == -1)
		fprintf(stderr, "no tag ");
	else if (rule->rule_tag && rule->rule_tagname[0])
		fprintf(stderr, "tag \"%s\" ",
		    rule->rule_tagname);

	if (rule->rule_tagged && rule->rule_taggedname[0])
		fprintf(stderr, "tagged \"%s\" ",
		    rule->rule_taggedname);

	if (rule->rule_label == -1)
		fprintf(stderr, "no label ");
	else if (rule->rule_label && rule->rule_labelname[0])
		fprintf(stderr, "label \"%s\" ",
		    rule->rule_labelname);

	fprintf(stderr, "\n");
}

void
relay_protodebug(struct relay *rlay)
{
	struct protocol		*proto = rlay->rl_proto;
	struct relay_rule	*rule = NULL;

	fprintf(stderr, "protocol %d: name %s\n",
	    proto->id, proto->name);
	fprintf(stderr, "\tflags: %s, relay flags: %s\n",
	    printb_flags(proto->flags, F_BITS),
	    printb_flags(rlay->rl_conf.flags, F_BITS));
	if (proto->tcpflags)
		fprintf(stderr, "\ttcp flags: %s\n",
		    printb_flags(proto->tcpflags, TCPFLAG_BITS));
	if ((rlay->rl_conf.flags & (F_TLS|F_TLSCLIENT)) && proto->tlsflags)
		fprintf(stderr, "\ttls flags: %s\n",
		    printb_flags(proto->tlsflags, TLSFLAG_BITS));
	fprintf(stderr, "\ttls session tickets: %s\n",
	    (proto->tickets == 1) ? "enabled" : "disabled");
	fprintf(stderr, "\ttype: ");
	switch (proto->type) {
	case RELAY_PROTO_TCP:
		fprintf(stderr, "tcp\n");
		break;
	case RELAY_PROTO_HTTP:
		fprintf(stderr, "http\n");
		break;
	case RELAY_PROTO_DNS:
		fprintf(stderr, "dns\n");
		break;
	}

	rule = TAILQ_FIRST(&proto->rules);
	while (rule != NULL) {
		relay_ruledebug(rule);
		rule = TAILQ_NEXT(rule, rule_entry);
	}
}

int
relay_privinit(struct relay *rlay)
{
	log_debug("%s: adding relay %s", __func__, rlay->rl_conf.name);

	if (log_getverbose() > 1)
		relay_protodebug(rlay);

	switch (rlay->rl_proto->type) {
	case RELAY_PROTO_DNS:
		relay_udp_privinit(rlay);
		break;
	case RELAY_PROTO_TCP:
		break;
	case RELAY_PROTO_HTTP:
		break;
	}

	if (rlay->rl_conf.flags & F_UDP)
		rlay->rl_s = relay_udp_bind(&rlay->rl_conf.ss,
		    rlay->rl_conf.port, rlay->rl_proto);
	else
		rlay->rl_s = relay_socket_listen(&rlay->rl_conf.ss,
		    rlay->rl_conf.port, rlay->rl_proto);
	if (rlay->rl_s == -1)
		return (-1);

	return (0);
}

void
relay_init(struct privsep *ps, struct privsep_proc *p, void *arg)
{
	struct timeval	 tv;

	if (config_init(ps->ps_env) == -1)
		fatal("failed to initialize configuration");

	/* We use a custom shutdown callback */
	p->p_shutdown = relay_shutdown;

	/* Unlimited file descriptors (use system limits) */
	socket_rlimit(-1);

	if (pledge("stdio recvfd inet", NULL) == -1)
		fatal("pledge");

	/* Schedule statistics timer */
	evtimer_set(&env->sc_statev, relay_statistics, ps);
	bcopy(&env->sc_conf.statinterval, &tv, sizeof(tv));
	evtimer_add(&env->sc_statev, &tv);
}

void
relay_session_publish(struct rsession *s)
{
	proc_compose(env->sc_ps, PROC_PFE, IMSG_SESS_PUBLISH, s, sizeof(*s));
}

void
relay_session_unpublish(struct rsession *s)
{
	proc_compose(env->sc_ps, PROC_PFE, IMSG_SESS_UNPUBLISH,
	    &s->se_id, sizeof(s->se_id));
}

void
relay_statistics(int fd, short events, void *arg)
{
	struct privsep		*ps = arg;
	struct relay		*rlay;
	struct ctl_stats	 crs, *cur;
	struct timeval		 tv, tv_now;
	int			 resethour = 0, resetday = 0;
	struct rsession		*con, *next_con;

	/*
	 * This is a hack to calculate some average statistics.
	 * It doesn't try to be very accurate, but could be improved...
	 */

	timerclear(&tv);
	getmonotime(&tv_now);

	TAILQ_FOREACH(rlay, env->sc_relays, rl_entry) {
		bzero(&crs, sizeof(crs));
		resethour = resetday = 0;

		cur = &rlay->rl_stats[ps->ps_instance];
		cur->cnt += cur->last;
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
		bcopy(cur, &crs, sizeof(crs));

		cur->last = 0;
		if (resethour)
			cur->last_hour = 0;
		if (resetday)
			cur->last_day = 0;

		crs.id = rlay->rl_conf.id;
		crs.proc = ps->ps_instance;
		proc_compose(env->sc_ps, PROC_PFE, IMSG_STATISTICS,
		    &crs, sizeof(crs));

		for (con = SPLAY_ROOT(&rlay->rl_sessions);
		    con != NULL; con = next_con) {
			next_con = SPLAY_NEXT(session_tree,
			    &rlay->rl_sessions, con);
			timersub(&tv_now, &con->se_tv_last, &tv);
			if (timercmp(&tv, &rlay->rl_conf.timeout, >=))
				relay_close(con, "hard timeout", 1);
		}
	}

	/* Schedule statistics timer */
	evtimer_set(&env->sc_statev, relay_statistics, ps);
	bcopy(&env->sc_conf.statinterval, &tv, sizeof(tv));
	evtimer_add(&env->sc_statev, &tv);
}

void
relay_launch(void)
{
	void			(*callback)(int, short, void *);
	struct relay		*rlay;
	struct host		*host;
	struct relay_table	*rlt;

	TAILQ_FOREACH(rlay, env->sc_relays, rl_entry) {
		if ((rlay->rl_conf.flags & (F_TLS|F_TLSCLIENT)) &&
		    relay_tls_ctx_create(rlay) == -1)
			fatalx("%s: failed to create TLS context", __func__);

		TAILQ_FOREACH(rlt, &rlay->rl_tables, rlt_entry) {
			/*
			 * set rule->rule_table in advance and save time
			 * looking up for this later on rule/connection
			 * evalution
			 */
			rule_settable(&rlay->rl_proto->rules, rlt);

			rlt->rlt_index = 0;
			rlt->rlt_nhosts = 0;
			TAILQ_FOREACH(host, &rlt->rlt_table->hosts, entry) {
				if (rlt->rlt_nhosts >= RELAY_MAXHOSTS)
					fatal("%s: too many hosts in table",
					    __func__);
				host->idx = rlt->rlt_nhosts;
				rlt->rlt_host[rlt->rlt_nhosts++] = host;
			}
			log_info("adding %d hosts from table %s%s",
			    rlt->rlt_nhosts, rlt->rlt_table->conf.name,
			    rlt->rlt_table->conf.check ? "" : " (no check)");
		}

		switch (rlay->rl_proto->type) {
		case RELAY_PROTO_DNS:
			relay_udp_init(env, rlay);
			break;
		case RELAY_PROTO_TCP:
		case RELAY_PROTO_HTTP:
			relay_http_init(rlay);
			/* Use defaults */
			break;
		}

		log_debug("%s: running relay %s", __func__,
		    rlay->rl_conf.name);

		rlay->rl_up = HOST_UP;

		if (rlay->rl_conf.flags & F_UDP)
			callback = relay_udp_server;
		else
			callback = relay_accept;

		event_set(&rlay->rl_ev, rlay->rl_s, EV_READ,
		    callback, rlay);
		event_add(&rlay->rl_ev, NULL);
		evtimer_set(&rlay->rl_evt, callback, rlay);
	}
}

int
relay_socket_af(struct sockaddr_storage *ss, in_port_t port)
{
	switch (ss->ss_family) {
	case AF_INET:
		((struct sockaddr_in *)ss)->sin_port = port;
		((struct sockaddr_in *)ss)->sin_len =
		    sizeof(struct sockaddr_in);
		break;
	case AF_INET6:
		((struct sockaddr_in6 *)ss)->sin6_port = port;
		((struct sockaddr_in6 *)ss)->sin6_len =
		    sizeof(struct sockaddr_in6);
		break;
	default:
		return (-1);
	}

	return (0);
}

in_port_t
relay_socket_getport(struct sockaddr_storage *ss)
{
	switch (ss->ss_family) {
	case AF_INET:
		return (((struct sockaddr_in *)ss)->sin_port);
	case AF_INET6:
		return (((struct sockaddr_in6 *)ss)->sin6_port);
	default:
		return (0);
	}

	/* NOTREACHED */
	return (0);
}

int
relay_socket(struct sockaddr_storage *ss, in_port_t port,
    struct protocol *proto, int fd, int reuseport)
{
	struct linger	lng;
	int		s = -1, val;

	if (relay_socket_af(ss, port) == -1)
		goto bad;

	s = fd == -1 ? socket(ss->ss_family,
	    SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP) : fd;
	if (s == -1)
		goto bad;

	/*
	 * Socket options
	 */
	bzero(&lng, sizeof(lng));
	if (setsockopt(s, SOL_SOCKET, SO_LINGER, &lng, sizeof(lng)) == -1)
		goto bad;
	if (reuseport) {
		val = 1;
		if (setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &val,
			sizeof(int)) == -1)
			goto bad;
	}
	if (proto->tcpflags & TCPFLAG_BUFSIZ) {
		val = proto->tcpbufsiz;
		if (setsockopt(s, SOL_SOCKET, SO_RCVBUF,
		    &val, sizeof(val)) == -1)
			goto bad;
		val = proto->tcpbufsiz;
		if (setsockopt(s, SOL_SOCKET, SO_SNDBUF,
		    &val, sizeof(val)) == -1)
			goto bad;
	}

	/*
	 * IP options
	 */
	if (proto->tcpflags & TCPFLAG_IPTTL) {
		val = (int)proto->tcpipttl;
		switch (ss->ss_family) {
		case AF_INET:
			if (setsockopt(s, IPPROTO_IP, IP_TTL,
			    &val, sizeof(val)) == -1)
				goto bad;
			break;
		case AF_INET6:
			if (setsockopt(s, IPPROTO_IPV6, IPV6_UNICAST_HOPS,
			    &val, sizeof(val)) == -1)
				goto bad;
			break;
		}
	}
	if (proto->tcpflags & TCPFLAG_IPMINTTL) {
		val = (int)proto->tcpipminttl;
		switch (ss->ss_family) {
		case AF_INET:
			if (setsockopt(s, IPPROTO_IP, IP_MINTTL,
			    &val, sizeof(val)) == -1)
				goto bad;
			break;
		case AF_INET6:
			if (setsockopt(s, IPPROTO_IPV6, IPV6_MINHOPCOUNT,
			    &val, sizeof(val)) == -1)
				goto bad;
			break;
		}
	}

	/*
	 * TCP options
	 */
	if (proto->tcpflags & (TCPFLAG_NODELAY|TCPFLAG_NNODELAY)) {
		if (proto->tcpflags & TCPFLAG_NNODELAY)
			val = 0;
		else
			val = 1;
		if (setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
		    &val, sizeof(val)) == -1)
			goto bad;
	}
	if (proto->tcpflags & (TCPFLAG_SACK|TCPFLAG_NSACK)) {
		if (proto->tcpflags & TCPFLAG_NSACK)
			val = 0;
		else
			val = 1;
		if (setsockopt(s, IPPROTO_TCP, TCP_SACK_ENABLE,
		    &val, sizeof(val)) == -1)
			goto bad;
	}

	return (s);

 bad:
	if (s != -1)
		close(s);
	return (-1);
}

int
relay_socket_connect(struct sockaddr_storage *ss, in_port_t port,
    struct protocol *proto, int fd)
{
	int	s;

	if ((s = relay_socket(ss, port, proto, fd, 0)) == -1)
		return (-1);

	if (connect(s, (struct sockaddr *)ss, ss->ss_len) == -1) {
		if (errno != EINPROGRESS)
			goto bad;
	}

	return (s);

 bad:
	close(s);
	return (-1);
}

int
relay_socket_listen(struct sockaddr_storage *ss, in_port_t port,
    struct protocol *proto)
{
	int s;

	if ((s = relay_socket(ss, port, proto, -1, 1)) == -1)
		return (-1);

	if (bind(s, (struct sockaddr *)ss, ss->ss_len) == -1)
		goto bad;
	if (listen(s, proto->tcpbacklog) == -1)
		goto bad;

	return (s);

 bad:
	close(s);
	return (-1);
}

void
relay_connected(int fd, short sig, void *arg)
{
	char			 obuf[128];
	struct rsession		*con = arg;
	struct relay		*rlay = con->se_relay;
	struct protocol		*proto = rlay->rl_proto;
	evbuffercb		 outrd = relay_read;
	evbuffercb		 outwr = relay_write;
	struct bufferevent	*bev;
	struct ctl_relay_event	*out = &con->se_out;
	char			*msg;
	socklen_t		 len;
	int			 error;

	if (sig == EV_TIMEOUT) {
		relay_abort_http(con, 504, "connect timeout", 0);
		return;
	}

	len = sizeof(error);
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) == -1) {
		relay_abort_http(con, 500, "getsockopt failed", 0);
		return;
	}
	if (error) {
		errno = error;
		if (asprintf(&msg, "socket error: %s",
		    strerror(error)) >= 0) {
			relay_abort_http(con, 500, msg, 0);
			free(msg);
			return;
		} else {
			relay_abort_http(con, 500,
			    "socket error and asprintf failed", 0);
			return;
		}
	}

	if ((rlay->rl_conf.flags & F_TLSCLIENT) && (out->tls == NULL)) {
		relay_tls_transaction(con, out);
		return;
	}

	DPRINTF("%s: session %d: successful", __func__, con->se_id);

	/* Log destination if it was changed in a keep-alive connection */
	if ((con->se_table != con->se_table0) &&
	    (env->sc_conf.opts & (RELAYD_OPT_LOGCON|RELAYD_OPT_LOGCONERR))) {
		con->se_table0 = con->se_table;
		memset(&obuf, 0, sizeof(obuf));
		(void)print_host(&con->se_out.ss, obuf, sizeof(obuf));
		if (asprintf(&msg, " -> %s:%d",
		    obuf, ntohs(con->se_out.port)) == -1) {
			relay_abort_http(con, 500,
			    "connection changed and asprintf failed", 0);
			return;
		}
		relay_log(con, msg);
		free(msg);
	}

	switch (rlay->rl_proto->type) {
	case RELAY_PROTO_HTTP:
		if (relay_httpdesc_init(out) == -1) {
			relay_close(con,
			    "failed to allocate http descriptor", 1);
			return;
		}
		con->se_out.toread = TOREAD_HTTP_HEADER;
		outrd = relay_read_http;
		break;
	case RELAY_PROTO_TCP:
		/* Use defaults */
		break;
	default:
		fatalx("%s: unknown protocol", __func__);
	}

	/*
	 * Relay <-> Server
	 */
	bev = bufferevent_new(fd, outrd, outwr, relay_error, &con->se_out);
	if (bev == NULL) {
		relay_abort_http(con, 500,
		    "failed to allocate output buffer event", 0);
		return;
	}
	/* write pending output buffer now */
	if (bufferevent_write_buffer(bev, con->se_out.output)) {
		relay_abort_http(con, 500, strerror(errno), 0);
		return;
	}
	con->se_out.bev = bev;

	/* Initialize the TLS wrapper */
	if ((rlay->rl_conf.flags & F_TLSCLIENT) && (out->tls != NULL))
		relay_tls_connected(out);

	bufferevent_settimeout(bev,
	    rlay->rl_conf.timeout.tv_sec, rlay->rl_conf.timeout.tv_sec);
	bufferevent_setwatermark(bev, EV_WRITE,
		RELAY_MIN_PREFETCHED * proto->tcpbufsiz, 0);
	bufferevent_enable(bev, EV_READ|EV_WRITE);
	if (con->se_in.bev)
		bufferevent_enable(con->se_in.bev, EV_READ);

	if (relay_splice(&con->se_out) == -1)
		relay_close(con, strerror(errno), 1);
}

void
relay_input(struct rsession *con)
{
	struct relay	*rlay = con->se_relay;
	struct protocol	*proto = rlay->rl_proto;
	evbuffercb	 inrd = relay_read;
	evbuffercb	 inwr = relay_write;

	switch (rlay->rl_proto->type) {
	case RELAY_PROTO_HTTP:
		if (relay_http_priv_init(con) == -1) {
			relay_close(con,
			    "failed to allocate http descriptor", 1);
			return;
		}
		con->se_in.toread = TOREAD_HTTP_HEADER;
		inrd = relay_read_http;
		break;
	case RELAY_PROTO_TCP:
		/* Use defaults */
		break;
	default:
		fatalx("%s: unknown protocol", __func__);
	}

	/*
	 * Client <-> Relay
	 */
	con->se_in.bev = bufferevent_new(con->se_in.s, inrd, inwr,
	    relay_error, &con->se_in);
	if (con->se_in.bev == NULL) {
		relay_close(con, "failed to allocate input buffer event", 1);
		return;
	}

	/* Initialize the TLS wrapper */
	if ((rlay->rl_conf.flags & F_TLS) && con->se_in.tls != NULL)
		relay_tls_connected(&con->se_in);

	bufferevent_settimeout(con->se_in.bev,
	    rlay->rl_conf.timeout.tv_sec, rlay->rl_conf.timeout.tv_sec);
	bufferevent_setwatermark(con->se_in.bev, EV_WRITE,
		RELAY_MIN_PREFETCHED * proto->tcpbufsiz, 0);
	bufferevent_enable(con->se_in.bev, EV_READ|EV_WRITE);

	if (relay_splice(&con->se_in) == -1)
		relay_close(con, strerror(errno), 1);
}

void
relay_write(struct bufferevent *bev, void *arg)
{
	struct ctl_relay_event	*cre = arg;
	struct rsession		*con = cre->con;

	getmonotime(&con->se_tv_last);

	if (con->se_done && EVBUFFER_LENGTH(EVBUFFER_OUTPUT(bev)) == 0)
		goto done;
	if (cre->dst->bev)
		bufferevent_enable(cre->dst->bev, EV_READ);
	if (relay_splice(cre->dst) == -1)
		goto fail;

	return;
 done:
	relay_close(con, "last write (done)", 0);
	return;
 fail:
	relay_close(con, strerror(errno), 1);
}

void
relay_dump(struct ctl_relay_event *cre, const void *buf, size_t len)
{
	if (!len)
		return;

	/*
	 * This function will dump the specified message directly
	 * to the underlying session, without waiting for success
	 * of non-blocking events etc. This is useful to print an
	 * error message before gracefully closing the session.
	 */
	if (cre->tls != NULL)
		(void)tls_write(cre->tls, buf, len);
	else
		(void)write(cre->s, buf, len);
}

void
relay_read(struct bufferevent *bev, void *arg)
{
	struct ctl_relay_event	*cre = arg;
	struct rsession		*con = cre->con;
	struct protocol		*proto = con->se_relay->rl_proto;
	struct evbuffer		*src = EVBUFFER_INPUT(bev);

	getmonotime(&con->se_tv_last);
	cre->timedout = 0;

	if (!EVBUFFER_LENGTH(src))
		return;
	if (relay_bufferevent_write_buffer(cre->dst, src) == -1)
		goto fail;
	if (con->se_done)
		goto done;
	if (cre->dst->bev)
		bufferevent_enable(cre->dst->bev, EV_READ);
	if (cre->dst->bev && EVBUFFER_LENGTH(EVBUFFER_OUTPUT(cre->dst->bev)) >
	    (size_t)RELAY_MAX_PREFETCH * proto->tcpbufsiz)
		bufferevent_disable(bev, EV_READ);

	return;
 done:
	relay_close(con, "last read (done)", 0);
	return;
 fail:
	relay_close(con, strerror(errno), 1);
}

/*
 * Splice sockets from cre to cre->dst if applicable.  Returns:
 * -1 socket splicing has failed
 * 0 socket splicing is currently not possible
 * 1 socket splicing was successful
 */
int
relay_splice(struct ctl_relay_event *cre)
{
	struct rsession		*con = cre->con;
	struct relay		*rlay = con->se_relay;
	struct protocol		*proto = rlay->rl_proto;
	struct splice		 sp;

	if ((rlay->rl_conf.flags & (F_TLS|F_TLSCLIENT)) ||
	    (proto->tcpflags & TCPFLAG_NSPLICE))
		return (0);

	if (cre->splicelen >= 0)
		return (0);

	/* still not connected */
	if (cre->bev == NULL || cre->dst->bev == NULL)
		return (0);

	if (!(cre->toread == TOREAD_UNLIMITED || cre->toread > 0)) {
		DPRINTF("%s: session %d: splice dir %d, nothing to read %lld",
		    __func__, con->se_id, cre->dir, cre->toread);
		return (0);
	}

	/* do not splice before buffers have not been completely flushed */
	if (EVBUFFER_LENGTH(cre->bev->input) ||
	    EVBUFFER_LENGTH(cre->dst->bev->output)) {
		DPRINTF("%s: session %d: splice dir %d, dirty buffer",
		    __func__, con->se_id, cre->dir);
		bufferevent_disable(cre->bev, EV_READ);
		return (0);
	}

	bzero(&sp, sizeof(sp));
	sp.sp_fd = cre->dst->s;
	sp.sp_max = cre->toread > 0 ? cre->toread : 0;
	bcopy(&rlay->rl_conf.timeout, &sp.sp_idle, sizeof(sp.sp_idle));
	if (setsockopt(cre->s, SOL_SOCKET, SO_SPLICE, &sp, sizeof(sp)) == -1) {
		log_debug("%s: session %d: splice dir %d failed: %s",
		    __func__, con->se_id, cre->dir, strerror(errno));
		return (-1);
	}
	cre->splicelen = 0;
	bufferevent_enable(cre->bev, EV_READ);

	DPRINTF("%s: session %d: splice dir %d, maximum %lld, successful",
	    __func__, con->se_id, cre->dir, cre->toread);

	return (1);
}

int
relay_splicelen(struct ctl_relay_event *cre)
{
	struct rsession		*con = cre->con;
	off_t			 len;
	socklen_t		 optlen;

	if (cre->splicelen < 0)
		return (0);

	optlen = sizeof(len);
	if (getsockopt(cre->s, SOL_SOCKET, SO_SPLICE, &len, &optlen) == -1) {
		log_debug("%s: session %d: splice dir %d get length failed: %s",
		    __func__, con->se_id, cre->dir, strerror(errno));
		return (-1);
	}

	DPRINTF("%s: session %d: splice dir %d, length %lld",
	    __func__, con->se_id, cre->dir, len);

	if (len > cre->splicelen) {
		getmonotime(&con->se_tv_last);

		cre->splicelen = len;
		return (1);
	}

	return (0);
}

int
relay_spliceadjust(struct ctl_relay_event *cre)
{
	if (cre->splicelen < 0)
		return (0);
	if (relay_splicelen(cre) == -1)
		return (-1);
	if (cre->splicelen > 0 && cre->toread > 0)
		cre->toread -= cre->splicelen;
	cre->splicelen = -1;

	return (0);
}

void
relay_error(struct bufferevent *bev, short error, void *arg)
{
	struct ctl_relay_event	*cre = arg;
	struct rsession		*con = cre->con;
	struct evbuffer		*dst;

	DPRINTF("%s: session %d: dir %d state %d to read %lld event error %x",
		__func__, con->se_id, cre->dir, cre->state, cre->toread, error);
	if (error & EVBUFFER_TIMEOUT) {
		if (cre->splicelen >= 0) {
			bufferevent_enable(bev, EV_READ);
		} else if (cre->dst->splicelen >= 0) {
			switch (relay_splicelen(cre->dst)) {
			case -1:
				goto fail;
			case 0:
				relay_close(con, "buffer event timeout", 1);
				break;
			case 1:
				cre->timedout = 1;
				bufferevent_enable(bev, EV_READ);
				break;
			}
		} else {
			relay_close(con, "buffer event timeout", 1);
		}
		return;
	}
	if (error & EVBUFFER_ERROR && errno == ETIMEDOUT) {
		if (cre->dst->splicelen >= 0) {
			switch (relay_splicelen(cre->dst)) {
			case -1:
				goto fail;
			case 0:
				relay_close(con, "splice timeout", 1);
				return;
			case 1:
				bufferevent_enable(bev, EV_READ);
				break;
			}
		} else if (cre->dst->timedout) {
			relay_close(con, "splice timeout", 1);
			return;
		}
		if (relay_spliceadjust(cre) == -1)
			goto fail;
		if (relay_splice(cre) == -1)
			goto fail;
		return;
	}
	if (error & EVBUFFER_ERROR && errno == EFBIG) {
		if (relay_spliceadjust(cre) == -1)
			goto fail;
		bufferevent_enable(cre->bev, EV_READ);
		return;
	}
	if (error & (EVBUFFER_READ|EVBUFFER_WRITE|EVBUFFER_EOF)) {
		bufferevent_disable(bev, EV_READ|EV_WRITE);

		con->se_done = 1;
		if (cre->dst->bev != NULL) {
			dst = EVBUFFER_OUTPUT(cre->dst->bev);
			if (EVBUFFER_LENGTH(dst))
				return;
		} else if (cre->toread == TOREAD_UNLIMITED || cre->toread == 0)
			return;

		relay_close(con, "done", 0);
		return;
	}
	relay_close(con, "buffer event error", 1);
	return;
 fail:
	relay_close(con, strerror(errno), 1);
}

void
relay_accept(int fd, short event, void *arg)
{
	struct privsep		*ps = env->sc_ps;
	struct relay		*rlay = arg;
	struct rsession		*con = NULL;
	struct ctl_natlook	*cnl = NULL;
	socklen_t		 slen;
	struct timeval		 tv;
	struct sockaddr_storage	 ss;
	int			 s = -1;

	event_add(&rlay->rl_ev, NULL);
	if ((event & EV_TIMEOUT))
		return;

	slen = sizeof(ss);
	if ((s = accept_reserve(fd, (struct sockaddr *)&ss,
	    &slen, FD_RESERVE, &relay_inflight)) == -1) {
		/*
		 * Pause accept if we are out of file descriptors, or
		 * libevent will haunt us here too.
		 */
		if (errno == ENFILE || errno == EMFILE) {
			struct timeval evtpause = { 1, 0 };

			event_del(&rlay->rl_ev);
			evtimer_add(&rlay->rl_evt, &evtpause);
			log_debug("%s: deferring connections", __func__);
		}
		return;
	}
	if (rlay->rl_conf.flags & F_DISABLE)
		goto err;

	if ((con = calloc(1, sizeof(*con))) == NULL)
		goto err;

	/* Pre-allocate log buffer */
	con->se_haslog = 0;
	con->se_log = evbuffer_new();
	if (con->se_log == NULL)
		goto err;

	con->se_in.s = s;
	con->se_in.tls = NULL;
	con->se_out.s = -1;
	con->se_out.tls = NULL;
	con->se_in.dst = &con->se_out;
	con->se_out.dst = &con->se_in;
	con->se_in.con = con;
	con->se_out.con = con;
	con->se_in.splicelen = -1;
	con->se_out.splicelen = -1;
	con->se_in.toread = TOREAD_UNLIMITED;
	con->se_out.toread = TOREAD_UNLIMITED;
	con->se_relay = rlay;
	con->se_id = ++relay_conid;
	con->se_relayid = rlay->rl_conf.id;
	con->se_pid = getpid();
	con->se_in.dir = RELAY_DIR_REQUEST;
	con->se_out.dir = RELAY_DIR_RESPONSE;
	con->se_retry = rlay->rl_conf.dstretry;
	con->se_bnds = -1;
	con->se_out.port = rlay->rl_conf.dstport;
	switch (ss.ss_family) {
	case AF_INET:
		con->se_in.port = ((struct sockaddr_in *)&ss)->sin_port;
		break;
	case AF_INET6:
		con->se_in.port = ((struct sockaddr_in6 *)&ss)->sin6_port;
		break;
	}
	memcpy(&con->se_in.ss, &ss, sizeof(con->se_in.ss));

	slen = sizeof(con->se_sockname);
	if (getsockname(s, (struct sockaddr *)&con->se_sockname, &slen) == -1) {
		relay_close(con, "sockname lookup failed", 1);
		return;
	}

	getmonotime(&con->se_tv_start);
	bcopy(&con->se_tv_start, &con->se_tv_last, sizeof(con->se_tv_last));

	if (rlay->rl_conf.flags & F_HASHKEY) {
		SipHash24_Init(&con->se_siphashctx,
		    &rlay->rl_conf.hashkey.siphashkey);
	}

	relay_sessions++;
	SPLAY_INSERT(session_tree, &rlay->rl_sessions, con);
	relay_session_publish(con);

	/* Increment the per-relay session counter */
	rlay->rl_stats[ps->ps_instance].last++;

	/* Pre-allocate output buffer */
	con->se_out.output = evbuffer_new();
	if (con->se_out.output == NULL) {
		relay_close(con, "failed to allocate output buffer", 1);
		return;
	}

	if (rlay->rl_conf.flags & F_DIVERT) {
		memcpy(&con->se_out.ss, &con->se_sockname,
		    sizeof(con->se_out.ss));
		con->se_out.port = relay_socket_getport(&con->se_out.ss);

		/* Detect loop and fall back to the alternate forward target */
		if (bcmp(&rlay->rl_conf.ss, &con->se_out.ss,
		    sizeof(con->se_out.ss)) == 0 &&
		    con->se_out.port == rlay->rl_conf.port)
			con->se_out.ss.ss_family = AF_UNSPEC;
	} else if (rlay->rl_conf.flags & F_NATLOOK) {
		if ((cnl = calloc(1, sizeof(*cnl))) == NULL) {
			relay_close(con, "failed to allocate nat lookup", 1);
			return;
		}

		con->se_cnl = cnl;
		bzero(cnl, sizeof(*cnl));
		cnl->in = -1;
		cnl->id = con->se_id;
		cnl->proc = ps->ps_instance;
		cnl->proto = IPPROTO_TCP;

		memcpy(&cnl->src, &con->se_in.ss, sizeof(cnl->src));
		memcpy(&cnl->dst, &con->se_sockname, sizeof(cnl->dst));

		proc_compose(env->sc_ps, PROC_PFE, IMSG_NATLOOK,
		    cnl, sizeof(*cnl));

		/* Schedule timeout */
		evtimer_set(&con->se_ev, relay_natlook, con);
		bcopy(&rlay->rl_conf.timeout, &tv, sizeof(tv));
		evtimer_add(&con->se_ev, &tv);
		return;
	}

	if (rlay->rl_conf.flags & F_TLSINSPECT) {
		relay_preconnect(con);
		return;
	}

	relay_session(con);
	return;
 err:
	if (s != -1) {
		close(s);
		free(con);
		/*
		 * the session struct was not completely set up, but still
		 * counted as an inflight session. account for this.
		 */
		relay_inflight--;
		log_debug("%s: inflight decremented, now %d",
		    __func__, relay_inflight);
	}
}

void
relay_hash_addr(SIPHASH_CTX *ctx, struct sockaddr_storage *ss, int portset)
{
	struct sockaddr_in	*sin4;
	struct sockaddr_in6	*sin6;
	in_port_t		 port;

	if (ss->ss_family == AF_INET) {
		sin4 = (struct sockaddr_in *)ss;
		SipHash24_Update(ctx, &sin4->sin_addr,
		    sizeof(struct in_addr));
	} else {
		sin6 = (struct sockaddr_in6 *)ss;
		SipHash24_Update(ctx, &sin6->sin6_addr,
		    sizeof(struct in6_addr));
	}

	if (portset != -1) {
		port = (in_port_t)portset;
		SipHash24_Update(ctx, &port, sizeof(port));
	}
}

int
relay_from_table(struct rsession *con)
{
	struct relay		*rlay = con->se_relay;
	struct host		*host = NULL;
	struct relay_table	*rlt = NULL;
	struct table		*table = NULL;
	int			 idx = -1;
	int			 cnt = 0;
	int			 maxtries;
	u_int64_t		 p = 0;

	/* the table is already selected */
	if (con->se_table != NULL) {
		rlt = con->se_table;
		table = rlt->rlt_table;
		if (table->conf.check && !table->up)
			table = NULL;
		goto gottable;
	}

	/* otherwise grep the first active table */
	TAILQ_FOREACH(rlt, &rlay->rl_tables, rlt_entry) {
		table = rlt->rlt_table;
		if ((rlt->rlt_flags & F_USED) == 0 ||
		    (table->conf.check && !table->up))
			table = NULL;
		else
			break;
	}

 gottable:
	if (table == NULL) {
		log_debug("%s: session %d: no active hosts",
		    __func__, con->se_id);
		return (-1);
	}

	switch (rlt->rlt_mode) {
	case RELAY_DSTMODE_ROUNDROBIN:
		if ((int)rlt->rlt_index >= rlt->rlt_nhosts)
			rlt->rlt_index = 0;
		idx = (int)rlt->rlt_index;
		break;
	case RELAY_DSTMODE_RANDOM:
		idx = (int)arc4random_uniform(rlt->rlt_nhosts);
		break;
	case RELAY_DSTMODE_SRCHASH:
		/* Source IP address without port */
		relay_hash_addr(&con->se_siphashctx, &con->se_in.ss, -1);
		break;
	case RELAY_DSTMODE_LOADBALANCE:
		/* Source IP address without port */
		relay_hash_addr(&con->se_siphashctx, &con->se_in.ss, -1);
		/* FALLTHROUGH */
	case RELAY_DSTMODE_HASH:
		/* Local "destination" IP address and port */
		relay_hash_addr(&con->se_siphashctx, &rlay->rl_conf.ss,
		    rlay->rl_conf.port);
		break;
	default:
		fatalx("%s: unsupported mode", __func__);
		/* NOTREACHED */
	}
	if (idx == -1) {
		/* handle all hashing algorithms */
		p = SipHash24_End(&con->se_siphashctx);

		/* Reset hash context */
		SipHash24_Init(&con->se_siphashctx,
		    &rlay->rl_conf.hashkey.siphashkey);

		maxtries = (rlt->rlt_nhosts < RELAY_MAX_HASH_RETRIES ?
		    rlt->rlt_nhosts : RELAY_MAX_HASH_RETRIES);
		for (cnt = 0; cnt < maxtries; cnt++) {
			if ((idx = p % rlt->rlt_nhosts) >= RELAY_MAXHOSTS)
				return (-1);

			host = rlt->rlt_host[idx];

			DPRINTF("%s: session %d: table %s host %s, "
			    "p 0x%016llx, idx %d, cnt %d, max %d",
			    __func__, con->se_id, table->conf.name,
			    host->conf.name, p, idx, cnt, maxtries);

			if (!table->conf.check || host->up == HOST_UP)
				goto found;
			p = p >> 1;
		}
	} else {
		/* handle all non-hashing algorithms */
		host = rlt->rlt_host[idx];
		DPRINTF("%s: session %d: table %s host %s, p 0x%016llx, idx %d",
		    __func__, con->se_id, table->conf.name, host->conf.name,
		    p, idx);
	}

	while (host != NULL) {
		DPRINTF("%s: session %d: host %s", __func__,
		    con->se_id, host->conf.name);
		if (!table->conf.check || host->up == HOST_UP)
			goto found;
		host = TAILQ_NEXT(host, entry);
	}
	TAILQ_FOREACH(host, &table->hosts, entry) {
		DPRINTF("%s: session %d: next host %s",
		    __func__, con->se_id, host->conf.name);
		if (!table->conf.check || host->up == HOST_UP)
			goto found;
	}

	/* Should not happen */
	fatalx("%s: no active hosts, desynchronized", __func__);

 found:
	if (rlt->rlt_mode == RELAY_DSTMODE_ROUNDROBIN)
		rlt->rlt_index = host->idx + 1;
	con->se_retry = host->conf.retry;
	con->se_out.port = table->conf.port;
	bcopy(&host->conf.ss, &con->se_out.ss, sizeof(con->se_out.ss));

	return (0);
}

void
relay_natlook(int fd, short event, void *arg)
{
	struct rsession		*con = arg;
	struct relay		*rlay = con->se_relay;
	struct ctl_natlook	*cnl = con->se_cnl;

	if (cnl == NULL)
		fatalx("invalid NAT lookup");

	if (con->se_out.ss.ss_family == AF_UNSPEC && cnl->in == -1 &&
	    rlay->rl_conf.dstss.ss_family == AF_UNSPEC &&
	    TAILQ_EMPTY(&rlay->rl_tables)) {
		relay_close(con, "session NAT lookup failed", 1);
		return;
	}
	if (cnl->in != -1) {
		bcopy(&cnl->rdst, &con->se_out.ss, sizeof(con->se_out.ss));
		con->se_out.port = cnl->rdport;
	}
	free(con->se_cnl);
	con->se_cnl = NULL;

	relay_session(con);
}

void
relay_session(struct rsession *con)
{
	struct relay		*rlay = con->se_relay;
	struct ctl_relay_event	*in = &con->se_in, *out = &con->se_out;

	if (bcmp(&rlay->rl_conf.ss, &out->ss, sizeof(out->ss)) == 0 &&
	    out->port == rlay->rl_conf.port) {
		log_debug("%s: session %d: looping", __func__, con->se_id);
		relay_close(con, "session aborted", 1);
		return;
	}

	if (rlay->rl_conf.flags & F_UDP) {
		/*
		 * Call the UDP protocol-specific handler
		 */
		if (rlay->rl_proto->request == NULL)
			fatalx("invalid UDP session");
		if ((*rlay->rl_proto->request)(con) == -1)
			relay_close(con, "session failed", 1);
		return;
	}

	if ((rlay->rl_conf.flags & F_TLS) && (in->tls == NULL)) {
		relay_tls_transaction(con, in);
		return;
	}

	if (rlay->rl_proto->type != RELAY_PROTO_HTTP) {
		if (rlay->rl_conf.fwdmode == FWD_TRANS)
			relay_bindanyreq(con, 0, IPPROTO_TCP);
		else if (relay_connect(con) == -1) {
			relay_close(con, "session failed", 1);
			return;
		}
	}

	relay_input(con);
}

void
relay_bindanyreq(struct rsession *con, in_port_t port, int proto)
{
	struct privsep		*ps = env->sc_ps;
	struct relay		*rlay = con->se_relay;
	struct ctl_bindany	 bnd;
	struct timeval		 tv;

	bzero(&bnd, sizeof(bnd));
	bnd.bnd_id = con->se_id;
	bnd.bnd_proc = ps->ps_instance;
	bnd.bnd_port = port;
	bnd.bnd_proto = proto;
	bcopy(&con->se_in.ss, &bnd.bnd_ss, sizeof(bnd.bnd_ss));
	proc_compose(env->sc_ps, PROC_PARENT, IMSG_BINDANY,
	    &bnd, sizeof(bnd));

	/* Schedule timeout */
	evtimer_set(&con->se_ev, relay_bindany, con);
	bcopy(&rlay->rl_conf.timeout, &tv, sizeof(tv));
	evtimer_add(&con->se_ev, &tv);
}

void
relay_bindany(int fd, short event, void *arg)
{
	struct rsession	*con = arg;

	if (con->se_bnds == -1) {
		relay_close(con, "bindany failed, invalid socket", 1);
		return;
	}
	if (relay_connect(con) == -1)
		relay_close(con, "session failed", 1);
}

void
relay_connect_state(struct rsession *con, struct ctl_relay_event *cre,
    enum relay_state new)
{
	DPRINTF("%s: session %d: %s state %s -> %s",
	    __func__, con->se_id,
	    cre->dir == RELAY_DIR_REQUEST ? "accept" : "connect",
	    relay_state(cre->state), relay_state(new));
	cre->state = new;
}

void
relay_connect_retry(int fd, short sig, void *arg)
{
	struct timeval	 evtpause = { 1, 0 };
	struct rsession	*con = arg;
	struct relay	*rlay = con->se_relay;
	int		 bnds = -1;

	if (relay_inflight < 1) {
		log_warnx("%s: no connection in flight", __func__);
		relay_inflight = 1;
	}

	DPRINTF("%s: retry %d of %d, inflight: %d",__func__,
	    con->se_retrycount, con->se_retry, relay_inflight);

	if (sig != EV_TIMEOUT)
		fatalx("%s: called without timeout", __func__);

	evtimer_del(&con->se_inflightevt);

	/*
	 * XXX we might want to check if the inbound socket is still
	 * available: client could have closed it while we were waiting?
	 */

	DPRINTF("%s: got EV_TIMEOUT", __func__);

	if (getdtablecount() + FD_RESERVE +
	    relay_inflight > getdtablesize()) {
		if (con->se_retrycount < RELAY_OUTOF_FD_RETRIES) {
			evtimer_add(&con->se_inflightevt, &evtpause);
			return;
		}
		/* we waited for RELAY_OUTOF_FD_RETRIES seconds, give up */
		event_add(&rlay->rl_ev, NULL);
		relay_abort_http(con, 504, "connection timed out", 0);
		return;
	}

	if (rlay->rl_conf.fwdmode == FWD_TRANS) {
		/* con->se_bnds cannot be unset */
		bnds = con->se_bnds;
	}

 retry:
	if ((con->se_out.s = relay_socket_connect(&con->se_out.ss,
	    con->se_out.port, rlay->rl_proto, bnds)) == -1) {
		log_debug("%s: session %d: "
		    "forward failed: %s, %s", __func__,
		    con->se_id, strerror(errno),
		    con->se_retry ? "next retry" : "last retry");

		con->se_retrycount++;

		if ((errno == ENFILE || errno == EMFILE) &&
		    (con->se_retrycount < con->se_retry)) {
			event_del(&rlay->rl_ev);
			evtimer_add(&con->se_inflightevt, &evtpause);
			evtimer_add(&rlay->rl_evt, &evtpause);
			return;
		} else if (con->se_retrycount < con->se_retry)
			goto retry;
		event_add(&rlay->rl_ev, NULL);
		relay_abort_http(con, 504, "connect failed", 0);
		return;
	}

	if (rlay->rl_conf.flags & F_TLSINSPECT)
		relay_connect_state(con, &con->se_out, STATE_PRECONNECT);
	else
		relay_connect_state(con, &con->se_out, STATE_CONNECTED);
	relay_inflight--;
	DPRINTF("%s: inflight decremented, now %d",__func__, relay_inflight);

	event_add(&rlay->rl_ev, NULL);

	if (errno == EINPROGRESS)
		event_again(&con->se_ev, con->se_out.s, EV_WRITE|EV_TIMEOUT,
		    relay_connected, &con->se_tv_start, &rlay->rl_conf.timeout,
		    con);
	else
		relay_connected(con->se_out.s, EV_WRITE, con);

	return;
}

int
relay_preconnect(struct rsession *con)
{
	int rv;

	log_debug("%s: session %d: process %d", __func__,
	    con->se_id, privsep_process);
	rv = relay_connect(con);
	if (con->se_out.state == STATE_CONNECTED)
		relay_connect_state(con, &con->se_out, STATE_PRECONNECT);
	return (rv);
}

int
relay_connect(struct rsession *con)
{
	struct relay	*rlay = con->se_relay;
	struct timeval	 evtpause = { 1, 0 };
	int		 bnds = -1, ret;

	/* relay_connect should only be called once per relay */
	if (con->se_out.state == STATE_CONNECTED) {
		log_debug("%s: connect already called once", __func__);
		return (0);
	}

	/* Connection is already established but session not active */
	if ((rlay->rl_conf.flags & F_TLSINSPECT) &&
	    con->se_out.state == STATE_PRECONNECT) {
		if (con->se_out.tls == NULL) {
			log_debug("%s: tls connect failed", __func__);
			return (-1);
		}
		relay_connected(con->se_out.s, EV_WRITE, con);
		relay_connect_state(con, &con->se_out, STATE_CONNECTED);
		return (0);
	}

	if (relay_inflight < 1) {
		log_warnx("relay_connect: no connection in flight");
		relay_inflight = 1;
	}

	getmonotime(&con->se_tv_start);

	if (con->se_out.ss.ss_family == AF_UNSPEC &&
	    !TAILQ_EMPTY(&rlay->rl_tables)) {
		if (relay_from_table(con) != 0)
			return (-1);
	} else if (con->se_out.ss.ss_family == AF_UNSPEC) {
		bcopy(&rlay->rl_conf.dstss, &con->se_out.ss,
		    sizeof(con->se_out.ss));
		con->se_out.port = rlay->rl_conf.dstport;
	}

	if (rlay->rl_conf.fwdmode == FWD_TRANS) {
		if (con->se_bnds == -1) {
			log_debug("%s: could not bind any sock", __func__);
			return (-1);
		}
		bnds = con->se_bnds;
	}

	/* Do the IPv4-to-IPv6 or IPv6-to-IPv4 translation if requested */
	if (rlay->rl_conf.dstaf.ss_family != AF_UNSPEC) {
		if (con->se_out.ss.ss_family == AF_INET &&
		    rlay->rl_conf.dstaf.ss_family == AF_INET6)
			ret = map4to6(&con->se_out.ss, &rlay->rl_conf.dstaf);
		else if (con->se_out.ss.ss_family == AF_INET6 &&
		    rlay->rl_conf.dstaf.ss_family == AF_INET)
			ret = map6to4(&con->se_out.ss);
		else
			ret = 0;
		if (ret != 0) {
			log_debug("%s: mapped to invalid address", __func__);
			return (-1);
		}
	}

 retry:
	if ((con->se_out.s = relay_socket_connect(&con->se_out.ss,
	    con->se_out.port, rlay->rl_proto, bnds)) == -1) {
		if (errno == ENFILE || errno == EMFILE) {
			log_debug("%s: session %d: forward failed: %s",
			    __func__, con->se_id, strerror(errno));
			evtimer_set(&con->se_inflightevt, relay_connect_retry,
			    con);
			event_del(&rlay->rl_ev);
			evtimer_add(&con->se_inflightevt, &evtpause);
			evtimer_add(&rlay->rl_evt, &evtpause);

			/* this connect is pending */
			relay_connect_state(con, &con->se_out, STATE_PENDING);
			return (0);
		} else {
			if (con->se_retry) {
				con->se_retry--;
				log_debug("%s: session %d: "
				    "forward failed: %s, %s", __func__,
				    con->se_id, strerror(errno),
				    con->se_retry ?
				    "next retry" : "last retry");
				goto retry;
			}
			log_debug("%s: session %d: forward failed: %s",
			    __func__, con->se_id, strerror(errno));
			return (-1);
		}
	}

	relay_connect_state(con, &con->se_out, STATE_CONNECTED);
	relay_inflight--;
	DPRINTF("%s: inflight decremented, now %d",__func__,
	    relay_inflight);

	if (errno == EINPROGRESS)
		event_again(&con->se_ev, con->se_out.s, EV_WRITE|EV_TIMEOUT,
		    relay_connected, &con->se_tv_start, &rlay->rl_conf.timeout,
		    con);
	else
		relay_connected(con->se_out.s, EV_WRITE, con);

	return (0);
}

void
relay_close(struct rsession *con, const char *msg, int err)
{
	char		 ibuf[128], obuf[128], *ptr = NULL;
	struct relay	*rlay = con->se_relay;
	struct protocol	*proto = rlay->rl_proto;

	SPLAY_REMOVE(session_tree, &rlay->rl_sessions, con);
	relay_session_unpublish(con);

	event_del(&con->se_ev);

	if ((env->sc_conf.opts & (RELAYD_OPT_LOGCON|RELAYD_OPT_LOGCONERR)) &&
	    msg != NULL) {
		bzero(&ibuf, sizeof(ibuf));
		bzero(&obuf, sizeof(obuf));
		(void)print_host(&con->se_in.ss, ibuf, sizeof(ibuf));
		(void)print_host(&con->se_out.ss, obuf, sizeof(obuf));
		if (EVBUFFER_LENGTH(con->se_log) &&
		    evbuffer_add_printf(con->se_log, "\r\n") != -1) {
			ptr = evbuffer_readln(con->se_log, NULL,
			    EVBUFFER_EOL_CRLF);
		}
		if (err == 0 && (env->sc_conf.opts & RELAYD_OPT_LOGCON))
			log_info("relay %s, "
			    "session %d (%d active), %s, %s -> %s:%d, "
			    "%s%s%s", rlay->rl_conf.name, con->se_id,
			    relay_sessions, con->se_tag != 0 ?
			    tag_id2name(con->se_tag) : "0", ibuf, obuf,
			    ntohs(con->se_out.port), msg, ptr == NULL ?
			    "" : ",", ptr == NULL ? "" : ptr);
		if (err == 1 && (env->sc_conf.opts & RELAYD_OPT_LOGCONERR))
			log_warn("relay %s, "
			    "session %d (%d active), %s, %s -> %s:%d, "
			    "%s%s%s", rlay->rl_conf.name, con->se_id,
			    relay_sessions, con->se_tag != 0 ?
			    tag_id2name(con->se_tag) : "0", ibuf, obuf,
			    ntohs(con->se_out.port), msg, ptr == NULL ?
			    "" : ",", ptr == NULL ? "" : ptr);
		free(ptr);
	}

	if (proto->close != NULL)
		(*proto->close)(con);

	free(con->se_priv);

	relay_connect_state(con, &con->se_in, STATE_DONE);
	if (relay_reset_event(con, &con->se_in)) {
		if (con->se_out.s == -1) {
			/*
			 * the output was never connected,
			 * thus this was an inflight session.
			 */
			relay_inflight--;
			log_debug("%s: sessions inflight decremented, now %d",
			    __func__, relay_inflight);
		}
	}
	if (con->se_in.output != NULL)
		evbuffer_free(con->se_in.output);

	relay_connect_state(con, &con->se_out, STATE_DONE);
	if (relay_reset_event(con, &con->se_out)) {
		/* Some file descriptors are available again. */
		if (evtimer_pending(&rlay->rl_evt, NULL)) {
			evtimer_del(&rlay->rl_evt);
			event_add(&rlay->rl_ev, NULL);
		}
	}
	if (con->se_out.output != NULL)
		evbuffer_free(con->se_out.output);

	if (con->se_log != NULL)
		evbuffer_free(con->se_log);

	if (con->se_cnl != NULL) {
#if 0
		proc_compose_imsg(env->sc_ps, PROC_PFE, -1, IMSG_KILLSTATES, -1,
		    cnl, sizeof(*cnl));
#endif
		free(con->se_cnl);
	}

	free(con);
	relay_sessions--;
}

int
relay_reset_event(struct rsession *con, struct ctl_relay_event *cre)
{
	int		 rv = 0;

	if (cre->state != STATE_DONE)
		relay_connect_state(con, cre, STATE_CLOSED);
	if (cre->bev != NULL) {
		bufferevent_disable(cre->bev, EV_READ|EV_WRITE);
		bufferevent_free(cre->bev);
	}
	if (cre->tls != NULL)
		tls_close(cre->tls);
	tls_free(cre->tls);
	tls_free(cre->tls_ctx);
	tls_config_free(cre->tls_cfg);
	free(cre->tlscert);
	if (cre->s != -1) {
		close(cre->s);
		rv = 1;
	}
	cre->bev = NULL;
	cre->tls = NULL;
	cre->tls_cfg = NULL;
	cre->tlscert = NULL;
	cre->s = -1;

	return (rv);
}

int
relay_dispatch_pfe(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	struct relay		*rlay;
	struct rsession		*con, se;
	struct ctl_natlook	 cnl;
	struct timeval		 tv;
	struct host		*host;
	struct table		*table;
	struct ctl_status	 st;
	objid_t			 id;
	int			 cid;

	switch (imsg->hdr.type) {
	case IMSG_HOST_DISABLE:
		memcpy(&id, imsg->data, sizeof(id));
		if ((host = host_find(env, id)) == NULL)
			fatalx("%s: desynchronized", __func__);
		if ((table = table_find(env, host->conf.tableid)) ==
		    NULL)
			fatalx("%s: invalid table id", __func__);
		if (host->up == HOST_UP)
			table->up--;
		host->flags |= F_DISABLE;
		host->up = HOST_UNKNOWN;
		break;
	case IMSG_HOST_ENABLE:
		memcpy(&id, imsg->data, sizeof(id));
		if ((host = host_find(env, id)) == NULL)
			fatalx("%s: desynchronized", __func__);
		host->flags &= ~(F_DISABLE);
		host->up = HOST_UNKNOWN;
		break;
	case IMSG_TABLE_DISABLE:
		memcpy(&id, imsg->data, sizeof(id));
		if ((table = table_find(env, id)) == NULL)
			fatalx("%s: desynchronized", __func__);
		table->conf.flags |= F_DISABLE;
		table->up = 0;
		TAILQ_FOREACH(host, &table->hosts, entry)
			host->up = HOST_UNKNOWN;
		break;
	case IMSG_TABLE_ENABLE:
		memcpy(&id, imsg->data, sizeof(id));
		if ((table = table_find(env, id)) == NULL)
			fatalx("%s: desynchronized", __func__);
		table->conf.flags &= ~(F_DISABLE);
		table->up = 0;
		TAILQ_FOREACH(host, &table->hosts, entry)
			host->up = HOST_UNKNOWN;
		break;
	case IMSG_HOST_STATUS:
		IMSG_SIZE_CHECK(imsg, &st);
		memcpy(&st, imsg->data, sizeof(st));
		if ((host = host_find(env, st.id)) == NULL)
			fatalx("%s: invalid host id", __func__);
		if (host->flags & F_DISABLE)
			break;
		if (host->up == st.up) {
			log_debug("%s: host %d => %d", __func__,
			    host->conf.id, host->up);
			fatalx("%s: desynchronized", __func__);
		}

		if ((table = table_find(env, host->conf.tableid))
		    == NULL)
			fatalx("%s: invalid table id", __func__);

		DPRINTF("%s: [%d] state %d for "
		    "host %u %s", __func__, p->p_ps->ps_instance, st.up,
		    host->conf.id, host->conf.name);

		if ((st.up == HOST_UNKNOWN && host->up == HOST_DOWN) ||
		    (st.up == HOST_DOWN && host->up == HOST_UNKNOWN)) {
			host->up = st.up;
			break;
		}
		if (st.up == HOST_UP)
			table->up++;
		else
			table->up--;
		host->up = st.up;
		break;
	case IMSG_NATLOOK:
		bcopy(imsg->data, &cnl, sizeof(cnl));
		if ((con = session_find(env, cnl.id)) == NULL ||
		    con->se_cnl == NULL) {
			log_debug("%s: session %d: expired",
			    __func__, cnl.id);
			break;
		}
		bcopy(&cnl, con->se_cnl, sizeof(*con->se_cnl));
		evtimer_del(&con->se_ev);
		evtimer_set(&con->se_ev, relay_natlook, con);
		bzero(&tv, sizeof(tv));
		evtimer_add(&con->se_ev, &tv);
		break;
	case IMSG_CTL_SESSION:
		IMSG_SIZE_CHECK(imsg, &cid);
		memcpy(&cid, imsg->data, sizeof(cid));
		TAILQ_FOREACH(rlay, env->sc_relays, rl_entry) {
			SPLAY_FOREACH(con, session_tree,
			    &rlay->rl_sessions) {
				memcpy(&se, con, sizeof(se));
				se.se_cid = cid;
				proc_compose(env->sc_ps, p->p_id,
				    IMSG_CTL_SESSION, &se, sizeof(se));
			}
		}
		proc_compose(env->sc_ps, p->p_id, IMSG_CTL_END,
		    &cid, sizeof(cid));
		break;
	default:
		return (-1);
	}

	return (0);
}

int
relay_dispatch_ca(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	switch (imsg->hdr.type) {
	case IMSG_CA_PRIVENC:
	case IMSG_CA_PRIVDEC:
		log_warnx("%s: priv%s result after timeout", __func__,
		    imsg->hdr.type == IMSG_CA_PRIVENC ? "enc" : "dec");
		return (0);
	}

	return (-1);
}

int
relay_dispatch_parent(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	struct relay_ticket_key	 ticket;
	struct relay		*rlay;
	struct rsession		*con;
	struct timeval		 tv;
	objid_t			 id;

	switch (imsg->hdr.type) {
	case IMSG_BINDANY:
		bcopy(imsg->data, &id, sizeof(id));
		if ((con = session_find(env, id)) == NULL) {
			log_debug("%s: session %d: expired",
			    __func__, id);
			break;
		}

		/* Will validate the result later */
		con->se_bnds = imsg_get_fd(imsg);

		evtimer_del(&con->se_ev);
		evtimer_set(&con->se_ev, relay_bindany, con);
		bzero(&tv, sizeof(tv));
		evtimer_add(&con->se_ev, &tv);
		break;
	case IMSG_CFG_TABLE:
		config_gettable(env, imsg);
		break;
	case IMSG_CFG_HOST:
		config_gethost(env, imsg);
		break;
	case IMSG_CFG_PROTO:
		config_getproto(env, imsg);
		break;
	case IMSG_CFG_RULE:
		config_getrule(env, imsg);
		break;
	case IMSG_CFG_RELAY:
		config_getrelay(env, imsg);
		break;
	case IMSG_CFG_RELAY_TABLE:
		config_getrelaytable(env, imsg);
		break;
	case IMSG_CFG_RELAY_FD:
		config_getrelayfd(env, imsg);
		break;
	case IMSG_CFG_DONE:
		config_getcfg(env, imsg);
		break;
	case IMSG_CTL_START:
		relay_launch();
		break;
	case IMSG_CTL_RESET:
		config_getreset(env, imsg);
		break;
	case IMSG_TLSTICKET_REKEY:
		IMSG_SIZE_CHECK(imsg, (&ticket));
		memcpy(&env->sc_ticket, imsg->data, sizeof(env->sc_ticket));
		TAILQ_FOREACH(rlay, env->sc_relays, rl_entry) {
			if (rlay->rl_conf.flags & F_TLS)
				tls_config_add_ticket_key(rlay->rl_tls_cfg,
				    env->sc_ticket.tt_keyrev,
				    env->sc_ticket.tt_key,
				    sizeof(env->sc_ticket.tt_key));
		}
		break;
	default:
		return (-1);
	}

	return (0);
}

int
relay_dispatch_hce(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	switch (imsg->hdr.type) {
	default:
		break;
	}

	return (-1);
}

static int
relay_tls_ctx_create_proto(struct protocol *proto, struct tls_config *tls_cfg)
{
	uint32_t		 protocols = 0;

	/* Set the allowed TLS protocols */
	if (proto->tlsflags & TLSFLAG_TLSV1_2)
		protocols |= TLS_PROTOCOL_TLSv1_2;
	if (proto->tlsflags & TLSFLAG_TLSV1_3)
		protocols |= TLS_PROTOCOL_TLSv1_3;
	if (tls_config_set_protocols(tls_cfg, protocols) == -1) {
		log_warnx("could not set the TLS protocol: %s",
		    tls_config_error(tls_cfg));
		return (-1);
	}

	if (tls_config_set_ciphers(tls_cfg, proto->tlsciphers)) {
		log_warnx("could not set the TLS cypers: %s",
		    tls_config_error(tls_cfg));
		return (-1);
	}

	if ((proto->tlsflags & TLSFLAG_CIPHER_SERVER_PREF) == 0)
		tls_config_prefer_ciphers_client(tls_cfg);

	/*
	 * Set session ID context to a random value. It needs to be the
	 * same across all relay processes or session caching will fail.
	 */
	if (tls_config_set_session_id(tls_cfg, env->sc_conf.tls_sid,
	    sizeof(env->sc_conf.tls_sid)) == -1) {
		log_warnx("could not set the TLS session ID: %s",
		    tls_config_error(tls_cfg));
		return (-1);
	}

	/* Set callback for TLS session tickets if enabled */
	if (proto->tickets == 1) {
		/* set timeout to the ticket rekey time */
		tls_config_set_session_lifetime(tls_cfg, TLS_SESSION_LIFETIME);

		tls_config_add_ticket_key(tls_cfg,
		    env->sc_ticket.tt_keyrev, env->sc_ticket.tt_key,
		    sizeof(env->sc_ticket.tt_key));
	}

	if (tls_config_set_ecdhecurves(tls_cfg, proto->tlsecdhecurves) != 0) {
		log_warnx("failed to set ecdhe curves %s: %s",
		    proto->tlsecdhecurves, tls_config_error(tls_cfg));
		return (-1);
	}

	if (tls_config_set_dheparams(tls_cfg, proto->tlsdhparams) != 0) {
		log_warnx("failed to set dh params %s: %s",
		    proto->tlsdhparams, tls_config_error(tls_cfg));
		return (-1);
	}

	return (0);
}

/*
 * This function is not publicy exported because it is a hack until libtls
 * has a proper privsep setup
 */
void tls_config_use_fake_private_key(struct tls_config *config);

int
relay_tls_ctx_create(struct relay *rlay)
{
	struct tls_config	*tls_cfg, *tls_client_cfg;
	struct tls		*tls = NULL;
	struct relay_cert	*cert;
	int			 keyfound = 0;
	char			*buf = NULL, *cabuf = NULL, *ocspbuf = NULL;
	off_t			 len = 0, calen = 0, ocsplen = 0;

	if ((tls_cfg = tls_config_new()) == NULL) {
		log_warnx("unable to allocate TLS config");
		return (-1);
	}
	if ((tls_client_cfg = tls_config_new()) == NULL) {
		log_warnx("unable to allocate TLS config");
		return (-1);
	}

	if (relay_tls_ctx_create_proto(rlay->rl_proto, tls_cfg) == -1)
		goto err;
	if (relay_tls_ctx_create_proto(rlay->rl_proto, tls_client_cfg) == -1)
		goto err;

	/* Verify the server certificate if we have a CA chain */
	if (rlay->rl_conf.flags & F_TLSCLIENT) {
		/*
		 * Currently relayd can't verify the name of certs and changing
		 * this is non trivial. For now just disable name verification.
		 */
		tls_config_insecure_noverifyname(tls_client_cfg);

		if (rlay->rl_tls_ca_fd != -1) {
			if ((buf = relay_load_fd(rlay->rl_tls_ca_fd, &len)) == NULL) {
				log_warn("failed to read root certificates");
				goto err;
			}
			rlay->rl_tls_ca_fd = -1;

			if (tls_config_set_ca_mem(tls_client_cfg, buf, len) !=
			    0) {
				log_warnx("failed to set root certificates: %s",
				    tls_config_error(tls_client_cfg));
				goto err;
			}
			purge_key(&buf, len);
		} else {
			/* No root cert available so disable the checking */
			tls_config_insecure_noverifycert(tls_client_cfg);
		}

		rlay->rl_tls_client_cfg = tls_client_cfg;
	}

	if (rlay->rl_conf.flags & F_TLS) {
		log_debug("%s: loading certificate", __func__);
		/*
		 * Use the public key as the "private" key - the secret key
		 * parameters are hidden in an extra process that will be
		 * contacted by the RSA engine.  The TLS library needs at
		 * least the public key parameters in the current process.
		 */
		tls_config_use_fake_private_key(tls_cfg);

		TAILQ_FOREACH(cert, env->sc_certs, cert_entry) {
			if (cert->cert_relayid != rlay->rl_conf.id ||
			    cert->cert_fd == -1)
				continue;
			keyfound++;

			if ((buf = relay_load_fd(cert->cert_fd,
			    &len)) == NULL) {
				log_warn("failed to load tls certificate");
				goto err;
			}
			cert->cert_fd = -1;

			if (cert->cert_ocsp_fd != -1 &&
			    (ocspbuf = relay_load_fd(cert->cert_ocsp_fd,
			    &ocsplen)) == NULL) {
				log_warn("failed to load OCSP staplefile");
				goto err;
			}
			if (ocsplen == 0)
				purge_key(&ocspbuf, ocsplen);
			cert->cert_ocsp_fd = -1;

			if (keyfound == 1 &&
			    tls_config_set_keypair_ocsp_mem(tls_cfg, buf, len,
			    NULL, 0, ocspbuf, ocsplen) != 0) {
				log_warnx("failed to set tls certificate: %s",
				    tls_config_error(tls_cfg));
				goto err;
			}

			/* loading certificate public key */
			if (keyfound == 1 &&
			    !ssl_load_pkey(buf, len, NULL, &rlay->rl_tls_pkey))
				goto err;

			if (tls_config_add_keypair_ocsp_mem(tls_cfg, buf, len,
			    NULL, 0, ocspbuf, ocsplen) != 0) {
				log_warnx("failed to add tls certificate: %s",
				    tls_config_error(tls_cfg));
				goto err;
			}

			purge_key(&buf, len);
			purge_key(&ocspbuf, ocsplen);
		}

		if (rlay->rl_tls_cacert_fd != -1) {
			if ((cabuf = relay_load_fd(rlay->rl_tls_cacert_fd,
			    &calen)) == NULL) {
				log_warn("failed to load tls CA certificate");
				goto err;
			}
			log_debug("%s: loading CA certificate", __func__);
			if (!ssl_load_pkey(cabuf, calen,
			    &rlay->rl_tls_cacertx509, &rlay->rl_tls_capkey))
				goto err;
		}
		rlay->rl_tls_cacert_fd = -1;

		if (rlay->rl_tls_client_ca_fd != -1) {
			if ((buf = relay_load_fd(rlay->rl_tls_client_ca_fd,
			    &len)) == NULL) {
				log_warn(
				    "failed to read tls client CA certificate");
				goto err;
			}

			if (tls_config_set_ca_mem(tls_cfg, buf, len) != 0) {
				log_warnx(
				    "failed to set tls client CA cert: %s",
				    tls_config_error(tls_cfg));
				goto err;
			}
			purge_key(&buf, len);

			tls_config_verify_client(tls_cfg);
		}
		rlay->rl_tls_client_ca_fd = -1;

		tls = tls_server();
		if (tls == NULL) {
			log_warnx("unable to allocate TLS context");
			goto err;
		}
		if (tls_configure(tls, tls_cfg) == -1) {
			log_warnx("could not configure the TLS context: %s",
			    tls_error(tls));
			tls_free(tls);
			goto err;
		}
		rlay->rl_tls_cfg = tls_cfg;
		rlay->rl_tls_ctx = tls;

		purge_key(&cabuf, calen);
	}

	if (rlay->rl_tls_client_cfg == NULL)
		tls_config_free(tls_client_cfg);
	if (rlay->rl_tls_cfg == NULL)
		tls_config_free(tls_cfg);

	return (0);
 err:
	purge_key(&ocspbuf, ocsplen);
	purge_key(&cabuf, calen);
	purge_key(&buf, len);

	tls_config_free(tls_client_cfg);
	tls_config_free(tls_cfg);
	return (-1);
}

static struct tls *
relay_tls_inspect_create(struct relay *rlay, struct ctl_relay_event *cre)
{
	struct tls_config	*tls_cfg;
	struct tls		*tls = NULL;

	/* TLS inspection: use session-specific certificate */
	if ((tls_cfg = tls_config_new()) == NULL) {
		log_warnx("unable to allocate TLS config");
		goto err;
	}
	if (relay_tls_ctx_create_proto(rlay->rl_proto, tls_cfg) == -1) {
		/* error already printed */
		goto err;
	}

	tls_config_use_fake_private_key(tls_cfg);

	if (tls_config_set_keypair_ocsp_mem(tls_cfg,
	    cre->tlscert, cre->tlscert_len, NULL, 0, NULL, 0) != 0) {
		log_warnx("failed to set tls certificate: %s",
		    tls_config_error(tls_cfg));
		goto err;
	}

	tls = tls_server();
	if (tls == NULL) {
		log_warnx("unable to allocate TLS context");
		goto err;
	}
	if (tls_configure(tls, tls_cfg) == -1) {
		log_warnx("could not configure the TLS context: %s",
		    tls_error(tls));
		tls_free(tls);
		goto err;
	}

	cre->tls_cfg = tls_cfg;
	cre->tls_ctx = tls;
	return (tls);
 err:
	tls_config_free(tls_cfg);
	return (NULL);
}

void
relay_tls_transaction(struct rsession *con, struct ctl_relay_event *cre)
{
	struct relay		*rlay = con->se_relay;
	struct tls		*tls_server;
	const char		*errstr;
	u_int			 flag;

	if (cre->dir == RELAY_DIR_REQUEST) {
		if (cre->tlscert != NULL)
			tls_server = relay_tls_inspect_create(rlay, cre);
		else
			tls_server = rlay->rl_tls_ctx;
		if (tls_server == NULL) {
			errstr = "no TLS server context available";
			goto err;
		}

		if (tls_accept_socket(tls_server, &cre->tls, cre->s) == -1) {
			errstr = "could not accept the TLS connection";
			goto err;
		}
		flag = EV_READ;
	} else {
		cre->tls = tls_client();
		if (cre->tls == NULL ||
		    tls_configure(cre->tls, rlay->rl_tls_client_cfg) == -1) {
			errstr = "could not configure the TLS client context";
			goto err;
		}
		if (tls_connect_socket(cre->tls, cre->s, NULL) == -1) {
			errstr = "could not connect the TLS connection";
			goto err;
		}
		flag = EV_WRITE;
	}

	log_debug("%s: session %d: scheduling on %s", __func__, con->se_id,
	    (flag == EV_READ) ? "EV_READ" : "EV_WRITE");
	event_again(&con->se_ev, cre->s, EV_TIMEOUT|flag, relay_tls_handshake,
	    &con->se_tv_start, &rlay->rl_conf.timeout, cre);
	return;

 err:
	relay_close(con, errstr, 1);
}

void
relay_tls_handshake(int fd, short event, void *arg)
{
	struct ctl_relay_event	*cre = arg;
	struct rsession		*con = cre->con;
	struct relay		*rlay = con->se_relay;
	int			 retry_flag = 0;
	int			 ret;
	char			*msg;

	if (event == EV_TIMEOUT) {
		relay_close(con, "TLS handshake timeout", 1);
		return;
	}

	ret = tls_handshake(cre->tls);
	if (ret == 0) {
#ifdef DEBUG
		log_info(
#else
		log_debug(
#endif
		    "relay %s, tls session %d %s (%d active)",
		    rlay->rl_conf.name, con->se_id,
		    cre->dir == RELAY_DIR_REQUEST ? "established" : "connected",
		    relay_sessions);

		if (cre->dir == RELAY_DIR_REQUEST) {
			relay_session(con);
			return;
		}

		if (rlay->rl_conf.flags & F_TLSINSPECT) {
			const uint8_t	*servercert;
			size_t		 len;

			servercert = tls_peer_cert_chain_pem(con->se_out.tls,
			    &len);
			if (servercert != NULL) {
				con->se_in.tlscert = ssl_update_certificate(
				    servercert, len,
				    rlay->rl_tls_pkey, rlay->rl_tls_capkey,
				    rlay->rl_tls_cacertx509,
				    &con->se_in.tlscert_len);
			} else
				con->se_in.tlscert = NULL;
			if (con->se_in.tlscert == NULL)
				relay_close(con,
				    "could not create certificate", 1);
			else
				relay_session(con);
			return;
		}
		relay_connected(fd, EV_WRITE, con);
		return;
	} else if (ret == TLS_WANT_POLLIN) {
		retry_flag = EV_READ;
	} else if (ret == TLS_WANT_POLLOUT) {
		retry_flag = EV_WRITE;
	} else {
		if (asprintf(&msg, "TLS handshake error: %s",
		    tls_error(cre->tls)) >= 0) {
			relay_close(con, msg, 1);
			free(msg);
		} else {
			relay_close(con, "TLS handshake error", 1);
		}
		return;
	}

	DPRINTF("%s: session %d: scheduling on %s", __func__, con->se_id,
	    (retry_flag == EV_READ) ? "EV_READ" : "EV_WRITE");
	event_again(&con->se_ev, fd, EV_TIMEOUT|retry_flag, relay_tls_handshake,
	    &con->se_tv_start, &rlay->rl_conf.timeout, cre);
}

void
relay_tls_connected(struct ctl_relay_event *cre)
{
	/*
	 * Hack libevent - we overwrite the internal bufferevent I/O
	 * functions to handle the TLS abstraction.
	 */
	event_del(&cre->bev->ev_read);
	event_del(&cre->bev->ev_write);

	event_set(&cre->bev->ev_read, cre->s, EV_READ,
	    relay_tls_readcb, cre->bev);
	event_set(&cre->bev->ev_write, cre->s, EV_WRITE,
	    relay_tls_writecb, cre->bev);
}

void
relay_tls_readcb(int fd, short event, void *arg)
{
	char			 rbuf[IBUF_READ_SIZE];
	struct bufferevent	*bufev = arg;
	struct ctl_relay_event	*cre = bufev->cbarg;
	short			 what = EVBUFFER_READ;
	int			 howmuch = IBUF_READ_SIZE;
	ssize_t			 ret;
	size_t			 len;

	if (event == EV_TIMEOUT) {
		what |= EVBUFFER_TIMEOUT;
		goto err;
	}

	if (bufev->wm_read.high != 0)
		howmuch = MINIMUM(sizeof(rbuf), bufev->wm_read.high);

	ret = tls_read(cre->tls, rbuf, howmuch);
	if (ret == TLS_WANT_POLLIN || ret == TLS_WANT_POLLOUT) {
		goto retry;
	} else if (ret == -1) {
		what |= EVBUFFER_ERROR;
		goto err;
	}
	len = ret;

	if (len == 0) {
		what |= EVBUFFER_EOF;
		goto err;
	}

	if (evbuffer_add(bufev->input, rbuf, ret) == -1) {
		what |= EVBUFFER_ERROR;
		goto err;
	}

	relay_bufferevent_add(&bufev->ev_read, bufev->timeout_read);

	len = EVBUFFER_LENGTH(bufev->input);
	if (bufev->wm_read.low != 0 && len < bufev->wm_read.low)
		return;
	if (bufev->wm_read.high != 0 && len > bufev->wm_read.high) {
		struct evbuffer *buf = bufev->input;
		event_del(&bufev->ev_read);
		evbuffer_setcb(buf, bufferevent_read_pressure_cb, bufev);
		return;
	}

	if (bufev->readcb != NULL)
		(*bufev->readcb)(bufev, bufev->cbarg);
	return;

 retry:
	relay_bufferevent_add(&bufev->ev_read, bufev->timeout_read);
	return;

 err:
	(*bufev->errorcb)(bufev, what, bufev->cbarg);
}

void
relay_tls_writecb(int fd, short event, void *arg)
{
	struct bufferevent	*bufev = arg;
	struct ctl_relay_event	*cre = bufev->cbarg;
	ssize_t			 ret;
	size_t			 len;
	short			 what = EVBUFFER_WRITE;

	if (event == EV_TIMEOUT) {
		what |= EVBUFFER_TIMEOUT;
		goto err;
	}

	if (EVBUFFER_LENGTH(bufev->output)) {
		ret = tls_write(cre->tls, EVBUFFER_DATA(bufev->output),
		    EVBUFFER_LENGTH(bufev->output));
		if (ret == TLS_WANT_POLLIN || ret == TLS_WANT_POLLOUT) {
			goto retry;
		} else if (ret == -1) {
			what |= EVBUFFER_ERROR;
			goto err;
		}
		len = ret;
		evbuffer_drain(bufev->output, len);
	}

	if (EVBUFFER_LENGTH(bufev->output) != 0)
		relay_bufferevent_add(&bufev->ev_write, bufev->timeout_write);

	if (bufev->writecb != NULL &&
	    EVBUFFER_LENGTH(bufev->output) <= bufev->wm_write.low)
		(*bufev->writecb)(bufev, bufev->cbarg);
	return;

 retry:
	relay_bufferevent_add(&bufev->ev_write, bufev->timeout_write);
	return;

 err:
	(*bufev->errorcb)(bufev, what, bufev->cbarg);
}

int
relay_bufferevent_add(struct event *ev, int timeout)
{
	struct timeval tv, *ptv = NULL;

	if (timeout) {
		timerclear(&tv);
		tv.tv_sec = timeout;
		ptv = &tv;
	}

	return (event_add(ev, ptv));
}

#ifdef notyet
int
relay_bufferevent_printf(struct ctl_relay_event *cre, const char *fmt, ...)
{
	int	ret;
	va_list	ap;

	va_start(ap, fmt);
	ret = evbuffer_add_vprintf(cre->output, fmt, ap);
	va_end(ap);

	if (cre->bev != NULL &&
	    ret != -1 && EVBUFFER_LENGTH(cre->output) > 0 &&
	    (cre->bev->enabled & EV_WRITE))
		bufferevent_enable(cre->bev, EV_WRITE);

	return (ret);
}
#endif

int
relay_bufferevent_print(struct ctl_relay_event *cre, const char *str)
{
	if (cre->bev == NULL)
		return (evbuffer_add(cre->output, str, strlen(str)));
	return (bufferevent_write(cre->bev, str, strlen(str)));
}

int
relay_bufferevent_write_buffer(struct ctl_relay_event *cre,
    struct evbuffer *buf)
{
	if (cre->bev == NULL)
		return (evbuffer_add_buffer(cre->output, buf));
	return (bufferevent_write_buffer(cre->bev, buf));
}

int
relay_bufferevent_write_chunk(struct ctl_relay_event *cre,
    struct evbuffer *buf, size_t size)
{
	int ret;
	ret = relay_bufferevent_write(cre, EVBUFFER_DATA(buf), size);
	if (ret != -1)
		evbuffer_drain(buf, size);
	return (ret);
}

int
relay_bufferevent_write(struct ctl_relay_event *cre, void *data, size_t size)
{
	if (cre->bev == NULL)
		return (evbuffer_add(cre->output, data, size));
	return (bufferevent_write(cre->bev, data, size));
}

int
relay_cmp_af(struct sockaddr_storage *a, struct sockaddr_storage *b)
{
	int			ret = -1;
	struct sockaddr_in	ia, ib;
	struct sockaddr_in6	ia6, ib6;

	switch (a->ss_family) {
	case AF_INET:
		bcopy(a, &ia, sizeof(struct sockaddr_in));
		bcopy(b, &ib, sizeof(struct sockaddr_in));

		ret = memcmp(&ia.sin_addr, &ib.sin_addr,
		    sizeof(ia.sin_addr));
		if (ret == 0)
			ret = memcmp(&ia.sin_port, &ib.sin_port,
			    sizeof(ia.sin_port));
		break;
	case AF_INET6:
		bcopy(a, &ia6, sizeof(struct sockaddr_in6));
		bcopy(b, &ib6, sizeof(struct sockaddr_in6));

		ret = memcmp(&ia6.sin6_addr, &ib6.sin6_addr,
		    sizeof(ia6.sin6_addr));
		if (ret == 0)
			ret = memcmp(&ia6.sin6_port, &ib6.sin6_port,
			    sizeof(ia6.sin6_port));
		break;
	default:
		break;
	}

	return (ret);
}

int
relay_session_cmp(struct rsession *a, struct rsession *b)
{
	struct relay	*rlay = b->se_relay;
	struct protocol	*proto = rlay->rl_proto;

	if (proto != NULL && proto->cmp != NULL)
		return ((*proto->cmp)(a, b));

	return ((int)a->se_id - b->se_id);
}

void
relay_log(struct rsession *con, char *msg)
{
	if (con->se_haslog && con->se_log != NULL) {
		evbuffer_add(con->se_log, msg, strlen(msg));
	}
}

SPLAY_GENERATE(session_tree, rsession, se_nodes, relay_session_cmp);
