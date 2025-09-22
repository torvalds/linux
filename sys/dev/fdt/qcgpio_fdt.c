/*	$OpenBSD: qcgpio_fdt.c,v 1.7 2025/06/16 09:27:38 kettenis Exp $	*/
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

#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/fdt.h>

#ifdef SUSPEND
extern int cpu_suspended;
#endif

/* Registers. */
#define TLMM_GPIO_CFG(pin)		(0x0000 + 0x1000 * (pin))
#define  TLMM_GPIO_CFG_OUT_EN				(1 << 9)
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
	void *ih_sc;
	int ih_pin;
	int ih_wakeup;
};

struct qcgpio_softc {
	struct device		sc_dev;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	void			*sc_ih;

	uint32_t		sc_npins;
	struct qcgpio_intrhand	*sc_pin_ih;

	struct gpio_controller	sc_gc;
	struct interrupt_controller sc_ic;
};

int	qcgpio_fdt_match(struct device *, void *, void *);
void	qcgpio_fdt_attach(struct device *, struct device *, void *);
int	qcgpio_fdt_activate(struct device *, int);

const struct cfattach qcgpio_fdt_ca = {
	sizeof(struct qcgpio_softc), qcgpio_fdt_match, qcgpio_fdt_attach, NULL,
	qcgpio_fdt_activate
};

void	qcgpio_fdt_config_pin(void *, uint32_t *, int);
int	qcgpio_fdt_get_pin(void *, uint32_t *);
void	qcgpio_fdt_set_pin(void *, uint32_t *, int);
void	*qcgpio_fdt_intr_establish_pin(void *, uint32_t *, int,
	    struct cpu_info *, int (*)(void *), void *, char *);

void	*qcgpio_fdt_intr_establish(void *, int *, int, struct cpu_info *,
	    int (*)(void *), void *, char *);
void	qcgpio_fdt_intr_disestablish(void *);
void	qcgpio_fdt_intr_enable(void *);
void	qcgpio_fdt_intr_disable(void *);
void	qcgpio_fdt_intr_barrier(void *);
int	qcgpio_fdt_intr(void *);

int
qcgpio_fdt_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "qcom,sc8280xp-tlmm") ||
	    OF_is_compatible(faa->fa_node, "qcom,x1e80100-tlmm"));
}

void
qcgpio_fdt_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct qcgpio_softc *sc = (struct qcgpio_softc *)self;

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr, faa->fa_reg[0].size,
	    0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	if (OF_is_compatible(faa->fa_node, "qcom,sc8280xp-tlmm"))
		sc->sc_npins = 230;
	else
		sc->sc_npins = 239;
	sc->sc_pin_ih = mallocarray(sc->sc_npins, sizeof(*sc->sc_pin_ih),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_BIO, qcgpio_fdt_intr,
	    sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto unmap;
	}

	sc->sc_gc.gc_node = faa->fa_node;
	sc->sc_gc.gc_cookie = sc;
	sc->sc_gc.gc_config_pin = qcgpio_fdt_config_pin;
	sc->sc_gc.gc_get_pin = qcgpio_fdt_get_pin;
	sc->sc_gc.gc_set_pin = qcgpio_fdt_set_pin;
	sc->sc_gc.gc_intr_establish = qcgpio_fdt_intr_establish_pin;
	gpio_controller_register(&sc->sc_gc);

	sc->sc_ic.ic_node = faa->fa_node;
	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_establish = qcgpio_fdt_intr_establish;
	sc->sc_ic.ic_disestablish = qcgpio_fdt_intr_disestablish;
	sc->sc_ic.ic_enable = qcgpio_fdt_intr_enable;
	sc->sc_ic.ic_disable = qcgpio_fdt_intr_disable;
	sc->sc_ic.ic_barrier = qcgpio_fdt_intr_barrier;
	fdt_intr_register(&sc->sc_ic);

	printf("\n");
	return;

unmap:
	if (sc->sc_ih)
		fdt_intr_disestablish(sc->sc_ih);
	free(sc->sc_pin_ih, M_DEVBUF, sc->sc_npins * sizeof(*sc->sc_pin_ih));
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, faa->fa_reg[0].size);
}

int
qcgpio_fdt_activate(struct device *self, int act)
{
	struct qcgpio_softc *sc = (struct qcgpio_softc *)self;
	int pin, rv = 0;

	switch (act) {
	case DVACT_SUSPEND:
		for (pin = 0; pin < sc->sc_npins; pin++) {
			if (sc->sc_pin_ih[pin].ih_func == NULL ||
			    sc->sc_pin_ih[pin].ih_wakeup)
				continue;
			HCLR4(sc, TLMM_GPIO_INTR_CFG(pin),
			    TLMM_GPIO_INTR_CFG_INTR_ENABLE);
		}
		break;
	case DVACT_RESUME:
		for (pin = 0; pin < sc->sc_npins; pin++) {
			if (sc->sc_pin_ih[pin].ih_func == NULL ||
			    sc->sc_pin_ih[pin].ih_wakeup)
				continue;
			HSET4(sc, TLMM_GPIO_INTR_CFG(pin),
			    TLMM_GPIO_INTR_CFG_INTR_ENABLE);
		}
		break;
	}

	return rv;
}

void
qcgpio_fdt_config_pin(void *cookie, uint32_t *cells, int config)
{
	struct qcgpio_softc *sc = cookie;
	uint32_t pin = cells[0];

	if (pin >= sc->sc_npins)
		return;

	if (config & GPIO_CONFIG_OUTPUT)
		HSET4(sc, TLMM_GPIO_CFG(pin), TLMM_GPIO_CFG_OUT_EN);
	else
		HCLR4(sc, TLMM_GPIO_CFG(pin), TLMM_GPIO_CFG_OUT_EN);
}

int
qcgpio_fdt_get_pin(void *cookie, uint32_t *cells)
{
	struct qcgpio_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	uint32_t reg;
	int val;

	if (pin >= sc->sc_npins)
		return 0;

	reg = HREAD4(sc, TLMM_GPIO_IN_OUT(pin));
	val = !!(reg & TLMM_GPIO_IN_OUT_GPIO_IN);
	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	return val;
}

void
qcgpio_fdt_set_pin(void *cookie, uint32_t *cells, int val)
{
	struct qcgpio_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];

	if (pin >= sc->sc_npins)
		return;

	if (flags & GPIO_ACTIVE_LOW)
		val = !val;

	if (val) {
		HSET4(sc, TLMM_GPIO_IN_OUT(pin),
		    TLMM_GPIO_IN_OUT_GPIO_OUT);
	} else {
		HCLR4(sc, TLMM_GPIO_IN_OUT(pin),
		    TLMM_GPIO_IN_OUT_GPIO_OUT);
	}
}

void *
qcgpio_fdt_intr_establish_pin(void *cookie, uint32_t *cells, int ipl,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct qcgpio_softc *sc = cookie;
	uint32_t icells[2];

	icells[0] = cells[0];
	icells[1] = 3; /* both edges */

	return qcgpio_fdt_intr_establish(sc, icells, ipl, ci, func, arg, name);
}

void *
qcgpio_fdt_intr_establish(void *cookie, int *cells, int ipl,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct qcgpio_softc *sc = cookie;
	uint32_t reg;
	int pin = cells[0];
	int level = cells[1];

	if (pin < 0 || pin >= sc->sc_npins)
		return NULL;

	sc->sc_pin_ih[pin].ih_func = func;
	sc->sc_pin_ih[pin].ih_arg = arg;
	sc->sc_pin_ih[pin].ih_pin = pin;
	sc->sc_pin_ih[pin].ih_sc = sc;

	if (ipl & IPL_WAKEUP) {
		sc->sc_pin_ih[pin].ih_wakeup = 1;
		intr_set_wakeup(sc->sc_ih);
	}

	reg = HREAD4(sc, TLMM_GPIO_INTR_CFG(pin));
	reg &= ~TLMM_GPIO_INTR_CFG_INTR_DECT_CTL_MASK;
	reg &= ~TLMM_GPIO_INTR_CFG_INTR_POL_CTL;
	switch (level) {
	case 1:
		reg |= TLMM_GPIO_INTR_CFG_INTR_DECT_CTL_EDGE_POS |
		    TLMM_GPIO_INTR_CFG_INTR_POL_CTL;
		break;
	case 2:
		reg |= TLMM_GPIO_INTR_CFG_INTR_DECT_CTL_EDGE_NEG |
		    TLMM_GPIO_INTR_CFG_INTR_POL_CTL;
		break;
	case 3:
		reg |= TLMM_GPIO_INTR_CFG_INTR_DECT_CTL_EDGE_BOTH;
		break;
	case 4:
		reg |= TLMM_GPIO_INTR_CFG_INTR_DECT_CTL_LEVEL |
		    TLMM_GPIO_INTR_CFG_INTR_POL_CTL;
		break;
	case 8:
		reg |= TLMM_GPIO_INTR_CFG_INTR_DECT_CTL_LEVEL;
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
	HWRITE4(sc, TLMM_GPIO_INTR_CFG(pin), reg);

	return &sc->sc_pin_ih[pin];
}

void
qcgpio_fdt_intr_disestablish(void *cookie)
{
	struct qcgpio_intrhand *ih = cookie;

	qcgpio_fdt_intr_disable(cookie);
	ih->ih_func = NULL;
}

void
qcgpio_fdt_intr_enable(void *cookie)
{
	struct qcgpio_intrhand *ih = cookie;
	struct qcgpio_softc *sc = ih->ih_sc;
	int pin = ih->ih_pin;

	if (pin < 0 || pin >= sc->sc_npins)
		return;

	HSET4(sc, TLMM_GPIO_INTR_CFG(pin),
	    TLMM_GPIO_INTR_CFG_INTR_ENABLE);
}

void
qcgpio_fdt_intr_disable(void *cookie)
{
	struct qcgpio_intrhand *ih = cookie;
	struct qcgpio_softc *sc = ih->ih_sc;
	int pin = ih->ih_pin;

	if (pin < 0 || pin >= sc->sc_npins)
		return;

	HCLR4(sc, TLMM_GPIO_INTR_CFG(pin),
	    TLMM_GPIO_INTR_CFG_INTR_ENABLE);
}

void
qcgpio_fdt_intr_barrier(void *cookie)
{
	struct qcgpio_intrhand *ih = cookie;
	struct qcgpio_softc *sc = ih->ih_sc;

	intr_barrier(sc->sc_ih);
}

int
qcgpio_fdt_intr(void *arg)
{
	struct qcgpio_softc *sc = arg;
	int pin, handled = 0;
	uint32_t stat;

	for (pin = 0; pin < sc->sc_npins; pin++) {
		if (sc->sc_pin_ih[pin].ih_func == NULL)
			continue;
#ifdef SUSPEND
		/*
		 * If we're suspend and this is not a wakeup pin,
		 * ignore the event and stay suspended.
		 */
		if (cpu_suspended && !sc->sc_pin_ih[pin].ih_wakeup)
			continue;
#endif

		stat = HREAD4(sc, TLMM_GPIO_INTR_STATUS(pin));
		if (stat & TLMM_GPIO_INTR_STATUS_INTR_STATUS) {
			sc->sc_pin_ih[pin].ih_func(sc->sc_pin_ih[pin].ih_arg);
			handled = 1;
		}
		HWRITE4(sc, TLMM_GPIO_INTR_STATUS(pin),
		    stat & ~TLMM_GPIO_INTR_STATUS_INTR_STATUS);
	}

	return handled;
}
