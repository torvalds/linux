/*-
 * Copyright (c) 1999-2002, 2007 Robert N. M. Watson
 * Copyright (c) 2001-2005 Networks Associates Technology, Inc.
 * Copyright (c) 2005 Tom Rhodes
 * Copyright (c) 2006 SPARTA, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 * It was later enhanced by Tom Rhodes for the TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by Network
 * Associates Laboratories, the Security Research Division of Network
 * Associates, Inc. under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"),
 * as part of the DARPA CHATS research program.
 *
 * This software was enhanced by SPARTA ISSO under SPAWAR contract
 * N66001-04-C-6019 ("SEFOS").
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/acl.h>
#include <sys/kernel.h>
#include <sys/jail.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/stat.h>

#include <security/mac/mac_policy.h>
#include <security/mac_bsdextended/mac_bsdextended.h>
#include <security/mac_bsdextended/ugidfw_internal.h>

int
ugidfw_system_check_acct(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{

	if (vp != NULL)
		return (ugidfw_check_vp(cred, vp, MBI_WRITE));
	else
		return (0);
}

int
ugidfw_system_check_auditctl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{

	if (vp != NULL)
		return (ugidfw_check_vp(cred, vp, MBI_WRITE));
	else
		return (0);
}

int
ugidfw_system_check_swapon(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{

	return (ugidfw_check_vp(cred, vp, MBI_WRITE));
}
