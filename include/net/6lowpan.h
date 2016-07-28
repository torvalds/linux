/*
 * Copyright 2011, Siemens AG
 * written by Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */

/*
 * Based on patches from Jon Smirl <jonsmirl@gmail.com>
 * Copyright (c) 2011 Jon Smirl <jonsmirl@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* Jon's code is based on 6lowpan implementation for Contiki which is:
 * Copyright (c) 2008, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef __6LOWPAN_H__
#define __6LOWPAN_H__

#include <linux/debugfs.h>

#include <net/ipv6.h>
#include <net/net_namespace.h>

/* special link-layer handling */
#include <net/mac802154.h>

#define EUI64_ADDR_LEN		8

#define LOWPAN_NHC_MAX_ID_LEN	1
/* Maximum next header compression length which we currently support inclusive
 * possible inline data.
 */
#define LOWPAN_NHC_MAX_HDR_LEN	(sizeof(struct udphdr))
/* Max IPHC Header len without IPv6 hdr specific inline data.
 * Useful for getting the "extra" bytes we need at worst case compression.
 *
 * LOWPAN_IPHC + CID + LOWPAN_NHC_MAX_ID_LEN
 */
#define LOWPAN_IPHC_MAX_HEADER_LEN	(2 + 1 + LOWPAN_NHC_MAX_ID_LEN)
/* Maximum worst case IPHC header buffer size */
#define LOWPAN_IPHC_MAX_HC_BUF_LEN	(sizeof(struct ipv6hdr) +	\
					 LOWPAN_IPHC_MAX_HEADER_LEN +	\
					 LOWPAN_NHC_MAX_HDR_LEN)
/* SCI/DCI is 4 bit width, so we have maximum 16 entries */
#define LOWPAN_IPHC_CTX_TABLE_SIZE	(1 << 4)

#define LOWPAN_DISPATCH_IPV6		0x41 /* 01000001 = 65 */
#define LOWPAN_DISPATCH_IPHC		0x60 /* 011xxxxx = ... */
#define LOWPAN_DISPATCH_IPHC_MASK	0xe0

static inline bool lowpan_is_ipv6(u8 dispatch)
{
	return dispatch == LOWPAN_DISPATCH_IPV6;
}

static inline bool lowpan_is_iphc(u8 dispatch)
{
	return (dispatch & LOWPAN_DISPATCH_IPHC_MASK) == LOWPAN_DISPATCH_IPHC;
}

#define LOWPAN_PRIV_SIZE(llpriv_size)	\
	(sizeof(struct lowpan_dev) + llpriv_size)

enum lowpan_lltypes {
	LOWPAN_LLTYPE_BTLE,
	LOWPAN_LLTYPE_IEEE802154,
};

enum lowpan_iphc_ctx_flags {
	LOWPAN_IPHC_CTX_FLAG_ACTIVE,
	LOWPAN_IPHC_CTX_FLAG_COMPRESSION,
};

struct lowpan_iphc_ctx {
	u8 id;
	struct in6_addr pfx;
	u8 plen;
	unsigned long flags;
};

struct lowpan_iphc_ctx_table {
	spinlock_t lock;
	const struct lowpan_iphc_ctx_ops *ops;
	struct lowpan_iphc_ctx table[LOWPAN_IPHC_CTX_TABLE_SIZE];
};

static inline bool lowpan_iphc_ctx_is_active(const struct lowpan_iphc_ctx *ctx)
{
	return test_bit(LOWPAN_IPHC_CTX_FLAG_ACTIVE, &ctx->flags);
}

static inline bool
lowpan_iphc_ctx_is_compression(const struct lowpan_iphc_ctx *ctx)
{
	return test_bit(LOWPAN_IPHC_CTX_FLAG_COMPRESSION, &ctx->flags);
}

struct lowpan_dev {
	enum lowpan_lltypes lltype;
	struct dentry *iface_debugfs;
	struct lowpan_iphc_ctx_table ctx;

	/* must be last */
	u8 priv[0] __aligned(sizeof(void *));
};

struct lowpan_802154_neigh {
	__le16 short_addr;
};

static inline
struct lowpan_802154_neigh *lowpan_802154_neigh(void *neigh_priv)
{
	return neigh_priv;
}

static inline
struct lowpan_dev *lowpan_dev(const struct net_device *dev)
{
	return netdev_priv(dev);
}

/* private device info */
struct lowpan_802154_dev {
	struct net_device	*wdev; /* wpan device ptr */
	u16			fragment_tag;
};

static inline struct
lowpan_802154_dev *lowpan_802154_dev(const struct net_device *dev)
{
	return (struct lowpan_802154_dev *)lowpan_dev(dev)->priv;
}

struct lowpan_802154_cb {
	u16 d_tag;
	unsigned int d_size;
	u8 d_offset;
};

static inline
struct lowpan_802154_cb *lowpan_802154_cb(const struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct lowpan_802154_cb) > sizeof(skb->cb));
	return (struct lowpan_802154_cb *)skb->cb;
}

static inline void lowpan_iphc_uncompress_eui64_lladdr(struct in6_addr *ipaddr,
						       const void *lladdr)
{
	/* fe:80::XXXX:XXXX:XXXX:XXXX
	 *        \_________________/
	 *              hwaddr
	 */
	ipaddr->s6_addr[0] = 0xFE;
	ipaddr->s6_addr[1] = 0x80;
	memcpy(&ipaddr->s6_addr[8], lladdr, EUI64_ADDR_LEN);
	/* second bit-flip (Universe/Local)
	 * is done according RFC2464
	 */
	ipaddr->s6_addr[8] ^= 0x02;
}

#ifdef DEBUG
/* print data in line */
static inline void raw_dump_inline(const char *caller, char *msg,
				   const unsigned char *buf, int len)
{
	if (msg)
		pr_debug("%s():%s: ", caller, msg);

	print_hex_dump_debug("", DUMP_PREFIX_NONE, 16, 1, buf, len, false);
}

/* print data in a table format:
 *
 * addr: xx xx xx xx xx xx
 * addr: xx xx xx xx xx xx
 * ...
 */
static inline void raw_dump_table(const char *caller, char *msg,
				  const unsigned char *buf, int len)
{
	if (msg)
		pr_debug("%s():%s:\n", caller, msg);

	print_hex_dump_debug("\t", DUMP_PREFIX_OFFSET, 16, 1, buf, len, false);
}
#else
static inline void raw_dump_table(const char *caller, char *msg,
				  const unsigned char *buf, int len) { }
static inline void raw_dump_inline(const char *caller, char *msg,
				   const unsigned char *buf, int len) { }
#endif

/**
 * lowpan_fetch_skb - getting inline data from 6LoWPAN header
 *
 * This function will pull data from sk buffer and put it into data to
 * remove the 6LoWPAN inline data. This function returns true if the
 * sk buffer is too small to pull the amount of data which is specified
 * by len.
 *
 * @skb: the buffer where the inline data should be pulled from.
 * @data: destination buffer for the inline data.
 * @len: amount of data which should be pulled in bytes.
 */
static inline bool lowpan_fetch_skb(struct sk_buff *skb, void *data,
				    unsigned int len)
{
	if (unlikely(!pskb_may_pull(skb, len)))
		return true;

	skb_copy_from_linear_data(skb, data, len);
	skb_pull(skb, len);

	return false;
}

static inline bool lowpan_802154_is_valid_src_short_addr(__le16 addr)
{
	/* First bit of addr is multicast, reserved or 802.15.4 specific */
	return !(addr & cpu_to_le16(0x8000));
}

static inline void lowpan_push_hc_data(u8 **hc_ptr, const void *data,
				       const size_t len)
{
	memcpy(*hc_ptr, data, len);
	*hc_ptr += len;
}

int lowpan_register_netdevice(struct net_device *dev,
			      enum lowpan_lltypes lltype);
int lowpan_register_netdev(struct net_device *dev,
			   enum lowpan_lltypes lltype);
void lowpan_unregister_netdevice(struct net_device *dev);
void lowpan_unregister_netdev(struct net_device *dev);

/**
 * lowpan_header_decompress - replace 6LoWPAN header with IPv6 header
 *
 * This function replaces the IPHC 6LoWPAN header which should be pointed at
 * skb->data and skb_network_header, with the IPv6 header.
 * It would be nice that the caller have the necessary headroom of IPv6 header
 * and greatest Transport layer header, this would reduce the overhead for
 * reallocate headroom.
 *
 * @skb: the buffer which should be manipulate.
 * @dev: the lowpan net device pointer.
 * @daddr: destination lladdr of mac header which is used for compression
 *	methods.
 * @saddr: source lladdr of mac header which is used for compression
 *	methods.
 */
int lowpan_header_decompress(struct sk_buff *skb, const struct net_device *dev,
			     const void *daddr, const void *saddr);

/**
 * lowpan_header_compress - replace IPv6 header with 6LoWPAN header
 *
 * This function replaces the IPv6 header which should be pointed at
 * skb->data and skb_network_header, with the IPHC 6LoWPAN header.
 * The caller need to be sure that the sk buffer is not shared and at have
 * at least a headroom which is smaller or equal LOWPAN_IPHC_MAX_HEADER_LEN,
 * which is the IPHC "more bytes than IPv6 header" at worst case.
 *
 * @skb: the buffer which should be manipulate.
 * @dev: the lowpan net device pointer.
 * @daddr: destination lladdr of mac header which is used for compression
 *	methods.
 * @saddr: source lladdr of mac header which is used for compression
 *	methods.
 */
int lowpan_header_compress(struct sk_buff *skb, const struct net_device *dev,
			   const void *daddr, const void *saddr);

#endif /* __6LOWPAN_H__ */
