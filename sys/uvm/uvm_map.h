/*	$OpenBSD: uvm_map.h,v 1.96 2025/09/14 13:06:02 mpi Exp $	*/
/*	$NetBSD: uvm_map.h,v 1.24 2001/02/18 21:19:08 chs Exp $	*/

/*
 * Copyright (c) 2011 Ariane van der Steldt <ariane@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * 
 * Copyright (c) 1997 Charles D. Cranor and Washington University.
 * Copyright (c) 1991, 1993, The Regents of the University of California.  
 *
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * The Mach Operating System project at Carnegie-Mellon University.
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
 *	@(#)vm_map.h    8.3 (Berkeley) 3/15/94
 * from: Id: uvm_map.h,v 1.1.2.3 1998/02/07 01:16:55 chs Exp
 *
 *
 * Copyright (c) 1987, 1990 Carnegie-Mellon University.
 * All rights reserved.
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#ifndef _UVM_UVM_MAP_H_
#define _UVM_UVM_MAP_H_

#include <sys/mutex.h>
#include <sys/rwlock.h>

#ifdef _KERNEL

/*
 * UVM_MAP_CLIP_START: ensure that the entry begins at or after
 * the starting address, if it doesn't we split the entry.
 * 
 * => map must be locked by caller
 */

#define UVM_MAP_CLIP_START(_map, _entry, _addr)				\
	do {								\
		KASSERT((_entry)->end + (_entry)->fspace > (_addr));	\
		if ((_entry)->start < (_addr))				\
			uvm_map_clip_start((_map), (_entry), (_addr));	\
	} while (0)

/*
 * UVM_MAP_CLIP_END: ensure that the entry ends at or before
 *      the ending address, if it doesn't we split the entry.
 *
 * => map must be locked by caller
 */

#define UVM_MAP_CLIP_END(_map, _entry, _addr)				\
	do {								\
		KASSERT((_entry)->start < (_addr));			\
		if ((_entry)->end > (_addr))				\
			uvm_map_clip_end((_map), (_entry), (_addr));	\
	} while (0)

/*
 * extract flags
 */
#define UVM_EXTRACT_FIXPROT	0x8	/* set prot to maxprot as we go */

#endif /* _KERNEL */

#include <uvm/uvm_anon.h>

/*
 * Address map entries consist of start and end addresses,
 * a VM object (or sharing map) and offset into that object,
 * and user-exported inheritance and protection information.
 * Also included is control information for virtual copy operations.
 */
struct vm_map_entry {
	union {
		RBT_ENTRY(vm_map_entry)	addr_entry; /* address tree */
		SLIST_ENTRY(vm_map_entry) addr_kentry;
	} daddrs;

	union {
		RBT_ENTRY(vm_map_entry)	rbtree;	/* Link freespace tree. */
		TAILQ_ENTRY(vm_map_entry) tailq;/* Link freespace queue. */
		TAILQ_ENTRY(vm_map_entry) deadq;/* dead entry queue */
	} dfree;

#define uvm_map_entry_start_copy start
	vaddr_t			start;		/* start address */
	vaddr_t			end;		/* end address */

	vsize_t			guard;		/* bytes in guard */
	vsize_t			fspace;		/* free space */

	union {
		struct uvm_object *uvm_obj;	/* uvm object */
		struct vm_map	*sub_map;	/* belongs to another map */
	} object;				/* object I point to */
	voff_t			offset;		/* offset into object */
	struct vm_aref		aref;		/* anonymous overlay */
	int			etype;		/* entry type */
	vm_prot_t		protection;	/* protection code */
	vm_prot_t		max_protection;	/* maximum protection */
	vm_inherit_t		inheritance;	/* inheritance */
	int			wired_count;	/* can be paged if == 0 */
	int			advice;		/* madvise advice */
#define uvm_map_entry_stop_copy flags
	u_int8_t		flags;		/* flags */

#define	UVM_MAP_STATIC		0x01		/* static map entry */
#define	UVM_MAP_KMEM		0x02		/* from kmem entry pool */

	vsize_t			fspace_augment;	/* max(fspace) in subtree */
};

#define	VM_MAPENT_ISWIRED(entry)	((entry)->wired_count != 0)

TAILQ_HEAD(uvm_map_deadq, vm_map_entry);	/* dead entry queue */
RBT_HEAD(uvm_map_addr, vm_map_entry);
#ifdef _KERNEL
RBT_PROTOTYPE(uvm_map_addr, vm_map_entry, daddrs.addr_entry,
    uvm_mapentry_addrcmp);
#endif

/*
 *	A Map is a rbtree of map entries, kept sorted by address.
 *	In addition, free space entries are also kept in a rbtree,
 *	indexed by free size.
 *
 *
 *
 *	LOCKING PROTOCOL NOTES:
 *	-----------------------
 *
 *	VM map locking is a little complicated.  There are both shared
 *	and exclusive locks on maps.  However, it is sometimes required
 *	to unlock a VM map (to prevent lock ordering issues) without
 *	allowing any other thread to modify it.
 *
 *	In order to prevent this scenario, we introduce the notion of
 *	a `busy' map.  A `busy' map is unlocked, but other threads
 *	attempting to write-lock wait for this flag to clear before
 *	entering the lock manager.  A map may only be marked busy
 *	when the map is write-locked and may only be marked unbusy by
 *	the thread which marked it busy.
 *
 *	Access to the map `flags' member is controlled by the `flags_lock'
 *	simple lock.  Note that some flags are static (set once at map
 *	creation time, and never changed), and thus require no locking
 *	to check those flags.  All flags which are r/w must be set or
 *	cleared while the `flags_lock' is asserted.  Additional locking
 *	requirements are:
 *
 *		VM_MAP_PAGEABLE		r/o static flag; no locking required
 *
 *		VM_MAP_INTRSAFE		r/o static flag; no locking required
 *
 *		VM_MAP_WIREFUTURE	r/w; may only be set or cleared when
 *					map is write-locked.  may be tested
 *					without asserting `flags_lock'.
 *
 *		VM_MAP_GUARDPAGES	r/o; must be specified at map
 *					initialization time.
 *					If set, guards will appear between
 *					automatic allocations.
 *					No locking required.
 *
 *		VM_MAP_ISVMSPACE	r/o; set by uvmspace_alloc.
 *					Signifies that this map is a vmspace.
 *					(The implementation treats all maps
 *					without this bit as kernel maps.)
 *					No locking required.
 *
 *
 * All automatic allocations (uvm_map without MAP_FIXED) will allocate
 * from vm_map.free.
 * If that allocation fails:
 * - vmspace maps will spill over into vm_map.bfree,
 * - all other maps will call uvm_map_kmem_grow() to increase the arena.
 *
 * vmspace maps have their data, brk() and stack arenas automatically
 * updated when uvm_map() is invoked without MAP_FIXED.
 * The spill over arena (vm_map.bfree) will contain the space in the brk()
 * and stack ranges.
 * Kernel maps never have a bfree arena and this tree will always be empty.
 *
 *
 * read_locks and write_locks are used in lock debugging code.
 *
 *  Locks used to protect struct members in this file:
 *	a	atomic operations
 *	I	immutable after creation or exec(2)
 *	v	`vm_map_lock' (this map `lock' or `mtx')
 *	f	flags_lock
 */
struct vm_map {
	struct pmap		*pmap;		/* [I] Physical map */
	u_long			sserial;	/* [v] # stack changes */

	struct uvm_map_addr	addr;		/* [v] Entry tree, by addr */

	vsize_t			size;		/* virtual size */
	int			ref_count;	/* [a] Reference count */
	int			flags;		/* [f] flags */
	unsigned int		timestamp;	/* Version number */
	struct proc		*busy;		/* [f] thread holding map busy*/
	unsigned int		nbusy;		/* [f] waiters for busy */

	vaddr_t			min_offset;	/* [I] First address in map. */
	vaddr_t			max_offset;	/* [I] Last address in map. */

	/*
	 * Allocation overflow regions.
	 */
	vaddr_t			b_start;	/* [v] Start for brk() alloc. */
	vaddr_t			b_end;		/* [v] End for brk() alloc. */
	vaddr_t			s_start;	/* [v] Start for stack alloc. */
	vaddr_t			s_end;		/* [v] End for stack alloc. */

	/*
	 * Special address selectors.
	 *
	 * The uaddr_exe mapping is used if:
	 * - protX is selected
	 * - the pointer is not NULL
	 *
	 * If uaddr_exe is not used, the other mappings are checked in
	 * order of appearance.
	 * If a hint is given, the selection will only be used if the hint
	 * falls in the range described by the mapping.
	 *
	 * The states are pointers because:
	 * - they may not all be in use
	 * - the struct size for different schemes is variable
	 *
	 * The uaddr_brk_stack selector will select addresses that are in
	 * the brk/stack area of the map.
	 */
	struct uvm_addr_state	*uaddr_exe;	/* Executable selector. */
	struct uvm_addr_state	*uaddr_any[4];	/* More selectors. */
	struct uvm_addr_state	*uaddr_brk_stack; /* Brk/stack selector. */

#define UVM_MAP_CHECK_COPYIN_MAX 4	/* main, sigtramp, ld.so, libc.so */
	struct uvm_check_copyin {
		vaddr_t		start, end;
	}			check_copyin[UVM_MAP_CHECK_COPYIN_MAX];
	int			check_copyin_count;

	/*
	 * XXX struct mutex changes size because of compile options, so
	 * place after fields which are inspected by libkvm / procmap(1)
	 */
	struct rwlock		lock;		/* Non-intrsafe lock */
	struct mutex		mtx;		/* Intrsafe lock */
	struct mutex		flags_lock;	/* flags lock */
};

/* vm_map flags */
#define	VM_MAP_PAGEABLE		0x01		/* ro: entries are pageable */
#define	VM_MAP_INTRSAFE		0x02		/* ro: interrupt safe map */
#define	VM_MAP_WIREFUTURE	0x04		/* rw: wire future mappings */
#define	VM_MAP_GUARDPAGES	0x20		/* rw: add guard pgs to map */
#define	VM_MAP_ISVMSPACE	0x40		/* ro: map is a vmspace */
#define	VM_MAP_PINSYSCALL_ONCE	0x100		/* rw: pinsyscall done */

/* Number of kernel maps and entries to statically allocate */
#define	MAX_KMAPENT	1024	/* Sufficient to make it to the scheduler. */

#ifdef _KERNEL
/*
 * globals:
 */

extern vaddr_t	uvm_maxkaddr;

/*
 * protos: the following prototypes define the interface to vm_map
 */

void		uvm_map_deallocate(struct vm_map *);

int		uvm_map_clean(struct vm_map *, vaddr_t, vaddr_t, int);
void		uvm_map_clip_start(struct vm_map *, struct vm_map_entry *,
		    vaddr_t);
void		uvm_map_clip_end(struct vm_map *, struct vm_map_entry *,
		    vaddr_t);
int		uvm_map_extract(struct vm_map *, vaddr_t, vsize_t,
		    vaddr_t *, int);
struct vm_map *	uvm_map_create(pmap_t, vaddr_t, vaddr_t, int);
vaddr_t		uvm_map_pie(vaddr_t);
vaddr_t		uvm_map_hint(struct vmspace *, vm_prot_t, vaddr_t, vaddr_t);
int		uvm_map_check_copyin_add(struct vm_map *, vaddr_t, vaddr_t);
int		uvm_map_immutable(struct vm_map *, vaddr_t, vaddr_t, int);
int		uvm_map_inherit(struct vm_map *, vaddr_t, vaddr_t, vm_inherit_t);
int		uvm_map_advice(struct vm_map *, vaddr_t, vaddr_t, int);
void		uvm_map_init(void);
boolean_t	uvm_map_lookup_entry(struct vm_map *, vaddr_t, vm_map_entry_t *);
boolean_t	uvm_map_is_stack_remappable(struct vm_map *, vaddr_t, vsize_t, int);
int		uvm_map_remap_as_stack(struct proc *, vaddr_t, vsize_t);
void		uvm_map_setup(struct vm_map *, pmap_t, vaddr_t, vaddr_t, int);
int		uvm_map_submap(struct vm_map *, vaddr_t, vaddr_t,
		    struct vm_map *);
void		uvm_unmap(struct vm_map *, vaddr_t, vaddr_t);
void		uvm_unmap_detach(struct uvm_map_deadq *, int);
int		uvm_unmap_remove(struct vm_map*, vaddr_t, vaddr_t,
		    struct uvm_map_deadq *, boolean_t, boolean_t, boolean_t);
void		uvm_map_set_uaddr(struct vm_map*, struct uvm_addr_state**,
		    struct uvm_addr_state*);
int		uvm_map_mquery(struct vm_map*, vaddr_t*, vsize_t, voff_t, int);


struct p_inentry;

int		uvm_map_inentry_sp(vm_map_entry_t);
boolean_t	uvm_map_inentry(struct proc *, struct p_inentry *, vaddr_t addr,
		    const char *fmt, int (*fn)(vm_map_entry_t), u_long serial);

struct kinfo_vmentry;

int		uvm_map_fill_vmmap(struct vm_map *, struct kinfo_vmentry *,
		    size_t *);

/*
 * VM map locking operations.
 */

boolean_t	vm_map_lock_try_ln(struct vm_map*, char*, int);
void		vm_map_lock_ln(struct vm_map*, char*, int);
void		vm_map_lock_read_ln(struct vm_map*, char*, int);
void		vm_map_unlock_ln(struct vm_map*, char*, int);
void		vm_map_unlock_read_ln(struct vm_map*, char*, int);
boolean_t	vm_map_upgrade_ln(struct vm_map*, char*, int);
void		vm_map_downgrade_ln(struct vm_map*, char*, int);
void		vm_map_busy_ln(struct vm_map*, char*, int);
void		vm_map_unbusy_ln(struct vm_map*, char*, int);
void		vm_map_assert_anylock_ln(struct vm_map*, char*, int);
void		vm_map_assert_wrlock_ln(struct vm_map*, char*, int);

#ifdef DIAGNOSTIC
#define vm_map_lock_try(map)	vm_map_lock_try_ln(map, __FILE__, __LINE__)
#define vm_map_lock(map)	vm_map_lock_ln(map, __FILE__, __LINE__)
#define vm_map_lock_read(map)	vm_map_lock_read_ln(map, __FILE__, __LINE__)
#define vm_map_unlock(map)	vm_map_unlock_ln(map, __FILE__, __LINE__)
#define vm_map_unlock_read(map)	vm_map_unlock_read_ln(map, __FILE__, __LINE__)
#define vm_map_upgrade(map)	vm_map_upgrade_ln(map, __FILE__, __LINE__)
#define vm_map_downgrade(map)	vm_map_downgrade_ln(map, __FILE__, __LINE__)
#define vm_map_busy(map)	vm_map_busy_ln(map, __FILE__, __LINE__)
#define vm_map_unbusy(map)	vm_map_unbusy_ln(map, __FILE__, __LINE__)
#define vm_map_assert_anylock(map)	\
		vm_map_assert_anylock_ln(map, __FILE__, __LINE__)
#define vm_map_assert_wrlock(map)	\
		vm_map_assert_wrlock_ln(map, __FILE__, __LINE__)
#else
#define vm_map_lock_try(map)	vm_map_lock_try_ln(map, NULL, 0)
#define vm_map_lock(map)	vm_map_lock_ln(map, NULL, 0)
#define vm_map_lock_read(map)	vm_map_lock_read_ln(map, NULL, 0)
#define vm_map_unlock(map)	vm_map_unlock_ln(map, NULL, 0)
#define vm_map_unlock_read(map)	vm_map_unlock_read_ln(map, NULL, 0)
#define vm_map_upgrade(map)	vm_map_upgrade_ln(map, NULL, 0)
#define vm_map_downgrade(map)	vm_map_downgrade_ln(map, NULL, 0)
#define vm_map_busy(map)	vm_map_busy_ln(map, NULL, 0)
#define vm_map_unbusy(map)	vm_map_unbusy_ln(map, NULL, 0)
#define vm_map_assert_anylock(map)	vm_map_assert_anylock_ln(map, NULL, 0)
#define vm_map_assert_wrlock(map)	vm_map_assert_wrlock_ln(map, NULL, 0)
#endif

void		uvm_map_lock_entry(struct vm_map_entry *);
void		uvm_map_unlock_entry(struct vm_map_entry *);

#endif /* _KERNEL */

/*
 *	Functions implemented as macros
 */
#define		vm_map_min(map)		((map)->min_offset)
#define		vm_map_max(map)		((map)->max_offset)
#define		vm_map_pmap(map)	((map)->pmap)

#endif /* _UVM_UVM_MAP_H_ */
