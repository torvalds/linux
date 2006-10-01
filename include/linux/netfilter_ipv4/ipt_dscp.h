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

#include <linux/netfilter/xt_dscp.h>

#define IPT_DSCP_MASK	XT_DSCP_MASK
#define IPT_DSCP_SHIFT	XT_DSCP_SHIFT
#define IPT_DSCP_MAX	XT_DSCP_MAX

#define ipt_dscp_info	xt_dscp_info

#endif /* _IPT_DSCP_H */
