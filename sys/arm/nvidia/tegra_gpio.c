/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Tegra GPIO driver.
 */
#include "opt_platform.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/gpio.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/resource.h>

#include <dev/gpio/gpiobusvar.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "pic_if.h"

#define	GPIO_LOCK(_sc)		mtx_lock(&(_sc)->mtx)
#define	GPIO_UNLOCK(_sc)	mtx_unlock(&(_sc)->mtx)
#define	GPIO_LOCK_INIT(_sc)	mtx_init(&_sc->mtx, 			\
	    device_get_nameunit(_sc->dev), "tegra_gpio", MTX_DEF)
#define	GPIO_LOCK_DESTROY(_sc)	mtx_destroy(&_sc->mtx);
#define	GPIO_ASSERT_LOCKED(_sc)	mtx_assert(&_sc->mtx, MA_OWNED);
#define	GPIO_ASSERT_UNLOCKED(_sc) mtx_assert(&_sc->mtx, MA_NOTOWNED);

#define	WR4(_sc, _r, _v)	bus_write_4((_sc)->mem_res, (_r), (_v))
#define	RD4(_sc, _r)		bus_read_4((_sc)->mem_res, (_r))

#define	GPIO_BANK_OFFS		0x100	/* Bank offset */
#define	GPIO_NUM_BANKS		8	/* Total number per bank */
#define	GPIO_REGS_IN_BANK	4	/* Total registers in bank */
#define	GPIO_PINS_IN_REG	8	/* Total pin in register */

#define	GPIO_BANKNUM(n)		((n) / (GPIO_REGS_IN_BANK * GPIO_PINS_IN_REG))
#define	GPIO_PORTNUM(n)		(((n) / GPIO_PINS_IN_REG) % GPIO_REGS_IN_BANK)
#define	GPIO_BIT(n)		((n) % GPIO_PINS_IN_REG)

#define	GPIO_REGNUM(n)		(GPIO_BANKNUM(n) * GPIO_BANK_OFFS + \
				    GPIO_PORTNUM(n) * 4)

#define	NGPIO	((GPIO_NUM_BANKS * GPIO_REGS_IN_BANK * GPIO_PINS_IN_REG) - 8)

/* Register offsets */
#define	GPIO_CNF		0x00
#define	GPIO_OE			0x10
#define	GPIO_OUT		0x20
#define	GPIO_IN			0x30
#define	GPIO_INT_STA		0x40
#define	GPIO_INT_ENB		0x50
#define	GPIO_INT_LVL		0x60
#define  GPIO_INT_LVL_DELTA		(1 << 16)
#define  GPIO_INT_LVL_EDGE		(1 << 8)
#define  GPIO_INT_LVL_HIGH		(1 << 0)
#define  GPIO_INT_LVL_MASK		(GPIO_INT_LVL_DELTA |		\
					 GPIO_INT_LVL_EDGE | GPIO_INT_LVL_HIGH)
#define	GPIO_INT_CLR		0x70
#define	GPIO_MSK_CNF		0x80
#define	GPIO_MSK_OE		0x90
#define	GPIO_MSK_OUT		0xA0
#define	GPIO_MSK_INT_STA	0xC0
#define	GPIO_MSK_INT_ENB	0xD0
#define	GPIO_MSK_INT_LVL	0xE0

char *tegra_gpio_port_names[] = {
	 "A",  "B",  "C",  "D", /* Bank 0 */
	 "E",  "F",  "G",  "H", /* Bank 1 */
	 "I",  "J",  "K",  "L", /* Bank 2 */
	 "M",  "N",  "O",  "P", /* Bank 3 */
	 "Q",  "R",  "S",  "T", /* Bank 4 */
	 "U",  "V",  "W",  "X", /* Bank 5 */
	 "Y",  "Z", "AA", "BB", /* Bank 6 */
	"CC", "DD", "EE"	/* Bank 7 */
};

struct tegra_gpio_irqsrc {
	struct intr_irqsrc	isrc;
	u_int			irq;
	uint32_t		cfgreg;
};

struct tegra_gpio_softc;
struct tegra_gpio_irq_cookie {
	struct tegra_gpio_softc	*sc;
	int			bank_num;
};

struct tegra_gpio_softc {
	device_t		dev;
	device_t		busdev;
	struct mtx		mtx;
	struct resource		*mem_res;
	struct resource		*irq_res[GPIO_NUM_BANKS];
	void			*irq_ih[GPIO_NUM_BANKS];
	struct tegra_gpio_irq_cookie irq_cookies[GPIO_NUM_BANKS];
	int			gpio_npins;
	struct gpio_pin		gpio_pins[NGPIO];
	struct tegra_gpio_irqsrc *isrcs;
};

static struct ofw_compat_data compat_data[] = {
	{"nvidia,tegra124-gpio", 1},
	{NULL,			0}
};

/* --------------------------------------------------------------------------
 *
 * GPIO
 *
 */
static inline void
gpio_write_masked(struct tegra_gpio_softc *sc, bus_size_t reg,
    struct gpio_pin *pin, uint32_t val)
{
	uint32_t tmp;
	int bit;

	bit = GPIO_BIT(pin->gp_pin);
	tmp = 0x100 << bit;		/* mask */
	tmp |= (val & 1) << bit;	/* value */
	bus_write_4(sc->mem_res, reg + GPIO_REGNUM(pin->gp_pin), tmp);
}

static inline uint32_t
gpio_read(struct tegra_gpio_softc *sc, bus_size_t reg, struct gpio_pin *pin)
{
	int bit;
	uint32_t val;

	bit = GPIO_BIT(pin->gp_pin);
	val = bus_read_4(sc->mem_res, reg + GPIO_REGNUM(pin->gp_pin));
	return (val >> bit) & 1;
}

static void
tegra_gpio_pin_configure(struct tegra_gpio_softc *sc, struct gpio_pin *pin,
    unsigned int flags)
{

	if ((flags & (GPIO_PIN_INPUT|GPIO_PIN_OUTPUT)) == 0)
		return;

	/* Manage input/output */
	pin->gp_flags &= ~(GPIO_PIN_INPUT | GPIO_PIN_OUTPUT);
	if (flags & GPIO_PIN_OUTPUT) {
		pin->gp_flags |= GPIO_PIN_OUTPUT;
		gpio_write_masked(sc, GPIO_MSK_OE, pin, 1);
	} else {
		pin->gp_flags |= GPIO_PIN_INPUT;
		gpio_write_masked(sc, GPIO_MSK_OE, pin, 0);
	}
}

static device_t
tegra_gpio_get_bus(device_t dev)
{
	struct tegra_gpio_softc *sc;

	sc = device_get_softc(dev);
	return (sc->busdev);
}

static int
tegra_gpio_pin_max(device_t dev, int *maxpin)
{

	*maxpin = NGPIO - 1;
	return (0);
}

static int
tegra_gpio_pin_getcaps(device_t dev, uint32_t pin, uint32_t *caps)
{
	struct tegra_gpio_softc *sc;

	sc = device_get_softc(dev);
	if (pin >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	*caps = sc->gpio_pins[pin].gp_caps;
	GPIO_UNLOCK(sc);

	return (0);
}

static int
tegra_gpio_pin_getflags(device_t dev, uint32_t pin, uint32_t *flags)
{
	struct tegra_gpio_softc *sc;
	int cnf;

	sc = device_get_softc(dev);
	if (pin >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	cnf = gpio_read(sc, GPIO_CNF, &sc->gpio_pins[pin]);
	if (cnf == 0) {
		GPIO_UNLOCK(sc);
		return (ENXIO);
	}
	*flags = sc->gpio_pins[pin].gp_flags;
	GPIO_UNLOCK(sc);

	return (0);
}

static int
tegra_gpio_pin_getname(device_t dev, uint32_t pin, char *name)
{
	struct tegra_gpio_softc *sc;

	sc = device_get_softc(dev);
	if (pin >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	memcpy(name, sc->gpio_pins[pin].gp_name, GPIOMAXNAME);
	GPIO_UNLOCK(sc);

	return (0);
}

static int
tegra_gpio_pin_setflags(device_t dev, uint32_t pin, uint32_t flags)
{
	struct tegra_gpio_softc *sc;
	int cnf;

	sc = device_get_softc(dev);
	if (pin >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	cnf = gpio_read(sc, GPIO_CNF,  &sc->gpio_pins[pin]);
	if (cnf == 0) {
		/* XXX - allow this for while ....
		GPIO_UNLOCK(sc);
		return (ENXIO);
		*/
		gpio_write_masked(sc, GPIO_MSK_CNF,  &sc->gpio_pins[pin], 1);
	}
	tegra_gpio_pin_configure(sc, &sc->gpio_pins[pin], flags);
	GPIO_UNLOCK(sc);

	return (0);
}

static int
tegra_gpio_pin_set(device_t dev, uint32_t pin, unsigned int value)
{
	struct tegra_gpio_softc *sc;

	sc = device_get_softc(dev);
	if (pin >= sc->gpio_npins)
		return (EINVAL);
	GPIO_LOCK(sc);
	gpio_write_masked(sc, GPIO_MSK_OUT, &sc->gpio_pins[pin], value);
	GPIO_UNLOCK(sc);

	return (0);
}

static int
tegra_gpio_pin_get(device_t dev, uint32_t pin, unsigned int *val)
{
	struct tegra_gpio_softc *sc;

	sc = device_get_softc(dev);
	if (pin >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	*val = gpio_read(sc, GPIO_IN, &sc->gpio_pins[pin]);
	GPIO_UNLOCK(sc);

	return (0);
}

static int
tegra_gpio_pin_toggle(device_t dev, uint32_t pin)
{
	struct tegra_gpio_softc *sc;

	sc = device_get_softc(dev);
	if (pin >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	gpio_write_masked(sc, GPIO_MSK_OE, &sc->gpio_pins[pin],
	     gpio_read(sc, GPIO_IN, &sc->gpio_pins[pin]) ^ 1);
	GPIO_UNLOCK(sc);

	return (0);
}

/* --------------------------------------------------------------------------
 *
 * Interrupts
 *
 */
static inline void
intr_write_masked(struct tegra_gpio_softc *sc, bus_addr_t reg,
    struct tegra_gpio_irqsrc *tgi, uint32_t val)
{
	uint32_t tmp;
	int bit;

	bit = GPIO_BIT(tgi->irq);
	tmp = 0x100 << bit;		/* mask */
	tmp |= (val & 1) << bit;	/* value */
	bus_write_4(sc->mem_res, reg + GPIO_REGNUM(tgi->irq), tmp);
}

static inline void
intr_write_modify(struct tegra_gpio_softc *sc, bus_addr_t reg,
    struct tegra_gpio_irqsrc *tgi, uint32_t val, uint32_t mask)
{
	uint32_t tmp;
	int bit;

	bit = GPIO_BIT(tgi->irq);
	GPIO_LOCK(sc);
	tmp = bus_read_4(sc->mem_res, reg + GPIO_REGNUM(tgi->irq));
	tmp &= ~(mask << bit);
	tmp |= val << bit;
	bus_write_4(sc->mem_res, reg + GPIO_REGNUM(tgi->irq), tmp);
	GPIO_UNLOCK(sc);
}

static inline void
tegra_gpio_isrc_mask(struct tegra_gpio_softc *sc,
     struct tegra_gpio_irqsrc *tgi, uint32_t val)
{

	intr_write_masked(sc, GPIO_MSK_INT_ENB, tgi, val);
}

static inline void
tegra_gpio_isrc_eoi(struct tegra_gpio_softc *sc,
     struct tegra_gpio_irqsrc *tgi)
{

	intr_write_masked(sc, GPIO_INT_CLR, tgi, 1);
}

static inline bool
tegra_gpio_isrc_is_level(struct tegra_gpio_irqsrc *tgi)
{

	return (tgi->cfgreg & GPIO_INT_LVL_EDGE);
}

static int
tegra_gpio_intr(void *arg)
{
	u_int irq, i, j, val, basepin;
	struct tegra_gpio_softc *sc;
	struct trapframe *tf;
	struct tegra_gpio_irqsrc *tgi;
	struct tegra_gpio_irq_cookie *cookie;

	cookie = (struct tegra_gpio_irq_cookie *)arg;
	sc = cookie->sc;
	tf = curthread->td_intr_frame;

	for (i = 0; i < GPIO_REGS_IN_BANK; i++) {
		basepin  = cookie->bank_num * GPIO_REGS_IN_BANK *
		    GPIO_PINS_IN_REG + i * GPIO_PINS_IN_REG;

		val = bus_read_4(sc->mem_res, GPIO_INT_STA +
		    GPIO_REGNUM(basepin));
		val &= bus_read_4(sc->mem_res, GPIO_INT_ENB +
		    GPIO_REGNUM(basepin));
		/* Interrupt handling */
		for (j = 0; j < GPIO_PINS_IN_REG; j++) {
			if ((val & (1 << j)) == 0)
				continue;
			irq = basepin + j;
			tgi = &sc->isrcs[irq];
			if (!tegra_gpio_isrc_is_level(tgi))
				tegra_gpio_isrc_eoi(sc, tgi);
			if (intr_isrc_dispatch(&tgi->isrc, tf) != 0) {
				tegra_gpio_isrc_mask(sc, tgi, 0);
				if (tegra_gpio_isrc_is_level(tgi))
					tegra_gpio_isrc_eoi(sc, tgi);
				device_printf(sc->dev,
				    "Stray irq %u disabled\n", irq);
			}

		}
	}

	return (FILTER_HANDLED);
}

static int
tegra_gpio_pic_attach(struct tegra_gpio_softc *sc)
{
	int error;
	uint32_t irq;
	const char *name;

	sc->isrcs = malloc(sizeof(*sc->isrcs) * sc->gpio_npins, M_DEVBUF,
	    M_WAITOK | M_ZERO);

	name = device_get_nameunit(sc->dev);
	for (irq = 0; irq < sc->gpio_npins; irq++) {
		sc->isrcs[irq].irq = irq;
		sc->isrcs[irq].cfgreg = 0;
		error = intr_isrc_register(&sc->isrcs[irq].isrc,
		    sc->dev, 0, "%s,%u", name, irq);
		if (error != 0)
			return (error); /* XXX deregister ISRCs */
	}
	if (intr_pic_register(sc->dev,
	    OF_xref_from_node(ofw_bus_get_node(sc->dev))) == NULL)
		return (ENXIO);

	return (0);
}

static int
tegra_gpio_pic_detach(struct tegra_gpio_softc *sc)
{

	/*
	 *  There has not been established any procedure yet
	 *  how to detach PIC from living system correctly.
	 */
	device_printf(sc->dev, "%s: not implemented yet\n", __func__);
	return (EBUSY);
}


static void
tegra_gpio_pic_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct tegra_gpio_softc *sc;
	struct tegra_gpio_irqsrc *tgi;

	sc = device_get_softc(dev);
	tgi = (struct tegra_gpio_irqsrc *)isrc;
	tegra_gpio_isrc_mask(sc, tgi, 0);
}

static void
tegra_gpio_pic_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct tegra_gpio_softc *sc;
	struct tegra_gpio_irqsrc *tgi;

	sc = device_get_softc(dev);
	tgi = (struct tegra_gpio_irqsrc *)isrc;
	tegra_gpio_isrc_mask(sc, tgi, 1);
}

static int
tegra_gpio_pic_map_fdt(struct tegra_gpio_softc *sc, u_int ncells,
    pcell_t *cells, u_int *irqp, uint32_t *regp)
{
	uint32_t reg;

	/*
	 * The first cell is the interrupt number.
	 * The second cell is used to specify flags:
	 *	bits[3:0] trigger type and level flags:
	 *		1 = low-to-high edge triggered.
	 *		2 = high-to-low edge triggered.
	 *		4 = active high level-sensitive.
	 *		8 = active low level-sensitive.
	 */
	if (ncells != 2 || cells[0] >= sc->gpio_npins)
		return (EINVAL);

	/*
	 * All interrupt types could be set for an interrupt at one moment.
	 * At least, the combination of 'low-to-high' and 'high-to-low' edge
	 * triggered interrupt types can make a sense.
	 */
	if (cells[1] == 1)
		reg = GPIO_INT_LVL_EDGE | GPIO_INT_LVL_HIGH;
	else if (cells[1] == 2)
		reg = GPIO_INT_LVL_EDGE;
	else if (cells[1] == 3)
		reg = GPIO_INT_LVL_EDGE | GPIO_INT_LVL_DELTA;
	else if (cells[1] == 4)
		reg = GPIO_INT_LVL_HIGH;
	else if (cells[1] == 8)
		reg = 0;
	else
		return (EINVAL);

	*irqp = cells[0];
	if (regp != NULL)
		*regp = reg;
	return (0);
}


static int
tegra_gpio_pic_map_gpio(struct tegra_gpio_softc *sc, u_int gpio_pin_num,
    u_int gpio_pin_flags, u_int intr_mode, u_int *irqp, uint32_t *regp)
{

	uint32_t reg;

	if (gpio_pin_num >= sc->gpio_npins)
		return (EINVAL);
	switch (intr_mode) {
	case GPIO_INTR_CONFORM:
	case GPIO_INTR_LEVEL_LOW:
		reg = 0;
		break;
	case GPIO_INTR_LEVEL_HIGH:
		reg = GPIO_INT_LVL_HIGH;
		break;
	case GPIO_INTR_EDGE_RISING:
		reg = GPIO_INT_LVL_EDGE | GPIO_INT_LVL_HIGH;
		break;
	case GPIO_INTR_EDGE_FALLING:
		reg = GPIO_INT_LVL_EDGE;
		break;
	case GPIO_INTR_EDGE_BOTH:
		reg = GPIO_INT_LVL_EDGE | GPIO_INT_LVL_DELTA;
		break;
	default:
		return (EINVAL);
	}
	*irqp = gpio_pin_num;
	if (regp != NULL)
		*regp = reg;
	return (0);
}

static int
tegra_gpio_pic_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	int rv;
	u_int irq;
	struct tegra_gpio_softc *sc;

	sc = device_get_softc(dev);

	if (data->type == INTR_MAP_DATA_FDT) {
		struct intr_map_data_fdt *daf;

		daf = (struct intr_map_data_fdt *)data;
		rv = tegra_gpio_pic_map_fdt(sc, daf->ncells, daf->cells, &irq,
		    NULL);
	} else if (data->type == INTR_MAP_DATA_GPIO) {
		struct intr_map_data_gpio *dag;

		dag = (struct intr_map_data_gpio *)data;
		rv = tegra_gpio_pic_map_gpio(sc, dag->gpio_pin_num,
		   dag->gpio_pin_flags, dag->gpio_intr_mode, &irq, NULL);
	} else
		return (ENOTSUP);

	if (rv == 0)
		*isrcp = &sc->isrcs[irq].isrc;
	return (rv);
}

static void
tegra_gpio_pic_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
	struct tegra_gpio_softc *sc;
	struct tegra_gpio_irqsrc *tgi;

	sc = device_get_softc(dev);
	tgi = (struct tegra_gpio_irqsrc *)isrc;
	if (tegra_gpio_isrc_is_level(tgi))
		tegra_gpio_isrc_eoi(sc, tgi);
}

static void
tegra_gpio_pic_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct tegra_gpio_softc *sc;
	struct tegra_gpio_irqsrc *tgi;

	sc = device_get_softc(dev);
	tgi = (struct tegra_gpio_irqsrc *)isrc;
	tegra_gpio_isrc_mask(sc, tgi, 1);
}

static void
tegra_gpio_pic_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct tegra_gpio_softc *sc;
	struct tegra_gpio_irqsrc *tgi;

	sc = device_get_softc(dev);
	tgi = (struct tegra_gpio_irqsrc *)isrc;

	tegra_gpio_isrc_mask(sc, tgi, 0);
	if (tegra_gpio_isrc_is_level(tgi))
		tegra_gpio_isrc_eoi(sc, tgi);
}

static int
tegra_gpio_pic_setup_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	u_int irq;
	uint32_t cfgreg;
	int rv;
	struct tegra_gpio_softc *sc;
	struct tegra_gpio_irqsrc *tgi;

	sc = device_get_softc(dev);
	tgi = (struct tegra_gpio_irqsrc *)isrc;

	if (data == NULL)
		return (ENOTSUP);

	/* Get and check config for an interrupt. */
	if (data->type == INTR_MAP_DATA_FDT) {
		struct intr_map_data_fdt *daf;

		daf = (struct intr_map_data_fdt *)data;
		rv = tegra_gpio_pic_map_fdt(sc, daf->ncells, daf->cells, &irq,
		    &cfgreg);
	} else if (data->type == INTR_MAP_DATA_GPIO) {
		struct intr_map_data_gpio *dag;

		dag = (struct intr_map_data_gpio *)data;
		rv = tegra_gpio_pic_map_gpio(sc, dag->gpio_pin_num,
		   dag->gpio_pin_flags, dag->gpio_intr_mode, &irq, &cfgreg);
	} else
		return (ENOTSUP);
	if (rv != 0)
		return (EINVAL);

	/*
	 * If this is a setup for another handler,
	 * only check that its configuration match.
	 */
	if (isrc->isrc_handlers != 0)
		return (tgi->cfgreg == cfgreg ? 0 : EINVAL);

	tgi->cfgreg = cfgreg;
	intr_write_modify(sc, GPIO_INT_LVL, tgi, cfgreg, GPIO_INT_LVL_MASK);
	tegra_gpio_pic_enable_intr(dev, isrc);

	return (0);
}

static int
tegra_gpio_pic_teardown_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct tegra_gpio_softc *sc;
	struct tegra_gpio_irqsrc *tgi;

	sc = device_get_softc(dev);
	tgi = (struct tegra_gpio_irqsrc *)isrc;

	if (isrc->isrc_handlers == 0)
		tegra_gpio_isrc_mask(sc, tgi, 0);
	return (0);
}

static int
tegra_gpio_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data != 0) {
		device_set_desc(dev, "Tegra GPIO Controller");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

/* --------------------------------------------------------------------------
 *
 * Bus
 *
 */
static int
tegra_gpio_detach(device_t dev)
{
	struct tegra_gpio_softc *sc;
	int i;

	sc = device_get_softc(dev);

	KASSERT(mtx_initialized(&sc->mtx), ("gpio mutex not initialized"));

	for (i = 0; i < GPIO_NUM_BANKS; i++) {
		if (sc->irq_ih[i] != NULL)
			bus_teardown_intr(dev, sc->irq_res[i], sc->irq_ih[i]);
	}

	if (sc->isrcs != NULL)
		tegra_gpio_pic_detach(sc);

	gpiobus_detach_bus(dev);

	for (i = 0; i < GPIO_NUM_BANKS; i++) {
		if (sc->irq_res[i] != NULL)
			bus_release_resource(dev, SYS_RES_IRQ, 0,
			    sc->irq_res[i]);
	}
	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);
	GPIO_LOCK_DESTROY(sc);

	return(0);
}

static int
tegra_gpio_attach(device_t dev)
{
	struct tegra_gpio_softc *sc;
	int i, rid;

	sc = device_get_softc(dev);
	sc->dev = dev;
	GPIO_LOCK_INIT(sc);

	/* Allocate bus_space resources. */
	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "Cannot allocate memory resources\n");
		tegra_gpio_detach(dev);
		return (ENXIO);
	}

	sc->gpio_npins = NGPIO;
	for (i = 0; i < sc->gpio_npins; i++) {
		sc->gpio_pins[i].gp_pin = i;
		sc->gpio_pins[i].gp_caps = GPIO_PIN_INPUT | GPIO_PIN_OUTPUT |
		    GPIO_INTR_LEVEL_LOW | GPIO_INTR_LEVEL_HIGH | 
		    GPIO_INTR_EDGE_RISING | GPIO_INTR_EDGE_FALLING |
		    GPIO_INTR_EDGE_BOTH;
		snprintf(sc->gpio_pins[i].gp_name, GPIOMAXNAME, "gpio_%s.%d",
		    tegra_gpio_port_names[ i / GPIO_PINS_IN_REG],
		    i % GPIO_PINS_IN_REG);
		sc->gpio_pins[i].gp_flags =
		    gpio_read(sc, GPIO_OE, &sc->gpio_pins[i]) != 0 ?
		    GPIO_PIN_OUTPUT : GPIO_PIN_INPUT;
	}

	/* Init interrupt related registes. */
	for (i = 0; i < sc->gpio_npins; i += GPIO_PINS_IN_REG) {
		bus_write_4(sc->mem_res, GPIO_INT_ENB + GPIO_REGNUM(i), 0);
		bus_write_4(sc->mem_res, GPIO_INT_STA + GPIO_REGNUM(i), 0xFF);
		bus_write_4(sc->mem_res, GPIO_INT_CLR + GPIO_REGNUM(i), 0xFF);
	}

	/* Allocate interrupts. */
	for (i = 0; i < GPIO_NUM_BANKS; i++) {
		sc->irq_cookies[i].sc = sc;
		sc->irq_cookies[i].bank_num = i;
		rid = i;
		sc->irq_res[i] = bus_alloc_resource_any(dev, SYS_RES_IRQ,
		    &rid, RF_ACTIVE);
		if (sc->irq_res[i] == NULL) {
			device_printf(dev, "Cannot allocate IRQ resources\n");
			tegra_gpio_detach(dev);
			return (ENXIO);
		}
		if ((bus_setup_intr(dev, sc->irq_res[i],
		    INTR_TYPE_MISC | INTR_MPSAFE, tegra_gpio_intr, NULL,
		    &sc->irq_cookies[i], &sc->irq_ih[i]))) {
			device_printf(dev,
			    "WARNING: unable to register interrupt handler\n");
			tegra_gpio_detach(dev);
			return (ENXIO);
		}
	}

	if (tegra_gpio_pic_attach(sc) != 0) {
		device_printf(dev, "WARNING: unable to attach PIC\n");
		tegra_gpio_detach(dev);
		return (ENXIO);
	}

	sc->busdev = gpiobus_attach_bus(dev);
	if (sc->busdev == NULL) {
		tegra_gpio_detach(dev);
		return (ENXIO);
	}

	return (bus_generic_attach(dev));
}

static int
tegra_map_gpios(device_t dev, phandle_t pdev, phandle_t gparent,
    int gcells, pcell_t *gpios, uint32_t *pin, uint32_t *flags)
{

	if (gcells != 2)
		return (ERANGE);
	*pin = gpios[0];
	*flags= gpios[1];
	return (0);
}

static phandle_t
tegra_gpio_get_node(device_t bus, device_t dev)
{

	/* We only have one child, the GPIO bus, which needs our own node. */
	return (ofw_bus_get_node(bus));
}

static device_method_t tegra_gpio_methods[] = {
	DEVMETHOD(device_probe,		tegra_gpio_probe),
	DEVMETHOD(device_attach,	tegra_gpio_attach),
	DEVMETHOD(device_detach,	tegra_gpio_detach),

	/* Interrupt controller interface */
	DEVMETHOD(pic_disable_intr,	tegra_gpio_pic_disable_intr),
	DEVMETHOD(pic_enable_intr,	tegra_gpio_pic_enable_intr),
	DEVMETHOD(pic_map_intr,		tegra_gpio_pic_map_intr),
	DEVMETHOD(pic_setup_intr,	tegra_gpio_pic_setup_intr),
	DEVMETHOD(pic_teardown_intr,	tegra_gpio_pic_teardown_intr),
	DEVMETHOD(pic_post_filter,	tegra_gpio_pic_post_filter),
	DEVMETHOD(pic_post_ithread,	tegra_gpio_pic_post_ithread),
	DEVMETHOD(pic_pre_ithread,	tegra_gpio_pic_pre_ithread),

	/* GPIO protocol */
	DEVMETHOD(gpio_get_bus,		tegra_gpio_get_bus),
	DEVMETHOD(gpio_pin_max,		tegra_gpio_pin_max),
	DEVMETHOD(gpio_pin_getname,	tegra_gpio_pin_getname),
	DEVMETHOD(gpio_pin_getflags,	tegra_gpio_pin_getflags),
	DEVMETHOD(gpio_pin_getcaps,	tegra_gpio_pin_getcaps),
	DEVMETHOD(gpio_pin_setflags,	tegra_gpio_pin_setflags),
	DEVMETHOD(gpio_pin_get,		tegra_gpio_pin_get),
	DEVMETHOD(gpio_pin_set,		tegra_gpio_pin_set),
	DEVMETHOD(gpio_pin_toggle,	tegra_gpio_pin_toggle),
	DEVMETHOD(gpio_map_gpios,	tegra_map_gpios),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_node,	tegra_gpio_get_node),

	DEVMETHOD_END
};

static devclass_t tegra_gpio_devclass;
static DEFINE_CLASS_0(gpio, tegra_gpio_driver, tegra_gpio_methods,
    sizeof(struct tegra_gpio_softc));
EARLY_DRIVER_MODULE(tegra_gpio, simplebus, tegra_gpio_driver,
    tegra_gpio_devclass, NULL, NULL, 70);
