#-
# Copyright (c) 1998 Doug Rabson
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
#include <dev/pci/pcivar.h>

INTERFACE pci;

CODE {
	static int
	null_msi_count(device_t dev, device_t child)
	{
		return (0);
	}

	static int
	null_msix_bar(device_t dev, device_t child)
	{
		return (-1);
	}

	static device_t
	null_create_iov_child(device_t bus, device_t pf, uint16_t rid,
	    uint16_t vid, uint16_t did)
	{
		device_printf(bus, "PCI_IOV not implemented on this bus.\n");
		return (NULL);
	}
};

HEADER {
	struct nvlist;

	enum pci_id_type {
	    PCI_ID_RID,
	    PCI_ID_MSI,
	};

	enum pci_feature {
	    PCI_FEATURE_HP,		/* Hot Plug feature */
	    PCI_FEATURE_AER,		/* Advanced Error Reporting */
	};
}


METHOD u_int32_t read_config {
	device_t	dev;
	device_t	child;
	int		reg;
	int		width;
};

METHOD void write_config {
	device_t	dev;
	device_t	child;
	int		reg;
	u_int32_t	val;
	int		width;
};

METHOD int get_powerstate {
	device_t	dev;
	device_t	child;
};

METHOD int set_powerstate {
	device_t	dev;
	device_t	child;
	int		state;
};

METHOD int get_vpd_ident {
	device_t	dev;
	device_t	child;
	const char	**identptr;
};

METHOD int get_vpd_readonly {
	device_t	dev;
	device_t	child;
	const char	*kw;
	const char	**vptr;
};

METHOD int enable_busmaster {
	device_t	dev;
	device_t	child;
};

METHOD int disable_busmaster {
	device_t	dev;
	device_t	child;
};

METHOD int enable_io {
	device_t	dev;
	device_t	child;
	int		space;
};

METHOD int disable_io {
	device_t	dev;
	device_t	child;
	int		space;
};

METHOD int assign_interrupt {
	device_t	dev;
	device_t	child;
};

METHOD int find_cap {
	device_t	dev;
	device_t	child;
	int		capability;
	int		*capreg;
};

METHOD int find_next_cap {
	device_t	dev;
	device_t	child;
	int		capability;
	int		start;
	int		*capreg;
};

METHOD int find_extcap {
	device_t	dev;
	device_t	child;
	int		capability;
	int		*capreg;
};

METHOD int find_next_extcap {
	device_t	dev;
	device_t	child;
	int		capability;
	int		start;
	int		*capreg;
};

METHOD int find_htcap {
	device_t	dev;
	device_t	child;
	int		capability;
	int		*capreg;
};

METHOD int find_next_htcap {
	device_t	dev;
	device_t	child;
	int		capability;
	int		start;
	int		*capreg;
};

METHOD int alloc_msi {
	device_t	dev;
	device_t	child;
	int		*count;
};

METHOD int alloc_msix {
	device_t	dev;
	device_t	child;
	int		*count;
};

METHOD void enable_msi {
	device_t	dev;
	device_t	child;
	uint64_t	address;
	uint16_t	data;
};

METHOD void enable_msix {
	device_t	dev;
	device_t	child;
	u_int		index;
	uint64_t	address;
	uint32_t	data;
};

METHOD void disable_msi {
	device_t	dev;
	device_t	child;
};

METHOD int remap_msix {
	device_t	dev;
	device_t	child;
	int		count;
	const u_int	*vectors;
};

METHOD int release_msi {
	device_t	dev;
	device_t	child;
};

METHOD int msi_count {
	device_t	dev;
	device_t	child;
} DEFAULT null_msi_count;

METHOD int msix_count {
	device_t	dev;
	device_t	child;
} DEFAULT null_msi_count;

METHOD int msix_pba_bar {
	device_t	dev;
	device_t	child;
} DEFAULT null_msix_bar;

METHOD int msix_table_bar {
	device_t	dev;
	device_t	child;
} DEFAULT null_msix_bar;

METHOD int get_id {
	device_t	dev;
	device_t	child;
	enum pci_id_type type;
	uintptr_t	*id;
};

METHOD struct pci_devinfo * alloc_devinfo {
	device_t	dev;
};

METHOD void child_added {
	device_t	dev;
	device_t	child;
};

METHOD int iov_attach {
	device_t	dev;
	device_t	child;
	struct nvlist	*pf_schema;
	struct nvlist	*vf_schema;
	const char	*name;
};

METHOD int iov_detach {
	device_t	dev;
	device_t	child;
};

METHOD device_t create_iov_child {
	device_t bus;
	device_t pf;
	uint16_t rid;
	uint16_t vid;
	uint16_t did;
} DEFAULT null_create_iov_child;
