#-
# Copyright (c) 2000 Doug Rabson
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

#
# The AGP interface is used internally to the agp driver to isolate the
# differences between various AGP chipsets into chipset mini drivers. It
# should not be used outside the AGP driver. The kernel api for accessing
# AGP functionality is described in <dev/agp/agpvar.h>
#
INTERFACE agp;

CODE {
	static int
	null_agp_chipset_flush(device_t dev)
	{
		return (ENXIO);
	}
};

#
# Return the current aperture size.
#
METHOD u_int32_t get_aperture {
	device_t	dev;
};

#
# Set the size of the aperture. Return EINVAL on error or 0 on success.
#
METHOD int set_aperture {
	device_t	dev;
	u_int32_t	aperture;
};

#
# Bind a single page in the AGP aperture to a given physical address.
# The offset is a byte offset within the aperture which must be
# aligned to an AGP page boundary.
#
METHOD int bind_page {
	device_t	dev;
	vm_offset_t	offset;
	vm_offset_t	physical;
};

#
# Unbind a single page in the AGP aperture.
#
METHOD int unbind_page {
	device_t	dev;
	vm_offset_t	offset;
};

#
# Flush the GATT TLB. This is used after a call to bind_page to
# ensure that any mappings cached in the chipset are discarded.
#
METHOD void flush_tlb {
	device_t	dev;
};

#
# Enable the agp hardware with the relavent mode. The mode bits are
# defined in <dev/agp/agpreg.h>
#
METHOD int enable {
	device_t	dev;
	u_int32_t	mode;
};

#
# Allocate memory of a given type. The type is a chipset-specific
# code which is used by certain integrated agp graphics chips
# (basically just the i810 for now) to access special features of
# the chipset. An opaque handle representing the memory region is
# returned and can be used as an argument to free_memory, bind_memory 
# and unbind_memory.
#
# The size is specified in bytes but must be a multiple of the AGP
# page size.
#
METHOD struct agp_memory * alloc_memory {
	device_t	dev;
	int		type;
	vm_size_t	size;
};

#
# Free a memory region previously allocated with alloc_memory. Return
# EBUSY if the memory is bound.
#
METHOD int free_memory {
	device_t	dev;
	struct agp_memory *mem;
};

#
# Bind a memory region to a specific byte offset within the chipset's
# AGP aperture. This effectively defines a range of contiguous
# physical address which alias the (possibly uncontiguous) pages in
# the memory region.
#
METHOD int bind_memory {
	device_t	dev;
	struct agp_memory *mem;
	vm_offset_t	offset;
};

#
# Unbind a memory region bound with bind_memory.
#
METHOD int unbind_memory {
	device_t	dev;
	struct agp_memory *handle;
};

METHOD int chipset_flush {
	device_t	dev;
} DEFAULT null_agp_chipset_flush;
