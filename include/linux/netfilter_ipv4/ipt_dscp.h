/* iptables module for matching the IPv4 DSCP field
 *
 * (C) 2002 Harald Welte <laforge@gnumonks.org>
 * This software is distributed under GNU GPL v2, 1991
 * 
 * See RFC2474 for a description of the DSCP field within the IP Header.
 *
 * ipt_dscp.h,v 1.3 2002/08/05 19:00:21 laforge Exp
*/
#ifndef _IPT_DSCP_H
#define _IPT_DSCP_H

#define IPT_DSCP_MASK	0xfc	/* 11111100 */
#define IPT_DSCP_SHIFT	2
#define IPT_DSCP_MAX	0x3f	/* 00111111 */

/* match info */
struct ipt_dscp_info {
	u_int8_t dscp;
	u_int8_t invert;
};

#endif /* _IPT_DSCP_H */
