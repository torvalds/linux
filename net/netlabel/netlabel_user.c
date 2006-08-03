/*
 * NetLabel NETLINK Interface
 *
 * This file defines the NETLINK interface for the NetLabel system.  The
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

#include <linux/init.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/socket.h>
#include <net/sock.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <net/netlabel.h>
#include <asm/bug.h>

#include "netlabel_mgmt.h"
#include "netlabel_unlabeled.h"
#include "netlabel_cipso_v4.h"
#include "netlabel_user.h"

/*
 * NetLabel NETLINK Setup Functions
 */

/**
 * netlbl_netlink_init - Initialize the NETLINK communication channel
 *
 * Description:
 * Call out to the NetLabel components so they can register their families and
 * commands with the Generic NETLINK mechanism.  Returns zero on success and
 * non-zero on failure.
 *
 */
int netlbl_netlink_init(void)
{
	int ret_val;

	ret_val = netlbl_mgmt_genl_init();
	if (ret_val != 0)
		return ret_val;

	ret_val = netlbl_cipsov4_genl_init();
	if (ret_val != 0)
		return ret_val;

	ret_val = netlbl_unlabel_genl_init();
	if (ret_val != 0)
		return ret_val;

	return 0;
}

/*
 * NetLabel Common Protocol Functions
 */

/**
 * netlbl_netlink_send_ack - Send an ACK message
 * @info: the generic NETLINK information
 * @genl_family: the generic NETLINK family ID value
 * @ack_cmd: the generic NETLINK family ACK command value
 * @ret_code: return code to use
 *
 * Description:
 * This function sends an ACK message to the sender of the NETLINK message
 * specified by @info.
 *
 */
void netlbl_netlink_send_ack(const struct genl_info *info,
			     u32 genl_family,
			     u8 ack_cmd,
			     u32 ret_code)
{
	size_t data_size;
	struct sk_buff *skb;

	data_size = GENL_HDRLEN + 2 * NETLBL_LEN_U32;
	skb = netlbl_netlink_alloc_skb(0, data_size, GFP_KERNEL);
	if (skb == NULL)
		return;

	if (netlbl_netlink_hdr_put(skb,
				   info->snd_pid,
				   0,
				   genl_family,
				   ack_cmd) == NULL)
		goto send_ack_failure;

	if (nla_put_u32(skb, NLA_U32, info->snd_seq) != 0)
		goto send_ack_failure;
	if (nla_put_u32(skb, NLA_U32, ret_code) != 0)
		goto send_ack_failure;

	netlbl_netlink_snd(skb, info->snd_pid);
	return;

send_ack_failure:
	kfree_skb(skb);
}

/*
 * NETLINK I/O Functions
 */

/**
 * netlbl_netlink_snd - Send a NetLabel message
 * @skb: NetLabel message
 * @pid: destination PID
 *
 * Description:
 * Sends a unicast NetLabel message over the NETLINK socket.
 *
 */
int netlbl_netlink_snd(struct sk_buff *skb, u32 pid)
{
	return genlmsg_unicast(skb, pid);
}

/**
 * netlbl_netlink_snd - Send a NetLabel message
 * @skb: NetLabel message
 * @pid: sending PID
 * @group: multicast group id
 *
 * Description:
 * Sends a multicast NetLabel message over the NETLINK socket to all members
 * of @group except @pid.
 *
 */
int netlbl_netlink_snd_multicast(struct sk_buff *skb, u32 pid, u32 group)
{
	return genlmsg_multicast(skb, pid, group);
}
