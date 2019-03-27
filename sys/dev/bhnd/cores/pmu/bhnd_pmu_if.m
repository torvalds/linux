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

INTERFACE bhnd_pmu;

#
# bhnd(4) PMU interface.
#
# Provides an interface to the PMU hardware found on modern bhnd(4) chipsets.
#

HEADER {
	#include <dev/bhnd/cores/pmu/bhnd_pmu_types.h>

	struct bhnd_core_pmu_info;
}

CODE {

	static uint32_t
	bhnd_pmu_null_read_chipctrl(device_t dev, uint32_t reg)
	{
		panic("bhnd_pmu_read_chipctrl unimplemented");
	}

	static void
	bhnd_pmu_null_write_chipctrl(device_t dev, uint32_t reg, uint32_t value,
	uint32_t mask)
	{
		panic("bhnd_pmu_write_chipctrl unimplemented");
	}

	static uint32_t
	bhnd_pmu_null_read_regctrl(device_t dev, uint32_t reg)
	{
		panic("bhnd_pmu_read_regctrl unimplemented");
	}

	static void
	bhnd_pmu_null_write_regctrl(device_t dev, uint32_t reg, uint32_t value,
	uint32_t mask)
	{
		panic("bhnd_pmu_write_regctrl unimplemented");
	}

	static uint32_t
	bhnd_pmu_null_read_pllctrl(device_t dev, uint32_t reg)
	{
		panic("bhnd_pmu_read_pllctrl unimplemented");
	}

	static void
	bhnd_pmu_null_write_pllctrl(device_t dev, uint32_t reg, uint32_t value,
	uint32_t mask)
	{
		panic("bhnd_pmu_write_pllctrl unimplemented");
	}

	static int
	bhnd_pmu_null_request_spuravoid(device_t dev,
	    bhnd_pmu_spuravoid spuravoid)
	{
		panic("bhnd_pmu_request_spuravoid unimplemented");
	}

	static int
	bhnd_pmu_null_set_voltage_raw(device_t dev,
	    bhnd_pmu_regulator regulator, uint32_t value)
	{
		panic("bhnd_pmu_set_voltage_raw unimplemented");
	}

	static int
	bhnd_pmu_null_enable_regulator(device_t dev,
	    bhnd_pmu_regulator regulator)
	{
		panic("bhnd_pmu_enable_regulator unimplemented");
	}

	static int
	bhnd_pmu_null_disable_regulator(device_t dev,
	    bhnd_pmu_regulator regulator)
	{
		panic("bhnd_pmu_disable_regulator unimplemented");
	}

	static int
	bhnd_pmu_null_get_clock_latency(device_t dev, bhnd_clock clock,
	    u_int *latency)
	{
		panic("bhnd_pmu_get_clock_latency unimplemented");
	}

	static int
	bhnd_pmu_null_get_clock_freq(device_t dev, bhnd_clock clock,
	    u_int *freq)
	{
		panic("bhnd_pmu_get_clock_freq unimplemented");
	}
}

/**
 * Return the current value of a PMU chipctrl register.
 *
 * @param dev A bhnd(4) PMU device.
 * @param reg The PMU chipctrl register to be read.
 *
 * Drivers should only use function for functionality that is not
 * available via another bhnd_chipc() function.
 *
 * @returns The chipctrl register value, or 0 if undefined by this hardware.
 */
METHOD uint32_t read_chipctrl {
	device_t dev;
	uint32_t reg;
} DEFAULT bhnd_pmu_null_read_chipctrl;

/**
 * Write @p value with @p mask to a PMU chipctrl register.
 *
 * @param dev	A bhnd(4) PMU device.
 * @param reg	The PMU chipctrl register to be written.
 * @param value	The value to write.
 * @param mask	The mask of bits to be written from @p value.
 *
 * Drivers should only use function for functionality that is not
 * available via another bhnd_pmu() function.
 */
METHOD void write_chipctrl {
	device_t dev;
	uint32_t reg;
	uint32_t value;
	uint32_t mask;
} DEFAULT bhnd_pmu_null_write_chipctrl;

/**
 * Return the current value of a PMU regulator control register.
 *
 * @param dev A bhnd(4) PMU device.
 * @param reg The PMU regctrl register to be read.
 *
 * Drivers should only use function for functionality that is not
 * available via another bhnd_chipc() function.
 *
 * @returns The regctrl register value, or 0 if undefined by this hardware.
 */
METHOD uint32_t read_regctrl {
	device_t dev;
	uint32_t reg;
} DEFAULT bhnd_pmu_null_read_regctrl;

/**
 * Write @p value with @p mask to a PMU regulator control register.
 *
 * @param dev	A bhnd(4) PMU device.
 * @param reg	The PMU regctrl register to be written.
 * @param value	The value to write.
 * @param mask	The mask of bits to be written from @p value.
 *
 * Drivers should only use function for functionality that is not
 * available via another bhnd_pmu() function.
 */
METHOD void write_regctrl {
	device_t dev;
	uint32_t reg;
	uint32_t value;
	uint32_t mask;
} DEFAULT bhnd_pmu_null_write_regctrl;

/**
 * Return the current value of a PMU PLL control register.
 *
 * @param dev A bhnd(4) PMU device.
 * @param reg The PMU pllctrl register to be read.
 *
 * Drivers should only use function for functionality that is not
 * available via another bhnd_chipc() function.
 *
 * @returns The pllctrl register value, or 0 if undefined by this hardware.
 */
METHOD uint32_t read_pllctrl {
	device_t dev;
	uint32_t reg;
} DEFAULT bhnd_pmu_null_read_pllctrl;

/**
 * Write @p value with @p mask to a PMU PLL control register.
 *
 * @param dev	A bhnd(4) PMU device.
 * @param reg	The PMU pllctrl register to be written.
 * @param value	The value to write.
 * @param mask	The mask of bits to be written from @p value.
 *
 * Drivers should only use function for functionality that is not
 * available via another bhnd_pmu() function.
 */
METHOD void write_pllctrl {
	device_t dev;
	uint32_t reg;
	uint32_t value;
	uint32_t mask;
} DEFAULT bhnd_pmu_null_write_pllctrl;

/**
 * Set a hardware-specific output voltage register value for @p regulator.
 *
 * @param dev		PMU device.
 * @param regulator	Regulator to be configured.
 * @param value		The raw voltage register value.
 *
 * @retval 0		success
 * @retval ENODEV	If @p regulator is not supported by this driver.
 */
METHOD int set_voltage_raw {
	 device_t		dev;
	 bhnd_pmu_regulator	regulator;
	 uint32_t		value;
} DEFAULT bhnd_pmu_null_set_voltage_raw;

/**
 * Enable the given @p regulator.
 *
 * @param dev		PMU device.
 * @param regulator	Regulator to be enabled.
 *
 * @retval 0		success
 * @retval ENODEV	If @p regulator is not supported by this driver.
 */
METHOD int enable_regulator {
	device_t		dev;
	bhnd_pmu_regulator	regulator;
} DEFAULT bhnd_pmu_null_enable_regulator;

/**
 * Disable the given @p regulator.
 *
 * @param dev		PMU device.
 * @param regulator	Regulator to be disabled.
 *
 * @retval 0		success
 * @retval ENODEV	If @p regulator is not supported by this driver.
 */
METHOD int disable_regulator {
	device_t		dev;
	bhnd_pmu_regulator	regulator;
} DEFAULT bhnd_pmu_null_disable_regulator;

/**
 * Return the transition latency required for @p clock in microseconds, if
 * known.
 *
 * The BHND_CLOCK_HT latency value is suitable for use as the D11 core's
 * 'fastpwrup_dly' value.
 *
 * @param	dev	PMU device.
 * @param	clock	The clock to be queried for transition latency.
 * @param[out]	latency	On success, the transition latency of @p clock in
 *			microseconds.
 * 
 * @retval 0		success
 * @retval ENODEV	If the transition latency for @p clock is not available.
 */
METHOD int get_clock_latency {
	device_t	 dev;
	bhnd_clock	 clock;
	u_int		*latency;
} DEFAULT bhnd_pmu_null_get_clock_latency;

/**
 * Return the frequency for @p clock in Hz, if known.
 *
 * @param	dev	PMU device.
 * @param	clock	The clock to be queried.
 * @param[out]	freq	On success, the frequency of @p clock in Hz.
 * 
 * @retval 0		success
 * @retval ENODEV	If the frequency for @p clock is not available.
 */
METHOD int get_clock_freq {
	device_t	 dev;
	bhnd_clock	 clock;
	u_int		*freq;
} DEFAULT bhnd_pmu_null_get_clock_freq;

/**
 * Request that the PMU configure itself for a given hardware-specific
 * spuravoid mode.
 *
 * @param dev		PMU device.
 * @param spuravoid	The requested mode.
 *
 * @retval 0		success
 * @retval ENODEV	If @p regulator is not supported by this driver.
 */
METHOD int request_spuravoid {
	 device_t		dev;
	 bhnd_pmu_spuravoid	spuravoid;
} DEFAULT bhnd_pmu_null_request_spuravoid;

/**
 * Return the PMU's maximum state transition latency in microseconds.
 *
 * This upper bound may be used to busy-wait on PMU clock and resource state
 * transitions.
 *
 * @param dev PMU device.
 *
 * @returns maximum PMU transition latency, in microseconds.
 */
METHOD u_int get_max_transition_latency {
	device_t dev;
};
