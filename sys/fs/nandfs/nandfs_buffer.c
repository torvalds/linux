/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2012 Semihalf.
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
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/buf.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/bio.h>

#include <fs/nandfs/nandfs_mount.h>
#include <fs/nandfs/nandfs.h>
#include <fs/nandfs/nandfs_subr.h>

struct buf *
nandfs_geteblk(int size, int flags)
{
	struct buf *bp;

	/*
	 * XXX
	 * Right now we can call geteblk with GB_NOWAIT_BD flag, which means
	 * it can return NULL. But we cannot afford to get NULL, hence this panic.
	 */
	bp = geteblk(size, flags);
	if (bp == NULL)
		panic("geteblk returned NULL");

	return (bp);
}

void
nandfs_dirty_bufs_increment(struct nandfs_device *fsdev)
{

	mtx_lock(&fsdev->nd_mutex);
	KASSERT(fsdev->nd_dirty_bufs >= 0, ("negative nd_dirty_bufs"));
	fsdev->nd_dirty_bufs++;
	mtx_unlock(&fsdev->nd_mutex);
}

void
nandfs_dirty_bufs_decrement(struct nandfs_device *fsdev)
{

	mtx_lock(&fsdev->nd_mutex);
	KASSERT(fsdev->nd_dirty_bufs > 0,
	    ("decrementing not-positive nd_dirty_bufs"));
	fsdev->nd_dirty_bufs--;
	mtx_unlock(&fsdev->nd_mutex);
}
