/*	$OpenBSD: pcfiic_ebus.c,v 1.15 2021/10/24 17:05:03 mpi Exp $ */

/*
 * Copyright (c) 2006 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Device specific driver for the EBus i2c devices found on some sun4u
 * systems. On systems not having a boot-bus controller the i2c devices
 * are PCF8584.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/rwlock.h>

#include <machine/bus.h>
#include <machine/openfirm.h>
#include <machine/autoconf.h>

#include <sparc64/dev/ebusreg.h>
#include <sparc64/dev/ebusvar.h>

#include <dev/i2c/i2cvar.h>
#include <sparc64/dev/ofwi2cvar.h>

#include <dev/ic/pcf8584var.h>

int	pcfiic_ebus_match(struct device *, void *, void *);
void	pcfiic_ebus_attach(struct device *, struct device *, void *);

struct pcfiic_ebus_softc {
	struct pcfiic_softc	esc_sc;

	int			esc_node;
	void			*esc_ih;
};

const struct cfattach pcfiic_ebus_ca = {
	sizeof(struct pcfiic_ebus_softc), pcfiic_ebus_match, pcfiic_ebus_attach
};

void	envctrl_scan(struct device *, struct i2cbus_attach_args *, void *);
void	envctrltwo_scan(struct device *, struct i2cbus_attach_args *, void *);

int
pcfiic_ebus_match(struct device *parent, void *match, void *aux)
{
	struct ebus_attach_args		*ea = aux;
	char				compat[32];

	if (strcmp(ea->ea_name, "SUNW,envctrl") == 0 ||
	    strcmp(ea->ea_name, "SUNW,envctrltwo") == 0)
		return (1);

	if (strcmp(ea->ea_name, "i2c") != 0)
		return (0);

	if (OF_getprop(ea->ea_node, "compatible", compat, sizeof(compat)) == -1)
		return (0);

	if (strcmp(compat, "pcf8584") == 0 ||
	    strcmp(compat, "i2cpcf,8584") == 0 ||
	    strcmp(compat, "SUNW,i2c-pic16f747") == 0 ||
	    strcmp(compat, "SUNW,bbc-i2c") == 0)
		return (1);

	return (0);
}

void
pcfiic_ebus_attach(struct device *parent, struct device *self, void *aux)
{
	struct pcfiic_ebus_softc	*esc = (struct pcfiic_ebus_softc *)self;
	struct pcfiic_softc		*sc = &esc->esc_sc;
	struct ebus_attach_args		*ea = aux;
	char				compat[32];
	u_int64_t			addr;
	u_int8_t			clock = PCF_CLOCK_12 | PCF_FREQ_90;
	int				swapregs = 0;

	if (ea->ea_nregs < 1 || ea->ea_nregs > 2) {
		printf(": expected 1 or 2 registers, got %d\n", ea->ea_nregs);
		return;
	}

	if (OF_getprop(ea->ea_node, "compatible", compat, sizeof(compat)) > 0 &&
	    strcmp(compat, "SUNW,bbc-i2c") == 0) {
		/*
		 * On BBC-based machines, Sun swapped the order of
		 * the registers on their clone pcf, plus they feed
		 * it a non-standard clock.
		 */
		int clk = getpropint(findroot(), "clock-frequency", 0);

		if (clk < 105000000)
			clock = PCF_CLOCK_3 | PCF_FREQ_90;
		else if (clk < 160000000)
			clock = PCF_CLOCK_4_43 | PCF_FREQ_90;
		swapregs = 1;
	}

	if (OF_getprop(ea->ea_node, "own-address", &addr, sizeof(addr)) == -1) {
		addr = 0xaa;
	} else if (addr == 0x00 || addr > 0xff) {
		printf(": invalid address on I2C bus");
		return;
	}

	/* Prefer prom mapping, then memory mapping, then io mapping */
	if (ea->ea_nvaddrs) {
		if (bus_space_map(ea->ea_memtag, ea->ea_vaddrs[0], 0,
		    BUS_SPACE_MAP_PROMADDRESS, &sc->sc_ioh) != 0)
			goto fail;
		sc->sc_iot = ea->ea_memtag;
	} else if (ebus_bus_map(ea->ea_memtag, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[0]),
	    ea->ea_regs[0].size, 0, 0, &sc->sc_ioh) == 0) {
		sc->sc_iot = ea->ea_memtag;
	} else if (ebus_bus_map(ea->ea_iotag, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[0]),
	    ea->ea_regs[0].size, 0, 0, &sc->sc_ioh) == 0) {
		sc->sc_iot = ea->ea_iotag;
	} else {
fail:
		printf(": can't map register space\n");
               	return;
	}

	if (ea->ea_nregs == 2) {
		/*
		 * Second register only occurs on BBC-based machines,
		 * and is likely not prom mapped
		*/
		if (ebus_bus_map(sc->sc_iot, 0, EBUS_PADDR_FROM_REG(&ea->ea_regs[1]),
		    ea->ea_regs[1].size, 0, 0, &sc->sc_ioh2) != 0) {
			printf(": can't map 2nd register space\n");
			return;
		}
		sc->sc_master = 1;
	}

	if (ea->ea_nintrs >= 1)
		esc->esc_ih = bus_intr_establish(sc->sc_iot, ea->ea_intrs[0],
		    IPL_BIO, 0, pcfiic_intr, sc, self->dv_xname);
	else
		esc->esc_ih = NULL;


	if (esc->esc_ih == NULL)
		sc->sc_poll = 1;

	if (strcmp(ea->ea_name, "SUNW,envctrl") == 0)
		pcfiic_attach(sc, 0x55, PCF_CLOCK_12 | PCF_FREQ_45, 0,
		    envctrl_scan, &ea->ea_node);
	else if (strcmp(ea->ea_name, "SUNW,envctrltwo") == 0)
		pcfiic_attach(sc, 0x55, PCF_CLOCK_12 | PCF_FREQ_45, 0,
		    envctrltwo_scan, &ea->ea_node);
	else
		pcfiic_attach(sc, (i2c_addr_t)(addr >> 1), clock, swapregs,
		    ofwiic_scan, &ea->ea_node);
}

void
envctrl_scan(struct device *self, struct i2cbus_attach_args *iba, void *aux)
{
	extern int iic_print(void *, const char *);
	struct i2c_attach_args ia;

	memset(&ia, 0, sizeof(ia));
	ia.ia_tag = iba->iba_tag;
	ia.ia_cookie = aux;

	/* Power supply 1 temperature. */
	ia.ia_addr = 0x48;
	ia.ia_name = "ecadc";
	config_found(self, &ia, iic_print);

	/* Power supply 2 temperature. */
	ia.ia_addr = 0x49;
	ia.ia_name = "ecadc";
	config_found(self, &ia, iic_print);

	/* Power supply 3 temperature. */
	ia.ia_addr = 0x4a;
	ia.ia_name = "ecadc";
	config_found(self, &ia, iic_print);

	/* Ambient temperature. */
	ia.ia_addr = 0x4d;
	ia.ia_name = "lm75";
	config_found(self, &ia, iic_print);

	/* CPU temperatures. */
	ia.ia_addr = 0x4f;
	ia.ia_name = "ecadc";
	config_found(self, &ia, iic_print);
}

void
envctrltwo_scan(struct device *self, struct i2cbus_attach_args *iba, void *aux)
{
	extern int iic_print(void *, const char *);
	struct i2c_attach_args ia;

	memset(&ia, 0, sizeof(ia));
	ia.ia_tag = iba->iba_tag;
	ia.ia_cookie = aux;

	ia.ia_addr = 0x4a;
	ia.ia_name = "ecadc";
	config_found(self, &ia, iic_print);

	ia.ia_addr = 0x4f;
	ia.ia_name = "ecadc";
	config_found(self, &ia, iic_print);
}
