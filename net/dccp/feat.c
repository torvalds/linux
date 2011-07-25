/*
 *  net/dccp/feat.c
 *
 *  Feature negotiation for the DCCP protocol (RFC 4340, section 6)
 *
 *  Copyright (c) 2008 Gerrit Renker <gerrit@erg.abdn.ac.uk>
 *  Rewrote from scratch, some bits from earlier code by
 *  Copyright (c) 2005 Andrea Bittau <a.bittau@cs.ucl.ac.uk>
 *
 *
 *  ASSUMPTIONS
 *  -----------
 *  o Feature negotiation is coordinated with connection setup (as in TCP), wild
 *    changes of parameters of an established connection are not supported.
 *  o Changing non-negotiable (NN) values is supported in state OPEN/PARTOPEN.
 *  o All currently known SP features have 1-byte quantities. If in the future
 *    extensions of RFCs 4340..42 define features with item lengths larger than
 *    one byte, a feature-specific extension of the code will be required.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/slab.h>
#include "ccid.h"
#include "feat.h"

/* feature-specific sysctls - initialised to the defaults from RFC 4340, 6.4 */
unsigned long	sysctl_dccp_sequence_window __read_mostly = 100;
int		sysctl_dccp_rx_ccid	    __read_mostly = 2,
		sysctl_dccp_tx_ccid	    __read_mostly = 2;

/*
 * Feature activation handlers.
 *
 * These all use an u64 argument, to provide enough room for NN/SP features. At
 * this stage the negotiated values have been checked to be within their range.
 */
static int dccp_hdlr_ccid(struct sock *sk, u64 ccid, bool rx)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid *new_ccid = ccid_new(ccid, sk, rx);

	if (new_ccid == NULL)
		return -ENOMEM;

	if (rx) {
		ccid_hc_rx_delete(dp->dccps_hc_rx_ccid, sk);
		dp->dccps_hc_rx_ccid = new_ccid;
	} else {
		ccid_hc_tx_delete(dp->dccps_hc_tx_ccid, sk);
		dp->dccps_hc_tx_ccid = new_ccid;
	}
	return 0;
}

static int dccp_hdlr_seq_win(struct sock *sk, u64 seq_win, bool rx)
{
	struct dccp_sock *dp = dccp_sk(sk);

	if (rx) {
		dp->dccps_r_seq_win = seq_win;
		/* propagate changes to update SWL/SWH */
		dccp_update_gsr(sk, dp->dccps_gsr);
	} else {
		dp->dccps_l_seq_win = seq_win;
		/* propagate changes to update AWL */
		dccp_update_gss(sk, dp->dccps_gss);
	}
	return 0;
}

static int dccp_hdlr_ack_ratio(struct sock *sk, u64 ratio, bool rx)
{
	if (rx)
		dccp_sk(sk)->dccps_r_ack_ratio = ratio;
	else
		dccp_sk(sk)->dccps_l_ack_ratio = ratio;
	return 0;
}

static int dccp_hdlr_ackvec(struct sock *sk, u64 enable, bool rx)
{
	struct dccp_sock *dp = dccp_sk(sk);

	if (rx) {
		if (enable && dp->dccps_hc_rx_ackvec == NULL) {
			dp->dccps_hc_rx_ackvec = dccp_ackvec_alloc(gfp_any());
			if (dp->dccps_hc_rx_ackvec == NULL)
				return -ENOMEM;
		} else if (!enable) {
			dccp_ackvec_free(dp->dccps_hc_rx_ackvec);
			dp->dccps_hc_rx_ackvec = NULL;
		}
	}
	return 0;
}

static int dccp_hdlr_ndp(struct sock *sk, u64 enable, bool rx)
{
	if (!rx)
		dccp_sk(sk)->dccps_send_ndp_count = (enable > 0);
	return 0;
}

/*
 * Minimum Checksum Coverage is located at the RX side (9.2.1). This means that
 * `rx' holds when the sending peer informs about his partial coverage via a
 * ChangeR() option. In the other case, we are the sender and the receiver
 * announces its coverage via ChangeL() options. The policy here is to honour
 * such communication by enabling the corresponding partial coverage - but only
 * if it has not been set manually before; the warning here means that all
 * packets will be dropped.
 */
static int dccp_hdlr_min_cscov(struct sock *sk, u64 cscov, bool rx)
{
	struct dccp_sock *dp = dccp_sk(sk);

	if (rx)
		dp->dccps_pcrlen = cscov;
	else {
		if (dp->dccps_pcslen == 0)
			dp->dccps_pcslen = cscov;
		else if (cscov > dp->dccps_pcslen)
			DCCP_WARN("CsCov %u too small, peer requires >= %u\n",
				  dp->dccps_pcslen, (u8)cscov);
	}
	return 0;
}

static const struct {
	u8			feat_num;		/* DCCPF_xxx */
	enum dccp_feat_type	rxtx;			/* RX or TX  */
	enum dccp_feat_type	reconciliation;		/* SP or NN  */
	u8			default_value;		/* as in 6.4 */
	int (*activation_hdlr)(struct sock *sk, u64 val, bool rx);
/*
 *    Lookup table for location and type of features (from RFC 4340/4342)
 *  +--------------------------+----+-----+----+----+---------+-----------+
 *  | Feature                  | Location | Reconc. | Initial |  Section  |
 *  |                          | RX | TX  | SP | NN |  Value  | Reference |
 *  +--------------------------+----+-----+----+----+---------+-----------+
 *  | DCCPF_CCID               |    |  X  | X  |    |   2     | 10        |
 *  | DCCPF_SHORT_SEQNOS       |    |  X  | X  |    |   0     |  7.6.1    |
 *  | DCCPF_SEQUENCE_WINDOW    |    |  X  |    | X  | 100     |  7.5.2    |
 *  | DCCPF_ECN_INCAPABLE      | X  |     | X  |    |   0     | 12.1      |
 *  | DCCPF_ACK_RATIO          |    |  X  |    | X  |   2     | 11.3      |
 *  | DCCPF_SEND_ACK_VECTOR    | X  |     | X  |    |   0     | 11.5      |
 *  | DCCPF_SEND_NDP_COUNT     |    |  X  | X  |    |   0     |  7.7.2    |
 *  | DCCPF_MIN_CSUM_COVER     | X  |     | X  |    |   0     |  9.2.1    |
 *  | DCCPF_DATA_CHECKSUM      | X  |     | X  |    |   0     |  9.3.1    |
 *  | DCCPF_SEND_LEV_RATE      | X  |     | X  |    |   0     | 4342/8.4  |
 *  +--------------------------+----+-----+----+----+---------+-----------+
 */
} dccp_feat_table[] = {
	{ DCCPF_CCID,		 FEAT_AT_TX, FEAT_SP, 2,   dccp_hdlr_ccid     },
	{ DCCPF_SHORT_SEQNOS,	 FEAT_AT_TX, FEAT_SP, 0,   NULL },
	{ DCCPF_SEQUENCE_WINDOW, FEAT_AT_TX, FEAT_NN, 100, dccp_hdlr_seq_win  },
	{ DCCPF_ECN_INCAPABLE,	 FEAT_AT_RX, FEAT_SP, 0,   NULL },
	{ DCCPF_ACK_RATIO,	 FEAT_AT_TX, FEAT_NN, 2,   dccp_hdlr_ack_ratio},
	{ DCCPF_SEND_ACK_VECTOR, FEAT_AT_RX, FEAT_SP, 0,   dccp_hdlr_ackvec   },
	{ DCCPF_SEND_NDP_COUNT,  FEAT_AT_TX, FEAT_SP, 0,   dccp_hdlr_ndp      },
	{ DCCPF_MIN_CSUM_COVER,  FEAT_AT_RX, FEAT_SP, 0,   dccp_hdlr_min_cscov},
	{ DCCPF_DATA_CHECKSUM,	 FEAT_AT_RX, FEAT_SP, 0,   NULL },
	{ DCCPF_SEND_LEV_RATE,	 FEAT_AT_RX, FEAT_SP, 0,   NULL },
};
#define DCCP_FEAT_SUPPORTED_MAX		ARRAY_SIZE(dccp_feat_table)

/**
 * dccp_feat_index  -  Hash function to map feature number into array position
 * Returns consecutive array index or -1 if the feature is not understood.
 */
static int dccp_feat_index(u8 feat_num)
{
	/* The first 9 entries are occupied by the types from RFC 4340, 6.4 */
	if (feat_num > DCCPF_RESERVED && feat_num <= DCCPF_DATA_CHECKSUM)
		return feat_num - 1;

	/*
	 * Other features: add cases for new feature types here after adding
	 * them to the above table.
	 */
	switch (feat_num) {
	case DCCPF_SEND_LEV_RATE:
			return DCCP_FEAT_SUPPORTED_MAX - 1;
	}
	return -1;
}

static u8 dccp_feat_type(u8 feat_num)
{
	int idx = dccp_feat_index(feat_num);

	if (idx < 0)
		return FEAT_UNKNOWN;
	return dccp_feat_table[idx].reconciliation;
}

static int dccp_feat_default_value(u8 feat_num)
{
	int idx = dccp_feat_index(feat_num);
	/*
	 * There are no default values for unknown features, so encountering a
	 * negative index here indicates a serious problem somewhere else.
	 */
	DCCP_BUG_ON(idx < 0);

	return idx < 0 ? 0 : dccp_feat_table[idx].default_value;
}

/*
 *	Debugging and verbose-printing section
 */
static const char *dccp_feat_fname(const u8 feat)
{
	static const char *const feature_names[] = {
		[DCCPF_RESERVED]	= "Reserved",
		[DCCPF_CCID]		= "CCID",
		[DCCPF_SHORT_SEQNOS]	= "Allow Short Seqnos",
		[DCCPF_SEQUENCE_WINDOW]	= "Sequence Window",
		[DCCPF_ECN_INCAPABLE]	= "ECN Incapable",
		[DCCPF_ACK_RATIO]	= "Ack Ratio",
		[DCCPF_SEND_ACK_VECTOR]	= "Send ACK Vector",
		[DCCPF_SEND_NDP_COUNT]	= "Send NDP Count",
		[DCCPF_MIN_CSUM_COVER]	= "Min. Csum Coverage",
		[DCCPF_DATA_CHECKSUM]	= "Send Data Checksum",
	};
	if (feat > DCCPF_DATA_CHECKSUM && feat < DCCPF_MIN_CCID_SPECIFIC)
		return feature_names[DCCPF_RESERVED];

	if (feat ==  DCCPF_SEND_LEV_RATE)
		return "Send Loss Event Rate";
	if (feat >= DCCPF_MIN_CCID_SPECIFIC)
		return "CCID-specific";

	return feature_names[feat];
}

static const char *const dccp_feat_sname[] = {
	"DEFAULT", "INITIALISING", "CHANGING", "UNSTABLE", "STABLE",
};

#ifdef CONFIG_IP_DCCP_DEBUG
static const char *dccp_feat_oname(const u8 opt)
{
	switch (opt) {
	case DCCPO_CHANGE_L:  return "Change_L";
	case DCCPO_CONFIRM_L: return "Confirm_L";
	case DCCPO_CHANGE_R:  return "Change_R";
	case DCCPO_CONFIRM_R: return "Confirm_R";
	}
	return NULL;
}

static void dccp_feat_printval(u8 feat_num, dccp_feat_val const *val)
{
	u8 i, type = dccp_feat_type(feat_num);

	if (val == NULL || (type == FEAT_SP && val->sp.vec == NULL))
		dccp_pr_debug_cat("(NULL)");
	else if (type == FEAT_SP)
		for (i = 0; i < val->sp.len; i++)
			dccp_pr_debug_cat("%s%u", i ? " " : "", val->sp.vec[i]);
	else if (type == FEAT_NN)
		dccp_pr_debug_cat("%llu", (unsigned long long)val->nn);
	else
		dccp_pr_debug_cat("unknown type %u", type);
}

static void dccp_feat_printvals(u8 feat_num, u8 *list, u8 len)
{
	u8 type = dccp_feat_type(feat_num);
	dccp_feat_val fval = { .sp.vec = list, .sp.len = len };

	if (type == FEAT_NN)
		fval.nn = dccp_decode_value_var(list, len);
	dccp_feat_printval(feat_num, &fval);
}

static void dccp_feat_print_entry(struct dccp_feat_entry const *entry)
{
	dccp_debug("   * %s %s = ", entry->is_local ? "local" : "remote",
				    dccp_feat_fname(entry->feat_num));
	dccp_feat_printval(entry->feat_num, &entry->val);
	dccp_pr_debug_cat(", state=%s %s\n", dccp_feat_sname[entry->state],
			  entry->needs_confirm ? "(Confirm pending)" : "");
}

#define dccp_feat_print_opt(opt, feat, val, len, mandatory)	do {	      \
	dccp_pr_debug("%s(%s, ", dccp_feat_oname(opt), dccp_feat_fname(feat));\
	dccp_feat_printvals(feat, val, len);				      \
	dccp_pr_debug_cat(") %s\n", mandatory ? "!" : "");	} while (0)

#define dccp_feat_print_fnlist(fn_list)  {		\
	const struct dccp_feat_entry *___entry;		\
							\
	dccp_pr_debug("List Dump:\n");			\
	list_for_each_entry(___entry, fn_list, node)	\
		dccp_feat_print_entry(___entry);	\
}
#else	/* ! CONFIG_IP_DCCP_DEBUG */
#define dccp_feat_print_opt(opt, feat, val, len, mandatory)
#define dccp_feat_print_fnlist(fn_list)
#endif

static int __dccp_feat_activate(struct sock *sk, const int idx,
				const bool is_local, dccp_feat_val const *fval)
{
	bool rx;
	u64 val;

	if (idx < 0 || idx >= DCCP_FEAT_SUPPORTED_MAX)
		return -1;
	if (dccp_feat_table[idx].activation_hdlr == NULL)
		return 0;

	if (fval == NULL) {
		val = dccp_feat_table[idx].default_value;
	} else if (dccp_feat_table[idx].reconciliation == FEAT_SP) {
		if (fval->sp.vec == NULL) {
			/*
			 * This can happen when an empty Confirm is sent
			 * for an SP (i.e. known) feature. In this case
			 * we would be using the default anyway.
			 */
			DCCP_CRIT("Feature #%d undefined: using default", idx);
			val = dccp_feat_table[idx].default_value;
		} else {
			val = fval->sp.vec[0];
		}
	} else {
		val = fval->nn;
	}

	/* Location is RX if this is a local-RX or remote-TX feature */
	rx = (is_local == (dccp_feat_table[idx].rxtx == FEAT_AT_RX));

	dccp_debug("   -> activating %s %s, %sval=%llu\n", rx ? "RX" : "TX",
		   dccp_feat_fname(dccp_feat_table[idx].feat_num),
		   fval ? "" : "default ",  (unsigned long long)val);

	return dccp_feat_table[idx].activation_hdlr(sk, val, rx);
}

/**
 * dccp_feat_activate  -  Activate feature value on socket
 * @sk: fully connected DCCP socket (after handshake is complete)
 * @feat_num: feature to activate, one of %dccp_feature_numbers
 * @local: whether local (1) or remote (0) @feat_num is meant
 * @fval: the value (SP or NN) to activate, or NULL to use the default value
 * For general use this function is preferable over __dccp_feat_activate().
 */
static int dccp_feat_activate(struct sock *sk, u8 feat_num, bool local,
			      dccp_feat_val const *fval)
{
	return __dccp_feat_activate(sk, dccp_feat_index(feat_num), local, fval);
}

/* Test for "Req'd" feature (RFC 4340, 6.4) */
static inline int dccp_feat_must_be_understood(u8 feat_num)
{
	return	feat_num == DCCPF_CCID || feat_num == DCCPF_SHORT_SEQNOS ||
		feat_num == DCCPF_SEQUENCE_WINDOW;
}

/* copy constructor, fval must not already contain allocated memory */
static int dccp_feat_clone_sp_val(dccp_feat_val *fval, u8 const *val, u8 len)
{
	fval->sp.len = len;
	if (fval->sp.len > 0) {
		fval->sp.vec = kmemdup(val, len, gfp_any());
		if (fval->sp.vec == NULL) {
			fval->sp.len = 0;
			return -ENOBUFS;
		}
	}
	return 0;
}

static void dccp_feat_val_destructor(u8 feat_num, dccp_feat_val *val)
{
	if (unlikely(val == NULL))
		return;
	if (dccp_feat_type(feat_num) == FEAT_SP)
		kfree(val->sp.vec);
	memset(val, 0, sizeof(*val));
}

static struct dccp_feat_entry *
	      dccp_feat_clone_entry(struct dccp_feat_entry const *original)
{
	struct dccp_feat_entry *new;
	u8 type = dccp_feat_type(original->feat_num);

	if (type == FEAT_UNKNOWN)
		return NULL;

	new = kmemdup(original, sizeof(struct dccp_feat_entry), gfp_any());
	if (new == NULL)
		return NULL;

	if (type == FEAT_SP && dccp_feat_clone_sp_val(&new->val,
						      original->val.sp.vec,
						      original->val.sp.len)) {
		kfree(new);
		return NULL;
	}
	return new;
}

static void dccp_feat_entry_destructor(struct dccp_feat_entry *entry)
{
	if (entry != NULL) {
		dccp_feat_val_destructor(entry->feat_num, &entry->val);
		kfree(entry);
	}
}

/*
 * List management functions
 *
 * Feature negotiation lists rely on and maintain the following invariants:
 * - each feat_num in the list is known, i.e. we know its type and default value
 * - each feat_num/is_local combination is unique (old entries are overwritten)
 * - SP values are always freshly allocated
 * - list is sorted in increasing order of feature number (faster lookup)
 */
static struct dccp_feat_entry *dccp_feat_list_lookup(struct list_head *fn_list,
						     u8 feat_num, bool is_local)
{
	struct dccp_feat_entry *entry;

	list_for_each_entry(entry, fn_list, node) {
		if (entry->feat_num == feat_num && entry->is_local == is_local)
			return entry;
		else if (entry->feat_num > feat_num)
			break;
	}
	return NULL;
}

/**
 * dccp_feat_entry_new  -  Central list update routine (called by all others)
 * @head:  list to add to
 * @feat:  feature number
 * @local: whether the local (1) or remote feature with number @feat is meant
 * This is the only constructor and serves to ensure the above invariants.
 */
static struct dccp_feat_entry *
	      dccp_feat_entry_new(struct list_head *head, u8 feat, bool local)
{
	struct dccp_feat_entry *entry;

	list_for_each_entry(entry, head, node)
		if (entry->feat_num == feat && entry->is_local == local) {
			dccp_feat_val_destructor(entry->feat_num, &entry->val);
			return entry;
		} else if (entry->feat_num > feat) {
			head = &entry->node;
			break;
		}

	entry = kmalloc(sizeof(*entry), gfp_any());
	if (entry != NULL) {
		entry->feat_num = feat;
		entry->is_local = local;
		list_add_tail(&entry->node, head);
	}
	return entry;
}

/**
 * dccp_feat_push_change  -  Add/overwrite a Change option in the list
 * @fn_list: feature-negotiation list to update
 * @feat: one of %dccp_feature_numbers
 * @local: whether local (1) or remote (0) @feat_num is meant
 * @needs_mandatory: whether to use Mandatory feature negotiation options
 * @fval: pointer to NN/SP value to be inserted (will be copied)
 */
static int dccp_feat_push_change(struct list_head *fn_list, u8 feat, u8 local,
				 u8 mandatory, dccp_feat_val *fval)
{
	struct dccp_feat_entry *new = dccp_feat_entry_new(fn_list, feat, local);

	if (new == NULL)
		return -ENOMEM;

	new->feat_num	     = feat;
	new->is_local	     = local;
	new->state	     = FEAT_INITIALISING;
	new->needs_confirm   = 0;
	new->empty_confirm   = 0;
	new->val	     = *fval;
	new->needs_mandatory = mandatory;

	return 0;
}

/**
 * dccp_feat_push_confirm  -  Add a Confirm entry to the FN list
 * @fn_list: feature-negotiation list to add to
 * @feat: one of %dccp_feature_numbers
 * @local: whether local (1) or remote (0) @feat_num is being confirmed
 * @fval: pointer to NN/SP value to be inserted or NULL
 * Returns 0 on success, a Reset code for further processing otherwise.
 */
static int dccp_feat_push_confirm(struct list_head *fn_list, u8 feat, u8 local,
				  dccp_feat_val *fval)
{
	struct dccp_feat_entry *new = dccp_feat_entry_new(fn_list, feat, local);

	if (new == NULL)
		return DCCP_RESET_CODE_TOO_BUSY;

	new->feat_num	     = feat;
	new->is_local	     = local;
	new->state	     = FEAT_STABLE;	/* transition in 6.6.2 */
	new->needs_confirm   = 1;
	new->empty_confirm   = (fval == NULL);
	new->val.nn	     = 0;		/* zeroes the whole structure */
	if (!new->empty_confirm)
		new->val     = *fval;
	new->needs_mandatory = 0;

	return 0;
}

static int dccp_push_empty_confirm(struct list_head *fn_list, u8 feat, u8 local)
{
	return dccp_feat_push_confirm(fn_list, feat, local, NULL);
}

static inline void dccp_feat_list_pop(struct dccp_feat_entry *entry)
{
	list_del(&entry->node);
	dccp_feat_entry_destructor(entry);
}

void dccp_feat_list_purge(struct list_head *fn_list)
{
	struct dccp_feat_entry *entry, *next;

	list_for_each_entry_safe(entry, next, fn_list, node)
		dccp_feat_entry_destructor(entry);
	INIT_LIST_HEAD(fn_list);
}
EXPORT_SYMBOL_GPL(dccp_feat_list_purge);

/* generate @to as full clone of @from - @to must not contain any nodes */
int dccp_feat_clone_list(struct list_head const *from, struct list_head *to)
{
	struct dccp_feat_entry *entry, *new;

	INIT_LIST_HEAD(to);
	list_for_each_entry(entry, from, node) {
		new = dccp_feat_clone_entry(entry);
		if (new == NULL)
			goto cloning_failed;
		list_add_tail(&new->node, to);
	}
	return 0;

cloning_failed:
	dccp_feat_list_purge(to);
	return -ENOMEM;
}

/**
 * dccp_feat_valid_nn_length  -  Enforce length constraints on NN options
 * Length is between 0 and %DCCP_OPTVAL_MAXLEN. Used for outgoing packets only,
 * incoming options are accepted as long as their values are valid.
 */
static u8 dccp_feat_valid_nn_length(u8 feat_num)
{
	if (feat_num == DCCPF_ACK_RATIO)	/* RFC 4340, 11.3 and 6.6.8 */
		return 2;
	if (feat_num == DCCPF_SEQUENCE_WINDOW)	/* RFC 4340, 7.5.2 and 6.5  */
		return 6;
	return 0;
}

static u8 dccp_feat_is_valid_nn_val(u8 feat_num, u64 val)
{
	switch (feat_num) {
	case DCCPF_ACK_RATIO:
		return val <= DCCPF_ACK_RATIO_MAX;
	case DCCPF_SEQUENCE_WINDOW:
		return val >= DCCPF_SEQ_WMIN && val <= DCCPF_SEQ_WMAX;
	}
	return 0;	/* feature unknown - so we can't tell */
}

/* check that SP values are within the ranges defined in RFC 4340 */
static u8 dccp_feat_is_valid_sp_val(u8 feat_num, u8 val)
{
	switch (feat_num) {
	case DCCPF_CCID:
		return val == DCCPC_CCID2 || val == DCCPC_CCID3;
	/* Type-check Boolean feature values: */
	case DCCPF_SHORT_SEQNOS:
	case DCCPF_ECN_INCAPABLE:
	case DCCPF_SEND_ACK_VECTOR:
	case DCCPF_SEND_NDP_COUNT:
	case DCCPF_DATA_CHECKSUM:
	case DCCPF_SEND_LEV_RATE:
		return val < 2;
	case DCCPF_MIN_CSUM_COVER:
		return val < 16;
	}
	return 0;			/* feature unknown */
}

static u8 dccp_feat_sp_list_ok(u8 feat_num, u8 const *sp_list, u8 sp_len)
{
	if (sp_list == NULL || sp_len < 1)
		return 0;
	while (sp_len--)
		if (!dccp_feat_is_valid_sp_val(feat_num, *sp_list++))
			return 0;
	return 1;
}

/**
 * dccp_feat_insert_opts  -  Generate FN options from current list state
 * @skb: next sk_buff to be sent to the peer
 * @dp: for client during handshake and general negotiation
 * @dreq: used by the server only (all Changes/Confirms in LISTEN/RESPOND)
 */
int dccp_feat_insert_opts(struct dccp_sock *dp, struct dccp_request_sock *dreq,
			  struct sk_buff *skb)
{
	struct list_head *fn = dreq ? &dreq->dreq_featneg : &dp->dccps_featneg;
	struct dccp_feat_entry *pos, *next;
	u8 opt, type, len, *ptr, nn_in_nbo[DCCP_OPTVAL_MAXLEN];
	bool rpt;

	/* put entries into @skb in the order they appear in the list */
	list_for_each_entry_safe_reverse(pos, next, fn, node) {
		opt  = dccp_feat_genopt(pos);
		type = dccp_feat_type(pos->feat_num);
		rpt  = false;

		if (pos->empty_confirm) {
			len = 0;
			ptr = NULL;
		} else {
			if (type == FEAT_SP) {
				len = pos->val.sp.len;
				ptr = pos->val.sp.vec;
				rpt = pos->needs_confirm;
			} else if (type == FEAT_NN) {
				len = dccp_feat_valid_nn_length(pos->feat_num);
				ptr = nn_in_nbo;
				dccp_encode_value_var(pos->val.nn, ptr, len);
			} else {
				DCCP_BUG("unknown feature %u", pos->feat_num);
				return -1;
			}
		}
		dccp_feat_print_opt(opt, pos->feat_num, ptr, len, 0);

		if (dccp_insert_fn_opt(skb, opt, pos->feat_num, ptr, len, rpt))
			return -1;
		if (pos->needs_mandatory && dccp_insert_option_mandatory(skb))
			return -1;
		/*
		 * Enter CHANGING after transmitting the Change option (6.6.2).
		 */
		if (pos->state == FEAT_INITIALISING)
			pos->state = FEAT_CHANGING;
	}
	return 0;
}

/**
 * __feat_register_nn  -  Register new NN value on socket
 * @fn: feature-negotiation list to register with
 * @feat: an NN feature from %dccp_feature_numbers
 * @mandatory: use Mandatory option if 1
 * @nn_val: value to register (restricted to 4 bytes)
 * Note that NN features are local by definition (RFC 4340, 6.3.2).
 */
static int __feat_register_nn(struct list_head *fn, u8 feat,
			      u8 mandatory, u64 nn_val)
{
	dccp_feat_val fval = { .nn = nn_val };

	if (dccp_feat_type(feat) != FEAT_NN ||
	    !dccp_feat_is_valid_nn_val(feat, nn_val))
		return -EINVAL;

	/* Don't bother with default values, they will be activated anyway. */
	if (nn_val - (u64)dccp_feat_default_value(feat) == 0)
		return 0;

	return dccp_feat_push_change(fn, feat, 1, mandatory, &fval);
}

/**
 * __feat_register_sp  -  Register new SP value/list on socket
 * @fn: feature-negotiation list to register with
 * @feat: an SP feature from %dccp_feature_numbers
 * @is_local: whether the local (1) or the remote (0) @feat is meant
 * @mandatory: use Mandatory option if 1
 * @sp_val: SP value followed by optional preference list
 * @sp_len: length of @sp_val in bytes
 */
static int __feat_register_sp(struct list_head *fn, u8 feat, u8 is_local,
			      u8 mandatory, u8 const *sp_val, u8 sp_len)
{
	dccp_feat_val fval;

	if (dccp_feat_type(feat) != FEAT_SP ||
	    !dccp_feat_sp_list_ok(feat, sp_val, sp_len))
		return -EINVAL;

	/* Avoid negotiating alien CCIDs by only advertising supported ones */
	if (feat == DCCPF_CCID && !ccid_support_check(sp_val, sp_len))
		return -EOPNOTSUPP;

	if (dccp_feat_clone_sp_val(&fval, sp_val, sp_len))
		return -ENOMEM;

	return dccp_feat_push_change(fn, feat, is_local, mandatory, &fval);
}

/**
 * dccp_feat_register_sp  -  Register requests to change SP feature values
 * @sk: client or listening socket
 * @feat: one of %dccp_feature_numbers
 * @is_local: whether the local (1) or remote (0) @feat is meant
 * @list: array of preferred values, in descending order of preference
 * @len: length of @list in bytes
 */
int dccp_feat_register_sp(struct sock *sk, u8 feat, u8 is_local,
			  u8 const *list, u8 len)
{	 /* any changes must be registered before establishing the connection */
	if (sk->sk_state != DCCP_CLOSED)
		return -EISCONN;
	if (dccp_feat_type(feat) != FEAT_SP)
		return -EINVAL;
	return __feat_register_sp(&dccp_sk(sk)->dccps_featneg, feat, is_local,
				  0, list, len);
}

/**
 * dccp_feat_nn_get  -  Query current/pending value of NN feature
 * @sk: DCCP socket of an established connection
 * @feat: NN feature number from %dccp_feature_numbers
 * For a known NN feature, returns value currently being negotiated, or
 * current (confirmed) value if no negotiation is going on.
 */
u64 dccp_feat_nn_get(struct sock *sk, u8 feat)
{
	if (dccp_feat_type(feat) == FEAT_NN) {
		struct dccp_sock *dp = dccp_sk(sk);
		struct dccp_feat_entry *entry;

		entry = dccp_feat_list_lookup(&dp->dccps_featneg, feat, 1);
		if (entry != NULL)
			return entry->val.nn;

		switch (feat) {
		case DCCPF_ACK_RATIO:
			return dp->dccps_l_ack_ratio;
		case DCCPF_SEQUENCE_WINDOW:
			return dp->dccps_l_seq_win;
		}
	}
	DCCP_BUG("attempt to look up unsupported feature %u", feat);
	return 0;
}
EXPORT_SYMBOL_GPL(dccp_feat_nn_get);

/**
 * dccp_feat_signal_nn_change  -  Update NN values for an established connection
 * @sk: DCCP socket of an established connection
 * @feat: NN feature number from %dccp_feature_numbers
 * @nn_val: the new value to use
 * This function is used to communicate NN updates out-of-band.
 */
int dccp_feat_signal_nn_change(struct sock *sk, u8 feat, u64 nn_val)
{
	struct list_head *fn = &dccp_sk(sk)->dccps_featneg;
	dccp_feat_val fval = { .nn = nn_val };
	struct dccp_feat_entry *entry;

	if (sk->sk_state != DCCP_OPEN && sk->sk_state != DCCP_PARTOPEN)
		return 0;

	if (dccp_feat_type(feat) != FEAT_NN ||
	    !dccp_feat_is_valid_nn_val(feat, nn_val))
		return -EINVAL;

	if (nn_val == dccp_feat_nn_get(sk, feat))
		return 0;	/* already set or negotiation under way */

	entry = dccp_feat_list_lookup(fn, feat, 1);
	if (entry != NULL) {
		dccp_pr_debug("Clobbering existing NN entry %llu -> %llu\n",
			      (unsigned long long)entry->val.nn,
			      (unsigned long long)nn_val);
		dccp_feat_list_pop(entry);
	}

	inet_csk_schedule_ack(sk);
	return dccp_feat_push_change(fn, feat, 1, 0, &fval);
}
EXPORT_SYMBOL_GPL(dccp_feat_signal_nn_change);

/*
 *	Tracking features whose value depend on the choice of CCID
 *
 * This is designed with an extension in mind so that a list walk could be done
 * before activating any features. However, the existing framework was found to
 * work satisfactorily up until now, the automatic verification is left open.
 * When adding new CCIDs, add a corresponding dependency table here.
 */
static const struct ccid_dependency *dccp_feat_ccid_deps(u8 ccid, bool is_local)
{
	static const struct ccid_dependency ccid2_dependencies[2][2] = {
		/*
		 * CCID2 mandates Ack Vectors (RFC 4341, 4.): as CCID is a TX
		 * feature and Send Ack Vector is an RX feature, `is_local'
		 * needs to be reversed.
		 */
		{	/* Dependencies of the receiver-side (remote) CCID2 */
			{
				.dependent_feat	= DCCPF_SEND_ACK_VECTOR,
				.is_local	= true,
				.is_mandatory	= true,
				.val		= 1
			},
			{ 0, 0, 0, 0 }
		},
		{	/* Dependencies of the sender-side (local) CCID2 */
			{
				.dependent_feat	= DCCPF_SEND_ACK_VECTOR,
				.is_local	= false,
				.is_mandatory	= true,
				.val		= 1
			},
			{ 0, 0, 0, 0 }
		}
	};
	static const struct ccid_dependency ccid3_dependencies[2][5] = {
		{	/*
			 * Dependencies of the receiver-side CCID3
			 */
			{	/* locally disable Ack Vectors */
				.dependent_feat	= DCCPF_SEND_ACK_VECTOR,
				.is_local	= true,
				.is_mandatory	= false,
				.val		= 0
			},
			{	/* see below why Send Loss Event Rate is on */
				.dependent_feat	= DCCPF_SEND_LEV_RATE,
				.is_local	= true,
				.is_mandatory	= true,
				.val		= 1
			},
			{	/* NDP Count is needed as per RFC 4342, 6.1.1 */
				.dependent_feat	= DCCPF_SEND_NDP_COUNT,
				.is_local	= false,
				.is_mandatory	= true,
				.val		= 1
			},
			{ 0, 0, 0, 0 },
		},
		{	/*
			 * CCID3 at the TX side: we request that the HC-receiver
			 * will not send Ack Vectors (they will be ignored, so
			 * Mandatory is not set); we enable Send Loss Event Rate
			 * (Mandatory since the implementation does not support
			 * the Loss Intervals option of RFC 4342, 8.6).
			 * The last two options are for peer's information only.
			*/
			{
				.dependent_feat	= DCCPF_SEND_ACK_VECTOR,
				.is_local	= false,
				.is_mandatory	= false,
				.val		= 0
			},
			{
				.dependent_feat	= DCCPF_SEND_LEV_RATE,
				.is_local	= false,
				.is_mandatory	= true,
				.val		= 1
			},
			{	/* this CCID does not support Ack Ratio */
				.dependent_feat	= DCCPF_ACK_RATIO,
				.is_local	= true,
				.is_mandatory	= false,
				.val		= 0
			},
			{	/* tell receiver we are sending NDP counts */
				.dependent_feat	= DCCPF_SEND_NDP_COUNT,
				.is_local	= true,
				.is_mandatory	= false,
				.val		= 1
			},
			{ 0, 0, 0, 0 }
		}
	};
	switch (ccid) {
	case DCCPC_CCID2:
		return ccid2_dependencies[is_local];
	case DCCPC_CCID3:
		return ccid3_dependencies[is_local];
	default:
		return NULL;
	}
}

/**
 * dccp_feat_propagate_ccid - Resolve dependencies of features on choice of CCID
 * @fn: feature-negotiation list to update
 * @id: CCID number to track
 * @is_local: whether TX CCID (1) or RX CCID (0) is meant
 * This function needs to be called after registering all other features.
 */
static int dccp_feat_propagate_ccid(struct list_head *fn, u8 id, bool is_local)
{
	const struct ccid_dependency *table = dccp_feat_ccid_deps(id, is_local);
	int i, rc = (table == NULL);

	for (i = 0; rc == 0 && table[i].dependent_feat != DCCPF_RESERVED; i++)
		if (dccp_feat_type(table[i].dependent_feat) == FEAT_SP)
			rc = __feat_register_sp(fn, table[i].dependent_feat,
						    table[i].is_local,
						    table[i].is_mandatory,
						    &table[i].val, 1);
		else
			rc = __feat_register_nn(fn, table[i].dependent_feat,
						    table[i].is_mandatory,
						    table[i].val);
	return rc;
}

/**
 * dccp_feat_finalise_settings  -  Finalise settings before starting negotiation
 * @dp: client or listening socket (settings will be inherited)
 * This is called after all registrations (socket initialisation, sysctls, and
 * sockopt calls), and before sending the first packet containing Change options
 * (ie. client-Request or server-Response), to ensure internal consistency.
 */
int dccp_feat_finalise_settings(struct dccp_sock *dp)
{
	struct list_head *fn = &dp->dccps_featneg;
	struct dccp_feat_entry *entry;
	int i = 2, ccids[2] = { -1, -1 };

	/*
	 * Propagating CCIDs:
	 * 1) not useful to propagate CCID settings if this host advertises more
	 *    than one CCID: the choice of CCID  may still change - if this is
	 *    the client, or if this is the server and the client sends
	 *    singleton CCID values.
	 * 2) since is that propagate_ccid changes the list, we defer changing
	 *    the sorted list until after the traversal.
	 */
	list_for_each_entry(entry, fn, node)
		if (entry->feat_num == DCCPF_CCID && entry->val.sp.len == 1)
			ccids[entry->is_local] = entry->val.sp.vec[0];
	while (i--)
		if (ccids[i] > 0 && dccp_feat_propagate_ccid(fn, ccids[i], i))
			return -1;
	dccp_feat_print_fnlist(fn);
	return 0;
}

/**
 * dccp_feat_server_ccid_dependencies  -  Resolve CCID-dependent features
 * It is the server which resolves the dependencies once the CCID has been
 * fully negotiated. If no CCID has been negotiated, it uses the default CCID.
 */
int dccp_feat_server_ccid_dependencies(struct dccp_request_sock *dreq)
{
	struct list_head *fn = &dreq->dreq_featneg;
	struct dccp_feat_entry *entry;
	u8 is_local, ccid;

	for (is_local = 0; is_local <= 1; is_local++) {
		entry = dccp_feat_list_lookup(fn, DCCPF_CCID, is_local);

		if (entry != NULL && !entry->empty_confirm)
			ccid = entry->val.sp.vec[0];
		else
			ccid = dccp_feat_default_value(DCCPF_CCID);

		if (dccp_feat_propagate_ccid(fn, ccid, is_local))
			return -1;
	}
	return 0;
}

/* Select the first entry in @servlist that also occurs in @clilist (6.3.1) */
static int dccp_feat_preflist_match(u8 *servlist, u8 slen, u8 *clilist, u8 clen)
{
	u8 c, s;

	for (s = 0; s < slen; s++)
		for (c = 0; c < clen; c++)
			if (servlist[s] == clilist[c])
				return servlist[s];
	return -1;
}

/**
 * dccp_feat_prefer  -  Move preferred entry to the start of array
 * Reorder the @array_len elements in @array so that @preferred_value comes
 * first. Returns >0 to indicate that @preferred_value does occur in @array.
 */
static u8 dccp_feat_prefer(u8 preferred_value, u8 *array, u8 array_len)
{
	u8 i, does_occur = 0;

	if (array != NULL) {
		for (i = 0; i < array_len; i++)
			if (array[i] == preferred_value) {
				array[i] = array[0];
				does_occur++;
			}
		if (does_occur)
			array[0] = preferred_value;
	}
	return does_occur;
}

/**
 * dccp_feat_reconcile  -  Reconcile SP preference lists
 *  @fval: SP list to reconcile into
 *  @arr: received SP preference list
 *  @len: length of @arr in bytes
 *  @is_server: whether this side is the server (and @fv is the server's list)
 *  @reorder: whether to reorder the list in @fv after reconciling with @arr
 * When successful, > 0 is returned and the reconciled list is in @fval.
 * A value of 0 means that negotiation failed (no shared entry).
 */
static int dccp_feat_reconcile(dccp_feat_val *fv, u8 *arr, u8 len,
			       bool is_server, bool reorder)
{
	int rc;

	if (!fv->sp.vec || !arr) {
		DCCP_CRIT("NULL feature value or array");
		return 0;
	}

	if (is_server)
		rc = dccp_feat_preflist_match(fv->sp.vec, fv->sp.len, arr, len);
	else
		rc = dccp_feat_preflist_match(arr, len, fv->sp.vec, fv->sp.len);

	if (!reorder)
		return rc;
	if (rc < 0)
		return 0;

	/*
	 * Reorder list: used for activating features and in dccp_insert_fn_opt.
	 */
	return dccp_feat_prefer(rc, fv->sp.vec, fv->sp.len);
}

/**
 * dccp_feat_change_recv  -  Process incoming ChangeL/R options
 * @fn: feature-negotiation list to update
 * @is_mandatory: whether the Change was preceded by a Mandatory option
 * @opt: %DCCPO_CHANGE_L or %DCCPO_CHANGE_R
 * @feat: one of %dccp_feature_numbers
 * @val: NN value or SP value/preference list
 * @len: length of @val in bytes
 * @server: whether this node is the server (1) or the client (0)
 */
static u8 dccp_feat_change_recv(struct list_head *fn, u8 is_mandatory, u8 opt,
				u8 feat, u8 *val, u8 len, const bool server)
{
	u8 defval, type = dccp_feat_type(feat);
	const bool local = (opt == DCCPO_CHANGE_R);
	struct dccp_feat_entry *entry;
	dccp_feat_val fval;

	if (len == 0 || type == FEAT_UNKNOWN)		/* 6.1 and 6.6.8 */
		goto unknown_feature_or_value;

	dccp_feat_print_opt(opt, feat, val, len, is_mandatory);

	/*
	 *	Negotiation of NN features: Change R is invalid, so there is no
	 *	simultaneous negotiation; hence we do not look up in the list.
	 */
	if (type == FEAT_NN) {
		if (local || len > sizeof(fval.nn))
			goto unknown_feature_or_value;

		/* 6.3.2: "The feature remote MUST accept any valid value..." */
		fval.nn = dccp_decode_value_var(val, len);
		if (!dccp_feat_is_valid_nn_val(feat, fval.nn))
			goto unknown_feature_or_value;

		return dccp_feat_push_confirm(fn, feat, local, &fval);
	}

	/*
	 *	Unidirectional/simultaneous negotiation of SP features (6.3.1)
	 */
	entry = dccp_feat_list_lookup(fn, feat, local);
	if (entry == NULL) {
		/*
		 * No particular preferences have been registered. We deal with
		 * this situation by assuming that all valid values are equally
		 * acceptable, and apply the following checks:
		 * - if the peer's list is a singleton, we accept a valid value;
		 * - if we are the server, we first try to see if the peer (the
		 *   client) advertises the default value. If yes, we use it,
		 *   otherwise we accept the preferred value;
		 * - else if we are the client, we use the first list element.
		 */
		if (dccp_feat_clone_sp_val(&fval, val, 1))
			return DCCP_RESET_CODE_TOO_BUSY;

		if (len > 1 && server) {
			defval = dccp_feat_default_value(feat);
			if (dccp_feat_preflist_match(&defval, 1, val, len) > -1)
				fval.sp.vec[0] = defval;
		} else if (!dccp_feat_is_valid_sp_val(feat, fval.sp.vec[0])) {
			kfree(fval.sp.vec);
			goto unknown_feature_or_value;
		}

		/* Treat unsupported CCIDs like invalid values */
		if (feat == DCCPF_CCID && !ccid_support_check(fval.sp.vec, 1)) {
			kfree(fval.sp.vec);
			goto not_valid_or_not_known;
		}

		return dccp_feat_push_confirm(fn, feat, local, &fval);

	} else if (entry->state == FEAT_UNSTABLE) {	/* 6.6.2 */
		return 0;
	}

	if (dccp_feat_reconcile(&entry->val, val, len, server, true)) {
		entry->empty_confirm = 0;
	} else if (is_mandatory) {
		return DCCP_RESET_CODE_MANDATORY_ERROR;
	} else if (entry->state == FEAT_INITIALISING) {
		/*
		 * Failed simultaneous negotiation (server only): try to `save'
		 * the connection by checking whether entry contains the default
		 * value for @feat. If yes, send an empty Confirm to signal that
		 * the received Change was not understood - which implies using
		 * the default value.
		 * If this also fails, we use Reset as the last resort.
		 */
		WARN_ON(!server);
		defval = dccp_feat_default_value(feat);
		if (!dccp_feat_reconcile(&entry->val, &defval, 1, server, true))
			return DCCP_RESET_CODE_OPTION_ERROR;
		entry->empty_confirm = 1;
	}
	entry->needs_confirm   = 1;
	entry->needs_mandatory = 0;
	entry->state	       = FEAT_STABLE;
	return 0;

unknown_feature_or_value:
	if (!is_mandatory)
		return dccp_push_empty_confirm(fn, feat, local);

not_valid_or_not_known:
	return is_mandatory ? DCCP_RESET_CODE_MANDATORY_ERROR
			    : DCCP_RESET_CODE_OPTION_ERROR;
}

/**
 * dccp_feat_confirm_recv  -  Process received Confirm options
 * @fn: feature-negotiation list to update
 * @is_mandatory: whether @opt was preceded by a Mandatory option
 * @opt: %DCCPO_CONFIRM_L or %DCCPO_CONFIRM_R
 * @feat: one of %dccp_feature_numbers
 * @val: NN value or SP value/preference list
 * @len: length of @val in bytes
 * @server: whether this node is server (1) or client (0)
 */
static u8 dccp_feat_confirm_recv(struct list_head *fn, u8 is_mandatory, u8 opt,
				 u8 feat, u8 *val, u8 len, const bool server)
{
	u8 *plist, plen, type = dccp_feat_type(feat);
	const bool local = (opt == DCCPO_CONFIRM_R);
	struct dccp_feat_entry *entry = dccp_feat_list_lookup(fn, feat, local);

	dccp_feat_print_opt(opt, feat, val, len, is_mandatory);

	if (entry == NULL) {	/* nothing queued: ignore or handle error */
		if (is_mandatory && type == FEAT_UNKNOWN)
			return DCCP_RESET_CODE_MANDATORY_ERROR;

		if (!local && type == FEAT_NN)		/* 6.3.2 */
			goto confirmation_failed;
		return 0;
	}

	if (entry->state != FEAT_CHANGING)		/* 6.6.2 */
		return 0;

	if (len == 0) {
		if (dccp_feat_must_be_understood(feat))	/* 6.6.7 */
			goto confirmation_failed;
		/*
		 * Empty Confirm during connection setup: this means reverting
		 * to the `old' value, which in this case is the default. Since
		 * we handle default values automatically when no other values
		 * have been set, we revert to the old value by removing this
		 * entry from the list.
		 */
		dccp_feat_list_pop(entry);
		return 0;
	}

	if (type == FEAT_NN) {
		if (len > sizeof(entry->val.nn))
			goto confirmation_failed;

		if (entry->val.nn == dccp_decode_value_var(val, len))
			goto confirmation_succeeded;

		DCCP_WARN("Bogus Confirm for non-existing value\n");
		goto confirmation_failed;
	}

	/*
	 * Parsing SP Confirms: the first element of @val is the preferred
	 * SP value which the peer confirms, the remainder depends on @len.
	 * Note that only the confirmed value need to be a valid SP value.
	 */
	if (!dccp_feat_is_valid_sp_val(feat, *val))
		goto confirmation_failed;

	if (len == 1) {		/* peer didn't supply a preference list */
		plist = val;
		plen  = len;
	} else {		/* preferred value + preference list */
		plist = val + 1;
		plen  = len - 1;
	}

	/* Check whether the peer got the reconciliation right (6.6.8) */
	if (dccp_feat_reconcile(&entry->val, plist, plen, server, 0) != *val) {
		DCCP_WARN("Confirm selected the wrong value %u\n", *val);
		return DCCP_RESET_CODE_OPTION_ERROR;
	}
	entry->val.sp.vec[0] = *val;

confirmation_succeeded:
	entry->state = FEAT_STABLE;
	return 0;

confirmation_failed:
	DCCP_WARN("Confirmation failed\n");
	return is_mandatory ? DCCP_RESET_CODE_MANDATORY_ERROR
			    : DCCP_RESET_CODE_OPTION_ERROR;
}

/**
 * dccp_feat_handle_nn_established  -  Fast-path reception of NN options
 * @sk:		socket of an established DCCP connection
 * @mandatory:	whether @opt was preceded by a Mandatory option
 * @opt:	%DCCPO_CHANGE_L | %DCCPO_CONFIRM_R (NN only)
 * @feat:	NN number, one of %dccp_feature_numbers
 * @val:	NN value
 * @len:	length of @val in bytes
 * This function combines the functionality of change_recv/confirm_recv, with
 * the following differences (reset codes are the same):
 *    - cleanup after receiving the Confirm;
 *    - values are directly activated after successful parsing;
 *    - deliberately restricted to NN features.
 * The restriction to NN features is essential since SP features can have non-
 * predictable outcomes (depending on the remote configuration), and are inter-
 * dependent (CCIDs for instance cause further dependencies).
 */
static u8 dccp_feat_handle_nn_established(struct sock *sk, u8 mandatory, u8 opt,
					  u8 feat, u8 *val, u8 len)
{
	struct list_head *fn = &dccp_sk(sk)->dccps_featneg;
	const bool local = (opt == DCCPO_CONFIRM_R);
	struct dccp_feat_entry *entry;
	u8 type = dccp_feat_type(feat);
	dccp_feat_val fval;

	dccp_feat_print_opt(opt, feat, val, len, mandatory);

	/* Ignore non-mandatory unknown and non-NN features */
	if (type == FEAT_UNKNOWN) {
		if (local && !mandatory)
			return 0;
		goto fast_path_unknown;
	} else if (type != FEAT_NN) {
		return 0;
	}

	/*
	 * We don't accept empty Confirms, since in fast-path feature
	 * negotiation the values are enabled immediately after sending
	 * the Change option.
	 * Empty Changes on the other hand are invalid (RFC 4340, 6.1).
	 */
	if (len == 0 || len > sizeof(fval.nn))
		goto fast_path_unknown;

	if (opt == DCCPO_CHANGE_L) {
		fval.nn = dccp_decode_value_var(val, len);
		if (!dccp_feat_is_valid_nn_val(feat, fval.nn))
			goto fast_path_unknown;

		if (dccp_feat_push_confirm(fn, feat, local, &fval) ||
		    dccp_feat_activate(sk, feat, local, &fval))
			return DCCP_RESET_CODE_TOO_BUSY;

		/* set the `Ack Pending' flag to piggyback a Confirm */
		inet_csk_schedule_ack(sk);

	} else if (opt == DCCPO_CONFIRM_R) {
		entry = dccp_feat_list_lookup(fn, feat, local);
		if (entry == NULL || entry->state != FEAT_CHANGING)
			return 0;

		fval.nn = dccp_decode_value_var(val, len);
		/*
		 * Just ignore a value that doesn't match our current value.
		 * If the option changes twice within two RTTs, then at least
		 * one CONFIRM will be received for the old value after a
		 * new CHANGE was sent.
		 */
		if (fval.nn != entry->val.nn)
			return 0;

		/* Only activate after receiving the Confirm option (6.6.1). */
		dccp_feat_activate(sk, feat, local, &fval);

		/* It has been confirmed - so remove the entry */
		dccp_feat_list_pop(entry);

	} else {
		DCCP_WARN("Received illegal option %u\n", opt);
		goto fast_path_failed;
	}
	return 0;

fast_path_unknown:
	if (!mandatory)
		return dccp_push_empty_confirm(fn, feat, local);

fast_path_failed:
	return mandatory ? DCCP_RESET_CODE_MANDATORY_ERROR
			 : DCCP_RESET_CODE_OPTION_ERROR;
}

/**
 * dccp_feat_parse_options  -  Process Feature-Negotiation Options
 * @sk: for general use and used by the client during connection setup
 * @dreq: used by the server during connection setup
 * @mandatory: whether @opt was preceded by a Mandatory option
 * @opt: %DCCPO_CHANGE_L | %DCCPO_CHANGE_R | %DCCPO_CONFIRM_L | %DCCPO_CONFIRM_R
 * @feat: one of %dccp_feature_numbers
 * @val: value contents of @opt
 * @len: length of @val in bytes
 * Returns 0 on success, a Reset code for ending the connection otherwise.
 */
int dccp_feat_parse_options(struct sock *sk, struct dccp_request_sock *dreq,
			    u8 mandatory, u8 opt, u8 feat, u8 *val, u8 len)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct list_head *fn = dreq ? &dreq->dreq_featneg : &dp->dccps_featneg;
	bool server = false;

	switch (sk->sk_state) {
	/*
	 *	Negotiation during connection setup
	 */
	case DCCP_LISTEN:
		server = true;			/* fall through */
	case DCCP_REQUESTING:
		switch (opt) {
		case DCCPO_CHANGE_L:
		case DCCPO_CHANGE_R:
			return dccp_feat_change_recv(fn, mandatory, opt, feat,
						     val, len, server);
		case DCCPO_CONFIRM_R:
		case DCCPO_CONFIRM_L:
			return dccp_feat_confirm_recv(fn, mandatory, opt, feat,
						      val, len, server);
		}
		break;
	/*
	 *	Support for exchanging NN options on an established connection.
	 */
	case DCCP_OPEN:
	case DCCP_PARTOPEN:
		return dccp_feat_handle_nn_established(sk, mandatory, opt, feat,
						       val, len);
	}
	return 0;	/* ignore FN options in all other states */
}

/**
 * dccp_feat_init  -  Seed feature negotiation with host-specific defaults
 * This initialises global defaults, depending on the value of the sysctls.
 * These can later be overridden by registering changes via setsockopt calls.
 * The last link in the chain is finalise_settings, to make sure that between
 * here and the start of actual feature negotiation no inconsistencies enter.
 *
 * All features not appearing below use either defaults or are otherwise
 * later adjusted through dccp_feat_finalise_settings().
 */
int dccp_feat_init(struct sock *sk)
{
	struct list_head *fn = &dccp_sk(sk)->dccps_featneg;
	u8 on = 1, off = 0;
	int rc;
	struct {
		u8 *val;
		u8 len;
	} tx, rx;

	/* Non-negotiable (NN) features */
	rc = __feat_register_nn(fn, DCCPF_SEQUENCE_WINDOW, 0,
				    sysctl_dccp_sequence_window);
	if (rc)
		return rc;

	/* Server-priority (SP) features */

	/* Advertise that short seqnos are not supported (7.6.1) */
	rc = __feat_register_sp(fn, DCCPF_SHORT_SEQNOS, true, true, &off, 1);
	if (rc)
		return rc;

	/* RFC 4340 12.1: "If a DCCP is not ECN capable, ..." */
	rc = __feat_register_sp(fn, DCCPF_ECN_INCAPABLE, true, true, &on, 1);
	if (rc)
		return rc;

	/*
	 * We advertise the available list of CCIDs and reorder according to
	 * preferences, to avoid failure resulting from negotiating different
	 * singleton values (which always leads to failure).
	 * These settings can still (later) be overridden via sockopts.
	 */
	if (ccid_get_builtin_ccids(&tx.val, &tx.len) ||
	    ccid_get_builtin_ccids(&rx.val, &rx.len))
		return -ENOBUFS;

	if (!dccp_feat_prefer(sysctl_dccp_tx_ccid, tx.val, tx.len) ||
	    !dccp_feat_prefer(sysctl_dccp_rx_ccid, rx.val, rx.len))
		goto free_ccid_lists;

	rc = __feat_register_sp(fn, DCCPF_CCID, true, false, tx.val, tx.len);
	if (rc)
		goto free_ccid_lists;

	rc = __feat_register_sp(fn, DCCPF_CCID, false, false, rx.val, rx.len);

free_ccid_lists:
	kfree(tx.val);
	kfree(rx.val);
	return rc;
}

int dccp_feat_activate_values(struct sock *sk, struct list_head *fn_list)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct dccp_feat_entry *cur, *next;
	int idx;
	dccp_feat_val *fvals[DCCP_FEAT_SUPPORTED_MAX][2] = {
		 [0 ... DCCP_FEAT_SUPPORTED_MAX-1] = { NULL, NULL }
	};

	list_for_each_entry(cur, fn_list, node) {
		/*
		 * An empty Confirm means that either an unknown feature type
		 * or an invalid value was present. In the first case there is
		 * nothing to activate, in the other the default value is used.
		 */
		if (cur->empty_confirm)
			continue;

		idx = dccp_feat_index(cur->feat_num);
		if (idx < 0) {
			DCCP_BUG("Unknown feature %u", cur->feat_num);
			goto activation_failed;
		}
		if (cur->state != FEAT_STABLE) {
			DCCP_CRIT("Negotiation of %s %s failed in state %s",
				  cur->is_local ? "local" : "remote",
				  dccp_feat_fname(cur->feat_num),
				  dccp_feat_sname[cur->state]);
			goto activation_failed;
		}
		fvals[idx][cur->is_local] = &cur->val;
	}

	/*
	 * Activate in decreasing order of index, so that the CCIDs are always
	 * activated as the last feature. This avoids the case where a CCID
	 * relies on the initialisation of one or more features that it depends
	 * on (e.g. Send NDP Count, Send Ack Vector, and Ack Ratio features).
	 */
	for (idx = DCCP_FEAT_SUPPORTED_MAX; --idx >= 0;)
		if (__dccp_feat_activate(sk, idx, 0, fvals[idx][0]) ||
		    __dccp_feat_activate(sk, idx, 1, fvals[idx][1])) {
			DCCP_CRIT("Could not activate %d", idx);
			goto activation_failed;
		}

	/* Clean up Change options which have been confirmed already */
	list_for_each_entry_safe(cur, next, fn_list, node)
		if (!cur->needs_confirm)
			dccp_feat_list_pop(cur);

	dccp_pr_debug("Activation OK\n");
	return 0;

activation_failed:
	/*
	 * We clean up everything that may have been allocated, since
	 * it is difficult to track at which stage negotiation failed.
	 * This is ok, since all allocation functions below are robust
	 * against NULL arguments.
	 */
	ccid_hc_rx_delete(dp->dccps_hc_rx_ccid, sk);
	ccid_hc_tx_delete(dp->dccps_hc_tx_ccid, sk);
	dp->dccps_hc_rx_ccid = dp->dccps_hc_tx_ccid = NULL;
	dccp_ackvec_free(dp->dccps_hc_rx_ackvec);
	dp->dccps_hc_rx_ackvec = NULL;
	return -1;
}
