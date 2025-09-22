/*	$OpenBSD: ripd.h,v 1.29 2024/10/22 22:50:49 jsg Exp $ */

/*
 * Copyright (c) 2004 Esben Norby <norby@openbsd.org>
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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

#ifndef RIPD_H
#define RIPD_H

#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/tree.h>
#include <md5.h>
#include <net/if.h>
#include <netinet/in.h>
#include <event.h>

#include <imsg.h>

#define	CONF_FILE		"/etc/ripd.conf"
#define	RIPD_SOCKET		"/var/run/ripd.sock"
#define	RIPD_USER		"_ripd"

#define	NBR_HASHSIZE		128
#define	NBR_IDSELF		1
#define	NBR_CNTSTART		(NBR_IDSELF + 1)

#define	ROUTE_TIMEOUT		180
#define ROUTE_GARBAGE		120

#define	NBR_TIMEOUT		180

#define READ_BUF_SIZE		65535
#define RT_BUF_SIZE		16384
#define MAX_RTSOCK_BUF		(2 * 1024 * 1024)

#define	RIPD_FLAG_NO_FIB_UPDATE	0x0001

#define	F_RIPD_INSERTED		0x0001
#define	F_KERNEL		0x0002
#define	F_CONNECTED		0x0008
#define	F_DOWN			0x0010
#define	F_STATIC		0x0020
#define	F_DYNAMIC		0x0040
#define	F_REDISTRIBUTED		0x0100
#define	F_REJECT		0x0200
#define	F_BLACKHOLE		0x0400

#define REDISTRIBUTE_ON		0x01

#define	OPT_SPLIT_HORIZON	0x01
#define	OPT_SPLIT_POISONED	0x02
#define	OPT_TRIGGERED_UPDATES	0x04

enum imsg_type {
	IMSG_NONE,
	IMSG_CTL_END,
	IMSG_CTL_RELOAD,
	IMSG_CTL_IFINFO,
	IMSG_IFINFO,
	IMSG_CTL_FIB_COUPLE,
	IMSG_CTL_FIB_DECOUPLE,
	IMSG_CTL_KROUTE,
	IMSG_CTL_KROUTE_ADDR,
	IMSG_CTL_SHOW_INTERFACE,
	IMSG_CTL_SHOW_IFACE,
	IMSG_CTL_SHOW_NBR,
	IMSG_CTL_SHOW_RIB,
	IMSG_CTL_LOG_VERBOSE,
	IMSG_KROUTE_CHANGE,
	IMSG_KROUTE_DELETE,
	IMSG_NETWORK_ADD,
	IMSG_NETWORK_DEL,
	IMSG_ROUTE_FEED,
	IMSG_RESPONSE_ADD,
	IMSG_SEND_RESPONSE,
	IMSG_FULL_RESPONSE,
	IMSG_ROUTE_REQUEST,
	IMSG_ROUTE_REQUEST_END,
	IMSG_FULL_REQUEST,
	IMSG_REQUEST_ADD,
	IMSG_SEND_REQUEST,
	IMSG_SEND_TRIGGERED_UPDATE,
	IMSG_DEMOTE
};

struct imsgev {
	struct imsgbuf		 ibuf;
	void			(*handler)(int, short, void *);
	struct event		 ev;
	void			*data;
	short			 events;
};

/* interface states */
#define IF_STA_DOWN		0x01
#define IF_STA_ACTIVE		(~IF_STA_DOWN)
#define IF_STA_ANY		0x7f

/* interface events */
enum iface_event {
	IF_EVT_NOTHING,
	IF_EVT_UP,
	IF_EVT_DOWN
};

/* interface actions */
enum iface_action {
	IF_ACT_NOTHING,
	IF_ACT_STRT,
	IF_ACT_RST
};

/* interface types */
enum iface_type {
	IF_TYPE_POINTOPOINT,
	IF_TYPE_BROADCAST,
	IF_TYPE_NBMA,
	IF_TYPE_POINTOMULTIPOINT
};

/* neighbor states */
#define NBR_STA_DOWN		0x01
#define	NBR_STA_REQ_RCVD	0x02
#define NBR_STA_ACTIVE		(~NBR_STA_DOWN)
#define NBR_STA_ANY		0xff

struct auth_md {
	TAILQ_ENTRY(auth_md)	 entry;
	u_int32_t		 seq_modulator;
	u_int8_t		 key[MD5_DIGEST_LENGTH];
	u_int8_t		 keyid;
};

#define MAX_SIMPLE_AUTH_LEN	16

/* auth types */
enum auth_type {
	AUTH_NONE = 1,
	AUTH_SIMPLE,
	AUTH_CRYPT
};

TAILQ_HEAD(auth_md_head, auth_md);
TAILQ_HEAD(packet_head, packet_entry);

struct iface {
	LIST_ENTRY(iface)	 entry;
	LIST_HEAD(, nbr)	 nbr_list;
	LIST_HEAD(, nbr_failed)	 failed_nbr_list;
	char			 name[IF_NAMESIZE];
	char			 demote_group[IFNAMSIZ];
	u_int8_t		 auth_key[MAX_SIMPLE_AUTH_LEN];
	struct in_addr		 addr;
	struct in_addr		 dst;
	struct in_addr		 mask;
	struct packet_head	 rq_list;
	struct packet_head	 rp_list;
	struct auth_md_head	 auth_md_list;

	u_int64_t		 baudrate;
	time_t			 uptime;
	u_int			 mtu;
	int			 fd; /* XXX */
	int			 state;
	u_short			 ifindex;
	u_int16_t		 cost;
	u_int16_t		 flags;
	enum iface_type		 type;
	enum auth_type		 auth_type;
	u_int8_t		 linktype;
	u_int8_t		 if_type;
	u_int8_t		 passive;
	u_int8_t		 linkstate;
	u_int8_t		 auth_keyid;
};

struct rip_route {
	struct in_addr		 address;
	struct in_addr		 mask;
	struct in_addr		 nexthop;
	int			 refcount;
	u_short			 ifindex;
	u_int8_t		 metric;
};

struct packet_entry {
	TAILQ_ENTRY(packet_entry)	 entry;
	struct rip_route		*rr;
};

#define	REDIST_CONNECTED	0x01
#define	REDIST_STATIC		0x02
#define	REDIST_LABEL		0x04
#define	REDIST_ADDR		0x08
#define	REDIST_DEFAULT		0x10
#define	REDIST_NO		0x20

struct redistribute {
	SIMPLEQ_ENTRY(redistribute)	entry;
	struct in_addr			addr;
	struct in_addr			mask;
	u_int32_t			metric;
	u_int16_t			label;
	u_int16_t			type;
};

struct ripd_conf {
	struct event		 ev;
	struct event		 report_timer;
	LIST_HEAD(, iface)	 iface_list;
	SIMPLEQ_HEAD(, redistribute) redist_list;

	u_int32_t		 opts;
#define RIPD_OPT_VERBOSE	0x00000001
#define	RIPD_OPT_VERBOSE2	0x00000002
#define	RIPD_OPT_NOACTION	0x00000004
#define	RIPD_OPT_FORCE_DEMOTE	0x00000008
	int			 flags;
	int			 options;
	int			 rip_socket;
	int			 redistribute;
	u_int8_t		 fib_priority;
	u_int			 rdomain;
	char			*csock;
};

/* kroute */
struct kroute {
	struct in_addr	prefix;
	struct in_addr	netmask;
	struct in_addr	nexthop;
	u_int16_t	flags;
	u_int16_t	rtlabel;
	u_short		ifindex;
	u_int8_t	metric;
	u_int8_t	priority;
};

struct kif {
	char		 ifname[IF_NAMESIZE];
	u_int64_t	 baudrate;
	int		 flags;
	int		 mtu;
	u_short		 ifindex;
	u_int8_t	 if_type;
	u_int8_t	 link_state;
	u_int8_t	 nh_reachable;	/* for nexthop verification */
};

/* control data structures */
struct ctl_iface {
	char			 name[IF_NAMESIZE];
	struct in_addr		 addr;
	struct in_addr		 mask;

	time_t			 uptime;
	time_t			 report_timer;

	u_int64_t		 baudrate;
	unsigned int		 ifindex;
	int			 state;
	int			 mtu;

	u_int16_t		 flags;
	u_int16_t		 metric;
	enum iface_type		 type;
	u_int8_t		 linkstate;
	u_int8_t		 if_type;
	u_int8_t		 passive;
};

struct ctl_rt {
	struct in_addr		 prefix;
	struct in_addr		 netmask;
	struct in_addr		 nexthop;
	time_t			 uptime;
	time_t			 expire;
	u_int32_t		 metric;
	u_int16_t		 flags;
};

struct ctl_nbr {
	char			 name[IF_NAMESIZE];
	struct in_addr		 id;
	struct in_addr		 addr;
	time_t			 dead_timer;
	time_t			 uptime;
	int			 nbr_state;
	int			 iface_state;
};

struct demote_msg {
	char			 demote_group[IF_NAMESIZE];
	int			 level;
};

int		 kif_init(void);
int		 kr_init(int, u_int, u_int8_t);
int		 kr_change(struct kroute *);
int		 kr_delete(struct kroute *);
void		 kr_shutdown(void);
void		 kr_fib_couple(void);
void		 kr_fib_decouple(void);
void		 kr_dispatch_msg(int, short, void *);
void		 kr_show_route(struct imsg *);
void		 kr_ifinfo(char *, pid_t);
struct kif	*kif_findname(char *);

in_addr_t	 prefixlen2mask(u_int8_t);
u_int8_t	 mask2prefixlen(in_addr_t);

/* ripd.c */
void		 main_imsg_compose_ripe(int, pid_t, void *, u_int16_t);
void		 main_imsg_compose_rde(int, pid_t, void *, u_int16_t);
int		 rip_redistribute(struct kroute *);
void		 imsg_event_add(struct imsgev *);
int		 imsg_compose_event(struct imsgev *, u_int16_t, u_int32_t,
		    pid_t, int, void *, u_int16_t);

/* parse.y */
struct ripd_conf	*parse_config(char *, int);
int			 cmdline_symset(char *);

/* carp.c */
int		 carp_demote_init(char *, int);
void		 carp_demote_shutdown(void);
int		 carp_demote_get(char *);
int		 carp_demote_set(char *, int);

/* printconf.c */
void		 print_config(struct ripd_conf *);

/* logmsg.c */
const char	*nbr_state_name(int);
const char	*if_type_name(enum iface_type);
const char	*if_auth_name(enum auth_type);
const char	*if_state_name(int);

/* interface.c */
struct iface	*if_find_index(u_short);

/* name2id.c */
u_int16_t	 rtlabel_name2id(const char *);
const char	*rtlabel_id2name(u_int16_t);
void		 rtlabel_unref(u_int16_t);

#endif /* RIPD_H */
