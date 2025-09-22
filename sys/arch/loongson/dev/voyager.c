/*	$OpenBSD: voyager.c,v 1.7 2022/02/21 12:46:59 jsg Exp $	*/

/*
 * Copyright (c) 2010 Miodrag Vallat.
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
 * Silicon Motion SM501/SM502 (VoyagerGX) master driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/gpio.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <dev/gpio/gpiovar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <loongson/dev/bonito_irq.h>	/* for BONITO_NINTS */
#include <loongson/dev/voyagerreg.h>
#include <loongson/dev/voyagervar.h>

struct voyager_softc {
	struct device		 sc_dev;

	bus_space_tag_t		 sc_fbt;
	bus_space_handle_t	 sc_fbh;
	bus_size_t		 sc_fbsize;

	bus_space_tag_t		 sc_mmiot;
	bus_space_handle_t	 sc_mmioh;
	bus_size_t		 sc_mmiosize;

	struct gpio_chipset_tag	 sc_gpio_tag;
	gpio_pin_t		 sc_gpio_pins[32 + 32];

	void			*sc_ih;
	struct intrhand		*sc_intr[32];
};

int	voyager_match(struct device *, void *, void *);
void	voyager_attach(struct device *, struct device *, void *);

const struct cfattach voyager_ca = {
	sizeof(struct voyager_softc), voyager_match, voyager_attach
};

struct cfdriver voyager_cd = {
	NULL, "voyager", DV_DULL
};

void	voyager_attach_gpio(struct voyager_softc *);
int	voyager_intr(void *);
int	voyager_print(void *, const char *);
int	voyager_search(struct device *, void *, void *);

const struct pci_matchid voyager_devices[] = {
	/*
	 * 502 shares the same device ID as 501, but uses a different
	 * revision number.
	 */
	{ PCI_VENDOR_SMI, PCI_PRODUCT_SMI_SM501 }
};

int
voyager_match(struct device *parent, void *vcf, void *aux)
{
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;

	return pci_matchbyid(pa, voyager_devices, nitems(voyager_devices));
}

void
voyager_attach(struct device *parent, struct device *self, void *aux)
{
	struct voyager_softc *sc = (struct voyager_softc *)self;
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	pci_intr_handle_t ih;
	const char *intrstr;

	printf(": ");

	/*
	 * Map registers.
	 */

	if (pci_mapreg_map(pa, PCI_MAPREG_START, PCI_MAPREG_TYPE_MEM,
	    BUS_SPACE_MAP_LINEAR, &sc->sc_fbt, &sc->sc_fbh,
	    NULL, &sc->sc_fbsize, 0) != 0) {
		printf("can't map frame buffer\n");
		return;
	}

	if (pci_mapreg_map(pa, PCI_MAPREG_START + 0x04, PCI_MAPREG_TYPE_MEM,
	    BUS_SPACE_MAP_LINEAR, &sc->sc_mmiot, &sc->sc_mmioh,
	    NULL, &sc->sc_mmiosize, 0) != 0) {
		printf("can't map mmio\n");
		goto fail1;
	}

	/*
	 * Setup interrupt handling.
	 */

	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, VOYAGER_RAW_ICR,
	    0xffffffff);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, VOYAGER_IMR, 0);
	(void)bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh, VOYAGER_IMR);

	if (pci_intr_map(pa, &ih) != 0) {
		printf("can't map interrupt\n");
		goto fail2;
	}
	intrstr = pci_intr_string(pa->pa_pc, ih);
	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_TTY, voyager_intr,
	    sc, self->dv_xname);
	if (sc->sc_ih == NULL) {
		printf("can't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto fail2;
	}

	printf("%s\n", intrstr);

	/*
	 * Attach GPIO devices.
	 */
	voyager_attach_gpio(sc);

	/*
	 * Attach child devices.
	 */
	config_search(voyager_search, self, pa);

	return;
fail2:
	bus_space_unmap(sc->sc_mmiot, sc->sc_mmioh, sc->sc_mmiosize);
fail1:
	bus_space_unmap(sc->sc_fbt, sc->sc_fbh, sc->sc_fbsize);
}

int
voyager_print(void *args, const char *parentname)
{
	struct voyager_attach_args *vaa = (struct voyager_attach_args *)args;

	if (parentname != NULL)
		printf("%s at %s", vaa->vaa_name, parentname);

	return UNCONF;
}

int
voyager_search(struct device *parent, void *vcf, void *args)
{
	struct voyager_softc *sc = (struct voyager_softc *)parent;
	struct cfdata *cf = (struct cfdata *)vcf;
	struct pci_attach_args *pa = (struct pci_attach_args *)args;
	struct voyager_attach_args vaa;

	/*
	 * Caller should have attached gpio already. If it didn't, bail
	 * out here.
	 */
	if (strcmp(cf->cf_driver->cd_name, "gpio") == 0)
		return 0;

	vaa.vaa_name = cf->cf_driver->cd_name;
	vaa.vaa_pa = pa;
	vaa.vaa_fbt = sc->sc_fbt;
	vaa.vaa_fbh = sc->sc_fbh;
	vaa.vaa_mmiot = sc->sc_mmiot;
	vaa.vaa_mmioh = sc->sc_mmioh;

	if (cf->cf_attach->ca_match(parent, cf, &vaa) == 0)
		return 0;

	config_attach(parent, cf, &vaa, voyager_print);
	return 1;
}

/*
 * Interrupt dispatcher
 */

int
voyager_intr(void *vsc)
{
	struct voyager_softc *sc = (struct voyager_softc *)vsc;
	uint32_t isr, imr, mask, bitno;
	struct intrhand *ih;

	isr = bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh, VOYAGER_ISR);
	imr = bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh, VOYAGER_IMR);

	isr &= imr;
	if (isr == 0)
		return 0;

	for (bitno = 0, mask = 1 << 0; isr != 0; bitno++, mask <<= 1) {
		if ((isr & mask) == 0)
			continue;
		isr ^= mask;
		for (ih = sc->sc_intr[bitno]; ih != NULL; ih = ih->ih_next) {
			if ((*ih->ih_fun)(ih->ih_arg) != 0)
				ih->ih_count.ec_count++;
		}
	}

	return 1;
}

void *
voyager_intr_establish(void *cookie, int irq, int level, int (*fun)(void *),
    void *arg, const char *name)
{
	struct voyager_softc *sc = (struct voyager_softc *)cookie;
	struct intrhand *prevh, *nh;
	uint32_t imr;

#ifdef DIAGNOSTIC
	if (irq < 0 || irq >= nitems(sc->sc_intr))
		return NULL;
#endif

	level &= ~IPL_MPSAFE;

	nh = (struct intrhand *)malloc(sizeof *nh, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (nh == NULL)
		return NULL;

	nh->ih_fun = fun;
	nh->ih_arg = arg;
	nh->ih_level = level;
	nh->ih_irq = irq + BONITO_NINTS;
	evcount_attach(&nh->ih_count, name, &nh->ih_irq);

	if (sc->sc_intr[irq] == NULL)
		sc->sc_intr[irq] = nh;
	else {
		/* insert at tail */
		for (prevh = sc->sc_intr[irq]; prevh->ih_next != NULL;
		    prevh = prevh->ih_next) ;
		prevh->ih_next = nh;
	}

	/* enable interrupt source */
	imr = bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh, VOYAGER_IMR);
	imr |= 1 << irq;
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, VOYAGER_IMR, imr);
	(void)bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh, VOYAGER_IMR);

	return nh;
}

const char *
voyager_intr_string(void *vih)
{
	struct intrhand *ih = (struct intrhand *)vih;
	static char intrstr[1 + 32];

	snprintf(intrstr, sizeof intrstr, "voyager irq %d",
	    ih->ih_irq - BONITO_NINTS);
	return intrstr;
}

/*
 * GPIO support code
 */

int	voyager_gpio_pin_read(void *, int);
void	voyager_gpio_pin_write(void *, int, int);
void	voyager_gpio_pin_ctl(void *, int, int);

static const struct gpio_chipset_tag voyager_gpio_tag = {
	.gp_pin_read = voyager_gpio_pin_read,
	.gp_pin_write = voyager_gpio_pin_write,
	.gp_pin_ctl = voyager_gpio_pin_ctl
};

int
voyager_gpio_pin_read(void *cookie, int pin)
{
	struct voyager_softc *sc = (struct voyager_softc *)cookie;
	bus_addr_t reg;
	int32_t data, mask;

	if (pin >= 32) {
		pin -= 32;
		reg = VOYAGER_GPIO_DATA_HIGH;
	} else {
		reg = VOYAGER_GPIO_DATA_LOW;
	}
	mask = 1 << pin;

	data = bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh, reg);
	return data & mask ? GPIO_PIN_HIGH : GPIO_PIN_LOW;
}

void
voyager_gpio_pin_write(void *cookie, int pin, int val)
{
	struct voyager_softc *sc = (struct voyager_softc *)cookie;
	bus_addr_t reg;
	int32_t data, mask;

	if (pin >= 32) {
		pin -= 32;
		reg = VOYAGER_GPIO_DATA_HIGH;
	} else {
		reg = VOYAGER_GPIO_DATA_LOW;
	}
	mask = 1 << pin;
	data = bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh, reg);
	if (val)
		data |= mask;
	else
		data &= ~mask;
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, reg, data);
	(void)bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh, reg);
}

void
voyager_gpio_pin_ctl(void *cookie, int pin, int flags)
{
	struct voyager_softc *sc = (struct voyager_softc *)cookie;
	bus_addr_t reg;
	int32_t data, mask;

	if (pin >= 32) {
		pin -= 32;
		reg = VOYAGER_GPIO_DIR_HIGH;
	} else {
		reg = VOYAGER_GPIO_DIR_LOW;
	}
	mask = 1 << pin;
	data = bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh, reg);
	if (ISSET(flags, GPIO_PIN_OUTPUT))
		data |= mask;
	else
		data &= ~mask;
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, reg, data);
	(void)bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh, reg);
}

void
voyager_attach_gpio(struct voyager_softc *sc)
{
	struct gpiobus_attach_args gba;
	int pin;
	uint32_t control, value;

	bcopy(&voyager_gpio_tag, &sc->sc_gpio_tag, sizeof voyager_gpio_tag);
	sc->sc_gpio_tag.gp_cookie = sc;

	control = bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh,
	    VOYAGER_GPIOL_CONTROL);
	value = bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh,
	    VOYAGER_GPIO_DATA_LOW);
	for (pin = 0; pin < 32; pin++) {
		sc->sc_gpio_pins[pin].pin_num = pin;
		if ((control & 1) == 0) {
			sc->sc_gpio_pins[pin].pin_caps =
			    GPIO_PIN_INPUT | GPIO_PIN_OUTPUT;
			sc->sc_gpio_pins[pin].pin_state = value & 1;
		} else {
			/* disable control of taken over pins */
			sc->sc_gpio_pins[pin].pin_caps = 0;
			sc->sc_gpio_pins[pin].pin_state = 0;
		}
	}

	control = bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh,
	    VOYAGER_GPIOH_CONTROL);
	value = bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh,
	    VOYAGER_GPIO_DATA_HIGH);
	for (pin = 32 + 0; pin < 32 + 32; pin++) {
		sc->sc_gpio_pins[pin].pin_num = pin;
		if ((control & 1) == 0) {
			sc->sc_gpio_pins[pin].pin_caps =
			    GPIO_PIN_INPUT | GPIO_PIN_OUTPUT;
			sc->sc_gpio_pins[pin].pin_state = value & 1;
		} else {
			/* disable control of taken over pins */
			sc->sc_gpio_pins[pin].pin_caps = 0;
			sc->sc_gpio_pins[pin].pin_state = 0;
		}
	}

	gba.gba_name = "gpio";
	gba.gba_gc = &sc->sc_gpio_tag;
	gba.gba_pins = sc->sc_gpio_pins;
	gba.gba_npins = nitems(sc->sc_gpio_pins);

	config_found(&sc->sc_dev, &gba, voyager_print);
}
