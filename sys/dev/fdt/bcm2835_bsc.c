/*	$OpenBSD: bcm2835_bsc.c,v 1.4 2022/04/06 18:59:28 naddy Exp $	*/
/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

/* Registers. */
#define BSC_C		0x00
#define  BSC_C_I2CEN	(1 << 15)
#define  BSC_C_INTR	(1 << 10)
#define  BSC_C_INTT	(1 << 9)
#define  BSC_C_INTD	(1 << 8)
#define  BSC_C_ST	(1 << 7)
#define  BSC_C_CLEAR	(0x3 << 4)
#define  BSC_C_READ	(1 << 0)
#define BSC_S		0x04
#define  BSC_S_CLKT	(1 << 9)
#define  BSC_S_ERR	(1 << 8)
#define  BSC_S_RXF	(1 << 7)
#define  BSC_S_TXE	(1 << 6)
#define  BSC_S_RXD	(1 << 5)
#define  BSC_S_TXD	(1 << 4)
#define  BSC_S_RXR	(1 << 3)
#define  BSC_S_TXW	(1 << 2)
#define  BSC_S_DONE	(1 << 1)
#define  BSC_S_TA	(1 << 0)
#define BSC_DLEN	0x08
#define BSC_A		0x0c
#define BSC_FIFO	0x10
#define BSC_DIV		0x14
#define BSC_DEL		0x18
#define  BSC_DEL_FEDL_SHIFT	16
#define  BSC_DEL_REDL_SHIFT	0
#define BSC_CLKT	0x1c

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct bcmbsc_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	int			sc_node;
	struct i2c_controller	sc_ic;
	struct i2c_bus		sc_ib;
};

int	bcmbsc_match(struct device *, void *, void *);
void	bcmbsc_attach(struct device *, struct device *, void *);

const struct cfattach bcmbsc_ca = {
	sizeof (struct bcmbsc_softc), bcmbsc_match, bcmbsc_attach
};

struct cfdriver bcmbsc_cd = {
	NULL, "bcmbsc", DV_DULL
};

int	bcmbsc_acquire_bus(void *, int);
void	bcmbsc_release_bus(void *, int);
int	bcmbsc_exec(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
	    void *, size_t, int);

void	bcmbsc_bus_scan(struct device *, struct i2cbus_attach_args *, void *);

int
bcmbsc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "brcm,bcm2835-i2c");
}

void
bcmbsc_attach(struct device *parent, struct device *self, void *aux)
{
	struct bcmbsc_softc *sc = (struct bcmbsc_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct i2cbus_attach_args iba;
	uint32_t clock_speed, bus_speed;
	uint32_t div, fedl, redl;

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

	div = clock_speed / bus_speed;
	if (div & 1)
		div++;
	fedl = MAX(div / 16, 1);
	redl = MAX(div / 4, 1);
	HWRITE4(sc, BSC_DIV, div);
	HWRITE4(sc, BSC_DEL, (fedl << BSC_DEL_FEDL_SHIFT) |
	    (redl << BSC_DEL_REDL_SHIFT));

	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_acquire_bus = bcmbsc_acquire_bus;
	sc->sc_ic.ic_release_bus = bcmbsc_release_bus;
	sc->sc_ic.ic_exec = bcmbsc_exec;

	/* Configure its children */
	memset(&iba, 0, sizeof(iba));
	iba.iba_name = "iic";
	iba.iba_tag = &sc->sc_ic;
	iba.iba_bus_scan = bcmbsc_bus_scan;
	iba.iba_bus_scan_arg = &sc->sc_node;

	config_found(&sc->sc_dev, &iba, iicbus_print);

	sc->sc_ib.ib_node = sc->sc_node;
	sc->sc_ib.ib_ic = &sc->sc_ic;
	i2c_register(&sc->sc_ib);
}

int
bcmbsc_acquire_bus(void *cookie, int flags)
{
	struct bcmbsc_softc *sc = cookie;

	HWRITE4(sc, BSC_S, HREAD4(sc, BSC_S));
	HWRITE4(sc, BSC_C, BSC_C_I2CEN | BSC_C_CLEAR);
	return 0;
}

void
bcmbsc_release_bus(void *cookie, int flags)
{
	struct bcmbsc_softc *sc = cookie;

	HWRITE4(sc, BSC_C, BSC_C_CLEAR);
}

int
bcmbsc_wait(struct bcmbsc_softc *sc, uint32_t mask, uint32_t value)
{
	uint32_t stat;
	int timo;

	for (timo = 10000; timo > 0; timo--) {
		stat = HREAD4(sc, BSC_S);
		if ((stat & mask) == value)
			return 0;
		if (stat & BSC_S_CLKT)
			return ETIMEDOUT;
		if (stat & BSC_S_ERR)
			return EIO;
		delay(1);
	}

	return ETIMEDOUT;
}

int
bcmbsc_read(struct bcmbsc_softc *sc, uint8_t *buf, size_t buflen)
{
	int i, error;

	for (i = 0; i < buflen; i++) {
		error = bcmbsc_wait(sc, BSC_S_RXD, BSC_S_RXD);
		if (error)
			return error;
		buf[i] = HREAD4(sc, BSC_FIFO);
	}

	return 0;
}

int
bcmbsc_write(struct bcmbsc_softc *sc, const uint8_t *buf, size_t buflen)
{
	int i, error;

	for (i = 0; i < buflen; i++) {
		error = bcmbsc_wait(sc, BSC_S_TXD, BSC_S_TXD);
		if (error)
			return error;
		HWRITE4(sc, BSC_FIFO, buf[i]);
	}

	return 0;
}

int
bcmbsc_exec(void *cookie, i2c_op_t op, i2c_addr_t addr, const void *cmd,
    size_t cmdlen, void *buf, size_t buflen, int flags)
{
	struct bcmbsc_softc *sc = cookie;
	uint32_t ctrl = BSC_C_I2CEN | BSC_C_ST;
	int error;

	if (cmdlen + buflen > 65535)
		return EINVAL;

	HWRITE4(sc, BSC_A, addr);

	if (I2C_OP_READ_P(op))
		HWRITE4(sc, BSC_DLEN, cmdlen);
	else
		HWRITE4(sc, BSC_DLEN, cmdlen + buflen);

	if (cmdlen > 0) {
		HWRITE4(sc, BSC_C, ctrl);
		error = bcmbsc_write(sc, cmd, cmdlen);
		if (error)
			return error;
		if (I2C_OP_READ_P(op))
			bcmbsc_wait(sc, BSC_S_DONE, BSC_S_DONE);
	}

	if (I2C_OP_READ_P(op)) {
		HWRITE4(sc, BSC_DLEN, buflen);
		HWRITE4(sc, BSC_C, ctrl | BSC_C_READ);
		error = bcmbsc_read(sc, buf, buflen);
		if (error)
			return error;
	} else {
		if (cmdlen == 0)
			HWRITE4(sc, BSC_C, ctrl);
		error = bcmbsc_write(sc, buf, buflen);
		if (error)
			return error;
	}

	return bcmbsc_wait(sc, BSC_S_DONE | BSC_S_TA, BSC_S_DONE);
}

void
bcmbsc_bus_scan(struct device *self, struct i2cbus_attach_args *iba, void *arg)
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
