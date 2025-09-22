/*	$OpenBSD: omehci.c,v 1.12 2025/07/16 07:15:42 jsg Exp $ */

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

/*-
 * Copyright (c) 2011
 *	Ben Gray <ben.r.gray@gmail.com>.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>

#include <armv7/omap/prcmvar.h>
#include <armv7/omap/omehcivar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

#include <dev/usb/ehcireg.h>
#include <dev/usb/ehcivar.h>

int	omehci_match(struct device *, void *, void *);
void	omehci_attach(struct device *, struct device *, void *);
int	omehci_detach(struct device *, int);
int	omehci_activate(struct device *, int);

struct omehci_softc {
	struct ehci_softc	 sc;
	void			*sc_ih;
	bus_space_handle_t	 uhh_ioh;
	bus_space_handle_t	 tll_ioh;

	uint32_t		 ehci_rev;
	uint32_t		 tll_avail;

	uint32_t		 port_mode[OMAP_HS_USB_PORTS];
};

int omehci_init(struct omehci_softc *);
void omehci_soft_phy_reset(struct omehci_softc *sc, unsigned int port);

const struct cfattach omehci_ca = {
	sizeof (struct omehci_softc), omehci_match, omehci_attach,
	omehci_detach, omehci_activate
};

struct cfdriver omehci_cd = {
	NULL, "omehci", DV_DULL
};

int
omehci_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "ti,usbhs-host");
}

void
omehci_attach(struct device *parent, struct device *self, void *aux)
{
	struct omehci_softc	*sc = (struct omehci_softc *)self;
	struct fdt_attach_args *faa = aux;
	usbd_status		 r;
	char			*devname = sc->sc.sc_bus.bdev.dv_xname;
	uint32_t		 i;
	char			 port_mode[16];
	char			 name[32];
	int			 node;
	uint32_t		 reg[2];

	if (faa->fa_nreg < 1)
		return;

	sc->sc.iot = faa->fa_iot;
	sc->sc.sc_bus.dmatag = faa->fa_dmat;

	/* set defaults */
	for (i = 0; i < OMAP_HS_USB_PORTS; i++)
		sc->port_mode[i] = EHCI_HCD_OMAP_MODE_UNKNOWN;

	strlcpy(name, "portX-mode", sizeof(name));

	for (i = 0; i < OMAP_HS_USB_PORTS; i++) {
		name[4] = '1' + i;
		memset(port_mode, 0, sizeof(port_mode));

		if (OF_getprop(faa->fa_node, name, port_mode,
		    sizeof(port_mode)) == -1)
			continue;

		if (strcmp(port_mode, "ehci-phy") == 0)
			sc->port_mode[i] = EHCI_HCD_OMAP_MODE_PHY;
		if (strcmp(port_mode, "ehci-hsic") == 0)
			sc->port_mode[i] = EHCI_HCD_OMAP_MODE_HSIC;
		if (strcmp(port_mode, "ehci-tll") == 0)
			sc->port_mode[i] = EHCI_HCD_OMAP_MODE_TLL ;
	}

	for (node = OF_child(faa->fa_node); node; node = OF_peer(node)) {
		if (OF_is_compatible(node, "ti,ehci-omap"))
			break;
	}

	if (node == 0)
		panic("could not find ehci child node");

	if (OF_getpropintarray(node, "reg", reg, sizeof(reg)) != sizeof(reg))
		return;

	/* Map I/O space */
	if (bus_space_map(sc->sc.iot, reg[0], reg[1], 0, &sc->sc.ioh)) {
		printf(": cannot map mem space\n");
		goto out;
	}
	sc->sc.sc_size = reg[1];

	if (bus_space_map(sc->sc.iot, faa->fa_reg[0].addr, faa->fa_reg[0].size,
	    0, &sc->uhh_ioh)) {
		printf(": cannot map mem space\n");
		goto mem0;
	}

#if 0
	if (sc->tll_avail &&
	    bus_space_map(sc->sc.iot, aa->aa_dev->mem[2].addr,
	    aa->aa_dev->mem[2].size, 0, &sc->tll_ioh)) {
		printf(": cannot map mem space\n");
		goto mem1;
	}
#endif

	printf("\n");

	phy_enable_idx(node, 0);

	if (omehci_init(sc))
		return;

	/* Disable interrupts, so we don't get any spurious ones. */
	sc->sc.sc_offs = EREAD1(&sc->sc, EHCI_CAPLENGTH);
	EOWRITE2(&sc->sc, EHCI_USBINTR, 0);

	sc->sc_ih = arm_intr_establish_fdt(node, IPL_USB,
	    ehci_intr, &sc->sc, devname);
	if (sc->sc_ih == NULL) {
		printf(": unable to establish interrupt\n");
		printf("XXX - disable ehci and prcm");
		goto mem2;
	}

	strlcpy(sc->sc.sc_vendor, "TI OMAP", sizeof(sc->sc.sc_vendor));
	r = ehci_init(&sc->sc);
	if (r != USBD_NORMAL_COMPLETION) {
		printf("%s: init failed, error=%d\n", devname, r);
		printf("XXX - disable ehci and prcm");
		goto intr;
	}

	config_found(self, &sc->sc.sc_bus, usbctlprint);

	goto out;

intr:
	arm_intr_disestablish(sc->sc_ih);
	sc->sc_ih = NULL;
mem2:
#if 0
	bus_space_unmap(sc->sc.iot, sc->tll_ioh, aa->aa_dev->mem[2].size);
mem1:
#endif
	bus_space_unmap(sc->sc.iot, sc->uhh_ioh, faa->fa_reg[0].size);
mem0:
	bus_space_unmap(sc->sc.iot, sc->sc.ioh, sc->sc.sc_size);
	sc->sc.sc_size = 0;
out:
	return;
}

int
omehci_init(struct omehci_softc *sc)
{
	uint32_t i = 0, reg;
	uint32_t reset_performed = 0;
	uint32_t timeout = 0;
	uint32_t tll_ch_mask = 0;

	/* enable high speed usb host clock */
	prcm_enablemodule(PRCM_USB);

	/* Hold the PHY in RESET for enough time till DIR is high */
	if (reset_performed)
		delay(10);

	/* Read the UHH revision */
	sc->ehci_rev = bus_space_read_4(sc->sc.iot, sc->uhh_ioh,
	    OMAP_USBHOST_UHH_REVISION);

	/* Initialise the low level interface module(s) */
	if (sc->ehci_rev == OMAP_EHCI_REV1) {
		/* Enable the USB TLL */
		prcm_enablemodule(PRCM_USBTLL);

		/* Perform TLL soft reset, and wait until reset is complete */
		bus_space_write_4(sc->sc.iot, sc->tll_ioh,
		    OMAP_USBTLL_SYSCONFIG, TLL_SYSCONFIG_SOFTRESET);

		/* Set the timeout to 100ms*/
		timeout = (hz < 10) ? 1 : ((100 * hz) / 1000);

		/* Wait for TLL reset to complete */
		while ((bus_space_read_4(sc->sc.iot, sc->tll_ioh,
		    OMAP_USBTLL_SYSSTATUS) & TLL_SYSSTATUS_RESETDONE)
		    == 0x00) {

			/* Sleep for a tick */
			delay(10);

			if (timeout-- == 0) {
				return 1;
			}
		}

		bus_space_write_4(sc->sc.iot, sc->tll_ioh,
		    OMAP_USBTLL_SYSCONFIG,
		    TLL_SYSCONFIG_ENAWAKEUP | TLL_SYSCONFIG_AUTOIDLE |
		    TLL_SYSCONFIG_SIDLE_SMART_IDLE | TLL_SYSCONFIG_CACTIVITY);
	} else if (sc->ehci_rev == OMAP_EHCI_REV2) {
		/* For OMAP44xx devices you have to enable the per-port clocks:
		 *  PHY_MODE  - External ULPI clock
		 *  TTL_MODE  - Internal UTMI clock
		 *  HSIC_MODE - Internal 480Mhz and 60Mhz clocks
		 */
		if (sc->port_mode[0] == EHCI_HCD_OMAP_MODE_PHY) {
			//ti_prcm_clk_set_source(USBP1_PHY_CLK, EXT_CLK);
			prcm_enablemodule(PRCM_USBP1_PHY);
		} else if (sc->port_mode[0] == EHCI_HCD_OMAP_MODE_TLL)
			prcm_enablemodule(PRCM_USBP1_UTMI);
		else if (sc->port_mode[0] == EHCI_HCD_OMAP_MODE_HSIC)
			prcm_enablemodule(PRCM_USBP1_HSIC);

		if (sc->port_mode[1] == EHCI_HCD_OMAP_MODE_PHY) {
			//ti_prcm_clk_set_source(USBP2_PHY_CLK, EXT_CLK);
			prcm_enablemodule(PRCM_USBP2_PHY);
		} else if (sc->port_mode[1] == EHCI_HCD_OMAP_MODE_TLL)
			prcm_enablemodule(PRCM_USBP2_UTMI);
		else if (sc->port_mode[1] == EHCI_HCD_OMAP_MODE_HSIC)
			prcm_enablemodule(PRCM_USBP2_HSIC);
	}

	/* Put UHH in SmartIdle/SmartStandby mode */
	reg = bus_space_read_4(sc->sc.iot, sc->uhh_ioh,
	    OMAP_USBHOST_UHH_SYSCONFIG);
	if (sc->ehci_rev == OMAP_EHCI_REV1) {
		reg &= ~(UHH_SYSCONFIG_SIDLEMODE_MASK |
		         UHH_SYSCONFIG_MIDLEMODE_MASK);
		reg |= (UHH_SYSCONFIG_ENAWAKEUP |
		        UHH_SYSCONFIG_AUTOIDLE |
		        UHH_SYSCONFIG_CLOCKACTIVITY |
		        UHH_SYSCONFIG_SIDLEMODE_SMARTIDLE |
		        UHH_SYSCONFIG_MIDLEMODE_SMARTSTANDBY);
	} else if (sc->ehci_rev == OMAP_EHCI_REV2) {
		reg &= ~UHH_SYSCONFIG_IDLEMODE_MASK;
		reg |=  UHH_SYSCONFIG_IDLEMODE_NOIDLE;
		reg &= ~UHH_SYSCONFIG_STANDBYMODE_MASK;
		reg |=  UHH_SYSCONFIG_STANDBYMODE_NOSTDBY;
	}
	bus_space_write_4(sc->sc.iot, sc->uhh_ioh, OMAP_USBHOST_UHH_SYSCONFIG,
	    reg);

	reg = bus_space_read_4(sc->sc.iot, sc->uhh_ioh,
	    OMAP_USBHOST_UHH_HOSTCONFIG);

	/* Setup ULPI bypass and burst configurations */
	reg |= (UHH_HOSTCONFIG_ENA_INCR4 |
		UHH_HOSTCONFIG_ENA_INCR8 |
		UHH_HOSTCONFIG_ENA_INCR16);
	reg &= ~UHH_HOSTCONFIG_ENA_INCR_ALIGN;

	if (sc->ehci_rev == OMAP_EHCI_REV1) {
		if (sc->port_mode[0] == EHCI_HCD_OMAP_MODE_UNKNOWN)
			reg &= ~UHH_HOSTCONFIG_P1_CONNECT_STATUS;
		if (sc->port_mode[1] == EHCI_HCD_OMAP_MODE_UNKNOWN)
			reg &= ~UHH_HOSTCONFIG_P2_CONNECT_STATUS;
		if (sc->port_mode[2] == EHCI_HCD_OMAP_MODE_UNKNOWN)
			reg &= ~UHH_HOSTCONFIG_P3_CONNECT_STATUS;

		/* Bypass the TLL module for PHY mode operation */
		if ((sc->port_mode[0] == EHCI_HCD_OMAP_MODE_PHY) ||
		    (sc->port_mode[1] == EHCI_HCD_OMAP_MODE_PHY) ||
		    (sc->port_mode[2] == EHCI_HCD_OMAP_MODE_PHY))
			reg &= ~UHH_HOSTCONFIG_P1_ULPI_BYPASS;
		else
			reg |= UHH_HOSTCONFIG_P1_ULPI_BYPASS;
	} else if (sc->ehci_rev == OMAP_EHCI_REV2) {
		reg |=  UHH_HOSTCONFIG_APP_START_CLK;
		
		/* Clear port mode fields for PHY mode*/
		reg &= ~UHH_HOSTCONFIG_P1_MODE_MASK;
		reg &= ~UHH_HOSTCONFIG_P2_MODE_MASK;

		if (sc->port_mode[0] == EHCI_HCD_OMAP_MODE_TLL)
			reg |= UHH_HOSTCONFIG_P1_MODE_UTMI_PHY;
		else if (sc->port_mode[0] == EHCI_HCD_OMAP_MODE_HSIC)
			reg |= UHH_HOSTCONFIG_P1_MODE_HSIC;

		if (sc->port_mode[1] == EHCI_HCD_OMAP_MODE_TLL)
			reg |= UHH_HOSTCONFIG_P2_MODE_UTMI_PHY;
		else if (sc->port_mode[1] == EHCI_HCD_OMAP_MODE_HSIC)
			reg |= UHH_HOSTCONFIG_P2_MODE_HSIC;
	}

	bus_space_write_4(sc->sc.iot, sc->uhh_ioh, OMAP_USBHOST_UHH_HOSTCONFIG, reg);

	/* If any of the ports are configured in TLL mode, enable them */
	for (i = 0; i < OMAP_HS_USB_PORTS; i++)
		if (sc->port_mode[i] == EHCI_HCD_OMAP_MODE_PHY)
			tll_ch_mask |= 1 << i;

	/* Enable UTMI mode for required TLL channels */
#ifdef notyet
	if (tll_ch_mask)
		omap_ehci_utmi_init(sc, tll_ch_mask);
#endif

	/* Set the interrupt threshold control, it controls the maximum rate at
	 * which the host controller issues interrupts.  We set it to 1 microframe
	 * at startup - the default is 8 microframes (equates to 1ms).
	 */
	reg = bus_space_read_4(sc->sc.iot, sc->sc.ioh, OMAP_USBHOST_USBCMD);
	reg &= 0xff00ffff;
	reg |= (1 << 16);
	bus_space_write_4(sc->sc.iot, sc->sc.ioh, OMAP_USBHOST_USBCMD, reg);

	/* Soft reset the PHY using PHY reset command over ULPI */
	for (i = 0; i < OMAP_HS_USB_PORTS; i++)
		if (sc->port_mode[i] == EHCI_HCD_OMAP_MODE_PHY)
			omehci_soft_phy_reset(sc, i);

	return(0);
}

void
omehci_soft_phy_reset(struct omehci_softc *sc, unsigned int port)
{
	unsigned long timeout = (hz < 10) ? 1 : ((100 * hz) / 1000);
	uint32_t reg;

	reg = ULPI_FUNC_CTRL_RESET
		/* FUNCTION_CTRL_SET register */
		| (ULPI_SET(ULPI_FUNC_CTRL) << OMAP_USBHOST_INSNREG05_ULPI_REGADD_SHIFT)
		/* Write */
		| (2 << OMAP_USBHOST_INSNREG05_ULPI_OPSEL_SHIFT)
		/* PORTn */
		| ((port + 1) << OMAP_USBHOST_INSNREG05_ULPI_PORTSEL_SHIFT)
		/* start ULPI access*/
		| (1 << OMAP_USBHOST_INSNREG05_ULPI_CONTROL_SHIFT);

	bus_space_write_4(sc->sc.iot, sc->sc.ioh, OMAP_USBHOST_INSNREG05_ULPI, reg);

	timeout += 1000000;
	/* Wait for ULPI access completion */
	while ((bus_space_read_4(sc->sc.iot, sc->sc.ioh, OMAP_USBHOST_INSNREG05_ULPI)
			& (1 << OMAP_USBHOST_INSNREG05_ULPI_CONTROL_SHIFT))) {

		/* Sleep for a tick */
		delay(10);

		if (timeout-- == 0) {
			printf("PHY reset operation timed out\n");
			break;
		}
	}
}

int
omehci_detach(struct device *self, int flags)
{
	struct omehci_softc		*sc = (struct omehci_softc *)self;
	int				rv;

	rv = ehci_detach(self, flags);
	if (rv)
		return (rv);

	if (sc->sc_ih != NULL) {
		arm_intr_disestablish(sc->sc_ih);
		sc->sc_ih = NULL;
	}

	if (sc->sc.sc_size) {
		bus_space_unmap(sc->sc.iot, sc->sc.ioh, sc->sc.sc_size);
		sc->sc.sc_size = 0;
	}

	/* XXX: stop clock */

	return (0);
}

int
omehci_activate(struct device *self, int act)
{
	struct omehci_softc *sc = (struct omehci_softc *)self;
	int rv;

	switch (act) {
	case DVACT_SUSPEND:
		rv = config_activate_children(self, act);
		sc->sc.sc_bus.use_polling++;
		/* FIXME */
		sc->sc.sc_bus.use_polling--;
		break;
	case DVACT_RESUME:
		sc->sc.sc_bus.use_polling++;
		/* FIXME */
		sc->sc.sc_bus.use_polling--;
		rv = config_activate_children(self, act);
		break;
	case DVACT_POWERDOWN:
		rv = config_activate_children(self, act);
		ehci_reset(&sc->sc);
		break;
	}
	return rv;
}
