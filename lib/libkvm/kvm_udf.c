/*	$OpenBSD: kvm_udf.c,v 1.11 2021/09/10 00:02:43 deraadt Exp $	*/

/*
 * Copyright (c) 2001, 2002 Scott Long <scottl@freebsd.org>
 * All rights reserved.
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
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/ucred.h>
#define _KERNEL
#include <sys/mount.h>
#undef _KERNEL
#include <sys/lock.h>
#include <sys/vnode.h>
#include <sys/sysctl.h>
#include <sys/specdev.h>

#include <crypto/siphash.h>

#include <isofs/udf/ecma167-udf.h>
#include <isofs/udf/udf.h>

#include <stdint.h>
#include <limits.h>
#include <kvm.h>
#include <db.h>

#include "kvm_private.h"
#include "kvm_file.h"

/* Convert file entry permission (5 bits per owner/group/user) to a mode_t */
static mode_t
udf_permtomode(struct unode *up)
{
	uint32_t perm;
	uint16_t flags;
	mode_t mode;

	perm = letoh32(up->u_fentry->perm);
	flags = letoh16(up->u_fentry->icbtag.flags);

	mode = perm & UDF_FENTRY_PERM_USER_MASK;
	mode |= ((perm & UDF_FENTRY_PERM_GRP_MASK) >> 2);
	mode |= ((perm & UDF_FENTRY_PERM_OWNER_MASK) >> 4);
	mode |= ((flags & UDF_ICB_TAG_FLAGS_STICKY) << 4);
	mode |= ((flags & UDF_ICB_TAG_FLAGS_SETGID) << 6);
	mode |= ((flags & UDF_ICB_TAG_FLAGS_SETUID) << 8);

	return (mode);
}

int
_kvm_stat_udf(kvm_t *kd, struct kinfo_file *kf, struct vnode *vp)
{
	struct unode up;
	struct file_entry fentry;
	struct umount um;

	if (KREAD(kd, (u_long)VTOU(vp), &up)) {
		_kvm_err(kd, kd->program, "can't read unode at %p", VTOU(vp));
		return (-1);
	}
	if (KREAD(kd, (u_long)up.u_fentry, &fentry)) {
		_kvm_err(kd, kd->program, "can't read file_entry at %p",
		    up.u_fentry);
		return (-1);
	}
	if (KREAD(kd, (u_long)up.u_ump, &um)) {
		_kvm_err(kd, kd->program, "can't read umount at %p",
		    up.u_ump);
		return (-1);
	}
	kf->va_fsid = up.u_dev;
	kf->va_fileid = (long)up.u_ino;
	kf->va_mode = udf_permtomode(&up); /* XXX */
	kf->va_rdev = 0;
	kf->va_nlink = letoh16(fentry.link_cnt);
	if (vp->v_type & VDIR) {
		/*
		 * Directories that are recorded within their ICB will show
		 * as having 0 blocks recorded.  Since tradition dictates
		 * that directories consume at least one logical block,
		 * make it appear so.
		 */
		if (fentry.logblks_rec != 0) {
			kf->va_size =
			    letoh64(fentry.logblks_rec) * um.um_bsize;
		} else {
			kf->va_size = um.um_bsize;
		}
	} else {
		kf->va_size = letoh64(fentry.inf_len);
	}

	return (0);
}
