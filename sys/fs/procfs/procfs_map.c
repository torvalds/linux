/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1993 Jan-Simon Pendry
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry.
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
 *	@(#)procfs_status.c	8.3 (Berkeley) 2/17/94
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/filedesc.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/rwlock.h>
#include <sys/sbuf.h>
#ifdef COMPAT_FREEBSD32
#include <sys/sysent.h>
#endif
#include <sys/uio.h>
#include <sys/vnode.h>

#include <fs/pseudofs/pseudofs.h>
#include <fs/procfs/procfs.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_object.h>

#define MEBUFFERSIZE 256

/*
 * The map entries can *almost* be read with programs like cat.  However,
 * large maps need special programs to read.  It is not easy to implement
 * a program that can sense the required size of the buffer, and then
 * subsequently do a read with the appropriate size.  This operation cannot
 * be atomic.  The best that we can do is to allow the program to do a read
 * with an arbitrarily large buffer, and return as much as we can.  We can
 * return an error code if the buffer is too small (EFBIG), then the program
 * can try a bigger buffer.
 */
int
procfs_doprocmap(PFS_FILL_ARGS)
{
	struct vmspace *vm;
	vm_map_t map;
	vm_map_entry_t entry, tmp_entry;
	struct vnode *vp;
	char *fullpath, *freepath, *type;
	struct ucred *cred;
	vm_object_t obj, tobj, lobj;
	int error, privateresident, ref_count, resident, shadow_count, flags;
	vm_offset_t e_start, e_end;
	vm_eflags_t e_eflags;
	vm_prot_t e_prot;
	unsigned int last_timestamp;
	bool super;
#ifdef COMPAT_FREEBSD32
	bool wrap32;
#endif

	PROC_LOCK(p);
	error = p_candebug(td, p);
	PROC_UNLOCK(p);
	if (error)
		return (error);

	if (uio->uio_rw != UIO_READ)
		return (EOPNOTSUPP);

#ifdef COMPAT_FREEBSD32
	wrap32 = false;
	if (SV_CURPROC_FLAG(SV_ILP32)) {
		if (!(SV_PROC_FLAG(p, SV_ILP32)))
			return (EOPNOTSUPP);
		wrap32 = true;
	}
#endif

	vm = vmspace_acquire_ref(p);
	if (vm == NULL)
		return (ESRCH);
	map = &vm->vm_map;
	vm_map_lock_read(map);
	for (entry = map->header.next; entry != &map->header;
	     entry = entry->next) {
		if (entry->eflags & MAP_ENTRY_IS_SUB_MAP)
			continue;

		e_eflags = entry->eflags;
		e_prot = entry->protection;
		e_start = entry->start;
		e_end = entry->end;
		privateresident = 0;
		resident = 0;
		obj = entry->object.vm_object;
		if (obj != NULL) {
			VM_OBJECT_RLOCK(obj);
			if (obj->shadow_count == 1)
				privateresident = obj->resident_page_count;
		}
		cred = (entry->cred) ? entry->cred : (obj ? obj->cred : NULL);

		for (lobj = tobj = obj; tobj != NULL;
		    tobj = tobj->backing_object) {
			if (tobj != obj)
				VM_OBJECT_RLOCK(tobj);
			lobj = tobj;
		}
		if (obj != NULL)
			kern_proc_vmmap_resident(map, entry, &resident, &super);
		for (tobj = obj; tobj != NULL; tobj = tobj->backing_object) {
			if (tobj != obj && tobj != lobj)
				VM_OBJECT_RUNLOCK(tobj);
		}
		last_timestamp = map->timestamp;
		vm_map_unlock_read(map);

		freepath = NULL;
		fullpath = "-";
		if (lobj) {
			vp = NULL;
			switch (lobj->type) {
			default:
			case OBJT_DEFAULT:
				type = "default";
				break;
			case OBJT_VNODE:
				type = "vnode";
				vp = lobj->handle;
				vref(vp);
				break;
			case OBJT_SWAP:
				if ((lobj->flags & OBJ_TMPFS_NODE) != 0) {
					type = "vnode";
					if ((lobj->flags & OBJ_TMPFS) != 0) {
						vp = lobj->un_pager.swp.swp_tmpfs;
						vref(vp);
					}
				} else {
					type = "swap";
				}
				break;
			case OBJT_SG:
			case OBJT_DEVICE:
				type = "device";
				break;
			}
			if (lobj != obj)
				VM_OBJECT_RUNLOCK(lobj);

			flags = obj->flags;
			ref_count = obj->ref_count;
			shadow_count = obj->shadow_count;
			VM_OBJECT_RUNLOCK(obj);
			if (vp != NULL) {
				vn_fullpath(td, vp, &fullpath, &freepath);
				vrele(vp);
			}
		} else {
			type = "none";
			flags = 0;
			ref_count = 0;
			shadow_count = 0;
		}

		/*
		 * format:
		 *  start, end, resident, private resident, cow, access, type,
		 *         charged, charged uid.
		 */
		error = sbuf_printf(sb,
		    "0x%lx 0x%lx %d %d %p %s%s%s %d %d 0x%x %s %s %s %s %s %d\n",
			(u_long)e_start, (u_long)e_end,
			resident, privateresident,
#ifdef COMPAT_FREEBSD32
			wrap32 ? NULL : obj,	/* Hide 64 bit value */
#else
			obj,
#endif
			(e_prot & VM_PROT_READ)?"r":"-",
			(e_prot & VM_PROT_WRITE)?"w":"-",
			(e_prot & VM_PROT_EXECUTE)?"x":"-",
			ref_count, shadow_count, flags,
			(e_eflags & MAP_ENTRY_COW)?"COW":"NCOW",
			(e_eflags & MAP_ENTRY_NEEDS_COPY)?"NC":"NNC",
			type, fullpath,
			cred ? "CH":"NCH", cred ? cred->cr_ruid : -1);

		if (freepath != NULL)
			free(freepath, M_TEMP);
		vm_map_lock_read(map);
		if (error == -1) {
			error = 0;
			break;
		}
		if (last_timestamp != map->timestamp) {
			/*
			 * Look again for the entry because the map was
			 * modified while it was unlocked.  Specifically,
			 * the entry may have been clipped, merged, or deleted.
			 */
			vm_map_lookup_entry(map, e_end - 1, &tmp_entry);
			entry = tmp_entry;
		}
	}
	vm_map_unlock_read(map);
	vmspace_free(vm);
	return (error);
}
