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
#include <linux/audit.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <net/netlabel.h>

/* NetLabel NETLINK helper functions */

/**
 * netlbl_netlink_auditinfo - Fetch the audit information from a NETLINK msg
 * @skb: the packet
 * @audit_info: NetLabel audit information
 */
static inline void netlbl_netlink_auditinfo(struct sk_buff *skb,
					    struct netlbl_audit *audit_info)
{
	audit_info->secid = NETLINK_CB(skb).sid;
	audit_info->loginuid = NETLINK_CB(skb).loginuid;
}

/* NetLabel NETLINK I/O functions */

int netlbl_netlink_init(void);

/* NetLabel Audit Functions */

struct audit_buffer *netlbl_audit_start_common(int type,
					      struct netlbl_audit *audit_info);

#endif
