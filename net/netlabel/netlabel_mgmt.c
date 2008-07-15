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
#include <asm/atomic.h>

#include "netlabel_domainhash.h"
#include "netlabel_user.h"
#include "netlabel_mgmt.h"

/* NetLabel configured protocol counter */
atomic_t netlabel_mgmt_protocount = ATOMIC_INIT(0);

/* Argument struct for netlbl_domhsh_walk() */
struct netlbl_domhsh_walk_arg {
	struct netlink_callback *nl_cb;
	struct sk_buff *skb;
	u32 seq;
};

/* NetLabel Generic NETLINK CIPSOv4 family */
static struct genl_family netlbl_mgmt_gnl_family = {
	.id = GENL_ID_GENERATE,
	.hdrsize = 0,
	.name = NETLBL_NLTYPE_MGMT_NAME,
	.version = NETLBL_PROTO_VERSION,
	.maxattr = NLBL_MGMT_A_MAX,
};

/* NetLabel Netlink attribute policy */
static const struct nla_policy netlbl_mgmt_genl_policy[NLBL_MGMT_A_MAX + 1] = {
	[NLBL_MGMT_A_DOMAIN] = { .type = NLA_NUL_STRING },
	[NLBL_MGMT_A_PROTOCOL] = { .type = NLA_U32 },
	[NLBL_MGMT_A_VERSION] = { .type = NLA_U32 },
	[NLBL_MGMT_A_CV4DOI] = { .type = NLA_U32 },
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
	struct netlbl_dom_map *entry = NULL;
	size_t tmp_size;
	u32 tmp_val;
	struct netlbl_audit audit_info;

	if (!info->attrs[NLBL_MGMT_A_DOMAIN] ||
	    !info->attrs[NLBL_MGMT_A_PROTOCOL])
		goto add_failure;

	netlbl_netlink_auditinfo(skb, &audit_info);

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (entry == NULL) {
		ret_val = -ENOMEM;
		goto add_failure;
	}
	tmp_size = nla_len(info->attrs[NLBL_MGMT_A_DOMAIN]);
	entry->domain = kmalloc(tmp_size, GFP_KERNEL);
	if (entry->domain == NULL) {
		ret_val = -ENOMEM;
		goto add_failure;
	}
	entry->type = nla_get_u32(info->attrs[NLBL_MGMT_A_PROTOCOL]);
	nla_strlcpy(entry->domain, info->attrs[NLBL_MGMT_A_DOMAIN], tmp_size);

	switch (entry->type) {
	case NETLBL_NLTYPE_UNLABELED:
		ret_val = netlbl_domhsh_add(entry, &audit_info);
		break;
	case NETLBL_NLTYPE_CIPSOV4:
		if (!info->attrs[NLBL_MGMT_A_CV4DOI])
			goto add_failure;

		tmp_val = nla_get_u32(info->attrs[NLBL_MGMT_A_CV4DOI]);
		/* We should be holding a rcu_read_lock() here while we hold
		 * the result but since the entry will always be deleted when
		 * the CIPSO DOI is deleted we aren't going to keep the
		 * lock. */
		rcu_read_lock();
		entry->type_def.cipsov4 = cipso_v4_doi_getdef(tmp_val);
		if (entry->type_def.cipsov4 == NULL) {
			rcu_read_unlock();
			goto add_failure;
		}
		ret_val = netlbl_domhsh_add(entry, &audit_info);
		rcu_read_unlock();
		break;
	default:
		goto add_failure;
	}
	if (ret_val != 0)
		goto add_failure;

	return 0;

add_failure:
	if (entry)
		kfree(entry->domain);
	kfree(entry);
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
	char *domain;
	struct netlbl_audit audit_info;

	if (!info->attrs[NLBL_MGMT_A_DOMAIN])
		return -EINVAL;

	netlbl_netlink_auditinfo(skb, &audit_info);

	domain = nla_data(info->attrs[NLBL_MGMT_A_DOMAIN]);
	return netlbl_domhsh_remove(domain, &audit_info);
}

/**
 * netlbl_mgmt_listall_cb - netlbl_domhsh_walk() callback for LISTALL
 * @entry: the domain mapping hash table entry
 * @arg: the netlbl_domhsh_walk_arg structure
 *
 * Description:
 * This function is designed to be used as a callback to the
 * netlbl_domhsh_walk() function for use in generating a response for a LISTALL
 * message.  Returns the size of the message on success, negative values on
 * failure.
 *
 */
static int netlbl_mgmt_listall_cb(struct netlbl_dom_map *entry, void *arg)
{
	int ret_val = -ENOMEM;
	struct netlbl_domhsh_walk_arg *cb_arg = arg;
	void *data;

	data = genlmsg_put(cb_arg->skb, NETLINK_CB(cb_arg->nl_cb->skb).pid,
			   cb_arg->seq, &netlbl_mgmt_gnl_family,
			   NLM_F_MULTI, NLBL_MGMT_C_LISTALL);
	if (data == NULL)
		goto listall_cb_failure;

	ret_val = nla_put_string(cb_arg->skb,
				 NLBL_MGMT_A_DOMAIN,
				 entry->domain);
	if (ret_val != 0)
		goto listall_cb_failure;
	ret_val = nla_put_u32(cb_arg->skb, NLBL_MGMT_A_PROTOCOL, entry->type);
	if (ret_val != 0)
		goto listall_cb_failure;
	switch (entry->type) {
	case NETLBL_NLTYPE_CIPSOV4:
		ret_val = nla_put_u32(cb_arg->skb,
				      NLBL_MGMT_A_CV4DOI,
				      entry->type_def.cipsov4->doi);
		if (ret_val != 0)
			goto listall_cb_failure;
		break;
	}

	cb_arg->seq++;
	return genlmsg_end(cb_arg->skb, data);

listall_cb_failure:
	genlmsg_cancel(cb_arg->skb, data);
	return ret_val;
}

/**
 * netlbl_mgmt_listall - Handle a LISTALL message
 * @skb: the NETLINK buffer
 * @cb: the NETLINK callback
 *
 * Description:
 * Process a user generated LISTALL message and dumps the domain hash table in
 * a form suitable for use in a kernel generated LISTALL message.  Returns zero
 * on success, negative values on failure.
 *
 */
static int netlbl_mgmt_listall(struct sk_buff *skb,
			       struct netlink_callback *cb)
{
	struct netlbl_domhsh_walk_arg cb_arg;
	u32 skip_bkt = cb->args[0];
	u32 skip_chain = cb->args[1];

	cb_arg.nl_cb = cb;
	cb_arg.skb = skb;
	cb_arg.seq = cb->nlh->nlmsg_seq;

	netlbl_domhsh_walk(&skip_bkt,
			   &skip_chain,
			   netlbl_mgmt_listall_cb,
			   &cb_arg);

	cb->args[0] = skip_bkt;
	cb->args[1] = skip_chain;
	return skb->len;
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
	struct netlbl_dom_map *entry = NULL;
	u32 tmp_val;
	struct netlbl_audit audit_info;

	if (!info->attrs[NLBL_MGMT_A_PROTOCOL])
		goto adddef_failure;

	netlbl_netlink_auditinfo(skb, &audit_info);

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (entry == NULL) {
		ret_val = -ENOMEM;
		goto adddef_failure;
	}
	entry->type = nla_get_u32(info->attrs[NLBL_MGMT_A_PROTOCOL]);

	switch (entry->type) {
	case NETLBL_NLTYPE_UNLABELED:
		ret_val = netlbl_domhsh_add_default(entry, &audit_info);
		break;
	case NETLBL_NLTYPE_CIPSOV4:
		if (!info->attrs[NLBL_MGMT_A_CV4DOI])
			goto adddef_failure;

		tmp_val = nla_get_u32(info->attrs[NLBL_MGMT_A_CV4DOI]);
		/* We should be holding a rcu_read_lock() here while we hold
		 * the result but since the entry will always be deleted when
		 * the CIPSO DOI is deleted we aren't going to keep the
		 * lock. */
		rcu_read_lock();
		entry->type_def.cipsov4 = cipso_v4_doi_getdef(tmp_val);
		if (entry->type_def.cipsov4 == NULL) {
			rcu_read_unlock();
			goto adddef_failure;
		}
		ret_val = netlbl_domhsh_add_default(entry, &audit_info);
		rcu_read_unlock();
		break;
	default:
		goto adddef_failure;
	}
	if (ret_val != 0)
		goto adddef_failure;

	return 0;

adddef_failure:
	kfree(entry);
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
	struct netlbl_audit audit_info;

	netlbl_netlink_auditinfo(skb, &audit_info);

	return netlbl_domhsh_remove_default(&audit_info);
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
	struct sk_buff *ans_skb = NULL;
	void *data;
	struct netlbl_dom_map *entry;

	ans_skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (ans_skb == NULL)
		return -ENOMEM;
	data = genlmsg_put_reply(ans_skb, info, &netlbl_mgmt_gnl_family,
				 0, NLBL_MGMT_C_LISTDEF);
	if (data == NULL)
		goto listdef_failure;

	rcu_read_lock();
	entry = netlbl_domhsh_getentry(NULL);
	if (entry == NULL) {
		ret_val = -ENOENT;
		goto listdef_failure_lock;
	}
	ret_val = nla_put_u32(ans_skb, NLBL_MGMT_A_PROTOCOL, entry->type);
	if (ret_val != 0)
		goto listdef_failure_lock;
	switch (entry->type) {
	case NETLBL_NLTYPE_CIPSOV4:
		ret_val = nla_put_u32(ans_skb,
				      NLBL_MGMT_A_CV4DOI,
				      entry->type_def.cipsov4->doi);
		if (ret_val != 0)
			goto listdef_failure_lock;
		break;
	}
	rcu_read_unlock();

	genlmsg_end(ans_skb, data);
	return genlmsg_reply(ans_skb, info);

listdef_failure_lock:
	rcu_read_unlock();
listdef_failure:
	kfree_skb(ans_skb);
	return ret_val;
}

/**
 * netlbl_mgmt_protocols_cb - Write an individual PROTOCOL message response
 * @skb: the skb to write to
 * @seq: the NETLINK sequence number
 * @cb: the NETLINK callback
 * @protocol: the NetLabel protocol to use in the message
 *
 * Description:
 * This function is to be used in conjunction with netlbl_mgmt_protocols() to
 * answer a application's PROTOCOLS message.  Returns the size of the message
 * on success, negative values on failure.
 *
 */
static int netlbl_mgmt_protocols_cb(struct sk_buff *skb,
				    struct netlink_callback *cb,
				    u32 protocol)
{
	int ret_val = -ENOMEM;
	void *data;

	data = genlmsg_put(skb, NETLINK_CB(cb->skb).pid, cb->nlh->nlmsg_seq,
			   &netlbl_mgmt_gnl_family, NLM_F_MULTI,
			   NLBL_MGMT_C_PROTOCOLS);
	if (data == NULL)
		goto protocols_cb_failure;

	ret_val = nla_put_u32(skb, NLBL_MGMT_A_PROTOCOL, protocol);
	if (ret_val != 0)
		goto protocols_cb_failure;

	return genlmsg_end(skb, data);

protocols_cb_failure:
	genlmsg_cancel(skb, data);
	return ret_val;
}

/**
 * netlbl_mgmt_protocols - Handle a PROTOCOLS message
 * @skb: the NETLINK buffer
 * @cb: the NETLINK callback
 *
 * Description:
 * Process a user generated PROTOCOLS message and respond accordingly.
 *
 */
static int netlbl_mgmt_protocols(struct sk_buff *skb,
				 struct netlink_callback *cb)
{
	u32 protos_sent = cb->args[0];

	if (protos_sent == 0) {
		if (netlbl_mgmt_protocols_cb(skb,
					     cb,
					     NETLBL_NLTYPE_UNLABELED) < 0)
			goto protocols_return;
		protos_sent++;
	}
	if (protos_sent == 1) {
		if (netlbl_mgmt_protocols_cb(skb,
					     cb,
					     NETLBL_NLTYPE_CIPSOV4) < 0)
			goto protocols_return;
		protos_sent++;
	}

protocols_return:
	cb->args[0] = protos_sent;
	return skb->len;
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
	void *data;

	ans_skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (ans_skb == NULL)
		return -ENOMEM;
	data = genlmsg_put_reply(ans_skb, info, &netlbl_mgmt_gnl_family,
				 0, NLBL_MGMT_C_VERSION);
	if (data == NULL)
		goto version_failure;

	ret_val = nla_put_u32(ans_skb,
			      NLBL_MGMT_A_VERSION,
			      NETLBL_PROTO_VERSION);
	if (ret_val != 0)
		goto version_failure;

	genlmsg_end(ans_skb, data);
	return genlmsg_reply(ans_skb, info);

version_failure:
	kfree_skb(ans_skb);
	return ret_val;
}


/*
 * NetLabel Generic NETLINK Command Definitions
 */

static struct genl_ops netlbl_mgmt_genl_ops[] = {
	{
	.cmd = NLBL_MGMT_C_ADD,
	.flags = GENL_ADMIN_PERM,
	.policy = netlbl_mgmt_genl_policy,
	.doit = netlbl_mgmt_add,
	.dumpit = NULL,
	},
	{
	.cmd = NLBL_MGMT_C_REMOVE,
	.flags = GENL_ADMIN_PERM,
	.policy = netlbl_mgmt_genl_policy,
	.doit = netlbl_mgmt_remove,
	.dumpit = NULL,
	},
	{
	.cmd = NLBL_MGMT_C_LISTALL,
	.flags = 0,
	.policy = netlbl_mgmt_genl_policy,
	.doit = NULL,
	.dumpit = netlbl_mgmt_listall,
	},
	{
	.cmd = NLBL_MGMT_C_ADDDEF,
	.flags = GENL_ADMIN_PERM,
	.policy = netlbl_mgmt_genl_policy,
	.doit = netlbl_mgmt_adddef,
	.dumpit = NULL,
	},
	{
	.cmd = NLBL_MGMT_C_REMOVEDEF,
	.flags = GENL_ADMIN_PERM,
	.policy = netlbl_mgmt_genl_policy,
	.doit = netlbl_mgmt_removedef,
	.dumpit = NULL,
	},
	{
	.cmd = NLBL_MGMT_C_LISTDEF,
	.flags = 0,
	.policy = netlbl_mgmt_genl_policy,
	.doit = netlbl_mgmt_listdef,
	.dumpit = NULL,
	},
	{
	.cmd = NLBL_MGMT_C_PROTOCOLS,
	.flags = 0,
	.policy = netlbl_mgmt_genl_policy,
	.doit = NULL,
	.dumpit = netlbl_mgmt_protocols,
	},
	{
	.cmd = NLBL_MGMT_C_VERSION,
	.flags = 0,
	.policy = netlbl_mgmt_genl_policy,
	.doit = netlbl_mgmt_version,
	.dumpit = NULL,
	},
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
int __init netlbl_mgmt_genl_init(void)
{
	int ret_val, i;

	ret_val = genl_register_family(&netlbl_mgmt_gnl_family);
	if (ret_val != 0)
		return ret_val;

	for (i = 0; i < ARRAY_SIZE(netlbl_mgmt_genl_ops); i++) {
		ret_val = genl_register_ops(&netlbl_mgmt_gnl_family,
				&netlbl_mgmt_genl_ops[i]);
		if (ret_val != 0)
			return ret_val;
	}

	return 0;
}
