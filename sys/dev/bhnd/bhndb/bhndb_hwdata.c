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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <dev/bhnd/bhnd_ids.h>
#include <dev/bhnd/bhndreg.h>
#include <dev/bhnd/bhnd.h>

#include "bhndb_hwdata.h"

/*
 * Resource priority specifications shared by all bhndb(4) bridge
 * implementations.
 */

/*
 * Define a bhndb_port_priority table.
 */
#define	BHNDB_PORTS(...)	\
	.ports		= _BHNDB_PORT_ARRAY(__VA_ARGS__),		\
	.num_ports	= nitems(_BHNDB_PORT_ARRAY(__VA_ARGS__))

#define	_BHNDB_PORT_ARRAY(...) (const struct bhndb_port_priority[]) {	\
	__VA_ARGS__							\
}

/*
 * Define a core priority record for all cores matching @p devclass
 */
#define	BHNDB_CLASS_PRIO(_devclass, _priority, ...) {		\
	.match	= {							\
		BHND_MATCH_CORE_CLASS(BHND_DEVCLASS_ ## _devclass),	\
	},								\
	.priority = (BHNDB_PRIORITY_ ## _priority),		\
	BHNDB_PORTS(__VA_ARGS__)					\
}

/*
 * Define a default core priority record
 */
#define	BHNDB_DEFAULT_PRIO(...) {		\
	.match	= {				\
		BHND_MATCH_ANY	,		\
	},					\
	.priority = (BHNDB_PRIORITY_DEFAULT),	\
	BHNDB_PORTS(__VA_ARGS__)		\
}

/* Define a port priority record for the type/port/region triplet, optionally
 * specifying port allocation flags as the final argument */
#define	BHNDB_PORT_PRIO(_type, _port, _region, _priority, ...)	\
	_BHNDB_PORT_PRIO(_type, _port, _region, _priority, ## __VA_ARGS__, 0)

#define	_BHNDB_PORT_PRIO(_type, _port, _region, _priority, _flags, ...)	\
{								\
	.type		= (BHND_PORT_ ## _type),		\
	.port		= _port,				\
	.region		= _region,				\
	.priority	= (BHNDB_PRIORITY_ ## _priority),	\
	.alloc_flags	= (_flags)				\
}

/* Define a port priority record for the default (_type, 0, 0) type/port/region
 * triplet. */
#define	BHNDB_PORT0_PRIO(_type, _priority, ...)	\
	BHNDB_PORT_PRIO(_type, 0, 0, _priority, ## __VA_ARGS__, 0)

/**
 * Generic resource priority configuration usable with all currently supported
 * bcma(4)-based PCI devices.
 */
const struct bhndb_hw_priority bhndb_bcma_priority_table[] = {
	/*
	 * Ignorable device classes.
	 * 
	 * Runtime access to these cores is not required, and no register
	 * windows should be reserved for these device types.
	 */
	BHNDB_CLASS_PRIO(SOC_ROUTER,	NONE),
	BHNDB_CLASS_PRIO(SOC_BRIDGE,	NONE),
	BHNDB_CLASS_PRIO(EROM,		NONE),
	BHNDB_CLASS_PRIO(OTHER,		NONE),

	/*
	 * Low priority device classes.
	 * 
	 * These devices do not sit in a performance-critical path and can be
	 * treated as a low allocation priority.
	 */
	BHNDB_CLASS_PRIO(CC,		LOW,
		/* Device Block */
		BHNDB_PORT0_PRIO(DEVICE,	LOW),

		/* CC agent registers are not accessed via the bridge. */
		BHNDB_PORT0_PRIO(AGENT,		NONE)
	),

	BHNDB_CLASS_PRIO(PMU,		LOW,
		/* Device Block */
		BHNDB_PORT0_PRIO(DEVICE,	LOW),

		/* PMU agent registers are not accessed via the bridge. */
		BHNDB_PORT0_PRIO(AGENT,		NONE)
	),

	/*
	 * Default Core Behavior
	 * 
	 * All other cores are assumed to require efficient runtime access to
	 * the default device port, and if supported by the bus, an agent port.
	 */
	BHNDB_DEFAULT_PRIO(
		/* Device Block */
		BHNDB_PORT0_PRIO(DEVICE,	HIGH),

		/* Agent Block */
		BHNDB_PORT0_PRIO(AGENT,		DEFAULT)
	),

	BHNDB_HW_PRIORITY_TABLE_END
};

/**
 * Generic resource priority configuration usable with all currently supported
 * siba(4)-based PCI devices.
 */
const struct bhndb_hw_priority bhndb_siba_priority_table[] = {
	/*
	 * Ignorable device classes.
	 * 
	 * Runtime access to these cores is not required, and no register
	 * windows should be reserved for these device types.
	 */
	BHNDB_CLASS_PRIO(SOC_ROUTER,	NONE),
	BHNDB_CLASS_PRIO(SOC_BRIDGE,	NONE),
	BHNDB_CLASS_PRIO(EROM,		NONE),
	BHNDB_CLASS_PRIO(OTHER,		NONE),

	/*
	 * Low priority device classes.
	 * 
	 * These devices do not sit in a performance-critical path and can be
	 * treated as a low allocation priority.
	 * 
	 * Agent ports are marked as 'NONE' on siba(4) devices, as they
	 * will be fully mappable via register windows shared with the
	 * device0.0 port.
	 * 
	 * To support early PCI_V0 devices, we enable FULFILL_ON_OVERCOMMIT for
	 * ChipCommon.
	 */
	BHNDB_CLASS_PRIO(CC,		LOW,
		/* Device Block */
		BHNDB_PORT_PRIO(DEVICE,	0,	0,	LOW,
		    BHNDB_ALLOC_FULFILL_ON_OVERCOMMIT)
	),

	BHNDB_CLASS_PRIO(PMU,		LOW,
		/* Device Block */
		BHNDB_PORT_PRIO(DEVICE,	0,	0,	LOW)
	),

	/*
	 * Default Core Behavior
	 * 
	 * All other cores are assumed to require efficient runtime access to
	 * the device port.
	 */
	BHNDB_DEFAULT_PRIO(
		/* Device Block */
		BHNDB_PORT_PRIO(DEVICE,	0,	0,	HIGH)
	),

	BHNDB_HW_PRIORITY_TABLE_END
};
