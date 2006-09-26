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
					   int flags,
					   u8 cmd)
{
	return genlmsg_put(skb,
			   pid,
			   seq,
			   type,
			   0,
			   flags,
			   cmd,
			   NETLBL_PROTO_VERSION);
}

/* NetLabel NETLINK I/O functions */

int netlbl_netlink_init(void);

#endif
