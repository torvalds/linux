/*
 * Copyright (C) 2008 IBM Corporation
 * Author: Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * File: integrity_audit.c
 * 	Audit calls for the integrity subsystem
 */

#include <linux/fs.h>
#include <linux/audit.h>
#include "ima.h"

static int ima_audit;

#ifdef CONFIG_IMA_AUDIT

/* ima_audit_setup - enable informational auditing messages */
static int __init ima_audit_setup(char *str)
{
	unsigned long audit;
	int rc, result = 0;
	char *op = "ima_audit";
	char *cause;

	rc = strict_strtoul(str, 0, &audit);
	if (rc || audit > 1)
		result = 1;
	else
		ima_audit = audit;
	cause = ima_audit ? "enabled" : "not_enabled";
	integrity_audit_msg(AUDIT_INTEGRITY_STATUS, NULL, NULL,
			    op, cause, result, 0);
	return 1;
}
__setup("ima_audit=", ima_audit_setup);
#endif

void integrity_audit_msg(int audit_msgno, struct inode *inode,
			 const unsigned char *fname, const char *op,
			 const char *cause, int result, int audit_info)
{
	struct audit_buffer *ab;

	if (!ima_audit && audit_info == 1) /* Skip informational messages */
		return;

	ab = audit_log_start(current->audit_context, GFP_KERNEL, audit_msgno);
	audit_log_format(ab, "integrity: pid=%d uid=%u auid=%u ses=%u",
			 current->pid, current->cred->uid,
			 audit_get_loginuid(current),
			 audit_get_sessionid(current));
	audit_log_task_context(ab);
	switch (audit_msgno) {
	case AUDIT_INTEGRITY_DATA:
	case AUDIT_INTEGRITY_METADATA:
	case AUDIT_INTEGRITY_PCR:
	case AUDIT_INTEGRITY_STATUS:
		audit_log_format(ab, " op=%s cause=%s", op, cause);
		break;
	case AUDIT_INTEGRITY_HASH:
		audit_log_format(ab, " op=%s hash=%s", op, cause);
		break;
	default:
		audit_log_format(ab, " op=%s", op);
	}
	audit_log_format(ab, " comm=");
	audit_log_untrustedstring(ab, current->comm);
	if (fname) {
		audit_log_format(ab, " name=");
		audit_log_untrustedstring(ab, fname);
	}
	if (inode)
		audit_log_format(ab, " dev=%s ino=%lu",
				 inode->i_sb->s_id, inode->i_ino);
	audit_log_format(ab, " res=%d", !result ? 0 : 1);
	audit_log_end(ab);
}
