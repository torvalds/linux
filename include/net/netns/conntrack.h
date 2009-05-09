#ifndef __NETNS_CONNTRACK_H
#define __NETNS_CONNTRACK_H

#include <linux/list.h>
#include <linux/list_nulls.h>
#include <asm/atomic.h>

struct ctl_table_header;
struct nf_conntrack_ecache;

struct netns_ct {
	atomic_t		count;
	unsigned int		expect_count;
	struct hlist_nulls_head	*hash;
	struct hlist_head	*expect_hash;
	struct hlist_nulls_head	unconfirmed;
	struct ip_conntrack_stat *stat;
#ifdef CONFIG_NF_CONNTRACK_EVENTS
	struct nf_conntrack_ecache *ecache;
#endif
	int			sysctl_acct;
	int			sysctl_checksum;
	unsigned int		sysctl_log_invalid; /* Log invalid packets */
#ifdef CONFIG_SYSCTL
	struct ctl_table_header	*sysctl_header;
	struct ctl_table_header	*acct_sysctl_header;
#endif
	int			hash_vmalloc;
	int			expect_vmalloc;
};
#endif
