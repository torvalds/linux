/* $OpenBSD: mviic.c,v 1.4 2021/10/24 17:52:26 mpi Exp $ */
/*
 * Copyright (c) 2019 Patrick Wildt <patrick@blueri.se>
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
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <dev/i2c/i2cvar.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

/* registers */
#define IDBR		0x04
#define ICR		0x08
#define  ICR_START		(1 << 0)
#define  ICR_STOP		(1 << 1)
#define  ICR_NAK		(1 << 2)
#define  ICR_TX_BYTE		(1 << 3)
#define  ICR_MSTA		(1 << 5)
#define  ICR_ENABLE		(1 << 6)
#define  ICR_GCD		(1 << 7)
#define  ICR_RESET		(1 << 14)
#define  ICR_MODE_MASK		(3 << 16)
#define  ICR_MODE_FAST		(1 << 16)
#define ISR		0x0c
#define  ISR_INIT		0x7ff
#define  ISR_NAK		(1 << 1)
#define  ISR_UB			(1 << 2)
#define  ISR_IBB		(1 << 3)
#define  ISR_TXE		(1 << 6)
#define  ISR_RXF		(1 << 7)

struct mviic_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_size_t		sc_ios;
	void			*sc_ih;
	int			sc_node;
	int			sc_fastmode;

	struct rwlock		sc_buslock;
	struct i2c_controller	sc_ic;
	struct i2c_bus		sc_ib;
};

int mviic_match(struct device *, void *, void *);
void mviic_attach(struct device *, struct device *, void *);
int mviic_detach(struct device *, int);
int mviic_wait_state(struct mviic_softc *, uint32_t, uint32_t);

int mviic_i2c_acquire_bus(void *, int);
void mviic_i2c_release_bus(void *, int);
int mviic_send_start(void *, int);
int mviic_send_stop(void *, int);
int mviic_initiate_xfer(void *, i2c_addr_t, int);
int mviic_read_byte(void *, uint8_t *, int);
int mviic_write_byte(void *, uint8_t, int);

void mviic_bus_scan(struct device *, struct i2cbus_attach_args *, void *);

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

const struct cfattach mviic_ca = {
	sizeof(struct mviic_softc), mviic_match, mviic_attach, mviic_detach
};

struct cfdriver mviic_cd = {
	NULL, "mviic", DV_DULL
};

int
mviic_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "marvell,armada-3700-i2c");
}

void
mviic_attach(struct device *parent, struct device *self, void *aux)
{
	struct mviic_softc *sc = (struct mviic_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct i2cbus_attach_args iba;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	sc->sc_ios = faa->fa_reg[0].size;
	sc->sc_node = faa->fa_node;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	printf("\n");

	clock_enable(faa->fa_node, NULL);
	pinctrl_byname(faa->fa_node, "default");

	sc->sc_fastmode = (OF_getproplen(sc->sc_node,
	    "mrvl,i2c-fast-mode") == 0);

	/* reset */
	HWRITE4(sc, ICR, ICR_RESET);
	HWRITE4(sc, ISR, ISR_INIT);
	HCLR4(sc, ICR, ICR_RESET);

	/* set defaults */
	HWRITE4(sc, ICR, ICR_MSTA | ICR_GCD);
	if (sc->sc_fastmode)
		HSET4(sc, ICR, ICR_MODE_FAST);

	/* enable */
	HSET4(sc, ICR, ICR_ENABLE);
	delay(100);

	rw_init(&sc->sc_buslock, sc->sc_dev.dv_xname);

	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_acquire_bus = mviic_i2c_acquire_bus;
	sc->sc_ic.ic_release_bus = mviic_i2c_release_bus;
	sc->sc_ic.ic_exec = NULL;
	sc->sc_ic.ic_send_start = mviic_send_start;
	sc->sc_ic.ic_send_stop = mviic_send_stop;
	sc->sc_ic.ic_initiate_xfer = mviic_initiate_xfer;
	sc->sc_ic.ic_read_byte = mviic_read_byte;
	sc->sc_ic.ic_write_byte = mviic_write_byte;

	bzero(&iba, sizeof iba);
	iba.iba_name = "iic";
	iba.iba_tag = &sc->sc_ic;
	iba.iba_bus_scan = mviic_bus_scan;
	iba.iba_bus_scan_arg = &sc->sc_node;
	config_found(&sc->sc_dev, &iba, iicbus_print);

	sc->sc_ib.ib_node = sc->sc_node;
	sc->sc_ib.ib_ic = &sc->sc_ic;
	i2c_register(&sc->sc_ib);
}

int
mviic_wait_state(struct mviic_softc *sc, uint32_t mask, uint32_t value)
{
	uint32_t state;
	int timeout;

	for (timeout = 1000; timeout > 0; timeout--) {
		if (((state = HREAD4(sc, ISR)) & mask) == value)
			return 0;
		delay(10);
	}
	return ETIMEDOUT;
}

int
mviic_i2c_acquire_bus(void *cookie, int flags)
{
	struct mviic_softc *sc = cookie;

	rw_enter(&sc->sc_buslock, RW_WRITE);

	clock_enable(sc->sc_node, NULL);
	delay(50);

	return 0;
}

void
mviic_i2c_release_bus(void *cookie, int flags)
{
	struct mviic_softc *sc = cookie;

	rw_exit(&sc->sc_buslock);
}

int
mviic_send_start(void *v, int flags)
{
	struct mviic_softc *sc = v;

	HSET4(sc, ICR, ICR_START);
	return 0;
}

int
mviic_send_stop(void *v, int flags)
{
	struct mviic_softc *sc = v;

	HSET4(sc, ICR, ICR_STOP);
	return 0;
}

int
mviic_initiate_xfer(void *v, i2c_addr_t addr, int flags)
{
	struct mviic_softc *sc = v;

	if (mviic_wait_state(sc, ISR_IBB, 0))
		return EIO;

	HCLR4(sc, ICR, ICR_START);
	HCLR4(sc, ICR, ICR_STOP);
	if (flags & I2C_F_READ)
		HWRITE4(sc, IDBR, addr << 1 | 1);
	else
		HWRITE4(sc, IDBR, addr << 1);
	HSET4(sc, ICR, ICR_START);

	HSET4(sc, ICR, ICR_TX_BYTE);
	if (mviic_wait_state(sc, ISR_TXE, ISR_TXE))
		return EIO;
	HWRITE4(sc, ISR, ISR_TXE);
	if (HREAD4(sc, ISR) & ISR_NAK)
		return EIO;

	return 0;
}

int
mviic_read_byte(void *v, uint8_t *valp, int flags)
{
	struct mviic_softc *sc = v;

	if (mviic_wait_state(sc, ISR_IBB, 0))
		return EIO;

	HCLR4(sc, ICR, ICR_START);
	HCLR4(sc, ICR, ICR_STOP);
	HCLR4(sc, ICR, ICR_NAK);
	if ((flags & (I2C_F_LAST | I2C_F_STOP)) == (I2C_F_LAST | I2C_F_STOP))
		HSET4(sc, ICR, ICR_STOP);
	if (flags & I2C_F_LAST)
		HSET4(sc, ICR, ICR_NAK);

	HSET4(sc, ICR, ICR_TX_BYTE);
	if (mviic_wait_state(sc, ISR_RXF, ISR_RXF))
		return EIO;
	HWRITE4(sc, ISR, ISR_RXF);
	*valp = HREAD4(sc, IDBR);

	return 0;
}

int
mviic_write_byte(void *v, uint8_t val, int flags)
{
	struct mviic_softc *sc = v;

	HCLR4(sc, ICR, ICR_START);
	HCLR4(sc, ICR, ICR_STOP);
	HWRITE4(sc, IDBR, val);
	if (flags & I2C_F_STOP)
		HSET4(sc, ICR, ICR_STOP);

	HSET4(sc, ICR, ICR_TX_BYTE);
	if (mviic_wait_state(sc, ISR_TXE, ISR_TXE))
		return EIO;
	HWRITE4(sc, ISR, ISR_TXE);
	if (HREAD4(sc, ISR) & ISR_NAK)
		return EIO;

	return 0;
}

int
mviic_detach(struct device *self, int flags)
{
	struct mviic_softc *sc = (struct mviic_softc *)self;

	HCLR4(sc, ICR, ICR_ENABLE);
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
	return 0;
}

void
mviic_bus_scan(struct device *self, struct i2cbus_attach_args *iba, void *aux)
{
	int iba_node = *(int *)aux;
	extern int iic_print(void *, const char *);
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
