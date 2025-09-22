/*	$OpenBSD: uni_n.c,v 1.17 2015/03/30 13:45:02 mpi Exp $	*/

/*
 * Copyright (c) 2013 Martin Pieuchot
 * Copyright (c) 1998-2001 Dale Rahn.
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/autoconf.h>

#include <dev/ofw/openfirm.h>

#define UNINORTH_CLK_OFFSET	0x20
#define UNINORTH_POW_OFFSET	0x30
#define UNINORTH_STA_OFFSET	0x70
#define UNINORTH_MPIC_OFFSET	0xe0

#define UNINORTH_PCICLOCK_CTL	0x01
#define UNINORTH_ETHERNET_CTL	0x02
#define UNINORTH_FIREWIRE_CTL	0x04

#define UNINORTH_POW_NORMAL	0x00
#define UNINORTH_POW_IDLE	0x01
#define UNINORTH_POW_SLEEP	0x02

#define UNINORTH_MPIC_RESET	0x02
#define UNINORTH_MPIC_ENABLE	0x04

#define UNINORTH_SLEEPING	0x01
#define UNINORTH_RUNNING	0x02


struct memc_softc {
	struct device sc_dev;
	struct ppc_bus_space sc_membus_space;

	uint8_t *sc_baseaddr;
};

int	memcmatch(struct device *, void *, void *);
void	memcattach(struct device *, struct device *, void *);
void	memc_attach_children(struct memc_softc *sc, int memc_node);
int	memc_print(void *aux, const char *name);

struct cfdriver memc_cd = {
	NULL, "memc", DV_DULL
};

const struct cfattach memc_ca = {
	sizeof(struct memc_softc), memcmatch, memcattach
};

void memc_sleep(void);
void memc_resume(void);
uint32_t memc_read(struct memc_softc *sc, int);
void memc_write(struct memc_softc *sc, int, uint32_t);
void memc_enable(struct memc_softc *, int, uint32_t);
void memc_disable(struct memc_softc *, int, uint32_t);

int
memcmatch(struct device *parent, void *cf, void *aux)
{
	struct confargs *ca = aux;

	if (strcmp(ca->ca_name, "memc") != 0)
		return (0);

	return (1);
}

void
memcattach(struct device *parent, struct device *self, void *aux)
{
	struct memc_softc *sc = (struct memc_softc *)self;
	struct confargs *ca = aux;
	uint32_t rev, reg[2];
	char name[32];
	int len;

	OF_getprop(ca->ca_node, "reg", &reg, sizeof(reg));

	len = OF_getprop(ca->ca_node, "name", name, sizeof(name));
	if (len > 0)
		name[len] = 0;

	/* Map the first page in order to access the registers */
	if (strcmp(name, "u3") == 0 || strcmp(name, "u4") == 0)
		sc->sc_baseaddr = mapiodev(reg[1], PAGE_SIZE);
	else
		sc->sc_baseaddr = mapiodev(reg[0], PAGE_SIZE);

	/* Enable the ethernet clock */
	memc_enable(sc, UNINORTH_CLK_OFFSET, UNINORTH_ETHERNET_CTL);
	len = OF_getprop(ca->ca_node, "device-rev", &rev, sizeof(rev));
	if (len < 0)
		rev = 0;

	printf (": %s rev 0x%x\n", name, rev);

	memc_attach_children(sc, ca->ca_node);
}

void
memc_attach_children(struct memc_softc *sc, int memc_node)
{
	struct confargs ca;
	int node, namelen;
	u_int32_t reg[20];
	int32_t intr[8];
	char	name[32];

	ca.ca_iot = &sc->sc_membus_space;
	ca.ca_dmat = 0; /* XXX */
	ca.ca_baseaddr = 0; /* XXX */
	sc->sc_membus_space.bus_base = ca.ca_baseaddr;

        for (node = OF_child(memc_node); node; node = OF_peer(node)) {
		namelen = OF_getprop(node, "name", name, sizeof(name));
		if (namelen < 0)
			continue;
		if (namelen >= sizeof(name))
			continue;
		name[namelen] = 0;

		ca.ca_name = name;
		ca.ca_node = node;
		ca.ca_nreg = OF_getprop(node, "reg", reg, sizeof(reg));
		ca.ca_reg = reg;
		ca.ca_nintr = OF_getprop(node, "AAPL,interrupts", intr,
				sizeof(intr));
		if (ca.ca_nintr == -1)
			ca.ca_nintr = OF_getprop(node, "interrupts", intr,
					sizeof(intr));
		ca.ca_intr = intr;

		if (strcmp(ca.ca_name, "mpic") == 0)
			memc_enable(sc, UNINORTH_MPIC_OFFSET,
			    UNINORTH_MPIC_RESET|UNINORTH_MPIC_ENABLE);

		config_found((struct device *)sc, &ca, memc_print);
	}
}

int
memc_print(void *aux, const char *name)
{
	struct confargs *ca = aux;
	/* we dont want extra stuff printing */
	if (name)
		printf("\"%s\" at %s", ca->ca_name, name);
	if (ca->ca_nreg > 0)
		printf(" offset 0x%x", ca->ca_reg[0]);
	return UNCONF;
}

void
memc_sleep(void)
{
	struct memc_softc *sc = memc_cd.cd_devs[0];

	memc_write(sc, UNINORTH_STA_OFFSET, UNINORTH_SLEEPING);
	DELAY(10);
	memc_write(sc, UNINORTH_POW_OFFSET, UNINORTH_POW_SLEEP);
	DELAY(10);
}

void
memc_resume(void)
{
	struct memc_softc *sc = memc_cd.cd_devs[0];

	memc_write(sc, UNINORTH_POW_OFFSET, UNINORTH_POW_NORMAL);
	DELAY(10);
	memc_write(sc, UNINORTH_STA_OFFSET, UNINORTH_RUNNING);
	DELAY(100); /* XXX */
}

uint32_t
memc_read(struct memc_softc *sc, int offset)
{
	return in32(sc->sc_baseaddr + offset);
}

void
memc_write(struct memc_softc *sc, int offset, uint32_t value)
{
	out32(sc->sc_baseaddr + offset, value);
}

void
memc_enable(struct memc_softc *sc, int offset, uint32_t bits)
{
	bits |= memc_read(sc, offset);
	memc_write(sc, offset, bits);
}

void
memc_disable(struct memc_softc *sc, int offset, uint32_t bits)
{
	bits = memc_read(sc, offset) & ~bits;
	memc_write(sc, offset, bits);
}
