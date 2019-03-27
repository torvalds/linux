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
#include <sys/rman.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcib_private.h>

INTERFACE pcib;

CODE {
	static int
	null_route_interrupt(device_t pcib, device_t dev, int pin)
	{
		return (PCI_INVALID_IRQ);
	}

	static int
	pcib_null_ari_enabled(device_t pcib)
	{

		return (0);
	}
};

HEADER {
	#include "pci_if.h"
};

#
# Return the number of slots on the attached PCI bus.
#
METHOD int maxslots {
	device_t	dev;
};

#
#
# Return the number of functions on the attached PCI bus.
#
METHOD int maxfuncs {
	device_t	dev;
} DEFAULT pcib_maxfuncs;

#
# Read configuration space on the PCI bus. The bus, slot and func
# arguments determine the device which is being read and the reg
# argument is a byte offset into configuration space for that
# device. The width argument (which should be 1, 2 or 4) specifies how
# many byte of configuration space to read from that offset.
#
METHOD u_int32_t read_config {
	device_t	dev;
	u_int		bus;
	u_int		slot;
	u_int		func;
	u_int		reg;
	int		width;
};

#
# Write configuration space on the PCI bus. The bus, slot and func
# arguments determine the device which is being written and the reg
# argument is a byte offset into configuration space for that
# device. The value field is written to the configuration space, with
# the number of bytes written depending on the width argument.
#
METHOD void write_config {
	device_t	dev;
	u_int		bus;
	u_int		slot;
	u_int		func;
	u_int		reg;
	u_int32_t	value;
	int		width;
};

#
# Route an interrupt.  Returns a value suitable for stuffing into
# a device's interrupt register.
#
METHOD int route_interrupt {
	device_t	pcib;
	device_t	dev;
	int		pin;
} DEFAULT null_route_interrupt;

#
# Allocate 'count' MSI messsages mapped onto 'count' IRQs.  'irq' points
# to an array of at least 'count' ints.  The max number of messages this
# device supports is included so that the MD code can take that into
# account when assigning resources so that the proper number of low bits
# are clear in the resulting message data value.
#
METHOD int alloc_msi {
	device_t	pcib;
	device_t	dev;
	int		count;
	int		maxcount;
	int		*irqs;
};

#
# Release 'count' MSI messages mapped onto 'count' IRQs stored in the
# array pointed to by 'irqs'.
#
METHOD int release_msi {
	device_t	pcib;
	device_t	dev;
	int		count;
	int		*irqs;
};

#
# Allocate a single MSI-X message mapped onto '*irq'.
#
METHOD int alloc_msix {
	device_t	pcib;
	device_t	dev;
	int		*irq;
};

#
# Release a single MSI-X message mapped onto 'irq'.
#
METHOD int release_msix {
	device_t	pcib;
	device_t	dev;
	int		irq;
};

#
# Determine the MSI/MSI-X message address and data for 'irq'.  The address
# is returned in '*addr', and the data in '*data'.
#
METHOD int map_msi {
	device_t	pcib;
	device_t	dev;
	int		irq;
	uint64_t	*addr;
	uint32_t	*data;
};

#
# Return the device power state to be used during a system sleep state
# transition such as suspend and resume.
#
METHOD int power_for_sleep {
	device_t	pcib;
	device_t	dev;
	int		*pstate;
};

#
# Return the PCI Routing Identifier (RID) for the device.
#
METHOD int get_id {
	device_t	pcib;
	device_t	dev;
	enum pci_id_type type;
	uintptr_t	*id;
} DEFAULT pcib_get_id;

#
# Enable Alternative RID Interpretation if both the downstream port (pcib)
# and the endpoint device (dev) both support it.
#
METHOD int try_enable_ari {
	device_t	pcib;
	device_t	dev;
};

#
# Return non-zero if PCI ARI is enabled, or zero otherwise
#
METHOD int ari_enabled {
	device_t	pcib;
} DEFAULT pcib_null_ari_enabled;

#
# Decode a PCI Routing Identifier (RID) into PCI bus/slot/function
#
METHOD void decode_rid {
	device_t	pcib;
	uint16_t	rid;
	int 		*bus;
	int 		*slot;
	int 		*func;
} DEFAULT pcib_decode_rid;

#
# Request control of PCI features from host firmware, if any.
#
METHOD int request_feature {
	device_t	pcib;
	device_t	dev;
	enum pci_feature feature;
};
