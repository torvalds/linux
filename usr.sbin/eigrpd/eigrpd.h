/*	$OpenBSD: eigrpd.h,v 1.27 2021/11/03 13:48:46 deraadt Exp $ */

/*
 * Copyright (c) 2015 Renato Westphal <renato@openbsd.org>
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

#ifndef _EIGRPD_H_
#define _EIGRPD_H_

#include <sys/queue.h>
#include <sys/tree.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>

#include <event.h>
#include <imsg.h>

#include "eigrp.h"

#define CONF_FILE		"/etc/eigrpd.conf"
#define	EIGRPD_SOCKET		"/var/run/eigrpd.sock"
#define EIGRPD_USER		"_eigrpd"

#define EIGRPD_OPT_VERBOSE	0x00000001
#define EIGRPD_OPT_VERBOSE2	0x00000002
#define EIGRPD_OPT_NOACTION	0x00000004

#define NBR_IDSELF		1
#define NBR_CNTSTART		(NBR_IDSELF + 1)

#define	READ_BUF_SIZE		65535
#define	PKG_DEF_SIZE		512	/* compromise */
#define	RT_BUF_SIZE		16384
#define	MAX_RTSOCK_BUF		(2 * 1024 * 1024)

#define	F_EIGRPD_INSERTED	0x0001
#define	F_KERNEL		0x0002
#define	F_CONNECTED		0x0004
#define	F_STATIC		0x0008
#define	F_DYNAMIC		0x0010
#define F_DOWN                  0x0020
#define	F_REJECT		0x0040
#define	F_BLACKHOLE		0x0080
#define	F_REDISTRIBUTED		0x0100
#define	F_CTL_EXTERNAL		0x0200	/* only used by eigrpctl */
#define	F_CTL_ACTIVE		0x0400
#define	F_CTL_ALLLINKS		0x0800

struct imsgev {
	struct imsgbuf		 ibuf;
	void			(*handler)(int, short, void *);
	struct event		 ev;
	short			 events;
};

enum imsg_type {
	IMSG_CTL_RELOAD,
	IMSG_CTL_SHOW_INTERFACE,
	IMSG_CTL_SHOW_NBR,
	IMSG_CTL_SHOW_TOPOLOGY,
	IMSG_CTL_SHOW_STATS,
	IMSG_CTL_CLEAR_NBR,
	IMSG_CTL_FIB_COUPLE,
	IMSG_CTL_FIB_DECOUPLE,
	IMSG_CTL_IFACE,
	IMSG_CTL_KROUTE,
	IMSG_CTL_IFINFO,
	IMSG_CTL_END,
	IMSG_CTL_LOG_VERBOSE,
	IMSG_KROUTE_CHANGE,
	IMSG_KROUTE_DELETE,
	IMSG_NONE,
	IMSG_IFINFO,
	IMSG_IFDOWN,
	IMSG_NEWADDR,
	IMSG_DELADDR,
	IMSG_NETWORK_ADD,
	IMSG_NETWORK_DEL,
	IMSG_NEIGHBOR_UP,
	IMSG_NEIGHBOR_DOWN,
	IMSG_RECV_UPDATE_INIT,
	IMSG_RECV_UPDATE,
	IMSG_RECV_QUERY,
	IMSG_RECV_REPLY,
	IMSG_RECV_SIAQUERY,
	IMSG_RECV_SIAREPLY,
	IMSG_SEND_UPDATE,
	IMSG_SEND_QUERY,
	IMSG_SEND_REPLY,
	IMSG_SEND_MUPDATE,
	IMSG_SEND_MQUERY,
	IMSG_SEND_UPDATE_END,
	IMSG_SEND_REPLY_END,
	IMSG_SEND_SIAQUERY_END,
	IMSG_SEND_SIAREPLY_END,
	IMSG_SEND_MUPDATE_END,
	IMSG_SEND_MQUERY_END,
	IMSG_SOCKET_IPC,
	IMSG_RECONF_CONF,
	IMSG_RECONF_IFACE,
	IMSG_RECONF_INSTANCE,
	IMSG_RECONF_EIGRP_IFACE,
	IMSG_RECONF_END
};

/* forward declarations */
struct eigrp_iface;
RB_HEAD(iface_id_head, eigrp_iface);
struct nbr;
RB_HEAD(nbr_addr_head, nbr);
RB_HEAD(nbr_pid_head, nbr);
struct rde_nbr;
RB_HEAD(rde_nbr_head, rde_nbr);
struct rt_node;
RB_HEAD(rt_tree, rt_node);

union eigrpd_addr {
	struct in_addr	v4;
	struct in6_addr	v6;
};

#define IN6_IS_SCOPE_EMBED(a)   \
	((IN6_IS_ADDR_LINKLOCAL(a)) ||  \
	 (IN6_IS_ADDR_MC_LINKLOCAL(a)) || \
	 (IN6_IS_ADDR_MC_INTFACELOCAL(a)))

/* interface types */
enum iface_type {
	IF_TYPE_POINTOPOINT,
	IF_TYPE_BROADCAST
};

struct if_addr {
	TAILQ_ENTRY(if_addr)	 entry;
	int			 af;
	union eigrpd_addr	 addr;
	uint8_t			 prefixlen;
	union eigrpd_addr	 dstbrd;
};
TAILQ_HEAD(if_addr_head, if_addr);

struct iface {
	TAILQ_ENTRY(iface)	 entry;
	TAILQ_HEAD(, eigrp_iface) ei_list;
	unsigned int		 ifindex;
	unsigned int		 rdomain;
	char			 name[IF_NAMESIZE];
	struct if_addr_head	 addr_list;
	struct in6_addr		 linklocal;
	int			 mtu;
	enum iface_type		 type;
	uint8_t			 if_type;
	uint64_t		 baudrate;
	uint16_t		 flags;
	uint8_t			 linkstate;
	uint8_t			 group_count_v4;
	uint8_t			 group_count_v6;
};

enum route_type {
	EIGRP_ROUTE_INTERNAL,
	EIGRP_ROUTE_EXTERNAL
};

/* routing information advertised by update/query/reply messages */
struct rinfo {
	int			 af;
	enum route_type		 type;
	union eigrpd_addr	 prefix;
	uint8_t			 prefixlen;
	union eigrpd_addr	 nexthop;
	struct classic_metric	 metric;
	struct classic_emetric	 emetric;
};

struct rinfo_entry {
	TAILQ_ENTRY(rinfo_entry) entry;
	struct rinfo		 rinfo;
};
TAILQ_HEAD(rinfo_head, rinfo_entry);

/* interface states */
#define	IF_STA_DOWN		0x01
#define	IF_STA_ACTIVE		0x02

struct summary_addr {
	TAILQ_ENTRY(summary_addr) entry;
	union eigrpd_addr	 prefix;
	uint8_t			 prefixlen;
};

struct eigrp_iface {
	RB_ENTRY(eigrp_iface)	 id_tree;
	TAILQ_ENTRY(eigrp_iface) e_entry;
	TAILQ_ENTRY(eigrp_iface) i_entry;
	struct eigrp		*eigrp;
	struct iface		*iface;
	int			 state;
	uint32_t		 ifaceid;
	struct event		 hello_timer;
	uint32_t		 delay;
	uint32_t		 bandwidth;
	uint16_t		 hello_holdtime;
	uint16_t		 hello_interval;
	uint8_t			 splithorizon;
	uint8_t			 passive;
	time_t			 uptime;
	TAILQ_HEAD(, nbr)	 nbr_list;
	struct nbr		*self;
	struct rinfo_head	 update_list;	/* multicast updates */
	struct rinfo_head	 query_list;	/* multicast queries */
	TAILQ_HEAD(, summary_addr) summary_list;
};
RB_PROTOTYPE(iface_id_head, eigrp_iface, id_tree, iface_id_compare)

struct seq_addr_entry {
	TAILQ_ENTRY(seq_addr_entry) entry;
	int			 af;
	union eigrpd_addr	 addr;
};
TAILQ_HEAD(seq_addr_head, seq_addr_entry);

#define	REDIST_STATIC		0x01
#define	REDIST_RIP		0x02
#define	REDIST_OSPF		0x04
#define	REDIST_CONNECTED	0x08
#define	REDIST_DEFAULT		0x10
#define	REDIST_ADDR		0x20
#define	REDIST_NO		0x40

struct redist_metric {
	uint32_t		 bandwidth;
	uint32_t		 delay;
	uint8_t			 reliability;
	uint8_t			 load;
	uint16_t		 mtu;
};

struct redistribute {
	SIMPLEQ_ENTRY(redistribute) entry;
	uint8_t			 type;
	int			 af;
	union eigrpd_addr	 addr;
	uint8_t			 prefixlen;
	struct redist_metric	*metric;
	struct {
		uint32_t	 as;
		uint32_t	 metric;
		uint32_t	 tag;
	} emetric;
};
SIMPLEQ_HEAD(redist_list, redistribute);

struct eigrp_stats {
	uint32_t		 hellos_sent;
	uint32_t		 hellos_recv;
	uint32_t		 updates_sent;
	uint32_t		 updates_recv;
	uint32_t		 queries_sent;
	uint32_t		 queries_recv;
	uint32_t		 replies_sent;
	uint32_t		 replies_recv;
	uint32_t		 acks_sent;
	uint32_t		 acks_recv;
	uint32_t		 squeries_sent;
	uint32_t		 squeries_recv;
	uint32_t		 sreplies_sent;
	uint32_t		 sreplies_recv;
};

/* eigrp instance */
struct eigrp {
	TAILQ_ENTRY(eigrp)	 entry;
	int			 af;
	uint16_t		 as;
	uint8_t			 kvalues[6];
	uint16_t		 active_timeout;
	uint8_t			 maximum_hops;
	uint8_t			 maximum_paths;
	uint8_t			 variance;
	struct redist_metric	*dflt_metric;
	struct redist_list	 redist_list;
	TAILQ_HEAD(, eigrp_iface) ei_list;
	struct nbr_addr_head	 nbrs;
	struct rde_nbr		*rnbr_redist;
	struct rde_nbr		*rnbr_summary;
	struct rt_tree		 topology;
	uint32_t		 seq_num;
	struct eigrp_stats	 stats;
};

/* eigrp_conf */
enum eigrpd_process {
	PROC_MAIN,
	PROC_EIGRP_ENGINE,
	PROC_RDE_ENGINE
};

struct eigrpd_conf {
	struct in_addr		 rtr_id;
	unsigned int		 rdomain;
	uint8_t			 fib_priority_internal;
	uint8_t			 fib_priority_external;
	uint8_t			 fib_priority_summary;
	TAILQ_HEAD(, iface)	 iface_list;
	TAILQ_HEAD(, eigrp)	 instances;
	int			 flags;
#define	EIGRPD_FLAG_NO_FIB_UPDATE 0x0001
};

struct eigrpd_global {
	int			 cmd_opts;
	time_t			 uptime;
	int			 eigrp_socket_v4;
	int			 eigrp_socket_v6;
	struct in_addr		 mcast_addr_v4;
	struct in6_addr		 mcast_addr_v6;
};

extern struct eigrpd_global global;

/* kroute */
struct kroute {
	int			 af;
	union eigrpd_addr	 prefix;
	uint8_t			 prefixlen;
	union eigrpd_addr	 nexthop;
	unsigned short		 ifindex;
	uint8_t			 priority;
	uint16_t		 flags;
};

struct kaddr {
	unsigned short		 ifindex;
	int			 af;
	union eigrpd_addr	 addr;
	uint8_t			 prefixlen;
	union eigrpd_addr	 dstbrd;
};

struct kif {
	char			 ifname[IF_NAMESIZE];
	unsigned short		 ifindex;
	int			 flags;
	uint8_t			 link_state;
	int			 mtu;
	unsigned int		 rdomain;
	uint8_t			 if_type;
	uint64_t		 baudrate;
	uint8_t			 nh_reachable;	/* for nexthop verification */
};

/* control data structures */
struct ctl_iface {
	int			 af;
	uint16_t		 as;
	char			 name[IF_NAMESIZE];
	unsigned int		 ifindex;
	union eigrpd_addr	 addr;
	uint8_t			 prefixlen;
	uint16_t		 flags;
	uint8_t			 linkstate;
	int			 mtu;
	enum iface_type		 type;
	uint8_t			 if_type;
	uint64_t		 baudrate;
	uint32_t		 delay;
	uint32_t		 bandwidth;
	uint16_t		 hello_holdtime;
	uint16_t		 hello_interval;
	uint8_t			 splithorizon;
	uint8_t			 passive;
	time_t			 uptime;
	int			 nbr_cnt;
};

struct ctl_nbr {
	int			 af;
	uint16_t		 as;
	char			 ifname[IF_NAMESIZE];
	union eigrpd_addr	 addr;
	uint16_t		 hello_holdtime;
	time_t			 uptime;
};

struct ctl_rt {
	int			 af;
	uint16_t		 as;
	union eigrpd_addr	 prefix;
	uint8_t			 prefixlen;
	enum route_type		 type;
	union eigrpd_addr	 nexthop;
	char			 ifname[IF_NAMESIZE];
	uint32_t		 distance;
	uint32_t		 rdistance;
	uint32_t		 fdistance;
	int			 state;
	uint8_t			 flags;
	struct {
		uint32_t	 delay;
		uint32_t	 bandwidth;
		uint32_t	 mtu;
		uint8_t		 hop_count;
		uint8_t		 reliability;
		uint8_t		 load;
	} metric;
	struct classic_emetric	 emetric;
};
#define	F_CTL_RT_FIRST		0x01
#define	F_CTL_RT_SUCCESSOR	0x02
#define	F_CTL_RT_FSUCCESSOR	0x04

struct ctl_show_topology_req {
	int			 af;
	union eigrpd_addr	 prefix;
	uint8_t			 prefixlen;
	uint16_t		 flags;
};

struct ctl_stats {
	int			 af;
	uint16_t		 as;
	struct eigrp_stats	 stats;
};

#define min(x,y) ((x) <= (y) ? (x) : (y))
#define max(x,y) ((x) > (y) ? (x) : (y))

extern struct eigrpd_conf	*eigrpd_conf;
extern struct iface_id_head	 ifaces_by_id;

/* parse.y */
struct eigrpd_conf	*parse_config(char *);
int			 cmdline_symset(char *);

/* in_cksum.c */
uint16_t	 in_cksum(void *, size_t);

/* kroute.c */
int		 kif_init(void);
int		 kr_init(int, unsigned int);
void		 kif_redistribute(void);
int		 kr_change(struct kroute *);
int		 kr_delete(struct kroute *);
void		 kr_shutdown(void);
void		 kr_fib_couple(void);
void		 kr_fib_decouple(void);
void		 kr_show_route(struct imsg *);
void		 kr_ifinfo(char *, pid_t);
struct kif	*kif_findname(char *);
void		 kif_clear(void);

/* util.c */
uint8_t		 mask2prefixlen(in_addr_t);
uint8_t		 mask2prefixlen6(struct sockaddr_in6 *);
in_addr_t	 prefixlen2mask(uint8_t);
struct in6_addr	*prefixlen2mask6(uint8_t);
void		 eigrp_applymask(int, union eigrpd_addr *,
		    const union eigrpd_addr *, int);
int		 eigrp_addrcmp(int, const union eigrpd_addr *,
		    const union eigrpd_addr *);
int		 eigrp_addrisset(int, const union eigrpd_addr *);
int		 eigrp_prefixcmp(int, const union eigrpd_addr *,
		    const union eigrpd_addr *, uint8_t);
int		 bad_addr_v4(struct in_addr);
int		 bad_addr_v6(struct in6_addr *);
int		 bad_addr(int, union eigrpd_addr *);
void		 embedscope(struct sockaddr_in6 *);
void		 recoverscope(struct sockaddr_in6 *);
void		 addscope(struct sockaddr_in6 *, uint32_t);
void		 clearscope(struct in6_addr *);
void		 sa2addr(struct sockaddr *, int *, union eigrpd_addr *);

/* eigrpd.c */
int		 main_imsg_compose_eigrpe(int, pid_t, void *, uint16_t);
int		 main_imsg_compose_rde(int, pid_t, void *, uint16_t);
void		 imsg_event_add(struct imsgev *);
int		 imsg_compose_event(struct imsgev *, uint16_t, uint32_t,
		    pid_t, int, void *, uint16_t);
struct eigrp	*eigrp_find(struct eigrpd_conf *, int, uint16_t);
void		 merge_config(struct eigrpd_conf *, struct eigrpd_conf *,
		    enum eigrpd_process);
struct eigrpd_conf *config_new_empty(void);
void		 config_clear(struct eigrpd_conf *, enum eigrpd_process);

/* printconf.c */
void		 print_config(struct eigrpd_conf *);

/* logmsg.c */
const char	*log_in6addr(const struct in6_addr *);
const char	*log_in6addr_scope(const struct in6_addr *, unsigned int);
const char	*log_sockaddr(void *);
const char	*log_addr(int, union eigrpd_addr *);
const char	*log_prefix(struct rt_node *);
const char	*log_route_origin(int, struct rde_nbr *);
const char	*opcode_name(uint8_t);
const char	*af_name(int);
const char	*if_type_name(enum iface_type);
const char	*dual_state_name(int);
const char	*ext_proto_name(int);

#endif	/* _EIGRPD_H_ */
