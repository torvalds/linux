/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NETNS_NETFILTER_H
#define __NETNS_NETFILTER_H

#include <linux/netfilter_defs.h>

struct proc_dir_entry;
struct nf_logger;
struct nf_queue_handler;

struct netns_nf {
#if defined CONFIG_PROC_FS
	struct proc_dir_entry *proc_netfilter;
#endif
	const struct nf_logger __rcu *nf_loggers[NFPROTO_NUMPROTO];
#ifdef CONFIG_SYSCTL
	struct ctl_table_header *nf_log_dir_header;
#endif
	struct nf_hook_entries __rcu *hooks_ipv4[NF_INET_NUMHOOKS];
	struct nf_hook_entries __rcu *hooks_ipv6[NF_INET_NUMHOOKS];
#ifdef CONFIG_NETFILTER_FAMILY_ARP
	struct nf_hook_entries __rcu *hooks_arp[NF_ARP_NUMHOOKS];
#endif
#ifdef CONFIG_NETFILTER_FAMILY_BRIDGE
	struct nf_hook_entries __rcu *hooks_bridge[NF_INET_NUMHOOKS];
#endif
#if IS_ENABLED(CONFIG_DECNET)
	struct nf_hook_entries __rcu *hooks_decnet[NF_DN_NUMHOOKS];
#endif
#if IS_ENABLED(CONFIG_NF_DEFRAG_IPV4)
	unsigned int defrag_ipv4_users;
#endif
#if IS_ENABLED(CONFIG_NF_DEFRAG_IPV6)
	unsigned int defrag_ipv6_users;
#endif
};
#endif
