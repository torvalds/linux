/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_MPLS_H
#define _UAPI_MPLS_H

#include <linux/types.h>
#include <asm/byteorder.h>

/* Reference: RFC 5462, RFC 3032
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                Label                  | TC  |S|       TTL     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *	Label:  Label Value, 20 bits
 *	TC:     Traffic Class field, 3 bits
 *	S:      Bottom of Stack, 1 bit
 *	TTL:    Time to Live, 8 bits
 */

struct mpls_label {
	__be32 entry;
};

#define MPLS_LS_LABEL_MASK      0xFFFFF000
#define MPLS_LS_LABEL_SHIFT     12
#define MPLS_LS_TC_MASK         0x00000E00
#define MPLS_LS_TC_SHIFT        9
#define MPLS_LS_S_MASK          0x00000100
#define MPLS_LS_S_SHIFT         8
#define MPLS_LS_TTL_MASK        0x000000FF
#define MPLS_LS_TTL_SHIFT       0

/* Reserved labels */
#define MPLS_LABEL_IPV4NULL		0 /* RFC3032 */
#define MPLS_LABEL_RTALERT		1 /* RFC3032 */
#define MPLS_LABEL_IPV6NULL		2 /* RFC3032 */
#define MPLS_LABEL_IMPLNULL		3 /* RFC3032 */
#define MPLS_LABEL_ENTROPY		7 /* RFC6790 */
#define MPLS_LABEL_GAL			13 /* RFC5586 */
#define MPLS_LABEL_OAMALERT		14 /* RFC3429 */
#define MPLS_LABEL_EXTENSION		15 /* RFC7274 */

#define MPLS_LABEL_FIRST_UNRESERVED	16 /* RFC3032 */

/* These are embedded into IFLA_STATS_AF_SPEC:
 * [IFLA_STATS_AF_SPEC]
 * -> [AF_MPLS]
 *    -> [MPLS_STATS_xxx]
 *
 * Attributes:
 * [MPLS_STATS_LINK] = {
 *     struct mpls_link_stats
 * }
 */
enum {
	MPLS_STATS_UNSPEC, /* also used as 64bit pad attribute */
	MPLS_STATS_LINK,
	__MPLS_STATS_MAX,
};

#define MPLS_STATS_MAX (__MPLS_STATS_MAX - 1)

struct mpls_link_stats {
	__u64	rx_packets;		/* total packets received	*/
	__u64	tx_packets;		/* total packets transmitted	*/
	__u64	rx_bytes;		/* total bytes received		*/
	__u64	tx_bytes;		/* total bytes transmitted	*/
	__u64	rx_errors;		/* bad packets received		*/
	__u64	tx_errors;		/* packet transmit problems	*/
	__u64	rx_dropped;		/* packet dropped on receive	*/
	__u64	tx_dropped;		/* packet dropped on transmit	*/
	__u64	rx_noroute;		/* no route for packet dest	*/
};

#endif /* _UAPI_MPLS_H */
