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

/* NetLabel Generic NETLINK CALIPSO family */
static struct genl_family netlbl_calipso_gnl_family = {
	.id = GENL_ID_GENERATE,
	.hdrsize = 0,
	.name = NETLBL_NLTYPE_CALIPSO_NAME,
	.version = NETLBL_PROTO_VERSION,
	.maxattr = NLBL_CALIPSO_A_MAX,
};

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
	return genl_register_family_with_ops(&netlbl_calipso_gnl_family,
					     netlbl_calipso_ops);
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
	return ACCESS_ONCE(calipso_ops);
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
