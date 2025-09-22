/*	$OpenBSD: kvm_ntfs.c,v 1.6 2021/09/10 00:02:43 deraadt Exp $	*/

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jaromir Dolecek.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/ucred.h>
#define _KERNEL
#include <sys/mount.h>
#undef _KERNEL
#include <sys/vnode.h>
#include <sys/sysctl.h>

#include <ntfs/ntfs.h>
#include <ntfs/ntfs_inode.h>

#include <limits.h>
#include <kvm.h>
#include <db.h>

#include "kvm_private.h"
#include "kvm_file.h"

int
_kvm_stat_ntfs(kvm_t *kd, struct kinfo_file *kf, struct vnode *vp)
{
	struct ntnode ntnode;
	struct fnode fn;
	struct ntfsmount ntm;

	/*
	 * To get the ntnode, we have to go in two steps - firstly
	 * to read appropriate struct fnode and then getting the address
	 * of ntnode and reading it's contents
	 */
	if (KREAD(kd, (u_long)VTOF(vp), &fn)) {
		_kvm_err(kd, kd->program, "can't read fnode at %p", VTOF(vp));
		return (-1);
	}
	if (KREAD(kd, (u_long)FTONT(&fn), &ntnode)) {
		_kvm_err(kd, kd->program, "can't read ntnode at %p", FTONT(&fn));
		return (-1);
	}
	if (KREAD(kd, (u_long)ntnode.i_mp, &ntm)) {
		_kvm_err(kd, kd->program, "can't read ntfsmount at %p",
			ntnode.i_mp);
		return (-1);
	}

	kf->va_fsid = ntnode.i_dev & 0xffff;
	kf->va_fileid = (long)ntnode.i_number;
	kf->va_mode = (mode_t)ntm.ntm_mode | _kvm_getftype(vp->v_type);
	kf->va_size = fn.f_size;
	kf->va_rdev = 0;  /* XXX */
	kf->va_nlink = 1;

	return (0);
}
