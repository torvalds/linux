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
 * USB phy driver for Tegra SoCs.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>
#include <dev/extres/phy/phy.h>
#include <dev/extres/regulator/regulator.h>
#include <dev/fdt/fdt_pinctrl.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "phynode_if.h"

#define	CTRL_ICUSB_CTRL			0x15c
#define	  ICUSB_CTR_IC_ENB1			(1 << 3)

#define	CTRL_USB_USBMODE		0x1f8
#define	  USB_USBMODE_MASK			(3 << 0)
#define	  USB_USBMODE_HOST			(3 << 0)
#define	  USB_USBMODE_DEVICE			(2 << 0)

#define	CTRL_USB_HOSTPC1_DEVLC		0x1b4
#define	 USB_HOSTPC1_DEVLC_PTS(x)		(((x) & 0x7) << 29)
#define	 USB_HOSTPC1_DEVLC_STS			(1 << 28)
#define	 USB_HOSTPC1_DEVLC_PHCD			(1 << 22)


#define	IF_USB_SUSP_CTRL		0x400
#define	 FAST_WAKEUP_RESP			(1 << 26)
#define	 UTMIP_SUSPL1_SET			(1 << 25)
#define	 USB_WAKEUP_DEBOUNCE_COUNT(x)		(((x) & 0x7) << 16)
#define	 USB_SUSP_SET				(1 << 14)
#define	 UTMIP_PHY_ENB				(1 << 12)
#define	 UTMIP_RESET				(1 << 11)
#define	 USB_SUSP_POL				(1 << 10)
#define	 USB_PHY_CLK_VALID_INT_ENB		(1 << 9)
#define	 USB_PHY_CLK_VALID_INT_STS		(1 << 8)
#define	 USB_PHY_CLK_VALID			(1 << 7)
#define	 USB_CLKEN				(1 << 6)
#define	 USB_SUSP_CLR				(1 << 5)
#define	 USB_WAKE_ON_DISCON_EN_DEV		(1 << 4)
#define	 USB_WAKE_ON_CNNT_EN_DEV		(1 << 3)
#define	 USB_WAKE_ON_RESUME_EN			(1 << 2)
#define	 USB_WAKEUP_INT_ENB			(1 << 1)
#define	 USB_WAKEUP_INT_STS			(1 << 0)

#define	IF_USB_PHY_VBUS_SENSORS		0x404
#define	 B_SESS_END_SW_VALUE			(1 << 4)
#define	 B_SESS_END_SW_EN			(1 << 3)


#define	UTMIP_XCVR_CFG0			0x808
#define	 UTMIP_XCVR_HSSLEW_MSB(x)		((((x) & 0x1fc) >> 2) << 25)
#define	 UTMIP_XCVR_SETUP_MSB(x)		((((x) & 0x70) >> 4) << 22)
#define	 UTMIP_XCVR_LSBIAS_SEL			(1 << 21)
#define	 UTMIP_XCVR_DISCON_METHOD		(1 << 20)
#define	 UTMIP_FORCE_PDZI_POWERUP		(1 << 19)
#define	 UTMIP_FORCE_PDZI_POWERDOWN		(1 << 18)
#define	 UTMIP_FORCE_PD2_POWERUP		(1 << 17)
#define	 UTMIP_FORCE_PD2_POWERDOWN		(1 << 16)
#define	 UTMIP_FORCE_PD_POWERUP			(1 << 15)
#define	 UTMIP_FORCE_PD_POWERDOWN		(1 << 14)
#define	 UTMIP_XCVR_TERMEN			(1 << 13)
#define	 UTMIP_XCVR_HSLOOPBACK			(1 << 12)
#define	 UTMIP_XCVR_LSFSLEW(x)			(((x) & 0x3) << 10)
#define	 UTMIP_XCVR_LSRSLEW(x)			(((x) & 0x3) << 8)
#define	 UTMIP_XCVR_FSSLEW(x)			(((x) & 0x3) << 6)
#define	 UTMIP_XCVR_HSSLEW(x)			(((x) & 0x3) << 4)
#define	 UTMIP_XCVR_SETUP(x)			(((x) & 0xf) << 0)

#define	UTMIP_BIAS_CFG0			0x80C
#define	 UTMIP_IDDIG_C_VAL			(1 << 30)
#define	 UTMIP_IDDIG_C_SEL			(1 << 29)
#define	 UTMIP_IDDIG_B_VAL			(1 << 28)
#define	 UTMIP_IDDIG_B_SEL			(1 << 27)
#define	 UTMIP_IDDIG_A_VAL			(1 << 26)
#define	 UTMIP_IDDIG_A_SEL			(1 << 25)
#define	 UTMIP_HSDISCON_LEVEL_MSB(x)		((((x) & 0x4) >> 2) << 24)
#define	 UTMIP_IDPD_VAL				(1 << 23)
#define	 UTMIP_IDPD_SEL				(1 << 22)
#define	 UTMIP_IDDIG_VAL			(1 << 21)
#define	 UTMIP_IDDIG_SEL			(1 << 20)
#define	 UTMIP_GPI_VAL				(1 << 19)
#define	 UTMIP_GPI_SEL				(1 << 18)
#define	 UTMIP_ACTIVE_TERM_OFFSET(x)		(((x) & 0x7) << 15)
#define	 UTMIP_ACTIVE_PULLUP_OFFSET(x)		(((x) & 0x7) << 12)
#define	 UTMIP_OTGPD				(1 << 11)
#define	 UTMIP_BIASPD				(1 << 10)
#define	 UTMIP_VBUS_LEVEL_LEVEL(x)		(((x) & 0x3) << 8)
#define	 UTMIP_SESS_LEVEL_LEVEL(x)		(((x) & 0x3) << 6)
#define	 UTMIP_HSCHIRP_LEVEL(x)			(((x) & 0x3) << 4)
#define	 UTMIP_HSDISCON_LEVEL(x)		(((x) & 0x3) << 2)
#define	 UTMIP_HSSQUELCH_LEVEL(x)		(((x) & 0x3) << 0)


#define	UTMIP_HSRX_CFG0			0x810
#define	 UTMIP_KEEP_PATT_ON_ACTIVE(x)		(((x) & 0x3) << 30)
#define	 UTMIP_ALLOW_CONSEC_UPDN		(1 << 29)
#define	 UTMIP_REALIGN_ON_NEW_PKT		(1 << 28)
#define	 UTMIP_PCOUNT_UPDN_DIV(x)		(((x) & 0xf) << 24)
#define	 UTMIP_SQUELCH_EOP_DLY(x)		(((x) & 0x7) << 21)
#define	 UTMIP_NO_STRIPPING			(1 << 20)
#define	 UTMIP_IDLE_WAIT(x)			(((x) & 0x1f) << 15)
#define	 UTMIP_ELASTIC_LIMIT(x)			(((x) & 0x1f) << 10)
#define	 UTMIP_ELASTIC_OVERRUN_DISABLE		(1 << 9)
#define	 UTMIP_ELASTIC_UNDERRUN_DISABLE		(1 << 8)
#define	 UTMIP_PASS_CHIRP			(1 << 7)
#define	 UTMIP_PASS_FEEDBACK			(1 << 6)
#define	 UTMIP_PCOUNT_INERTIA(x)		(((x) & 0x3) << 4)
#define	 UTMIP_PHASE_ADJUST(x)			(((x) & 0x3) << 2)
#define	 UTMIP_THREE_SYNCBITS			(1 << 1)
#define	 UTMIP_USE4SYNC_TRAN			(1 << 0)

#define	UTMIP_HSRX_CFG1			0x814
#define	 UTMIP_HS_SYNC_START_DLY(x)		(((x) & 0x1F) << 1)
#define	 UTMIP_HS_ALLOW_KEEP_ALIVE		(1 << 0)

#define	UTMIP_TX_CFG0			0x820
#define	 UTMIP_FS_PREAMBLE_J			(1 << 19)
#define	 UTMIP_FS_POSTAMBLE_OUTPUT_ENABLE	(1 << 18)
#define	 UTMIP_FS_PREAMBLE_OUTPUT_ENABLE	(1 << 17)
#define	 UTMIP_FSLS_ALLOW_SOP_TX_STUFF_ERR	(1 << 16)
#define	 UTMIP_HS_READY_WAIT_FOR_VALID		(1 << 15)
#define	 UTMIP_HS_TX_IPG_DLY(x)			(((x) & 0x1f) << 10)
#define	 UTMIP_HS_DISCON_EOP_ONLY		(1 << 9)
#define	 UTMIP_HS_DISCON_DISABLE		(1 << 8)
#define	 UTMIP_HS_POSTAMBLE_OUTPUT_ENABLE	(1 << 7)
#define	 UTMIP_HS_PREAMBLE_OUTPUT_ENABLE	(1 << 6)
#define	 UTMIP_SIE_RESUME_ON_LINESTATE		(1 << 5)
#define	 UTMIP_SOF_ON_NO_STUFF			(1 << 4)
#define	 UTMIP_SOF_ON_NO_ENCODE			(1 << 3)
#define	 UTMIP_NO_STUFFING			(1 << 2)
#define	 UTMIP_NO_ENCODING			(1 << 1)
#define	 UTMIP_NO_SYNC_NO_EOP			(1 << 0)

#define	UTMIP_MISC_CFG0			0x824
#define	 UTMIP_DPDM_OBSERVE_SEL(x)		(((x) & 0xf) << 27)
#define	 UTMIP_DPDM_OBSERVE			(1 << 26)
#define	 UTMIP_KEEP_XCVR_PD_ON_SOFT_DISCON	(1 << 25)
#define	 UTMIP_ALLOW_LS_ON_SOFT_DISCON		(1 << 24)
#define	 UTMIP_FORCE_FS_DISABLE_ON_DEV_CHIRP	(1 << 23)
#define	 UTMIP_SUSPEND_EXIT_ON_EDGE		(1 << 22)
#define	 UTMIP_LS_TO_FS_SKIP_4MS		(1 << 21)
#define	 UTMIP_INJECT_ERROR_TYPE(x)		(((x) & 0x3) << 19)
#define	 UTMIP_FORCE_HS_CLOCK_ON		(1 << 18)
#define	 UTMIP_DISABLE_HS_TERM			(1 << 17)
#define	 UTMIP_FORCE_HS_TERM			(1 << 16)
#define	 UTMIP_DISABLE_PULLUP_DP		(1 << 15)
#define	 UTMIP_DISABLE_PULLUP_DM		(1 << 14)
#define	 UTMIP_DISABLE_PULLDN_DP		(1 << 13)
#define	 UTMIP_DISABLE_PULLDN_DM		(1 << 12)
#define	 UTMIP_FORCE_PULLUP_DP			(1 << 11)
#define	 UTMIP_FORCE_PULLUP_DM			(1 << 10)
#define	 UTMIP_FORCE_PULLDN_DP			(1 << 9)
#define	 UTMIP_FORCE_PULLDN_DM			(1 << 8)
#define	 UTMIP_STABLE_COUNT(x)			(((x) & 0x7) << 5)
#define	 UTMIP_STABLE_ALL			(1 << 4)
#define	 UTMIP_NO_FREE_ON_SUSPEND		(1 << 3)
#define	 UTMIP_NEVER_FREE_RUNNING_TERMS		(1 << 2)
#define	 UTMIP_ALWAYS_FREE_RUNNING_TERMS	(1 << 1)
#define	 UTMIP_COMB_TERMS			(1 << 0)

#define	UTMIP_MISC_CFG1			0x828
#define	 UTMIP_PHY_XTAL_CLOCKEN			(1 << 30)

#define	UTMIP_DEBOUNCE_CFG0		0x82C
#define	 UTMIP_BIAS_DEBOUNCE_B(x)		(((x) & 0xffff) << 16)
#define	 UTMIP_BIAS_DEBOUNCE_A(x)		(((x) & 0xffff) << 0)

#define	UTMIP_BAT_CHRG_CFG0		0x830
#define	 UTMIP_CHRG_DEBOUNCE_TIMESCALE(x) 	(((x) & 0x1f) << 8)
#define	 UTMIP_OP_I_SRC_ENG			(1 << 5)
#define	 UTMIP_ON_SRC_ENG			(1 << 4)
#define	 UTMIP_OP_SRC_ENG			(1 << 3)
#define	 UTMIP_ON_SINK_ENG			(1 << 2)
#define	 UTMIP_OP_SINK_ENG			(1 << 1)
#define	 UTMIP_PD_CHRG				(1 << 0)

#define	UTMIP_SPARE_CFG0		0x834
#define	 FUSE_HS_IREF_CAP_CFG			(1 << 7)
#define	 FUSE_HS_SQUELCH_LEVEL			(1 << 6)
#define	 FUSE_SPARE				(1 << 5)
#define	 FUSE_TERM_RANGE_ADJ_SEL		(1 << 4)
#define	 FUSE_SETUP_SEL				(1 << 3)
#define	 HS_RX_LATE_SQUELCH			(1 << 2)
#define	 HS_RX_FLUSH_ALAP  			(1 << 1)
#define	 HS_RX_IPG_ERROR_ENABLE 		(1 << 0)

#define	UTMIP_XCVR_CFG1			0x838
#define	 UTMIP_XCVR_RPU_RANGE_ADJ(x)		(((x) & 0x3) << 26)
#define	 UTMIP_XCVR_HS_IREF_CAP(x)		(((x) & 0x3) << 24)
#define	 UTMIP_XCVR_SPARE(x)			(((x) & 0x3) << 22)
#define	 UTMIP_XCVR_TERM_RANGE_ADJ(x)		(((x) & 0xf) << 18)
#define	 UTMIP_RCTRL_SW_SET			(1 << 17)
#define	 UTMIP_RCTRL_SW_VAL(x)			(((x) & 0x1f) << 12)
#define	 UTMIP_TCTRL_SW_SET			(1 << 11)
#define	 UTMIP_TCTRL_SW_VAL(x)			(((x) & 0x1f) << 6)
#define	 UTMIP_FORCE_PDDR_POWERUP		(1 << 5)
#define	 UTMIP_FORCE_PDDR_POWERDOWN		(1 << 4)
#define	 UTMIP_FORCE_PDCHRP_POWERUP		(1 << 3)
#define	 UTMIP_FORCE_PDCHRP_POWERDOWN		(1 << 2)
#define	 UTMIP_FORCE_PDDISC_POWERUP		(1 << 1)
#define	 UTMIP_FORCE_PDDISC_POWERDOWN		(1 << 0)

#define	UTMIP_BIAS_CFG1			0x83c
#define	 UTMIP_BIAS_DEBOUNCE_TIMESCALE(x)	(((x) & 0x3f) << 8)
#define	 UTMIP_BIAS_PDTRK_COUNT(x)		(((x) & 0x1f) << 3)
#define	 UTMIP_VBUS_WAKEUP_POWERDOWN		(1 << 2)
#define	 UTMIP_FORCE_PDTRK_POWERUP		(1 << 1)
#define	 UTMIP_FORCE_PDTRK_POWERDOWN		(1 << 0)

static int usbpby_enable_cnt;

enum usb_ifc_type {
	USB_IFC_TYPE_UNKNOWN = 0,
	USB_IFC_TYPE_UTMI,
	USB_IFC_TYPE_ULPI
};

enum usb_dr_mode {
	USB_DR_MODE_UNKNOWN = 0,
	USB_DR_MODE_DEVICE,
	USB_DR_MODE_HOST,
	USB_DR_MODE_OTG
};

struct usbphy_softc {
	device_t		dev;
	struct resource		*mem_res;
	struct resource		*pads_res;
	clk_t			clk_reg;
	clk_t			clk_pads;
	clk_t			clk_pllu;
	regulator_t		supply_vbus;
	hwreset_t		reset_usb;
	hwreset_t		reset_pads;
	enum usb_ifc_type	ifc_type;
	enum usb_dr_mode	dr_mode;
	bool			have_utmi_regs;

	/* UTMI params */
	int			hssync_start_delay;
	int			elastic_limit;
	int			idle_wait_delay;
	int			term_range_adj;
	int			xcvr_lsfslew;
	int			xcvr_lsrslew;
	int			xcvr_hsslew;
	int			hssquelch_level;
	int			hsdiscon_level;
	int			xcvr_setup;
	int			xcvr_setup_use_fuses;
};

static struct ofw_compat_data compat_data[] = {
	{"nvidia,tegra30-usb-phy",	1},
	{NULL,				0},
};

 /* Phy controller class and methods. */
static int usbphy_phy_enable(struct phynode *phy, bool enable);
static phynode_method_t usbphy_phynode_methods[] = {
	PHYNODEMETHOD(phynode_enable, usbphy_phy_enable),

	PHYNODEMETHOD_END
};
DEFINE_CLASS_1(usbphy_phynode, usbphy_phynode_class, usbphy_phynode_methods,
    0, phynode_class);

#define	RD4(sc, offs)							\
	 bus_read_4(sc->mem_res, offs)

#define	WR4(sc, offs, val)						\
	 bus_write_4(sc->mem_res, offs, val)

static int
reg_wait(struct usbphy_softc *sc, uint32_t reg, uint32_t mask, uint32_t val)
{
	int i;

	for (i = 0; i < 1000; i++) {
		if ((RD4(sc, reg) & mask) == val)
			return (0);
		DELAY(10);
	}
	return (ETIMEDOUT);
}

static int
usbphy_utmi_phy_clk(struct usbphy_softc *sc, bool enable)
{
	uint32_t val;
	int rv;

	val = RD4(sc, CTRL_USB_HOSTPC1_DEVLC);
	if (enable)
		val &= ~USB_HOSTPC1_DEVLC_PHCD;
	else
		val |= USB_HOSTPC1_DEVLC_PHCD;
	WR4(sc, CTRL_USB_HOSTPC1_DEVLC, val);

	rv = reg_wait(sc, IF_USB_SUSP_CTRL, USB_PHY_CLK_VALID,
	    enable ? USB_PHY_CLK_VALID: 0);
	if (rv != 0) {
		device_printf(sc->dev, "USB phy clock timeout.\n");
		return (ETIMEDOUT);
	}
	return (0);
}

static int
usbphy_utmi_enable(struct usbphy_softc *sc)
{
	int rv;
	uint32_t val;

	/* Reset phy */
	val = RD4(sc, IF_USB_SUSP_CTRL);
	val |= UTMIP_RESET;
	WR4(sc, IF_USB_SUSP_CTRL, val);


	val = RD4(sc, UTMIP_TX_CFG0);
	val |= UTMIP_FS_PREAMBLE_J;
	WR4(sc, UTMIP_TX_CFG0, val);

	val = RD4(sc, UTMIP_HSRX_CFG0);
	val &= ~UTMIP_IDLE_WAIT(~0);
	val &= ~UTMIP_ELASTIC_LIMIT(~0);
	val |= UTMIP_IDLE_WAIT(sc->idle_wait_delay);
	val |= UTMIP_ELASTIC_LIMIT(sc->elastic_limit);
	WR4(sc, UTMIP_HSRX_CFG0, val);

	val = RD4(sc, UTMIP_HSRX_CFG1);
	val &= ~UTMIP_HS_SYNC_START_DLY(~0);
	val |= UTMIP_HS_SYNC_START_DLY(sc->hssync_start_delay);
	WR4(sc, UTMIP_HSRX_CFG1, val);

	val = RD4(sc, UTMIP_DEBOUNCE_CFG0);
	val &= ~UTMIP_BIAS_DEBOUNCE_A(~0);
	val |= UTMIP_BIAS_DEBOUNCE_A(0x7530);  /* For 12MHz */
	WR4(sc, UTMIP_DEBOUNCE_CFG0, val);

	val = RD4(sc, UTMIP_MISC_CFG0);
	val &= ~UTMIP_SUSPEND_EXIT_ON_EDGE;
	WR4(sc, UTMIP_MISC_CFG0, val);

	if (sc->dr_mode == USB_DR_MODE_DEVICE) {
		val = RD4(sc,IF_USB_SUSP_CTRL);
		val &= ~USB_WAKE_ON_CNNT_EN_DEV;
		val &= ~USB_WAKE_ON_DISCON_EN_DEV;
		WR4(sc, IF_USB_SUSP_CTRL, val);

		val = RD4(sc, UTMIP_BAT_CHRG_CFG0);
		val &= ~UTMIP_PD_CHRG;
		WR4(sc, UTMIP_BAT_CHRG_CFG0, val);
	} else {
		val = RD4(sc, UTMIP_BAT_CHRG_CFG0);
		val |= UTMIP_PD_CHRG;
		WR4(sc, UTMIP_BAT_CHRG_CFG0, val);
	}

	usbpby_enable_cnt++;
	if (usbpby_enable_cnt == 1) {
		rv = hwreset_deassert(sc->reset_pads);
		if (rv != 0) {
			device_printf(sc->dev,
			     "Cannot unreset 'utmi-pads' reset\n");
			return (rv);
		}
		rv = clk_enable(sc->clk_pads);
		if (rv != 0) {
			device_printf(sc->dev,
			    "Cannot enable 'utmi-pads' clock\n");
			return (rv);
		}

		val = bus_read_4(sc->pads_res, UTMIP_BIAS_CFG0);
		val &= ~UTMIP_OTGPD;
		val &= ~UTMIP_BIASPD;
		val &= ~UTMIP_HSSQUELCH_LEVEL(~0);
		val &= ~UTMIP_HSDISCON_LEVEL(~0);
		val &= ~UTMIP_HSDISCON_LEVEL_MSB(~0);
		val |= UTMIP_HSSQUELCH_LEVEL(sc->hssquelch_level);
		val |= UTMIP_HSDISCON_LEVEL(sc->hsdiscon_level);
		val |= UTMIP_HSDISCON_LEVEL_MSB(sc->hsdiscon_level);
		bus_write_4(sc->pads_res, UTMIP_BIAS_CFG0, val);

		rv = clk_disable(sc->clk_pads);
		if (rv != 0) {
			device_printf(sc->dev,
			    "Cannot disable 'utmi-pads' clock\n");
			return (rv);
		}
	}

	val = RD4(sc, UTMIP_XCVR_CFG0);
	val &= ~UTMIP_FORCE_PD_POWERDOWN;
	val &= ~UTMIP_FORCE_PD2_POWERDOWN ;
	val &= ~UTMIP_FORCE_PDZI_POWERDOWN;
	val &= ~UTMIP_XCVR_LSBIAS_SEL;
	val &= ~UTMIP_XCVR_LSFSLEW(~0);
	val &= ~UTMIP_XCVR_LSRSLEW(~0);
	val &= ~UTMIP_XCVR_HSSLEW(~0);
	val &= ~UTMIP_XCVR_HSSLEW_MSB(~0);
	val |= UTMIP_XCVR_LSFSLEW(sc->xcvr_lsfslew);
	val |= UTMIP_XCVR_LSRSLEW(sc->xcvr_lsrslew);
	val |= UTMIP_XCVR_HSSLEW(sc->xcvr_hsslew);
	val |= UTMIP_XCVR_HSSLEW_MSB(sc->xcvr_hsslew);
	if (!sc->xcvr_setup_use_fuses) {
		val &= ~UTMIP_XCVR_SETUP(~0);
		val &= ~UTMIP_XCVR_SETUP_MSB(~0);
		val |= UTMIP_XCVR_SETUP(sc->xcvr_setup);
		val |= UTMIP_XCVR_SETUP_MSB(sc->xcvr_setup);
	}
	WR4(sc, UTMIP_XCVR_CFG0, val);

	val = RD4(sc, UTMIP_XCVR_CFG1);
	val &= ~UTMIP_FORCE_PDDISC_POWERDOWN;
	val &= ~UTMIP_FORCE_PDCHRP_POWERDOWN;
	val &= ~UTMIP_FORCE_PDDR_POWERDOWN;
	val &= ~UTMIP_XCVR_TERM_RANGE_ADJ(~0);
	val |= UTMIP_XCVR_TERM_RANGE_ADJ(sc->term_range_adj);
	WR4(sc, UTMIP_XCVR_CFG1, val);


	val = RD4(sc, UTMIP_BIAS_CFG1);
	val &= ~UTMIP_BIAS_PDTRK_COUNT(~0);
	val |= UTMIP_BIAS_PDTRK_COUNT(0x5);
	WR4(sc, UTMIP_BIAS_CFG1, val);

	val = RD4(sc, UTMIP_SPARE_CFG0);
	if (sc->xcvr_setup_use_fuses)
		val |= FUSE_SETUP_SEL;
	else
		val &= ~FUSE_SETUP_SEL;
	WR4(sc, UTMIP_SPARE_CFG0, val);

	val = RD4(sc, IF_USB_SUSP_CTRL);
	val |= UTMIP_PHY_ENB;
	WR4(sc, IF_USB_SUSP_CTRL, val);

	val = RD4(sc, IF_USB_SUSP_CTRL);
	val &= ~UTMIP_RESET;
	WR4(sc, IF_USB_SUSP_CTRL, val);

	usbphy_utmi_phy_clk(sc, true);

	val = RD4(sc, CTRL_USB_USBMODE);
	val &= ~USB_USBMODE_MASK;
	if (sc->dr_mode == USB_DR_MODE_HOST)
		val |= USB_USBMODE_HOST;
	else
		val |= USB_USBMODE_DEVICE;
	WR4(sc, CTRL_USB_USBMODE, val);

	val = RD4(sc, CTRL_USB_HOSTPC1_DEVLC);
	val &= ~USB_HOSTPC1_DEVLC_PTS(~0);
	val |= USB_HOSTPC1_DEVLC_PTS(0);
	WR4(sc, CTRL_USB_HOSTPC1_DEVLC, val);

	return (0);
}

static int
usbphy_utmi_disable(struct usbphy_softc *sc)
{
	int rv;
	uint32_t val;

	usbphy_utmi_phy_clk(sc, false);

	if (sc->dr_mode == USB_DR_MODE_DEVICE) {
		val = RD4(sc, IF_USB_SUSP_CTRL);
		val &= ~USB_WAKEUP_DEBOUNCE_COUNT(~0);
		val |= USB_WAKE_ON_CNNT_EN_DEV;
		val |= USB_WAKEUP_DEBOUNCE_COUNT(5);
		WR4(sc, IF_USB_SUSP_CTRL, val);
	}

	val = RD4(sc, IF_USB_SUSP_CTRL);
	val |= UTMIP_RESET;
	WR4(sc, IF_USB_SUSP_CTRL, val);

	val = RD4(sc, UTMIP_BAT_CHRG_CFG0);
	val |= UTMIP_PD_CHRG;
	WR4(sc, UTMIP_BAT_CHRG_CFG0, val);

	val = RD4(sc, UTMIP_XCVR_CFG0);
	val |= UTMIP_FORCE_PD_POWERDOWN;
	val |= UTMIP_FORCE_PD2_POWERDOWN;
	val |= UTMIP_FORCE_PDZI_POWERDOWN;
	WR4(sc, UTMIP_XCVR_CFG0, val);

	val = RD4(sc, UTMIP_XCVR_CFG1);
	val |= UTMIP_FORCE_PDDISC_POWERDOWN;
	val |= UTMIP_FORCE_PDCHRP_POWERDOWN;
	val |= UTMIP_FORCE_PDDR_POWERDOWN;
	WR4(sc, UTMIP_XCVR_CFG1, val);

	usbpby_enable_cnt--;
	if (usbpby_enable_cnt <= 0) {
		rv = clk_enable(sc->clk_pads);
		if (rv != 0) {
			device_printf(sc->dev,
			    "Cannot enable 'utmi-pads' clock\n");
			return (rv);
		}
		val =bus_read_4(sc->pads_res, UTMIP_BIAS_CFG0);
		val |= UTMIP_OTGPD;
		val |= UTMIP_BIASPD;
		bus_write_4(sc->pads_res, UTMIP_BIAS_CFG0, val);

		rv = clk_disable(sc->clk_pads);
		if (rv != 0) {
			device_printf(sc->dev,
			    "Cannot disable 'utmi-pads' clock\n");
			return (rv);
		}
	}
	return (0);
}

static int
usbphy_phy_enable(struct phynode *phy, bool enable)
{
	device_t dev;
	struct usbphy_softc *sc;
	int rv = 0;

	dev = phynode_get_device(phy);
	sc = device_get_softc(dev);

	if (sc->ifc_type != USB_IFC_TYPE_UTMI) {
			device_printf(sc->dev,
			    "Only UTMI interface is supported.\n");
			return (ENXIO);
	}
	if (enable)
		rv = usbphy_utmi_enable(sc);
	else
		rv = usbphy_utmi_disable(sc);

	return (rv);
}

static enum usb_ifc_type
usb_get_ifc_mode(device_t dev, phandle_t node, char *name)
{
	char *tmpstr;
	int rv;
	enum usb_ifc_type ret;

	rv = OF_getprop_alloc(node, name, (void **)&tmpstr);
	if (rv <= 0)
		return (USB_IFC_TYPE_UNKNOWN);

	ret = USB_IFC_TYPE_UNKNOWN;
	if (strcmp(tmpstr, "utmi") == 0)
		ret = USB_IFC_TYPE_UTMI;
	else if (strcmp(tmpstr, "ulpi") == 0)
		ret = USB_IFC_TYPE_ULPI;
	else
		device_printf(dev, "Unsupported phy type: %s\n", tmpstr);
	OF_prop_free(tmpstr);
	return (ret);
}

static enum usb_dr_mode
usb_get_dr_mode(device_t dev, phandle_t node, char *name)
{
	char *tmpstr;
	int rv;
	enum usb_dr_mode ret;

	rv = OF_getprop_alloc(node, name, (void **)&tmpstr);
	if (rv <= 0)
		return (USB_DR_MODE_UNKNOWN);

	ret = USB_DR_MODE_UNKNOWN;
	if (strcmp(tmpstr, "device") == 0)
		ret = USB_DR_MODE_DEVICE;
	else if (strcmp(tmpstr, "host") == 0)
		ret = USB_DR_MODE_HOST;
	else if (strcmp(tmpstr, "otg") == 0)
		ret = USB_DR_MODE_OTG;
	else
		device_printf(dev, "Unknown dr mode: %s\n", tmpstr);
	OF_prop_free(tmpstr);
	return (ret);
}

static int
usbphy_utmi_read_params(struct usbphy_softc *sc, phandle_t node)
{
	int rv;

	rv = OF_getencprop(node, "nvidia,hssync-start-delay",
	    &sc->hssync_start_delay, sizeof (sc->hssync_start_delay));
	if (rv <= 0)
		return (ENXIO);

	rv = OF_getencprop(node, "nvidia,elastic-limit",
	    &sc->elastic_limit, sizeof (sc->elastic_limit));
	if (rv <= 0)
		return (ENXIO);

	rv = OF_getencprop(node, "nvidia,idle-wait-delay",
	    &sc->idle_wait_delay, sizeof (sc->idle_wait_delay));
	if (rv <= 0)
		return (ENXIO);

	rv = OF_getencprop(node, "nvidia,term-range-adj",
	    &sc->term_range_adj, sizeof (sc->term_range_adj));
	if (rv <= 0)
		return (ENXIO);

	rv = OF_getencprop(node, "nvidia,xcvr-lsfslew",
	    &sc->xcvr_lsfslew, sizeof (sc->xcvr_lsfslew));
	if (rv <= 0)
		return (ENXIO);

	rv = OF_getencprop(node, "nvidia,xcvr-lsrslew",
	    &sc->xcvr_lsrslew, sizeof (sc->xcvr_lsrslew));
	if (rv <= 0)
		return (ENXIO);

	rv = OF_getencprop(node, "nvidia,xcvr-hsslew",
	    &sc->xcvr_hsslew, sizeof (sc->xcvr_hsslew));
	if (rv <= 0)
		return (ENXIO);

	rv = OF_getencprop(node, "nvidia,hssquelch-level",
	    &sc->hssquelch_level, sizeof (sc->hssquelch_level));
	if (rv <= 0)
		return (ENXIO);

	rv = OF_getencprop(node, "nvidia,hsdiscon-level",
	    &sc->hsdiscon_level, sizeof (sc->hsdiscon_level));
	if (rv <= 0)
		return (ENXIO);

	rv = OF_getproplen(node, "nvidia,xcvr-setup-use-fuses");
	if (rv >= 1) {
		sc->xcvr_setup_use_fuses = 1;
	} else {
		rv = OF_getencprop(node, "nvidia,xcvr-setup",
		    &sc->xcvr_setup, sizeof (sc->xcvr_setup));
		if (rv <= 0)
			return (ENXIO);
	}

	return (0);
}

static int
usbphy_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "Tegra USB phy");
	return (BUS_PROBE_DEFAULT);
}

static int
usbphy_attach(device_t dev)
{
	struct usbphy_softc *sc;
	int rid, rv;
	phandle_t node;
	struct phynode *phynode;
	struct phynode_init_def phy_init;

	sc = device_get_softc(dev);
	sc->dev = dev;

	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE | RF_SHAREABLE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "Cannot allocate memory resources\n");
		return (ENXIO);
	}

	rid = 1;
	sc->pads_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE | RF_SHAREABLE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "Cannot allocate memory resources\n");
		return (ENXIO);
	}

	node = ofw_bus_get_node(dev);

	rv = hwreset_get_by_ofw_name(sc->dev, 0, "usb", &sc->reset_usb);
	if (rv != 0) {
		device_printf(dev, "Cannot get 'usb' reset\n");
		return (ENXIO);
	}
	rv = hwreset_get_by_ofw_name(sc->dev, 0, "utmi-pads", &sc->reset_pads);
	if (rv != 0) {
		device_printf(dev, "Cannot get 'utmi-pads' reset\n");
		return (ENXIO);
	}

	rv = clk_get_by_ofw_name(sc->dev, 0, "reg", &sc->clk_reg);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'reg' clock\n");
		return (ENXIO);
	}
	rv = clk_get_by_ofw_name(sc->dev, 0, "pll_u", &sc->clk_pllu);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'pll_u' clock\n");
		return (ENXIO);
	}
	rv = clk_get_by_ofw_name(sc->dev, 0, "utmi-pads", &sc->clk_pads);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'utmi-pads' clock\n");
		return (ENXIO);
	}

	rv = hwreset_deassert(sc->reset_usb);
	if (rv != 0) {
		device_printf(dev, "Cannot unreset 'usb' reset\n");
		return (ENXIO);
	}

	rv = clk_enable(sc->clk_pllu);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable 'pllu' clock\n");
		return (ENXIO);
	}
	rv = clk_enable(sc->clk_reg);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable 'reg' clock\n");
		return (ENXIO);
	}
	if (OF_hasprop(node, "nvidia,has-utmi-pad-registers"))
		sc->have_utmi_regs = true;

	sc->dr_mode = usb_get_dr_mode(dev, node, "dr_mode");
	if (sc->dr_mode == USB_DR_MODE_UNKNOWN)
		sc->dr_mode = USB_DR_MODE_HOST;

	sc->ifc_type = usb_get_ifc_mode(dev, node, "phy_type");

	/* We supports only utmi phy mode for now .... */
	if (sc->ifc_type != USB_IFC_TYPE_UTMI) {
		device_printf(dev, "Unsupported phy type\n");
		return (ENXIO);
	}
	rv = usbphy_utmi_read_params(sc, node);
	if (rv < 0)
		return rv;

	if (OF_hasprop(node, "vbus-supply")) {
		rv = regulator_get_by_ofw_property(sc->dev, 0, "vbus-supply",
		    &sc->supply_vbus);
		if (rv != 0) {
			device_printf(sc->dev,
			   "Cannot get \"vbus\" regulator\n");
			return (ENXIO);
		}
		rv = regulator_enable(sc->supply_vbus);
		if (rv != 0) {
			device_printf(sc->dev,
			    "Cannot enable  \"vbus\" regulator\n");
			return (rv);
		}
	}

	/* Create and register phy. */
	bzero(&phy_init, sizeof(phy_init));
	phy_init.id = 1;
	phy_init.ofw_node = node;
	phynode = phynode_create(dev, &usbphy_phynode_class, &phy_init);
	if (phynode == NULL) {
		device_printf(sc->dev, "Cannot create phy\n");
		return (ENXIO);
	}
	if (phynode_register(phynode) == NULL) {
		device_printf(sc->dev, "Cannot create phy\n");
		return (ENXIO);
	}

	return (0);
}

static int
usbphy_detach(device_t dev)
{

	/* This device is always present. */
	return (EBUSY);
}

static device_method_t tegra_usbphy_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		usbphy_probe),
	DEVMETHOD(device_attach,	usbphy_attach),
	DEVMETHOD(device_detach,	usbphy_detach),

	DEVMETHOD_END
};

static devclass_t tegra_usbphy_devclass;
static DEFINE_CLASS_0(usbphy, tegra_usbphy_driver, tegra_usbphy_methods,
    sizeof(struct usbphy_softc));
EARLY_DRIVER_MODULE(tegra_usbphy, simplebus, tegra_usbphy_driver,
    tegra_usbphy_devclass, NULL, NULL, 79);
