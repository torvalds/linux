/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2015 Tom Herbert <tom@herbertland.com>
 */

#ifndef __ILA_H
#define __ILA_H

#include <linux/errno.h>
#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/socket.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <net/checksum.h>
#include <net/genetlink.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <uapi/linux/ila.h>

struct ila_locator {
	union {
		__u8            v8[8];
		__be16          v16[4];
		__be32          v32[2];
		__be64		v64;
	};
};

struct ila_identifier {
	union {
		struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
			u8 __space:4;
			u8 csum_neutral:1;
			u8 type:3;
#elif defined(__BIG_ENDIAN_BITFIELD)
			u8 type:3;
			u8 csum_neutral:1;
			u8 __space:4;
#else
#error  "Adjust your <asm/byteorder.h> defines"
#endif
			u8 __space2[7];
		};
		__u8            v8[8];
		__be16          v16[4];
		__be32          v32[2];
		__be64		v64;
	};
};

#define CSUM_NEUTRAL_FLAG	htonl(0x10000000)

struct ila_addr {
	union {
		struct in6_addr addr;
		struct {
			struct ila_locator loc;
			struct ila_identifier ident;
		};
	};
};

static inline struct ila_addr *ila_a2i(struct in6_addr *addr)
{
	return (struct ila_addr *)addr;
}

struct ila_params {
	struct ila_locator locator;
	struct ila_locator locator_match;
	__wsum csum_diff;
	u8 csum_mode;
	u8 ident_type;
};

static inline __wsum compute_csum_diff8(const __be32 *from, const __be32 *to)
{
	__be32 diff[] = {
		~from[0], ~from[1], to[0], to[1],
	};

	return csum_partial(diff, sizeof(diff), 0);
}

static inline bool ila_csum_neutral_set(struct ila_identifier ident)
{
	return !!(ident.csum_neutral);
}

void ila_update_ipv6_locator(struct sk_buff *skb, struct ila_params *p,
			     bool set_csum_neutral);

void ila_init_saved_csum(struct ila_params *p);

struct ila_net {
	struct {
		struct rhashtable rhash_table;
		spinlock_t *locks; /* Bucket locks for entry manipulation */
		unsigned int locks_mask;
		bool hooks_registered;
	} xlat;
};

int ila_lwt_init(void);
void ila_lwt_fini(void);

int ila_xlat_init_net(struct net *net);
void ila_xlat_pre_exit_net(struct net *net);
void ila_xlat_exit_net(struct net *net);

int ila_xlat_nl_cmd_add_mapping(struct sk_buff *skb, struct genl_info *info);
int ila_xlat_nl_cmd_del_mapping(struct sk_buff *skb, struct genl_info *info);
int ila_xlat_nl_cmd_get_mapping(struct sk_buff *skb, struct genl_info *info);
int ila_xlat_nl_cmd_flush(struct sk_buff *skb, struct genl_info *info);
int ila_xlat_nl_dump_start(struct netlink_callback *cb);
int ila_xlat_nl_dump_done(struct netlink_callback *cb);
int ila_xlat_nl_dump(struct sk_buff *skb, struct netlink_callback *cb);

extern unsigned int ila_net_id;

extern struct genl_family ila_nl_family;

#endif /* __ILA_H */
