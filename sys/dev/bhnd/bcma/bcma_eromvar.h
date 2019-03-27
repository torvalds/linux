/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015 Landon Fuller <landon@landonf.org>
 * All rights reserved.
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

#ifndef	_BCMA_BCMA_EROMVAR_H_
#define	_BCMA_BCMA_EROMVAR_H_

#include <dev/bhnd/bhnd.h>
#include <dev/bhnd/bhnd_erom.h>

#include "bcmavar.h"

struct bcma_erom;

int	bcma_erom_next_corecfg(struct bcma_erom *sc,
	    struct bcma_corecfg **result);

/** EROM core descriptor. */
struct bcma_erom_core {
	uint16_t	vendor;		/**< core's designer */
	uint16_t	device;		/**< core's device identifier */
	uint16_t	rev;		/**< core's hardware revision */
	u_long		num_mport;	/**< number of master port descriptors */
	u_long		num_dport;	/**< number of slave port descriptors */
	u_long		num_mwrap;	/**< number of master wrapper slave port descriptors */
	u_long		num_swrap;	/**< number of slave wrapper slave port descriptors */
};

/** EROM master port descriptor. */
struct bcma_erom_mport {
	uint8_t		port_num;	/**< the port number (bus-unique) */
	uint8_t		port_vid;	/**< the port VID. A single physical
					     master port may have multiple VIDs;
					     the canonical port address is
					     composed of the port number + the
					     port VID */
};

/** EROM slave port region descriptor. */
struct bcma_erom_sport_region {
	uint8_t		region_port;	/**< the slave port mapping this region */
	uint8_t		region_type;	/**< the mapping port's type */
	bhnd_addr_t	base_addr;	/**< region base address */
	bhnd_addr_t	size;		/**< region size */
};

#endif /* _BCMA_BCMA_EROMVAR_H_ */
