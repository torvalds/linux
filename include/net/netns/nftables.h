/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NETNS_NFTABLES_H_
#define _NETNS_NFTABLES_H_

struct netns_nftables {
	unsigned int		base_seq;
	u8			gencursor;
};

#endif
