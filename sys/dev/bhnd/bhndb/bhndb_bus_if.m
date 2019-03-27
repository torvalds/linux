#-
# Copyright (c) 2015-2016 Landon Fuller <landonf@FreeBSD.org>
# All rights reserved.
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

#include <sys/types.h>
#include <sys/bus.h>

#
# Parent bus interface required by attached bhndb bridge devices.
#

INTERFACE bhndb_bus;

HEADER {
	struct bhnd_core_info;
	struct bhndb_hwcfg;
	struct bhndb_hw;
};

CODE {
	#include <sys/systm.h>

	static const struct bhnd_chipid *
	bhndb_null_get_chipid(device_t dev, device_t child)
	{
		return (NULL);
	}

	static const struct bhndb_hwcfg *
	bhndb_null_get_generic_hwcfg(device_t dev, device_t child)
	{
		panic("bhndb_get_generic_hwcfg unimplemented");
	}

	static const struct bhndb_hw *
	bhndb_null_get_hardware_table(device_t dev, device_t child)
	{
		panic("bhndb_get_hardware_table unimplemented");
	}
	
	static const struct bhndb_hw_priority *
	bhndb_null_get_hardware_prio(device_t dev, device_t child)
	{
		panic("bhndb_get_hardware_prio unimplemented");
	}

	static bool
	bhndb_null_is_core_disabled(device_t dev, device_t child,
	    struct bhnd_core_info *core)
	{
		return (true);
	}
}

/**
 * Return a generic hardware configuration to be used by
 * the bhndb bridge device to enumerate attached devices.
 *
 * @param dev The parent device.
 * @param child The attached bhndb device.
 *
 * @retval bhndb_hwcfg The configuration to use for bus enumeration.
 */
METHOD const struct bhndb_hwcfg * get_generic_hwcfg {
	device_t dev;
	device_t child;
} DEFAULT bhndb_null_get_generic_hwcfg;

/**
 * Provide chip identification information to be used by a @p child during
 * device enumeration.
 * 
 * May return NULL if the device includes a ChipCommon core.
 *
 * @param dev The parent device.
 * @param child The attached bhndb device.
 */
METHOD const struct bhnd_chipid * get_chipid {
	device_t dev;
	device_t child;
} DEFAULT bhndb_null_get_chipid;

/**
 * Return the hardware specification table to be used when identifying the
 * bridge's full hardware configuration.
 *
 * @param dev The parent device.
 * @param child The attached bhndb device.
 */
METHOD const struct bhndb_hw * get_hardware_table {
	device_t dev;
	device_t child;
} DEFAULT bhndb_null_get_hardware_table;

/**
 * Return the hardware priority table to be used when allocating bridge
 * resources.
 *
 * @param dev The parent device.
 * @param child The attached bhndb device.
 */
METHOD const struct bhndb_hw_priority * get_hardware_prio {
	device_t dev;
	device_t child;
} DEFAULT bhndb_null_get_hardware_prio;

/**
 * Return true if the hardware required by @p core is unpopulated or
 * otherwise unusable.
 *
 * In some cases, the core's pins may be left floating, or the hardware
 * may otherwise be non-functional; this method allows the parent device
 * to explicitly specify whether @p core should be disabled.
 *
 * @param dev The parent device.
 * @param child The attached bhndb device.
 * @param core A core discovered on @p child.
 */
METHOD bool is_core_disabled {
	device_t dev;
	device_t child;
	struct bhnd_core_info *core;
} DEFAULT bhndb_null_is_core_disabled;
