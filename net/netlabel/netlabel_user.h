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

#ifndef _NETLABEL_USER_H
#define _NETLABEL_USER_H

#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/capability.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <net/netlabel.h>

/* NetLabel NETLINK helper functions */

/**
 * netlbl_netlink_cap_check - Check the NETLINK msg capabilities
 * @skb: the NETLINK buffer
 * @req_cap: the required capability
 *
 * Description:
 * Check the NETLINK buffer's capabilities against the required capabilities.
 * Returns zero on success, negative values on failure.
 *
 */
static inline int netlbl_netlink_cap_check(const struct sk_buff *skb,
					   kernel_cap_t req_cap)
{
	if (cap_raised(NETLINK_CB(skb).eff_cap, req_cap))
		return 0;
	return -EPERM;
}

/**
 * netlbl_getinc_u8 - Read a u8 value from a nlattr stream and move on
 * @nla: the attribute
 * @rem_len: remaining length
 *
 * Description:
 * Return a u8 value pointed to by @nla and advance it to the next attribute.
 *
 */
static inline u8 netlbl_getinc_u8(struct nlattr **nla, int *rem_len)
{
	u8 val = nla_get_u8(*nla);
	*nla = nla_next(*nla, rem_len);
	return val;
}

/**
 * netlbl_getinc_u16 - Read a u16 value from a nlattr stream and move on
 * @nla: the attribute
 * @rem_len: remaining length
 *
 * Description:
 * Return a u16 value pointed to by @nla and advance it to the next attribute.
 *
 */
static inline u16 netlbl_getinc_u16(struct nlattr **nla, int *rem_len)
{
	u16 val = nla_get_u16(*nla);
	*nla = nla_next(*nla, rem_len);
	return val;
}

/**
 * netlbl_getinc_u32 - Read a u32 value from a nlattr stream and move on
 * @nla: the attribute
 * @rem_len: remaining length
 *
 * Description:
 * Return a u32 value pointed to by @nla and advance it to the next attribute.
 *
 */
static inline u32 netlbl_getinc_u32(struct nlattr **nla, int *rem_len)
{
	u32 val = nla_get_u32(*nla);
	*nla = nla_next(*nla, rem_len);
	return val;
}

/**
 * netlbl_netlink_hdr_put - Write the NETLINK buffers into a sk_buff
 * @skb: the packet
 * @pid: the PID of the receipient
 * @seq: the sequence number
 * @type: the generic NETLINK message family type
 * @cmd: command
 *
 * Description:
 * Write both a NETLINK nlmsghdr structure and a Generic NETLINK genlmsghdr
 * struct to the packet.  Returns a pointer to the start of the payload buffer
 * on success or NULL on failure.
 *
 */
static inline void *netlbl_netlink_hdr_put(struct sk_buff *skb,
					   u32 pid,
					   u32 seq,
					   int type,
					   u8 cmd)
{
	return genlmsg_put(skb,
			   pid,
			   seq,
			   type,
			   0,
			   0,
			   cmd,
			   NETLBL_PROTO_VERSION);
}

/**
 * netlbl_netlink_hdr_push - Write the NETLINK buffers into a sk_buff
 * @skb: the packet
 * @pid: the PID of the receipient
 * @seq: the sequence number
 * @type: the generic NETLINK message family type
 * @cmd: command
 *
 * Description:
 * Write both a NETLINK nlmsghdr structure and a Generic NETLINK genlmsghdr
 * struct to the packet.
 *
 */
static inline void netlbl_netlink_hdr_push(struct sk_buff *skb,
					   u32 pid,
					   u32 seq,
					   int type,
					   u8 cmd)

{
	struct nlmsghdr *nlh;
	struct genlmsghdr *hdr;

	nlh = (struct nlmsghdr *)skb_push(skb, NLMSG_SPACE(GENL_HDRLEN));
	nlh->nlmsg_type = type;
	nlh->nlmsg_len = skb->len;
	nlh->nlmsg_flags = 0;
	nlh->nlmsg_pid = pid;
	nlh->nlmsg_seq = seq;

	hdr = nlmsg_data(nlh);
	hdr->cmd = cmd;
	hdr->version = NETLBL_PROTO_VERSION;
	hdr->reserved = 0;
}

/**
 * netlbl_netlink_payload_len - Return the length of the payload
 * @skb: the NETLINK buffer
 *
 * Description:
 * This function returns the length of the NetLabel payload.
 *
 */
static inline u32 netlbl_netlink_payload_len(const struct sk_buff *skb)
{
	return nlmsg_len((struct nlmsghdr *)skb->data) - GENL_HDRLEN;
}

/**
 * netlbl_netlink_payload_data - Returns a pointer to the start of the payload
 * @skb: the NETLINK buffer
 *
 * Description:
 * This function returns a pointer to the start of the NetLabel payload.
 *
 */
static inline void *netlbl_netlink_payload_data(const struct sk_buff *skb)
{
  return (unsigned char *)nlmsg_data((struct nlmsghdr *)skb->data) +
	  GENL_HDRLEN;
}

/* NetLabel common protocol functions */

void netlbl_netlink_send_ack(const struct genl_info *info,
			     u32 genl_family,
			     u8 ack_cmd,
			     u32 ret_code);

/* NetLabel NETLINK I/O functions */

int netlbl_netlink_init(void);
int netlbl_netlink_snd(struct sk_buff *skb, u32 pid);
int netlbl_netlink_snd_multicast(struct sk_buff *skb, u32 pid, u32 group);

#endif
