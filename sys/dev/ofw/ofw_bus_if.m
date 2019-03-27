#-
# Copyright (c) 2001, 2003 by Thomas Moestl <tmm@FreeBSD.org>
# Copyright (c) 2004, 2005 by Marius Strobl <marius@FreeBSD.org>
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
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
# USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$

# Interface for retrieving the package handle and a subset, namely
# 'compatible', 'device_type', 'model' and 'name', of the standard
# properties of a device on an Open Firmware assisted bus for use
# in device drivers. The rest of the standard properties, 'address',
# 'interrupts', 'reg' and 'status', are not covered by this interface
# as they are expected to be only of interest in the respective bus
# driver.

#include <sys/bus.h>

#include <dev/ofw/openfirm.h>

INTERFACE ofw_bus;

HEADER {
	struct ofw_bus_devinfo {
		phandle_t	obd_node;
		char		*obd_compat;
		char		*obd_model;
		char		*obd_name;
		char		*obd_type;
		char		*obd_status;
	};
};

CODE {
	static ofw_bus_get_devinfo_t ofw_bus_default_get_devinfo;
	static ofw_bus_get_compat_t ofw_bus_default_get_compat;
	static ofw_bus_get_model_t ofw_bus_default_get_model;
	static ofw_bus_get_name_t ofw_bus_default_get_name;
	static ofw_bus_get_node_t ofw_bus_default_get_node;
	static ofw_bus_get_type_t ofw_bus_default_get_type;
	static ofw_bus_map_intr_t ofw_bus_default_map_intr;

	static const struct ofw_bus_devinfo *
	ofw_bus_default_get_devinfo(device_t bus, device_t dev)
	{

		return (NULL);
	}

	static const char *
	ofw_bus_default_get_compat(device_t bus, device_t dev)
	{

		return (NULL);
	}

	static const char *
	ofw_bus_default_get_model(device_t bus, device_t dev)
	{

		return (NULL);
	}

	static const char *
	ofw_bus_default_get_name(device_t bus, device_t dev)
	{

		return (NULL);
	}

	static phandle_t
	ofw_bus_default_get_node(device_t bus, device_t dev)
	{

		return (-1);
	}

	static const char *
	ofw_bus_default_get_type(device_t bus, device_t dev)
	{

		return (NULL);
	}

	int
	ofw_bus_default_map_intr(device_t bus, device_t dev, phandle_t iparent,
	    int icells, pcell_t *interrupt)
	{
		/* Propagate up the bus hierarchy until someone handles it. */	
		if (device_get_parent(bus) != NULL)
			return OFW_BUS_MAP_INTR(device_get_parent(bus), dev,
			    iparent, icells, interrupt);

		/* If that fails, then assume a one-domain system */
		return (interrupt[0]);
	}
};

# Get the ofw_bus_devinfo struct for the device dev on the bus. Used for bus
# drivers which use the generic methods in ofw_bus_subr.c to implement the
# reset of this interface. The default method will return NULL, which means
# there is no such struct associated with the device.
METHOD const struct ofw_bus_devinfo * get_devinfo {
	device_t bus;
	device_t dev;
} DEFAULT ofw_bus_default_get_devinfo;

# Get the alternate firmware name for the device dev on the bus. The default
# method will return NULL, which means the device doesn't have such a property.
METHOD const char * get_compat {
	device_t bus;
	device_t dev;
} DEFAULT ofw_bus_default_get_compat;

# Get the firmware model name for the device dev on the bus. The default method
# will return NULL, which means the device doesn't have such a property.
METHOD const char * get_model {
	device_t bus;
	device_t dev;
} DEFAULT ofw_bus_default_get_model;

# Get the firmware name for the device dev on the bus. The default method will
# return NULL, which means the device doesn't have such a property.
METHOD const char * get_name {
	device_t bus;
	device_t dev;
} DEFAULT ofw_bus_default_get_name;

# Get the firmware node for the device dev on the bus. The default method will
# return -1, which signals that there is no such node.
METHOD phandle_t get_node {
	device_t bus;
	device_t dev;
} DEFAULT ofw_bus_default_get_node;

# Get the firmware device type for the device dev on the bus. The default
# method will return NULL, which means the device doesn't have such a property.
METHOD const char * get_type {
	device_t bus;
	device_t dev;
} DEFAULT ofw_bus_default_get_type;

# Map an (interrupt parent, IRQ) pair to a unique system-wide interrupt number.
# If the interrupt encoding includes a sense field, the interrupt sense will
# also be configured.
METHOD int map_intr {
	device_t bus;
	device_t dev;
	phandle_t iparent;
	int icells;
	pcell_t *interrupt;
} DEFAULT ofw_bus_default_map_intr;
