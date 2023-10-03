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
	} policies[] __counted_by(n_alloc);
};

static int add_policy(struct netlink_policy_dump_state **statep,
		      const struct nla_policy *policy,
		      unsigned int maxtype)
{
	struct netlink_policy_dump_state *state = *statep;
	unsigned int old_n_alloc, n_alloc, i;

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

	old_n_alloc = state->n_alloc;
	state->n_alloc = n_alloc;
	memset(&state->policies[old_n_alloc], 0,
	       flex_array_size(state, policies, n_alloc - old_n_alloc));

	state->policies[old_n_alloc].policy = policy;
	state->policies[old_n_alloc].maxtype = maxtype;
	*statep = state;

	return 0;
}

/**
 * netlink_policy_dump_get_policy_idx - retrieve policy index
 * @state: the policy dump state
 * @policy: the policy to find
 * @maxtype: the policy's maxattr
 *
 * Returns: the index of the given policy in the dump state
 *
 * Call this to find a policy index when you've added multiple and e.g.
 * need to tell userspace which command has which policy (by index).
 *
 * Note: this will WARN and return 0 if the policy isn't found, which
 *	 means it wasn't added in the first place, which would be an
 *	 internal consistency bug.
 */
int netlink_policy_dump_get_policy_idx(struct netlink_policy_dump_state *state,
				       const struct nla_policy *policy,
				       unsigned int maxtype)
{
	unsigned int i;

	if (WARN_ON(!policy || !maxtype))
                return 0;

	for (i = 0; i < state->n_alloc; i++) {
		if (state->policies[i].policy == policy &&
		    state->policies[i].maxtype == maxtype)
			return i;
	}

	WARN_ON(1);
	return 0;
}

static struct netlink_policy_dump_state *alloc_state(void)
{
	struct netlink_policy_dump_state *state;

	state = kzalloc(struct_size(state, policies, INITIAL_POLICIES_ALLOC),
			GFP_KERNEL);
	if (!state)
		return ERR_PTR(-ENOMEM);
	state->n_alloc = INITIAL_POLICIES_ALLOC;

	return state;
}

/**
 * netlink_policy_dump_add_policy - add a policy to the dump
 * @pstate: state to add to, may be reallocated, must be %NULL the first time
 * @policy: the new policy to add to the dump
 * @maxtype: the new policy's max attr type
 *
 * Returns: 0 on success, a negative error code otherwise.
 *
 * Call this to allocate a policy dump state, and to add policies to it. This
 * should be called from the dump start() callback.
 *
 * Note: on failures, any previously allocated state is freed.
 */
int netlink_policy_dump_add_policy(struct netlink_policy_dump_state **pstate,
				   const struct nla_policy *policy,
				   unsigned int maxtype)
{
	struct netlink_policy_dump_state *state = *pstate;
	unsigned int policy_idx;
	int err;

	if (!state) {
		state = alloc_state();
		if (IS_ERR(state))
			return PTR_ERR(state);
	}

	/*
	 * walk the policies and nested ones first, and build
	 * a linear list of them.
	 */

	err = add_policy(&state, policy, maxtype);
	if (err)
		goto err_try_undo;

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
					goto err_try_undo;
				break;
			default:
				break;
			}
		}
	}

	*pstate = state;
	return 0;

err_try_undo:
	/* Try to preserve reasonable unwind semantics - if we're starting from
	 * scratch clean up fully, otherwise record what we got and caller will.
	 */
	if (!*pstate)
		netlink_policy_dump_free(state);
	else
		*pstate = state;
	return err;
}

static bool
netlink_policy_dump_finished(struct netlink_policy_dump_state *state)
{
	return state->policy_idx >= state->n_alloc ||
	       !state->policies[state->policy_idx].policy;
}

/**
 * netlink_policy_dump_loop - dumping loop indicator
 * @state: the policy dump state
 *
 * Returns: %true if the dump continues, %false otherwise
 *
 * Note: this frees the dump state when finishing
 */
bool netlink_policy_dump_loop(struct netlink_policy_dump_state *state)
{
	return !netlink_policy_dump_finished(state);
}

int netlink_policy_dump_attr_size_estimate(const struct nla_policy *pt)
{
	/* nested + type */
	int common = 2 * nla_attr_size(sizeof(u32));

	switch (pt->type) {
	case NLA_UNSPEC:
	case NLA_REJECT:
		/* these actually don't need any space */
		return 0;
	case NLA_NESTED:
	case NLA_NESTED_ARRAY:
		/* common, policy idx, policy maxattr */
		return common + 2 * nla_attr_size(sizeof(u32));
	case NLA_U8:
	case NLA_U16:
	case NLA_U32:
	case NLA_U64:
	case NLA_MSECS:
	case NLA_S8:
	case NLA_S16:
	case NLA_S32:
	case NLA_S64:
		/* maximum is common, u64 min/max with padding */
		return common +
		       2 * (nla_attr_size(0) + nla_attr_size(sizeof(u64)));
	case NLA_BITFIELD32:
		return common + nla_attr_size(sizeof(u32));
	case NLA_STRING:
	case NLA_NUL_STRING:
	case NLA_BINARY:
		/* maximum is common, u32 min-length/max-length */
		return common + 2 * nla_attr_size(sizeof(u32));
	case NLA_FLAG:
		return common;
	}

	/* this should then cause a warning later */
	return 0;
}

static int
__netlink_policy_dump_write_attr(struct netlink_policy_dump_state *state,
				 struct sk_buff *skb,
				 const struct nla_policy *pt,
				 int nestattr)
{
	int estimate = netlink_policy_dump_attr_size_estimate(pt);
	enum netlink_attribute_type type;
	struct nlattr *attr;

	attr = nla_nest_start(skb, nestattr);
	if (!attr)
		return -ENOBUFS;

	switch (pt->type) {
	default:
	case NLA_UNSPEC:
	case NLA_REJECT:
		/* skip - use NLA_MIN_LEN to advertise such */
		nla_nest_cancel(skb, attr);
		return -ENODATA;
	case NLA_NESTED:
		type = NL_ATTR_TYPE_NESTED;
		fallthrough;
	case NLA_NESTED_ARRAY:
		if (pt->type == NLA_NESTED_ARRAY)
			type = NL_ATTR_TYPE_NESTED_ARRAY;
		if (state && pt->nested_policy && pt->len &&
		    (nla_put_u32(skb, NL_POLICY_TYPE_ATTR_POLICY_IDX,
				 netlink_policy_dump_get_policy_idx(state,
								    pt->nested_policy,
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

		if (pt->validation_type == NLA_VALIDATE_MASK) {
			if (nla_put_u64_64bit(skb, NL_POLICY_TYPE_ATTR_MASK,
					      pt->mask,
					      NL_POLICY_TYPE_ATTR_PAD))
				goto nla_put_failure;
			break;
		}

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

	nla_nest_end(skb, attr);
	WARN_ON(attr->nla_len > estimate);

	return 0;
nla_put_failure:
	nla_nest_cancel(skb, attr);
	return -ENOBUFS;
}

/**
 * netlink_policy_dump_write_attr - write a given attribute policy
 * @skb: the message skb to write to
 * @pt: the attribute's policy
 * @nestattr: the nested attribute ID to use
 *
 * Returns: 0 on success, an error code otherwise; -%ENODATA is
 *	    special, indicating that there's no policy data and
 *	    the attribute is generally rejected.
 */
int netlink_policy_dump_write_attr(struct sk_buff *skb,
				   const struct nla_policy *pt,
				   int nestattr)
{
	return __netlink_policy_dump_write_attr(NULL, skb, pt, nestattr);
}

/**
 * netlink_policy_dump_write - write current policy dump attributes
 * @skb: the message skb to write to
 * @state: the policy dump state
 *
 * Returns: 0 on success, an error code otherwise
 */
int netlink_policy_dump_write(struct sk_buff *skb,
			      struct netlink_policy_dump_state *state)
{
	const struct nla_policy *pt;
	struct nlattr *policy;
	bool again;
	int err;

send_attribute:
	again = false;

	pt = &state->policies[state->policy_idx].policy[state->attr_idx];

	policy = nla_nest_start(skb, state->policy_idx);
	if (!policy)
		return -ENOBUFS;

	err = __netlink_policy_dump_write_attr(state, skb, pt, state->attr_idx);
	if (err == -ENODATA) {
		nla_nest_cancel(skb, policy);
		again = true;
		goto next;
	} else if (err) {
		goto nla_put_failure;
	}

	/* finish and move state to next attribute */
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

/**
 * netlink_policy_dump_free - free policy dump state
 * @state: the policy dump state to free
 *
 * Call this from the done() method to ensure dump state is freed.
 */
void netlink_policy_dump_free(struct netlink_policy_dump_state *state)
{
	kfree(state);
}
