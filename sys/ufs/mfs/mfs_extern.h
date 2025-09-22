/*	$OpenBSD: mfs_extern.h,v 1.22 2021/10/02 08:51:41 semarie Exp $	*/
/*	$NetBSD: mfs_extern.h,v 1.4 1996/02/09 22:31:27 christos Exp $	*/

/*-
 * Copyright (c) 1991, 1993
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
 *	@(#)mfs_extern.h	8.2 (Berkeley) 6/16/94
 */

struct buf;
struct mount;
struct nameidata;
struct proc;
struct statfs;
struct ucred;
struct vnode;
struct vfsconf;
struct mbuf;
struct mfsnode;

extern const struct vops mfs_vops;

/* mfs_vfsops.c */
int mfs_mount(struct mount *, const char *, void *, struct nameidata *,
    struct proc *);
int mfs_start(struct mount *, int, struct proc *);
int mfs_init(struct vfsconf *);
int mfs_checkexp(struct mount *, struct mbuf *, int *, struct ucred **);

/* mfs_vnops.c */
int mfs_open(void *);
int mfs_ioctl(void *);
int mfs_strategy(void *);
void mfs_doio(struct mfsnode *, struct buf *);
int mfs_close(void *);
int mfs_inactive(void *);
int mfs_reclaim(void *);
int mfs_print(void *);

