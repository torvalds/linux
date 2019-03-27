#-
# Copyright (c) 2015 M. Warner Losh <imp@freebsd.org>
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
#include <dev/ow/ow.h>

INTERFACE own;

#
# Dallas Semiconductor 1-Wire bus network and transport layer (own)
#
# See Maxim Application Note AN937: Book of iButton Standards for the
# 1-Wire protocol specification.
# http://pdfserv.maximintegrated.com/en/an/AN937.pdf
#
# Note: 1-Wire is a registered trademark of Maxim Integrated Products, Inc.
#

#
# Send a command up the stack.
#
METHOD int send_command {
	device_t	ndev;		/* Network (bus) level device */
	device_t	pdev;		/* Device to send command for */
	struct ow_cmd   *cmd;		/* Pointer to filled in command */
};

#
# Grab exclusive use of the bus (advisory)
#
METHOD int acquire_bus {
	device_t	ndev;
	device_t	pdev;
	int		how;
};

#
# Release exclusive use of the bus (advisory)
#
METHOD void release_bus {
	device_t	ndev;
	device_t	pdev;
};

#
# Compute a CRC for a given range of bytes
#
METHOD uint8_t crc {
	device_t	ndev;
	device_t	pdev;
	uint8_t		*buffer;
	size_t		len;
};
