/*
 * NetLabel CIPSO/IPv4 Support
 *
 * This file defines the CIPSO/IPv4 functions for the NetLabel system.  The
 * NetLabel system manages static and dynamic label mappings for network
 * protocols such as CIPSO and RIPSO.
 *
 * Author: Paul Moore <paul.moore@hp.com>
 *
 */

/*
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2006
 *
 * This program is free software;  you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY;  without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program;  if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/audit.h>
#include <net/sock.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <net/netlabel.h>
#include <net/cipso_ipv4.h>
#include <asm/atomic.h>

#include "netlabel_user.h"
#include "netlabel_cipso_v4.h"
#include "netlabel_mgmt.h"

/* Argument struct for cipso_v4_doi_walk() */
struct netlbl_cipsov4_doiwalk_arg {
	struct netlink_callback *nl_cb;
	struct sk_buff *skb;
	u32 seq;
};

/* NetLabel Generic NETLINK CIPSOv4 family */
static struct genl_family netlbl_cipsov4_gnl_family = {
	.id = GENL_ID_GENERATE,
	.hdrsize = 0,
	.name = NETLBL_NLTYPE_CIPSOV4_NAME,
	.version = NETLBL_PROTO_VERSION,
	.maxattr = NLBL_CIPSOV4_A_MAX,
};

/* NetLabel Netlink attribute policy */
static const struct nla_policy netlbl_cipsov4_genl_policy[NLBL_CIPSOV4_A_MAX + 1] = {
	[NLBL_CIPSOV4_A_DOI] = { .type = NLA_U32 },
	[NLBL_CIPSOV4_A_MTYPE] = { .type = NLA_U32 },
	[NLBL_CIPSOV4_A_TAG] = { .type = NLA_U8 },
	[NLBL_CIPSOV4_A_TAGLST] = { .type = NLA_NESTED },
	[NLBL_CIPSOV4_A_MLSLVLLOC] = { .type = NLA_U32 },
	[NLBL_CIPSOV4_A_MLSLVLREM] = { .type = NLA_U32 },
	[NLBL_CIPSOV4_A_MLSLVL] = { .type = NLA_NESTED },
	[NLBL_CIPSOV4_A_MLSLVLLST] = { .type = NLA_NESTED },
	[NLBL_CIPSOV4_A_MLSCATLOC] = { .type = NLA_U32 },
	[NLBL_CIPSOV4_A_MLSCATREM] = { .type = NLA_U32 },
	[NLBL_CIPSOV4_A_MLSCAT] = { .type = NLA_NESTED },
	[NLBL_CIPSOV4_A_MLSCATLST] = { .type = NLA_NESTED },
};

/*
 * Helper Functions
 */

/**
 * netlbl_cipsov4_doi_free - Frees a CIPSO V4 DOI definition
 * @entry: the entry's RCU field
 *
 * Description:
 * This function is designed to be used as a callback to the call_rcu()
 * function so that the memory allocated to the DOI definition can be released
 * safely.
 *
 */
void netlbl_cipsov4_doi_free(struct rcu_head *entry)
{
	struct cipso_v4_doi *ptr;

	ptr = container_of(entry, struct cipso_v4_doi, rcu);
	switch (ptr->type) {
	case CIPSO_V4_MAP_STD:
		kfree(ptr->map.std->lvl.cipso);
		kfree(ptr->map.std->lvl.local);
		kfree(ptr->map.std->cat.cipso);
		kfree(ptr->map.std->cat.local);
		break;
	}
	kfree(ptr);
}

/**
 * netlbl_cipsov4_add_common - Parse the common sections of a ADD message
 * @info: the Generic NETLINK info block
 * @doi_def: the CIPSO V4 DOI definition
 *
 * Description:
 * Parse the common sections of a ADD message and fill in the related values
 * in @doi_def.  Returns zero on success, negative values on failure.
 *
 */
static int netlbl_cipsov4_add_common(struct genl_info *info,
				     struct cipso_v4_doi *doi_def)
{
	struct nlattr *nla;
	int nla_rem;
	u32 iter = 0;

	doi_def->doi = nla_get_u32(info->attrs[NLBL_CIPSOV4_A_DOI]);

	if (nla_validate_nested(info->attrs[NLBL_CIPSOV4_A_TAGLST],
				NLBL_CIPSOV4_A_MAX,
				netlbl_cipsov4_genl_policy) != 0)
		return -EINVAL;

	nla_for_each_nested(nla, info->attrs[NLBL_CIPSOV4_A_TAGLST], nla_rem)
		if (nla_type(nla) == NLBL_CIPSOV4_A_TAG) {
			if (iter >= CIPSO_V4_TAG_MAXCNT)
				return -EINVAL;
			doi_def->tags[iter++] = nla_get_u8(nla);
		}
	while (iter < CIPSO_V4_TAG_MAXCNT)
		doi_def->tags[iter++] = CIPSO_V4_TAG_INVALID;

	return 0;
}

/*
 * NetLabel Command Handlers
 */

/**
 * netlbl_cipsov4_add_std - Adds a CIPSO V4 DOI definition
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Create a new CIPSO_V4_MAP_STD DOI definition based on the given ADD message
 * and add it to the CIPSO V4 engine.  Return zero on success and non-zero on
 * error.
 *
 */
static int netlbl_cipsov4_add_std(struct genl_info *info)
{
	int ret_val = -EINVAL;
	struct cipso_v4_doi *doi_def = NULL;
	struct nlattr *nla_a;
	struct nlattr *nla_b;
	int nla_a_rem;
	int nla_b_rem;
	u32 iter;

	if (!info->attrs[NLBL_CIPSOV4_A_TAGLST] ||
	    !info->attrs[NLBL_CIPSOV4_A_MLSLVLLST])
		return -EINVAL;

	if (nla_validate_nested(info->attrs[NLBL_CIPSOV4_A_MLSLVLLST],
				NLBL_CIPSOV4_A_MAX,
				netlbl_cipsov4_genl_policy) != 0)
		return -EINVAL;

	doi_def = kmalloc(sizeof(*doi_def), GFP_KERNEL);
	if (doi_def == NULL)
		return -ENOMEM;
	doi_def->map.std = kzalloc(sizeof(*doi_def->map.std), GFP_KERNEL);
	if (doi_def->map.std == NULL) {
		ret_val = -ENOMEM;
		goto add_std_failure;
	}
	doi_def->type = CIPSO_V4_MAP_STD;

	ret_val = netlbl_cipsov4_add_common(info, doi_def);
	if (ret_val != 0)
		goto add_std_failure;
	ret_val = -EINVAL;

	nla_for_each_nested(nla_a,
			    info->attrs[NLBL_CIPSOV4_A_MLSLVLLST],
			    nla_a_rem)
		if (nla_type(nla_a) == NLBL_CIPSOV4_A_MLSLVL) {
			if (nla_validate_nested(nla_a,
					    NLBL_CIPSOV4_A_MAX,
					    netlbl_cipsov4_genl_policy) != 0)
					goto add_std_failure;
			nla_for_each_nested(nla_b, nla_a, nla_b_rem)
				switch (nla_type(nla_b)) {
				case NLBL_CIPSOV4_A_MLSLVLLOC:
					if (nla_get_u32(nla_b) >
					    CIPSO_V4_MAX_LOC_LVLS)
						goto add_std_failure;
					if (nla_get_u32(nla_b) >=
					    doi_def->map.std->lvl.local_size)
					     doi_def->map.std->lvl.local_size =
						     nla_get_u32(nla_b) + 1;
					break;
				case NLBL_CIPSOV4_A_MLSLVLREM:
					if (nla_get_u32(nla_b) >
					    CIPSO_V4_MAX_REM_LVLS)
						goto add_std_failure;
					if (nla_get_u32(nla_b) >=
					    doi_def->map.std->lvl.cipso_size)
					     doi_def->map.std->lvl.cipso_size =
						     nla_get_u32(nla_b) + 1;
					break;
				}
		}
	doi_def->map.std->lvl.local = kcalloc(doi_def->map.std->lvl.local_size,
					      sizeof(u32),
					      GFP_KERNEL);
	if (doi_def->map.std->lvl.local == NULL) {
		ret_val = -ENOMEM;
		goto add_std_failure;
	}
	doi_def->map.std->lvl.cipso = kcalloc(doi_def->map.std->lvl.cipso_size,
					      sizeof(u32),
					      GFP_KERNEL);
	if (doi_def->map.std->lvl.cipso == NULL) {
		ret_val = -ENOMEM;
		goto add_std_failure;
	}
	for (iter = 0; iter < doi_def->map.std->lvl.local_size; iter++)
		doi_def->map.std->lvl.local[iter] = CIPSO_V4_INV_LVL;
	for (iter = 0; iter < doi_def->map.std->lvl.cipso_size; iter++)
		doi_def->map.std->lvl.cipso[iter] = CIPSO_V4_INV_LVL;
	nla_for_each_nested(nla_a,
			    info->attrs[NLBL_CIPSOV4_A_MLSLVLLST],
			    nla_a_rem)
		if (nla_type(nla_a) == NLBL_CIPSOV4_A_MLSLVL) {
			struct nlattr *lvl_loc;
			struct nlattr *lvl_rem;

			lvl_loc = nla_find_nested(nla_a,
						  NLBL_CIPSOV4_A_MLSLVLLOC);
			lvl_rem = nla_find_nested(nla_a,
						  NLBL_CIPSOV4_A_MLSLVLREM);
			if (lvl_loc == NULL || lvl_rem == NULL)
				goto add_std_failure;
			doi_def->map.std->lvl.local[nla_get_u32(lvl_loc)] =
				nla_get_u32(lvl_rem);
			doi_def->map.std->lvl.cipso[nla_get_u32(lvl_rem)] =
				nla_get_u32(lvl_loc);
		}

	if (info->attrs[NLBL_CIPSOV4_A_MLSCATLST]) {
		if (nla_validate_nested(info->attrs[NLBL_CIPSOV4_A_MLSCATLST],
					NLBL_CIPSOV4_A_MAX,
					netlbl_cipsov4_genl_policy) != 0)
			goto add_std_failure;

		nla_for_each_nested(nla_a,
				    info->attrs[NLBL_CIPSOV4_A_MLSCATLST],
				    nla_a_rem)
			if (nla_type(nla_a) == NLBL_CIPSOV4_A_MLSCAT) {
				if (nla_validate_nested(nla_a,
					      NLBL_CIPSOV4_A_MAX,
					      netlbl_cipsov4_genl_policy) != 0)
					goto add_std_failure;
				nla_for_each_nested(nla_b, nla_a, nla_b_rem)
					switch (nla_type(nla_b)) {
					case NLBL_CIPSOV4_A_MLSCATLOC:
						if (nla_get_u32(nla_b) >
						    CIPSO_V4_MAX_LOC_CATS)
							goto add_std_failure;
						if (nla_get_u32(nla_b) >=
					      doi_def->map.std->cat.local_size)
					     doi_def->map.std->cat.local_size =
						     nla_get_u32(nla_b) + 1;
						break;
					case NLBL_CIPSOV4_A_MLSCATREM:
						if (nla_get_u32(nla_b) >
						    CIPSO_V4_MAX_REM_CATS)
							goto add_std_failure;
						if (nla_get_u32(nla_b) >=
					      doi_def->map.std->cat.cipso_size)
					     doi_def->map.std->cat.cipso_size =
						     nla_get_u32(nla_b) + 1;
						break;
					}
			}
		doi_def->map.std->cat.local = kcalloc(
					      doi_def->map.std->cat.local_size,
					      sizeof(u32),
					      GFP_KERNEL);
		if (doi_def->map.std->cat.local == NULL) {
			ret_val = -ENOMEM;
			goto add_std_failure;
		}
		doi_def->map.std->cat.cipso = kcalloc(
					      doi_def->map.std->cat.cipso_size,
					      sizeof(u32),
					      GFP_KERNEL);
		if (doi_def->map.std->cat.cipso == NULL) {
			ret_val = -ENOMEM;
			goto add_std_failure;
		}
		for (iter = 0; iter < doi_def->map.std->cat.local_size; iter++)
			doi_def->map.std->cat.local[iter] = CIPSO_V4_INV_CAT;
		for (iter = 0; iter < doi_def->map.std->cat.cipso_size; iter++)
			doi_def->map.std->cat.cipso[iter] = CIPSO_V4_INV_CAT;
		nla_for_each_nested(nla_a,
				    info->attrs[NLBL_CIPSOV4_A_MLSCATLST],
				    nla_a_rem)
			if (nla_type(nla_a) == NLBL_CIPSOV4_A_MLSCAT) {
				struct nlattr *cat_loc;
				struct nlattr *cat_rem;

				cat_loc = nla_find_nested(nla_a,
						     NLBL_CIPSOV4_A_MLSCATLOC);
				cat_rem = nla_find_nested(nla_a,
						     NLBL_CIPSOV4_A_MLSCATREM);
				if (cat_loc == NULL || cat_rem == NULL)
					goto add_std_failure;
				doi_def->map.std->cat.local[
							nla_get_u32(cat_loc)] =
					nla_get_u32(cat_rem);
				doi_def->map.std->cat.cipso[
							nla_get_u32(cat_rem)] =
					nla_get_u32(cat_loc);
			}
	}

	ret_val = cipso_v4_doi_add(doi_def);
	if (ret_val != 0)
		goto add_std_failure;
	return 0;

add_std_failure:
	if (doi_def)
		netlbl_cipsov4_doi_free(&doi_def->rcu);
	return ret_val;
}

/**
 * netlbl_cipsov4_add_pass - Adds a CIPSO V4 DOI definition
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Create a new CIPSO_V4_MAP_PASS DOI definition based on the given ADD message
 * and add it to the CIPSO V4 engine.  Return zero on success and non-zero on
 * error.
 *
 */
static int netlbl_cipsov4_add_pass(struct genl_info *info)
{
	int ret_val;
	struct cipso_v4_doi *doi_def = NULL;

	if (!info->attrs[NLBL_CIPSOV4_A_TAGLST])
		return -EINVAL;

	doi_def = kmalloc(sizeof(*doi_def), GFP_KERNEL);
	if (doi_def == NULL)
		return -ENOMEM;
	doi_def->type = CIPSO_V4_MAP_PASS;

	ret_val = netlbl_cipsov4_add_common(info, doi_def);
	if (ret_val != 0)
		goto add_pass_failure;

	ret_val = cipso_v4_doi_add(doi_def);
	if (ret_val != 0)
		goto add_pass_failure;
	return 0;

add_pass_failure:
	netlbl_cipsov4_doi_free(&doi_def->rcu);
	return ret_val;
}

/**
 * netlbl_cipsov4_add - Handle an ADD message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Create a new DOI definition based on the given ADD message and add it to the
 * CIPSO V4 engine.  Returns zero on success, negative values on failure.
 *
 */
static int netlbl_cipsov4_add(struct sk_buff *skb, struct genl_info *info)

{
	int ret_val = -EINVAL;
	u32 type;
	u32 doi;
	const char *type_str = "(unknown)";
	struct audit_buffer *audit_buf;
	struct netlbl_audit audit_info;

	if (!info->attrs[NLBL_CIPSOV4_A_DOI] ||
	    !info->attrs[NLBL_CIPSOV4_A_MTYPE])
		return -EINVAL;

	doi = nla_get_u32(info->attrs[NLBL_CIPSOV4_A_DOI]);
	netlbl_netlink_auditinfo(skb, &audit_info);

	type = nla_get_u32(info->attrs[NLBL_CIPSOV4_A_MTYPE]);
	switch (type) {
	case CIPSO_V4_MAP_STD:
		type_str = "std";
		ret_val = netlbl_cipsov4_add_std(info);
		break;
	case CIPSO_V4_MAP_PASS:
		type_str = "pass";
		ret_val = netlbl_cipsov4_add_pass(info);
		break;
	}
	if (ret_val == 0)
		atomic_inc(&netlabel_mgmt_protocount);

	audit_buf = netlbl_audit_start_common(AUDIT_MAC_CIPSOV4_ADD,
					      &audit_info);
	if (audit_buf != NULL) {
		audit_log_format(audit_buf,
				 " cipso_doi=%u cipso_type=%s res=%u",
				 doi,
				 type_str,
				 ret_val == 0 ? 1 : 0);
		audit_log_end(audit_buf);
	}

	return ret_val;
}

/**
 * netlbl_cipsov4_list - Handle a LIST message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Process a user generated LIST message and respond accordingly.  While the
 * response message generated by the kernel is straightforward, determining
 * before hand the size of the buffer to allocate is not (we have to generate
 * the message to know the size).  In order to keep this function sane what we
 * do is allocate a buffer of NLMSG_GOODSIZE and try to fit the response in
 * that size, if we fail then we restart with a larger buffer and try again.
 * We continue in this manner until we hit a limit of failed attempts then we
 * give up and just send an error message.  Returns zero on success and
 * negative values on error.
 *
 */
static int netlbl_cipsov4_list(struct sk_buff *skb, struct genl_info *info)
{
	int ret_val;
	struct sk_buff *ans_skb = NULL;
	u32 nlsze_mult = 1;
	void *data;
	u32 doi;
	struct nlattr *nla_a;
	struct nlattr *nla_b;
	struct cipso_v4_doi *doi_def;
	u32 iter;

	if (!info->attrs[NLBL_CIPSOV4_A_DOI]) {
		ret_val = -EINVAL;
		goto list_failure;
	}

list_start:
	ans_skb = nlmsg_new(NLMSG_DEFAULT_SIZE * nlsze_mult, GFP_KERNEL);
	if (ans_skb == NULL) {
		ret_val = -ENOMEM;
		goto list_failure;
	}
	data = genlmsg_put_reply(ans_skb, info, &netlbl_cipsov4_gnl_family,
				 0, NLBL_CIPSOV4_C_LIST);
	if (data == NULL) {
		ret_val = -ENOMEM;
		goto list_failure;
	}

	doi = nla_get_u32(info->attrs[NLBL_CIPSOV4_A_DOI]);

	rcu_read_lock();
	doi_def = cipso_v4_doi_getdef(doi);
	if (doi_def == NULL) {
		ret_val = -EINVAL;
		goto list_failure_lock;
	}

	ret_val = nla_put_u32(ans_skb, NLBL_CIPSOV4_A_MTYPE, doi_def->type);
	if (ret_val != 0)
		goto list_failure_lock;

	nla_a = nla_nest_start(ans_skb, NLBL_CIPSOV4_A_TAGLST);
	if (nla_a == NULL) {
		ret_val = -ENOMEM;
		goto list_failure_lock;
	}
	for (iter = 0;
	     iter < CIPSO_V4_TAG_MAXCNT &&
	       doi_def->tags[iter] != CIPSO_V4_TAG_INVALID;
	     iter++) {
		ret_val = nla_put_u8(ans_skb,
				     NLBL_CIPSOV4_A_TAG,
				     doi_def->tags[iter]);
		if (ret_val != 0)
			goto list_failure_lock;
	}
	nla_nest_end(ans_skb, nla_a);

	switch (doi_def->type) {
	case CIPSO_V4_MAP_STD:
		nla_a = nla_nest_start(ans_skb, NLBL_CIPSOV4_A_MLSLVLLST);
		if (nla_a == NULL) {
			ret_val = -ENOMEM;
			goto list_failure_lock;
		}
		for (iter = 0;
		     iter < doi_def->map.std->lvl.local_size;
		     iter++) {
			if (doi_def->map.std->lvl.local[iter] ==
			    CIPSO_V4_INV_LVL)
				continue;

			nla_b = nla_nest_start(ans_skb, NLBL_CIPSOV4_A_MLSLVL);
			if (nla_b == NULL) {
				ret_val = -ENOMEM;
				goto list_retry;
			}
			ret_val = nla_put_u32(ans_skb,
					      NLBL_CIPSOV4_A_MLSLVLLOC,
					      iter);
			if (ret_val != 0)
				goto list_retry;
			ret_val = nla_put_u32(ans_skb,
					    NLBL_CIPSOV4_A_MLSLVLREM,
					    doi_def->map.std->lvl.local[iter]);
			if (ret_val != 0)
				goto list_retry;
			nla_nest_end(ans_skb, nla_b);
		}
		nla_nest_end(ans_skb, nla_a);

		nla_a = nla_nest_start(ans_skb, NLBL_CIPSOV4_A_MLSCATLST);
		if (nla_a == NULL) {
			ret_val = -ENOMEM;
			goto list_retry;
		}
		for (iter = 0;
		     iter < doi_def->map.std->cat.local_size;
		     iter++) {
			if (doi_def->map.std->cat.local[iter] ==
			    CIPSO_V4_INV_CAT)
				continue;

			nla_b = nla_nest_start(ans_skb, NLBL_CIPSOV4_A_MLSCAT);
			if (nla_b == NULL) {
				ret_val = -ENOMEM;
				goto list_retry;
			}
			ret_val = nla_put_u32(ans_skb,
					      NLBL_CIPSOV4_A_MLSCATLOC,
					      iter);
			if (ret_val != 0)
				goto list_retry;
			ret_val = nla_put_u32(ans_skb,
					    NLBL_CIPSOV4_A_MLSCATREM,
					    doi_def->map.std->cat.local[iter]);
			if (ret_val != 0)
				goto list_retry;
			nla_nest_end(ans_skb, nla_b);
		}
		nla_nest_end(ans_skb, nla_a);

		break;
	}
	rcu_read_unlock();

	genlmsg_end(ans_skb, data);
	return genlmsg_reply(ans_skb, info);

list_retry:
	/* XXX - this limit is a guesstimate */
	if (nlsze_mult < 4) {
		rcu_read_unlock();
		kfree_skb(ans_skb);
		nlsze_mult *= 2;
		goto list_start;
	}
list_failure_lock:
	rcu_read_unlock();
list_failure:
	kfree_skb(ans_skb);
	return ret_val;
}

/**
 * netlbl_cipsov4_listall_cb - cipso_v4_doi_walk() callback for LISTALL
 * @doi_def: the CIPSOv4 DOI definition
 * @arg: the netlbl_cipsov4_doiwalk_arg structure
 *
 * Description:
 * This function is designed to be used as a callback to the
 * cipso_v4_doi_walk() function for use in generating a response for a LISTALL
 * message.  Returns the size of the message on success, negative values on
 * failure.
 *
 */
static int netlbl_cipsov4_listall_cb(struct cipso_v4_doi *doi_def, void *arg)
{
	int ret_val = -ENOMEM;
	struct netlbl_cipsov4_doiwalk_arg *cb_arg = arg;
	void *data;

	data = genlmsg_put(cb_arg->skb, NETLINK_CB(cb_arg->nl_cb->skb).pid,
			   cb_arg->seq, &netlbl_cipsov4_gnl_family,
			   NLM_F_MULTI, NLBL_CIPSOV4_C_LISTALL);
	if (data == NULL)
		goto listall_cb_failure;

	ret_val = nla_put_u32(cb_arg->skb, NLBL_CIPSOV4_A_DOI, doi_def->doi);
	if (ret_val != 0)
		goto listall_cb_failure;
	ret_val = nla_put_u32(cb_arg->skb,
			      NLBL_CIPSOV4_A_MTYPE,
			      doi_def->type);
	if (ret_val != 0)
		goto listall_cb_failure;

	return genlmsg_end(cb_arg->skb, data);

listall_cb_failure:
	genlmsg_cancel(cb_arg->skb, data);
	return ret_val;
}

/**
 * netlbl_cipsov4_listall - Handle a LISTALL message
 * @skb: the NETLINK buffer
 * @cb: the NETLINK callback
 *
 * Description:
 * Process a user generated LISTALL message and respond accordingly.  Returns
 * zero on success and negative values on error.
 *
 */
static int netlbl_cipsov4_listall(struct sk_buff *skb,
				  struct netlink_callback *cb)
{
	struct netlbl_cipsov4_doiwalk_arg cb_arg;
	u32 doi_skip = cb->args[0];

	cb_arg.nl_cb = cb;
	cb_arg.skb = skb;
	cb_arg.seq = cb->nlh->nlmsg_seq;

	cipso_v4_doi_walk(&doi_skip, netlbl_cipsov4_listall_cb, &cb_arg);

	cb->args[0] = doi_skip;
	return skb->len;
}

/**
 * netlbl_cipsov4_remove - Handle a REMOVE message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Process a user generated REMOVE message and respond accordingly.  Returns
 * zero on success, negative values on failure.
 *
 */
static int netlbl_cipsov4_remove(struct sk_buff *skb, struct genl_info *info)
{
	int ret_val = -EINVAL;
	u32 doi = 0;
	struct audit_buffer *audit_buf;
	struct netlbl_audit audit_info;

	if (!info->attrs[NLBL_CIPSOV4_A_DOI])
		return -EINVAL;

	doi = nla_get_u32(info->attrs[NLBL_CIPSOV4_A_DOI]);
	netlbl_netlink_auditinfo(skb, &audit_info);

	ret_val = cipso_v4_doi_remove(doi,
				      &audit_info,
				      netlbl_cipsov4_doi_free);
	if (ret_val == 0)
		atomic_dec(&netlabel_mgmt_protocount);

	audit_buf = netlbl_audit_start_common(AUDIT_MAC_CIPSOV4_DEL,
					      &audit_info);
	if (audit_buf != NULL) {
		audit_log_format(audit_buf,
				 " cipso_doi=%u res=%u",
				 doi,
				 ret_val == 0 ? 1 : 0);
		audit_log_end(audit_buf);
	}

	return ret_val;
}

/*
 * NetLabel Generic NETLINK Command Definitions
 */

static struct genl_ops netlbl_cipsov4_ops[] = {
	{
	.cmd = NLBL_CIPSOV4_C_ADD,
	.flags = GENL_ADMIN_PERM,
	.policy = netlbl_cipsov4_genl_policy,
	.doit = netlbl_cipsov4_add,
	.dumpit = NULL,
	},
	{
	.cmd = NLBL_CIPSOV4_C_REMOVE,
	.flags = GENL_ADMIN_PERM,
	.policy = netlbl_cipsov4_genl_policy,
	.doit = netlbl_cipsov4_remove,
	.dumpit = NULL,
	},
	{
	.cmd = NLBL_CIPSOV4_C_LIST,
	.flags = 0,
	.policy = netlbl_cipsov4_genl_policy,
	.doit = netlbl_cipsov4_list,
	.dumpit = NULL,
	},
	{
	.cmd = NLBL_CIPSOV4_C_LISTALL,
	.flags = 0,
	.policy = netlbl_cipsov4_genl_policy,
	.doit = NULL,
	.dumpit = netlbl_cipsov4_listall,
	},
};

/*
 * NetLabel Generic NETLINK Protocol Functions
 */

/**
 * netlbl_cipsov4_genl_init - Register the CIPSOv4 NetLabel component
 *
 * Description:
 * Register the CIPSOv4 packet NetLabel component with the Generic NETLINK
 * mechanism.  Returns zero on success, negative values on failure.
 *
 */
int __init netlbl_cipsov4_genl_init(void)
{
	int ret_val, i;

	ret_val = genl_register_family(&netlbl_cipsov4_gnl_family);
	if (ret_val != 0)
		return ret_val;

	for (i = 0; i < ARRAY_SIZE(netlbl_cipsov4_ops); i++) {
		ret_val = genl_register_ops(&netlbl_cipsov4_gnl_family,
				&netlbl_cipsov4_ops[i]);
		if (ret_val != 0)
			return ret_val;
	}

	return 0;
}
