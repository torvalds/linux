/*	$OpenBSD: ntfs_vfsops.h,v 1.4 2020/02/27 09:10:31 mpi Exp $	*/
/*	$NetBSD: ntfs_vfsops.h,v 1.1 2002/12/23 17:38:34 jdolecek Exp $	*/

/*-
 * Copyright (c) 1998, 1999 Semen Ustimenko (semenu@FreeBSD.org)
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
 *
 *	Id: ntfs_vfsops.h,v 1.3 1999/05/12 09:43:06 semenu Exp
 */
#define VG_DONTLOADIN	0x0001	/* Tells ntfs_vgetex to do not call */
				/* ntfs_loadntnode() on ntnode, even if */
				/* ntnode not loaded */
#define	VG_DONTVALIDFN	0x0002	/* Tells ntfs_vgetex to do not validate */
				/* fnode */
#define	VG_EXT		0x0004	/* This is not main record */

int ntfs_vgetex(struct mount *, ntfsino_t, u_int32_t, char *, u_long, u_long,
		struct vnode **);
int ntfs_calccfree(struct ntfsmount *, cn_t *);
