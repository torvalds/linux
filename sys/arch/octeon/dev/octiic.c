/*	$OpenBSD: octiic.c,v 1.3 2020/09/10 16:40:40 visa Exp $	*/

/*
 * Copyright (c) 2019 Visa Hankala
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
 * Driver for OCTEON two-wire serial interface core.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/stdint.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/octeonvar.h>

#define _I2C_PRIVATE
#include <dev/i2c/i2cvar.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>

struct octiic_softc {
	struct device		 sc_dev;
	int			 sc_node;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;

	struct i2c_bus		 sc_i2c_bus;
	struct i2c_controller	 sc_i2c_tag;
	struct rwlock		 sc_i2c_lock;

	int			 sc_start_sent;
};

int	octiic_match(struct device *, void *, void *);
void	octiic_attach(struct device *, struct device *, void *);

int	octiic_i2c_acquire_bus(void *, int);
void	octiic_i2c_release_bus(void *, int);
int	octiic_i2c_send_start(void *, int);
int	octiic_i2c_send_stop(void *, int);
int	octiic_i2c_initiate_xfer(void *, i2c_addr_t, int);
int	octiic_i2c_read_byte(void *, uint8_t *, int);
int	octiic_i2c_write_byte(void *, uint8_t, int);
void	octiic_i2c_scan(struct device *, struct i2cbus_attach_args *, void *);

int	octiic_reg_read(struct octiic_softc *, uint8_t, uint8_t *);
int	octiic_reg_write(struct octiic_softc *, uint8_t, uint8_t);
int	octiic_set_clock(struct octiic_softc *, uint32_t);
int	octiic_wait(struct octiic_softc *, uint8_t, int);

const struct cfattach octiic_ca = {
	sizeof(struct octiic_softc), octiic_match, octiic_attach
};

struct cfdriver octiic_cd = {
	NULL, "octiic", DV_DULL
};

#define TWSI_RD_8(sc, reg) \
	bus_space_read_8((sc)->sc_iot, (sc)->sc_ioh, (reg))
#define TWSI_WR_8(sc, reg, val) \
	bus_space_write_8((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

#define TWSI_SW_TWSI		0x00
#define   TWSI_SW_TWSI_V		0x8000000000000000ull
#define   TWSI_SW_TWSI_SLONLY		0x4000000000000000ull
#define   TWSI_SW_TWSI_EIA		0x2000000000000000ull
#define   TWSI_SW_TWSI_OP_M		0x1e00000000000000ull
#define   TWSI_SW_TWSI_OP_S		57
#define   TWSI_SW_TWSI_R                0x0100000000000000ull
#define   TWSI_SW_TWSI_SOVR		0x0080000000000000ull
#define   TWSI_SW_TWSI_SIZE_M		0x0070000000000000ull
#define   TWSI_SW_TWSI_SIZE_S		52
#define   TWSI_SW_TWSI_SCR_M		0x000c000000000000ull
#define   TWSI_SW_TWSI_SCR_S		50
#define   TWSI_SW_TWSI_A_M		0x0003ff0000000000ull
#define   TWSI_SW_TWSI_A_S		40
#define   TWSI_SW_TWSI_IA_M		0x000000f800000000ull
#define   TWSI_SW_TWSI_IA_S		35
#define   TWSI_SW_TWSI_EOP_IA_M		0x0000000700000000ull
#define   TWSI_SW_TWSI_EOP_IA_S		32
#define   TWSI_SW_TWSI_D_M		0x00000000ffffffffull

/* Opcodes for field TWSI_SW_TWSI_OP */
#define TWSI_OP_CLK			0x04
#define TWSI_OP_EOP			0x06

/* Addresses for field TWSI_SW_TWSI_IA */
#define TWSI_IA_DATA			0x01
#define TWSI_IA_CTL			0x02
#define TWSI_IA_CLKCTL			0x03	/* write only */
#define TWSI_IA_STAT			0x03	/* read only */
#define TWSI_IA_RST			0x07

#define TWSI_INT		0x10

/* Control register bits */
#define TWSI_CTL_CE			0x80
#define TWSI_CTL_ENAB			0x40
#define TWSI_CTL_STA			0x20
#define TWSI_CTL_STP			0x10
#define TWSI_CTL_IFLG			0x08
#define TWSI_CTL_AAK			0x04

/* Core states */
#define TWSI_STAT_ERROR			0x00
#define TWSI_STAT_START			0x08
#define TWSI_STAT_RSTART		0x10
#define TWSI_STAT_AWT_ACK		0x18
#define TWSI_STAT_MBT_ACK		0x28
#define TWSI_STAT_ART_ACK		0x40
#define TWSI_STAT_MBR_ACK		0x50
#define TWSI_STAT_MBR_NAK		0x58
#define TWSI_STAT_IDLE			0xf8

int
octiic_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *fa = aux;

	return OF_is_compatible(fa->fa_node, "cavium,octeon-3860-twsi") ||
	    OF_is_compatible(fa->fa_node, "cavium,octeon-7890-twsi");
}

void
octiic_attach(struct device *parent, struct device *self, void *aux)
{
	struct i2cbus_attach_args iba;
	struct fdt_attach_args *faa = aux;
	struct octiic_softc *sc = (struct octiic_softc *)self;
	uint32_t freq;

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr, faa->fa_reg[0].size,
	    0, &sc->sc_ioh)) {
		printf(": failed to map registers\n");
		return;
	}

	freq = OF_getpropint(faa->fa_node, "clock-frequency", 100000);
	if (octiic_set_clock(sc, freq) != 0) {
		printf(": clock setup failed\n");
		return;
	}

	/* Reset the controller. */
	if (octiic_reg_write(sc, TWSI_IA_RST, 0) != 0) {
		printf(": register write timeout\n");
		return;
	}

	delay(1000);

	if (octiic_wait(sc, TWSI_STAT_IDLE, I2C_F_POLL) != 0) {
		printf(": reset failed\n");
		return;
	}

	printf("\n");

	rw_init(&sc->sc_i2c_lock, "iiclk");
	sc->sc_i2c_tag.ic_cookie = sc;
	sc->sc_i2c_tag.ic_acquire_bus = octiic_i2c_acquire_bus;
	sc->sc_i2c_tag.ic_release_bus = octiic_i2c_release_bus;
	sc->sc_i2c_tag.ic_send_start = octiic_i2c_send_start;
	sc->sc_i2c_tag.ic_send_stop = octiic_i2c_send_stop;
	sc->sc_i2c_tag.ic_initiate_xfer = octiic_i2c_initiate_xfer;
	sc->sc_i2c_tag.ic_read_byte = octiic_i2c_read_byte;
	sc->sc_i2c_tag.ic_write_byte = octiic_i2c_write_byte;

	memset(&iba, 0, sizeof(iba));
	iba.iba_name = "iic";
	iba.iba_tag = &sc->sc_i2c_tag;
	iba.iba_bus_scan = octiic_i2c_scan;
	iba.iba_bus_scan_arg = sc;
	config_found(self, &iba, iicbus_print);

	sc->sc_i2c_bus.ib_node = sc->sc_node;
	sc->sc_i2c_bus.ib_ic = &sc->sc_i2c_tag;
	i2c_register(&sc->sc_i2c_bus);
}

int
octiic_i2c_acquire_bus(void *arg, int flags)
{
	struct octiic_softc *sc = arg;

	if (cold || (flags & I2C_F_POLL))
		return 0;

	return rw_enter(&sc->sc_i2c_lock, RW_WRITE | RW_INTR);
}

void
octiic_i2c_release_bus(void *arg, int flags)
{
	struct octiic_softc *sc = arg;

	if (cold || (flags & I2C_F_POLL))
		return;

	rw_exit(&sc->sc_i2c_lock);
}

int
octiic_i2c_send_start(void *cookie, int flags)
{
	struct octiic_softc *sc = cookie;
	int error;
	uint8_t nstate;

	error = octiic_reg_write(sc, TWSI_IA_CTL, TWSI_CTL_ENAB | TWSI_CTL_STA);
	if (error != 0)
		return error;

	delay(10);

	if (sc->sc_start_sent)
		nstate = TWSI_STAT_RSTART;
	else
		nstate = TWSI_STAT_START;
	error = octiic_wait(sc, nstate, flags);
	if (error != 0)
		return error;

	sc->sc_start_sent = 1;

	return 0;
}

int
octiic_i2c_send_stop(void *cookie, int flags)
{
	struct octiic_softc *sc = cookie;

	sc->sc_start_sent = 0;

	return octiic_reg_write(sc, TWSI_IA_CTL, TWSI_CTL_ENAB | TWSI_CTL_STP);
}

int
octiic_i2c_initiate_xfer(void *cookie, i2c_addr_t addr, int flags)
{
	struct octiic_softc *sc = cookie;
	int error;
	uint8_t mode = 0, nstate;

	error = octiic_i2c_send_start(sc, flags);
	if (error != 0)
		return error;

	if (flags & I2C_F_READ)
		mode = 0x01;
	nstate = flags & I2C_F_READ ? TWSI_STAT_ART_ACK : TWSI_STAT_AWT_ACK;

	/* Handle 10-bit addressing. */
	if (addr > 0x7f) {
		octiic_reg_write(sc, TWSI_IA_DATA, ((addr >> 7) << 1) | mode);
		octiic_reg_write(sc, TWSI_IA_CTL, TWSI_CTL_ENAB);

		error = octiic_wait(sc, nstate, flags);
		if (error != 0)
			return error;
	}

	octiic_reg_write(sc, TWSI_IA_DATA, ((addr & 0x7f) << 1) | mode);
	octiic_reg_write(sc, TWSI_IA_CTL, TWSI_CTL_ENAB);

	error = octiic_wait(sc, nstate, flags);
	if (error != 0)
		return error;

	return 0;
}

int
octiic_i2c_read_byte(void *cookie, uint8_t *datap, int flags)
{
	struct octiic_softc *sc = cookie;
	int error;
	uint8_t ctl, nstate;

	ctl = TWSI_CTL_ENAB;
	if ((flags & I2C_F_LAST) == 0)
		ctl |= TWSI_CTL_AAK;
	octiic_reg_write(sc, TWSI_IA_CTL, ctl);

	nstate = flags & I2C_F_LAST ? TWSI_STAT_MBR_NAK : TWSI_STAT_MBR_ACK;
	error = octiic_wait(sc, nstate, flags);
	if (error != 0)
		return error;

	octiic_reg_read(sc, TWSI_IA_DATA, datap);

	if (flags & I2C_F_STOP)
		error = octiic_i2c_send_stop(sc, flags);

	return 0;
}

int
octiic_i2c_write_byte(void *cookie, uint8_t data, int flags)
{
	struct octiic_softc *sc = cookie;
	int error;

	octiic_reg_write(sc, TWSI_IA_DATA, data);
	octiic_reg_write(sc, TWSI_IA_CTL, TWSI_CTL_ENAB);

	error = octiic_wait(sc, TWSI_STAT_MBT_ACK, flags);
	if (error != 0)
		return error;

	if (flags & I2C_F_STOP)
		error = octiic_i2c_send_stop(sc, flags);

	return error;
}

void
octiic_i2c_scan(struct device *self, struct i2cbus_attach_args *iba, void *arg)
{
	struct i2c_attach_args ia;
	char name[32];
	uint32_t reg[1];
	struct octiic_softc *sc = arg;
	int node;

	for (node = OF_child(sc->sc_node); node != 0; node = OF_peer(node)) {
		memset(name, 0, sizeof(name));
		memset(reg, 0, sizeof(reg));

		if (OF_getprop(node, "compatible", name, sizeof(name)) == -1)
			continue;
		if (name[0] == '\0')
			continue;

		if (OF_getprop(node, "reg", &reg, sizeof(reg)) != sizeof(reg))
			continue;

		memset(&ia, 0, sizeof(ia));
		ia.ia_tag = iba->iba_tag;
		ia.ia_addr = reg[0];
		ia.ia_name = name;
		ia.ia_cookie = &node;
		config_found(self, &ia, iic_print);
	}
}

int
octiic_reg_read(struct octiic_softc *sc, uint8_t reg, uint8_t *pval)
{
	uint64_t data;
	int timeout;

	TWSI_WR_8(sc, TWSI_SW_TWSI, TWSI_SW_TWSI_V | TWSI_SW_TWSI_R |
	    ((uint64_t)TWSI_OP_EOP << TWSI_SW_TWSI_OP_S) |
	    ((uint64_t)reg << TWSI_SW_TWSI_EOP_IA_S));

	for (timeout = 100000; timeout > 0; timeout--) {
		data = TWSI_RD_8(sc, TWSI_SW_TWSI);
		if ((data & TWSI_SW_TWSI_V) == 0)
			break;
		delay(1);
	}
	if (timeout == 0)
		return ETIMEDOUT;

	*pval = (uint8_t)data;
	return 0;
}

int
octiic_reg_write(struct octiic_softc *sc, uint8_t reg, uint8_t val)
{
	uint64_t data;
	int timeout;

	TWSI_WR_8(sc, TWSI_SW_TWSI, TWSI_SW_TWSI_V |
	    ((uint64_t)TWSI_OP_EOP << TWSI_SW_TWSI_OP_S) |
	    ((uint64_t)reg << TWSI_SW_TWSI_EOP_IA_S) | val);

	for (timeout = 100000; timeout > 0; timeout--) {
		data = TWSI_RD_8(sc, TWSI_SW_TWSI);
		if ((data & TWSI_SW_TWSI_V) == 0)
			break;
		delay(1);
	}
	if (timeout == 0)
		return ETIMEDOUT;

	return 0;
}

/*
 * Wait until the controller has finished current operation.
 * Fail if the new state is not `nstate'.
 */
int
octiic_wait(struct octiic_softc *sc, uint8_t nstate, int flags)
{
	uint8_t ctl, stat;
	int timeout;

	for (timeout = 100000; timeout > 0; timeout--) {
		octiic_reg_read(sc, TWSI_IA_CTL, &ctl);
		if (ctl & TWSI_CTL_IFLG)
			break;
	}

	octiic_reg_read(sc, TWSI_IA_STAT, &stat);
	if (stat != nstate)
		return EIO;

	return 0;
}

int
octiic_set_clock(struct octiic_softc *sc, uint32_t freq)
{
	uint64_t best_tclk = 0, tclk;
	uint64_t ioclk = octeon_ioclock_speed();
	int best_m = 2, best_n = 0, best_thp = 24;
	int m, n, thp;

	/*
	 * Find a combination of clock dividers `thp', `m' and `n' that gives
	 * bus frequency close to but no more than `freq'.
	 */
#define TCLK(ioclk, thp, n, m) \
    ((ioclk) / (20 * ((thp) + 1) * (1 << (n)) * ((m) + 1)))
	for (thp = 6; thp <= 72 && best_tclk < freq; thp <<= 1) {
		for (n = 7; n > 0; n--) {
			if (TCLK(ioclk, thp, n, 16) > freq)
				break;
		}
		for (m = 15; m > 2; m--) {
			if (TCLK(ioclk, thp, n, m - 1) > freq)
				break;
		}

		tclk = TCLK(ioclk, thp, n, m);
		if (tclk <= freq && tclk > best_tclk) {
			best_tclk = tclk;
			best_thp = thp;
			best_m = m;
			best_n = n;
		}
	}
#undef TCLK

	TWSI_WR_8(sc, TWSI_SW_TWSI, TWSI_SW_TWSI_V |
	    ((uint64_t)TWSI_OP_CLK << TWSI_SW_TWSI_OP_S) | best_thp);

	octiic_reg_write(sc, TWSI_IA_CLKCTL, (best_m << 3) | best_n);

	return 0;
}
