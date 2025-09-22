/*	$OpenBSD: mpfiic.c,v 1.1 2022/02/16 13:07:36 visa Exp $	*/

/*
 * Copyright (c) 2022 Visa Hankala
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

/*
 * Driver for PolarFire SoC MSS I2C controller.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#define _I2C_PRIVATE
#include <dev/i2c/i2cvar.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>

#define I2C_CTRL		0x0000
#define  I2C_CTRL_CR2			(1 << 7)
#define  I2C_CTRL_ENS1			(1 << 6)
#define  I2C_CTRL_STA			(1 << 5)
#define  I2C_CTRL_STO			(1 << 4)
#define  I2C_CTRL_SI			(1 << 3)
#define  I2C_CTRL_AA			(1 << 2)
#define  I2C_CTRL_CR1			(1 << 1)
#define  I2C_CTRL_CR0			(1 << 0)
#define I2C_STATUS		0x0004
#define I2C_DATA		0x0008
#define I2C_SLAVE0ADR		0x000c
#define I2C_SMBUS		0x0010
#define I2C_FREQ		0x0014
#define I2C_GLITCHREG		0x0018
#define I2C_SLAVE1ADR		0x001c

#define I2C_STATUS_START		0x08
#define I2C_STATUS_RESTART		0x10
#define I2C_STATUS_SLAW_ACK		0x18
#define I2C_STATUS_DATAW_ACK		0x28
#define I2C_STATUS_LOSTARB		0x38
#define I2C_STATUS_SLAR_ACK		0x40
#define I2C_STATUS_DATAR_ACK		0x50
#define I2C_STATUS_DATAR_NACK		0x58
#define I2C_STATUS_IDLE			0xf8

struct mpfiic_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	int			sc_node;

	struct i2c_bus		sc_i2c_bus;
	struct i2c_controller	sc_i2c_tag;
	struct rwlock		sc_i2c_lock;

	uint32_t		sc_bus_freq;		/* in Hz */
	uint8_t			sc_ctrl;
	uint8_t			sc_start_sent;
};

#define HREAD4(sc, reg) \
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val) \
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

int	mpfiic_match(struct device *, void *, void*);
void	mpfiic_attach(struct device *, struct device *, void *);

int	mpfiic_i2c_acquire_bus(void *, int);
void	mpfiic_i2c_release_bus(void *, int);
int	mpfiic_i2c_send_start(void *, int);
int	mpfiic_i2c_send_stop(void *, int);
int	mpfiic_i2c_initiate_xfer(void *, i2c_addr_t, int);
int	mpfiic_i2c_read_byte(void *, uint8_t *, int);
int	mpfiic_i2c_write_byte(void *, uint8_t, int);
void	mpfiic_i2c_scan(struct device *, struct i2cbus_attach_args *, void *);

int	mpfiic_wait(struct mpfiic_softc *, uint8_t);

const struct cfattach mpfiic_ca = {
	sizeof(struct mpfiic_softc), mpfiic_match, mpfiic_attach
};

struct cfdriver mpfiic_cd = {
	NULL, "mpfiic", DV_DULL
};

static struct {
	uint32_t	div;
	uint32_t	cr;
} mpfiic_clk_divs[] = {
#ifdef notused
	/* BCLK */
	{ 8,	I2C_CTRL_CR2 | I2C_CTRL_CR1 | I2C_CTRL_CR0 },
#endif
	/* PCLK */
	{ 60,	I2C_CTRL_CR2 | I2C_CTRL_CR1 },
	{ 120,	I2C_CTRL_CR2 | I2C_CTRL_CR0 },
	{ 160,	I2C_CTRL_CR1 | I2C_CTRL_CR0 },
	{ 192,	I2C_CTRL_CR1 },
	{ 224,	I2C_CTRL_CR0 },
	{ 256,	0 },
	{ 960,	I2C_CTRL_CR2 },
};

int
mpfiic_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1)
		return 0;
	return OF_is_compatible(faa->fa_node, "microchip,mpfs-i2c");
}

void
mpfiic_attach(struct device *parent, struct device *self, void *aux)
{
	struct i2cbus_attach_args iba;
	struct fdt_attach_args *faa = aux;
	struct mpfiic_softc *sc = (struct mpfiic_softc *)self;
	uint32_t i, bus_freq, clock_freq;

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;

	clock_freq = clock_get_frequency(sc->sc_node, NULL);
	bus_freq = OF_getpropint(sc->sc_node, "clock-frequency", 100000);

	/* Determine clock divider, assumes PCLK. */
	for (i = 0; i < nitems(mpfiic_clk_divs) - 1; i++) {
		if (clock_freq / mpfiic_clk_divs[i].div <= bus_freq)
			break;
	}
	sc->sc_bus_freq = clock_freq / mpfiic_clk_divs[i].div;
	sc->sc_ctrl = mpfiic_clk_divs[i].cr | I2C_CTRL_ENS1;

	if (sc->sc_bus_freq == 0) {
		printf(": invalid bus frequency\n");
		return;
	}

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr, faa->fa_reg[0].size,
	    0, &sc->sc_ioh) != 0) {
		printf(": can't map registers\n");
		return;
	}

	clock_enable_all(sc->sc_node);

	/* Initialize the device. */
	HWRITE4(sc, I2C_CTRL, sc->sc_ctrl);
	HWRITE4(sc, I2C_CTRL, 0);

	/* Disable slave address comparison. */
	HWRITE4(sc, I2C_SLAVE0ADR, 0);
	HWRITE4(sc, I2C_SLAVE1ADR, 0);

	/* Disable SMBus logic, operate in standard I2C mode. */
	HWRITE4(sc, I2C_SMBUS, 0);

	printf("\n");

	rw_init(&sc->sc_i2c_lock, "iiclk");
	sc->sc_i2c_tag.ic_cookie = sc;
	sc->sc_i2c_tag.ic_acquire_bus = mpfiic_i2c_acquire_bus;
	sc->sc_i2c_tag.ic_release_bus = mpfiic_i2c_release_bus;
	sc->sc_i2c_tag.ic_send_start = mpfiic_i2c_send_start;
	sc->sc_i2c_tag.ic_send_stop = mpfiic_i2c_send_stop;
	sc->sc_i2c_tag.ic_initiate_xfer = mpfiic_i2c_initiate_xfer;
	sc->sc_i2c_tag.ic_read_byte = mpfiic_i2c_read_byte;
	sc->sc_i2c_tag.ic_write_byte = mpfiic_i2c_write_byte;

	memset(&iba, 0, sizeof(iba));
	iba.iba_name = "iic";
	iba.iba_tag = &sc->sc_i2c_tag;
	iba.iba_bus_scan = mpfiic_i2c_scan;
	iba.iba_bus_scan_arg = &sc->sc_node;
	config_found(self, &iba, iicbus_print);

	sc->sc_i2c_bus.ib_node = faa->fa_node;
	sc->sc_i2c_bus.ib_ic = &sc->sc_i2c_tag;
	i2c_register(&sc->sc_i2c_bus);
}

int
mpfiic_i2c_acquire_bus(void *arg, int flags)
{
	struct mpfiic_softc *sc = arg;

	if (cold || (flags & I2C_F_POLL))
		return 0;

	return rw_enter(&sc->sc_i2c_lock, RW_WRITE | RW_INTR);
}

void
mpfiic_i2c_release_bus(void *arg, int flags)
{
	struct mpfiic_softc *sc = arg;

	if (cold || (flags & I2C_F_POLL))
		return;

	rw_exit(&sc->sc_i2c_lock);
}

int
mpfiic_i2c_send_start(void *cookie, int flags)
{
	struct mpfiic_softc *sc = cookie;
	int error;
	uint8_t nstatus;

	HWRITE4(sc, I2C_CTRL, sc->sc_ctrl | I2C_CTRL_STA);

	if (sc->sc_start_sent)
		nstatus = I2C_STATUS_RESTART;
	else
		nstatus = I2C_STATUS_START;
	error = mpfiic_wait(sc, nstatus);
	if (error != 0)
		return error;

	sc->sc_start_sent = 1;

	return 0;
}

int
mpfiic_i2c_send_stop(void *cookie, int flags)
{
	struct mpfiic_softc *sc = cookie;

	sc->sc_start_sent = 0;

	HWRITE4(sc, I2C_CTRL, sc->sc_ctrl | I2C_CTRL_STO);

	/* Let a few bus clock cycles pass. */
	delay(4 * 1000000 / sc->sc_bus_freq);

	/* Disable the device. This resets the state machine. */
	HWRITE4(sc, I2C_CTRL, 0);

	return 0;
}

int
mpfiic_i2c_initiate_xfer(void *cookie, i2c_addr_t addr, int flags)
{
	struct mpfiic_softc *sc = cookie;
	int error;
	uint8_t mode, nstatus;

	if (addr >= 0x80)
		return EINVAL;

	error = mpfiic_i2c_send_start(sc, flags);
	if (error != 0)
		return error;

	if (flags & I2C_F_READ) {
		mode = 0x01;
		nstatus = I2C_STATUS_SLAR_ACK;
	} else {
		mode = 0x00;
		nstatus = I2C_STATUS_SLAW_ACK;
	}

	HWRITE4(sc, I2C_DATA, (addr << 1) | mode);
	HWRITE4(sc, I2C_CTRL, sc->sc_ctrl);

	return mpfiic_wait(sc, nstatus);
}

int
mpfiic_i2c_read_byte(void *cookie, uint8_t *datap, int flags)
{
	struct mpfiic_softc *sc = cookie;
	int error;
	uint8_t ack = 0, nstatus;

	if ((flags & I2C_F_LAST) == 0)
		ack = I2C_CTRL_AA;
	HWRITE4(sc, I2C_CTRL, sc->sc_ctrl | ack);

	if (flags & I2C_F_LAST)
		nstatus = I2C_STATUS_DATAR_NACK;
	else
		nstatus = I2C_STATUS_DATAR_ACK;
	error = mpfiic_wait(sc, nstatus);
	if (error != 0)
		return error;

	*datap = HREAD4(sc, I2C_DATA);

	if (flags & I2C_F_STOP)
		error = mpfiic_i2c_send_stop(sc, flags);

	return error;
}

int
mpfiic_i2c_write_byte(void *cookie, uint8_t data, int flags)
{
	struct mpfiic_softc *sc = cookie;
	int error;

	HWRITE4(sc, I2C_DATA, data);
	HWRITE4(sc, I2C_CTRL, sc->sc_ctrl);

	error = mpfiic_wait(sc, I2C_STATUS_DATAW_ACK);
	if (error != 0)
		return error;

	if (flags & I2C_F_STOP)
		error = mpfiic_i2c_send_stop(sc, flags);

	return error;
}

void
mpfiic_i2c_scan(struct device *self, struct i2cbus_attach_args *iba, void *arg)
{
	struct i2c_attach_args ia;
	char status[32];
	char *compat;
	uint32_t reg[1];
	int iba_node = *(int *)arg;
	int len, node;

	for (node = OF_child(iba_node); node != 0; node = OF_peer(node)) {
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

int
mpfiic_wait(struct mpfiic_softc *sc, uint8_t nstatus)
{
	int timeout;
	uint8_t ctrl, status;

	for (timeout = 100000; timeout > 0; timeout--) {
		ctrl = HREAD4(sc, I2C_CTRL);
		if (ctrl & I2C_CTRL_SI)
			break;
		delay(1);
	}
	if (timeout == 0)
		return ETIMEDOUT;

	status = HREAD4(sc, I2C_STATUS);
	if (status != nstatus)
		return EIO;

	return 0;
}
