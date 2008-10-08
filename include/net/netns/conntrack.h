#ifndef __NETNS_CONNTRACK_H
#define __NETNS_CONNTRACK_H

#include <asm/atomic.h>

struct netns_ct {
	atomic_t		count;
	unsigned int		expect_count;
	struct hlist_head	*hash;
	struct hlist_head	*expect_hash;
	int			hash_vmalloc;
	int			expect_vmalloc;
};
#endif
