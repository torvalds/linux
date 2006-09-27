/*
 * NetLabel Unlabeled Support
 *
 * This file defines functions for dealing with unlabeled packets for the
 * NetLabel system.  The NetLabel system manages static and dynamic label
 * mappings for network protocols such as CIPSO and RIPSO.
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
#include <linux/rcupdate.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/netlink.h>
#include <net/genetlink.h>

#include <net/netlabel.h>
#include <asm/bug.h>

#include "netlabel_user.h"
#include "netlabel_domainhash.h"
#include "netlabel_unlabeled.h"

/* Accept unlabeled packets flag */
static atomic_t netlabel_unlabel_accept_flg = ATOMIC_INIT(0);

/* NetLabel Generic NETLINK CIPSOv4 family */
static struct genl_family netlbl_unlabel_gnl_family = {
	.id = GENL_ID_GENERATE,
	.hdrsize = 0,
	.name = NETLBL_NLTYPE_UNLABELED_NAME,
	.version = NETLBL_PROTO_VERSION,
	.maxattr = 0,
};


/*
 * NetLabel Command Handlers
 */

/**
 * netlbl_unlabel_accept - Handle an ACCEPT message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Process a user generated ACCEPT message and set the accept flag accordingly.
 * Returns zero on success, negative values on failure.
 *
 */
static int netlbl_unlabel_accept(struct sk_buff *skb, struct genl_info *info)
{
	int ret_val;
	struct nlattr *data = netlbl_netlink_payload_data(skb);
	u32 value;

	ret_val = netlbl_netlink_cap_check(skb, CAP_NET_ADMIN);
	if (ret_val != 0)
		return ret_val;

	if (netlbl_netlink_payload_len(skb) == NETLBL_LEN_U32) {
		value = nla_get_u32(data);
		if (value == 1 || value == 0) {
			atomic_set(&netlabel_unlabel_accept_flg, value);
			netlbl_netlink_send_ack(info,
						netlbl_unlabel_gnl_family.id,
						NLBL_UNLABEL_C_ACK,
						NETLBL_E_OK);
			return 0;
		}
	}

	netlbl_netlink_send_ack(info,
				netlbl_unlabel_gnl_family.id,
				NLBL_UNLABEL_C_ACK,
				EINVAL);
	return -EINVAL;
}

/**
 * netlbl_unlabel_list - Handle a LIST message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Process a user generated LIST message and respond with the current status.
 * Returns zero on success, negative values on failure.
 *
 */
static int netlbl_unlabel_list(struct sk_buff *skb, struct genl_info *info)
{
	int ret_val = -ENOMEM;
	struct sk_buff *ans_skb;

	ans_skb = netlbl_netlink_alloc_skb(0,
					   GENL_HDRLEN + NETLBL_LEN_U32,
					   GFP_KERNEL);
	if (ans_skb == NULL)
		goto list_failure;

	if (netlbl_netlink_hdr_put(ans_skb,
				   info->snd_pid,
				   0,
				   netlbl_unlabel_gnl_family.id,
				   NLBL_UNLABEL_C_LIST) == NULL)
		goto list_failure;

	ret_val = nla_put_u32(ans_skb,
			      NLA_U32,
			      atomic_read(&netlabel_unlabel_accept_flg));
	if (ret_val != 0)
		goto list_failure;

	ret_val = netlbl_netlink_snd(ans_skb, info->snd_pid);
	if (ret_val != 0)
		goto list_failure;

	return 0;

list_failure:
	netlbl_netlink_send_ack(info,
				netlbl_unlabel_gnl_family.id,
				NLBL_UNLABEL_C_ACK,
				-ret_val);
	return ret_val;
}


/*
 * NetLabel Generic NETLINK Command Definitions
 */

static struct genl_ops netlbl_unlabel_genl_c_accept = {
	.cmd = NLBL_UNLABEL_C_ACCEPT,
	.flags = 0,
	.doit = netlbl_unlabel_accept,
	.dumpit = NULL,
};

static struct genl_ops netlbl_unlabel_genl_c_list = {
	.cmd = NLBL_UNLABEL_C_LIST,
	.flags = 0,
	.doit = netlbl_unlabel_list,
	.dumpit = NULL,
};


/*
 * NetLabel Generic NETLINK Protocol Functions
 */

/**
 * netlbl_unlabel_genl_init - Register the Unlabeled NetLabel component
 *
 * Description:
 * Register the unlabeled packet NetLabel component with the Generic NETLINK
 * mechanism.  Returns zero on success, negative values on failure.
 *
 */
int netlbl_unlabel_genl_init(void)
{
	int ret_val;

	ret_val = genl_register_family(&netlbl_unlabel_gnl_family);
	if (ret_val != 0)
		return ret_val;

	ret_val = genl_register_ops(&netlbl_unlabel_gnl_family,
				    &netlbl_unlabel_genl_c_accept);
	if (ret_val != 0)
		return ret_val;

	ret_val = genl_register_ops(&netlbl_unlabel_gnl_family,
				    &netlbl_unlabel_genl_c_list);
	if (ret_val != 0)
		return ret_val;

	return 0;
}

/*
 * NetLabel KAPI Hooks
 */

/**
 * netlbl_unlabel_getattr - Get the security attributes for an unlabled packet
 * @secattr: the security attributes
 *
 * Description:
 * Determine the security attributes, if any, for an unlabled packet and return
 * them in @secattr.  Returns zero on success and negative values on failure.
 *
 */
int netlbl_unlabel_getattr(struct netlbl_lsm_secattr *secattr)
{
	if (atomic_read(&netlabel_unlabel_accept_flg) == 1) {
		memset(secattr, 0, sizeof(*secattr));
		return 0;
	}

	return -ENOMSG;
}

/**
 * netlbl_unlabel_defconf - Set the default config to allow unlabeled packets
 *
 * Description:
 * Set the default NetLabel configuration to allow incoming unlabeled packets
 * and to send unlabeled network traffic by default.
 *
 */
int netlbl_unlabel_defconf(void)
{
	int ret_val;
	struct netlbl_dom_map *entry;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (entry == NULL)
		return -ENOMEM;
	entry->type = NETLBL_NLTYPE_UNLABELED;
	ret_val = netlbl_domhsh_add_default(entry);
	if (ret_val != 0)
		return ret_val;

	atomic_set(&netlabel_unlabel_accept_flg, 1);

	return 0;
}
