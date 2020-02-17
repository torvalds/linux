/* SPDX-License-Identifier: GPL-2.0 */
/* IP Virtual Server
 * data structure and functionality definitions
 */

#ifndef _NET_IP_VS_H
#define _NET_IP_VS_H

#include <linux/ip_vs.h>                /* definitions shared with userland */

#include <asm/types.h>                  /* for __uXX types */

#include <linux/list.h>                 /* for struct list_head */
#include <linux/spinlock.h>             /* for struct rwlock_t */
#include <linux/atomic.h>               /* for struct atomic_t */
#include <linux/refcount.h>             /* for struct refcount_t */

#include <linux/compiler.h>
#include <linux/timer.h>
#include <linux/bug.h>

#include <net/checksum.h>
#include <linux/netfilter.h>		/* for union nf_inet_addr */
#include <linux/ip.h>
#include <linux/ipv6.h>			/* for struct ipv6hdr */
#include <net/ipv6.h>
#if IS_ENABLED(CONFIG_IP_VS_IPV6)
#include <linux/netfilter_ipv6/ip6_tables.h>
#endif
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
#include <net/netfilter/nf_conntrack.h>
#endif
#include <net/net_namespace.h>		/* Netw namespace */

#define IP_VS_HDR_INVERSE	1
#define IP_VS_HDR_ICMP		2

/* Generic access of ipvs struct */
static inline struct netns_ipvs *net_ipvs(struct net* net)
{
	return net->ipvs;
}

/* Connections' size value needed by ip_vs_ctl.c */
extern int ip_vs_conn_tab_size;

struct ip_vs_iphdr {
	int hdr_flags;	/* ipvs flags */
	__u32 off;	/* Where IP or IPv4 header starts */
	__u32 len;	/* IPv4 simply where L4 starts
			 * IPv6 where L4 Transport Header starts */
	__u16 fragoffs; /* IPv6 fragment offset, 0 if first frag (or not frag)*/
	__s16 protocol;
	__s32 flags;
	union nf_inet_addr saddr;
	union nf_inet_addr daddr;
};

static inline void *frag_safe_skb_hp(const struct sk_buff *skb, int offset,
				      int len, void *buffer)
{
	return skb_header_pointer(skb, offset, len, buffer);
}

/* This function handles filling *ip_vs_iphdr, both for IPv4 and IPv6.
 * IPv6 requires some extra work, as finding proper header position,
 * depend on the IPv6 extension headers.
 */
static inline int
ip_vs_fill_iph_skb_off(int af, const struct sk_buff *skb, int offset,
		       int hdr_flags, struct ip_vs_iphdr *iphdr)
{
	iphdr->hdr_flags = hdr_flags;
	iphdr->off = offset;

#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6) {
		struct ipv6hdr _iph;
		const struct ipv6hdr *iph = skb_header_pointer(
			skb, offset, sizeof(_iph), &_iph);
		if (!iph)
			return 0;

		iphdr->saddr.in6 = iph->saddr;
		iphdr->daddr.in6 = iph->daddr;
		/* ipv6_find_hdr() updates len, flags */
		iphdr->len	 = offset;
		iphdr->flags	 = 0;
		iphdr->protocol  = ipv6_find_hdr(skb, &iphdr->len, -1,
						 &iphdr->fragoffs,
						 &iphdr->flags);
		if (iphdr->protocol < 0)
			return 0;
	} else
#endif
	{
		struct iphdr _iph;
		const struct iphdr *iph = skb_header_pointer(
			skb, offset, sizeof(_iph), &_iph);
		if (!iph)
			return 0;

		iphdr->len	= offset + iph->ihl * 4;
		iphdr->fragoffs	= 0;
		iphdr->protocol	= iph->protocol;
		iphdr->saddr.ip	= iph->saddr;
		iphdr->daddr.ip	= iph->daddr;
	}

	return 1;
}

static inline int
ip_vs_fill_iph_skb_icmp(int af, const struct sk_buff *skb, int offset,
			bool inverse, struct ip_vs_iphdr *iphdr)
{
	int hdr_flags = IP_VS_HDR_ICMP;

	if (inverse)
		hdr_flags |= IP_VS_HDR_INVERSE;

	return ip_vs_fill_iph_skb_off(af, skb, offset, hdr_flags, iphdr);
}

static inline int
ip_vs_fill_iph_skb(int af, const struct sk_buff *skb, bool inverse,
		   struct ip_vs_iphdr *iphdr)
{
	int hdr_flags = 0;

	if (inverse)
		hdr_flags |= IP_VS_HDR_INVERSE;

	return ip_vs_fill_iph_skb_off(af, skb, skb_network_offset(skb),
				      hdr_flags, iphdr);
}

static inline bool
ip_vs_iph_inverse(const struct ip_vs_iphdr *iph)
{
	return !!(iph->hdr_flags & IP_VS_HDR_INVERSE);
}

static inline bool
ip_vs_iph_icmp(const struct ip_vs_iphdr *iph)
{
	return !!(iph->hdr_flags & IP_VS_HDR_ICMP);
}

static inline void ip_vs_addr_copy(int af, union nf_inet_addr *dst,
				   const union nf_inet_addr *src)
{
#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6)
		dst->in6 = src->in6;
	else
#endif
	dst->ip = src->ip;
}

static inline void ip_vs_addr_set(int af, union nf_inet_addr *dst,
				  const union nf_inet_addr *src)
{
#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6) {
		dst->in6 = src->in6;
		return;
	}
#endif
	dst->ip = src->ip;
	dst->all[1] = 0;
	dst->all[2] = 0;
	dst->all[3] = 0;
}

static inline int ip_vs_addr_equal(int af, const union nf_inet_addr *a,
				   const union nf_inet_addr *b)
{
#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6)
		return ipv6_addr_equal(&a->in6, &b->in6);
#endif
	return a->ip == b->ip;
}

#ifdef CONFIG_IP_VS_DEBUG
#include <linux/net.h>

int ip_vs_get_debug_level(void);

static inline const char *ip_vs_dbg_addr(int af, char *buf, size_t buf_len,
					 const union nf_inet_addr *addr,
					 int *idx)
{
	int len;
#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6)
		len = snprintf(&buf[*idx], buf_len - *idx, "[%pI6c]",
			       &addr->in6) + 1;
	else
#endif
		len = snprintf(&buf[*idx], buf_len - *idx, "%pI4",
			       &addr->ip) + 1;

	*idx += len;
	BUG_ON(*idx > buf_len + 1);
	return &buf[*idx - len];
}

#define IP_VS_DBG_BUF(level, msg, ...)					\
	do {								\
		char ip_vs_dbg_buf[160];				\
		int ip_vs_dbg_idx = 0;					\
		if (level <= ip_vs_get_debug_level())			\
			printk(KERN_DEBUG pr_fmt(msg), ##__VA_ARGS__);	\
	} while (0)
#define IP_VS_ERR_BUF(msg...)						\
	do {								\
		char ip_vs_dbg_buf[160];				\
		int ip_vs_dbg_idx = 0;					\
		pr_err(msg);						\
	} while (0)

/* Only use from within IP_VS_DBG_BUF() or IP_VS_ERR_BUF macros */
#define IP_VS_DBG_ADDR(af, addr)					\
	ip_vs_dbg_addr(af, ip_vs_dbg_buf,				\
		       sizeof(ip_vs_dbg_buf), addr,			\
		       &ip_vs_dbg_idx)

#define IP_VS_DBG(level, msg, ...)					\
	do {								\
		if (level <= ip_vs_get_debug_level())			\
			printk(KERN_DEBUG pr_fmt(msg), ##__VA_ARGS__);	\
	} while (0)
#define IP_VS_DBG_RL(msg, ...)						\
	do {								\
		if (net_ratelimit())					\
			printk(KERN_DEBUG pr_fmt(msg), ##__VA_ARGS__);	\
	} while (0)
#define IP_VS_DBG_PKT(level, af, pp, skb, ofs, msg)			\
	do {								\
		if (level <= ip_vs_get_debug_level())			\
			pp->debug_packet(af, pp, skb, ofs, msg);	\
	} while (0)
#define IP_VS_DBG_RL_PKT(level, af, pp, skb, ofs, msg)			\
	do {								\
		if (level <= ip_vs_get_debug_level() &&			\
		    net_ratelimit())					\
			pp->debug_packet(af, pp, skb, ofs, msg);	\
	} while (0)
#else	/* NO DEBUGGING at ALL */
#define IP_VS_DBG_BUF(level, msg...)  do {} while (0)
#define IP_VS_ERR_BUF(msg...)  do {} while (0)
#define IP_VS_DBG(level, msg...)  do {} while (0)
#define IP_VS_DBG_RL(msg...)  do {} while (0)
#define IP_VS_DBG_PKT(level, af, pp, skb, ofs, msg)	do {} while (0)
#define IP_VS_DBG_RL_PKT(level, af, pp, skb, ofs, msg)	do {} while (0)
#endif

#define IP_VS_BUG() BUG()
#define IP_VS_ERR_RL(msg, ...)						\
	do {								\
		if (net_ratelimit())					\
			pr_err(msg, ##__VA_ARGS__);			\
	} while (0)

#ifdef CONFIG_IP_VS_DEBUG
#define EnterFunction(level)						\
	do {								\
		if (level <= ip_vs_get_debug_level())			\
			printk(KERN_DEBUG				\
			       pr_fmt("Enter: %s, %s line %i\n"),	\
			       __func__, __FILE__, __LINE__);		\
	} while (0)
#define LeaveFunction(level)						\
	do {								\
		if (level <= ip_vs_get_debug_level())			\
			printk(KERN_DEBUG				\
			       pr_fmt("Leave: %s, %s line %i\n"),	\
			       __func__, __FILE__, __LINE__);		\
	} while (0)
#else
#define EnterFunction(level)   do {} while (0)
#define LeaveFunction(level)   do {} while (0)
#endif

/* The port number of FTP service (in network order). */
#define FTPPORT  cpu_to_be16(21)
#define FTPDATA  cpu_to_be16(20)

/* TCP State Values */
enum {
	IP_VS_TCP_S_NONE = 0,
	IP_VS_TCP_S_ESTABLISHED,
	IP_VS_TCP_S_SYN_SENT,
	IP_VS_TCP_S_SYN_RECV,
	IP_VS_TCP_S_FIN_WAIT,
	IP_VS_TCP_S_TIME_WAIT,
	IP_VS_TCP_S_CLOSE,
	IP_VS_TCP_S_CLOSE_WAIT,
	IP_VS_TCP_S_LAST_ACK,
	IP_VS_TCP_S_LISTEN,
	IP_VS_TCP_S_SYNACK,
	IP_VS_TCP_S_LAST
};

/* UDP State Values */
enum {
	IP_VS_UDP_S_NORMAL,
	IP_VS_UDP_S_LAST,
};

/* ICMP State Values */
enum {
	IP_VS_ICMP_S_NORMAL,
	IP_VS_ICMP_S_LAST,
};

/* SCTP State Values */
enum ip_vs_sctp_states {
	IP_VS_SCTP_S_NONE,
	IP_VS_SCTP_S_INIT1,
	IP_VS_SCTP_S_INIT,
	IP_VS_SCTP_S_COOKIE_SENT,
	IP_VS_SCTP_S_COOKIE_REPLIED,
	IP_VS_SCTP_S_COOKIE_WAIT,
	IP_VS_SCTP_S_COOKIE,
	IP_VS_SCTP_S_COOKIE_ECHOED,
	IP_VS_SCTP_S_ESTABLISHED,
	IP_VS_SCTP_S_SHUTDOWN_SENT,
	IP_VS_SCTP_S_SHUTDOWN_RECEIVED,
	IP_VS_SCTP_S_SHUTDOWN_ACK_SENT,
	IP_VS_SCTP_S_REJECTED,
	IP_VS_SCTP_S_CLOSED,
	IP_VS_SCTP_S_LAST
};

/* Connection templates use bits from state */
#define IP_VS_CTPL_S_NONE		0x0000
#define IP_VS_CTPL_S_ASSURED		0x0001
#define IP_VS_CTPL_S_LAST		0x0002

/* Delta sequence info structure
 * Each ip_vs_conn has 2 (output AND input seq. changes).
 * Only used in the VS/NAT.
 */
struct ip_vs_seq {
	__u32			init_seq;	/* Add delta from this seq */
	__u32			delta;		/* Delta in sequence numbers */
	__u32			previous_delta;	/* Delta in sequence numbers
						 * before last resized pkt */
};

/* counters per cpu */
struct ip_vs_counters {
	__u64		conns;		/* connections scheduled */
	__u64		inpkts;		/* incoming packets */
	__u64		outpkts;	/* outgoing packets */
	__u64		inbytes;	/* incoming bytes */
	__u64		outbytes;	/* outgoing bytes */
};
/* Stats per cpu */
struct ip_vs_cpu_stats {
	struct ip_vs_counters   cnt;
	struct u64_stats_sync   syncp;
};

/* IPVS statistics objects */
struct ip_vs_estimator {
	struct list_head	list;

	u64			last_inbytes;
	u64			last_outbytes;
	u64			last_conns;
	u64			last_inpkts;
	u64			last_outpkts;

	u64			cps;
	u64			inpps;
	u64			outpps;
	u64			inbps;
	u64			outbps;
};

/*
 * IPVS statistics object, 64-bit kernel version of struct ip_vs_stats_user
 */
struct ip_vs_kstats {
	u64			conns;		/* connections scheduled */
	u64			inpkts;		/* incoming packets */
	u64			outpkts;	/* outgoing packets */
	u64			inbytes;	/* incoming bytes */
	u64			outbytes;	/* outgoing bytes */

	u64			cps;		/* current connection rate */
	u64			inpps;		/* current in packet rate */
	u64			outpps;		/* current out packet rate */
	u64			inbps;		/* current in byte rate */
	u64			outbps;		/* current out byte rate */
};

struct ip_vs_stats {
	struct ip_vs_kstats	kstats;		/* kernel statistics */
	struct ip_vs_estimator	est;		/* estimator */
	struct ip_vs_cpu_stats __percpu	*cpustats;	/* per cpu counters */
	spinlock_t		lock;		/* spin lock */
	struct ip_vs_kstats	kstats0;	/* reset values */
};

struct dst_entry;
struct iphdr;
struct ip_vs_conn;
struct ip_vs_app;
struct sk_buff;
struct ip_vs_proto_data;

struct ip_vs_protocol {
	struct ip_vs_protocol	*next;
	char			*name;
	u16			protocol;
	u16			num_states;
	int			dont_defrag;

	void (*init)(struct ip_vs_protocol *pp);

	void (*exit)(struct ip_vs_protocol *pp);

	int (*init_netns)(struct netns_ipvs *ipvs, struct ip_vs_proto_data *pd);

	void (*exit_netns)(struct netns_ipvs *ipvs, struct ip_vs_proto_data *pd);

	int (*conn_schedule)(struct netns_ipvs *ipvs,
			     int af, struct sk_buff *skb,
			     struct ip_vs_proto_data *pd,
			     int *verdict, struct ip_vs_conn **cpp,
			     struct ip_vs_iphdr *iph);

	struct ip_vs_conn *
	(*conn_in_get)(struct netns_ipvs *ipvs,
		       int af,
		       const struct sk_buff *skb,
		       const struct ip_vs_iphdr *iph);

	struct ip_vs_conn *
	(*conn_out_get)(struct netns_ipvs *ipvs,
			int af,
			const struct sk_buff *skb,
			const struct ip_vs_iphdr *iph);

	int (*snat_handler)(struct sk_buff *skb, struct ip_vs_protocol *pp,
			    struct ip_vs_conn *cp, struct ip_vs_iphdr *iph);

	int (*dnat_handler)(struct sk_buff *skb, struct ip_vs_protocol *pp,
			    struct ip_vs_conn *cp, struct ip_vs_iphdr *iph);

	int (*csum_check)(int af, struct sk_buff *skb,
			  struct ip_vs_protocol *pp);

	const char *(*state_name)(int state);

	void (*state_transition)(struct ip_vs_conn *cp, int direction,
				 const struct sk_buff *skb,
				 struct ip_vs_proto_data *pd);

	int (*register_app)(struct netns_ipvs *ipvs, struct ip_vs_app *inc);

	void (*unregister_app)(struct netns_ipvs *ipvs, struct ip_vs_app *inc);

	int (*app_conn_bind)(struct ip_vs_conn *cp);

	void (*debug_packet)(int af, struct ip_vs_protocol *pp,
			     const struct sk_buff *skb,
			     int offset,
			     const char *msg);

	void (*timeout_change)(struct ip_vs_proto_data *pd, int flags);
};

/* protocol data per netns */
struct ip_vs_proto_data {
	struct ip_vs_proto_data	*next;
	struct ip_vs_protocol	*pp;
	int			*timeout_table;	/* protocol timeout table */
	atomic_t		appcnt;		/* counter of proto app incs. */
	struct tcp_states_t	*tcp_state_table;
};

struct ip_vs_protocol   *ip_vs_proto_get(unsigned short proto);
struct ip_vs_proto_data *ip_vs_proto_data_get(struct netns_ipvs *ipvs,
					      unsigned short proto);

struct ip_vs_conn_param {
	struct netns_ipvs		*ipvs;
	const union nf_inet_addr	*caddr;
	const union nf_inet_addr	*vaddr;
	__be16				cport;
	__be16				vport;
	__u16				protocol;
	u16				af;

	const struct ip_vs_pe		*pe;
	char				*pe_data;
	__u8				pe_data_len;
};

/* IP_VS structure allocated for each dynamically scheduled connection */
struct ip_vs_conn {
	struct hlist_node	c_list;         /* hashed list heads */
	/* Protocol, addresses and port numbers */
	__be16                  cport;
	__be16                  dport;
	__be16                  vport;
	u16			af;		/* address family */
	union nf_inet_addr      caddr;          /* client address */
	union nf_inet_addr      vaddr;          /* virtual address */
	union nf_inet_addr      daddr;          /* destination address */
	volatile __u32          flags;          /* status flags */
	__u16                   protocol;       /* Which protocol (TCP/UDP) */
	__u16			daf;		/* Address family of the dest */
	struct netns_ipvs	*ipvs;

	/* counter and timer */
	refcount_t		refcnt;		/* reference count */
	struct timer_list	timer;		/* Expiration timer */
	volatile unsigned long	timeout;	/* timeout */

	/* Flags and state transition */
	spinlock_t              lock;           /* lock for state transition */
	volatile __u16          state;          /* state info */
	volatile __u16          old_state;      /* old state, to be used for
						 * state transition triggerd
						 * synchronization
						 */
	__u32			fwmark;		/* Fire wall mark from skb */
	unsigned long		sync_endtime;	/* jiffies + sent_retries */

	/* Control members */
	struct ip_vs_conn       *control;       /* Master control connection */
	atomic_t                n_control;      /* Number of controlled ones */
	struct ip_vs_dest       *dest;          /* real server */
	atomic_t                in_pkts;        /* incoming packet counter */

	/* Packet transmitter for different forwarding methods.  If it
	 * mangles the packet, it must return NF_DROP or better NF_STOLEN,
	 * otherwise this must be changed to a sk_buff **.
	 * NF_ACCEPT can be returned when destination is local.
	 */
	int (*packet_xmit)(struct sk_buff *skb, struct ip_vs_conn *cp,
			   struct ip_vs_protocol *pp, struct ip_vs_iphdr *iph);

	/* Note: we can group the following members into a structure,
	 * in order to save more space, and the following members are
	 * only used in VS/NAT anyway
	 */
	struct ip_vs_app        *app;           /* bound ip_vs_app object */
	void                    *app_data;      /* Application private data */
	struct ip_vs_seq        in_seq;         /* incoming seq. struct */
	struct ip_vs_seq        out_seq;        /* outgoing seq. struct */

	const struct ip_vs_pe	*pe;
	char			*pe_data;
	__u8			pe_data_len;

	struct rcu_head		rcu_head;
};

/* Extended internal versions of struct ip_vs_service_user and ip_vs_dest_user
 * for IPv6 support.
 *
 * We need these to conveniently pass around service and destination
 * options, but unfortunately, we also need to keep the old definitions to
 * maintain userspace backwards compatibility for the setsockopt interface.
 */
struct ip_vs_service_user_kern {
	/* virtual service addresses */
	u16			af;
	u16			protocol;
	union nf_inet_addr	addr;		/* virtual ip address */
	__be16			port;
	u32			fwmark;		/* firwall mark of service */

	/* virtual service options */
	char			*sched_name;
	char			*pe_name;
	unsigned int		flags;		/* virtual service flags */
	unsigned int		timeout;	/* persistent timeout in sec */
	__be32			netmask;	/* persistent netmask or plen */
};


struct ip_vs_dest_user_kern {
	/* destination server address */
	union nf_inet_addr	addr;
	__be16			port;

	/* real server options */
	unsigned int		conn_flags;	/* connection flags */
	int			weight;		/* destination weight */

	/* thresholds for active connections */
	u32			u_threshold;	/* upper threshold */
	u32			l_threshold;	/* lower threshold */

	/* Address family of addr */
	u16			af;
};


/*
 * The information about the virtual service offered to the net and the
 * forwarding entries.
 */
struct ip_vs_service {
	struct hlist_node	s_list;   /* for normal service table */
	struct hlist_node	f_list;   /* for fwmark-based service table */
	atomic_t		refcnt;   /* reference counter */

	u16			af;       /* address family */
	__u16			protocol; /* which protocol (TCP/UDP) */
	union nf_inet_addr	addr;	  /* IP address for virtual service */
	__be16			port;	  /* port number for the service */
	__u32                   fwmark;   /* firewall mark of the service */
	unsigned int		flags;	  /* service status flags */
	unsigned int		timeout;  /* persistent timeout in ticks */
	__be32			netmask;  /* grouping granularity, mask/plen */
	struct netns_ipvs	*ipvs;

	struct list_head	destinations;  /* real server d-linked list */
	__u32			num_dests;     /* number of servers */
	struct ip_vs_stats      stats;         /* statistics for the service */

	/* for scheduling */
	struct ip_vs_scheduler __rcu *scheduler; /* bound scheduler object */
	spinlock_t		sched_lock;    /* lock sched_data */
	void			*sched_data;   /* scheduler application data */

	/* alternate persistence engine */
	struct ip_vs_pe __rcu	*pe;
	int			conntrack_afmask;

	struct rcu_head		rcu_head;
};

/* Information for cached dst */
struct ip_vs_dest_dst {
	struct dst_entry	*dst_cache;	/* destination cache entry */
	u32			dst_cookie;
	union nf_inet_addr	dst_saddr;
	struct rcu_head		rcu_head;
};

/* The real server destination forwarding entry with ip address, port number,
 * and so on.
 */
struct ip_vs_dest {
	struct list_head	n_list;   /* for the dests in the service */
	struct hlist_node	d_list;   /* for table with all the dests */

	u16			af;		/* address family */
	__be16			port;		/* port number of the server */
	union nf_inet_addr	addr;		/* IP address of the server */
	volatile unsigned int	flags;		/* dest status flags */
	atomic_t		conn_flags;	/* flags to copy to conn */
	atomic_t		weight;		/* server weight */
	atomic_t		last_weight;	/* server latest weight */

	refcount_t		refcnt;		/* reference counter */
	struct ip_vs_stats      stats;          /* statistics */
	unsigned long		idle_start;	/* start time, jiffies */

	/* connection counters and thresholds */
	atomic_t		activeconns;	/* active connections */
	atomic_t		inactconns;	/* inactive connections */
	atomic_t		persistconns;	/* persistent connections */
	__u32			u_threshold;	/* upper threshold */
	__u32			l_threshold;	/* lower threshold */

	/* for destination cache */
	spinlock_t		dst_lock;	/* lock of dst_cache */
	struct ip_vs_dest_dst __rcu *dest_dst;	/* cached dst info */

	/* for virtual service */
	struct ip_vs_service __rcu *svc;	/* service it belongs to */
	__u16			protocol;	/* which protocol (TCP/UDP) */
	__be16			vport;		/* virtual port number */
	union nf_inet_addr	vaddr;		/* virtual IP address */
	__u32			vfwmark;	/* firewall mark of service */

	struct list_head	t_list;		/* in dest_trash */
	unsigned int		in_rs_table:1;	/* we are in rs_table */
};

/* The scheduler object */
struct ip_vs_scheduler {
	struct list_head	n_list;		/* d-linked list head */
	char			*name;		/* scheduler name */
	atomic_t		refcnt;		/* reference counter */
	struct module		*module;	/* THIS_MODULE/NULL */

	/* scheduler initializing service */
	int (*init_service)(struct ip_vs_service *svc);
	/* scheduling service finish */
	void (*done_service)(struct ip_vs_service *svc);
	/* dest is linked */
	int (*add_dest)(struct ip_vs_service *svc, struct ip_vs_dest *dest);
	/* dest is unlinked */
	int (*del_dest)(struct ip_vs_service *svc, struct ip_vs_dest *dest);
	/* dest is updated */
	int (*upd_dest)(struct ip_vs_service *svc, struct ip_vs_dest *dest);

	/* selecting a server from the given service */
	struct ip_vs_dest* (*schedule)(struct ip_vs_service *svc,
				       const struct sk_buff *skb,
				       struct ip_vs_iphdr *iph);
};

/* The persistence engine object */
struct ip_vs_pe {
	struct list_head	n_list;		/* d-linked list head */
	char			*name;		/* scheduler name */
	atomic_t		refcnt;		/* reference counter */
	struct module		*module;	/* THIS_MODULE/NULL */

	/* get the connection template, if any */
	int (*fill_param)(struct ip_vs_conn_param *p, struct sk_buff *skb);
	bool (*ct_match)(const struct ip_vs_conn_param *p,
			 struct ip_vs_conn *ct);
	u32 (*hashkey_raw)(const struct ip_vs_conn_param *p, u32 initval,
			   bool inverse);
	int (*show_pe_data)(const struct ip_vs_conn *cp, char *buf);
	/* create connections for real-server outgoing packets */
	struct ip_vs_conn* (*conn_out)(struct ip_vs_service *svc,
				       struct ip_vs_dest *dest,
				       struct sk_buff *skb,
				       const struct ip_vs_iphdr *iph,
				       __be16 dport, __be16 cport);
};

/* The application module object (a.k.a. app incarnation) */
struct ip_vs_app {
	struct list_head	a_list;		/* member in app list */
	int			type;		/* IP_VS_APP_TYPE_xxx */
	char			*name;		/* application module name */
	__u16			protocol;
	struct module		*module;	/* THIS_MODULE/NULL */
	struct list_head	incs_list;	/* list of incarnations */

	/* members for application incarnations */
	struct list_head	p_list;		/* member in proto app list */
	struct ip_vs_app	*app;		/* its real application */
	__be16			port;		/* port number in net order */
	atomic_t		usecnt;		/* usage counter */
	struct rcu_head		rcu_head;

	/* output hook: Process packet in inout direction, diff set for TCP.
	 * Return: 0=Error, 1=Payload Not Mangled/Mangled but checksum is ok,
	 *	   2=Mangled but checksum was not updated
	 */
	int (*pkt_out)(struct ip_vs_app *, struct ip_vs_conn *,
		       struct sk_buff *, int *diff, struct ip_vs_iphdr *ipvsh);

	/* input hook: Process packet in outin direction, diff set for TCP.
	 * Return: 0=Error, 1=Payload Not Mangled/Mangled but checksum is ok,
	 *	   2=Mangled but checksum was not updated
	 */
	int (*pkt_in)(struct ip_vs_app *, struct ip_vs_conn *,
		      struct sk_buff *, int *diff, struct ip_vs_iphdr *ipvsh);

	/* ip_vs_app initializer */
	int (*init_conn)(struct ip_vs_app *, struct ip_vs_conn *);

	/* ip_vs_app finish */
	int (*done_conn)(struct ip_vs_app *, struct ip_vs_conn *);


	/* not used now */
	int (*bind_conn)(struct ip_vs_app *, struct ip_vs_conn *,
			 struct ip_vs_protocol *);

	void (*unbind_conn)(struct ip_vs_app *, struct ip_vs_conn *);

	int *			timeout_table;
	int *			timeouts;
	int			timeouts_size;

	int (*conn_schedule)(struct sk_buff *skb, struct ip_vs_app *app,
			     int *verdict, struct ip_vs_conn **cpp);

	struct ip_vs_conn *
	(*conn_in_get)(const struct sk_buff *skb, struct ip_vs_app *app,
		       const struct iphdr *iph, int inverse);

	struct ip_vs_conn *
	(*conn_out_get)(const struct sk_buff *skb, struct ip_vs_app *app,
			const struct iphdr *iph, int inverse);

	int (*state_transition)(struct ip_vs_conn *cp, int direction,
				const struct sk_buff *skb,
				struct ip_vs_app *app);

	void (*timeout_change)(struct ip_vs_app *app, int flags);
};

struct ipvs_master_sync_state {
	struct list_head	sync_queue;
	struct ip_vs_sync_buff	*sync_buff;
	unsigned long		sync_queue_len;
	unsigned int		sync_queue_delay;
	struct delayed_work	master_wakeup_work;
	struct netns_ipvs	*ipvs;
};

struct ip_vs_sync_thread_data;

/* How much time to keep dests in trash */
#define IP_VS_DEST_TRASH_PERIOD		(120 * HZ)

struct ipvs_sync_daemon_cfg {
	union nf_inet_addr	mcast_group;
	int			syncid;
	u16			sync_maxlen;
	u16			mcast_port;
	u8			mcast_af;
	u8			mcast_ttl;
	/* multicast interface name */
	char			mcast_ifn[IP_VS_IFNAME_MAXLEN];
};

/* IPVS in network namespace */
struct netns_ipvs {
	int			gen;		/* Generation */
	int			enable;		/* enable like nf_hooks do */
	/* Hash table: for real service lookups */
	#define IP_VS_RTAB_BITS 4
	#define IP_VS_RTAB_SIZE (1 << IP_VS_RTAB_BITS)
	#define IP_VS_RTAB_MASK (IP_VS_RTAB_SIZE - 1)

	struct hlist_head	rs_table[IP_VS_RTAB_SIZE];
	/* ip_vs_app */
	struct list_head	app_list;
	/* ip_vs_proto */
	#define IP_VS_PROTO_TAB_SIZE	32	/* must be power of 2 */
	struct ip_vs_proto_data *proto_data_table[IP_VS_PROTO_TAB_SIZE];
	/* ip_vs_proto_tcp */
#ifdef CONFIG_IP_VS_PROTO_TCP
	#define	TCP_APP_TAB_BITS	4
	#define	TCP_APP_TAB_SIZE	(1 << TCP_APP_TAB_BITS)
	#define	TCP_APP_TAB_MASK	(TCP_APP_TAB_SIZE - 1)
	struct list_head	tcp_apps[TCP_APP_TAB_SIZE];
#endif
	/* ip_vs_proto_udp */
#ifdef CONFIG_IP_VS_PROTO_UDP
	#define	UDP_APP_TAB_BITS	4
	#define	UDP_APP_TAB_SIZE	(1 << UDP_APP_TAB_BITS)
	#define	UDP_APP_TAB_MASK	(UDP_APP_TAB_SIZE - 1)
	struct list_head	udp_apps[UDP_APP_TAB_SIZE];
#endif
	/* ip_vs_proto_sctp */
#ifdef CONFIG_IP_VS_PROTO_SCTP
	#define SCTP_APP_TAB_BITS	4
	#define SCTP_APP_TAB_SIZE	(1 << SCTP_APP_TAB_BITS)
	#define SCTP_APP_TAB_MASK	(SCTP_APP_TAB_SIZE - 1)
	/* Hash table for SCTP application incarnations	 */
	struct list_head	sctp_apps[SCTP_APP_TAB_SIZE];
#endif
	/* ip_vs_conn */
	atomic_t		conn_count;      /* connection counter */

	/* ip_vs_ctl */
	struct ip_vs_stats		tot_stats;  /* Statistics & est. */

	int			num_services;    /* no of virtual services */

	/* Trash for destinations */
	struct list_head	dest_trash;
	spinlock_t		dest_trash_lock;
	struct timer_list	dest_trash_timer; /* expiration timer */
	/* Service counters */
	atomic_t		ftpsvc_counter;
	atomic_t		nullsvc_counter;
	atomic_t		conn_out_counter;

#ifdef CONFIG_SYSCTL
	/* 1/rate drop and drop-entry variables */
	struct delayed_work	defense_work;   /* Work handler */
	int			drop_rate;
	int			drop_counter;
	int			old_secure_tcp;
	atomic_t		dropentry;
	/* locks in ctl.c */
	spinlock_t		dropentry_lock;  /* drop entry handling */
	spinlock_t		droppacket_lock; /* drop packet handling */
	spinlock_t		securetcp_lock;  /* state and timeout tables */

	/* sys-ctl struct */
	struct ctl_table_header	*sysctl_hdr;
	struct ctl_table	*sysctl_tbl;
#endif

	/* sysctl variables */
	int			sysctl_amemthresh;
	int			sysctl_am_droprate;
	int			sysctl_drop_entry;
	int			sysctl_drop_packet;
	int			sysctl_secure_tcp;
#ifdef CONFIG_IP_VS_NFCT
	int			sysctl_conntrack;
#endif
	int			sysctl_snat_reroute;
	int			sysctl_sync_ver;
	int			sysctl_sync_ports;
	int			sysctl_sync_persist_mode;
	unsigned long		sysctl_sync_qlen_max;
	int			sysctl_sync_sock_size;
	int			sysctl_cache_bypass;
	int			sysctl_expire_nodest_conn;
	int			sysctl_sloppy_tcp;
	int			sysctl_sloppy_sctp;
	int			sysctl_expire_quiescent_template;
	int			sysctl_sync_threshold[2];
	unsigned int		sysctl_sync_refresh_period;
	int			sysctl_sync_retries;
	int			sysctl_nat_icmp_send;
	int			sysctl_pmtu_disc;
	int			sysctl_backup_only;
	int			sysctl_conn_reuse_mode;
	int			sysctl_schedule_icmp;
	int			sysctl_ignore_tunneled;

	/* ip_vs_lblc */
	int			sysctl_lblc_expiration;
	struct ctl_table_header	*lblc_ctl_header;
	struct ctl_table	*lblc_ctl_table;
	/* ip_vs_lblcr */
	int			sysctl_lblcr_expiration;
	struct ctl_table_header	*lblcr_ctl_header;
	struct ctl_table	*lblcr_ctl_table;
	/* ip_vs_est */
	struct list_head	est_list;	/* estimator list */
	spinlock_t		est_lock;
	struct timer_list	est_timer;	/* Estimation timer */
	/* ip_vs_sync */
	spinlock_t		sync_lock;
	struct ipvs_master_sync_state *ms;
	spinlock_t		sync_buff_lock;
	struct ip_vs_sync_thread_data *master_tinfo;
	struct ip_vs_sync_thread_data *backup_tinfo;
	int			threads_mask;
	volatile int		sync_state;
	struct mutex		sync_mutex;
	struct ipvs_sync_daemon_cfg	mcfg;	/* Master Configuration */
	struct ipvs_sync_daemon_cfg	bcfg;	/* Backup Configuration */
	/* net name space ptr */
	struct net		*net;            /* Needed by timer routines */
	/* Number of heterogeneous destinations, needed becaus heterogeneous
	 * are not supported when synchronization is enabled.
	 */
	unsigned int		mixed_address_family_dests;
};

#define DEFAULT_SYNC_THRESHOLD	3
#define DEFAULT_SYNC_PERIOD	50
#define DEFAULT_SYNC_VER	1
#define DEFAULT_SLOPPY_TCP	0
#define DEFAULT_SLOPPY_SCTP	0
#define DEFAULT_SYNC_REFRESH_PERIOD	(0U * HZ)
#define DEFAULT_SYNC_RETRIES		0
#define IPVS_SYNC_WAKEUP_RATE	8
#define IPVS_SYNC_QLEN_MAX	(IPVS_SYNC_WAKEUP_RATE * 4)
#define IPVS_SYNC_SEND_DELAY	(HZ / 50)
#define IPVS_SYNC_CHECK_PERIOD	HZ
#define IPVS_SYNC_FLUSH_TIME	(HZ * 2)
#define IPVS_SYNC_PORTS_MAX	(1 << 6)

#ifdef CONFIG_SYSCTL

static inline int sysctl_sync_threshold(struct netns_ipvs *ipvs)
{
	return ipvs->sysctl_sync_threshold[0];
}

static inline int sysctl_sync_period(struct netns_ipvs *ipvs)
{
	return READ_ONCE(ipvs->sysctl_sync_threshold[1]);
}

static inline unsigned int sysctl_sync_refresh_period(struct netns_ipvs *ipvs)
{
	return READ_ONCE(ipvs->sysctl_sync_refresh_period);
}

static inline int sysctl_sync_retries(struct netns_ipvs *ipvs)
{
	return ipvs->sysctl_sync_retries;
}

static inline int sysctl_sync_ver(struct netns_ipvs *ipvs)
{
	return ipvs->sysctl_sync_ver;
}

static inline int sysctl_sloppy_tcp(struct netns_ipvs *ipvs)
{
	return ipvs->sysctl_sloppy_tcp;
}

static inline int sysctl_sloppy_sctp(struct netns_ipvs *ipvs)
{
	return ipvs->sysctl_sloppy_sctp;
}

static inline int sysctl_sync_ports(struct netns_ipvs *ipvs)
{
	return READ_ONCE(ipvs->sysctl_sync_ports);
}

static inline int sysctl_sync_persist_mode(struct netns_ipvs *ipvs)
{
	return ipvs->sysctl_sync_persist_mode;
}

static inline unsigned long sysctl_sync_qlen_max(struct netns_ipvs *ipvs)
{
	return ipvs->sysctl_sync_qlen_max;
}

static inline int sysctl_sync_sock_size(struct netns_ipvs *ipvs)
{
	return ipvs->sysctl_sync_sock_size;
}

static inline int sysctl_pmtu_disc(struct netns_ipvs *ipvs)
{
	return ipvs->sysctl_pmtu_disc;
}

static inline int sysctl_backup_only(struct netns_ipvs *ipvs)
{
	return ipvs->sync_state & IP_VS_STATE_BACKUP &&
	       ipvs->sysctl_backup_only;
}

static inline int sysctl_conn_reuse_mode(struct netns_ipvs *ipvs)
{
	return ipvs->sysctl_conn_reuse_mode;
}

static inline int sysctl_schedule_icmp(struct netns_ipvs *ipvs)
{
	return ipvs->sysctl_schedule_icmp;
}

static inline int sysctl_ignore_tunneled(struct netns_ipvs *ipvs)
{
	return ipvs->sysctl_ignore_tunneled;
}

static inline int sysctl_cache_bypass(struct netns_ipvs *ipvs)
{
	return ipvs->sysctl_cache_bypass;
}

#else

static inline int sysctl_sync_threshold(struct netns_ipvs *ipvs)
{
	return DEFAULT_SYNC_THRESHOLD;
}

static inline int sysctl_sync_period(struct netns_ipvs *ipvs)
{
	return DEFAULT_SYNC_PERIOD;
}

static inline unsigned int sysctl_sync_refresh_period(struct netns_ipvs *ipvs)
{
	return DEFAULT_SYNC_REFRESH_PERIOD;
}

static inline int sysctl_sync_retries(struct netns_ipvs *ipvs)
{
	return DEFAULT_SYNC_RETRIES & 3;
}

static inline int sysctl_sync_ver(struct netns_ipvs *ipvs)
{
	return DEFAULT_SYNC_VER;
}

static inline int sysctl_sloppy_tcp(struct netns_ipvs *ipvs)
{
	return DEFAULT_SLOPPY_TCP;
}

static inline int sysctl_sloppy_sctp(struct netns_ipvs *ipvs)
{
	return DEFAULT_SLOPPY_SCTP;
}

static inline int sysctl_sync_ports(struct netns_ipvs *ipvs)
{
	return 1;
}

static inline int sysctl_sync_persist_mode(struct netns_ipvs *ipvs)
{
	return 0;
}

static inline unsigned long sysctl_sync_qlen_max(struct netns_ipvs *ipvs)
{
	return IPVS_SYNC_QLEN_MAX;
}

static inline int sysctl_sync_sock_size(struct netns_ipvs *ipvs)
{
	return 0;
}

static inline int sysctl_pmtu_disc(struct netns_ipvs *ipvs)
{
	return 1;
}

static inline int sysctl_backup_only(struct netns_ipvs *ipvs)
{
	return 0;
}

static inline int sysctl_conn_reuse_mode(struct netns_ipvs *ipvs)
{
	return 1;
}

static inline int sysctl_schedule_icmp(struct netns_ipvs *ipvs)
{
	return 0;
}

static inline int sysctl_ignore_tunneled(struct netns_ipvs *ipvs)
{
	return 0;
}

static inline int sysctl_cache_bypass(struct netns_ipvs *ipvs)
{
	return 0;
}

#endif

/* IPVS core functions
 * (from ip_vs_core.c)
 */
const char *ip_vs_proto_name(unsigned int proto);
void ip_vs_init_hash_table(struct list_head *table, int rows);
struct ip_vs_conn *ip_vs_new_conn_out(struct ip_vs_service *svc,
				      struct ip_vs_dest *dest,
				      struct sk_buff *skb,
				      const struct ip_vs_iphdr *iph,
				      __be16 dport,
				      __be16 cport);
#define IP_VS_INIT_HASH_TABLE(t) ip_vs_init_hash_table((t), ARRAY_SIZE((t)))

#define IP_VS_APP_TYPE_FTP	1

/* ip_vs_conn handling functions
 * (from ip_vs_conn.c)
 */
enum {
	IP_VS_DIR_INPUT = 0,
	IP_VS_DIR_OUTPUT,
	IP_VS_DIR_INPUT_ONLY,
	IP_VS_DIR_LAST,
};

static inline void ip_vs_conn_fill_param(struct netns_ipvs *ipvs, int af, int protocol,
					 const union nf_inet_addr *caddr,
					 __be16 cport,
					 const union nf_inet_addr *vaddr,
					 __be16 vport,
					 struct ip_vs_conn_param *p)
{
	p->ipvs = ipvs;
	p->af = af;
	p->protocol = protocol;
	p->caddr = caddr;
	p->cport = cport;
	p->vaddr = vaddr;
	p->vport = vport;
	p->pe = NULL;
	p->pe_data = NULL;
}

struct ip_vs_conn *ip_vs_conn_in_get(const struct ip_vs_conn_param *p);
struct ip_vs_conn *ip_vs_ct_in_get(const struct ip_vs_conn_param *p);

struct ip_vs_conn * ip_vs_conn_in_get_proto(struct netns_ipvs *ipvs, int af,
					    const struct sk_buff *skb,
					    const struct ip_vs_iphdr *iph);

struct ip_vs_conn *ip_vs_conn_out_get(const struct ip_vs_conn_param *p);

struct ip_vs_conn * ip_vs_conn_out_get_proto(struct netns_ipvs *ipvs, int af,
					     const struct sk_buff *skb,
					     const struct ip_vs_iphdr *iph);

/* Get reference to gain full access to conn.
 * By default, RCU read-side critical sections have access only to
 * conn fields and its PE data, see ip_vs_conn_rcu_free() for reference.
 */
static inline bool __ip_vs_conn_get(struct ip_vs_conn *cp)
{
	return refcount_inc_not_zero(&cp->refcnt);
}

/* put back the conn without restarting its timer */
static inline void __ip_vs_conn_put(struct ip_vs_conn *cp)
{
	smp_mb__before_atomic();
	refcount_dec(&cp->refcnt);
}
void ip_vs_conn_put(struct ip_vs_conn *cp);
void ip_vs_conn_fill_cport(struct ip_vs_conn *cp, __be16 cport);

struct ip_vs_conn *ip_vs_conn_new(const struct ip_vs_conn_param *p, int dest_af,
				  const union nf_inet_addr *daddr,
				  __be16 dport, unsigned int flags,
				  struct ip_vs_dest *dest, __u32 fwmark);
void ip_vs_conn_expire_now(struct ip_vs_conn *cp);

const char *ip_vs_state_name(const struct ip_vs_conn *cp);

void ip_vs_tcp_conn_listen(struct ip_vs_conn *cp);
int ip_vs_check_template(struct ip_vs_conn *ct, struct ip_vs_dest *cdest);
void ip_vs_random_dropentry(struct netns_ipvs *ipvs);
int ip_vs_conn_init(void);
void ip_vs_conn_cleanup(void);

static inline void ip_vs_control_del(struct ip_vs_conn *cp)
{
	struct ip_vs_conn *ctl_cp = cp->control;
	if (!ctl_cp) {
		IP_VS_ERR_BUF("request control DEL for uncontrolled: "
			      "%s:%d to %s:%d\n",
			      IP_VS_DBG_ADDR(cp->af, &cp->caddr),
			      ntohs(cp->cport),
			      IP_VS_DBG_ADDR(cp->af, &cp->vaddr),
			      ntohs(cp->vport));

		return;
	}

	IP_VS_DBG_BUF(7, "DELeting control for: "
		      "cp.dst=%s:%d ctl_cp.dst=%s:%d\n",
		      IP_VS_DBG_ADDR(cp->af, &cp->caddr),
		      ntohs(cp->cport),
		      IP_VS_DBG_ADDR(cp->af, &ctl_cp->caddr),
		      ntohs(ctl_cp->cport));

	cp->control = NULL;
	if (atomic_read(&ctl_cp->n_control) == 0) {
		IP_VS_ERR_BUF("BUG control DEL with n=0 : "
			      "%s:%d to %s:%d\n",
			      IP_VS_DBG_ADDR(cp->af, &cp->caddr),
			      ntohs(cp->cport),
			      IP_VS_DBG_ADDR(cp->af, &cp->vaddr),
			      ntohs(cp->vport));

		return;
	}
	atomic_dec(&ctl_cp->n_control);
}

static inline void
ip_vs_control_add(struct ip_vs_conn *cp, struct ip_vs_conn *ctl_cp)
{
	if (cp->control) {
		IP_VS_ERR_BUF("request control ADD for already controlled: "
			      "%s:%d to %s:%d\n",
			      IP_VS_DBG_ADDR(cp->af, &cp->caddr),
			      ntohs(cp->cport),
			      IP_VS_DBG_ADDR(cp->af, &cp->vaddr),
			      ntohs(cp->vport));

		ip_vs_control_del(cp);
	}

	IP_VS_DBG_BUF(7, "ADDing control for: "
		      "cp.dst=%s:%d ctl_cp.dst=%s:%d\n",
		      IP_VS_DBG_ADDR(cp->af, &cp->caddr),
		      ntohs(cp->cport),
		      IP_VS_DBG_ADDR(cp->af, &ctl_cp->caddr),
		      ntohs(ctl_cp->cport));

	cp->control = ctl_cp;
	atomic_inc(&ctl_cp->n_control);
}

/* Mark our template as assured */
static inline void
ip_vs_control_assure_ct(struct ip_vs_conn *cp)
{
	struct ip_vs_conn *ct = cp->control;

	if (ct && !(ct->state & IP_VS_CTPL_S_ASSURED) &&
	    (ct->flags & IP_VS_CONN_F_TEMPLATE))
		ct->state |= IP_VS_CTPL_S_ASSURED;
}

/* IPVS netns init & cleanup functions */
int ip_vs_estimator_net_init(struct netns_ipvs *ipvs);
int ip_vs_control_net_init(struct netns_ipvs *ipvs);
int ip_vs_protocol_net_init(struct netns_ipvs *ipvs);
int ip_vs_app_net_init(struct netns_ipvs *ipvs);
int ip_vs_conn_net_init(struct netns_ipvs *ipvs);
int ip_vs_sync_net_init(struct netns_ipvs *ipvs);
void ip_vs_conn_net_cleanup(struct netns_ipvs *ipvs);
void ip_vs_app_net_cleanup(struct netns_ipvs *ipvs);
void ip_vs_protocol_net_cleanup(struct netns_ipvs *ipvs);
void ip_vs_control_net_cleanup(struct netns_ipvs *ipvs);
void ip_vs_estimator_net_cleanup(struct netns_ipvs *ipvs);
void ip_vs_sync_net_cleanup(struct netns_ipvs *ipvs);
void ip_vs_service_net_cleanup(struct netns_ipvs *ipvs);

/* IPVS application functions
 * (from ip_vs_app.c)
 */
#define IP_VS_APP_MAX_PORTS  8
struct ip_vs_app *register_ip_vs_app(struct netns_ipvs *ipvs, struct ip_vs_app *app);
void unregister_ip_vs_app(struct netns_ipvs *ipvs, struct ip_vs_app *app);
int ip_vs_bind_app(struct ip_vs_conn *cp, struct ip_vs_protocol *pp);
void ip_vs_unbind_app(struct ip_vs_conn *cp);
int register_ip_vs_app_inc(struct netns_ipvs *ipvs, struct ip_vs_app *app, __u16 proto,
			   __u16 port);
int ip_vs_app_inc_get(struct ip_vs_app *inc);
void ip_vs_app_inc_put(struct ip_vs_app *inc);

int ip_vs_app_pkt_out(struct ip_vs_conn *, struct sk_buff *skb,
		      struct ip_vs_iphdr *ipvsh);
int ip_vs_app_pkt_in(struct ip_vs_conn *, struct sk_buff *skb,
		     struct ip_vs_iphdr *ipvsh);

int register_ip_vs_pe(struct ip_vs_pe *pe);
int unregister_ip_vs_pe(struct ip_vs_pe *pe);
struct ip_vs_pe *ip_vs_pe_getbyname(const char *name);
struct ip_vs_pe *__ip_vs_pe_getbyname(const char *pe_name);

/* Use a #define to avoid all of module.h just for these trivial ops */
#define ip_vs_pe_get(pe)			\
	if (pe && pe->module)			\
		__module_get(pe->module);

#define ip_vs_pe_put(pe)			\
	if (pe && pe->module)			\
		module_put(pe->module);

/* IPVS protocol functions (from ip_vs_proto.c) */
int ip_vs_protocol_init(void);
void ip_vs_protocol_cleanup(void);
void ip_vs_protocol_timeout_change(struct netns_ipvs *ipvs, int flags);
int *ip_vs_create_timeout_table(int *table, int size);
void ip_vs_tcpudp_debug_packet(int af, struct ip_vs_protocol *pp,
			       const struct sk_buff *skb, int offset,
			       const char *msg);

extern struct ip_vs_protocol ip_vs_protocol_tcp;
extern struct ip_vs_protocol ip_vs_protocol_udp;
extern struct ip_vs_protocol ip_vs_protocol_icmp;
extern struct ip_vs_protocol ip_vs_protocol_esp;
extern struct ip_vs_protocol ip_vs_protocol_ah;
extern struct ip_vs_protocol ip_vs_protocol_sctp;

/* Registering/unregistering scheduler functions
 * (from ip_vs_sched.c)
 */
int register_ip_vs_scheduler(struct ip_vs_scheduler *scheduler);
int unregister_ip_vs_scheduler(struct ip_vs_scheduler *scheduler);
int ip_vs_bind_scheduler(struct ip_vs_service *svc,
			 struct ip_vs_scheduler *scheduler);
void ip_vs_unbind_scheduler(struct ip_vs_service *svc,
			    struct ip_vs_scheduler *sched);
struct ip_vs_scheduler *ip_vs_scheduler_get(const char *sched_name);
void ip_vs_scheduler_put(struct ip_vs_scheduler *scheduler);
struct ip_vs_conn *
ip_vs_schedule(struct ip_vs_service *svc, struct sk_buff *skb,
	       struct ip_vs_proto_data *pd, int *ignored,
	       struct ip_vs_iphdr *iph);
int ip_vs_leave(struct ip_vs_service *svc, struct sk_buff *skb,
		struct ip_vs_proto_data *pd, struct ip_vs_iphdr *iph);

void ip_vs_scheduler_err(struct ip_vs_service *svc, const char *msg);

/* IPVS control data and functions (from ip_vs_ctl.c) */
extern struct ip_vs_stats ip_vs_stats;
extern int sysctl_ip_vs_sync_ver;

struct ip_vs_service *
ip_vs_service_find(struct netns_ipvs *ipvs, int af, __u32 fwmark, __u16 protocol,
		  const union nf_inet_addr *vaddr, __be16 vport);

bool ip_vs_has_real_service(struct netns_ipvs *ipvs, int af, __u16 protocol,
			    const union nf_inet_addr *daddr, __be16 dport);

struct ip_vs_dest *
ip_vs_find_real_service(struct netns_ipvs *ipvs, int af, __u16 protocol,
			const union nf_inet_addr *daddr, __be16 dport);

int ip_vs_use_count_inc(void);
void ip_vs_use_count_dec(void);
int ip_vs_register_nl_ioctl(void);
void ip_vs_unregister_nl_ioctl(void);
int ip_vs_control_init(void);
void ip_vs_control_cleanup(void);
struct ip_vs_dest *
ip_vs_find_dest(struct netns_ipvs *ipvs, int svc_af, int dest_af,
		const union nf_inet_addr *daddr, __be16 dport,
		const union nf_inet_addr *vaddr, __be16 vport,
		__u16 protocol, __u32 fwmark, __u32 flags);
void ip_vs_try_bind_dest(struct ip_vs_conn *cp);

static inline void ip_vs_dest_hold(struct ip_vs_dest *dest)
{
	refcount_inc(&dest->refcnt);
}

static inline void ip_vs_dest_put(struct ip_vs_dest *dest)
{
	smp_mb__before_atomic();
	refcount_dec(&dest->refcnt);
}

static inline void ip_vs_dest_put_and_free(struct ip_vs_dest *dest)
{
	if (refcount_dec_and_test(&dest->refcnt))
		kfree(dest);
}

/* IPVS sync daemon data and function prototypes
 * (from ip_vs_sync.c)
 */
int start_sync_thread(struct netns_ipvs *ipvs, struct ipvs_sync_daemon_cfg *cfg,
		      int state);
int stop_sync_thread(struct netns_ipvs *ipvs, int state);
void ip_vs_sync_conn(struct netns_ipvs *ipvs, struct ip_vs_conn *cp, int pkts);

/* IPVS rate estimator prototypes (from ip_vs_est.c) */
void ip_vs_start_estimator(struct netns_ipvs *ipvs, struct ip_vs_stats *stats);
void ip_vs_stop_estimator(struct netns_ipvs *ipvs, struct ip_vs_stats *stats);
void ip_vs_zero_estimator(struct ip_vs_stats *stats);
void ip_vs_read_estimator(struct ip_vs_kstats *dst, struct ip_vs_stats *stats);

/* Various IPVS packet transmitters (from ip_vs_xmit.c) */
int ip_vs_null_xmit(struct sk_buff *skb, struct ip_vs_conn *cp,
		    struct ip_vs_protocol *pp, struct ip_vs_iphdr *iph);
int ip_vs_bypass_xmit(struct sk_buff *skb, struct ip_vs_conn *cp,
		      struct ip_vs_protocol *pp, struct ip_vs_iphdr *iph);
int ip_vs_nat_xmit(struct sk_buff *skb, struct ip_vs_conn *cp,
		   struct ip_vs_protocol *pp, struct ip_vs_iphdr *iph);
int ip_vs_tunnel_xmit(struct sk_buff *skb, struct ip_vs_conn *cp,
		      struct ip_vs_protocol *pp, struct ip_vs_iphdr *iph);
int ip_vs_dr_xmit(struct sk_buff *skb, struct ip_vs_conn *cp,
		  struct ip_vs_protocol *pp, struct ip_vs_iphdr *iph);
int ip_vs_icmp_xmit(struct sk_buff *skb, struct ip_vs_conn *cp,
		    struct ip_vs_protocol *pp, int offset,
		    unsigned int hooknum, struct ip_vs_iphdr *iph);
void ip_vs_dest_dst_rcu_free(struct rcu_head *head);

#ifdef CONFIG_IP_VS_IPV6
int ip_vs_bypass_xmit_v6(struct sk_buff *skb, struct ip_vs_conn *cp,
			 struct ip_vs_protocol *pp, struct ip_vs_iphdr *iph);
int ip_vs_nat_xmit_v6(struct sk_buff *skb, struct ip_vs_conn *cp,
		      struct ip_vs_protocol *pp, struct ip_vs_iphdr *iph);
int ip_vs_tunnel_xmit_v6(struct sk_buff *skb, struct ip_vs_conn *cp,
			 struct ip_vs_protocol *pp, struct ip_vs_iphdr *iph);
int ip_vs_dr_xmit_v6(struct sk_buff *skb, struct ip_vs_conn *cp,
		     struct ip_vs_protocol *pp, struct ip_vs_iphdr *iph);
int ip_vs_icmp_xmit_v6(struct sk_buff *skb, struct ip_vs_conn *cp,
		       struct ip_vs_protocol *pp, int offset,
		       unsigned int hooknum, struct ip_vs_iphdr *iph);
#endif

#ifdef CONFIG_SYSCTL
/* This is a simple mechanism to ignore packets when
 * we are loaded. Just set ip_vs_drop_rate to 'n' and
 * we start to drop 1/rate of the packets
 */
static inline int ip_vs_todrop(struct netns_ipvs *ipvs)
{
	if (!ipvs->drop_rate)
		return 0;
	if (--ipvs->drop_counter > 0)
		return 0;
	ipvs->drop_counter = ipvs->drop_rate;
	return 1;
}
#else
static inline int ip_vs_todrop(struct netns_ipvs *ipvs) { return 0; }
#endif

/* ip_vs_fwd_tag returns the forwarding tag of the connection */
#define IP_VS_FWD_METHOD(cp)  (cp->flags & IP_VS_CONN_F_FWD_MASK)

static inline char ip_vs_fwd_tag(struct ip_vs_conn *cp)
{
	char fwd;

	switch (IP_VS_FWD_METHOD(cp)) {
	case IP_VS_CONN_F_MASQ:
		fwd = 'M'; break;
	case IP_VS_CONN_F_LOCALNODE:
		fwd = 'L'; break;
	case IP_VS_CONN_F_TUNNEL:
		fwd = 'T'; break;
	case IP_VS_CONN_F_DROUTE:
		fwd = 'R'; break;
	case IP_VS_CONN_F_BYPASS:
		fwd = 'B'; break;
	default:
		fwd = '?'; break;
	}
	return fwd;
}

void ip_vs_nat_icmp(struct sk_buff *skb, struct ip_vs_protocol *pp,
		    struct ip_vs_conn *cp, int dir);

#ifdef CONFIG_IP_VS_IPV6
void ip_vs_nat_icmp_v6(struct sk_buff *skb, struct ip_vs_protocol *pp,
		       struct ip_vs_conn *cp, int dir);
#endif

__sum16 ip_vs_checksum_complete(struct sk_buff *skb, int offset);

static inline __wsum ip_vs_check_diff4(__be32 old, __be32 new, __wsum oldsum)
{
	__be32 diff[2] = { ~old, new };

	return csum_partial(diff, sizeof(diff), oldsum);
}

#ifdef CONFIG_IP_VS_IPV6
static inline __wsum ip_vs_check_diff16(const __be32 *old, const __be32 *new,
					__wsum oldsum)
{
	__be32 diff[8] = { ~old[3], ~old[2], ~old[1], ~old[0],
			    new[3],  new[2],  new[1],  new[0] };

	return csum_partial(diff, sizeof(diff), oldsum);
}
#endif

static inline __wsum ip_vs_check_diff2(__be16 old, __be16 new, __wsum oldsum)
{
	__be16 diff[2] = { ~old, new };

	return csum_partial(diff, sizeof(diff), oldsum);
}

/* Forget current conntrack (unconfirmed) and attach notrack entry */
static inline void ip_vs_notrack(struct sk_buff *skb)
{
#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);

	if (ct) {
		nf_conntrack_put(&ct->ct_general);
		nf_ct_set(skb, NULL, IP_CT_UNTRACKED);
	}
#endif
}

#ifdef CONFIG_IP_VS_NFCT
/* Netfilter connection tracking
 * (from ip_vs_nfct.c)
 */
static inline int ip_vs_conntrack_enabled(struct netns_ipvs *ipvs)
{
#ifdef CONFIG_SYSCTL
	return ipvs->sysctl_conntrack;
#else
	return 0;
#endif
}

void ip_vs_update_conntrack(struct sk_buff *skb, struct ip_vs_conn *cp,
			    int outin);
int ip_vs_confirm_conntrack(struct sk_buff *skb);
void ip_vs_nfct_expect_related(struct sk_buff *skb, struct nf_conn *ct,
			       struct ip_vs_conn *cp, u_int8_t proto,
			       const __be16 port, int from_rs);
void ip_vs_conn_drop_conntrack(struct ip_vs_conn *cp);

#else

static inline int ip_vs_conntrack_enabled(struct netns_ipvs *ipvs)
{
	return 0;
}

static inline void ip_vs_update_conntrack(struct sk_buff *skb,
					  struct ip_vs_conn *cp, int outin)
{
}

static inline int ip_vs_confirm_conntrack(struct sk_buff *skb)
{
	return NF_ACCEPT;
}

static inline void ip_vs_conn_drop_conntrack(struct ip_vs_conn *cp)
{
}
#endif /* CONFIG_IP_VS_NFCT */

/* Really using conntrack? */
static inline bool ip_vs_conn_uses_conntrack(struct ip_vs_conn *cp,
					     struct sk_buff *skb)
{
#ifdef CONFIG_IP_VS_NFCT
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;

	if (!(cp->flags & IP_VS_CONN_F_NFCT))
		return false;
	ct = nf_ct_get(skb, &ctinfo);
	if (ct)
		return true;
#endif
	return false;
}

static inline int ip_vs_register_conntrack(struct ip_vs_service *svc)
{
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	int afmask = (svc->af == AF_INET6) ? 2 : 1;
	int ret = 0;

	if (!(svc->conntrack_afmask & afmask)) {
		ret = nf_ct_netns_get(svc->ipvs->net, svc->af);
		if (ret >= 0)
			svc->conntrack_afmask |= afmask;
	}
	return ret;
#else
	return 0;
#endif
}

static inline void ip_vs_unregister_conntrack(struct ip_vs_service *svc)
{
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	int afmask = (svc->af == AF_INET6) ? 2 : 1;

	if (svc->conntrack_afmask & afmask) {
		nf_ct_netns_put(svc->ipvs->net, svc->af);
		svc->conntrack_afmask &= ~afmask;
	}
#endif
}

static inline int
ip_vs_dest_conn_overhead(struct ip_vs_dest *dest)
{
	/* We think the overhead of processing active connections is 256
	 * times higher than that of inactive connections in average. (This
	 * 256 times might not be accurate, we will change it later) We
	 * use the following formula to estimate the overhead now:
	 *		  dest->activeconns*256 + dest->inactconns
	 */
	return (atomic_read(&dest->activeconns) << 8) +
		atomic_read(&dest->inactconns);
}

#endif	/* _NET_IP_VS_H */
