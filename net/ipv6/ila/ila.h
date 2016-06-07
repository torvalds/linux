/*
 * Copyright (c) 2015 Tom Herbert <tom@herbertland.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
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

enum {
	ILA_ATYPE_IID = 0,
	ILA_ATYPE_LUID,
	ILA_ATYPE_VIRT_V4,
	ILA_ATYPE_VIRT_UNI_V6,
	ILA_ATYPE_VIRT_MULTI_V6,
	ILA_ATYPE_RSVD_1,
	ILA_ATYPE_RSVD_2,
	ILA_ATYPE_RSVD_3,
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

static inline bool ila_addr_is_ila(struct ila_addr *iaddr)
{
	return (iaddr->ident.type != ILA_ATYPE_IID);
}

struct ila_params {
	struct ila_locator locator;
	struct ila_locator locator_match;
	__wsum csum_diff;
	u8 csum_mode;
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

int ila_lwt_init(void);
void ila_lwt_fini(void);
int ila_xlat_init(void);
void ila_xlat_fini(void);

#endif /* __ILA_H */
