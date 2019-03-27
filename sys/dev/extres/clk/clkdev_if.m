#-
# Copyright 2016 Michal Meloun <mmel@FreeBSD.org>
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

#include <machine/bus.h>

INTERFACE clkdev;

CODE {
	#include <sys/systm.h>
	#include <sys/bus.h>
	static int
	clkdev_default_write_4(device_t dev, bus_addr_t addr, uint32_t val)
	{
		device_t pdev;

		pdev = device_get_parent(dev);
		if (pdev == NULL)
			return (ENXIO);

		return (CLKDEV_WRITE_4(pdev, addr, val));
	}

	static int
	clkdev_default_read_4(device_t dev, bus_addr_t addr, uint32_t *val)
	{
		device_t pdev;

		pdev = device_get_parent(dev);
		if (pdev == NULL)
			return (ENXIO);

		return (CLKDEV_READ_4(pdev, addr, val));
	}

	static int
	clkdev_default_modify_4(device_t dev, bus_addr_t addr,
	    uint32_t clear_mask, uint32_t set_mask)
	{
		device_t pdev;

		pdev = device_get_parent(dev);
		if (pdev == NULL)
			return (ENXIO);

		return (CLKDEV_MODIFY_4(pdev, addr, clear_mask, set_mask));
	}

	static void
	clkdev_default_device_lock(device_t dev)
	{
		device_t pdev;

		pdev = device_get_parent(dev);
		if (pdev == NULL)
			panic("clkdev_device_lock not implemented");

		CLKDEV_DEVICE_LOCK(pdev);
	}

	static void
	clkdev_default_device_unlock(device_t dev)
	{
		device_t pdev;

		pdev = device_get_parent(dev);
		if (pdev == NULL)
			panic("clkdev_device_unlock not implemented");

		CLKDEV_DEVICE_UNLOCK(pdev);
	}
}

#
# Write single register
#
METHOD int write_4 {
	device_t	dev;
	bus_addr_t	addr;
	uint32_t	val;
} DEFAULT clkdev_default_write_4;

#
# Read single register
#
METHOD int read_4 {
	device_t	dev;
	bus_addr_t	addr;
	uint32_t	*val;
} DEFAULT clkdev_default_read_4;

#
# Modify single register
#
METHOD int modify_4 {
	device_t	dev;
	bus_addr_t	addr;
	uint32_t	clear_mask;
	uint32_t	set_mask;
} DEFAULT clkdev_default_modify_4;

#
# Get exclusive access to underlying device
#
METHOD void device_lock {
	device_t	dev;
} DEFAULT clkdev_default_device_lock;

#
# Release exclusive access to underlying device
#
METHOD void device_unlock {
	device_t	dev;
} DEFAULT clkdev_default_device_unlock;
