#ifndef __NETNS_CONNTRACK_H
#define __NETNS_CONNTRACK_H

#include <linux/list.h>
#include <asm/atomic.h>

struct nf_conntrack_ecache;

struct netns_ct {
	atomic_t		count;
	unsigned int		expect_count;
	struct hlist_head	*hash;
	struct hlist_head	*expect_hash;
	struct hlist_head	unconfirmed;
#ifdef CONFIG_NF_CONNTRACK_EVENTS
	struct nf_conntrack_ecache *ecache;
#endif
	int			hash_vmalloc;
	int			expect_vmalloc;
};
#endif
