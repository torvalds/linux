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
 *    Vlad Yasevich <vladislav.yasevich@hp.com>
 */

#ifndef __sctp_checksum_h__
#define __sctp_checksum_h__

#include <linux/types.h>
#include <linux/sctp.h>

static inline __le32 sctp_compute_cksum(const struct sk_buff *skb,
					unsigned int offset)
{
	struct sctphdr *sh = (struct sctphdr *)(skb->data + offset);
	__le32 old = sh->checksum;
	u32 new;

	sh->checksum = 0;
	new = ~skb_crc32c(skb, offset, skb->len - offset, ~0);
	sh->checksum = old;
	return cpu_to_le32(new);
}

#endif /* __sctp_checksum_h__ */
