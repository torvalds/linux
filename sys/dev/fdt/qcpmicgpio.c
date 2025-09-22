/*	$OpenBSD: qcpmicgpio.c,v 1.4 2025/07/06 11:07:09 kettenis Exp $	*/
/*
 * Copyright (c) 2022 Patrick Wildt <patrick@blueri.se>
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

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/fdt/spmivar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/fdt.h>

/* Registers. */
#define GPIO_TYPE		0x04
#define  GPIO_TYPE_VAL			0x10
#define GPIO_SUBTYPE		0x05
#define  GPIO_SUBTYPE_GPIO_4CH		0x1
#define  GPIO_SUBTYPE_GPIOC_4CH		0x5
#define  GPIO_SUBTYPE_GPIO_8CH		0x9
#define  GPIO_SUBTYPE_GPIOC_8CH		0xd
#define  GPIO_SUBTYPE_GPIO_LV		0x10
#define  GPIO_SUBTYPE_GPIO_MV		0x11
#define  GPIO_SUBTYPE_GPIO_LV_VIN2	0x12
#define  GPIO_SUBTYPE_GPIO_MV_VIN3	0x13
#define GPIO_PIN_OFF(x)		(0x100 * (x))
#define GPIO_PIN_STATUS		0x10
#define  GPIO_PIN_STATUS_ON		(1 << 0)
#define GPIO_PIN_MODE		0x40
#define  GPIO_PIN_MODE_VALUE		(1 << 0)
#define  GPIO_PIN_MODE_DIR_SHIFT	4
#define  GPIO_PIN_MODE_DIR_MASK		0x7
#define  GPIO_PIN_MODE_DIR_LVMV_SHIFT	0
#define  GPIO_PIN_MODE_DIR_LVMV_MASK	0x3
#define  GPIO_PIN_MODE_DIR_DIGITAL_IN	0
#define  GPIO_PIN_MODE_DIR_DIGITAL_OUT	1
#define  GPIO_PIN_MODE_DIR_DIGITAL_IO	2
#define  GPIO_PIN_MODE_DIR_ANALOG_PT	3
#define GPIO_PIN_LVMV_DOUT_CTL	0x44
#define  GPIO_PIN_LVMV_DOUT_CTL_INVERT	(1U << 7)

struct qcpmicgpio_softc {
	struct device		sc_dev;
	int			sc_node;

	spmi_tag_t		sc_tag;
	int8_t			sc_sid;
	uint16_t		sc_addr;

	int			*sc_is_lv_mv;

	int			sc_npins;
	struct gpio_controller	sc_gc;
};

int	qcpmicgpio_match(struct device *, void *, void *);
void	qcpmicgpio_attach(struct device *, struct device *, void *);

const struct cfattach qcpmicgpio_ca = {
	sizeof(struct qcpmicgpio_softc), qcpmicgpio_match, qcpmicgpio_attach
};

struct cfdriver qcpmicgpio_cd = {
	NULL, "qcpmicgpio", DV_DULL
};

uint8_t	qcpmicgpio_read(struct qcpmicgpio_softc *, uint16_t);
void	qcpmicgpio_write(struct qcpmicgpio_softc *, uint16_t, uint8_t);

void	qcpmicgpio_config_pin(void *, uint32_t *, int);
int	qcpmicgpio_get_pin(void *, uint32_t *);
void	qcpmicgpio_set_pin(void *, uint32_t *, int);

int
qcpmicgpio_match(struct device *parent, void *match, void *aux)
{
	struct spmi_attach_args *saa = aux;

	return OF_is_compatible(saa->sa_node, "qcom,spmi-gpio");
}

void
qcpmicgpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct spmi_attach_args *saa = aux;
	struct qcpmicgpio_softc *sc = (struct qcpmicgpio_softc *)self;
	uint8_t reg;
	int pin;

	sc->sc_addr = OF_getpropint(saa->sa_node, "reg", -1);
	if (sc->sc_addr < 0) {
		printf(": can't find registers\n");
		return;
	}

	sc->sc_node = saa->sa_node;
	sc->sc_tag = saa->sa_tag;
	sc->sc_sid = saa->sa_sid;

	if (OF_is_compatible(saa->sa_node, "qcom,pm8350-gpio"))
		sc->sc_npins = 10;
	if (OF_is_compatible(saa->sa_node, "qcom,pm8350c-gpio"))
		sc->sc_npins = 9;
	if (OF_is_compatible(saa->sa_node, "qcom,pmc8380-gpio"))
		sc->sc_npins = 10;
	if (OF_is_compatible(saa->sa_node, "qcom,pmr735a-gpio"))
		sc->sc_npins = 4;

	if (!sc->sc_npins) {
		printf(": no pins\n");
		return;
	}

	printf("\n");

	sc->sc_is_lv_mv = mallocarray(sc->sc_npins, sizeof(int),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	for (pin = 0; pin < sc->sc_npins; pin++) {
		reg = qcpmicgpio_read(sc, GPIO_PIN_OFF(pin) + GPIO_TYPE);
		if (reg != GPIO_TYPE_VAL)
			continue;

		reg = qcpmicgpio_read(sc, GPIO_PIN_OFF(pin) + GPIO_SUBTYPE);
		switch (reg) {
		case GPIO_SUBTYPE_GPIO_4CH:
		case GPIO_SUBTYPE_GPIOC_4CH:
		case GPIO_SUBTYPE_GPIO_8CH:
		case GPIO_SUBTYPE_GPIOC_8CH:
			break;
		case GPIO_SUBTYPE_GPIO_LV:
		case GPIO_SUBTYPE_GPIO_MV:
		case GPIO_SUBTYPE_GPIO_LV_VIN2:
		case GPIO_SUBTYPE_GPIO_MV_VIN3:
			sc->sc_is_lv_mv[pin] = 1;
			break;
		default:
			printf("%s: unknown pin subtype 0x%02x for pin %d\n",
			    sc->sc_dev.dv_xname, reg, pin);
			break;
		}
	}

	sc->sc_gc.gc_node = saa->sa_node;
	sc->sc_gc.gc_cookie = sc;
	sc->sc_gc.gc_config_pin = qcpmicgpio_config_pin;
	sc->sc_gc.gc_get_pin = qcpmicgpio_get_pin;
	sc->sc_gc.gc_set_pin = qcpmicgpio_set_pin;
	gpio_controller_register(&sc->sc_gc);
}

void
qcpmicgpio_config_pin(void *cookie, uint32_t *cells, int config)
{
	struct qcpmicgpio_softc *sc = cookie;
	uint32_t pin = cells[0] - 1;
	uint8_t reg;
	int val;

	if (pin >= sc->sc_npins)
		return;

	if (config & GPIO_CONFIG_OUTPUT)
		val = GPIO_PIN_MODE_DIR_DIGITAL_OUT;
	else
		val = GPIO_PIN_MODE_DIR_DIGITAL_IN;

	if (sc->sc_is_lv_mv[pin]) {
		qcpmicgpio_write(sc, GPIO_PIN_OFF(pin) + GPIO_PIN_MODE, val);
	} else {
		reg = qcpmicgpio_read(sc, GPIO_PIN_OFF(pin) + GPIO_PIN_MODE);
		reg &= ~(GPIO_PIN_MODE_DIR_MASK << GPIO_PIN_MODE_DIR_SHIFT);
		reg |= val << GPIO_PIN_MODE_DIR_SHIFT;
		qcpmicgpio_write(sc, GPIO_PIN_OFF(pin) + GPIO_PIN_MODE, reg);
	}
}

int
qcpmicgpio_get_pin(void *cookie, uint32_t *cells)
{
	struct qcpmicgpio_softc *sc = cookie;
	uint32_t pin = cells[0] - 1;
	uint32_t flags = cells[1];
	uint8_t reg;
	int val = 0;

	if (pin >= sc->sc_npins)
		return 0;

	reg = qcpmicgpio_read(sc, GPIO_PIN_OFF(pin) + GPIO_PIN_MODE);
	if (sc->sc_is_lv_mv[pin]) {
		reg >>= GPIO_PIN_MODE_DIR_LVMV_SHIFT;
		reg &= GPIO_PIN_MODE_DIR_LVMV_MASK;
	} else {
		reg >>= GPIO_PIN_MODE_DIR_SHIFT;
		reg &= GPIO_PIN_MODE_DIR_MASK;
	}

	if (reg == GPIO_PIN_MODE_DIR_DIGITAL_IN ||
	    reg == GPIO_PIN_MODE_DIR_DIGITAL_IO) {
		reg = qcpmicgpio_read(sc, GPIO_PIN_OFF(pin) + GPIO_PIN_STATUS);
		val = !!(reg & GPIO_PIN_STATUS_ON);
	}

	if (reg == GPIO_PIN_MODE_DIR_DIGITAL_OUT) {
		if (sc->sc_is_lv_mv[pin]) {
			reg = qcpmicgpio_read(sc, GPIO_PIN_OFF(pin) +
			    GPIO_PIN_LVMV_DOUT_CTL);
			val = !!(reg & GPIO_PIN_LVMV_DOUT_CTL_INVERT);
		} else {
			reg = qcpmicgpio_read(sc, GPIO_PIN_OFF(pin) +
			    GPIO_PIN_MODE);
			val = !!(reg & GPIO_PIN_MODE_VALUE);
		}
	}

	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	return val;
}

void
qcpmicgpio_set_pin(void *cookie, uint32_t *cells, int val)
{
	struct qcpmicgpio_softc *sc = cookie;
	uint32_t pin = cells[0] - 1;
	uint32_t flags = cells[1];
	uint8_t reg;

	if (pin >= sc->sc_npins)
		return;

	if (flags & GPIO_ACTIVE_LOW)
		val = !val;

	if (sc->sc_is_lv_mv[pin]) {
		reg = qcpmicgpio_read(sc, GPIO_PIN_OFF(pin) +
		    GPIO_PIN_LVMV_DOUT_CTL);
		reg &= ~GPIO_PIN_LVMV_DOUT_CTL_INVERT;
		if (val)
			reg |= GPIO_PIN_LVMV_DOUT_CTL_INVERT;
		qcpmicgpio_write(sc, GPIO_PIN_OFF(pin) +
		    GPIO_PIN_LVMV_DOUT_CTL, reg);
	} else {
		reg = qcpmicgpio_read(sc, GPIO_PIN_OFF(pin) + GPIO_PIN_MODE);
		reg &= ~GPIO_PIN_MODE_VALUE;
		if (val)
			reg |= GPIO_PIN_MODE_VALUE;
		qcpmicgpio_write(sc, GPIO_PIN_OFF(pin) + GPIO_PIN_MODE, reg);
	}
}

uint8_t
qcpmicgpio_read(struct qcpmicgpio_softc *sc, uint16_t reg)
{
	uint8_t val = 0;
	int error;

	error = spmi_cmd_read(sc->sc_tag, sc->sc_sid, SPMI_CMD_EXT_READL,
	    sc->sc_addr + reg, &val, sizeof(val));
	if (error)
		printf("%s: error reading\n", sc->sc_dev.dv_xname);

	return val;
}

void
qcpmicgpio_write(struct qcpmicgpio_softc *sc, uint16_t reg, uint8_t val)
{
	int error;

	error = spmi_cmd_write(sc->sc_tag, sc->sc_sid, SPMI_CMD_EXT_WRITEL,
	    sc->sc_addr + reg, &val, sizeof(val));
	if (error)
		printf("%s: error writing\n", sc->sc_dev.dv_xname);
}
