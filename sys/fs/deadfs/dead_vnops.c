/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 *
 *	@(#)dead_vnops.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/poll.h>
#include <sys/vnode.h>

/*
 * Prototypes for dead operations on vnodes.
 */
static vop_lookup_t	dead_lookup;
static vop_open_t	dead_open;
static vop_getwritemount_t dead_getwritemount;
static vop_rename_t	dead_rename;

struct vop_vector dead_vnodeops = {
	.vop_default =		&default_vnodeops,

	.vop_access =		VOP_EBADF,
	.vop_advlock =		VOP_EBADF,
	.vop_bmap =		VOP_EBADF,
	.vop_create =		VOP_PANIC,
	.vop_getattr =		VOP_EBADF,
	.vop_getwritemount =	dead_getwritemount,
	.vop_inactive =		VOP_NULL,
	.vop_ioctl =		VOP_EBADF,
	.vop_link =		VOP_PANIC,
	.vop_lookup =		dead_lookup,
	.vop_mkdir =		VOP_PANIC,
	.vop_mknod =		VOP_PANIC,
	.vop_open =		dead_open,
	.vop_pathconf =		VOP_EBADF,	/* per pathconf(2) */
	.vop_poll =		dead_poll,
	.vop_read =		dead_read,
	.vop_readdir =		VOP_EBADF,
	.vop_readlink =		VOP_EBADF,
	.vop_reclaim =		VOP_NULL,
	.vop_remove =		VOP_PANIC,
	.vop_rename =		dead_rename,
	.vop_rmdir =		VOP_PANIC,
	.vop_setattr =		VOP_EBADF,
	.vop_symlink =		VOP_PANIC,
	.vop_vptocnp =		VOP_EBADF,
	.vop_write =		dead_write,
};

static int
dead_getwritemount(struct vop_getwritemount_args *ap)
{

	*(ap->a_mpp) = NULL;
	return (0);
}

/*
 * Trivial lookup routine that always fails.
 */
static int
dead_lookup(struct vop_lookup_args *ap)
{

	*ap->a_vpp = NULL;
	return (ENOTDIR);
}

/*
 * Open always fails as if device did not exist.
 */
static int
dead_open(struct vop_open_args *ap)
{

	return (ENXIO);
}

int
dead_read(struct vop_read_args *ap)
{

	/*
	 * Return EOF for tty devices, EIO for others
	 */
	if ((ap->a_vp->v_vflag & VV_ISTTY) == 0)
		return (EIO);
	return (0);
}

int
dead_write(struct vop_write_args *ap)
{

	return (EIO);
}

int
dead_poll(struct vop_poll_args *ap)
{

	if (ap->a_events & ~POLLSTANDARD)
		return (POLLNVAL);

	/*
	 * Let the user find out that the descriptor is gone.
	 */
	return (POLLHUP | ((POLLIN | POLLRDNORM) & ap->a_events));

}

static int
dead_rename(struct vop_rename_args *ap)
{

	vop_rename_fail(ap);
	return (EXDEV);
}
