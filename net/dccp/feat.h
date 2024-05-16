/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _DCCP_FEAT_H
#define _DCCP_FEAT_H
/*
 *  net/dccp/feat.h
 *
 *  Feature negotiation for the DCCP protocol (RFC 4340, section 6)
 *  Copyright (c) 2008 Gerrit Renker <gerrit@erg.abdn.ac.uk>
 *  Copyright (c) 2005 Andrea Bittau <a.bittau@cs.ucl.ac.uk>
 */
#include <linux/types.h>
#include "dccp.h"

/*
 * Known limit values
 */
/* Ack Ratio takes 2-byte integer values (11.3) */
#define DCCPF_ACK_RATIO_MAX	0xFFFF
/* Wmin=32 and Wmax=2^46-1 from 7.5.2 */
#define DCCPF_SEQ_WMIN		32
#define DCCPF_SEQ_WMAX		0x3FFFFFFFFFFFull
/* Maximum number of SP values that fit in a single (Confirm) option */
#define DCCP_FEAT_MAX_SP_VALS	(DCCP_SINGLE_OPT_MAXLEN - 2)

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
 * @val: feature's current value (SP features may have preference list)
 * @state: feature's current state
 * @feat_num: one of %dccp_feature_numbers
 * @needs_mandatory: whether Mandatory options should be sent
 * @needs_confirm: whether to send a Confirm instead of a Change
 * @empty_confirm: whether to send an empty Confirm (depends on @needs_confirm)
 * @is_local: feature location (1) or feature-remote (0)
 * @node: list pointers, entries arranged in FIFO order
 */
struct dccp_feat_entry {
	dccp_feat_val           val;
	enum dccp_feat_state    state:8;
	u8                      feat_num;

	bool			needs_mandatory,
				needs_confirm,
				empty_confirm,
				is_local;

	struct list_head	node;
};

static inline u8 dccp_feat_genopt(struct dccp_feat_entry *entry)
{
	if (entry->needs_confirm)
		return entry->is_local ? DCCPO_CONFIRM_L : DCCPO_CONFIRM_R;
	return entry->is_local ? DCCPO_CHANGE_L : DCCPO_CHANGE_R;
}

/**
 * struct ccid_dependency  -  Track changes resulting from choosing a CCID
 * @dependent_feat: one of %dccp_feature_numbers
 * @is_local: local (1) or remote (0) @dependent_feat
 * @is_mandatory: whether presence of @dependent_feat is mission-critical or not
 * @val: corresponding default value for @dependent_feat (u8 is sufficient here)
 */
struct ccid_dependency {
	u8	dependent_feat;
	bool	is_local:1,
		is_mandatory:1;
	u8	val;
};

/*
 * Sysctls to seed defaults for feature negotiation
 */
extern unsigned long sysctl_dccp_sequence_window;
extern int	     sysctl_dccp_rx_ccid;
extern int	     sysctl_dccp_tx_ccid;

int dccp_feat_init(struct sock *sk);
int dccp_feat_register_sp(struct sock *sk, u8 feat, u8 is_local,
			  u8 const *list, u8 len);
int dccp_feat_parse_options(struct sock *, struct dccp_request_sock *,
			    u8 mand, u8 opt, u8 feat, u8 *val, u8 len);
int dccp_feat_clone_list(struct list_head const *, struct list_head *);

/*
 * Encoding variable-length options and their maximum length.
 *
 * This affects NN options (SP options are all u8) and other variable-length
 * options (see table 3 in RFC 4340). The limit is currently given the Sequence
 * Window NN value (sec. 7.5.2) and the NDP count (sec. 7.7) option, all other
 * options consume less than 6 bytes (timestamps are 4 bytes).
 * When updating this constant (e.g. due to new internet drafts / RFCs), make
 * sure that you also update all code which refers to it.
 */
#define DCCP_OPTVAL_MAXLEN	6

void dccp_encode_value_var(const u64 value, u8 *to, const u8 len);
u64 dccp_decode_value_var(const u8 *bf, const u8 len);
u64 dccp_feat_nn_get(struct sock *sk, u8 feat);

int dccp_insert_option_mandatory(struct sk_buff *skb);
int dccp_insert_fn_opt(struct sk_buff *skb, u8 type, u8 feat, u8 *val, u8 len,
		       bool repeat_first);
#endif /* _DCCP_FEAT_H */
