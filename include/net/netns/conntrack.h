#ifndef __NETNS_CONNTRACK_H
#define __NETNS_CONNTRACK_H

#include <asm/atomic.h>

struct netns_ct {
	atomic_t		count;
	struct hlist_head	*hash;
	int			hash_vmalloc;
};
#endif
