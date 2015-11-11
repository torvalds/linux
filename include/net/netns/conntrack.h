#ifndef __NETNS_CONNTRACK_H
#define __NETNS_CONNTRACK_H

#include <linux/list.h>
#include <linux/list_nulls.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>
#include <linux/netfilter/nf_conntrack_tcp.h>
#include <linux/seqlock.h>

struct ctl_table_header;
struct nf_conntrack_ecache;

struct nf_proto_net {
#ifdef CONFIG_SYSCTL
	struct ctl_table_header *ctl_table_header;
	struct ctl_table        *ctl_table;
#ifdef CONFIG_NF_CONNTRACK_PROC_COMPAT
	struct ctl_table_header *ctl_compat_header;
	struct ctl_table        *ctl_compat_table;
#endif
#endif
	unsigned int		users;
};

struct nf_generic_net {
	struct nf_proto_net pn;
	unsigned int timeout;
};

struct nf_tcp_net {
	struct nf_proto_net pn;
	unsigned int timeouts[TCP_CONNTRACK_TIMEOUT_MAX];
	unsigned int tcp_loose;
	unsigned int tcp_be_liberal;
	unsigned int tcp_max_retrans;
};

enum udp_conntrack {
	UDP_CT_UNREPLIED,
	UDP_CT_REPLIED,
	UDP_CT_MAX
};

struct nf_udp_net {
	struct nf_proto_net pn;
	unsigned int timeouts[UDP_CT_MAX];
};

struct nf_icmp_net {
	struct nf_proto_net pn;
	unsigned int timeout;
};

struct nf_ip_net {
	struct nf_generic_net   generic;
	struct nf_tcp_net	tcp;
	struct nf_udp_net	udp;
	struct nf_icmp_net	icmp;
	struct nf_icmp_net	icmpv6;
#if defined(CONFIG_SYSCTL) && defined(CONFIG_NF_CONNTRACK_PROC_COMPAT)
	struct ctl_table_header *ctl_table_header;
	struct ctl_table	*ctl_table;
#endif
};

struct ct_pcpu {
	spinlock_t		lock;
	struct hlist_nulls_head unconfirmed;
	struct hlist_nulls_head dying;
};

struct netns_ct {
	atomic_t		count;
	unsigned int		expect_count;
#ifdef CONFIG_NF_CONNTRACK_EVENTS
	struct delayed_work ecache_dwork;
	bool ecache_dwork_pending;
#endif
#ifdef CONFIG_SYSCTL
	struct ctl_table_header	*sysctl_header;
	struct ctl_table_header	*acct_sysctl_header;
	struct ctl_table_header	*tstamp_sysctl_header;
	struct ctl_table_header	*event_sysctl_header;
	struct ctl_table_header	*helper_sysctl_header;
#endif
	char			*slabname;
	unsigned int		sysctl_log_invalid; /* Log invalid packets */
	int			sysctl_events;
	int			sysctl_acct;
	int			sysctl_auto_assign_helper;
	bool			auto_assign_helper_warned;
	int			sysctl_tstamp;
	int			sysctl_checksum;

	unsigned int		htable_size;
	seqcount_t		generation;
	struct kmem_cache	*nf_conntrack_cachep;
	struct hlist_nulls_head	*hash;
	struct hlist_head	*expect_hash;
	struct ct_pcpu __percpu *pcpu_lists;
	struct ip_conntrack_stat __percpu *stat;
	struct nf_ct_event_notifier __rcu *nf_conntrack_event_cb;
	struct nf_exp_event_notifier __rcu *nf_expect_event_cb;
	struct nf_ip_net	nf_ct_proto;
#if defined(CONFIG_NF_CONNTRACK_LABELS)
	unsigned int		labels_used;
	u8			label_words;
#endif
#ifdef CONFIG_NF_NAT_NEEDED
	struct hlist_head	*nat_bysource;
	unsigned int		nat_htable_size;
#endif
};
#endif
