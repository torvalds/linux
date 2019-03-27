#
# Copyright (c) 2004 Nate Lawson
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

INTERFACE cpufreq;

HEADER {
	struct cf_level;
	struct cf_setting;
};

# cpufreq interface methods

#
# Set the current CPU frequency level.
#
METHOD int set {
	device_t		dev;
	const struct cf_level	*level;
	int			priority;
};

#
# Get the current active level.
#
METHOD int get {
	device_t		dev;
	struct cf_level		*level;
};

#
# Get the current possible levels, based on all drivers.
#
METHOD int levels {
	device_t		dev;
	struct cf_level		*levels;
	int			*count;
};

# Individual frequency driver methods

#
# Set an individual driver's setting.
#
METHOD int drv_set {
	device_t		dev;
	const struct cf_setting	*set;
};

#
# Get an individual driver's setting.
#
METHOD int drv_get {
	device_t		dev;
	struct cf_setting	*set;
};

#
# Get the settings supported by a driver.
#
METHOD int drv_settings {
	device_t		dev;
	struct cf_setting	*sets;
	int			*count;
};

#
# Get an individual driver's type.
#
METHOD int drv_type {
	device_t		dev;
	int			*type;
};

