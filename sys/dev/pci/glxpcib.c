/*      $OpenBSD: glxpcib.c,v 1.17 2024/08/19 00:01:40 deraadt Exp $	*/

/*
 * Copyright (c) 2007 Marc Balmer <mbalmer@openbsd.org>
 * Copyright (c) 2007 Michael Shalayeff
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * AMD CS5536 series LPC bridge also containing timer, watchdog, and GPIO.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/gpio.h>
#include <sys/timetc.h>
#include <sys/rwlock.h>

#include <machine/bus.h>
#ifdef __i386__
#include <machine/cpufunc.h>
#endif

#include <dev/gpio/gpiovar.h>
#include <dev/i2c/i2cvar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/glxreg.h>
#include <dev/pci/glxvar.h>

#include "gpio.h"

#define	AMD5536_REV		GLCP_CHIP_REV_ID
#define	AMD5536_REV_MASK	0xff
#define	AMD5536_TMC		PMC_LTMR

#define	MSR_LBAR_ENABLE		0x100000000ULL

/* Multi-Functional General Purpose Timer */
#define	MSR_LBAR_MFGPT		DIVIL_LBAR_MFGPT
#define	MSR_MFGPT_SIZE		0x40
#define	MSR_MFGPT_ADDR_MASK	0xffc0
#define	AMD5536_MFGPT0_CMP1	0x00000000
#define	AMD5536_MFGPT0_CMP2	0x00000002
#define	AMD5536_MFGPT0_CNT	0x00000004
#define	AMD5536_MFGPT0_SETUP	0x00000006
#define	AMD5536_MFGPT_DIV_MASK	0x000f	/* div = 1 << mask */
#define	AMD5536_MFGPT_CLKSEL	0x0010
#define	AMD5536_MFGPT_REV_EN	0x0020
#define	AMD5536_MFGPT_CMP1DIS	0x0000
#define	AMD5536_MFGPT_CMP1EQ	0x0040
#define	AMD5536_MFGPT_CMP1GE	0x0080
#define	AMD5536_MFGPT_CMP1EV	0x00c0
#define	AMD5536_MFGPT_CMP2DIS	0x0000
#define	AMD5536_MFGPT_CMP2EQ	0x0100
#define	AMD5536_MFGPT_CMP2GE	0x0200
#define	AMD5536_MFGPT_CMP2EV	0x0300
#define	AMD5536_MFGPT_STOP_EN	0x0800
#define	AMD5536_MFGPT_SET	0x1000
#define	AMD5536_MFGPT_CMP1	0x2000
#define	AMD5536_MFGPT_CMP2	0x4000
#define	AMD5536_MFGPT_CNT_EN	0x8000
#define	AMD5536_MFGPT_IRQ	MFGPT_IRQ
#define	AMD5536_MFGPT0_C1_IRQM	0x00000001
#define	AMD5536_MFGPT1_C1_IRQM	0x00000002
#define	AMD5536_MFGPT2_C1_IRQM	0x00000004
#define	AMD5536_MFGPT3_C1_IRQM	0x00000008
#define	AMD5536_MFGPT4_C1_IRQM	0x00000010
#define	AMD5536_MFGPT5_C1_IRQM	0x00000020
#define	AMD5536_MFGPT6_C1_IRQM	0x00000040
#define	AMD5536_MFGPT7_C1_IRQM	0x00000080
#define	AMD5536_MFGPT0_C2_IRQM	0x00000100
#define	AMD5536_MFGPT1_C2_IRQM	0x00000200
#define	AMD5536_MFGPT2_C2_IRQM	0x00000400
#define	AMD5536_MFGPT3_C2_IRQM	0x00000800
#define	AMD5536_MFGPT4_C2_IRQM	0x00001000
#define	AMD5536_MFGPT5_C2_IRQM	0x00002000
#define	AMD5536_MFGPT6_C2_IRQM	0x00004000
#define	AMD5536_MFGPT7_C2_IRQM	0x00008000
#define	AMD5536_MFGPT_NR	MFGPT_NR
#define	AMD5536_MFGPT0_C1_NMIM	0x00000001
#define	AMD5536_MFGPT1_C1_NMIM	0x00000002
#define	AMD5536_MFGPT2_C1_NMIM	0x00000004
#define	AMD5536_MFGPT3_C1_NMIM	0x00000008
#define	AMD5536_MFGPT4_C1_NMIM	0x00000010
#define	AMD5536_MFGPT5_C1_NMIM	0x00000020
#define	AMD5536_MFGPT6_C1_NMIM	0x00000040
#define	AMD5536_MFGPT7_C1_NMIM	0x00000080
#define	AMD5536_MFGPT0_C2_NMIM	0x00000100
#define	AMD5536_MFGPT1_C2_NMIM	0x00000200
#define	AMD5536_MFGPT2_C2_NMIM	0x00000400
#define	AMD5536_MFGPT3_C2_NMIM	0x00000800
#define	AMD5536_MFGPT4_C2_NMIM	0x00001000
#define	AMD5536_MFGPT5_C2_NMIM	0x00002000
#define	AMD5536_MFGPT6_C2_NMIM	0x00004000
#define	AMD5536_MFGPT7_C2_NMIM	0x00008000
#define	AMD5536_NMI_LEG		0x00010000
#define	AMD5536_MFGPT0_C2_RSTEN	0x01000000
#define	AMD5536_MFGPT1_C2_RSTEN	0x02000000
#define	AMD5536_MFGPT2_C2_RSTEN	0x04000000
#define	AMD5536_MFGPT3_C2_RSTEN	0x08000000
#define	AMD5536_MFGPT4_C2_RSTEN	0x10000000
#define	AMD5536_MFGPT5_C2_RSTEN	0x20000000
#define	AMD5536_MFGPT_SETUP	MFGPT_SETUP

/* GPIO */
#define	MSR_LBAR_GPIO		DIVIL_LBAR_GPIO
#define	MSR_GPIO_SIZE		0x100
#define	MSR_GPIO_ADDR_MASK	0xff00
#define	AMD5536_GPIO_NPINS	32
#define	AMD5536_GPIOH_OFFSET	0x80	/* high bank register offset */
#define	AMD5536_GPIO_OUT_VAL	0x00	/* output value */
#define	AMD5536_GPIO_OUT_EN	0x04	/* output enable */
#define	AMD5536_GPIO_OD_EN	0x08	/* open-drain enable */
#define AMD5536_GPIO_OUT_INVRT_EN 0x0c	/* invert output */
#define	AMD5536_GPIO_PU_EN	0x18	/* pull-up enable */
#define	AMD5536_GPIO_PD_EN	0x1c	/* pull-down enable */
#define	AMD5536_GPIO_IN_EN	0x20	/* input enable */
#define AMD5536_GPIO_IN_INVRT_EN 0x24	/* invert input */
#define	AMD5536_GPIO_READ_BACK	0x30	/* read back value */

/* SMB */
#define MSR_LBAR_SMB		DIVIL_LBAR_SMB
#define MSR_SMB_SIZE		0x08
#define MSR_SMB_ADDR_MASK	0xfff8
#define AMD5536_SMB_SDA		0x00 /* serial data */
#define AMD5536_SMB_STS		0x01 /* status */
#define AMD5536_SMB_STS_SLVSTOP	0x80 /* slave stop */
#define AMD5536_SMB_STS_SDAST	0x40 /* smb data status */
#define AMD5536_SMB_STS_BER	0x20 /* bus error */
#define AMD5536_SMB_STS_NEGACK	0x10 /* negative acknowledge */
#define AMD5536_SMB_STS_STASTR	0x08 /* stall after start */
#define AMD5536_SMB_STS_MASTER	0x02 /* master */
#define AMD5536_SMB_STS_XMIT	0x01 /* transmit or receive */
#define AMD5536_SMB_CST		0x02 /* control status */
#define AMD5536_SMB_CST_MATCH	0x04 /* address match */
#define AMD5536_SMB_CST_BB	0x02 /* bus busy */
#define AMD5536_SMB_CST_BUSY	0x01 /* busy */
#define AMD5536_SMB_CTL1	0x03 /* control 1 */
#define AMD5536_SMB_CTL1_STASTRE 0x80 /* stall after start enable */
#define AMD5536_SMB_CTL1_ACK	0x10 /* receive acknowledge */
#define AMD5536_SMB_CTL1_INTEN	0x04 /* interrupt enable  */
#define AMD5536_SMB_CTL1_STOP	0x02 /* stop */
#define AMD5536_SMB_CTL1_START	0x01 /* start */
#define AMD5536_SMB_ADDR	0x04 /* serial address */
#define AMD5536_SMB_ADDR_SAEN	0x80 /* slave enable */
#define AMD5536_SMB_CTL2	0x05 /* control 2 */
#define AMD5536_SMB_CTL2_EN	0x01 /* enable clock */
#define AMD5536_SMB_CTL2_FREQ	0x78 /* 100 kHz */
#define AMD5536_SMB_CTL3	0x06 /* control 3 */

/* PMS */
#define	MSR_LBAR_PMS		DIVIL_LBAR_PMS
#define	MSR_PMS_SIZE		0x80
#define	MSR_PMS_ADDR_MASK	0xff80
#define	AMD5536_PMS_SSC		0x54
#define	AMD5536_PMS_SSC_PI	0x00040000
#define	AMD5536_PMS_SSC_CLR_PI	0x00020000
#define	AMD5536_PMS_SSC_SET_PI	0x00010000

/*
 * MSR registers we want to preserve across suspend/resume
 */
const uint32_t glxpcib_msrlist[] = {
	GLIU_PAE,
	GLCP_GLD_MSR_PM,
	DIVIL_BALL_OPTS
};

struct glxpcib_softc {
	struct device		sc_dev;

	struct timecounter	sc_timecounter;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	uint64_t 		sc_msrsave[nitems(glxpcib_msrlist)];

#ifndef SMALL_KERNEL
#if NGPIO > 0
	/* GPIO interface */
	bus_space_tag_t		sc_gpio_iot;
	bus_space_handle_t	sc_gpio_ioh;
	struct gpio_chipset_tag	sc_gpio_gc;
	gpio_pin_t		sc_gpio_pins[AMD5536_GPIO_NPINS];
#endif
	/* I2C interface */
	bus_space_tag_t		sc_smb_iot;
	bus_space_handle_t	sc_smb_ioh;
	struct i2c_controller	sc_smb_ic;
	struct rwlock		sc_smb_lck;

	/* Watchdog */
	int			sc_wdog;
	int			sc_wdog_period;
#endif
};

struct cfdriver glxpcib_cd = {
	NULL, "glxpcib", DV_DULL
};

int	glxpcib_match(struct device *, void *, void *);
void	glxpcib_attach(struct device *, struct device *, void *);
int	glxpcib_activate(struct device *, int);
int	glxpcib_search(struct device *, void *, void *);
int	glxpcib_print(void *, const char *);

const struct cfattach glxpcib_ca = {
	sizeof(struct glxpcib_softc), glxpcib_match, glxpcib_attach,
	NULL, glxpcib_activate
};

/* from arch/<*>/pci/pcib.c */
void	pcibattach(struct device *parent, struct device *self, void *aux);

u_int	glxpcib_get_timecount(struct timecounter *tc);

#ifndef SMALL_KERNEL
int     glxpcib_wdogctl_cb(void *, int);
#if NGPIO > 0
void	glxpcib_gpio_pin_ctl(void *, int, int);
int	glxpcib_gpio_pin_read(void *, int);
void	glxpcib_gpio_pin_write(void *, int, int);
#endif
int	glxpcib_smb_acquire_bus(void *, int);
void	glxpcib_smb_release_bus(void *, int);
int	glxpcib_smb_send_start(void *, int);
int	glxpcib_smb_send_stop(void *, int);
void	glxpcib_smb_send_ack(void *, int);
int	glxpcib_smb_initiate_xfer(void *, i2c_addr_t, int);
int	glxpcib_smb_read_byte(void *, uint8_t *, int);
int	glxpcib_smb_write_byte(void *, uint8_t, int);
void	glxpcib_smb_reset(struct glxpcib_softc *);
int	glxpcib_smb_wait(struct glxpcib_softc *, int, int);
#endif

const struct pci_matchid glxpcib_devices[] = {
	{ PCI_VENDOR_AMD,	PCI_PRODUCT_AMD_CS5536_PCIB }
};

int
glxpcib_match(struct device *parent, void *match, void *aux)
{ 
	if (pci_matchbyid((struct pci_attach_args *)aux, glxpcib_devices,
	    nitems(glxpcib_devices))) {
		/* needs to win over pcib */
		return 2;
	}

	return 0;
}

void
glxpcib_attach(struct device *parent, struct device *self, void *aux)
{
	struct glxpcib_softc *sc = (struct glxpcib_softc *)self;
	struct timecounter *tc = &sc->sc_timecounter;
#ifndef SMALL_KERNEL
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	u_int64_t wa;
#if NGPIO > 0
	u_int64_t ga;
	struct gpiobus_attach_args gba;
	int i, gpio = 0;
#endif
	u_int64_t sa;
	struct i2cbus_attach_args iba;
	int i2c = 0;
	bus_space_handle_t tmpioh;
#endif
	tc->tc_get_timecount = glxpcib_get_timecount;
	tc->tc_counter_mask = 0xffffffff;
	tc->tc_frequency = 3579545;
	tc->tc_name = "CS5536";
	tc->tc_quality = 1000;
	tc->tc_priv = sc;
	tc_init(tc);

	printf(": rev %d, 32-bit %lluHz timer",
	    (int)rdmsr(AMD5536_REV) & AMD5536_REV_MASK,
	    tc->tc_frequency);

#ifndef SMALL_KERNEL
	/* Attach the watchdog timer */
	sc->sc_iot = pa->pa_iot;
	wa = rdmsr(MSR_LBAR_MFGPT);
	if (wa & MSR_LBAR_ENABLE &&
	    !bus_space_map(sc->sc_iot, wa & MSR_MFGPT_ADDR_MASK,
	    MSR_MFGPT_SIZE, 0, &sc->sc_ioh)) {
		/* count in seconds (as upper level desires) */
		bus_space_write_2(sc->sc_iot, sc->sc_ioh, AMD5536_MFGPT0_SETUP,
		    AMD5536_MFGPT_CNT_EN | AMD5536_MFGPT_CMP2EV |
		    AMD5536_MFGPT_CMP2 | AMD5536_MFGPT_DIV_MASK |
		    AMD5536_MFGPT_STOP_EN);
		wdog_register(glxpcib_wdogctl_cb, sc);
		sc->sc_wdog = 1;
		printf(", watchdog");
	}

#if NGPIO > 0
	/* map GPIO I/O space */
	sc->sc_gpio_iot = pa->pa_iot;
	ga = rdmsr(MSR_LBAR_GPIO);
	if (ga & MSR_LBAR_ENABLE &&
	    !bus_space_map(sc->sc_gpio_iot, ga & MSR_GPIO_ADDR_MASK,
	    MSR_GPIO_SIZE, 0, &sc->sc_gpio_ioh)) {
		printf(", gpio");

		/* initialize pin array */
		for (i = 0; i < AMD5536_GPIO_NPINS; i++) {
			sc->sc_gpio_pins[i].pin_num = i;
			sc->sc_gpio_pins[i].pin_caps = GPIO_PIN_INPUT |
			    GPIO_PIN_OUTPUT | GPIO_PIN_OPENDRAIN |
			    GPIO_PIN_PULLUP | GPIO_PIN_PULLDOWN |
			    GPIO_PIN_INVIN | GPIO_PIN_INVOUT;

			/* read initial state */
			sc->sc_gpio_pins[i].pin_state =
			    glxpcib_gpio_pin_read(sc, i);
		}

		/* create controller tag */
		sc->sc_gpio_gc.gp_cookie = sc;
		sc->sc_gpio_gc.gp_pin_read = glxpcib_gpio_pin_read;
		sc->sc_gpio_gc.gp_pin_write = glxpcib_gpio_pin_write;
		sc->sc_gpio_gc.gp_pin_ctl = glxpcib_gpio_pin_ctl;

		gba.gba_name = "gpio";
		gba.gba_gc = &sc->sc_gpio_gc;
		gba.gba_pins = sc->sc_gpio_pins;
		gba.gba_npins = AMD5536_GPIO_NPINS;
		gpio = 1;

	}
#endif /* NGPIO */

	/* Map SMB I/O space */
	sc->sc_smb_iot = pa->pa_iot;
	sa = rdmsr(MSR_LBAR_SMB);
	if (sa & MSR_LBAR_ENABLE &&
	    !bus_space_map(sc->sc_smb_iot, sa & MSR_SMB_ADDR_MASK,
	    MSR_SMB_SIZE, 0, &sc->sc_smb_ioh)) {
		printf(", i2c");

		/* Enable controller */
		bus_space_write_1(sc->sc_smb_iot, sc->sc_smb_ioh,
		    AMD5536_SMB_CTL2, AMD5536_SMB_CTL2_EN |
		    AMD5536_SMB_CTL2_FREQ);

		/* Disable interrupts */
		bus_space_write_1(sc->sc_smb_iot, sc->sc_smb_ioh,
		    AMD5536_SMB_CTL1, 0);

		/* Disable slave address */
		bus_space_write_1(sc->sc_smb_iot, sc->sc_smb_ioh,
		    AMD5536_SMB_ADDR, 0);

		/* Stall the bus after start */
		bus_space_write_1(sc->sc_smb_iot, sc->sc_smb_ioh,
		    AMD5536_SMB_CTL1, AMD5536_SMB_CTL1_STASTRE);

		/* Attach I2C framework */
		sc->sc_smb_ic.ic_cookie = sc;
		sc->sc_smb_ic.ic_acquire_bus = glxpcib_smb_acquire_bus;
		sc->sc_smb_ic.ic_release_bus = glxpcib_smb_release_bus;
		sc->sc_smb_ic.ic_send_start = glxpcib_smb_send_start;
		sc->sc_smb_ic.ic_send_stop = glxpcib_smb_send_stop;
		sc->sc_smb_ic.ic_initiate_xfer = glxpcib_smb_initiate_xfer;
		sc->sc_smb_ic.ic_read_byte = glxpcib_smb_read_byte;
		sc->sc_smb_ic.ic_write_byte = glxpcib_smb_write_byte;

		rw_init(&sc->sc_smb_lck, "iiclk");

		bzero(&iba, sizeof(iba));
		iba.iba_name = "iic";
		iba.iba_tag = &sc->sc_smb_ic;
		i2c = 1;
	}

	/* Map PMS I/O space and enable the ``Power Immediate'' feature */
	sa = rdmsr(MSR_LBAR_PMS);
	if (sa & MSR_LBAR_ENABLE &&
	    !bus_space_map(pa->pa_iot, sa & MSR_PMS_ADDR_MASK,
	    MSR_PMS_SIZE, 0, &tmpioh)) {
		bus_space_write_4(pa->pa_iot, tmpioh, AMD5536_PMS_SSC,
		    AMD5536_PMS_SSC_SET_PI);
		bus_space_barrier(pa->pa_iot, tmpioh, AMD5536_PMS_SSC, 4,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
		bus_space_unmap(pa->pa_iot, tmpioh, MSR_PMS_SIZE);
	}
#endif /* SMALL_KERNEL */
	pcibattach(parent, self, aux);

#ifndef SMALL_KERNEL
#if NGPIO > 0
	if (gpio)
		config_found(&sc->sc_dev, &gba, gpiobus_print);
#endif
	if (i2c)
		config_found(&sc->sc_dev, &iba, iicbus_print);

	config_search(glxpcib_search, self, pa);
#endif
}

int
glxpcib_activate(struct device *self, int act)
{
#ifndef SMALL_KERNEL
	struct glxpcib_softc *sc = (struct glxpcib_softc *)self;
	uint i;
#endif
	int rv = 0;

	switch (act) {
	case DVACT_SUSPEND:
		rv = config_activate_children(self, act);
#ifndef SMALL_KERNEL
		if (sc->sc_wdog) {
			sc->sc_wdog_period = bus_space_read_2(sc->sc_iot,
			    sc->sc_ioh, AMD5536_MFGPT0_CMP2);
			glxpcib_wdogctl_cb(sc, 0);
		}
#endif
#ifndef SMALL_KERNEL
		for (i = 0; i < nitems(glxpcib_msrlist); i++)
			sc->sc_msrsave[i] = rdmsr(glxpcib_msrlist[i]);
#endif

		break;
	case DVACT_RESUME:
#ifndef SMALL_KERNEL
		if (sc->sc_wdog)
			glxpcib_wdogctl_cb(sc, sc->sc_wdog_period);
		for (i = 0; i < nitems(glxpcib_msrlist); i++)
			wrmsr(glxpcib_msrlist[i], sc->sc_msrsave[i]);
#endif
		rv = config_activate_children(self, act);
		break;
	case DVACT_POWERDOWN:
		rv = config_activate_children(self, act);
#ifndef SMALL_KERNEL
		if (sc->sc_wdog)
			wdog_shutdown(self);
#endif
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}

u_int
glxpcib_get_timecount(struct timecounter *tc)
{
        return rdmsr(AMD5536_TMC);
}

#ifndef SMALL_KERNEL
int
glxpcib_wdogctl_cb(void *v, int period)
{
	struct glxpcib_softc *sc = v;

	if (period > 0xffff)
		period = 0xffff;

	bus_space_write_2(sc->sc_iot, sc->sc_ioh, AMD5536_MFGPT0_SETUP,
	    AMD5536_MFGPT_CNT_EN | AMD5536_MFGPT_CMP2);
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, AMD5536_MFGPT0_CNT, 0);
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, AMD5536_MFGPT0_CMP2, period);

	if (period)
		wrmsr(AMD5536_MFGPT_NR,
		    rdmsr(AMD5536_MFGPT_NR) | AMD5536_MFGPT0_C2_RSTEN);
	else
		wrmsr(AMD5536_MFGPT_NR,
		    rdmsr(AMD5536_MFGPT_NR) & ~AMD5536_MFGPT0_C2_RSTEN);

	return period;
}

#if NGPIO > 0
int
glxpcib_gpio_pin_read(void *arg, int pin)
{
	struct glxpcib_softc *sc = arg;
	u_int32_t data;
	int reg, off = 0;

	reg = AMD5536_GPIO_IN_EN;
	if (pin > 15) {
		pin &= 0x0f;
		off = AMD5536_GPIOH_OFFSET;
	}
	reg += off;
	data = bus_space_read_4(sc->sc_gpio_iot, sc->sc_gpio_ioh, reg);

	if (data & (1 << pin))
		reg = AMD5536_GPIO_READ_BACK + off;
	else
		reg = AMD5536_GPIO_OUT_VAL + off;

	data = bus_space_read_4(sc->sc_gpio_iot, sc->sc_gpio_ioh, reg);

	return data & 1 << pin ? GPIO_PIN_HIGH : GPIO_PIN_LOW;
}

void
glxpcib_gpio_pin_write(void *arg, int pin, int value)
{
	struct glxpcib_softc *sc = arg;
	u_int32_t data;
	int reg;

	reg = AMD5536_GPIO_OUT_VAL;
	if (pin > 15) {
		pin &= 0x0f;
		reg += AMD5536_GPIOH_OFFSET;
	}
	if (value == 1)
		data = 1 << pin;
	else
		data = 1 << (pin + 16);

	bus_space_write_4(sc->sc_gpio_iot, sc->sc_gpio_ioh, reg, data);
}

void
glxpcib_gpio_pin_ctl(void *arg, int pin, int flags)
{
	struct glxpcib_softc *sc = arg;
	int n, reg[7], val[7], nreg = 0, off = 0;

	if (pin > 15) {
		pin &= 0x0f;
		off = AMD5536_GPIOH_OFFSET;
	}

	reg[nreg] = AMD5536_GPIO_IN_EN + off;
	if (flags & GPIO_PIN_INPUT)
		val[nreg++] = 1 << pin;
	else
		val[nreg++] = 1 << (pin + 16);

	reg[nreg] = AMD5536_GPIO_OUT_EN + off;
	if (flags & GPIO_PIN_OUTPUT)
		val[nreg++] = 1 << pin;
	else
		val[nreg++] = 1 << (pin + 16);

	reg[nreg] = AMD5536_GPIO_OD_EN + off;
	if (flags & GPIO_PIN_OPENDRAIN)
		val[nreg++] = 1 << pin;
	else
		val[nreg++] = 1 << (pin + 16);

	reg[nreg] = AMD5536_GPIO_PU_EN + off;
	if (flags & GPIO_PIN_PULLUP)
		val[nreg++] = 1 << pin;
	else
		val[nreg++] = 1 << (pin + 16);

	reg[nreg] = AMD5536_GPIO_PD_EN + off;
	if (flags & GPIO_PIN_PULLDOWN)
		val[nreg++] = 1 << pin;
	else
		val[nreg++] = 1 << (pin + 16);

	reg[nreg] = AMD5536_GPIO_IN_INVRT_EN + off;
	if (flags & GPIO_PIN_INVIN)
		val[nreg++] = 1 << pin;
	else
		val[nreg++] = 1 << (pin + 16);

	reg[nreg] = AMD5536_GPIO_OUT_INVRT_EN + off;
	if (flags & GPIO_PIN_INVOUT)
		val[nreg++] = 1 << pin;
	else
		val[nreg++] = 1 << (pin + 16);

	/* set flags */
	for (n = 0; n < nreg; n++)
		bus_space_write_4(sc->sc_gpio_iot, sc->sc_gpio_ioh, reg[n],
		    val[n]);
}
#endif /* GPIO */

int
glxpcib_smb_acquire_bus(void *arg, int flags)
{
	struct glxpcib_softc *sc = arg;

	if (cold || flags & I2C_F_POLL)
		return (0);

	return (rw_enter(&sc->sc_smb_lck, RW_WRITE | RW_INTR));
}

void
glxpcib_smb_release_bus(void *arg, int flags)
{
	struct glxpcib_softc *sc = arg;

	if (cold || flags & I2C_F_POLL)
		return;

	rw_exit(&sc->sc_smb_lck);
}

int
glxpcib_smb_send_start(void *arg, int flags)
{
	struct glxpcib_softc *sc = arg;
	u_int8_t ctl;

	ctl = bus_space_read_1(sc->sc_smb_iot, sc->sc_smb_ioh,
	    AMD5536_SMB_CTL1);
	bus_space_write_1(sc->sc_smb_iot, sc->sc_smb_ioh, AMD5536_SMB_CTL1,
	    ctl | AMD5536_SMB_CTL1_START);

	return (0);
}

int
glxpcib_smb_send_stop(void *arg, int flags)
{
	struct glxpcib_softc *sc = arg;
	u_int8_t ctl;

	ctl = bus_space_read_1(sc->sc_smb_iot, sc->sc_smb_ioh,
	    AMD5536_SMB_CTL1);
	bus_space_write_1(sc->sc_smb_iot, sc->sc_smb_ioh, AMD5536_SMB_CTL1,
	    ctl | AMD5536_SMB_CTL1_STOP);

	return (0);
}

void
glxpcib_smb_send_ack(void *arg, int flags)
{
	struct glxpcib_softc *sc = arg;
	u_int8_t ctl;

	ctl = bus_space_read_1(sc->sc_smb_iot, sc->sc_smb_ioh,
	    AMD5536_SMB_CTL1);
	bus_space_write_1(sc->sc_smb_iot, sc->sc_smb_ioh, AMD5536_SMB_CTL1,
	    ctl | AMD5536_SMB_CTL1_ACK);
}

int
glxpcib_smb_initiate_xfer(void *arg, i2c_addr_t addr, int flags)
{
	struct glxpcib_softc *sc = arg;
	int error, dir;

	/* Issue start condition */
	glxpcib_smb_send_start(sc, flags);

	/* Wait for bus mastership */
	if ((error = glxpcib_smb_wait(sc, AMD5536_SMB_STS_MASTER |
	    AMD5536_SMB_STS_SDAST, flags)) != 0)
		return (error);

	/* Send address byte */
	dir = (flags & I2C_F_READ ? 1 : 0);
	bus_space_write_1(sc->sc_smb_iot, sc->sc_smb_ioh, AMD5536_SMB_SDA,
	    (addr << 1) | dir);

	return (0);
}

int
glxpcib_smb_read_byte(void *arg, uint8_t *bytep, int flags)
{
	struct glxpcib_softc *sc = arg;
	int error;

	/* Wait for the bus to be ready */
	if ((error = glxpcib_smb_wait(sc, AMD5536_SMB_STS_SDAST, flags)))
		return (error);

	/* Acknowledge the last byte */
	if (flags & I2C_F_LAST)
		glxpcib_smb_send_ack(sc, 0);

	/* Read data byte */
	*bytep = bus_space_read_1(sc->sc_smb_iot, sc->sc_smb_ioh,
	    AMD5536_SMB_SDA);

	return (0);
}

int
glxpcib_print(void *args, const char *parentname)
{
	struct glxpcib_attach_args *gaa = (struct glxpcib_attach_args *)args;

	if (parentname != NULL)
		printf("%s at %s", gaa->gaa_name, parentname);

	return UNCONF;
}

int
glxpcib_search(struct device *parent, void *gcf, void *args)
{
	struct glxpcib_softc *sc = (struct glxpcib_softc *)parent;
	struct cfdata *cf = (struct cfdata *)gcf;
	struct pci_attach_args *pa = (struct pci_attach_args *)args;
	struct glxpcib_attach_args gaa;

	gaa.gaa_name = cf->cf_driver->cd_name;
	gaa.gaa_pa = pa;
	gaa.gaa_iot = sc->sc_iot;
	gaa.gaa_ioh = sc->sc_ioh;

	/*
	 * These devices are attached directly, either from
	 * glxpcib_attach() or later in time from pcib_callback().
	 */
	if (strcmp(cf->cf_driver->cd_name, "gpio") == 0 ||
	    strcmp(cf->cf_driver->cd_name, "iic") == 0 ||
	    strcmp(cf->cf_driver->cd_name, "isa") == 0)
		return 0;

	if (cf->cf_attach->ca_match(parent, cf, &gaa) == 0)
		return 0;

	config_attach(parent, cf, &gaa, glxpcib_print);
	return 1;
}

int
glxpcib_smb_write_byte(void *arg, uint8_t byte, int flags)
{
	struct glxpcib_softc *sc = arg;
	int error;

	/* Wait for the bus to be ready */
	if ((error = glxpcib_smb_wait(sc, AMD5536_SMB_STS_SDAST, flags)))
		return (error);

	/* Send stop after the last byte */
	if (flags & I2C_F_STOP)
		glxpcib_smb_send_stop(sc, 0);

	/* Write data byte */
	bus_space_write_1(sc->sc_smb_iot, sc->sc_smb_ioh, AMD5536_SMB_SDA,
	    byte);

	return (0);
}

void
glxpcib_smb_reset(struct glxpcib_softc *sc)
{
	u_int8_t st;

	/* Clear MASTER, NEGACK and BER */
	st = bus_space_read_1(sc->sc_smb_iot, sc->sc_smb_ioh, AMD5536_SMB_STS);
	bus_space_write_1(sc->sc_smb_iot, sc->sc_smb_ioh, AMD5536_SMB_STS, st |
	    AMD5536_SMB_STS_MASTER | AMD5536_SMB_STS_NEGACK |
	    AMD5536_SMB_STS_BER);

	/* Disable and re-enable controller */
	bus_space_write_1(sc->sc_smb_iot, sc->sc_smb_ioh, AMD5536_SMB_CTL2, 0);
	bus_space_write_1(sc->sc_smb_iot, sc->sc_smb_ioh, AMD5536_SMB_CTL2,
	    AMD5536_SMB_CTL2_EN | AMD5536_SMB_CTL2_FREQ);

	/* Send stop */
	glxpcib_smb_send_stop(sc, 0);
}

int
glxpcib_smb_wait(struct glxpcib_softc *sc, int bits, int flags)
{
	u_int8_t st;
	int i;

	for (i = 0; i < 100; i++) {
		st = bus_space_read_1(sc->sc_smb_iot, sc->sc_smb_ioh,
		    AMD5536_SMB_STS);
		if (st & AMD5536_SMB_STS_BER) {
			printf("%s: bus error, bits=%#x st=%#x\n",
			    sc->sc_dev.dv_xname, bits, st);
			glxpcib_smb_reset(sc);
			return (EIO);
		}
		if ((bits & AMD5536_SMB_STS_MASTER) == 0 &&
		    (st & AMD5536_SMB_STS_NEGACK)) {
			glxpcib_smb_reset(sc);
			return (EIO);
		}
		if (st & AMD5536_SMB_STS_STASTR)
			bus_space_write_1(sc->sc_smb_iot, sc->sc_smb_ioh,
			    AMD5536_SMB_STS, AMD5536_SMB_STS_STASTR);
		if ((st & bits) == bits)
			break;
		delay(2);
	}
	if ((st & bits) != bits) {
		glxpcib_smb_reset(sc);
		return (ETIMEDOUT);
	}
	return (0);
}
#endif /* SMALL_KERNEL */
