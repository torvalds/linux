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

# NAND controller interface description
#

#include <sys/bus.h>
#include <dev/nand/nand.h>

INTERFACE nfc;

CODE {
	static int nfc_default_method(device_t dev)
	{
		return (0);
	}

	static int nfc_softecc_get(device_t dev, void *buf, int pagesize, 
	    void *ecc, int *needwrite)
	{
		*needwrite = 1;
		return (nand_softecc_get(dev, buf, pagesize, ecc));
	}

	static int nfc_softecc_correct(device_t dev, void *buf, int pagesize,
	    void *readecc, void *calcecc)
	{
		return (nand_softecc_correct(dev, buf, pagesize, readecc,
		    calcecc));
	}
};

# Send command to a NAND chip
#
# Return values:
# 0: Success
#
METHOD int send_command {
	device_t dev;
	uint8_t command;
};

# Send address to a NAND chip
#
# Return values:
# 0: Success
#
METHOD int send_address {
	device_t dev;
	uint8_t address;
};

# Read byte
#
# Return values:
# byte read
#
METHOD uint8_t read_byte {
	device_t dev;
};

# Write byte
#
METHOD void write_byte {
	device_t dev;
	uint8_t byte;
};

# Read word
#
# Return values:
# word read
#
METHOD uint16_t read_word {
	device_t dev;
};

# Write word
#
METHOD void write_word {
	device_t dev;
	uint16_t word;
};

# Read buf
#
METHOD void read_buf {
	device_t dev;
	void *buf;
	uint32_t len;
};

# Write buf
#
METHOD void write_buf {
	device_t dev;
	void *buf;
	uint32_t len;
};

# Select CS
#
METHOD int select_cs {
	device_t dev;
	uint8_t cs;
};

# Read ready/busy signal
#
METHOD int read_rnb {
	device_t dev;
};

# Start command
#
# Return values:
# 0: Success
#
METHOD int start_command {
	device_t dev;
} DEFAULT nfc_default_method;

# Generate ECC or get it from H/W
#
METHOD int get_ecc {
	device_t dev;
	void *buf;
	int pagesize;
	void *ecc;
	int *needwrite;
} DEFAULT nfc_softecc_get;

# Correct ECC
#
METHOD int correct_ecc {
	device_t dev;
	void *buf;
	int pagesize;
	void *readecc;
	void *calcecc;
} DEFAULT nfc_softecc_correct;
