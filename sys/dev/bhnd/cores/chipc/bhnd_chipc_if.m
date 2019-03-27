#-
# Copyright (c) 2016 Landon Fuller <landon@landonf.org>
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

#include <dev/bhnd/bhnd.h>

INTERFACE bhnd_chipc;

#
# bhnd(4) ChipCommon interface.
#

HEADER {
	/* forward declarations */
	struct chipc_caps;
}

CODE {
	static struct chipc_caps *
	bhnd_chipc_null_get_caps(device_t dev)
	{
		panic("bhnd_chipc_generic_get_caps unimplemented");
	}
}


/**
 * Return the current value of the chipstatus register.
 *
 * @param dev A bhnd(4) ChipCommon device.
 *
 * Drivers should only use function for functionality that is not
 * available via another bhnd_chipc() function.
 *
 * @returns The chipstatus register value, or 0 if undefined by this
 * hardware (e.g. if @p dev is an EXTIF core).
 */
METHOD uint32_t read_chipst {
	device_t dev;
}

/**
 * Write @p value with @p mask directly to the chipctrl register.
 *
 * @param dev A bhnd(4) ChipCommon device.
 * @param value The value to write.
 * @param mask The mask of bits to be written from @p value.
 *
 * Drivers should only use function for functionality that is not
 * available via another bhnd_chipc() function.
 *
 * Currently, the only known valid use-case is in implementing a hardware
 * work-around for the BCM4321 PCIe rev7 core revision.
 */
METHOD void write_chipctrl {
	device_t dev;
	uint32_t value;
	uint32_t mask;
}

/**
 * Return a borrowed reference to ChipCommon's capability
 * table.
 *
 * @param dev A bhnd(4) ChipCommon device
 */
METHOD struct chipc_caps * get_caps {
	device_t dev;
} DEFAULT bhnd_chipc_null_get_caps;

/**
 * Enable hardware access to the SPROM/OTP source.
 * 
 * @param sc chipc driver state.
 *
 * @retval 0		success
 * @retval EBUSY	If enabling the hardware may conflict with
 *			other active devices.
 */
METHOD int enable_sprom {
	device_t dev;
}

/**
 * Release hardware access to the SPROM/OTP source.
 * 
 * @param sc chipc driver state.
 */
METHOD void disable_sprom {
	device_t dev;
}
