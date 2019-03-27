#-
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2018 Emmanuel Vadot <manu@FreeBSD.org>
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
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#

#include <sys/bus.h>

INTERFACE pwmbus;

CODE {
	static int
	pwm_default_set_flags(device_t dev, int channel, uint32_t flags)
	{

		return (EOPNOTSUPP);
	}

	static int
	pwm_default_get_flags(device_t dev, int channel, uint32_t *flags)
	{

		*flags = 0;
		return (0);
	}
};

HEADER {
	#include <sys/pwm.h>
};

#
# Config the period (Total number of cycle in ns) and
# the duty (active number of cycle in ns)
#
METHOD int channel_config {
	device_t bus;
	int channel;
	unsigned int period;
	unsigned int duty;
};

#
# Get the period (Total number of cycle in ns) and
# the duty (active number of cycle in ns)
#
METHOD int channel_get_config {
	device_t bus;
	int channel;
	unsigned int *period;
	unsigned int *duty;
};

#
# Set the flags
#
METHOD int channel_set_flags {
	device_t bus;
	int channel;
	uint32_t flags;
} DEFAULT pwm_default_set_flags;

#
# Get the flags
#
METHOD int channel_get_flags {
	device_t dev;
	int channel;
	uint32_t *flags;
} DEFAULT pwm_default_get_flags;

#
# Enable the pwm output
#
METHOD int channel_enable {
	device_t bus;
	int channel;
	bool enable;
};

#
# Is the pwm output enabled
#
METHOD int channel_is_enabled {
	device_t bus;
	int channel;
	bool *enabled;
};
