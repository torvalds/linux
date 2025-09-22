/*	$OpenBSD: qciic_fdt.c,v 1.2 2024/10/17 17:58:58 kettenis Exp $	*/
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

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

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

struct qciic_fdt_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	int			sc_node;
	struct device		*sc_iic;

	struct i2c_controller	sc_ic;
};

int	qciic_fdt_match(struct device *, void *, void *);
void	qciic_fdt_attach(struct device *, struct device *, void *);

const struct cfattach qciic_fdt_ca = {
	sizeof (struct qciic_fdt_softc), qciic_fdt_match, qciic_fdt_attach
};

int	qciic_fdt_acquire_bus(void *, int);
void	qciic_fdt_release_bus(void *, int);
int	qciic_fdt_exec(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
	    void *, size_t, int);

void	*qciic_fdt_i2c_intr_establish(void *, void *, int, int (*)(void *),
	    void *, const char *);
void	qciic_fdt_i2c_intr_disestablish(void *, void *);
const char *qciic_fdt_i2c_intr_string(void *, void *);

void	qciic_fdt_bus_scan(struct device *, struct i2cbus_attach_args *,
	    void *);

int
qciic_fdt_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "qcom,geni-i2c");
}

void
qciic_fdt_attach(struct device *parent, struct device *self, void *aux)
{
	struct qciic_fdt_softc *sc = (struct qciic_fdt_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct i2cbus_attach_args iba;

	sc->sc_node = faa->fa_node;

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr, faa->fa_reg[0].size,
	    0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	printf("\n");

	pinctrl_byname(sc->sc_node, "default");

	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_acquire_bus = qciic_fdt_acquire_bus;
	sc->sc_ic.ic_release_bus = qciic_fdt_release_bus;
	sc->sc_ic.ic_exec = qciic_fdt_exec;
	sc->sc_ic.ic_intr_establish = qciic_fdt_i2c_intr_establish;
	sc->sc_ic.ic_intr_disestablish = qciic_fdt_i2c_intr_disestablish;
	sc->sc_ic.ic_intr_string = qciic_fdt_i2c_intr_string;

	memset(&iba, 0, sizeof(iba));
	iba.iba_name = "iic";
	iba.iba_tag = &sc->sc_ic;
	iba.iba_bus_scan = qciic_fdt_bus_scan;
	iba.iba_bus_scan_arg = &sc->sc_node;
	config_found(&sc->sc_dev, &iba, iicbus_print);
}

int
qciic_fdt_acquire_bus(void *cookie, int flags)
{
	return 0;
}

void
qciic_fdt_release_bus(void *cookie, int flags)
{
}

int
qciic_fdt_wait(struct qciic_fdt_softc *sc, uint32_t bits)
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
qciic_fdt_read(struct qciic_fdt_softc *sc, uint8_t *buf, size_t len)
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
qciic_fdt_write(struct qciic_fdt_softc *sc, const uint8_t *buf, size_t len)
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
qciic_fdt_exec(void *cookie, i2c_op_t op, i2c_addr_t addr, const void *cmd,
    size_t cmdlen, void *buf, size_t buflen, int flags)
{
	struct qciic_fdt_softc *sc = cookie;
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

		error = qciic_fdt_write(sc, cmd, cmdlen);
		if (error)
			return error;

		error = qciic_fdt_wait(sc, GENI_M_IRQ_CMD_DONE);
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

		error = qciic_fdt_read(sc, buf, buflen);
		if (error)
			return error;

		error = qciic_fdt_wait(sc, GENI_M_IRQ_CMD_DONE);
		if (error)
			return error;
	} else {
		stat = HREAD4(sc, GENI_M_IRQ_STATUS);
		HWRITE4(sc, GENI_M_IRQ_CLEAR, stat);
		HWRITE4(sc, GENI_I2C_TX_TRANS_LEN, buflen);
		m_cmd = GENI_M_CMD0_OPCODE_I2C_WRITE | m_param;
		HWRITE4(sc, GENI_M_CMD0, m_cmd);

		error = qciic_fdt_write(sc, buf, buflen);
		if (error)
			return error;

		error = qciic_fdt_wait(sc, GENI_M_IRQ_CMD_DONE);
		if (error)
			return error;
	}

	return 0;
}

void *
qciic_fdt_i2c_intr_establish(void *cookie, void *ih, int level,
    int (*func)(void *), void *arg, const char *name)
{
	int node = *(int *)ih;

	return fdt_intr_establish(node, level, func, arg, (char *)name);
}

void
qciic_fdt_i2c_intr_disestablish(void *cookie, void *ih)
{
	fdt_intr_disestablish(ih);
}

const char *
qciic_fdt_i2c_intr_string(void *cookie, void *ih)
{
	static char irqstr[64];

	snprintf(irqstr, sizeof(irqstr), "irq");

	return irqstr;
}

void
qciic_fdt_bus_scan(struct device *self, struct i2cbus_attach_args *iba, void *aux)
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
		ia.ia_intr = &node;

		/* Quirk for ihidev(4) */
		if (strcmp(name, "hid-over-i2c") == 0) {
			ia.ia_name = "ihidev";
			ia.ia_size = OF_getpropint(node, "hid-descr-addr", 0);
			ia.ia_cookie = name;
		}

		config_found(self, &ia, iic_print);
	}
}
