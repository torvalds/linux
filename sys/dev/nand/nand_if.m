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

# NAND chip interface description
#

#include <sys/bus.h>
#include <dev/nand/nand.h>

INTERFACE nand;

CODE {
	static int nand_method_not_supported(device_t dev)
	{
		return (ENOENT);
	}
};

# Read NAND page
#
# Return values:
# 0: Success
#
METHOD int read_page {
	device_t dev;
	uint32_t page;
	void* buf;
	uint32_t len;
	uint32_t offset;
};

# Program NAND page
#
# Return values:
# 0: Success
#
METHOD int program_page {
	device_t dev;
	uint32_t page;
	void* buf;
	uint32_t len;
	uint32_t offset;
};

# Program NAND page interleaved
#
# Return values:
# 0: Success
#
METHOD int program_page_intlv {
	device_t dev;
	uint32_t page;
	void* buf;
	uint32_t len;
	uint32_t offset;
} DEFAULT nand_method_not_supported;

# Read NAND oob
#
# Return values:
# 0: Success
#
METHOD int read_oob {
	device_t dev;
	uint32_t page;
	void* buf;
	uint32_t len;
	uint32_t offset;
};

# Program NAND oob
#
# Return values:
# 0: Success
#
METHOD int program_oob {
	device_t dev;
	uint32_t page;
	void* buf;
	uint32_t len;
	uint32_t offset;
};

# Erase NAND block
#
# Return values:
# 0: Success
#
METHOD int erase_block {
	device_t dev;
	uint32_t block;
};

# Erase NAND block interleaved
#
# Return values:
# 0: Success
#
METHOD int erase_block_intlv {
	device_t dev;
	uint32_t block;
} DEFAULT nand_method_not_supported;

# NAND get status
#
# Return values:
# 0: Success
#
METHOD int get_status {
	device_t dev;
	uint8_t *status;
};

# NAND check if block is bad
#
# Return values:
# 0: Success
#
METHOD int is_blk_bad {
	device_t dev;
	uint32_t block_number;
	uint8_t  *bad;
};

# NAND get ECC
#
#
METHOD int get_ecc {
	device_t dev;
	void *buf;
	void *ecc;
	int *needwrite;
};

# NAND correct ECC
#
#
METHOD int correct_ecc {
	device_t dev;
	void *buf;
	void *readecc;
	void *calcecc;
};

