/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * NetLabel NETLINK Interface
 *
 * This file defines the NETLINK interface for the NetLabel system.  The
 * NetLabel system manages static and dynamic label mappings for network
 * protocols such as CIPSO and RIPSO.
 *
 * Author: Paul Moore <paul@paul-moore.com>
 */

/*
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2006
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
 * @audit_info: NetLabel audit information
 */
static inline void netlbl_netlink_auditinfo(struct netlbl_audit *audit_info)
{
	security_current_getlsmprop_subj(&audit_info->prop);
	audit_info->loginuid = audit_get_loginuid(current);
	audit_info->sessionid = audit_get_sessionid(current);
}

/* NetLabel NETLINK I/O functions */

int netlbl_netlink_init(void);

/* NetLabel Audit Functions */

struct audit_buffer *netlbl_audit_start_common(int type,
					      struct netlbl_audit *audit_info);

#endif
