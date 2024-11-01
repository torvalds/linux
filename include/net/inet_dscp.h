/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * inet_dscp.h: helpers for handling differentiated services codepoints (DSCP)
 *
 * DSCP is defined in RFC 2474:
 *
 *        0   1   2   3   4   5   6   7
 *      +---+---+---+---+---+---+---+---+
 *      |         DSCP          |  CU   |
 *      +---+---+---+---+---+---+---+---+
 *
 *        DSCP: differentiated services codepoint
 *        CU:   currently unused
 *
 * The whole DSCP + CU bits form the DS field.
 * The DS field is also commonly called TOS or Traffic Class (for IPv6).
 *
 * Note: the CU bits are now used for Explicit Congestion Notification
 *       (RFC 3168).
 */

#ifndef _INET_DSCP_H
#define _INET_DSCP_H

#include <linux/types.h>

/* Special type for storing DSCP values.
 *
 * A dscp_t variable stores a DS field with the CU (ECN) bits cleared.
 * Using dscp_t allows to strictly separate DSCP and ECN bits, thus avoiding
 * bugs where ECN bits are erroneously taken into account during FIB lookups
 * or policy routing.
 *
 * Note: to get the real DSCP value contained in a dscp_t variable one would
 * have to do a bit shift after calling inet_dscp_to_dsfield(). We could have
 * a helper for that, but there's currently no users.
 */
typedef u8 __bitwise dscp_t;

#define INET_DSCP_MASK 0xfc

static inline dscp_t inet_dsfield_to_dscp(__u8 dsfield)
{
	return (__force dscp_t)(dsfield & INET_DSCP_MASK);
}

static inline __u8 inet_dscp_to_dsfield(dscp_t dscp)
{
	return (__force __u8)dscp;
}

static inline bool inet_validate_dscp(__u8 val)
{
	return !(val & ~INET_DSCP_MASK);
}

#endif /* _INET_DSCP_H */
