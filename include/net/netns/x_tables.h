#ifndef __NETNS_X_TABLES_H
#define __NETNS_X_TABLES_H

#include <linux/list.h>
#include <linux/net.h>

struct netns_xt {
	struct list_head tables[NPROTO];
};
#endif
