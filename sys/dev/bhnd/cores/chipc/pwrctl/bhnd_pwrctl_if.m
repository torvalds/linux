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

INTERFACE bhnd_pwrctl;

#
# bhnd(4) PWRCTL interface.
#

HEADER {
	#include <dev/bhnd/bhnd.h>
};

/** 
 * Request that @p clock (or a faster clock) be enabled on behalf of
 * @p child.
 *
 * @param dev	PWRCTL device.
 * @param child	The requesting bhnd(4) device.
 * @param clock	Clock requested.
 *
 * @retval 0		success
 * @retval ENODEV	If an unsupported clock was requested.
 */
METHOD int request_clock {
	device_t	dev;
	device_t	child;
	bhnd_clock	clock;
};

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
 * @retval 0		success
 * @retval ENODEV	If the transition latency for @p clock is not available.
 */
METHOD int get_clock_latency {
	device_t	 dev;
	bhnd_clock	 clock;
	u_int		*latency;
};

/**
 * Return the frequency for @p clock in Hz, if known.
 *
 * @param	dev	PWRCTL device.
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
};
