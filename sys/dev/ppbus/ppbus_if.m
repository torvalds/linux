#-
# Copyright (c) 1999 Nicolas Souchu
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
#include <dev/ppbus/ppbconf.h>

INTERFACE ppbus;

#
# Do low level i/o operations
#
METHOD u_char io {
	device_t dev;
	int opcode;
	u_char *addr;
	int cnt;
	u_char byte;
};

#
# Execution of a microsequence
#
METHOD int exec_microseq {
	device_t dev;
	struct ppb_microseq **ppb_microseq;
};

#
# Reset EPP timeout
#
METHOD int reset_epp {
	device_t dev;
}

#
# Set chipset mode
#
METHOD int setmode {
	device_t dev;
	int mode;
}

#
# Synchronize ECP FIFO
#
METHOD int ecp_sync {
	device_t dev;
}

#
# Do chipset dependent low level read
#
METHOD int read {
	device_t dev;
	char *buf;
	int len;
	int how;
}

#
# Do chipset dependent low level write
#
METHOD int write {
	device_t dev;
	char *buf;
	int len;
	int how;
}
