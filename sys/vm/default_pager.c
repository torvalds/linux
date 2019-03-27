/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995, David Greenman
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by David Greenman.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/rwlock.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/swap_pager.h>

static vm_object_t	default_pager_alloc(void *, vm_ooffset_t, vm_prot_t,
			    vm_ooffset_t, struct ucred *);
static void		default_pager_dealloc(vm_object_t);
static int		default_pager_getpages(vm_object_t, vm_page_t *, int,
			    int *, int *);
static void		default_pager_putpages(vm_object_t, vm_page_t *, int, 
			    boolean_t, int *);
static boolean_t	default_pager_haspage(vm_object_t, vm_pindex_t, int *, 
			    int *);

/*
 * pagerops for OBJT_DEFAULT - "default pager".
 *
 * This pager handles anonymous (no handle) swap-backed memory, just
 * like the swap pager.  It allows several optimizations based on the
 * fact that no pages of a default object can be swapped out.  The
 * most important optimization is in vm_fault(), where the pager is
 * never asked for a non-resident page.  Instead, a freshly allocated
 * zeroed page is used.
 *
 * On the first request to page out a page from a default object, the
 * object is converted to swap pager type.
 */
struct pagerops defaultpagerops = {
	.pgo_alloc =	default_pager_alloc,
	.pgo_dealloc =	default_pager_dealloc,
	.pgo_getpages =	default_pager_getpages,
	.pgo_putpages =	default_pager_putpages,
	.pgo_haspage =	default_pager_haspage,
};

/*
 * Return an initialized object.
 */
static vm_object_t
default_pager_alloc(void *handle, vm_ooffset_t size, vm_prot_t prot,
    vm_ooffset_t offset, struct ucred *cred)
{
	vm_object_t object;

	if (handle != NULL)
		panic("default_pager_alloc: handle specified");
	if (cred != NULL) {
		if (!swap_reserve_by_cred(size, cred))
			return (NULL);
		crhold(cred);
	}
	object = vm_object_allocate(OBJT_DEFAULT,
	    OFF_TO_IDX(round_page(offset + size)));
	if (cred != NULL) {
		object->cred = cred;
		object->charge = size;
	}
	return (object);
}

/*
 * Deallocate resources associated with the object.
 */
static void
default_pager_dealloc(vm_object_t object)
{

	/* Reserved swap is released by vm_object_destroy(). */
	object->type = OBJT_DEAD;
}

/*
 * Load pages from backing store.
 */
static int
default_pager_getpages(vm_object_t object, vm_page_t *m, int count,
    int *rbehind, int *rahead)
{

	/*
	 * Since an OBJT_DEFAULT object is converted to OBJT_SWAP by the first
	 * call to the putpages method, this function will never be called on
	 * a vm_page with assigned swap.
	 */
	return (VM_PAGER_FAIL);
}

/*
 * Store pages to backing store.
 */
static void
default_pager_putpages(vm_object_t object, vm_page_t *m, int count,
    int flags, int *rtvals)
{

	/* The swap pager will convert the object to OBJT_SWAP. */
	swappagerops.pgo_putpages(object, m, count, flags, rtvals);
}

/*
 * Tell us whether the requested (object,index) is available from the object's
 * backing store.
 */
static boolean_t
default_pager_haspage(vm_object_t object, vm_pindex_t pindex, int *before,
    int *after)
{

	/* An OBJT_DEFAULT object has no backing store. */
	return (FALSE);
}

