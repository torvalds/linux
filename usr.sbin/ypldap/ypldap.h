/*	$OpenBSD: ypldap.h,v 1.16 2015/01/16 06:40:22 deraadt Exp $ */
/*	$FreeBSD$ */

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

#include <imsg.h>

#define YPLDAP_USER		"_ypldap"
#define YPLDAP_CONF_FILE	"/etc/ypldap.conf"
#define DEFAULT_INTERVAL	600
#define LINE_WIDTH		1024
#define FILTER_WIDTH		128
#define ATTR_WIDTH		32

#define        MAX_SERVERS_DNS                 8

enum imsg_type {
	IMSG_NONE,
	IMSG_CONF_START,
	IMSG_CONF_IDM,
	IMSG_CONF_END,
	IMSG_START_UPDATE,
	IMSG_END_UPDATE,
	IMSG_TRASH_UPDATE,
	IMSG_PW_ENTRY,
	IMSG_GRP_ENTRY,
	IMSG_HOST_DNS
};

struct ypldap_addr {
	TAILQ_ENTRY(ypldap_addr)	next;
	struct sockaddr_storage         ss;
};
TAILQ_HEAD(ypldap_addr_list, ypldap_addr);

enum {
	PROC_MAIN,
	PROC_CLIENT
} ypldap_process;

struct userent {
	RB_ENTRY(userent)		 ue_name_node;
	RB_ENTRY(userent)		 ue_uid_node;
	uid_t				 ue_uid;
	char				*ue_line;
	char				*ue_netid_line;
	gid_t				 ue_gid;
};

struct groupent {
	RB_ENTRY(groupent)		 ge_name_node;
	RB_ENTRY(groupent)		 ge_gid_node;
	gid_t				 ge_gid;
	char				*ge_line;
};

enum client_state {
        STATE_NONE,
        STATE_DNS_INPROGRESS,
        STATE_DNS_TEMPFAIL,
        STATE_DNS_DONE,
	STATE_LDAP_FAIL,
	STATE_LDAP_DONE
};

/*
 * beck, djm, dlg: pay attention to the struct name
 */
struct idm {
	TAILQ_ENTRY(idm)		 idm_entry;
	u_int32_t                        idm_id;
	char				 idm_name[MAXHOSTNAMELEN];
#define F_SSL				 0x00100000
#define F_CONFIGURING			 0x00200000
#define F_NEEDAUTH			 0x00400000
#define F_FIXED_ATTR(n)			 (1<<n)
#define F_LIST(n)                        (1<<n)
	enum client_state		 idm_state;
	u_int32_t			 idm_flags; /* lower 20 reserved */
	u_int32_t			 idm_list;
	struct ypldap_addr_list		 idm_addr;
	in_port_t			 idm_port;
	char				 idm_binddn[LINE_WIDTH];
	char				 idm_bindcred[LINE_WIDTH];
	char				 idm_basedn[LINE_WIDTH];
	char				 idm_groupdn[LINE_WIDTH];
#define FILTER_USER			 1
#define FILTER_GROUP			 0
	char				 idm_filters[2][FILTER_WIDTH];
#define ATTR_NAME			 0
#define ATTR_PASSWD			 1
#define ATTR_UID			 2
#define ATTR_GID			 3
#define ATTR_CLASS			 4
#define ATTR_CHANGE			 5
#define ATTR_EXPIRE			 6
#define ATTR_GECOS			 7
#define ATTR_DIR			 8
#define ATTR_SHELL			 9
#define ATTR_GR_NAME			 10
#define ATTR_GR_PASSWD			 11
#define ATTR_GR_GID			 12
#define ATTR_GR_MEMBERS			 13
#define ATTR_MAX			 10
#define ATTR_GR_MIN			 10
#define ATTR_GR_MAX			 14
	char				 idm_attrs[14][ATTR_WIDTH];
	struct env			*idm_env;
	struct event			 idm_ev;
#ifdef SSL
	struct ssl			*idm_ssl;
#endif
};

struct idm_req {
	union {
		uid_t			 ik_uid;
		uid_t			 ik_gid;
	}				 ir_key;
	char				 ir_line[LINE_WIDTH];
};

struct imsgev {
	struct imsgbuf		 ibuf;
	void			(*handler)(int, short, void *);
	struct event		 ev;
	void			*data;
	short			 events;
};

struct env {
#define YPLDAP_OPT_VERBOSE		 0x01
#define YPLDAP_OPT_NOACTION		 0x02
	u_int8_t			 sc_opts;
#define YPMAP_PASSWD_BYNAME		 0x00000001
#define YPMAP_PASSWD_BYUID		 0x00000002
#define YPMAP_MASTER_PASSWD_BYNAME	 0x00000004
#define YPMAP_MASTER_PASSWD_BYUID	 0x00000008
#define YPMAP_GROUP_BYNAME		 0x00000010
#define YPMAP_GROUP_BYGID		 0x00000020
#define YPMAP_NETID_BYNAME		 0x00000040
	u_int32_t			 sc_flags;

	u_int32_t			 sc_maxid;

	char				 sc_domainname[MAXHOSTNAMELEN];
	struct timeval			 sc_conf_tv;
	struct event			 sc_conf_ev;
	TAILQ_HEAD(idm_list, idm)	 sc_idms;
	struct imsgev			*sc_iev;
	struct imsgev			*sc_iev_dns;

	RB_HEAD(user_name_tree,userent)	 *sc_user_names;
	RB_HEAD(user_uid_tree,userent)	 sc_user_uids;
	RB_HEAD(group_name_tree,groupent)*sc_group_names;
	RB_HEAD(group_gid_tree,groupent) sc_group_gids;
	struct user_name_tree		 *sc_user_names_t;
	struct group_name_tree		 *sc_group_names_t;
	size_t				 sc_user_line_len;
	size_t				 sc_group_line_len;
	char				*sc_user_lines;
	char				*sc_group_lines;

	struct yp_data			*sc_yp;

	int				 update_trashed;
};

/* log.c */
void		 log_init(int);
void		 log_warn(const char *, ...);
void		 log_warnx(const char *, ...);
void		 log_info(const char *, ...);
void		 log_debug(const char *, ...);
void		 logit(int, const char *, ...);
void		 vlog(int, const char *, va_list);
__dead2 void	 fatal(const char *);
__dead2 void	 fatalx(const char *);

/* parse.y */
int		 parse_config(struct env *, const char *, int);
int		 cmdline_symset(char *);

/* ldapclient.c */
pid_t		 ldapclient(int []);

/* ypldap.c */
void		 purge_config(struct env *);
void		 imsg_event_add(struct imsgev *);
int	 	 imsg_compose_event(struct imsgev *, u_int16_t, u_int32_t,
		    pid_t, int, void *, u_int16_t);

/* entries.c */
void		 flatten_entries(struct env *);
int		 userent_name_cmp(struct userent *, struct userent *);
int		 userent_uid_cmp(struct userent *, struct userent *);
int		 groupent_name_cmp(struct groupent *, struct groupent *);
int		 groupent_gid_cmp(struct groupent *, struct groupent *);
RB_PROTOTYPE(	 user_name_tree, userent, ue_name_node, userent_name_cmp);
RB_PROTOTYPE(	 user_uid_tree, userent, ue_uid_node, userent_uid_cmp);
RB_PROTOTYPE(	 group_name_tree, groupent, ge_name_node, groupent_name_cmp);
RB_PROTOTYPE(	 group_gid_tree, groupent, ge_gid_node, groupent_gid_cmp);

/* yp.c */
void		 yp_init(struct env *);
void		 yp_enable_events(void);

/* ypldap_dns.c */
pid_t		 ypldap_dns(int[2], struct passwd *);
