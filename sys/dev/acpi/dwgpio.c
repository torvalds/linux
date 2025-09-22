/*	$OpenBSD: dwgpio.c,v 1.10 2025/06/16 15:44:35 kettenis Exp $	*/
/*
 * Copyright (c) 2020 Mark Kettenis
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

/* Registers. */
#define GPIO_SWPORTA_DR		0x0000
#define GPIO_SWPORTA_DDR	0x0004
#define GPIO_INTEN		0x0030
#define GPIO_INTTYPE_LEVEL	0x0034
#define GPIO_INT_POLARITY	0x0038
#define GPIO_INT_STATUS		0x003c
#define GPIO_PORTS_EOI		0x0040
#define GPIO_INTMASK		0x0044
#define GPIO_DEBOUNCE		0x0048
#define GPIO_EXT_PORTA		0x0050

#define GPIO_NUM_PORTS		4
#define GPIO_NUM_PINS		32

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_memt, (sc)->sc_memh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_memt, (sc)->sc_memh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct dwgpio_intrhand {
	int (*ih_func)(void *);
	void *ih_arg;
	int ih_ipl;
};

struct dwgpio_softc {
	struct device		sc_dev;
	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_node;

	bus_space_tag_t		sc_memt;
	bus_space_handle_t	sc_memh;

	int			sc_nirq;
	void			**sc_ih;

	uint32_t		sc_npins;
	struct dwgpio_intrhand	*sc_pin_ih;

	struct acpi_gpio sc_gpio;
};

int	dwgpio_match(struct device *, void *, void *);
void	dwgpio_attach(struct device *, struct device *, void *);

const struct cfattach dwgpio_ca = {
	sizeof(struct dwgpio_softc), dwgpio_match, dwgpio_attach
};

struct cfdriver dwgpio_cd = {
	NULL, "dwgpio", DV_DULL
};

const char *dwgpio_hids[] = {
	"APMC0D81",
	NULL
};

int	dwgpio_found_hid(struct aml_node *, void *);
int	dwgpio_read_pin(void *, int);
void	dwgpio_write_pin(void *, int, int);
void	dwgpio_intr_establish(void *, int, int, int, int (*)(void *), void *);
int	dwgpio_intr(void *);

int
dwgpio_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	if (aaa->aaa_naddr < 1 || aaa->aaa_nirq < 1)
		return 0;
	return acpi_matchhids(aaa, dwgpio_hids, cf->cf_driver->cd_name);
}

void
dwgpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct dwgpio_softc *sc = (struct dwgpio_softc *)self;
	int i;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_node = aaa->aaa_node;
	printf(" %s", sc->sc_node->name);

	printf(" addr 0x%llx/0x%llx", aaa->aaa_addr[0], aaa->aaa_size[0]);

	sc->sc_memt = aaa->aaa_bst[0];
	if (bus_space_map(sc->sc_memt, aaa->aaa_addr[0], aaa->aaa_size[0],
	    0, &sc->sc_memh)) {
		printf(": can't map registers\n");
		return;
	}

	aml_find_node(sc->sc_node, "_HID", dwgpio_found_hid, sc);

	if (sc->sc_npins == 0) {
		printf(": no pins\n");
		return;
	}
	if (sc->sc_npins >= GPIO_NUM_PINS) {
		printf(": too many pins\n");
		return;
	}
	
	sc->sc_pin_ih = mallocarray(sc->sc_npins, sizeof(*sc->sc_pin_ih),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	printf(" irq");

	sc->sc_nirq = aaa->aaa_nirq;
	sc->sc_ih = mallocarray(sc->sc_nirq, sizeof(*sc->sc_ih),
	    M_DEVBUF, M_WAITOK | M_ZERO);
	for (i = 0; i < sc->sc_nirq; i++) {
		printf(" %d", aaa->aaa_irq[i]);

		sc->sc_ih[i] = acpi_intr_establish(aaa->aaa_irq[i],
		    aaa->aaa_irq_flags[i], IPL_BIO, dwgpio_intr,
		    sc, sc->sc_dev.dv_xname);
		if (sc->sc_ih[i] == NULL) {
			printf(": can't establish interrupt\n");
			goto unmap;
		}
	}

	sc->sc_gpio.cookie = sc;
	sc->sc_gpio.read_pin = dwgpio_read_pin;
	sc->sc_gpio.write_pin = dwgpio_write_pin;
	sc->sc_gpio.intr_establish = dwgpio_intr_establish;
	sc->sc_node->gpio = &sc->sc_gpio;

	printf(", %d pins\n", sc->sc_npins);

	acpi_register_gpio(sc->sc_acpi, sc->sc_node);
	return;

unmap:
	for (i = 0; i < sc->sc_nirq; i++) {
		if (sc->sc_ih[i])
			acpi_intr_disestablish(sc->sc_ih[i]);
	}
	free(sc->sc_ih, M_DEVBUF, sc->sc_nirq * sizeof(*sc->sc_ih));
	free(sc->sc_pin_ih, M_DEVBUF, sc->sc_npins * sizeof(*sc->sc_pin_ih));
	bus_space_unmap(sc->sc_memt, sc->sc_memh, aaa->aaa_size[0]);
}


int
dwgpio_found_hid(struct aml_node *child, void *arg)
{
	struct dwgpio_softc *sc = (struct dwgpio_softc *)arg;
	struct aml_node *node = child->parent;
	int reg;

	/* Skip our own _HID. */
	if (node == sc->sc_node)
		return 0;

	/* Only handle a single port for now. */
	reg = acpi_getpropint(node, "reg", -1);
	if (reg != 0)
		return 0;

	sc->sc_npins = acpi_getpropint(node, "snps,nr-gpios", GPIO_NUM_PINS);
	node->attached = 1;
	return 0;
}

int
dwgpio_read_pin(void *cookie, int pin)
{
	struct dwgpio_softc *sc = cookie;
	uint32_t reg;
	int val;

	if (pin < 0 || pin >= sc->sc_npins)
		return 0;

	reg = HREAD4(sc, GPIO_EXT_PORTA);
	val = (reg >> pin) & 1;
	return val;
}

void
dwgpio_write_pin(void *cookie, int pin, int val)
{
	struct dwgpio_softc *sc = cookie;

	if (pin < 0 || pin >= sc->sc_npins)
		return;

	if (val)
		HSET4(sc, GPIO_SWPORTA_DR, (1 << pin));
	else
		HCLR4(sc, GPIO_SWPORTA_DR, (1 << pin));
}

void
dwgpio_intr_establish(void *cookie, int pin, int flags, int level,
    int (*func)(void *), void *arg)
{
	struct dwgpio_softc *sc = cookie;

	if (pin < 0 || pin >= sc->sc_npins)
		return;

	sc->sc_pin_ih[pin].ih_func = func;
	sc->sc_pin_ih[pin].ih_arg = arg;
	sc->sc_pin_ih[pin].ih_ipl = level & ~IPL_WAKEUP;

	if ((flags & LR_GPIO_MODE) == LR_GPIO_EDGE)
		HSET4(sc, GPIO_INTTYPE_LEVEL, 1 << pin);
	else
		HCLR4(sc, GPIO_INTTYPE_LEVEL, 1 << pin);
	if ((flags & LR_GPIO_POLARITY) == LR_GPIO_ACTHI)
		HSET4(sc, GPIO_INT_POLARITY, 1 << pin);
	else
		HCLR4(sc, GPIO_INT_POLARITY, 1 << pin);

	HCLR4(sc, GPIO_SWPORTA_DDR, 1 << pin);
	HSET4(sc, GPIO_INTEN, 1 << pin);
	HCLR4(sc, GPIO_INTMASK, 1 << pin);
}

int
dwgpio_intr(void *arg)
{
	struct dwgpio_softc *sc = arg;
	uint32_t status;
	int pin, s, handled = 0;

	status = HREAD4(sc, GPIO_INT_STATUS);
	HWRITE4(sc, GPIO_PORTS_EOI, status);

	for (pin = 0; pin < sc->sc_npins; pin++) {
		if ((status & (1 << pin)) && sc->sc_pin_ih[pin].ih_func) {
			s = splraise(sc->sc_pin_ih[pin].ih_ipl);
			sc->sc_pin_ih[pin].ih_func(sc->sc_pin_ih[pin].ih_arg);
			splx(s);
			handled = 1;
		}
	}

	return handled;
}
