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

struct sock;
struct dccp_minisock;

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
