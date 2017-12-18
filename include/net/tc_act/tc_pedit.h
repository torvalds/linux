/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_TC_PED_H
#define __NET_TC_PED_H

#include <net/act_api.h>
#include <linux/tc_act/tc_pedit.h>

struct tcf_pedit_key_ex {
	enum pedit_header_type htype;
	enum pedit_cmd cmd;
};

struct tcf_pedit {
	struct tc_action	common;
	unsigned char		tcfp_nkeys;
	unsigned char		tcfp_flags;
	struct tc_pedit_key	*tcfp_keys;
	struct tcf_pedit_key_ex	*tcfp_keys_ex;
};
#define to_pedit(a) ((struct tcf_pedit *)a)

static inline bool is_tcf_pedit(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	if (a->ops && a->ops->type == TCA_ACT_PEDIT)
		return true;
#endif
	return false;
}

static inline int tcf_pedit_nkeys(const struct tc_action *a)
{
	return to_pedit(a)->tcfp_nkeys;
}

static inline u32 tcf_pedit_htype(const struct tc_action *a, int index)
{
	if (to_pedit(a)->tcfp_keys_ex)
		return to_pedit(a)->tcfp_keys_ex[index].htype;

	return TCA_PEDIT_KEY_EX_HDR_TYPE_NETWORK;
}

static inline u32 tcf_pedit_cmd(const struct tc_action *a, int index)
{
	if (to_pedit(a)->tcfp_keys_ex)
		return to_pedit(a)->tcfp_keys_ex[index].cmd;

	return __PEDIT_CMD_MAX;
}

static inline u32 tcf_pedit_mask(const struct tc_action *a, int index)
{
	return to_pedit(a)->tcfp_keys[index].mask;
}

static inline u32 tcf_pedit_val(const struct tc_action *a, int index)
{
	return to_pedit(a)->tcfp_keys[index].val;
}

static inline u32 tcf_pedit_offset(const struct tc_action *a, int index)
{
	return to_pedit(a)->tcfp_keys[index].off;
}
#endif /* __NET_TC_PED_H */
