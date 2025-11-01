/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_REJECT_H
#define _NF_REJECT_H

#include <linux/types.h>
#include <uapi/linux/in.h>

static inline bool nf_reject_verify_csum(struct sk_buff *skb, int dataoff,
					  __u8 proto)
{
	/* Skip protocols that don't use 16-bit one's complement checksum
	 * of the entire payload.
	 */
	switch (proto) {
		/* Protocols with optional checksums. */
		case IPPROTO_UDP: {
			const struct udphdr *udp_hdr;
			struct udphdr _udp_hdr;

			udp_hdr = skb_header_pointer(skb, dataoff,
						     sizeof(_udp_hdr),
						     &_udp_hdr);
			if (!udp_hdr || udp_hdr->check)
				return true;

			return false;
		}
		case IPPROTO_GRE:

		/* Protocols with other integrity checks. */
		case IPPROTO_AH:
		case IPPROTO_ESP:
		case IPPROTO_SCTP:

		/* Protocols with partial checksums. */
		case IPPROTO_UDPLITE:
			return false;
	}
	return true;
}

#endif /* _NF_REJECT_H */
