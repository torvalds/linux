/*	$OpenBSD: config.c,v 1.58 2024/01/04 09:30:09 op Exp $	*/

/*
 * Copyright (c) 2008 Pierre-Yves Ritschard <pyr@openbsd.org>
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

#include <sys/resource.h>

#include <ifaddrs.h>
#include <stdlib.h>
#include <string.h>

#include "smtpd.h"
#include "log.h"
#include "ssl.h"

void		 set_local(struct smtpd *, const char *);
void		 set_localaddrs(struct smtpd *, struct table *);

struct smtpd *
config_default(void)
{
	struct smtpd	       *conf = NULL;
	struct mta_limits      *limits = NULL;
	struct table	       *t = NULL;
	char			hostname[HOST_NAME_MAX+1];

	if (getmailname(hostname, sizeof hostname) == -1)
		return NULL;

	if ((conf = calloc(1, sizeof(*conf))) == NULL)
		return conf;

	(void)strlcpy(conf->sc_hostname, hostname, sizeof(conf->sc_hostname));

	conf->sc_maxsize = DEFAULT_MAX_BODY_SIZE;
	conf->sc_subaddressing_delim = SUBADDRESSING_DELIMITER;
	conf->sc_ttl = SMTPD_QUEUE_EXPIRY;
	conf->sc_srs_ttl = SMTPD_QUEUE_EXPIRY / 86400;

	conf->sc_mta_max_deferred = 100;
	conf->sc_scheduler_max_inflight = 5000;
	conf->sc_scheduler_max_schedule = 10;
	conf->sc_scheduler_max_evp_batch_size = 256;
	conf->sc_scheduler_max_msg_batch_size = 1024;

	conf->sc_session_max_rcpt = 1000;
	conf->sc_session_max_mails = 100;

	conf->sc_mda_max_session = 50;
	conf->sc_mda_max_user_session = 7;
	conf->sc_mda_task_hiwat = 50;
	conf->sc_mda_task_lowat = 30;
	conf->sc_mda_task_release = 10;

	/* Report mails delayed for more than 4 hours */
	conf->sc_bounce_warn[0] = 3600 * 4;

	conf->sc_tables_dict = calloc(1, sizeof(*conf->sc_tables_dict));
	conf->sc_rules = calloc(1, sizeof(*conf->sc_rules));
	conf->sc_dispatchers = calloc(1, sizeof(*conf->sc_dispatchers));
	conf->sc_listeners = calloc(1, sizeof(*conf->sc_listeners));
	conf->sc_ca_dict = calloc(1, sizeof(*conf->sc_ca_dict));
	conf->sc_pki_dict = calloc(1, sizeof(*conf->sc_pki_dict));
	conf->sc_ssl_dict = calloc(1, sizeof(*conf->sc_ssl_dict));
	conf->sc_limits_dict = calloc(1, sizeof(*conf->sc_limits_dict));
	conf->sc_mda_wrappers = calloc(1, sizeof(*conf->sc_mda_wrappers));
	conf->sc_filter_processes_dict = calloc(1, sizeof(*conf->sc_filter_processes_dict));
	conf->sc_dispatcher_bounce = calloc(1, sizeof(*conf->sc_dispatcher_bounce));
	conf->sc_filters_dict = calloc(1, sizeof(*conf->sc_filters_dict));
	limits = calloc(1, sizeof(*limits));

	if (conf->sc_tables_dict == NULL	||
	    conf->sc_rules == NULL		||
	    conf->sc_dispatchers == NULL	||
	    conf->sc_listeners == NULL		||
	    conf->sc_ca_dict == NULL		||
	    conf->sc_pki_dict == NULL		||
	    conf->sc_ssl_dict == NULL		||
	    conf->sc_limits_dict == NULL        ||
	    conf->sc_mda_wrappers == NULL	||
	    conf->sc_filter_processes_dict == NULL	||
	    conf->sc_dispatcher_bounce == NULL	||
	    conf->sc_filters_dict == NULL	||
	    limits == NULL)
		goto error;

	dict_init(conf->sc_dispatchers);
	dict_init(conf->sc_mda_wrappers);
	dict_init(conf->sc_ca_dict);
	dict_init(conf->sc_pki_dict);
	dict_init(conf->sc_ssl_dict);
	dict_init(conf->sc_tables_dict);
	dict_init(conf->sc_limits_dict);
	dict_init(conf->sc_filter_processes_dict);

	limit_mta_set_defaults(limits);

	dict_xset(conf->sc_limits_dict, "default", limits);

	TAILQ_INIT(conf->sc_listeners);
	TAILQ_INIT(conf->sc_rules);


	/* bounce dispatcher */
	conf->sc_dispatcher_bounce->type = DISPATCHER_BOUNCE;

	/*
	 * declare special "localhost", "anyhost" and "localnames" tables
	 */
	set_local(conf, conf->sc_hostname);

	t = table_create(conf, "static", "<anydestination>", NULL);
	table_add(t, "*", NULL);

	hostname[strcspn(hostname, ".")] = '\0';
	if (strcmp(conf->sc_hostname, hostname) != 0)
		table_add(t, hostname, NULL);

	table_create(conf, "getpwnam", "<getpwnam>", NULL);

	return conf;

error:
	free(conf->sc_tables_dict);
	free(conf->sc_rules);
	free(conf->sc_dispatchers);
	free(conf->sc_listeners);
	free(conf->sc_ca_dict);
	free(conf->sc_pki_dict);
	free(conf->sc_ssl_dict);
	free(conf->sc_limits_dict);
	free(conf->sc_mda_wrappers);
	free(conf->sc_filter_processes_dict);
	free(conf->sc_dispatcher_bounce);
	free(conf->sc_filters_dict);
	free(limits);
	free(conf);
	return NULL;
}

void
set_local(struct smtpd *conf, const char *hostname)
{
	struct table	*t;

	t = table_create(conf, "static", "<localnames>", NULL);
	table_add(t, "localhost", NULL);
	table_add(t, hostname, NULL);

	set_localaddrs(conf, t);
}

void
set_localaddrs(struct smtpd *conf, struct table *localnames)
{
	struct ifaddrs *ifap, *p;
	struct sockaddr_storage ss;
	struct sockaddr_in	*sain;
	struct sockaddr_in6	*sin6;
	struct table		*t;

	t = table_create(conf, "static", "<anyhost>", NULL);
	table_add(t, "local", NULL);
	table_add(t, "0.0.0.0/0", NULL);
	table_add(t, "::/0", NULL);

	if (getifaddrs(&ifap) == -1)
		fatal("getifaddrs");

	t = table_create(conf, "static", "<localhost>", NULL);
	table_add(t, "local", NULL);

	for (p = ifap; p != NULL; p = p->ifa_next) {
		if (p->ifa_addr == NULL)
			continue;
		switch (p->ifa_addr->sa_family) {
		case AF_INET:
			sain = (struct sockaddr_in *)&ss;
			*sain = *(struct sockaddr_in *)p->ifa_addr;
			sain->sin_len = sizeof(struct sockaddr_in);
			table_add(t, ss_to_text(&ss), NULL);
			table_add(localnames, ss_to_text(&ss), NULL);
			break;

		case AF_INET6:
			sin6 = (struct sockaddr_in6 *)&ss;
			*sin6 = *(struct sockaddr_in6 *)p->ifa_addr;
			sin6->sin6_len = sizeof(struct sockaddr_in6);
#ifdef __KAME__
			if ((IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr) ||
			    IN6_IS_ADDR_MC_LINKLOCAL(&sin6->sin6_addr) ||
			    IN6_IS_ADDR_MC_INTFACELOCAL(&sin6->sin6_addr)) &&
			    sin6->sin6_scope_id == 0) {
				sin6->sin6_scope_id = ntohs(
				    *(u_int16_t *)&sin6->sin6_addr.s6_addr[2]);
				sin6->sin6_addr.s6_addr[2] = 0;
				sin6->sin6_addr.s6_addr[3] = 0;
			}
#endif
			table_add(t, ss_to_text(&ss), NULL);
			table_add(localnames, ss_to_text(&ss), NULL);
			break;
		}
	}

	freeifaddrs(ifap);
}

void
purge_config(uint8_t what)
{
	struct dispatcher	*d;
	struct listener	*l;
	struct table	*t;
	struct rule	*r;
	struct pki	*p;
	const char	*k;
	void		*iter_dict;

	if (what & PURGE_LISTENERS) {
		while ((l = TAILQ_FIRST(env->sc_listeners)) != NULL) {
			TAILQ_REMOVE(env->sc_listeners, l, entry);
			free(l->tls_ciphers);
			free(l->tls_protocols);
			free(l->pki);
			free(l);
		}
		free(env->sc_listeners);
		env->sc_listeners = NULL;
	}
	if (what & PURGE_TABLES) {
		while (dict_root(env->sc_tables_dict, NULL, (void **)&t))
			table_destroy(env, t);
		free(env->sc_tables_dict);
		env->sc_tables_dict = NULL;
	}
	if (what & PURGE_RULES) {
		while ((r = TAILQ_FIRST(env->sc_rules)) != NULL) {
			TAILQ_REMOVE(env->sc_rules, r, r_entry);
			free(r);
		}
		free(env->sc_rules);
		env->sc_rules = NULL;
	}
	if (what & PURGE_DISPATCHERS) {
		while (dict_poproot(env->sc_dispatchers, (void **)&d)) {
			free(d);
		}
		free(env->sc_dispatchers);
		env->sc_dispatchers = NULL;
	}
	if (what & PURGE_PKI) {
		while (dict_poproot(env->sc_pki_dict, (void **)&p)) {
			freezero(p->pki_cert, p->pki_cert_len);
			freezero(p->pki_key, p->pki_key_len);
			free(p);
		}
		free(env->sc_pki_dict);
		env->sc_pki_dict = NULL;
	} else if (what & PURGE_PKI_KEYS) {
		iter_dict = NULL;
		while (dict_iter(env->sc_pki_dict, &iter_dict, &k,
		    (void **)&p)) {
			freezero(p->pki_cert, p->pki_cert_len);
			p->pki_cert = NULL;
			freezero(p->pki_key, p->pki_key_len);
			p->pki_key = NULL;
		}
	}
}

#ifndef CONFIG_MINIMUM

void
config_process(enum smtp_proc_type proc)
{
	struct rlimit rl;

	smtpd_process = proc;
	setproctitle("%s", proc_title(proc));

	if (getrlimit(RLIMIT_NOFILE, &rl) == -1)
		fatal("fdlimit: getrlimit");
	rl.rlim_cur = rl.rlim_max;
	if (setrlimit(RLIMIT_NOFILE, &rl) == -1)
		fatal("fdlimit: setrlimit");
}

void
config_peer(enum smtp_proc_type proc)
{
	struct mproc	*p;

	if (proc == smtpd_process)
		fatal("config_peers: cannot peer with oneself");

	if (proc == PROC_CONTROL)
		p = p_control;
	else if (proc == PROC_LKA)
		p = p_lka;
	else if (proc == PROC_PARENT)
		p = p_parent;
	else if (proc == PROC_QUEUE)
		p = p_queue;
	else if (proc == PROC_SCHEDULER)
		p = p_scheduler;
	else if (proc == PROC_DISPATCHER)
		p = p_dispatcher;
	else if (proc == PROC_CA)
		p = p_ca;
	else
		fatalx("bad peer");

	mproc_enable(p);
}

#else

void config_process(enum smtp_proc_type proc) {}
void config_peer(enum smtp_proc_type proc) {}

#endif
