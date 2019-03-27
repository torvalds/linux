/*-
 * Copyright (c) 2015-2016 Landon Fuller <landon@landonf.org>
 * Copyright (c) 2017 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Landon Fuller
 * under sponsorship from the FreeBSD Foundation.
 *
 * Portions of this software were developed by Landon Fuller
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 * 
 * $FreeBSD$
 */

#ifndef _BHND_BHNDB_PRIVATE_H_
#define _BHND_BHNDB_PRIVATE_H_

#include <sys/param.h>
#include <sys/bitstring.h>
#include <sys/bus.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include "bhndbvar.h"

/*
 * Private bhndb(4) driver definitions.
 */

struct bhndb_dw_alloc;
struct bhndb_intr_handler;
struct bhndb_region;
struct bhndb_resources;

struct bhndb_resources		*bhndb_alloc_resources(device_t dev,
				     device_t parent_dev,
				     const struct bhndb_hwcfg *cfg);

void				 bhndb_free_resources(
				     struct bhndb_resources *br);

int				 bhndb_add_resource_region(
				     struct bhndb_resources *br,
				     bhnd_addr_t addr, bhnd_size_t size,
				     bhndb_priority_t priority,
				     uint32_t alloc_flags,
				     const struct bhndb_regwin *static_regwin);

int				 bhndb_find_resource_limits(
				     struct bhndb_resources *br, int type,
				     struct resource *r, rman_res_t *start,
				     rman_res_t *end);

struct bhndb_intr_handler	*bhndb_alloc_intr_handler(device_t owner,
				     struct resource *r,
				     struct bhndb_intr_isrc *isrc);
void				 bhndb_free_intr_handler(
				     struct bhndb_intr_handler *ih);

void				 bhndb_register_intr_handler(
				     struct bhndb_resources *br,
				     struct bhndb_intr_handler *ih);
void				 bhndb_deregister_intr_handler(
				     struct bhndb_resources *br,
				     struct bhndb_intr_handler *ih);
struct bhndb_intr_handler	*bhndb_find_intr_handler(
				     struct bhndb_resources *br,
				     void *cookiep);

bool				 bhndb_has_static_region_mapping(
				     struct bhndb_resources *br,
				     bhnd_addr_t addr, bhnd_size_t size);

struct bhndb_region		*bhndb_find_resource_region(
				     struct bhndb_resources *br,
				     bhnd_addr_t addr, bhnd_size_t size);

struct bhndb_dw_alloc		*bhndb_dw_find_resource(
				     struct bhndb_resources *dr,
				     struct resource *r);
				     
struct bhndb_dw_alloc		*bhndb_dw_find_mapping(
				     struct bhndb_resources *br,
				     bhnd_addr_t addr, bhnd_size_t size);

int				 bhndb_dw_retain(
				     struct bhndb_resources *br,
				     struct bhndb_dw_alloc *dwa,
				     struct resource *res);

void				 bhndb_dw_release(
				     struct bhndb_resources *br,
				     struct bhndb_dw_alloc *dwa,
				     struct resource *res);

int				 bhndb_dw_set_addr(device_t dev,
				     struct bhndb_resources *br,
				     struct bhndb_dw_alloc *dwa,
				     bus_addr_t addr, bus_size_t size);

struct bhndb_dw_alloc		*bhndb_dw_steal(struct bhndb_resources *br,
				     bus_addr_t *saved);

void				 bhndb_dw_return_stolen(device_t dev,
				     struct bhndb_resources *br,
				     struct bhndb_dw_alloc *dwa,
				     bus_addr_t saved);

const struct bhndb_hw_priority	*bhndb_hw_priority_find_core(
				     const struct bhndb_hw_priority *table,
				     struct bhnd_core_info *core);

const struct bhndb_port_priority *bhndb_hw_priorty_find_port(
				     const struct bhndb_hw_priority *table,
				     struct bhnd_core_info *core,
				     bhnd_port_type port_type, u_int port,
				     u_int region);


/**
 * Dynamic register window allocation reference.
 */
struct bhndb_dw_rentry {
	struct resource			*dw_res;	/**< child resource */
	LIST_ENTRY(bhndb_dw_rentry)	 dw_link;
};

/**
 * A dynamic register window allocation record. 
 */
struct bhndb_dw_alloc {
	const struct bhndb_regwin	*win;		/**< window definition */
	struct resource			*parent_res;	/**< enclosing resource */
	u_int				 rnid;		/**< region identifier */
	rman_res_t			 target;	/**< the current window address, or 0x0 if unknown */

	LIST_HEAD(, bhndb_dw_rentry)	 refs;		/**< references */
};

/**
 * A bus address region description.
 */
struct bhndb_region {
	bhnd_addr_t			 addr;		/**< start of mapped range */
	bhnd_size_t			 size;		/**< size of mapped range */
	bhndb_priority_t		 priority;	/**< direct resource allocation priority */
	uint32_t			 alloc_flags;	/**< resource allocation flags (@see bhndb_alloc_flags) */
	const struct bhndb_regwin	*static_regwin;	/**< fixed mapping regwin, if any */

	STAILQ_ENTRY(bhndb_region)	 link;
};

/**
 * Attached interrupt handler state
 */
struct bhndb_intr_handler {
	device_t		 ih_owner;	/**< child device */
	struct resource		*ih_res;	/**< child resource */
	void			*ih_cookiep;	/**< hostb-assigned cookiep, or NULL if bus_setup_intr() incomplete. */
	struct bhndb_intr_isrc	*ih_isrc;	/**< host interrupt source routing the child's interrupt  */
	bool			 ih_active;	/**< handler has been registered via bhndb_register_intr_handler */

	STAILQ_ENTRY(bhndb_intr_handler) ih_link;
};

/**
 * BHNDB resource allocation state.
 */
struct bhndb_resources {
	device_t			 dev;		/**< bridge device */
	const struct bhndb_hwcfg	*cfg;		/**< hardware configuration */

	struct bhndb_host_resources	*res;		/**< host resources, or NULL if not allocated */
	
	struct rman			 ht_mem_rman;	/**< host memory manager */
	struct rman			 br_mem_rman;	/**< bridged memory manager */
	struct rman			 br_irq_rman;	/**< bridged irq manager */

	STAILQ_HEAD(, bhndb_region) 	 bus_regions;	/**< bus region descriptors */

	struct mtx			 dw_steal_mtx;	/**< spinlock must be held when stealing a dynamic window allocation */
	struct bhndb_dw_alloc		*dw_alloc;	/**< dynamic window allocation records */
	size_t				 dwa_count;	/**< number of dynamic windows available. */
	bitstr_t			*dwa_freelist;	/**< dynamic window free list */
	bhndb_priority_t		 min_prio;	/**< minimum resource priority required to
							     allocate a dynamic window */

	STAILQ_HEAD(,bhndb_intr_handler) bus_intrs;	/**< attached child interrupt handlers */
};

/**
 * Returns true if the all dynamic windows are marked free, false
 * otherwise.
 * 
 * @param br The resource state to check.
 */
static inline bool
bhndb_dw_all_free(struct bhndb_resources *br)
{
	int bit;
	bit_ffs(br->dwa_freelist, br->dwa_count, &bit);
	return (bit == -1);
}

/**
 * Find the next free dynamic window region in @p br.
 * 
 * @param br The resource state to search.
 */
static inline struct bhndb_dw_alloc *
bhndb_dw_next_free(struct bhndb_resources *br)
{
	struct bhndb_dw_alloc	*dw_free;
	int			 bit;

	bit_ffc(br->dwa_freelist, br->dwa_count, &bit);
	if (bit == -1)
		return (NULL);

	dw_free = &br->dw_alloc[bit];

	KASSERT(LIST_EMPTY(&dw_free->refs),
	    ("free list out of sync with refs"));

	return (dw_free);
}

/**
 * Returns true if a dynamic window allocation is marked as free.
 * 
 * @param br The resource state owning @p dwa.
 * @param dwa The dynamic window allocation record to be checked.
 */
static inline bool
bhndb_dw_is_free(struct bhndb_resources *br, struct bhndb_dw_alloc *dwa)
{
	bool is_free = LIST_EMPTY(&dwa->refs);

	KASSERT(is_free == !bit_test(br->dwa_freelist, dwa->rnid),
	    ("refs out of sync with free list"));

	return (is_free);
}


#define	BHNDB_LOCK_INIT(sc) \
	mtx_init(&(sc)->sc_mtx, device_get_nameunit((sc)->dev), \
	    "bhndb resource allocator lock", MTX_DEF)
#define	BHNDB_LOCK(sc)			mtx_lock(&(sc)->sc_mtx)
#define	BHNDB_UNLOCK(sc)		mtx_unlock(&(sc)->sc_mtx)
#define	BHNDB_LOCK_ASSERT(sc, what)	mtx_assert(&(sc)->sc_mtx, what)
#define	BHNDB_LOCK_DESTROY(sc)		mtx_destroy(&(sc)->sc_mtx)

#endif /* _BHND_BHNDB_PRIVATE_H_ */
