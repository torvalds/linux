/*	$OpenBSD: radiusd_ipcp.c,v 1.27 2025/06/25 11:38:21 yasuoka Exp $	*/

/*
 * Copyright (c) 2024 Internet Initiative Japan Inc.
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
#include <sys/tree.h>
#include <arpa/inet.h>

#include <inttypes.h>
#include <netdb.h>
#include <db.h>
#include <err.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <pwd.h>
#include <radius.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <imsg.h>

#include "radiusd.h"
#include "radiusd_module.h"
#include "radiusd_ipcp.h"
#include "log.h"

#define RADIUSD_IPCP_START_WAIT	60

enum ipcp_address_type {
	ADDRESS_TYPE_POOL,
	ADDRESS_TYPE_STATIC
};

struct ipcp_address {
	enum ipcp_address_type		 type;
	struct in_addr			 start;
	struct in_addr			 end;
	int				 naddrs;
	TAILQ_ENTRY(ipcp_address)	 next;
};

struct user {
	TAILQ_HEAD(, assigned_ipv4)	 ipv4s;
	RB_ENTRY(user)			 tree;
	char				 name[0];
};

struct radiusctl_client {
	int				 peerid;
	TAILQ_ENTRY(radiusctl_client)	 entry;
};

struct module_ipcp_dae;

struct assigned_ipv4 {
	struct in_addr			 ipv4;
	unsigned			 seq;
	char				 session_id[256];
	char				 auth_method[16];
	struct user			*user;
	uint32_t			 session_timeout;
	struct timespec			 start;
	struct timespec			 timeout;
	struct in_addr			 nas_ipv4;
	struct in6_addr			 nas_ipv6;
	char				 nas_id[256];
	uint32_t			 nas_port;
	char				 nas_port_id[256];
	const char			*tun_type;
	union {
		struct sockaddr_in	 sin4;
		struct sockaddr_in6	 sin6;
	}				 tun_client;

	struct timespec			 authtime;
	RB_ENTRY(assigned_ipv4)		 tree;
	TAILQ_ENTRY(assigned_ipv4)	 next;

	/* RFC 5176 Dynamic Authorization Extensions for RADIUS */
	struct module_ipcp_dae		*dae;
	RADIUS_PACKET			*dae_reqpkt;
	TAILQ_ENTRY(assigned_ipv4)	 dae_next;
	int				 dae_ntry;
	struct event			 dae_evtimer;
	TAILQ_HEAD(, radiusctl_client)	 dae_clients;
};

struct module_ipcp_ctrlconn {
	uint32_t			 peerid;
	TAILQ_ENTRY(module_ipcp_ctrlconn)
					 next;
};

struct module_ipcp_dae {
	struct module_ipcp		*ipcp;
	int				 sock;
	char				 nas_id[256];
	char				 secret[80];
	union {
		struct sockaddr_in	 sin4;
		struct sockaddr_in6	 sin6;
	}				 nas_addr;
	struct event			 ev_sock;
	struct event			 ev_reqs;
	TAILQ_ENTRY(module_ipcp_dae)	 next;
	TAILQ_HEAD(, assigned_ipv4)	 reqs;
	int				 ninflight;
};

struct module_ipcp {
	struct module_base		*base;
	int				 nsessions;
	unsigned			 seq;
	int				 max_sessions;
	int				 user_max_sessions;
	int				 start_wait;
	int				 session_timeout;
	bool				 no_session_timeout;
	struct timespec			 uptime;
	struct in_addr			 name_server[2];
	struct in_addr			 netbios_server[2];
	RB_HEAD(assigned_ipv4_tree, assigned_ipv4)
					 ipv4s;
	RB_HEAD(user_tree, user)	 users;
	int				 npools;
	TAILQ_HEAD(,ipcp_address)	 addrs;
	TAILQ_HEAD(,module_ipcp_ctrlconn)
					 ctrls;
	TAILQ_HEAD(,module_ipcp_dae)	 daes;
	struct event			 ev_timer;
};

#ifndef nitems
#define nitems(_x)    (sizeof((_x)) / sizeof((_x)[0]))
#endif

#ifndef MAXIMUM
#define MAXIMUM(_a, _b)	(((_a) > (_b))? (_a) : (_b))
#endif

static void	 ipcp_init(struct module_ipcp *);
static void	 ipcp_start(void *);
static void	 ipcp_stop(void *);
static void	 ipcp_fini(struct module_ipcp *);
static void	 ipcp_config_set(void *, const char *, int, char * const *);
static void	 ipcp_dispatch_control(void *, struct imsg *);
static int	 ipcp_notice_startstop(struct module_ipcp *,
		    struct assigned_ipv4 *, int,
		    struct radiusd_ipcp_statistics *);
static void	 ipcp_resdeco(void *, u_int, const u_char *, size_t reqlen,
		    const u_char *, size_t reslen);
static void	 ipcp_reject(struct module_ipcp *, RADIUS_PACKET *,
		    unsigned int, RADIUS_PACKET *, int);
static void	 ipcp_accounting_request(void *, u_int, const u_char *,
		    size_t);

struct assigned_ipv4
		*ipcp_ipv4_assign(struct module_ipcp *, struct user *,
		    struct in_addr);
static struct assigned_ipv4
		*ipcp_ipv4_find(struct module_ipcp *, struct in_addr);
static struct assigned_ipv4
		*ipcp_ipv4_check_valid(struct module_ipcp *,
		    struct assigned_ipv4 *);
static void	 ipcp_ipv4_delete(struct module_ipcp *,
		    struct assigned_ipv4 *, const char *);
static void	 ipcp_ipv4_release(struct module_ipcp *,
		    struct assigned_ipv4 *);
static int	 assigned_ipv4_compar(struct assigned_ipv4 *,
		    struct assigned_ipv4 *);
static struct user
		*ipcp_user_get(struct module_ipcp *, const char *);
static int	 user_compar(struct user *, struct user *);
static int	 ipcp_prepare_db(void);
static int	 ipcp_restore_from_db(struct module_ipcp *);
static void	 ipcp_put_db(struct module_ipcp *, struct assigned_ipv4 *);
static void	 ipcp_del_db(struct module_ipcp *, struct assigned_ipv4 *);
static void	 ipcp_db_dump_fill_record(struct radiusd_ipcp_db_dump *, int,
		    struct assigned_ipv4 *);
static void	 ipcp_update_time(struct module_ipcp *);
static void	 ipcp_on_timer(int, short, void *);
static void	 ipcp_schedule_timer(struct module_ipcp *);
static void	 ipcp_dae_send_disconnect_request(struct assigned_ipv4 *);
static void	 ipcp_dae_request_on_timeout(int, short, void *);
static void	 ipcp_dae_on_event(int, short, void *);
static void	 ipcp_dae_reset_request(struct assigned_ipv4 *);
static void	 ipcp_dae_send_pending_requests(int, short, void *);
static struct ipcp_address
		*parse_address_range(const char *);
static const char
		*radius_tunnel_type_string(unsigned, const char *);
static const char
		*radius_terminate_cause_string(unsigned);
static const char
		*radius_error_cause_string(unsigned);
static int	 parse_addr(const char *, int, struct sockaddr *, socklen_t);
static const char
		*print_addr(struct sockaddr *, char *, size_t);

RB_PROTOTYPE_STATIC(assigned_ipv4_tree, assigned_ipv4, tree,
    assigned_ipv4_compar);
RB_PROTOTYPE_STATIC(user_tree, user, tree, user_compar);

int
main(int argc, char *argv[])
{
	struct module_ipcp	 module_ipcp;
	struct module_handlers	 handlers = {
		.start =		ipcp_start,
		.stop =			ipcp_stop,
		.config_set =		ipcp_config_set,
		.response_decoration =	ipcp_resdeco,
		.accounting_request =	ipcp_accounting_request,
		.dispatch_control =	ipcp_dispatch_control
	};

	ipcp_init(&module_ipcp);

	if ((module_ipcp.base = module_create(STDIN_FILENO, &module_ipcp,
	    &handlers)) == NULL)
		err(1, "Could not create a module instance");

	if (ipcp_prepare_db() == -1)
		err(1, "ipcp_prepare_db");

	module_drop_privilege(module_ipcp.base, 1);
	if (unveil(_PATH_RADIUSD_IPCP_DB, "rw") == -1)
		err(1, "unveil");
	if (pledge("stdio inet rpath wpath flock", NULL) == -1)
		err(1, "pledge");
	setproctitle("[main]");

	module_load(module_ipcp.base);
	log_init(0);
	event_init();

	module_start(module_ipcp.base);
	event_loop(0);

	ipcp_fini(&module_ipcp);

	event_loop(0);
	event_base_free(NULL);

	exit(EXIT_SUCCESS);
}

void
ipcp_init(struct module_ipcp *self)
{
	memset(self, 0, sizeof(struct module_ipcp));
	TAILQ_INIT(&self->addrs);
	RB_INIT(&self->ipv4s);
	RB_INIT(&self->users);
	TAILQ_INIT(&self->ctrls);
	TAILQ_INIT(&self->daes);
	self->seq = 1;
	self->no_session_timeout = true;
	ipcp_update_time(self);
}

void
ipcp_start(void *ctx)
{
	struct module_ipcp	*self = ctx;
	struct ipcp_address	*addr;
	struct module_ipcp_dae	*dae;
	int			 sock;

	ipcp_update_time(self);
	if (self->start_wait == 0)
		self->start_wait = RADIUSD_IPCP_START_WAIT;

	/* count pool address*/
	TAILQ_FOREACH(addr, &self->addrs, next) {
		if (addr->type == ADDRESS_TYPE_POOL)
			self->npools += addr->naddrs;
	}
	log_info("number of pooled IP addresses = %d", self->npools);

	if (ipcp_restore_from_db(self) == -1) {
		module_send_message(self->base, IMSG_NG,
		    "Restoring the database failed: %s", strerror(errno));
		module_stop(self->base);
		return;
	}
	ipcp_schedule_timer(self);

	/* prepare socket for DAE */
	TAILQ_FOREACH(dae, &self->daes, next) {
		if ((sock = socket(dae->nas_addr.sin4.sin_family,
		    SOCK_DGRAM, IPPROTO_UDP)) == -1) {
			log_warn("%s: could not start dae: socket()", __func__);
			return;
		}
		if (connect(sock, (struct sockaddr *)&dae->nas_addr,
		    dae->nas_addr.sin4.sin_len) == -1) {
			log_warn("%s: could not start dae: connect()",
			    __func__);
			return;
		}
		dae->sock = sock;
		event_set(&dae->ev_sock, sock, EV_READ | EV_PERSIST,
		    ipcp_dae_on_event, dae);
		event_add(&dae->ev_sock, NULL);
		evtimer_set(&dae->ev_reqs, ipcp_dae_send_pending_requests, dae);
	}

	module_send_message(self->base, IMSG_OK, NULL);
}

void
ipcp_stop(void *ctx)
{
	struct module_ipcp		*self = ctx;
	struct module_ipcp_dae		*dae;

	ipcp_update_time(self);
	/* stop the sockets for DAE */
	TAILQ_FOREACH(dae, &self->daes, next) {
		if (dae->sock >= 0) {
			event_del(&dae->ev_sock);
			close(dae->sock);
			dae->sock = -1;
		}
		if (evtimer_pending(&dae->ev_reqs, NULL))
			event_del(&dae->ev_reqs);
	}
	if (evtimer_pending(&self->ev_timer, NULL))
		evtimer_del(&self->ev_timer);
}

void
ipcp_fini(struct module_ipcp *self)
{
	struct assigned_ipv4		*assign, *assignt;
	struct user			*user, *usert;
	struct module_ipcp_ctrlconn	*ctrl, *ctrlt;
	struct module_ipcp_dae		*dae, *daet;
	struct ipcp_address		*addr, *addrt;

	RB_FOREACH_SAFE(assign, assigned_ipv4_tree, &self->ipv4s, assignt)
		ipcp_ipv4_release(self, assign);
	RB_FOREACH_SAFE(user, user_tree, &self->users, usert) {
		RB_REMOVE(user_tree, &self->users, user);
		free(user);
	}
	TAILQ_FOREACH_SAFE(ctrl, &self->ctrls, next, ctrlt)
		free(ctrl);
	TAILQ_FOREACH_SAFE(dae, &self->daes, next, daet) {
		if (dae->sock >= 0) {
			event_del(&dae->ev_sock);
			close(dae->sock);
		}
		free(dae);
	}
	TAILQ_FOREACH_SAFE(addr, &self->addrs, next, addrt)
		free(addr);
	if (evtimer_pending(&self->ev_timer, NULL))
		evtimer_del(&self->ev_timer);
	module_destroy(self->base);
}

void
ipcp_config_set(void *ctx, const char *name, int argc, char * const * argv)
{
	struct module_ipcp	*module = ctx;
	const char		*errmsg = "none";
	int			 i;
	struct ipcp_address	*addr;
	struct in_addr		 ina;
	struct module_ipcp_dae	 dae, *dae0;

	if (strcmp(name, "address") == 0) {
		SYNTAX_ASSERT(argc >= 1,
		    "specify one of pool, server, nas-select, or user-select");
		if (strcmp(argv[0], "pool") == 0) {
			SYNTAX_ASSERT(argc >= 2,
			    "`address pool' must have one address range at "
			    "least");
			addr = TAILQ_FIRST(&module->addrs);
			for (i = 0; i < argc - 1; i++) {
				if ((addr = parse_address_range(argv[i + 1]))
				    == NULL) {
					module_send_message(module->base,
					    IMSG_NG, "Invalid address range: "
					    "%s", argv[i + 1]);
					return;
				}
				addr->type = ADDRESS_TYPE_POOL;
				TAILQ_INSERT_TAIL(&module->addrs, addr, next);
			}
		} else if (strcmp(argv[0], "static") == 0) {
			SYNTAX_ASSERT(argc >= 2,
			    "`address static' must have one address range at "
			    "least");
			addr = TAILQ_FIRST(&module->addrs);
			for (i = 0; i < argc - 1; i++) {
				if ((addr = parse_address_range(argv[i + 1]))
				    == NULL) {
					module_send_message(module->base,
					    IMSG_NG, "Invalid address range: "
					    "%s", argv[i + 1]);
					return;
				}
				addr->type = ADDRESS_TYPE_STATIC;
				TAILQ_INSERT_TAIL(&module->addrs, addr, next);
			}
		} else
			SYNTAX_ASSERT(0, "specify pool or static");
	} else if (strcmp(name, "max-sessions") == 0) {
		SYNTAX_ASSERT(argc == 1,
		    "`max-sessions' must have an argument");
		module->max_sessions = strtonum(argv[0], 0, INT_MAX, &errmsg);
		if (errmsg != NULL) {
			module_send_message(module->base, IMSG_NG,
			    "could not parse `max-sessions': %s", errmsg);
			return;
		}
	} else if (strcmp(name, "user-max-sessions") == 0) {
		SYNTAX_ASSERT(argc == 1, "`max-session' must have an argument");
		module->user_max_sessions = strtonum(argv[0], 0, INT_MAX,
		    &errmsg);
		if (errmsg != NULL) {
			module_send_message(module->base, IMSG_NG,
			    "could not parse `user-max-session': %s", errmsg);
			return;
		}
	} else if (strcmp(name, "start-wait") == 0) {
		SYNTAX_ASSERT(argc == 1, "`start-wait' must have an argument");
		module->start_wait = strtonum(argv[0], 1, INT_MAX, &errmsg);
		if (errmsg != NULL) {
			module_send_message(module->base, IMSG_NG,
			    "could not parse `start-wait': %s", errmsg);
			return;
		}
	} else if (strcmp(name, "name-server") == 0) {
		SYNTAX_ASSERT(argc == 1 || argc == 2,
		    "specify 1 or 2 addresses for `name-server'");
		for (i = 0; i < argc; i++) {
			if (inet_pton(AF_INET, argv[i], &ina) != 1) {
				module_send_message(module->base, IMSG_NG,
				    "Invalid IP address: %s", argv[i]);
				return;
			}
			if (module->name_server[0].s_addr == 0)
				module->name_server[0] = ina;
			else if (module->name_server[1].s_addr == 0)
				module->name_server[1] = ina;
			else
				SYNTAX_ASSERT(0,
				    "too many `name-server' is configured");
		}
	} else if (strcmp(name, "netbios-server") == 0) {
		SYNTAX_ASSERT(argc == 1 || argc == 2,
		    "specify 1 or 2 addresses for `name-server'");
		for (i = 0; i < argc; i++) {
			if (inet_pton(AF_INET, argv[i], &ina) != 1) {
				module_send_message(module->base, IMSG_NG,
				    "Invalid IP address: %s", argv[i]);
				return;
			}
			if (module->netbios_server[0].s_addr == 0)
				module->netbios_server[0] = ina;
			else if (module->netbios_server[1].s_addr == 0)
				module->netbios_server[1] = ina;
			else
				SYNTAX_ASSERT(0,
				    "too many `name-server' is configured");
		}
	} else if (strcmp(name, "session-timeout") == 0) {
		SYNTAX_ASSERT(argc == 1,
		    "`session-timeout' must have an argument");
		if (strcmp(argv[0], "radius") == 0) {
			module->no_session_timeout = false;
			module->session_timeout = 0;
		} else {
			module->no_session_timeout = false;
			module->session_timeout = strtonum(argv[0], 1, INT_MAX,
			    &errmsg);
			if (errmsg != NULL) {
				module_send_message(module->base, IMSG_NG,
				    "could not parse `session-timeout': %s",
				    errmsg);
				return;
			}
		}
	} else if (strcmp(name, "dae") == 0) {
		memset(&dae, 0, sizeof(dae));
		dae.sock = -1;
		if (!(argc >= 1 || strcmp(argv[1], "server") == 0)) {
			module_send_message(module->base, IMSG_NG,
			    "`%s' is unknown", argv[1]);
			return;
		}
		i = 1;
		SYNTAX_ASSERT(i < argc, "no address[:port] for dae server");
		if (i < argc &&
		    parse_addr(argv[i], AF_UNSPEC, (struct sockaddr *)
		    &dae.nas_addr, sizeof(dae.nas_addr)) == -1) {
			module_send_message(module->base, IMSG_NG,
			    "failed to parse dae server's address, %s",
			    argv[i]);
			return;
		}
		if (ntohs(dae.nas_addr.sin4.sin_port) == 0)
			dae.nas_addr.sin4.sin_port =
			    htons(RADIUS_DAE_DEFAULT_PORT);
		i++;
		SYNTAX_ASSERT(i < argc, "no secret for dae server");
		if (strlcpy(dae.secret, argv[i++], sizeof(dae.secret)) >=
		    sizeof(dae.secret)) {
			module_send_message(module->base, IMSG_NG,
			    "dae server's secret must be < %d bytes",
			    (int)sizeof(dae.secret) - 1);
			return;
		}
		if (i < argc)
			strlcpy(dae.nas_id, argv[i++], sizeof(dae.nas_id));
		if ((dae0 = calloc(1, sizeof(struct module_ipcp_dae))) == NULL)
		{
			module_send_message(module->base, IMSG_NG,
			    "%s", strerror(errno));
			return;
		}
		*dae0 = dae;
		TAILQ_INIT(&dae0->reqs);
		TAILQ_INSERT_TAIL(&module->daes, dae0, next);
		dae0->ipcp = module;
	} else if (strcmp(name, "_debug") == 0)
		log_init(1);
	else if (strncmp(name, "_", 1) == 0)
		/* ignore */;
	else {
		module_send_message(module->base, IMSG_NG,
		    "Unknown config parameter name `%s'", name);
		return;
	}
	module_send_message(module->base, IMSG_OK, NULL);

	return;
 syntax_error:
	module_send_message(module->base, IMSG_NG, "%s", errmsg);
}

void
ipcp_dispatch_control(void *ctx, struct imsg *imsg)
{
	struct module_ipcp		*self = ctx;
	struct assigned_ipv4		*assign;
	struct radiusd_ipcp_db_dump	*dump;
	struct module_ipcp_ctrlconn	*ctrl, *ctrlt;
	int				 i;
	size_t				 dumpsiz;
	u_int				 datalen;
	unsigned			 seq;
	struct radiusctl_client		*client;
	const char			*cause;

	ipcp_update_time(self);
	datalen = imsg->hdr.len - IMSG_HEADER_SIZE;
	switch (imsg->hdr.type) {
	case IMSG_RADIUSD_MODULE_CTRL_UNBIND:
		TAILQ_FOREACH_SAFE(ctrl, &self->ctrls, next, ctrlt) {
			if (ctrl->peerid == imsg->hdr.peerid) {
				TAILQ_REMOVE(&self->ctrls, ctrl, next);
				free(ctrl);
				break;
			}
		}
		break;
	case IMSG_RADIUSD_MODULE_IPCP_MONITOR:
	case IMSG_RADIUSD_MODULE_IPCP_DUMP_AND_MONITOR:
		if ((ctrl = calloc(1, sizeof(struct module_ipcp_ctrlconn)))
		    == NULL) {
			log_warn("%s: calloc()", __func__);
			goto fail;
		}
		ctrl->peerid = imsg->hdr.peerid;
		TAILQ_INSERT_TAIL(&self->ctrls, ctrl, next);
		module_imsg_compose(self->base, IMSG_RADIUSD_MODULE_CTRL_BIND,
		    imsg->hdr.peerid, 0, -1, NULL, 0);
		if (imsg->hdr.type == IMSG_RADIUSD_MODULE_IPCP_MONITOR)
			break;
		/* FALLTHROUGH */
	case IMSG_RADIUSD_MODULE_IPCP_DUMP:
		dumpsiz = MAX_IMSGSIZE;
		if ((dump = calloc(1, dumpsiz)) == NULL) {
			log_warn("%s: calloc()", __func__);
			goto fail;
		}
		i = 0;
		RB_FOREACH(assign, assigned_ipv4_tree, &self->ipv4s) {
			if (!timespecisset(&assign->start))
				/* not started yet */
				continue;
			ipcp_db_dump_fill_record(dump, i++, assign);
			if (RB_NEXT(assigned_ipv4_tree, &self->ipv4s, assign)
			    == NULL)
				break;
			if (offsetof(struct radiusd_ipcp_db_dump,
			    records[i + 1]) >= dumpsiz) {
				module_imsg_compose(self->base,
				    IMSG_RADIUSD_MODULE_IPCP_DUMP,
				    imsg->hdr.peerid, 0, -1,
				    dump, offsetof(struct radiusd_ipcp_db_dump,
				    records[i]));
				i = 0;
			}
		}
		dump->islast = 1;
		module_imsg_compose(self->base, IMSG_RADIUSD_MODULE_IPCP_DUMP,
		    imsg->hdr.peerid, 0, -1, dump, offsetof(
		    struct radiusd_ipcp_db_dump, records[i]));
		freezero(dump ,dumpsiz);
		break;
	case IMSG_RADIUSD_MODULE_IPCP_DISCONNECT:
	case IMSG_RADIUSD_MODULE_IPCP_DELETE:
		if (datalen < sizeof(unsigned)) {
			log_warn("%s: received "
			    "%s message size is wrong", __func__,
			    (imsg->hdr.type ==
			    IMSG_RADIUSD_MODULE_IPCP_DISCONNECT)
			    ? "IMSG_RADIUSD_MODULE_IPCP_DISCONNECT"
			    : "IMSG_RADIUSD_MODULE_IPCP_DELETE");
			goto fail;
		}
		seq = *(unsigned *)imsg->data;
		RB_FOREACH(assign, assigned_ipv4_tree, &self->ipv4s) {
			if (!timespecisset(&assign->start))
				/* not started yet */
				continue;
			if (assign->seq == seq)
				break;
		}
		if (assign == NULL) {
			cause = "session not found";
			log_warnx("%s seq=%u requested, but the "
			    "session is not found",
			    (imsg->hdr.type ==
			    IMSG_RADIUSD_MODULE_IPCP_DISCONNECT)? "Disconnect"
			    : "Delete", seq);
			module_imsg_compose(self->base, IMSG_NG,
			    imsg->hdr.peerid, 0, -1, cause, strlen(cause) + 1);
		} else if (imsg->hdr.type == IMSG_RADIUSD_MODULE_IPCP_DELETE) {
			log_info("Delete seq=%u by request", assign->seq);
			ipcp_ipv4_delete(self,  assign, "By control");
			module_imsg_compose(self->base, IMSG_OK,
			    imsg->hdr.peerid, 0, -1, NULL, 0);
		} else {
			if (assign->dae == NULL)
				log_warnx("Disconnect seq=%u requested, but "
				    "DAE is not configured", assign->seq);
			else {
				log_info("Disconnect seq=%u requested",
				    assign->seq);
				if ((client = calloc(1, sizeof(struct
				    radiusctl_client))) == NULL) {
					log_warn("%s: calloc: %m",
					    __func__);
					goto fail;
				}
				client->peerid = imsg->hdr.peerid;
				if (assign->dae_ntry == 0)
					ipcp_dae_send_disconnect_request(
					    assign);
				TAILQ_INSERT_TAIL(&assign->dae_clients,
				    client, entry);
			}
		}
		break;
	}
	return;
 fail:
	module_stop(self->base);
}

int
ipcp_notice_startstop(struct module_ipcp *self, struct assigned_ipv4 *assign,
    int start, struct radiusd_ipcp_statistics *stat)
{
	struct module_ipcp_ctrlconn	*ctrl;
	struct radiusd_ipcp_db_dump	*dump;
	size_t				 dumpsiz;
	struct iovec			 iov[2];
	int				 niov = 0;

	dumpsiz = offsetof(struct radiusd_ipcp_db_dump, records[1]);
	if ((dump = calloc(1, dumpsiz)) == NULL) {
		log_warn("%s: calloc()", __func__);
		return (-1);
	}
	dump->islast = 1;
	ipcp_db_dump_fill_record(dump, 0, assign);

	iov[niov].iov_base = dump;
	iov[niov].iov_len = dumpsiz;
	if (start == 0) {
		iov[++niov].iov_base = stat;
		iov[niov].iov_len = sizeof(struct radiusd_ipcp_statistics);
	}
	TAILQ_FOREACH(ctrl, &self->ctrls, next)
		module_imsg_composev(self->base,
		    (start)? IMSG_RADIUSD_MODULE_IPCP_START :
		    IMSG_RADIUSD_MODULE_IPCP_STOP, ctrl->peerid, 0, -1, iov,
		    niov + 1);
	freezero(dump, dumpsiz);
	return (0);
}

void
ipcp_resdeco(void *ctx, u_int q_id, const u_char *req, size_t reqlen,
    const u_char *res, size_t reslen)
{
	struct module_ipcp	*self = ctx;
	RADIUS_PACKET		*radres = NULL, *radreq = NULL;
	struct in_addr		 addr4;
	const struct in_addr	 mask4 = { .s_addr = 0xffffffffUL };
	int			 res_code, msraserr = 935;
	struct ipcp_address	*addr;
	int			 i, n;
	bool			 found = false;
	char			 username[256], buf[128];
	struct user		*user = NULL;
	struct assigned_ipv4	*assigned = NULL, *assign, *assignt;

	ipcp_update_time(self);

	if ((radres = radius_convert_packet(res, reslen)) == NULL) {
		log_warn("%s: radius_convert_packet() failed", __func__);
		goto fatal;
	}
	res_code = radius_get_code(radres);
	if (res_code != RADIUS_CODE_ACCESS_ACCEPT)
		goto accept;

	if ((radreq = radius_convert_packet(req, reqlen)) == NULL) {
		log_warn("%s: radius_convert_packet() failed", __func__);
		goto fatal;
	}

	/*
	 * prefer User-Name of the response rather than the request,
	 * since it must be the authenticated user.
	 */
	if (radius_get_string_attr(radres, RADIUS_TYPE_USER_NAME, username,
	    sizeof(username)) != 0 &&
	    radius_get_string_attr(radreq, RADIUS_TYPE_USER_NAME, username,
	    sizeof(username)) != 0) {
		log_warnx("q=%u unexpected request: no user-name", q_id);
		goto fatal;
	}

	if ((addr = TAILQ_FIRST(&self->addrs)) != NULL) {
		/* The address assignment is configured */

		struct in_addr		 nas_ipv4;
		struct in6_addr		 nas_ipv6;
		char			 nas_id[256];
		uint32_t		 nas_port;
		char			 nas_port_id[256];

		memset(&nas_ipv4, 0, sizeof(nas_ipv4));
		memset(&nas_ipv6, 0, sizeof(nas_ipv6));
		memset(nas_id, 0, sizeof(nas_id));
		memset(&nas_port, 0, sizeof(nas_port));
		memset(nas_port_id, 0, sizeof(nas_port_id));

		radius_get_ipv4_attr(radreq, RADIUS_TYPE_NAS_IP_ADDRESS,
		    &nas_ipv4);
		radius_get_ipv6_attr(radreq, RADIUS_TYPE_NAS_IPV6_ADDRESS,
		    &nas_ipv6);
		radius_get_string_attr(radreq, RADIUS_TYPE_NAS_IDENTIFIER,
		    nas_id, sizeof(nas_id));
		radius_get_uint32_attr(radreq, RADIUS_TYPE_NAS_PORT, &nas_port);
		radius_get_string_attr(radreq, RADIUS_TYPE_NAS_PORT_ID,
		    nas_port_id, sizeof(nas_port_id));

		if ((user = ipcp_user_get(self, username)) == NULL) {
			log_warn("%s: ipcp_user_get()", __func__);
			goto fatal;
		}

		msraserr = 935;
		if (self->max_sessions != 0) {
			if (self->nsessions >= self->max_sessions) {
				log_info("q=%u user=%s rejected: number of "
				    "sessions reached the limit(%d)", q_id,
				    user->name, self->max_sessions);
				goto reject;
			}
		}

		n = 0;
		TAILQ_FOREACH_SAFE(assign, &user->ipv4s, next, assignt) {
			assign = ipcp_ipv4_check_valid(self, assign);
			if (assign == NULL)
				continue;
			/*
			 * This assigned IP is for the same NAS Port,
			 * reuse it.
			 */
			if (assign->start.tv_sec == 0 &&
			    memcmp(&assign->nas_ipv4, &nas_ipv4,
			    sizeof(struct in_addr)) == 0 &&
			    memcmp(&assign->nas_ipv6, &nas_ipv6,
			    sizeof(struct in6_addr)) == 0 && memcmp(
			    assign->nas_id, nas_id, sizeof(nas_id)) == 0 &&
			    assign->nas_port == nas_port &&
			    memcmp(assign->nas_port_id, nas_port_id,
			    sizeof(nas_port_id)) == 0) {
				addr4 = assign->ipv4;
				assigned = assign;
				assigned->authtime = self->uptime;
				log_info("q=%u Reassign %s for %s", q_id,
				    inet_ntop(AF_INET, &addr4, buf,
				    sizeof(buf)), username);
				goto reassign;
			}
			n++;
		}
		if (self->user_max_sessions != 0 &&
		    n >= self->user_max_sessions) {
			log_info("q=%u user=%s rejected: number of sessions "
			    "per a user reached the limit(%d)", q_id,
			    user->name, self->user_max_sessions);
			goto reject;
		}

		msraserr = 716;
		if (radius_get_ipv4_attr(radres,
		    RADIUS_TYPE_FRAMED_IP_ADDRESS, &addr4) == 0) {
			if (ipcp_ipv4_find(self, addr4) != NULL)
				log_info("q=%u user=%s rejected: server "
				    "requested IP address is busy", q_id,
				    user->name);
			else {
				/* compare in host byte order */
				addr4.s_addr = ntohl(addr4.s_addr);
				TAILQ_FOREACH(addr, &self->addrs, next) {
					if (addr->type != ADDRESS_TYPE_STATIC &&
					    addr->type != ADDRESS_TYPE_POOL)
						continue;
					if (addr->start.s_addr <= addr4.s_addr
					    && addr4.s_addr <= addr->end.s_addr)
						break;
				}
				if (addr == NULL)
					log_info("q=%u user=%s rejected: "
					    "server requested IP address is "
					    "out of the range", q_id,
					    user->name);
				else
					found = true;
				/* revert the addr to the network byte order */
				addr4.s_addr = htonl(addr4.s_addr);
			}
			if (!found)
				goto reject;
		} else {
			int inpool_idx = 0;

			/* select a random address */
			n = arc4random_uniform(self->npools);
			i = 0;
			TAILQ_FOREACH(addr, &self->addrs, next) {
				if (addr->type == ADDRESS_TYPE_POOL) {
					if (i <= n && n < i + addr->naddrs) {
						inpool_idx = n - i;
						break;
					}
					i += addr->naddrs;
				}
			}
			/* loop npools times until a free address is found */
			for (i = 0; i < self->npools && addr != NULL; i++) {
				addr4.s_addr = htonl(
				    addr->start.s_addr + inpool_idx);
				if (ipcp_ipv4_find(self, addr4) == NULL) {
					found = true;
					break;
				}
				/* try inpool_idx if it's in the range */
				if (++inpool_idx < addr->naddrs)
					continue;
				/* iterate addr to the next pool */
				do {
					addr = TAILQ_NEXT(addr, next);
					if (addr == NULL)
						addr = TAILQ_FIRST(
						    &self->addrs);
				} while (addr->type != ADDRESS_TYPE_POOL);
				inpool_idx = 0;	/* try the first */
			}
			if (!found) {
				log_info("q=%u user=%s rejected: ran out of "
				    "the address pool", q_id, user->name);
				goto reject;
			}
		}
		if ((assigned = ipcp_ipv4_assign(self, user, addr4)) == NULL) {
			log_warn("%s: ipcp_ipv4_assign()", __func__);
			goto fatal;
		}
		radius_set_ipv4_attr(radres, RADIUS_TYPE_FRAMED_IP_NETMASK,
		    mask4);
		log_info("q=%u Assign %s for %s", q_id,
		    inet_ntop(AF_INET, &addr4, buf, sizeof(buf)), username);
 reassign:
		radius_del_attr_all(radres, RADIUS_TYPE_FRAMED_IP_ADDRESS);
		radius_put_ipv4_attr(radres, RADIUS_TYPE_FRAMED_IP_ADDRESS,
		    addr4);
		if (radius_has_attr(radreq, RADIUS_TYPE_USER_PASSWORD))
			strlcpy(assigned->auth_method, "PAP",
			    sizeof(assigned->auth_method));
		else if (radius_has_attr(radreq, RADIUS_TYPE_CHAP_PASSWORD))
			strlcpy(assigned->auth_method, "CHAP",
			    sizeof(assigned->auth_method));
		else if (radius_has_vs_attr(radreq, RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_CHAP_RESPONSE))
			strlcpy(assigned->auth_method, "MS-CHAP",
			    sizeof(assigned->auth_method));
		else if (radius_has_vs_attr(radreq, RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_CHAP2_RESPONSE))
			strlcpy(assigned->auth_method, "MS-CHAP-V2",
			    sizeof(assigned->auth_method));
		else if (radius_has_attr(radreq, RADIUS_TYPE_EAP_MESSAGE))
			strlcpy(assigned->auth_method, "EAP",
			    sizeof(assigned->auth_method));

		assigned->nas_ipv4 = nas_ipv4;
		assigned->nas_ipv6 = nas_ipv6;
		memcpy(assigned->nas_id, nas_id, sizeof(assign->nas_id));
		assigned->nas_port = nas_port;
		memcpy(assigned->nas_port_id, nas_port_id,
		    sizeof(assign->nas_port_id));
	}

	if (self->name_server[0].s_addr != 0) {
		addr4.s_addr = htonl(self->name_server[0].s_addr);
		radius_del_vs_attr_all(radres,
		    RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_PRIMARY_DNS_SERVER);
		radius_put_vs_ipv4_attr(radres,
		    RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_PRIMARY_DNS_SERVER, self->name_server[0]);
	}
	if (self->name_server[1].s_addr != 0) {
		addr4.s_addr = htonl(self->name_server[1].s_addr);
		radius_del_vs_attr_all(radres,
		    RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_SECONDARY_DNS_SERVER);
		radius_put_vs_ipv4_attr(radres,
		    RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_SECONDARY_DNS_SERVER, self->name_server[1]);
	}
	if (self->netbios_server[0].s_addr != 0) {
		addr4.s_addr = htonl(self->netbios_server[0].s_addr);
		radius_del_vs_attr_all(radres,
		    RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_PRIMARY_DNS_SERVER);
		radius_put_vs_ipv4_attr(radres,
		    RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_PRIMARY_DNS_SERVER,
		    self->netbios_server[0]);
	}
	if (self->netbios_server[1].s_addr != 0) {
		addr4.s_addr = htonl(self->netbios_server[1].s_addr);
		radius_del_vs_attr_all(radres,
		    RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_SECONDARY_NBNS_SERVER);
		radius_put_vs_ipv4_attr(radres,
		    RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_SECONDARY_NBNS_SERVER,
		    self->netbios_server[1]);
	}
	if (!self->no_session_timeout && assigned != NULL &&
	    radius_has_attr(radres, RADIUS_TYPE_SESSION_TIMEOUT)) {
		radius_get_uint32_attr(radres, RADIUS_TYPE_SESSION_TIMEOUT,
		    &assigned->session_timeout);
		/* we handle this session-timeout */
		radius_del_attr_all(radres, RADIUS_TYPE_SESSION_TIMEOUT);
	}

 accept:
	if (module_resdeco_done(self->base, q_id, radius_get_data(radres),
	    radius_get_length(radres)) == -1) {
		log_warn("%s: module_resdeco_done() failed", __func__);
		module_stop(self->base);
	}
	if (radreq != NULL)
		radius_delete_packet(radreq);
	radius_delete_packet(radres);
	return;
 reject:
	ipcp_reject(self, radreq, q_id, radres, msraserr);
	radius_delete_packet(radreq);
	radius_delete_packet(radres);
	return;
 fatal:
	if (radreq != NULL)
		radius_delete_packet(radreq);
	if (radres != NULL)
		radius_delete_packet(radres);
	module_stop(self->base);
}

void
ipcp_reject(struct module_ipcp *self, RADIUS_PACKET *reqp, unsigned int q_id,
    RADIUS_PACKET *orig_resp, int mserr)
{
	bool			 is_eap, is_mschap, is_mschap2;
	uint8_t			 attr[256];
	size_t			 attrlen;
	RADIUS_PACKET		*resp;
	struct {
		uint8_t		 code;
		uint8_t		 id;
		uint16_t	 length;
	} __packed		 eap;

	resp = radius_new_response_packet(RADIUS_CODE_ACCESS_REJECT, reqp);
	if (resp == NULL) {
		log_warn("%s: radius_new_response_packet() failed", __func__);
		module_accsreq_aborted(self->base, q_id);
		return;
	}

	is_eap = radius_has_attr(reqp, RADIUS_TYPE_EAP_MESSAGE);
	if (radius_get_vs_raw_attr(reqp, RADIUS_VENDOR_MICROSOFT,
	    RADIUS_VTYPE_MS_CHAP_RESPONSE, attr, &attrlen) == 0)
		is_mschap = true;
	else if (radius_get_vs_raw_attr(reqp, RADIUS_VENDOR_MICROSOFT,
	    RADIUS_VTYPE_MS_CHAP2_RESPONSE, attr, &attrlen) == 0)
		is_mschap2 = true;

	if (is_eap) {
		memset(&eap, 0, sizeof(eap));	/* just in case */
		eap.code = 1;	/* EAP Request */
		attrlen = sizeof(attr);
		if (orig_resp != NULL && radius_get_raw_attr(orig_resp,
		    RADIUS_TYPE_EAP_MESSAGE, &attr, &attrlen) == 0)
			eap.id = attr[1];
		else
			eap.id = 0;
		eap.length = htons(sizeof(eap));
		radius_put_raw_attr(resp, RADIUS_TYPE_EAP_MESSAGE, &eap,
		    ntohs(eap.length));
	} else if (is_mschap || is_mschap2) {
		attr[0] = attr[1];	/* Copy the ident of the request */
		snprintf(attr + 1, sizeof(attr) - 1, "E=%d R=0 V=3", mserr);
		radius_put_vs_raw_attr(resp, RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_CHAP_ERROR, attr, strlen(attr + 1) + 1);
	}

	module_resdeco_done(self->base, q_id, radius_get_data(resp),
	    radius_get_length(resp));
	radius_delete_packet(resp);
}

/***********************************************************************
 * RADIUS Accounting
 ***********************************************************************/
void
ipcp_accounting_request(void *ctx, u_int q_id, const u_char *pkt,
    size_t pktlen)
{
	RADIUS_PACKET		*radpkt = NULL;
	int			 code, af;
	uint32_t		 type, delay, uval;
	struct in_addr		 addr4, nas_ipv4;
	struct in6_addr		 nas_ipv6, ipv6_zero;
	struct module_ipcp	*self = ctx;
	struct assigned_ipv4	*assign, *assignt;
	char			 username[256], nas_id[256], buf[256],
				    buf1[384];
	struct timespec		 dur;
	struct radiusd_ipcp_statistics
				 stat;
	struct module_ipcp_dae	*dae;

	ipcp_update_time(self);

	if ((radpkt = radius_convert_packet(pkt, pktlen)) == NULL) {
		log_warn("%s: radius_convert_packet() failed", __func__);
		module_stop(self->base);
		return;
	}
	code = radius_get_code(radpkt);
	if (code != RADIUS_CODE_ACCOUNTING_REQUEST &&
	    code != RADIUS_CODE_ACCOUNTING_RESPONSE)
		goto out;

	if (radius_get_uint32_attr(radpkt, RADIUS_TYPE_ACCT_STATUS_TYPE, &type)
	    != 0)
		goto out;

	/* identifier for the NAS */
	memset(&ipv6_zero, 0, sizeof(ipv6_zero));
	memset(&nas_ipv4, 0, sizeof(nas_ipv4));
	memset(&nas_ipv6, 0, sizeof(nas_ipv6));
	memset(&nas_id, 0, sizeof(nas_id));

	radius_get_ipv4_attr(radpkt, RADIUS_TYPE_NAS_IP_ADDRESS, &nas_ipv4);
	radius_get_ipv6_attr(radpkt, RADIUS_TYPE_NAS_IPV6_ADDRESS, &nas_ipv6);
	radius_get_string_attr(radpkt, RADIUS_TYPE_NAS_IDENTIFIER, nas_id,
	    sizeof(nas_id));

	if (nas_ipv4.s_addr == 0 && IN6_ARE_ADDR_EQUAL(&nas_ipv6, &ipv6_zero) &&
	    nas_id[0] == '\0') {
		log_warnx("q=%u no NAS-IP-Address, NAS-IPV6-Address, or "
		    "NAS-Identifier", q_id);
		goto out;
	}

	if (type == RADIUS_ACCT_STATUS_TYPE_ACCT_ON ||
	    type == RADIUS_ACCT_STATUS_TYPE_ACCT_OFF) {
		/*
		 * NAS or daemon is restarted.  Delete all assigned records
		 * from it
		 */
		RB_FOREACH_SAFE(assign, assigned_ipv4_tree, &self->ipv4s,
		    assignt) {
			if (assign->nas_ipv4.s_addr != nas_ipv4.s_addr ||
			    !IN6_ARE_ADDR_EQUAL(&assign->nas_ipv6, &nas_ipv6) ||
			    strcmp(assign->nas_id, nas_id) != 0)
				continue;
			log_info("q=%u Delete record for %s", q_id,
			    inet_ntop(AF_INET, &assign->ipv4, buf,
			    sizeof(buf)));
			ipcp_ipv4_delete(self, assign,
			    (type == RADIUS_ACCT_STATUS_TYPE_ACCT_ON)
			    ? "Receive Acct-On" : "Receive Acct-Off");
		}
		return;
	}

	if (radius_get_ipv4_attr(radpkt, RADIUS_TYPE_FRAMED_IP_ADDRESS, &addr4)
	    != 0) {
		log_warnx("q=%u no Framed-IP-Address-Address attribute", q_id);
		goto out;
	}
	if (radius_get_string_attr(radpkt, RADIUS_TYPE_USER_NAME, username,
	    sizeof(username)) != 0) {
		log_warnx("q=%u no User-Name attribute", q_id);
		goto out;
	}
	if ((assign = ipcp_ipv4_find(self, addr4)) == NULL) {
		/* not assigned by this */
		log_warnx("q=%u %s is not assigned by us", q_id,
		    inet_ntop(AF_INET, &addr4, buf, sizeof(buf)));
		goto out;
	}

	if (radius_get_uint32_attr(radpkt, RADIUS_TYPE_ACCT_DELAY_TIME, &delay)
	    != 0)
		delay = 0;

	if (type == RADIUS_ACCT_STATUS_TYPE_START) {
		assign->start = self->uptime;
		assign->start.tv_sec -= delay;

		if (!self->no_session_timeout && (self->session_timeout > 0 ||
		    assign->session_timeout > 0)) {
			assign->timeout = assign->start;
			/* prefer the value from the RADIUS attribute */
			if (assign->session_timeout > 0)
				assign->timeout.tv_sec +=
				    assign->session_timeout;
			else
				assign->timeout.tv_sec += self->session_timeout;
		}
		assign->nas_ipv4 = nas_ipv4;
		assign->nas_ipv6 = nas_ipv6;
		strlcpy(assign->nas_id, nas_id, sizeof(assign->nas_id));

		if (radius_get_string_attr(radpkt, RADIUS_TYPE_ACCT_SESSION_ID,
		    assign->session_id, sizeof(assign->session_id)) != 0)
			assign->session_id[0] = '\0';
		if (radius_get_uint32_attr(radpkt, RADIUS_TYPE_TUNNEL_TYPE,
		    &uval) == 0)
			assign->tun_type = radius_tunnel_type_string(uval,
			    NULL);

		/*
		 * Get "tunnel from" from Tunnel-Client-Endpoint or Calling-
		 * Station-Id
		 */
		af = AF_UNSPEC;
		if (radius_get_string_attr(radpkt,
		    RADIUS_TYPE_TUNNEL_CLIENT_ENDPOINT, buf, sizeof(buf)) == 0)
		    {
			if (radius_get_uint32_attr(radpkt,
			    RADIUS_TYPE_TUNNEL_MEDIUM_TYPE, &uval) == 0) {
				if (uval == RADIUS_TUNNEL_MEDIUM_TYPE_IPV4)
					af = AF_INET;
				else if (uval == RADIUS_TUNNEL_MEDIUM_TYPE_IPV6)
					af = AF_INET6;
			}
			parse_addr(buf, af, (struct sockaddr *)
			    &assign->tun_client, sizeof(assign->tun_client));
		}
		if (assign->tun_client.sin4.sin_family == 0 &&
		    radius_get_string_attr(radpkt,
		    RADIUS_TYPE_CALLING_STATION_ID, buf, sizeof(buf)) == 0)
			parse_addr(buf, af, (struct sockaddr *)
			    &assign->tun_client, sizeof(assign->tun_client));

		TAILQ_FOREACH(dae, &self->daes, next) {
			if (dae->nas_id[0] == '\0' ||
			    strcmp(dae->nas_id, assign->nas_id) == 0)
				break;
		}
		assign->dae = dae;

		ipcp_put_db(self, assign);
		ipcp_schedule_timer(self);

		if (ipcp_notice_startstop(self, assign, 1, NULL) != 0)
			goto fail;
		log_info("q=%u Start seq=%u user=%s duration=%dsec "
		    "session=%s tunnel=%s from=%s auth=%s ip=%s", q_id,
		    assign->seq, assign->user->name, delay, assign->session_id,
		    (assign->tun_type != NULL)? assign->tun_type : "",
		    print_addr((struct sockaddr *)&assign->tun_client, buf1,
		    sizeof(buf1)), assign->auth_method, inet_ntop(AF_INET,
		    &addr4, buf,
		    sizeof(buf)));
	} else if (type == RADIUS_ACCT_STATUS_TYPE_STOP) {
		memset(&stat, 0, sizeof(stat));

		dur = self->uptime;
		dur.tv_sec -= delay;
		timespecsub(&dur, &assign->start, &dur);

		if (radius_get_uint32_attr(radpkt,
		    RADIUS_TYPE_ACCT_INPUT_OCTETS, &uval) == 0)
			stat.ibytes = uval;
		if (radius_get_uint32_attr(radpkt,
		    RADIUS_TYPE_ACCT_INPUT_GIGAWORDS, &uval) == 0)
			stat.ibytes = ((uint64_t)uval << 32) | stat.ibytes;
		if (radius_get_uint32_attr(radpkt,
		    RADIUS_TYPE_ACCT_OUTPUT_OCTETS, &uval) == 0)
			stat.obytes = uval;
		if (radius_get_uint32_attr(radpkt,
		    RADIUS_TYPE_ACCT_OUTPUT_GIGAWORDS, &uval) == 0)
			stat.obytes = ((uint64_t)uval << 32) | stat.obytes;
		radius_get_uint32_attr(radpkt, RADIUS_TYPE_ACCT_INPUT_PACKETS,
		    &stat.ipackets);
		radius_get_uint32_attr(radpkt, RADIUS_TYPE_ACCT_OUTPUT_PACKETS,
		    &stat.opackets);

		if (radius_get_uint32_attr(radpkt,
		    RADIUS_TYPE_ACCT_TERMINATE_CAUSE, &uval) == 0)
			strlcpy(stat.cause, radius_terminate_cause_string(uval),
			    sizeof(stat.cause));

		log_info("q=%u Stop seq=%u user=%s duration=%lldsec "
		    "session=%s tunnel=%s from=%s auth=%s ip=%s "
		    "datain=%"PRIu64"bytes,%" PRIu32"packets dataout=%"PRIu64
		    "bytes,%"PRIu32"packets cause=\"%s\"", q_id,
		    assign->seq, assign->user->name, dur.tv_sec,
		    assign->session_id, (assign->tun_type != NULL)?
		    assign->tun_type : "", print_addr((struct sockaddr *)
		    &assign->tun_client, buf1, sizeof(buf1)),
		    assign->auth_method, inet_ntop(AF_INET, &addr4, buf,
		    sizeof(buf)), stat.ibytes, stat.ipackets, stat.obytes,
		    stat.opackets, stat.cause);

		ipcp_del_db(self, assign);
		if (ipcp_notice_startstop(self, assign, 0, &stat) != 0)
			goto fail;
		ipcp_ipv4_release(self, ipcp_ipv4_find(self, addr4));
	}
 out:
	radius_delete_packet(radpkt);
	return;
 fail:
	module_stop(self->base);
	radius_delete_packet(radpkt);
	return;
}

/***********************************************************************
 * On memory database to manage IP address assignment
 ***********************************************************************/
struct assigned_ipv4 *
ipcp_ipv4_assign(struct module_ipcp *self, struct user *user,
    struct in_addr ina)
{
	struct assigned_ipv4 *ip;

	ip = calloc(1, sizeof(struct assigned_ipv4));
	if (ip == NULL) {
		log_warn("%s: calloc()", __func__);
		return (NULL);
	}
	ip->ipv4 = ina;
	ip->user = user;
	ip->authtime = self->uptime;
	RB_INSERT(assigned_ipv4_tree, &self->ipv4s, ip);
	TAILQ_INSERT_TAIL(&user->ipv4s, ip, next);
	TAILQ_INIT(&ip->dae_clients);
	self->nsessions++;
	ip->seq = self->seq++;

	return (ip);
}

struct assigned_ipv4 *
ipcp_ipv4_find(struct module_ipcp *self, struct in_addr ina)
{
	struct assigned_ipv4	 key, *ret;

	key.ipv4 = ina;
	ret = RB_FIND(assigned_ipv4_tree, &self->ipv4s, &key);
	ret = ipcp_ipv4_check_valid(self, ret);
	return (ret);
}

struct assigned_ipv4 *
ipcp_ipv4_check_valid(struct module_ipcp *self, struct assigned_ipv4 *ip)
{
	struct timespec		 dif;

	if (ip != NULL && ip->start.tv_sec == 0) {
		/* not yet assigned */
		timespecsub(&self->uptime, &ip->authtime, &dif);
		if (dif.tv_sec >= self->start_wait) {
			/* assumed NAS finally didn't use the address */
			TAILQ_REMOVE(&ip->user->ipv4s, ip, next);
			RB_REMOVE(assigned_ipv4_tree, &self->ipv4s, ip);
			free(ip);
			self->nsessions--;
			return (NULL);
		}
	}

	return (ip);
}

void
ipcp_ipv4_delete(struct module_ipcp *self, struct assigned_ipv4 *assign,
    const char *cause)
{
	struct radiusd_ipcp_statistics stat;

	memset(&stat, 0, sizeof(stat));
	strlcpy(stat.cause, cause, sizeof(stat.cause));

	ipcp_del_db(self, assign);
	ipcp_notice_startstop(self, assign, 0, &stat);
	ipcp_ipv4_release(self, assign);
}

void
ipcp_ipv4_release(struct module_ipcp *self, struct assigned_ipv4 *assign)
{
	if (assign != NULL) {
		TAILQ_REMOVE(&assign->user->ipv4s, assign, next);
		RB_REMOVE(assigned_ipv4_tree, &self->ipv4s, assign);
		self->nsessions--;
		ipcp_dae_reset_request(assign);
		free(assign);
	}
}

int
assigned_ipv4_compar(struct assigned_ipv4 *a, struct assigned_ipv4 *b)
{
	if (a->ipv4.s_addr > b->ipv4.s_addr)
		return (1);
	else if (a->ipv4.s_addr < b->ipv4.s_addr)
		return (-1);
	return (0);
}

struct user *
ipcp_user_get(struct module_ipcp *self, const char *username)
{
	struct {
		struct user	 user;
		char		 name[256];
	} key;
	struct user		*elm;

	strlcpy(key.user.name, username, 256);
	elm = RB_FIND(user_tree, &self->users, &key.user);
	if (elm == NULL) {
		if ((elm = calloc(1, offsetof(struct user, name[
		    strlen(username) + 1]))) == NULL)
			return (NULL);
		memcpy(elm->name, username, strlen(username));
		RB_INSERT(user_tree, &self->users, elm);
		TAILQ_INIT(&elm->ipv4s);
	}

	return (elm);
}

int
user_compar(struct user *a, struct user *b)
{
	return (strcmp(a->name, b->name));
}

RB_GENERATE_STATIC(assigned_ipv4_tree, assigned_ipv4, tree,
    assigned_ipv4_compar);
RB_GENERATE_STATIC(user_tree, user, tree, user_compar);

/***********************************************************************
 * DB for the persistent over processes
 ***********************************************************************/
int
ipcp_prepare_db(void)
{
	struct passwd	*pw;
	DB		*db;

	if ((db = dbopen(_PATH_RADIUSD_IPCP_DB, O_CREAT | O_RDWR | O_EXLOCK,
	    0600, DB_BTREE, NULL)) == NULL)
		return (-1);
	if ((pw = getpwnam(RADIUSD_USER)) == NULL)
		return (-1);
	fchown(db->fd(db), pw->pw_uid, pw->pw_gid);
	db->close(db);

	return (0);
}

int
ipcp_restore_from_db(struct module_ipcp *self)
{
	DB			*db;
	DBT			 key, val;
	char			 keybuf[128];
	struct user		*user;
	struct radiusd_ipcp_db_record
				*record;
	struct assigned_ipv4	*assigned;
	struct in_addr		 ipv4;
	struct module_ipcp_dae	*dae;

	if ((db = dbopen(_PATH_RADIUSD_IPCP_DB, O_RDONLY | O_SHLOCK, 0600,
	    DB_BTREE, NULL)) == NULL)
		return (-1);

	key.data = "ipv4/";
	key.size = 5;
	if (db->seq(db, &key, &val, R_CURSOR) == 0) {
		do {
			if (key.size >= sizeof(keybuf))
				break;
			memcpy(keybuf, key.data, key.size);
			keybuf[key.size] = '\0';
			if (strncmp(keybuf, "ipv4/", 5) != 0)
				break;
			inet_pton(AF_INET, keybuf + 5, &ipv4);
			record = (struct radiusd_ipcp_db_record *)val.data;
			if ((user = ipcp_user_get(self, record->username))
			    == NULL)
				return (-1);
			if ((assigned = ipcp_ipv4_assign(self, user, ipv4))
			    == NULL)
				return (-1);
			assigned->seq = record->seq;
			self->seq = MAXIMUM(assigned->seq + 1, self->seq);
			strlcpy(assigned->auth_method, record->auth_method,
			    sizeof(assigned->auth_method));
			strlcpy(assigned->session_id, record->session_id,
			    sizeof(assigned->session_id));
			assigned->start = record->start;
			assigned->timeout = record->timeout;
			assigned->nas_ipv4 = record->nas_ipv4;
			assigned->nas_ipv6 = record->nas_ipv6;
			strlcpy(assigned->nas_id, record->nas_id,
			    sizeof(assigned->nas_id));
			assigned->tun_type = radius_tunnel_type_string(0,
			    record->tun_type);
			memcpy(&assigned->tun_client, &record->tun_client,
			    sizeof(assigned->tun_client));

			TAILQ_FOREACH(dae, &self->daes, next) {
				if (dae->nas_id[0] == '\0' ||
				    strcmp(dae->nas_id, assigned->nas_id) == 0)
					break;
			}
			assigned->dae = dae;
		} while (db->seq(db, &key, &val, R_NEXT) == 0);
	}
	db->close(db);

	return (0);
}

void
ipcp_put_db(struct module_ipcp *self, struct assigned_ipv4 *assigned)
{
	DB			*db;
	DBT			 key, val;
	char			 keybuf[128];
	struct radiusd_ipcp_db_record
				 record;

	memset(&record, 0, sizeof(record));
	strlcpy(keybuf, "ipv4/", sizeof(keybuf));
	inet_ntop(AF_INET, &assigned->ipv4, keybuf + 5, sizeof(keybuf) - 5);
	key.data = keybuf;
	key.size = strlen(keybuf);
	strlcpy(record.session_id, assigned->session_id,
	    sizeof(record.session_id));
	strlcpy(record.auth_method, assigned->auth_method,
	    sizeof(record.auth_method));
	strlcpy(record.username, assigned->user->name, sizeof(record.username));
	record.seq = assigned->seq;
	record.start = assigned->start;
	record.timeout = assigned->timeout;
	record.nas_ipv4 = assigned->nas_ipv4;
	record.nas_ipv6 = assigned->nas_ipv6;
	strlcpy(record.nas_id, assigned->nas_id, sizeof(record.nas_id));
	if (assigned->tun_type != NULL)
		strlcpy(record.tun_type, assigned->tun_type,
		    sizeof(record.tun_type));
	memcpy(&record.tun_client, &assigned->tun_client,
	    sizeof(record.tun_client));

	val.data = &record;
	val.size = sizeof(record);
	if ((db = dbopen(_PATH_RADIUSD_IPCP_DB, O_RDWR | O_EXLOCK, 0600,
	    DB_BTREE, NULL)) == NULL)
		return;
	db->put(db, &key, &val, 0);
	db->close(db);
}

void
ipcp_del_db(struct module_ipcp *self, struct assigned_ipv4 *assigned)
{
	DB			*db;
	DBT			 key;
	char			 keybuf[128];

	strlcpy(keybuf, "ipv4/", sizeof(keybuf));
	inet_ntop(AF_INET, &assigned->ipv4, keybuf + 5, sizeof(keybuf) - 5);
	key.data = keybuf;
	key.size = strlen(keybuf);

	if ((db = dbopen(_PATH_RADIUSD_IPCP_DB, O_RDWR | O_EXLOCK, 0600,
	    DB_BTREE, NULL)) == NULL)
		return;
	db->del(db, &key, 0);
	db->close(db);
}

void
ipcp_db_dump_fill_record(struct radiusd_ipcp_db_dump *dump, int idx,
    struct assigned_ipv4 *assign)
{
	dump->records[idx].af = AF_INET;
	dump->records[idx].addr.ipv4 = assign->ipv4;
	dump->records[idx].rec.seq = assign->seq;
	strlcpy(dump->records[idx].rec.session_id, assign->session_id,
	    sizeof(dump->records[idx].rec.session_id));
	strlcpy(dump->records[idx].rec.auth_method, assign->auth_method,
	    sizeof(dump->records[idx].rec.auth_method));
	strlcpy(dump->records[idx].rec.username, assign->user->name,
	    sizeof(dump->records[idx].rec.username));
	dump->records[idx].rec.start = assign->start;
	dump->records[idx].rec.timeout = assign->timeout;
	dump->records[idx].rec.nas_ipv4 = assign->nas_ipv4;
	dump->records[idx].rec.nas_ipv6 = assign->nas_ipv6;
	strlcpy(dump->records[idx].rec.nas_id, assign->nas_id,
	    sizeof(dump->records[idx].rec.nas_id));
	if (assign->tun_type != NULL)
		strlcpy(dump->records[idx].rec.tun_type, assign->tun_type,
		    sizeof(dump->records[idx].rec.tun_type));
	memcpy(&dump->records[idx].rec.tun_client, &assign->tun_client,
	    sizeof(dump->records[idx].rec.tun_client));
}

/***********************************************************************
 * Timer
 ***********************************************************************/
void
ipcp_update_time(struct module_ipcp *self)
{
	clock_gettime(CLOCK_BOOTTIME, &self->uptime);
}

void
ipcp_on_timer(int fd, short ev, void *ctx)
{
	struct module_ipcp *self = ctx;

	ipcp_update_time(self);
	ipcp_schedule_timer(self);
}

void
ipcp_schedule_timer(struct module_ipcp *self)
{
	struct assigned_ipv4	*assign, *min_assign = NULL;
	struct timespec		 tsd;
	struct timeval		 tv;

	/* check session timeout */
	RB_FOREACH(assign, assigned_ipv4_tree, &self->ipv4s) {
		if (assign->timeout.tv_sec == 0)
			continue;
		if (timespeccmp(&assign->timeout, &self->uptime, <=)) {
			log_info("Reached session timeout seq=%u", assign->seq);
			ipcp_dae_send_disconnect_request(assign);
			memset(&assign->timeout, 0, sizeof(assign->timeout));
			ipcp_put_db(self, assign);
		}
		if (min_assign == NULL ||
		    timespeccmp(&min_assign->timeout, &assign->timeout, >))
			min_assign = assign;
	}
	if (evtimer_pending(&self->ev_timer, NULL))
		evtimer_del(&self->ev_timer);

	if (min_assign != NULL) {
		timespecsub(&min_assign->timeout, &self->uptime, &tsd);
		TIMESPEC_TO_TIMEVAL(&tv, &tsd);
		evtimer_set(&self->ev_timer, ipcp_on_timer, self);
		evtimer_add(&self->ev_timer, &tv);
	}
}

/***********************************************************************
 * Dynamic Authorization Extension for RAIDUS (RFC 5176)
 ***********************************************************************/
static const int dae_request_timeouts[] = { 2, 4, 8, 8 };

void
ipcp_dae_send_disconnect_request(struct assigned_ipv4 *assign)
{
	RADIUS_PACKET		*reqpkt = NULL;
	struct timeval		 tv;
	char			 buf[80];

	if (assign->dae == NULL)
		return;		/* DAE is not configured */

	if (assign->dae_reqpkt == NULL) {
		if ((reqpkt = radius_new_request_packet(
		    RADIUS_CODE_DISCONNECT_REQUEST)) == NULL) {
			log_warn("%s: radius_new_request_packet(): %m",
			    __func__);
			return;
		}
		radius_put_string_attr(reqpkt, RADIUS_TYPE_ACCT_SESSION_ID,
		    assign->session_id);
		/*
		 * RFC 5176 Section 3, "either the User-Name or
		 * Chargeable-User-Identity attribute SHOULD be present in
		 * Disconnect-Request and CoA-Request packets."
		 */
		radius_put_string_attr(reqpkt, RADIUS_TYPE_USER_NAME,
		    assign->user->name);
		if (assign->nas_id[0] != '\0')
			radius_put_string_attr(reqpkt,
			    RADIUS_TYPE_NAS_IDENTIFIER, assign->nas_id);
		if (ntohl(assign->nas_ipv4.s_addr) != 0)
			radius_put_ipv4_attr(reqpkt,
			    RADIUS_TYPE_NAS_IP_ADDRESS, assign->nas_ipv4);
		if (!IN6_IS_ADDR_UNSPECIFIED(&assign->nas_ipv6))
			radius_put_ipv6_attr(reqpkt,
			    RADIUS_TYPE_NAS_IPV6_ADDRESS, &assign->nas_ipv6);
		radius_set_accounting_request_authenticator(reqpkt,
		    assign->dae->secret);
		assign->dae_reqpkt = reqpkt;
		TAILQ_INSERT_TAIL(&assign->dae->reqs, assign, dae_next);
	}

	if (assign->dae_ntry == 0) {
		if (assign->dae->ninflight >= RADIUSD_IPCP_DAE_MAX_INFLIGHT)
			return;
		log_info("Sending Disconnect-Request seq=%u to %s",
		    assign->seq, print_addr((struct sockaddr *)
		    &assign->dae->nas_addr, buf, sizeof(buf)));
	}

	if (radius_send(assign->dae->sock, assign->dae_reqpkt, 0) < 0)
		log_warn("%s: sendto: %m", __func__);

	tv.tv_sec = dae_request_timeouts[assign->dae_ntry];
	tv.tv_usec = 0;
	evtimer_set(&assign->dae_evtimer, ipcp_dae_request_on_timeout, assign);
	evtimer_add(&assign->dae_evtimer, &tv);
	if (assign->dae_ntry == 0)
		assign->dae->ninflight++;
	assign->dae_ntry++;
}

void
ipcp_dae_request_on_timeout(int fd, short ev, void *ctx)
{
	struct assigned_ipv4	*assign = ctx;
	char			 buf[80];
	struct radiusctl_client	*client;

	if (assign->dae_ntry >= (int)nitems(dae_request_timeouts)) {
		log_warnx("No answer for Disconnect-Request seq=%u from %s",
		    assign->seq, print_addr((struct sockaddr *)
		    &assign->dae->nas_addr, buf, sizeof(buf)));
		TAILQ_FOREACH(client, &assign->dae_clients, entry)
			module_imsg_compose(assign->dae->ipcp->base, IMSG_NG,
			    client->peerid, 0, -1, NULL, 0);
		ipcp_dae_reset_request(assign);
	} else
		ipcp_dae_send_disconnect_request(assign);
}

void
ipcp_dae_on_event(int fd, short ev, void *ctx)
{
	struct module_ipcp_dae	*dae = ctx;
	struct module_ipcp	*self = dae->ipcp;
	RADIUS_PACKET		*radres = NULL;
	int			 code;
	uint32_t		 u32;
	struct assigned_ipv4	*assign;
	char			 buf[80], causestr[80];
	const char		*cause = "";
	struct radiusctl_client	*client;

	ipcp_update_time(self);

	if ((ev & EV_READ) == 0)
		return;

	if ((radres = radius_recv(dae->sock, 0)) == NULL) {
		if (errno == EAGAIN)
			return;
		log_warn("%s: Failed to receive from %s", __func__, print_addr(
		    (struct sockaddr *)&dae->nas_addr, buf, sizeof(buf)));
		return;
	}
	TAILQ_FOREACH(assign, &dae->reqs, dae_next) {
		if (radius_get_id(assign->dae_reqpkt) == radius_get_id(radres))
			break;
	}
	if (assign == NULL) {
		log_warnx("%s: Received RADIUS packet from %s has unknown "
		    "id=%d", __func__, print_addr((struct sockaddr *)
		    &dae->nas_addr, buf, sizeof(buf)), radius_get_id(radres));
		goto out;
	}

	radius_set_request_packet(radres, assign->dae_reqpkt);
	if ((radius_check_response_authenticator(radres, dae->secret)) != 0) {
		log_warnx("%s: Received RADIUS packet for seq=%u from %s has "
		    "a bad authenticator", __func__, assign->seq, print_addr(
			(struct sockaddr *)&dae->nas_addr, buf,
		    sizeof(buf)));
		goto out;
	}
	causestr[0] = '\0';
	if (radius_get_uint32_attr(radres, RADIUS_TYPE_ERROR_CAUSE, &u32) == 0){
		cause = radius_error_cause_string(u32);
		if (cause != NULL)
			snprintf(causestr, sizeof(causestr), " cause=%u(%s)",
			    u32, cause);
		else
			snprintf(causestr, sizeof(causestr), " cause=%u", u32);
		cause = causestr;
	}

	code = radius_get_code(radres);
	switch (code) {
	case RADIUS_CODE_DISCONNECT_ACK:
		log_info("Received Disconnect-ACK for seq=%u from %s%s",
		    assign->seq, print_addr((struct sockaddr *)
		    &dae->nas_addr, buf, sizeof(buf)), cause);
		break;
	case RADIUS_CODE_DISCONNECT_NAK:
		log_info("Received Disconnect-NAK for seq=%u from %s%s",
		    assign->seq, print_addr((struct sockaddr *)
		    &dae->nas_addr, buf, sizeof(buf)), cause);
		break;
	default:
		log_warn("%s: Received unknown code=%d for id=%u from %s",
		    __func__, code, assign->seq, print_addr((struct sockaddr *)
		    &dae->nas_addr, buf, sizeof(buf)));
		break;
	}

	TAILQ_FOREACH(client, &assign->dae_clients, entry) {
		if (*cause != '\0')
			module_imsg_compose(self->base,
			    (code == RADIUS_CODE_DISCONNECT_ACK)
			    ? IMSG_OK : IMSG_NG, client->peerid, 0, -1,
			    cause + 1, strlen(cause + 1) + 1);
		else
			module_imsg_compose(self->base,
			    (code == RADIUS_CODE_DISCONNECT_ACK)
			    ? IMSG_OK : IMSG_NG, client->peerid, 0, -1,
			    NULL, 0);
	}
	ipcp_dae_reset_request(assign);
 out:
	if (radres != NULL)
		radius_delete_packet(radres);
}

void
ipcp_dae_reset_request(struct assigned_ipv4 *assign)
{
	struct radiusctl_client		*client, *clientt;
	const struct timeval		 zero = { 0, 0 };

	if (assign->dae != NULL) {
		if (assign->dae_reqpkt != NULL)
			TAILQ_REMOVE(&assign->dae->reqs, assign, dae_next);
		if (assign->dae_ntry > 0) {
			assign->dae->ninflight--;
			if (!evtimer_pending(&assign->dae->ev_reqs, NULL))
				evtimer_add(&assign->dae->ev_reqs, &zero);
		}
	}
	if (assign->dae_reqpkt != NULL)
		radius_delete_packet(assign->dae_reqpkt);
	assign->dae_reqpkt = NULL;
	if (evtimer_pending(&assign->dae_evtimer, NULL))
		evtimer_del(&assign->dae_evtimer);
	TAILQ_FOREACH_SAFE(client, &assign->dae_clients, entry, clientt) {
		TAILQ_REMOVE(&assign->dae_clients, client, entry);
		free(client);
	}
	assign->dae_ntry = 0;
}

void
ipcp_dae_send_pending_requests(int fd, short ev, void *ctx)
{
	struct module_ipcp_dae	*dae = ctx;
	struct module_ipcp	*self = dae->ipcp;
	struct assigned_ipv4	*assign, *assignt;

	ipcp_update_time(self);

	TAILQ_FOREACH_SAFE(assign, &dae->reqs, dae_next, assignt) {
		if (dae->ninflight >= RADIUSD_IPCP_DAE_MAX_INFLIGHT)
			break;
		if (assign->dae_ntry == 0)	/* pending */
			ipcp_dae_send_disconnect_request(assign);
	}
}

/***********************************************************************
 * Miscellaneous functions
 ***********************************************************************/
struct ipcp_address *
parse_address_range(const char *range)
{
	char			*buf, *sep;
	int			 masklen;
	uint32_t		 mask;
	struct in_addr		 start, end;
	struct ipcp_address	*ret;
	const char		*errstr;

	buf = strdup(range);
	if (buf == NULL)
		goto error;
	if ((sep = strchr(buf, '-')) != NULL) {
		*sep = '\0';
		if (inet_pton(AF_INET, buf, &start) != 1)
			goto error;
		else if (inet_pton(AF_INET, ++sep, &end) != 1)
			goto error;
		start.s_addr = ntohl(start.s_addr);
		end.s_addr = ntohl(end.s_addr);
		if (end.s_addr < start.s_addr)
			goto error;
	} else {
		if ((sep = strchr(buf, '/')) != NULL) {
			*sep = '\0';
			if (inet_pton(AF_INET, buf, &start) != 1)
				goto error;
			masklen = strtonum(++sep, 0, 32, &errstr);
			if (errstr != NULL)
				goto error;
		} else {
			if (inet_pton(AF_INET, buf, &start) != 1)
				goto error;
			masklen = 32;
		}
		mask = 0xFFFFFFFFUL;
		if (masklen < 32)
			mask <<= (32 - masklen);
		start.s_addr = ntohl(start.s_addr) & mask;
		if (masklen == 32)
			end = start;
		else if (masklen == 31)
			end.s_addr = start.s_addr + 1;
		else {
			end.s_addr = start.s_addr + (1 << (32 - masklen)) - 2;
			start.s_addr = start.s_addr + 1;
		}
	}
	free(buf);
	if ((ret = calloc(1, sizeof(struct ipcp_address))) == NULL)
		return (NULL);
	ret->start = start;
	ret->end = end;
	ret->naddrs = end.s_addr - start.s_addr + 1;
	return (ret);
 error:
	free(buf);
	return (NULL);
}

const char *
radius_tunnel_type_string(unsigned val, const char *label)
{
	unsigned int		 i;
	struct {
		const unsigned	 constval;
		const char	*label;
	} tunnel_types[] = {
		{ RADIUS_TUNNEL_TYPE_PPTP,	"PPTP" },
		{ RADIUS_TUNNEL_TYPE_L2F,	"L2F" },
		{ RADIUS_TUNNEL_TYPE_L2TP,	"L2TP" },
		{ RADIUS_TUNNEL_TYPE_ATMP,	"ATMP" },
		{ RADIUS_TUNNEL_TYPE_VTP,	"VTP" },
		{ RADIUS_TUNNEL_TYPE_AH,	"AH" },
		{ RADIUS_TUNNEL_TYPE_IP,	"IP" },
		{ RADIUS_TUNNEL_TYPE_MOBILE,	"MIN-IP-IP" },
		{ RADIUS_TUNNEL_TYPE_ESP,	"ESP" },
		{ RADIUS_TUNNEL_TYPE_GRE,	"GRE" },
		{ RADIUS_TUNNEL_TYPE_VDS,	"DVS" },
		/* [MS-RNAS] 3.3.5.1.9 Tunnel-Type */
		{ RADIUS_VENDOR_MICROSOFT << 8 | 1,
						"SSTP" }
	};

	if (label != NULL) {	/* for conversion to the const value */
		for (i = 0; i < nitems(tunnel_types); i++) {
			if (strcmp(tunnel_types[i].label, label) == 0)
				return (tunnel_types[i].label);
		}
	}

	for (i = 0; i < nitems(tunnel_types); i++) {
		if (tunnel_types[i].constval == val)
			return (tunnel_types[i].label);
	}

	return (NULL);
}

const char *
radius_terminate_cause_string(unsigned val)
{
	unsigned int		 i;
	struct {
		const unsigned	 constval;
		const char	*label;
	} terminate_causes[] = {
	    { RADIUS_TERMNATE_CAUSE_USER_REQUEST,	"User Request" },
	    { RADIUS_TERMNATE_CAUSE_LOST_CARRIER,	"Lost Carrier" },
	    { RADIUS_TERMNATE_CAUSE_LOST_SERVICE,	"Lost Service" },
	    { RADIUS_TERMNATE_CAUSE_IDLE_TIMEOUT,	"Idle Timeout" },
	    { RADIUS_TERMNATE_CAUSE_SESSION_TIMEOUT,	"Session Timeout" },
	    { RADIUS_TERMNATE_CAUSE_ADMIN_RESET,	"Admin Reset" },
	    { RADIUS_TERMNATE_CAUSE_ADMIN_REBOOT,	"Admin Reboot" },
	    { RADIUS_TERMNATE_CAUSE_PORT_ERROR,		"Port Error" },
	    { RADIUS_TERMNATE_CAUSE_NAS_ERROR,		"NAS Error" },
	    { RADIUS_TERMNATE_CAUSE_NAS_RESET,		"NAS Request" },
	    { RADIUS_TERMNATE_CAUSE_NAS_REBOOT,		"NAS Reboot" },
	    { RADIUS_TERMNATE_CAUSE_PORT_UNNEEDED,	"Port Unneeded" },
	    { RADIUS_TERMNATE_CAUSE_PORT_PREEMPTED,	"Port Preempted" },
	    { RADIUS_TERMNATE_CAUSE_PORT_SUSPENDED,	"Port Suspended" },
	    { RADIUS_TERMNATE_CAUSE_SERVICE_UNAVAIL,	"Service Unavailable" },
	    { RADIUS_TERMNATE_CAUSE_CALLBACK,		"Callback" },
	    { RADIUS_TERMNATE_CAUSE_USER_ERROR,		"User Error" },
	    { RADIUS_TERMNATE_CAUSE_HOST_REQUEST,	"Host Request" },
	};

	for (i = 0; i < nitems(terminate_causes); i++) {
		if (terminate_causes[i].constval == val)
			return (terminate_causes[i].label);
	}

	return (NULL);
}

const char *
radius_error_cause_string(unsigned val)
{
	unsigned int		 i;
	struct {
		const unsigned	 constval;
		const char	*label;
	} error_causes[] = {
	    { RADIUS_ERROR_CAUSE_RESIDUAL_SESSION_REMOVED,
	      "Residual Session Context Removed" },
	    { RADIUS_ERROR_CAUSE_INVALID_EAP_PACKET,
	      "Invalid EAP Packet (Ignored)" },
	    { RADIUS_ERROR_CAUSE_UNSUPPORTED_ATTRIBUTE,
	      "Unsupported Attribute" },
	    { RADIUS_ERROR_CAUSE_MISSING_ATTRIBUTE,
	      "Missing Attribute" },
	    { RADIUS_ERROR_CAUSE_NAS_IDENTIFICATION_MISMATCH,
	      "NAS Identification Mismatch" },
	    { RADIUS_ERROR_CAUSE_INVALID_REQUEST,
	      "Invalid Request" },
	    { RADIUS_ERROR_CAUSE_UNSUPPORTED_SERVICE,
	      "Unsupported Service" },
	    { RADIUS_ERROR_CAUSE_UNSUPPORTED_EXTENSION,
	      "Unsupported Extension" },
	    { RADIUS_ERROR_CAUSE_INVALID_ATTRIBUTE_VALUE,
	      "Invalid Attribute Value" },
	    { RADIUS_ERROR_CAUSE_ADMINISTRATIVELY_PROHIBITED,
	      "Administratively Prohibited" },
	    { RADIUS_ERROR_CAUSE_REQUEST_NOT_ROUTABLE,
	      "Request Not Routable (Proxy)" },
	    { RADIUS_ERROR_CAUSE_SESSION_NOT_FOUND,
	      "Session Context Not Found" },
	    { RADIUS_ERROR_CAUSE_SESSION_NOT_REMOVABLE,
	      "Session Context Not Removable" },
	    { RADIUS_ERROR_CAUSE_OTHER_PROXY_PROCESSING_ERROR,
	      "Other Proxy Processing Error" },
	    { RADIUS_ERROR_CAUSE_RESOURCES_UNAVAILABLE,
	      "Resources Unavailable" },
	    { RADIUS_ERROR_CAUSE_REQUEST_INITIATED,
	      "equest Initiated" },
	    { RADIUS_ERROR_CAUSE_MULTI_SELECTION_UNSUPPORTED,
	      "Multiple Session Selection Unsupported" }
	};

	for (i = 0; i < nitems(error_causes); i++) {
		if (error_causes[i].constval == val)
			return (error_causes[i].label);
	}

	return (NULL);
}

int
parse_addr(const char *str0, int af, struct sockaddr *sa, socklen_t salen)
{
	int		 error;
	char		*str, *end, *colon, *colon0, *addr = NULL, *port = NULL;
	char		*sb, *sb0;
	struct addrinfo	 hints, *ai;

	if ((str = strdup(str0)) == NULL)
		return (-1);
	if (*str == '[' && (end = strchr(str + 1, ']')) != NULL) {
		addr = str + 1;
		*end = '\0';
		if (*(end + 1) == ':')
			port = end + 2;
		else if (*(end + 1) == '[' && (sb = strrchr(end + 2, ']'))
		    != NULL) {
			port = end + 2;
			*sb = '\0';
		}
	} else if ((sb0 = strchr(str, '[')) != NULL &&
	    (sb = strrchr(sb0 + 1, ']')) != NULL && sb0 < sb) {
		addr = str;
		*sb0 = '\0';
		port = sb0 + 1;
		*sb = '\0';
	} else if ((colon0 = strchr(str, ':')) != NULL &&
	    (colon = strrchr(str, ':')) != NULL && colon0 == colon) {
		/* has one : */
		addr = str;
		*colon = '\0';
		port = colon + 1;
	} else {
		addr = str;
		port = NULL;
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = af;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_NUMERICHOST;
	if (port != NULL)
		hints.ai_flags |= AI_NUMERICSERV;
	if ((error = getaddrinfo(addr, port, &hints, &ai)) != 0) {
		free(str);
		return (-1);
	}
	free(str);
	if (salen < ai->ai_addrlen) {
		freeaddrinfo(ai);
		return (-1);
	}
	memcpy(sa, ai->ai_addr, ai->ai_addrlen);
	freeaddrinfo(ai);

	return (0);
}

const char *
print_addr(struct sockaddr *sa, char *buf, size_t bufsiz)
{
	int	noport, ret;
	char	hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

	if (ntohs(((struct sockaddr_in *)sa)->sin_port) == 0) {
		noport = 1;
		ret = getnameinfo(sa, sa->sa_len, hbuf, sizeof(hbuf), NULL, 0,
		    NI_NUMERICHOST);
	} else {
		noport = 0;
		ret = getnameinfo(sa, sa->sa_len, hbuf, sizeof(hbuf), sbuf,
		    sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
	}
	if (ret != 0)
		return "";
	if (noport)
		strlcpy(buf, hbuf, bufsiz);
	else if (sa->sa_family == AF_INET6)
		snprintf(buf, bufsiz, "[%s]:%s", hbuf, sbuf);
	else
		snprintf(buf, bufsiz, "%s:%s", hbuf, sbuf);

	return (buf);
}
