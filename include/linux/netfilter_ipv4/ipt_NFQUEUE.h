/* iptables module for using NFQUEUE mechanism
 *
 * (C) 2005 Harald Welte <laforge@netfilter.org>
 *
 * This software is distributed under GNU GPL v2, 1991
 * 
*/
#ifndef _IPT_NFQ_TARGET_H
#define _IPT_NFQ_TARGET_H

/* Backwards compatibility for old userspace */
#include <linux/netfilter/xt_NFQUEUE.h>

#define ipt_NFQ_info xt_NFQ_info

#endif /* _IPT_DSCP_TARGET_H */
