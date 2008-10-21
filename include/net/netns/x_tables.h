#ifndef __NETNS_X_TABLES_H
#define __NETNS_X_TABLES_H

#include <linux/list.h>
#include <linux/netfilter.h>

struct netns_xt {
	struct list_head tables[NFPROTO_NUMPROTO];
};
#endif
