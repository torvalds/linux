#-
# Copyright (c) 2015-2016 Chelsio Communications, Inc.
# All rights reserved.
# Written by: John Baldwin <jhb@FreeBSD.org>
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

INTERFACE t4;

# The "main" device of a T4/T5 NIC is the PF4 device.  Drivers for other
# functions on the NIC need to wait for the main device to be initialized
# before finishing attach.  These routines allow drivers for other devices
# to coordinate with the main driver for the PF4.

# Called by a driver during attach to determine if the PF4 driver is
# initialized.  If the main driver is not ready, the driver should defer
# further initialization until 'attach_child'.
METHOD int is_main_ready {
	device_t	dev;
};

# Called by the PF4 driver on each sibling device when the PF4 driver is
# initialized.
METHOD int attach_child {
	device_t	dev;
};

# Called by the PF4 driver on each sibling device when the PF4 driver is
# preparing to detach.
METHOD int detach_child {
	device_t	dev;
};

# Called by a driver to query the PF4 driver for the child device
# associated with a given port.  If the port is not enabled on the adapter,
# this will fail.
METHOD int read_port_device {
	device_t	dev;
	int		port;
	device_t	*child;
};
