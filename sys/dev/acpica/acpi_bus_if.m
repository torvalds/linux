#-
# Copyright (c) 2016 The FreeBSD Foundation
# All rights reserved.
#
# This software was developed by Andrew Turner under
# sponsorship from the FreeBSD Foundation.
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
# ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/bus.h>

INTERFACE acpi_bus;

CODE {
	static acpi_bus_map_intr_t acpi_bus_default_map_intr;

	int
	acpi_bus_default_map_intr(device_t bus, device_t dev, u_int irq,
	    int trig, int pol)
	{
		device_t parent;

		/* Pass up the hierarchy */
		parent = device_get_parent(bus);
		if (parent != NULL)
			return (ACPI_BUS_MAP_INTR(parent, dev, irq, trig, pol));

		panic("Unable to map interrupt %u", irq);
	}
};

# Map an interrupt from ACPI space to the FreeBSD IRQ space. Note that
# both of these may be different than the pysical interrupt space as this
# may be local to each interrupt controller.
#
# This method also associates interrupt metadata with the interrupt,
# removing the need for a post hoc configure step.
METHOD int map_intr {
	device_t bus;
	device_t dev;
	u_int irq;
	int trig;
	int pol;
} DEFAULT acpi_bus_default_map_intr;
