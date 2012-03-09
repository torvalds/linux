/* iptables module for matching the ECN header in IPv4 and TCP header
 *
 * (C) 2002 Harald Welte <laforge@gnumonks.org>
 *
 * This software is distributed under GNU GPL v2, 1991
 * 
 * ipt_ecn.h,v 1.4 2002/08/05 19:39:00 laforge Exp
*/
#ifndef _XT_ECN_H
#define _XT_ECN_H

#include <linux/types.h>
#include <linux/netfilter/xt_dscp.h>

#define XT_ECN_IP_MASK	(~XT_DSCP_MASK)

#define XT_ECN_OP_MATCH_IP	0x01
#define XT_ECN_OP_MATCH_ECE	0x10
#define XT_ECN_OP_MATCH_CWR	0x20

#define XT_ECN_OP_MATCH_MASK	0xce

/* match info */
struct xt_ecn_info {
	__u8 operation;
	__u8 invert;
	__u8 ip_ect;
	union {
		struct {
			__u8 ect;
		} tcp;
	} proto;
};

#endif /* _XT_ECN_H */
