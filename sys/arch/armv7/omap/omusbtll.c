/* $OpenBSD: omusbtll.c,v 1.6 2024/06/26 01:40:49 jsg Exp $ */
/*
 * Copyright (c) 2010 Dale Rahn <drahn@openbsd.org>
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
#include <armv7/armv7/armv7var.h>
#include <armv7/omap/prcmvar.h>

/* registers */
#define USBTLL_REVISION			0x0000
#define USBTLL_SYSCONFIG		0x0010
#define USBTLL_SYSSTATUS		0x0014
#define USBTLL_IRQSTATUS		0x0018
#define USBTLL_IRQENABLE		0x001C
#define USBTLL_SHARED_CONF			0x0030
#define  USBTLL_SHARED_CONF_USB_90D_DDR_EN	(1<<6)
#define  USBTLL_SHARED_CONF_USB_180D_SDR_EN	(1<<5)
#define  USBTLL_SHARED_CONF_USB_DIVRATIO_SH	2
#define  USBTLL_SHARED_CONF_FCLK_REQ		(1<<1)
#define  USBTLL_SHARED_CONF_FCLK_IS_ON		(1<<0)

#define USBTLL_CHANNEL_CONF_(i)		(0x0040 + (0x04 * (i)))
#define  USBTLL_CHANNEL_CONF_FSLSLINESTATE_SH       	28
#define  USBTLL_CHANNEL_CONF_FSLSMODE_SH   		24
#define  USBTLL_CHANNEL_CONF_TESTTXSE0   		(1<<20)
#define  USBTLL_CHANNEL_CONF_TESTTXDAT   		(1<<19)
#define  USBTLL_CHANNEL_CONF_TESTTXEN   		(1<<18)
#define  USBTLL_CHANNEL_CONF_TESTEN   			(1<<17)
#define  USBTLL_CHANNEL_CONF_DRVVBUS   			(1<<16)
#define  USBTLL_CHANNEL_CONF_CHRGVBUS   		(1<<15)
#define  USBTLL_CHANNEL_CONF_ULPINOBITSTUFF   		(1<<11)
#define  USBTLL_CHANNEL_CONF_ULPIAUTOIDLE   		(1<<10)
#define  USBTLL_CHANNEL_CONF_UTMIAUTOIDLE   		(1<<9)
#define  USBTLL_CHANNEL_CONF_ULPIDDRMODE     		(1<<8)
#define  USBTLL_CHANNEL_CONF_LPIOUTCLKMODE   		(1<<7)
#define  USBTLL_CHANNEL_CONF_TLLFULLSPEED   		(1<<6)
#define  USBTLL_CHANNEL_CONF_TLLCONNECT   		(1<<5)
#define  USBTLL_CHANNEL_CONF_TLLATTACH   		(1<<4)
#define  USBTLL_CHANNEL_CONF_UTMIISADEV       		(1<<3)
#define  USBTLL_CHANNEL_CONF_CHANMODE_SH    		1
#define  USBTLL_CHANNEL_CONF_CHANEN			(1<<0)

/*
ULPI_VENDOR_ID_LO_(i)		(0x0800 + (0x100 * (i)))
ULPI_VENDOR_ID_HI_(i)		(0x0801 + (0x100 * (i)))
ULPI_PRODUCT_ID_LO_(i)		(0x0802 + (0x100 * (i)))
ULPI_PRODUCT_ID_HI_(i)		(0x0803 + (0x100 * (i)))
ULPI_FUNCTION_CTRL_(i)		(0x0804 + (0x100 * (i)))
ULPI_FUNCTION_CTRL_SET_(i)	(0x0805 + (0x100 * (i)))
ULPI_FUNCTION_CTRL_CLR_(i)	(0x0806 + (0x100 * (i)))
ULPI_INTERFACE_CTRL_(i)		(0x0807 + (0x100 * (i)))
ULPI_INTERFACE_CTRL_SET_(i)	(0x0808 + (0x100 * (i)))
ULPI_INTERFACE_CTRL_CLR_(i)	(0x0809 + (0x100 * (i)))
ULPI_OTG_CTRL_(i)		(0x080A + (0x100 * (i)))
ULPI_OTG_CTRL_SET_(i)		(0x080B + (0x100 * (i)))
ULPI_OTG_CTRL_CLR_(i)		(0x080C + (0x100 * (i)))
ULPI_USB_INT_EN_RISE_(i)	(0x080D + (0x100 * (i)))
ULPI_USB_INT_EN_RISE_SET_(i)	(0x080E + (0x100 * (i)))
ULPI_USB_INT_EN_RISE_CLR_(i)	(0x080F + (0x100 * (i)))
ULPI_USB_INT_EN_FALL_(i)	(0x0810 + (0x100 * (i)))
ULPI_USB_INT_EN_FALL_SET_(i)	(0x0811 + (0x100 * (i)))
ULPI_USB_INT_EN_FALL_CLR_(i)	(0x0812 + (0x100 * (i)))
ULPI_USB_INT_STATUS_(i)		(0x0813 + (0x100 * (i)))
ULPI_USB_INT_LATCH_(i)		(0x0814 + (0x100 * (i)))
*/

struct omusbtll_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
};

void omusbtll_attach(struct device *parent, struct device *self, void *args);
void omusbtll_init(uint32_t channel_mask);

const struct cfattach	omusbtll_ca = {
	sizeof (struct omusbtll_softc), NULL, omusbtll_attach
};

struct cfdriver omusbtll_cd = {
	NULL, "omusbtll", DV_DULL
};

struct omusbtll_softc *omusbtll_sc;
void
omusbtll_attach(struct device *parent, struct device *self, void *args)
{
	struct omusbtll_softc *sc = (struct omusbtll_softc *) self;
	struct armv7_attach_args *aa = args;
	u_int32_t rev;

	sc->sc_iot = aa->aa_iot;
	if (bus_space_map(sc->sc_iot, aa->aa_dev->mem[0].addr,
	    aa->aa_dev->mem[0].size, 0, &sc->sc_ioh)) {
		printf("%s: bus_space_map failed!\n", __func__);
		return;
	}

#if 0
	prcm_enablemodule(PRCM_USBHOST1);
	prcm_enablemodule(PRCM_USBHOST2);
#endif
	prcm_enablemodule(PRCM_USBTLL);

	delay(10000);

	//return;
#if 1
	rev = bus_space_read_1(sc->sc_iot, sc->sc_ioh, USBTLL_SYSCONFIG);

	printf(" rev %d.%d\n", rev >> 4 & 0xf, rev & 0xf);
#endif

	omusbtll_sc = sc;

	omusbtll_init(0x3);
}

void
omusbtll_init(uint32_t channel_mask)
{
	int i;
	uint32_t val;
	/* global reacharound */
	struct omusbtll_softc *sc = omusbtll_sc;

	for(i = 0; i < 3; i++) {
		val = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    USBTLL_CHANNEL_CONF_(i));
		val &= ~(USBTLL_CHANNEL_CONF_ULPINOBITSTUFF |
		    USBTLL_CHANNEL_CONF_ULPIAUTOIDLE |
		    USBTLL_CHANNEL_CONF_ULPIDDRMODE);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    USBTLL_CHANNEL_CONF_(i), val);
	}

	val = bus_space_read_4(sc->sc_iot, sc->sc_ioh, USBTLL_SHARED_CONF);
	val |= (USBTLL_SHARED_CONF_USB_180D_SDR_EN |
	    (1 << USBTLL_SHARED_CONF_USB_DIVRATIO_SH) |
	    USBTLL_SHARED_CONF_FCLK_IS_ON);
	val &= ~(USBTLL_SHARED_CONF_USB_90D_DDR_EN);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, USBTLL_SHARED_CONF, val);

	for (i = 0; i < 3; i++) {
		if (channel_mask & (1<<i)) {
			val = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
			    USBTLL_CHANNEL_CONF_(i));

			val |= USBTLL_CHANNEL_CONF_CHANEN;
			bus_space_write_4(sc->sc_iot, sc->sc_ioh,
			    USBTLL_CHANNEL_CONF_(i), val);
		printf("usbtll enabling %d\n", i);
		}
	}
}
