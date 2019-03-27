/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Emmanuel Vadot <manu@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef _PWMBUS_H_

#include <dev/ofw/openfirm.h>
#include <sys/pwm.h>

struct pwm_channel {
	device_t	dev;
	device_t	busdev;
	int		channel;
	uint64_t	period;
	uint64_t	duty;
	uint32_t	flags;
	bool		enabled;
};
typedef struct pwm_channel *pwm_channel_t;

device_t pwmbus_attach_bus(device_t dev);
int pwmbus_acquire_channel(device_t bus, int channel);
int pwmbus_release_channel(device_t bus, int channel);

int
pwm_get_by_ofw_propidx(device_t consumer, phandle_t node,
    const char *prop_name, int idx, pwm_channel_t *channel);
int
pwm_get_by_ofw_idx(device_t consumer, phandle_t node, int idx,
    pwm_channel_t *out_channel);
int
pwm_get_by_ofw_property(device_t consumer, phandle_t node,
    const char *prop_name, pwm_channel_t *out_channel);
int
pwm_get_by_ofw_name(device_t consumer, phandle_t node, const char *name,
    pwm_channel_t *out_channel);

#endif /* _PWMBUS_H_ */
