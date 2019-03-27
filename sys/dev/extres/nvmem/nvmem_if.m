#-
# Copyright (c) 2018 Emmanuel Vadot <manu@FreeBSD.org>
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

INTERFACE nvmem;

#
# Default implementations of some methods.
#
CODE {
	static int
	null_nvmem_read(device_t dev __unused, uint32_t offset __unused, uint32_t size __unused, uint8_t *buffer __unused)
	{

		return (ENXIO);
	}

	static int
	null_nvmem_write(device_t dev __unused, uint32_t offset __unused, uint32_t size __unused, uint8_t *buffer __unused)
	{

		return (ENXIO);
	}
};

#
# Read
#
METHOD int read {
	device_t dev;
	uint32_t offset;
	uint32_t size;
	uint8_t *buffer;
} DEFAULT null_nvmem_read;

#
# Write
#
METHOD int write {
	device_t dev;
	uint32_t offset;
	uint32_t size;
	uint8_t *buffer;
} DEFAULT null_nvmem_write;
