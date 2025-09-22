/*	$OpenBSD: tipmic.c,v 1.8 2023/03/04 01:23:40 dlg Exp $	*/
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

#define TIPMIC_INTR_STAT		0x01
#define  TIPMIC_INTR_STAT_ADC		(1 << 2)
#define TIPMIC_INTR_MASK		0x02
#define  TIPMIC_INTR_MASK_ADC		(1 << 2)
#define  TIPMIC_INTR_MASK_ALL		0xff
#define TIPMIC_LDO1_CTRL		0x41
#define TIPMIC_LDO2_CTRL		0x42
#define TIPMIC_LDO3_CTRL		0x43
#define TIPMIC_LDO5_CTRL		0x45
#define TIPMIC_LDO6_CTRL		0x46
#define TIPMIC_LDO7_CTRL		0x47
#define TIPMIC_LDO8_CTRL		0x48
#define TIPMIC_LDO9_CTRL		0x49
#define TIPMIC_LDO10_CTRL		0x4a
#define TIPMIC_LDO11_CTRL		0x4b
#define TIPMIC_LDO12_CTRL		0x4c
#define TIPMIC_LDO13_CTRL		0x4d
#define TIPMIC_LDO14_CTRL		0x4e
#define TIPMIC_ADC_CTRL			0x50
#define  TIPMIC_ADC_CTRL_START		(1 << 0)
#define  TIPMIC_ADC_CTRL_CH_MASK	(3 << 1)
#define  TIPMIC_ADC_CTRL_CH_PMICTEMP	(1 << 1)
#define  TIPMIC_ADC_CTRL_CH_BATTEMP	(2 << 1)
#define  TIPMIC_ADC_CTRL_CH_SYSTEMP	(3 << 1)
#define  TIPMIC_ADC_CTRL_EN		(1 << 5)
#define TIPMIC_PMICTEMP_HI		0x56
#define TIPMIC_PMICTEMP_LO		0x57
#define TIPMIC_BATTEMP_HI		0x58
#define TIPMIC_BATTEMP_LO		0x59
#define TIPMIC_SYSTEMP_HI		0x5a
#define TIPMIC_SYSTEMP_LO		0x5b

#define TIPMIC_REGIONSPACE_THERMAL	0x8c
#define TIPMIC_REGIONSPACE_POWER	0x8d

struct acpi_lpat {
	int32_t temp;
	int32_t raw;
};

struct tipmic_softc {
	struct device	sc_dev;
	struct acpi_softc *sc_acpi;
	struct aml_node *sc_node;
	i2c_tag_t	sc_tag;
	i2c_addr_t	sc_addr;

	void		*sc_ih;
	volatile int	sc_stat_adc;

	struct acpi_lpat *sc_lpat;
	size_t		sc_lpat_len;

	struct acpi_gpio sc_gpio;
};

int	tipmic_match(struct device *, void *, void *);
void	tipmic_attach(struct device *, struct device *, void *);

const struct cfattach tipmic_ca = {
	sizeof(struct tipmic_softc), tipmic_match, tipmic_attach
};

struct cfdriver tipmic_cd = {
	NULL, "tipmic", DV_DULL
};

uint8_t	tipmic_read_1(struct tipmic_softc *, uint8_t, int);
void	tipmic_write_1(struct tipmic_softc *, uint8_t, uint8_t, int);
int	tipmic_intr(void *);
void	tipmic_get_lpat(struct tipmic_softc *);
int32_t	tipmic_raw_to_temp(struct tipmic_softc *, int32_t);
int	tipmic_thermal_opreg_handler(void *, int, uint64_t, int, uint64_t *);
int	tipmic_power_opreg_handler(void *, int, uint64_t, int, uint64_t *);
int	tipmic_read_pin(void *, int);
void	tipmic_write_pin(void *, int, int);

int
tipmic_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	return (strcmp(ia->ia_name, "INT33F5") == 0);
}

void
tipmic_attach(struct device *parent, struct device *self, void *aux)
{
	struct tipmic_softc *sc = (struct tipmic_softc *)self;
	struct i2c_attach_args *ia = aux;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;
	sc->sc_acpi = acpi_softc;
	sc->sc_node = ia->ia_cookie;

	if (ia->ia_intr == NULL) {
		printf(": no interrupt\n");
		return;
	}

	/* Mask all interrupts before we install our interrupt handler. */
	tipmic_write_1(sc, TIPMIC_INTR_MASK, TIPMIC_INTR_MASK_ALL, I2C_F_POLL);

	printf(" %s", iic_intr_string(sc->sc_tag, ia->ia_intr));
	sc->sc_ih = iic_intr_establish(sc->sc_tag, ia->ia_intr,
	    IPL_BIO, tipmic_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		return;
	}

	printf("\n");

	tipmic_get_lpat(sc);
	if (sc->sc_lpat == NULL)
		return;

	sc->sc_gpio.cookie = sc;
	sc->sc_gpio.read_pin = tipmic_read_pin;
	sc->sc_gpio.write_pin = tipmic_write_pin;
	sc->sc_node->gpio = &sc->sc_gpio;
	acpi_register_gpio(sc->sc_acpi, sc->sc_node);

	/* Register OEM defined address space. */
	aml_register_regionspace(sc->sc_node, TIPMIC_REGIONSPACE_THERMAL,
	    sc, tipmic_thermal_opreg_handler);
	aml_register_regionspace(sc->sc_node, TIPMIC_REGIONSPACE_POWER,
	    sc, tipmic_power_opreg_handler);
}

uint8_t
tipmic_read_1(struct tipmic_softc *sc, uint8_t reg, int flags)
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
tipmic_write_1(struct tipmic_softc *sc, uint8_t reg, uint8_t val, int flags)
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

int
tipmic_intr(void *arg)
{
	struct tipmic_softc *sc = arg;
	int handled = 0;
	uint8_t stat;

	stat = tipmic_read_1(sc, TIPMIC_INTR_STAT, I2C_F_POLL);
	tipmic_write_1(sc, TIPMIC_INTR_STAT, stat, I2C_F_POLL);
	if (stat & TIPMIC_INTR_STAT_ADC) {
		sc->sc_stat_adc = 1;
		wakeup(&sc->sc_stat_adc);
		handled = 1;
	}

	return handled;
}

void
tipmic_get_lpat(struct tipmic_softc *sc)
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
tipmic_raw_to_temp(struct tipmic_softc *sc, int32_t raw)
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

struct tipmic_regmap {
	uint8_t address;
	uint8_t hi, lo;
};

struct tipmic_regmap tipmic_thermal_regmap[] = {
	{ 0x00, TIPMIC_SYSTEMP_HI, TIPMIC_SYSTEMP_LO },
	{ 0x18, TIPMIC_SYSTEMP_HI, TIPMIC_SYSTEMP_LO }
};

static int
tipmic_wait_adc(struct tipmic_softc *sc)
{
	int i;

	if (!cold) {
		return (tsleep_nsec(&sc->sc_stat_adc, PRIBIO, "tipmic",
		    SEC_TO_NSEC(1)));
	}

	for (i = 0; i < 1000; i++) {
		delay(1000);
		if (tipmic_intr(sc) == 1)
			return (0);
	}

	return (EWOULDBLOCK);
}

int
tipmic_thermal_opreg_handler(void *cookie, int iodir, uint64_t address,
    int size, uint64_t *value)
{
	struct tipmic_softc *sc = cookie;
	int32_t temp;
	uint16_t raw;
	uint8_t hi, lo;
	uint8_t reg;
	int i, s;

	/* Only allow 32-bit read access. */
	if (size != 4 || iodir != ACPI_IOREAD)
		return -1;

	for (i = 0; i < nitems(tipmic_thermal_regmap); i++) {
		if (address == tipmic_thermal_regmap[i].address)
			break;
	}
	if (i == nitems(tipmic_thermal_regmap)) {
		printf("%s: addr 0x%02llx\n", __func__, address);
		return -1;
	}

	/* Turn ADC on and select the appropriate channel. */
	reg = tipmic_read_1(sc, TIPMIC_ADC_CTRL, 0);
	reg |= TIPMIC_ADC_CTRL_EN;
	tipmic_write_1(sc, TIPMIC_ADC_CTRL, reg, 0);
	switch (tipmic_thermal_regmap[i].hi) {
	case TIPMIC_SYSTEMP_HI:
		reg |= TIPMIC_ADC_CTRL_CH_SYSTEMP;
		break;
	default:
		panic("%s: unsupported channel", sc->sc_dev.dv_xname);
	}
	tipmic_write_1(sc, TIPMIC_ADC_CTRL, reg, 0);

	/* Need to wait 50us before starting the conversion. */
	delay(50);

	/* Start conversion. */
	sc->sc_stat_adc = 0;
	reg |= TIPMIC_ADC_CTRL_START;
	tipmic_write_1(sc, TIPMIC_ADC_CTRL, reg, 0);

	/*
	 * Block interrupts to prevent I2C access from the interrupt
	 * handler during the completion of the write that unmasks the
	 * ADC interrupt.
	 */
	s = splbio();
	reg = tipmic_read_1(sc, TIPMIC_INTR_MASK, I2C_F_POLL);
	reg &= ~TIPMIC_INTR_MASK_ADC;
	tipmic_write_1(sc, TIPMIC_INTR_MASK, reg, I2C_F_POLL);
	splx(s);

	while (sc->sc_stat_adc == 0) {
		if (tipmic_wait_adc(sc)) {
			printf("%s: ADC timeout\n", sc->sc_dev.dv_xname);
			break;
		}
	}

	/* Mask ADC interrupt again. */
	s = splbio();
	reg = tipmic_read_1(sc, TIPMIC_INTR_MASK, I2C_F_POLL);
	reg |= TIPMIC_INTR_MASK_ADC;
	tipmic_write_1(sc, TIPMIC_INTR_MASK, reg, I2C_F_POLL);
	splx(s);

	hi = tipmic_thermal_regmap[i].hi;
	lo = tipmic_thermal_regmap[i].lo;
	raw = (tipmic_read_1(sc, hi, 0) & 0x03) << 8;
	raw |= tipmic_read_1(sc, lo, 0);

	/* Turn ADC off. */
	reg = tipmic_read_1(sc, TIPMIC_ADC_CTRL, 0);
	reg &= ~(TIPMIC_ADC_CTRL_EN | TIPMIC_ADC_CTRL_CH_MASK);
	tipmic_write_1(sc, TIPMIC_ADC_CTRL, reg, 0);

	temp = tipmic_raw_to_temp(sc, raw);
	if (temp < 0)
		return -1;

	*value = temp;
	return 0;
}

struct tipmic_regmap tipmic_power_regmap[] = {
	{ 0x00, TIPMIC_LDO1_CTRL },
	{ 0x04, TIPMIC_LDO2_CTRL },
	{ 0x08, TIPMIC_LDO3_CTRL },
	{ 0x0c, TIPMIC_LDO5_CTRL },
	{ 0x10, TIPMIC_LDO6_CTRL },
	{ 0x14, TIPMIC_LDO7_CTRL },
	{ 0x18, TIPMIC_LDO8_CTRL },
	{ 0x1c, TIPMIC_LDO9_CTRL },
	{ 0x20, TIPMIC_LDO10_CTRL },
	{ 0x24, TIPMIC_LDO11_CTRL },
	{ 0x28, TIPMIC_LDO12_CTRL },
	{ 0x2c, TIPMIC_LDO13_CTRL },
	{ 0x30, TIPMIC_LDO14_CTRL }
};

int
tipmic_power_opreg_handler(void *cookie, int iodir, uint64_t address,
    int size, uint64_t *value)
{
	struct tipmic_softc *sc = cookie;
	uint8_t reg, val;
	int i;

	/* Only allow 32-bit access. */
	if (size != 4)
		return -1;

	for (i = 0; i < nitems(tipmic_power_regmap); i++) {
		if (address == tipmic_power_regmap[i].address)
			break;
	}
	if (i == nitems(tipmic_power_regmap)) {
		printf("%s: addr 0x%02llx\n", __func__, address);
		return -1;
	}

	reg = tipmic_power_regmap[i].hi;
	val = tipmic_read_1(sc, reg, 0);
	if (iodir == ACPI_IOREAD) {
		*value = val & 0x1;
	} else {
		if (*value)
			val |= 0x1;
		else
			val &= ~0x1;
		tipmic_write_1(sc, reg, val, 0);
	}

	return 0;
}

/* 
 * Allegedly the GPIOs are virtual and only there to deal with a
 * limitation of Microsoft Windows.
 */

int
tipmic_read_pin(void *cookie, int pin)
{
	return 0;
}

void
tipmic_write_pin(void *cookie, int pin, int value)
{
}
