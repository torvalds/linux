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
#include <linux/audit.h>
#include <linux/tty.h>
#include <linux/security.h>
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
int __init netlbl_netlink_init(void)
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
 * NetLabel Audit Functions
 */

/**
 * netlbl_audit_start_common - Start an audit message
 * @type: audit message type
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Start an audit message using the type specified in @type and fill the audit
 * message with some fields common to all NetLabel audit messages.  Returns
 * a pointer to the audit buffer on success, NULL on failure.
 *
 */
struct audit_buffer *netlbl_audit_start_common(int type,
					       struct netlbl_audit *audit_info)
{
	struct audit_buffer *audit_buf;
	char *secctx;
	u32 secctx_len;

	if (audit_enabled == 0)
		return NULL;

	audit_buf = audit_log_start(current->audit_context, GFP_ATOMIC, type);
	if (audit_buf == NULL)
		return NULL;

	audit_log_format(audit_buf, "netlabel: auid=%u", audit_info->loginuid);

	if (audit_info->secid != 0 &&
	    security_secid_to_secctx(audit_info->secid,
				     &secctx,
				     &secctx_len) == 0) {
		audit_log_format(audit_buf, " subj=%s", secctx);
		security_release_secctx(secctx, secctx_len);
	}

	return audit_buf;
}
