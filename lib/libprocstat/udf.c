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

#include <fs/udf/ecma167-udf.h>

#include "libprocstat.h"
#include "common_kvm.h"

/* XXX */
struct udf_mnt {
	int			im_flags;
	struct mount		*im_mountp;
	struct g_consumer	*im_cp;
	struct bufobj		*im_bo;
	struct cdev		*im_dev;
	struct vnode		*im_devvp;
	int			bsize;
	int			bshift;
	int			bmask;
	uint32_t		part_start;
	uint32_t		part_len;
	uint64_t		root_id;
	struct long_ad		root_icb;
	int			p_sectors;
	int			s_table_entries;
	void			*s_table;
	void			*im_d2l;
};
struct udf_node {
	struct vnode	*i_vnode;
	struct udf_mnt	*udfmp;
	ino_t		hash_id;
	long		diroff;
	struct file_entry *fentry;
};
#define VTON(vp)	((struct udf_node *)((vp)->v_data))

int
udf_filestat(kvm_t *kd, struct vnode *vp, struct vnstat *vn)
{
	struct udf_node node;
	struct udf_mnt mnt;
	int error;

	assert(kd);
	assert(vn);
	error = kvm_read_all(kd, (unsigned long)VTON(vp), &node, sizeof(node));
	if (error != 0) {
		warnx("can't read udf fnode at %p", (void *)VTON(vp));
		return (1);
	}
        error = kvm_read_all(kd, (unsigned long)node.udfmp, &mnt, sizeof(mnt));
        if (error != 0) {
                warnx("can't read udf_mnt at %p for vnode %p",
                    (void *)node.udfmp, vp);
                return (1);
        }
	vn->vn_fileid = node.hash_id;
	vn->vn_fsid = dev2udev(kd, mnt.im_dev);
	return (0);
}
