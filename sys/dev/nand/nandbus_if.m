#-
# Copyright (C) 2009-2012 Semihalf
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

# NAND bus interface description
#

#include <sys/bus.h>
#include <dev/nand/nand.h>

INTERFACE nandbus;

METHOD int get_status {
	device_t	dev;
	uint8_t	*	status;
};

METHOD void read_buffer {
	device_t	dev;
	void *		buf;
	uint32_t	len;
};

METHOD int select_cs {
	device_t	dev;
	uint8_t		cs;
};

METHOD int send_command {
	device_t	dev;
	uint8_t		command;
};

METHOD int send_address {
	device_t	dev;
	uint8_t		address;
};

METHOD int start_command {
	device_t	dev;
};

METHOD int wait_ready {
	device_t 	dev;
	uint8_t *	status;	
}

METHOD void write_buffer {
	device_t	dev;
	void *		buf;
	uint32_t	len;
};

METHOD int get_ecc {
	device_t	dev;
	void *		buf;
	uint32_t	pagesize;
	void *		ecc;
	int *		needwrite;
};

METHOD int correct_ecc {
	device_t	dev;
	void *		buf;
	int		pagesize;
	void *		readecc;
	void *		calcecc;
};

METHOD void lock {
	device_t	dev;
};

METHOD void unlock {
	device_t	dev;
};
	
