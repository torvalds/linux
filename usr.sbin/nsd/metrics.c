/*
 * metrics.c -- prometheus metrics endpoint
 *
 * Copyright (c) 2001-2025, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"

#ifdef USE_METRICS

#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <event2/event.h>
#include <event2/http.h>

#include "nsd.h"
#include "xfrd.h"
#include "options.h"
#include "remote.h"
#include "metrics.h"

/** if you want zero to be inhibited in stats output.
 * it omits zeroes for types that have no acronym and unused-rcodes */
const int metrics_inhibit_zero = 1;

/**
 * list of connection accepting file descriptors
 */
struct metrics_acceptlist {
	struct metrics_acceptlist* next;
	int accept_fd;
	char* ident;
	struct daemon_metrics* metrics;
};

/**
 * The metrics daemon state.
 */
struct daemon_metrics {
	/** the master process for this metrics daemon */
	struct xfrd_state* xfrd;
	/** commpoints for accepting HTTP connections */
	struct metrics_acceptlist* accept_list;
	/** last time stats was reported */
	struct timeval stats_time, boot_time;
	/** libevent http server */
	struct evhttp *http_server;
};

static void
metrics_http_callback(struct evhttp_request *req, void *p);

struct daemon_metrics*
daemon_metrics_create(struct nsd_options* cfg)
{
	struct daemon_metrics* metrics = (struct daemon_metrics*)xalloc_zero(
		sizeof(*metrics));
	assert(cfg->metrics_enable);

	/* and try to open the ports */
	if(!daemon_metrics_open_ports(metrics, cfg)) {
		log_msg(LOG_ERR, "could not open metrics port");
		daemon_metrics_delete(metrics);
		return NULL;
	}

	if(gettimeofday(&metrics->boot_time, NULL) == -1)
		log_msg(LOG_ERR, "gettimeofday: %s", strerror(errno));
	metrics->stats_time = metrics->boot_time;

	return metrics;
}

void daemon_metrics_close(struct daemon_metrics* metrics)
{
	struct metrics_acceptlist *h, *nh;
	if(!metrics) return;

	/* close listen sockets */
	h = metrics->accept_list;
	while(h) {
		nh = h->next;
		close(h->accept_fd);
		free(h->ident);
		free(h);
		h = nh;
	}
	metrics->accept_list = NULL;

	if (metrics->http_server) {
		evhttp_free(metrics->http_server);
	}
}

void daemon_metrics_delete(struct daemon_metrics* metrics)
{
	if(!metrics) return;
	daemon_metrics_close(metrics);
	free(metrics);
}

static int
create_tcp_accept_sock(struct addrinfo* addr, int* noproto)
{
#if defined(SO_REUSEADDR) || (defined(INET6) && (defined(IPV6_V6ONLY) || defined(IPV6_USE_MIN_MTU) || defined(IPV6_MTU)))
	int on = 1;
#endif
	int s;
	*noproto = 0;
	if ((s = socket(addr->ai_family, addr->ai_socktype, 0)) == -1) {
#if defined(INET6)
		if (addr->ai_family == AF_INET6 &&
			errno == EAFNOSUPPORT) {
			*noproto = 1;
			log_msg(LOG_WARNING, "fallback to TCP4, no IPv6: not supported");
			return -1;
		}
#endif /* INET6 */
		log_msg(LOG_ERR, "can't create a socket: %s", strerror(errno));
		return -1;
	}
#ifdef  SO_REUSEADDR
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
		log_msg(LOG_ERR, "setsockopt(..., SO_REUSEADDR, ...) failed: %s", strerror(errno));
	}
#endif /* SO_REUSEADDR */
#if defined(INET6) && defined(IPV6_V6ONLY)
	if (addr->ai_family == AF_INET6 &&
		setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on)) < 0)
	{
		log_msg(LOG_ERR, "setsockopt(..., IPV6_V6ONLY, ...) failed: %s", strerror(errno));
		close(s);
		return -1;
	}
#endif
	/* set it nonblocking */
	/* (StevensUNP p463), if tcp listening socket is blocking, then
	   it may block in accept, even if select() says readable. */
	if (fcntl(s, F_SETFL, O_NONBLOCK) == -1) {
		log_msg(LOG_ERR, "cannot fcntl tcp: %s", strerror(errno));
	}
	/* Bind it... */
	if (bind(s, (struct sockaddr *)addr->ai_addr, addr->ai_addrlen) != 0) {
		log_msg(LOG_ERR, "can't bind tcp socket: %s", strerror(errno));
		close(s);
		return -1;
	}
	/* Listen to it... */
	if (listen(s, TCP_BACKLOG_METRICS) == -1) {
		log_msg(LOG_ERR, "can't listen: %s", strerror(errno));
		close(s);
		return -1;
	}
	return s;
}

/**
 * Add and open a new metrics port
 * @param metrics: metrics with result list.
 * @param ip: ip str
 * @param nr: port nr
 * @param noproto_is_err: if lack of protocol support is an error.
 * @return false on failure.
 */
static int
metrics_add_open(struct daemon_metrics* metrics, struct nsd_options* cfg, const char* ip,
	int nr, int noproto_is_err)
{
	struct addrinfo hints;
	struct addrinfo* res;
	struct metrics_acceptlist* hl;
	int noproto = 0;
	int fd, r;
	char port[15];
	snprintf(port, sizeof(port), "%d", nr);
	port[sizeof(port)-1]=0;
	memset(&hints, 0, sizeof(hints));
	assert(ip);

	if(ip[0] == '/') {
		/* This looks like a local socket */
		fd = create_local_accept_sock(ip, &noproto);
		/*
		 * Change socket ownership and permissions so users other
		 * than root can access it provided they are in the same
		 * group as the user we run as.
		 */
		if(fd != -1) {
#ifdef HAVE_CHOWN
			if(chmod(ip, (mode_t)(S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) == -1) {
				VERBOSITY(3, (LOG_INFO, "cannot chmod metrics socket %s: %s", ip, strerror(errno)));
			}
			if (cfg->username && cfg->username[0] &&
				nsd.uid != (uid_t)-1) {
				if(chown(ip, nsd.uid, nsd.gid) == -1)
					VERBOSITY(2, (LOG_INFO, "cannot chown %u.%u %s: %s",
					  (unsigned)nsd.uid, (unsigned)nsd.gid,
					  ip, strerror(errno)));
			}
#else
			(void)cfg;
#endif
		}
	} else {
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;
		/* if we had no interface ip name, "default" is what we
		 * would do getaddrinfo for. */
		if((r = getaddrinfo(ip, port, &hints, &res)) != 0 || !res) {
			log_msg(LOG_ERR, "metrics interface %s:%s getaddrinfo: %s %s",
				ip, port, gai_strerror(r),
#ifdef EAI_SYSTEM
				r==EAI_SYSTEM?(char*)strerror(errno):""
#else
				""
#endif
				);
			return 0;
		}

		/* open fd */
		fd = create_tcp_accept_sock(res, &noproto);
		freeaddrinfo(res);
	}

	if(fd == -1 && noproto) {
		if(!noproto_is_err)
			return 1; /* return success, but do nothing */
		log_msg(LOG_ERR, "cannot open metrics interface %s %d : "
			"protocol not supported", ip, nr);
		return 0;
	}
	if(fd == -1) {
		log_msg(LOG_ERR, "cannot open metrics interface %s %d", ip, nr);
		return 0;
	}

	/* alloc */
	hl = (struct metrics_acceptlist*)xalloc_zero(sizeof(*hl));
	hl->metrics = metrics;
	hl->ident = strdup(ip);
	if(!hl->ident) {
		log_msg(LOG_ERR, "malloc failure");
		close(fd);
		free(hl);
		return 0;
	}
	hl->next = metrics->accept_list;
	metrics->accept_list = hl;

	hl->accept_fd = fd;
	return 1;
}

int
daemon_metrics_open_ports(struct daemon_metrics* metrics, struct nsd_options* cfg)
{
	assert(cfg->metrics_enable && cfg->metrics_port);
	if(cfg->metrics_interface) {
		ip_address_option_type* p;
		for(p = cfg->metrics_interface; p; p = p->next) {
			if(!metrics_add_open(metrics, cfg, p->address, cfg->metrics_port, 1)) {
				return 0;
			}
		}
	} else {
		/* defaults */
		if(cfg->do_ip6 && !metrics_add_open(metrics, cfg, "::1", cfg->metrics_port, 0)) {
			return 0;
		}
		if(cfg->do_ip4 &&
			!metrics_add_open(metrics, cfg, "127.0.0.1", cfg->metrics_port, 1)) {
			return 0;
		}
	}
	return 1;
}

void
daemon_metrics_attach(struct daemon_metrics* metrics, struct xfrd_state* xfrd)
{
	int fd;
	struct metrics_acceptlist* p;
	if(!metrics) return;
	metrics->xfrd = xfrd;

	metrics->http_server = evhttp_new(xfrd->event_base);
	for(p = metrics->accept_list; p; p = p->next) {
		fd = p->accept_fd;
		if (evhttp_accept_socket(metrics->http_server, fd)) {
			log_msg(LOG_ERR, "metrics: cannot set http server to accept socket");
		}

		/* only handle requests to metrics_path, anything else returns 404 */
		evhttp_set_cb(metrics->http_server,
                      metrics->xfrd->nsd->options->metrics_path,
                      metrics_http_callback, p);
		/* evhttp_set_gencb(metrics->http_server, metrics_http_callback_generic, p); */
	}
}

/* Callback for handling the active http request to the specific URI */
static void
metrics_http_callback(struct evhttp_request *req, void *p)
{
	struct evbuffer *reply = NULL;
	struct daemon_metrics *metrics = ((struct metrics_acceptlist *)p)->metrics;

	/* currently only GET requests are supported/allowed */
	enum evhttp_cmd_type cmd = evhttp_request_get_command(req);
	if (cmd != EVHTTP_REQ_GET /* && cmd != EVHTTP_REQ_HEAD */) {
		evhttp_send_error(req, HTTP_BADMETHOD, 0);
		return;
	}

	reply = evbuffer_new();

	if (!reply) {
		evhttp_send_error(req, HTTP_INTERNAL, 0);
		log_msg(LOG_ERR, "failed to allocate reply buffer\n");
		return;
	}

	evhttp_add_header(evhttp_request_get_output_headers(req),
	                  "Content-Type", "text/plain; version=0.0.4");
#ifdef BIND8_STATS
	process_stats(NULL, reply, metrics->xfrd, 1);
	evhttp_send_reply(req, HTTP_OK, NULL, reply);
	VERBOSITY(3, (LOG_INFO, "metrics operation completed, response sent"));
#else
	evhttp_send_reply(req, HTTP_NOCONTENT, "No Content - Statistics disabled", reply);
	log_msg(LOG_NOTICE, "metrics requested, but no stats enabled at compile time\n");
	(void)metrics;
#endif /* BIND8_STATS */

	evbuffer_free(reply);
}

#ifdef BIND8_STATS
/** print long number*/
static int
print_longnum(struct evbuffer *buf, char* desc, uint64_t x)
{
	if(x > (uint64_t)1024*1024*1024) {
		/*more than a Gb*/
		size_t front = (size_t)(x / (uint64_t)1000000);
		size_t back = (size_t)(x % (uint64_t)1000000);
		return evbuffer_add_printf(buf, "%s%lu%6.6lu\n", desc,
			(unsigned long)front, (unsigned long)back);
	} else {
		return evbuffer_add_printf(buf, "%s%lu\n", desc, (unsigned long)x);
	}
}

static void
print_metric_help_and_type(struct evbuffer *buf, char *prefix, char *name,
						   char *help, char *type)
{
	evbuffer_add_printf(buf, "# HELP %s%s %s\n# TYPE %s%s %s\n",
	                    prefix, name, help, prefix, name, type);
}

static void
print_stat_block(struct evbuffer *buf, struct nsdst* st,
                  char *name)
{
	size_t i;

	const char* rcstr[] = {"NOERROR", "FORMERR", "SERVFAIL", "NXDOMAIN",
		"NOTIMP", "REFUSED", "YXDOMAIN", "YXRRSET", "NXRRSET", "NOTAUTH",
		"NOTZONE", "RCODE11", "RCODE12", "RCODE13", "RCODE14", "RCODE15",
		"BADVERS"
	};

	char prefix[512] = {0};
	if (name) {
		snprintf(prefix, sizeof(prefix), "nsd_zonestats_%s_", name);
	} else {
		snprintf(prefix, sizeof(prefix), "nsd_");
	}

	/* nsd_queries_by_type_total */
	print_metric_help_and_type(buf, prefix, "queries_by_type_total",
	                           "Total number of queries received by type.",
	                           "counter");
	for(i=0; i<= 255; i++) {
		if(metrics_inhibit_zero && st->qtype[i] == 0 &&
			strncmp(rrtype_to_string(i), "TYPE", 4) == 0)
			continue;
		evbuffer_add_printf(buf, "%squeries_by_type_total{type=\"%s\"} %lu\n",
			prefix, rrtype_to_string(i), (unsigned long)st->qtype[i]);
	}

	/* nsd_queries_by_class_total */
	print_metric_help_and_type(buf, prefix, "queries_by_class_total",
	                           "Total number of queries received by class.",
	                           "counter");
	for(i=0; i<4; i++) {
		if(metrics_inhibit_zero && st->qclass[i] == 0 && i != CLASS_IN)
			continue;
		evbuffer_add_printf(buf, "%squeries_by_class_total{class=\"%s\"} %lu\n",
			prefix, rrclass_to_string(i), (unsigned long)st->qclass[i]);
	}

	/* nsd_queries_by_opcode_total */
	print_metric_help_and_type(buf, prefix, "queries_by_opcode_total",
	                           "Total number of queries received by opcode.",
	                           "counter");
	for(i=0; i<6; i++) {
		if(metrics_inhibit_zero && st->opcode[i] == 0 && i != OPCODE_QUERY)
			continue;
		evbuffer_add_printf(buf, "%squeries_by_opcode_total{opcode=\"%s\"} %lu\n",
			prefix, opcode2str(i), (unsigned long)st->opcode[i]);
	}

	/* nsd_queries_by_rcode_total */
	print_metric_help_and_type(buf, prefix, "queries_by_rcode_total",
	                           "Total number of queries received by rcode.",
	                           "counter");
	for(i=0; i<17; i++) {
		if(metrics_inhibit_zero && st->rcode[i] == 0 &&
			i > RCODE_YXDOMAIN) /*NSD does not use larger*/
			continue;
		evbuffer_add_printf(buf, "%squeries_by_rcode_total{rcode=\"%s\"} %lu\n",
			prefix, rcstr[i], (unsigned long)st->rcode[i]);
	}

	/* nsd_queries_by_transport_total */
	print_metric_help_and_type(buf, prefix, "queries_by_transport_total",
		"Total number of queries received by transport.",
		"counter");
	evbuffer_add_printf(buf, "%squeries_by_transport_total{transport=\"udp\"} %lu\n", prefix, (unsigned long)st->qudp);
	evbuffer_add_printf(buf, "%squeries_by_transport_total{transport=\"udp6\"} %lu\n", prefix, (unsigned long)st->qudp6);

	/* nsd_queries_with_edns_total */
	print_metric_help_and_type(buf, prefix, "queries_with_edns_total",
		"Total number of queries received with EDNS OPT.",
		"counter");
	evbuffer_add_printf(buf, "%squeries_with_edns_total %lu\n", prefix, (unsigned long)st->edns);

	/* nsd_queries_with_edns_failed_total */
	print_metric_help_and_type(buf, prefix, "queries_with_edns_failed_total",
		"Total number of queries received with EDNS OPT where EDNS parsing failed.",
		"counter");
	evbuffer_add_printf(buf, "%squeries_with_edns_failed_total %lu\n", prefix, (unsigned long)st->ednserr);

	/* nsd_connections_total */
	print_metric_help_and_type(buf, prefix, "connections_total",
		"Total number of connections.",
		"counter");
	evbuffer_add_printf(buf, "%sconnections_total{transport=\"tcp\"} %lu\n", prefix, (unsigned long)st->ctcp);
	evbuffer_add_printf(buf, "%sconnections_total{transport=\"tcp6\"} %lu\n", prefix, (unsigned long)st->ctcp6);
	evbuffer_add_printf(buf, "%sconnections_total{transport=\"tls\"} %lu\n", prefix, (unsigned long)st->ctls);
	evbuffer_add_printf(buf, "%sconnections_total{transport=\"tls6\"} %lu\n", prefix, (unsigned long)st->ctls6);

	/* nsd_xfr_requests_served_total */
	print_metric_help_and_type(buf, prefix, "xfr_requests_served_total",
		"Total number of answered zone transfers.",
		"counter");
	evbuffer_add_printf(buf, "%sxfr_requests_served_total{xfrtype=\"AXFR\"} %lu\n", prefix, (unsigned long)st->raxfr);
	evbuffer_add_printf(buf, "%sxfr_requests_served_total{xfrtype=\"IXFR\"} %lu\n", prefix, (unsigned long)st->rixfr);

	/* nsd_queries_dropped_total */
	print_metric_help_and_type(buf, prefix, "queries_dropped_total",
		"Total number of dropped queries.",
		"counter");
	evbuffer_add_printf(buf, "%squeries_dropped_total %lu\n", prefix, (unsigned long)st->dropped);

	/* nsd_queries_rx_failed_total */
	print_metric_help_and_type(buf, prefix, "queries_rx_failed_total",
		"Total number of queries where receive failed.",
		"counter");
	evbuffer_add_printf(buf, "%squeries_rx_failed_total %lu\n", prefix, (unsigned long)st->rxerr);

	/* nsd_answers_tx_failed_total */
	print_metric_help_and_type(buf, prefix, "answers_tx_failed_total",
		"Total number of answers where transmit failed.",
		"counter");
	evbuffer_add_printf(buf, "%sanswers_tx_failed_total %lu\n", prefix, (unsigned long)st->txerr);

	/* nsd_answers_without_aa_total */
	print_metric_help_and_type(buf, prefix, "answers_without_aa_total",
		"Total number of NOERROR answers without AA flag set.",
		"counter");
	evbuffer_add_printf(buf, "%sanswers_without_aa_total %lu\n", prefix, (unsigned long)st->nona);

	/* nsd_answers_truncated_total */
	print_metric_help_and_type(buf, prefix, "answers_truncated_total",
		"Total number of truncated answers.",
		"counter");
	evbuffer_add_printf(buf, "%sanswers_truncated_total %lu\n", prefix, (unsigned long)st->truncated);
}

#ifdef USE_ZONE_STATS
void
metrics_zonestat_print_one(struct evbuffer *buf, char *name,
                           struct nsdst *zst)
{
	char prefix[512] = {0};
	snprintf(prefix, sizeof(prefix), "nsd_zonestats_%s_", name);

	print_metric_help_and_type(buf, prefix, "queries_total",
		"Total number of queries received.", "counter");
	evbuffer_add_printf(buf, "nsd_zonestats_%s_queries_total %lu\n", name,
		(unsigned long)(zst->qudp + zst->qudp6 + zst->ctcp +
			zst->ctcp6 + zst->ctls + zst->ctls6));
	print_stat_block(buf, zst, name);
}
#endif /*USE_ZONE_STATS*/

void
metrics_print_stats(struct evbuffer *buf, xfrd_state_type *xfrd,
                    struct timeval *now, int clear, struct nsdst *st,
                    struct nsdst **zonestats, struct timeval *rc_stats_time)
{
	size_t i;
	struct timeval elapsed, uptime;

	/* nsd_queries_total */
	print_metric_help_and_type(buf, "nsd_", "queries_total",
	                           "Total number of queries received.", "counter");
	/*per CPU and total*/
	for(i=0; i<xfrd->nsd->child_count; i++) {
		evbuffer_add_printf(buf, "nsd_queries_total{server=\"%d\"} %lu\n",
			(int)i, (unsigned long)xfrd->nsd->children[i].query_count);
	}

	print_stat_block(buf, st, NULL);

	/* uptime (in seconds) */
	timeval_subtract(&uptime, now, &xfrd->nsd->metrics->boot_time);
	print_metric_help_and_type(buf, "nsd_", "time_up_seconds_total",
	                           "Uptime since server boot in seconds.", "counter");
	evbuffer_add_printf(buf, "nsd_time_up_seconds_total %lu.%6.6lu\n",
		(unsigned long)uptime.tv_sec, (unsigned long)uptime.tv_usec);

	/* time elapsed since last nsd-control stats reset (in seconds) */
	/* if remote-control is disabled aka rc_stats_time == NULL
	 * use metrics' stats_time */
	if (rc_stats_time) {
		timeval_subtract(&elapsed, now, rc_stats_time);
	} else {
		timeval_subtract(&elapsed, now, &xfrd->nsd->metrics->stats_time);
	}
	print_metric_help_and_type(buf, "nsd_", "time_elapsed_seconds",
	                           "Time since last statistics printout and "
	                           "reset (by nsd-control stats) in seconds.",
	                           "untyped");
	evbuffer_add_printf(buf, "nsd_time_elapsed_seconds %lu.%6.6lu\n",
		(unsigned long)elapsed.tv_sec, (unsigned long)elapsed.tv_usec);

	/*mem info, database on disksize*/
	print_metric_help_and_type(buf, "nsd_", "size_db_on_disk_bytes",
	                           "Size of DNS database on disk.", "gauge");
	print_longnum(buf, "nsd_size_db_on_disk_bytes ", st->db_disk);

	print_metric_help_and_type(buf, "nsd_", "size_db_in_mem_bytes",
	                           "Size of DNS database in memory.", "gauge");
	print_longnum(buf, "nsd_size_db_in_mem_bytes ", st->db_mem);

	print_metric_help_and_type(buf, "nsd_", "size_xfrd_in_mem_bytes",
	                           "Size of zone transfers and notifies in xfrd process, excluding TSIG data.",
	                           "gauge");
	print_longnum(buf, "nsd_size_xfrd_in_mem_bytes ", region_get_mem(xfrd->region));

	print_metric_help_and_type(buf, "nsd_", "size_config_on_disk_bytes",
	                           "Size of zonelist file on disk, excluding nsd.conf.",
	                           "gauge");
	print_longnum(buf, "nsd_size_config_on_disk_bytes ",
		xfrd->nsd->options->zonelist_off);

	print_metric_help_and_type(buf, "nsd_", "size_config_in_mem_bytes",
	                           "Size of config data in memory.", "gauge");
	print_longnum(buf, "nsd_size_config_in_mem_bytes ", region_get_mem(
		xfrd->nsd->options->region));

	/* number of zones serverd */
	print_metric_help_and_type(buf, "nsd_", "zones_primary",
	                           "Number of primary zones served.", "gauge");
	evbuffer_add_printf(buf, "nsd_zones_primary %lu\n",
		(unsigned long)(xfrd->notify_zones->count - xfrd->zones->count));

	print_metric_help_and_type(buf, "nsd_", "zones_secondary",
	                           "Number of secondary zones served.", "gauge");
	evbuffer_add_printf(buf, "nsd_zones_secondary %lu\n",
		(unsigned long)xfrd->zones->count);

#ifdef USE_ZONE_STATS
	zonestat_print(NULL, buf, xfrd, clear, zonestats); /*per-zone statistics*/
#else
	(void)clear; (void)zonestats;
#endif
}

#endif /*BIND8_STATS*/

#endif /* USE_METRICS */
