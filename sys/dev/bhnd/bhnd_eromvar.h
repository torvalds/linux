/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Landon Fuller under sponsorship from
 * the FreeBSD Foundation.
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

#ifndef _BHND_EROM_BHND_EROMVAR_H_
#define _BHND_EROM_BHND_EROMVAR_H_

#include <sys/param.h>

#include "bhnd_erom.h"

/* forward declarations */
struct bhnd_erom_io;
struct bhnd_erom_iobus;

/** @see bhnd_erom_io_map() */
typedef int		(bhnd_erom_io_map_t)(struct bhnd_erom_io *eio,
			     bhnd_addr_t addr, bhnd_size_t size);

/** @see bhnd_erom_io_tell() */
typedef int		(bhnd_erom_io_tell_t)(struct bhnd_erom_io *eio,
			     bhnd_addr_t *addr, bhnd_size_t *size);

/** @see bhnd_erom_io_read() */
typedef uint32_t	(bhnd_erom_io_read_t)(struct bhnd_erom_io *eio,
			     bhnd_size_t offset, u_int width);

/** @see bhnd_erom_io_fini() */
typedef void		(bhnd_erom_io_fini_t)(struct bhnd_erom_io *eio);


int			 bhnd_erom_read_chipid(struct bhnd_erom_io *eio,
			     struct bhnd_chipid *cid);


/**
 * Abstract EROM bus I/O support.
 */
struct bhnd_erom_io {
	bhnd_erom_io_map_t	*map;	/**< @see bhnd_erom_io_map() */
	bhnd_erom_io_tell_t	*tell;	/**< @see bhnd_erom_io_tell() */
	bhnd_erom_io_read_t	*read;	/**< @see bhnd_erom_io_read() */
	bhnd_erom_io_fini_t	*fini;	/**< @see bhnd_erom_io_fini(). May be NULL */
};

/**
 * EROM bus handle/tag I/O instance state.
 */
struct bhnd_erom_iobus {
	struct bhnd_erom_io	eio;
	bhnd_addr_t		addr;	/**< the address of @p bsh */
	bhnd_size_t		size;	/**< the size of @p bsh */
	bus_space_tag_t		bst;	/**< bus space tag */
	bus_space_handle_t	bsh;	/**< bus space handle mapping the full enumeration space */
	bool			mapped;	/**< if a mapping is active */
	bus_size_t		offset;	/**< the current mapped offset within bsh */
	bus_size_t		limit;	/**< the current mapped size relative to offset */
};

#endif /* _BHND_EROM_BHND_EROMVAR_H_ */
