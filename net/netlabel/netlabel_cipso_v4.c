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
#include <net/sock.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <net/netlabel.h>
#include <net/cipso_ipv4.h>

#include "netlabel_user.h"
#include "netlabel_cipso_v4.h"

/* NetLabel Generic NETLINK CIPSOv4 family */
static struct genl_family netlbl_cipsov4_gnl_family = {
	.id = GENL_ID_GENERATE,
	.hdrsize = 0,
	.name = NETLBL_NLTYPE_CIPSOV4_NAME,
	.version = NETLBL_PROTO_VERSION,
	.maxattr = 0,
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
static void netlbl_cipsov4_doi_free(struct rcu_head *entry)
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


/*
 * NetLabel Command Handlers
 */

/**
 * netlbl_cipsov4_add_std - Adds a CIPSO V4 DOI definition
 * @doi: the DOI value
 * @msg: the ADD message data
 * @msg_size: the size of the ADD message buffer
 *
 * Description:
 * Create a new CIPSO_V4_MAP_STD DOI definition based on the given ADD message
 * and add it to the CIPSO V4 engine.  Return zero on success and non-zero on
 * error.
 *
 */
static int netlbl_cipsov4_add_std(u32 doi, struct nlattr *msg, size_t msg_size)
{
	int ret_val = -EINVAL;
	int msg_len = msg_size;
	u32 num_tags;
	u32 num_lvls;
	u32 num_cats;
	struct cipso_v4_doi *doi_def = NULL;
	u32 iter;
	u32 tmp_val_a;
	u32 tmp_val_b;

	if (msg_len < NETLBL_LEN_U32)
		goto add_std_failure;
	num_tags = netlbl_getinc_u32(&msg, &msg_len);
	if (num_tags == 0 || num_tags > CIPSO_V4_TAG_MAXCNT)
		goto add_std_failure;

	doi_def = kmalloc(sizeof(*doi_def), GFP_KERNEL);
	if (doi_def == NULL) {
		ret_val = -ENOMEM;
		goto add_std_failure;
	}
	doi_def->map.std = kzalloc(sizeof(*doi_def->map.std), GFP_KERNEL);
	if (doi_def->map.std == NULL) {
		ret_val = -ENOMEM;
		goto add_std_failure;
	}
	doi_def->type = CIPSO_V4_MAP_STD;

	for (iter = 0; iter < num_tags; iter++) {
		if (msg_len < NETLBL_LEN_U8)
			goto add_std_failure;
		doi_def->tags[iter] = netlbl_getinc_u8(&msg, &msg_len);
		switch (doi_def->tags[iter]) {
		case CIPSO_V4_TAG_RBITMAP:
			break;
		default:
			goto add_std_failure;
		}
	}
	if (iter < CIPSO_V4_TAG_MAXCNT)
		doi_def->tags[iter] = CIPSO_V4_TAG_INVALID;

	if (msg_len < 6 * NETLBL_LEN_U32)
		goto add_std_failure;

	num_lvls = netlbl_getinc_u32(&msg, &msg_len);
	if (num_lvls == 0)
		goto add_std_failure;
	doi_def->map.std->lvl.local_size = netlbl_getinc_u32(&msg, &msg_len);
	if (doi_def->map.std->lvl.local_size > CIPSO_V4_MAX_LOC_LVLS)
		goto add_std_failure;
	doi_def->map.std->lvl.local = kcalloc(doi_def->map.std->lvl.local_size,
					      sizeof(u32),
					      GFP_KERNEL);
	if (doi_def->map.std->lvl.local == NULL) {
		ret_val = -ENOMEM;
		goto add_std_failure;
	}
	doi_def->map.std->lvl.cipso_size = netlbl_getinc_u8(&msg, &msg_len);
	if (doi_def->map.std->lvl.cipso_size > CIPSO_V4_MAX_REM_LVLS)
		goto add_std_failure;
	doi_def->map.std->lvl.cipso = kcalloc(doi_def->map.std->lvl.cipso_size,
					      sizeof(u32),
					      GFP_KERNEL);
	if (doi_def->map.std->lvl.cipso == NULL) {
		ret_val = -ENOMEM;
		goto add_std_failure;
	}

	num_cats = netlbl_getinc_u32(&msg, &msg_len);
	doi_def->map.std->cat.local_size = netlbl_getinc_u32(&msg, &msg_len);
	if (doi_def->map.std->cat.local_size > CIPSO_V4_MAX_LOC_CATS)
		goto add_std_failure;
	doi_def->map.std->cat.local = kcalloc(doi_def->map.std->cat.local_size,
					      sizeof(u32),
					      GFP_KERNEL);
	if (doi_def->map.std->cat.local == NULL) {
		ret_val = -ENOMEM;
		goto add_std_failure;
	}
	doi_def->map.std->cat.cipso_size = netlbl_getinc_u16(&msg, &msg_len);
	if (doi_def->map.std->cat.cipso_size > CIPSO_V4_MAX_REM_CATS)
		goto add_std_failure;
	doi_def->map.std->cat.cipso = kcalloc(doi_def->map.std->cat.cipso_size,
					      sizeof(u32),
					      GFP_KERNEL);
	if (doi_def->map.std->cat.cipso == NULL) {
		ret_val = -ENOMEM;
		goto add_std_failure;
	}

	if (msg_len <
	    num_lvls * (NETLBL_LEN_U32 + NETLBL_LEN_U8) +
	    num_cats * (NETLBL_LEN_U32 + NETLBL_LEN_U16))
		goto add_std_failure;

	for (iter = 0; iter < doi_def->map.std->lvl.cipso_size; iter++)
		doi_def->map.std->lvl.cipso[iter] = CIPSO_V4_INV_LVL;
	for (iter = 0; iter < doi_def->map.std->lvl.local_size; iter++)
		doi_def->map.std->lvl.local[iter] = CIPSO_V4_INV_LVL;
	for (iter = 0; iter < doi_def->map.std->cat.cipso_size; iter++)
		doi_def->map.std->cat.cipso[iter] = CIPSO_V4_INV_CAT;
	for (iter = 0; iter < doi_def->map.std->cat.local_size; iter++)
		doi_def->map.std->cat.local[iter] = CIPSO_V4_INV_CAT;

	for (iter = 0; iter < num_lvls; iter++) {
		tmp_val_a = netlbl_getinc_u32(&msg, &msg_len);
		tmp_val_b = netlbl_getinc_u8(&msg, &msg_len);

		if (tmp_val_a >= doi_def->map.std->lvl.local_size ||
		    tmp_val_b >= doi_def->map.std->lvl.cipso_size)
			goto add_std_failure;

		doi_def->map.std->lvl.cipso[tmp_val_b] = tmp_val_a;
		doi_def->map.std->lvl.local[tmp_val_a] = tmp_val_b;
	}

	for (iter = 0; iter < num_cats; iter++) {
		tmp_val_a = netlbl_getinc_u32(&msg, &msg_len);
		tmp_val_b = netlbl_getinc_u16(&msg, &msg_len);

		if (tmp_val_a >= doi_def->map.std->cat.local_size ||
		    tmp_val_b >= doi_def->map.std->cat.cipso_size)
			goto add_std_failure;

		doi_def->map.std->cat.cipso[tmp_val_b] = tmp_val_a;
		doi_def->map.std->cat.local[tmp_val_a] = tmp_val_b;
	}

	doi_def->doi = doi;
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
 * @doi: the DOI value
 * @msg: the ADD message data
 * @msg_size: the size of the ADD message buffer
 *
 * Description:
 * Create a new CIPSO_V4_MAP_PASS DOI definition based on the given ADD message
 * and add it to the CIPSO V4 engine.  Return zero on success and non-zero on
 * error.
 *
 */
static int netlbl_cipsov4_add_pass(u32 doi,
				   struct nlattr *msg,
				   size_t msg_size)
{
	int ret_val = -EINVAL;
	int msg_len = msg_size;
	u32 num_tags;
	struct cipso_v4_doi *doi_def = NULL;
	u32 iter;

	if (msg_len < NETLBL_LEN_U32)
		goto add_pass_failure;
	num_tags = netlbl_getinc_u32(&msg, &msg_len);
	if (num_tags == 0 || num_tags > CIPSO_V4_TAG_MAXCNT)
		goto add_pass_failure;

	doi_def = kmalloc(sizeof(*doi_def), GFP_KERNEL);
	if (doi_def == NULL) {
		ret_val = -ENOMEM;
		goto add_pass_failure;
	}
	doi_def->type = CIPSO_V4_MAP_PASS;

	for (iter = 0; iter < num_tags; iter++) {
		if (msg_len < NETLBL_LEN_U8)
			goto add_pass_failure;
		doi_def->tags[iter] = netlbl_getinc_u8(&msg, &msg_len);
		switch (doi_def->tags[iter]) {
		case CIPSO_V4_TAG_RBITMAP:
			break;
		default:
			goto add_pass_failure;
		}
	}
	if (iter < CIPSO_V4_TAG_MAXCNT)
		doi_def->tags[iter] = CIPSO_V4_TAG_INVALID;

	doi_def->doi = doi;
	ret_val = cipso_v4_doi_add(doi_def);
	if (ret_val != 0)
		goto add_pass_failure;
	return 0;

add_pass_failure:
	if (doi_def)
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
	u32 doi;
	u32 map_type;
	int msg_len = netlbl_netlink_payload_len(skb);
	struct nlattr *msg = netlbl_netlink_payload_data(skb);

	ret_val = netlbl_netlink_cap_check(skb, CAP_NET_ADMIN);
	if (ret_val != 0)
		goto add_return;

	if (msg_len < 2 * NETLBL_LEN_U32)
		goto add_return;

	doi = netlbl_getinc_u32(&msg, &msg_len);
	map_type = netlbl_getinc_u32(&msg, &msg_len);
	switch (map_type) {
	case CIPSO_V4_MAP_STD:
		ret_val = netlbl_cipsov4_add_std(doi, msg, msg_len);
		break;
	case CIPSO_V4_MAP_PASS:
		ret_val = netlbl_cipsov4_add_pass(doi, msg, msg_len);
		break;
	}

add_return:
	netlbl_netlink_send_ack(info,
				netlbl_cipsov4_gnl_family.id,
				NLBL_CIPSOV4_C_ACK,
				-ret_val);
	return ret_val;
}

/**
 * netlbl_cipsov4_list - Handle a LIST message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Process a user generated LIST message and respond accordingly.  Returns
 * zero on success and negative values on error.
 *
 */
static int netlbl_cipsov4_list(struct sk_buff *skb, struct genl_info *info)
{
	int ret_val = -EINVAL;
	u32 doi;
	struct nlattr *msg = netlbl_netlink_payload_data(skb);
	struct sk_buff *ans_skb;

	if (netlbl_netlink_payload_len(skb) != NETLBL_LEN_U32)
		goto list_failure;

	doi = nla_get_u32(msg);
	ans_skb = cipso_v4_doi_dump(doi, NLMSG_SPACE(GENL_HDRLEN));
	if (ans_skb == NULL) {
		ret_val = -ENOMEM;
		goto list_failure;
	}
	netlbl_netlink_hdr_push(ans_skb,
				info->snd_pid,
				0,
				netlbl_cipsov4_gnl_family.id,
				NLBL_CIPSOV4_C_LIST);

	ret_val = netlbl_netlink_snd(ans_skb, info->snd_pid);
	if (ret_val != 0)
		goto list_failure;

	return 0;

list_failure:
	netlbl_netlink_send_ack(info,
				netlbl_cipsov4_gnl_family.id,
				NLBL_CIPSOV4_C_ACK,
				-ret_val);
	return ret_val;
}

/**
 * netlbl_cipsov4_listall - Handle a LISTALL message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Process a user generated LISTALL message and respond accordingly.  Returns
 * zero on success and negative values on error.
 *
 */
static int netlbl_cipsov4_listall(struct sk_buff *skb, struct genl_info *info)
{
	int ret_val = -EINVAL;
	struct sk_buff *ans_skb;

	ans_skb = cipso_v4_doi_dump_all(NLMSG_SPACE(GENL_HDRLEN));
	if (ans_skb == NULL) {
		ret_val = -ENOMEM;
		goto listall_failure;
	}
	netlbl_netlink_hdr_push(ans_skb,
				info->snd_pid,
				0,
				netlbl_cipsov4_gnl_family.id,
				NLBL_CIPSOV4_C_LISTALL);

	ret_val = netlbl_netlink_snd(ans_skb, info->snd_pid);
	if (ret_val != 0)
		goto listall_failure;

	return 0;

listall_failure:
	netlbl_netlink_send_ack(info,
				netlbl_cipsov4_gnl_family.id,
				NLBL_CIPSOV4_C_ACK,
				-ret_val);
	return ret_val;
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
	int ret_val;
	u32 doi;
	struct nlattr *msg = netlbl_netlink_payload_data(skb);

	ret_val = netlbl_netlink_cap_check(skb, CAP_NET_ADMIN);
	if (ret_val != 0)
		goto remove_return;

	if (netlbl_netlink_payload_len(skb) != NETLBL_LEN_U32) {
		ret_val = -EINVAL;
		goto remove_return;
	}

	doi = nla_get_u32(msg);
	ret_val = cipso_v4_doi_remove(doi, netlbl_cipsov4_doi_free);

remove_return:
	netlbl_netlink_send_ack(info,
				netlbl_cipsov4_gnl_family.id,
				NLBL_CIPSOV4_C_ACK,
				-ret_val);
	return ret_val;
}

/*
 * NetLabel Generic NETLINK Command Definitions
 */

static struct genl_ops netlbl_cipsov4_genl_c_add = {
	.cmd = NLBL_CIPSOV4_C_ADD,
	.flags = 0,
	.doit = netlbl_cipsov4_add,
	.dumpit = NULL,
};

static struct genl_ops netlbl_cipsov4_genl_c_remove = {
	.cmd = NLBL_CIPSOV4_C_REMOVE,
	.flags = 0,
	.doit = netlbl_cipsov4_remove,
	.dumpit = NULL,
};

static struct genl_ops netlbl_cipsov4_genl_c_list = {
	.cmd = NLBL_CIPSOV4_C_LIST,
	.flags = 0,
	.doit = netlbl_cipsov4_list,
	.dumpit = NULL,
};

static struct genl_ops netlbl_cipsov4_genl_c_listall = {
	.cmd = NLBL_CIPSOV4_C_LISTALL,
	.flags = 0,
	.doit = netlbl_cipsov4_listall,
	.dumpit = NULL,
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
int netlbl_cipsov4_genl_init(void)
{
	int ret_val;

	ret_val = genl_register_family(&netlbl_cipsov4_gnl_family);
	if (ret_val != 0)
		return ret_val;

	ret_val = genl_register_ops(&netlbl_cipsov4_gnl_family,
				    &netlbl_cipsov4_genl_c_add);
	if (ret_val != 0)
		return ret_val;
	ret_val = genl_register_ops(&netlbl_cipsov4_gnl_family,
				    &netlbl_cipsov4_genl_c_remove);
	if (ret_val != 0)
		return ret_val;
	ret_val = genl_register_ops(&netlbl_cipsov4_gnl_family,
				    &netlbl_cipsov4_genl_c_list);
	if (ret_val != 0)
		return ret_val;
	ret_val = genl_register_ops(&netlbl_cipsov4_gnl_family,
				    &netlbl_cipsov4_genl_c_listall);
	if (ret_val != 0)
		return ret_val;

	return 0;
}
