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
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
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

static inline __u32 sctp_crc32c(__u32 crc, u8 *buffer, u16 length)
{
	return crc32c(crc, buffer, length);
}

static inline __u32 sctp_start_cksum(__u8 *buffer, __u16 length)
{
	__u32 crc = ~(__u32)0;
	__u8  zero[sizeof(__u32)] = {0};

	/* Optimize this routine to be SCTP specific, knowing how
	 * to skip the checksum field of the SCTP header.
	 */

	/* Calculate CRC up to the checksum. */
	crc = sctp_crc32c(crc, buffer, sizeof(struct sctphdr) - sizeof(__u32));

	/* Skip checksum field of the header. */
	crc = sctp_crc32c(crc, zero, sizeof(__u32));

	/* Calculate the rest of the CRC. */
	crc = sctp_crc32c(crc, &buffer[sizeof(struct sctphdr)],
			    length - sizeof(struct sctphdr));
	return crc;
}

static inline __u32 sctp_update_cksum(__u8 *buffer, __u16 length, __u32 crc32)
{
	return sctp_crc32c(crc32, buffer, length);
}

static inline __le32 sctp_end_cksum(__u32 crc32)
{
	return cpu_to_le32(~crc32);
}

/* Calculate the CRC32C checksum of an SCTP packet.  */
static inline __le32 sctp_compute_cksum(const struct sk_buff *skb,
					unsigned int offset)
{
	const struct sk_buff *iter;

	__u32 crc32 = sctp_start_cksum(skb->data + offset,
				       skb_headlen(skb) - offset);
	skb_walk_frags(skb, iter)
		crc32 = sctp_update_cksum((__u8 *) iter->data,
					  skb_headlen(iter), crc32);

	return sctp_end_cksum(crc32);
}

#endif /* __sctp_checksum_h__ */
