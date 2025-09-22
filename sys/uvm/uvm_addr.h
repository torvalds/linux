/*	$OpenBSD: uvm_addr.h,v 1.8 2024/07/04 04:52:10 jsg Exp $	*/

/*
 * Copyright (c) 2011 Ariane van der Steldt <ariane@stack.nl>
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
 */

#ifndef _UVM_UVM_ADDR_H_
#define _UVM_UVM_ADDR_H_

/*
 * Address selection logic.
 *
 * Address selection is just that: selection. These functions may make no
 * changes to the map, except for their own state (which is passed as a
 * uaddr_state pointer).
 */


/*
 * UVM address selection base state.
 *
 * Each uvm address algorithm requires these parameters:
 * - lower bound address (page aligned)
 * - upper bound address (page aligned)
 * - function address pointers
 */
struct uvm_addr_state {
	vaddr_t uaddr_minaddr;
	vaddr_t uaddr_maxaddr;
	const struct uvm_addr_functions *uaddr_functions;
};

/*
 * This structure describes one algorithm implementation.
 *
 * Each algorithm is described in terms of:
 * - uaddr_select: an address selection algorithm
 * - uaddr_free_insert: a freelist insertion function (optional)
 * - uaddr_free_remove: a freelist deletion function (optional)
 * - uaddr_destroy: a destructor for the algorithm state
 */
struct uvm_addr_functions {
	int (*uaddr_select)(struct vm_map *map,
	    struct uvm_addr_state *uaddr,
	    struct vm_map_entry **entry_out, vaddr_t *addr_out,
	    vsize_t sz, vaddr_t align, vaddr_t offset,
	    vm_prot_t prot, vaddr_t hint);
	void (*uaddr_free_insert)(struct vm_map *map,
	    struct uvm_addr_state *uaddr_state,
	    struct vm_map_entry *entry);
	void (*uaddr_free_remove)(struct vm_map *map,
	    struct uvm_addr_state *uaddr_state,
	    struct vm_map_entry *entry);
	void (*uaddr_destroy)(struct uvm_addr_state *uaddr_state);
	void (*uaddr_print)(struct uvm_addr_state *uaddr_state, boolean_t full,
	    int (*pr)(const char *, ...));

	const char* uaddr_name;		/* Name of the allocator. */
};


#ifdef _KERNEL

void			 uvm_addr_init(void);
void			 uvm_addr_destroy(struct uvm_addr_state *);
int			 uvm_addr_linsearch(struct vm_map *,
			    struct uvm_addr_state *, struct vm_map_entry **,
			    vaddr_t *addr_out, vaddr_t, vsize_t,
			    vaddr_t, vaddr_t, int, vaddr_t, vaddr_t,
			    vsize_t, vsize_t);
int			 uvm_addr_invoke(struct vm_map *,
			    struct uvm_addr_state *, struct vm_map_entry **,
			    struct vm_map_entry **, vaddr_t*,
			    vsize_t, vaddr_t, vaddr_t, vm_prot_t, vaddr_t);
#if 0
struct uvm_addr_state	*uaddr_lin_create(vaddr_t, vaddr_t);
#endif
struct uvm_addr_state	*uaddr_rnd_create(vaddr_t, vaddr_t);
#ifndef SMALL_KERNEL
struct uvm_addr_state	*uaddr_bestfit_create(vaddr_t, vaddr_t);
struct uvm_addr_state	*uaddr_pivot_create(vaddr_t, vaddr_t);
struct uvm_addr_state	*uaddr_stack_brk_create(vaddr_t, vaddr_t);
#endif /* SMALL_KERNEL */
int			 uvm_addr_fitspace(vaddr_t *, vaddr_t *,
			    vaddr_t, vaddr_t, vsize_t, vaddr_t, vaddr_t,
			    vsize_t, vsize_t);

#if defined(DEBUG) || defined(DDB)
void			 uvm_addr_print(struct uvm_addr_state *, const char *,
			    boolean_t, int (*pr)(const char *, ...));
#endif /* DEBUG || DDB */

/*
 * Kernel bootstrap allocator.
 */
RBT_HEAD(uaddr_free_rbtree, vm_map_entry);
RBT_PROTOTYPE(uaddr_free_rbtree, vm_map_entry, dfree.rbtree,
    uvm_mapent_fspace_cmp);

extern struct uvm_addr_state uaddr_kbootstrap;

#endif /* _KERNEL */
#endif /* _UVM_UVM_ADDR_H_ */
