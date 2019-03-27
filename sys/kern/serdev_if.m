#-
# Copyright (c) 2006 Marcel Moolenaar
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
#include <sys/serial.h>

# The serdev interface is used by umbrella drivers and children thereof to
# establish a more intimate relationship, necessary for efficient handling
# of multiple (concurrent) serial communication channels.  Examples include
# serial communications controller (SCC) drivers, multi-I/O adapter drivers
# and intelligent multi-port serial drivers.  Methods specifically deal
# with interrupt handling and configuration.  Conceptually, the umbrella
# driver is responsible for the overall operation of the hardware and uses
# child drivers to handle each individual channel.
# The serdev interface is intended to inherit the device interface.

INTERFACE serdev;

# Default implementations of some methods.
CODE {
	static serdev_intr_t *
	default_ihand(device_t dev, int ipend)
	{
		return (NULL);
	}

	static int
	default_ipend(device_t dev)
	{
		return (-1);
	}

	static int
	default_sysdev(device_t dev)
	{
		return (0);
	}
};

# ihand() - Query serial device interrupt handler.
# This method is called by the umbrella driver to obtain function pointers
# to interrupt handlers for each individual interrupt source. This allows
# the umbralla driver to control the servicing of interrupts between the
# different channels in the most flexible way.
METHOD serdev_intr_t* ihand {
	device_t dev;
	int ipend;
} DEFAULT default_ihand;

# ipend() - Query pending interrupt status.
# This method is called by the umbrella driver to obtain interrupt status
# for the UART in question. This allows the umbrella driver to build a
# matrix and service the interrupts in the most flexible way by calling
# interrupt handlers collected with the ihand() method.
METHOD int ipend {
	device_t dev;
} DEFAULT default_ipend;

# sysdev() - Query system device status 
# This method may be called by the umbrella driver for each child driver
# to establish if a particular channel and mode is currently being used
# for system specific usage. If this is the case, the hardware is not
# reset and the channel will not change its operation mode.
# The return value is !0 if the channel and mode are used for a system
# device and 0 otherwise.
METHOD int sysdev {
	device_t dev;
} DEFAULT default_sysdev;

