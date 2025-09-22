/*	$OpenBSD: pca9554.c,v 1.19 2022/04/06 18:59:28 naddy Exp $	*/

/*
 * Copyright (c) 2005 Theo de Raadt
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
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/i2c/i2cvar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>

/* Philips 9554/6/7 registers */
#define PCA9554_IN		0x00
#define PCA9554_OUT		0x01
#define PCA9554_POLARITY	0x02
#define PCA9554_CONFIG		0x03

/* Philips 9555 registers */
#define PCA9555_IN0		0x00
#define PCA9555_IN1		0x01
#define PCA9555_OUT0		0x02
#define PCA9555_OUT1		0x03
#define PCA9555_POLARITY0	0x04
#define PCA9555_POLARITY1	0x05
#define PCA9555_CONFIG0		0x06
#define PCA9555_CONFIG1		0x07

/* Sensors */
#define PCAGPIO_NPINS	16

#define PCAGPIO_NPORTS	2
#define PCAGPIO_PORT(_pin)	((_pin) > 7 ? 1 : 0)
#define PCAGPIO_BIT(_pin)	(1 << ((_pin) % 8))

/* Register mapping index */
enum pcigpio_cmd {
	PCAGPIO_IN		= 0,
	PCAGPIO_OUT,
	PCAGPIO_POLARITY,
	PCAGPIO_CONFIG,
	PCAGPIO_MAX
};

struct pcagpio_softc {
	struct device	sc_dev;
	i2c_tag_t	sc_tag;
	i2c_addr_t	sc_addr;
	int		sc_node;

	u_int8_t	sc_npins;
	u_int8_t	sc_regs[PCAGPIO_NPORTS][PCAGPIO_MAX];

	struct gpio_controller sc_gc;
};

int	pcagpio_match(struct device *, void *, void *);
void	pcagpio_attach(struct device *, struct device *, void *);
int	pcagpio_init(struct pcagpio_softc *, int);

void	pcagpio_config_pin(void *, uint32_t *, int);
int	pcagpio_get_pin(void *, uint32_t *);
void	pcagpio_set_pin(void *, uint32_t *, int);

const struct cfattach pcagpio_ca = {
	sizeof(struct pcagpio_softc), pcagpio_match, pcagpio_attach
};

struct cfdriver pcagpio_cd = {
	NULL, "pcagpio", DV_DULL
};

int
pcagpio_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "nxp,pca9554") == 0 ||
	    strcmp(ia->ia_name, "nxp,pca9555") == 0 ||
	    strcmp(ia->ia_name, "nxp,pca9557") == 0)
		return (1);
	return (0);
}

void
pcagpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct pcagpio_softc *sc = (struct pcagpio_softc *)self;
	struct i2c_attach_args *ia = aux;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;
	sc->sc_node = *(int *)ia->ia_cookie;

	if (strcmp(ia->ia_name, "nxp,pca9555") == 0) {
		/* The pca9555 has two 8 bit ports */
		sc->sc_regs[0][PCAGPIO_IN] = PCA9555_IN0;
		sc->sc_regs[0][PCAGPIO_OUT] = PCA9555_OUT0;
		sc->sc_regs[0][PCAGPIO_POLARITY] = PCA9555_POLARITY0;
		sc->sc_regs[0][PCAGPIO_CONFIG] = PCA9555_CONFIG0;
		sc->sc_regs[1][PCAGPIO_IN] = PCA9555_IN1;
		sc->sc_regs[1][PCAGPIO_OUT] = PCA9555_OUT1;
		sc->sc_regs[1][PCAGPIO_POLARITY] = PCA9555_POLARITY1;
		sc->sc_regs[1][PCAGPIO_CONFIG] = PCA9555_CONFIG1;
		sc->sc_npins = 16;
	} else {
		/* All other supported devices have one 8 bit port */
		sc->sc_regs[0][PCAGPIO_IN] = PCA9554_IN;
		sc->sc_regs[0][PCAGPIO_OUT] = PCA9554_OUT;
		sc->sc_regs[0][PCAGPIO_POLARITY] = PCA9554_POLARITY;
		sc->sc_regs[0][PCAGPIO_CONFIG] = PCA9554_CONFIG;
		sc->sc_npins = 8;
	}
	if (pcagpio_init(sc, 0) != 0)
		return;
	if (sc->sc_npins > 8 && pcagpio_init(sc, 1) != 0)
		return;

	printf("\n");

	/* Create controller tag */
	sc->sc_gc.gc_node = sc->sc_node;
	sc->sc_gc.gc_cookie = sc;
	sc->sc_gc.gc_config_pin = pcagpio_config_pin;
	sc->sc_gc.gc_get_pin = pcagpio_get_pin;
	sc->sc_gc.gc_set_pin = pcagpio_set_pin;
	gpio_controller_register(&sc->sc_gc);
}

int
pcagpio_init(struct pcagpio_softc *sc, int port)
{
	u_int8_t cmd, data;

	/* Don't invert input. */
	data = 0;
	cmd = sc->sc_regs[port][PCAGPIO_POLARITY];
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		printf(": failed to initialize\n");
		return (-1);
	}

	return (0);
}

void
pcagpio_config_pin(void *arg, uint32_t *cells, int config)
{
	struct pcagpio_softc *sc = arg;
	uint32_t pin = cells[0];
	u_int8_t cmd, data;
	int port, bit;

	if (pin >= 16)
		return;

	port = PCAGPIO_PORT(pin);
	bit = PCAGPIO_BIT(pin);

	cmd = sc->sc_regs[port][PCAGPIO_CONFIG];
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0))
		return;

	if (config & GPIO_CONFIG_OUTPUT)
		data &= ~bit;
	else
		data |= bit;

	cmd = sc->sc_regs[port][PCAGPIO_CONFIG];
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0))
		return;
}

int
pcagpio_get_pin(void *arg, uint32_t *cells)
{
	struct pcagpio_softc *sc = arg;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	u_int8_t cmd, data;
	int port, bit, value;

	if (pin >= 16)
		return 0;

	port = PCAGPIO_PORT(pin);
	bit = PCAGPIO_BIT(pin);

	cmd = sc->sc_regs[port][PCAGPIO_IN];
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0))
		return 0;

	value = !!(data & bit);
	if (flags & GPIO_ACTIVE_LOW)
		value = !value;

	return value;
}

void
pcagpio_set_pin(void *arg, uint32_t *cells, int value)
{
	struct pcagpio_softc *sc = arg;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	u_int8_t cmd, data;
	int port, bit;

	if (pin >= 16)
		return;

	port = PCAGPIO_PORT(pin);
	bit = PCAGPIO_BIT(pin);

	cmd = sc->sc_regs[port][PCAGPIO_OUT];
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0))
		return;

	if (flags & GPIO_ACTIVE_LOW)
		value = !value;

	data &= ~bit;
	if (value)
		data |= bit;

	cmd = sc->sc_regs[port][PCAGPIO_OUT];
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0))
		return;
}
