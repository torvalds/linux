/*	$OpenBSD: amliic.c,v 1.5 2021/10/24 17:52:26 mpi Exp $	*/
/*
 * Copyright (c) 2019 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#define _I2C_PRIVATE
#include <dev/i2c/i2cvar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

/* Registers. */
#define I2C_M_CONTROL			0x00
#define  I2C_M_CONTROL_QTR_CLK_DLY_SHIFT	12
#define  I2C_M_CONTROL_QTR_CLK_EXT_SHIFT	28
#define  I2C_M_CONTROL_ERROR			(1 << 3)
#define  I2C_M_CONTROL_STATUS			(1 << 2)
#define  I2C_M_CONTROL_START			(1 << 0)
#define I2C_M_SLAVE_ADDRESS		0x04
#define I2C_M_TOKEN_LIST0		0x08
#define I2C_M_TOKEN_LIST1		0x0c
#define I2C_M_TOKEN_WDATA0		0x10
#define I2C_M_TOKEN_WDATA1		0x14
#define I2C_M_TOKEN_RDATA0		0x18
#define I2C_M_TOKEN_RDATA1		0x1c

/* Token definitions. */
#define END				0x0
#define START				0x1
#define SLAVE_ADDR_WRITE		0x2
#define SLAVE_ADDR_READ			0x3
#define DATA				0x4
#define DATA_LAST			0x5
#define STOP				0x6

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct amliic_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	int			sc_node;
	struct i2c_controller	sc_ic;
};

int amliic_match(struct device *, void *, void *);
void amliic_attach(struct device *, struct device *, void *);

const struct cfattach	amliic_ca = {
	sizeof (struct amliic_softc), amliic_match, amliic_attach
};

struct cfdriver amliic_cd = {
	NULL, "amliic", DV_DULL
};

int	amliic_acquire_bus(void *, int);
void	amliic_release_bus(void *, int);
int	amliic_exec(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
	    void *, size_t, int);

void	amliic_bus_scan(struct device *, struct i2cbus_attach_args *, void *);

int
amliic_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "amlogic,meson-axg-i2c");
}

void
amliic_attach(struct device *parent, struct device *self, void *aux)
{
	struct amliic_softc *sc = (struct amliic_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct i2cbus_attach_args iba;
	uint32_t clock_speed, bus_speed;
	uint32_t div, divl, divh;

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

	clock_enable_all(sc->sc_node);

	clock_speed = clock_get_frequency(sc->sc_node, NULL);
	bus_speed = OF_getpropint(sc->sc_node, "clock-frequency", 100000);

	div = clock_speed / bus_speed / 4;
	divl = div & 0x3ff;
	divh = div >> 10;
	HWRITE4(sc, I2C_M_CONTROL, divh << I2C_M_CONTROL_QTR_CLK_EXT_SHIFT |
	    divl << I2C_M_CONTROL_QTR_CLK_DLY_SHIFT);

	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_acquire_bus = amliic_acquire_bus;
	sc->sc_ic.ic_release_bus = amliic_release_bus;
	sc->sc_ic.ic_exec = amliic_exec;

	/* Configure its children */
	memset(&iba, 0, sizeof(iba));
	iba.iba_name = "iic";
	iba.iba_tag = &sc->sc_ic;
	iba.iba_bus_scan = amliic_bus_scan;
	iba.iba_bus_scan_arg = &sc->sc_node;

	config_found(&sc->sc_dev, &iba, iicbus_print);
}

int
amliic_acquire_bus(void *cookie, int flags)
{
	return 0;
}

void
amliic_release_bus(void *cookie, int flags)
{
}

int
amliic_exec(void *cookie, i2c_op_t op, i2c_addr_t addr, const void *cmd,
    size_t cmdlen, void *buf, size_t buflen, int flags)
{
	struct amliic_softc *sc = cookie;
	uint64_t tokens = 0;
	uint64_t data = 0;
	uint32_t rdata;
	uint32_t ctrl;
	size_t pos, len;
	int i = 0, j = 0;
	int timo, k;

#define SET_TOKEN(i, t) \
	tokens |= (((uint64_t)(t)) << ((i) * 4))
#define SET_DATA(i, d) \
	data |= (((uint64_t)(d)) << ((i) * 8))

	if (cmdlen > 8)
		return EINVAL;

	if (cmdlen > 0) {
		SET_TOKEN(i++, START);
		SET_TOKEN(i++, SLAVE_ADDR_WRITE);
		for (k = 0; k < cmdlen; k++) {
			SET_TOKEN(i++, DATA);
			SET_DATA(j++, ((uint8_t *)cmd)[k]);
		}
	}

	if (I2C_OP_READ_P(op)) {
		SET_TOKEN(i++, START);
		SET_TOKEN(i++, SLAVE_ADDR_READ);
	} else if (cmdlen == 0) {
		SET_TOKEN(i++, START);
		SET_TOKEN(i++, SLAVE_ADDR_WRITE);
	}

	HWRITE4(sc, I2C_M_SLAVE_ADDRESS, addr << 1);

	pos = 0;
	while (pos < buflen) {
		len = MIN(buflen - pos, 8 - j);

		if (I2C_OP_READ_P(op)) {
			for (k = 0; k < len; k++)
				SET_TOKEN(i++, (pos == (buflen - 1)) ?
				    DATA_LAST : DATA);
		} else {
			for (k = 0; k < len; k++) {
				SET_TOKEN(i++, DATA);
				SET_DATA(j++, ((uint8_t *)buf)[pos++]);
			}
		}

		if (pos == buflen && I2C_OP_STOP_P(op))
			SET_TOKEN(i++, STOP);

		SET_TOKEN(i++, END);

		/* Write slave address, tokens and data to hardware. */
		HWRITE4(sc, I2C_M_TOKEN_LIST0, tokens);
		HWRITE4(sc, I2C_M_TOKEN_LIST1, tokens >> 32);
		HWRITE4(sc, I2C_M_TOKEN_WDATA0, data);
		HWRITE4(sc, I2C_M_TOKEN_WDATA1, data >> 32);

		/* Start token list processing. */
		HSET4(sc, I2C_M_CONTROL, I2C_M_CONTROL_START);
		for (timo = 50000; timo > 0; timo--) {
			ctrl = HREAD4(sc, I2C_M_CONTROL);
			if ((ctrl & I2C_M_CONTROL_STATUS) == 0)
				break;
			delay(10);
		}
		HCLR4(sc, I2C_M_CONTROL, I2C_M_CONTROL_START);
		if (ctrl & I2C_M_CONTROL_ERROR)
			return EIO;
		if (timo == 0)
			return ETIMEDOUT;

		if (I2C_OP_READ_P(op)) {
			rdata = HREAD4(sc, I2C_M_TOKEN_RDATA0);
			for (i = 0; i < len; i++) {
				if (i == 4)
					rdata = HREAD4(sc, I2C_M_TOKEN_RDATA1);
				((uint8_t *)buf)[pos++] = rdata;
				rdata >>= 8;
			}
		}

		/* Reset tokens. */
		tokens = 0;
		data = 0;
		i = j = 0;
	}

	return 0;
}

void
amliic_bus_scan(struct device *self, struct i2cbus_attach_args *iba, void *arg)
{
	int iba_node = *(int *)arg;
	struct i2c_attach_args ia;
	char name[32], status[32];
	uint32_t reg[1];
	int node;

	for (node = OF_child(iba_node); node; node = OF_peer(node)) {
		memset(name, 0, sizeof(name));
		memset(status, 0, sizeof(status));
		memset(reg, 0, sizeof(reg));

		if (OF_getprop(node, "compatible", name, sizeof(name)) == -1)
			continue;
		if (name[0] == '\0')
			continue;

		if (OF_getprop(node, "status", status, sizeof(status)) > 0 &&
		    strcmp(status, "disabled") == 0)
			continue;

		if (OF_getprop(node, "reg", &reg, sizeof(reg)) != sizeof(reg))
			continue;

		memset(&ia, 0, sizeof(ia));
		ia.ia_tag = iba->iba_tag;
		ia.ia_addr = bemtoh32(&reg[0]);
		ia.ia_name = name;
		ia.ia_cookie = &node;
		config_found(self, &ia, iic_print);
	}
}
