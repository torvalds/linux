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

enum dccp_feat_type {
	FEAT_AT_RX   = 1,	/* located at RX side of half-connection  */
	FEAT_AT_TX   = 2,	/* located at TX side of half-connection  */
	FEAT_SP      = 4,	/* server-priority reconciliation (6.3.1) */
	FEAT_NN	     = 8,	/* non-negotiable reconciliation (6.3.2)  */
	FEAT_UNKNOWN = 0xFF	/* not understood or invalid feature	  */
};

enum dccp_feat_state {
	FEAT_DEFAULT = 0,	/* using default values from 6.4 */
	FEAT_INITIALISING,	/* feature is being initialised  */
	FEAT_CHANGING,		/* Change sent but not confirmed yet */
	FEAT_UNSTABLE,		/* local modification in state CHANGING */
	FEAT_STABLE		/* both ends (think they) agree */
};

/**
 * dccp_feat_val  -  Container for SP or NN feature values
 * @nn:     single NN value
 * @sp.vec: single SP value plus optional preference list
 * @sp.len: length of @sp.vec in bytes
 */
typedef union {
	u64 nn;
	struct {
		u8	*vec;
		u8	len;
	}   sp;
} dccp_feat_val;

/**
 * struct feat_entry  -  Data structure to perform feature negotiation
 * @feat_num: one of %dccp_feature_numbers
 * @val: feature's current value (SP features may have preference list)
 * @state: feature's current state
 * @needs_mandatory: whether Mandatory options should be sent
 * @needs_confirm: whether to send a Confirm instead of a Change
 * @empty_confirm: whether to send an empty Confirm (depends on @needs_confirm)
 * @is_local: feature location (1) or feature-remote (0)
 * @node: list pointers, entries arranged in FIFO order
 */
struct dccp_feat_entry {
	u8                      feat_num;
	dccp_feat_val           val;
	enum dccp_feat_state    state:8;
	bool			needs_mandatory:1,
				needs_confirm:1,
				empty_confirm:1,
				is_local:1;

	struct list_head	node;
};

static inline u8 dccp_feat_genopt(struct dccp_feat_entry *entry)
{
	if (entry->needs_confirm)
		return entry->is_local ? DCCPO_CONFIRM_L : DCCPO_CONFIRM_R;
	return entry->is_local ? DCCPO_CHANGE_L : DCCPO_CHANGE_R;
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
extern int  dccp_feat_clone_list(struct list_head const *, struct list_head *);
extern int  dccp_feat_init(struct dccp_minisock *dmsk);

#endif /* _DCCP_FEAT_H */
