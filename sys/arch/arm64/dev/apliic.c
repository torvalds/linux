/*	$OpenBSD: apliic.c,v 1.5 2022/12/10 18:43:48 kettenis Exp $	*/
/*
 * Copyright (c) 2021 Patrick Wildt <patrick@blueri.se>
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
#include <sys/malloc.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#define _I2C_PRIVATE
#include <dev/i2c/i2cvar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_power.h>
#include <dev/ofw/fdt.h>

/* Registers. */
#define I2C_MTXFIFO		0x00
#define  I2C_MTXFIFO_DATA_MASK		(0xff << 0)
#define  I2C_MTXFIFO_START		(1 << 8)
#define  I2C_MTXFIFO_STOP		(1 << 9)
#define  I2C_MTXFIFO_READ		(1 << 10)
#define I2C_MRXFIFO		0x04
#define  I2C_MRXFIFO_DATA_MASK		(0xff << 0)
#define  I2C_MRXFIFO_EMPTY		(1 << 8)
#define I2C_SMSTA		0x14
#define  I2C_SMSTA_MTN			(1 << 21)
#define  I2C_SMSTA_XEN			(1 << 27)
#define  I2C_SMSTA_XBUSY		(1 << 28)
#define I2C_CTL			0x1c
#define  I2C_CTL_CLK_MASK		(0xff << 0)
#define  I2C_CTL_MTR			(1 << 9)
#define  I2C_CTL_MRR			(1 << 10)
#define  I2C_CTL_EN			(1 << 11)
#define I2C_REV			0x28

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct apliic_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	int			sc_node;
	int			sc_hwrev;
	uint32_t		sc_clkdiv;
	struct i2c_controller	sc_ic;
};

int apliic_match(struct device *, void *, void *);
void apliic_attach(struct device *, struct device *, void *);

const struct cfattach	apliic_ca = {
	sizeof (struct apliic_softc), apliic_match, apliic_attach
};

struct cfdriver apliic_cd = {
	NULL, "apliic", DV_DULL
};

int	apliic_acquire_bus(void *, int);
void	apliic_release_bus(void *, int);
int	apliic_exec(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
	    void *, size_t, int);

void	apliic_bus_scan(struct device *, struct i2cbus_attach_args *, void *);

int
apliic_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "apple,i2c");
}

void
apliic_attach(struct device *parent, struct device *self, void *aux)
{
	struct apliic_softc *sc = (struct apliic_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct i2cbus_attach_args iba;
	uint32_t clock_speed, bus_speed;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	sc->sc_node = faa->fa_node;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	printf("\n");

	pinctrl_byname(sc->sc_node, "default");
	power_domain_enable(sc->sc_node);
	clock_enable_all(sc->sc_node);

	clock_speed = clock_get_frequency(sc->sc_node, NULL);
	bus_speed = OF_getpropint(sc->sc_node, "clock-frequency", 100000) * 16;
	sc->sc_clkdiv = (clock_speed + bus_speed - 1) / bus_speed;
	KASSERT(sc->sc_clkdiv <= I2C_CTL_CLK_MASK);

	sc->sc_hwrev = HREAD4(sc, I2C_REV);

	HWRITE4(sc, I2C_CTL, sc->sc_clkdiv | I2C_CTL_MTR | I2C_CTL_MRR |
	    (sc->sc_hwrev >= 6 ? I2C_CTL_EN : 0));

	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_acquire_bus = apliic_acquire_bus;
	sc->sc_ic.ic_release_bus = apliic_release_bus;
	sc->sc_ic.ic_exec = apliic_exec;

	/* Configure its children */
	memset(&iba, 0, sizeof(iba));
	iba.iba_name = "iic";
	iba.iba_tag = &sc->sc_ic;
	iba.iba_bus_scan = apliic_bus_scan;
	iba.iba_bus_scan_arg = &sc->sc_node;

	config_found(&sc->sc_dev, &iba, iicbus_print);
}

int
apliic_wait(struct apliic_softc *sc)
{
	uint32_t reg;
	int timo;

	for (timo = 100; timo > 0; timo--) {
		reg = HREAD4(sc, I2C_SMSTA);
		if (reg & I2C_SMSTA_XEN)
			break;
		delay(1000);
	}
	if (reg & I2C_SMSTA_MTN)
		return ENXIO;
	if (timo == 0) {
		HWRITE4(sc, I2C_SMSTA, reg);
		return ETIMEDOUT;
	}

	HWRITE4(sc, I2C_SMSTA, I2C_SMSTA_XEN);
	return 0;
}

int
apliic_acquire_bus(void *cookie, int flags)
{
	return 0;
}

void
apliic_release_bus(void *cookie, int flags)
{
}

int
apliic_exec(void *cookie, i2c_op_t op, i2c_addr_t addr, const void *cmd,
    size_t cmdlen, void *buf, size_t buflen, int flags)
{
	struct apliic_softc *sc = cookie;
	uint32_t reg;
	int error, i;

	if (!I2C_OP_STOP_P(op))
		return EINVAL;

	reg = HREAD4(sc, I2C_SMSTA);
	HWRITE4(sc, I2C_SMSTA, reg);

	if (cmdlen > 0) {
		HWRITE4(sc, I2C_MTXFIFO, I2C_MTXFIFO_START | addr << 1);
		for (i = 0; i < cmdlen - 1; i++)
			HWRITE4(sc, I2C_MTXFIFO, ((uint8_t *)cmd)[i]);
		HWRITE4(sc, I2C_MTXFIFO, ((uint8_t *)cmd)[cmdlen - 1] |
		    (buflen == 0 ? I2C_MTXFIFO_STOP : 0));
	}

	if (buflen == 0)
		return 0;

	if (I2C_OP_READ_P(op)) {
		HWRITE4(sc, I2C_MTXFIFO, I2C_MTXFIFO_START | addr << 1 | 1);
		HWRITE4(sc, I2C_MTXFIFO, I2C_MTXFIFO_READ | buflen |
		    I2C_MTXFIFO_STOP);
		error = apliic_wait(sc);
		if (error)
			return error;
		for (i = 0; i < buflen; i++) {
			reg = HREAD4(sc, I2C_MRXFIFO);
			if (reg & I2C_MRXFIFO_EMPTY)
				return EIO;
			((uint8_t *)buf)[i] = reg & I2C_MRXFIFO_DATA_MASK;
		}
	} else {
		if (cmdlen == 0)
			HWRITE4(sc, I2C_MTXFIFO, I2C_MTXFIFO_START | addr << 1);
		for (i = 0; i < buflen - 1; i++)
			HWRITE4(sc, I2C_MTXFIFO, ((uint8_t *)buf)[i]);
		HWRITE4(sc, I2C_MTXFIFO, ((uint8_t *)buf)[buflen - 1] |
		    I2C_MTXFIFO_STOP);
		error = apliic_wait(sc);
		if (error)
			return error;
	}

	return 0;
}

void
apliic_bus_scan(struct device *self, struct i2cbus_attach_args *iba, void *arg)
{
	int iba_node = *(int *)arg;
	struct i2c_attach_args ia;
	char status[32];
	char *compat;
	uint32_t reg[1];
	int node;
	int len;

	for (node = OF_child(iba_node); node; node = OF_peer(node)) {
		memset(status, 0, sizeof(status));
		if (OF_getprop(node, "status", status, sizeof(status)) > 0 &&
		    strcmp(status, "disabled") == 0)
			continue;

		memset(reg, 0, sizeof(reg));
		if (OF_getprop(node, "reg", &reg, sizeof(reg)) != sizeof(reg))
			continue;

		len = OF_getproplen(node, "compatible");
		if (len <= 0)
			continue;

		compat = malloc(len, M_TEMP, M_WAITOK);
		OF_getprop(node, "compatible", compat, len);

		memset(&ia, 0, sizeof(ia));
		ia.ia_tag = iba->iba_tag;
		ia.ia_addr = bemtoh32(&reg[0]);
		ia.ia_name = compat;
		ia.ia_namelen = len;
		ia.ia_cookie = &node;
		config_found(self, &ia, iic_print);

		free(compat, M_TEMP, len);
	}
}
