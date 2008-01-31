/*
 * SELinux services exported to the rest of the kernel.
 *
 * Author: James Morris <jmorris@redhat.com>
 *
 * Copyright (C) 2005 Red Hat, Inc., James Morris <jmorris@redhat.com>
 * Copyright (C) 2006 Trusted Computer Solutions, Inc. <dgoeddel@trustedcs.com>
 * Copyright (C) 2006 IBM Corporation, Timothy R. Chavez <tinytim@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2,
 * as published by the Free Software Foundation.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/selinux.h>
#include <linux/fs.h>
#include <linux/ipc.h>
#include <asm/atomic.h>

#include "security.h"
#include "objsec.h"

/* SECMARK reference count */
extern atomic_t selinux_secmark_refcount;

int selinux_sid_to_string(u32 sid, char **ctx, u32 *ctxlen)
{
	if (selinux_enabled)
		return security_sid_to_context(sid, ctx, ctxlen);
	else {
		*ctx = NULL;
		*ctxlen = 0;
	}

	return 0;
}

void selinux_get_inode_sid(const struct inode *inode, u32 *sid)
{
	if (selinux_enabled) {
		struct inode_security_struct *isec = inode->i_security;
		*sid = isec->sid;
		return;
	}
	*sid = 0;
}

void selinux_get_ipc_sid(const struct kern_ipc_perm *ipcp, u32 *sid)
{
	if (selinux_enabled) {
		struct ipc_security_struct *isec = ipcp->security;
		*sid = isec->sid;
		return;
	}
	*sid = 0;
}

void selinux_get_task_sid(struct task_struct *tsk, u32 *sid)
{
	if (selinux_enabled) {
		struct task_security_struct *tsec = tsk->security;
		*sid = tsec->sid;
		return;
	}
	*sid = 0;
}

int selinux_string_to_sid(char *str, u32 *sid)
{
	if (selinux_enabled)
		return security_context_to_sid(str, strlen(str), sid);
	else {
		*sid = 0;
		return 0;
	}
}
EXPORT_SYMBOL_GPL(selinux_string_to_sid);

int selinux_secmark_relabel_packet_permission(u32 sid)
{
	if (selinux_enabled) {
		struct task_security_struct *tsec = current->security;

		return avc_has_perm(tsec->sid, sid, SECCLASS_PACKET,
				    PACKET__RELABELTO, NULL);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(selinux_secmark_relabel_packet_permission);

void selinux_secmark_refcount_inc(void)
{
	atomic_inc(&selinux_secmark_refcount);
}
EXPORT_SYMBOL_GPL(selinux_secmark_refcount_inc);

void selinux_secmark_refcount_dec(void)
{
	atomic_dec(&selinux_secmark_refcount);
}
EXPORT_SYMBOL_GPL(selinux_secmark_refcount_dec);
