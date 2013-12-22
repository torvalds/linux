#ifndef _NETNS_NFTABLES_H_
#define _NETNS_NFTABLES_H_

#include <linux/list.h>

struct nft_af_info;

struct netns_nftables {
	struct list_head	af_info;
	struct list_head	commit_list;
	struct nft_af_info	*ipv4;
	struct nft_af_info	*ipv6;
	struct nft_af_info	*arp;
	struct nft_af_info	*bridge;
	u8			gencursor;
	u8			genctr;
};

#endif
