/* iptables module for setting the IPv4 DSCP field
 *
 * (C) 2002 Harald Welte <laforge@gnumonks.org>
 * based on ipt_FTOS.c (C) 2000 by Matthew G. Marsh <mgm@paktronix.com>
 * This software is distributed under GNU GPL v2, 1991
 * 
 * See RFC2474 for a description of the DSCP field within the IP Header.
 *
 * ipt_DSCP.h,v 1.7 2002/03/14 12:03:13 laforge Exp
*/
#ifndef _IPT_DSCP_TARGET_H
#define _IPT_DSCP_TARGET_H
#include <linux/netfilter_ipv4/ipt_dscp.h>
#include <linux/netfilter/xt_DSCP.h>

#define ipt_DSCP_info xt_DSCP_info

#endif /* _IPT_DSCP_TARGET_H */
