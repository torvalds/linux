/*	$OpenBSD: chvgpio.c,v 1.15 2025/06/16 15:44:35 kettenis Exp $	*/
/*
 * Copyright (c) 2016 Mark Kettenis
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

#define CHVGPIO_INTERRUPT_STATUS	0x0300
#define CHVGPIO_INTERRUPT_MASK		0x0380
#define CHVGPIO_PAD_CFG0		0x4400
#define CHVGPIO_PAD_CFG1		0x4404

#define CHVGPIO_PAD_CFG0_GPIORXSTATE		0x00000001
#define CHVGPIO_PAD_CFG0_GPIOTXSTATE		0x00000002
#define CHVGPIO_PAD_CFG0_INTSEL_MASK		0xf0000000
#define CHVGPIO_PAD_CFG0_INTSEL_SHIFT		28

#define CHVGPIO_PAD_CFG1_INTWAKECFG_MASK	0x00000007
#define CHVGPIO_PAD_CFG1_INTWAKECFG_FALLING	0x00000001
#define CHVGPIO_PAD_CFG1_INTWAKECFG_RISING	0x00000002
#define CHVGPIO_PAD_CFG1_INTWAKECFG_BOTH	0x00000003
#define CHVGPIO_PAD_CFG1_INTWAKECFG_LEVEL	0x00000004
#define CHVGPIO_PAD_CFG1_INVRXTX_MASK		0x000000f0
#define CHVGPIO_PAD_CFG1_INVRXTX_RXDATA		0x00000040

/* OEM defined RegionSpace. */
#define CHVGPIO_REGIONSPACE_BASE	0x90

struct chvgpio_intrhand {
	int (*ih_func)(void *);
	void *ih_arg;
	int ih_ipl;
};

struct chvgpio_softc {
	struct device sc_dev;
	struct acpi_softc *sc_acpi;
	struct aml_node *sc_node;

	bus_space_tag_t sc_memt;
	bus_space_handle_t sc_memh;
	bus_size_t sc_size;
	void *sc_ih;

	const int *sc_pins;
	int sc_npins;
	int sc_ngroups;

	struct chvgpio_intrhand sc_pin_ih[16];

	struct acpi_gpio sc_gpio;
};

static inline int
chvgpio_pad_cfg0_offset(int pin)
{
	return (CHVGPIO_PAD_CFG0 + 1024 * (pin / 15) + 8 * (pin % 15));
}

static inline int
chvgpio_read_pad_cfg0(struct chvgpio_softc *sc, int pin)
{
	return bus_space_read_4(sc->sc_memt, sc->sc_memh,
	    chvgpio_pad_cfg0_offset(pin));
}

static inline void
chvgpio_write_pad_cfg0(struct chvgpio_softc *sc, int pin, uint32_t val)
{
	bus_space_write_4(sc->sc_memt, sc->sc_memh,
	    chvgpio_pad_cfg0_offset(pin), val);
}

static inline int
chvgpio_read_pad_cfg1(struct chvgpio_softc *sc, int pin)
{
	return bus_space_read_4(sc->sc_memt, sc->sc_memh,
	    chvgpio_pad_cfg0_offset(pin) + 4);
}

static inline void
chvgpio_write_pad_cfg1(struct chvgpio_softc *sc, int pin, uint32_t val)
{
	bus_space_write_4(sc->sc_memt, sc->sc_memh,
	    chvgpio_pad_cfg0_offset(pin) + 4, val);
}

int	chvgpio_match(struct device *, void *, void *);
void	chvgpio_attach(struct device *, struct device *, void *);

const struct cfattach chvgpio_ca = {
	sizeof(struct chvgpio_softc), chvgpio_match, chvgpio_attach
};

struct cfdriver chvgpio_cd = {
	NULL, "chvgpio", DV_DULL
};

const char *chvgpio_hids[] = {
	"INT33FF",
	NULL
};

/*
 * The pads for the pins are arranged in groups of maximal 15 pins.
 * The arrays below give the number of pins per group, such that we
 * can validate the (untrusted) pin numbers from ACPI.
 */

const int chv_southwest_pins[] = {
	8, 8, 8, 8, 8, 8, 8, -1
};

const int chv_north_pins[] = {
	9, 13, 12, 12, 13, -1
};

const int chv_east_pins[] = {
	12, 12, -1
};

const int chv_southeast_pins[] = {
	8, 12, 6, 8, 10, 11, -1
};

int	chvgpio_check_pin(struct chvgpio_softc *, int);
int	chvgpio_read_pin(void *, int);
void	chvgpio_write_pin(void *, int, int);
void	chvgpio_intr_establish(void *, int, int, int, int (*)(void *), void *);
void	chvgpio_intr_enable(void *, int);
void	chvgpio_intr_disable(void *, int);
int	chvgpio_intr(void *);
int	chvgpio_opreg_handler(void *, int, uint64_t, int, uint64_t *);

int
chvgpio_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	if (aaa->aaa_naddr < 1 || aaa->aaa_nirq < 1)
		return 0;
	return acpi_matchhids(aaa, chvgpio_hids, cf->cf_driver->cd_name);
}

void
chvgpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct chvgpio_softc *sc = (struct chvgpio_softc *)self;
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
		sc->sc_pins = chv_southwest_pins;
		break;
	case 2:
		sc->sc_pins = chv_north_pins;
		break;
	case 3:
		sc->sc_pins = chv_east_pins;
		break;
	case 4:
		sc->sc_pins = chv_southeast_pins;
		break;
	default:
		printf("\n");
		return;
	}

	for (i = 0; sc->sc_pins[i] >= 0; i++) {
		sc->sc_npins += sc->sc_pins[i];
		sc->sc_ngroups++;
	}

	printf(" addr 0x%llx/0x%llx", aaa->aaa_addr[0], aaa->aaa_size[0]);
	printf(" irq %d", aaa->aaa_irq[0]);

	sc->sc_memt = aaa->aaa_bst[0];
	sc->sc_size = aaa->aaa_size[0];
	if (bus_space_map(sc->sc_memt, aaa->aaa_addr[0], aaa->aaa_size[0],
	    0, &sc->sc_memh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_ih = acpi_intr_establish(aaa->aaa_irq[0], aaa->aaa_irq_flags[0],
	    IPL_BIO, chvgpio_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto unmap;
	}

	sc->sc_gpio.cookie = sc;
	sc->sc_gpio.read_pin = chvgpio_read_pin;
	sc->sc_gpio.write_pin = chvgpio_write_pin;
	sc->sc_gpio.intr_establish = chvgpio_intr_establish;
	sc->sc_gpio.intr_enable = chvgpio_intr_enable;
	sc->sc_gpio.intr_disable = chvgpio_intr_disable;
	sc->sc_node->gpio = &sc->sc_gpio;

	/* Mask and ack all interrupts. */
	bus_space_write_4(sc->sc_memt, sc->sc_memh,
	    CHVGPIO_INTERRUPT_MASK, 0);
	bus_space_write_4(sc->sc_memt, sc->sc_memh,
	    CHVGPIO_INTERRUPT_STATUS, 0xffff);

	printf(", %d pins\n", sc->sc_npins);

	/* Register OEM defined address space. */
	aml_register_regionspace(sc->sc_node, CHVGPIO_REGIONSPACE_BASE + uid,
	    sc, chvgpio_opreg_handler);

	acpi_register_gpio(sc->sc_acpi, sc->sc_node);
	return;

unmap:
	bus_space_unmap(sc->sc_memt, sc->sc_memh, sc->sc_size);
}

int
chvgpio_check_pin(struct chvgpio_softc *sc, int pin)
{
	if (pin < 0)
		return EINVAL;
	if ((pin / 15) >= sc->sc_ngroups)
		return EINVAL;
	if ((pin % 15) >= sc->sc_pins[pin / 15])
		return EINVAL;

	return 0;
}

int
chvgpio_read_pin(void *cookie, int pin)
{
	struct chvgpio_softc *sc = cookie;
	uint32_t reg;

	KASSERT(chvgpio_check_pin(sc, pin) == 0);

	reg = chvgpio_read_pad_cfg0(sc, pin);
	return (reg & CHVGPIO_PAD_CFG0_GPIORXSTATE);
}

void
chvgpio_write_pin(void *cookie, int pin, int value)
{
	struct chvgpio_softc *sc = cookie;
	uint32_t reg;

	KASSERT(chvgpio_check_pin(sc, pin) == 0);

	reg = chvgpio_read_pad_cfg0(sc, pin);
	if (value)
		reg |= CHVGPIO_PAD_CFG0_GPIOTXSTATE;
	else
		reg &= ~CHVGPIO_PAD_CFG0_GPIOTXSTATE;
	chvgpio_write_pad_cfg0(sc, pin, reg);
}

void
chvgpio_intr_establish(void *cookie, int pin, int flags, int level,
    int (*func)(void *), void *arg)
{
	struct chvgpio_softc *sc = cookie;
	uint32_t reg;
	int line;

	KASSERT(chvgpio_check_pin(sc, pin) == 0);

	reg = chvgpio_read_pad_cfg0(sc, pin);
	reg &= CHVGPIO_PAD_CFG0_INTSEL_MASK;
	line = reg >> CHVGPIO_PAD_CFG0_INTSEL_SHIFT;

	sc->sc_pin_ih[line].ih_func = func;
	sc->sc_pin_ih[line].ih_arg = arg;
	sc->sc_pin_ih[line].ih_ipl = level & ~IPL_WAKEUP;

	reg = chvgpio_read_pad_cfg1(sc, pin);
	reg &= ~CHVGPIO_PAD_CFG1_INTWAKECFG_MASK;
	reg &= ~CHVGPIO_PAD_CFG1_INVRXTX_MASK;
	switch (flags & (LR_GPIO_MODE | LR_GPIO_POLARITY)) {
	case LR_GPIO_LEVEL | LR_GPIO_ACTLO:
		reg |= CHVGPIO_PAD_CFG1_INVRXTX_RXDATA;
		/* FALLTHROUGH */
	case LR_GPIO_LEVEL | LR_GPIO_ACTHI:
		reg |= CHVGPIO_PAD_CFG1_INTWAKECFG_LEVEL;
		break;
	case LR_GPIO_EDGE | LR_GPIO_ACTLO:
		reg |= CHVGPIO_PAD_CFG1_INTWAKECFG_FALLING;
		break;
	case LR_GPIO_EDGE | LR_GPIO_ACTHI:
		reg |= CHVGPIO_PAD_CFG1_INTWAKECFG_RISING;
		break;
	case LR_GPIO_EDGE | LR_GPIO_ACTBOTH:
		reg |= CHVGPIO_PAD_CFG1_INTWAKECFG_BOTH;
		break;
	default:
		printf("%s: unsupported interrupt mode/polarity\n",
		    sc->sc_dev.dv_xname);
		break;
	}
	chvgpio_write_pad_cfg1(sc, pin, reg);

	reg = bus_space_read_4(sc->sc_memt, sc->sc_memh,
	    CHVGPIO_INTERRUPT_MASK);
	bus_space_write_4(sc->sc_memt, sc->sc_memh,
	    CHVGPIO_INTERRUPT_MASK, reg | (1 << line));
}

void
chvgpio_intr_enable(void *cookie, int pin)
{
	struct chvgpio_softc *sc = cookie;
	uint32_t reg;
	int line;

	KASSERT(chvgpio_check_pin(sc, pin) == 0);

	reg = chvgpio_read_pad_cfg0(sc, pin);
	reg &= CHVGPIO_PAD_CFG0_INTSEL_MASK;
	line = reg >> CHVGPIO_PAD_CFG0_INTSEL_SHIFT;

	reg = bus_space_read_4(sc->sc_memt, sc->sc_memh,
	    CHVGPIO_INTERRUPT_MASK);
	bus_space_write_4(sc->sc_memt, sc->sc_memh,
	    CHVGPIO_INTERRUPT_MASK, reg | (1 << line));
}	

void
chvgpio_intr_disable(void *cookie, int pin)
{
	struct chvgpio_softc *sc = cookie;
	uint32_t reg;
	int line;

	KASSERT(chvgpio_check_pin(sc, pin) == 0);

	reg = chvgpio_read_pad_cfg0(sc, pin);
	reg &= CHVGPIO_PAD_CFG0_INTSEL_MASK;
	line = reg >> CHVGPIO_PAD_CFG0_INTSEL_SHIFT;

	reg = bus_space_read_4(sc->sc_memt, sc->sc_memh,
	    CHVGPIO_INTERRUPT_MASK);
	bus_space_write_4(sc->sc_memt, sc->sc_memh,
	    CHVGPIO_INTERRUPT_MASK, reg & ~(1 << line));
}	

int
chvgpio_intr(void *arg)
{
	struct chvgpio_softc *sc = arg;
	uint32_t reg;
	int rc = 0;
	int line, s;

	reg = bus_space_read_4(sc->sc_memt, sc->sc_memh,
	    CHVGPIO_INTERRUPT_STATUS);
	for (line = 0; line < 16; line++) {
		if ((reg & (1 << line)) == 0)
			continue;

		bus_space_write_4(sc->sc_memt,sc->sc_memh,
		    CHVGPIO_INTERRUPT_STATUS, 1 << line);
		if (sc->sc_pin_ih[line].ih_func) {
			s = splraise(sc->sc_pin_ih[line].ih_ipl);
			sc->sc_pin_ih[line].ih_func(sc->sc_pin_ih[line].ih_arg);
			splx(s);
		}
		rc = 1;
	}

	return rc;
}

int
chvgpio_opreg_handler(void *cookie, int iodir, uint64_t address, int size,
    uint64_t *value)
{
	struct chvgpio_softc *sc = cookie;

	/* Only allow 32-bit access. */
	if (size != 4 || address > sc->sc_size - size)
		return -1;

	if (iodir == ACPI_IOREAD)
		*value = bus_space_read_4(sc->sc_memt, sc->sc_memh, address);
	else
		bus_space_write_4(sc->sc_memt, sc->sc_memh, address, *value);

	return 0;
}
