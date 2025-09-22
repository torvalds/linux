/* $OpenBSD: tps65090.c,v 1.6 2021/10/24 17:52:27 mpi Exp $ */
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
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/i2c/i2cvar.h>

#define	REG_IRQ1		0x00
#define	REG_IRQ2		0x01
#define	REG_IRQ1MASK		0x02
#define	REG_IRQ2MASK		0x03
#define	REG_CG_CTRL0		0x04
#define	 REG_CG_CTRL0_ENC_MASK		(1 << 0)
#define	REG_CG_CTRL1		0x05
#define	REG_CG_CTRL2		0x06
#define	REG_CG_CTRL3		0x07
#define	REG_CG_CTRL4		0x08
#define	REG_CG_CTRL5		0x09
#define	REG_CG_STATUS1		0x0a
#define	REG_CG_STATUS2		0x0b
#define	REG_DCDC1_CTRL		0x0c
#define	REG_DCDC2_CTRL		0x0d
#define	REG_DCDC3_CTRL		0x0e
#define	REG_FET1_CTRL		0x0f
#define	REG_FET2_CTRL		0x10
#define	REG_FET3_CTRL		0x11
#define	REG_FET4_CTRL		0x12
#define	REG_FET5_CTRL		0x13
#define	REG_FET6_CTRL		0x14
#define	REG_FET7_CTRL		0x15
#define	REG_FETx_CTRL(x)	(0x0e + (x))
#define	 REG_FETx_CTRL_ENFET		(1 << 0) /* Enable FET */
#define	 REG_FETx_CTRL_ADENFET		(1 << 1) /* Enable output auto discharge */
#define	 REG_FETx_CTRL_WAIT		(3 << 2) /* Overcurrent timeout max */
#define	 REG_FETx_CTRL_PGFET		(1 << 4) /* Power good for FET status */
#define	 REG_FETx_CTRL_TOFET		(1 << 7) /* Timeout, startup, overload */
#define	REG_AD_CTRL		0x16
#define	REG_AD_OUT1		0x17
#define	REG_AD_OUT2		0x18

#define	NFET			7

#ifdef DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

struct tps65090_softc {
	struct device		sc_dev;
	i2c_tag_t sc_tag;
	i2c_addr_t sc_addr;
};

struct tps65090_softc *tps65090_sc;

int	tps65090_match(struct device *, void *, void *);
void	tps65090_attach(struct device *, struct device *, void *);

int	tps65090_read_reg(struct tps65090_softc *, uint8_t);
void	tps65090_write_reg(struct tps65090_softc *, uint8_t, uint8_t);
int	tps65090_fet_set(int, int);
int	tps65090_fet_get(int);
void	tps65090_fet_enable(int);
void	tps65090_fet_disable(int);
int	tps65090_get_charging(void);
void	tps65090_set_charging(int);

const struct cfattach tpspmic_ca = {
	sizeof(struct tps65090_softc), tps65090_match, tps65090_attach
};

struct cfdriver tpspmic_cd = {
	NULL, "tpspmic", DV_DULL
};

int
tps65090_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "ti,tps65090") == 0)
		return (1);
	return (0);
}

void
tps65090_attach(struct device *parent, struct device *self, void *aux)
{
	struct tps65090_softc *sc = (struct tps65090_softc *)self;
	struct i2c_attach_args *ia = aux;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;
	tps65090_sc = sc;

	printf("\n");
}

/*
 * FET1: Backlight on the Chromebook
 * FET6: LCD panel on the Chromebook
 */
void
tps65090_fet_enable(int fet)
{
	int i;

	if (fet < 1 || fet > NFET)
		return;

	for (i = 0; i < 10; i++) {
		if (!tps65090_fet_set(fet, 1))
			break;

		if (i != 9)
			tps65090_fet_set(fet, 0);
	}
}

void
tps65090_fet_disable(int fet)
{
	if (fet < 1 || fet > NFET)
		return;

	tps65090_fet_set(fet, 0);
}

int
tps65090_fet_set(int fet, int set)
{
	struct tps65090_softc *sc = tps65090_sc;
	int i;
	uint8_t val, check;

	val = REG_FETx_CTRL_ADENFET | REG_FETx_CTRL_WAIT;
	if (set)
		val |= REG_FETx_CTRL_ENFET;

	tps65090_write_reg(sc, REG_FETx_CTRL(fet), val);
	for (i = 0; i < 5; i++) {
		check = tps65090_read_reg(sc, REG_FETx_CTRL(fet));

		/* FET state correct? */
		if (!!(check & REG_FETx_CTRL_PGFET) == set)
			return 0;

		/* Timeout, don't need to try again. */
		if (check & REG_FETx_CTRL_TOFET)
			break;

		delay(1000);
	}

	return -1;
}

int
tps65090_fet_get(int fet)
{
	struct tps65090_softc *sc = tps65090_sc;
	uint8_t val = tps65090_read_reg(sc, REG_FETx_CTRL(fet));
	return val & REG_FETx_CTRL_ENFET;
}

int
tps65090_get_charging(void)
{
	struct tps65090_softc *sc = tps65090_sc;
	uint8_t val = tps65090_read_reg(sc, REG_CG_CTRL0);
	return val & REG_CG_CTRL0_ENC_MASK;
}

void
tps65090_set_charging(int set)
{
	struct tps65090_softc *sc = tps65090_sc;
	uint8_t val = tps65090_read_reg(sc, REG_CG_CTRL0);
	if (set)
		val |= REG_CG_CTRL0_ENC_MASK;
	else
		val &= REG_CG_CTRL0_ENC_MASK;
	tps65090_write_reg(sc, REG_CG_CTRL0, val);
}

int
tps65090_read_reg(struct tps65090_softc *sc, uint8_t cmd)
{
	uint8_t val;

	iic_acquire_bus(sc->sc_tag, 0);
	iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, 1, &val, 1, 0);
	iic_release_bus(sc->sc_tag, 0);

	return val;
}

void
tps65090_write_reg(struct tps65090_softc *sc, uint8_t cmd, uint8_t val)
{
	iic_acquire_bus(sc->sc_tag, 0);
	iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, 1, &val, 1, 0);
	iic_release_bus(sc->sc_tag, 0);
}
