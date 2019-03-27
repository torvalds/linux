/*-
 * Copyright (c) 2016 Nuxi, https://nuxi.nl/
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

#include <sys/types.h>
#include <sys/lock.h>
#include <sys/sysent.h>
#include <sys/rwlock.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>

#include <compat/cloudabi/cloudabi_util.h>

void
cloudabi_vdso_init(struct sysentvec *sv, char *begin, char *end)
{
	vm_page_t m;
	vm_object_t obj;
	vm_offset_t addr;
	size_t i, pages, pages_length, vdso_length;

	/* Determine the number of pages needed to store the vDSO. */
	vdso_length = end - begin;
	pages = howmany(vdso_length, PAGE_SIZE);
	pages_length = pages * PAGE_SIZE;

	/* Allocate a VM object and fill it with the vDSO. */
	obj = vm_pager_allocate(OBJT_PHYS, 0, pages_length,
	    VM_PROT_DEFAULT, 0, NULL);
	addr = kva_alloc(PAGE_SIZE);
	for (i = 0; i < pages; ++i) {
		VM_OBJECT_WLOCK(obj);
		m = vm_page_grab(obj, i, VM_ALLOC_NOBUSY | VM_ALLOC_ZERO);
		m->valid = VM_PAGE_BITS_ALL;
		VM_OBJECT_WUNLOCK(obj);

		pmap_qenter(addr, &m, 1);
		memcpy((void *)addr, begin + i * PAGE_SIZE,
		    MIN(vdso_length - i * PAGE_SIZE, PAGE_SIZE));
		pmap_qremove(addr, 1);
	}
	kva_free(addr, PAGE_SIZE);

	/*
	 * Place the vDSO at the top of the address space. The user
	 * stack can start right below it.
	 */
	sv->sv_shared_page_base = sv->sv_maxuser - pages_length;
	sv->sv_shared_page_len = pages_length;
	sv->sv_shared_page_obj = obj;
	sv->sv_usrstack = sv->sv_shared_page_base;
}

void
cloudabi_vdso_destroy(struct sysentvec *sv)
{

	vm_object_deallocate(sv->sv_shared_page_obj);
}
