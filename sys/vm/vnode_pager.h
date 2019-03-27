/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990 University of Utah.
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
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
 *	@(#)vnode_pager.h	8.1 (Berkeley) 6/11/93
 * $FreeBSD$
 */

#ifndef	_VNODE_PAGER_
#define	_VNODE_PAGER_	1

#ifdef _KERNEL

int vnode_pager_generic_getpages(struct vnode *vp, vm_page_t *m,
    int count, int *rbehind, int *rahead, vop_getpages_iodone_t iodone,
    void *arg);
int vnode_pager_generic_putpages(struct vnode *vp, vm_page_t *m,
    int count, int flags, int *rtvals);
int vnode_pager_local_getpages(struct vop_getpages_args *ap);
int vnode_pager_local_getpages_async(struct vop_getpages_async_args *ap);
int vnode_pager_putpages_ioflags(int pager_flags);
void vnode_pager_release_writecount(vm_object_t object, vm_offset_t start,
    vm_offset_t end);
void vnode_pager_undirty_pages(vm_page_t *ma, int *rtvals, int written,
    off_t eof, int lpos);
void vnode_pager_update_writecount(vm_object_t object, vm_offset_t start,
    vm_offset_t end);

#endif				/* _KERNEL */
#endif				/* _VNODE_PAGER_ */
