/*-
 * Copyright (c) 2015 Oleksandr Tymoshenko <gonzo@freebsd.org>
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
 * HDMI core module
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <machine/bus.h>

#include <dev/videomode/videomode.h>
#include <dev/videomode/edidvar.h>

#include <dev/iicbus/iicbus.h>
#include <dev/iicbus/iiconf.h>

#include <dev/hdmi/dwc_hdmi.h>
#include <dev/hdmi/dwc_hdmireg.h>

#include "hdmi_if.h"

#define	I2C_DDC_ADDR	(0x50 << 1)
#define	I2C_DDC_SEGADDR	(0x30 << 1)
#define	EDID_LENGTH	0x80

#define	EXT_TAG			0x00
#define	CEA_TAG_ID		0x02
#define	CEA_DTD			0x03
#define	DTD_BASIC_AUDIO		(1 << 6)
#define	CEA_REV			0x02
#define	CEA_DATA_OFF		0x03
#define	CEA_DATA_START		4
#define	BLOCK_TAG(x)		(((x) >> 5) & 0x7)
#define	BLOCK_TAG_VSDB		3
#define	BLOCK_LEN(x)		((x) & 0x1f)
#define	HDMI_VSDB_MINLEN	5
#define	HDMI_OUI		"\x03\x0c\x00"
#define	HDMI_OUI_LEN		3

static void
dwc_hdmi_phy_wait_i2c_done(struct dwc_hdmi_softc *sc, int msec)
{
	uint8_t val;

	val = RD1(sc, HDMI_IH_I2CMPHY_STAT0) &
	    (HDMI_IH_I2CMPHY_STAT0_DONE | HDMI_IH_I2CMPHY_STAT0_ERROR);
	while (val == 0) {
		pause("HDMI_PHY", hz/100);
		msec -= 10;
		if (msec <= 0)
			return;
		val = RD1(sc, HDMI_IH_I2CMPHY_STAT0) &
		    (HDMI_IH_I2CMPHY_STAT0_DONE | HDMI_IH_I2CMPHY_STAT0_ERROR);
	}
}

static void
dwc_hdmi_phy_i2c_write(struct dwc_hdmi_softc *sc, unsigned short data,
    unsigned char addr)
{

	/* clear DONE and ERROR flags */
	WR1(sc, HDMI_IH_I2CMPHY_STAT0,
	    HDMI_IH_I2CMPHY_STAT0_DONE | HDMI_IH_I2CMPHY_STAT0_ERROR);
	WR1(sc, HDMI_PHY_I2CM_ADDRESS_ADDR, addr);
	WR1(sc, HDMI_PHY_I2CM_DATAO_1_ADDR, ((data >> 8) & 0xff));
	WR1(sc, HDMI_PHY_I2CM_DATAO_0_ADDR, ((data >> 0) & 0xff));
	WR1(sc, HDMI_PHY_I2CM_OPERATION_ADDR, HDMI_PHY_I2CM_OPERATION_ADDR_WRITE);
	dwc_hdmi_phy_wait_i2c_done(sc, 1000);
}

static void
dwc_hdmi_disable_overflow_interrupts(struct dwc_hdmi_softc *sc)
{
	WR1(sc, HDMI_IH_MUTE_FC_STAT2, HDMI_IH_MUTE_FC_STAT2_OVERFLOW_MASK);
	WR1(sc, HDMI_FC_MASK2,
	    HDMI_FC_MASK2_LOW_PRI | HDMI_FC_MASK2_HIGH_PRI);
}

static void
dwc_hdmi_av_composer(struct dwc_hdmi_softc *sc)
{
	uint8_t inv_val;
	int is_dvi;
	int hblank, vblank, hsync_len, hfp, vfp;

	/* Set up HDMI_FC_INVIDCONF */
	inv_val = ((sc->sc_mode.flags & VID_PVSYNC) ?
		HDMI_FC_INVIDCONF_VSYNC_IN_POLARITY_ACTIVE_HIGH :
		HDMI_FC_INVIDCONF_VSYNC_IN_POLARITY_ACTIVE_LOW);

	inv_val |= ((sc->sc_mode.flags & VID_PHSYNC) ?
		HDMI_FC_INVIDCONF_HSYNC_IN_POLARITY_ACTIVE_HIGH :
		HDMI_FC_INVIDCONF_HSYNC_IN_POLARITY_ACTIVE_LOW);

	inv_val |= HDMI_FC_INVIDCONF_DE_IN_POLARITY_ACTIVE_HIGH;

	inv_val |= ((sc->sc_mode.flags & VID_INTERLACE) ?
			HDMI_FC_INVIDCONF_R_V_BLANK_IN_OSC_ACTIVE_HIGH :
			HDMI_FC_INVIDCONF_R_V_BLANK_IN_OSC_ACTIVE_LOW);

	inv_val |= ((sc->sc_mode.flags & VID_INTERLACE) ?
		HDMI_FC_INVIDCONF_IN_I_P_INTERLACED :
		HDMI_FC_INVIDCONF_IN_I_P_PROGRESSIVE);

	/* TODO: implement HDMI part */
	is_dvi = sc->sc_has_audio == 0;
	inv_val |= (is_dvi ?
		HDMI_FC_INVIDCONF_DVI_MODEZ_DVI_MODE :
		HDMI_FC_INVIDCONF_DVI_MODEZ_HDMI_MODE);

	WR1(sc, HDMI_FC_INVIDCONF, inv_val);

	/* Set up horizontal active pixel region width */
	WR1(sc, HDMI_FC_INHACTV1, sc->sc_mode.hdisplay >> 8);
	WR1(sc, HDMI_FC_INHACTV0, sc->sc_mode.hdisplay);

	/* Set up vertical blanking pixel region width */
	WR1(sc, HDMI_FC_INVACTV1, sc->sc_mode.vdisplay >> 8);
	WR1(sc, HDMI_FC_INVACTV0, sc->sc_mode.vdisplay);

	/* Set up horizontal blanking pixel region width */
	hblank = sc->sc_mode.htotal - sc->sc_mode.hdisplay;
	WR1(sc, HDMI_FC_INHBLANK1, hblank >> 8);
	WR1(sc, HDMI_FC_INHBLANK0, hblank);

	/* Set up vertical blanking pixel region width */
	vblank = sc->sc_mode.vtotal - sc->sc_mode.vdisplay;
	WR1(sc, HDMI_FC_INVBLANK, vblank);

	/* Set up HSYNC active edge delay width (in pixel clks) */
	hfp = sc->sc_mode.hsync_start - sc->sc_mode.hdisplay;
	WR1(sc, HDMI_FC_HSYNCINDELAY1, hfp >> 8);
	WR1(sc, HDMI_FC_HSYNCINDELAY0, hfp);

	/* Set up VSYNC active edge delay (in pixel clks) */
	vfp = sc->sc_mode.vsync_start - sc->sc_mode.vdisplay;
	WR1(sc, HDMI_FC_VSYNCINDELAY, vfp);

	hsync_len = (sc->sc_mode.hsync_end - sc->sc_mode.hsync_start);
	/* Set up HSYNC active pulse width (in pixel clks) */
	WR1(sc, HDMI_FC_HSYNCINWIDTH1, hsync_len >> 8);
	WR1(sc, HDMI_FC_HSYNCINWIDTH0, hsync_len);

	/* Set up VSYNC active edge delay (in pixel clks) */
	WR1(sc, HDMI_FC_VSYNCINWIDTH, (sc->sc_mode.vsync_end - sc->sc_mode.vsync_start));
}

static void
dwc_hdmi_phy_enable_power(struct dwc_hdmi_softc *sc, uint8_t enable)
{
	uint8_t reg;

	reg = RD1(sc, HDMI_PHY_CONF0);
	reg &= ~HDMI_PHY_CONF0_PDZ_MASK;
	reg |= (enable << HDMI_PHY_CONF0_PDZ_OFFSET);
	WR1(sc, HDMI_PHY_CONF0, reg);
}

static void
dwc_hdmi_phy_enable_tmds(struct dwc_hdmi_softc *sc, uint8_t enable)
{
	uint8_t reg;

	reg = RD1(sc, HDMI_PHY_CONF0);
	reg &= ~HDMI_PHY_CONF0_ENTMDS_MASK;
	reg |= (enable << HDMI_PHY_CONF0_ENTMDS_OFFSET);
	WR1(sc, HDMI_PHY_CONF0, reg);
}

static void
dwc_hdmi_phy_gen2_pddq(struct dwc_hdmi_softc *sc, uint8_t enable)
{
	uint8_t reg;

	reg = RD1(sc, HDMI_PHY_CONF0);
	reg &= ~HDMI_PHY_CONF0_GEN2_PDDQ_MASK;
	reg |= (enable << HDMI_PHY_CONF0_GEN2_PDDQ_OFFSET);
	WR1(sc, HDMI_PHY_CONF0, reg);
}

static void
dwc_hdmi_phy_gen2_txpwron(struct dwc_hdmi_softc *sc, uint8_t enable)
{
	uint8_t reg;

	reg = RD1(sc, HDMI_PHY_CONF0);
	reg &= ~HDMI_PHY_CONF0_GEN2_TXPWRON_MASK;
	reg |= (enable << HDMI_PHY_CONF0_GEN2_TXPWRON_OFFSET);
	WR1(sc, HDMI_PHY_CONF0, reg);
}

static void
dwc_hdmi_phy_sel_data_en_pol(struct dwc_hdmi_softc *sc, uint8_t enable)
{
	uint8_t reg;

	reg = RD1(sc, HDMI_PHY_CONF0);
	reg &= ~HDMI_PHY_CONF0_SELDATAENPOL_MASK;
	reg |= (enable << HDMI_PHY_CONF0_SELDATAENPOL_OFFSET);
	WR1(sc, HDMI_PHY_CONF0, reg);
}

static void
dwc_hdmi_phy_sel_interface_control(struct dwc_hdmi_softc *sc, uint8_t enable)
{
	uint8_t reg;

	reg = RD1(sc, HDMI_PHY_CONF0);
	reg &= ~HDMI_PHY_CONF0_SELDIPIF_MASK;
	reg |= (enable << HDMI_PHY_CONF0_SELDIPIF_OFFSET);
	WR1(sc, HDMI_PHY_CONF0, reg);
}

static inline void
dwc_hdmi_phy_test_clear(struct dwc_hdmi_softc *sc, unsigned char bit)
{
	uint8_t val;

	val = RD1(sc, HDMI_PHY_TST0);
	val &= ~HDMI_PHY_TST0_TSTCLR_MASK;
	val |= (bit << HDMI_PHY_TST0_TSTCLR_OFFSET) &
		HDMI_PHY_TST0_TSTCLR_MASK;
	WR1(sc, HDMI_PHY_TST0, val);
}

static void
dwc_hdmi_clear_overflow(struct dwc_hdmi_softc *sc)
{
	int count;
	uint8_t val;

	/* TMDS software reset */
	WR1(sc, HDMI_MC_SWRSTZ, (uint8_t)~HDMI_MC_SWRSTZ_TMDSSWRST_REQ);

	val = RD1(sc, HDMI_FC_INVIDCONF);

	for (count = 0 ; count < 4 ; count++)
		WR1(sc, HDMI_FC_INVIDCONF, val);
}

static int
dwc_hdmi_phy_configure(struct dwc_hdmi_softc *sc)
{
	uint8_t val;
	uint8_t msec;

	WR1(sc, HDMI_MC_FLOWCTRL, HDMI_MC_FLOWCTRL_FEED_THROUGH_OFF_CSC_BYPASS);

	/* gen2 tx power off */
	dwc_hdmi_phy_gen2_txpwron(sc, 0);

	/* gen2 pddq */
	dwc_hdmi_phy_gen2_pddq(sc, 1);

	/* PHY reset */
	WR1(sc, HDMI_MC_PHYRSTZ, HDMI_MC_PHYRSTZ_DEASSERT);
	WR1(sc, HDMI_MC_PHYRSTZ, HDMI_MC_PHYRSTZ_ASSERT);

	WR1(sc, HDMI_MC_HEACPHY_RST, HDMI_MC_HEACPHY_RST_ASSERT);

	dwc_hdmi_phy_test_clear(sc, 1);
	WR1(sc, HDMI_PHY_I2CM_SLAVE_ADDR, HDMI_PHY_I2CM_SLAVE_ADDR_PHY_GEN2);
	dwc_hdmi_phy_test_clear(sc, 0);

	/*
	 * Following initialization are for 8bit per color case
	 */

	/*
	 * PLL/MPLL config, see section 24.7.22 in TRM
	 *  config, see section 24.7.22
	 */
	if (sc->sc_mode.dot_clock*1000 <= 45250000) {
		dwc_hdmi_phy_i2c_write(sc, CPCE_CTRL_45_25, HDMI_PHY_I2C_CPCE_CTRL);
		dwc_hdmi_phy_i2c_write(sc, GMPCTRL_45_25, HDMI_PHY_I2C_GMPCTRL);
	} else if (sc->sc_mode.dot_clock*1000 <= 92500000) {
		dwc_hdmi_phy_i2c_write(sc, CPCE_CTRL_92_50, HDMI_PHY_I2C_CPCE_CTRL);
		dwc_hdmi_phy_i2c_write(sc, GMPCTRL_92_50, HDMI_PHY_I2C_GMPCTRL);
	} else if (sc->sc_mode.dot_clock*1000 <= 185000000) {
		dwc_hdmi_phy_i2c_write(sc, CPCE_CTRL_185, HDMI_PHY_I2C_CPCE_CTRL);
		dwc_hdmi_phy_i2c_write(sc, GMPCTRL_185, HDMI_PHY_I2C_GMPCTRL);
	} else {
		dwc_hdmi_phy_i2c_write(sc, CPCE_CTRL_370, HDMI_PHY_I2C_CPCE_CTRL);
		dwc_hdmi_phy_i2c_write(sc, GMPCTRL_370, HDMI_PHY_I2C_GMPCTRL);
	}

	/*
	 * Values described in TRM section 34.9.2 PLL/MPLL Generic
	 *    Configuration Settings. Table 34-23.
	 */
	if (sc->sc_mode.dot_clock*1000 <= 54000000) {
		dwc_hdmi_phy_i2c_write(sc, 0x091c, HDMI_PHY_I2C_CURRCTRL);
	} else if (sc->sc_mode.dot_clock*1000 <= 58400000) {
		dwc_hdmi_phy_i2c_write(sc, 0x091c, HDMI_PHY_I2C_CURRCTRL);
	} else if (sc->sc_mode.dot_clock*1000 <= 72000000) {
		dwc_hdmi_phy_i2c_write(sc, 0x06dc, HDMI_PHY_I2C_CURRCTRL);
	} else if (sc->sc_mode.dot_clock*1000 <= 74250000) {
		dwc_hdmi_phy_i2c_write(sc, 0x06dc, HDMI_PHY_I2C_CURRCTRL);
	} else if (sc->sc_mode.dot_clock*1000 <= 118800000) {
		dwc_hdmi_phy_i2c_write(sc, 0x091c, HDMI_PHY_I2C_CURRCTRL);
	} else if (sc->sc_mode.dot_clock*1000 <= 216000000) {
		dwc_hdmi_phy_i2c_write(sc, 0x06dc, HDMI_PHY_I2C_CURRCTRL);
	} else {
		panic("Unsupported mode\n");
	}

	dwc_hdmi_phy_i2c_write(sc, 0x0000, HDMI_PHY_I2C_PLLPHBYCTRL);
	dwc_hdmi_phy_i2c_write(sc, MSM_CTRL_FB_CLK, HDMI_PHY_I2C_MSM_CTRL);
	/* RESISTANCE TERM 133 Ohm */
	dwc_hdmi_phy_i2c_write(sc, TXTERM_133, HDMI_PHY_I2C_TXTERM);

	/* REMOVE CLK TERM */
	dwc_hdmi_phy_i2c_write(sc, CKCALCTRL_OVERRIDE, HDMI_PHY_I2C_CKCALCTRL);

	if (sc->sc_mode.dot_clock*1000 > 148500000) {
		dwc_hdmi_phy_i2c_write(sc,CKSYMTXCTRL_OVERRIDE | CKSYMTXCTRL_TX_SYMON |
		    CKSYMTXCTRL_TX_TRBON | CKSYMTXCTRL_TX_CK_SYMON, HDMI_PHY_I2C_CKSYMTXCTRL); 
		dwc_hdmi_phy_i2c_write(sc, VLEVCTRL_TX_LVL(9) | VLEVCTRL_CK_LVL(9),
		    HDMI_PHY_I2C_VLEVCTRL);
	} else {
		dwc_hdmi_phy_i2c_write(sc,CKSYMTXCTRL_OVERRIDE | CKSYMTXCTRL_TX_SYMON |
		    CKSYMTXCTRL_TX_TRAON | CKSYMTXCTRL_TX_CK_SYMON, HDMI_PHY_I2C_CKSYMTXCTRL); 
		dwc_hdmi_phy_i2c_write(sc, VLEVCTRL_TX_LVL(13) | VLEVCTRL_CK_LVL(13),
		    HDMI_PHY_I2C_VLEVCTRL);
	}

	dwc_hdmi_phy_enable_power(sc, 1);

	/* toggle TMDS enable */
	dwc_hdmi_phy_enable_tmds(sc, 0);
	dwc_hdmi_phy_enable_tmds(sc, 1);

	/* gen2 tx power on */
	dwc_hdmi_phy_gen2_txpwron(sc, 1);
	dwc_hdmi_phy_gen2_pddq(sc, 0);

	/*Wait for PHY PLL lock */
	msec = 4;
	val = RD1(sc, HDMI_PHY_STAT0) & HDMI_PHY_TX_PHY_LOCK;
	while (val == 0) {
		DELAY(1000);
		if (msec-- == 0) {
			device_printf(sc->sc_dev, "PHY PLL not locked\n");
			return (-1);
		}
		val = RD1(sc, HDMI_PHY_STAT0) & HDMI_PHY_TX_PHY_LOCK;
	}

	return true;
}

static void
dwc_hdmi_phy_init(struct dwc_hdmi_softc *sc)
{
	int i;

	/* HDMI Phy spec says to do the phy initialization sequence twice */
	for (i = 0 ; i < 2 ; i++) {
		dwc_hdmi_phy_sel_data_en_pol(sc, 1);
		dwc_hdmi_phy_sel_interface_control(sc, 0);
		dwc_hdmi_phy_enable_tmds(sc, 0);
		dwc_hdmi_phy_enable_power(sc, 0);

		/* Enable CSC */
		dwc_hdmi_phy_configure(sc);
	}
}

static void
dwc_hdmi_enable_video_path(struct dwc_hdmi_softc *sc)
{
	uint8_t clkdis;

	/*
	 * Control period timing
	 * Values are minimal according to HDMI spec 1.4a
	 */
	WR1(sc, HDMI_FC_CTRLDUR, 12);
	WR1(sc, HDMI_FC_EXCTRLDUR, 32);
	WR1(sc, HDMI_FC_EXCTRLSPAC, 1);

	/*
	 * Bits to fill data lines not used to transmit preamble
	 * for channels 0, 1, and 2 respectively
	 */
	WR1(sc, HDMI_FC_CH0PREAM, 0x0B);
	WR1(sc, HDMI_FC_CH1PREAM, 0x16);
	WR1(sc, HDMI_FC_CH2PREAM, 0x21);

	/* Save CEC clock */
	clkdis = RD1(sc, HDMI_MC_CLKDIS) & HDMI_MC_CLKDIS_CECCLK_DISABLE;
	clkdis |= ~HDMI_MC_CLKDIS_CECCLK_DISABLE;

	/* Enable pixel clock and tmds data path */
	clkdis &= ~HDMI_MC_CLKDIS_PIXELCLK_DISABLE;
	WR1(sc, HDMI_MC_CLKDIS, clkdis);

	clkdis &= ~HDMI_MC_CLKDIS_TMDSCLK_DISABLE;
	WR1(sc, HDMI_MC_CLKDIS, clkdis);
}

static void
dwc_hdmi_configure_audio(struct dwc_hdmi_softc *sc)
{
	unsigned int n;
	uint8_t val;

	if (sc->sc_has_audio == 0)
		return;

	/* The following values are for 48 kHz */
	switch (sc->sc_mode.dot_clock) {
	case 25170:
		n = 6864;
		break;
	case 27020:
		n = 6144;
		break;
	case 74170:
		n = 11648;
		break;
	case 148350:
		n = 5824;
		break;
	default:
		n = 6144;
		break;
	}

	WR1(sc, HDMI_AUD_N1, (n >> 0) & 0xff);
	WR1(sc, HDMI_AUD_N2, (n >> 8) & 0xff);
	WR1(sc, HDMI_AUD_N3, (n >> 16) & 0xff);

	val = RD1(sc, HDMI_AUD_CTS3);
	val &= ~(HDMI_AUD_CTS3_N_SHIFT_MASK | HDMI_AUD_CTS3_CTS_MANUAL);
	WR1(sc, HDMI_AUD_CTS3, val);

	val = RD1(sc, HDMI_AUD_CONF0);
	val &= ~HDMI_AUD_CONF0_INTERFACE_MASK;
	val |= HDMI_AUD_CONF0_INTERFACE_IIS;
	val &= ~HDMI_AUD_CONF0_I2SINEN_MASK;
	val |= HDMI_AUD_CONF0_I2SINEN_CH2;
	WR1(sc, HDMI_AUD_CONF0, val);

	val = RD1(sc, HDMI_AUD_CONF1);
	val &= ~HDMI_AUD_CONF1_DATAMODE_MASK;
	val |= HDMI_AUD_CONF1_DATAMODE_IIS;
	val &= ~HDMI_AUD_CONF1_DATWIDTH_MASK;
	val |= HDMI_AUD_CONF1_DATWIDTH_16BIT;
	WR1(sc, HDMI_AUD_CONF1, val);

	WR1(sc, HDMI_AUD_INPUTCLKFS, HDMI_AUD_INPUTCLKFS_64);

	WR1(sc, HDMI_FC_AUDICONF0, 1 << 4);	/* CC=1 */
	WR1(sc, HDMI_FC_AUDICONF1, 0);
	WR1(sc, HDMI_FC_AUDICONF2, 0);		/* CA=0 */
	WR1(sc, HDMI_FC_AUDICONF3, 0);
	WR1(sc, HDMI_FC_AUDSV, 0xee);		/* channels valid */

	/* Enable audio clock */
	val = RD1(sc, HDMI_MC_CLKDIS);
	val &= ~HDMI_MC_CLKDIS_AUDCLK_DISABLE;
	WR1(sc, HDMI_MC_CLKDIS, val);
}

static void
dwc_hdmi_video_packetize(struct dwc_hdmi_softc *sc)
{
	unsigned int color_depth = 0;
	unsigned int remap_size = HDMI_VP_REMAP_YCC422_16BIT;
	unsigned int output_select = HDMI_VP_CONF_OUTPUT_SELECTOR_PP;
	uint8_t val;

	output_select = HDMI_VP_CONF_OUTPUT_SELECTOR_BYPASS;
	color_depth = 4;

	/* set the packetizer registers */
	val = ((color_depth << HDMI_VP_PR_CD_COLOR_DEPTH_OFFSET) &
		HDMI_VP_PR_CD_COLOR_DEPTH_MASK);
	WR1(sc, HDMI_VP_PR_CD, val);

	val = RD1(sc, HDMI_VP_STUFF);
	val &= ~HDMI_VP_STUFF_PR_STUFFING_MASK;
	val |= HDMI_VP_STUFF_PR_STUFFING_STUFFING_MODE;
	WR1(sc, HDMI_VP_STUFF, val);

	val = RD1(sc, HDMI_VP_CONF);
	val &= ~(HDMI_VP_CONF_PR_EN_MASK |
		HDMI_VP_CONF_BYPASS_SELECT_MASK);
	val |= HDMI_VP_CONF_PR_EN_DISABLE |
		HDMI_VP_CONF_BYPASS_SELECT_VID_PACKETIZER;
	WR1(sc, HDMI_VP_CONF, val);

	val = RD1(sc, HDMI_VP_STUFF);
	val &= ~HDMI_VP_STUFF_IDEFAULT_PHASE_MASK;
	val |= 1 << HDMI_VP_STUFF_IDEFAULT_PHASE_OFFSET;
	WR1(sc, HDMI_VP_STUFF, val);

	WR1(sc, HDMI_VP_REMAP, remap_size);

	if (output_select == HDMI_VP_CONF_OUTPUT_SELECTOR_PP) {
		val = RD1(sc, HDMI_VP_CONF);
		val &= ~(HDMI_VP_CONF_BYPASS_EN_MASK |
			HDMI_VP_CONF_PP_EN_ENMASK |
			HDMI_VP_CONF_YCC422_EN_MASK);
		val |= HDMI_VP_CONF_BYPASS_EN_DISABLE |
			HDMI_VP_CONF_PP_EN_ENABLE |
			HDMI_VP_CONF_YCC422_EN_DISABLE;
		WR1(sc, HDMI_VP_CONF, val);
	} else if (output_select == HDMI_VP_CONF_OUTPUT_SELECTOR_YCC422) {
		val = RD1(sc, HDMI_VP_CONF);
		val &= ~(HDMI_VP_CONF_BYPASS_EN_MASK |
			HDMI_VP_CONF_PP_EN_ENMASK |
			HDMI_VP_CONF_YCC422_EN_MASK);
		val |= HDMI_VP_CONF_BYPASS_EN_DISABLE |
			HDMI_VP_CONF_PP_EN_DISABLE |
			HDMI_VP_CONF_YCC422_EN_ENABLE;
		WR1(sc, HDMI_VP_CONF, val);
	} else if (output_select == HDMI_VP_CONF_OUTPUT_SELECTOR_BYPASS) {
		val = RD1(sc, HDMI_VP_CONF);
		val &= ~(HDMI_VP_CONF_BYPASS_EN_MASK |
			HDMI_VP_CONF_PP_EN_ENMASK |
			HDMI_VP_CONF_YCC422_EN_MASK);
		val |= HDMI_VP_CONF_BYPASS_EN_ENABLE |
			HDMI_VP_CONF_PP_EN_DISABLE |
			HDMI_VP_CONF_YCC422_EN_DISABLE;
		WR1(sc, HDMI_VP_CONF, val);
	} else {
		return;
	}

	val = RD1(sc, HDMI_VP_STUFF);
	val &= ~(HDMI_VP_STUFF_PP_STUFFING_MASK |
		HDMI_VP_STUFF_YCC422_STUFFING_MASK);
	val |= HDMI_VP_STUFF_PP_STUFFING_STUFFING_MODE |
		HDMI_VP_STUFF_YCC422_STUFFING_STUFFING_MODE;
	WR1(sc, HDMI_VP_STUFF, val);

	val = RD1(sc, HDMI_VP_CONF);
	val &= ~HDMI_VP_CONF_OUTPUT_SELECTOR_MASK;
	val |= output_select;
	WR1(sc, HDMI_VP_CONF, val);
}

static void
dwc_hdmi_video_sample(struct dwc_hdmi_softc *sc)
{
	int color_format;
	uint8_t val;

	color_format = 0x01;
	val = HDMI_TX_INVID0_INTERNAL_DE_GENERATOR_DISABLE |
		((color_format << HDMI_TX_INVID0_VIDEO_MAPPING_OFFSET) &
		HDMI_TX_INVID0_VIDEO_MAPPING_MASK);
	WR1(sc, HDMI_TX_INVID0, val);

	/* Enable TX stuffing: When DE is inactive, fix the output data to 0 */
	val = HDMI_TX_INSTUFFING_BDBDATA_STUFFING_ENABLE |
		HDMI_TX_INSTUFFING_RCRDATA_STUFFING_ENABLE |
		HDMI_TX_INSTUFFING_GYDATA_STUFFING_ENABLE;
	WR1(sc, HDMI_TX_INSTUFFING, val);
	WR1(sc, HDMI_TX_GYDATA0, 0x0);
	WR1(sc, HDMI_TX_GYDATA1, 0x0);
	WR1(sc, HDMI_TX_RCRDATA0, 0x0);
	WR1(sc, HDMI_TX_RCRDATA1, 0x0);
	WR1(sc, HDMI_TX_BCBDATA0, 0x0);
	WR1(sc, HDMI_TX_BCBDATA1, 0x0);
}

static void
dwc_hdmi_tx_hdcp_config(struct dwc_hdmi_softc *sc)
{
	uint8_t de, val;

	de = HDMI_A_VIDPOLCFG_DATAENPOL_ACTIVE_HIGH;

	/* Disable RX detect */
	val = RD1(sc, HDMI_A_HDCPCFG0);
	val &= ~HDMI_A_HDCPCFG0_RXDETECT_MASK;
	val |= HDMI_A_HDCPCFG0_RXDETECT_DISABLE;
	WR1(sc, HDMI_A_HDCPCFG0, val);

	/* Set polarity */
	val = RD1(sc, HDMI_A_VIDPOLCFG);
	val &= ~HDMI_A_VIDPOLCFG_DATAENPOL_MASK;
	val |= de;
	WR1(sc, HDMI_A_VIDPOLCFG, val);

	/* Disable encryption */
	val = RD1(sc, HDMI_A_HDCPCFG1);
	val &= ~HDMI_A_HDCPCFG1_ENCRYPTIONDISABLE_MASK;
	val |= HDMI_A_HDCPCFG1_ENCRYPTIONDISABLE_DISABLE;
	WR1(sc, HDMI_A_HDCPCFG1, val);
}

static int
dwc_hdmi_set_mode(struct dwc_hdmi_softc *sc)
{

	/* XXX */
	sc->sc_has_audio = 1;

	dwc_hdmi_disable_overflow_interrupts(sc);
	dwc_hdmi_av_composer(sc);
	dwc_hdmi_phy_init(sc);
	dwc_hdmi_enable_video_path(sc);
	dwc_hdmi_configure_audio(sc);
	/* TODO:  dwc_hdmi_config_avi(sc); */
	dwc_hdmi_video_packetize(sc);
	/* TODO:  dwc_hdmi_video_csc(sc); */
	dwc_hdmi_video_sample(sc);
	dwc_hdmi_tx_hdcp_config(sc);
	dwc_hdmi_clear_overflow(sc);

	return (0);
}

static int
hdmi_edid_read(struct dwc_hdmi_softc *sc, int block, uint8_t **edid,
    uint32_t *edid_len)
{
	device_t i2c_dev;
	int result;
	uint8_t addr = block & 1 ? EDID_LENGTH : 0;
	uint8_t segment = block >> 1;
	struct iic_msg msg[] = {
		{ I2C_DDC_SEGADDR, IIC_M_WR, 1, &segment },
		{ I2C_DDC_ADDR, IIC_M_WR, 1, &addr },
		{ I2C_DDC_ADDR, IIC_M_RD, EDID_LENGTH, sc->sc_edid }
	};

	*edid = NULL;
	*edid_len = 0;
	i2c_dev = NULL;

	if (sc->sc_get_i2c_dev != NULL)
		i2c_dev = sc->sc_get_i2c_dev(sc->sc_dev);
	if (!i2c_dev) {
		device_printf(sc->sc_dev, "no DDC device found\n");
		return (ENXIO);
	}

	if (bootverbose)
		device_printf(sc->sc_dev,
		    "reading EDID from %s, block %d, addr %02x\n",
		    device_get_nameunit(i2c_dev), block, I2C_DDC_ADDR/2);

	result = iicbus_request_bus(i2c_dev, sc->sc_dev, IIC_INTRWAIT);

	if (result) {
		device_printf(sc->sc_dev, "failed to request i2c bus: %d\n", result);
		return (result);
	}

	result = iicbus_transfer(i2c_dev, msg, 3);
	iicbus_release_bus(i2c_dev, sc->sc_dev);

	if (result) {
		device_printf(sc->sc_dev, "i2c transfer failed: %d\n", result);
		return (result);
	} else {
		*edid_len = sc->sc_edid_len;
		*edid = sc->sc_edid;
	}

	return (result);
}

static void
dwc_hdmi_detect_cable(void *arg)
{
	struct dwc_hdmi_softc *sc;
	uint32_t stat;

	sc = arg;

	stat = RD1(sc, HDMI_IH_PHY_STAT0);
	if ((stat & HDMI_IH_PHY_STAT0_HPD) != 0) {
		EVENTHANDLER_INVOKE(hdmi_event, sc->sc_dev,
		    HDMI_EVENT_CONNECTED);
	}

	/* Finished with the interrupt hook */
	config_intrhook_disestablish(&sc->sc_mode_hook);
}

int
dwc_hdmi_init(device_t dev)
{
	struct dwc_hdmi_softc *sc;
	int err;

	sc = device_get_softc(dev);
	err = 0;

	sc->sc_edid = malloc(EDID_LENGTH, M_DEVBUF, M_WAITOK | M_ZERO);
	sc->sc_edid_len = EDID_LENGTH;

	device_printf(sc->sc_dev, "HDMI controller %02x:%02x:%02x:%02x\n",
	    RD1(sc, HDMI_DESIGN_ID), RD1(sc, HDMI_REVISION_ID),
	    RD1(sc, HDMI_PRODUCT_ID0), RD1(sc, HDMI_PRODUCT_ID1));

	WR1(sc, HDMI_PHY_POL0, HDMI_PHY_POL0_HPD);
	WR1(sc, HDMI_IH_PHY_STAT0, HDMI_IH_PHY_STAT0_HPD);

	sc->sc_mode_hook.ich_func = dwc_hdmi_detect_cable;
	sc->sc_mode_hook.ich_arg = sc;
	if (config_intrhook_establish(&sc->sc_mode_hook) != 0) {
		err = ENOMEM;
		goto out;
	}

out:

	if (err != 0) {
		free(sc->sc_edid, M_DEVBUF);
		sc->sc_edid = NULL;
	}

	return (err);
}

static int
dwc_hdmi_detect_hdmi_vsdb(uint8_t *edid)
{
	int off, p, btag, blen;

	if (edid[EXT_TAG] != CEA_TAG_ID)
		return (0);

	off = edid[CEA_DATA_OFF];

	/* CEA data block collection starts at byte 4 */
	if (off <= CEA_DATA_START)
		return (0);

	/* Parse the CEA data blocks */
	for (p = CEA_DATA_START; p < off;) {
		btag = BLOCK_TAG(edid[p]);
		blen = BLOCK_LEN(edid[p]);

		/* Make sure the length is sane */
		if (p + blen + 1 > off)
			break;

		/* Look for a VSDB with the HDMI 24-bit IEEE registration ID */
		if (btag == BLOCK_TAG_VSDB && blen >= HDMI_VSDB_MINLEN &&
		    memcmp(&edid[p + 1], HDMI_OUI, HDMI_OUI_LEN) == 0)
			return (1);

		/* Next data block */
		p += (1 + blen);
	}

	/* Not found */
	return (0);
}

static void
dwc_hdmi_detect_hdmi(struct dwc_hdmi_softc *sc)
{
	uint8_t *edid;
	uint32_t edid_len;
	int block;

	sc->sc_has_audio = 0;

	/* Scan through extension blocks, looking for a CEA-861 block */
	for (block = 1; block <= sc->sc_edid_info.edid_ext_block_count;
	    block++) {
		if (hdmi_edid_read(sc, block, &edid, &edid_len) != 0)
			return;
		if (dwc_hdmi_detect_hdmi_vsdb(edid) != 0) {
			if (bootverbose)
				device_printf(sc->sc_dev,
				    "enabling audio support\n");
			sc->sc_has_audio =
			    (edid[CEA_DTD] & DTD_BASIC_AUDIO) != 0;
			return;
		}
	}
}

int
dwc_hdmi_get_edid(device_t dev, uint8_t **edid, uint32_t *edid_len)
{
	struct dwc_hdmi_softc *sc;
	int error;

	sc = device_get_softc(dev);

	memset(&sc->sc_edid_info, 0, sizeof(sc->sc_edid_info));

	error = hdmi_edid_read(sc, 0, edid, edid_len);
	if (error != 0)
		return (error);

	edid_parse(*edid, &sc->sc_edid_info);

	return (0);
}

int
dwc_hdmi_set_videomode(device_t dev, const struct videomode *mode)
{
	struct dwc_hdmi_softc *sc;

	sc = device_get_softc(dev);
	memcpy(&sc->sc_mode, mode, sizeof(*mode));

	dwc_hdmi_detect_hdmi(sc);

	dwc_hdmi_set_mode(sc);

	return (0);
}
