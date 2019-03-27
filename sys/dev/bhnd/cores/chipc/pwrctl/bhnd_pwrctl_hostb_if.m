#-
# Copyright (c) 2016 Landon Fuller <landon@landonf.org>
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

#include <sys/types.h>
#include <sys/bus.h>

#include <dev/bhnd/bhnd.h>

INTERFACE bhnd_pwrctl_hostb;

#
# bhnd(4) PWRCTL host bridge interface.
#
# Provides a common interface to the clock hardware managed by a parent host
# bridge (e.g. bhndb_pci(4)).
#
# Early PWRCTL chipsets[1] expose clock management via their host bridge
# interface, requiring that a host bridge driver (e.g. bhndb(4)) work in
# tandem with the ChipCommon-attached PWRCTL driver.
#
# [1] Currently, this is known to include PCI (not PCIe) devices, with
# ChipCommon core revisions 0-9.
#

HEADER {
	#include <dev/bhnd/bhnd.h>
};

CODE {
	static bhnd_clksrc
	bhnd_pwrctl_hostb_get_clksrc(device_t dev, device_t child,
	    bhnd_clock clock)
	{
		return (BHND_CLKSRC_UNKNOWN);
	}

	static int
	bhnd_pwrctl_hostb_gate_clock(device_t dev, device_t child,
	    bhnd_clock clock)
	{
		return (ENODEV);
	}

	static int
	bhnd_pwrctl_hostb_ungate_clock(device_t dev, device_t child,
	    bhnd_clock clock)
	{
		return (ENODEV);
	}

};

/**
 * If supported by the chipset, return the clock source for the given clock.
 *
 * @param dev	The parent of @p child.
 * @param child	The bhnd device requesting a clock source.
 * @param clock	The clock for which a clock source will be returned.
 *
 * @retval	bhnd_clksrc		The clock source for @p clock.
 * @retval	BHND_CLKSRC_UNKNOWN	If @p clock is unsupported, or its
 *					clock source is not known to the bus.
 */
METHOD bhnd_clksrc get_clksrc {
	device_t	dev;
	device_t	child;
	bhnd_clock	clock;
} DEFAULT bhnd_pwrctl_hostb_get_clksrc;

/**
 * If supported by the chipset, gate the clock source for @p clock.
 *
 * @param dev	The parent of @p child.
 * @param child	The bhnd device requesting clock gating.
 * @param clock	The clock to be disabled.
 *
 * @retval 0		success
 * @retval ENODEV	If bus-level clock source management is not supported.
 * @retval ENXIO	If bus-level management of @p clock is not supported.
 */
METHOD int gate_clock {
	device_t	dev;
	device_t	child;
	bhnd_clock	clock;
} DEFAULT bhnd_pwrctl_hostb_gate_clock;

/**
 * If supported by the chipset, ungate the clock source for @p clock.
 *
 * @param dev	The parent of @p child.
 * @param child	The bhnd device requesting clock gating.
 * @param clock	The clock to be enabled.
 *
 * @retval 0		success
 * @retval ENODEV	If bus-level clock source management is not supported.
 * @retval ENXIO	If bus-level management of @p clock is not supported.
 */
METHOD int ungate_clock {
	device_t	dev;
	device_t	child;
	bhnd_clock	clock;
} DEFAULT bhnd_pwrctl_hostb_ungate_clock;