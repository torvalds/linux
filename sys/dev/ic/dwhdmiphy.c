/* $OpenBSD: dwhdmiphy.c,v 1.3 2020/06/30 02:19:12 deraadt Exp $ */
/* $NetBSD: dw_hdmi_phy.c,v 1.2 2019/11/10 10:36:01 jmcneill Exp $ */

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

#include <sys/param.h>

#include <dev/ic/dwhdmi.h>

#define	HDMI_IH_PHY_STAT0                       0x0104
#define	  HDMI_IH_PHY_STAT0_HPD (1 << 0)
#define	HDMI_IH_I2CMPHY_STAT0                   0x0108
#define	  HDMI_IH_I2CMPHY_STAT0_DONE            (1 << 1)
#define	  HDMI_IH_I2CMPHY_STAT0_ERROR           (1 << 0)

#define	HDMI_PHY_CONF0				0x3000
#define	  HDMI_PHY_CONF0_PDZ_MASK			0x80
#define	  HDMI_PHY_CONF0_PDZ_OFFSET		7
#define	  HDMI_PHY_CONF0_ENTMDS_MASK		0x40
#define	  HDMI_PHY_CONF0_ENTMDS_OFFSET		6
#define	  HDMI_PHY_CONF0_SVSRET_MASK		0x20
#define	  HDMI_PHY_CONF0_SVSRET_OFFSET			5
#define	  HDMI_PHY_CONF0_GEN2_PDDQ_MASK		0x10
#define	  HDMI_PHY_CONF0_GEN2_PDDQ_OFFSET		4
#define	  HDMI_PHY_CONF0_GEN2_TXPWRON_MASK	0x8
#define	  HDMI_PHY_CONF0_GEN2_TXPWRON_OFFSET	3
#define	  HDMI_PHY_CONF0_GEN2_ENHPDRXSENSE_MASK	0x4
#define	  HDMI_PHY_CONF0_GEN2_ENHPDRXSENSE_OFFSET	2
#define	  HDMI_PHY_CONF0_SELDATAENPOL_MASK	0x2
#define	  HDMI_PHY_CONF0_SELDATAENPOL_OFFSET	1
#define	  HDMI_PHY_CONF0_SELDIPIF_MASK		0x1
#define	  HDMI_PHY_CONF0_SELDIPIF_OFFSET		0
#define	HDMI_PHY_TST0				0x3001
#define	  HDMI_PHY_TST0_TSTCLR_MASK		0x20
#define	  HDMI_PHY_TST0_TSTCLR_OFFSET		5
#define	  HDMI_PHY_TST0_TSTEN_MASK		0x10
#define	  HDMI_PHY_TST0_TSTEN_OFFSET		4
#define	  HDMI_PHY_TST0_TSTCLK_MASK		0x1
#define	  HDMI_PHY_TST0_TSTCLK_OFFSET		0
#define	HDMI_PHY_TST1				0x3002
#define	HDMI_PHY_TST2				0x3003
#define	HDMI_PHY_STAT0				0x3004
#define	  HDMI_PHY_STAT0_RX_SENSE3		0x80
#define	  HDMI_PHY_STAT0_RX_SENSE2		0x40
#define	  HDMI_PHY_STAT0_RX_SENSE1		0x20
#define	  HDMI_PHY_STAT0_RX_SENSE0		0x10
#define	  HDMI_PHY_STAT0_RX_SENSE		0xf0
#define	  HDMI_PHY_STAT0_HPD			0x02
#define	  HDMI_PHY_TX_PHY_LOCK			0x01
#define	HDMI_PHY_INT0				0x3005
#define	HDMI_PHY_MASK0				0x3006
#define	HDMI_PHY_POL0				0x3007
#define	  HDMI_PHY_POL0_HPD			0x02

/* HDMI Master PHY Registers */
#define	HDMI_PHY_I2CM_SLAVE_ADDR		0x3020
#define	  HDMI_PHY_I2CM_SLAVE_ADDR_PHY_GEN2	0x69
#define	  HDMI_PHY_I2CM_SLAVE_ADDR_HEAC_PHY	0x49
#define	HDMI_PHY_I2CM_ADDRESS_ADDR		0x3021
#define	HDMI_PHY_I2CM_DATAO_1_ADDR		0x3022
#define	HDMI_PHY_I2CM_DATAO_0_ADDR		0x3023
#define	HDMI_PHY_I2CM_DATAI_1_ADDR		0x3024
#define	HDMI_PHY_I2CM_DATAI_0_ADDR		0x3025
#define	HDMI_PHY_I2CM_OPERATION_ADDR		0x3026
#define	  HDMI_PHY_I2CM_OPERATION_ADDR_WRITE    0x10
#define	  HDMI_PHY_I2CM_OPERATION_ADDR_READ     0x1
#define	HDMI_PHY_I2CM_INT_ADDR			0x3027
#define	HDMI_PHY_I2CM_CTLINT_ADDR		0x3028
#define	HDMI_PHY_I2CM_DIV_ADDR			0x3029
#define	HDMI_PHY_I2CM_SOFTRSTZ_ADDR		0x302a
#define	HDMI_PHY_I2CM_SS_SCL_HCNT_1_ADDR	0x302b
#define	HDMI_PHY_I2CM_SS_SCL_HCNT_0_ADDR	0x302c
#define	HDMI_PHY_I2CM_SS_SCL_LCNT_1_ADDR	0x302d
#define	HDMI_PHY_I2CM_SS_SCL_LCNT_0_ADDR	0x302e
#define	HDMI_PHY_I2CM_FS_SCL_HCNT_1_ADDR	0x302f
#define	HDMI_PHY_I2CM_FS_SCL_HCNT_0_ADDR	0x3030
#define	HDMI_PHY_I2CM_FS_SCL_LCNT_1_ADDR	0x3031
#define	HDMI_PHY_I2CM_FS_SCL_LCNT_0_ADDR	0x3032

#define	HDMI_MC_FLOWCTRL                        0x4004
#define	  HDMI_MC_FLOWCTRL_FEED_THROUGH_OFF_MASK                0x1
#define	  HDMI_MC_FLOWCTRL_FEED_THROUGH_OFF_CSC_IN_PATH 0x1
#define	  HDMI_MC_FLOWCTRL_FEED_THROUGH_OFF_CSC_BYPASS  0x0
#define	HDMI_MC_PHYRSTZ                         0x4005
#define	  HDMI_MC_PHYRSTZ_ASSERT                        0x0
#define	  HDMI_MC_PHYRSTZ_DEASSERT              0x1
#define	HDMI_MC_HEACPHY_RST                     0x4007
#define	  HDMI_MC_HEACPHY_RST_ASSERT            0x1
#define	  HDMI_MC_HEACPHY_RST_DEASSERT          0x0

/* HDMI PHY register with access through I2C */
#define	HDMI_PHY_I2C_CKCALCTRL	0x5
#define	  CKCALCTRL_OVERRIDE	(1 << 15)
#define	HDMI_PHY_I2C_CPCE_CTRL	0x6
#define	  CPCE_CTRL_45_25		((3 << 7) | (3 << 5))
#define	  CPCE_CTRL_92_50		((2 << 7) | (2 << 5))
#define	  CPCE_CTRL_185		((1 << 7) | (1 << 5))
#define	  CPCE_CTRL_370		((0 << 7) | (0 << 5))
#define	HDMI_PHY_I2C_CKSYMTXCTRL	0x9
#define	  CKSYMTXCTRL_OVERRIDE	(1 << 15)
#define	  CKSYMTXCTRL_TX_SYMON	(1 << 3)
#define	  CKSYMTXCTRL_TX_TRAON	(1 << 2)
#define	  CKSYMTXCTRL_TX_TRBON	(1 << 1)
#define	  CKSYMTXCTRL_TX_CK_SYMON	(1 << 0)
#define	HDMI_PHY_I2C_VLEVCTRL		0x0E
#define	HDMI_PHY_I2C_CURRCTRL		0x10
#define	HDMI_PHY_I2C_PLLPHBYCTRL	0x13
#define	  VLEVCTRL_TX_LVL(x)	((x) << 5)
#define	  VLEVCTRL_CK_LVL(x)	(x)
#define	HDMI_PHY_I2C_GMPCTRL	0x15
#define	  GMPCTRL_45_25		0x00
#define	  GMPCTRL_92_50		0x05
#define	  GMPCTRL_185		0x0a
#define	  GMPCTRL_370		0x0f
#define	HDMI_PHY_I2C_MSM_CTRL	0x17
#define	  MSM_CTRL_FB_CLK		(0x3 << 1)
#define	HDMI_PHY_I2C_TXTERM	0x19
#define	  TXTERM_133		0x5

void
dwhdmi_phy_wait_i2c_done(struct dwhdmi_softc *sc, int msec)
{
	uint8_t val;

	val = dwhdmi_read(sc, HDMI_IH_I2CMPHY_STAT0) &
	    (HDMI_IH_I2CMPHY_STAT0_DONE | HDMI_IH_I2CMPHY_STAT0_ERROR);
	while (val == 0) {
		delay(1000);
		msec -= 10;
		if (msec <= 0)
			return;
		val = dwhdmi_read(sc, HDMI_IH_I2CMPHY_STAT0) &
		    (HDMI_IH_I2CMPHY_STAT0_DONE | HDMI_IH_I2CMPHY_STAT0_ERROR);
	}
}

void
dwhdmi_phy_i2c_write(struct dwhdmi_softc *sc, unsigned short data,
    unsigned char addr)
{

	/* clear DONE and ERROR flags */
	dwhdmi_write(sc, HDMI_IH_I2CMPHY_STAT0,
	    HDMI_IH_I2CMPHY_STAT0_DONE | HDMI_IH_I2CMPHY_STAT0_ERROR);
	dwhdmi_write(sc, HDMI_PHY_I2CM_ADDRESS_ADDR, addr);
	dwhdmi_write(sc, HDMI_PHY_I2CM_DATAO_1_ADDR, ((data >> 8) & 0xff));
	dwhdmi_write(sc, HDMI_PHY_I2CM_DATAO_0_ADDR, ((data >> 0) & 0xff));
	dwhdmi_write(sc, HDMI_PHY_I2CM_OPERATION_ADDR, HDMI_PHY_I2CM_OPERATION_ADDR_WRITE);
	dwhdmi_phy_wait_i2c_done(sc, 1000);
}

void
dwhdmi_phy_enable_power(struct dwhdmi_softc *sc, uint8_t enable)
{
	uint8_t reg;

	reg = dwhdmi_read(sc, HDMI_PHY_CONF0);
	reg &= ~HDMI_PHY_CONF0_PDZ_MASK;
	reg |= (enable << HDMI_PHY_CONF0_PDZ_OFFSET);
	dwhdmi_write(sc, HDMI_PHY_CONF0, reg);
}

void
dwhdmi_phy_enable_tmds(struct dwhdmi_softc *sc, uint8_t enable)
{
	uint8_t reg;

	reg = dwhdmi_read(sc, HDMI_PHY_CONF0);
	reg &= ~HDMI_PHY_CONF0_ENTMDS_MASK;
	reg |= (enable << HDMI_PHY_CONF0_ENTMDS_OFFSET);
	dwhdmi_write(sc, HDMI_PHY_CONF0, reg);
}

void
dwhdmi_phy_gen2_pddq(struct dwhdmi_softc *sc, uint8_t enable)
{
	uint8_t reg;

	reg = dwhdmi_read(sc, HDMI_PHY_CONF0);
	reg &= ~HDMI_PHY_CONF0_GEN2_PDDQ_MASK;
	reg |= (enable << HDMI_PHY_CONF0_GEN2_PDDQ_OFFSET);
	dwhdmi_write(sc, HDMI_PHY_CONF0, reg);
}

void
dwhdmi_phy_gen2_txpwron(struct dwhdmi_softc *sc, uint8_t enable)
{
	uint8_t reg;

	reg = dwhdmi_read(sc, HDMI_PHY_CONF0);
	reg &= ~HDMI_PHY_CONF0_GEN2_TXPWRON_MASK;
	reg |= (enable << HDMI_PHY_CONF0_GEN2_TXPWRON_OFFSET);
	dwhdmi_write(sc, HDMI_PHY_CONF0, reg);
}

void
dwhdmi_phy_sel_data_en_pol(struct dwhdmi_softc *sc, uint8_t enable)
{
	uint8_t reg;

	reg = dwhdmi_read(sc, HDMI_PHY_CONF0);
	reg &= ~HDMI_PHY_CONF0_SELDATAENPOL_MASK;
	reg |= (enable << HDMI_PHY_CONF0_SELDATAENPOL_OFFSET);
	dwhdmi_write(sc, HDMI_PHY_CONF0, reg);
}

void
dwhdmi_phy_sel_interface_control(struct dwhdmi_softc *sc, uint8_t enable)
{
	uint8_t reg;

	reg = dwhdmi_read(sc, HDMI_PHY_CONF0);
	reg &= ~HDMI_PHY_CONF0_SELDIPIF_MASK;
	reg |= (enable << HDMI_PHY_CONF0_SELDIPIF_OFFSET);
	dwhdmi_write(sc, HDMI_PHY_CONF0, reg);
}

void
dwhdmi_phy_enable_svsret(struct dwhdmi_softc *sc, uint8_t enable)
{
	uint8_t reg;

	reg = dwhdmi_read(sc, HDMI_PHY_CONF0);
	reg &= ~HDMI_PHY_CONF0_SVSRET_MASK;
	reg |= (enable << HDMI_PHY_CONF0_SVSRET_OFFSET);
	dwhdmi_write(sc, HDMI_PHY_CONF0, reg);
}

void
dwhdmi_phy_test_clear(struct dwhdmi_softc *sc, unsigned char bit)
{
	uint8_t val;

	val = dwhdmi_read(sc, HDMI_PHY_TST0);
	val &= ~HDMI_PHY_TST0_TSTCLR_MASK;
	val |= (bit << HDMI_PHY_TST0_TSTCLR_OFFSET) &
		HDMI_PHY_TST0_TSTCLR_MASK;
	dwhdmi_write(sc, HDMI_PHY_TST0, val);
}

int
dwhdmi_phy_configure(struct dwhdmi_softc *sc, const struct drm_display_mode *mode)
{
	const struct dwhdmi_mpll_config *mpll_conf;
	const struct dwhdmi_phy_config *phy_conf;
	uint8_t val;
	uint8_t msec;

	dwhdmi_write(sc, HDMI_MC_FLOWCTRL, HDMI_MC_FLOWCTRL_FEED_THROUGH_OFF_CSC_BYPASS);

	/* gen2 tx power off */
	dwhdmi_phy_gen2_txpwron(sc, 0);

	/* gen2 pddq */
	dwhdmi_phy_gen2_pddq(sc, 1);

	/* PHY reset */
	dwhdmi_write(sc, HDMI_MC_PHYRSTZ, HDMI_MC_PHYRSTZ_DEASSERT);
	dwhdmi_write(sc, HDMI_MC_PHYRSTZ, HDMI_MC_PHYRSTZ_ASSERT);

	dwhdmi_write(sc, HDMI_MC_HEACPHY_RST, HDMI_MC_HEACPHY_RST_ASSERT);

	dwhdmi_phy_test_clear(sc, 1);
	dwhdmi_write(sc, HDMI_PHY_I2CM_SLAVE_ADDR, HDMI_PHY_I2CM_SLAVE_ADDR_PHY_GEN2);
	dwhdmi_phy_test_clear(sc, 0);

	/*
	 * Following initialization are for 8bit per color case
	 */

	/*
	 * PLL/MPLL config
	 */
	for (mpll_conf = &sc->sc_mpll_config[0]; mpll_conf->pixel_clock != 0; mpll_conf++)
		if (mode->clock <= mpll_conf->pixel_clock)
			break;

	dwhdmi_phy_i2c_write(sc, mpll_conf->cpce, HDMI_PHY_I2C_CPCE_CTRL);
	dwhdmi_phy_i2c_write(sc, mpll_conf->gmp, HDMI_PHY_I2C_GMPCTRL);
	dwhdmi_phy_i2c_write(sc, mpll_conf->curr, HDMI_PHY_I2C_CURRCTRL);

	for (phy_conf = &sc->sc_phy_config[0]; phy_conf->pixel_clock != 0; phy_conf++)
		if (mode->clock <= phy_conf->pixel_clock)
			break;

	dwhdmi_phy_i2c_write(sc, 0x0000, HDMI_PHY_I2C_PLLPHBYCTRL);
	dwhdmi_phy_i2c_write(sc, MSM_CTRL_FB_CLK, HDMI_PHY_I2C_MSM_CTRL);

	dwhdmi_phy_i2c_write(sc, phy_conf->term, HDMI_PHY_I2C_TXTERM);
	dwhdmi_phy_i2c_write(sc, phy_conf->sym, HDMI_PHY_I2C_CKSYMTXCTRL);
	dwhdmi_phy_i2c_write(sc, phy_conf->vlev, HDMI_PHY_I2C_VLEVCTRL);

	/* REMOVE CLK TERM */
	dwhdmi_phy_i2c_write(sc, CKCALCTRL_OVERRIDE, HDMI_PHY_I2C_CKCALCTRL);

	dwhdmi_phy_enable_power(sc, 1);

	/* toggle TMDS enable */
	dwhdmi_phy_enable_tmds(sc, 0);
	dwhdmi_phy_enable_tmds(sc, 1);

	/* gen2 tx power on */
	dwhdmi_phy_gen2_txpwron(sc, 1);
	dwhdmi_phy_gen2_pddq(sc, 0);

	switch (sc->sc_phytype) {
	case 0xb2:	/* MHL PHY HEAC */
	case 0xc2:	/* MHL PHY */
	case 0xf3:	/* HDMI 2.0 TX PHY */
		dwhdmi_phy_enable_svsret(sc, 1);
		break;
	}

	/*Wait for PHY PLL lock */
	msec = 4;
	val = dwhdmi_read(sc, HDMI_PHY_STAT0) & HDMI_PHY_TX_PHY_LOCK;
	while (val == 0) {
		delay(1000);
		if (msec-- == 0) {
			printf("%s: PHY PLL not locked\n",
			    sc->sc_dev.dv_xname);
			return (-1);
		}
		val = dwhdmi_read(sc, HDMI_PHY_STAT0) & HDMI_PHY_TX_PHY_LOCK;
	}

	return (0);
}

void
dwhdmi_phy_init(struct dwhdmi_softc *sc, const struct drm_display_mode *mode)
{
	int i;

	/* HDMI Phy spec says to do the phy initialization sequence twice */
	for (i = 0 ; i < 2 ; i++) {
		dwhdmi_phy_sel_data_en_pol(sc, 1);
		dwhdmi_phy_sel_interface_control(sc, 0);
		dwhdmi_phy_enable_tmds(sc, 0);
		dwhdmi_phy_enable_power(sc, 0);

		/* Enable CSC */
		dwhdmi_phy_configure(sc, mode);
	}
}

enum drm_connector_status
dwhdmi_phy_detect(struct dwhdmi_softc *sc, int force)
{
	uint8_t val;

	val = dwhdmi_read(sc, HDMI_PHY_STAT0);

	return ((val & HDMI_PHY_STAT0_HPD) != 0) ?
	    connector_status_connected :
	    connector_status_disconnected;
}

void
dwhdmi_phy_enable(struct dwhdmi_softc *sc)
{
}

void
dwhdmi_phy_disable(struct dwhdmi_softc *sc)
{
}

void
dwhdmi_phy_mode_set(struct dwhdmi_softc *sc,
    const struct drm_display_mode *mode, const struct drm_display_mode *adjusted_mode)
{
	dwhdmi_phy_init(sc, adjusted_mode);
}
