/*	$OpenBSD: rkiic.c,v 1.7 2021/10/24 17:52:26 mpi Exp $	*/
/*
 * Copyright (c) 2017 Mark Kettenis <kettenis@openbsd.org>
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
#define RKI2C_CON		0x0000
#define  RKI2C_CON_ACT2NAK		(1 << 6)
#define  RKI2C_CON_ACK			(0 << 5)
#define  RKI2C_CON_NAK			(1 << 5)
#define  RKI2C_CON_STOP			(1 << 4)
#define  RKI2C_CON_START		(1 << 3)
#define  RKI2C_CON_I2C_MODE_MASK	(3 << 1)
#define  RKI2C_CON_I2C_MODE_TX		(0 << 1)
#define  RKI2C_CON_I2C_MODE_RRX		(1 << 1)
#define  RKI2C_CON_I2C_MODE_RX		(2 << 1)
#define  RKI2C_CON_I2C_MODE_BROKEN	(3 << 1)
#define  RKI2C_CON_I2C_EN		(1 << 0)
#define RKI2C_CLKDIV		0x0004
#define RKI2C_MRXADDR		0x0008
#define  RKI2C_MRXADDR_ADDLVLD		(1 << 24)
#define RKI2C_MRXRADDR		0x000c
#define  RKI2C_MRXRADDR_SRADDLVLD	(1 << 24)
#define RKI2C_MTXCNT		0x0010
#define RKI2C_MRXCNT		0x0014
#define RKI2C_IEN		0x0018
#define  RKI2C_IEN_START		(1 << 4)
#define RKI2C_IPD		0x001c
#define  RKI2C_IPD_STOP			(1 << 5)
#define  RKI2C_IPD_START		(1 << 4)
#define  RKI2C_IPD_MBRF			(1 << 3)
#define  RKI2C_IPD_MBTF			(1 << 2)
#define  RKI2C_IPD_ALL			0xff
#define RKI2C_FCNT		0x0020
#define RKI2C_SCL_OE_DB		0x0024
#define RKI2C_TXDATA0		0x0100
#define RKI2C_RXDATA0		0x0200
#define RKI2C_ST		0x0220

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct rkiic_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	int			sc_node;
	struct i2c_controller	sc_ic;
	struct i2c_bus		sc_ib;
};

int rkiic_match(struct device *, void *, void *);
void rkiic_attach(struct device *, struct device *, void *);

const struct cfattach	rkiic_ca = {
	sizeof (struct rkiic_softc), rkiic_match, rkiic_attach
};

struct cfdriver rkiic_cd = {
	NULL, "rkiic", DV_DULL
};

int	rkiic_acquire_bus(void *, int);
void	rkiic_release_bus(void *, int);
int	rkiic_send_start(void *, int);
int	rkiic_send_stop(void *, int);
int	rkiic_exec(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
	    void *, size_t, int);

void	rkiic_bus_scan(struct device *, struct i2cbus_attach_args *, void *);

int
rkiic_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "rockchip,rk3288-i2c") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3328-i2c") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3399-i2c"));
}

void
rkiic_attach(struct device *parent, struct device *self, void *aux)
{
	struct rkiic_softc *sc = (struct rkiic_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct i2cbus_attach_args iba;
	uint32_t clock_speed, bus_speed;
	uint32_t div, divl, divh;
	uint32_t clkdivl, clkdivh;

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

	clock_set_assigned(sc->sc_node);
	clock_enable(sc->sc_node, "i2c");
	clock_enable(sc->sc_node, "pclk");

	clock_speed = clock_get_frequency(sc->sc_node, "i2c");
	bus_speed = OF_getpropint(sc->sc_node, "clock-frequency", 100000);

	div = 2;
	while (clock_speed > div * bus_speed * 8)
		div++;
	divl = div / 2;
	divh = div - divl;
	clkdivl = (divl - 1) & 0xffff;
	clkdivh = (divh - 1) & 0xffff;
	HWRITE4(sc, RKI2C_CLKDIV, clkdivh << 16 | clkdivl);

	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_acquire_bus = rkiic_acquire_bus;
	sc->sc_ic.ic_release_bus = rkiic_release_bus;
	sc->sc_ic.ic_exec = rkiic_exec;

	/* Configure its children */
	memset(&iba, 0, sizeof(iba));
	iba.iba_name = "iic";
	iba.iba_tag = &sc->sc_ic;
	iba.iba_bus_scan = rkiic_bus_scan;
	iba.iba_bus_scan_arg = &sc->sc_node;

	config_found(&sc->sc_dev, &iba, iicbus_print);

	sc->sc_ib.ib_node = sc->sc_node;
	sc->sc_ib.ib_ic = &sc->sc_ic;
	i2c_register(&sc->sc_ib);
}

int
rkiic_acquire_bus(void *cookie, int flags)
{
	struct rkiic_softc *sc = cookie;

	HSET4(sc, RKI2C_CON, RKI2C_CON_I2C_EN);
	return 0;
}

void
rkiic_release_bus(void *cookie, int flags)
{
	struct rkiic_softc *sc = cookie;
	
	HCLR4(sc, RKI2C_CON, RKI2C_CON_I2C_EN);
}

int
rkiic_send_start(void *cookie, int flags)
{
	struct rkiic_softc *sc = cookie;
	int timo;

	HSET4(sc, RKI2C_IPD, RKI2C_IPD_START);
	HSET4(sc, RKI2C_CON, RKI2C_CON_START);
	for (timo = 1000; timo > 0; timo--) {
		if (HREAD4(sc, RKI2C_IPD) & RKI2C_IPD_START)
			break;
		delay(10);
	}
	HCLR4(sc, RKI2C_CON, RKI2C_CON_START);
	if (timo == 0)
		return ETIMEDOUT;
	return 0;
}

int
rkiic_send_stop(void *cookie, int flags)
{
	struct rkiic_softc *sc = cookie;
	int timo;

	HSET4(sc, RKI2C_IPD, RKI2C_IPD_STOP);
	HSET4(sc, RKI2C_CON, RKI2C_CON_STOP);
	for (timo = 1000; timo > 0; timo--) {
		if (HREAD4(sc, RKI2C_IPD) & RKI2C_IPD_STOP)
			break;
		delay(10);
	}
	HCLR4(sc, RKI2C_CON, RKI2C_CON_STOP);
	if (timo == 0)
		return ETIMEDOUT;
	return 0;
}

int
rkiic_write(struct rkiic_softc *sc, i2c_addr_t addr, const void *cmd,
    size_t cmdlen, void *buf, size_t buflen)
{
	uint8_t txbuf[32];
	int len = 0;
	int timo, i;

	/* 
	 * Lump slave address, command and data into one single buffer
	 * and transfer it in a single operation.
	 */
	txbuf[len++] = addr << 1;
	for (i = 0; i < cmdlen; i++)
		txbuf[len++] = ((uint8_t *)cmd)[i];
	for (i = 0; i < buflen; i++)
		txbuf[len++] = ((uint8_t *)buf)[i];

	for (i = 0; i < len; i += 4) {
		HWRITE4(sc, RKI2C_TXDATA0 + i,
			*((uint32_t *)&txbuf[i]));
	}

	/* Start operation. */
	HWRITE4(sc, RKI2C_MTXCNT, len);

	/* Wait for completion. */
	for (timo = 1000; timo > 0; timo--) {
		if (HREAD4(sc, RKI2C_IPD) & RKI2C_IPD_MBTF)
			break;
		delay(10);
	}
	if (timo == 0)
		return ETIMEDOUT;

	return 0;
}

int
rkiic_read(struct rkiic_softc *sc, i2c_addr_t addr, const void *cmd,
    size_t cmdlen, void *buf, size_t buflen)
{
	uint32_t mrxraddr, rxdata;
	size_t pos = 0;
	uint32_t con;
	int timo, i;

	HWRITE4(sc, RKI2C_MRXADDR, (addr << 1) | RKI2C_MRXADDR_ADDLVLD);

	/* Send the command as "register address". */
	mrxraddr = 0;
	for (i = 0; i < cmdlen; i++) {
		mrxraddr |= ((uint8_t *)cmd)[i] << (i * 8);
		mrxraddr |= RKI2C_MRXRADDR_SRADDLVLD << i;
	}
	HWRITE4(sc, RKI2C_MRXRADDR, mrxraddr);

	while (1) {
		/* Indicate that we're done after this operation. */
		if (buflen <= 32)
			HSET4(sc, RKI2C_CON, RKI2C_CON_NAK);

		/* Start operation. */
		HWRITE4(sc, RKI2C_MRXCNT, MIN(buflen, 32));

		/* Wait for completion. */
		for (timo = 1000; timo > 0; timo--) {
			if (HREAD4(sc, RKI2C_IPD) & RKI2C_IPD_MBRF)
				break;
			delay(10);
		}
		if (timo == 0)
			return ETIMEDOUT;

		/* Ack interrupt. */
		HWRITE4(sc, RKI2C_IPD, RKI2C_IPD_MBRF);

		for (i = 0; i < MIN(buflen, 32); i++) {
			if (i % 4 == 0)
				rxdata = HREAD4(sc, RKI2C_RXDATA0 + i);
			((uint8_t *)buf)[pos++] = rxdata;
			rxdata >>= 8;
		}

		if (buflen <= 32)
			return 0;

		/* Switch transfer mode after the first block. */
		if (pos <= 32) {
			con = HREAD4(sc, RKI2C_CON);
			con &= ~RKI2C_CON_I2C_MODE_MASK;
			con |= RKI2C_CON_I2C_MODE_RX;
			HWRITE4(sc, RKI2C_CON, con);
		}
		buflen -= 32;
	}
}

int
rkiic_exec(void *cookie, i2c_op_t op, i2c_addr_t addr, const void *cmd,
    size_t cmdlen, void *buf, size_t buflen, int flags)
{
	struct rkiic_softc *sc = cookie;
	uint32_t con;
	int error;

	if (cmdlen > 3 || (I2C_OP_WRITE_P(op) && buflen > 28))
		return EINVAL;

	/* Clear interrupts.  */
	HWRITE4(sc, RKI2C_IPD, RKI2C_IPD_ALL);

	/* Configure transfer mode. */
	con = HREAD4(sc, RKI2C_CON);
	con &= ~RKI2C_CON_I2C_MODE_MASK;
	if (I2C_OP_WRITE_P(op))
		con |= RKI2C_CON_I2C_MODE_TX;
	else
		con |= RKI2C_CON_I2C_MODE_RRX;
	con &= ~RKI2C_CON_NAK;
	con |= RKI2C_CON_ACT2NAK;
	HWRITE4(sc, RKI2C_CON, con);

	error = rkiic_send_start(sc, flags);
	if (error)
		return error;

	if (I2C_OP_WRITE_P(op))
		error = rkiic_write(sc, addr, cmd, cmdlen, buf, buflen);
	else
		error = rkiic_read(sc, addr, cmd, cmdlen, buf, buflen);

	if (I2C_OP_STOP_P(op))
		rkiic_send_stop(sc, flags);

	return error;
}

void
rkiic_bus_scan(struct device *self, struct i2cbus_attach_args *iba, void *arg)
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
