/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Nathan Whitehorn
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>

#include <powerpc/powermac/macgpiovar.h>

static int	vcoregpio_probe(device_t);
static int	vcoregpio_attach(device_t);
static void	vcoregpio_pre_change(device_t, const struct cf_level *level);
static void	vcoregpio_post_change(device_t, const struct cf_level *level);

static device_method_t  vcoregpio_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		vcoregpio_probe),
	DEVMETHOD(device_attach,	vcoregpio_attach),
	{ 0, 0 },
};

static driver_t vcoregpio_driver = {
	"vcoregpio",
	vcoregpio_methods,
	0
};

static devclass_t vcoregpio_devclass;

DRIVER_MODULE(vcoregpio, macgpio, vcoregpio_driver, vcoregpio_devclass, 0, 0);

static int
vcoregpio_probe(device_t dev)
{
	const char *name = ofw_bus_get_name(dev);

	if (strcmp(name, "cpu-vcore-select") != 0)
		return (ENXIO);

	device_set_desc(dev, "CPU Core Voltage Control");
	return (0);
}

static int
vcoregpio_attach(device_t dev)
{
	EVENTHANDLER_REGISTER(cpufreq_pre_change, vcoregpio_pre_change, dev,
	    EVENTHANDLER_PRI_ANY);
	EVENTHANDLER_REGISTER(cpufreq_post_change, vcoregpio_post_change, dev,
	    EVENTHANDLER_PRI_ANY);

	return (0);
}

static void
vcoregpio_pre_change(device_t dev, const struct cf_level *level)
{
	if (level->rel_set[0].freq == 10000 /* max */) {
		/*
		 * Make sure the CPU voltage is raised before we raise
		 * the clock.
		 */
		macgpio_write(dev, GPIO_DDR_OUTPUT | 1);
		DELAY(1000);
	}
}

static void
vcoregpio_post_change(device_t dev, const struct cf_level *level)
{
	if (level->rel_set[0].freq < 10000 /* max */) {
		DELAY(1000);
		/* We are safe to reduce CPU voltage now */
		macgpio_write(dev, GPIO_DDR_OUTPUT | 0);
	}
}

