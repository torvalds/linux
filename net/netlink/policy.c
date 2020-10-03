// SPDX-License-Identifier: GPL-2.0
/*
 * NETLINK      Policy advertisement to userspace
 *
 * 		Authors:	Johannes Berg <johannes@sipsolutions.net>
 *
 * Copyright 2019 Intel Corporation
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <net/netlink.h>

#define INITIAL_POLICIES_ALLOC	10

struct netlink_policy_dump_state {
	unsigned int policy_idx;
	unsigned int attr_idx;
	unsigned int n_alloc;
	struct {
		const struct nla_policy *policy;
		unsigned int maxtype;
	} policies[];
};

static int add_policy(struct netlink_policy_dump_state **statep,
		      const struct nla_policy *policy,
		      unsigned int maxtype)
{
	struct netlink_policy_dump_state *state = *statep;
	unsigned int n_alloc, i;

	if (!policy || !maxtype)
		return 0;

	for (i = 0; i < state->n_alloc; i++) {
		if (state->policies[i].policy == policy &&
		    state->policies[i].maxtype == maxtype)
			return 0;

		if (!state->policies[i].policy) {
			state->policies[i].policy = policy;
			state->policies[i].maxtype = maxtype;
			return 0;
		}
	}

	n_alloc = state->n_alloc + INITIAL_POLICIES_ALLOC;
	state = krealloc(state, struct_size(state, policies, n_alloc),
			 GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	memset(&state->policies[state->n_alloc], 0,
	       flex_array_size(state, policies, n_alloc - state->n_alloc));

	state->policies[state->n_alloc].policy = policy;
	state->policies[state->n_alloc].maxtype = maxtype;
	state->n_alloc = n_alloc;
	*statep = state;

	return 0;
}

static unsigned int get_policy_idx(struct netlink_policy_dump_state *state,
				   const struct nla_policy *policy,
				   unsigned int maxtype)
{
	unsigned int i;

	for (i = 0; i < state->n_alloc; i++) {
		if (state->policies[i].policy == policy &&
		    state->policies[i].maxtype == maxtype)
			return i;
	}

	WARN_ON_ONCE(1);
	return -1;
}

int netlink_policy_dump_start(const struct nla_policy *policy,
			      unsigned int maxtype,
			      struct netlink_policy_dump_state **statep)
{
	struct netlink_policy_dump_state *state;
	unsigned int policy_idx;
	int err;

	if (*statep)
		return 0;

	/*
	 * walk the policies and nested ones first, and build
	 * a linear list of them.
	 */

	state = kzalloc(struct_size(state, policies, INITIAL_POLICIES_ALLOC),
			GFP_KERNEL);
	if (!state)
		return -ENOMEM;
	state->n_alloc = INITIAL_POLICIES_ALLOC;

	err = add_policy(&state, policy, maxtype);
	if (err)
		return err;

	for (policy_idx = 0;
	     policy_idx < state->n_alloc && state->policies[policy_idx].policy;
	     policy_idx++) {
		const struct nla_policy *policy;
		unsigned int type;

		policy = state->policies[policy_idx].policy;

		for (type = 0;
		     type <= state->policies[policy_idx].maxtype;
		     type++) {
			switch (policy[type].type) {
			case NLA_NESTED:
			case NLA_NESTED_ARRAY:
				err = add_policy(&state,
						 policy[type].nested_policy,
						 policy[type].len);
				if (err)
					return err;
				break;
			default:
				break;
			}
		}
	}

	*statep = state;

	return 0;
}

static bool
netlink_policy_dump_finished(struct netlink_policy_dump_state *state)
{
	return state->policy_idx >= state->n_alloc ||
	       !state->policies[state->policy_idx].policy;
}

bool netlink_policy_dump_loop(struct netlink_policy_dump_state *state)
{
	return !netlink_policy_dump_finished(state);
}

int netlink_policy_dump_write(struct sk_buff *skb,
			      struct netlink_policy_dump_state *state)
{
	const struct nla_policy *pt;
	struct nlattr *policy, *attr;
	enum netlink_attribute_type type;
	bool again;

send_attribute:
	again = false;

	pt = &state->policies[state->policy_idx].policy[state->attr_idx];

	policy = nla_nest_start(skb, state->policy_idx);
	if (!policy)
		return -ENOBUFS;

	attr = nla_nest_start(skb, state->attr_idx);
	if (!attr)
		goto nla_put_failure;

	switch (pt->type) {
	default:
	case NLA_UNSPEC:
	case NLA_REJECT:
		/* skip - use NLA_MIN_LEN to advertise such */
		nla_nest_cancel(skb, policy);
		again = true;
		goto next;
	case NLA_NESTED:
		type = NL_ATTR_TYPE_NESTED;
		fallthrough;
	case NLA_NESTED_ARRAY:
		if (pt->type == NLA_NESTED_ARRAY)
			type = NL_ATTR_TYPE_NESTED_ARRAY;
		if (pt->nested_policy && pt->len &&
		    (nla_put_u32(skb, NL_POLICY_TYPE_ATTR_POLICY_IDX,
				 get_policy_idx(state, pt->nested_policy,
						pt->len)) ||
		     nla_put_u32(skb, NL_POLICY_TYPE_ATTR_POLICY_MAXTYPE,
				 pt->len)))
			goto nla_put_failure;
		break;
	case NLA_U8:
	case NLA_U16:
	case NLA_U32:
	case NLA_U64:
	case NLA_MSECS: {
		struct netlink_range_validation range;

		if (pt->type == NLA_U8)
			type = NL_ATTR_TYPE_U8;
		else if (pt->type == NLA_U16)
			type = NL_ATTR_TYPE_U16;
		else if (pt->type == NLA_U32)
			type = NL_ATTR_TYPE_U32;
		else
			type = NL_ATTR_TYPE_U64;

		nla_get_range_unsigned(pt, &range);

		if (nla_put_u64_64bit(skb, NL_POLICY_TYPE_ATTR_MIN_VALUE_U,
				      range.min, NL_POLICY_TYPE_ATTR_PAD) ||
		    nla_put_u64_64bit(skb, NL_POLICY_TYPE_ATTR_MAX_VALUE_U,
				      range.max, NL_POLICY_TYPE_ATTR_PAD))
			goto nla_put_failure;
		break;
	}
	case NLA_S8:
	case NLA_S16:
	case NLA_S32:
	case NLA_S64: {
		struct netlink_range_validation_signed range;

		if (pt->type == NLA_S8)
			type = NL_ATTR_TYPE_S8;
		else if (pt->type == NLA_S16)
			type = NL_ATTR_TYPE_S16;
		else if (pt->type == NLA_S32)
			type = NL_ATTR_TYPE_S32;
		else
			type = NL_ATTR_TYPE_S64;

		nla_get_range_signed(pt, &range);

		if (nla_put_s64(skb, NL_POLICY_TYPE_ATTR_MIN_VALUE_S,
				range.min, NL_POLICY_TYPE_ATTR_PAD) ||
		    nla_put_s64(skb, NL_POLICY_TYPE_ATTR_MAX_VALUE_S,
				range.max, NL_POLICY_TYPE_ATTR_PAD))
			goto nla_put_failure;
		break;
	}
	case NLA_BITFIELD32:
		type = NL_ATTR_TYPE_BITFIELD32;
		if (nla_put_u32(skb, NL_POLICY_TYPE_ATTR_BITFIELD32_MASK,
				pt->bitfield32_valid))
			goto nla_put_failure;
		break;
	case NLA_STRING:
	case NLA_NUL_STRING:
	case NLA_BINARY:
		if (pt->type == NLA_STRING)
			type = NL_ATTR_TYPE_STRING;
		else if (pt->type == NLA_NUL_STRING)
			type = NL_ATTR_TYPE_NUL_STRING;
		else
			type = NL_ATTR_TYPE_BINARY;

		if (pt->validation_type == NLA_VALIDATE_RANGE ||
		    pt->validation_type == NLA_VALIDATE_RANGE_WARN_TOO_LONG) {
			struct netlink_range_validation range;

			nla_get_range_unsigned(pt, &range);

			if (range.min &&
			    nla_put_u32(skb, NL_POLICY_TYPE_ATTR_MIN_LENGTH,
					range.min))
				goto nla_put_failure;

			if (range.max < U16_MAX &&
			    nla_put_u32(skb, NL_POLICY_TYPE_ATTR_MAX_LENGTH,
					range.max))
				goto nla_put_failure;
		} else if (pt->len &&
			   nla_put_u32(skb, NL_POLICY_TYPE_ATTR_MAX_LENGTH,
				       pt->len)) {
			goto nla_put_failure;
		}
		break;
	case NLA_FLAG:
		type = NL_ATTR_TYPE_FLAG;
		break;
	}

	if (nla_put_u32(skb, NL_POLICY_TYPE_ATTR_TYPE, type))
		goto nla_put_failure;

	/* finish and move state to next attribute */
	nla_nest_end(skb, attr);
	nla_nest_end(skb, policy);

next:
	state->attr_idx += 1;
	if (state->attr_idx > state->policies[state->policy_idx].maxtype) {
		state->attr_idx = 0;
		state->policy_idx++;
	}

	if (again) {
		if (netlink_policy_dump_finished(state))
			return -ENODATA;
		goto send_attribute;
	}

	return 0;

nla_put_failure:
	nla_nest_cancel(skb, policy);
	return -ENOBUFS;
}

void netlink_policy_dump_free(struct netlink_policy_dump_state *state)
{
	kfree(state);
}
