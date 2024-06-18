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

extern struct proto 		udplite_prot;
extern struct udp_table		udplite_table;

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
static inline int udplite_checksum_init(struct sk_buff *skb, struct udphdr *uh)
{
	u16 cscov;

        /* In UDPv4 a zero checksum means that the transmitter generated no
         * checksum. UDP-Lite (like IPv6) mandates checksums, hence packets
         * with a zero checksum field are illegal.                            */
	if (uh->check == 0) {
		net_dbg_ratelimited("UDPLite: zeroed checksum field\n");
		return 1;
	}

	cscov = ntohs(uh->len);

	if (cscov == 0)		 /* Indicates that full coverage is required. */
		;
	else if (cscov < 8  || cscov > skb->len) {
		/*
		 * Coverage length violates RFC 3828: log and discard silently.
		 */
		net_dbg_ratelimited("UDPLite: bad csum coverage %d/%d\n",
				    cscov, skb->len);
		return 1;

	} else if (cscov < skb->len) {
        	UDP_SKB_CB(skb)->partial_cov = 1;
		UDP_SKB_CB(skb)->cscov = cscov;
		if (skb->ip_summed == CHECKSUM_COMPLETE)
			skb->ip_summed = CHECKSUM_NONE;
		skb->csum_valid = 0;
        }

	return 0;
}

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

void udplite4_register(void);
#endif	/* _UDPLITE_H */
