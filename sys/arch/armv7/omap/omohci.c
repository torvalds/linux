/*	$OpenBSD: omohci.c,v 1.5 2024/08/20 16:24:50 deraadt Exp $ */

/*
 * Copyright (c) 2005 David Gwynne <dlg@openbsd.org>
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
#include <sys/timeout.h>

#include <machine/intr.h>
#include <machine/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>

#include <dev/usb/ohcireg.h>
#include <dev/usb/ohcivar.h>
#include <armv7/omap/prcmvar.h>

#define HOSTUEADDR		0x0E0
#define HOSTUESTATUS		0x0E4
#define HOSTTIMEOUTCTRL		0x0E8
#define HOSTREVISION		0x0EC
#define WHM_REVID_REGISTER	0x0F4
#define WHM_TEST_OBSV		0x0F8
#define WHM_TEST_CTL		0x0FC
#define HHC_TEST_CFG		0x100
#define HHC_TEST_CTL		0x104
#define HHC_TEST_OBSV		0x108
#define REVDEV			0x200 /* 16 bit */
#define EP_NUM			0x204 /* 16 bit */
#define DATA			0x208 /* 16 bit */
#define CTRL			0x20C /* 16 bit */
#define STAT_FLG		0x210 /* 16 bit */
#define RXFSTAT			0x214 /* 16 bit */
#define SYSCON1			0x218 /* 16 bit */
#define SYSCON2			0x21C /* 16 bit */
#define DEVSTAT			0x220 /* 16 bit */
#define SOFREG			0x224 /* 16 bit */
#define IRQ_EN			0x228 /* 16 bit */
#define DMA_IRQ_EN		0x22C /* 16 bit */
#define IRQ_SRC			0x230 /* 16 bit */
#define EPN_STAT		0x234 /* 16 bit */
#define DMAN_STAT		0x238 /* 16 bit */
#define RXDMA_CFG		0x240 /* 16 bit */
#define TXDMA_CFG		0x244 /* 16 bit */

#define TXDMA0			0x250
#define TXDMA1		0x254
#define TXDMA2		0x258
#define RXDMA0		0x260
#define RXDMA1		0x264
#define RXDMA2		0x268

#define EP0	 	0x280
#define EP_RX(x)	0x280 + (x * 4)
#define EP_TX(x)	0x2C0 + (x * 4)

#define OTG_REV		0x300
#define OTG_SYSCON_1	0x304
#define OTG_SYSCON_2	0x308
#define		OTG_SYSCON2_OTG_EN		0x80000000
#define		OTG_SYSCON2_UHOST_EN		0x00000100
#define		OTG_SYSCON2_MODE_DISABLED	0x00000000
#define		OTG_SYSCON2_MODE_CLIENT		0x00000001
#define		OTG_SYSCON2_MODE_HOST		0x00000004
#define OTG_CTRL	0x30C
#if 0
#define OTG_IRQ_EN	0x310 /* 16 bit */
#define OTG_IRQ_SRC	0x314 /* 16 bit */
#define OTG_OUTCTRL	0x318 /* 16 bit */
#define OTG_TEST	0x320 /* 16 bit */
#endif
#define OTG_VC		0x3FC


int	omohci_match(struct device *, void *, void *);
void	omohci_attach(struct device *, struct device *, void *);
int	omohci_detach(struct device *, int);
int	omohci_activate(struct device *, int);

struct omohci_softc {
	struct ohci_softc	sc;
	void			*sc_ihc0;
	void			*sc_ihc1;
	void			*sc_ihc2;
	void			*sc_ih0;
	void			*sc_ih1;
	void			*sc_ihotg;
};

void	omohci_enable(struct omohci_softc *);
void	omohci_disable(struct omohci_softc *);

const struct cfattach omohci_ca = {
        sizeof (struct omohci_softc), omohci_match, omohci_attach,
	omohci_detach, omohci_detach
};

int
omohci_match(struct device *parent, void *match, void *aux)
{
#if 0
	if ((cputype & ~CPU_ID_XSCALE_COREREV_MASK) != CPU_ID_PXA27X)
		return (0);
#endif

	return (1);
}

void
omohci_attach(struct device *parent, struct device *self, void *aux)
{
	struct omohci_softc	*sc = (struct omohci_softc *)self;
        struct ahb_attach_args	*aa = aux;
	usbd_status		r;

	sc->sc.iot = aa->aa_iot;
	sc->sc.sc_bus.dmatag = aa->aa_dmat;
	sc->sc_ih0 = NULL;
	sc->sc_ih1 = NULL;
	sc->sc_ihc0 = NULL;
	sc->sc_ihc1 = NULL;
	sc->sc_ihc2 = NULL;
	sc->sc_ihotg = NULL;
	sc->sc.sc_size = 0;

	/* Map I/O space */
	if (bus_space_map(sc->sc.iot, aa->aa_addr, aa->aa_size, 0,
	    &sc->sc.ioh)) {
		printf(": cannot map mem space\n");
		return;
	}
	sc->sc.sc_size = aa->aa_size;

	/* XXX copied from ohci_pci.c. needed? */
	bus_space_barrier(sc->sc.iot, sc->sc.ioh, 0, sc->sc.sc_size,
	    BUS_SPACE_BARRIER_READ|BUS_SPACE_BARRIER_WRITE);

#if 0
	/* start the usb clock */
	pxa2x0_clkman_config(CKEN_USBHC, 1);
#endif
	omohci_enable(sc);

	/* Disable interrupts, so we don't get any spurious ones. */
	bus_space_write_4(sc->sc.iot, sc->sc.ioh, OHCI_INTERRUPT_DISABLE,
	    OHCI_MIE);

	sc->sc_ihc0 = arm_intr_establish(aa->aa_intr, IPL_USB,
	    ohci_intr, &sc->sc, sc->sc.sc_bus.bdev.dv_xname);
	sc->sc_ihc1 = arm_intr_establish(aa->aa_intr+1, IPL_USB,
	    ohci_intr, &sc->sc, sc->sc.sc_bus.bdev.dv_xname);
	sc->sc_ihc2 = arm_intr_establish(aa->aa_intr+2, IPL_USB,
	    ohci_intr, &sc->sc, sc->sc.sc_bus.bdev.dv_xname);
	sc->sc_ih0 = arm_intr_establish(aa->aa_intr+3, IPL_USB,
	    ohci_intr, &sc->sc, sc->sc.sc_bus.bdev.dv_xname);
	sc->sc_ih1 = arm_intr_establish(aa->aa_intr+4, IPL_USB,
	    ohci_intr, &sc->sc, sc->sc.sc_bus.bdev.dv_xname);
	sc->sc_ihotg = arm_intr_establish(aa->aa_intr+5, IPL_USB,
	    ohci_intr, &sc->sc, sc->sc.sc_bus.bdev.dv_xname);
	if (sc->sc_ih0 == NULL ||
	    sc->sc_ih1 == NULL ||
	    sc->sc_ihc0 == NULL ||
	    sc->sc_ihc1 == NULL ||
	    sc->sc_ihc2 == NULL ||
	    sc->sc_ihotg == NULL) {
		printf(": unable to establish interrupt\n");
		omohci_disable(sc);
#if 0
		pxa2x0_clkman_config(CKEN_USBHC, 0);
#endif
		bus_space_unmap(sc->sc.iot, sc->sc.ioh, sc->sc.sc_size);
		sc->sc.sc_size = 0;
		return;
	}

	prcm_enablemodule(PRCM_USB);

	bus_space_write_4(sc->sc.iot, sc->sc.ioh, OTG_SYSCON_2,
	    OTG_SYSCON2_UHOST_EN | OTG_SYSCON2_MODE_HOST);

	strlcpy(sc->sc.sc_vendor, "OMAP24xx", sizeof(sc->sc.sc_vendor));
	r = ohci_init(&sc->sc);
	if (r != USBD_NORMAL_COMPLETION) {
		printf("%s: init failed, error=%d\n",
		    sc->sc.sc_bus.bdev.dv_xname, r);
		arm_intr_disestablish(sc->sc_ih0);
		arm_intr_disestablish(sc->sc_ih1);
		arm_intr_disestablish(sc->sc_ihc0);
		arm_intr_disestablish(sc->sc_ihc1);
		arm_intr_disestablish(sc->sc_ihc2);
		arm_intr_disestablish(sc->sc_ihotg);
		sc->sc_ih0 = NULL;
		sc->sc_ih1 = NULL;
		sc->sc_ihc0 = NULL;
		sc->sc_ihc1 = NULL;
		sc->sc_ihc2 = NULL;
		sc->sc_ihotg = NULL;
		omohci_disable(sc);
#if 0
		pxa2x0_clkman_config(CKEN_USBHC, 0);
#endif
		bus_space_unmap(sc->sc.iot, sc->sc.ioh, sc->sc.sc_size);
		sc->sc.sc_size = 0;
		return;
	}

	config_found(self, &sc->sc.sc_bus, usbctlprint);
}

int
omohci_detach(struct device *self, int flags)
{
	struct omohci_softc		*sc = (struct omohci_softc *)self;
	int				rv;

	rv = ohci_detach(self, flags);
	if (rv)
		return (rv);

	if (sc->sc_ih0 != NULL) {
		arm_intr_disestablish(sc->sc_ih0);
		arm_intr_disestablish(sc->sc_ih1);
		arm_intr_disestablish(sc->sc_ihc0);
		arm_intr_disestablish(sc->sc_ihc1);
		arm_intr_disestablish(sc->sc_ihc2);
		arm_intr_disestablish(sc->sc_ihotg);
		sc->sc_ih0 = NULL;
		sc->sc_ih1 = NULL;
		sc->sc_ihc0 = NULL;
		sc->sc_ihc1 = NULL;
		sc->sc_ihc2 = NULL;
		sc->sc_ihotg = NULL;
	}

	omohci_disable(sc);

	/* stop clock */
#if 0
	pxa2x0_clkman_config(CKEN_USBHC, 0);
#endif

	if (sc->sc.sc_size) {
		bus_space_unmap(sc->sc.iot, sc->sc.ioh, sc->sc.sc_size);
		sc->sc.sc_size = 0;
	}

	return (0);
}


int
omohci_activate(struct device *self, int act)
{
	struct omohci_softc *sc = (struct omohci_softc *)self;
	int rv;

	switch (act) {
	case DVACT_SUSPEND:
		sc->sc.sc_bus.use_polling++;
		ohci_power(why, &sc->sc);
#if 0
		pxa2x0_clkman_config(CKEN_USBHC, 0);
#endif
		sc->sc.sc_bus.use_polling--;
		rv = config_activate_children(self, act);
		break;

	case DVACT_RESUME:
		rv = config_activate_children(self, act);
		sc->sc.sc_bus.use_polling++;
#if 0
		pxa2x0_clkman_config(CKEN_USBHC, 1);
#endif
		omohci_enable(sc);
		ohci_power(why, &sc->sc);
		sc->sc.sc_bus.use_polling--;
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return rv;
}

void
omohci_enable(struct omohci_softc *sc)
{
#if 0
	u_int32_t			hr;

	/* Full host reset */
	hr = bus_space_read_4(sc->sc.iot, sc->sc.ioh, USBHC_HR);
	bus_space_write_4(sc->sc.iot, sc->sc.ioh, USBHC_HR,
	    (hr & USBHC_HR_MASK) | USBHC_HR_FHR);

	DELAY(USBHC_RST_WAIT);

	hr = bus_space_read_4(sc->sc.iot, sc->sc.ioh, USBHC_HR);
	bus_space_write_4(sc->sc.iot, sc->sc.ioh, USBHC_HR,
	    (hr & USBHC_HR_MASK) & ~(USBHC_HR_FHR));

	/* Force system bus interface reset */
	hr = bus_space_read_4(sc->sc.iot, sc->sc.ioh, USBHC_HR);
	bus_space_write_4(sc->sc.iot, sc->sc.ioh, USBHC_HR,
	    (hr & USBHC_HR_MASK) | USBHC_HR_FSBIR);

	while (bus_space_read_4(sc->sc.iot, sc->sc.ioh, USBHC_HR) & \
	    USBHC_HR_FSBIR)
		DELAY(3);

	/* Enable the ports (physically only one, only enable that one?) */
	hr = bus_space_read_4(sc->sc.iot, sc->sc.ioh, USBHC_HR);
	bus_space_write_4(sc->sc.iot, sc->sc.ioh, USBHC_HR,
	    (hr & USBHC_HR_MASK) & ~(USBHC_HR_SSE));
	hr = bus_space_read_4(sc->sc.iot, sc->sc.ioh, USBHC_HR);
	bus_space_write_4(sc->sc.iot, sc->sc.ioh, USBHC_HR,
	    (hr & USBHC_HR_MASK) & ~(USBHC_HR_SSEP2));
#endif
}

void
omohci_disable(struct omohci_softc *sc)
{
#if 0
	u_int32_t			hr;

	/* Full host reset */
	hr = bus_space_read_4(sc->sc.iot, sc->sc.ioh, USBHC_HR);
	bus_space_write_4(sc->sc.iot, sc->sc.ioh, USBHC_HR,
	    (hr & USBHC_HR_MASK) | USBHC_HR_FHR);

	DELAY(USBHC_RST_WAIT);

	hr = bus_space_read_4(sc->sc.iot, sc->sc.ioh, USBHC_HR);
	bus_space_write_4(sc->sc.iot, sc->sc.ioh, USBHC_HR,
	    (hr & USBHC_HR_MASK) & ~(USBHC_HR_FHR));
#endif
}
