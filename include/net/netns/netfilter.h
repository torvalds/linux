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
	const struct nf_queue_handler __rcu *queue_handler;
	const struct nf_logger __rcu *nf_loggers[NFPROTO_NUMPROTO];
#ifdef CONFIG_SYSCTL
	struct ctl_table_header *nf_log_dir_header;
#endif
	struct list_head hooks[NFPROTO_NUMPROTO][NF_MAX_HOOKS];
};
#endif
