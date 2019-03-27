#-
# Copyright (c) 1998 Nicolas Souchu
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
#include <dev/iicbus/iic.h>

INTERFACE iicbus;

CODE {
	static int iicbus_nosupport(void)
	{

		return (ENODEV);
	}

	static u_int
	iicbus_default_frequency(device_t bus, u_char speed)
	{

		return (100000);
	}
};

#
# Interpret interrupt
#
METHOD int intr {
	device_t dev;
	int event;
	char *buf;
};

#
# iicbus callback
# Request ownership of bus
# index: IIC_REQUEST_BUS or IIC_RELEASE_BUS
# data: pointer to int containing IIC_WAIT or IIC_DONTWAIT and either IIC_INTR or IIC_NOINTR
# This function is allowed to sleep if *data contains IIC_WAIT.
#
METHOD int callback {
	device_t dev;
	int index;
	caddr_t data;
};

#
# Send REPEATED_START condition
#
METHOD int repeated_start {
	device_t dev;
	u_char slave;
	int timeout;
} DEFAULT iicbus_nosupport;

#
# Send START condition
#
METHOD int start {
	device_t dev;
	u_char slave;
	int timeout;
} DEFAULT iicbus_nosupport;

#
# Send STOP condition
#
METHOD int stop {
	device_t dev;
} DEFAULT iicbus_nosupport;

#
# Read from I2C bus
#
METHOD int read {
	device_t dev;
	char *buf;
	int len;
	int *bytes;
	int last;
	int delay;
} DEFAULT iicbus_nosupport;

#
# Write to the I2C bus
#
METHOD int write {
	device_t dev;
	const char *buf;
	int len;
	int *bytes;
	int timeout;
} DEFAULT iicbus_nosupport;

#
# Reset I2C bus
#
METHOD int reset {
	device_t dev;
	u_char speed;
	u_char addr;
	u_char *oldaddr;
};

#
# Generalized Read/Write interface
#
METHOD int transfer {
	device_t dev;
	struct iic_msg *msgs;
	uint32_t nmsgs;
};

#
# Return the frequency in Hz for the bus running at the given 
# symbolic speed.  Only the IIC_SLOW speed has meaning, it is always
# 100KHz.  The UNKNOWN, FAST, and FASTEST rates all map to the
# configured bus frequency, or 100KHz when not otherwise configured.
#
METHOD u_int get_frequency {
	device_t dev;
	u_char speed;
} DEFAULT iicbus_default_frequency;
