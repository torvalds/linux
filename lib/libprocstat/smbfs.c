/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2009 Stanislav Sedov <stas@FreeBSD.org>
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
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/vnode.h>
#define _KERNEL
#include <sys/mount.h>
#undef _KERNEL

#include <netinet/in.h>

#include <assert.h>
#include <err.h>
#include <kvm.h>
#include <stdlib.h>

#include <fs/smbfs/smbfs.h>
#include <fs/smbfs/smbfs_node.h>

#include "libprocstat.h"
#include "common_kvm.h"

int
smbfs_filestat(kvm_t *kd, struct vnode *vp, struct vnstat *vn)
{
	struct smbnode node;
	struct mount mnt;
	int error;

	assert(kd);
	assert(vn);
	error = kvm_read_all(kd, (unsigned long)VTOSMB(vp), &node,
	    sizeof(node));
	if (error != 0) {
		warnx("can't read smbfs fnode at %p", (void *)VTOSMB(vp));
		return (1);
	}
        error = kvm_read_all(kd, (unsigned long)getvnodemount(vp), &mnt,
	    sizeof(mnt));
        if (error != 0) {
                warnx("can't read mount at %p for vnode %p",
                    (void *)getvnodemount(vp), vp);
                return (1);
        }
	vn->vn_fileid = node.n_ino;
	if (vn->vn_fileid == 0)
		vn->vn_fileid = 2;
	vn->vn_fsid = mnt.mnt_stat.f_fsid.val[0];
	return (0);
}
