/*	$OpenBSD: uvm_object.h,v 1.30 2022/09/04 06:49:11 jsg Exp $	*/
/*	$NetBSD: uvm_object.h,v 1.11 2001/03/09 01:02:12 chs Exp $	*/

/*
 * Copyright (c) 1997 Charles D. Cranor and Washington University.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * from: Id: uvm_object.h,v 1.1.2.2 1998/01/04 22:44:51 chuck Exp
 */

#ifndef _UVM_UVM_OBJECT_H_
#define _UVM_UVM_OBJECT_H_

/*
 * The UVM memory object interface.  Notes:
 *
 * A UVM memory object represents a list of pages, which are managed by
 * the object's pager operations (uvm_object::pgops).  All pages belonging
 * to an object are owned by it and thus protected by the object lock.
 *
 * The lock (uvm_object::vmobjlock) may be shared amongst the UVM objects.
 * By default, the lock is allocated dynamically using rw_obj_init() cache.
 * Lock sharing is normally used when there is an underlying object.  For
 * example, vnode representing a file may have an underlying node, which
 * is the case for tmpfs and layered file systems.  In such case, vnode's
 * UVM object and the underlying UVM object shares the lock.
 *
 * The reference count is managed atomically for the anonymous UVM objects.
 * For other objects, it is arbitrary (may use the lock or atomics).
 */

struct uvm_object {
	struct rwlock			*vmobjlock;	/* lock on object */
	const struct uvm_pagerops	*pgops;		/* pager ops */
	RBT_HEAD(uvm_objtree, vm_page)	 memt;		/* pages in object */
	int				 uo_npages;	/* # of pages in memt */
	int				 uo_refs;	/* reference count */
};

/*
 * UVM_OBJ_KERN is a 'special' uo_refs value which indicates that the
 * object is a kernel memory object rather than a normal one (kernel
 * memory objects don't have reference counts -- they never die).
 *
 * this value is used to detected kernel object mappings at uvm_unmap()
 * time.   normally when an object is unmapped its pages eventually become
 * deactivated and then paged out and/or freed.    this is not useful
 * for kernel objects... when a kernel object is unmapped we always want
 * to free the resources associated with the mapping.   UVM_OBJ_KERN
 * allows us to decide which type of unmapping we want to do.
 *
 * in addition, we have kernel objects which may be used in an
 * interrupt context.  these objects get their mappings entered
 * with pmap_kenter*() and removed with pmap_kremove(), which
 * are safe to call in interrupt context, and must be used ONLY
 * for wired kernel mappings in these objects and their associated
 * maps.
 */
#define UVM_OBJ_KERN		(-2)

#define	UVM_OBJ_IS_KERN_OBJECT(uobj)					\
	((uobj)->uo_refs == UVM_OBJ_KERN)

#ifdef _KERNEL

extern const struct uvm_pagerops uvm_vnodeops;
extern const struct uvm_pagerops uvm_deviceops;
extern const struct uvm_pagerops pmap_pager;
extern const struct uvm_pagerops bufcache_pager;

/* For object trees */
int	uvm_pagecmp(const struct vm_page *, const struct vm_page *);
RBT_PROTOTYPE(uvm_objtree, vm_page, objt, uvm_pagecmp)

#define	UVM_OBJ_IS_VNODE(uobj)						\
	((uobj)->pgops == &uvm_vnodeops)

#define	UVM_OBJ_IS_DEVICE(uobj)						\
	((uobj)->pgops == &uvm_deviceops)

#define	UVM_OBJ_IS_VTEXT(uobj)						\
	((uobj)->pgops == &uvm_vnodeops &&				\
	 ((struct vnode *)uobj)->v_flag & VTEXT)

#define	UVM_OBJ_IS_AOBJ(uobj)						\
	((uobj)->pgops == &aobj_pager)

#define UVM_OBJ_IS_PMAP(uobj)						\
	((uobj)->pgops == &pmap_pager)

#define UVM_OBJ_IS_BUFCACHE(uobj)					\
	((uobj)->pgops == &bufcache_pager)

#define UVM_OBJ_IS_DUMMY(uobj)						\
	(UVM_OBJ_IS_PMAP(uobj) || UVM_OBJ_IS_BUFCACHE(uobj))

void	uvm_obj_init(struct uvm_object *, const struct uvm_pagerops *, int);
void	uvm_obj_destroy(struct uvm_object *);
void	uvm_obj_setlock(struct uvm_object *, struct rwlock *);
int	uvm_obj_wire(struct uvm_object *, voff_t, voff_t, struct pglist *);
void	uvm_obj_unwire(struct uvm_object *, voff_t, voff_t);
void	uvm_obj_free(struct uvm_object *);

#endif /* _KERNEL */

#endif /* _UVM_UVM_OBJECT_H_ */
