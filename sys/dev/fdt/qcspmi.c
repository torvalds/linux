/*	$OpenBSD: qcspmi.c,v 1.7 2025/01/03 14:13:25 kettenis Exp $	*/
/*
 * Copyright (c) 2022 Patrick Wildt <patrick@blueri.se>
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
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/fdt/spmivar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

/* Core registers. */
#define SPMI_VERSION		0x00
#define  SPMI_VERSION_V2_MIN		0x20010000
#define  SPMI_VERSION_V3_MIN		0x30000000
#define  SPMI_VERSION_V5_MIN		0x50000000
#define  SPMI_VERSION_V7_MIN		0x70000000
#define SPMI_ARB_APID_MAP(sc, x)	((sc)->sc_arb_apid_map + (x) * 0x4)
#define  SPMI_ARB_APID_MAP_PPID_MASK	0xfff
#define  SPMI_ARB_APID_MAP_PPID_SHIFT	8
#define  SPMI_ARB_APID_MAP_IRQ_OWNER	(1 << 14)

/* Channel registers. */
#define SPMI_CHAN_OFF(sc, x)	((sc)->sc_chan_stride * (x))
#define SPMI_OBSV_OFF(sc, x, y)	\
	((sc)->sc_obsv_ee_stride * (x) + (sc)->sc_obsv_apid_stride * (y))
#define SPMI_COMMAND		0x00
#define  SPMI_COMMAND_OP_EXT_WRITEL	(0 << 27)
#define  SPMI_COMMAND_OP_EXT_READL	(1 << 27)
#define  SPMI_COMMAND_OP_EXT_WRITE	(2 << 27)
#define  SPMI_COMMAND_OP_RESET		(3 << 27)
#define  SPMI_COMMAND_OP_SLEEP		(4 << 27)
#define  SPMI_COMMAND_OP_SHUTDOWN	(5 << 27)
#define  SPMI_COMMAND_OP_WAKEUP		(6 << 27)
#define  SPMI_COMMAND_OP_AUTHENTICATE	(7 << 27)
#define  SPMI_COMMAND_OP_MSTR_READ	(8 << 27)
#define  SPMI_COMMAND_OP_MSTR_WRITE	(9 << 27)
#define  SPMI_COMMAND_OP_EXT_READ	(13 << 27)
#define  SPMI_COMMAND_OP_WRITE		(14 << 27)
#define  SPMI_COMMAND_OP_READ		(15 << 27)
#define  SPMI_COMMAND_OP_ZERO_WRITE	(16 << 27)
#define  SPMI_COMMAND_ADDR(x)		(((x) & 0xff) << 4)
#define  SPMI_COMMAND_LEN(x)		(((x) & 0x7) << 0)
#define SPMI_CONFIG		0x04
#define SPMI_STATUS		0x08
#define  SPMI_STATUS_DONE		(1 << 0)
#define  SPMI_STATUS_FAILURE		(1 << 1)
#define  SPMI_STATUS_DENIED		(1 << 2)
#define  SPMI_STATUS_DROPPED		(1 << 3)
#define SPMI_WDATA0		0x10
#define SPMI_WDATA1		0x14
#define SPMI_RDATA0		0x18
#define SPMI_RDATA1		0x1c
#define SPMI_ACC_ENABLE		0x100
#define  SPMI_ACC_ENABLE_BIT		(1 << 0)
#define SPMI_IRQ_STATUS		0x104
#define SPMI_IRQ_CLEAR		0x108

/* Intr registers */
#define SPMI_OWNER_ACC_STATUS(sc, x, y)	\
	((sc)->sc_chan_stride * (x) + 0x4 * (y))

/* Config registers */
#define SPMI_OWNERSHIP_TABLE(sc, x)	((sc)->sc_ownership_table + (x) * 0x4)
#define  SPMI_OWNERSHIP_TABLE_OWNER(x)	((x) & 0x7)

/* Misc */
#define SPMI_MAX_PERIPH		1024
#define SPMI_MAX_PPID		4096
#define SPMI_PPID_TO_APID_VALID	(1U << 15)
#define SPMI_PPID_TO_APID_MASK	(0x7fff)

/* Intr commands */
#define INTR_RT_STS		0x10
#define INTR_SET_TYPE		0x11
#define INTR_POLARITY_HIGH	0x12
#define INTR_POLARITY_LOW	0x13
#define INTR_LATCHED_CLR	0x14
#define INTR_EN_SET		0x15
#define INTR_EN_CLR		0x16
#define INTR_LATCHED_STS	0x18

#define HREAD4(sc, obj, reg)						\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh[obj], (reg)))
#define HWRITE4(sc, obj, reg, val)					\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh[obj], (reg), (val))
#define HSET4(sc, obj, reg, bits)					\
	HWRITE4((sc), (obj), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, obj, reg, bits)					\
	HWRITE4((sc), (obj), (reg), HREAD4((sc), (reg)) & ~(bits))

#define QCSPMI_REG_CORE		0
#define QCSPMI_REG_CHNLS	1
#define QCSPMI_REG_OBSRVR	2
#define QCSPMI_REG_INTR		3
#define QCSPMI_REG_CNFG		4
#define QCSPMI_REG_MAX		5

char *qcspmi_regs[] = { "core", "chnls", "obsrvr", "intr", "cnfg" };

struct qcspmi_apid {
	uint16_t		ppid;
	uint8_t			write_ee;
	uint8_t			irq_ee;
};

struct qcspmi_intrhand {
	TAILQ_ENTRY(qcspmi_intrhand) ih_q;
	int (*ih_func)(void *);
	void *ih_arg;
	void *ih_sc;
	uint16_t ih_per;
	uint8_t ih_pin;
	uint8_t ih_sid;
	uint32_t ih_apid;
};

struct qcspmi_softc {
	struct device		sc_dev;
	int			sc_node;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh[QCSPMI_REG_MAX];
	void			*sc_ih;

	int			sc_ee;

	struct qcspmi_apid	sc_apid[SPMI_MAX_PERIPH];
	uint16_t		sc_ppid_to_apid[SPMI_MAX_PPID];
	uint16_t		sc_max_periph;
	bus_size_t		sc_chan_stride;
	bus_size_t		sc_obsv_ee_stride;
	bus_size_t		sc_obsv_apid_stride;
	bus_size_t		sc_arb_apid_map;
	bus_size_t		sc_ownership_table;

	struct spmi_controller	sc_tag;
	struct interrupt_controller sc_ic;

	TAILQ_HEAD(,qcspmi_intrhand) sc_intrq;
};

int	qcspmi_match(struct device *, void *, void *);
void	qcspmi_attach(struct device *, struct device *, void *);
int	qcspmi_print(void *, const char *);

int	qcspmi_cmd_read(void *, uint8_t, uint8_t, uint16_t, void *, size_t);
int	qcspmi_cmd_write(void *, uint8_t, uint8_t, uint16_t,
	    const void *, size_t);

void	*qcspmi_intr_establish(void *, int *, int, struct cpu_info *,
	    int (*)(void *), void *, char *);
void	qcspmi_intr_disestablish(void *);
void	qcspmi_intr_enable(void *);
void	qcspmi_intr_disable(void *);
void	qcspmi_intr_barrier(void *);
int	qcspmi_intr(void *);

const struct cfattach qcspmi_ca = {
	sizeof(struct qcspmi_softc), qcspmi_match, qcspmi_attach
};

struct cfdriver qcspmi_cd = {
	NULL, "qcspmi", DV_DULL
};

int
qcspmi_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "qcom,spmi-pmic-arb") ||
	    OF_is_compatible(faa->fa_node, "qcom,x1e80100-spmi-pmic-arb");
}

void
qcspmi_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct qcspmi_softc *sc = (struct qcspmi_softc *)self;
	struct qcspmi_apid *apid, *last_apid;
	uint32_t val, ppid, irq_own;
	struct fdt_reg *spmi_reg;
	int spmi_nreg;
	int i, j, node, spmi;

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;

	for (i = QCSPMI_REG_CORE; i < QCSPMI_REG_INTR; i++) {
		j = OF_getindex(faa->fa_node, qcspmi_regs[i], "reg-names");
		if (j < 0 || j >= faa->fa_nreg) {
			printf(": no %s registers\n", qcspmi_regs[i]);
			return;
		}

		if (bus_space_map(sc->sc_iot, faa->fa_reg[j].addr,
		    faa->fa_reg[j].size, 0, &sc->sc_ioh[i])) {
			printf(": can't map %s registers\n", qcspmi_regs[i]);
			return;
		}
	}

	spmi = OF_getnodebyname(faa->fa_node, "spmi");
	if (spmi) {
		/* Multiple busses; only support the first. */
		uint32_t reg[8];

		if (OF_getpropintarray(spmi, "reg", reg,
		    sizeof(reg)) != sizeof(reg)) {
			printf(": no spmi registers\n");
			return;
		}

		spmi_reg = faa->fa_reg;
		spmi_reg[0].addr = ((uint64_t)reg[0] << 32) | reg[1];
		spmi_reg[0].size = ((uint64_t)reg[2] << 32) | reg[3];
		spmi_reg[1].addr = ((uint64_t)reg[4] << 32) | reg[5];
		spmi_reg[1].size = ((uint64_t)reg[6] << 32) | reg[7];
		spmi_nreg = 2;
	} else {
		/* Single bus. */
		spmi = faa->fa_node;
		spmi_reg = faa->fa_reg;
		spmi_nreg = faa->fa_nreg;
	}

	for (i = QCSPMI_REG_INTR; i < QCSPMI_REG_MAX; i++) {
		j = OF_getindex(spmi, qcspmi_regs[i], "reg-names");
		if (j < 0 || j >= spmi_nreg) {
			printf(": no %s registers\n", qcspmi_regs[i]);
			return;
		}

		if (bus_space_map(sc->sc_iot, spmi_reg[j].addr,
		    spmi_reg[j].size, 0, &sc->sc_ioh[i])) {
			printf(": can't map %s registers\n", qcspmi_regs[i]);
			return;
		}
	}

	/* Support only version 5 and 7 for now */
	val = HREAD4(sc, QCSPMI_REG_CORE, SPMI_VERSION);
	if (val < SPMI_VERSION_V5_MIN) {
		printf(": unsupported version 0x%08x\n", val);
		return;
	}

	if (val < SPMI_VERSION_V7_MIN) {
		sc->sc_max_periph = 512;
		sc->sc_chan_stride = 0x10000;
		sc->sc_obsv_ee_stride = 0x10000;
		sc->sc_obsv_apid_stride = 0x00080;
		sc->sc_arb_apid_map = 0x00900;
		sc->sc_ownership_table = 0x00700;
	} else {
		sc->sc_max_periph = 1024;
		sc->sc_chan_stride = 0x01000;
		sc->sc_obsv_ee_stride = 0x08000;
		sc->sc_obsv_apid_stride = 0x00020;
		sc->sc_arb_apid_map = 0x02000;
		sc->sc_ownership_table = 0x00000;
	}

	KASSERT(sc->sc_max_periph <= SPMI_MAX_PERIPH);

	sc->sc_ee = OF_getpropint(sc->sc_node, "qcom,ee", 0);
	if (sc->sc_ee > 5) {
		printf(": unsupported EE\n");
		return;
	}

	TAILQ_INIT(&sc->sc_intrq);

	sc->sc_ih = fdt_intr_establish(spmi, IPL_BIO, qcspmi_intr,
	    sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		return;
	}

	printf("\n");

	for (i = 0; i < sc->sc_max_periph; i++) {
		val = HREAD4(sc, QCSPMI_REG_CORE, SPMI_ARB_APID_MAP(sc, i));
		if (!val)
			continue;
		ppid = (val >> SPMI_ARB_APID_MAP_PPID_SHIFT) &
		    SPMI_ARB_APID_MAP_PPID_MASK;
		irq_own = val & SPMI_ARB_APID_MAP_IRQ_OWNER;
		val = HREAD4(sc, QCSPMI_REG_CNFG, SPMI_OWNERSHIP_TABLE(sc, i));
		apid = &sc->sc_apid[i];
		apid->write_ee = SPMI_OWNERSHIP_TABLE_OWNER(val);
		apid->irq_ee = 0xff;
		if (irq_own)
			apid->irq_ee = apid->write_ee;
		last_apid = &sc->sc_apid[sc->sc_ppid_to_apid[ppid] &
		    SPMI_PPID_TO_APID_MASK];
		if (!(sc->sc_ppid_to_apid[ppid] & SPMI_PPID_TO_APID_VALID) ||
		    apid->write_ee == sc->sc_ee) {
			sc->sc_ppid_to_apid[ppid] = SPMI_PPID_TO_APID_VALID | i;
		} else if ((sc->sc_ppid_to_apid[ppid] &
		    SPMI_PPID_TO_APID_VALID) && irq_own &&
		    last_apid->write_ee == sc->sc_ee) {
			last_apid->irq_ee = apid->irq_ee;
		}
	}

	sc->sc_tag.sc_cookie = sc;
	sc->sc_tag.sc_cmd_read = qcspmi_cmd_read;
	sc->sc_tag.sc_cmd_write = qcspmi_cmd_write;

	sc->sc_ic.ic_node = spmi;
	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_establish = qcspmi_intr_establish;
	sc->sc_ic.ic_disestablish = qcspmi_intr_disestablish;
	sc->sc_ic.ic_enable = qcspmi_intr_enable;
	sc->sc_ic.ic_disable = qcspmi_intr_disable;
	sc->sc_ic.ic_barrier = qcspmi_intr_barrier;
	fdt_intr_register(&sc->sc_ic);

	for (node = OF_child(spmi); node; node = OF_peer(node)) {
		struct spmi_attach_args sa;
		uint32_t reg[2];
		char name[32];

		if (OF_getpropintarray(node, "reg", reg,
		    sizeof(reg)) != sizeof(reg))
			continue;

		memset(name, 0, sizeof(name));
		if (OF_getprop(node, "compatible", name, sizeof(name)) == -1)
			continue;
		if (name[0] == '\0')
			continue;

		memset(&sa, 0, sizeof(sa));
		sa.sa_tag = &sc->sc_tag;
		sa.sa_sid = reg[0];
		sa.sa_name = name;
		sa.sa_node = node;
		config_found(self, &sa, qcspmi_print);
	}
}

int
qcspmi_print(void *aux, const char *pnp)
{
	struct spmi_attach_args *sa = aux;

	if (pnp != NULL)
		printf("\"%s\" at %s", sa->sa_name, pnp);
	printf(" sid 0x%x", sa->sa_sid);

	return UNCONF;
}

int
qcspmi_cmd_read(void *cookie, uint8_t sid, uint8_t cmd, uint16_t addr,
    void *buf, size_t len)
{
	struct qcspmi_softc *sc = cookie;
	uint8_t *cbuf = buf;
	uint32_t reg;
	uint16_t apid, ppid;
	int bc = len - 1;
	int i;

	if (len == 0 || len > 8)
		return EINVAL;

	/* TODO: support more types */
	if (cmd != SPMI_CMD_EXT_READL)
		return EINVAL;

	ppid = (sid << 8) | (addr >> 8);
	if (!(sc->sc_ppid_to_apid[ppid] & SPMI_PPID_TO_APID_VALID))
		return ENXIO;
	apid = sc->sc_ppid_to_apid[ppid] & SPMI_PPID_TO_APID_MASK;

	HWRITE4(sc, QCSPMI_REG_OBSRVR,
	    SPMI_OBSV_OFF(sc, sc->sc_ee, apid) + SPMI_COMMAND,
	    SPMI_COMMAND_OP_EXT_READL | SPMI_COMMAND_ADDR(addr) |
	    SPMI_COMMAND_LEN(bc));

	for (i = 1000; i > 0; i--) {
		reg = HREAD4(sc, QCSPMI_REG_OBSRVR,
		    SPMI_OBSV_OFF(sc, sc->sc_ee, apid) + SPMI_STATUS);
		if (reg & SPMI_STATUS_DONE)
			break;
		if (reg & SPMI_STATUS_FAILURE) {
			printf(": transaction failed\n");
			return EIO;
		}
		if (reg & SPMI_STATUS_DENIED) {
			printf(": transaction denied\n");
			return EIO;
		}
		if (reg & SPMI_STATUS_DROPPED) {
			printf(": transaction dropped\n");
			return EIO;
		}
	}
	if (i == 0) {
		printf("\n");
		return ETIMEDOUT;
	}

	if (len > 0) {
		reg = HREAD4(sc, QCSPMI_REG_OBSRVR,
		    SPMI_OBSV_OFF(sc, sc->sc_ee, apid) + SPMI_RDATA0);
		memcpy(cbuf, &reg, MIN(len, 4));
		cbuf += MIN(len, 4);
		len -= MIN(len, 4);
	}
	if (len > 0) {
		reg = HREAD4(sc, QCSPMI_REG_OBSRVR,
		    SPMI_OBSV_OFF(sc, sc->sc_ee, apid) + SPMI_RDATA1);
		memcpy(cbuf, &reg, MIN(len, 4));
		cbuf += MIN(len, 4);
		len -= MIN(len, 4);
	}

	return 0;
}

int
qcspmi_cmd_write(void *cookie, uint8_t sid, uint8_t cmd, uint16_t addr,
    const void *buf, size_t len)
{
	struct qcspmi_softc *sc = cookie;
	const uint8_t *cbuf = buf;
	uint32_t reg;
	uint16_t apid, ppid;
	int bc = len - 1;
	int i;

	if (len == 0 || len > 8)
		return EINVAL;

	/* TODO: support more types */
	if (cmd != SPMI_CMD_EXT_WRITEL)
		return EINVAL;

	ppid = (sid << 8) | (addr >> 8);
	if (!(sc->sc_ppid_to_apid[ppid] & SPMI_PPID_TO_APID_VALID))
		return ENXIO;
	apid = sc->sc_ppid_to_apid[ppid] & SPMI_PPID_TO_APID_MASK;

	if (sc->sc_apid[apid].write_ee != sc->sc_ee)
		return EPERM;

	if (len > 0) {
		memcpy(&reg, cbuf, MIN(len, 4));
		HWRITE4(sc, QCSPMI_REG_CHNLS, SPMI_CHAN_OFF(sc, apid) +
		    SPMI_WDATA0, reg);
		cbuf += MIN(len, 4);
		len -= MIN(len, 4);
	}
	if (len > 0) {
		memcpy(&reg, cbuf, MIN(len, 4));
		HWRITE4(sc, QCSPMI_REG_CHNLS, SPMI_CHAN_OFF(sc, apid) +
		    SPMI_WDATA1, reg);
		cbuf += MIN(len, 4);
		len -= MIN(len, 4);
	}

	HWRITE4(sc, QCSPMI_REG_CHNLS, SPMI_CHAN_OFF(sc, apid) + SPMI_COMMAND,
	    SPMI_COMMAND_OP_EXT_WRITEL | SPMI_COMMAND_ADDR(addr) |
	    SPMI_COMMAND_LEN(bc));

	for (i = 1000; i > 0; i--) {
		reg = HREAD4(sc, QCSPMI_REG_CHNLS, SPMI_CHAN_OFF(sc, apid) +
		    SPMI_STATUS);
		if (reg & SPMI_STATUS_DONE)
			break;
	}
	if (i == 0)
		return ETIMEDOUT;

	if (reg & SPMI_STATUS_FAILURE ||
	    reg & SPMI_STATUS_DENIED ||
	    reg & SPMI_STATUS_DROPPED)
		return EIO;

	return 0;
}

void *
qcspmi_intr_establish(void *cookie, int *cells, int ipl,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct qcspmi_softc *sc = cookie;
	struct qcspmi_intrhand *ih;
	uint16_t ppid;
	uint8_t reg[3];
	int error;

	ppid = cells[0] << 8 | cells[1];
	if (!(sc->sc_ppid_to_apid[ppid] & SPMI_PPID_TO_APID_VALID))
		return NULL;

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK | M_ZERO);
	ih->ih_func = func;
	ih->ih_arg = arg;
	ih->ih_sc = sc;
	ih->ih_sid = cells[0];
	ih->ih_per = cells[1];
	ih->ih_pin = cells[2];
	ih->ih_apid = sc->sc_ppid_to_apid[ppid] & SPMI_PPID_TO_APID_MASK;
	TAILQ_INSERT_TAIL(&sc->sc_intrq, ih, ih_q);

	error = spmi_cmd_read(&sc->sc_tag, ih->ih_sid, SPMI_CMD_EXT_READL,
	    (ih->ih_per << 8) | INTR_SET_TYPE, &reg, sizeof(reg));
	if (error)
		printf("%s: cannot read irq setting\n", sc->sc_dev.dv_xname);

	reg[0] &= ~(1U << ih->ih_pin);
	reg[1] &= ~(1U << ih->ih_pin);
	reg[2] &= ~(1U << ih->ih_pin);

	switch (cells[3]) {
	case 1:
		reg[0] |= (1U << ih->ih_pin); /* edge */
		reg[1] |= (1U << ih->ih_pin); /* rising */
		break;
	case 2:
		reg[0] |= (1U << ih->ih_pin); /* edge */
		reg[2] |= (1U << ih->ih_pin); /* falling */
		break;
	case 3:
		reg[0] |= (1U << ih->ih_pin); /* edge */
		reg[1] |= (1U << ih->ih_pin); /* rising */
		reg[2] |= (1U << ih->ih_pin); /* falling */
		break;
	case 4:
		reg[1] |= (1U << ih->ih_pin); /* high */
		break;
	case 8:
		reg[2] |= (1U << ih->ih_pin); /* low */
		break;
	default:
		printf("%s: unsupported interrupt mode/polarity\n",
		    sc->sc_dev.dv_xname);
		break;
	}

	error = spmi_cmd_write(&sc->sc_tag, ih->ih_sid, SPMI_CMD_EXT_WRITEL,
	    (ih->ih_per << 8) | INTR_SET_TYPE, &reg, sizeof(reg));
	if (error)
		printf("%s: cannot write irq setting\n", sc->sc_dev.dv_xname);

	HWRITE4(sc, QCSPMI_REG_CHNLS, SPMI_CHAN_OFF(sc, ih->ih_apid) +
	    SPMI_IRQ_CLEAR, (1U << ih->ih_pin));
	qcspmi_intr_enable(ih);

	if (ipl & IPL_WAKEUP)
		intr_set_wakeup(sc->sc_ih);

	return ih;
}

void
qcspmi_intr_disestablish(void *cookie)
{
	struct qcspmi_intrhand *ih = cookie;
	struct qcspmi_softc *sc = ih->ih_sc;

	qcspmi_intr_disable(cookie);

	TAILQ_REMOVE(&sc->sc_intrq, ih, ih_q);
	free(ih, M_DEVBUF, sizeof(*ih));
}

void
qcspmi_intr_enable(void *cookie)
{
	struct qcspmi_intrhand *ih = cookie;
	struct qcspmi_softc *sc = ih->ih_sc;
	uint8_t reg[2];
	int error;

	HWRITE4(sc, QCSPMI_REG_CHNLS, SPMI_CHAN_OFF(sc, ih->ih_apid) +
	    SPMI_ACC_ENABLE, SPMI_ACC_ENABLE_BIT);

	error = spmi_cmd_read(&sc->sc_tag, ih->ih_sid, SPMI_CMD_EXT_READL,
	    (ih->ih_per << 8) | INTR_EN_SET, &reg, 1);
	if (error)
		printf("%s: cannot read irq setting\n", sc->sc_dev.dv_xname);

	if (!(reg[0] & (1U << ih->ih_pin))) {
		reg[0] = (1U << ih->ih_pin);
		reg[1] = (1U << ih->ih_pin);
		error = spmi_cmd_write(&sc->sc_tag, ih->ih_sid,
		    SPMI_CMD_EXT_WRITEL, (ih->ih_per << 8) | INTR_LATCHED_CLR,
		    &reg, 2);
		if (error)
			printf("%s: cannot enable irq\n", sc->sc_dev.dv_xname);
	}
}

void
qcspmi_intr_disable(void *cookie)
{
	struct qcspmi_intrhand *ih = cookie;
	struct qcspmi_softc *sc = ih->ih_sc;
	uint8_t reg = (1U << ih->ih_pin);
	int error;

	error = spmi_cmd_write(&sc->sc_tag, ih->ih_sid, SPMI_CMD_EXT_WRITEL,
	    (ih->ih_per << 8) | INTR_EN_CLR, &reg, sizeof(reg));
	if (error)
		printf("%s: cannot disable irq\n", sc->sc_dev.dv_xname);
}

void
qcspmi_intr_barrier(void *cookie)
{
	struct qcspmi_intrhand *ih = cookie;
	struct qcspmi_softc *sc = ih->ih_sc;

	intr_barrier(sc->sc_ih);
}

int
qcspmi_intr(void *arg)
{
	struct qcspmi_softc *sc = arg;
	struct qcspmi_intrhand *ih;
	uint32_t status;
	uint8_t reg;
	int error;
	int handled = 0;

	TAILQ_FOREACH(ih, &sc->sc_intrq, ih_q) {
		status = HREAD4(sc, QCSPMI_REG_INTR,
		    SPMI_OWNER_ACC_STATUS(sc, sc->sc_ee, ih->ih_apid / 32));
		if (!(status & (1U << (ih->ih_apid % 32))))
			continue;
		status = HREAD4(sc, QCSPMI_REG_CHNLS,
		    SPMI_CHAN_OFF(sc, ih->ih_apid) + SPMI_ACC_ENABLE);
		if (!(status & SPMI_ACC_ENABLE_BIT))
			continue;
		status = HREAD4(sc, QCSPMI_REG_CHNLS,
		    SPMI_CHAN_OFF(sc, ih->ih_apid) + SPMI_IRQ_STATUS);
		if (!(status & (1U << ih->ih_pin)))
			continue;

		ih->ih_func(ih->ih_arg);
		handled = 1;

		HWRITE4(sc, QCSPMI_REG_CHNLS, SPMI_CHAN_OFF(sc, ih->ih_apid) +
		    SPMI_IRQ_CLEAR, (1U << ih->ih_pin));
		reg = 1U << ih->ih_pin;
		error = spmi_cmd_write(&sc->sc_tag, ih->ih_sid,
		    SPMI_CMD_EXT_WRITEL, (ih->ih_per << 8) | INTR_LATCHED_CLR,
		    &reg, sizeof(reg));
		if (error)
			printf("%s: cannot clear irq\n", sc->sc_dev.dv_xname);
	}

	return handled;
}
