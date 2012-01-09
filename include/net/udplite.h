/*
 *	Definitions for the UDP-Lite (RFC 3828) code.
 */
#ifndef _UDPLITE_H
#define _UDPLITE_H

#include <net/ip6_checksum.h>

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
	return memcpy_fromiovecend(to, (struct iovec *) from, offset, len);
}

/* Designate sk as UDP-Lite socket */
static inline int udplite_sk_init(struct sock *sk)
{
	udp_sk(sk)->pcflag = UDPLITE_BIT;
	return 0;
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
		LIMIT_NETDEBUG(KERN_DEBUG "UDPLITE: zeroed checksum field\n");
		return 1;
	}

	cscov = ntohs(uh->len);

	if (cscov == 0)		 /* Indicates that full coverage is required. */
		;
	else if (cscov < 8  || cscov > skb->len) {
		/*
		 * Coverage length violates RFC 3828: log and discard silently.
		 */
		LIMIT_NETDEBUG(KERN_DEBUG "UDPLITE: bad csum coverage %d/%d\n",
			       cscov, skb->len);
		return 1;

	} else if (cscov < skb->len) {
        	UDP_SKB_CB(skb)->partial_cov = 1;
		UDP_SKB_CB(skb)->cscov = cscov;
		if (skb->ip_summed == CHECKSUM_COMPLETE)
			skb->ip_summed = CHECKSUM_NONE;
        }

	return 0;
}

/* Slow-path computation of checksum. Socket is locked. */
static inline __wsum udplite_csum_outgoing(struct sock *sk, struct sk_buff *skb)
{
	const struct udp_sock *up = udp_sk(skb->sk);
	int cscov = up->len;
	__wsum csum = 0;

	if (up->pcflag & UDPLITE_SEND_CC) {
		/*
		 * Sender has set `partial coverage' option on UDP-Lite socket.
		 * The special case "up->pcslen == 0" signifies full coverage.
		 */
		if (up->pcslen < up->len) {
			if (0 < up->pcslen)
				cscov = up->pcslen;
			udp_hdr(skb)->len = htons(up->pcslen);
		}
		/*
		 * NOTE: Causes for the error case  `up->pcslen > up->len':
		 *        (i)  Application error (will not be penalized).
		 *       (ii)  Payload too big for send buffer: data is split
		 *             into several packets, each with its own header.
		 *             In this case (e.g. last segment), coverage may
		 *             exceed packet length.
		 *       Since packets with coverage length > packet length are
		 *       illegal, we fall back to the defaults here.
		 */
	}

	skb->ip_summed = CHECKSUM_NONE;     /* no HW support for checksumming */

	skb_queue_walk(&sk->sk_write_queue, skb) {
		const int off = skb_transport_offset(skb);
		const int len = skb->len - off;

		csum = skb_checksum(skb, off, (cscov > len)? len : cscov, csum);

		if ((cscov -= len) <= 0)
			break;
	}
	return csum;
}

/* Fast-path computation of checksum. Socket may not be locked. */
static inline __wsum udplite_csum(struct sk_buff *skb)
{
	const struct udp_sock *up = udp_sk(skb->sk);
	const int off = skb_transport_offset(skb);
	int len = skb->len - off;

	if ((up->pcflag & UDPLITE_SEND_CC) && up->pcslen < len) {
		if (0 < up->pcslen)
			len = up->pcslen;
		udp_hdr(skb)->len = htons(up->pcslen);
	}
	skb->ip_summed = CHECKSUM_NONE;     /* no HW support for checksumming */

	return skb_checksum(skb, off, len, 0);
}

extern void	udplite4_register(void);
extern int 	udplite_get_port(struct sock *sk, unsigned short snum,
			int (*scmp)(const struct sock *, const struct sock *));
#endif	/* _UDPLITE_H */
