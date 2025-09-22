/*	$OpenBSD: kvm_cd9660.c,v 1.9 2021/09/10 00:02:43 deraadt Exp $	*/

/*
 * Copyright (c) 2009 Todd C. Miller <millert@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
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

#include <isofs/cd9660/iso.h>
#include <isofs/cd9660/cd9660_extern.h>
#include <isofs/cd9660/cd9660_node.h>

#include <limits.h>
#include <kvm.h>
#include <db.h>

#include "kvm_private.h"
#include "kvm_file.h"

int
_kvm_stat_cd9660(kvm_t *kd, struct kinfo_file *kf, struct vnode *vp)
{
	struct iso_node inode;

	if (KREAD(kd, (u_long)VTOI(vp), &inode)) {
		_kvm_err(kd, kd->program, "can't read inode at %p", VTOI(vp));
		return (-1);
	}
	kf->va_fsid = inode.i_dev & 0xffff;
	kf->va_fileid = (long)inode.i_number;
	kf->va_mode = inode.inode.iso_mode;
	kf->va_size = inode.i_size;
	kf->va_rdev = inode.i_dev;
	kf->va_nlink = inode.inode.iso_links;

	return (0);
}
