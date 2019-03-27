#-
# Copyright (c) 1999 M. Warner Losh.
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
#include <machine/bus.h>
#include <dev/pccard/pccardvar.h>

INTERFACE card;

#
# Companion interface for pccard.  We need to set attributes for memory
# and i/o port mappings (as well as other types of attributes) that have
# a well defined meaning inside the PC Card/CardBus system.  The bus
# methods are inadequate for this because this must be done at the time the
# resources are set for the device, which predates their activation.  Also,
# the driver activating the resources doesn't necessarily know or need to know
# these attributes.
#
METHOD int set_res_flags {
	device_t dev;
	device_t child;
	int	 restype;
	int	 rid;
	u_long	 value;
};

METHOD int get_res_flags {
	device_t dev;
	device_t child;
	int	 restype;
	int	 rid;
	u_long	 *value;
};

#
# Sets the memory offset of the pccard bridge's window into attribute
# or common memory space.
#
METHOD int set_memory_offset {
	device_t  dev;
	device_t  child;
	int	  rid;
	uint32_t cardaddr;
	uint32_t *deltap;
}

METHOD int get_memory_offset {
	device_t  dev;
	device_t  child;
	int	  rid;
	uint32_t *offset;
}

#
# pccard bridges call this method to initate the attachment of a card
#
METHOD int attach_card {
	device_t  dev;
}

#
# pccard bridges call this to detach a card.
#
METHOD int detach_card {
	device_t  dev;
}

#
# Find "dev" in the passed table of devices.  Return it or NULL.
#
METHOD const struct pccard_product * do_product_lookup {
	device_t bus;
	device_t dev;
	const struct pccard_product *tab;
	size_t ent_size;
	pccard_product_match_fn matchfn;
}

#
# Scanning function for accessing the CIS of a card in its driver.
#
METHOD int cis_scan {
	device_t bus;
	device_t dev;
        pccard_scan_t fnp;
	void *argp;
};

#
# Convenience function to read attribute memory.
#
METHOD int attr_read {
	device_t bus;
	device_t dev;
	uint32_t offset;
	uint8_t *val;
}

#
# Convenience function to write attribute memory.
#
METHOD int attr_write {
	device_t bus;
	device_t dev;
	uint32_t offset;
	uint8_t val;
}

#
# Read the CCR register
#
METHOD int ccr_read {
	device_t bus;
	device_t dev;
	uint32_t offset;
	uint8_t *val;
}

#
# Write the CCR register
#
METHOD int ccr_write {
	device_t bus;
	device_t dev;
	uint32_t offset;
	uint8_t val;
}
