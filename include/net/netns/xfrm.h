#ifndef __NETNS_XFRM_H
#define __NETNS_XFRM_H

#include <linux/list.h>

struct netns_xfrm {
	struct list_head	state_all;
};

#endif
