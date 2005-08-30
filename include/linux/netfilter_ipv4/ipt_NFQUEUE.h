/* iptables module for using NFQUEUE mechanism
 *
 * (C) 2005 Harald Welte <laforge@netfilter.org>
 *
 * This software is distributed under GNU GPL v2, 1991
 * 
*/
#ifndef _IPT_NFQ_TARGET_H
#define _IPT_NFQ_TARGET_H

/* target info */
struct ipt_NFQ_info {
	u_int16_t queuenum;
};

#endif /* _IPT_DSCP_TARGET_H */
