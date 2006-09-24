/*
 * NetLabel Management Support
 *
 * This file defines the management functions for the NetLabel system.  The
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

#include "netlabel_domainhash.h"
#include "netlabel_user.h"
#include "netlabel_mgmt.h"

/* NetLabel Generic NETLINK CIPSOv4 family */
static struct genl_family netlbl_mgmt_gnl_family = {
	.id = GENL_ID_GENERATE,
	.hdrsize = 0,
	.name = NETLBL_NLTYPE_MGMT_NAME,
	.version = NETLBL_PROTO_VERSION,
	.maxattr = 0,
};


/*
 * NetLabel Command Handlers
 */

/**
 * netlbl_mgmt_add - Handle an ADD message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Process a user generated ADD message and add the domains from the message
 * to the hash table.  See netlabel.h for a description of the message format.
 * Returns zero on success, negative values on failure.
 *
 */
static int netlbl_mgmt_add(struct sk_buff *skb, struct genl_info *info)
{
	int ret_val = -EINVAL;
	struct nlattr *msg_ptr = netlbl_netlink_payload_data(skb);
	int msg_len = netlbl_netlink_payload_len(skb);
	u32 count;
	struct netlbl_dom_map *entry = NULL;
	u32 iter;
	u32 tmp_val;
	int tmp_size;

	ret_val = netlbl_netlink_cap_check(skb, CAP_NET_ADMIN);
	if (ret_val != 0)
		goto add_failure;

	if (msg_len < NETLBL_LEN_U32)
		goto add_failure;
	count = netlbl_getinc_u32(&msg_ptr, &msg_len);

	for (iter = 0; iter < count && msg_len > 0; iter++, entry = NULL) {
		if (msg_len <= 0) {
			ret_val = -EINVAL;
			goto add_failure;
		}
		entry = kzalloc(sizeof(*entry), GFP_KERNEL);
		if (entry == NULL) {
			ret_val = -ENOMEM;
			goto add_failure;
		}
		tmp_size = nla_len(msg_ptr);
		if (tmp_size <= 0 || tmp_size > msg_len) {
			ret_val = -EINVAL;
			goto add_failure;
		}
		entry->domain = kmalloc(tmp_size, GFP_KERNEL);
		if (entry->domain == NULL) {
			ret_val = -ENOMEM;
			goto add_failure;
		}
		nla_strlcpy(entry->domain, msg_ptr, tmp_size);
		entry->domain[tmp_size - 1] = '\0';
		msg_ptr = nla_next(msg_ptr, &msg_len);

		if (msg_len < NETLBL_LEN_U32) {
			ret_val = -EINVAL;
			goto add_failure;
		}
		tmp_val = netlbl_getinc_u32(&msg_ptr, &msg_len);
		entry->type = tmp_val;
		switch (tmp_val) {
		case NETLBL_NLTYPE_UNLABELED:
			ret_val = netlbl_domhsh_add(entry);
			break;
		case NETLBL_NLTYPE_CIPSOV4:
			if (msg_len < NETLBL_LEN_U32) {
				ret_val = -EINVAL;
				goto add_failure;
			}
			tmp_val = netlbl_getinc_u32(&msg_ptr, &msg_len);
			/* We should be holding a rcu_read_lock() here
			 * while we hold the result but since the entry
			 * will always be deleted when the CIPSO DOI
			 * is deleted we aren't going to keep the lock. */
			rcu_read_lock();
			entry->type_def.cipsov4 = cipso_v4_doi_getdef(tmp_val);
			if (entry->type_def.cipsov4 == NULL) {
				rcu_read_unlock();
				ret_val = -EINVAL;
				goto add_failure;
			}
			ret_val = netlbl_domhsh_add(entry);
			rcu_read_unlock();
			break;
		default:
			ret_val = -EINVAL;
		}
		if (ret_val != 0)
			goto add_failure;
	}

	netlbl_netlink_send_ack(info,
				netlbl_mgmt_gnl_family.id,
				NLBL_MGMT_C_ACK,
				NETLBL_E_OK);
	return 0;

add_failure:
	if (entry)
		kfree(entry->domain);
	kfree(entry);
	netlbl_netlink_send_ack(info,
				netlbl_mgmt_gnl_family.id,
				NLBL_MGMT_C_ACK,
				-ret_val);
	return ret_val;
}

/**
 * netlbl_mgmt_remove - Handle a REMOVE message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Process a user generated REMOVE message and remove the specified domain
 * mappings.  Returns zero on success, negative values on failure.
 *
 */
static int netlbl_mgmt_remove(struct sk_buff *skb, struct genl_info *info)
{
	int ret_val = -EINVAL;
	struct nlattr *msg_ptr = netlbl_netlink_payload_data(skb);
	int msg_len = netlbl_netlink_payload_len(skb);
	u32 count;
	u32 iter;
	int tmp_size;
	unsigned char *domain;

	ret_val = netlbl_netlink_cap_check(skb, CAP_NET_ADMIN);
	if (ret_val != 0)
		goto remove_return;

	if (msg_len < NETLBL_LEN_U32)
		goto remove_return;
	count = netlbl_getinc_u32(&msg_ptr, &msg_len);

	for (iter = 0; iter < count && msg_len > 0; iter++) {
		if (msg_len <= 0) {
			ret_val = -EINVAL;
			goto remove_return;
		}
		tmp_size = nla_len(msg_ptr);
		domain = nla_data(msg_ptr);
		if (tmp_size <= 0 || tmp_size > msg_len ||
		    domain[tmp_size - 1] != '\0') {
			ret_val = -EINVAL;
			goto remove_return;
		}
		ret_val = netlbl_domhsh_remove(domain);
		if (ret_val != 0)
			goto remove_return;
		msg_ptr = nla_next(msg_ptr, &msg_len);
	}

	ret_val = 0;

remove_return:
	netlbl_netlink_send_ack(info,
				netlbl_mgmt_gnl_family.id,
				NLBL_MGMT_C_ACK,
				-ret_val);
	return ret_val;
}

/**
 * netlbl_mgmt_list - Handle a LIST message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Process a user generated LIST message and dumps the domain hash table in a
 * form suitable for use in a kernel generated LIST message.  Returns zero on
 * success, negative values on failure.
 *
 */
static int netlbl_mgmt_list(struct sk_buff *skb, struct genl_info *info)
{
	int ret_val = -ENOMEM;
	struct sk_buff *ans_skb;

	ans_skb = netlbl_domhsh_dump(NLMSG_SPACE(GENL_HDRLEN));
	if (ans_skb == NULL)
		goto list_failure;
	netlbl_netlink_hdr_push(ans_skb,
				info->snd_pid,
				0,
				netlbl_mgmt_gnl_family.id,
				NLBL_MGMT_C_LIST);

	ret_val = netlbl_netlink_snd(ans_skb, info->snd_pid);
	if (ret_val != 0)
		goto list_failure;

	return 0;

list_failure:
	netlbl_netlink_send_ack(info,
				netlbl_mgmt_gnl_family.id,
				NLBL_MGMT_C_ACK,
				-ret_val);
	return ret_val;
}

/**
 * netlbl_mgmt_adddef - Handle an ADDDEF message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Process a user generated ADDDEF message and respond accordingly.  Returns
 * zero on success, negative values on failure.
 *
 */
static int netlbl_mgmt_adddef(struct sk_buff *skb, struct genl_info *info)
{
	int ret_val = -EINVAL;
	struct nlattr *msg_ptr = netlbl_netlink_payload_data(skb);
	int msg_len = netlbl_netlink_payload_len(skb);
	struct netlbl_dom_map *entry = NULL;
	u32 tmp_val;

	ret_val = netlbl_netlink_cap_check(skb, CAP_NET_ADMIN);
	if (ret_val != 0)
		goto adddef_failure;

	if (msg_len < NETLBL_LEN_U32)
		goto adddef_failure;
	tmp_val = netlbl_getinc_u32(&msg_ptr, &msg_len);

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (entry == NULL) {
		ret_val = -ENOMEM;
		goto adddef_failure;
	}

	entry->type = tmp_val;
	switch (entry->type) {
	case NETLBL_NLTYPE_UNLABELED:
		ret_val = netlbl_domhsh_add_default(entry);
		break;
	case NETLBL_NLTYPE_CIPSOV4:
		if (msg_len < NETLBL_LEN_U32) {
			ret_val = -EINVAL;
			goto adddef_failure;
		}
		tmp_val = netlbl_getinc_u32(&msg_ptr, &msg_len);
		/* We should be holding a rcu_read_lock here while we
		 * hold the result but since the entry will always be
		 * deleted when the CIPSO DOI is deleted we are going
		 * to skip the lock. */
		rcu_read_lock();
		entry->type_def.cipsov4 = cipso_v4_doi_getdef(tmp_val);
		if (entry->type_def.cipsov4 == NULL) {
			rcu_read_unlock();
			ret_val = -EINVAL;
			goto adddef_failure;
		}
		ret_val = netlbl_domhsh_add_default(entry);
		rcu_read_unlock();
		break;
	default:
		ret_val = -EINVAL;
	}
	if (ret_val != 0)
		goto adddef_failure;

	netlbl_netlink_send_ack(info,
				netlbl_mgmt_gnl_family.id,
				NLBL_MGMT_C_ACK,
				NETLBL_E_OK);
	return 0;

adddef_failure:
	kfree(entry);
	netlbl_netlink_send_ack(info,
				netlbl_mgmt_gnl_family.id,
				NLBL_MGMT_C_ACK,
				-ret_val);
	return ret_val;
}

/**
 * netlbl_mgmt_removedef - Handle a REMOVEDEF message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Process a user generated REMOVEDEF message and remove the default domain
 * mapping.  Returns zero on success, negative values on failure.
 *
 */
static int netlbl_mgmt_removedef(struct sk_buff *skb, struct genl_info *info)
{
	int ret_val;

	ret_val = netlbl_netlink_cap_check(skb, CAP_NET_ADMIN);
	if (ret_val != 0)
		goto removedef_return;

	ret_val = netlbl_domhsh_remove_default();

removedef_return:
	netlbl_netlink_send_ack(info,
				netlbl_mgmt_gnl_family.id,
				NLBL_MGMT_C_ACK,
				-ret_val);
	return ret_val;
}

/**
 * netlbl_mgmt_listdef - Handle a LISTDEF message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Process a user generated LISTDEF message and dumps the default domain
 * mapping in a form suitable for use in a kernel generated LISTDEF message.
 * Returns zero on success, negative values on failure.
 *
 */
static int netlbl_mgmt_listdef(struct sk_buff *skb, struct genl_info *info)
{
	int ret_val = -ENOMEM;
	struct sk_buff *ans_skb;

	ans_skb = netlbl_domhsh_dump_default(NLMSG_SPACE(GENL_HDRLEN));
	if (ans_skb == NULL)
		goto listdef_failure;
	netlbl_netlink_hdr_push(ans_skb,
				info->snd_pid,
				0,
				netlbl_mgmt_gnl_family.id,
				NLBL_MGMT_C_LISTDEF);

	ret_val = netlbl_netlink_snd(ans_skb, info->snd_pid);
	if (ret_val != 0)
		goto listdef_failure;

	return 0;

listdef_failure:
	netlbl_netlink_send_ack(info,
				netlbl_mgmt_gnl_family.id,
				NLBL_MGMT_C_ACK,
				-ret_val);
	return ret_val;
}

/**
 * netlbl_mgmt_modules - Handle a MODULES message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Process a user generated MODULES message and respond accordingly.
 *
 */
static int netlbl_mgmt_modules(struct sk_buff *skb, struct genl_info *info)
{
	int ret_val = -ENOMEM;
	size_t data_size;
	u32 mod_count;
	struct sk_buff *ans_skb = NULL;

	/* unlabeled + cipsov4 */
	mod_count = 2;

	data_size = GENL_HDRLEN + NETLBL_LEN_U32 + mod_count * NETLBL_LEN_U32;
	ans_skb = netlbl_netlink_alloc_skb(0, data_size, GFP_KERNEL);
	if (ans_skb == NULL)
		goto modules_failure;

	if (netlbl_netlink_hdr_put(ans_skb,
				   info->snd_pid,
				   0,
				   netlbl_mgmt_gnl_family.id,
				   NLBL_MGMT_C_MODULES) == NULL)
		goto modules_failure;

	ret_val = nla_put_u32(ans_skb, NLA_U32, mod_count);
	if (ret_val != 0)
		goto modules_failure;
	ret_val = nla_put_u32(ans_skb, NLA_U32, NETLBL_NLTYPE_UNLABELED);
	if (ret_val != 0)
		goto modules_failure;
	ret_val = nla_put_u32(ans_skb, NLA_U32, NETLBL_NLTYPE_CIPSOV4);
	if (ret_val != 0)
		goto modules_failure;

	ret_val = netlbl_netlink_snd(ans_skb, info->snd_pid);
	if (ret_val != 0)
		goto modules_failure;

	return 0;

modules_failure:
	kfree_skb(ans_skb);
	netlbl_netlink_send_ack(info,
				netlbl_mgmt_gnl_family.id,
				NLBL_MGMT_C_ACK,
				-ret_val);
	return ret_val;
}

/**
 * netlbl_mgmt_version - Handle a VERSION message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Process a user generated VERSION message and respond accordingly.  Returns
 * zero on success, negative values on failure.
 *
 */
static int netlbl_mgmt_version(struct sk_buff *skb, struct genl_info *info)
{
	int ret_val = -ENOMEM;
	struct sk_buff *ans_skb = NULL;

	ans_skb = netlbl_netlink_alloc_skb(0,
					   GENL_HDRLEN + NETLBL_LEN_U32,
					   GFP_KERNEL);
	if (ans_skb == NULL)
		goto version_failure;
	if (netlbl_netlink_hdr_put(ans_skb,
				   info->snd_pid,
				   0,
				   netlbl_mgmt_gnl_family.id,
				   NLBL_MGMT_C_VERSION) == NULL)
		goto version_failure;

	ret_val = nla_put_u32(ans_skb, NLA_U32, NETLBL_PROTO_VERSION);
	if (ret_val != 0)
		goto version_failure;

	ret_val = netlbl_netlink_snd(ans_skb, info->snd_pid);
	if (ret_val != 0)
		goto version_failure;

	return 0;

version_failure:
	kfree_skb(ans_skb);
	netlbl_netlink_send_ack(info,
				netlbl_mgmt_gnl_family.id,
				NLBL_MGMT_C_ACK,
				-ret_val);
	return ret_val;
}


/*
 * NetLabel Generic NETLINK Command Definitions
 */

static struct genl_ops netlbl_mgmt_genl_c_add = {
	.cmd = NLBL_MGMT_C_ADD,
	.flags = 0,
	.doit = netlbl_mgmt_add,
	.dumpit = NULL,
};

static struct genl_ops netlbl_mgmt_genl_c_remove = {
	.cmd = NLBL_MGMT_C_REMOVE,
	.flags = 0,
	.doit = netlbl_mgmt_remove,
	.dumpit = NULL,
};

static struct genl_ops netlbl_mgmt_genl_c_list = {
	.cmd = NLBL_MGMT_C_LIST,
	.flags = 0,
	.doit = netlbl_mgmt_list,
	.dumpit = NULL,
};

static struct genl_ops netlbl_mgmt_genl_c_adddef = {
	.cmd = NLBL_MGMT_C_ADDDEF,
	.flags = 0,
	.doit = netlbl_mgmt_adddef,
	.dumpit = NULL,
};

static struct genl_ops netlbl_mgmt_genl_c_removedef = {
	.cmd = NLBL_MGMT_C_REMOVEDEF,
	.flags = 0,
	.doit = netlbl_mgmt_removedef,
	.dumpit = NULL,
};

static struct genl_ops netlbl_mgmt_genl_c_listdef = {
	.cmd = NLBL_MGMT_C_LISTDEF,
	.flags = 0,
	.doit = netlbl_mgmt_listdef,
	.dumpit = NULL,
};

static struct genl_ops netlbl_mgmt_genl_c_modules = {
	.cmd = NLBL_MGMT_C_MODULES,
	.flags = 0,
	.doit = netlbl_mgmt_modules,
	.dumpit = NULL,
};

static struct genl_ops netlbl_mgmt_genl_c_version = {
	.cmd = NLBL_MGMT_C_VERSION,
	.flags = 0,
	.doit = netlbl_mgmt_version,
	.dumpit = NULL,
};

/*
 * NetLabel Generic NETLINK Protocol Functions
 */

/**
 * netlbl_mgmt_genl_init - Register the NetLabel management component
 *
 * Description:
 * Register the NetLabel management component with the Generic NETLINK
 * mechanism.  Returns zero on success, negative values on failure.
 *
 */
int netlbl_mgmt_genl_init(void)
{
	int ret_val;

	ret_val = genl_register_family(&netlbl_mgmt_gnl_family);
	if (ret_val != 0)
		return ret_val;

	ret_val = genl_register_ops(&netlbl_mgmt_gnl_family,
				    &netlbl_mgmt_genl_c_add);
	if (ret_val != 0)
		return ret_val;
	ret_val = genl_register_ops(&netlbl_mgmt_gnl_family,
				    &netlbl_mgmt_genl_c_remove);
	if (ret_val != 0)
		return ret_val;
	ret_val = genl_register_ops(&netlbl_mgmt_gnl_family,
				    &netlbl_mgmt_genl_c_list);
	if (ret_val != 0)
		return ret_val;
	ret_val = genl_register_ops(&netlbl_mgmt_gnl_family,
				    &netlbl_mgmt_genl_c_adddef);
	if (ret_val != 0)
		return ret_val;
	ret_val = genl_register_ops(&netlbl_mgmt_gnl_family,
				    &netlbl_mgmt_genl_c_removedef);
	if (ret_val != 0)
		return ret_val;
	ret_val = genl_register_ops(&netlbl_mgmt_gnl_family,
				    &netlbl_mgmt_genl_c_listdef);
	if (ret_val != 0)
		return ret_val;
	ret_val = genl_register_ops(&netlbl_mgmt_gnl_family,
				    &netlbl_mgmt_genl_c_modules);
	if (ret_val != 0)
		return ret_val;
	ret_val = genl_register_ops(&netlbl_mgmt_gnl_family,
				    &netlbl_mgmt_genl_c_version);
	if (ret_val != 0)
		return ret_val;

	return 0;
}
