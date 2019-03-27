#-
# Copyright (c) 2015-2016 Landon Fuller <landon@landonf.org>
# Copyright (c) 2017 The FreeBSD Foundation
# All rights reserved.
#
# Portions of this software were developed by Landon Fuller
# under sponsorship from the FreeBSD Foundation.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
# USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$

#include <sys/param.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/bhnd/bhnd.h>

#
# bhndb bridge device interface.
#

INTERFACE bhndb;

HEADER {
	struct bhndb_intr_isrc;
	struct bhndb_regwin;
	struct bhndb_hw;
	struct bhndb_hw_priority;
}

CODE {
	#include <sys/systm.h>
	#include <dev/bhnd/bhndb/bhndbvar.h>

	static const struct bhnd_chipid *
	bhndb_null_get_chipid(device_t dev, device_t child)
	{
		panic("bhndb_get_chipid unimplemented");
	}

	static int
	bhndb_null_populate_board_info(device_t dev, device_t child,
	    struct bhnd_board_info *info)
	{
		panic("bhndb_populate_board_info unimplemented");
	}

	static int
	bhndb_null_is_core_disabled(device_t dev, device_t child,
	    struct bhnd_core_info *core)
	{
		panic("bhndb_is_core_disabled unimplemented");
	}

	static int
	bhndb_null_get_hostb_core(device_t dev, device_t child,
	    struct bhnd_core_info *core)
	{
		panic("bhndb_get_hostb_core unimplemented");
	}
	
	static void
	bhndb_null_suspend_resource(device_t dev, device_t child, int type,
	    struct resource *r)
	{
		panic("bhndb_suspend_resource unimplemented");
	}

	static int
	bhndb_null_resume_resource(device_t dev, device_t child, int type,
	    struct resource *r)
	{
		panic("bhndb_resume_resource unimplemented");
	}

	static int
	bhndb_null_route_interrupts(device_t dev, device_t child)
	{
		panic("bhndb_route_interrupts unimplemented");
	}

	static int
	bhndb_null_set_window_addr(device_t dev,
	    const struct bhndb_regwin *rw, bhnd_addr_t addr)
	{
		panic("bhndb_set_window_addr unimplemented");
	}

	static int
	bhndb_null_map_intr_isrc(device_t dev, struct resource *irq,
	    struct bhndb_intr_isrc **isrc)
	{
		panic("bhndb_map_intr_isrc unimplemented");
	}
}

/**
 * Return the chip identification information for @p child.
 *
 * @param dev The parent device of @p child.
 * @param child The bhndb-attached device.
 */
METHOD const struct bhnd_chipid * get_chipid {
	device_t dev;
	device_t child;
} DEFAULT bhndb_null_get_chipid;

/**
 * Populate @p info with board info known only to the bridge,
 * deferring to any existing initialized fields in @p info.
 *
 * @param dev The parent device of @p child.
 * @param child The bhndb-attached device.
 * @param[in,out] info A board info structure previously initialized with any
 * information available from NVRAM.
 */
METHOD int populate_board_info {
	device_t dev;
	device_t child;
	struct bhnd_board_info *info;
} DEFAULT bhndb_null_populate_board_info;

/**
 * Return true if the hardware required by @p core is unpopulated or
 * otherwise unusable.
 *
 * In some cases, the core's pins may be left floating, or the hardware
 * may otherwise be non-functional; this method allows the parent device
 * to explicitly specify whether @p core should be disabled.
 *
 * @param dev The parent device of @p child.
 * @param child The attached bhnd device.
 * @param core A core discovered on @p child.
 */
METHOD bool is_core_disabled {
	device_t dev;
	device_t child;
	struct bhnd_core_info *core;
} DEFAULT bhndb_null_is_core_disabled;

/**
 * Get the host bridge core info for the attached bhnd bus.
 *
 * @param	dev	The bridge device.
 * @param	child	The bhnd bus device attached to @p dev.
 * @param[out]	core	Will be populated with the host bridge core info, if
 *			found.
 *
 * @retval 0		success
 * @retval ENOENT	No host bridge core found.
 * @retval non-zero	If locating the host bridge core otherwise fails, a
 *			regular UNIX error code should be returned.
 */
METHOD int get_hostb_core {
	device_t dev;
	device_t child;
	struct bhnd_core_info *core;
} DEFAULT bhndb_null_get_hostb_core;

/**
 * Mark a resource as 'suspended', gauranteeing to the bridge that no
 * further use of the resource will be made until BHNDB_RESUME_RESOURCE()
 * is called.
 *
 * Bridge resources consumed by the reference may be released; these will
 * be reacquired if BHNDB_RESUME_RESOURCE() completes successfully.
 *
 * Requests to suspend a suspended resource will be ignored.
 *
 * @param dev The bridge device.
 * @param child The child device requesting resource suspension. This does
 * not need to be the owner of @p r.
 * @param type The resource type.
 * @param r The resource to be suspended.
 */
METHOD void suspend_resource {
	device_t dev;
	device_t child;
	int type;
	struct resource *r;
} DEFAULT bhndb_null_suspend_resource;

/**
 * Attempt to re-enable a resource previously suspended by
 * BHNDB_SUSPEND_RESOURCE().
 *
 * Bridge resources required by the reference may not be available, in which
 * case an error will be returned and the resource mapped by @p r must not be
 * used in any capacity.
 *
 * Requests to resume a non-suspended resource will be ignored.
 * 
 * @param dev The bridge device.
 * @param child The child device requesting resource suspension. This does
 * not need to be the owner of @p r.
 * @param type The resource type.
 * @param r The resource to be suspended.
 */
METHOD int resume_resource {
	device_t dev;
	device_t child;
	int type;
	struct resource *r;
} DEFAULT bhndb_null_resume_resource;

/**
 * Enable bridge-level interrupt routing for @p child.
 *
 * @param dev The bridge device.
 * @param child The bhnd child device for which interrupts should be routed.
 */
METHOD int route_interrupts {
	device_t dev;
	device_t child;
} DEFAULT bhndb_null_route_interrupts;

/**
 * Set a given register window's base address.
 *
 * @param dev The bridge device.
 * @param win The register window.
 * @param addr The address to be configured for @p win.
 *
 * @retval 0 success
 * @retval ENODEV The provided @p win is not memory-mapped on the bus or does
 * not support setting a base address.
 * @retval non-zero failure
 */
METHOD int set_window_addr {
	device_t dev;
	const struct bhndb_regwin *win;
	bhnd_addr_t addr;
} DEFAULT bhndb_null_set_window_addr;

/**
 * Map a bridged interrupt resource to its corresponding host interrupt source,
 * if any.
 *
 * @param dev The bridge device.
 * @param irq The bridged interrupt resource.
 * @param[out] isrc The host interrupt source to which the bridged interrupt
 * is routed.
 *
 * @retval 0 success
 * @retval non-zero if mapping @p irq otherwise fails, a regular unix error code
 * will be returned.
 */
METHOD int map_intr_isrc {
	device_t dev;
	struct resource *irq;
	struct bhndb_intr_isrc **isrc;
} DEFAULT bhndb_null_map_intr_isrc;
