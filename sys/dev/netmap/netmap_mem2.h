/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2012-2014 Matteo Landi
 * Copyright (C) 2012-2016 Luigi Rizzo
 * Copyright (C) 2012-2016 Giuseppe Lettieri
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
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

/*
 * $FreeBSD$
 *
 * (New) memory allocator for netmap
 */

/*
 * This allocator creates three memory pools:
 *	nm_if_pool	for the struct netmap_if
 *	nm_ring_pool	for the struct netmap_ring
 *	nm_buf_pool	for the packet buffers.
 *
 * that contain netmap objects. Each pool is made of a number of clusters,
 * multiple of a page size, each containing an integer number of objects.
 * The clusters are contiguous in user space but not in the kernel.
 * Only nm_buf_pool needs to be dma-able,
 * but for convenience use the same type of allocator for all.
 *
 * Once mapped, the three pools are exported to userspace
 * as a contiguous block, starting from nm_if_pool. Each
 * cluster (and pool) is an integral number of pages.
 *   [ . . . ][ . . . . . .][ . . . . . . . . . .]
 *    nm_if     nm_ring            nm_buf
 *
 * The userspace areas contain offsets of the objects in userspace.
 * When (at init time) we write these offsets, we find out the index
 * of the object, and from there locate the offset from the beginning
 * of the region.
 *
 * The invididual allocators manage a pool of memory for objects of
 * the same size.
 * The pool is split into smaller clusters, whose size is a
 * multiple of the page size. The cluster size is chosen
 * to minimize the waste for a given max cluster size
 * (we do it by brute force, as we have relatively few objects
 * per cluster).
 *
 * Objects are aligned to the cache line (64 bytes) rounding up object
 * sizes when needed. A bitmap contains the state of each object.
 * Allocation scans the bitmap; this is done only on attach, so we are not
 * too worried about performance
 *
 * For each allocator we can define (thorugh sysctl) the size and
 * number of each object. Memory is allocated at the first use of a
 * netmap file descriptor, and can be freed when all such descriptors
 * have been released (including unmapping the memory).
 * If memory is scarce, the system tries to get as much as possible
 * and the sysctl values reflect the actual allocation.
 * Together with desired values, the sysctl export also absolute
 * min and maximum values that cannot be overridden.
 *
 * struct netmap_if:
 *	variable size, max 16 bytes per ring pair plus some fixed amount.
 *	1024 bytes should be large enough in practice.
 *
 *	In the worst case we have one netmap_if per ring in the system.
 *
 * struct netmap_ring
 *	variable size, 8 byte per slot plus some fixed amount.
 *	Rings can be large (e.g. 4k slots, or >32Kbytes).
 *	We default to 36 KB (9 pages), and a few hundred rings.
 *
 * struct netmap_buffer
 *	The more the better, both because fast interfaces tend to have
 *	many slots, and because we may want to use buffers to store
 *	packets in userspace avoiding copies.
 *	Must contain a full frame (eg 1518, or more for vlans, jumbo
 *	frames etc.) plus be nicely aligned, plus some NICs restrict
 *	the size to multiple of 1K or so. Default to 2K
 */
#ifndef _NET_NETMAP_MEM2_H_
#define _NET_NETMAP_MEM2_H_



/* We implement two kinds of netmap_mem_d structures:
 *
 * - global: used by hardware NICS;
 *
 * - private: used by VALE ports.
 *
 * In both cases, the netmap_mem_d structure has the same lifetime as the
 * netmap_adapter of the corresponding NIC or port. It is the responsibility of
 * the client code to delete the private allocator when the associated
 * netmap_adapter is freed (this is implemented by the NAF_MEM_OWNER flag in
 * netmap.c).  The 'refcount' field counts the number of active users of the
 * structure. The global allocator uses this information to prevent/allow
 * reconfiguration. The private allocators release all their memory when there
 * are no active users.  By 'active user' we mean an existing netmap_priv
 * structure holding a reference to the allocator.
 */

extern struct netmap_mem_d nm_mem;
typedef uint16_t nm_memid_t;

int	   netmap_mem_get_lut(struct netmap_mem_d *, struct netmap_lut *);
nm_memid_t netmap_mem_get_id(struct netmap_mem_d *);
vm_paddr_t netmap_mem_ofstophys(struct netmap_mem_d *, vm_ooffset_t);
#ifdef _WIN32
PMDL win32_build_user_vm_map(struct netmap_mem_d* nmd);
#endif
int	   netmap_mem_finalize(struct netmap_mem_d *, struct netmap_adapter *);
int 	   netmap_mem_init(void);
void 	   netmap_mem_fini(void);
struct netmap_if * netmap_mem_if_new(struct netmap_adapter *, struct netmap_priv_d *);
void 	   netmap_mem_if_delete(struct netmap_adapter *, struct netmap_if *);
int	   netmap_mem_rings_create(struct netmap_adapter *);
void	   netmap_mem_rings_delete(struct netmap_adapter *);
int 	   netmap_mem_deref(struct netmap_mem_d *, struct netmap_adapter *);
int	   netmap_mem2_get_pool_info(struct netmap_mem_d *, u_int, u_int *, u_int *);
int	   netmap_mem_get_info(struct netmap_mem_d *, uint64_t *size,
				u_int *memflags, nm_memid_t *id);
ssize_t    netmap_mem_if_offset(struct netmap_mem_d *, const void *vaddr);
struct netmap_mem_d* netmap_mem_private_new( u_int txr, u_int txd, u_int rxr, u_int rxd,
		u_int extra_bufs, u_int npipes, int* error);

#define netmap_mem_get(d) __netmap_mem_get(d, __FUNCTION__, __LINE__)
#define netmap_mem_put(d) __netmap_mem_put(d, __FUNCTION__, __LINE__)
struct netmap_mem_d* __netmap_mem_get(struct netmap_mem_d *, const char *, int);
void __netmap_mem_put(struct netmap_mem_d *, const char *, int);
struct netmap_mem_d* netmap_mem_find(nm_memid_t);
unsigned netmap_mem_bufsize(struct netmap_mem_d *nmd);

#ifdef WITH_EXTMEM
struct netmap_mem_d* netmap_mem_ext_create(uint64_t, struct nmreq_pools_info *, int *);
#else /* !WITH_EXTMEM */
#define netmap_mem_ext_create(nmr, _perr) \
	({ int *perr = _perr; if (perr) *(perr) = EOPNOTSUPP; NULL; })
#endif /* WITH_EXTMEM */

#ifdef WITH_PTNETMAP
struct netmap_mem_d* netmap_mem_pt_guest_new(struct ifnet *,
					     unsigned int nifp_offset,
					     unsigned int memid);
struct ptnetmap_memdev;
struct netmap_mem_d* netmap_mem_pt_guest_attach(struct ptnetmap_memdev *, uint16_t);
int netmap_mem_pt_guest_ifp_del(struct netmap_mem_d *, struct ifnet *);
#endif /* WITH_PTNETMAP */

int netmap_mem_pools_info_get(struct nmreq_pools_info *,
				struct netmap_mem_d *);

#define NETMAP_MEM_PRIVATE	0x2	/* allocator uses private address space */
#define NETMAP_MEM_IO		0x4	/* the underlying memory is mmapped I/O */
#define NETMAP_MEM_EXT		0x10	/* external memory (not remappable) */

uint32_t netmap_extra_alloc(struct netmap_adapter *, uint32_t *, uint32_t n);

#ifdef WITH_EXTMEM
#include <net/netmap_virt.h>
struct nm_os_extmem; /* opaque */
struct nm_os_extmem *nm_os_extmem_create(unsigned long, struct nmreq_pools_info *, int *perror);
char *nm_os_extmem_nextpage(struct nm_os_extmem *);
int nm_os_extmem_nr_pages(struct nm_os_extmem *);
int nm_os_extmem_isequal(struct nm_os_extmem *, struct nm_os_extmem *);
void nm_os_extmem_delete(struct nm_os_extmem *);
#endif /* WITH_EXTMEM */

#endif
