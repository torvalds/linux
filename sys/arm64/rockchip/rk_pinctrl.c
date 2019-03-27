/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Emmanuel Vadot <manu@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/intr.h>

#include <dev/fdt/simplebus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/fdt/fdt_pinctrl.h>

#include <dev/extres/syscon/syscon.h>

#include "syscon_if.h"

#include "opt_soc.h"

struct rk_pinctrl_pin_drive {
	uint32_t	bank;
	uint32_t	subbank;
	uint32_t	offset;
	uint32_t	value;
	uint32_t	ma;
};

struct rk_pinctrl_bank {
	uint32_t	bank_num;
	uint32_t	subbank_num;
	uint32_t	offset;
	uint32_t	nbits;
};

struct rk_pinctrl_pin_fixup {
	uint32_t	bank;
	uint32_t	subbank;
	uint32_t	pin;
	uint32_t	reg;
	uint32_t	bit;
	uint32_t	mask;
};

struct rk_pinctrl_softc;

struct rk_pinctrl_conf {
	struct rk_pinctrl_bank		*iomux_conf;
	uint32_t			iomux_nbanks;
	struct rk_pinctrl_pin_fixup	*pin_fixup;
	uint32_t			npin_fixup;
	struct rk_pinctrl_pin_drive	*pin_drive;
	uint32_t			npin_drive;
	uint32_t			(*get_pd_offset)(struct rk_pinctrl_softc *, uint32_t);
	struct syscon			*(*get_syscon)(struct rk_pinctrl_softc *, uint32_t);
};

struct rk_pinctrl_softc {
	struct simplebus_softc	simplebus_sc;
	device_t		dev;
	struct syscon		*grf;
	struct syscon		*pmu;
	struct rk_pinctrl_conf	*conf;
};

static struct rk_pinctrl_bank rk3328_iomux_bank[] = {
	{
		.bank_num = 0,
		.subbank_num = 0,
		.offset = 0x00,
		.nbits = 2,
	},
	{
		.bank_num = 0,
		.subbank_num = 1,
		.offset = 0x04,
		.nbits = 2,
	},
	{
		.bank_num = 0,
		.subbank_num = 2,
		.offset = 0x08,
		.nbits = 2,
	},
	{
		.bank_num = 0,
		.subbank_num = 3,
		.offset = 0xc,
		.nbits = 2,
	},
	{
		.bank_num = 1,
		.subbank_num = 0,
		.offset = 0x10,
		.nbits = 2,
	},
	{
		.bank_num = 1,
		.subbank_num = 1,
		.offset = 0x14,
		.nbits = 2,
	},
	{
		.bank_num = 1,
		.subbank_num = 2,
		.offset = 0x18,
		.nbits = 2,
	},
	{
		.bank_num = 1,
		.subbank_num = 3,
		.offset = 0x1C,
		.nbits = 2,
	},
	{
		.bank_num = 2,
		.subbank_num = 0,
		.offset = 0x20,
		.nbits = 2,
	},
	{
		.bank_num = 2,
		.subbank_num = 1,
		.offset = 0x24,
		.nbits = 3,
	},
	{
		.bank_num = 2,
		.subbank_num = 2,
		.offset = 0x2c,
		.nbits = 3,
	},
	{
		.bank_num = 2,
		.subbank_num = 3,
		.offset = 0x34,
		.nbits = 2,
	},
	{
		.bank_num = 3,
		.subbank_num = 0,
		.offset = 0x38,
		.nbits = 3,
	},
	{
		.bank_num = 3,
		.subbank_num = 1,
		.offset = 0x40,
		.nbits = 3,
	},
	{
		.bank_num = 3,
		.subbank_num = 2,
		.offset = 0x48,
		.nbits = 2,
	},
	{
		.bank_num = 3,
		.subbank_num = 3,
		.offset = 0x4c,
		.nbits = 2,
	},
};

static struct rk_pinctrl_pin_fixup rk3328_pin_fixup[] = {
	{
		.bank = 2,
		.pin = 12,
		.reg = 0x24,
		.bit = 8,
		.mask = 0x300,
	},
	{
		.bank = 2,
		.pin = 15,
		.reg = 0x28,
		.bit = 0,
		.mask = 0x7,
	},
	{
		.bank = 2,
		.pin = 23,
		.reg = 0x30,
		.bit = 14,
		.mask = 0x6000,
	},
};

#define	RK_PINDRIVE(_bank, _subbank, _offset, _value, _ma)	\
	{	\
		.bank = _bank,		\
		.subbank = _subbank,	\
		.offset = _offset,	\
		.value = _value,	\
		.ma = _ma,		\
	},

static struct rk_pinctrl_pin_drive rk3328_pin_drive[] = {
	RK_PINDRIVE(0, 0, 0x200, 0, 2)
	RK_PINDRIVE(0, 0, 0x200, 1, 4)
	RK_PINDRIVE(0, 0, 0x200, 2, 8)
	RK_PINDRIVE(0, 0, 0x200, 3, 12)

	RK_PINDRIVE(0, 1, 0x204, 0, 2)
	RK_PINDRIVE(0, 1, 0x204, 1, 4)
	RK_PINDRIVE(0, 1, 0x204, 2, 8)
	RK_PINDRIVE(0, 1, 0x204, 3, 12)

	RK_PINDRIVE(0, 2, 0x208, 0, 2)
	RK_PINDRIVE(0, 2, 0x208, 1, 4)
	RK_PINDRIVE(0, 2, 0x208, 2, 8)
	RK_PINDRIVE(0, 2, 0x208, 3, 12)

	RK_PINDRIVE(0, 3, 0x20C, 0, 2)
	RK_PINDRIVE(0, 3, 0x20C, 1, 4)
	RK_PINDRIVE(0, 3, 0x20C, 2, 8)
	RK_PINDRIVE(0, 3, 0x20C, 3, 12)

	RK_PINDRIVE(1, 0, 0x210, 0, 2)
	RK_PINDRIVE(1, 0, 0x210, 1, 4)
	RK_PINDRIVE(1, 0, 0x210, 2, 8)
	RK_PINDRIVE(1, 0, 0x210, 3, 12)

	RK_PINDRIVE(1, 1, 0x214, 0, 2)
	RK_PINDRIVE(1, 1, 0x214, 1, 4)
	RK_PINDRIVE(1, 1, 0x214, 2, 8)
	RK_PINDRIVE(1, 1, 0x214, 3, 12)

	RK_PINDRIVE(1, 2, 0x218, 0, 2)
	RK_PINDRIVE(1, 2, 0x218, 1, 4)
	RK_PINDRIVE(1, 2, 0x218, 2, 8)
	RK_PINDRIVE(1, 2, 0x218, 3, 12)

	RK_PINDRIVE(1, 3, 0x21C, 0, 2)
	RK_PINDRIVE(1, 3, 0x21C, 1, 4)
	RK_PINDRIVE(1, 3, 0x21C, 2, 8)
	RK_PINDRIVE(1, 3, 0x21C, 3, 12)

	RK_PINDRIVE(2, 0, 0x220, 0, 2)
	RK_PINDRIVE(2, 0, 0x220, 1, 4)
	RK_PINDRIVE(2, 0, 0x220, 2, 8)
	RK_PINDRIVE(2, 0, 0x220, 3, 12)

	RK_PINDRIVE(2, 1, 0x224, 0, 2)
	RK_PINDRIVE(2, 1, 0x224, 1, 4)
	RK_PINDRIVE(2, 1, 0x224, 2, 8)
	RK_PINDRIVE(2, 1, 0x224, 3, 12)

	RK_PINDRIVE(2, 2, 0x228, 0, 2)
	RK_PINDRIVE(2, 2, 0x228, 1, 4)
	RK_PINDRIVE(2, 2, 0x228, 2, 8)
	RK_PINDRIVE(2, 2, 0x228, 3, 12)

	RK_PINDRIVE(2, 3, 0x22C, 0, 2)
	RK_PINDRIVE(2, 3, 0x22C, 1, 4)
	RK_PINDRIVE(2, 3, 0x22C, 2, 8)
	RK_PINDRIVE(2, 3, 0x22C, 3, 12)

	RK_PINDRIVE(3, 0, 0x230, 0, 2)
	RK_PINDRIVE(3, 0, 0x230, 1, 4)
	RK_PINDRIVE(3, 0, 0x230, 2, 8)
	RK_PINDRIVE(3, 0, 0x230, 3, 12)

	RK_PINDRIVE(3, 1, 0x234, 0, 2)
	RK_PINDRIVE(3, 1, 0x234, 1, 4)
	RK_PINDRIVE(3, 1, 0x234, 2, 8)
	RK_PINDRIVE(3, 1, 0x234, 3, 12)

	RK_PINDRIVE(3, 2, 0x238, 0, 2)
	RK_PINDRIVE(3, 2, 0x238, 1, 4)
	RK_PINDRIVE(3, 2, 0x238, 2, 8)
	RK_PINDRIVE(3, 2, 0x238, 3, 12)

	RK_PINDRIVE(3, 3, 0x23C, 0, 2)
	RK_PINDRIVE(3, 3, 0x23C, 1, 4)
	RK_PINDRIVE(3, 3, 0x23C, 2, 8)
	RK_PINDRIVE(3, 3, 0x23C, 3, 12)
};

static uint32_t
rk3328_get_pd_offset(struct rk_pinctrl_softc *sc, uint32_t bank)
{
	return (0x100);
}

static struct syscon *
rk3328_get_syscon(struct rk_pinctrl_softc *sc, uint32_t bank)
{
	return (sc->grf);
}

struct rk_pinctrl_conf rk3328_conf = {
	.iomux_conf = rk3328_iomux_bank,
	.iomux_nbanks = nitems(rk3328_iomux_bank),
	.pin_fixup = rk3328_pin_fixup,
	.npin_fixup = nitems(rk3328_pin_fixup),
	.pin_drive = rk3328_pin_drive,
	.npin_drive = nitems(rk3328_pin_drive),
	.get_pd_offset = rk3328_get_pd_offset,
	.get_syscon = rk3328_get_syscon,
};

static struct rk_pinctrl_bank rk3399_iomux_bank[] = {
	{
		.bank_num = 0,
		.subbank_num = 0,
		.offset = 0x00,
		.nbits = 2,
	},
	{
		.bank_num = 0,
		.subbank_num = 1,
		.offset = 0x04,
		.nbits = 2,
	},
	{
		.bank_num = 0,
		.subbank_num = 2,
		.offset = 0x08,
		.nbits = 2,
	},
	{
		.bank_num = 0,
		.subbank_num = 3,
		.offset = 0x0c,
		.nbits = 2,
	},
	{
		.bank_num = 1,
		.subbank_num = 0,
		.offset = 0x10,
		.nbits = 2,
	},
	{
		.bank_num = 1,
		.subbank_num = 1,
		.offset = 0x14,
		.nbits = 2,
	},
	{
		.bank_num = 1,
		.subbank_num = 2,
		.offset = 0x18,
		.nbits = 2,
	},
	{
		.bank_num = 1,
		.subbank_num = 3,
		.offset = 0x1c,
		.nbits = 2,
	},
	{
		.bank_num = 2,
		.subbank_num = 0,
		.offset = 0xe000,
		.nbits = 2,
	},
	{
		.bank_num = 2,
		.subbank_num = 1,
		.offset = 0xe004,
		.nbits = 2,
	},
	{
		.bank_num = 2,
		.subbank_num = 2,
		.offset = 0xe008,
		.nbits = 2,
	},
	{
		.bank_num = 2,
		.subbank_num = 3,
		.offset = 0xe00c,
		.nbits = 2,
	},
	{
		.bank_num = 3,
		.subbank_num = 0,
		.offset = 0xe010,
		.nbits = 2,
	},
	{
		.bank_num = 3,
		.subbank_num = 1,
		.offset = 0xe014,
		.nbits = 2,
	},
	{
		.bank_num = 3,
		.subbank_num = 2,
		.offset = 0xe018,
		.nbits = 2,
	},
	{
		.bank_num = 3,
		.subbank_num = 3,
		.offset = 0xe01c,
		.nbits = 2,
	},
	{
		.bank_num = 4,
		.subbank_num = 0,
		.offset = 0xe020,
		.nbits = 2,
	},
	{
		.bank_num = 4,
		.subbank_num = 1,
		.offset = 0xe024,
		.nbits = 2,
	},
	{
		.bank_num = 4,
		.subbank_num = 2,
		.offset = 0xe028,
		.nbits = 2,
	},
	{
		.bank_num = 4,
		.subbank_num = 3,
		.offset = 0xe02c,
		.nbits = 2,
	},
};

static struct rk_pinctrl_pin_fixup rk3399_pin_fixup[] = {};

static struct rk_pinctrl_pin_drive rk3399_pin_drive[] = {
	/* GPIO0A */
	RK_PINDRIVE(0, 0, 0x80, 0, 5)
	RK_PINDRIVE(0, 0, 0x80, 1, 10)
	RK_PINDRIVE(0, 0, 0x80, 2, 15)
	RK_PINDRIVE(0, 0, 0x80, 3, 20)

	/* GPIOB */
	RK_PINDRIVE(0, 1, 0x88, 0, 5)
	RK_PINDRIVE(0, 1, 0x88, 1, 10)
	RK_PINDRIVE(0, 1, 0x88, 2, 15)
	RK_PINDRIVE(0, 1, 0x88, 3, 20)

	/* GPIO1A */
	RK_PINDRIVE(1, 0, 0xA0, 0, 3)
	RK_PINDRIVE(1, 0, 0xA0, 1, 6)
	RK_PINDRIVE(1, 0, 0xA0, 2, 9)
	RK_PINDRIVE(1, 0, 0xA0, 3, 12)

	/* GPIO1B */
	RK_PINDRIVE(1, 1, 0xA8, 0, 3)
	RK_PINDRIVE(1, 1, 0xA8, 1, 6)
	RK_PINDRIVE(1, 1, 0xA8, 2, 9)
	RK_PINDRIVE(1, 1, 0xA8, 3, 12)

	/* GPIO1C */
	RK_PINDRIVE(1, 2, 0xB0, 0, 3)
	RK_PINDRIVE(1, 2, 0xB0, 1, 6)
	RK_PINDRIVE(1, 2, 0xB0, 2, 9)
	RK_PINDRIVE(1, 2, 0xB0, 3, 12)

	/* GPIO1D */
	RK_PINDRIVE(1, 3, 0xB8, 0, 3)
	RK_PINDRIVE(1, 3, 0xB8, 1, 6)
	RK_PINDRIVE(1, 3, 0xB8, 2, 9)
	RK_PINDRIVE(1, 3, 0xB8, 3, 12)
};

static uint32_t
rk3399_get_pd_offset(struct rk_pinctrl_softc *sc, uint32_t bank)
{
	if (bank < 2)
		return (0x40);

	return (0xe040);
}

static struct syscon *
rk3399_get_syscon(struct rk_pinctrl_softc *sc, uint32_t bank)
{
	if (bank < 2)
		return (sc->pmu);

	return (sc->grf);
}

struct rk_pinctrl_conf rk3399_conf = {
	.iomux_conf = rk3399_iomux_bank,
	.iomux_nbanks = nitems(rk3399_iomux_bank),
	.pin_fixup = rk3399_pin_fixup,
	.npin_fixup = nitems(rk3399_pin_fixup),
	.pin_drive = rk3399_pin_drive,
	.npin_drive = nitems(rk3399_pin_drive),
	.get_pd_offset = rk3399_get_pd_offset,
	.get_syscon = rk3399_get_syscon,
};

static struct ofw_compat_data compat_data[] = {
#ifdef SOC_ROCKCHIP_RK3328
	{"rockchip,rk3328-pinctrl", (uintptr_t)&rk3328_conf},
#endif
#ifdef SOC_ROCKCHIP_RK3399
	{"rockchip,rk3399-pinctrl", (uintptr_t)&rk3399_conf},
#endif
	{NULL,             0}
};

static int
rk_pinctrl_parse_bias(phandle_t node)
{
	if (OF_hasprop(node, "bias-disable"))
		return (0);
	if (OF_hasprop(node, "bias-pull-up"))
		return (1);
	if (OF_hasprop(node, "bias-pull-down"))
		return (2);

	return (-1);
}

static int rk_pinctrl_parse_drive(struct rk_pinctrl_softc *sc, phandle_t node,
  uint32_t bank, uint32_t subbank, uint32_t *drive, uint32_t *offset)
{
	uint32_t value;
	int i;

	if (OF_getencprop(node, "drive-strength", &value,
	    sizeof(value)) != 0)
		return (-1);

	/* Map to the correct drive value */
	for (i = 0; i < sc->conf->npin_drive; i++) {
		if (sc->conf->pin_drive[i].bank != bank &&
		    sc->conf->pin_drive[i].subbank != subbank)
			continue;
		if (sc->conf->pin_drive[i].ma == value) {
			*drive = sc->conf->pin_drive[i].value;
			return (0);
		}
	}

	return (-1);
}

static void
rk_pinctrl_get_fixup(struct rk_pinctrl_softc *sc, uint32_t bank, uint32_t pin,
    uint32_t *reg, uint32_t *mask, uint32_t *bit)
{
	int i;

	for (i = 0; i < sc->conf->npin_fixup; i++)
		if (sc->conf->pin_fixup[i].bank == bank &&
		    sc->conf->pin_fixup[i].pin == pin) {
			*reg = sc->conf->pin_fixup[i].reg;
			*mask = sc->conf->pin_fixup[i].mask;
			*bit = sc->conf->pin_fixup[i].bit;

			return;
		}
}

static void
rk_pinctrl_configure_pin(struct rk_pinctrl_softc *sc, uint32_t *pindata)
{
	phandle_t pin_conf;
	struct syscon *syscon;
	uint32_t bank, subbank, pin, function, bias;
	uint32_t bit, mask, reg, drive;
	int i;

	bank = pindata[0];
	pin = pindata[1];
	function = pindata[2];
	pin_conf = OF_node_from_xref(pindata[3]);
	subbank = pin / 8;

	for (i = 0; i < sc->conf->iomux_nbanks; i++)
		if (sc->conf->iomux_conf[i].bank_num == bank &&
		    sc->conf->iomux_conf[i].subbank_num == subbank)
			break;

	if (i == sc->conf->iomux_nbanks) {
		device_printf(sc->dev, "Unknown pin %d in bank %d\n", pin,
		    bank);
		return;
	}

	/* Find syscon */
	syscon = sc->conf->get_syscon(sc, bank);

	/* Parse pin function */
	reg = sc->conf->iomux_conf[i].offset;
	switch (sc->conf->iomux_conf[i].nbits) {
	case 3:
		if ((pin % 8) >= 5)
			reg += 4;
		bit = (pin % 8 % 5) * 3;
		mask = (0x7 << bit) << 16;
		break;
	case 2:
	default:
		bit = (pin % 8) * 2;
		mask = (0x3 << bit) << 16;
		break;
	}
	rk_pinctrl_get_fixup(sc, bank, pin, &reg, &mask, &bit);
	SYSCON_WRITE_4(syscon, reg, function << bit | mask);

	/* Pull-Up/Down */
	bias = rk_pinctrl_parse_bias(pin_conf);
	if (bias >= 0) {
		reg = sc->conf->get_pd_offset(sc, bank);

		reg += bank * 0x10 + ((pin / 8) * 0x4);
		bit = (pin % 8) * 2;
		mask = (0x3 << bit) << 16;
		SYSCON_WRITE_4(syscon, reg, bias << bit | mask);
	}

	/* Drive Strength */
	if (rk_pinctrl_parse_drive(sc, pin_conf, bank, subbank, &drive, &reg) == 0) {
		bit = (pin % 8) * 2;
		mask = (0x3 << bit) << 16;
		SYSCON_WRITE_4(syscon, reg, bias << bit | mask);
	}
}

static int
rk_pinctrl_configure_pins(device_t dev, phandle_t cfgxref)
{
	struct rk_pinctrl_softc *sc;
	phandle_t node;
	uint32_t *pins;
	int i, npins;

	sc = device_get_softc(dev);
	node = OF_node_from_xref(cfgxref);

	npins = OF_getencprop_alloc_multi(node, "rockchip,pins",  sizeof(*pins),
	    (void **)&pins);
	if (npins <= 0)
		return (ENOENT);

	for (i = 0; i != npins; i += 4)
		rk_pinctrl_configure_pin(sc, pins + i);

	return (0);
}

static int
rk_pinctrl_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "RockChip Pinctrl controller");
	return (BUS_PROBE_DEFAULT);
}

static int
rk_pinctrl_attach(device_t dev)
{
	struct rk_pinctrl_softc *sc;
	phandle_t node;
	device_t cdev;

	sc = device_get_softc(dev);
	sc->dev = dev;

	node = ofw_bus_get_node(dev);

	if (OF_hasprop(node, "rockchip,grf") &&
	    syscon_get_by_ofw_property(dev, node,
	    "rockchip,grf", &sc->grf) != 0) {
		device_printf(dev, "cannot get grf driver handle\n");
		return (ENXIO);
	}

	// RK3399 has banks in PMU. RK3328 does not have a PMU.
	if (ofw_bus_node_is_compatible(node, "rockchip,rk3399-pinctrl")) {
		if (OF_hasprop(node, "rockchip,pmu") &&
		    syscon_get_by_ofw_property(dev, node,
		    "rockchip,pmu", &sc->pmu) != 0) {
			device_printf(dev, "cannot get pmu driver handle\n");
			return (ENXIO);
		}
	}

	sc->conf = (struct rk_pinctrl_conf *)ofw_bus_search_compatible(dev,
	    compat_data)->ocd_data;

	fdt_pinctrl_register(dev, "rockchip,pins");
	fdt_pinctrl_configure_tree(dev);

	simplebus_init(dev, node);

	bus_generic_probe(dev);

	/* Attach child devices */
	for (node = OF_child(node); node > 0; node = OF_peer(node)) {
		if (!ofw_bus_node_is_compatible(node, "rockchip,gpio-bank"))
			continue;
		cdev = simplebus_add_device(dev, node, 0, NULL, -1, NULL);
		if (cdev != NULL)
			device_probe_and_attach(cdev);
	}

	return (bus_generic_attach(dev));
}

static int
rk_pinctrl_detach(device_t dev)
{

	return (EBUSY);
}

static device_method_t rk_pinctrl_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		rk_pinctrl_probe),
	DEVMETHOD(device_attach,	rk_pinctrl_attach),
	DEVMETHOD(device_detach,	rk_pinctrl_detach),

        /* fdt_pinctrl interface */
	DEVMETHOD(fdt_pinctrl_configure,rk_pinctrl_configure_pins),

	DEVMETHOD_END
};

static devclass_t rk_pinctrl_devclass;

DEFINE_CLASS_1(rk_pinctrl, rk_pinctrl_driver, rk_pinctrl_methods,
    sizeof(struct rk_pinctrl_softc), simplebus_driver);

EARLY_DRIVER_MODULE(rk_pinctrl, simplebus, rk_pinctrl_driver,
    rk_pinctrl_devclass, 0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);
MODULE_VERSION(rk_pinctrl, 1);
