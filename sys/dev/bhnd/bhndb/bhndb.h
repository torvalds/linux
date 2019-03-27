/*-
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

#ifndef _BHND_BHNDB_H_
#define _BHND_BHNDB_H_

#include <sys/param.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/bhnd/bhnd.h>

#include "bhndb_bus_if.h"

extern devclass_t bhndb_devclass;
DECLARE_CLASS(bhnd_bhndb_driver);

int	bhndb_attach_bridge(device_t parent, device_t *bhndb, int unit);

/**
 * bhndb register window types.
 */
typedef enum {
	BHNDB_REGWIN_T_CORE,		/**< Fixed mapping of a core port region. */
	BHNDB_REGWIN_T_SPROM,		/**< Fixed mapping of device SPROM */
	BHNDB_REGWIN_T_DYN,		/**< A dynamically configurable window */
	BHNDB_REGWIN_T_INVALID		/**< Invalid type */
} bhndb_regwin_type_t;

/**
 * Evaluates to true if @p _rt defines a static mapping.
 * 
 * @param _rt A bhndb_regwin_type_t value.
 */
#define	BHNDB_REGWIN_T_IS_STATIC(_rt)		\
	((_rt) == BHNDB_REGWIN_T_CORE ||	\
	 (_rt) == BHNDB_REGWIN_T_SPROM)

/**
 * bhndb register window definition.
 */
struct bhndb_regwin {
	bhndb_regwin_type_t	win_type;	/**< window type */
	bus_size_t		win_offset;	/**< offset of the window within the resource */
	bus_size_t		win_size;	/**< size of the window */
	
	/** Resource identification */
	struct {
		int		type;		/**< resource type */
		int		rid;		/**< resource id */
	} res;


	union {
		/** Core-specific register window (BHNDB_REGWIN_T_CORE). */
		struct {
			bhnd_devclass_t	class;		/**< mapped core's class */
			u_int		unit;		/**< mapped core's unit */
			bhnd_port_type	port_type;	/**< mapped port type */
			u_int		port;		/**< mapped port number */
			u_int		region;		/**< mapped region number */
			bhnd_size_t	offset;		/**< mapped offset within the region */
		} core;

		/** SPROM register window (BHNDB_REGWIN_T_SPROM). */
		struct {} sprom;

                /** Dynamic register window (BHNDB_REGWIN_T_DYN). */
		struct {
			bus_size_t	cfg_offset;	/**< window address config offset. */
		} dyn;
	} d;
};

#define	BHNDB_REGWIN_TABLE_END	{ BHNDB_REGWIN_T_INVALID, 0, 0, { 0, 0 } }

/**
 * Bridge hardware configuration.
 * 
 * Provides the bridge's DMA address translation descriptions, register/address
 * mappings, and the resources via which those mappings may be accessed.
 */
struct bhndb_hwcfg {
	const struct resource_spec		*resource_specs;	/**< resources required by our register windows */
	const struct bhndb_regwin		*register_windows;	/**< register window table */
	const struct bhnd_dma_translation	*dma_translations;	/**< DMA address translation table, or NULL if DMA is not supported */
};

/**
 * Hardware specification entry.
 * 
 * Defines a set of match criteria that may be used to determine the
 * register map and resource configuration for a bhndb bridge device. 
 */
struct bhndb_hw {
	const char			*name;		/**< configuration name */
	const struct bhnd_core_match	*hw_reqs;	/**< match requirements */
	u_int				 num_hw_reqs;	/**< number of match requirements */
	const struct bhndb_hwcfg	*cfg;		/**< associated hardware configuration */
};


/**
 * bhndb resource allocation priorities.
 */
typedef enum {
	/** No direct resources should ever be allocated for this device. */
	BHNDB_PRIORITY_NONE	= 0,

	/** Allocate a direct resource if available after serving all other
	  * higher-priority requests. */
	BHNDB_PRIORITY_LOW	= 1,

	/** Direct resource allocation is preferred, but not necessary
	 *  for reasonable runtime performance. */
	BHNDB_PRIORITY_DEFAULT	= 2,

	/** Indirect resource allocation would incur high runtime overhead. */
	BHNDB_PRIORITY_HIGH	= 3
} bhndb_priority_t;

/**
 * bhndb resource allocation flags.
 */
enum bhndb_alloc_flags {
	/**
	 * If resource overcommit prevents fulfilling a request for this
	 * resource, an in-use resource should be be borrowed to fulfill the
	 * request.
	 * 
	 * The only known use case is to support accessing the ChipCommon core
	 * during Wi-Fi driver operation on early PCI Wi-Fi devices
	 * (PCI_V0, SSB) that do not provide a dedicated ChipCommon register
	 * window mapping; on such devices, device and firmware semantics
	 * guarantee the safety of -- after disabling interrupts -- borrowing
	 * the single dynamic register window that's been assigned to the D11
	 * core to perform the few ChipCommon operations required by the driver.
	 */
	BHNDB_ALLOC_FULFILL_ON_OVERCOMMIT	= (1<<0),
};

/**
 * Port resource priority descriptor.
 */
struct bhndb_port_priority {
	bhnd_port_type		type;		/**< port type. */
	u_int			port;		/**< port */
	u_int			region;		/**< region */
	bhndb_priority_t	priority;	/**< port priority */
	uint32_t		alloc_flags;	/**< port allocation flags (@see bhndb_alloc_flags) */
};

/**
 * Core resource priority descriptor.
 */
struct bhndb_hw_priority {
	struct bhnd_core_match			 match;		/**< core match descriptor */
	bhndb_priority_t			 priority;	/**< core-level priority */
	const struct bhndb_port_priority	*ports;		/**< port priorities */
	u_int					 num_ports;	/**< number of port priority records. */
};

#define	BHNDB_HW_PRIORITY_TABLE_END	{ {}, BHNDB_PRIORITY_NONE, NULL, 0 }


#endif /* _BHND_BHNDB_H_ */
