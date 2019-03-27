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

INTERFACE smbus;

#
# Interpret interrupt
#
METHOD void intr {
	device_t dev;
	u_char devaddr;
	char low;
	char high;
	int error;
};

#
# smbus callback
#
METHOD int callback {
	device_t dev;
	int index;
	void *data;
};

#
# Quick command
#
METHOD int quick {
	device_t dev;
	u_char slave;
	int how;
};

#
# Send Byte command
#
METHOD int sendb {
	device_t dev;
	u_char slave;
	char byte;
};

#
# Receive Byte command
#
METHOD int recvb {
	device_t dev;
	u_char slave;
	char *byte;
};

#
# Write Byte command
#
METHOD int writeb {
	device_t dev;
	u_char slave;
	char cmd;
	char byte;
};

#
# Write Word command
#
METHOD int writew {
	device_t dev;
	u_char slave;
	char cmd;
	short word;
};

#
# Read Byte command
#
METHOD int readb {
	device_t dev;
	u_char slave;
	char cmd;
	char *byte;
};

#
# Read Word command
#
METHOD int readw {
	device_t dev;
	u_char slave;
	char cmd;
	short *word;
};

#
# Process Call command
#
METHOD int pcall {
	device_t dev;
	u_char slave;
	char cmd;
	short sdata;
	short *rdata;
};

#
# Block Write command
#
METHOD int bwrite {
	device_t dev;
	u_char slave;
	char cmd;
	u_char count;
	char *buf;
};

#
# Block Read command
#
METHOD int bread {
	device_t dev;
	u_char slave;
	char cmd;
	u_char *count;
	char *buf;
};
