#ifndef _NETNS_NFTABLES_H_
#define _NETNS_NFTABLES_H_

#include <linux/list.h>

struct nft_af_info;

struct netns_nftables {
	struct list_head	af_info;
	struct nft_af_info	*ipv4;
	struct nft_af_info	*ipv6;
	struct nft_af_info	*bridge;
};

#endif
