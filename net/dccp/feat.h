#ifndef _DCCP_FEAT_H
#define _DCCP_FEAT_H
/*
 *  net/dccp/feat.h
 *
 *  An implementation of the DCCP protocol
 *  Copyright (c) 2005 Andrea Bittau <a.bittau@cs.ucl.ac.uk>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License version 2 as
 *	published by the Free Software Foundation.
 */

#include <linux/types.h>
#include "dccp.h"

#ifdef CONFIG_IP_DCCP_DEBUG
extern const char *dccp_feat_typename(const u8 type);
extern const char *dccp_feat_name(const u8 feat);

static inline void dccp_feat_debug(const u8 type, const u8 feat, const u8 val)
{
	dccp_pr_debug("%s(%s (%d), %d)\n", dccp_feat_typename(type),
					   dccp_feat_name(feat), feat, val);
}
#else
#define dccp_feat_debug(type, feat, val)
#endif /* CONFIG_IP_DCCP_DEBUG */

extern int  dccp_feat_change(struct dccp_minisock *dmsk, u8 type, u8 feature,
			     u8 *val, u8 len, gfp_t gfp);
extern int  dccp_feat_change_recv(struct sock *sk, u8 type, u8 feature,
				  u8 *val, u8 len);
extern int  dccp_feat_confirm_recv(struct sock *sk, u8 type, u8 feature,
				   u8 *val, u8 len);
extern void dccp_feat_clean(struct dccp_minisock *dmsk);
extern int  dccp_feat_clone(struct sock *oldsk, struct sock *newsk);
extern int  dccp_feat_init(struct dccp_minisock *dmsk);

#endif /* _DCCP_FEAT_H */
