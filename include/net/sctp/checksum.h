/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2003 International Business Machines, Corp.
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * SCTP Checksum functions
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Written or modified by:
 *    Dinakaran Joseph
 *    Jon Grimm <jgrimm@us.ibm.com>
 *    Sridhar Samudrala <sri@us.ibm.com>
 *
 * Rewritten to use libcrc32c by:
 *    Vlad Yasevich <vladislav.yasevich@hp.com>
 */

#ifndef __sctp_checksum_h__
#define __sctp_checksum_h__

#include <linux/types.h>
#include <linux/sctp.h>
#include <linux/crc32c.h>
#include <linux/crc32.h>

static inline __wsum sctp_csum_update(const void *buff, int len, __wsum sum)
{
	/* This uses the crypto implementation of crc32c, which is either
	 * implemented w/ hardware support or resolves to __crc32c_le().
	 */
	return (__force __wsum)crc32c((__force __u32)sum, buff, len);
}

static inline __wsum sctp_csum_combine(__wsum csum, __wsum csum2,
				       int offset, int len)
{
	return (__force __wsum)__crc32c_le_combine((__force __u32)csum,
						   (__force __u32)csum2, len);
}

static const struct skb_checksum_ops sctp_csum_ops = {
	.update  = sctp_csum_update,
	.combine = sctp_csum_combine,
};

static inline __le32 sctp_compute_cksum(const struct sk_buff *skb,
					unsigned int offset)
{
	struct sctphdr *sh = (struct sctphdr *)(skb->data + offset);
	__le32 old = sh->checksum;
	__wsum new;

	sh->checksum = 0;
	new = ~__skb_checksum(skb, offset, skb->len - offset, ~(__wsum)0,
			      &sctp_csum_ops);
	sh->checksum = old;

	return cpu_to_le32((__force __u32)new);
}

#endif /* __sctp_checksum_h__ */
