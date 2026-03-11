/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	Definitions for the UDP-Lite (RFC 3828) code.
 */
#ifndef _UDPLITE_H
#define _UDPLITE_H

#include <net/ip6_checksum.h>
#include <net/udp.h>

/* UDP-Lite socket options */
#define UDPLITE_SEND_CSCOV   10 /* sender partial coverage (as sent)      */
#define UDPLITE_RECV_CSCOV   11 /* receiver partial coverage (threshold ) */

/*
 *	Checksum computation is all in software, hence simpler getfrag.
 */
static __inline__ int udplite_getfrag(void *from, char *to, int  offset,
				      int len, int odd, struct sk_buff *skb)
{
	struct msghdr *msg = from;
	return copy_from_iter_full(to, len, &msg->msg_iter) ? 0 : -EFAULT;
}

/*
 * 	Checksumming routines
 */

/* Fast-path computation of checksum. Socket may not be locked. */
static inline __wsum udplite_csum(struct sk_buff *skb)
{
	const int off = skb_transport_offset(skb);
	const struct sock *sk = skb->sk;
	int len = skb->len - off;

	if (udp_test_bit(UDPLITE_SEND_CC, sk)) {
		u16 pcslen = READ_ONCE(udp_sk(sk)->pcslen);

		if (pcslen < len) {
			if (pcslen > 0)
				len = pcslen;
			udp_hdr(skb)->len = htons(pcslen);
		}
	}
	skb->ip_summed = CHECKSUM_NONE;     /* no HW support for checksumming */

	return skb_checksum(skb, off, len, 0);
}

#endif	/* _UDPLITE_H */
