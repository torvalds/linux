/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_REJECT_H
#define _NF_REJECT_H

#include <linux/types.h>
#include <uapi/linux/in.h>

static inline bool nf_reject_verify_csum(__u8 proto)
{
	/* Skip protocols that don't use 16-bit one's complement checksum
	 * of the entire payload.
	 */
	switch (proto) {
		/* Protocols with other integrity checks. */
		case IPPROTO_AH:
		case IPPROTO_ESP:
		case IPPROTO_SCTP:

		/* Protocols with partial checksums. */
		case IPPROTO_UDPLITE:
		case IPPROTO_DCCP:

		/* Protocols with optional checksums. */
		case IPPROTO_GRE:
			return false;
	}
	return true;
}

#endif /* _NF_REJECT_H */
