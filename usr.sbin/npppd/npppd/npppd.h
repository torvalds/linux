/*	$OpenBSD: npppd.h,v 1.21 2024/07/11 14:05:59 yasuoka Exp $ */

/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef	NPPPD_H
#define	NPPPD_H 1


#include <sys/queue.h>
#include <sys/socket.h>
#include <stdbool.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <event.h>
#include <limits.h>

#include "addr_range.h"

#include "l2tp_conf.h"
#include "pptp_conf.h"
#include "pppoe_conf.h"
#include "slist.h"

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))
#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))

#define	NPPPD_USER			"_ppp"
#define	NPPPD_GENERIC_NAME_LEN		32

/** Constants of tunnel type */
#define NPPPD_TUNNEL_NONE		0	/** None Tunnel Type */
#define NPPPD_TUNNEL_L2TP		1	/** L2TP Tunnel Type */
#define NPPPD_TUNNEL_PPTP		2	/** PPTP Tunnel Type */
#define NPPPD_TUNNEL_PPPOE		3	/** PPPoE Tunnel Type */
#define NPPPD_TUNNEL_SSTP		4	/** SSTP Tunnel Type */

#define npppd_pipex_proto(_tunnel)				\
    (((_tunnel) == NPPPD_TUNNEL_L2TP)? PIPEX_PROTO_L2TP :	\
    ((_tunnel) == NPPPD_TUNNEL_PPTP)? PIPEX_PROTO_PPTP :	\
    ((_tunnel) == NPPPD_TUNNEL_PPPOE)? PIPEX_PROTO_PPPOE : -1)

#define	NPPPD_AUTH_METHODS_PAP		0x0001
#define	NPPPD_AUTH_METHODS_CHAP		0x0002
#define	NPPPD_AUTH_METHODS_MSCHAPV2	0x0004

#define	NPPPD_MPPE_DISABLED		0x0000
#define	NPPPD_MPPE_ENABLED		0x0001
#define	NPPPD_MPPE_REQUIRED		0x0002

#define	NPPPD_MPPE_40BIT		0x0001
#define	NPPPD_MPPE_56BIT		0x0002
#define	NPPPD_MPPE_128BIT		0x0004

#define	NPPPD_MPPE_STATEFUL		0x0001
#define	NPPPD_MPPE_STATELESS		0x0002

#define NPPPD_PROTO_BIT_IP		0x0001
#define NPPPD_PROTO_BIT_LCP		0x0002
#define NPPPD_PROTO_BIT_PAP		0x0004
#define NPPPD_PROTO_BIT_CHAP		0x0008
#define NPPPD_PROTO_BIT_EAP		0x0010
#define NPPPD_PROTO_BIT_MPPE		0x0020
#define NPPPD_PROTO_BIT_CCP		0x0040
#define NPPPD_PROTO_BIT_IPCP		0x0080

#define	NPPPD_CALLNUM_CHECK_NONE	0
#define	NPPPD_CALLNUM_CHECK_STRICT	1
#define	NPPPD_CALLNUM_CHECK_LOOSE	2

struct tunnconf {
	TAILQ_ENTRY(tunnconf)              entry;
	const char                        *name;
	int                                protocol;

	union {
		struct l2tp_conf           l2tp;
		struct pptp_conf           pptp;
		struct pppoe_conf          pppoe;
	} proto;

	int                                lcp_timeout;
	int                                lcp_max_configure;
	int                                lcp_max_terminate;
	int                                lcp_max_nak_loop;
	int                                mru;
	bool                               lcp_keepalive;
	int                                lcp_keepalive_interval;
	int                                lcp_keepalive_retry_interval;
	int                                lcp_keepalive_max_retries;

	u_int                              auth_methods;

	int                                ipcp_timeout;
	int                                ipcp_max_configure;
	int                                ipcp_max_terminate;
	int                                ipcp_max_nak_loop;
	int                                ccp_timeout;
	int                                ccp_max_configure;
	int                                ccp_max_terminate;
	int                                ccp_max_nak_loop;
	char                              *chap_name;

	bool                               mppe_yesno;
	bool                               mppe_required;
	u_int                              mppe_keylen;
	u_int                              mppe_keystate;

	int                                idle_timeout;
	bool                               tcp_mss_adjust;
	bool                               ingress_filter;
	int                                callnum_check;

	bool                               pipex;

	u_int                              debug_dump_pktin;
	u_int                              debug_dump_pktout;
};

struct radserver {
	TAILQ_ENTRY(radserver)             entry;
	struct sockaddr_storage            address;
	char                              *secret;
};

struct radconf {
	TAILQ_HEAD(radservers, radserver)  servers;
	int                                timeout;
	int                                max_tries;
	int                                max_failovers;
};

struct authconf {
	TAILQ_ENTRY(authconf)              entry;
	char                               name[NPPPD_GENERIC_NAME_LEN];
	int                                auth_type;
	char                              *username_suffix;
	bool                               eap_capable;
	bool                               strip_nt_domain;
	bool                               strip_atmark_realm;
	char                               users_file_path[PATH_MAX];
	int                                user_max_session;
	union {
		struct {
			struct radconf     auth;
			struct radconf     acct;
		} radius;
	} data;
};

struct radclientconf {
	union {
		struct sockaddr_in         sin4;
		struct sockaddr_in6        sin6;
	}                                  addr;
	TAILQ_ENTRY(radclientconf)         entry;
	char                               secret[];
};
TAILQ_HEAD(radclientconfs,radclientconf);

struct radlistenconf {
	union {
		struct sockaddr_in         sin4;
		struct sockaddr_in6        sin6;
	}                                  addr;
	TAILQ_ENTRY(radlistenconf)         entry;
};
TAILQ_HEAD(radlistenconfs,radlistenconf);

struct ipcpconf {
	TAILQ_ENTRY(ipcpconf)              entry;
	char                               name[NPPPD_GENERIC_NAME_LEN];
	bool                               dns_use_resolver;
	bool                               dns_configured;
	struct in_addr                     dns_servers[2];
	bool                               nbns_configured;
	struct in_addr                     nbns_servers[2];
	bool                               allow_user_select;
	struct in_addr_range              *dynamic_pool;
	struct in_addr_range              *static_pool;
	int                                max_session;
};

struct iface {
	TAILQ_ENTRY(iface)                 entry;
	char                               name[IFNAMSIZ];
	struct in_addr                     ip4addr;
	struct ipcpconf                   *ipcpconf;
	bool                               is_pppx;
};

struct confbind {
	TAILQ_ENTRY(confbind)              entry;
	struct tunnconf                   *tunnconf;
	struct authconf                   *authconf;
	struct iface                      *iface;
};

struct npppd_conf {
	int                                max_session;
	int                                user_max_session;
	TAILQ_HEAD(tunnconfs, tunnconf)    tunnconfs;
	TAILQ_HEAD(authconfs, authconf)    authconfs;
	TAILQ_HEAD(ipcpconfs, ipcpconf)    ipcpconfs;
	TAILQ_HEAD(ifaces, iface)          ifaces;
	TAILQ_HEAD(confbinds, confbind)    confbinds;
	struct radclientconfs              raddaeclientconfs;
	struct radlistenconfs              raddaelistenconfs;
	char				   nas_id[NPPPD_GENERIC_NAME_LEN];
	struct l2tp_confs                  l2tp_confs;
	struct pptp_confs                  pptp_confs;
	struct pppoe_confs                 pppoe_confs;
};

/** sockaddr_npppd */
struct sockaddr_npppd {
	struct sockaddr_in sin4;
	struct sockaddr_in sin4mask;
#define			snp_len		sin4.sin_len
#define			snp_family	sin4.sin_family
#define			snp_addr	sin4.sin_addr
	int		snp_type;	/* SNP_POOL or SNP_PPP */
#define			snp_mask	sin4mask.sin_addr
	/** next entry */
	struct sockaddr_npppd *snp_next;
	/** contents of entry */
	void 		*snp_data_ptr;
};
#define	SNP_POOL		1
#define	SNP_DYN_POOL		2
#define	SNP_PPP			3

struct ipcpstat {
	LIST_ENTRY(ipcpstat)	entry;
	char			name[NPPPD_GENERIC_NAME_LEN];
	int			nsession;
	LIST_HEAD(, _npppd_ppp) ppp;
};
LIST_HEAD(ipcpstat_head, ipcpstat);

typedef struct _npppd		npppd;

#include "ppp.h"

#include <imsg.h>

struct imsgev {
	struct imsgbuf           ibuf;
	void                    (*handler)(int, short, void *);
	struct event             ev;
	void                    *data;
	short                    events;
};

struct ctl_conn {
	TAILQ_ENTRY(ctl_conn)    entry;
	struct control_sock     *parent;
	u_int8_t                 flags;
#define CTL_CONN_NOTIFY          0x01
#define CTL_CONN_LOCKED          0x02   /* restricted mode */
	struct imsgev            iev;
	void                    *ctx;
};

TAILQ_HEAD(ctl_conn_list, ctl_conn);
extern struct ctl_conn_list ctl_conns;

__BEGIN_DECLS
npppd		*npppd_get_npppd(void);
int		 npppd_init(npppd *, const char *);
void		 npppd_start(npppd *);
void		 npppd_stop(npppd *);
void		 npppd_fini(npppd *);
int		 npppd_reset_routing_table(npppd *, int);
int		 npppd_get_user_password(npppd *, npppd_ppp *, const char *,
		    char *, int *);
struct in_addr	*npppd_get_user_framed_ip_address(npppd *, npppd_ppp *,
		    const char *);
int		 npppd_check_calling_number(npppd *, npppd_ppp *);
npppd_ppp	*npppd_get_ppp_by_ip(npppd *, struct in_addr);
npppd_ppp	*npppd_get_ppp_by_id(npppd *, u_int);
slist		*npppd_get_ppp_by_user(npppd *, const char *);
int		 npppd_check_user_max_session(npppd *, npppd_ppp *);
void		 npppd_network_output(npppd *, npppd_ppp *, int, u_char *, int);
int		 npppd_ppp_pipex_enable(npppd *, npppd_ppp *);
int		 npppd_ppp_pipex_disable(npppd *, npppd_ppp *);
int		 npppd_prepare_ip(npppd *, npppd_ppp *);
void		 npppd_release_ip(npppd *, npppd_ppp *);
void		 npppd_set_ip_enabled(npppd *, npppd_ppp *, int);
int		 npppd_assign_ip_addr(npppd *, npppd_ppp *, uint32_t);
int		 npppd_set_radish(npppd *, void *);
int		 npppd_ppp_bind_realm(npppd *, npppd_ppp *, const char *, int);
int		 npppd_ppp_is_realm_local(npppd *, npppd_ppp *);
int		 npppd_ppp_is_realm_radius(npppd *, npppd_ppp *);
int		 npppd_ppp_is_realm_ready(npppd *, npppd_ppp *);
const char	*npppd_ppp_get_realm_name(npppd *, npppd_ppp *);
const char	*npppd_ppp_get_iface_name(npppd *, npppd_ppp *);
int		 npppd_ppp_iface_is_ready(npppd *, npppd_ppp *);
int		 npppd_ppp_bind_iface(npppd *, npppd_ppp *);
void		 npppd_ppp_unbind_iface(npppd *, npppd_ppp *);
void		*npppd_get_radius_auth_setting(npppd *, npppd_ppp *);
int		 sockaddr_npppd_match(void *, void *);
const char	*npppd_ppp_get_username_for_auth(npppd *, npppd_ppp *,
		    const char *, char *);
const char	*npppd_ppp_tunnel_protocol_name(npppd *, npppd_ppp *);
const char	*npppd_tunnel_protocol_name(int);
struct tunnconf *npppd_get_tunnconf(npppd *, const char *);
int		 npppd_reload_config(npppd *);
int		 npppd_modules_reload(npppd *);
int		 npppd_ifaces_load_config(npppd *);

int		 npppd_conf_parse(struct npppd_conf *, const char *);
void		 npppd_conf_init(struct npppd_conf *);
void		 npppd_conf_fini(struct npppd_conf *);
int		 npppd_config_check(const char *);
void		 npppd_on_ppp_start(npppd *, npppd_ppp *);
void		 npppd_on_ppp_stop(npppd *, npppd_ppp *);
void		 imsg_event_add(struct imsgev *);

int		 control_init(struct control_sock *);
int		 control_listen(struct control_sock *);
void		 control_cleanup(struct control_sock *);
struct npppd_ctl
		*npppd_ctl_create(npppd *);
void		 npppd_ctl_destroy(struct npppd_ctl *);
int		 npppd_ctl_who(struct npppd_ctl *);
int		 npppd_ctl_monitor(struct npppd_ctl *);
int		 npppd_ctl_who_and_monitor(struct npppd_ctl *);
int		 npppd_ctl_add_started_ppp_id(struct npppd_ctl *, uint32_t);
int		 npppd_ctl_add_stopped_ppp(struct npppd_ctl *, npppd_ppp *);
int		 npppd_ctl_imsg_compose(struct npppd_ctl *, struct imsgbuf *);
int		 npppd_ctl_disconnect(struct npppd_ctl *, u_int *, int);

__END_DECLS

#endif
