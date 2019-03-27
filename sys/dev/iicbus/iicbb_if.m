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

INTERFACE iicbb;

#
# Default implementation of optional methods
#
CODE {
	static int
	null_pre_xfer(device_t dev)
	{
		return 0;
	}

	static void
	null_post_xfer(device_t dev)
	{
	}

	static int
	null_callback(device_t dev, int index, caddr_t data)
	{
		return 0;
	}
};

#
# iicbus callback
#
METHOD int callback {
	device_t dev;
	int index;
	caddr_t data;
} DEFAULT null_callback;

#
# Prepare device for I2C transfer
#
METHOD int pre_xfer {
	device_t dev;
} DEFAULT null_pre_xfer;

#
# Cleanup device after I2C transfer
#
METHOD void post_xfer {
	device_t dev;
} DEFAULT null_post_xfer;

#
# Set I2C bus data line
#
METHOD void setsda {
	device_t dev;
	int val;
};

#
# Set I2C bus clock line
#
METHOD void setscl {
	device_t dev;
	int val;
};

#
# Get I2C bus data line
#
#
METHOD int getsda {
	device_t dev;
};

#
# Get I2C bus clock line
#
#
METHOD int getscl {
	device_t dev;
};

#
# Reset interface
#
METHOD int reset {
	device_t dev;
	u_char speed;
	u_char addr;
	u_char *oldaddr;
};
