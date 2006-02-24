/*
 * SELinux services exported to the rest of the kernel.
 *
 * Author: James Morris <jmorris@redhat.com>
 *
 * Copyright (C) 2005 Red Hat, Inc., James Morris <jmorris@redhat.com>
 * Copyright (C) 2006 Trusted Computer Solutions, Inc. <dgoeddel@trustedcs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2,
 * as published by the Free Software Foundation.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/selinux.h>

#include "security.h"
#include "objsec.h"

void selinux_task_ctxid(struct task_struct *tsk, u32 *ctxid)
{
	struct task_security_struct *tsec = tsk->security;
	if (selinux_enabled)
		*ctxid = tsec->sid;
	else
		*ctxid = 0;
}
