/*
 *  IP Virtual Server
 *  Data structure for network namspace
 *
 */

#ifndef IP_VS_H_
#define IP_VS_H_

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/list_nulls.h>
#include <linux/ip_vs.h>
#include <asm/atomic.h>
#include <linux/in.h>

struct ip_vs_stats;
struct ip_vs_sync_buff;
struct ctl_table_header;

struct netns_ipvs {
	int			gen;		/* Generation */
	/*
	 *	Hash table: for real service lookups
	 */
	#define IP_VS_RTAB_BITS 4
	#define IP_VS_RTAB_SIZE (1 << IP_VS_RTAB_BITS)
	#define IP_VS_RTAB_MASK (IP_VS_RTAB_SIZE - 1)

	struct list_head	rs_table[IP_VS_RTAB_SIZE];
	/* ip_vs_app */
	struct list_head	app_list;
	struct mutex		app_mutex;
	struct lock_class_key	app_key;	/* mutex debuging */

	/* ip_vs_proto */
	#define IP_VS_PROTO_TAB_SIZE	32	/* must be power of 2 */
	struct ip_vs_proto_data *proto_data_table[IP_VS_PROTO_TAB_SIZE];
	/* ip_vs_proto_tcp */
#ifdef CONFIG_IP_VS_PROTO_TCP
	#define	TCP_APP_TAB_BITS	4
	#define	TCP_APP_TAB_SIZE	(1 << TCP_APP_TAB_BITS)
	#define	TCP_APP_TAB_MASK	(TCP_APP_TAB_SIZE - 1)
	struct list_head	tcp_apps[TCP_APP_TAB_SIZE];
	spinlock_t		tcp_app_lock;
#endif
	/* ip_vs_proto_udp */
#ifdef CONFIG_IP_VS_PROTO_UDP
	#define	UDP_APP_TAB_BITS	4
	#define	UDP_APP_TAB_SIZE	(1 << UDP_APP_TAB_BITS)
	#define	UDP_APP_TAB_MASK	(UDP_APP_TAB_SIZE - 1)
	struct list_head	udp_apps[UDP_APP_TAB_SIZE];
	spinlock_t		udp_app_lock;
#endif
	/* ip_vs_proto_sctp */
#ifdef CONFIG_IP_VS_PROTO_SCTP
	#define SCTP_APP_TAB_BITS	4
	#define SCTP_APP_TAB_SIZE	(1 << SCTP_APP_TAB_BITS)
	#define SCTP_APP_TAB_MASK	(SCTP_APP_TAB_SIZE - 1)
	/* Hash table for SCTP application incarnations	 */
	struct list_head	sctp_apps[SCTP_APP_TAB_SIZE];
	spinlock_t		sctp_app_lock;
#endif
	/* ip_vs_conn */
	atomic_t		conn_count;      /*  connection counter */

	/* ip_vs_ctl */
	struct ip_vs_stats		*tot_stats;  /* Statistics & est. */
	struct ip_vs_cpu_stats __percpu *cpustats;   /* Stats per cpu */
	seqcount_t			*ustats_seq; /* u64 read retry */

	int			num_services;    /* no of virtual services */
	/* 1/rate drop and drop-entry variables */
	int			drop_rate;
	int			drop_counter;
	atomic_t		dropentry;
	/* locks in ctl.c */
	spinlock_t		dropentry_lock;  /* drop entry handling */
	spinlock_t		droppacket_lock; /* drop packet handling */
	spinlock_t		securetcp_lock;  /* state and timeout tables */
	rwlock_t		rs_lock;         /* real services table */
	/* semaphore for IPVS sockopts. And, [gs]etsockopt may sleep. */
	struct lock_class_key	ctl_key;	/* ctl_mutex debuging */
	/* sys-ctl struct */
	struct ctl_table_header	*sysctl_hdr;
	struct ctl_table	*sysctl_tbl;
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
	int			sysctl_cache_bypass;
	int			sysctl_expire_nodest_conn;
	int			sysctl_expire_quiescent_template;
	int			sysctl_sync_threshold[2];
	int			sysctl_nat_icmp_send;

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
	struct list_head	sync_queue;
	spinlock_t		sync_lock;
	struct ip_vs_sync_buff  *sync_buff;
	spinlock_t		sync_buff_lock;
	struct sockaddr_in	sync_mcast_addr;
	struct task_struct	*master_thread;
	struct task_struct	*backup_thread;
	int			send_mesg_maxlen;
	int			recv_mesg_maxlen;
	volatile int		sync_state;
	volatile int		master_syncid;
	volatile int		backup_syncid;
	/* multicast interface name */
	char			master_mcast_ifn[IP_VS_IFNAME_MAXLEN];
	char			backup_mcast_ifn[IP_VS_IFNAME_MAXLEN];
};

#endif /* IP_VS_H_ */
