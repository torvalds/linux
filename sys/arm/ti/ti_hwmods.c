/*-
 * Copyright (c) 2015 Oleksandr Tymoshenko <gonzo@freebsd.org>
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <arm/ti/ti_prcm.h>
#include <arm/ti/ti_hwmods.h>

struct hwmod {
	const char	*name;
	int		clock_id;
};

struct hwmod ti_hwmods[] = {
	{"i2c1",	I2C1_CLK},
	{"i2c2",	I2C2_CLK},
	{"i2c3",	I2C3_CLK},
	{"i2c4",	I2C4_CLK},
	{"i2c5",	I2C5_CLK},

	{"gpio1",	GPIO1_CLK},
	{"gpio2",	GPIO2_CLK},
	{"gpio3",	GPIO3_CLK},
	{"gpio4",	GPIO4_CLK},
	{"gpio5",	GPIO5_CLK},
	{"gpio6",	GPIO6_CLK},
	{"gpio7",	GPIO7_CLK},

	{"mmc1",	MMC1_CLK},
	{"mmc2",	MMC2_CLK},
	{"mmc3",	MMC3_CLK},
	{"mmc4",	MMC4_CLK},
	{"mmc5",	MMC5_CLK},
	{"mmc6",	MMC6_CLK},

	{"epwmss0",	PWMSS0_CLK},
	{"epwmss1",	PWMSS1_CLK},
	{"epwmss2",	PWMSS2_CLK},

	{"spi0",	SPI0_CLK},
	{"spi1",	SPI1_CLK},

	{"timer1",	TIMER1_CLK},
	{"timer2",	TIMER2_CLK},
	{"timer3",	TIMER3_CLK},
	{"timer4",	TIMER4_CLK},
	{"timer5",	TIMER5_CLK},
	{"timer6",	TIMER6_CLK},
	{"timer7",	TIMER7_CLK},

	{"uart1",	UART1_CLK},
	{"uart2",	UART2_CLK},
	{"uart3",	UART3_CLK},
	{"uart4",	UART4_CLK},
	{"uart5",	UART5_CLK},
	{"uart6",	UART6_CLK},
	{"uart7",	UART7_CLK},

	{NULL,		0}
};

clk_ident_t
ti_hwmods_get_clock(device_t dev)
{
	phandle_t node;
	int len, l;
	char *name;
	char *buf;
	int clk;
	struct hwmod *hw;

	if ((node = ofw_bus_get_node(dev)) == 0)
		return (INVALID_CLK_IDENT);

	if ((len = OF_getprop_alloc(node, "ti,hwmods", (void**)&name)) <= 0)
		return (INVALID_CLK_IDENT);

	buf = name;

	clk = INVALID_CLK_IDENT;
	while ((len > 0) && (clk == INVALID_CLK_IDENT)) {
		for (hw = ti_hwmods; hw->name != NULL; ++hw) {
			if (strcmp(hw->name, name) == 0) {
				clk = hw->clock_id;
				break;
			}
		}

		/* Slide to the next sub-string. */
		l = strlen(name) + 1;
		name += l;
		len -= l;
	}

	if (len > 0)
		device_printf(dev, "WARNING: more than one ti,hwmod \n");

	OF_prop_free(buf);
	return (clk);
}

int ti_hwmods_contains(device_t dev, const char *hwmod)
{
	phandle_t node;
	int len, l;
	char *name;
	char *buf;
	int result;

	if ((node = ofw_bus_get_node(dev)) == 0)
		return (0);

	if ((len = OF_getprop_alloc(node, "ti,hwmods", (void**)&name)) <= 0)
		return (0);

	buf = name;

	result = 0;
	while (len > 0) {
		if (strcmp(name, hwmod) == 0) {
			result = 1;
			break;
		}

		/* Slide to the next sub-string. */
		l = strlen(name) + 1;
		name += l;
		len -= l;
	}

	OF_prop_free(buf);

	return (result);
}

int 
ti_hwmods_get_unit(device_t dev, const char *hwmod)
{
	phandle_t node;
	int l, len, hwmodlen, result;
	char *name;
	char *buf;

	if ((node = ofw_bus_get_node(dev)) == 0)
		return (0);

	if ((len = OF_getprop_alloc(node, "ti,hwmods", (void**)&name)) <= 0)
		return (0);

	buf = name;
	hwmodlen = strlen(hwmod);
	result = 0;
	while (len > 0) {
		if (strncmp(name, hwmod, hwmodlen) == 0) {
                        result = (int)strtoul(name + hwmodlen, NULL, 10);
			break;
		}
		/* Slide to the next sub-string. */
		l = strlen(name) + 1;
		name += l;
		len -= l;
	}

	OF_prop_free(buf);
	return (result);
}
