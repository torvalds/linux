/*	$OpenBSD: bd718x7.c,v 1.5 2022/06/28 23:43:12 naddy Exp $	*/
/*
 * Copyright (c) 2019 Patrick Wildt <patrick@blueri.se>
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
#include <sys/malloc.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

#include <dev/i2c/i2cvar.h>

#define BDPMIC_REGLOCK			0x2f
#define  BDPMIC_REGLOCK_PWRSEQ			(1 << 0)
#define  BDPMIC_REGLOCK_VREG			(1 << 4)

struct bdpmic_regdata {
	const char *name;
	uint8_t reg, mask;
	uint32_t base, delta;
};

const struct bdpmic_regdata bd71837_regdata[] = {
	{ "BUCK2", 0x10, 0x3f, 700000, 10000 },
	{ }
};

const struct bdpmic_regdata bd71847_regdata[] = {
	{ "BUCK2", 0x10, 0x3f, 700000, 10000 },
	{ }
};

struct bdpmic_softc {
	struct device	sc_dev;
	i2c_tag_t	sc_tag;
	i2c_addr_t	sc_addr;

	const struct bdpmic_regdata *sc_regdata;
};

int	bdpmic_match(struct device *, void *, void *);
void	bdpmic_attach(struct device *, struct device *, void *);

void	bdpmic_attach_regulator(struct bdpmic_softc *, int);
uint8_t	bdpmic_reg_read(struct bdpmic_softc *, int);
void	bdpmic_reg_write(struct bdpmic_softc *, int, uint8_t);

const struct cfattach bdpmic_ca = {
	sizeof(struct bdpmic_softc), bdpmic_match, bdpmic_attach
};

struct cfdriver bdpmic_cd = {
	NULL, "bdpmic", DV_DULL
};

int
bdpmic_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	return (strcmp(ia->ia_name, "rohm,bd71837") == 0 ||
	    strcmp(ia->ia_name, "rohm,bd71847") == 0);
}

void
bdpmic_attach(struct device *parent, struct device *self, void *aux)
{
	struct bdpmic_softc *sc = (struct bdpmic_softc *)self;
	struct i2c_attach_args *ia = aux;
	int node = *(int *)ia->ia_cookie;
	const char *chip;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	if (OF_is_compatible(node, "rohm,bd71837")) {
		chip = "BD71837";
		sc->sc_regdata = bd71837_regdata;
	} else {
		chip = "BD71847";
		sc->sc_regdata = bd71847_regdata;
	}
	printf(": %s\n", chip);

	node = OF_getnodebyname(node, "regulators");
	if (node == 0)
		return;
	for (node = OF_child(node); node; node = OF_peer(node))
		bdpmic_attach_regulator(sc, node);
}

struct bdpmic_regulator {
	struct bdpmic_softc *bd_sc;

	uint8_t bd_reg, bd_mask;
	uint32_t bd_base, bd_delta;

	struct regulator_device bd_rd;
};

uint32_t bdpmic_get_voltage(void *);
int	bdpmic_set_voltage(void *, uint32_t);

void
bdpmic_attach_regulator(struct bdpmic_softc *sc, int node)
{
	struct bdpmic_regulator *bd;
	char name[32];
	int i;

	name[0] = 0;
	OF_getprop(node, "name", name, sizeof(name));
	name[sizeof(name) - 1] = 0;
	for (i = 0; sc->sc_regdata[i].name; i++) {
		if (strcmp(sc->sc_regdata[i].name, name) == 0)
			break;
	}
	if (sc->sc_regdata[i].name == NULL)
		return;

	bd = malloc(sizeof(*bd), M_DEVBUF, M_WAITOK | M_ZERO);
	bd->bd_sc = sc;

	bd->bd_reg = sc->sc_regdata[i].reg;
	bd->bd_mask = sc->sc_regdata[i].mask;
	bd->bd_base = sc->sc_regdata[i].base;
	bd->bd_delta = sc->sc_regdata[i].delta;

	bd->bd_rd.rd_node = node;
	bd->bd_rd.rd_cookie = bd;
	bd->bd_rd.rd_get_voltage = bdpmic_get_voltage;
	bd->bd_rd.rd_set_voltage = bdpmic_set_voltage;
	regulator_register(&bd->bd_rd);
}

uint32_t
bdpmic_get_voltage(void *cookie)
{
	struct bdpmic_regulator *bd = cookie;
	uint8_t vsel;

	vsel = bdpmic_reg_read(bd->bd_sc, bd->bd_reg);
	return bd->bd_base + (vsel & bd->bd_mask) * bd->bd_delta;
}

int
bdpmic_set_voltage(void *cookie, uint32_t voltage)
{
	struct bdpmic_regulator *bd = cookie;
	uint32_t vmin = bd->bd_base;
	uint32_t vmax = vmin + bd->bd_mask * bd->bd_delta;
	uint8_t vsel;

	if (voltage < vmin || voltage > vmax)
		return EINVAL;

	/* Unlock */
	bdpmic_reg_write(bd->bd_sc, BDPMIC_REGLOCK,
	    BDPMIC_REGLOCK_PWRSEQ);

	vsel = bdpmic_reg_read(bd->bd_sc, bd->bd_reg);
	vsel &= ~bd->bd_mask;
	vsel |= (voltage - bd->bd_base) / bd->bd_delta;
	bdpmic_reg_write(bd->bd_sc, bd->bd_reg, vsel);

	/* Lock */
	bdpmic_reg_write(bd->bd_sc, BDPMIC_REGLOCK,
	    BDPMIC_REGLOCK_PWRSEQ | BDPMIC_REGLOCK_VREG);

	return 0;
}

uint8_t
bdpmic_reg_read(struct bdpmic_softc *sc, int reg)
{
	uint8_t cmd = reg;
	uint8_t val;
	int error;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	error = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &val, sizeof val, I2C_F_POLL);
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	if (error) {
		printf("%s: can't read register 0x%02x\n",
		    sc->sc_dev.dv_xname, reg);
		val = 0xff;
	}

	return val;
}

void
bdpmic_reg_write(struct bdpmic_softc *sc, int reg, uint8_t val)
{
	uint8_t cmd = reg;
	int error;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	error = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &val, sizeof val, I2C_F_POLL);
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	if (error) {
		printf("%s: can't write register 0x%02x\n",
		    sc->sc_dev.dv_xname, reg);
	}
}
