/*	$OpenBSD: amdgpio.c,v 1.12 2025/06/16 15:44:35 kettenis Exp $	*/
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

#define AMDGPIO_CONF_LEVEL		0x00000100
#define AMDGPIO_CONF_ACTLO		0x00000200
#define AMDGPIO_CONF_ACTBOTH		0x00000400
#define AMDGPIO_CONF_MASK		0x00000600
#define AMDGPIO_CONF_INT_EN		0x00000800
#define AMDGPIO_CONF_INT_MASK		0x00001000
#define AMDGPIO_CONF_RXSTATE		0x00010000
#define AMDGPIO_CONF_TXSTATE		0x00400000
#define AMDGPIO_CONF_TXSTATE_EN		0x00800000
#define AMDGPIO_CONF_INT_STS		0x10000000
#define AMDGPIO_IRQ_MASTER_EOI		0x20000000
#define AMDGPIO_IRQ_BITS		46
#define AMDGPIO_IRQ_PINS		4

#define AMDGPIO_IRQ_MASTER		0xfc
#define AMDGPIO_IRQ_STS			0x2f8

struct amdgpio_intrhand {
	int (*ih_func)(void *);
	void *ih_arg;
	int ih_ipl;
	int ih_wakeup;
};

struct amdgpio_pincfg {
	/* Modeled after pchgpio but we only have one value to save/restore */
	uint32_t	pin_cfg;
};

struct amdgpio_softc {
	struct device sc_dev;
	struct acpi_softc *sc_acpi;
	struct aml_node *sc_node;

	bus_space_tag_t sc_memt;
	bus_space_handle_t sc_memh;
	int sc_irq_flags;
	void *sc_ih;

	int sc_npins;
	struct amdgpio_pincfg *sc_pin_cfg;
	struct amdgpio_intrhand *sc_pin_ih;

	struct acpi_gpio sc_gpio;
};

int	amdgpio_match(struct device *, void *, void *);
void	amdgpio_attach(struct device *, struct device *, void *);
int	amdgpio_activate(struct device *, int);

const struct cfattach amdgpio_ca = {
	sizeof(struct amdgpio_softc), amdgpio_match, amdgpio_attach,
	NULL, amdgpio_activate
};

struct cfdriver amdgpio_cd = {
	NULL, "amdgpio", DV_DULL
};

const char *amdgpio_hids[] = {
	"AMDI0030",
	"AMD0030",
	NULL
};

int	amdgpio_read_pin(void *, int);
void	amdgpio_write_pin(void *, int, int);
void	amdgpio_intr_establish(void *, int, int, int, int (*)(void *), void *);
void	amdgpio_intr_enable(void *, int);
void	amdgpio_intr_disable(void *, int);
int	amdgpio_pin_intr(struct amdgpio_softc *, int);
int	amdgpio_intr(void *);
void	amdgpio_save_pin(struct amdgpio_softc *, int pin);
void	amdgpio_save(struct amdgpio_softc *);
void	amdgpio_restore_pin(struct amdgpio_softc *, int pin);
void	amdgpio_restore(struct amdgpio_softc *);

int
amdgpio_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	if (aaa->aaa_naddr < 1 || aaa->aaa_nirq < 1)
		return 0;
	return acpi_matchhids(aaa, amdgpio_hids, cf->cf_driver->cd_name);
}

void
amdgpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct amdgpio_softc *sc = (struct amdgpio_softc *)self;
	int64_t uid;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_node = aaa->aaa_node;
	printf(" %s", sc->sc_node->name);

	if (aml_evalinteger(sc->sc_acpi, sc->sc_node, "_UID", 0, NULL, &uid)) {
		printf(": can't find uid\n");
		return;
	}

	printf(" uid %lld", uid);

	switch (uid) {
	case 0:
		sc->sc_npins = 184;
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

	sc->sc_pin_cfg = mallocarray(sc->sc_npins, sizeof(*sc->sc_pin_cfg),
	    M_DEVBUF, M_WAITOK);
	sc->sc_pin_ih = mallocarray(sc->sc_npins, sizeof(*sc->sc_pin_ih),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	sc->sc_ih = acpi_intr_establish(aaa->aaa_irq[0], aaa->aaa_irq_flags[0],
	    IPL_BIO, amdgpio_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto unmap;
	}

	sc->sc_gpio.cookie = sc;
	sc->sc_gpio.read_pin = amdgpio_read_pin;
	sc->sc_gpio.write_pin = amdgpio_write_pin;
	sc->sc_gpio.intr_establish = amdgpio_intr_establish;
	sc->sc_gpio.intr_enable = amdgpio_intr_enable;
	sc->sc_gpio.intr_disable = amdgpio_intr_disable;
	sc->sc_node->gpio = &sc->sc_gpio;

	printf(", %d pins\n", sc->sc_npins);

	acpi_register_gpio(sc->sc_acpi, sc->sc_node);
	return;

unmap:
	free(sc->sc_pin_ih, M_DEVBUF, sc->sc_npins * sizeof(*sc->sc_pin_ih));
	bus_space_unmap(sc->sc_memt, sc->sc_memh, aaa->aaa_size[0]);
}

int
amdgpio_activate(struct device *self, int act)
{
	struct amdgpio_softc *sc = (struct amdgpio_softc *)self;

	switch (act) {
	case DVACT_SUSPEND:
		amdgpio_save(sc);
		break;
	case DVACT_RESUME:
		amdgpio_restore(sc);
		break;
	}

	return 0;
}

void
amdgpio_save_pin(struct amdgpio_softc *sc, int pin)
{
	sc->sc_pin_cfg[pin].pin_cfg = bus_space_read_4(sc->sc_memt, sc->sc_memh,
	    pin * 4);
}

void
amdgpio_save(struct amdgpio_softc *sc)
{
	int pin;

	for (pin = 0; pin < sc->sc_npins; pin++)
		amdgpio_save_pin(sc, pin);
}

void
amdgpio_restore_pin(struct amdgpio_softc *sc, int pin)
{
	if (!sc->sc_pin_ih[pin].ih_func)
		return;

	bus_space_write_4(sc->sc_memt, sc->sc_memh, pin * 4,
	    sc->sc_pin_cfg[pin].pin_cfg);
}

void
amdgpio_restore(struct amdgpio_softc *sc)
{
	int pin;

	for (pin = 0; pin < sc->sc_npins; pin++)
		amdgpio_restore_pin(sc, pin);
}

int
amdgpio_read_pin(void *cookie, int pin)
{
	struct amdgpio_softc *sc = cookie;
	uint32_t reg;

	reg = bus_space_read_4(sc->sc_memt, sc->sc_memh, pin * 4);

	return !!(reg & AMDGPIO_CONF_RXSTATE);
}

void
amdgpio_write_pin(void *cookie, int pin, int value)
{
	struct amdgpio_softc *sc = cookie;
	uint32_t reg;

	reg = bus_space_read_4(sc->sc_memt, sc->sc_memh, pin * 4);
	reg |= AMDGPIO_CONF_TXSTATE_EN;
	if (value)
		reg |= AMDGPIO_CONF_TXSTATE;
	else
		reg &= ~AMDGPIO_CONF_TXSTATE;
	bus_space_write_4(sc->sc_memt, sc->sc_memh, pin * 4, reg);
}

void
amdgpio_intr_establish(void *cookie, int pin, int flags, int level,
    int (*func)(void *), void *arg)
{
	struct amdgpio_softc *sc = cookie;
	uint32_t reg;

	KASSERT(pin >= 0 && pin != 63 && pin < sc->sc_npins);

	sc->sc_pin_ih[pin].ih_func = func;
	sc->sc_pin_ih[pin].ih_arg = arg;
	sc->sc_pin_ih[pin].ih_ipl = level & ~IPL_WAKEUP;
	sc->sc_pin_ih[pin].ih_wakeup = level & IPL_WAKEUP;

	reg = bus_space_read_4(sc->sc_memt, sc->sc_memh, pin * 4);
	reg &= ~(AMDGPIO_CONF_MASK | AMDGPIO_CONF_LEVEL |
	    AMDGPIO_CONF_TXSTATE_EN);
	if ((flags & LR_GPIO_MODE) == 0)
		reg |= AMDGPIO_CONF_LEVEL;
	if ((flags & LR_GPIO_POLARITY) == LR_GPIO_ACTLO)
		reg |= AMDGPIO_CONF_ACTLO;
	if ((flags & LR_GPIO_POLARITY) == LR_GPIO_ACTBOTH)
		reg |= AMDGPIO_CONF_ACTBOTH;
	reg |= (AMDGPIO_CONF_INT_MASK | AMDGPIO_CONF_INT_EN);
	bus_space_write_4(sc->sc_memt, sc->sc_memh, pin * 4, reg);

	if (sc->sc_pin_ih[pin].ih_wakeup)
		intr_set_wakeup(sc->sc_ih);
}

void
amdgpio_intr_enable(void *cookie, int pin)
{
	struct amdgpio_softc *sc = cookie;
	uint32_t reg;

	KASSERT(pin >= 0 && pin != 63 && pin < sc->sc_npins);

	reg = bus_space_read_4(sc->sc_memt, sc->sc_memh, pin * 4);
	reg |= (AMDGPIO_CONF_INT_MASK | AMDGPIO_CONF_INT_EN);
	bus_space_write_4(sc->sc_memt, sc->sc_memh, pin * 4, reg);
}

void
amdgpio_intr_disable(void *cookie, int pin)
{
	struct amdgpio_softc *sc = cookie;
	uint32_t reg;

	KASSERT(pin >= 0 && pin != 63 && pin < sc->sc_npins);

	reg = bus_space_read_4(sc->sc_memt, sc->sc_memh, pin * 4);
	reg &= ~(AMDGPIO_CONF_INT_MASK | AMDGPIO_CONF_INT_EN);
	bus_space_write_4(sc->sc_memt, sc->sc_memh, pin * 4, reg);
}

int
amdgpio_pin_intr(struct amdgpio_softc *sc, int pin)
{
	uint32_t reg;
	int s, rc = 0;

	reg = bus_space_read_4(sc->sc_memt, sc->sc_memh, pin * 4);
	if (reg & AMDGPIO_CONF_INT_STS) {
		if (sc->sc_pin_ih[pin].ih_func) {
			s = splraise(sc->sc_pin_ih[pin].ih_ipl);
			sc->sc_pin_ih[pin].ih_func(sc->sc_pin_ih[pin].ih_arg);
			splx(s);

			/* Clear interrupt */
			reg = bus_space_read_4(sc->sc_memt, sc->sc_memh,
			    pin * 4);
			bus_space_write_4(sc->sc_memt, sc->sc_memh,
			    pin * 4, reg);
			rc = 1;
		} else {
			/* Mask unhandled interrupt */
			reg &= ~(AMDGPIO_CONF_INT_MASK | AMDGPIO_CONF_INT_EN);
			bus_space_write_4(sc->sc_memt, sc->sc_memh,
			    pin * 4, reg);
		}
	}

	return rc;
}

int
amdgpio_intr(void *arg)
{
	struct amdgpio_softc *sc = arg;
	uint64_t status;
	uint32_t reg;
	int rc = 0, pin = 0;
	int i, j;

	status = bus_space_read_4(sc->sc_memt, sc->sc_memh,
	    AMDGPIO_IRQ_STS + 4);
	status <<= 32;
	status |= bus_space_read_4(sc->sc_memt, sc->sc_memh,
	    AMDGPIO_IRQ_STS);

	/* One status bit for every four pins */
	for (i = 0; i < AMDGPIO_IRQ_BITS; i++, pin += 4) {
		if (status & (1ULL << i)) {
			for (j = 0; j < AMDGPIO_IRQ_PINS; j++) {
				if (amdgpio_pin_intr(sc, pin + j))
					rc = 1;
			}
		}
	}

	/* Signal end of interrupt */
	reg = bus_space_read_4(sc->sc_memt, sc->sc_memh,
	    AMDGPIO_IRQ_MASTER);
	reg |= AMDGPIO_IRQ_MASTER_EOI;
	bus_space_write_4(sc->sc_memt, sc->sc_memh,
	    AMDGPIO_IRQ_MASTER, reg);

	return rc;
}
