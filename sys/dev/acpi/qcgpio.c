/*	$OpenBSD: qcgpio.c,v 1.15 2025/06/16 15:44:35 kettenis Exp $	*/
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
#include <sys/malloc.h>
#include <sys/systm.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

//#define QCGPIO_DEBUG
#ifdef QCGPIO_DEBUG
int qcgpio_debug = 1;
#define DPRINTF(l, x...) do { if ((l) <= qcgpio_debug) printf(x); } while (0)
#else
#define DPRINTF(l, x...)
#endif

/* Registers. */
#define TLMM_GPIO_IN_OUT(pin)		(0x0004 + 0x1000 * (pin))
#define  TLMM_GPIO_IN_OUT_GPIO_IN			(1 << 0)
#define  TLMM_GPIO_IN_OUT_GPIO_OUT			(1 << 1)
#define TLMM_GPIO_INTR_CFG(pin)		(0x0008 + 0x1000 * (pin))
#define  TLMM_GPIO_INTR_CFG_TARGET_PROC_MASK		(0x7 << 5)
#define  TLMM_GPIO_INTR_CFG_TARGET_PROC_RPM		(0x3 << 5)
#define  TLMM_GPIO_INTR_CFG_INTR_RAW_STATUS_EN		(1 << 4)
#define  TLMM_GPIO_INTR_CFG_INTR_DECT_CTL_MASK		(0x3 << 2)
#define  TLMM_GPIO_INTR_CFG_INTR_DECT_CTL_LEVEL		(0x0 << 2)
#define  TLMM_GPIO_INTR_CFG_INTR_DECT_CTL_EDGE_POS	(0x1 << 2)
#define  TLMM_GPIO_INTR_CFG_INTR_DECT_CTL_EDGE_NEG	(0x2 << 2)
#define  TLMM_GPIO_INTR_CFG_INTR_DECT_CTL_EDGE_BOTH	(0x3 << 2)
#define  TLMM_GPIO_INTR_CFG_INTR_POL_CTL		(1 << 1)
#define  TLMM_GPIO_INTR_CFG_INTR_ENABLE			(1 << 0)
#define TLMM_GPIO_INTR_STATUS(pin)	(0x000c + 0x1000 * (pin))
#define  TLMM_GPIO_INTR_STATUS_INTR_STATUS		(1 << 0)

/* SC7180 has multiple tiles */
#define QCGPIO_SC7180_WEST	0x00100000
#define QCGPIO_SC7180_NORTH	0x00500000
#define QCGPIO_SC7180_SOUTH	0x00900000

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct qcgpio_intrhand {
	int (*ih_func)(void *);
	void *ih_arg;
	int ih_ipl;
};

struct qcgpio_pdcmap {
	int		pm_pin;
	uint32_t	pm_irq;
};

struct qcgpio_softc {
	struct device		sc_dev;
	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_node;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	void			*sc_ih;

	uint32_t		sc_npins;
	int			(*sc_pin_map)(struct qcgpio_softc *, int,
				    bus_size_t *);
	struct qcgpio_intrhand	*sc_pin_ih;

	struct acpi_gpio sc_gpio;

	struct qcgpio_pdcmap	*sc_pdcmap;
	uint32_t		 sc_npdcmap;
	uint32_t		 sc_ipdcmap;
};

int	qcgpio_acpi_match(struct device *, void *, void *);
void	qcgpio_acpi_attach(struct device *, struct device *, void *);

const struct cfattach qcgpio_acpi_ca = {
	sizeof(struct qcgpio_softc), qcgpio_acpi_match, qcgpio_acpi_attach
};

struct cfdriver qcgpio_cd = {
	NULL, "qcgpio", DV_DULL
};

const char *qcgpio_hids[] = {
	"QCOM060C",
	"QCOM080D",
	"QCOM0C0C",
	NULL
};

/* 98b9b2a4-1663-4a5f-82f2-c6c99a394726 */
static uint8_t qcgpio_gpio_dsm_uuid[] = {
	0xa4, 0xb2, 0xb9, 0x98, 0x63, 0x16, 0x5f, 0x4a,
	0x82, 0xf2, 0xc6, 0xc9, 0x9a, 0x39, 0x47, 0x26
};
#define QCGPIO_GPIO_DSM_REV		0
#define QCGPIO_GPIO_DSM_FUNC_NUM_PINS	2

/* 921b0fd4-567c-43a0-bb14-2648f7b2a18c */
static uint8_t qcgpio_pdc_dsm_uuid[] = {
	0xd4, 0x0f, 0x1b, 0x92, 0x7c, 0x56, 0xa0, 0x43,
	0xbb, 0x14, 0x26, 0x48, 0xf7, 0xb2, 0xa1, 0x8c
};
#define QCGPIO_PDC_DSM_REV		0
#define QCGPIO_PDC_DSM_FUNC_CIPR	2

int	qcgpio_get_nirq(int, union acpi_resource *, void *);
int	qcgpio_get_irqs(int, union acpi_resource *, void *);
void	qcgpio_fill_pdcmap(struct qcgpio_softc *);
int	qcgpio_get_pin_count(struct acpi_softc *, struct aml_node *);
int	qcgpio_sc7180_pin_map(struct qcgpio_softc *, int, bus_size_t *);
int	qcgpio_sc8280xp_pin_map(struct qcgpio_softc *, int, bus_size_t *);
int	qcgpio_x1e80100_pin_map(struct qcgpio_softc *, int, bus_size_t *);

int	qcgpio_read_pin(void *, int);
void	qcgpio_write_pin(void *, int, int);
void	qcgpio_intr_establish(void *, int, int, int, int (*)(void *), void *);
void	qcgpio_intr_enable(void *, int);
void	qcgpio_intr_disable(void *, int);
int	qcgpio_intr(void *);

int
qcgpio_get_nirq(int crsidx, union acpi_resource *crs, void *arg)
{
	struct qcgpio_softc *sc = arg;
	int typ;

	typ = AML_CRSTYPE(crs);

	switch (typ) {
	case LR_EXTIRQ:
		sc->sc_npdcmap++;
		break;
	}

	return 0;
}

int
qcgpio_get_irqs(int crsidx, union acpi_resource *crs, void *arg)
{
	struct qcgpio_softc *sc = arg;
	int typ;

	typ = AML_CRSTYPE(crs);

	switch (typ) {
	case LR_EXTIRQ:
		sc->sc_pdcmap[sc->sc_ipdcmap].pm_irq = crs->lr_extirq.irq[0];
		sc->sc_pdcmap[sc->sc_ipdcmap].pm_pin = -1;
		DPRINTF(1, "%s: irq index %d: irq %d\n",
		    __func__, sc->sc_ipdcmap,
		    sc->sc_pdcmap[sc->sc_ipdcmap].pm_irq);
		sc->sc_ipdcmap++;
		break;
	}

	return 0;
}

void
qcgpio_fill_pdcmap(struct qcgpio_softc *sc)
{
	struct aml_value cmd[4], res, *ref;
	int i, j, pin;
	uint32_t irq;

	bzero(&cmd, sizeof(cmd));
	cmd[0].type = AML_OBJTYPE_BUFFER;
	cmd[0].v_buffer = (uint8_t *)&qcgpio_pdc_dsm_uuid;
	cmd[0].length = sizeof(qcgpio_pdc_dsm_uuid);
	/* rev */
	cmd[1].type = AML_OBJTYPE_INTEGER;
	cmd[1].v_integer = QCGPIO_PDC_DSM_REV;
	cmd[1].length = 1;
	/* func */
	cmd[2].type = AML_OBJTYPE_INTEGER;
	cmd[2].v_integer = QCGPIO_PDC_DSM_FUNC_CIPR;
	cmd[2].length = 1;
	/* not used */
	cmd[3].type = AML_OBJTYPE_PACKAGE;
	cmd[3].v_integer = 0;
	cmd[3].length = 0;

	if (aml_evalname(sc->sc_acpi, sc->sc_node, "_DSM", 4, cmd, &res)) {
		printf("%s: PDC _DSM failed\n", __func__);
		return;
	}

	for (i = 0; i < res.length; i++) {
		ref = res.v_package[i];

		if (ref->type != AML_OBJTYPE_PACKAGE ||
		    ref->length < 3 ||
		    ref->v_package[0]->type != AML_OBJTYPE_INTEGER ||
		    ref->v_package[1]->type != AML_OBJTYPE_INTEGER ||
		    ref->v_package[2]->type != AML_OBJTYPE_INTEGER) {
			continue;
		}

		irq = ref->v_package[2]->v_integer;
		pin = ref->v_package[1]->v_integer;
		DPRINTF(1, "%s: pdc index %d: probing irq %d, pin %d\n",
		    __func__, i, irq, pin);

		for (j = 0; j < sc->sc_npdcmap; j++) {
			if (sc->sc_pdcmap[j].pm_irq == irq) {
				sc->sc_pdcmap[j].pm_pin = pin;
				break;
			}
		}
	}
#ifdef QCGPIO_DEBUG
	for (i = 0; i < sc->sc_npdcmap; i++) {
		printf("%s: irq index %d: irq=%d, pin=%d\n",
		    __func__, i, sc->sc_pdcmap[i].pm_irq,
		    sc->sc_pdcmap[i].pm_pin);
	}
#endif
}

int
qcgpio_get_pin_count(struct acpi_softc *sc, struct aml_node *node)
{
	struct aml_value cmd[4];
	int64_t npins;

	bzero(&cmd, sizeof(cmd));
	cmd[0].type = AML_OBJTYPE_BUFFER;
	cmd[0].v_buffer = (uint8_t *)&qcgpio_gpio_dsm_uuid;
	cmd[0].length = sizeof(qcgpio_gpio_dsm_uuid);
	/* rev */
	cmd[1].type = AML_OBJTYPE_INTEGER;
	cmd[1].v_integer = QCGPIO_GPIO_DSM_REV;
	cmd[1].length = 1;
	/* func */
	cmd[2].type = AML_OBJTYPE_INTEGER;
	cmd[2].v_integer = QCGPIO_GPIO_DSM_FUNC_NUM_PINS;
	cmd[2].length = 1;
	/* not used */
	cmd[3].type = AML_OBJTYPE_PACKAGE;
	cmd[3].v_integer = 0;
	cmd[3].length = 0;

	if (aml_evalinteger(sc, node, "_DSM", 4, cmd, &npins)) {
		printf("%s: GPIO _DSM failed\n", __func__);
		return 0;
	}

	return (uint32_t)npins;
}

int
qcgpio_acpi_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	if (aaa->aaa_naddr < 1 || aaa->aaa_nirq < 1)
		return 0;
	return acpi_matchhids(aaa, qcgpio_hids, cf->cf_driver->cd_name);
}

void
qcgpio_acpi_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct qcgpio_softc *sc = (struct qcgpio_softc *)self;
	struct aml_value res;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_node = aaa->aaa_node;
	printf(" %s", sc->sc_node->name);

	printf(" addr 0x%llx/0x%llx", aaa->aaa_addr[0], aaa->aaa_size[0]);

	sc->sc_iot = aaa->aaa_bst[0];
	if (bus_space_map(sc->sc_iot, aaa->aaa_addr[0], aaa->aaa_size[0],
	    0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	if (strcmp(aaa->aaa_dev, "QCOM080D") == 0) {
		sc->sc_npins = 119;
		sc->sc_pin_map = qcgpio_sc7180_pin_map;
	} else if (strcmp(aaa->aaa_dev, "QCOM060C") == 0) {
		sc->sc_npins = 228;
		sc->sc_pin_map = qcgpio_sc8280xp_pin_map;
	} else if (strcmp(aaa->aaa_dev, "QCOM0C0C") == 0) {
		if (aml_evalname(sc->sc_acpi, sc->sc_node, "_CRS", 0, NULL,
		    &res)) {
			printf("no _CRS method\n");
			return;
		}
		if (res.type != AML_OBJTYPE_BUFFER || res.length < 5) {
			printf("invalid _CRS object\n");
			aml_freevalue(&res);
			return;
		}
		aml_parse_resource(&res, qcgpio_get_nirq, sc);
		DPRINTF(1, "\n%s: npdcmap=%d\n", __func__, sc->sc_npdcmap);
		sc->sc_pdcmap = mallocarray(sc->sc_npdcmap,
		    sizeof(*sc->sc_pdcmap), M_DEVBUF, M_WAITOK | M_ZERO);
		aml_parse_resource(&res, qcgpio_get_irqs, sc);
		aml_freevalue(&res);
		qcgpio_fill_pdcmap(sc);
		sc->sc_npins = qcgpio_get_pin_count(sc->sc_acpi, sc->sc_node);
		DPRINTF(1, "%s: npins=%d\n", __func__, sc->sc_npins);
		sc->sc_pin_map = qcgpio_x1e80100_pin_map;
	}
	KASSERT(sc->sc_npins != 0);

	sc->sc_pin_ih = mallocarray(sc->sc_npins, sizeof(*sc->sc_pin_ih),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	printf(" irq %d", aaa->aaa_irq[0]);

	sc->sc_ih = acpi_intr_establish(aaa->aaa_irq[0],
	    aaa->aaa_irq_flags[0], IPL_BIO, qcgpio_intr,
	    sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto unmap;
	}

	sc->sc_gpio.cookie = sc;
	sc->sc_gpio.read_pin = qcgpio_read_pin;
	sc->sc_gpio.write_pin = qcgpio_write_pin;
	sc->sc_gpio.intr_establish = qcgpio_intr_establish;
	sc->sc_gpio.intr_enable = qcgpio_intr_enable;
	sc->sc_gpio.intr_disable = qcgpio_intr_disable;
	sc->sc_node->gpio = &sc->sc_gpio;

	printf("\n");

	acpi_register_gpio(sc->sc_acpi, sc->sc_node);
	return;

unmap:
	if (sc->sc_ih)
		acpi_intr_disestablish(sc->sc_ih);
	free(sc->sc_pin_ih, M_DEVBUF, sc->sc_npins * sizeof(*sc->sc_pin_ih));
	free(sc->sc_pdcmap, M_DEVBUF, sc->sc_npdcmap * sizeof(*sc->sc_pdcmap));
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, aaa->aaa_size[0]);
}

int
qcgpio_sc7180_pin_map(struct qcgpio_softc *sc, int pin, bus_size_t *off)
{
	switch (pin) {
	case 30:
		*off = QCGPIO_SC7180_SOUTH;
		return 30;
#if 0
	/* XXX: Disable until we can fix the interrupt storm. */
	case 32:
	case 0x140:
		*off = QCGPIO_SC7180_NORTH;
		return 32;
#endif
	case 33:
	case 0x180:
		*off = QCGPIO_SC7180_NORTH;
		return 33;
	case 94:
	case 0x1c0:
		*off = QCGPIO_SC7180_SOUTH;
		return 94;
	default:
		return -1;
	}
}

int
qcgpio_sc8280xp_pin_map(struct qcgpio_softc *sc, int pin, bus_size_t *off)
{
	switch (pin) {
	case 107:
	case 175:
		return pin;
	case 0x2c0:
		return 107;
	case 0x340:
		return 104;
	case 0x380:
		return 182;
	default:
		return -1;
	}
}

int
qcgpio_x1e80100_pin_map(struct qcgpio_softc *sc, int pin, bus_size_t *off)
{
	int real_pin = -1;

	if (pin < sc->sc_npins) {
		real_pin = pin;
	} else if (pin / 64 < sc->sc_npdcmap) {
		real_pin = sc->sc_pdcmap[pin / 64].pm_pin;
	}

	DPRINTF(2, "%s: map pin %d to real_pin %d\n", __func__, pin, real_pin);

	return real_pin;
}

int
qcgpio_read_pin(void *cookie, int pin)
{
	struct qcgpio_softc *sc = cookie;
	bus_size_t off = 0;
	uint32_t reg;

	pin = sc->sc_pin_map(sc, pin, &off);
	if (pin < 0 || pin >= sc->sc_npins)
		return 0;

	reg = HREAD4(sc, off + TLMM_GPIO_IN_OUT(pin));
	return !!(reg & TLMM_GPIO_IN_OUT_GPIO_IN);
}

void
qcgpio_write_pin(void *cookie, int pin, int val)
{
	struct qcgpio_softc *sc = cookie;
	bus_size_t off = 0;

	pin = sc->sc_pin_map(sc, pin, &off);
	if (pin < 0 || pin >= sc->sc_npins)
		return;

	if (val) {
		HSET4(sc, off + TLMM_GPIO_IN_OUT(pin),
		    TLMM_GPIO_IN_OUT_GPIO_OUT);
	} else {
		HCLR4(sc, off + TLMM_GPIO_IN_OUT(pin),
		    TLMM_GPIO_IN_OUT_GPIO_OUT);
	}
}

void
qcgpio_intr_establish(void *cookie, int pin, int flags, int level,
    int (*func)(void *), void *arg)
{
	struct qcgpio_softc *sc = cookie;
	bus_size_t off = 0;
	uint32_t reg;

	pin = sc->sc_pin_map(sc, pin, &off);
	if (pin < 0 || pin >= sc->sc_npins)
		return;

	sc->sc_pin_ih[pin].ih_func = func;
	sc->sc_pin_ih[pin].ih_arg = arg;
	sc->sc_pin_ih[pin].ih_ipl = level & ~IPL_WAKEUP;

	reg = HREAD4(sc, off + TLMM_GPIO_INTR_CFG(pin));
	reg &= ~TLMM_GPIO_INTR_CFG_INTR_DECT_CTL_MASK;
	reg &= ~TLMM_GPIO_INTR_CFG_INTR_POL_CTL;
	switch (flags & (LR_GPIO_MODE | LR_GPIO_POLARITY)) {
	case LR_GPIO_LEVEL | LR_GPIO_ACTLO:
		reg |= TLMM_GPIO_INTR_CFG_INTR_DECT_CTL_LEVEL;
		break;
	case LR_GPIO_LEVEL | LR_GPIO_ACTHI:
		reg |= TLMM_GPIO_INTR_CFG_INTR_DECT_CTL_LEVEL |
		    TLMM_GPIO_INTR_CFG_INTR_POL_CTL;
		break;
	case LR_GPIO_EDGE | LR_GPIO_ACTLO:
		reg |= TLMM_GPIO_INTR_CFG_INTR_DECT_CTL_EDGE_NEG |
		    TLMM_GPIO_INTR_CFG_INTR_POL_CTL;
		break;
	case LR_GPIO_EDGE | LR_GPIO_ACTHI:
		reg |= TLMM_GPIO_INTR_CFG_INTR_DECT_CTL_EDGE_POS |
		    TLMM_GPIO_INTR_CFG_INTR_POL_CTL;
		break;
	case LR_GPIO_EDGE | LR_GPIO_ACTBOTH:
		reg |= TLMM_GPIO_INTR_CFG_INTR_DECT_CTL_EDGE_BOTH;
		break;
	default:
		printf("%s: unsupported interrupt mode/polarity\n",
		    sc->sc_dev.dv_xname);
		break;
	}
	reg &= ~TLMM_GPIO_INTR_CFG_TARGET_PROC_MASK;
	reg |= TLMM_GPIO_INTR_CFG_TARGET_PROC_RPM;
	reg |= TLMM_GPIO_INTR_CFG_INTR_RAW_STATUS_EN;
	reg |= TLMM_GPIO_INTR_CFG_INTR_ENABLE;
	HWRITE4(sc, off + TLMM_GPIO_INTR_CFG(pin), reg);
}

void
qcgpio_intr_enable(void *cookie, int pin)
{
	struct qcgpio_softc *sc = cookie;
	bus_size_t off = 0;

	pin = sc->sc_pin_map(sc, pin, &off);
	if (pin < 0 || pin >= sc->sc_npins)
		return;

	HSET4(sc, off + TLMM_GPIO_INTR_CFG(pin),
	    TLMM_GPIO_INTR_CFG_INTR_ENABLE);
}

void
qcgpio_intr_disable(void *cookie, int pin)
{
	struct qcgpio_softc *sc = cookie;
	bus_size_t off = 0;

	pin = sc->sc_pin_map(sc, pin, &off);
	if (pin < 0 || pin >= sc->sc_npins)
		return;

	HCLR4(sc, off + TLMM_GPIO_INTR_CFG(pin),
	    TLMM_GPIO_INTR_CFG_INTR_ENABLE);
}

int
qcgpio_intr(void *arg)
{
	struct qcgpio_softc *sc = arg;
	int pin, s, handled = 0;
	bus_size_t off = 0;
	uint32_t stat;

	for (pin = 0; pin < sc->sc_npins; pin++) {
		if (sc->sc_pin_ih[pin].ih_func == NULL)
			continue;

		sc->sc_pin_map(sc, pin, &off);

		stat = HREAD4(sc, off + TLMM_GPIO_INTR_STATUS(pin));
		if (stat & TLMM_GPIO_INTR_STATUS_INTR_STATUS) {
			s = splraise(sc->sc_pin_ih[pin].ih_ipl);
			sc->sc_pin_ih[pin].ih_func(sc->sc_pin_ih[pin].ih_arg);
			splx(s);
			HWRITE4(sc, off + TLMM_GPIO_INTR_STATUS(pin),
			    stat & ~TLMM_GPIO_INTR_STATUS_INTR_STATUS);
			handled = 1;
		}
	}

	return handled;
}
