/*
 *      IP Virtual Server
 *      data structure and functionality definitions
 */

#ifndef _IP_VS_H
#define _IP_VS_H

#include <asm/types.h>		/* For __uXX types */

#define IP_VS_VERSION_CODE	0x010201
#define NVERSION(version)			\
	(version >> 16) & 0xFF,			\
	(version >> 8) & 0xFF,			\
	version & 0xFF

/*
 *      Virtual Service Flags
 */
#define IP_VS_SVC_F_PERSISTENT	0x0001		/* persistent port */
#define IP_VS_SVC_F_HASHED	0x0002		/* hashed entry */

/*
 *      Destination Server Flags
 */
#define IP_VS_DEST_F_AVAILABLE	0x0001		/* server is available */
#define IP_VS_DEST_F_OVERLOAD	0x0002		/* server is overloaded */

/*
 *      IPVS sync daemon states
 */
#define IP_VS_STATE_NONE	0x0000		/* daemon is stopped */
#define IP_VS_STATE_MASTER	0x0001		/* started as master */
#define IP_VS_STATE_BACKUP	0x0002		/* started as backup */

/*
 *      IPVS socket options
 */
#define IP_VS_BASE_CTL		(64+1024+64)		/* base */

#define IP_VS_SO_SET_NONE	IP_VS_BASE_CTL		/* just peek */
#define IP_VS_SO_SET_INSERT	(IP_VS_BASE_CTL+1)
#define IP_VS_SO_SET_ADD	(IP_VS_BASE_CTL+2)
#define IP_VS_SO_SET_EDIT	(IP_VS_BASE_CTL+3)
#define IP_VS_SO_SET_DEL	(IP_VS_BASE_CTL+4)
#define IP_VS_SO_SET_FLUSH	(IP_VS_BASE_CTL+5)
#define IP_VS_SO_SET_LIST	(IP_VS_BASE_CTL+6)
#define IP_VS_SO_SET_ADDDEST	(IP_VS_BASE_CTL+7)
#define IP_VS_SO_SET_DELDEST	(IP_VS_BASE_CTL+8)
#define IP_VS_SO_SET_EDITDEST	(IP_VS_BASE_CTL+9)
#define IP_VS_SO_SET_TIMEOUT	(IP_VS_BASE_CTL+10)
#define IP_VS_SO_SET_STARTDAEMON (IP_VS_BASE_CTL+11)
#define IP_VS_SO_SET_STOPDAEMON (IP_VS_BASE_CTL+12)
#define IP_VS_SO_SET_RESTORE    (IP_VS_BASE_CTL+13)
#define IP_VS_SO_SET_SAVE       (IP_VS_BASE_CTL+14)
#define IP_VS_SO_SET_ZERO	(IP_VS_BASE_CTL+15)
#define IP_VS_SO_SET_MAX	IP_VS_SO_SET_ZERO

#define IP_VS_SO_GET_VERSION	IP_VS_BASE_CTL
#define IP_VS_SO_GET_INFO	(IP_VS_BASE_CTL+1)
#define IP_VS_SO_GET_SERVICES	(IP_VS_BASE_CTL+2)
#define IP_VS_SO_GET_SERVICE	(IP_VS_BASE_CTL+3)
#define IP_VS_SO_GET_DESTS	(IP_VS_BASE_CTL+4)
#define IP_VS_SO_GET_DEST	(IP_VS_BASE_CTL+5)	/* not used now */
#define IP_VS_SO_GET_TIMEOUT	(IP_VS_BASE_CTL+6)
#define IP_VS_SO_GET_DAEMON	(IP_VS_BASE_CTL+7)
#define IP_VS_SO_GET_MAX	IP_VS_SO_GET_DAEMON


/*
 *      IPVS Connection Flags
 */
#define IP_VS_CONN_F_FWD_MASK	0x0007		/* mask for the fwd methods */
#define IP_VS_CONN_F_MASQ	0x0000		/* masquerading/NAT */
#define IP_VS_CONN_F_LOCALNODE	0x0001		/* local node */
#define IP_VS_CONN_F_TUNNEL	0x0002		/* tunneling */
#define IP_VS_CONN_F_DROUTE	0x0003		/* direct routing */
#define IP_VS_CONN_F_BYPASS	0x0004		/* cache bypass */
#define IP_VS_CONN_F_SYNC	0x0020		/* entry created by sync */
#define IP_VS_CONN_F_HASHED	0x0040		/* hashed entry */
#define IP_VS_CONN_F_NOOUTPUT	0x0080		/* no output packets */
#define IP_VS_CONN_F_INACTIVE	0x0100		/* not established */
#define IP_VS_CONN_F_OUT_SEQ	0x0200		/* must do output seq adjust */
#define IP_VS_CONN_F_IN_SEQ	0x0400		/* must do input seq adjust */
#define IP_VS_CONN_F_SEQ_MASK	0x0600		/* in/out sequence mask */
#define IP_VS_CONN_F_NO_CPORT	0x0800		/* no client port set yet */
#define IP_VS_CONN_F_TEMPLATE	0x1000		/* template, not connection */

/* Move it to better place one day, for now keep it unique */
#define NFC_IPVS_PROPERTY	0x10000

#define IP_VS_SCHEDNAME_MAXLEN	16
#define IP_VS_IFNAME_MAXLEN	16


/*
 *	The struct ip_vs_service_user and struct ip_vs_dest_user are
 *	used to set IPVS rules through setsockopt.
 */
struct ip_vs_service_user {
	/* virtual service addresses */
	u_int16_t		protocol;
	u_int32_t		addr;		/* virtual ip address */
	u_int16_t		port;
	u_int32_t		fwmark;		/* firwall mark of service */

	/* virtual service options */
	char			sched_name[IP_VS_SCHEDNAME_MAXLEN];
	unsigned		flags;		/* virtual service flags */
	unsigned		timeout;	/* persistent timeout in sec */
	u_int32_t		netmask;	/* persistent netmask */
};


struct ip_vs_dest_user {
	/* destination server address */
	u_int32_t		addr;
	u_int16_t		port;

	/* real server options */
	unsigned		conn_flags;	/* connection flags */
	int			weight;		/* destination weight */

	/* thresholds for active connections */
	u_int32_t		u_threshold;	/* upper threshold */
	u_int32_t		l_threshold;	/* lower threshold */
};


/*
 *	IPVS statistics object (for user space)
 */
struct ip_vs_stats_user
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
};


/* The argument to IP_VS_SO_GET_INFO */
struct ip_vs_getinfo {
	/* version number */
	unsigned int		version;

	/* size of connection hash table */
	unsigned int		size;

	/* number of virtual services */
	unsigned int		num_services;
};


/* The argument to IP_VS_SO_GET_SERVICE */
struct ip_vs_service_entry {
	/* which service: user fills in these */
	u_int16_t		protocol;
	u_int32_t		addr;		/* virtual address */
	u_int16_t		port;
	u_int32_t		fwmark;		/* firwall mark of service */

	/* service options */
	char			sched_name[IP_VS_SCHEDNAME_MAXLEN];
	unsigned		flags;          /* virtual service flags */
	unsigned		timeout;	/* persistent timeout */
	u_int32_t		netmask;	/* persistent netmask */

	/* number of real servers */
	unsigned int		num_dests;

	/* statistics */
	struct ip_vs_stats_user stats;
};


struct ip_vs_dest_entry {
	u_int32_t		addr;		/* destination address */
	u_int16_t		port;
	unsigned		conn_flags;	/* connection flags */
	int			weight;		/* destination weight */

	u_int32_t		u_threshold;	/* upper threshold */
	u_int32_t		l_threshold;	/* lower threshold */

	u_int32_t		activeconns;	/* active connections */
	u_int32_t		inactconns;	/* inactive connections */
	u_int32_t		persistconns;	/* persistent connections */

	/* statistics */
	struct ip_vs_stats_user stats;
};


/* The argument to IP_VS_SO_GET_DESTS */
struct ip_vs_get_dests {
	/* which service: user fills in these */
	u_int16_t		protocol;
	u_int32_t		addr;		/* virtual address */
	u_int16_t		port;
	u_int32_t		fwmark;		/* firwall mark of service */

	/* number of real servers */
	unsigned int		num_dests;

	/* the real servers */
	struct ip_vs_dest_entry	entrytable[0];
};


/* The argument to IP_VS_SO_GET_SERVICES */
struct ip_vs_get_services {
	/* number of virtual services */
	unsigned int		num_services;

	/* service table */
	struct ip_vs_service_entry entrytable[0];
};


/* The argument to IP_VS_SO_GET_TIMEOUT */
struct ip_vs_timeout_user {
	int			tcp_timeout;
	int			tcp_fin_timeout;
	int			udp_timeout;
};


/* The argument to IP_VS_SO_GET_DAEMON */
struct ip_vs_daemon_user {
	/* sync daemon state (master/backup) */
	int			state;

	/* multicast interface name */
	char			mcast_ifn[IP_VS_IFNAME_MAXLEN];

	/* SyncID we belong to */
	int			syncid;
};


#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/list.h>                 /* for struct list_head */
#include <linux/spinlock.h>             /* for struct rwlock_t */
#include <linux/skbuff.h>               /* for struct sk_buff */
#include <linux/ip.h>                   /* for struct iphdr */
#include <asm/atomic.h>                 /* for struct atomic_t */
#include <linux/netdevice.h>		/* for struct neighbour */
#include <net/dst.h>			/* for struct dst_entry */
#include <net/udp.h>
#include <linux/compiler.h>


#ifdef CONFIG_IP_VS_DEBUG
extern int ip_vs_get_debug_level(void);
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
 *      IPVS sysctl variables under the /proc/sys/net/ipv4/vs/
 */
#define NET_IPV4_VS              21

enum {
	NET_IPV4_VS_DEBUG_LEVEL=1,
	NET_IPV4_VS_AMEMTHRESH=2,
	NET_IPV4_VS_AMDROPRATE=3,
	NET_IPV4_VS_DROP_ENTRY=4,
	NET_IPV4_VS_DROP_PACKET=5,
	NET_IPV4_VS_SECURE_TCP=6,
	NET_IPV4_VS_TO_ES=7,
	NET_IPV4_VS_TO_SS=8,
	NET_IPV4_VS_TO_SR=9,
	NET_IPV4_VS_TO_FW=10,
	NET_IPV4_VS_TO_TW=11,
	NET_IPV4_VS_TO_CL=12,
	NET_IPV4_VS_TO_CW=13,
	NET_IPV4_VS_TO_LA=14,
	NET_IPV4_VS_TO_LI=15,
	NET_IPV4_VS_TO_SA=16,
	NET_IPV4_VS_TO_UDP=17,
	NET_IPV4_VS_TO_ICMP=18,
	NET_IPV4_VS_LBLC_EXPIRE=19,
	NET_IPV4_VS_LBLCR_EXPIRE=20,
	NET_IPV4_VS_CACHE_BYPASS=22,
	NET_IPV4_VS_EXPIRE_NODEST_CONN=23,
	NET_IPV4_VS_SYNC_THRESHOLD=24,
	NET_IPV4_VS_NAT_ICMP_SEND=25,
	NET_IPV4_VS_EXPIRE_QUIESCENT_TEMPLATE=26,
	NET_IPV4_VS_LAST
};

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
 *	IPVS statistics object
 */
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

	spinlock_t              lock;           /* spin lock */
};

struct ip_vs_conn;
struct ip_vs_app;

struct ip_vs_protocol {
	struct ip_vs_protocol	*next;
	char			*name;
	__u16			protocol;
	int			dont_defrag;
	atomic_t		appcnt;		/* counter of proto app incs */
	int			*timeout_table;	/* protocol timeout table */

	void (*init)(struct ip_vs_protocol *pp);

	void (*exit)(struct ip_vs_protocol *pp);

	int (*conn_schedule)(struct sk_buff *skb,
			     struct ip_vs_protocol *pp,
			     int *verdict, struct ip_vs_conn **cpp);

	struct ip_vs_conn *
	(*conn_in_get)(const struct sk_buff *skb,
		       struct ip_vs_protocol *pp,
		       const struct iphdr *iph,
		       unsigned int proto_off,
		       int inverse);

	struct ip_vs_conn *
	(*conn_out_get)(const struct sk_buff *skb,
			struct ip_vs_protocol *pp,
			const struct iphdr *iph,
			unsigned int proto_off,
			int inverse);

	int (*snat_handler)(struct sk_buff **pskb,
			    struct ip_vs_protocol *pp, struct ip_vs_conn *cp);

	int (*dnat_handler)(struct sk_buff **pskb,
			    struct ip_vs_protocol *pp, struct ip_vs_conn *cp);

	int (*csum_check)(struct sk_buff *skb, struct ip_vs_protocol *pp);

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
	__u32                   caddr;          /* client address */
	__u32                   vaddr;          /* virtual address */
	__u32                   daddr;          /* destination address */
	__u16                   cport;
	__u16                   vport;
	__u16                   dport;
	__u16                   protocol;       /* Which protocol (TCP/UDP) */

	/* counter and timer */
	atomic_t		refcnt;		/* reference count */
	struct timer_list	timer;		/* Expiration timer */
	volatile unsigned long	timeout;	/* timeout */

	/* Flags and state transition */
	spinlock_t              lock;           /* lock for state transition */
	volatile __u16          flags;          /* status flags */
	volatile __u16          state;          /* state info */

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
 *	The information about the virtual service offered to the net
 *	and the forwarding entries
 */
struct ip_vs_service {
	struct list_head	s_list;   /* for normal service table */
	struct list_head	f_list;   /* for fwmark-based service table */
	atomic_t		refcnt;   /* reference counter */
	atomic_t		usecnt;   /* use counter */

	__u16			protocol; /* which protocol (TCP/UDP) */
	__u32			addr;	  /* IP address for virtual service */
	__u16			port;	  /* port number for the service */
	__u32                   fwmark;   /* firewall mark of the service */
	unsigned		flags;	  /* service status flags */
	unsigned		timeout;  /* persistent timeout in ticks */
	__u32			netmask;  /* grouping granularity */

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

	__u32			addr;		/* IP address of the server */
	__u16			port;		/* port number of the server */
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
	__u32			vaddr;		/* virtual IP address */
	__u16			vport;		/* virtual port number */
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
	__u16			port;		/* port number in net order */
	atomic_t		usecnt;		/* usage counter */

	/* output hook: return false if can't linearize. diff set for TCP.  */
	int (*pkt_out)(struct ip_vs_app *, struct ip_vs_conn *,
		       struct sk_buff **, int *diff);

	/* input hook: return false if can't linearize. diff set for TCP. */
	int (*pkt_in)(struct ip_vs_app *, struct ip_vs_conn *,
		      struct sk_buff **, int *diff);

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
#define IP_VS_INIT_HASH_TABLE(t) ip_vs_init_hash_table(t, sizeof(t)/sizeof(t[0]))

#define IP_VS_APP_TYPE_UNSPEC	0
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
(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port);
extern struct ip_vs_conn *ip_vs_ct_in_get
(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port);
extern struct ip_vs_conn *ip_vs_conn_out_get
(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port);

/* put back the conn without restarting its timer */
static inline void __ip_vs_conn_put(struct ip_vs_conn *cp)
{
	atomic_dec(&cp->refcnt);
}
extern void ip_vs_conn_put(struct ip_vs_conn *cp);
extern void ip_vs_conn_fill_cport(struct ip_vs_conn *cp, __u16 cport);

extern struct ip_vs_conn *
ip_vs_conn_new(int proto, __u32 caddr, __u16 cport, __u32 vaddr, __u16 vport,
	       __u32 daddr, __u16 dport, unsigned flags,
	       struct ip_vs_dest *dest);
extern void ip_vs_conn_expire_now(struct ip_vs_conn *cp);

extern const char * ip_vs_state_name(__u16 proto, int state);

extern void ip_vs_tcp_conn_listen(struct ip_vs_conn *cp);
extern int ip_vs_check_template(struct ip_vs_conn *ct);
extern void ip_vs_secure_tcp_set(int on);
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

extern int ip_vs_app_pkt_out(struct ip_vs_conn *, struct sk_buff **pskb);
extern int ip_vs_app_pkt_in(struct ip_vs_conn *, struct sk_buff **pskb);
extern int ip_vs_skb_replace(struct sk_buff *skb, unsigned int __nocast pri,
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

extern struct ip_vs_service *
ip_vs_service_get(__u32 fwmark, __u16 protocol, __u32 vaddr, __u16 vport);

static inline void ip_vs_service_put(struct ip_vs_service *svc)
{
	atomic_dec(&svc->usecnt);
}

extern struct ip_vs_dest *
ip_vs_lookup_real_service(__u16 protocol, __u32 daddr, __u16 dport);
extern int ip_vs_use_count_inc(void);
extern void ip_vs_use_count_dec(void);
extern int ip_vs_control_init(void);
extern void ip_vs_control_cleanup(void);


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
extern int ip_vs_new_estimator(struct ip_vs_stats *stats);
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

extern int ip_vs_make_skb_writable(struct sk_buff **pskb, int len);
extern void ip_vs_nat_icmp(struct sk_buff *skb, struct ip_vs_protocol *pp,
		struct ip_vs_conn *cp, int dir);

extern u16 ip_vs_checksum_complete(struct sk_buff *skb, int offset);

static inline u16 ip_vs_check_diff(u32 old, u32 new, u16 oldsum)
{
	u32 diff[2] = { old, new };

	return csum_fold(csum_partial((char *) diff, sizeof(diff),
				      oldsum ^ 0xFFFF));
}

#endif /* __KERNEL__ */

#endif	/* _IP_VS_H */
