/*	$OpenBSD: ccpmic.c,v 1.3 2022/04/06 18:59:27 naddy Exp $	*/
/*
 * Copyright (c) 2018 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/kernel.h>
#include <sys/malloc.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <dev/i2c/i2cvar.h>

#define CCPMIC_GPIO0P0CTLO		0x2b
#define CCPMIC_GPIO0P0CTLI		0x33
#define CCPMIC_GPIO1P0CTLO		0x3b
#define CCPMIC_GPIO1P0CTLI		0x43
#define CCPMIC_GPIOPANELCTL		0x52
#define  CCPMIC_GPIOCTLO_RVAL_2KUP	(1 << 1)
#define  CCPMIC_GPIOCTLO_DRV_REN	(1 << 3)
#define  CCPMIC_GPIOCTLO_DIR_OUT	(1 << 5)
#define  CCPMIC_GPIOCTLI_VALUE		(1 << 0)

#define CCPMIC_GPIOCTLO_INPUT \
    (CCPMIC_GPIOCTLO_DRV_REN | CCPMIC_GPIOCTLO_RVAL_2KUP)
#define CCPMIC_GPIOCTLO_OUTPUT \
    (CCPMIC_GPIOCTLO_INPUT | CCPMIC_GPIOCTLO_DIR_OUT)

#define CCPMIC_V1P8SX			0x5d
#define CCPMIC_V1P2SX			0x61
#define CCPMIC_V2P85SX			0x66
#define  CCPMIC_PWR_ON			(1 << 0)
#define  CCPMIC_PWR_SEL			(1 << 1)
#define CCPMIC_SYS2_THRM_RSLT_H		0x78
#define CCPMIC_SYS2_THRM_RSLT_L		0x79

#define CCPMIC_REGIONSPACE_THERMAL	0x8c
#define CCPMIC_REGIONSPACE_POWER	0x8d

#define CCPMIC_NPINS			16

struct acpi_lpat {
	int32_t temp;
	int32_t raw;
};

struct ccpmic_softc {
	struct device	sc_dev;
	struct acpi_softc *sc_acpi;
	struct aml_node *sc_node;
	i2c_tag_t	sc_tag;
	i2c_addr_t	sc_addr;

	struct acpi_lpat *sc_lpat;
	size_t		sc_lpat_len;

	struct acpi_gpio sc_gpio;
};

int	ccpmic_match(struct device *, void *, void *);
void	ccpmic_attach(struct device *, struct device *, void *);

const struct cfattach ccpmic_ca = {
	sizeof(struct ccpmic_softc), ccpmic_match, ccpmic_attach
};

struct cfdriver ccpmic_cd = {
	NULL, "ccpmic", DV_DULL
};

uint8_t	ccpmic_read_1(struct ccpmic_softc *, uint8_t, int);
void	ccpmic_write_1(struct ccpmic_softc *, uint8_t, uint8_t, int);
void	ccpmic_get_lpat(struct ccpmic_softc *);
int32_t	ccpmic_raw_to_temp(struct ccpmic_softc *, int32_t);
int	ccpmic_thermal_opreg_handler(void *, int, uint64_t, int, uint64_t *);
int	ccpmic_power_opreg_handler(void *, int, uint64_t, int, uint64_t *);
int	ccpmic_read_pin(void *, int);
void	ccpmic_write_pin(void *, int, int);

int
ccpmic_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	return (strcmp(ia->ia_name, "INT33FD") == 0);
}

void
ccpmic_attach(struct device *parent, struct device *self, void *aux)
{
	struct ccpmic_softc *sc = (struct ccpmic_softc *)self;
	struct i2c_attach_args *ia = aux;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;
	sc->sc_acpi = acpi_softc;
	sc->sc_node = ia->ia_cookie;

	printf("\n");

	ccpmic_get_lpat(sc);
	if (sc->sc_lpat == NULL)
		return;

	sc->sc_gpio.cookie = sc;
	sc->sc_gpio.read_pin = ccpmic_read_pin;
	sc->sc_gpio.write_pin = ccpmic_write_pin;
	sc->sc_node->gpio = &sc->sc_gpio;
	acpi_register_gpio(sc->sc_acpi, sc->sc_node);

	/* Register OEM defined address space. */
	aml_register_regionspace(sc->sc_node, CCPMIC_REGIONSPACE_THERMAL,
	    sc, ccpmic_thermal_opreg_handler);
	aml_register_regionspace(sc->sc_node, CCPMIC_REGIONSPACE_POWER,
	    sc, ccpmic_power_opreg_handler);
}

uint8_t
ccpmic_read_1(struct ccpmic_softc *sc, uint8_t reg, int flags)
{
	uint8_t val;
	int error;

	iic_acquire_bus(sc->sc_tag, flags);
	error = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &reg, sizeof(reg), &val, sizeof(val), flags);
	iic_release_bus(sc->sc_tag, flags);

	if (error) {
		printf("%s: can't read register 0x%02x\n",
		    sc->sc_dev.dv_xname, reg);
		val = 0xff;
	}

	return val;
}

void
ccpmic_write_1(struct ccpmic_softc *sc, uint8_t reg, uint8_t val, int flags)
{
	int error;

	iic_acquire_bus(sc->sc_tag, flags);
	error = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
	    &reg, sizeof(reg), &val, sizeof(val), flags);
	iic_release_bus(sc->sc_tag, flags);

	if (error) {
		printf("%s: can't write register 0x%02x\n",
		    sc->sc_dev.dv_xname, reg);
	}
}

void
ccpmic_get_lpat(struct ccpmic_softc *sc)
{
	struct aml_value res;
	int i;

	if (aml_evalname(sc->sc_acpi, sc->sc_node, "LPAT", 0, NULL, &res))
		return;
	if (res.type != AML_OBJTYPE_PACKAGE)
		goto out;
	if (res.length < 4 || (res.length % 2) != 0)
		goto out;

	sc->sc_lpat_len = res.length / 2;
	sc->sc_lpat = mallocarray(sc->sc_lpat_len, sizeof(struct acpi_lpat),
	    M_DEVBUF, M_WAITOK);

	for (i = 0; i < sc->sc_lpat_len; i++) {
		sc->sc_lpat[i].temp = aml_val2int(res.v_package[2 * i]);
		sc->sc_lpat[i].raw = aml_val2int(res.v_package[2 * i + 1]);
	}

out:
	aml_freevalue(&res);
}

int32_t
ccpmic_raw_to_temp(struct ccpmic_softc *sc, int32_t raw)
{
	struct acpi_lpat *lpat = sc->sc_lpat;
	int32_t raw0, delta_raw;
	int32_t temp0, delta_temp;
	int i;

	for (i = 1; i < sc->sc_lpat_len; i++) {
		/* Coefficient can be positive or negative. */
		if (raw >= lpat[i - 1].raw && raw <= lpat[i].raw)
			break;
		if (raw <= lpat[i - 1].raw && raw >= lpat[i].raw)
			break;
	}
	if (i == sc->sc_lpat_len)
		return -1;

	raw0 = lpat[i - 1].raw;
	temp0 = lpat[i - 1].temp;
	delta_raw = lpat[i].raw - raw0;
	delta_temp = lpat[i].temp - temp0;

	return temp0 + (raw - raw0) * delta_temp / delta_raw;
}

struct ccpmic_regmap {
	uint8_t address;
	uint8_t hi, lo;
};

struct ccpmic_regmap ccpmic_thermal_regmap[] = {
	{ 0x18, CCPMIC_SYS2_THRM_RSLT_H, CCPMIC_SYS2_THRM_RSLT_L }, /* TMP2 */
};

int
ccpmic_thermal_opreg_handler(void *cookie, int iodir, uint64_t address,
    int size, uint64_t *value)
{
	struct ccpmic_softc *sc = cookie;
	int32_t temp;
	uint16_t raw;
	uint8_t lo, hi;
	int i;

	/* Only allow 32-bit read access. */
	if (size != 4 || iodir != ACPI_IOREAD)
		return -1;

	for (i = 0; i < nitems(ccpmic_thermal_regmap); i++) {
		if (address == ccpmic_thermal_regmap[i].address)
			break;
	}
	if (i == nitems(ccpmic_thermal_regmap)) {
		printf("%s: addr 0x%02llx\n", __func__, address);
		return -1;
	}

	lo = ccpmic_thermal_regmap[i].lo;
	hi = ccpmic_thermal_regmap[i].hi;
	raw = ccpmic_read_1(sc, lo, 0);
	raw |= (ccpmic_read_1(sc, hi, 0) & 0x03) << 8;

	temp = ccpmic_raw_to_temp(sc, raw);
	if (temp < 0)
		return -1;

	*value = temp;
	return 0;
}

struct ccpmic_regmap ccpmic_power_regmap[] = {
	{ 0x24, CCPMIC_V2P85SX },	/* X285 */
	{ 0x48, CCPMIC_V1P8SX },	/* V18X */
	{ 0x50, CCPMIC_V1P2SX },	/* V12X */
};

int
ccpmic_power_opreg_handler(void *cookie, int iodir, uint64_t address,
    int size, uint64_t *value)
{
	struct ccpmic_softc *sc = cookie;
	uint8_t reg, val;
	int i;

	/* Only allow 32-bit access. */
	if (size != 4)
		return -1;

	for (i = 0; i < nitems(ccpmic_power_regmap); i++) {
		if (address == ccpmic_power_regmap[i].address)
			break;
	}
	if (i == nitems(ccpmic_power_regmap)) {
		printf("%s: addr 0x%02llx\n", __func__, address);
		return -1;
	}

	reg = ccpmic_power_regmap[i].hi;
	val = ccpmic_read_1(sc, reg, 0);
	if (iodir == ACPI_IOREAD) {
		if ((val & CCPMIC_PWR_SEL) && (val & CCPMIC_PWR_ON))
			*value = 1;
		else
			*value = 0;
	} else {
		if (*value)
			val |= CCPMIC_PWR_ON;
		else
			val &= ~CCPMIC_PWR_ON;
		ccpmic_write_1(sc, reg, val | CCPMIC_PWR_SEL, 0);
	}

	return 0;
}

/* 
 * We have 16 real GPIOs and a bunch of virtual ones.  The virtual
 * ones are mostly there to deal with a limitation of Microsoft
 * Windows.  We only implement the "panel" control GPIO, which
 * actually maps onto a real GPIO.
 */

int
ccpmic_read_pin(void *cookie, int pin)
{
	struct ccpmic_softc *sc = cookie;
	uint8_t reg;

	if (pin >= CCPMIC_NPINS)
		return 0;

	reg = ((pin < 8) ? CCPMIC_GPIO0P0CTLO : CCPMIC_GPIO1P0CTLO) + pin % 8;
	ccpmic_write_1(sc, reg, CCPMIC_GPIOCTLO_INPUT, 0);
	reg = ((pin < 8) ? CCPMIC_GPIO0P0CTLI : CCPMIC_GPIO1P0CTLI) + pin % 8;
	return ccpmic_read_1(sc, reg, 0) & CCPMIC_GPIOCTLI_VALUE;
}

void
ccpmic_write_pin(void *cookie, int pin, int value)
{
	struct ccpmic_softc *sc = cookie;
	uint8_t reg;

	if (pin == 0x5e) {
		reg = CCPMIC_GPIOPANELCTL;
		ccpmic_write_1(sc, reg, CCPMIC_GPIOCTLO_OUTPUT | !!value, 0);
		return;
	}

	if (pin >= CCPMIC_NPINS)
		return;

	reg = ((pin < 8) ? CCPMIC_GPIO0P0CTLO : CCPMIC_GPIO1P0CTLO) + pin % 8;
	ccpmic_write_1(sc, reg, CCPMIC_GPIOCTLO_OUTPUT | !!value, 0);
}
