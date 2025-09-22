/*	$OpenBSD: ldpd.h,v 1.93 2024/11/21 13:29:28 claudio Exp $ */

/*
 * Copyright (c) 2013, 2016 Renato Westphal <renato@openbsd.org>
 * Copyright (c) 2009 Michele Marchetto <michele@openbsd.org>
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

#ifndef _LDPD_H_
#define _LDPD_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/tree.h>
#include <net/if_arp.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <event.h>
#include <imsg.h>

#include "ldp.h"

#define CONF_FILE		"/etc/ldpd.conf"
#define	LDPD_SOCKET		"/var/run/ldpd.sock"
#define LDPD_USER		"_ldpd"

#define LDPD_OPT_VERBOSE	0x00000001
#define LDPD_OPT_VERBOSE2	0x00000002
#define LDPD_OPT_NOACTION	0x00000004

#define TCP_MD5_KEY_LEN		80
#define L2VPN_NAME_LEN		32

#define	READ_BUF_SIZE		65535
#define	RT_BUF_SIZE		16384
#define	MAX_RTSOCK_BUF		(2 * 1024 * 1024)
#define	LDP_BACKLOG		128

#define	F_LDPD_INSERTED		0x0001
#define	F_CONNECTED		0x0002
#define	F_STATIC		0x0004
#define	F_DYNAMIC		0x0008
#define	F_REJECT		0x0010
#define	F_BLACKHOLE		0x0020
#define	F_REDISTRIBUTED		0x0040

struct evbuf {
	struct msgbuf		*wbuf;
	struct event		 ev;
};

struct imsgev {
	struct imsgbuf		 ibuf;
	void			(*handler)(int, short, void *);
	struct event		 ev;
	short			 events;
};

enum imsg_type {
	IMSG_NONE,
	IMSG_CTL_RELOAD,
	IMSG_CTL_SHOW_INTERFACE,
	IMSG_CTL_SHOW_DISCOVERY,
	IMSG_CTL_SHOW_NBR,
	IMSG_CTL_SHOW_LIB,
	IMSG_CTL_SHOW_L2VPN_PW,
	IMSG_CTL_SHOW_L2VPN_BINDING,
	IMSG_CTL_CLEAR_NBR,
	IMSG_CTL_FIB_COUPLE,
	IMSG_CTL_FIB_DECOUPLE,
	IMSG_CTL_KROUTE,
	IMSG_CTL_KROUTE_ADDR,
	IMSG_CTL_IFINFO,
	IMSG_CTL_END,
	IMSG_CTL_LOG_VERBOSE,
	IMSG_KLABEL_CHANGE,
	IMSG_KLABEL_DELETE,
	IMSG_KPWLABEL_CHANGE,
	IMSG_KPWLABEL_DELETE,
	IMSG_IFSTATUS,
	IMSG_NEWADDR,
	IMSG_DELADDR,
	IMSG_LABEL_MAPPING,
	IMSG_LABEL_MAPPING_FULL,
	IMSG_LABEL_REQUEST,
	IMSG_LABEL_RELEASE,
	IMSG_LABEL_WITHDRAW,
	IMSG_LABEL_ABORT,
	IMSG_REQUEST_ADD,
	IMSG_REQUEST_ADD_END,
	IMSG_MAPPING_ADD,
	IMSG_MAPPING_ADD_END,
	IMSG_RELEASE_ADD,
	IMSG_RELEASE_ADD_END,
	IMSG_WITHDRAW_ADD,
	IMSG_WITHDRAW_ADD_END,
	IMSG_ADDRESS_ADD,
	IMSG_ADDRESS_DEL,
	IMSG_NOTIFICATION,
	IMSG_NOTIFICATION_SEND,
	IMSG_NEIGHBOR_UP,
	IMSG_NEIGHBOR_DOWN,
	IMSG_NETWORK_ADD,
	IMSG_NETWORK_DEL,
	IMSG_SOCKET_IPC,
	IMSG_SOCKET_NET,
	IMSG_CLOSE_SOCKETS,
	IMSG_REQUEST_SOCKETS,
	IMSG_SETUP_SOCKETS,
	IMSG_RECONF_CONF,
	IMSG_RECONF_IFACE,
	IMSG_RECONF_TNBR,
	IMSG_RECONF_NBRP,
	IMSG_RECONF_L2VPN,
	IMSG_RECONF_L2VPN_IF,
	IMSG_RECONF_L2VPN_PW,
	IMSG_RECONF_CONF_AUTH,
	IMSG_RECONF_END
};

union ldpd_addr {
	struct in_addr	v4;
	struct in6_addr	v6;
};

#define IN6_IS_SCOPE_EMBED(a)   \
	((IN6_IS_ADDR_LINKLOCAL(a)) ||  \
	 (IN6_IS_ADDR_MC_LINKLOCAL(a)) || \
	 (IN6_IS_ADDR_MC_INTFACELOCAL(a)))

/* interface states */
#define	IF_STA_DOWN		0x01
#define	IF_STA_ACTIVE		0x02

/* targeted neighbor states */
#define	TNBR_STA_DOWN		0x01
#define	TNBR_STA_ACTIVE		0x02

/* interface types */
enum iface_type {
	IF_TYPE_POINTOPOINT,
	IF_TYPE_BROADCAST
};

/* neighbor states */
#define	NBR_STA_PRESENT		0x0001
#define	NBR_STA_INITIAL		0x0002
#define	NBR_STA_OPENREC		0x0004
#define	NBR_STA_OPENSENT	0x0008
#define	NBR_STA_OPER		0x0010
#define	NBR_STA_SESSION		(NBR_STA_INITIAL | NBR_STA_OPENREC | \
				NBR_STA_OPENSENT | NBR_STA_OPER)

/* neighbor events */
enum nbr_event {
	NBR_EVT_NOTHING,
	NBR_EVT_MATCH_ADJ,
	NBR_EVT_CONNECT_UP,
	NBR_EVT_CLOSE_SESSION,
	NBR_EVT_INIT_RCVD,
	NBR_EVT_KEEPALIVE_RCVD,
	NBR_EVT_PDU_RCVD,
	NBR_EVT_PDU_SENT,
	NBR_EVT_INIT_SENT
};

/* neighbor actions */
enum nbr_action {
	NBR_ACT_NOTHING,
	NBR_ACT_RST_KTIMEOUT,
	NBR_ACT_SESSION_EST,
	NBR_ACT_RST_KTIMER,
	NBR_ACT_CONNECT_SETUP,
	NBR_ACT_PASSIVE_INIT,
	NBR_ACT_KEEPALIVE_SEND,
	NBR_ACT_CLOSE_SESSION
};

TAILQ_HEAD(mapping_head, mapping_entry);

struct map {
	uint8_t		type;
	uint32_t	msg_id;
	union {
		struct {
			uint16_t	af;
			union ldpd_addr	prefix;
			uint8_t		prefixlen;
		} prefix;
		struct {
			uint16_t	type;
			uint32_t	pwid;
			uint32_t	group_id;
			uint16_t	ifmtu;
		} pwid;
		struct {
			uint8_t		type;
			union {
				uint16_t	prefix_af;
				uint16_t	pw_type;
			} u;
		} twcard;
	} fec;
	struct {
		uint32_t	status_code;
		uint32_t	msg_id;
		uint16_t	msg_type;
	} st;
	uint32_t	label;
	uint32_t	requestid;
	uint32_t	pw_status;
	uint8_t		flags;
};
#define F_MAP_REQ_ID	0x01	/* optional request message id present */
#define F_MAP_STATUS	0x02	/* status */
#define F_MAP_PW_CWORD	0x04	/* pseudowire control word */
#define F_MAP_PW_ID	0x08	/* pseudowire connection id */
#define F_MAP_PW_IFMTU	0x10	/* pseudowire interface parameter */
#define F_MAP_PW_STATUS	0x20	/* pseudowire status */

struct notify_msg {
	uint32_t	status_code;
	uint32_t	msg_id;		/* network byte order */
	uint16_t	msg_type;	/* network byte order */
	uint32_t	pw_status;
	struct map	fec;
	struct {
		uint16_t	 type;
		uint16_t	 length;
		char		*data;
	} rtlvs;
	uint8_t		flags;
};
#define F_NOTIF_PW_STATUS	0x01	/* pseudowire status tlv present */
#define F_NOTIF_FEC		0x02	/* fec tlv present */
#define F_NOTIF_RETURNED_TLVS	0x04	/* returned tlvs present */

struct if_addr {
	LIST_ENTRY(if_addr)	 entry;
	int			 af;
	union ldpd_addr		 addr;
	uint8_t			 prefixlen;
	union ldpd_addr		 dstbrd;
};
LIST_HEAD(if_addr_head, if_addr);

struct iface_af {
	struct iface		*iface;
	int			 af;
	int			 enabled;
	int			 state;
	LIST_HEAD(, adj)	 adj_list;
	time_t			 uptime;
	struct event		 hello_timer;
	uint16_t		 hello_holdtime;
	uint16_t		 hello_interval;
};

struct iface {
	LIST_ENTRY(iface)	 entry;
	char			 name[IF_NAMESIZE];
	unsigned int		 ifindex;
	unsigned int		 rdomain;
	struct if_addr_head	 addr_list;
	struct in6_addr		 linklocal;
	enum iface_type		 type;
	uint8_t			 if_type;
	uint16_t		 flags;
	uint8_t			 linkstate;
	struct iface_af		 ipv4;
	struct iface_af		 ipv6;
};

/* source of targeted hellos */
struct tnbr {
	LIST_ENTRY(tnbr)	 entry;
	struct event		 hello_timer;
	struct adj		*adj;
	int			 af;
	union ldpd_addr		 addr;
	int			 state;
	uint16_t		 hello_holdtime;
	uint16_t		 hello_interval;
	uint16_t		 pw_count;
	uint8_t			 flags;
};
#define F_TNBR_CONFIGURED	 0x01
#define F_TNBR_DYNAMIC		 0x02

/* neighbor specific parameters */
struct nbr_params {
	LIST_ENTRY(nbr_params)	 entry;
	struct in_addr		 lsr_id;
	uint16_t		 keepalive;
	int			 gtsm_enabled;
	uint8_t			 gtsm_hops;
	uint8_t			 flags;
};
#define F_NBRP_KEEPALIVE	 0x01
#define F_NBRP_GTSM		 0x02
#define F_NBRP_GTSM_HOPS	 0x04

struct l2vpn_if {
	LIST_ENTRY(l2vpn_if)	 entry;
	struct l2vpn		*l2vpn;
	char			 ifname[IF_NAMESIZE];
	unsigned int		 ifindex;
	uint16_t		 flags;
	uint8_t			 linkstate;
	uint8_t			 mac[ETHER_ADDR_LEN];
};

struct l2vpn_pw {
	LIST_ENTRY(l2vpn_pw)	 entry;
	struct l2vpn		*l2vpn;
	struct in_addr		 lsr_id;
	int			 af;
	union ldpd_addr		 addr;
	uint32_t		 pwid;
	char			 ifname[IF_NAMESIZE];
	unsigned int		 ifindex;
	uint32_t		 remote_group;
	uint16_t		 remote_mtu;
	uint32_t		 remote_status;
	uint8_t			 flags;
};
#define F_PW_STATUSTLV_CONF	0x01	/* status tlv configured */
#define F_PW_STATUSTLV		0x02	/* status tlv negotiated */
#define F_PW_CWORD_CONF		0x04	/* control word configured */
#define F_PW_CWORD		0x08	/* control word negotiated */
#define F_PW_STATUS_UP		0x10	/* pseudowire is operational */

struct l2vpn {
	LIST_ENTRY(l2vpn)	 entry;
	char			 name[L2VPN_NAME_LEN];
	int			 type;
	int			 pw_type;
	int			 mtu;
	char			 br_ifname[IF_NAMESIZE];
	unsigned int		 br_ifindex;
	LIST_HEAD(, l2vpn_if)	 if_list;
	LIST_HEAD(, l2vpn_pw)	 pw_list;
};
#define L2VPN_TYPE_VPWS		1
#define L2VPN_TYPE_VPLS		2

/* ldp_conf */
enum ldpd_process {
	PROC_MAIN,
	PROC_LDP_ENGINE,
	PROC_LDE_ENGINE
};
extern enum ldpd_process	ldpd_process;

enum socket_type {
	LDP_SOCKET_DISC,
	LDP_SOCKET_EDISC,
	LDP_SOCKET_SESSION
};

enum hello_type {
	HELLO_LINK,
	HELLO_TARGETED
};

struct ldpd_af_conf {
	uint16_t		 keepalive;
	uint16_t		 thello_holdtime;
	uint16_t		 thello_interval;
	union ldpd_addr		 trans_addr;
	int			 flags;
};
#define	F_LDPD_AF_ENABLED	0x0001
#define	F_LDPD_AF_THELLO_ACCEPT	0x0002
#define	F_LDPD_AF_EXPNULL	0x0004
#define	F_LDPD_AF_NO_GTSM	0x0008

struct ldp_auth {
	LIST_ENTRY(ldp_auth)	 entry;
	char			 md5key[TCP_MD5_KEY_LEN];
	unsigned int		 md5key_len;
	struct in_addr		 id;
	int			 idlen;
};

#define LDP_AUTH_REQUIRED(_a)	 ((_a)->md5key_len != 0)

struct ldpd_conf {
	struct in_addr		 rtr_id;
	unsigned int		 rdomain;
	struct ldpd_af_conf	 ipv4;
	struct ldpd_af_conf	 ipv6;
	LIST_HEAD(, iface)	 iface_list;
	LIST_HEAD(, tnbr)	 tnbr_list;
	LIST_HEAD(, nbr_params)	 nbrp_list;
	LIST_HEAD(, l2vpn)	 l2vpn_list;
	LIST_HEAD(, ldp_auth)	 auth_list;
	uint16_t		 trans_pref;
	int			 flags;
};
#define	F_LDPD_NO_FIB_UPDATE	0x0001
#define	F_LDPD_DS_CISCO_INTEROP	0x0002

struct ldpd_af_global {
	struct event		 disc_ev;
	struct event		 edisc_ev;
	int			 ldp_disc_socket;
	int			 ldp_edisc_socket;
	int			 ldp_session_socket;
};

struct ldpd_global {
	int			 cmd_opts;
	char			*csock;
	time_t			 uptime;
	struct ldpd_af_global	 ipv4;
	struct ldpd_af_global	 ipv6;
	uint32_t		 conf_seqnum;
	int			 pfkeysock;
	struct if_addr_head	 addr_list;
	LIST_HEAD(, adj)	 adj_list;
	struct in_addr		 mcast_addr_v4;
	struct in6_addr		 mcast_addr_v6;
	TAILQ_HEAD(, pending_conn) pending_conns;
};

/* kroute */
struct kroute {
	int			 af;
	union ldpd_addr		 prefix;
	uint8_t			 prefixlen;
	union ldpd_addr		 nexthop;
	uint32_t		 local_label;
	uint32_t		 remote_label;
	unsigned short		 ifindex;
	uint8_t			 priority;
	uint16_t		 flags;
};

struct kpw {
	unsigned short		 ifindex;
	int			 pw_type;
	int			 af;
	union ldpd_addr		 nexthop;
	uint32_t		 local_label;
	uint32_t		 remote_label;
	uint8_t			 flags;
};

struct kaddr {
	unsigned short		 ifindex;
	int			 af;
	union ldpd_addr		 addr;
	uint8_t			 prefixlen;
	union ldpd_addr	 	 dstbrd;
};

struct kif {
	char			 ifname[IF_NAMESIZE];
	unsigned short		 ifindex;
	int			 flags;
	uint8_t			 link_state;
	uint8_t			 mac[ETHER_ADDR_LEN];
	int			 mtu;
	unsigned int		 rdomain;
	uint8_t			 if_type;
	uint64_t		 baudrate;
};

/* control data structures */
struct ctl_iface {
	int			 af;
	char			 name[IF_NAMESIZE];
	unsigned int		 ifindex;
	int			 state;
	uint16_t		 flags;
	uint8_t			 linkstate;
	enum iface_type		 type;
	uint8_t			 if_type;
	uint16_t		 hello_holdtime;
	uint16_t		 hello_interval;
	time_t			 uptime;
	uint16_t		 adj_cnt;
};

struct ctl_adj {
	int			 af;
	struct in_addr		 id;
	enum hello_type		 type;
	char			 ifname[IF_NAMESIZE];
	union ldpd_addr		 src_addr;
	uint16_t		 holdtime;
	union ldpd_addr		 trans_addr;
};

struct ctl_nbr {
	int			 af;
	struct in_addr		 id;
	union ldpd_addr		 laddr;
	union ldpd_addr		 raddr;
	time_t			 uptime;
	int			 nbr_state;
};

struct ctl_rt {
	int			 af;
	union ldpd_addr		 prefix;
	uint8_t			 prefixlen;
	struct in_addr		 nexthop;	/* lsr-id */
	uint32_t		 local_label;
	uint32_t		 remote_label;
	uint8_t			 flags;
	uint8_t			 in_use;
};

struct ctl_pw {
	uint16_t		 type;
	char			 ifname[IF_NAMESIZE];
	uint32_t		 pwid;
	struct in_addr		 lsr_id;
	uint32_t		 local_label;
	uint32_t		 local_gid;
	uint16_t		 local_ifmtu;
	uint32_t		 remote_label;
	uint32_t		 remote_gid;
	uint16_t		 remote_ifmtu;
	uint32_t		 status;
};

extern struct ldpd_conf		*ldpd_conf;
extern struct ldpd_global	 global;

/* parse.y */
struct ldpd_conf	*parse_config(char *);
int			 cmdline_symset(char *);

/* kroute.c */
int		 kif_init(void);
int		 kr_init(int, unsigned int);
void		 kif_redistribute(const char *);
int		 kr_change(struct kroute *);
int		 kr_delete(struct kroute *);
void		 kr_shutdown(void);
void		 kr_fib_couple(void);
void		 kr_fib_decouple(void);
void		 kr_change_egress_label(int, int);
void		 kr_show_route(struct imsg *);
void		 kr_ifinfo(char *, pid_t);
struct kif	*kif_findname(char *);
void		 kif_clear(void);
int		 kmpw_set(struct kpw *);
int		 kmpw_unset(struct kpw *);
int		 kmpw_find(const char *);

/* util.c */
uint8_t		 mask2prefixlen(in_addr_t);
uint8_t		 mask2prefixlen6(struct sockaddr_in6 *);
in_addr_t	 prefixlen2mask(uint8_t);
struct in6_addr	*prefixlen2mask6(uint8_t);
void		 ldp_applymask(int, union ldpd_addr *,
		    const union ldpd_addr *, int);
int		 ldp_addrcmp(int, const union ldpd_addr *,
		    const union ldpd_addr *);
int		 ldp_addrisset(int, const union ldpd_addr *);
int		 ldp_prefixcmp(int, const union ldpd_addr *,
		    const union ldpd_addr *, uint8_t);
int		 bad_addr_v4(struct in_addr);
int		 bad_addr_v6(struct in6_addr *);
int		 bad_addr(int, union ldpd_addr *);
void		 embedscope(struct sockaddr_in6 *);
void		 recoverscope(struct sockaddr_in6 *);
void		 addscope(struct sockaddr_in6 *, uint32_t);
void		 clearscope(struct in6_addr *);
struct sockaddr	*addr2sa(int af, union ldpd_addr *, uint16_t);
void		 sa2addr(struct sockaddr *, int *, union ldpd_addr *);

/* ldpd.c */
void			 main_imsg_compose_ldpe(int, pid_t, void *, uint16_t);
void			 main_imsg_compose_lde(int, pid_t, void *, uint16_t);
void			 imsg_event_add(struct imsgev *);
int			 imsg_compose_event(struct imsgev *, uint16_t, uint32_t, pid_t,
			    int, void *, uint16_t);
void			 evbuf_enqueue(struct evbuf *, struct ibuf *);
void			 evbuf_event_add(struct evbuf *);
void			 evbuf_init(struct evbuf *, int, void (*)(int, short, void *), void *);
void			 evbuf_clear(struct evbuf *);
struct ldpd_af_conf	*ldp_af_conf_get(struct ldpd_conf *, int);
struct ldpd_af_global	*ldp_af_global_get(struct ldpd_global *, int);
int			 ldp_is_dual_stack(struct ldpd_conf *);
void			 merge_config(struct ldpd_conf *, struct ldpd_conf *);
struct ldpd_conf	*config_new_empty(void);
void			 config_clear(struct ldpd_conf *);

/* socket.c */
int		 ldp_create_socket(int, enum socket_type);
void		 sock_set_recvbuf(int);
int		 sock_set_reuse(int, int);
int		 sock_set_bindany(int, int);
int		 sock_set_ipv4_tos(int, int);
int		 sock_set_ipv4_recvif(int, int);
int		 sock_set_ipv4_minttl(int, int);
int		 sock_set_ipv4_ucast_ttl(int fd, int);
int		 sock_set_ipv4_mcast_ttl(int, uint8_t);
int		 sock_set_ipv4_mcast(struct iface *);
int		 sock_set_ipv4_mcast_loop(int);
int		 sock_set_ipv6_dscp(int, int);
int		 sock_set_ipv6_pktinfo(int, int);
int		 sock_set_ipv6_minhopcount(int, int);
int		 sock_set_ipv6_ucast_hops(int, int);
int		 sock_set_ipv6_mcast_hops(int, int);
int		 sock_set_ipv6_mcast(struct iface *);
int		 sock_set_ipv6_mcast_loop(int);

/* printconf.c */
void	print_config(struct ldpd_conf *);

/* logmsg.h */
struct in6_addr;
union ldpd_addr;
struct hello_source;
struct fec;

const char	*log_sockaddr(void *);
const char	*log_in6addr(const struct in6_addr *);
const char	*log_in6addr_scope(const struct in6_addr *, unsigned int);
const char	*log_addr(int, const union ldpd_addr *);
char		*log_label(uint32_t);
char		*log_hello_src(const struct hello_source *);
const char	*log_map(const struct map *);
const char	*log_fec(const struct fec *);
const char	*af_name(int);
const char	*socket_name(int);
const char	*nbr_state_name(int);
const char	*if_state_name(int);
const char	*if_type_name(enum iface_type);
const char	*msg_name(uint16_t);
const char	*status_code_name(uint32_t);
const char	*pw_type_name(uint16_t);

#endif	/* _LDPD_H_ */
