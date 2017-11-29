/*
 * NetLabel CALIPSO/IPv6 Support
 *
 * This file defines the CALIPSO/IPv6 functions for the NetLabel system.  The
 * NetLabel system manages static and dynamic label mappings for network
 * protocols such as CIPSO and CALIPSO.
 *
 * Authors: Paul Moore <paul@paul-moore.com>
 *          Huw Davies <huw@codeweavers.com>
 *
 */

/* (c) Copyright Hewlett-Packard Development Company, L.P., 2006
 * (c) Copyright Huw Davies <huw@codeweavers.com>, 2015
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
 * along with this program;  if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/audit.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <net/netlabel.h>
#include <net/calipso.h>
#include <linux/atomic.h>

#include "netlabel_user.h"
#include "netlabel_calipso.h"
#include "netlabel_mgmt.h"
#include "netlabel_domainhash.h"

/* Argument struct for calipso_doi_walk() */
struct netlbl_calipso_doiwalk_arg {
	struct netlink_callback *nl_cb;
	struct sk_buff *skb;
	u32 seq;
};

/* Argument struct for netlbl_domhsh_walk() */
struct netlbl_domhsh_walk_arg {
	struct netlbl_audit *audit_info;
	u32 doi;
};

/* NetLabel Generic NETLINK CALIPSO family */
static struct genl_family netlbl_calipso_gnl_family;

/* NetLabel Netlink attribute policy */
static const struct nla_policy calipso_genl_policy[NLBL_CALIPSO_A_MAX + 1] = {
	[NLBL_CALIPSO_A_DOI] = { .type = NLA_U32 },
	[NLBL_CALIPSO_A_MTYPE] = { .type = NLA_U32 },
};

/* NetLabel Command Handlers
 */
/**
 * netlbl_calipso_add_pass - Adds a CALIPSO pass DOI definition
 * @info: the Generic NETLINK info block
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Create a new CALIPSO_MAP_PASS DOI definition based on the given ADD message
 * and add it to the CALIPSO engine.  Return zero on success and non-zero on
 * error.
 *
 */
static int netlbl_calipso_add_pass(struct genl_info *info,
				   struct netlbl_audit *audit_info)
{
	int ret_val;
	struct calipso_doi *doi_def = NULL;

	doi_def = kmalloc(sizeof(*doi_def), GFP_KERNEL);
	if (!doi_def)
		return -ENOMEM;
	doi_def->type = CALIPSO_MAP_PASS;
	doi_def->doi = nla_get_u32(info->attrs[NLBL_CALIPSO_A_DOI]);
	ret_val = calipso_doi_add(doi_def, audit_info);
	if (ret_val != 0)
		calipso_doi_free(doi_def);

	return ret_val;
}

/**
 * netlbl_calipso_add - Handle an ADD message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Create a new DOI definition based on the given ADD message and add it to the
 * CALIPSO engine.  Returns zero on success, negative values on failure.
 *
 */
static int netlbl_calipso_add(struct sk_buff *skb, struct genl_info *info)

{
	int ret_val = -EINVAL;
	struct netlbl_audit audit_info;

	if (!info->attrs[NLBL_CALIPSO_A_DOI] ||
	    !info->attrs[NLBL_CALIPSO_A_MTYPE])
		return -EINVAL;

	netlbl_netlink_auditinfo(skb, &audit_info);
	switch (nla_get_u32(info->attrs[NLBL_CALIPSO_A_MTYPE])) {
	case CALIPSO_MAP_PASS:
		ret_val = netlbl_calipso_add_pass(info, &audit_info);
		break;
	}
	if (ret_val == 0)
		atomic_inc(&netlabel_mgmt_protocount);

	return ret_val;
}

/**
 * netlbl_calipso_list - Handle a LIST message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Process a user generated LIST message and respond accordingly.
 * Returns zero on success and negative values on error.
 *
 */
static int netlbl_calipso_list(struct sk_buff *skb, struct genl_info *info)
{
	int ret_val;
	struct sk_buff *ans_skb = NULL;
	void *data;
	u32 doi;
	struct calipso_doi *doi_def;

	if (!info->attrs[NLBL_CALIPSO_A_DOI]) {
		ret_val = -EINVAL;
		goto list_failure;
	}

	doi = nla_get_u32(info->attrs[NLBL_CALIPSO_A_DOI]);

	doi_def = calipso_doi_getdef(doi);
	if (!doi_def) {
		ret_val = -EINVAL;
		goto list_failure;
	}

	ans_skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!ans_skb) {
		ret_val = -ENOMEM;
		goto list_failure_put;
	}
	data = genlmsg_put_reply(ans_skb, info, &netlbl_calipso_gnl_family,
				 0, NLBL_CALIPSO_C_LIST);
	if (!data) {
		ret_val = -ENOMEM;
		goto list_failure_put;
	}

	ret_val = nla_put_u32(ans_skb, NLBL_CALIPSO_A_MTYPE, doi_def->type);
	if (ret_val != 0)
		goto list_failure_put;

	calipso_doi_putdef(doi_def);

	genlmsg_end(ans_skb, data);
	return genlmsg_reply(ans_skb, info);

list_failure_put:
	calipso_doi_putdef(doi_def);
list_failure:
	kfree_skb(ans_skb);
	return ret_val;
}

/**
 * netlbl_calipso_listall_cb - calipso_doi_walk() callback for LISTALL
 * @doi_def: the CALIPSO DOI definition
 * @arg: the netlbl_calipso_doiwalk_arg structure
 *
 * Description:
 * This function is designed to be used as a callback to the
 * calipso_doi_walk() function for use in generating a response for a LISTALL
 * message.  Returns the size of the message on success, negative values on
 * failure.
 *
 */
static int netlbl_calipso_listall_cb(struct calipso_doi *doi_def, void *arg)
{
	int ret_val = -ENOMEM;
	struct netlbl_calipso_doiwalk_arg *cb_arg = arg;
	void *data;

	data = genlmsg_put(cb_arg->skb, NETLINK_CB(cb_arg->nl_cb->skb).portid,
			   cb_arg->seq, &netlbl_calipso_gnl_family,
			   NLM_F_MULTI, NLBL_CALIPSO_C_LISTALL);
	if (!data)
		goto listall_cb_failure;

	ret_val = nla_put_u32(cb_arg->skb, NLBL_CALIPSO_A_DOI, doi_def->doi);
	if (ret_val != 0)
		goto listall_cb_failure;
	ret_val = nla_put_u32(cb_arg->skb,
			      NLBL_CALIPSO_A_MTYPE,
			      doi_def->type);
	if (ret_val != 0)
		goto listall_cb_failure;

	genlmsg_end(cb_arg->skb, data);
	return 0;

listall_cb_failure:
	genlmsg_cancel(cb_arg->skb, data);
	return ret_val;
}

/**
 * netlbl_calipso_listall - Handle a LISTALL message
 * @skb: the NETLINK buffer
 * @cb: the NETLINK callback
 *
 * Description:
 * Process a user generated LISTALL message and respond accordingly.  Returns
 * zero on success and negative values on error.
 *
 */
static int netlbl_calipso_listall(struct sk_buff *skb,
				  struct netlink_callback *cb)
{
	struct netlbl_calipso_doiwalk_arg cb_arg;
	u32 doi_skip = cb->args[0];

	cb_arg.nl_cb = cb;
	cb_arg.skb = skb;
	cb_arg.seq = cb->nlh->nlmsg_seq;

	calipso_doi_walk(&doi_skip, netlbl_calipso_listall_cb, &cb_arg);

	cb->args[0] = doi_skip;
	return skb->len;
}

/**
 * netlbl_calipso_remove_cb - netlbl_calipso_remove() callback for REMOVE
 * @entry: LSM domain mapping entry
 * @arg: the netlbl_domhsh_walk_arg structure
 *
 * Description:
 * This function is intended for use by netlbl_calipso_remove() as the callback
 * for the netlbl_domhsh_walk() function; it removes LSM domain map entries
 * which are associated with the CALIPSO DOI specified in @arg.  Returns zero on
 * success, negative values on failure.
 *
 */
static int netlbl_calipso_remove_cb(struct netlbl_dom_map *entry, void *arg)
{
	struct netlbl_domhsh_walk_arg *cb_arg = arg;

	if (entry->def.type == NETLBL_NLTYPE_CALIPSO &&
	    entry->def.calipso->doi == cb_arg->doi)
		return netlbl_domhsh_remove_entry(entry, cb_arg->audit_info);

	return 0;
}

/**
 * netlbl_calipso_remove - Handle a REMOVE message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Process a user generated REMOVE message and respond accordingly.  Returns
 * zero on success, negative values on failure.
 *
 */
static int netlbl_calipso_remove(struct sk_buff *skb, struct genl_info *info)
{
	int ret_val = -EINVAL;
	struct netlbl_domhsh_walk_arg cb_arg;
	struct netlbl_audit audit_info;
	u32 skip_bkt = 0;
	u32 skip_chain = 0;

	if (!info->attrs[NLBL_CALIPSO_A_DOI])
		return -EINVAL;

	netlbl_netlink_auditinfo(skb, &audit_info);
	cb_arg.doi = nla_get_u32(info->attrs[NLBL_CALIPSO_A_DOI]);
	cb_arg.audit_info = &audit_info;
	ret_val = netlbl_domhsh_walk(&skip_bkt, &skip_chain,
				     netlbl_calipso_remove_cb, &cb_arg);
	if (ret_val == 0 || ret_val == -ENOENT) {
		ret_val = calipso_doi_remove(cb_arg.doi, &audit_info);
		if (ret_val == 0)
			atomic_dec(&netlabel_mgmt_protocount);
	}

	return ret_val;
}

/* NetLabel Generic NETLINK Command Definitions
 */

static const struct genl_ops netlbl_calipso_ops[] = {
	{
	.cmd = NLBL_CALIPSO_C_ADD,
	.flags = GENL_ADMIN_PERM,
	.policy = calipso_genl_policy,
	.doit = netlbl_calipso_add,
	.dumpit = NULL,
	},
	{
	.cmd = NLBL_CALIPSO_C_REMOVE,
	.flags = GENL_ADMIN_PERM,
	.policy = calipso_genl_policy,
	.doit = netlbl_calipso_remove,
	.dumpit = NULL,
	},
	{
	.cmd = NLBL_CALIPSO_C_LIST,
	.flags = 0,
	.policy = calipso_genl_policy,
	.doit = netlbl_calipso_list,
	.dumpit = NULL,
	},
	{
	.cmd = NLBL_CALIPSO_C_LISTALL,
	.flags = 0,
	.policy = calipso_genl_policy,
	.doit = NULL,
	.dumpit = netlbl_calipso_listall,
	},
};

static struct genl_family netlbl_calipso_gnl_family __ro_after_init = {
	.hdrsize = 0,
	.name = NETLBL_NLTYPE_CALIPSO_NAME,
	.version = NETLBL_PROTO_VERSION,
	.maxattr = NLBL_CALIPSO_A_MAX,
	.module = THIS_MODULE,
	.ops = netlbl_calipso_ops,
	.n_ops = ARRAY_SIZE(netlbl_calipso_ops),
};

/* NetLabel Generic NETLINK Protocol Functions
 */

/**
 * netlbl_calipso_genl_init - Register the CALIPSO NetLabel component
 *
 * Description:
 * Register the CALIPSO packet NetLabel component with the Generic NETLINK
 * mechanism.  Returns zero on success, negative values on failure.
 *
 */
int __init netlbl_calipso_genl_init(void)
{
	return genl_register_family(&netlbl_calipso_gnl_family);
}

static const struct netlbl_calipso_ops *calipso_ops;

/**
 * netlbl_calipso_ops_register - Register the CALIPSO operations
 *
 * Description:
 * Register the CALIPSO packet engine operations.
 *
 */
const struct netlbl_calipso_ops *
netlbl_calipso_ops_register(const struct netlbl_calipso_ops *ops)
{
	return xchg(&calipso_ops, ops);
}
EXPORT_SYMBOL(netlbl_calipso_ops_register);

static const struct netlbl_calipso_ops *netlbl_calipso_ops_get(void)
{
	return READ_ONCE(calipso_ops);
}

/**
 * calipso_doi_add - Add a new DOI to the CALIPSO protocol engine
 * @doi_def: the DOI structure
 * @audit_info: NetLabel audit information
 *
 * Description:
 * The caller defines a new DOI for use by the CALIPSO engine and calls this
 * function to add it to the list of acceptable domains.  The caller must
 * ensure that the mapping table specified in @doi_def->map meets all of the
 * requirements of the mapping type (see calipso.h for details).  Returns
 * zero on success and non-zero on failure.
 *
 */
int calipso_doi_add(struct calipso_doi *doi_def,
		    struct netlbl_audit *audit_info)
{
	int ret_val = -ENOMSG;
	const struct netlbl_calipso_ops *ops = netlbl_calipso_ops_get();

	if (ops)
		ret_val = ops->doi_add(doi_def, audit_info);
	return ret_val;
}

/**
 * calipso_doi_free - Frees a DOI definition
 * @doi_def: the DOI definition
 *
 * Description:
 * This function frees all of the memory associated with a DOI definition.
 *
 */
void calipso_doi_free(struct calipso_doi *doi_def)
{
	const struct netlbl_calipso_ops *ops = netlbl_calipso_ops_get();

	if (ops)
		ops->doi_free(doi_def);
}

/**
 * calipso_doi_remove - Remove an existing DOI from the CALIPSO protocol engine
 * @doi: the DOI value
 * @audit_secid: the LSM secid to use in the audit message
 *
 * Description:
 * Removes a DOI definition from the CALIPSO engine.  The NetLabel routines will
 * be called to release their own LSM domain mappings as well as our own
 * domain list.  Returns zero on success and negative values on failure.
 *
 */
int calipso_doi_remove(u32 doi, struct netlbl_audit *audit_info)
{
	int ret_val = -ENOMSG;
	const struct netlbl_calipso_ops *ops = netlbl_calipso_ops_get();

	if (ops)
		ret_val = ops->doi_remove(doi, audit_info);
	return ret_val;
}

/**
 * calipso_doi_getdef - Returns a reference to a valid DOI definition
 * @doi: the DOI value
 *
 * Description:
 * Searches for a valid DOI definition and if one is found it is returned to
 * the caller.  Otherwise NULL is returned.  The caller must ensure that
 * calipso_doi_putdef() is called when the caller is done.
 *
 */
struct calipso_doi *calipso_doi_getdef(u32 doi)
{
	struct calipso_doi *ret_val = NULL;
	const struct netlbl_calipso_ops *ops = netlbl_calipso_ops_get();

	if (ops)
		ret_val = ops->doi_getdef(doi);
	return ret_val;
}

/**
 * calipso_doi_putdef - Releases a reference for the given DOI definition
 * @doi_def: the DOI definition
 *
 * Description:
 * Releases a DOI definition reference obtained from calipso_doi_getdef().
 *
 */
void calipso_doi_putdef(struct calipso_doi *doi_def)
{
	const struct netlbl_calipso_ops *ops = netlbl_calipso_ops_get();

	if (ops)
		ops->doi_putdef(doi_def);
}

/**
 * calipso_doi_walk - Iterate through the DOI definitions
 * @skip_cnt: skip past this number of DOI definitions, updated
 * @callback: callback for each DOI definition
 * @cb_arg: argument for the callback function
 *
 * Description:
 * Iterate over the DOI definition list, skipping the first @skip_cnt entries.
 * For each entry call @callback, if @callback returns a negative value stop
 * 'walking' through the list and return.  Updates the value in @skip_cnt upon
 * return.  Returns zero on success, negative values on failure.
 *
 */
int calipso_doi_walk(u32 *skip_cnt,
		     int (*callback)(struct calipso_doi *doi_def, void *arg),
		     void *cb_arg)
{
	int ret_val = -ENOMSG;
	const struct netlbl_calipso_ops *ops = netlbl_calipso_ops_get();

	if (ops)
		ret_val = ops->doi_walk(skip_cnt, callback, cb_arg);
	return ret_val;
}

/**
 * calipso_sock_getattr - Get the security attributes from a sock
 * @sk: the sock
 * @secattr: the security attributes
 *
 * Description:
 * Query @sk to see if there is a CALIPSO option attached to the sock and if
 * there is return the CALIPSO security attributes in @secattr.  This function
 * requires that @sk be locked, or privately held, but it does not do any
 * locking itself.  Returns zero on success and negative values on failure.
 *
 */
int calipso_sock_getattr(struct sock *sk, struct netlbl_lsm_secattr *secattr)
{
	int ret_val = -ENOMSG;
	const struct netlbl_calipso_ops *ops = netlbl_calipso_ops_get();

	if (ops)
		ret_val = ops->sock_getattr(sk, secattr);
	return ret_val;
}

/**
 * calipso_sock_setattr - Add a CALIPSO option to a socket
 * @sk: the socket
 * @doi_def: the CALIPSO DOI to use
 * @secattr: the specific security attributes of the socket
 *
 * Description:
 * Set the CALIPSO option on the given socket using the DOI definition and
 * security attributes passed to the function.  This function requires
 * exclusive access to @sk, which means it either needs to be in the
 * process of being created or locked.  Returns zero on success and negative
 * values on failure.
 *
 */
int calipso_sock_setattr(struct sock *sk,
			 const struct calipso_doi *doi_def,
			 const struct netlbl_lsm_secattr *secattr)
{
	int ret_val = -ENOMSG;
	const struct netlbl_calipso_ops *ops = netlbl_calipso_ops_get();

	if (ops)
		ret_val = ops->sock_setattr(sk, doi_def, secattr);
	return ret_val;
}

/**
 * calipso_sock_delattr - Delete the CALIPSO option from a socket
 * @sk: the socket
 *
 * Description:
 * Removes the CALIPSO option from a socket, if present.
 *
 */
void calipso_sock_delattr(struct sock *sk)
{
	const struct netlbl_calipso_ops *ops = netlbl_calipso_ops_get();

	if (ops)
		ops->sock_delattr(sk);
}

/**
 * calipso_req_setattr - Add a CALIPSO option to a connection request socket
 * @req: the connection request socket
 * @doi_def: the CALIPSO DOI to use
 * @secattr: the specific security attributes of the socket
 *
 * Description:
 * Set the CALIPSO option on the given socket using the DOI definition and
 * security attributes passed to the function.  Returns zero on success and
 * negative values on failure.
 *
 */
int calipso_req_setattr(struct request_sock *req,
			const struct calipso_doi *doi_def,
			const struct netlbl_lsm_secattr *secattr)
{
	int ret_val = -ENOMSG;
	const struct netlbl_calipso_ops *ops = netlbl_calipso_ops_get();

	if (ops)
		ret_val = ops->req_setattr(req, doi_def, secattr);
	return ret_val;
}

/**
 * calipso_req_delattr - Delete the CALIPSO option from a request socket
 * @reg: the request socket
 *
 * Description:
 * Removes the CALIPSO option from a request socket, if present.
 *
 */
void calipso_req_delattr(struct request_sock *req)
{
	const struct netlbl_calipso_ops *ops = netlbl_calipso_ops_get();

	if (ops)
		ops->req_delattr(req);
}

/**
 * calipso_optptr - Find the CALIPSO option in the packet
 * @skb: the packet
 *
 * Description:
 * Parse the packet's IP header looking for a CALIPSO option.  Returns a pointer
 * to the start of the CALIPSO option on success, NULL if one if not found.
 *
 */
unsigned char *calipso_optptr(const struct sk_buff *skb)
{
	unsigned char *ret_val = NULL;
	const struct netlbl_calipso_ops *ops = netlbl_calipso_ops_get();

	if (ops)
		ret_val = ops->skbuff_optptr(skb);
	return ret_val;
}

/**
 * calipso_getattr - Get the security attributes from a memory block.
 * @calipso: the CALIPSO option
 * @secattr: the security attributes
 *
 * Description:
 * Inspect @calipso and return the security attributes in @secattr.
 * Returns zero on success and negative values on failure.
 *
 */
int calipso_getattr(const unsigned char *calipso,
		    struct netlbl_lsm_secattr *secattr)
{
	int ret_val = -ENOMSG;
	const struct netlbl_calipso_ops *ops = netlbl_calipso_ops_get();

	if (ops)
		ret_val = ops->opt_getattr(calipso, secattr);
	return ret_val;
}

/**
 * calipso_skbuff_setattr - Set the CALIPSO option on a packet
 * @skb: the packet
 * @doi_def: the CALIPSO DOI to use
 * @secattr: the security attributes
 *
 * Description:
 * Set the CALIPSO option on the given packet based on the security attributes.
 * Returns a pointer to the IP header on success and NULL on failure.
 *
 */
int calipso_skbuff_setattr(struct sk_buff *skb,
			   const struct calipso_doi *doi_def,
			   const struct netlbl_lsm_secattr *secattr)
{
	int ret_val = -ENOMSG;
	const struct netlbl_calipso_ops *ops = netlbl_calipso_ops_get();

	if (ops)
		ret_val = ops->skbuff_setattr(skb, doi_def, secattr);
	return ret_val;
}

/**
 * calipso_skbuff_delattr - Delete any CALIPSO options from a packet
 * @skb: the packet
 *
 * Description:
 * Removes any and all CALIPSO options from the given packet.  Returns zero on
 * success, negative values on failure.
 *
 */
int calipso_skbuff_delattr(struct sk_buff *skb)
{
	int ret_val = -ENOMSG;
	const struct netlbl_calipso_ops *ops = netlbl_calipso_ops_get();

	if (ops)
		ret_val = ops->skbuff_delattr(skb);
	return ret_val;
}

/**
 * calipso_cache_invalidate - Invalidates the current CALIPSO cache
 *
 * Description:
 * Invalidates and frees any entries in the CALIPSO cache.  Returns zero on
 * success and negative values on failure.
 *
 */
void calipso_cache_invalidate(void)
{
	const struct netlbl_calipso_ops *ops = netlbl_calipso_ops_get();

	if (ops)
		ops->cache_invalidate();
}

/**
 * calipso_cache_add - Add an entry to the CALIPSO cache
 * @calipso_ptr: the CALIPSO option
 * @secattr: the packet's security attributes
 *
 * Description:
 * Add a new entry into the CALIPSO label mapping cache.
 * Returns zero on success, negative values on failure.
 *
 */
int calipso_cache_add(const unsigned char *calipso_ptr,
		      const struct netlbl_lsm_secattr *secattr)

{
	int ret_val = -ENOMSG;
	const struct netlbl_calipso_ops *ops = netlbl_calipso_ops_get();

	if (ops)
		ret_val = ops->cache_add(calipso_ptr, secattr);
	return ret_val;
}
