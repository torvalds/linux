/* $OpenBSD: imxiic.c,v 1.2 2024/04/14 03:26:25 jsg Exp $ */
/*
 * Copyright (c) 2013 Patrick Wildt <patrick@blueri.se>
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

#include <machine/bus.h>

#include <dev/ic/imxiicvar.h>

/* registers */
#define I2C_IADR	0x00
#define I2C_IFDR	0x01
#define I2C_I2CR	0x02
#define I2C_I2SR	0x03
#define I2C_I2DR	0x04

#define I2C_I2CR_RSTA	(1 << 2)
#define I2C_I2CR_TXAK	(1 << 3)
#define I2C_I2CR_MTX	(1 << 4)
#define I2C_I2CR_MSTA	(1 << 5)
#define I2C_I2CR_IIEN	(1 << 6)
#define I2C_I2CR_IEN	(1 << 7)
#define I2C_I2SR_RXAK	(1 << 0)
#define I2C_I2SR_IIF	(1 << 1)
#define I2C_I2SR_IAL	(1 << 4)
#define I2C_I2SR_IBB	(1 << 5)

void imxiic_enable(struct imxiic_softc *, int);
void imxiic_clear_iodone(struct imxiic_softc *);
void imxiic_setspeed(struct imxiic_softc *, u_int);
int imxiic_wait_state(struct imxiic_softc *, uint32_t, uint32_t);
int imxiic_read(struct imxiic_softc *, int, const void *, int,
    void *, int);
int imxiic_write(struct imxiic_softc *, int, const void *, int,
    const void *, int);

int imxiic_i2c_acquire_bus(void *, int);
void imxiic_i2c_release_bus(void *, int);
int imxiic_i2c_exec(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
    void *, size_t, int);

uint8_t imxiic_read_1(struct imxiic_softc *, int);
void imxiic_write_1(struct imxiic_softc *, int, uint8_t);

#define HREAD1(sc, reg)							\
	imxiic_read_1((sc), (reg))
#define HWRITE1(sc, reg, val)						\
	imxiic_write_1((sc), (reg), (val))
#define HSET1(sc, reg, bits)						\
	HWRITE1((sc), (reg), HREAD1((sc), (reg)) | (bits))
#define HCLR1(sc, reg, bits)						\
	HWRITE1((sc), (reg), HREAD1((sc), (reg)) & ~(bits))

struct cfdriver imxiic_cd = {
	NULL, "imxiic", DV_DULL
};

void
imxiic_enable(struct imxiic_softc *sc, int on)
{
	/*
	 * VF610: write 1 to clear bits
	 * iMX21: write 0 to clear bits
	 */
	if (sc->sc_type == I2C_TYPE_VF610)
		HWRITE1(sc, I2C_I2SR, I2C_I2SR_IAL | I2C_I2SR_IIF);
	else
		HWRITE1(sc, I2C_I2SR, 0);

	/* VF610 inverts enable bit meaning */
	if (sc->sc_type == I2C_TYPE_VF610)
		on = !on;
	if (on)
		HWRITE1(sc, I2C_I2CR, I2C_I2CR_IEN);
	else
		HWRITE1(sc, I2C_I2CR, 0);
}

void
imxiic_clear_iodone(struct imxiic_softc *sc)
{
	/*
	 * VF610: write bit to clear bit
	 * iMX21: clear bit, keep rest
	 */
	if (sc->sc_type == I2C_TYPE_VF610)
		HWRITE1(sc, I2C_I2SR, I2C_I2SR_IIF);
	else
		HCLR1(sc, I2C_I2SR, I2C_I2SR_IIF);
}

void
imxiic_setspeed(struct imxiic_softc *sc, u_int speed)
{
	if (!sc->frequency) {
		uint32_t div;
		int i;

		div = (sc->sc_clkrate + speed - 1) / speed;
		if (div < sc->sc_clk_div[0].div)
			i = 0;
		else if (div > sc->sc_clk_div[sc->sc_clk_ndiv - 1].div)
			i = sc->sc_clk_ndiv - 1;
		else
			for (i = 0; sc->sc_clk_div[i].div < div; i++)
				;

		sc->frequency = sc->sc_clk_div[i].val;
	}

	HWRITE1(sc, I2C_IFDR, sc->frequency);
}

int
imxiic_wait_state(struct imxiic_softc *sc, uint32_t mask, uint32_t value)
{
	uint32_t state;
	int timeout;
	for (timeout = 1000; timeout > 0; timeout--) {
		if (((state = HREAD1(sc, I2C_I2SR)) & mask) == value)
			return 0;
		delay(10);
	}
	return ETIMEDOUT;
}

int
imxiic_read(struct imxiic_softc *sc, int addr, const void *cmd, int cmdlen,
    void *data, int len)
{
	int i;

	if (cmdlen > 0) {
		if (imxiic_write(sc, addr, cmd, cmdlen, NULL, 0))
			return (EIO);

		HSET1(sc, I2C_I2CR, I2C_I2CR_RSTA);
		delay(1);
		if (imxiic_wait_state(sc, I2C_I2SR_IBB, I2C_I2SR_IBB))
			return (EIO);
	}

	imxiic_clear_iodone(sc);
	HWRITE1(sc, I2C_I2DR, (addr << 1) | 1);

	if (imxiic_wait_state(sc, I2C_I2SR_IIF, I2C_I2SR_IIF))
		return (EIO);
	imxiic_clear_iodone(sc);
	if (HREAD1(sc, I2C_I2SR) & I2C_I2SR_RXAK)
		return (EIO);

	HCLR1(sc, I2C_I2CR, I2C_I2CR_MTX);
	if (len - 1)
		HCLR1(sc, I2C_I2CR, I2C_I2CR_TXAK);

	/* dummy read */
	HREAD1(sc, I2C_I2DR);

	for (i = 0; i < len; i++) {
		if (imxiic_wait_state(sc, I2C_I2SR_IIF, I2C_I2SR_IIF))
			return (EIO);
		imxiic_clear_iodone(sc);

		if (i == (len - 1)) {
			HCLR1(sc, I2C_I2CR, I2C_I2CR_MSTA | I2C_I2CR_MTX);
			imxiic_wait_state(sc, I2C_I2SR_IBB, 0);
			sc->stopped = 1;
		} else if (i == (len - 2)) {
			HSET1(sc, I2C_I2CR, I2C_I2CR_TXAK);
		}
		((uint8_t*)data)[i] = HREAD1(sc, I2C_I2DR);
	}

	return 0;
}

int
imxiic_write(struct imxiic_softc *sc, int addr, const void *cmd, int cmdlen,
    const void *data, int len)
{
	int i;

	imxiic_clear_iodone(sc);
	HWRITE1(sc, I2C_I2DR, addr << 1);

	if (imxiic_wait_state(sc, I2C_I2SR_IIF, I2C_I2SR_IIF))
		return (EIO);
	imxiic_clear_iodone(sc);
	if (HREAD1(sc, I2C_I2SR) & I2C_I2SR_RXAK)
		return (EIO);

	for (i = 0; i < cmdlen; i++) {
		HWRITE1(sc, I2C_I2DR, ((uint8_t*)cmd)[i]);
		if (imxiic_wait_state(sc, I2C_I2SR_IIF, I2C_I2SR_IIF))
			return (EIO);
		imxiic_clear_iodone(sc);
		if (HREAD1(sc, I2C_I2SR) & I2C_I2SR_RXAK)
			return (EIO);
	}

	for (i = 0; i < len; i++) {
		HWRITE1(sc, I2C_I2DR, ((uint8_t*)data)[i]);
		if (imxiic_wait_state(sc, I2C_I2SR_IIF, I2C_I2SR_IIF))
			return (EIO);
		imxiic_clear_iodone(sc);
		if (HREAD1(sc, I2C_I2SR) & I2C_I2SR_RXAK)
			return (EIO);
	}
	return 0;
}

int
imxiic_i2c_acquire_bus(void *cookie, int flags)
{
	struct imxiic_softc *sc = cookie;

	rw_enter(&sc->sc_buslock, RW_WRITE);

	/* set speed */
	imxiic_setspeed(sc, sc->sc_bitrate);

	/* enable the controller */
	imxiic_enable(sc, 1);

	/* wait for it to be stable */
	delay(50);

	return 0;
}

void
imxiic_i2c_release_bus(void *cookie, int flags)
{
	struct imxiic_softc *sc = cookie;

	imxiic_enable(sc, 0);

	rw_exit(&sc->sc_buslock);
}

int
imxiic_i2c_exec(void *cookie, i2c_op_t op, i2c_addr_t addr,
    const void *cmdbuf, size_t cmdlen, void *buf, size_t len, int flags)
{
	struct imxiic_softc *sc = cookie;
	int ret = 0;

	if (!I2C_OP_STOP_P(op))
		return EINVAL;

	/* start transaction */
	HSET1(sc, I2C_I2CR, I2C_I2CR_MSTA);

	if (imxiic_wait_state(sc, I2C_I2SR_IBB, I2C_I2SR_IBB)) {
		ret = EIO;
		goto fail;
	}

	sc->stopped = 0;

	HSET1(sc, I2C_I2CR, I2C_I2CR_IIEN | I2C_I2CR_MTX | I2C_I2CR_TXAK);

	if (I2C_OP_READ_P(op)) {
		ret = imxiic_read(sc, addr, cmdbuf, cmdlen, buf, len);
	} else {
		ret = imxiic_write(sc, addr, cmdbuf, cmdlen, buf, len);
	}

fail:
	if (!sc->stopped) {
		HCLR1(sc, I2C_I2CR, I2C_I2CR_MSTA | I2C_I2CR_MTX);
		imxiic_wait_state(sc, I2C_I2SR_IBB, 0);
		sc->stopped = 1;
	}

	return ret;
}

uint8_t
imxiic_read_1(struct imxiic_softc *sc, int reg)
{
	reg <<= sc->sc_reg_shift;

	return bus_space_read_1(sc->sc_iot, sc->sc_ioh, reg);
}

void
imxiic_write_1(struct imxiic_softc *sc, int reg, uint8_t val)
{
	reg <<= sc->sc_reg_shift;

	bus_space_write_1(sc->sc_iot, sc->sc_ioh, reg, val);
}
