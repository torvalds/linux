/*	$OpenBSD: glkgpio.c,v 1.9 2025/06/16 15:44:35 kettenis Exp $	*/
/*
 * Copyright (c) 2016 Mark Kettenis
 * Copyright (c) 2019 James Hastings
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

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#define GLKGPIO_CONF_TXSTATE	0x00000001
#define GLKGPIO_CONF_RXSTATE	0x00000002
#define GLKGPIO_CONF_RXINV	0x00800000
#define GLKGPIO_CONF_RXEV_EDGE	0x02000000
#define GLKGPIO_CONF_RXEV_ZERO	0x04000000
#define GLKGPIO_CONF_RXEV_MASK	0x06000000

#define GLKGPIO_IRQ_STS		0x100
#define GLKGPIO_IRQ_EN		0x110
#define GLKGPIO_PAD_CFG0	0x600

struct glkgpio_intrhand {
	int (*ih_func)(void *);
	void *ih_arg;
	int ih_ipl;
};

struct glkgpio_softc {
	struct device sc_dev;
	struct acpi_softc *sc_acpi;
	struct aml_node *sc_node;

	bus_space_tag_t sc_memt;
	bus_space_handle_t sc_memh;
	void *sc_ih;

	int sc_npins;
	struct glkgpio_intrhand *sc_pin_ih;

	struct acpi_gpio sc_gpio;
};

int	glkgpio_match(struct device *, void *, void *);
void	glkgpio_attach(struct device *, struct device *, void *);

const struct cfattach glkgpio_ca = {
	sizeof(struct glkgpio_softc), glkgpio_match, glkgpio_attach
};

struct cfdriver glkgpio_cd = {
	NULL, "glkgpio", DV_DULL
};

const char *glkgpio_hids[] = {
	"INT3453",
	NULL
};

int	glkgpio_read_pin(void *, int);
void	glkgpio_write_pin(void *, int, int);
void	glkgpio_intr_establish(void *, int, int, int, int (*)(void *), void *);
void	glkgpio_intr_enable(void *, int);
void	glkgpio_intr_disable(void *, int);
int	glkgpio_intr(void *);

int
glkgpio_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	if (aaa->aaa_naddr < 1 || aaa->aaa_nirq < 1)
		return 0;
	return acpi_matchhids(aaa, glkgpio_hids, cf->cf_driver->cd_name);
}

void
glkgpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct glkgpio_softc *sc = (struct glkgpio_softc *)self;
	struct acpi_attach_args *aaa = aux;
	int64_t uid;
	int i;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_node = aaa->aaa_node;
	printf(" %s", sc->sc_node->name);

	if (aml_evalinteger(sc->sc_acpi, sc->sc_node, "_UID", 0, NULL, &uid)) {
		printf(": can't find uid\n");
		return;
	}

	printf(" uid %lld", uid);

	switch (uid) {
	case 1:
		sc->sc_npins = 80;
		break;
	case 2:
		sc->sc_npins = 80;
		break;
	case 3:
		sc->sc_npins = 20;
		break;
	case 4:
		sc->sc_npins = 35;
		break;
	default:
		printf("\n");
		return;
	}

	printf(" addr 0x%llx/0x%llx", aaa->aaa_addr[0], aaa->aaa_size[0]);
	printf(" irq %d", aaa->aaa_irq[0]);

	sc->sc_memt = aaa->aaa_bst[0];
	if (bus_space_map(sc->sc_memt, aaa->aaa_addr[0], aaa->aaa_size[0],
	    0, &sc->sc_memh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_pin_ih = mallocarray(sc->sc_npins, sizeof(*sc->sc_pin_ih),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	sc->sc_ih = acpi_intr_establish(aaa->aaa_irq[0], aaa->aaa_irq_flags[0],
	    IPL_BIO, glkgpio_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto unmap;
	}

	sc->sc_gpio.cookie = sc;
	sc->sc_gpio.read_pin = glkgpio_read_pin;
	sc->sc_gpio.write_pin = glkgpio_write_pin;
	sc->sc_gpio.intr_establish = glkgpio_intr_establish;
	sc->sc_gpio.intr_enable = glkgpio_intr_enable;
	sc->sc_gpio.intr_disable = glkgpio_intr_disable;
	sc->sc_node->gpio = &sc->sc_gpio;

	/* Mask and clear all interrupts. */
	for (i = 0; i < sc->sc_npins; i++) {
		if (i % 32 == 0) {
			bus_space_write_4(sc->sc_memt, sc->sc_memh,
			    GLKGPIO_IRQ_EN + (i / 32) * 4, 0x00000000);
			bus_space_write_4(sc->sc_memt, sc->sc_memh,
			    GLKGPIO_IRQ_STS + (i / 32) * 4, 0xffffffff);
		}
	}

	printf(", %d pins\n", sc->sc_npins);

	acpi_register_gpio(sc->sc_acpi, sc->sc_node);
	return;

unmap:
	free(sc->sc_pin_ih, M_DEVBUF, sc->sc_npins * sizeof(*sc->sc_pin_ih));
	bus_space_unmap(sc->sc_memt, sc->sc_memh, aaa->aaa_size[0]);
}

int
glkgpio_read_pin(void *cookie, int pin)
{
	struct glkgpio_softc *sc = cookie;
	uint32_t reg;

	reg = bus_space_read_4(sc->sc_memt, sc->sc_memh,
	    GLKGPIO_PAD_CFG0 + pin * 16);

	return !!(reg & GLKGPIO_CONF_RXSTATE);
}

void
glkgpio_write_pin(void *cookie, int pin, int value)
{
	struct glkgpio_softc *sc = cookie;
	uint32_t reg;

	reg = bus_space_read_4(sc->sc_memt, sc->sc_memh,
	    GLKGPIO_PAD_CFG0 + pin * 16);
	if (value)
		reg |= GLKGPIO_CONF_TXSTATE;
	else
		reg &= ~GLKGPIO_CONF_TXSTATE;
	bus_space_write_4(sc->sc_memt, sc->sc_memh,
	    GLKGPIO_PAD_CFG0 + pin * 16, reg);
}

void
glkgpio_intr_establish(void *cookie, int pin, int flags, int level,
    int (*func)(void *), void *arg)
{
	struct glkgpio_softc *sc = cookie;
	uint32_t reg;

	KASSERT(pin >= 0 && pin < sc->sc_npins);

	sc->sc_pin_ih[pin].ih_func = func;
	sc->sc_pin_ih[pin].ih_arg = arg;
	sc->sc_pin_ih[pin].ih_ipl = level & ~IPL_WAKEUP;

	reg = bus_space_read_4(sc->sc_memt, sc->sc_memh,
	    GLKGPIO_PAD_CFG0 + pin * 16);
	reg &= ~(GLKGPIO_CONF_RXEV_MASK | GLKGPIO_CONF_RXINV);
	if ((flags & LR_GPIO_MODE) == 1)
		reg |= GLKGPIO_CONF_RXEV_EDGE;
	if ((flags & LR_GPIO_POLARITY) == LR_GPIO_ACTLO)
		reg |= GLKGPIO_CONF_RXINV;
	if ((flags & LR_GPIO_POLARITY) == LR_GPIO_ACTBOTH)
		reg |= GLKGPIO_CONF_RXEV_EDGE | GLKGPIO_CONF_RXEV_ZERO;
	bus_space_write_4(sc->sc_memt, sc->sc_memh,
	    GLKGPIO_PAD_CFG0 + pin * 16, reg);

	reg = bus_space_read_4(sc->sc_memt, sc->sc_memh,
	    GLKGPIO_IRQ_EN + (pin / 32) * 4);
	reg |= (1 << (pin % 32));
	bus_space_write_4(sc->sc_memt, sc->sc_memh,
	    GLKGPIO_IRQ_EN + (pin / 32) * 4, reg);
}

void
glkgpio_intr_enable(void *cookie, int pin)
{
	struct glkgpio_softc *sc = cookie;
	uint32_t reg;

	KASSERT(pin >= 0 && pin < sc->sc_npins);

	reg = bus_space_read_4(sc->sc_memt, sc->sc_memh,
	    GLKGPIO_IRQ_EN + (pin / 32) * 4);
	reg |= (1 << (pin % 32));
	bus_space_write_4(sc->sc_memt, sc->sc_memh,
	    GLKGPIO_IRQ_EN + (pin / 32) * 4, reg);
}

void
glkgpio_intr_disable(void *cookie, int pin)
{
	struct glkgpio_softc *sc = cookie;
	uint32_t reg;

	KASSERT(pin >= 0 && pin < sc->sc_npins);

	reg = bus_space_read_4(sc->sc_memt, sc->sc_memh,
	    GLKGPIO_IRQ_EN + (pin / 32) * 4);
	reg &= ~(1 << (pin % 32));
	bus_space_write_4(sc->sc_memt, sc->sc_memh,
	    GLKGPIO_IRQ_EN + (pin / 32) * 4, reg);
}

int
glkgpio_intr(void *arg)
{
	struct glkgpio_softc *sc = arg;
	struct glkgpio_intrhand *ih;
	uint32_t status, enable;
	int rc = 0;
	int pin, s;

	for (pin = 0; pin < sc->sc_npins; pin++) {
		if (pin % 32 == 0) {
			status = bus_space_read_4(sc->sc_memt, sc->sc_memh,
			    GLKGPIO_IRQ_STS + (pin / 32) * 4);
			bus_space_write_4(sc->sc_memt, sc->sc_memh,
			    GLKGPIO_IRQ_STS + (pin / 32) * 4, status);
			enable = bus_space_read_4(sc->sc_memt, sc->sc_memh,
			    GLKGPIO_IRQ_EN + (pin / 32) * 4);
			status &= enable;
		}
		if (status & (1 << (pin % 32))) {
			ih = &sc->sc_pin_ih[pin];
			if (ih->ih_func) {
				s = splraise(ih->ih_ipl);
				ih->ih_func(ih->ih_arg);
				splx(s);
			}
			rc = 1;
		}
	}
	return rc;
}
