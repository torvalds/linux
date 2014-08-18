/*
 * ieee802154 6lowpan in net namespaces
 */

#include <net/inet_frag.h>

#ifndef __NETNS_IEEE802154_6LOWPAN_H__
#define __NETNS_IEEE802154_6LOWPAN_H__

struct netns_sysctl_lowpan {
#ifdef CONFIG_SYSCTL
	struct ctl_table_header *frags_hdr;
#endif
};

struct netns_ieee802154_lowpan {
	struct netns_sysctl_lowpan sysctl;
	struct netns_frags	frags;
	int			max_dsize;
};

#endif
