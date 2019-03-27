/*-
 * Copyright (c) 2013 EMC Corp.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _OPENSOLARIS_SYS_VM_H_
#define	_OPENSOLARIS_SYS_VM_H_

#ifdef _KERNEL

#include <sys/sf_buf.h>

extern const int zfs_vm_pagerret_bad;
extern const int zfs_vm_pagerret_error;
extern const int zfs_vm_pagerret_ok;
extern const int zfs_vm_pagerput_sync;
extern const int zfs_vm_pagerput_inval;

void	zfs_vmobject_assert_wlocked(vm_object_t object);
void	zfs_vmobject_wlock(vm_object_t object);
void	zfs_vmobject_wunlock(vm_object_t object);

static inline caddr_t
zfs_map_page(vm_page_t pp, struct sf_buf **sfp)
{
	*sfp = sf_buf_alloc(pp, 0);
	return ((caddr_t)sf_buf_kva(*sfp));
}

static inline void
zfs_unmap_page(struct sf_buf *sf)
{
	sf_buf_free(sf);
}

#endif	/* _KERNEL */

#endif	/* _OPENSOLARIS_SYS_VM_H_ */
