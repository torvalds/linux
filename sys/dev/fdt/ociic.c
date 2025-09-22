/*	$OpenBSD: ociic.c,v 1.4 2024/05/15 22:54:03 kettenis Exp $	*/
/*
 * Copyright (c) 2021 Mark Kettenis <kettenis@openbsd.org>
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

/* Registers */
#define I2C_PRER_LO	0x0000
#define I2C_PRER_HI	0x0004
#define I2C_CTR		0x0008
#define  I2C_CTR_EN	(1 << 7)
#define  I2C_CTR_IEN	(1 << 6)
#define I2C_TXR		0x000C
#define I2C_RXR		0x000C
#define I2C_CR		0x0010
#define  I2C_CR_STA	(1 << 7)
#define  I2C_CR_STO	(1 << 6)
#define  I2C_CR_RD	(1 << 5)
#define  I2C_CR_WR	(1 << 4)
#define  I2C_CR_NACK	(1 << 3)
#define  I2C_CR_IACK	(1 << 0)
#define I2C_SR		0x0010
#define  I2C_SR_RXNACK	(1 << 7)
#define  I2C_SR_BUSY	(1 << 6)
#define  I2C_SR_AL	(1 << 5)
#define  I2C_SR_TIP	(1 << 1)
#define  I2C_SR_IF	(1 << 0)

/*
 * OpenSBI on the SiFive HiFive Unmatched board implements reboot and
 * powerdown functionality through the Dialog DA9063 Power Management
 * IC over I2C.  The code expects the I2C controller to be enabled so
 * we have to make sure we leave it in that state.
 */

struct ociic_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	int			sc_node;
	struct i2c_controller	sc_ic;
};

static inline uint8_t
ociic_read(struct ociic_softc *sc, bus_size_t reg)
{
	return bus_space_read_1(sc->sc_iot, sc->sc_ioh, reg);
}

static inline void
ociic_write(struct ociic_softc *sc, bus_size_t reg, uint8_t value)
{
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, reg, value);
}

static inline void
ociic_set(struct ociic_softc *sc, bus_size_t reg, uint8_t bits)
{
	ociic_write(sc, reg, ociic_read(sc, reg) | bits);
}

static inline void
ociic_clr(struct ociic_softc *sc, bus_size_t reg, uint8_t bits)
{
	ociic_write(sc, reg, ociic_read(sc, reg) & ~bits);
}

int ociic_match(struct device *, void *, void *);
void ociic_attach(struct device *, struct device *, void *);

const struct cfattach ociic_ca = {
	sizeof (struct ociic_softc), ociic_match, ociic_attach
};

struct cfdriver ociic_cd = {
	NULL, "ociic", DV_DULL
};

int	ociic_acquire_bus(void *, int);
void	ociic_release_bus(void *, int);
int	ociic_exec(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
	    void *, size_t, int);

void	ociic_bus_scan(struct device *, struct i2cbus_attach_args *, void *);

int
ociic_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "sifive,i2c0");
}

void
ociic_attach(struct device *parent, struct device *self, void *aux)
{
	struct ociic_softc *sc = (struct ociic_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct i2cbus_attach_args iba;
	uint32_t clock_speed, bus_speed;
	uint32_t div;

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

	ociic_clr(sc, I2C_CTR, I2C_CTR_EN);

	clock_speed = clock_get_frequency(sc->sc_node, NULL);
	bus_speed = OF_getpropint(sc->sc_node, "clock-frequency", 100000);

	if (clock_speed > 0) {
		div = (clock_speed / (5 * bus_speed));
		if (div > 0)
			div -= 1;
		if (div > 0xffff)
			div = 0xffff;

		ociic_write(sc, I2C_PRER_LO, div & 0xff);
		ociic_write(sc, I2C_PRER_HI, div >> 8);
	}

	ociic_set(sc, I2C_CTR, I2C_CTR_EN);

	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_acquire_bus = ociic_acquire_bus;
	sc->sc_ic.ic_release_bus = ociic_release_bus;
	sc->sc_ic.ic_exec = ociic_exec;

	/* Configure its children */
	memset(&iba, 0, sizeof(iba));
	iba.iba_name = "iic";
	iba.iba_tag = &sc->sc_ic;
	iba.iba_bus_scan = ociic_bus_scan;
	iba.iba_bus_scan_arg = &sc->sc_node;

	config_found(&sc->sc_dev, &iba, iicbus_print);
}

int
ociic_acquire_bus(void *cookie, int flags)
{
	return 0;
}

void
ociic_release_bus(void *cookie, int flags)
{
}

int
ociic_unbusy(struct ociic_softc *sc)
{
	uint8_t stat;
	int timo;

	for (timo = 50000; timo > 0; timo--) {
		stat = ociic_read(sc, I2C_SR);
		if ((stat & I2C_SR_BUSY) == 0)
			break;
		delay(10);
	}
	if (timo == 0) {
		ociic_write(sc, I2C_CR, I2C_CR_STO);
		return ETIMEDOUT;
	}

	return 0;
}

int
ociic_wait(struct ociic_softc *sc, int ack)
{
	uint8_t stat;
	int timo;

	for (timo = 50000; timo > 0; timo--) {
		stat = ociic_read(sc, I2C_SR);
		if ((stat & I2C_SR_TIP) == 0)
			break;
		if ((stat & I2C_SR_AL))
			break;
		delay(10);
	}
	if (timo == 0) {
		ociic_write(sc, I2C_CR, I2C_CR_STO);
		return ETIMEDOUT;
	}

	if (stat & I2C_SR_AL) {
		ociic_write(sc, I2C_CR, I2C_CR_STO);
		return EIO;
	}
	if (ack && (stat & I2C_SR_RXNACK)) {
		ociic_write(sc, I2C_CR, I2C_CR_STO);
		return EIO;
	}

	return 0;
}

int
ociic_exec(void *cookie, i2c_op_t op, i2c_addr_t addr, const void *cmd,
    size_t cmdlen, void *buf, size_t buflen, int flags)
{
	struct ociic_softc *sc = cookie;
	int error, i;

	error = ociic_unbusy(sc);
	if (error)
		return error;

	if (cmdlen > 0) {
		ociic_write(sc, I2C_TXR, addr << 1);
		ociic_write(sc, I2C_CR, I2C_CR_STA | I2C_CR_WR);
		error = ociic_wait(sc, 1);
		if (error)
			return error;

		for (i = 0; i < cmdlen; i++) {
			ociic_write(sc, I2C_TXR, ((uint8_t *)cmd)[i]);
			ociic_write(sc, I2C_CR, I2C_CR_WR);
			error = ociic_wait(sc, 1);
			if (error)
				return error;
		}
	}

	if (I2C_OP_READ_P(op)) {
		ociic_write(sc, I2C_TXR, addr << 1 | 1);
		ociic_write(sc, I2C_CR, I2C_CR_STA | I2C_CR_WR);
		error = ociic_wait(sc, 1);
		if (error)
			return error;

		for (i = 0; i < buflen; i++) {
			ociic_write(sc, I2C_CR, I2C_CR_RD |
			    (i == (buflen - 1) ? I2C_CR_NACK : 0));
			error = ociic_wait(sc, 0);
			if (error)
				return error;
			((uint8_t *)buf)[i] = ociic_read(sc, I2C_RXR);
		}
	} else {
		if (cmdlen == 0) {
			ociic_write(sc, I2C_TXR, addr << 1);
			ociic_write(sc, I2C_CR, I2C_CR_STA | I2C_CR_WR);
		}

		for (i = 0; i < buflen; i++) {
			ociic_write(sc, I2C_TXR, ((uint8_t *)buf)[i]);
			ociic_write(sc, I2C_CR, I2C_CR_WR);
			error = ociic_wait(sc, 1);
			if (error)
				return error;
		}
	}

	if (I2C_OP_STOP_P(op))
		ociic_write(sc, I2C_CR, I2C_CR_STO);

	return 0;
}

void
ociic_bus_scan(struct device *self, struct i2cbus_attach_args *iba, void *arg)
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
