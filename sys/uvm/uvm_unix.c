/*	$OpenBSD: uvm_unix.c,v 1.73 2024/01/17 22:22:25 kurt Exp $	*/
/*	$NetBSD: uvm_unix.c,v 1.18 2000/09/13 15:00:25 thorpej Exp $	*/

/*
 * Copyright (c) 1997 Charles D. Cranor and Washington University.
 * Copyright (c) 1991, 1993 The Regents of the University of California.  
 * Copyright (c) 1988 University of Utah.
 *
 * All rights reserved.
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
 * from: Utah $Hdr: vm_unix.c 1.1 89/11/07$
 *      @(#)vm_unix.c   8.1 (Berkeley) 6/11/93
 * from: Id: uvm_unix.c,v 1.1.2.2 1997/08/25 18:52:30 chuck Exp
 */

/*
 * uvm_unix.c: traditional sbrk/grow interface to vm.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/vnode.h>

#include <sys/mount.h>
#include <sys/syscallargs.h>

#include <uvm/uvm.h>

/*
 * sys_obreak: set break
 */

int
sys_obreak(struct proc *p, void *v, register_t *retval)
{
	struct sys_obreak_args /* {
		syscallarg(char *) nsize;
	} */ *uap = v;
	struct vmspace *vm = p->p_vmspace;
	vaddr_t new, old, base;
	int error;

	base = (vaddr_t)vm->vm_daddr;
	new = round_page((vaddr_t)SCARG(uap, nsize));
	if (new < base || (new - base) > lim_cur(RLIMIT_DATA))
		return (ENOMEM);

	old = round_page(base + ptoa(vm->vm_dsize));

	if (new == old)
		return (0);

	/* grow or shrink? */
	if (new > old) {
		error = uvm_map(&vm->vm_map, &old, new - old, NULL,
		    UVM_UNKNOWN_OFFSET, 0,
		    UVM_MAPFLAG(PROT_READ | PROT_WRITE,
		    PROT_READ | PROT_WRITE | PROT_EXEC, MAP_INHERIT_COPY,
		    MADV_NORMAL, UVM_FLAG_FIXED|UVM_FLAG_COPYONW));
		if (error) {
			uprintf("sbrk: grow %ld failed, error = %d\n",
			    new - old, error);
			return (ENOMEM);
		}
		vm->vm_dsize += atop(new - old);
	} else {
		uvm_unmap(&vm->vm_map, new, old);
		vm->vm_dsize -= atop(old - new);
	}

	return (0);
}

/*
 * uvm_grow: enlarge the "stack segment" to include sp.
 */
void
uvm_grow(struct proc *p, vaddr_t sp)
{
	struct vmspace *vm = p->p_vmspace;
	vm_map_t map = &vm->vm_map;
	int si;

	/* For user defined stacks (from sendsig). */
	if (sp < (vaddr_t)vm->vm_maxsaddr)
		return;
#ifdef MACHINE_STACK_GROWS_UP
	if (sp >= (vaddr_t)vm->vm_minsaddr)
		return;
#endif

	vm_map_lock(map);

	/* For common case of already allocated (from trap). */
#ifdef MACHINE_STACK_GROWS_UP
	if (sp < (vaddr_t)vm->vm_maxsaddr + ptoa(vm->vm_ssize))
#else
	if (sp >= (vaddr_t)vm->vm_minsaddr - ptoa(vm->vm_ssize))
#endif
		goto out;

	/* Really need to check vs limit and increment stack size if ok. */
#ifdef MACHINE_STACK_GROWS_UP
	si = atop(sp - (vaddr_t)vm->vm_maxsaddr) - vm->vm_ssize + 1;
#else
	si = atop((vaddr_t)vm->vm_minsaddr - sp) - vm->vm_ssize;
#endif
	if (vm->vm_ssize + si <= atop(lim_cur(RLIMIT_STACK)))
		vm->vm_ssize += si;
out:
	vm_map_unlock(map);
}

#ifndef SMALL_KERNEL

#define WALK_CHUNK	32
/*
 * Not all the pages in an amap may be present.  When dumping core,
 * we don't want to force all the pages to be present: it's a waste
 * of time and memory when we already know what they contain (zeros)
 * and the ELF format at least can adequately represent them as a
 * segment with memory size larger than its file size.
 *
 * So, we walk the amap with calls to amap_lookups() and scan the
 * resulting pointers to find ranges of zero or more present pages
 * followed by at least one absent page or the end of the amap.
 * When then pass that range to the walk callback with 'start'
 * pointing to the start of the present range, 'realend' pointing
 * to the first absent page (or the end of the entry), and 'end'
 * pointing to the page past the last absent page (or the end of
 * the entry).
 *
 * Note that if the first page of the amap is empty then the callback
 * must be invoked with 'start' == 'realend' so it can present that
 * first range of absent pages.
 */
int
uvm_coredump_walk_amap(struct vm_map_entry *entry, int *nsegmentp,
    uvm_coredump_walk_cb *walk, void *cookie)
{
	struct vm_anon *anons[WALK_CHUNK];
	vaddr_t pos, start, realend, end, entry_end;
	vm_prot_t prot;
	int nsegment, absent, npages, i, error;

	prot = entry->protection;
	nsegment = *nsegmentp;
	start = entry->start;
	entry_end = MIN(entry->end, VM_MAXUSER_ADDRESS);

	absent = 0;
	for (pos = start; pos < entry_end; pos += npages << PAGE_SHIFT) {
		npages = (entry_end - pos) >> PAGE_SHIFT;
		if (npages > WALK_CHUNK)
			npages = WALK_CHUNK;
		amap_lookups(&entry->aref, pos - entry->start, anons, npages);
		for (i = 0; i < npages; i++) {
			if ((anons[i] == NULL) == absent)
				continue;
			if (!absent) {
				/* going from present to absent: set realend */
				realend = pos + (i << PAGE_SHIFT);
				absent = 1;
				continue;
			}

			/* going from absent to present: invoke callback */
			end = pos + (i << PAGE_SHIFT);
			if (start != end) {
				error = (*walk)(start, realend, end, prot,
				    0, nsegment, cookie);
				if (error)
					return error;
				nsegment++;
			}
			start = realend = end;
			absent = 0;
		}
	}

	if (!absent)
		realend = entry_end;
	error = (*walk)(start, realend, entry_end, prot, 0, nsegment, cookie);
	*nsegmentp = nsegment + 1;
	return error;
}

/*
 * Common logic for whether a map entry should be included in a coredump
 */
static inline int
uvm_should_coredump(struct proc *p, struct vm_map_entry *entry)
{
	if (!(entry->protection & PROT_WRITE) &&
	    entry->aref.ar_amap == NULL &&
	    entry->start != p->p_p->ps_sigcode &&
	    entry->start != p->p_p->ps_timekeep)
		return 0;

	/*
	 * Skip ranges marked as unreadable, as uiomove(UIO_USERSPACE)
	 * will fail on them.  Maybe this really should be a test of
	 * entry->max_protection, but doing
	 *	uvm_map_extract(UVM_EXTRACT_FIXPROT)
	 * on each such page would suck.
	 */
	if (!(entry->protection & PROT_READ) &&
	    entry->start != p->p_p->ps_sigcode)
		return 0;

	/* Skip ranges excluded from coredumps. */
	if (UVM_ET_ISCONCEAL(entry))
		return 0;

	/* Don't dump mmaped devices. */
	if (entry->object.uvm_obj != NULL &&
	    UVM_OBJ_IS_DEVICE(entry->object.uvm_obj))
		return 0;

	if (entry->start >= VM_MAXUSER_ADDRESS)
		return 0;

	return 1;
}


/* do nothing callback for uvm_coredump_walk_amap() */
static int
noop(vaddr_t start, vaddr_t realend, vaddr_t end, vm_prot_t prot,
    int isvnode, int nsegment, void *cookie)
{
	return 0;
}

/*
 * Walk the VA space for a process to identify what to write to
 * a coredump.  First the number of contiguous ranges is counted,
 * then the 'setup' callback is invoked to prepare for actually
 * recording the ranges, then the VA is walked again, invoking
 * the 'walk' callback for each range.  The number of ranges walked
 * is guaranteed to match the count seen by the 'setup' callback.
 */

int
uvm_coredump_walkmap(struct proc *p, uvm_coredump_setup_cb *setup,
    uvm_coredump_walk_cb *walk, void *cookie)
{
	struct vmspace *vm = p->p_vmspace;
	struct vm_map *map = &vm->vm_map;
	struct vm_map_entry *entry;
	vaddr_t end;
	int refed_amaps = 0;
	int nsegment, error, isvnode;

	/*
	 * Walk the map once to count the segments.  If an amap is
	 * referenced more than once than take *another* reference
	 * and treat the amap as exactly one segment instead of
	 * checking page presence inside it.  On the second pass
	 * we'll recognize which amaps we did that for by the ref
	 * count being >1...and decrement it then.
	 */
	nsegment = 0;
	RBT_FOREACH(entry, uvm_map_addr, &map->addr) {
		/* should never happen for a user process */
		if (UVM_ET_ISSUBMAP(entry)) {
			panic("%s: user process with submap?", __func__);
		}

		if (! uvm_should_coredump(p, entry))
			continue;

		if (entry->aref.ar_amap != NULL) {
			if (entry->aref.ar_amap->am_ref == 1) {
				uvm_coredump_walk_amap(entry, &nsegment,
				    &noop, cookie);
				continue;
			}

			/*
			 * Multiple refs currently, so take another and
			 * treat it as a single segment
			 */
			entry->aref.ar_amap->am_ref++;
			refed_amaps++;
		}

		nsegment++;
	}

	/*
	 * Okay, we have a count in nsegment.  Prepare to
	 * walk it again, then invoke the setup callback. 
	 */
	entry = RBT_MIN(uvm_map_addr, &map->addr);
	error = (*setup)(nsegment, cookie);
	if (error)
		goto cleanup;

	/*
	 * Setup went okay, so do the second walk, invoking the walk
	 * callback on the counted segments and cleaning up references
	 * as we go.
	 */
	nsegment = 0;
	for (; entry != NULL; entry = RBT_NEXT(uvm_map_addr, entry)) {
		if (! uvm_should_coredump(p, entry))
			continue;

		if (entry->aref.ar_amap != NULL &&
		    entry->aref.ar_amap->am_ref == 1) {
			error = uvm_coredump_walk_amap(entry, &nsegment,
			    walk, cookie);
			if (error)
				break;
			continue;
		}

		end = entry->end;
		if (end > VM_MAXUSER_ADDRESS)
			end = VM_MAXUSER_ADDRESS;

		isvnode = (entry->object.uvm_obj != NULL &&
		    UVM_OBJ_IS_VNODE(entry->object.uvm_obj));
		error = (*walk)(entry->start, end, end, entry->protection,
		    isvnode, nsegment, cookie);
		if (error)
			break;
		nsegment++;

		if (entry->aref.ar_amap != NULL &&
		    entry->aref.ar_amap->am_ref > 1) {
			/* multiple refs, so we need to drop one */
			entry->aref.ar_amap->am_ref--;
			refed_amaps--;
		}
	}

	if (error) {
cleanup:
		/* clean up the extra references from where we left off */
		if (refed_amaps > 0) {
			for (; entry != NULL;
			    entry = RBT_NEXT(uvm_map_addr, entry)) {
				if (entry->aref.ar_amap == NULL ||
				    entry->aref.ar_amap->am_ref == 1)
					continue;
				if (! uvm_should_coredump(p, entry))
					continue;
				entry->aref.ar_amap->am_ref--;
				if (refed_amaps-- == 0)
					break;
			}
		}
	}

	return error;
}

#endif	/* !SMALL_KERNEL */
