/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Management Component Transport Protocol (MCTP)
 *
 * Copyright (c) 2021 Code Construct
 * Copyright (c) 2021 Google
 */

#ifndef __NET_MCTP_H
#define __NET_MCTP_H

#include <linux/bits.h>
#include <linux/mctp.h>

/* MCTP packet definitions */
struct mctp_hdr {
	u8	ver;
	u8	dest;
	u8	src;
	u8	flags_seq_tag;
};

#define MCTP_VER_MIN	1
#define MCTP_VER_MAX	1

/* Definitions for flags_seq_tag field */
#define MCTP_HDR_FLAG_SOM	BIT(7)
#define MCTP_HDR_FLAG_EOM	BIT(6)
#define MCTP_HDR_FLAG_TO	BIT(3)
#define MCTP_HDR_FLAGS		GENMASK(5, 3)
#define MCTP_HDR_SEQ_SHIFT	4
#define MCTP_HDR_SEQ_MASK	GENMASK(1, 0)
#define MCTP_HDR_TAG_SHIFT	0
#define MCTP_HDR_TAG_MASK	GENMASK(2, 0)

static inline bool mctp_address_ok(mctp_eid_t eid)
{
	return eid >= 8 && eid < 255;
}

static inline struct mctp_hdr *mctp_hdr(struct sk_buff *skb)
{
	return (struct mctp_hdr *)skb_network_header(skb);
}

void mctp_device_init(void);
void mctp_device_exit(void);

#endif /* __NET_MCTP_H */
