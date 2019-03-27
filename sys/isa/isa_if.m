#-
# Copyright (c) 1999 Doug Rabson
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
#include <isa/isavar.h>

INTERFACE isa;

#
# Add a Plug-and-play configuration to the device. Configurations with 
# a lower priority are preferred.
#
METHOD int add_config {
	device_t	dev;
	device_t	child;
	int		priority;
	struct isa_config *config;
};

#
# Register a function which can be called to configure a device with
# a given set of resources. The function will be called with a struct
# isa_config representing the desired configuration and a flag to
# state whether the device should be enabled.
#
METHOD void set_config_callback {
	device_t	dev;
	device_t	child;
	isa_config_cb	*fn;
	void		*arg;
};

#
# A helper method for implementing probe methods for PnP compatible
# drivers. The driver calls this method with a list of PnP ids and
# descriptions and it returns zero if one of the ids matches or ENXIO
# otherwise.
# 
# If the device is not plug-and-play compatible, this method returns
# ENOENT, allowing the caller to fall back to heuristic probing
# techniques.
#  
METHOD int pnp_probe {
	device_t	dev;
	device_t	child;
	struct isa_pnp_id *ids;
};
