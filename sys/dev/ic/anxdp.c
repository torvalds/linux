/* $OpenBSD: anxdp.c,v 1.7 2023/01/01 01:34:33 jsg Exp $ */
/* $NetBSD: anx_dp.c,v 1.2 2020/01/04 12:08:32 jmcneill Exp $ */
/*-
 * Copyright (c) 2019 Jonathan A. Kollasch <jakllsch@kollasch.net>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <machine/bus.h>

#include <dev/ic/anxdp.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/display/drm_dp_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_edid.h>

#define	ANXDP_DP_TX_VERSION	0x010
#define	ANXDP_TX_SW_RESET	0x014
#define	 RESET_DP_TX			(1 << 0)
#define	ANXDP_FUNC_EN_1		0x018
#define	 MASTER_VID_FUNC_EN_N		(1 << 7)
#define	 RK_VID_CAP_FUNC_EN_N		(1 << 6)
#define	 SLAVE_VID_FUNC_EN_N		(1 << 5)
#define	 RK_VID_FIFO_FUNC_EN_N		(1 << 5)
#define	 AUD_FIFO_FUNC_EN_N		(1 << 4)
#define	 AUD_FUNC_EN_N			(1 << 3)
#define	 HDCP_FUNC_EN_N			(1 << 2)
#define	 CRC_FUNC_EN_N			(1 << 1)
#define	 SW_FUNC_EN_N			(1 << 0)
#define	ANXDP_FUNC_EN_2		0x01c
#define	 SSC_FUNC_EN_N			(1 << 7)
#define	 AUX_FUNC_EN_N			(1 << 2)
#define	 SERDES_FIFO_FUNC_EN_N		(1 << 1)
#define	 LS_CLK_DOMAIN_FUNC_EN_N	(1 << 0)
#define	ANXDP_VIDEO_CTL_1	0x020
#define	 VIDEO_EN			(1 << 7)
#define	 VIDEO_MUTE			(1 << 6)
#define	ANXDP_VIDEO_CTL_2	0x024
#define	ANXDP_VIDEO_CTL_3	0x028
#define	ANXDP_VIDEO_CTL_4	0x02c
#define	ANXDP_VIDEO_CTL_8	0x03c
#define	ANXDP_VIDEO_CTL_10	0x044
#define	 F_SEL				(1 << 4)
#define	 SLAVE_I_SCAN_CFG		(1 << 2)
#define	 SLAVE_VSYNC_P_CFG		(1 << 1)
#define	 SLAVE_HSYNC_P_CFG		(1 << 0)
#define	ANXDP_PLL_REG_1		0x0fc
#define	 REF_CLK_24M			(1 << 0)
#define	RKANXDP_PD		0x12c
#define	 DP_INC_BG			(1 << 7)
#define	 DP_EXP_PD			(1 << 6)
#define	 DP_PHY_PD			(1 << 5)
#define	 RK_AUX_PD			(1 << 5)
#define	 AUX_PD				(1 << 4)
#define	 RK_PLL_PD			(1 << 4)
#define	 CHx_PD(x)			(1 << x)	/* 0<=x<=3 */
#define  DP_ALL_PD			0xff
#define	ANXDP_LANE_MAP		0x35c
#define ANXDP_ANALOG_CTL_1	0x370
#define	 TX_TERMINAL_CTRL_50_OHM	(1 << 4)
#define ANXDP_ANALOG_CTL_2	0x374
#define	 SEL_24M			(1 << 3)
#define	 TX_DVDD_BIT_1_0625V		0x4
#define ANXDP_ANALOG_CTL_3	0x378
#define	 DRIVE_DVDD_BIT_1_0625V		(0x4 << 5)
#define	 VCO_BIT_600_MICRO		(0x5 << 0)
#define ANXDP_PLL_FILTER_CTL_1	0x37c
#define	 PD_RING_OSC			(1 << 6)
#define	 AUX_TERMINAL_CTRL_50_OHM	(2 << 4)
#define	 TX_CUR1_2X			(1 << 2)
#define	 TX_CUR_16_MA			3
#define ANXDP_TX_AMP_TUNING_CTL	0x380
#define	ANXDP_AUX_HW_RETRY_CTL	0x390
#define	 AUX_BIT_PERIOD_EXPECTED_DELAY(x) ((x) << 8)
#define	 AUX_HW_RETRY_INTERVAL_600_US	(0 << 3)
#define	 AUX_HW_RETRY_INTERVAL_800_US	(1 << 3)
#define	 AUX_HW_RETRY_INTERVAL_1000_US	(2 << 3)
#define	 AUX_HW_RETRY_INTERVAL_1800_US	(3 << 3)
#define	 AUX_HW_RETRY_COUNT_SEL(x)	((x) << 0)
#define	ANXDP_COMMON_INT_STA_1	0x3c4
#define	 PLL_LOCK_CHG			(1 << 6)
#define	ANXDP_COMMON_INT_STA_2	0x3c8
#define	ANXDP_COMMON_INT_STA_3	0x3cc
#define	ANXDP_COMMON_INT_STA_4	0x3d0
#define	ANXDP_DP_INT_STA	0x3dc
#define	 INT_HPD			(1 << 6)
#define	 HW_TRAINING_FINISH		(1 << 5)
#define	 RPLY_RECEIV			(1 << 1)
#define	 AUX_ERR			(1 << 0)
#define	ANXDP_SYS_CTL_1		0x600
#define	 DET_STA			(1 << 2)
#define	 FORCE_DET			(1 << 1)
#define	 DET_CTRL			(1 << 0)
#define	ANXDP_SYS_CTL_2		0x604
#define	ANXDP_SYS_CTL_3		0x608
#define	 HPD_STATUS			(1 << 6)
#define	 F_HPD				(1 << 5)
#define	 HPD_CTRL			(1 << 4)
#define	 HDCP_RDY			(1 << 3)
#define	 STRM_VALID			(1 << 2)
#define	 F_VALID			(1 << 1)
#define	 VALID_CTRL			(1 << 0)
#define	ANXDP_SYS_CTL_4		0x60c
#define	ANXDP_PKT_SEND_CTL	0x640
#define	ANXDP_HDCP_CTL		0x648
#define	ANXDP_LINK_BW_SET	0x680
#define	ANXDP_LANE_COUNT_SET	0x684
#define	ANXDP_TRAINING_PTN_SET	0x688
#define	 SCRAMBLING_DISABLE		(1 << 5)
#define	 SW_TRAINING_PATTERN_SET_PTN2	(2 << 0)
#define	 SW_TRAINING_PATTERN_SET_PTN1	(1 << 0)
#define	ANXDP_LNx_LINK_TRAINING_CTL(x) (0x68c + 4 * (x)) /* 0 <= x <= 3 */
#define	 MAX_PRE_REACH			(1 << 5)
#define  PRE_EMPHASIS_SET(x)		((x) << 3)
#define	 MAX_DRIVE_REACH		(1 << 2)
#define  DRIVE_CURRENT_SET(x)		((x) << 0)
#define	ANXDP_DEBUG_CTL		0x6c0
#define	 PLL_LOCK			(1 << 4)
#define	 F_PLL_LOCK			(1 << 3)
#define	 PLL_LOCK_CTRL			(1 << 2)
#define	 PN_INV				(1 << 0)
#define	ANXDP_LINK_DEBUG_CTL	0x6e0
#define	ANXDP_PLL_CTL		0x71c
#define	ANXDP_PHY_PD		0x720
#define	ANXDP_PHY_TEST		0x724
#define	 MACRO_RST			(1 << 5)
#define ANXDP_M_AUD_GEN_FILTER_TH 0x778
#define	ANXDP_AUX_CH_STA	0x780
#define	 AUX_BUSY			(1 << 4)
#define	 AUX_STATUS(x)			(((x) >> 0) & 0xf)
#define	ANXDP_AUX_ERR_NUM	0x784
#define	ANXDP_AUX_CH_DEFER_CTL	0x788
#define	 DEFER_CTRL_EN			(1 << 7)
#define	 DEFER_COUNT(x)			((x) << 0)
#define	ANXDP_AUX_RX_COMM	0x78c
#define	 AUX_RX_COMM_I2C_DEFER		(1 << 3)
#define	 AUX_RX_COMM_AUX_DEFER		(1 << 1)
#define	ANXDP_BUFFER_DATA_CTL	0x790
#define	 BUF_CLR			(1 << 7)
#define	 BUF_DATA_COUNT(x)		((x) << 0)
#define	ANXDP_AUX_CH_CTL_1	0x794
#define	 AUX_LENGTH(x)			(((x) - 1) << 4)
#define	 AUX_TX_COMM(x)			(((x) >> 0) & 0xf)
#define	 AUX_TX_COMM_DP			(1 << 3)
#define	 AUX_TX_COMM_MOT		(1 << 2)
#define	 AUX_TX_COMM_READ		(1 << 0)
#define	ANXDP_AUX_ADDR_7_0	0x798
#define	 AUX_ADDR_7_0(x)	 (((x) >> 0) & 0xff)
#define	ANXDP_AUX_ADDR_15_8	0x79c
#define	 AUX_ADDR_15_8(x)	 (((x) >> 8) & 0xff)
#define	ANXDP_AUX_ADDR_19_16	0x7a0
#define	 AUX_ADDR_19_16(x)	 (((x) >> 16) & 0xf)
#define	ANXDP_AUX_CH_CTL_2	0x7a4
#define	 ADDR_ONLY			(1 << 1)
#define	 AUX_EN				(1 << 0)
#define	ANXDP_BUF_DATA(x)	(0x7c0 + 4 * (x))
#define	ANXDP_SOC_GENERAL_CTL	0x800
#define	 AUDIO_MODE_SPDIF_MODE		(1 << 8)
#define	 VIDEO_MODE_SLAVE_MODE		(1 << 1)
#define	ANXDP_CRC_CON		0x890
#define	ANXDP_PLL_REG_2		0x9e4
#define	ANXDP_PLL_REG_3		0x9e8
#define	ANXDP_PLL_REG_4		0x9ec
#define	ANXDP_PLL_REG_5		0xa00

static inline const bool
isrockchip(struct anxdp_softc *sc)
{
	return (sc->sc_flags & ANXDP_FLAG_ROCKCHIP) != 0;
}

enum drm_connector_status
anxdp_connector_detect(struct drm_connector *connector, bool force)
{
#if 0
	struct anxdp_connector *anxdp_connector = to_anxdp_connector(connector);
	struct anxdp_softc *sc = anxdp_connector->sc;

	/* XXX HPD */
#endif
	return connector_status_connected;
}

void
anxdp_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

struct drm_connector_funcs anxdp_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = anxdp_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = anxdp_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

void
anxdp_analog_power_up_all(struct anxdp_softc *sc)
{
	const bus_size_t pd_reg = isrockchip(sc) ? RKANXDP_PD : ANXDP_PHY_PD;

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, pd_reg, DP_ALL_PD);
	delay(15);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, pd_reg,
	    DP_ALL_PD & ~DP_INC_BG);
	delay(15);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, pd_reg, 0);
}

int
anxdp_await_pll_lock(struct anxdp_softc *sc)
{
	u_int timeout;

	for (timeout = 0; timeout < 100; timeout++) {
		if ((bus_space_read_4(sc->sc_iot, sc->sc_ioh, ANXDP_DEBUG_CTL) &
		    PLL_LOCK) != 0)
			return 0;
		delay(20);
	}

	return ETIMEDOUT;
}

void
anxdp_init_hpd(struct anxdp_softc *sc)
{
	uint32_t sc3;

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_COMMON_INT_STA_4, 0x7);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_DP_INT_STA, INT_HPD);

	sc3 = bus_space_read_4(sc->sc_iot, sc->sc_ioh, ANXDP_SYS_CTL_3);
	sc3 &= ~(F_HPD | HPD_CTRL);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_SYS_CTL_3, sc3);

	sc3 = bus_space_read_4(sc->sc_iot, sc->sc_ioh, ANXDP_SYS_CTL_3);
	sc3 |= F_HPD | HPD_CTRL;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_SYS_CTL_3, sc3);
}

void
anxdp_init_aux(struct anxdp_softc *sc)
{
	uint32_t fe2, pd, hrc;
	const bus_size_t pd_reg = isrockchip(sc) ? RKANXDP_PD : ANXDP_PHY_PD;
	const uint32_t pd_mask = isrockchip(sc) ? RK_AUX_PD : AUX_PD;

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_DP_INT_STA,
	    RPLY_RECEIV | AUX_ERR);

	pd = bus_space_read_4(sc->sc_iot, sc->sc_ioh, pd_reg);
	pd |= pd_mask;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, pd_reg, pd);

	delay(11);

	pd = bus_space_read_4(sc->sc_iot, sc->sc_ioh, pd_reg);
	pd &= ~pd_mask;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, pd_reg, pd);

	fe2 = bus_space_read_4(sc->sc_iot, sc->sc_ioh, ANXDP_FUNC_EN_2);
	fe2 |= AUX_FUNC_EN_N;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_FUNC_EN_2, fe2);

	hrc = AUX_HW_RETRY_COUNT_SEL(0) | AUX_HW_RETRY_INTERVAL_600_US;
	if (!isrockchip(sc))
		hrc |= AUX_BIT_PERIOD_EXPECTED_DELAY(3);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_AUX_HW_RETRY_CTL, hrc);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_AUX_CH_DEFER_CTL,
	    DEFER_CTRL_EN | DEFER_COUNT(1));

	fe2 = bus_space_read_4(sc->sc_iot, sc->sc_ioh, ANXDP_FUNC_EN_2);
	fe2 &= ~AUX_FUNC_EN_N;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_FUNC_EN_2, fe2);
}

int
anxdp_connector_get_modes(struct drm_connector *connector)
{
	struct anxdp_connector *anxdp_connector = to_anxdp_connector(connector);
	struct anxdp_softc *sc = anxdp_connector->sc;
	struct edid *pedid = NULL;
	int error;

	if (sc->sc_panel)
		return drm_panel_get_modes(sc->sc_panel, connector);

	pedid = drm_get_edid(connector, &sc->sc_dpaux.ddc);

	drm_connector_update_edid_property(connector, pedid);
	if (pedid == NULL)
		return 0;

	error = drm_add_edid_modes(connector, pedid);

	if (pedid != NULL)
		kfree(pedid);

	return error;
}

struct drm_connector_helper_funcs anxdp_connector_helper_funcs = {
	.get_modes = anxdp_connector_get_modes,
};

int
anxdp_bridge_attach(struct drm_bridge *bridge,
    enum drm_bridge_attach_flags flags)
{
	struct anxdp_softc *sc = bridge->driver_private;
	struct anxdp_connector *anxdp_connector = &sc->sc_connector;
	struct drm_connector *connector = &anxdp_connector->base;
	int error;

	anxdp_connector->sc = sc;

	connector->polled =
	    DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;
	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	drm_connector_init(bridge->dev, connector, &anxdp_connector_funcs,
	    connector->connector_type);
	drm_connector_helper_add(connector, &anxdp_connector_helper_funcs);

	error = drm_connector_attach_encoder(connector, bridge->encoder);
	if (error != 0)
		return error;

	return drm_connector_register(connector);
}

void
anxdp_macro_reset(struct anxdp_softc *sc)
{
	uint32_t val;

	val = bus_space_read_4(sc->sc_iot, sc->sc_ioh, ANXDP_PHY_TEST);
	val |= MACRO_RST;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_PHY_TEST, val);
	delay(10);
	val &= ~MACRO_RST;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_PHY_TEST, val);
}

int
anxdp_link_configure(struct anxdp_softc *sc)
{
	uint8_t values[2];
	int error;

	values[0] = drm_dp_link_rate_to_bw_code(sc->sc_link_rate);
	values[1] = sc->sc_num_lanes;

	if (sc->sc_dpcd[2] & DP_ENHANCED_FRAME_CAP)
		values[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;

	error = drm_dp_dpcd_write(&sc->sc_dpaux, DP_LINK_BW_SET,
	    values, sizeof(values));
	if (error < 0)
		return error;

	return 0;
}

int
anxdp_link_power_up(struct anxdp_softc *sc)
{
	uint8_t value;
	int error;

	if (sc->sc_dpcd[0] < 0x11)
		return 0;

	error = drm_dp_dpcd_readb(&sc->sc_dpaux, DP_SET_POWER, &value);
	if (error < 0)
		return error;

	value &= ~DP_SET_POWER_MASK;
	value |= DP_SET_POWER_D0;

	error = drm_dp_dpcd_writeb(&sc->sc_dpaux, DP_SET_POWER, value);
	if (error < 0)
		return error;

	delay(1000);
	return 0;
}

void
anxdp_link_start(struct anxdp_softc *sc)
{
	uint8_t training[4];
	uint32_t val;

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_LINK_BW_SET,
	    drm_dp_link_rate_to_bw_code(sc->sc_link_rate));
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_LANE_COUNT_SET,
	    sc->sc_num_lanes);
	if (anxdp_link_configure(sc))
		return;

	for (u_int i = 0; i < sc->sc_num_lanes; i++) {
		val = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    ANXDP_LNx_LINK_TRAINING_CTL(i));
		val &= ~(PRE_EMPHASIS_SET(3)|DRIVE_CURRENT_SET(3));
		val |= PRE_EMPHASIS_SET(0);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    ANXDP_LNx_LINK_TRAINING_CTL(i), val);
	}

	if (anxdp_await_pll_lock(sc) != 0) {
		printf("%s: PLL lock timeout\n", sc->sc_dev.dv_xname);
	}

	for (u_int i = 0; i < sc->sc_num_lanes; i++) {
		training[i] = DP_TRAIN_PRE_EMPH_LEVEL_0 |
		    DP_TRAIN_VOLTAGE_SWING_LEVEL_0;
	}

	drm_dp_dpcd_write(&sc->sc_dpaux, DP_TRAINING_LANE0_SET, training,
	    sc->sc_num_lanes);
}

void
anxdp_process_clock_recovery(struct anxdp_softc *sc)
{
	u_int i, tries;
	uint8_t link_status[DP_LINK_STATUS_SIZE];
	uint8_t training[4];

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_TRAINING_PTN_SET,
	    SCRAMBLING_DISABLE | SW_TRAINING_PATTERN_SET_PTN1);
	drm_dp_dpcd_writeb(&sc->sc_dpaux, DP_TRAINING_PATTERN_SET,
	    DP_LINK_SCRAMBLING_DISABLE | DP_TRAINING_PATTERN_1);

	tries = 0;
again:
	if (tries++ >= 10) {
		printf("%s: cr fail\n", sc->sc_dev.dv_xname);
		return;
	}
	drm_dp_link_train_clock_recovery_delay(&sc->sc_dpaux, sc->sc_dpcd);
	if (DP_LINK_STATUS_SIZE !=
	    drm_dp_dpcd_read_link_status(&sc->sc_dpaux, link_status)) {
		return;
	}
	if (!drm_dp_clock_recovery_ok(link_status, sc->sc_num_lanes)) {
		goto cr_fail;
	}

	return;

cr_fail:
	for (i = 0; i < sc->sc_num_lanes; i++) {
		uint8_t vs, pe;
		vs = drm_dp_get_adjust_request_voltage(link_status, i);
		pe = drm_dp_get_adjust_request_pre_emphasis(link_status, i);
		training[i] = vs | pe;
	}
	for (i = 0; i < sc->sc_num_lanes; i++) {
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    ANXDP_LNx_LINK_TRAINING_CTL(i), training[i]);
	}
	drm_dp_dpcd_write(&sc->sc_dpaux, DP_TRAINING_LANE0_SET, training,
	    sc->sc_num_lanes);
	goto again;
}

void
anxdp_process_eq(struct anxdp_softc *sc)
{
	u_int i, tries;
	uint8_t link_status[DP_LINK_STATUS_SIZE];
	uint8_t training[4];

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_TRAINING_PTN_SET,
	    SCRAMBLING_DISABLE | SW_TRAINING_PATTERN_SET_PTN2);
	drm_dp_dpcd_writeb(&sc->sc_dpaux, DP_TRAINING_PATTERN_SET,
	    DP_LINK_SCRAMBLING_DISABLE | DP_TRAINING_PATTERN_2);

	tries = 0;
again:
	if (tries++ >= 10) {
		printf("%s: eq fail\n", sc->sc_dev.dv_xname);
		return;
	}
	drm_dp_link_train_channel_eq_delay(&sc->sc_dpaux, sc->sc_dpcd);
	if (DP_LINK_STATUS_SIZE !=
	    drm_dp_dpcd_read_link_status(&sc->sc_dpaux, link_status)) {
		return;
	}
	if (!drm_dp_channel_eq_ok(link_status, sc->sc_num_lanes)) {
		goto eq_fail;
	}

	return;

eq_fail:
	for (i = 0; i < sc->sc_num_lanes; i++) {
		uint8_t vs, pe;
		vs = drm_dp_get_adjust_request_voltage(link_status, i);
		pe = drm_dp_get_adjust_request_pre_emphasis(link_status, i);
		training[i] = vs | pe;
	}
	for (i = 0; i < sc->sc_num_lanes; i++) {
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    ANXDP_LNx_LINK_TRAINING_CTL(i), training[i]);
	}
	drm_dp_dpcd_write(&sc->sc_dpaux, DP_TRAINING_LANE0_SET, training,
	    sc->sc_num_lanes);
	goto again;
}

void
anxdp_train_link(struct anxdp_softc *sc)
{
	anxdp_macro_reset(sc);

	if (DP_RECEIVER_CAP_SIZE != drm_dp_dpcd_read(&sc->sc_dpaux,
	    DP_DPCD_REV, sc->sc_dpcd, DP_RECEIVER_CAP_SIZE)) {
		printf("%s: link probe failed\n", sc->sc_dev.dv_xname);
		return;
	}

	sc->sc_link_rate = drm_dp_bw_code_to_link_rate(sc->sc_dpcd[1]);
	sc->sc_num_lanes = sc->sc_dpcd[2] & DP_MAX_LANE_COUNT_MASK;

	if (anxdp_link_power_up(sc))
		return;

	anxdp_link_start(sc);
	anxdp_process_clock_recovery(sc);
	anxdp_process_eq(sc);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_TRAINING_PTN_SET, 0);
	drm_dp_dpcd_writeb(&sc->sc_dpaux, DP_TRAINING_PATTERN_SET,
	    DP_TRAINING_PATTERN_DISABLE);
}

void
anxdp_bringup(struct anxdp_softc *sc)
{
	uint32_t val;

	val = bus_space_read_4(sc->sc_iot, sc->sc_ioh, ANXDP_VIDEO_CTL_1);
	val &= ~VIDEO_EN;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_VIDEO_CTL_1, val);

	val = bus_space_read_4(sc->sc_iot, sc->sc_ioh, ANXDP_VIDEO_CTL_1);
	val &= ~VIDEO_MUTE;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_VIDEO_CTL_1, val);

	val = SW_FUNC_EN_N;
	if (isrockchip(sc)) {
		val |= RK_VID_CAP_FUNC_EN_N | RK_VID_FIFO_FUNC_EN_N;
	} else {
		val |= MASTER_VID_FUNC_EN_N | SLAVE_VID_FUNC_EN_N |
		    AUD_FIFO_FUNC_EN_N | AUD_FUNC_EN_N | HDCP_FUNC_EN_N;
	}
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_FUNC_EN_1, val);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_FUNC_EN_2,
	    SSC_FUNC_EN_N | AUX_FUNC_EN_N | SERDES_FIFO_FUNC_EN_N |
	    LS_CLK_DOMAIN_FUNC_EN_N);

	delay(30);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_M_AUD_GEN_FILTER_TH, 2);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_SOC_GENERAL_CTL, 0x101);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_TX_SW_RESET,
	    RESET_DP_TX);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_ANALOG_CTL_1,
	    TX_TERMINAL_CTRL_50_OHM);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_ANALOG_CTL_2,
	    SEL_24M | TX_DVDD_BIT_1_0625V);
	if (isrockchip(sc)) {
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_PLL_REG_1,
		    REF_CLK_24M);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_PLL_REG_2,
		    0x95);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_PLL_REG_3,
		    0x40);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_PLL_REG_4,
		    0x58);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_PLL_REG_5,
		    0x22);
	}
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_ANALOG_CTL_3,
	    DRIVE_DVDD_BIT_1_0625V | VCO_BIT_600_MICRO);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_PLL_FILTER_CTL_1,
	    PD_RING_OSC | AUX_TERMINAL_CTRL_50_OHM | TX_CUR1_2X | TX_CUR_16_MA);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_TX_AMP_TUNING_CTL, 0);

	val = bus_space_read_4(sc->sc_iot, sc->sc_ioh, ANXDP_FUNC_EN_1);
	val &= ~SW_FUNC_EN_N;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_FUNC_EN_1, val);

	anxdp_analog_power_up_all(sc);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_COMMON_INT_STA_1,
	    PLL_LOCK_CHG);

	val = bus_space_read_4(sc->sc_iot, sc->sc_ioh, ANXDP_DEBUG_CTL);
	val &= ~(F_PLL_LOCK | PLL_LOCK_CTRL);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_DEBUG_CTL, val);

	if (anxdp_await_pll_lock(sc) != 0) {
		printf("%s: PLL lock timeout\n", sc->sc_dev.dv_xname);
	}

	val = bus_space_read_4(sc->sc_iot, sc->sc_ioh, ANXDP_FUNC_EN_2);
	val &= ~(SERDES_FIFO_FUNC_EN_N | LS_CLK_DOMAIN_FUNC_EN_N |
	    AUX_FUNC_EN_N);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_FUNC_EN_2, val);

	anxdp_init_hpd(sc);
	anxdp_init_aux(sc);
}

void
anxdp_bridge_enable(struct drm_bridge *bridge)
{
	struct anxdp_softc *sc = bridge->driver_private;
	uint32_t val;

	val = bus_space_read_4(sc->sc_iot, sc->sc_ioh, ANXDP_FUNC_EN_1);
	if (isrockchip(sc)) {
		val &= ~(RK_VID_CAP_FUNC_EN_N | RK_VID_FIFO_FUNC_EN_N);
	} else {
		val &= ~(MASTER_VID_FUNC_EN_N | SLAVE_VID_FUNC_EN_N);
		val |= MASTER_VID_FUNC_EN_N;
	}
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_FUNC_EN_1, val);

	val = bus_space_read_4(sc->sc_iot, sc->sc_ioh, ANXDP_VIDEO_CTL_10);
	val &= ~(SLAVE_I_SCAN_CFG|SLAVE_VSYNC_P_CFG|SLAVE_HSYNC_P_CFG);
	if ((sc->sc_curmode.flags & DRM_MODE_FLAG_INTERLACE) != 0)
		val |= SLAVE_I_SCAN_CFG;
	if ((sc->sc_curmode.flags & DRM_MODE_FLAG_NVSYNC) != 0)
		val |= SLAVE_VSYNC_P_CFG;
	if ((sc->sc_curmode.flags & DRM_MODE_FLAG_NHSYNC) != 0)
		val |= SLAVE_HSYNC_P_CFG;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_VIDEO_CTL_10, val);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_SOC_GENERAL_CTL,
	    AUDIO_MODE_SPDIF_MODE | VIDEO_MODE_SLAVE_MODE);

	anxdp_train_link(sc);

	val = bus_space_read_4(sc->sc_iot, sc->sc_ioh, ANXDP_VIDEO_CTL_1);
	val |= VIDEO_EN;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_VIDEO_CTL_1, val);

	if (sc->sc_panel != NULL &&
	    sc->sc_panel->funcs != NULL &&
	    sc->sc_panel->funcs->enable != NULL)
		sc->sc_panel->funcs->enable(sc->sc_panel);
}

void
anxdp_bridge_pre_enable(struct drm_bridge *bridge)
{
}

void
anxdp_bridge_disable(struct drm_bridge *bridge)
{
}

void
anxdp_bridge_post_disable(struct drm_bridge *bridge)
{
}

void
anxdp_bridge_mode_set(struct drm_bridge *bridge,
    const struct drm_display_mode *mode,
    const struct drm_display_mode *adjusted_mode)
{
	struct anxdp_softc *sc = bridge->driver_private;

	sc->sc_curmode = *adjusted_mode;
}

bool
anxdp_bridge_mode_fixup(struct drm_bridge *bridge,
    const struct drm_display_mode *mode,
    struct drm_display_mode *adjusted_mode)
{
	return true;
}

const struct drm_bridge_funcs anxdp_bridge_funcs = {
	.attach = anxdp_bridge_attach,
	.enable = anxdp_bridge_enable,
	.pre_enable = anxdp_bridge_pre_enable,
	.disable = anxdp_bridge_disable,
	.post_disable = anxdp_bridge_post_disable,
	.mode_set = anxdp_bridge_mode_set,
	.mode_fixup = anxdp_bridge_mode_fixup,
};

ssize_t
anxdp_dp_aux_transfer(struct drm_dp_aux *dpaux, struct drm_dp_aux_msg *dpmsg)
{
	struct anxdp_softc *sc = container_of(dpaux, struct anxdp_softc,
	    sc_dpaux);
	size_t loop_timeout = 0;
	uint32_t val;
	size_t i;
	ssize_t ret = 0;

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_BUFFER_DATA_CTL,
	    BUF_CLR);

	val = AUX_LENGTH(dpmsg->size);
	if ((dpmsg->request & DP_AUX_I2C_MOT) != 0)
		val |= AUX_TX_COMM_MOT;

	switch (dpmsg->request & ~DP_AUX_I2C_MOT) {
	case DP_AUX_I2C_WRITE:
		break;
	case DP_AUX_I2C_READ:
		val |= AUX_TX_COMM_READ;
		break;
	case DP_AUX_NATIVE_WRITE:
		val |= AUX_TX_COMM_DP;
		break;
	case DP_AUX_NATIVE_READ:
		val |= AUX_TX_COMM_READ | AUX_TX_COMM_DP;
		break;
	}

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_AUX_CH_CTL_1, val);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_AUX_ADDR_7_0,
	    AUX_ADDR_7_0(dpmsg->address));
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_AUX_ADDR_15_8,
	    AUX_ADDR_15_8(dpmsg->address));
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_AUX_ADDR_19_16,
	    AUX_ADDR_19_16(dpmsg->address));

	if (!(dpmsg->request & DP_AUX_I2C_READ)) {
		for (i = 0; i < dpmsg->size; i++) {
			bus_space_write_4(sc->sc_iot, sc->sc_ioh,
			    ANXDP_BUF_DATA(i),
			    ((const uint8_t *)(dpmsg->buffer))[i]);
			ret++;
		}
	}


	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_AUX_CH_CTL_2,
	    AUX_EN | ((dpmsg->size == 0) ? ADDR_ONLY : 0));

	loop_timeout = 0;
	val = bus_space_read_4(sc->sc_iot, sc->sc_ioh, ANXDP_AUX_CH_CTL_2);
	while ((val & AUX_EN) != 0) {
		if (++loop_timeout > 20000) {
			ret = -ETIMEDOUT;
			goto out;
		}
		delay(25);
		val = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    ANXDP_AUX_CH_CTL_2);
	}

	loop_timeout = 0;
	val = bus_space_read_4(sc->sc_iot, sc->sc_ioh, ANXDP_DP_INT_STA);
	while (!(val & RPLY_RECEIV)) {
		if (++loop_timeout > 2000) {
			ret = -ETIMEDOUT;
			goto out;
		}
		delay(10);
		val = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    ANXDP_DP_INT_STA);
	}

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_DP_INT_STA,
	    RPLY_RECEIV);

	val = bus_space_read_4(sc->sc_iot, sc->sc_ioh, ANXDP_DP_INT_STA);
	if ((val & AUX_ERR) != 0) {
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, ANXDP_DP_INT_STA,
		    AUX_ERR);
		ret = -EREMOTEIO;
		goto out;
	}

	val = bus_space_read_4(sc->sc_iot, sc->sc_ioh, ANXDP_AUX_CH_STA);
	if (AUX_STATUS(val) != 0) {
		ret = -EREMOTEIO;
		goto out;
	}

	if ((dpmsg->request & DP_AUX_I2C_READ)) {
		for (i = 0; i < dpmsg->size; i++) {
			val = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
			    ANXDP_BUF_DATA(i));
			((uint8_t *)(dpmsg->buffer))[i] = val & 0xffU;
			ret++;
		}
	}

	val = bus_space_read_4(sc->sc_iot, sc->sc_ioh, ANXDP_AUX_RX_COMM);
	if (val == AUX_RX_COMM_AUX_DEFER)
		dpmsg->reply = DP_AUX_NATIVE_REPLY_DEFER;
	else if (val == AUX_RX_COMM_I2C_DEFER)
		dpmsg->reply = DP_AUX_I2C_REPLY_DEFER;
	else if ((dpmsg->request & ~DP_AUX_I2C_MOT) == DP_AUX_I2C_WRITE ||
		 (dpmsg->request & ~DP_AUX_I2C_MOT) == DP_AUX_I2C_READ)
		dpmsg->reply = DP_AUX_I2C_REPLY_ACK;
	else if ((dpmsg->request & ~DP_AUX_I2C_MOT) == DP_AUX_NATIVE_WRITE ||
		 (dpmsg->request & ~DP_AUX_I2C_MOT) == DP_AUX_NATIVE_READ)
		dpmsg->reply = DP_AUX_NATIVE_REPLY_ACK;

out:
	if (ret < 0)
		anxdp_init_aux(sc);

	return ret;
}

void
anxdp_dpms(struct anxdp_softc *sc, int mode)
{
	switch (mode) {
	case DRM_MODE_DPMS_ON:
#ifdef notyet
		pmf_event_inject(NULL, PMFE_DISPLAY_ON);
#endif
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
#ifdef notyet
		pmf_event_inject(NULL, PMFE_DISPLAY_OFF);
#endif
		break;
	}
}

int
anxdp_attach(struct anxdp_softc *sc)
{
	sc->sc_dpaux.name = "DP Aux";
	sc->sc_dpaux.transfer = anxdp_dp_aux_transfer;
	sc->sc_dpaux.dev = &sc->sc_dev;
	if (drm_dp_aux_register(&sc->sc_dpaux) != 0) {
		printf("%s: registering DP Aux failed\n", sc->sc_dev.dv_xname);
	}

	anxdp_bringup(sc);

	return 0;
}

int
anxdp_bind(struct anxdp_softc *sc, struct drm_encoder *encoder)
{
	int error;

	sc->sc_bridge.driver_private = sc;
	sc->sc_bridge.funcs = &anxdp_bridge_funcs;
	sc->sc_bridge.encoder = encoder;

	error = drm_bridge_attach(encoder, &sc->sc_bridge, NULL, 0);
	if (error != 0)
		return EIO;

	if (sc->sc_panel != NULL && sc->sc_panel->funcs != NULL &&
	    sc->sc_panel->funcs->prepare != NULL)
		sc->sc_panel->funcs->prepare(sc->sc_panel);

	return 0;
}
