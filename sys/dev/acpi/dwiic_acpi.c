/* $OpenBSD: dwiic_acpi.c,v 1.23 2025/06/11 09:57:01 kettenis Exp $ */
/*
 * Synopsys DesignWare I2C controller
 *
 * Copyright (c) 2015, 2016 joshua stein <jcs@openbsd.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

#include "iosf.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <dev/ic/dwiicvar.h>
#include <dev/ic/iosfvar.h>

struct dwiic_crs {
	int irq_int;
	uint8_t irq_flags;
	uint16_t i2c_addr;
	struct aml_node *devnode;
	struct aml_node *gpio_int_node;
	uint16_t gpio_int_pin;
	uint16_t gpio_int_flags;
};

int		dwiic_acpi_match(struct device *, void *, void *);
void		dwiic_acpi_attach(struct device *, struct device *, void *);

int		dwiic_acpi_parse_crs(int, union acpi_resource *, void *);
int		dwiic_acpi_found_ihidev(struct dwiic_softc *,
		    struct aml_node *, char *, struct dwiic_crs);
int		dwiic_acpi_found_iatp(struct dwiic_softc *, struct aml_node *,
		    char *, struct dwiic_crs);
int		dwiic_acpi_found_ietp(struct dwiic_softc *, struct aml_node *,
		    char *, struct dwiic_crs);
void		dwiic_acpi_get_params(struct dwiic_softc *, char *, uint16_t *,
		    uint16_t *, uint32_t *);
void		dwiic_acpi_power(struct dwiic_softc *, int);
void		dwiic_acpi_bus_scan(struct device *,
		    struct i2cbus_attach_args *, void *);

#if NIOSF > 0
int		dwiic_acpi_acquire_bus(void *, int);
void		dwiic_acpi_release_bus(void *, int);
#endif

const struct cfattach dwiic_acpi_ca = {
	sizeof(struct dwiic_softc),
	dwiic_acpi_match,
	dwiic_acpi_attach,
	NULL,
	dwiic_activate
};

const char *dwiic_hids[] = {
	"AMDI0010",
	"APMC0D0F",
	"INT33C2",
	"INT33C3",
	"INT3432",
	"INT3433",
	"80860F41",
	"808622C1",
	NULL
};

const char *ihidev_hids[] = {
	"PNP0C50",
	"ACPI0C50",
	NULL
};

const char *ietp_hids[] = {
	"ELAN0000",
	"ELAN0100",
	"ELAN0600",
	"ELAN0601",
	"ELAN0602",
	"ELAN0603",
	"ELAN0604",
	"ELAN0605",
	"ELAN0606",
	"ELAN0607",
	"ELAN0608",
	"ELAN0609",
	"ELAN060B",
	"ELAN060C",
	"ELAN060F",
	"ELAN0610",
	"ELAN0611",
	"ELAN0612",
	"ELAN0615",
	"ELAN0616",
	"ELAN0617",
	"ELAN0618",
	"ELAN0619",
	"ELAN061A",
	"ELAN061B",
	"ELAN061C",
	"ELAN061D",
	"ELAN061E",
	"ELAN061F",
	"ELAN0620",
	"ELAN0621",
	"ELAN0622",
	"ELAN0623",
	"ELAN0624",
	"ELAN0625",
	"ELAN0626",
	"ELAN0627",
	"ELAN0628",
	"ELAN0629",
	"ELAN062A",
	"ELAN062B",
	"ELAN062C",
	"ELAN062D",
	"ELAN062E",	/* Lenovo V340 Whiskey Lake U */
	"ELAN062F",	/* Lenovo V340 Comet Lake U */
	"ELAN0631",
	"ELAN0632",
	"ELAN0633",	/* Lenovo S145 */
	"ELAN0634",	/* Lenovo V340 Ice lake */
	"ELAN0635",	/* Lenovo V1415-IIL */
	"ELAN0636",	/* Lenovo V1415-Dali */
	"ELAN0637",	/* Lenovo V1415-IGLR */
	"ELAN1000",
	NULL
};

const char *iatp_hids[] = {
	"ATML0000",
	"ATML0001",
	NULL
};

int
dwiic_acpi_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	if (aaa->aaa_naddr < 1)
		return 0;
	return acpi_matchhids(aaa, dwiic_hids, cf->cf_driver->cd_name);
}

void
dwiic_acpi_attach(struct device *parent, struct device *self, void *aux)
{
	struct dwiic_softc *sc = (struct dwiic_softc *)self;
	struct acpi_attach_args *aaa = aux;
	struct aml_value res;
	struct dwiic_crs crs;
	uint64_t sem;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aaa->aaa_node;
	memcpy(&sc->sc_hid, aaa->aaa_dev, sizeof(sc->sc_hid));

	printf(" %s", sc->sc_devnode->name);

	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, "_CRS", 0, NULL, &res)) {
		printf(", no _CRS method\n");
		return;
	}
	if (res.type != AML_OBJTYPE_BUFFER || res.length < 5) {
		printf(", invalid _CRS object (type %d len %d)\n",
		    res.type, res.length);
		aml_freevalue(&res);
		return;
	}
	memset(&crs, 0, sizeof(crs));
	crs.devnode = sc->sc_devnode;
	aml_parse_resource(&res, dwiic_acpi_parse_crs, &crs);
	aml_freevalue(&res);

	printf(" addr 0x%llx/0x%llx", aaa->aaa_addr[0], aaa->aaa_size[0]);

	sc->sc_iot = aaa->aaa_bst[0];
	if (bus_space_map(sc->sc_iot, aaa->aaa_addr[0], aaa->aaa_size[0],
	    0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	/* power up the controller */
	dwiic_acpi_power(sc, 1);

	/* fetch timing parameters */
	dwiic_acpi_get_params(sc, "SSCN", &sc->ss_hcnt, &sc->ss_lcnt, NULL);
	dwiic_acpi_get_params(sc, "FMCN", &sc->fs_hcnt, &sc->fs_lcnt,
	    &sc->sda_hold_time);

	if (dwiic_init(sc)) {
		printf(", failed initializing\n");
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, aaa->aaa_size[0]);
		return;
	}

	/* leave the controller disabled */
	dwiic_write(sc, DW_IC_INTR_MASK, 0);
	dwiic_enable(sc, 0);
	dwiic_read(sc, DW_IC_CLR_INTR);

	/* try to register interrupt with apic, but not fatal without it */
	if (aaa->aaa_nirq > 0) {
		printf(" irq %d", aaa->aaa_irq[0]);

		sc->sc_ih = acpi_intr_establish(aaa->aaa_irq[0], aaa->aaa_irq_flags[0],
		    IPL_BIO, dwiic_intr, sc, sc->sc_dev.dv_xname);
		if (sc->sc_ih == NULL)
			printf(": can't establish interrupt");
	}

	if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode,
	    "_SEM", 0, NULL, &sem))
		sem = 0;

	if (sem)
		printf(", sem");

	printf("\n");

	rw_init(&sc->sc_i2c_lock, "iiclk");

	/* setup and attach iic bus */
	sc->sc_i2c_tag.ic_cookie = sc;
	sc->sc_i2c_tag.ic_acquire_bus = dwiic_i2c_acquire_bus;
	sc->sc_i2c_tag.ic_release_bus = dwiic_i2c_release_bus;
	sc->sc_i2c_tag.ic_exec = dwiic_i2c_exec;
	sc->sc_i2c_tag.ic_intr_establish = dwiic_i2c_intr_establish;
	sc->sc_i2c_tag.ic_intr_disestablish = dwiic_i2c_intr_disestablish;
	sc->sc_i2c_tag.ic_intr_string = dwiic_i2c_intr_string;

#if NIOSF > 0
	if (sem) {
		sc->sc_i2c_tag.ic_acquire_bus = dwiic_acpi_acquire_bus;
		sc->sc_i2c_tag.ic_release_bus = dwiic_acpi_release_bus;
	}
#endif

	bzero(&sc->sc_iba, sizeof(sc->sc_iba));
	sc->sc_iba.iba_name = "iic";
	sc->sc_iba.iba_tag = &sc->sc_i2c_tag;
	sc->sc_iba.iba_bus_scan = dwiic_acpi_bus_scan;
	sc->sc_iba.iba_bus_scan_arg = sc;

	config_found((struct device *)sc, &sc->sc_iba, iicbus_print);

#ifndef SMALL_KERNEL
	sc->sc_devnode->i2c = &sc->sc_i2c_tag;
	acpi_register_gsb(sc->sc_acpi, sc->sc_devnode);
#endif
}

int
dwiic_acpi_parse_crs(int crsidx, union acpi_resource *crs, void *arg)
{
	struct dwiic_crs *sc_crs = arg;
	struct aml_node *node;
	uint16_t pin;
	uint8_t flags;

	switch (AML_CRSTYPE(crs)) {
	case SR_IRQ:
		sc_crs->irq_int = ffs(letoh16(crs->sr_irq.irq_mask)) - 1;
		/* Default is exclusive, active-high, edge triggered. */
		if (AML_CRSLEN(crs) < 4)
			flags = SR_IRQ_MODE;
		else
			flags = crs->sr_irq.irq_flags;
		/* Map flags to those of the extended interrupt descriptor. */
		if (flags & SR_IRQ_SHR)
			sc_crs->irq_flags |= LR_EXTIRQ_SHR;
		if (flags & SR_IRQ_POLARITY)
			sc_crs->irq_flags |= LR_EXTIRQ_POLARITY;
		if (flags & SR_IRQ_MODE)
			sc_crs->irq_flags |= LR_EXTIRQ_MODE;
		break;

	case LR_EXTIRQ:
		sc_crs->irq_int = letoh32(crs->lr_extirq.irq[0]);
		sc_crs->irq_flags = crs->lr_extirq.flags;
		break;

	case LR_GPIO:
		node = aml_searchname(sc_crs->devnode,
		    (char *)&crs->pad[crs->lr_gpio.res_off]);
		pin = *(uint16_t *)&crs->pad[crs->lr_gpio.pin_off];
		if (crs->lr_gpio.type == LR_GPIO_INT) {
			sc_crs->gpio_int_node = node;
			sc_crs->gpio_int_pin = pin;
			sc_crs->gpio_int_flags = crs->lr_gpio.tflags;
		}
		break;

	case LR_SERBUS:
		if (crs->lr_serbus.type == LR_SERBUS_I2C)
			sc_crs->i2c_addr = letoh16(crs->lr_i2cbus._adr);
		break;

	case LR_MEM32:
	case LR_MEM32FIXED:
		break;

	default:
		DPRINTF(("%s: unknown resource type %d\n", __func__,
		    AML_CRSTYPE(crs)));
	}

	return 0;
}

void
dwiic_acpi_get_params(struct dwiic_softc *sc, char *method, uint16_t *hcnt,
    uint16_t *lcnt, uint32_t *sda_hold_time)
{
	struct aml_value res;

	if (!aml_searchname(sc->sc_devnode, method))
		return;

	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, method, 0, NULL, &res)) {
		printf(": eval of %s at %s failed", method,
		    aml_nodename(sc->sc_devnode));
		return;
	}

	if (res.type != AML_OBJTYPE_PACKAGE) {
		printf(": %s is not a package (%d)", method, res.type);
		aml_freevalue(&res);
		return;
	}

	if (res.length <= 2) {
		printf(": %s returned package of len %d", method, res.length);
		aml_freevalue(&res);
		return;
	}

	*hcnt = aml_val2int(res.v_package[0]);
	*lcnt = aml_val2int(res.v_package[1]);
	if (sda_hold_time)
		*sda_hold_time = aml_val2int(res.v_package[2]);
	aml_freevalue(&res);
}

void
dwiic_acpi_bus_scan(struct device *iic, struct i2cbus_attach_args *iba,
    void *aux)
{
	struct dwiic_softc *sc = (struct dwiic_softc *)aux;

	sc->sc_iic = iic;
	aml_find_node(sc->sc_devnode, "_HID", dwiic_acpi_found_hid, sc);
}

void *
dwiic_i2c_intr_establish(void *cookie, void *ih, int level,
    int (*func)(void *), void *arg, const char *name)
{
	struct dwiic_crs *crs = ih;

	if (crs->gpio_int_node) {
		if (!crs->gpio_int_node->gpio)
			/* found ACPI device but no driver for it */
			return NULL;

		struct acpi_gpio *gpio = crs->gpio_int_node->gpio;
		gpio->intr_establish(gpio->cookie, crs->gpio_int_pin,
		    crs->gpio_int_flags, level, func, arg);
		return ih;
	}

	return acpi_intr_establish(crs->irq_int, crs->irq_flags,
	    level, func, arg, name);
}

void
dwiic_i2c_intr_disestablish(void *cookie, void *ih)
{
	/* XXX GPIO interrupts */
	acpi_intr_disestablish(ih);
}

const char *
dwiic_i2c_intr_string(void *cookie, void *ih)
{
	struct dwiic_crs *crs = ih;
	static char irqstr[64];

	if (crs->gpio_int_node) {
		if (crs->gpio_int_node->gpio)
			snprintf(irqstr, sizeof(irqstr), "gpio %d",
			    crs->gpio_int_pin);
	} else
		snprintf(irqstr, sizeof(irqstr), "irq %d", crs->irq_int);

	return irqstr;
}

int
dwiic_matchhids(const char *hid, const char *hids[])
{
	int i;

	for (i = 0; hids[i]; i++)
		if (!strcmp(hid, hids[i]))
			return (1);

	return (0);
}

int
dwiic_acpi_found_hid(struct aml_node *node, void *arg)
{
	struct dwiic_softc *sc = (struct dwiic_softc *)arg;
	struct dwiic_crs crs;
	struct aml_value res;
	int64_t sta;
	char cdev[16], dev[16];
	struct i2c_attach_args ia;

	/* Skip our own _HID. */
	if (node->parent == sc->sc_devnode)
		return 0;

	if (acpi_parsehid(node, arg, cdev, dev, 16) != 0)
		return 0;

	if (aml_evalinteger(acpi_softc, node->parent, "_STA", 0, NULL, &sta))
		sta = STA_PRESENT | STA_ENABLED | STA_DEV_OK | 0x1000;

	if ((sta & STA_PRESENT) == 0)
		return 0;

	DPRINTF(("%s: found HID %s at %s\n", sc->sc_dev.dv_xname, dev,
	    aml_nodename(node)));

	if (aml_evalname(acpi_softc, node->parent, "_CRS", 0, NULL, &res))
		return 0;

	if (res.type != AML_OBJTYPE_BUFFER || res.length < 5) {
		printf("%s: invalid _CRS object (type %d len %d)\n",
		    sc->sc_dev.dv_xname, res.type, res.length);
		aml_freevalue(&res);
		return (0);
	}
	memset(&crs, 0, sizeof(crs));
	crs.devnode = sc->sc_devnode;
	aml_parse_resource(&res, dwiic_acpi_parse_crs, &crs);
	aml_freevalue(&res);

	acpi_attach_deps(acpi_softc, node->parent);

	if (dwiic_matchhids(cdev, ihidev_hids))
		return dwiic_acpi_found_ihidev(sc, node, dev, crs);
	else if (dwiic_matchhids(dev, iatp_hids))
		return dwiic_acpi_found_iatp(sc, node, dev, crs);
	else if (dwiic_matchhids(dev, ietp_hids) || dwiic_matchhids(cdev, ietp_hids))
		return dwiic_acpi_found_ietp(sc, node, dev, crs);

	memset(&ia, 0, sizeof(ia));
	ia.ia_tag = sc->sc_iba.iba_tag;
	ia.ia_name = dev;
	ia.ia_addr = crs.i2c_addr;
	ia.ia_cookie = node->parent;

	if (crs.irq_int != 0 || crs.gpio_int_node != NULL)
		ia.ia_intr = &crs;

	config_found(sc->sc_iic, &ia, dwiic_i2c_print);
	node->parent->attached = 1;

	return 0;
}

int
dwiic_acpi_found_ihidev(struct dwiic_softc *sc, struct aml_node *node,
    char *dev, struct dwiic_crs crs)
{
	struct i2c_attach_args ia;
	struct aml_value cmd[4], res;

	/* 3cdff6f7-4267-4555-ad05-b30a3d8938de */
	static uint8_t i2c_hid_guid[] = {
		0xF7, 0xF6, 0xDF, 0x3C, 0x67, 0x42, 0x55, 0x45,
		0xAD, 0x05, 0xB3, 0x0A, 0x3D, 0x89, 0x38, 0xDE,
	};

	if (!aml_searchname(node->parent, "_DSM")) {
		printf("%s: couldn't find _DSM at %s\n", sc->sc_dev.dv_xname,
		    aml_nodename(node->parent));
		return 0;
	}

	bzero(&cmd, sizeof(cmd));
	cmd[0].type = AML_OBJTYPE_BUFFER;
	cmd[0].v_buffer = (uint8_t *)&i2c_hid_guid;
	cmd[0].length = sizeof(i2c_hid_guid);
	/* rev */
	cmd[1].type = AML_OBJTYPE_INTEGER;
	cmd[1].v_integer = 1;
	cmd[1].length = 1;
	/* func */
	cmd[2].type = AML_OBJTYPE_INTEGER;
	cmd[2].v_integer = 1; /* HID */
	cmd[2].length = 1;
	/* not used */
	cmd[3].type = AML_OBJTYPE_PACKAGE;
	cmd[3].length = 0;

	if (aml_evalname(acpi_softc, node->parent, "_DSM", 4, cmd, &res)) {
		printf("%s: eval of _DSM at %s failed\n",
		    sc->sc_dev.dv_xname, aml_nodename(node->parent));
		return 0;
	}

	if (res.type != AML_OBJTYPE_INTEGER) {
		printf("%s: bad _DSM result at %s: %d\n",
		    sc->sc_dev.dv_xname, aml_nodename(node->parent), res.type);
		aml_freevalue(&res);
		return 0;
	}

	memset(&ia, 0, sizeof(ia));
	ia.ia_tag = sc->sc_iba.iba_tag;
	ia.ia_size = 1;
	ia.ia_name = "ihidev";
	ia.ia_size = aml_val2int(&res); /* hid descriptor address */
	ia.ia_addr = crs.i2c_addr;
	ia.ia_cookie = dev;

	aml_freevalue(&res);

	if (sc->sc_poll_ihidev)
		ia.ia_poll = 1;
	if (!(crs.irq_int == 0 && crs.gpio_int_node == NULL))
		ia.ia_intr = &crs;

	if (config_found(sc->sc_iic, &ia, dwiic_i2c_print)) {
		node->parent->attached = 1;
		return 0;
	}

	return 1;
}

int
dwiic_acpi_found_ietp(struct dwiic_softc *sc, struct aml_node *node,
    char *dev, struct dwiic_crs crs)
{
	struct i2c_attach_args ia;

	memset(&ia, 0, sizeof(ia));
	ia.ia_tag = sc->sc_iba.iba_tag;
	ia.ia_size = 1;
	ia.ia_name = "ietp";
	ia.ia_addr = crs.i2c_addr;
	ia.ia_cookie = dev;

	if (sc->sc_poll_ihidev)
		ia.ia_poll = 1;
	if (!(crs.irq_int == 0 && crs.gpio_int_node == NULL))
		ia.ia_intr = &crs;

	if (config_found(sc->sc_iic, &ia, dwiic_i2c_print)) {
		node->parent->attached = 1;
		return 0;
	}

	return 1;
}

int
dwiic_acpi_found_iatp(struct dwiic_softc *sc, struct aml_node *node, char *dev,
    struct dwiic_crs crs)
{
	struct i2c_attach_args ia;
	struct aml_value res;

	if (aml_evalname(acpi_softc, node->parent, "GPIO", 0, NULL, &res))
		/* no gpio, assume this is the bootloader interface */
		return (0);

	memset(&ia, 0, sizeof(ia));
	ia.ia_tag = sc->sc_iba.iba_tag;
	ia.ia_size = 1;
	ia.ia_name = "iatp";
	ia.ia_addr = crs.i2c_addr;
	ia.ia_cookie = dev;

	if (crs.irq_int <= 0 && crs.gpio_int_node == NULL) {
		printf("%s: couldn't find irq for %s\n", sc->sc_dev.dv_xname,
		   aml_nodename(node->parent));
		return 0;
	}
	ia.ia_intr = &crs;

	if (config_found(sc->sc_iic, &ia, dwiic_i2c_print)) {
		node->parent->attached = 1;
		return 0;
	}

	return 1;
}

void
dwiic_acpi_power(struct dwiic_softc *sc, int power)
{
	char ps[] = "_PS0";

	if (!power)
		ps[3] = '3';

	if (aml_searchname(sc->sc_devnode, ps)) {
		if (aml_evalname(sc->sc_acpi, sc->sc_devnode, ps, 0, NULL,
		    NULL)) {
			printf("%s: failed powering %s with %s\n",
			    sc->sc_dev.dv_xname, power ? "on" : "off",
			    ps);
			return;
		}

		DELAY(10000); /* 10 milliseconds */
	} else
		DPRINTF(("%s: no %s method\n", sc->sc_dev.dv_xname, ps));

	if (strcmp(sc->sc_hid, "INT3432") == 0 ||
	    strcmp(sc->sc_hid, "INT3433") == 0) {
		/*
		 * XXX: broadwell i2c devices may need this for initial power
		 * up and/or after s3 resume.
		 *
		 * linux does this write via LPSS -> clk_register_gate ->
		 * clk_gate_enable -> clk_gate_endisable -> clk_writel
		 */
		dwiic_write(sc, 0x800, 1);
	}
}

#if NIOSF > 0
extern int	iosf_i2c_acquire(int);
extern void	iosf_i2c_release(int);

int
dwiic_acpi_acquire_bus(void *cookie, int flags)
{
	int rv;

	rv = dwiic_i2c_acquire_bus(cookie, flags);
	if (rv != 0)
		return (rv);

	return (iosf_i2c_acquire(flags));
}

void
dwiic_acpi_release_bus(void *cookie, int flags)
{
	iosf_i2c_release(flags);
	dwiic_i2c_release_bus(cookie, flags);
}
#endif /* NIOSF > 0 */
