/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NETNS_CONNTRACK_H
#define __NETNS_CONNTRACK_H

#include <linux/list.h>
#include <linux/list_nulls.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>
#include <linux/netfilter/nf_conntrack_tcp.h>
#ifdef CONFIG_NF_CT_PROTO_DCCP
#include <linux/netfilter/nf_conntrack_dccp.h>
#endif
#ifdef CONFIG_NF_CT_PROTO_SCTP
#include <linux/netfilter/nf_conntrack_sctp.h>
#endif
#include <linux/seqlock.h>

struct ctl_table_header;
struct nf_conntrack_ecache;

struct nf_generic_net {
	unsigned int timeout;
};

struct nf_tcp_net {
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
	unsigned int timeouts[UDP_CT_MAX];
};

struct nf_icmp_net {
	unsigned int timeout;
};

#ifdef CONFIG_NF_CT_PROTO_DCCP
struct nf_dccp_net {
	int dccp_loose;
	unsigned int dccp_timeout[CT_DCCP_MAX + 1];
};
#endif

#ifdef CONFIG_NF_CT_PROTO_SCTP
struct nf_sctp_net {
	unsigned int timeouts[SCTP_CONNTRACK_MAX];
};
#endif

#ifdef CONFIG_NF_CT_PROTO_GRE
enum gre_conntrack {
	GRE_CT_UNREPLIED,
	GRE_CT_REPLIED,
	GRE_CT_MAX
};

struct nf_gre_net {
	struct list_head	keymap_list;
	unsigned int		timeouts[GRE_CT_MAX];
};
#endif

struct nf_ip_net {
	struct nf_generic_net   generic;
	struct nf_tcp_net	tcp;
	struct nf_udp_net	udp;
	struct nf_icmp_net	icmp;
	struct nf_icmp_net	icmpv6;
#ifdef CONFIG_NF_CT_PROTO_DCCP
	struct nf_dccp_net	dccp;
#endif
#ifdef CONFIG_NF_CT_PROTO_SCTP
	struct nf_sctp_net	sctp;
#endif
#ifdef CONFIG_NF_CT_PROTO_GRE
	struct nf_gre_net	gre;
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
	bool			auto_assign_helper_warned;
#ifdef CONFIG_SYSCTL
	struct ctl_table_header	*sysctl_header;
#endif
	unsigned int		sysctl_log_invalid; /* Log invalid packets */
	int			sysctl_events;
	int			sysctl_acct;
	int			sysctl_auto_assign_helper;
	int			sysctl_tstamp;
	int			sysctl_checksum;

	struct ct_pcpu __percpu *pcpu_lists;
	struct ip_conntrack_stat __percpu *stat;
	struct nf_ct_event_notifier __rcu *nf_conntrack_event_cb;
	struct nf_exp_event_notifier __rcu *nf_expect_event_cb;
	struct nf_ip_net	nf_ct_proto;
#if defined(CONFIG_NF_CONNTRACK_LABELS)
	unsigned int		labels_used;
#endif
};
#endif
