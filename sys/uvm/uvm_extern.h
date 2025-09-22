/*	$OpenBSD: uvm_extern.h,v 1.184 2025/06/03 08:38:17 mpi Exp $	*/
/*	$NetBSD: uvm_extern.h,v 1.57 2001/03/09 01:02:12 chs Exp $	*/

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
 */

/*-
 * Copyright (c) 1991, 1992, 1993
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
 *	@(#)vm_extern.h	8.5 (Berkeley) 5/3/95
 */

#ifndef _UVM_UVM_EXTERN_H_
#define _UVM_UVM_EXTERN_H_

typedef int vm_fault_t;

typedef int vm_inherit_t;	/* XXX: inheritance codes */
typedef off_t voff_t;		/* XXX: offset within a uvm_object */

struct vm_map_entry;
typedef struct vm_map_entry *vm_map_entry_t;

struct vm_map;
typedef struct vm_map *vm_map_t;

struct vm_page;
typedef struct vm_page  *vm_page_t;

/*
 * Bit assignments assigned by UVM_MAPFLAG() and extracted by
 * UVM_{PROTECTION,INHERIT,MAXPROTECTION,ADVICE}():
 * bits 0-2	protection
 *  bit 3	 unused
 * bits 4-5	inheritance
 *  bits 6-7	 unused
 * bits 8-10	max protection
 *  bit 11	 unused
 * bits 12-14	advice
 *  bit 15	 unused
 * bits 16-N	flags
 */

/* protections bits */
#define PROT_MASK	(PROT_READ | PROT_WRITE | PROT_EXEC)

/* inherit codes */
#define MAP_INHERIT_MASK	0x3	/* inherit mask */

typedef int		vm_prot_t;

#define MADV_MASK	0x7	/* mask */

/* mapping flags */
#define UVM_FLAG_FIXED   0x0010000 /* find space */
#define UVM_FLAG_OVERLAY 0x0020000 /* establish overlay */
#define UVM_FLAG_NOMERGE 0x0040000 /* don't merge map entries */
#define UVM_FLAG_COPYONW 0x0080000 /* set copy_on_write flag */
#define UVM_FLAG_TRYLOCK 0x0100000 /* fail if we can not lock map */
#define UVM_FLAG_HOLE    0x0200000 /* no backend */
#define UVM_FLAG_QUERY   0x0400000 /* do everything, except actual execution */
#define UVM_FLAG_NOFAULT 0x0800000 /* don't fault */
#define UVM_FLAG_UNMAP   0x1000000 /* unmap to make space */
#define UVM_FLAG_STACK   0x2000000 /* page may contain a stack */
#define UVM_FLAG_WC      0x4000000 /* write combining */
#define UVM_FLAG_CONCEAL 0x8000000 /* omit from dumps */
#define UVM_FLAG_SIGALTSTACK 0x20000000 /* sigaltstack validation required */

/* macros to extract info */
#define UVM_PROTECTION(X)	((X) & PROT_MASK)
#define UVM_INHERIT(X)		(((X) >> 4) & MAP_INHERIT_MASK)
#define UVM_MAXPROTECTION(X)	(((X) >> 8) & PROT_MASK)
#define UVM_ADVICE(X)		(((X) >> 12) & MADV_MASK)

#define UVM_MAPFLAG(prot, maxprot, inh, advice, flags) \
	((prot) | ((maxprot) << 8) | ((inh) << 4) | ((advice) << 12) | (flags))

/* magic offset value */
#define UVM_UNKNOWN_OFFSET ((voff_t) -1)
				/* offset not known(obj) or don't care(!obj) */

/*
 * the following defines are for uvm_km_kmemalloc's flags
 */
#define UVM_KMF_NOWAIT	0x1			/* matches M_NOWAIT */
#define UVM_KMF_VALLOC	0x2			/* allocate VA only */
#define UVM_KMF_CANFAIL	0x4			/* caller handles failure */
#define UVM_KMF_ZERO	0x08			/* zero pages */
#define UVM_KMF_TRYLOCK	UVM_FLAG_TRYLOCK	/* try locking only */

/*
 * flags for uvm_pagealloc()
 */
#define UVM_PGA_USERESERVE	0x0001	/* ok to use reserve pages */
#define	UVM_PGA_ZERO		0x0002	/* returned page must be zeroed */

/*
 * flags for uvm_pglistalloc() also used by uvm_pmr_getpages()
 */
#define UVM_PLA_WAITOK		0x0001	/* may sleep */
#define UVM_PLA_NOWAIT		0x0002	/* can't sleep (need one of the two) */
#define UVM_PLA_ZERO		0x0004	/* zero all pages before returning */
#define UVM_PLA_TRYCONTIG	0x0008	/* try to allocate contig physmem */
#define UVM_PLA_FAILOK		0x0010	/* caller can handle failure */
#define UVM_PLA_NOWAKE		0x0020	/* don't wake page daemon on failure */
#define UVM_PLA_USERESERVE	0x0040	/* can allocate from kernel reserve */

/*
 * lockflags that control the locking behavior of various functions.
 */
#define	UVM_LK_ENTER	0x00000001	/* map locked on entry */
#define	UVM_LK_EXIT	0x00000002	/* leave map locked on exit */

/*
 * flags to uvm_page_physload.
 */
#define	PHYSLOAD_DEVICE	0x01	/* don't add to the page queue */

#include <sys/queue.h>
#include <sys/tree.h>
#include <sys/mman.h>

#ifdef _KERNEL
struct buf;
struct pglist;
struct vmspace;
struct pmap;
#endif

#include <uvm/uvm_param.h>

#include <uvm/uvm_pmap.h>
#include <uvm/uvm_object.h>
#include <uvm/uvm_page.h>
#include <uvm/uvm_map.h>

#ifdef _KERNEL
#include <uvm/uvm_fault.h>
#include <uvm/uvm_pager.h>
#endif

/*
 * Shareable process virtual address space.
 * May eventually be merged with vm_map.
 * Several fields are temporary (text, data stuff).
 *
 *  Locks used to protect struct members in this file:
 *	K	kernel lock
 *	I	immutable after creation
 *	a	atomic operations
 *	v	vm_map's lock
 */
struct vmspace {
	struct	vm_map vm_map;	/* VM address map */
	int	vm_refcnt;	/* [a] number of references */
	caddr_t	vm_shm;		/* SYS5 shared memory private data XXX */
/* we copy from vm_startcopy to the end of the structure on fork */
#define vm_startcopy vm_rssize
	segsz_t vm_rssize; 	/* current resident set size in pages */
	segsz_t vm_swrss;	/* resident set size before last swap */
	segsz_t vm_tsize;	/* text size (pages) XXX */
	segsz_t vm_dsize;	/* data size (pages) XXX */
	segsz_t vm_dused;	/* data segment length (pages) XXX */
	segsz_t vm_ssize;	/* [v] stack size (pages) */
	caddr_t	vm_taddr;	/* [I] user virtual address of text */
	caddr_t	vm_daddr;	/* [I] user virtual address of data */
	caddr_t vm_maxsaddr;	/* [I] user VA at max stack growth */
	caddr_t vm_minsaddr;	/* [I] user VA at top of stack */
};

/*
 * uvm_constraint_range's:
 * MD code is allowed to setup constraint ranges for memory allocators, the
 * primary use for this is to keep allocation for certain memory consumers
 * such as mbuf pools within address ranges that are reachable by devices
 * that perform DMA.
 *
 * It is also to discourage memory allocations from being satisfied from ranges
 * such as the ISA memory range, if they can be satisfied with allocation
 * from other ranges.
 *
 * the MD ranges are defined in arch/ARCH/ARCH/machdep.c
 */
struct uvm_constraint_range {
	paddr_t	ucr_low;
	paddr_t ucr_high;
};

#ifdef _KERNEL

#include <uvm/uvmexp.h>
extern struct uvmexp uvmexp;

/* Constraint ranges, set by MD code. */
extern struct uvm_constraint_range  isa_constraint;
extern struct uvm_constraint_range  dma_constraint;
extern struct uvm_constraint_range  no_constraint;
extern struct uvm_constraint_range *uvm_md_constraints[];

/*
 * the various kernel maps, owned by MD code
 */
extern struct vm_map *exec_map;
extern struct vm_map *kernel_map;
extern struct vm_map *kmem_map;
extern struct vm_map *phys_map;

/* base of kernel virtual memory */
extern vaddr_t vm_min_kernel_address;

#define vm_resident_count(vm) (pmap_resident_count((vm)->vm_map.pmap))

struct plimit;

void			vmapbuf(struct buf *, vsize_t);
void			vunmapbuf(struct buf *, vsize_t);
struct uvm_object	*uao_create(vsize_t, int);
void			uao_detach(struct uvm_object *);
void			uao_reference(struct uvm_object *);
int			uvm_fault(vm_map_t, vaddr_t, vm_fault_t, vm_prot_t);

vaddr_t			uvm_uarea_alloc(void);
void			uvm_uarea_free(struct proc *);
void			uvm_purge(void);
void			uvm_exit(struct process *);
void			uvm_init_limits(struct plimit *);
boolean_t		uvm_kernacc(caddr_t, size_t, int);

int			uvm_vslock(struct proc *, caddr_t, size_t,
			    vm_prot_t);
void			uvm_vsunlock(struct proc *, caddr_t, size_t);
int			uvm_vslock_device(struct proc *, void *, size_t,
			    vm_prot_t, void **);
void			uvm_vsunlock_device(struct proc *, void *, size_t,
			    void *);
void			uvm_init(void);	
void			uvm_init_percpu(void);
int			uvm_io(vm_map_t, struct uio *, int);

#define	UVM_IO_FIXPROT	0x01

void			uvm_km_free(vm_map_t, vaddr_t, vsize_t);
vaddr_t			uvm_km_kmemalloc_pla(struct vm_map *,
			    struct uvm_object *, vsize_t, vsize_t, int,
			    paddr_t, paddr_t, paddr_t, paddr_t, int);
#define uvm_km_kmemalloc(map, obj, sz, flags)				\
	uvm_km_kmemalloc_pla(map, obj, sz, 0, flags, 0, (paddr_t)-1, 0, 0, 0)
struct vm_map		*uvm_km_suballoc(vm_map_t, vaddr_t *, vaddr_t *,
			    vsize_t, int, boolean_t, vm_map_t);
/*
 * Allocation mode for virtual space.
 *
 *  kv_map - pointer to the pointer to the map we're allocating from.
 *  kv_align - alignment.
 *  kv_wait - wait for free space in the map if it's full. The default
 *   allocators don't wait since running out of space in kernel_map and
 *   kmem_map is usually fatal. Special maps like exec_map are specifically
 *   limited, so waiting for space in them is necessary.
 *  kv_singlepage - use the single page allocator.
 *  kv_executable - map the physical pages with PROT_EXEC.
 */
struct kmem_va_mode {
	struct vm_map **kv_map;
	vsize_t kv_align;
	char kv_wait;
	char kv_singlepage;
};

/*
 * Allocation mode for physical pages.
 *
 *  kp_constraint - allocation constraint for physical pages.
 *  kp_object - if the pages should be allocated from an object.
 *  kp_align - physical alignment of the first page in the allocation.
 *  kp_boundary - boundary that the physical addresses can't cross if
 *   the allocation is contiguous.
 *  kp_nomem - don't allocate any backing pages.
 *  kp_maxseg - maximal amount of contiguous segments.
 *  kp_zero - zero the returned memory.
 *  kp_pageable - allocate pageable memory.
 */
struct kmem_pa_mode {
	struct uvm_constraint_range *kp_constraint;
	struct uvm_object **kp_object;
	paddr_t kp_align;
	paddr_t kp_boundary;
	int kp_maxseg;
	char kp_nomem;
	char kp_zero;
	char kp_pageable;
};

/*
 * Dynamic allocation parameters. Stuff that changes too often or too much
 * to create separate va and pa modes for.
 *
 * kd_waitok - is it ok to sleep?
 * kd_trylock - don't sleep on map locks.
 * kd_prefer - offset to feed to PMAP_PREFER
 * kd_slowdown - special parameter for the singlepage va allocator
 *  that tells the caller to sleep if possible to let the singlepage
 *  allocator catch up.
 */
struct kmem_dyn_mode {
	voff_t kd_prefer;
	int *kd_slowdown;
	char kd_waitok;
	char kd_trylock;
};

#define KMEM_DYN_INITIALIZER { UVM_UNKNOWN_OFFSET, NULL, 0, 0 }

/*
 * Notice that for kv_ waiting has a different meaning. It's only supposed
 * to be used for very space constrained maps where waiting is a way
 * to throttle some other operation.
 * The exception is kv_page which needs to wait relatively often.
 * All kv_ except kv_intrsafe will potentially sleep.
 */
extern const struct kmem_va_mode kv_any;
extern const struct kmem_va_mode kv_intrsafe;
extern const struct kmem_va_mode kv_page;

extern const struct kmem_pa_mode kp_dirty;
extern const struct kmem_pa_mode kp_zero;
extern const struct kmem_pa_mode kp_dma;
extern const struct kmem_pa_mode kp_dma_contig;
extern const struct kmem_pa_mode kp_dma_zero;
extern const struct kmem_pa_mode kp_pageable;
extern const struct kmem_pa_mode kp_none;

extern const struct kmem_dyn_mode kd_waitok;
extern const struct kmem_dyn_mode kd_nowait;
extern const struct kmem_dyn_mode kd_trylock;

void			*km_alloc(size_t, const struct kmem_va_mode *,
			    const struct kmem_pa_mode *,
			    const struct kmem_dyn_mode *);
void			km_free(void *, size_t, const struct kmem_va_mode *,
			    const struct kmem_pa_mode *);
int			uvm_map(vm_map_t, vaddr_t *, vsize_t,
			    struct uvm_object *, voff_t, vsize_t, unsigned int);
int			uvm_mapanon(vm_map_t, vaddr_t *, vsize_t, vsize_t, unsigned int);
int			uvm_map_pageable(vm_map_t, vaddr_t, 
			    vaddr_t, boolean_t, int);
int			uvm_map_pageable_all(vm_map_t, int, vsize_t);
boolean_t		uvm_map_checkprot(vm_map_t, vaddr_t,
			    vaddr_t, vm_prot_t);
int			uvm_map_protect(vm_map_t, vaddr_t, 
			    vaddr_t, vm_prot_t, int etype, boolean_t, boolean_t);
struct vmspace		*uvmspace_alloc(vaddr_t, vaddr_t,
			    boolean_t, boolean_t);
void			uvmspace_init(struct vmspace *, struct pmap *,
			    vaddr_t, vaddr_t, boolean_t, boolean_t);
void			uvmspace_exec(struct proc *, vaddr_t, vaddr_t);
struct vmspace		*uvmspace_fork(struct process *);
void			uvmspace_addref(struct vmspace *);
void			uvmspace_purge(struct vmspace *);
void			uvmspace_free(struct vmspace *);
struct vmspace		*uvmspace_share(struct process *);
int			uvm_sysctl(int *, u_int, void *, size_t *, 
			    void *, size_t, struct proc *);
struct vm_page		*uvm_pagealloc(struct uvm_object *,
			    voff_t, struct vm_anon *, int);
int			uvm_pagealloc_multi(struct uvm_object *, voff_t,
    			    vsize_t, int);
void			uvm_pagerealloc(struct vm_page *, 
			    struct uvm_object *, voff_t);
int			uvm_pagerealloc_multi(struct uvm_object *, voff_t,
			    vsize_t, int, struct uvm_constraint_range *);
/* Actually, uvm_page_physload takes PF#s which need their own type */
void			uvm_page_physload(paddr_t, paddr_t, paddr_t,
			    paddr_t, int);
void			uvm_setpagesize(void);
void			uvm_shutdown(void);
void			uvm_aio_biodone(struct buf *);
void			uvm_aio_aiodone(struct buf *);
void			uvm_pageout(void *);
void			uvm_aiodone_daemon(void *);
void			uvm_wait(const char *);
int			uvm_pglistalloc(psize_t, paddr_t, paddr_t,
			    paddr_t, paddr_t, struct pglist *, int, int);
void			uvm_pglistfree(struct pglist *);
void			uvm_pmr_use_inc(paddr_t, paddr_t);
void			uvm_swap_init(void);
typedef int		uvm_coredump_setup_cb(int _nsegment, void *_cookie);
typedef int		uvm_coredump_walk_cb(vaddr_t _start, vaddr_t _realend,
			    vaddr_t _end, vm_prot_t _prot, int _isvnode,
			    int _nsegment, void *_cookie);
int			uvm_coredump_walkmap(struct proc *_p,
			    uvm_coredump_setup_cb *_setup,
			    uvm_coredump_walk_cb *_walk, void *_cookie);
void			uvm_grow(struct proc *, vaddr_t);
void			uvm_pagezero_thread(void *);
void			kmeminit_nkmempages(void);
void			kmeminit(void);
extern u_int		nkmempages;

struct vnode;
struct uvm_object	*uvn_attach(struct vnode *, vm_prot_t);

struct process;
struct kinfo_vmentry;
int			fill_vmmap(struct process *, struct kinfo_vmentry *,
			    size_t *);

#endif /* _KERNEL */

#endif /* _UVM_UVM_EXTERN_H_ */
