/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2003 International Business Machines, Corp.
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * SCTP Checksum functions
 *
 * The SCTP reference implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The SCTP reference implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, see
 * <http://www.gnu.org/licenses/>.
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
#include <net/sctp/sctp.h>
#include <linux/crc32c.h>
#include <linux/crc32.h>

static inline __wsum sctp_csum_update(const void *buff, int len, __wsum sum)
{
	/* This uses the crypto implementation of crc32c, which is either
	 * implemented w/ hardware support or resolves to __crc32c_le().
	 */
	return crc32c(sum, buff, len);
}

static inline __wsum sctp_csum_combine(__wsum csum, __wsum csum2,
				       int offset, int len)
{
	return __crc32c_le_combine(csum, csum2, len);
}

static inline __le32 sctp_compute_cksum(const struct sk_buff *skb,
					unsigned int offset)
{
	struct sctphdr *sh = sctp_hdr(skb);
        __le32 ret, old = sh->checksum;
	const struct skb_checksum_ops ops = {
		.update  = sctp_csum_update,
		.combine = sctp_csum_combine,
	};

	sh->checksum = 0;
	ret = cpu_to_le32(~__skb_checksum(skb, offset, skb->len - offset,
					  ~(__u32)0, &ops));
	sh->checksum = old;

	return ret;
}

#endif /* __sctp_checksum_h__ */
