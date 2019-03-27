/* 
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2000 Peter Edwards
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by Peter Edwards
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * XXX -
 * This had to be separated from fstat.c because cd9660s has namespace
 * conflicts with UFS.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <sys/mount.h>

#include <netinet/in.h>

#include <err.h>

#define _KERNEL
#include <isofs/cd9660/iso.h>
#undef _KERNEL
#include <isofs/cd9660/cd9660_node.h>

#include <kvm.h>
#include <stdio.h>

#include "libprocstat.h"
#include "common_kvm.h"

int
isofs_filestat(kvm_t *kd, struct vnode *vp, struct vnstat *vn)
{
	struct iso_node isonode;
	struct iso_mnt mnt;

	if (!kvm_read_all(kd, (unsigned long)VTOI(vp), &isonode,
	    sizeof(isonode))) {
		warnx("can't read iso_node at %p",
		    (void *)VTOI(vp));
		return (1);
	}
	if (!kvm_read_all(kd, (unsigned long)isonode.i_mnt, &mnt,
	    sizeof(mnt))) {
		warnx("can't read iso_mnt at %p",
		    (void *)VTOI(vp));
		return (1);
	}
	vn->vn_fsid = dev2udev(kd, mnt.im_dev);
	vn->vn_mode = (mode_t)isonode.inode.iso_mode;
	vn->vn_fileid = isonode.i_number;
	vn->vn_size = isonode.i_size;
	return (0);
}
