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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/freebsd_rwlock.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>

const int zfs_vm_pagerret_bad = VM_PAGER_BAD;
const int zfs_vm_pagerret_error = VM_PAGER_ERROR;
const int zfs_vm_pagerret_ok = VM_PAGER_OK;
const int zfs_vm_pagerput_sync = VM_PAGER_PUT_SYNC;
const int zfs_vm_pagerput_inval = VM_PAGER_PUT_INVAL;

void
zfs_vmobject_assert_wlocked(vm_object_t object)
{

	/*
	 * This is not ideal because FILE/LINE used by assertions will not
	 * be too helpful, but it must be an hard function for
	 * compatibility reasons.
	 */
	VM_OBJECT_ASSERT_WLOCKED(object);
}

void
zfs_vmobject_wlock(vm_object_t object)
{

	VM_OBJECT_WLOCK(object);
}

void
zfs_vmobject_wunlock(vm_object_t object)
{

	VM_OBJECT_WUNLOCK(object);
}
