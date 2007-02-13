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

static inline int dccp_feat_is_valid_length(u8 type, u8 feature, u8 len)
{
	/* sec. 6.1: Confirm has at least length 3,
	 * sec. 6.2: Change  has at least length 4 */
	if (len < 3)
		return 1;
	if (len < 4  && (type == DCCPO_CHANGE_L || type == DCCPO_CHANGE_R))
		return 1;
	/* XXX: add per-feature length validation (sec. 6.6.8) */
	return 0;
}

static inline int dccp_feat_is_reserved(const u8 feat)
{
	return (feat > DCCPF_DATA_CHECKSUM &&
		feat < DCCPF_MIN_CCID_SPECIFIC) ||
		feat == DCCPF_RESERVED;
}

/* feature negotiation knows only these four option types (RFC 4340, sec. 6) */
static inline int dccp_feat_is_valid_type(const u8 optnum)
{
	return optnum >= DCCPO_CHANGE_L && optnum <= DCCPO_CONFIRM_R;

}

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
