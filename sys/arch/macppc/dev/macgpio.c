/*	$OpenBSD: macgpio.c,v 1.10 2022/03/13 12:33:01 mpi Exp $	*/
/*	$NetBSD: gpio.c,v 1.2 2001/02/27 05:16:33 matt Exp $	*/

/*-
 * Copyright (C) 1998	Internet Research Institute, Inc.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by
 *	Internet Research Institute, Inc.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <dev/ofw/openfirm.h>

#include <machine/autoconf.h>
#include <machine/pio.h>

#include "adb.h"

static void macgpio_attach (struct device *, struct device *, void *);
static int macgpio_match (struct device *, void *, void *);
static int macgpio_print (void *aux, const char *gpio);

static void macgpio_gpio_attach (struct device *, struct device *, void *);
static int macgpio_gpio_match (struct device *, void *, void *);
static int gpio_intr (void *);

struct gpio_softc {
	struct device sc_dev;
	u_int8_t *sc_port;
};

const struct cfattach macgpio_ca = {
	sizeof(struct gpio_softc), macgpio_match, macgpio_attach
};

const struct cfattach macgpio_gpio_ca = {
	sizeof(struct gpio_softc), macgpio_gpio_match, macgpio_gpio_attach
};

struct cfdriver macgpio_cd = {
	NULL, "macgpio", DV_DULL
};

int
macgpio_match(struct device *parent, void *cf, void *aux)
{
	struct confargs *ca = aux;

	if (strcmp(ca->ca_name, "gpio") != 0)
		return 0;

	if (ca->ca_nreg < 8)
		return 0;

	return 1;
}

void
macgpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct gpio_softc *sc = (struct gpio_softc *)self;
	struct confargs *ca = aux, ca2;
	int child;
	int namelen;
	int intr[6];
	u_int reg[20];
	char name[32];

	printf("\n");

	sc->sc_port = mapiodev(ca->ca_baseaddr + ca->ca_reg[0], ca->ca_reg[1]);

	ca2.ca_baseaddr = ca->ca_baseaddr;
	for (child = OF_child(ca->ca_node); child; child = OF_peer(child)) {
		namelen = OF_getprop(child, "name", name, sizeof(name));
		if (namelen < 0)
			continue;
		if (namelen >= sizeof(name))
			continue;

		name[namelen] = 0;
		ca2.ca_name = name;
		ca2.ca_node = child;

		ca2.ca_nreg  = OF_getprop(child, "reg", reg, sizeof(reg));
		ca2.ca_nintr = OF_getprop(child, "AAPL,interrupts", intr,
				sizeof(intr));
		if (ca2.ca_nintr == -1)
			ca2.ca_nintr = OF_getprop(child, "interrupts", intr,
					sizeof(intr));

		ca2.ca_reg = reg;
		ca2.ca_intr = intr;

		config_found(self, &ca2, macgpio_print);
	}
}

int
macgpio_print(void *aux, const char *gpio)
{
	struct confargs *ca = aux;
	if (gpio)
		printf("\"%s\" at %s", ca->ca_name, gpio);

	if (ca->ca_nreg > 0)
		printf(" offset 0x%x", ca->ca_reg[0]);

	return UNCONF;
}

int
macgpio_gpio_match(struct device *parent, void *cf, void *aux)
{
	struct confargs *ca = aux;

	if (strcmp(ca->ca_name, "extint-gpio1") != 0)
		return 0;

	if (ca->ca_nintr < 4)
		return 0;

	return 1;
}

void
macgpio_gpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct gpio_softc *sc = (struct gpio_softc *)self;
	struct confargs *ca = aux;


	sc->sc_port = ((struct gpio_softc *) parent)->sc_port;
	mac_intr_establish(parent, ca->ca_intr[0], IST_LEVEL, IPL_TTY,
	    gpio_intr, sc, sc->sc_dev.dv_xname);

	printf(": irq %d\n", ca->ca_intr[0]);
}

#if NADB > 0
extern int adb_intr (void *);
extern struct cfdriver adb_cd;
#endif

int
gpio_intr(void *arg)
{
	int rv = 0;

#if NADB > 0
	if (adb_cd.cd_devs[0] != NULL)
		rv = adb_intr(adb_cd.cd_devs[0]);
#endif

	return rv;
}
