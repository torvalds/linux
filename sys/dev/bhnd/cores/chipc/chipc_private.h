/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2016 Landon Fuller <landon@landonf.org>
 * Copyright (c) 2017 The FreeBSD Foundation
 * All rights reserved.
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

#ifndef _BHND_CORES_CHIPC_CHIPC_PRIVATE_H_
#define _BHND_CORES_CHIPC_CHIPC_PRIVATE_H_

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/bhnd/bhnd.h>
#include <dev/bhnd/bhndvar.h>

/*
 * Private bhnd_chipc(4) driver definitions.
 */

struct chipc_caps;
struct chipc_region;
struct chipc_softc;

int			 chipc_init_child_resource(struct resource *r,
			     struct resource *parent, 
			     bhnd_size_t offset, bhnd_size_t size);

int			 chipc_set_irq_resource(struct chipc_softc *sc,
			     device_t child, int rid, u_int intr);
int			 chipc_set_mem_resource(struct chipc_softc *sc,
			     device_t child, int rid, rman_res_t start,
			     rman_res_t count, u_int port, u_int region);

struct chipc_region	*chipc_alloc_region(struct chipc_softc *sc,
			     bhnd_port_type type, u_int port,
			     u_int region);
void			 chipc_free_region(struct chipc_softc *sc,
			     struct chipc_region *cr);
struct chipc_region	*chipc_find_region(struct chipc_softc *sc,
			     rman_res_t start, rman_res_t end);
struct chipc_region	*chipc_find_region_by_rid(struct chipc_softc *sc,
			     int rid);

int			 chipc_retain_region(struct chipc_softc *sc,
			     struct chipc_region *cr, int flags);
int			 chipc_release_region(struct chipc_softc *sc,
			     struct chipc_region *cr, int flags);

void			 chipc_print_caps(device_t dev,
			     struct chipc_caps *caps);

/**
 * chipc SYS_RES_MEMORY region allocation record.
 */
struct chipc_region {
	bhnd_port_type		 cr_port_type;	/**< bhnd port type */
	u_int			 cr_port_num;	/**< bhnd port number */
	u_int			 cr_region_num;	/**< bhnd region number */

	bhnd_addr_t		 cr_addr;	/**< region base address */
	bhnd_addr_t		 cr_end;	/**< region end address */
	bhnd_size_t		 cr_count;	/**< region count */
	int			 cr_rid;	/**< rid to use when performing
						     resource allocation, or -1
						     if region has no assigned
						     resource ID */

	struct bhnd_resource	*cr_res;	/**< bus resource, or NULL */
	int			 cr_res_rid;	/**< cr_res RID, if any. */
	u_int			 cr_refs;	/**< RF_ALLOCATED refcount */
	u_int			 cr_act_refs;	/**< RF_ACTIVE refcount */

	STAILQ_ENTRY(chipc_region) cr_link;
};

#endif /* _BHND_CORES_CHIPC_CHIPC_PRIVATE_H_ */
