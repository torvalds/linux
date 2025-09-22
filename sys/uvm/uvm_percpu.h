/*	$OpenBSD: uvm_percpu.h,v 1.3 2024/05/01 12:54:27 mpi Exp $	*/

/*
 * Copyright (c) 2024 Martin Pieuchot <mpi@openbsd.org>
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

#ifndef _UVM_UVM_PCPU_H_
#define _UVM_UVM_PCPU_H_

struct vm_page;

/*
 * The number of pages per magazine should be large enough to get rid of the
 * contention in the pmemrange allocator during concurrent page faults and
 * small enough to limit fragmentation.
 */
#define UVM_PMR_CACHEMAGSZ 8

/*
 * Magazine
 */
struct uvm_pmr_cache_item {
	struct vm_page		*upci_pages[UVM_PMR_CACHEMAGSZ];
	int			 upci_npages;	/* # of pages in magazine */
};

/*
 * Per-CPU cache of physical pages.
 */
struct uvm_pmr_cache {
	struct uvm_pmr_cache_item upc_magz[2];	/* magazines */
	int			  upc_actv;	/* index of active magazine */

};

#endif /* _UVM_UVM_PCPU_H_ */
