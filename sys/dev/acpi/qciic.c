/*	$OpenBSD: qciic.c,v 1.8 2025/06/11 09:57:01 kettenis Exp $	*/
/*
 * Copyright (c) 2022 Mark Kettenis <kettenis@openbsd.org>
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

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#define _I2C_PRIVATE
#include <dev/i2c/i2cvar.h>

/* Registers */
#define GENI_I2C_TX_TRANS_LEN		0x26c
#define GENI_I2C_RX_TRANS_LEN		0x270
#define GENI_M_CMD0			0x600
#define  GENI_M_CMD0_OPCODE_I2C_WRITE	(0x1 << 27)
#define  GENI_M_CMD0_OPCODE_I2C_READ	(0x2 << 27)
#define  GENI_M_CMD0_SLV_ADDR_SHIFT	9
#define  GENI_M_CMD0_STOP_STRETCH	(1 << 2)
#define GENI_M_IRQ_STATUS		0x610
#define GENI_M_IRQ_CLEAR		0x618
#define  GENI_M_IRQ_CMD_DONE		(1 << 0)
#define GENI_TX_FIFO			0x700
#define GENI_RX_FIFO			0x780
#define GENI_TX_FIFO_STATUS		0x800
#define GENI_RX_FIFO_STATUS		0x804
#define  GENI_RX_FIFO_STATUS_WC(val)	((val) & 0xffffff)

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct qciic_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_node;
	struct device		*sc_iic;

	struct i2c_controller	sc_ic;
};

struct qciic_crs {
	uint16_t i2c_addr;
	struct aml_node *i2c_bus;
	int irq_int;
	uint8_t irq_flags;
	struct aml_node *gpio_int_node;
	uint16_t gpio_int_pin;
	uint16_t gpio_int_flags;
	struct aml_node *node;
	int skip;
};

int	qciic_acpi_match(struct device *, void *, void *);
void	qciic_acpi_attach(struct device *, struct device *, void *);

const struct cfattach qciic_acpi_ca = {
	sizeof (struct qciic_softc), qciic_acpi_match, qciic_acpi_attach
};

struct cfdriver qciic_cd = {
	NULL, "qciic", DV_DULL
};

int	qciic_acquire_bus(void *, int);
void	qciic_release_bus(void *, int);
int	qciic_exec(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
	    void *, size_t, int);

void	*qciic_i2c_intr_establish(void *, void *, int, int (*)(void *),
	    void *, const char *);
void	qciic_i2c_intr_disestablish(void *, void *);
const char *qciic_i2c_intr_string(void *, void *);

int	qciic_acpi_parse_crs(int, union acpi_resource *, void *);
void	qciic_acpi_bus_scan(struct device *, struct i2cbus_attach_args *,
	    void *);
int	qciic_acpi_found_hid(struct aml_node *, void *);
int	qciic_acpi_found_ihidev(struct qciic_softc *,
	    struct aml_node *, char *, struct qciic_crs);

const char *qciic_hids[] = {
	"QCOM0610",
	"QCOM0811",
	"QCOM0C10",
	NULL
};

int
qciic_acpi_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	if (aaa->aaa_naddr < 1)
		return 0;
	return acpi_matchhids(aaa, qciic_hids, cf->cf_driver->cd_name);
}

void
qciic_acpi_attach(struct device *parent, struct device *self, void *aux)
{
	struct qciic_softc *sc = (struct qciic_softc *)self;
	struct acpi_attach_args *aaa = aux;
	struct i2cbus_attach_args iba;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_node = aaa->aaa_node;
	printf(" %s", aaa->aaa_node->name);

	printf(" addr 0x%llx/0x%llx", aaa->aaa_addr[0], aaa->aaa_size[0]);

	sc->sc_iot = aaa->aaa_bst[0];
	if (bus_space_map(sc->sc_iot, aaa->aaa_addr[0], aaa->aaa_size[0],
	    0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	printf("\n");

	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_acquire_bus = qciic_acquire_bus;
	sc->sc_ic.ic_release_bus = qciic_release_bus;
	sc->sc_ic.ic_exec = qciic_exec;
	sc->sc_ic.ic_intr_establish = qciic_i2c_intr_establish;
	sc->sc_ic.ic_intr_disestablish = qciic_i2c_intr_disestablish;
	sc->sc_ic.ic_intr_string = qciic_i2c_intr_string;

#ifndef SMALL_KERNEL
	/*
	 * XXX Registering the I2C9 node with ACPI leads to AML
	 * executing I2C transaction that fail and spin with the
	 * kernel lock held until they fail.
	 */
	if (strcmp(aaa->aaa_dev, "QCOM0610") != 0) {
		sc->sc_node->i2c = &sc->sc_ic;
		acpi_register_gsb(sc->sc_acpi, sc->sc_node);
	}
#endif

	memset(&iba, 0, sizeof(iba));
	iba.iba_name = "iic";
	iba.iba_tag = &sc->sc_ic;
	iba.iba_bus_scan = qciic_acpi_bus_scan;
	iba.iba_bus_scan_arg = sc;
	config_found(&sc->sc_dev, &iba, iicbus_print);
}

int
qciic_acquire_bus(void *cookie, int flags)
{
	return 0;
}

void
qciic_release_bus(void *cookie, int flags)
{
}

int
qciic_wait(struct qciic_softc *sc, uint32_t bits)
{
	uint32_t stat;
	int timo;

	for (timo = 50000; timo > 0; timo--) {
		stat = HREAD4(sc, GENI_M_IRQ_STATUS);
		if (stat & bits)
			break;
		delay(10);
	}
	if (timo == 0)
		return ETIMEDOUT;

	return 0;
}

int
qciic_read(struct qciic_softc *sc, uint8_t *buf, size_t len)
{
	uint32_t stat, word;
	int timo, i;

	word = 0;
	for (i = 0; i < len; i++) {
		if ((i % 4) == 0) {
			for (timo = 50000; timo > 0; timo--) {
				stat = HREAD4(sc, GENI_RX_FIFO_STATUS);
				if (GENI_RX_FIFO_STATUS_WC(stat) > 0)
					break;
				delay(10);
			}
			if (timo == 0)
				return ETIMEDOUT;
			word = HREAD4(sc, GENI_RX_FIFO);
		}
		buf[i] = word >> ((i % 4) * 8);
	}

	return 0;
}

int
qciic_write(struct qciic_softc *sc, const uint8_t *buf, size_t len)
{
	uint32_t stat, word;
	int timo, i;

	word = 0;
	for (i = 0; i < len; i++) {
		word |= buf[i] << ((i % 4) * 8);
		if ((i % 4) == 3 || i == (len - 1)) {
			for (timo = 50000; timo > 0; timo--) {
				stat = HREAD4(sc, GENI_TX_FIFO_STATUS);
				if (stat < 16)
					break;
				delay(10);
			}
			if (timo == 0)
				return ETIMEDOUT;
			HWRITE4(sc, GENI_TX_FIFO, word);
			word = 0;
		}
	}

	return 0;
}

int
qciic_exec(void *cookie, i2c_op_t op, i2c_addr_t addr, const void *cmd,
    size_t cmdlen, void *buf, size_t buflen, int flags)
{
	struct qciic_softc *sc = cookie;
	uint32_t m_cmd, m_param, stat;
	int error;

	m_param = addr << GENI_M_CMD0_SLV_ADDR_SHIFT;
	m_param |= GENI_M_CMD0_STOP_STRETCH;

	if (buflen == 0 && I2C_OP_STOP_P(op))
		m_param &= ~GENI_M_CMD0_STOP_STRETCH;

	if (cmdlen > 0) {
		stat = HREAD4(sc, GENI_M_IRQ_STATUS);
		HWRITE4(sc, GENI_M_IRQ_CLEAR, stat);
		HWRITE4(sc, GENI_I2C_TX_TRANS_LEN, cmdlen);
		m_cmd = GENI_M_CMD0_OPCODE_I2C_WRITE | m_param;
		HWRITE4(sc, GENI_M_CMD0, m_cmd);

		error = qciic_write(sc, cmd, cmdlen);
		if (error)
			return error;

		error = qciic_wait(sc, GENI_M_IRQ_CMD_DONE);
		if (error)
			return error;
	}

	if (buflen == 0)
		return 0;

	if (I2C_OP_STOP_P(op))
		m_param &= ~GENI_M_CMD0_STOP_STRETCH;

	if (I2C_OP_READ_P(op)) {
		stat = HREAD4(sc, GENI_M_IRQ_STATUS);
		HWRITE4(sc, GENI_M_IRQ_CLEAR, stat);
		HWRITE4(sc, GENI_I2C_RX_TRANS_LEN, buflen);
		m_cmd = GENI_M_CMD0_OPCODE_I2C_READ | m_param;
		HWRITE4(sc, GENI_M_CMD0, m_cmd);

		error = qciic_read(sc, buf, buflen);
		if (error)
			return error;

		error = qciic_wait(sc, GENI_M_IRQ_CMD_DONE);
		if (error)
			return error;
	} else {
		stat = HREAD4(sc, GENI_M_IRQ_STATUS);
		HWRITE4(sc, GENI_M_IRQ_CLEAR, stat);
		HWRITE4(sc, GENI_I2C_TX_TRANS_LEN, buflen);
		m_cmd = GENI_M_CMD0_OPCODE_I2C_WRITE | m_param;
		HWRITE4(sc, GENI_M_CMD0, m_cmd);

		error = qciic_write(sc, buf, buflen);
		if (error)
			return error;

		error = qciic_wait(sc, GENI_M_IRQ_CMD_DONE);
		if (error)
			return error;
	}

	return 0;
}

void *
qciic_i2c_intr_establish(void *cookie, void *ih, int level,
    int (*func)(void *), void *arg, const char *name)
{
	struct qciic_crs *crs = ih;

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
qciic_i2c_intr_disestablish(void *cookie, void *ih)
{
	/* XXX GPIO interrupts */
	acpi_intr_disestablish(ih);
}

const char *
qciic_i2c_intr_string(void *cookie, void *ih)
{
	struct qciic_crs *crs = ih;
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
qciic_acpi_parse_crs(int crsidx, union acpi_resource *crs, void *arg)
{
	struct qciic_crs *sc_crs = arg;
	struct aml_node *node;
	uint16_t pin;

	switch (AML_CRSTYPE(crs)) {
	case LR_MEM32FIXED:
		/* An MMIO address means this is not an I2C device. */
		sc_crs->skip = 1;
		break;

	case LR_SERBUS:
		if (crs->lr_serbus.type == LR_SERBUS_I2C) {
			sc_crs->i2c_addr = crs->lr_i2cbus._adr;
			sc_crs->i2c_bus = aml_searchname(sc_crs->node,
			    &crs->lr_i2cbus.vdata[crs->lr_i2cbus.tlength - 6]);
		}
		break;

	case LR_GPIO:
		node = aml_searchname(sc_crs->node,
		    (char *)&crs->pad[crs->lr_gpio.res_off]);
		pin = *(uint16_t *)&crs->pad[crs->lr_gpio.pin_off];
		if (crs->lr_gpio.type == LR_GPIO_INT) {
			sc_crs->gpio_int_node = node;
			sc_crs->gpio_int_pin = pin;
			sc_crs->gpio_int_flags = crs->lr_gpio.tflags;
		}
		break;
	}

	return 0;
}

void
qciic_acpi_bus_scan(struct device *iic, struct i2cbus_attach_args *iba,
    void *aux)
{
	struct qciic_softc *sc = aux;

	sc->sc_iic = iic;
	aml_find_node(acpi_softc->sc_root, "_HID", qciic_acpi_found_hid, sc);
}

int
qciic_acpi_found_hid(struct aml_node *node, void *arg)
{
	struct qciic_softc *sc = arg;
	struct qciic_crs crs;
	struct aml_value res;
	int64_t sta;
	char cdev[16], dev[16];
	struct i2c_attach_args ia;

	/* Skip our own _HID. */
	if (node->parent == sc->sc_node)
		return 0;

	if (acpi_parsehid(node, arg, cdev, dev, 16) != 0)
		return 0;

	sta = acpi_getsta(acpi_softc, node->parent);
	if ((sta & STA_PRESENT) == 0)
		return 0;

	if (aml_evalname(acpi_softc, node->parent, "_CRS", 0, NULL, &res))
		return 0;

	if (res.type != AML_OBJTYPE_BUFFER || res.length < 5) {
		aml_freevalue(&res);
		return 0;
	}
	memset(&crs, 0, sizeof(crs));
	crs.node = node->parent;
	aml_parse_resource(&res, qciic_acpi_parse_crs, &crs);
	aml_freevalue(&res);

	/* Skip if not using this bus. */
	if (crs.skip || crs.i2c_bus != sc->sc_node)
		return 0;

	acpi_attach_deps(acpi_softc, node->parent);

	if (strcmp(dev, "PNP0C50") == 0 || strcmp(cdev, "PNP0C50") == 0)
		return qciic_acpi_found_ihidev(sc, node, dev, crs);

	memset(&ia, 0, sizeof(ia));
	ia.ia_tag = &sc->sc_ic;
	ia.ia_name = dev;
	ia.ia_addr = crs.i2c_addr;
	ia.ia_cookie = node->parent;

	config_found(sc->sc_iic, &ia, iic_print);
	node->parent->attached = 1;

	return 0;
}

int
qciic_acpi_found_ihidev(struct qciic_softc *sc, struct aml_node *node,
    char *dev, struct qciic_crs crs)
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

	memset(&cmd, 0, sizeof(cmd));
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
	ia.ia_tag = &sc->sc_ic;
	ia.ia_name = "ihidev";
	ia.ia_size = aml_val2int(&res); /* hid descriptor address */
	ia.ia_addr = crs.i2c_addr;
	ia.ia_cookie = dev;

	aml_freevalue(&res);

	if (!(crs.irq_int == 0 && crs.gpio_int_node == NULL))
		ia.ia_intr = &crs;

	if (config_found(sc->sc_iic, &ia, iic_print)) {
		node->parent->attached = 1;
		return 0;
	}

	return 1;
}
