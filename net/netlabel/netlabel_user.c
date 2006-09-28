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
 * NetLabel Audit Functions
 */

/**
 * netlbl_audit_start_common - Start an audit message
 * @type: audit message type
 * @secid: LSM context ID
 *
 * Description:
 * Start an audit message using the type specified in @type and fill the audit
 * message with some fields common to all NetLabel audit messages.  Returns
 * a pointer to the audit buffer on success, NULL on failure.
 *
 */
struct audit_buffer *netlbl_audit_start_common(int type, u32 secid)
{
	struct audit_context *audit_ctx = current->audit_context;
	struct audit_buffer *audit_buf;
	uid_t audit_loginuid;
	const char *audit_tty;
	char audit_comm[sizeof(current->comm)];
	struct vm_area_struct *vma;
	char *secctx;
	u32 secctx_len;

	audit_buf = audit_log_start(audit_ctx, GFP_ATOMIC, type);
	if (audit_buf == NULL)
		return NULL;

	audit_loginuid = audit_get_loginuid(audit_ctx);
	if (current->signal &&
	    current->signal->tty &&
	    current->signal->tty->name)
		audit_tty = current->signal->tty->name;
	else
		audit_tty = "(none)";
	get_task_comm(audit_comm, current);

	audit_log_format(audit_buf,
			 "netlabel: auid=%u uid=%u tty=%s pid=%d",
			 audit_loginuid,
			 current->uid,
			 audit_tty,
			 current->pid);
	audit_log_format(audit_buf, " comm=");
	audit_log_untrustedstring(audit_buf, audit_comm);
	if (current->mm) {
		down_read(&current->mm->mmap_sem);
		vma = current->mm->mmap;
		while (vma) {
			if ((vma->vm_flags & VM_EXECUTABLE) &&
			    vma->vm_file) {
				audit_log_d_path(audit_buf,
						 " exe=",
						 vma->vm_file->f_dentry,
						 vma->vm_file->f_vfsmnt);
				break;
			}
			vma = vma->vm_next;
		}
		up_read(&current->mm->mmap_sem);
	}

	if (secid != 0 &&
	    security_secid_to_secctx(secid, &secctx, &secctx_len) == 0)
		audit_log_format(audit_buf, " subj=%s", secctx);

	return audit_buf;
}

/**
 * netlbl_audit_nomsg - Send an audit message without additional text
 * @type: audit message type
 * @secid: LSM context ID
 *
 * Description:
 * Send an audit message with only the common NetLabel audit fields.
 *
 */
void netlbl_audit_nomsg(int type, u32 secid)
{
	struct audit_buffer *audit_buf;

	audit_buf = netlbl_audit_start_common(type, secid);
	audit_log_end(audit_buf);
}
