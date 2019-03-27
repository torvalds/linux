/*-
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
 *     notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _BHND_PWRCTL_BHND_PWRCTL_H_
#define _BHND_PWRCTL_BHND_PWRCTL_H_

#include <sys/param.h>
#include <sys/bus.h>

#include "bhnd_pwrctl_if.h"

/** 
 * Request that @p clock (or a faster clock) be enabled on behalf of
 * @p child.
 *
 * @param dev PWRCTL device.
 * @param child The requesting bhnd(4) device.
 * @param clock Clock requested.
 *
 * @retval 0 success
 * @retval ENODEV If an unsupported clock was requested.
 */
static inline int
bhnd_pwrctl_request_clock(device_t dev, device_t child, bhnd_clock clock)
{
	return (BHND_PWRCTL_REQUEST_CLOCK(dev, child, clock));
}

/**
 * Return the transition latency required for @p clock in microseconds, if
 * known.
 *
 * The BHND_CLOCK_HT latency value is suitable for use as the D11 core's
 * 'fastpwrup_dly' value.
 *
 * @param	dev	PWRCTL device.
 * @param	clock	The clock to be queried for transition latency.
 * @param[out]	latency	On success, the transition latency of @p clock in
 *			microseconds.
 * 
 * @retval 0 success
 * @retval ENODEV If the transition latency for @p clock is not available.
 */
static inline int
bhnd_pwrctl_get_clock_latency(device_t dev, bhnd_clock clock, u_int *latency)
{
	return (BHND_PWRCTL_GET_CLOCK_LATENCY(dev, clock, latency));
}

/**
 * Return the frequency for @p clock in Hz, if known.
 *
 * @param	dev	PWRCTL device.
 * @param	clock	The clock to be queried.
 * @param[out]	freq	On success, the frequency of @p clock in Hz.
 * 
 * @retval 0 success
 * @retval ENODEV If the frequency for @p clock is not available.
 */
static inline int
bhnd_pwrctl_get_clock_freq(device_t dev, bhnd_clock clock, u_int *freq)
{
	return (BHND_PWRCTL_GET_CLOCK_FREQ(dev, clock, freq));
}

#endif /* _BHND_PWRCTL_BHND_PWRCTL_H_ */
