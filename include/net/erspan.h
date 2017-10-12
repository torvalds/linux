#ifndef __LINUX_ERSPAN_H
#define __LINUX_ERSPAN_H

/*
 * GRE header for ERSPAN encapsulation (8 octets [34:41]) -- 8 bytes
 *       0                   1                   2                   3
 *      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |0|0|0|1|0|00000|000000000|00000|    Protocol Type for ERSPAN   |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |      Sequence Number (increments per packet per session)      |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *  Note that in the above GRE header [RFC1701] out of the C, R, K, S,
 *  s, Recur, Flags, Version fields only S (bit 03) is set to 1. The
 *  other fields are set to zero, so only a sequence number follows.
 *
 *  ERSPAN Type II header (8 octets [42:49])
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Ver  |          VLAN         | COS | En|T|    Session ID     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |      Reserved         |                  Index                |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * GRE proto ERSPAN type II = 0x88BE, type III = 0x22EB
 */

#define ERSPAN_VERSION	0x1

#define VER_MASK	0xf000
#define VLAN_MASK	0x0fff
#define COS_MASK	0xe000
#define EN_MASK		0x1800
#define T_MASK		0x0400
#define ID_MASK		0x03ff
#define INDEX_MASK	0xfffff

enum erspan_encap_type {
	ERSPAN_ENCAP_NOVLAN = 0x0,	/* originally without VLAN tag */
	ERSPAN_ENCAP_ISL = 0x1,		/* originally ISL encapsulated */
	ERSPAN_ENCAP_8021Q = 0x2,	/* originally 802.1Q encapsulated */
	ERSPAN_ENCAP_INFRAME = 0x3,	/* VLAN tag perserved in frame */
};

struct erspan_metadata {
	__be32 index;   /* type II */
};

struct erspanhdr {
	__be16 ver_vlan;
#define VER_OFFSET  12
	__be16 session_id;
#define COS_OFFSET  13
#define EN_OFFSET   11
#define T_OFFSET    10
	struct erspan_metadata md;
};

#endif
