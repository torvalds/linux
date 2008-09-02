/*
 *      IP Virtual Server
 *      data structure and functionality definitions
 */

#ifndef _NET_IP_VS_H
#define _NET_IP_VS_H

#include <linux/ip_vs.h>                /* definitions shared with userland */

/* old ipvsadm versions still include this file directly */
#ifdef __KERNEL__

#include <asm/types.h>                  /* for __uXX types */

#include <linux/sysctl.h>               /* for ctl_path */
#include <linux/list.h>                 /* for struct list_head */
#include <linux/spinlock.h>             /* for struct rwlock_t */
#include <asm/atomic.h>                 /* for struct atomic_t */
#include <linux/compiler.h>
#include <linux/timer.h>

#include <net/checksum.h>
#include <linux/netfilter.h>		/* for union nf_inet_addr */
#include <linux/ipv6.h>			/* for struct ipv6hdr */
#include <net/ipv6.h>			/* for ipv6_addr_copy */

struct ip_vs_iphdr {
	int len;
	__u8 protocol;
	union nf_inet_addr saddr;
	union nf_inet_addr daddr;
};

static inline void
ip_vs_fill_iphdr(int af, const void *nh, struct ip_vs_iphdr *iphdr)
{
#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6) {
		const struct ipv6hdr *iph = nh;
		iphdr->len = sizeof(struct ipv6hdr);
		iphdr->protocol = iph->nexthdr;
		ipv6_addr_copy(&iphdr->saddr.in6, &iph->saddr);
		ipv6_addr_copy(&iphdr->daddr.in6, &iph->daddr);
	} else
#endif
	{
		const struct iphdr *iph = nh;
		iphdr->len = iph->ihl * 4;
		iphdr->protocol = iph->protocol;
		iphdr->saddr.ip = iph->saddr;
		iphdr->daddr.ip = iph->daddr;
	}
}

static inline void ip_vs_addr_copy(int af, union nf_inet_addr *dst,
				   const union nf_inet_addr *src)
{
#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6)
		ipv6_addr_copy(&dst->in6, &src->in6);
	else
#endif
	dst->ip = src->ip;
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

extern int ip_vs_get_debug_level(void);

static inline const char *ip_vs_dbg_addr(int af, char *buf, size_t buf_len,
					 const union nf_inet_addr *addr,
					 int *idx)
{
	int len;
#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6)
		len = snprintf(&buf[*idx], buf_len - *idx, "[" NIP6_FMT "]",
			       NIP6(addr->in6)) + 1;
	else
#endif
		len = snprintf(&buf[*idx], buf_len - *idx, NIPQUAD_FMT,
			       NIPQUAD(addr->ip)) + 1;

	*idx += len;
	BUG_ON(*idx > buf_len + 1);
	return &buf[*idx - len];
}

#define IP_VS_DBG_BUF(level, msg...)			\
    do {						\
	    char ip_vs_dbg_buf[160];			\
	    int ip_vs_dbg_idx = 0;			\
	    if (level <= ip_vs_get_debug_level())	\
		    printk(KERN_DEBUG "IPVS: " msg);	\
    } while (0)
#define IP_VS_ERR_BUF(msg...)				\
    do {						\
	    char ip_vs_dbg_buf[160];			\
	    int ip_vs_dbg_idx = 0;			\
	    printk(KERN_ERR "IPVS: " msg);		\
    } while (0)

/* Only use from within IP_VS_DBG_BUF() or IP_VS_ERR_BUF macros */
#define IP_VS_DBG_ADDR(af, addr)			\
    ip_vs_dbg_addr(af, ip_vs_dbg_buf,			\
		   sizeof(ip_vs_dbg_buf), addr,		\
		   &ip_vs_dbg_idx)

#define IP_VS_DBG(level, msg...)			\
    do {						\
	    if (level <= ip_vs_get_debug_level())	\
		    printk(KERN_DEBUG "IPVS: " msg);	\
    } while (0)
#define IP_VS_DBG_RL(msg...)				\
    do {						\
	    if (net_ratelimit())			\
		    printk(KERN_DEBUG "IPVS: " msg);	\
    } while (0)
#define IP_VS_DBG_PKT(level, pp, skb, ofs, msg)		\
    do {						\
	    if (level <= ip_vs_get_debug_level())	\
		pp->debug_packet(pp, skb, ofs, msg);	\
    } while (0)
#define IP_VS_DBG_RL_PKT(level, pp, skb, ofs, msg)	\
    do {						\
	    if (level <= ip_vs_get_debug_level() &&	\
		net_ratelimit())			\
		pp->debug_packet(pp, skb, ofs, msg);	\
    } while (0)
#else	/* NO DEBUGGING at ALL */
#define IP_VS_DBG_BUF(level, msg...)  do {} while (0)
#define IP_VS_ERR_BUF(msg...)  do {} while (0)
#define IP_VS_DBG(level, msg...)  do {} while (0)
#define IP_VS_DBG_RL(msg...)  do {} while (0)
#define IP_VS_DBG_PKT(level, pp, skb, ofs, msg)		do {} while (0)
#define IP_VS_DBG_RL_PKT(level, pp, skb, ofs, msg)	do {} while (0)
#endif

#define IP_VS_BUG() BUG()
#define IP_VS_ERR(msg...) printk(KERN_ERR "IPVS: " msg)
#define IP_VS_INFO(msg...) printk(KERN_INFO "IPVS: " msg)
#define IP_VS_WARNING(msg...) \
	printk(KERN_WARNING "IPVS: " msg)
#define IP_VS_ERR_RL(msg...)				\
    do {						\
	    if (net_ratelimit())			\
		    printk(KERN_ERR "IPVS: " msg);	\
    } while (0)

#ifdef CONFIG_IP_VS_DEBUG
#define EnterFunction(level)						\
    do {								\
	    if (level <= ip_vs_get_debug_level())			\
		    printk(KERN_DEBUG "Enter: %s, %s line %i\n",	\
			   __FUNCTION__, __FILE__, __LINE__);		\
    } while (0)
#define LeaveFunction(level)                                            \
    do {                                                                \
	    if (level <= ip_vs_get_debug_level())                       \
			printk(KERN_DEBUG "Leave: %s, %s line %i\n",    \
			       __FUNCTION__, __FILE__, __LINE__);       \
    } while (0)
#else
#define EnterFunction(level)   do {} while (0)
#define LeaveFunction(level)   do {} while (0)
#endif

#define	IP_VS_WAIT_WHILE(expr)	while (expr) { cpu_relax(); }


/*
 *      The port number of FTP service (in network order).
 */
#define FTPPORT  __constant_htons(21)
#define FTPDATA  __constant_htons(20)

/*
 *      TCP State Values
 */
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

/*
 *	UDP State Values
 */
enum {
	IP_VS_UDP_S_NORMAL,
	IP_VS_UDP_S_LAST,
};

/*
 *	ICMP State Values
 */
enum {
	IP_VS_ICMP_S_NORMAL,
	IP_VS_ICMP_S_LAST,
};

/*
 *	Delta sequence info structure
 *	Each ip_vs_conn has 2 (output AND input seq. changes).
 *      Only used in the VS/NAT.
 */
struct ip_vs_seq {
	__u32			init_seq;	/* Add delta from this seq */
	__u32			delta;		/* Delta in sequence numbers */
	__u32			previous_delta;	/* Delta in sequence numbers
						   before last resized pkt */
};


/*
 *	IPVS statistics objects
 */
struct ip_vs_estimator {
	struct list_head	list;

	u64			last_inbytes;
	u64			last_outbytes;
	u32			last_conns;
	u32			last_inpkts;
	u32			last_outpkts;

	u32			cps;
	u32			inpps;
	u32			outpps;
	u32			inbps;
	u32			outbps;
};

struct ip_vs_stats
{
	__u32                   conns;          /* connections scheduled */
	__u32                   inpkts;         /* incoming packets */
	__u32                   outpkts;        /* outgoing packets */
	__u64                   inbytes;        /* incoming bytes */
	__u64                   outbytes;       /* outgoing bytes */

	__u32			cps;		/* current connection rate */
	__u32			inpps;		/* current in packet rate */
	__u32			outpps;		/* current out packet rate */
	__u32			inbps;		/* current in byte rate */
	__u32			outbps;		/* current out byte rate */

	/*
	 * Don't add anything before the lock, because we use memcpy() to copy
	 * the members before the lock to struct ip_vs_stats_user in
	 * ip_vs_ctl.c.
	 */

	spinlock_t              lock;           /* spin lock */

	struct ip_vs_estimator	est;		/* estimator */
};

struct dst_entry;
struct iphdr;
struct ip_vs_conn;
struct ip_vs_app;
struct sk_buff;

struct ip_vs_protocol {
	struct ip_vs_protocol	*next;
	char			*name;
	u16			protocol;
	u16			num_states;
	int			dont_defrag;
	atomic_t		appcnt;		/* counter of proto app incs */
	int			*timeout_table;	/* protocol timeout table */

	void (*init)(struct ip_vs_protocol *pp);

	void (*exit)(struct ip_vs_protocol *pp);

	int (*conn_schedule)(int af, struct sk_buff *skb,
			     struct ip_vs_protocol *pp,
			     int *verdict, struct ip_vs_conn **cpp);

	struct ip_vs_conn *
	(*conn_in_get)(int af,
		       const struct sk_buff *skb,
		       struct ip_vs_protocol *pp,
		       const struct ip_vs_iphdr *iph,
		       unsigned int proto_off,
		       int inverse);

	struct ip_vs_conn *
	(*conn_out_get)(int af,
			const struct sk_buff *skb,
			struct ip_vs_protocol *pp,
			const struct ip_vs_iphdr *iph,
			unsigned int proto_off,
			int inverse);

	int (*snat_handler)(struct sk_buff *skb,
			    struct ip_vs_protocol *pp, struct ip_vs_conn *cp);

	int (*dnat_handler)(struct sk_buff *skb,
			    struct ip_vs_protocol *pp, struct ip_vs_conn *cp);

	int (*csum_check)(int af, struct sk_buff *skb,
			  struct ip_vs_protocol *pp);

	const char *(*state_name)(int state);

	int (*state_transition)(struct ip_vs_conn *cp, int direction,
				const struct sk_buff *skb,
				struct ip_vs_protocol *pp);

	int (*register_app)(struct ip_vs_app *inc);

	void (*unregister_app)(struct ip_vs_app *inc);

	int (*app_conn_bind)(struct ip_vs_conn *cp);

	void (*debug_packet)(struct ip_vs_protocol *pp,
			     const struct sk_buff *skb,
			     int offset,
			     const char *msg);

	void (*timeout_change)(struct ip_vs_protocol *pp, int flags);

	int (*set_state_timeout)(struct ip_vs_protocol *pp, char *sname, int to);
};

extern struct ip_vs_protocol * ip_vs_proto_get(unsigned short proto);

/*
 *	IP_VS structure allocated for each dynamically scheduled connection
 */
struct ip_vs_conn {
	struct list_head        c_list;         /* hashed list heads */

	/* Protocol, addresses and port numbers */
	u16                      af;		/* address family */
	union nf_inet_addr       caddr;          /* client address */
	union nf_inet_addr       vaddr;          /* virtual address */
	union nf_inet_addr       daddr;          /* destination address */
	__be16                   cport;
	__be16                   vport;
	__be16                   dport;
	__u16                   protocol;       /* Which protocol (TCP/UDP) */

	/* counter and timer */
	atomic_t		refcnt;		/* reference count */
	struct timer_list	timer;		/* Expiration timer */
	volatile unsigned long	timeout;	/* timeout */

	/* Flags and state transition */
	spinlock_t              lock;           /* lock for state transition */
	volatile __u16          flags;          /* status flags */
	volatile __u16          state;          /* state info */
	volatile __u16          old_state;      /* old state, to be used for
						 * state transition triggerd
						 * synchronization
						 */

	/* Control members */
	struct ip_vs_conn       *control;       /* Master control connection */
	atomic_t                n_control;      /* Number of controlled ones */
	struct ip_vs_dest       *dest;          /* real server */
	atomic_t                in_pkts;        /* incoming packet counter */

	/* packet transmitter for different forwarding methods.  If it
	   mangles the packet, it must return NF_DROP or better NF_STOLEN,
	   otherwise this must be changed to a sk_buff **.
	 */
	int (*packet_xmit)(struct sk_buff *skb, struct ip_vs_conn *cp,
			   struct ip_vs_protocol *pp);

	/* Note: we can group the following members into a structure,
	   in order to save more space, and the following members are
	   only used in VS/NAT anyway */
	struct ip_vs_app        *app;           /* bound ip_vs_app object */
	void                    *app_data;      /* Application private data */
	struct ip_vs_seq        in_seq;         /* incoming seq. struct */
	struct ip_vs_seq        out_seq;        /* outgoing seq. struct */
};


/*
 *	Extended internal versions of struct ip_vs_service_user and
 *	ip_vs_dest_user for IPv6 support.
 *
 *	We need these to conveniently pass around service and destination
 *	options, but unfortunately, we also need to keep the old definitions to
 *	maintain userspace backwards compatibility for the setsockopt interface.
 */
struct ip_vs_service_user_kern {
	/* virtual service addresses */
	u16			af;
	u16			protocol;
	union nf_inet_addr	addr;		/* virtual ip address */
	u16			port;
	u32			fwmark;		/* firwall mark of service */

	/* virtual service options */
	char			*sched_name;
	unsigned		flags;		/* virtual service flags */
	unsigned		timeout;	/* persistent timeout in sec */
	u32			netmask;	/* persistent netmask */
};


struct ip_vs_dest_user_kern {
	/* destination server address */
	union nf_inet_addr	addr;
	u16			port;

	/* real server options */
	unsigned		conn_flags;	/* connection flags */
	int			weight;		/* destination weight */

	/* thresholds for active connections */
	u32			u_threshold;	/* upper threshold */
	u32			l_threshold;	/* lower threshold */
};


/*
 *	The information about the virtual service offered to the net
 *	and the forwarding entries
 */
struct ip_vs_service {
	struct list_head	s_list;   /* for normal service table */
	struct list_head	f_list;   /* for fwmark-based service table */
	atomic_t		refcnt;   /* reference counter */
	atomic_t		usecnt;   /* use counter */

	u16			af;       /* address family */
	__u16			protocol; /* which protocol (TCP/UDP) */
	union nf_inet_addr	addr;	  /* IP address for virtual service */
	__be16			port;	  /* port number for the service */
	__u32                   fwmark;   /* firewall mark of the service */
	unsigned		flags;	  /* service status flags */
	unsigned		timeout;  /* persistent timeout in ticks */
	__be32			netmask;  /* grouping granularity */

	struct list_head	destinations;  /* real server d-linked list */
	__u32			num_dests;     /* number of servers */
	struct ip_vs_stats      stats;         /* statistics for the service */
	struct ip_vs_app	*inc;	  /* bind conns to this app inc */

	/* for scheduling */
	struct ip_vs_scheduler	*scheduler;    /* bound scheduler object */
	rwlock_t		sched_lock;    /* lock sched_data */
	void			*sched_data;   /* scheduler application data */
};


/*
 *	The real server destination forwarding entry
 *	with ip address, port number, and so on.
 */
struct ip_vs_dest {
	struct list_head	n_list;   /* for the dests in the service */
	struct list_head	d_list;   /* for table with all the dests */

	u16			af;		/* address family */
	union nf_inet_addr	addr;		/* IP address of the server */
	__be16			port;		/* port number of the server */
	volatile unsigned	flags;		/* dest status flags */
	atomic_t		conn_flags;	/* flags to copy to conn */
	atomic_t		weight;		/* server weight */

	atomic_t		refcnt;		/* reference counter */
	struct ip_vs_stats      stats;          /* statistics */

	/* connection counters and thresholds */
	atomic_t		activeconns;	/* active connections */
	atomic_t		inactconns;	/* inactive connections */
	atomic_t		persistconns;	/* persistent connections */
	__u32			u_threshold;	/* upper threshold */
	__u32			l_threshold;	/* lower threshold */

	/* for destination cache */
	spinlock_t		dst_lock;	/* lock of dst_cache */
	struct dst_entry	*dst_cache;	/* destination cache entry */
	u32			dst_rtos;	/* RT_TOS(tos) for dst */

	/* for virtual service */
	struct ip_vs_service	*svc;		/* service it belongs to */
	__u16			protocol;	/* which protocol (TCP/UDP) */
	union nf_inet_addr	vaddr;		/* virtual IP address */
	__be16			vport;		/* virtual port number */
	__u32			vfwmark;	/* firewall mark of service */
};


/*
 *	The scheduler object
 */
struct ip_vs_scheduler {
	struct list_head	n_list;		/* d-linked list head */
	char			*name;		/* scheduler name */
	atomic_t		refcnt;		/* reference counter */
	struct module		*module;	/* THIS_MODULE/NULL */
#ifdef CONFIG_IP_VS_IPV6
	int			supports_ipv6;	/* scheduler has IPv6 support */
#endif

	/* scheduler initializing service */
	int (*init_service)(struct ip_vs_service *svc);
	/* scheduling service finish */
	int (*done_service)(struct ip_vs_service *svc);
	/* scheduler updating service */
	int (*update_service)(struct ip_vs_service *svc);

	/* selecting a server from the given service */
	struct ip_vs_dest* (*schedule)(struct ip_vs_service *svc,
				       const struct sk_buff *skb);
};


/*
 *	The application module object (a.k.a. app incarnation)
 */
struct ip_vs_app
{
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

	/* output hook: return false if can't linearize. diff set for TCP.  */
	int (*pkt_out)(struct ip_vs_app *, struct ip_vs_conn *,
		       struct sk_buff *, int *diff);

	/* input hook: return false if can't linearize. diff set for TCP. */
	int (*pkt_in)(struct ip_vs_app *, struct ip_vs_conn *,
		      struct sk_buff *, int *diff);

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
		       const struct iphdr *iph, unsigned int proto_off,
		       int inverse);

	struct ip_vs_conn *
	(*conn_out_get)(const struct sk_buff *skb, struct ip_vs_app *app,
			const struct iphdr *iph, unsigned int proto_off,
			int inverse);

	int (*state_transition)(struct ip_vs_conn *cp, int direction,
				const struct sk_buff *skb,
				struct ip_vs_app *app);

	void (*timeout_change)(struct ip_vs_app *app, int flags);
};


/*
 *      IPVS core functions
 *      (from ip_vs_core.c)
 */
extern const char *ip_vs_proto_name(unsigned proto);
extern void ip_vs_init_hash_table(struct list_head *table, int rows);
#define IP_VS_INIT_HASH_TABLE(t) ip_vs_init_hash_table((t), ARRAY_SIZE((t)))

#define IP_VS_APP_TYPE_FTP	1

/*
 *     ip_vs_conn handling functions
 *     (from ip_vs_conn.c)
 */

/*
 *     IPVS connection entry hash table
 */
#ifndef CONFIG_IP_VS_TAB_BITS
#define CONFIG_IP_VS_TAB_BITS   12
#endif
/* make sure that IP_VS_CONN_TAB_BITS is located in [8, 20] */
#if CONFIG_IP_VS_TAB_BITS < 8
#define IP_VS_CONN_TAB_BITS	8
#endif
#if CONFIG_IP_VS_TAB_BITS > 20
#define IP_VS_CONN_TAB_BITS	20
#endif
#if 8 <= CONFIG_IP_VS_TAB_BITS && CONFIG_IP_VS_TAB_BITS <= 20
#define IP_VS_CONN_TAB_BITS	CONFIG_IP_VS_TAB_BITS
#endif
#define IP_VS_CONN_TAB_SIZE     (1 << IP_VS_CONN_TAB_BITS)
#define IP_VS_CONN_TAB_MASK     (IP_VS_CONN_TAB_SIZE - 1)

enum {
	IP_VS_DIR_INPUT = 0,
	IP_VS_DIR_OUTPUT,
	IP_VS_DIR_INPUT_ONLY,
	IP_VS_DIR_LAST,
};

extern struct ip_vs_conn *ip_vs_conn_in_get
(int af, int protocol, const union nf_inet_addr *s_addr, __be16 s_port,
 const union nf_inet_addr *d_addr, __be16 d_port);

extern struct ip_vs_conn *ip_vs_ct_in_get
(int af, int protocol, const union nf_inet_addr *s_addr, __be16 s_port,
 const union nf_inet_addr *d_addr, __be16 d_port);

extern struct ip_vs_conn *ip_vs_conn_out_get
(int af, int protocol, const union nf_inet_addr *s_addr, __be16 s_port,
 const union nf_inet_addr *d_addr, __be16 d_port);

/* put back the conn without restarting its timer */
static inline void __ip_vs_conn_put(struct ip_vs_conn *cp)
{
	atomic_dec(&cp->refcnt);
}
extern void ip_vs_conn_put(struct ip_vs_conn *cp);
extern void ip_vs_conn_fill_cport(struct ip_vs_conn *cp, __be16 cport);

extern struct ip_vs_conn *
ip_vs_conn_new(int af, int proto, const union nf_inet_addr *caddr, __be16 cport,
	       const union nf_inet_addr *vaddr, __be16 vport,
	       const union nf_inet_addr *daddr, __be16 dport, unsigned flags,
	       struct ip_vs_dest *dest);
extern void ip_vs_conn_expire_now(struct ip_vs_conn *cp);

extern const char * ip_vs_state_name(__u16 proto, int state);

extern void ip_vs_tcp_conn_listen(struct ip_vs_conn *cp);
extern int ip_vs_check_template(struct ip_vs_conn *ct);
extern void ip_vs_random_dropentry(void);
extern int ip_vs_conn_init(void);
extern void ip_vs_conn_cleanup(void);

static inline void ip_vs_control_del(struct ip_vs_conn *cp)
{
	struct ip_vs_conn *ctl_cp = cp->control;
	if (!ctl_cp) {
		IP_VS_ERR("request control DEL for uncontrolled: "
			  "%d.%d.%d.%d:%d to %d.%d.%d.%d:%d\n",
			  NIPQUAD(cp->caddr),ntohs(cp->cport),
			  NIPQUAD(cp->vaddr),ntohs(cp->vport));
		return;
	}

	IP_VS_DBG(7, "DELeting control for: "
		  "cp.dst=%d.%d.%d.%d:%d ctl_cp.dst=%d.%d.%d.%d:%d\n",
		  NIPQUAD(cp->caddr),ntohs(cp->cport),
		  NIPQUAD(ctl_cp->caddr),ntohs(ctl_cp->cport));

	cp->control = NULL;
	if (atomic_read(&ctl_cp->n_control) == 0) {
		IP_VS_ERR("BUG control DEL with n=0 : "
			  "%d.%d.%d.%d:%d to %d.%d.%d.%d:%d\n",
			  NIPQUAD(cp->caddr),ntohs(cp->cport),
			  NIPQUAD(cp->vaddr),ntohs(cp->vport));
		return;
	}
	atomic_dec(&ctl_cp->n_control);
}

static inline void
ip_vs_control_add(struct ip_vs_conn *cp, struct ip_vs_conn *ctl_cp)
{
	if (cp->control) {
		IP_VS_ERR("request control ADD for already controlled: "
			  "%d.%d.%d.%d:%d to %d.%d.%d.%d:%d\n",
			  NIPQUAD(cp->caddr),ntohs(cp->cport),
			  NIPQUAD(cp->vaddr),ntohs(cp->vport));
		ip_vs_control_del(cp);
	}

	IP_VS_DBG(7, "ADDing control for: "
		  "cp.dst=%d.%d.%d.%d:%d ctl_cp.dst=%d.%d.%d.%d:%d\n",
		  NIPQUAD(cp->caddr),ntohs(cp->cport),
		  NIPQUAD(ctl_cp->caddr),ntohs(ctl_cp->cport));

	cp->control = ctl_cp;
	atomic_inc(&ctl_cp->n_control);
}


/*
 *      IPVS application functions
 *      (from ip_vs_app.c)
 */
#define IP_VS_APP_MAX_PORTS  8
extern int register_ip_vs_app(struct ip_vs_app *app);
extern void unregister_ip_vs_app(struct ip_vs_app *app);
extern int ip_vs_bind_app(struct ip_vs_conn *cp, struct ip_vs_protocol *pp);
extern void ip_vs_unbind_app(struct ip_vs_conn *cp);
extern int
register_ip_vs_app_inc(struct ip_vs_app *app, __u16 proto, __u16 port);
extern int ip_vs_app_inc_get(struct ip_vs_app *inc);
extern void ip_vs_app_inc_put(struct ip_vs_app *inc);

extern int ip_vs_app_pkt_out(struct ip_vs_conn *, struct sk_buff *skb);
extern int ip_vs_app_pkt_in(struct ip_vs_conn *, struct sk_buff *skb);
extern int ip_vs_skb_replace(struct sk_buff *skb, gfp_t pri,
			     char *o_buf, int o_len, char *n_buf, int n_len);
extern int ip_vs_app_init(void);
extern void ip_vs_app_cleanup(void);


/*
 *	IPVS protocol functions (from ip_vs_proto.c)
 */
extern int ip_vs_protocol_init(void);
extern void ip_vs_protocol_cleanup(void);
extern void ip_vs_protocol_timeout_change(int flags);
extern int *ip_vs_create_timeout_table(int *table, int size);
extern int
ip_vs_set_state_timeout(int *table, int num, char **names, char *name, int to);
extern void
ip_vs_tcpudp_debug_packet(struct ip_vs_protocol *pp, const struct sk_buff *skb,
			  int offset, const char *msg);

extern struct ip_vs_protocol ip_vs_protocol_tcp;
extern struct ip_vs_protocol ip_vs_protocol_udp;
extern struct ip_vs_protocol ip_vs_protocol_icmp;
extern struct ip_vs_protocol ip_vs_protocol_esp;
extern struct ip_vs_protocol ip_vs_protocol_ah;


/*
 *      Registering/unregistering scheduler functions
 *      (from ip_vs_sched.c)
 */
extern int register_ip_vs_scheduler(struct ip_vs_scheduler *scheduler);
extern int unregister_ip_vs_scheduler(struct ip_vs_scheduler *scheduler);
extern int ip_vs_bind_scheduler(struct ip_vs_service *svc,
				struct ip_vs_scheduler *scheduler);
extern int ip_vs_unbind_scheduler(struct ip_vs_service *svc);
extern struct ip_vs_scheduler *ip_vs_scheduler_get(const char *sched_name);
extern void ip_vs_scheduler_put(struct ip_vs_scheduler *scheduler);
extern struct ip_vs_conn *
ip_vs_schedule(struct ip_vs_service *svc, const struct sk_buff *skb);
extern int ip_vs_leave(struct ip_vs_service *svc, struct sk_buff *skb,
			struct ip_vs_protocol *pp);


/*
 *      IPVS control data and functions (from ip_vs_ctl.c)
 */
extern int sysctl_ip_vs_cache_bypass;
extern int sysctl_ip_vs_expire_nodest_conn;
extern int sysctl_ip_vs_expire_quiescent_template;
extern int sysctl_ip_vs_sync_threshold[2];
extern int sysctl_ip_vs_nat_icmp_send;
extern struct ip_vs_stats ip_vs_stats;
extern const struct ctl_path net_vs_ctl_path[];

extern struct ip_vs_service *
ip_vs_service_get(int af, __u32 fwmark, __u16 protocol,
		  const union nf_inet_addr *vaddr, __be16 vport);

static inline void ip_vs_service_put(struct ip_vs_service *svc)
{
	atomic_dec(&svc->usecnt);
}

extern struct ip_vs_dest *
ip_vs_lookup_real_service(__u16 protocol, __be32 daddr, __be16 dport);
extern int ip_vs_use_count_inc(void);
extern void ip_vs_use_count_dec(void);
extern int ip_vs_control_init(void);
extern void ip_vs_control_cleanup(void);
extern struct ip_vs_dest *
ip_vs_find_dest(__be32 daddr, __be16 dport,
		 __be32 vaddr, __be16 vport, __u16 protocol);
extern struct ip_vs_dest *ip_vs_try_bind_dest(struct ip_vs_conn *cp);


/*
 *      IPVS sync daemon data and function prototypes
 *      (from ip_vs_sync.c)
 */
extern volatile int ip_vs_sync_state;
extern volatile int ip_vs_master_syncid;
extern volatile int ip_vs_backup_syncid;
extern char ip_vs_master_mcast_ifn[IP_VS_IFNAME_MAXLEN];
extern char ip_vs_backup_mcast_ifn[IP_VS_IFNAME_MAXLEN];
extern int start_sync_thread(int state, char *mcast_ifn, __u8 syncid);
extern int stop_sync_thread(int state);
extern void ip_vs_sync_conn(struct ip_vs_conn *cp);


/*
 *      IPVS rate estimator prototypes (from ip_vs_est.c)
 */
extern int ip_vs_estimator_init(void);
extern void ip_vs_estimator_cleanup(void);
extern void ip_vs_new_estimator(struct ip_vs_stats *stats);
extern void ip_vs_kill_estimator(struct ip_vs_stats *stats);
extern void ip_vs_zero_estimator(struct ip_vs_stats *stats);

/*
 *	Various IPVS packet transmitters (from ip_vs_xmit.c)
 */
extern int ip_vs_null_xmit
(struct sk_buff *skb, struct ip_vs_conn *cp, struct ip_vs_protocol *pp);
extern int ip_vs_bypass_xmit
(struct sk_buff *skb, struct ip_vs_conn *cp, struct ip_vs_protocol *pp);
extern int ip_vs_nat_xmit
(struct sk_buff *skb, struct ip_vs_conn *cp, struct ip_vs_protocol *pp);
extern int ip_vs_tunnel_xmit
(struct sk_buff *skb, struct ip_vs_conn *cp, struct ip_vs_protocol *pp);
extern int ip_vs_dr_xmit
(struct sk_buff *skb, struct ip_vs_conn *cp, struct ip_vs_protocol *pp);
extern int ip_vs_icmp_xmit
(struct sk_buff *skb, struct ip_vs_conn *cp, struct ip_vs_protocol *pp, int offset);
extern void ip_vs_dst_reset(struct ip_vs_dest *dest);


/*
 *	This is a simple mechanism to ignore packets when
 *	we are loaded. Just set ip_vs_drop_rate to 'n' and
 *	we start to drop 1/rate of the packets
 */
extern int ip_vs_drop_rate;
extern int ip_vs_drop_counter;

static __inline__ int ip_vs_todrop(void)
{
	if (!ip_vs_drop_rate) return 0;
	if (--ip_vs_drop_counter > 0) return 0;
	ip_vs_drop_counter = ip_vs_drop_rate;
	return 1;
}

/*
 *      ip_vs_fwd_tag returns the forwarding tag of the connection
 */
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

extern void ip_vs_nat_icmp(struct sk_buff *skb, struct ip_vs_protocol *pp,
		struct ip_vs_conn *cp, int dir);

extern __sum16 ip_vs_checksum_complete(struct sk_buff *skb, int offset);

static inline __wsum ip_vs_check_diff4(__be32 old, __be32 new, __wsum oldsum)
{
	__be32 diff[2] = { ~old, new };

	return csum_partial((char *) diff, sizeof(diff), oldsum);
}

#ifdef CONFIG_IP_VS_IPV6
static inline __wsum ip_vs_check_diff16(const __be32 *old, const __be32 *new,
					__wsum oldsum)
{
	__be32 diff[8] = { ~old[3], ~old[2], ~old[1], ~old[0],
			    new[3],  new[2],  new[1],  new[0] };

	return csum_partial((char *) diff, sizeof(diff), oldsum);
}
#endif

static inline __wsum ip_vs_check_diff2(__be16 old, __be16 new, __wsum oldsum)
{
	__be16 diff[2] = { ~old, new };

	return csum_partial((char *) diff, sizeof(diff), oldsum);
}

#endif /* __KERNEL__ */

#endif	/* _NET_IP_VS_H */
