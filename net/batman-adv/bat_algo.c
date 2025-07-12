// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 */

#include "main.h"

#include <linux/errno.h>
#include <linux/list.h>
#include <linux/moduleparam.h>
#include <linux/netlink.h>
#include <linux/printk.h>
#include <linux/skbuff.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/types.h>
#include <net/genetlink.h>
#include <net/netlink.h>
#include <uapi/linux/batman_adv.h>

#include "bat_algo.h"
#include "netlink.h"

char batadv_routing_algo[20] = "BATMAN_IV";
static struct hlist_head batadv_algo_list;

/**
 * batadv_algo_init() - Initialize batman-adv algorithm management data
 *  structures
 */
void batadv_algo_init(void)
{
	INIT_HLIST_HEAD(&batadv_algo_list);
}

/**
 * batadv_algo_get() - Search for algorithm with specific name
 * @name: algorithm name to find
 *
 * Return: Pointer to batadv_algo_ops on success, NULL otherwise
 */
struct batadv_algo_ops *batadv_algo_get(const char *name)
{
	struct batadv_algo_ops *bat_algo_ops = NULL, *bat_algo_ops_tmp;

	hlist_for_each_entry(bat_algo_ops_tmp, &batadv_algo_list, list) {
		if (strcmp(bat_algo_ops_tmp->name, name) != 0)
			continue;

		bat_algo_ops = bat_algo_ops_tmp;
		break;
	}

	return bat_algo_ops;
}

/**
 * batadv_algo_register() - Register callbacks for a mesh algorithm
 * @bat_algo_ops: mesh algorithm callbacks to add
 *
 * Return: 0 on success or negative error number in case of failure
 */
int batadv_algo_register(struct batadv_algo_ops *bat_algo_ops)
{
	struct batadv_algo_ops *bat_algo_ops_tmp;

	bat_algo_ops_tmp = batadv_algo_get(bat_algo_ops->name);
	if (bat_algo_ops_tmp) {
		pr_info("Trying to register already registered routing algorithm: %s\n",
			bat_algo_ops->name);
		return -EEXIST;
	}

	/* all algorithms must implement all ops (for now) */
	if (!bat_algo_ops->iface.enable ||
	    !bat_algo_ops->iface.disable ||
	    !bat_algo_ops->iface.update_mac ||
	    !bat_algo_ops->iface.primary_set ||
	    !bat_algo_ops->neigh.cmp ||
	    !bat_algo_ops->neigh.is_similar_or_better) {
		pr_info("Routing algo '%s' does not implement required ops\n",
			bat_algo_ops->name);
		return -EINVAL;
	}

	INIT_HLIST_NODE(&bat_algo_ops->list);
	hlist_add_head(&bat_algo_ops->list, &batadv_algo_list);

	return 0;
}

/**
 * batadv_algo_select() - Select algorithm of mesh interface
 * @bat_priv: the bat priv with all the mesh interface information
 * @name: name of the algorithm to select
 *
 * The algorithm callbacks for the mesh interface will be set when the algorithm
 * with the correct name was found. Any previous selected algorithm will not be
 * deinitialized and the new selected algorithm will also not be initialized.
 * It is therefore not allowed to call batadv_algo_select outside the creation
 * function of the mesh interface.
 *
 * Return: 0 on success or negative error number in case of failure
 */
int batadv_algo_select(struct batadv_priv *bat_priv, const char *name)
{
	struct batadv_algo_ops *bat_algo_ops;

	bat_algo_ops = batadv_algo_get(name);
	if (!bat_algo_ops)
		return -EINVAL;

	bat_priv->algo_ops = bat_algo_ops;

	return 0;
}

static int batadv_param_set_ra(const char *val, const struct kernel_param *kp)
{
	struct batadv_algo_ops *bat_algo_ops;
	char *algo_name = (char *)val;
	size_t name_len = strlen(algo_name);

	if (name_len > 0 && algo_name[name_len - 1] == '\n')
		algo_name[name_len - 1] = '\0';

	bat_algo_ops = batadv_algo_get(algo_name);
	if (!bat_algo_ops) {
		pr_err("Routing algorithm '%s' is not supported\n", algo_name);
		return -EINVAL;
	}

	return param_set_copystring(algo_name, kp);
}

static const struct kernel_param_ops batadv_param_ops_ra = {
	.set = batadv_param_set_ra,
	.get = param_get_string,
};

static struct kparam_string batadv_param_string_ra = {
	.maxlen = sizeof(batadv_routing_algo),
	.string = batadv_routing_algo,
};

module_param_cb(routing_algo, &batadv_param_ops_ra, &batadv_param_string_ra,
		0644);

/**
 * batadv_algo_dump_entry() - fill in information about one supported routing
 *  algorithm
 * @msg: netlink message to be sent back
 * @portid: Port to reply to
 * @seq: Sequence number of message
 * @bat_algo_ops: Algorithm to be dumped
 *
 * Return: Error number, or 0 on success
 */
static int batadv_algo_dump_entry(struct sk_buff *msg, u32 portid, u32 seq,
				  struct batadv_algo_ops *bat_algo_ops)
{
	void *hdr;

	hdr = genlmsg_put(msg, portid, seq, &batadv_netlink_family,
			  NLM_F_MULTI, BATADV_CMD_GET_ROUTING_ALGOS);
	if (!hdr)
		return -EMSGSIZE;

	if (nla_put_string(msg, BATADV_ATTR_ALGO_NAME, bat_algo_ops->name))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

/**
 * batadv_algo_dump() - fill in information about supported routing
 *  algorithms
 * @msg: netlink message to be sent back
 * @cb: Parameters to the netlink request
 *
 * Return: Length of reply message.
 */
int batadv_algo_dump(struct sk_buff *msg, struct netlink_callback *cb)
{
	int portid = NETLINK_CB(cb->skb).portid;
	struct batadv_algo_ops *bat_algo_ops;
	int skip = cb->args[0];
	int i = 0;

	hlist_for_each_entry(bat_algo_ops, &batadv_algo_list, list) {
		if (i++ < skip)
			continue;

		if (batadv_algo_dump_entry(msg, portid, cb->nlh->nlmsg_seq,
					   bat_algo_ops)) {
			i--;
			break;
		}
	}

	cb->args[0] = i;

	return msg->len;
}
