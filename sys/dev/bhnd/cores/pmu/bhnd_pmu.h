/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2016 Landon Fuller <landonf@FreeBSD.org>
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

#ifndef _BHND_CORES_PMU_BHND_PMU_H_
#define _BHND_CORES_PMU_BHND_PMU_H_

#include <sys/types.h>

#include <dev/bhnd/bhnd.h>

#include "bhnd_pmu_if.h"
#include "bhnd_pmu_types.h"


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
static inline uint32_t
bhnd_pmu_read_chipctrl(device_t dev, uint32_t reg)
{
	return (BHND_PMU_READ_CHIPCTRL(dev, reg));
}

/**
 * Write @p value with @p mask to a PMU chipctrl register.
 *
 * @param dev A bhnd(4) PMU device.
 * @param reg The PMU chipctrl register to be written.
 * @param value The value to write.
 * @param mask The mask of bits to be written from @p value.
 *
 * Drivers should only use function for functionality that is not
 * available via another bhnd_pmu() function.
 */
static inline void
bhnd_pmu_write_chipctrl(device_t dev, uint32_t reg, uint32_t value,
    uint32_t mask)
{
	return (BHND_PMU_WRITE_CHIPCTRL(dev, reg, value, mask));
}

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
static inline uint32_t
bhnd_pmu_read_regctrl(device_t dev, uint32_t reg)
{
	return (BHND_PMU_READ_REGCTRL(dev, reg));
}

/**
 * Write @p value with @p mask to a PMU regulator control register.
 *
 * @param dev A bhnd(4) PMU device.
 * @param reg The PMU regctrl register to be written.
 * @param value The value to write.
 * @param mask The mask of bits to be written from @p value.
 *
 * Drivers should only use function for functionality that is not
 * available via another bhnd_pmu() function.
 */
static inline void
bhnd_pmu_write_regctrl(device_t dev, uint32_t reg, uint32_t value,
    uint32_t mask)
{
	return (BHND_PMU_WRITE_REGCTRL(dev, reg, value, mask));
}

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
static inline uint32_t
bhnd_pmu_read_pllctrl(device_t dev, uint32_t reg)
{
	return (BHND_PMU_READ_PLLCTRL(dev, reg));
}

/**
 * Write @p value with @p mask to a PMU PLL control register.
 *
 * @param dev A bhnd(4) PMU device.
 * @param reg The PMU pllctrl register to be written.
 * @param value The value to write.
 * @param mask The mask of bits to be written from @p value.
 *
 * Drivers should only use function for functionality that is not
 * available via another bhnd_pmu() function.
 */
static inline void
bhnd_pmu_write_pllctrl(device_t dev, uint32_t reg, uint32_t value,
    uint32_t mask)
{
	return (BHND_PMU_WRITE_PLLCTRL(dev, reg, value, mask));
}

/**
 * Set a hardware-specific output voltage register value for @p regulator.
 *
 * @param dev PMU device.
 * @param regulator Regulator to be configured.
 * @param value The raw voltage register value.
 *
 * @retval 0 success
 * @retval ENODEV If @p regulator is not supported by this driver.
 */
static inline int
bhnd_pmu_set_voltage_raw(device_t dev, bhnd_pmu_regulator regulator,
    uint32_t value)
{
	return (BHND_PMU_SET_VOLTAGE_RAW(dev, regulator, value));
}

/**
 * Enable the given @p regulator.
 *
 * @param dev PMU device.
 * @param regulator Regulator to be enabled.
 *
 * @retval 0 success
 * @retval ENODEV If @p regulator is not supported by this driver.
 */
static inline int
bhnd_pmu_enable_regulator(device_t dev, bhnd_pmu_regulator regulator)
{
	return (BHND_PMU_ENABLE_REGULATOR(dev, regulator));
}

/**
 * Disable the given @p regulator.
 *
 * @param dev PMU device.
 * @param regulator Regulator to be disabled.
 *
 * @retval 0 success
 * @retval ENODEV If @p regulator is not supported by this driver.
 */
static inline int
bhnd_pmu_disable_regulator(device_t dev, bhnd_pmu_regulator regulator)
{
	return (BHND_PMU_DISABLE_REGULATOR(dev, regulator));
}

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
 * @retval 0 success
 * @retval ENODEV If the transition latency for @p clock is not available.
 */
static inline int
bhnd_pmu_get_clock_latency(device_t dev, bhnd_clock clock, u_int *latency)
{
	return (BHND_PMU_GET_CLOCK_LATENCY(dev, clock, latency));
}

/**
 * Return the frequency for @p clock in Hz, if known.
 *
 * @param dev PMU device.
 * @param clock The clock to be queried.
 * @param[out] freq On success, the frequency of @p clock in Hz.
 * 
 * @retval 0 success
 * @retval ENODEV If the frequency for @p clock is not available.
 */
static inline int
bhnd_pmu_get_clock_freq(device_t dev, bhnd_clock clock, u_int *freq)
{
	return (BHND_PMU_GET_CLOCK_FREQ(dev, clock, freq));
}

/**
 * Request that the PMU configure itself for a given hardware-specific
 * spuravoid mode.
 *
 * @param dev PMU device.
 * @param spuravoid The requested mode.
 *
 * @retval 0 success
 * @retval ENODEV If @p regulator is not supported by this driver.
 */
static inline int
bhnd_pmu_request_spuravoid(device_t dev, bhnd_pmu_spuravoid spuravoid)
{
	return (BHND_PMU_REQUEST_SPURAVOID(dev, spuravoid));
}


/**
 * Return the PMU's maximum state transition latency in microseconds.
 *
 * This upper bound may be used to busy-wait on PMU clock and resource state
 * transitions.
 *
 * @param dev PMU device.
 */
static inline u_int
bhnd_pmu_get_max_transition_latency(device_t dev)
{
	return (BHND_PMU_GET_MAX_TRANSITION_LATENCY(dev));
}

#endif /* _BHND_CORES_PMU_BHND_PMU_H_ */
