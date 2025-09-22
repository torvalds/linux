/*	$OpenBSD: qcpdc.c,v 1.3 2022/12/21 23:26:54 patrick Exp $	*/
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
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#define PDC_INTR_ENABLE(x)	(0x10 + (sizeof(uint32_t) * ((x) / 32)))
#define  PDC_INTR_ENABLE_BIT(x)		(1U << ((x) % 32))
#define PDC_INTR_CONFIG(x)	(0x110 + (sizeof(uint32_t) * (x)))
#define  PDC_INTR_CONFIG_LEVEL_LOW	0x0
#define  PDC_INTR_CONFIG_EDGE_FALLING	0x2
#define  PDC_INTR_CONFIG_LEVEL_HIGH	0x4
#define  PDC_INTR_CONFIG_EDGE_RISING	0x6
#define  PDC_INTR_CONFIG_EDGE_BOTH	0x7

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct intrhand {
	void *ih_cookie;
	void *ih_sc;
	int ih_pin;
};

struct qcpdc_pin_region {
	uint32_t pin_base;
	uint32_t gic_base;
	uint32_t count;
};

struct qcpdc_softc {
	struct device			 sc_dev;
	bus_space_tag_t			 sc_iot;
	bus_space_handle_t		 sc_ioh;
	int				 sc_node;

	struct qcpdc_pin_region		*sc_pr;
	int				 sc_npr;

	struct interrupt_controller	 sc_ic;
};

int	qcpdc_match(struct device *, void *, void *);
void	qcpdc_attach(struct device *, struct device *, void *);

const struct cfattach qcpdc_ca = {
	sizeof(struct qcpdc_softc), qcpdc_match, qcpdc_attach
};

struct cfdriver qcpdc_cd = {
	NULL, "qcpdc", DV_DULL
};

void	*qcpdc_intr_establish(void *, int *, int, struct cpu_info *,
	    int (*)(void *), void *, char *);
void	qcpdc_intr_disestablish(void *);
void	qcpdc_intr_enable(void *);
void	qcpdc_intr_disable(void *);
void	qcpdc_intr_barrier(void *);
void	qcpdc_intr_set_wakeup(void *);

int
qcpdc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "qcom,pdc");
}

void
qcpdc_attach(struct device *parent, struct device *self, void *aux)
{
	struct qcpdc_softc *sc = (struct qcpdc_softc *)self;
	struct fdt_attach_args *faa = aux;
	int i, j, len;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	len = OF_getproplen(faa->fa_node, "qcom,pdc-ranges");
	if (len <= 0 || len % (3 * sizeof(uint32_t)) != 0) {
		printf(": invalid ranges property\n");
		return;
	}

	sc->sc_npr = len / (3 * sizeof(uint32_t));
	sc->sc_pr = mallocarray(sc->sc_npr, sizeof(*sc->sc_pr),
	    M_DEVBUF, M_WAITOK);
	OF_getpropintarray(faa->fa_node, "qcom,pdc-ranges",
	    (uint32_t *)sc->sc_pr, len);

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	for (i = 0; i < sc->sc_npr; i++) {
		for (j = 0; j < sc->sc_pr[i].count; j++) {
			HCLR4(sc, PDC_INTR_ENABLE(sc->sc_pr[i].pin_base + j),
			    PDC_INTR_ENABLE_BIT(sc->sc_pr[i].pin_base + j));
		}
	}

	sc->sc_ic.ic_node = faa->fa_node;
	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_establish = qcpdc_intr_establish;
	sc->sc_ic.ic_disestablish = qcpdc_intr_disestablish;
	sc->sc_ic.ic_enable = qcpdc_intr_enable;
	sc->sc_ic.ic_disable = qcpdc_intr_disable;
	sc->sc_ic.ic_barrier = qcpdc_intr_barrier;
	sc->sc_ic.ic_set_wakeup = qcpdc_intr_set_wakeup;
	fdt_intr_register(&sc->sc_ic);

	printf("\n");
}

void *
qcpdc_intr_establish(void *aux, int *cells, int ipl,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct qcpdc_softc *sc = aux;
	struct intrhand *ih;
	void *cookie;
	uint32_t pcells[3];
	int pin = cells[0];
	int level = cells[1];
	int i, s;

	for (i = 0; i < sc->sc_npr; i++) {
		if (pin >= sc->sc_pr[i].pin_base &&
		    pin < sc->sc_pr[i].pin_base + sc->sc_pr[i].count)
			break;
	}
	if (i == sc->sc_npr)
		return NULL;

	switch (level) {
	case 1:
		HWRITE4(sc, PDC_INTR_CONFIG(pin), PDC_INTR_CONFIG_EDGE_RISING);
		break;
	case 2:
		HWRITE4(sc, PDC_INTR_CONFIG(pin), PDC_INTR_CONFIG_EDGE_FALLING);
		break;
	case 3:
		HWRITE4(sc, PDC_INTR_CONFIG(pin), PDC_INTR_CONFIG_EDGE_BOTH);
		break;
	case 4:
		HWRITE4(sc, PDC_INTR_CONFIG(pin), PDC_INTR_CONFIG_LEVEL_HIGH);
		break;
	case 8:
		HWRITE4(sc, PDC_INTR_CONFIG(pin), PDC_INTR_CONFIG_LEVEL_LOW);
		break;
	default:
		printf("%s: unsupported interrupt mode/polarity\n",
		    sc->sc_dev.dv_xname);
		return NULL;
	}

	pcells[0] = 0; /* GIC-SPI */
	pcells[1] = pin - sc->sc_pr[i].pin_base + sc->sc_pr[i].gic_base;
	pcells[2] = level;

	cookie = fdt_intr_parent_establish(&sc->sc_ic, &pcells[0], ipl, ci,
	    func, arg, name);
	if (cookie == NULL)
		return NULL;

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_cookie = cookie;
	ih->ih_sc = sc;
	ih->ih_pin = pin;

	s = splhigh();
	HSET4(sc, PDC_INTR_ENABLE(pin), PDC_INTR_ENABLE_BIT(pin));
	splx(s);

	return ih;
}

void
qcpdc_intr_disestablish(void *cookie)
{
	struct intrhand *ih = cookie;
	struct qcpdc_softc *sc = ih->ih_sc;
	int s;

	s = splhigh();
	HCLR4(sc, PDC_INTR_ENABLE(ih->ih_pin), PDC_INTR_ENABLE_BIT(ih->ih_pin));
	splx(s);

	fdt_intr_parent_disestablish(ih->ih_cookie);

	free(ih, M_DEVBUF, sizeof(*ih));
}

void
qcpdc_intr_enable(void *cookie)
{
	struct intrhand *ih = cookie;

	fdt_intr_enable(ih->ih_cookie);
}

void
qcpdc_intr_disable(void *cookie)
{
	struct intrhand *ih = cookie;

	fdt_intr_disable(ih->ih_cookie);
}

void
qcpdc_intr_barrier(void *cookie)
{
	struct intrhand *ih = cookie;

	intr_barrier(ih->ih_cookie);
}

void
qcpdc_intr_set_wakeup(void *cookie)
{
	struct intrhand *ih = cookie;

	intr_set_wakeup(ih->ih_cookie);
}
