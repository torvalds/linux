/*-
 * Copyright (c) 2013 Cedric GROSS <cg@cgross.info>
 * Copyright (c) 2011 Intel Corporation
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
 *
 * $FreeBSD$
 */

#ifndef	__IF_IWN_CHIP_CFG_H__
#define	__IF_IWN_CHIP_CFG_H__

/* ==========================================================================
 *                                  NIC PARAMETERS
 *
 * ==========================================================================
 */

/*
 * Flags for managing calibration result. See calib_need
 * in iwn_base_params struct
 *
 * These are bitmasks that determine which indexes in the calibcmd
 * array are pushed up.
 */
#define IWN_FLG_NEED_PHY_CALIB_DC		(1<<0)
#define IWN_FLG_NEED_PHY_CALIB_LO		(1<<1)
#define IWN_FLG_NEED_PHY_CALIB_TX_IQ		(1<<2)
#define IWN_FLG_NEED_PHY_CALIB_TX_IQ_PERIODIC	(1<<3)
#define IWN_FLG_NEED_PHY_CALIB_BASE_BAND	(1<<4)
/*
 * These aren't (yet) included in the calibcmd array, but
 * are used as flags for which calibrations to use.
 *
 * XXX I think they should be named differently and
 * stuffed in a different member in the config struct!
 */
#define IWN_FLG_NEED_PHY_CALIB_TEMP_OFFSET	(1<<5)
#define IWN_FLG_NEED_PHY_CALIB_CRYSTAL		(1<<6)
#define IWN_FLG_NEED_PHY_CALIB_TEMP_OFFSETv2	(1<<7)

/*
 * Each chip has a different threshold for PLCP errors that should trigger a
 * retune.
 */
#define	IWN_PLCP_ERR_DEFAULT_THRESHOLD		50
#define	IWN_PLCP_ERR_LONG_THRESHOLD		100
#define	IWN_PLCP_ERR_EXT_LONG_THRESHOLD		200

/*
 * Define some parameters for managing different NIC.
 * Refer to linux specific file like iwl-xxxx.c to determine correct value
 * for NIC.
 *
 * @max_ll_items: max number of OTP blocks
 * @shadow_ram_support: shadow support for OTP memory
 * @shadow_reg_enable: HW shadhow register bit
 * @no_idle_support: do not support idle mode
 * @advanced_bt_coexist : Advanced BT management
 * @bt_session_2 : NIC need a new struct for configure BT coexistence. Needed
 *   only if advanced_bt_coexist is true
 * @bt_sco_disable :
 * @additional_nic_config: For 6005 series
 * @iq_invert : ? But need it for N 2000 series
 * @regulatory_bands : XXX
 * @enhanced_TX_power : EEPROM Has advanced TX power options. Set 'True'
 *    if update_enhanced_txpower = iwl_eeprom_enhanced_txpower.
 *    See iwl-agn-devices.c file to determine that(enhanced_txpower)
 * @need_temp_offset_calib : Need to compute some temp offset for calibration.
 * @calib_need : Use IWN_FLG_NEED_PHY_CALIB_* flags to specify which
 *    calibration data ucode need. See calib_init_cfg in iwl-xxxx.c
 *    linux kernel file
 * @support_hostap: Define IEEE80211_C_HOSTAP for ic_caps
 * @no_multi_vaps: See iwn_vap_create
 * @additional_gp_drv_bit : Specific bit to defined during nic_config
 * @bt_mode: BT configuration mode
 */
enum bt_mode_enum {
	IWN_BT_NONE,
	IWN_BT_SIMPLE,
	IWN_BT_ADVANCED
};

struct iwn_base_params {
	uint32_t	pll_cfg_val;
	const uint16_t	max_ll_items;
#define IWN_OTP_MAX_LL_ITEMS_1000		(3)	/* OTP blocks for 1000 */
#define IWN_OTP_MAX_LL_ITEMS_6x00		(4)	/* OTP blocks for 6x00 */
#define IWN_OTP_MAX_LL_ITEMS_6x50		(7)	/* OTP blocks for 6x50 */
#define IWN_OTP_MAX_LL_ITEMS_2x00		(4)	/* OTP blocks for 2x00 */
	const bool	shadow_ram_support;
	const bool	shadow_reg_enable;
	const bool	bt_session_2;
	const bool	bt_sco_disable;
	const bool	additional_nic_config;
	const uint32_t	*regulatory_bands;
	const bool	enhanced_TX_power;
	const uint16_t	calib_need;
	const bool	support_hostap;
	const bool	no_multi_vaps;
	uint8_t	additional_gp_drv_bit;
	enum bt_mode_enum	bt_mode;
	uint32_t	plcp_err_threshold;
};

static const struct iwn_base_params iwn5000_base_params = {
	.pll_cfg_val = IWN_ANA_PLL_INIT,	/* pll_cfg_val; */
	.max_ll_items = IWN_OTP_MAX_LL_ITEMS_6x00,	/* max_ll_items */
	.shadow_ram_support = false,	/* shadow_ram_support */
	.shadow_reg_enable = false,	/* shadow_reg_enable */
	.bt_session_2 = false,	/* bt_session_2 */
	.bt_sco_disable = true,	/* bt_sco_disable */
	.additional_nic_config = false,	/* additional_nic_config */
	.regulatory_bands = iwn5000_regulatory_bands,	/* regulatory_bands */
	.enhanced_TX_power = false,	/* enhanced_TX_power */
	.calib_need =
	    ( IWN_FLG_NEED_PHY_CALIB_LO
	    | IWN_FLG_NEED_PHY_CALIB_TX_IQ_PERIODIC
	    | IWN_FLG_NEED_PHY_CALIB_TX_IQ
	    | IWN_FLG_NEED_PHY_CALIB_BASE_BAND ),
	.support_hostap = false,	/* support_hostap */
	.no_multi_vaps = true,	/* no_multi_vaps */
	.additional_gp_drv_bit = IWN_GP_DRIVER_NONE,	/* additional_gp_drv_bit */
	.bt_mode = IWN_BT_NONE,	/* bt_mode */
	.plcp_err_threshold = IWN_PLCP_ERR_LONG_THRESHOLD,
};

/*
 * 4965 support
 */
static const struct iwn_base_params iwn4965_base_params = {
	.pll_cfg_val = 0,				/* pll_cfg_val; */
	.max_ll_items = IWN_OTP_MAX_LL_ITEMS_6x00,	/* max_ll_items - ignored for 4965 */
	.shadow_ram_support = true,	/* shadow_ram_support */
	.shadow_reg_enable = false,	/* shadow_reg_enable */
	.bt_session_2 = false,	/* bt_session_2 XXX unknown? */
	.bt_sco_disable = true,	/* bt_sco_disable XXX unknown? */
	.additional_nic_config = false,	/* additional_nic_config - not for 4965 */
	.regulatory_bands = iwn5000_regulatory_bands,	/* regulatory_bands */
	.enhanced_TX_power = false,	/* enhanced_TX_power - not for 4965 */
	.calib_need =
	    (IWN_FLG_NEED_PHY_CALIB_DC
	    | IWN_FLG_NEED_PHY_CALIB_LO
	    | IWN_FLG_NEED_PHY_CALIB_TX_IQ_PERIODIC
	    | IWN_FLG_NEED_PHY_CALIB_TX_IQ
	    | IWN_FLG_NEED_PHY_CALIB_BASE_BAND ),
	.support_hostap = false,	/* support_hostap - XXX should work on fixing! */
	.no_multi_vaps = true,	/* no_multi_vaps - XXX should work on fixing!  */
	.additional_gp_drv_bit = IWN_GP_DRIVER_NONE,	/* additional_gp_drv_bit */
	.bt_mode = IWN_BT_SIMPLE,	/* bt_mode */
	.plcp_err_threshold = IWN_PLCP_ERR_DEFAULT_THRESHOLD,
};


static const struct iwn_base_params iwn2000_base_params = {
	.pll_cfg_val = 0,
	.max_ll_items = IWN_OTP_MAX_LL_ITEMS_2x00,
	.shadow_ram_support = true,
	.shadow_reg_enable = false,
	.bt_session_2 = false,
	.bt_sco_disable = true,
	.additional_nic_config = false,
	.regulatory_bands = iwn2030_regulatory_bands,
	.enhanced_TX_power = true,
	.calib_need =
	    (IWN_FLG_NEED_PHY_CALIB_DC
	    | IWN_FLG_NEED_PHY_CALIB_LO
	    | IWN_FLG_NEED_PHY_CALIB_TX_IQ
	    | IWN_FLG_NEED_PHY_CALIB_BASE_BAND
	    | IWN_FLG_NEED_PHY_CALIB_TEMP_OFFSETv2 ),
	.support_hostap = true,
	.no_multi_vaps = false,
	.additional_gp_drv_bit = IWN_GP_DRIVER_REG_BIT_RADIO_IQ_INVERT,
	.bt_mode = IWN_BT_NONE,
	.plcp_err_threshold = IWN_PLCP_ERR_DEFAULT_THRESHOLD,
};

static const struct iwn_base_params iwn2030_base_params = {
	.pll_cfg_val = 0,
	.max_ll_items = IWN_OTP_MAX_LL_ITEMS_2x00,
	.shadow_ram_support = true,
	.shadow_reg_enable = false,     /* XXX check? */
	.bt_session_2 = true,
	.bt_sco_disable = true,
	.additional_nic_config = false,
	.regulatory_bands = iwn2030_regulatory_bands,
	.enhanced_TX_power = true,
	.calib_need =
	    (IWN_FLG_NEED_PHY_CALIB_DC
	    | IWN_FLG_NEED_PHY_CALIB_LO
	    | IWN_FLG_NEED_PHY_CALIB_TX_IQ
	    | IWN_FLG_NEED_PHY_CALIB_BASE_BAND
	    | IWN_FLG_NEED_PHY_CALIB_TEMP_OFFSETv2 ),
	.support_hostap = true,
	.no_multi_vaps = false,
	.additional_gp_drv_bit = IWN_GP_DRIVER_REG_BIT_RADIO_IQ_INVERT,
	.bt_mode = IWN_BT_ADVANCED,
	.plcp_err_threshold = IWN_PLCP_ERR_DEFAULT_THRESHOLD,
};

static const struct iwn_base_params iwn1000_base_params = {
	.pll_cfg_val = IWN_ANA_PLL_INIT,
	.max_ll_items = IWN_OTP_MAX_LL_ITEMS_1000,
	.shadow_ram_support = false,
	.shadow_reg_enable = false,	/* XXX check? */
	.bt_session_2 = false,
	.bt_sco_disable = false,
	.additional_nic_config = false,
	.regulatory_bands = iwn5000_regulatory_bands,
	.enhanced_TX_power = false,
	.calib_need =
	    ( IWN_FLG_NEED_PHY_CALIB_LO
	    | IWN_FLG_NEED_PHY_CALIB_TX_IQ_PERIODIC
	    | IWN_FLG_NEED_PHY_CALIB_TX_IQ
	    | IWN_FLG_NEED_PHY_CALIB_BASE_BAND
	    ),
	.support_hostap = false,
	.no_multi_vaps = true,
	.additional_gp_drv_bit = IWN_GP_DRIVER_NONE,
	/* XXX 1000 - no BT */
	.bt_mode = IWN_BT_SIMPLE,
	.plcp_err_threshold = IWN_PLCP_ERR_EXT_LONG_THRESHOLD,
};
static const struct iwn_base_params iwn_6000_base_params = {
	.pll_cfg_val = 0,
	.max_ll_items = IWN_OTP_MAX_LL_ITEMS_6x00,
	.shadow_ram_support = true,
	.shadow_reg_enable = true,
	.bt_session_2 = false,
	.bt_sco_disable = false,
	.additional_nic_config = false,
	.regulatory_bands = iwn6000_regulatory_bands,
	.enhanced_TX_power = true,
	.calib_need =
	    (IWN_FLG_NEED_PHY_CALIB_DC
	    | IWN_FLG_NEED_PHY_CALIB_LO
	    | IWN_FLG_NEED_PHY_CALIB_TX_IQ
	    | IWN_FLG_NEED_PHY_CALIB_BASE_BAND ),
	.support_hostap = false,
	.no_multi_vaps = true,
	.additional_gp_drv_bit = IWN_GP_DRIVER_NONE,
	.bt_mode = IWN_BT_SIMPLE,
	.plcp_err_threshold = IWN_PLCP_ERR_DEFAULT_THRESHOLD,
};
static const struct iwn_base_params iwn_6000i_base_params = {
	.pll_cfg_val = 0,
	.max_ll_items = IWN_OTP_MAX_LL_ITEMS_6x00,
	.shadow_ram_support = true,
	.shadow_reg_enable = true,
	.bt_session_2 = false,
	.bt_sco_disable = true,
	.additional_nic_config = false,
	.regulatory_bands = iwn6000_regulatory_bands,
	.enhanced_TX_power = true,
	.calib_need =
	    (IWN_FLG_NEED_PHY_CALIB_DC
	    | IWN_FLG_NEED_PHY_CALIB_LO
	    | IWN_FLG_NEED_PHY_CALIB_TX_IQ
	    | IWN_FLG_NEED_PHY_CALIB_BASE_BAND ),
	.support_hostap = false,
	.no_multi_vaps = true,
	.additional_gp_drv_bit = IWN_GP_DRIVER_NONE,
	.bt_mode = IWN_BT_SIMPLE,
	.plcp_err_threshold = IWN_PLCP_ERR_DEFAULT_THRESHOLD,
};
static const struct iwn_base_params iwn_6000g2_base_params = {
	.pll_cfg_val = 0,
	.max_ll_items = IWN_OTP_MAX_LL_ITEMS_6x00,
	.shadow_ram_support = true,
	.shadow_reg_enable = true,
	.bt_session_2 = false,
	.bt_sco_disable = true,
	.additional_nic_config = false,
	.regulatory_bands = iwn6000_regulatory_bands,
	.enhanced_TX_power = true,
	.calib_need =
	    (IWN_FLG_NEED_PHY_CALIB_DC
	    | IWN_FLG_NEED_PHY_CALIB_LO
	    | IWN_FLG_NEED_PHY_CALIB_TX_IQ
	    | IWN_FLG_NEED_PHY_CALIB_BASE_BAND
	    | IWN_FLG_NEED_PHY_CALIB_TEMP_OFFSET ),
	.support_hostap = false,
	.no_multi_vaps = true,
	.additional_gp_drv_bit = 0,
	.bt_mode = IWN_BT_SIMPLE,
	.plcp_err_threshold = IWN_PLCP_ERR_DEFAULT_THRESHOLD,
};

static const struct iwn_base_params iwn_6050_base_params = {
	.pll_cfg_val = 0,
	.max_ll_items = IWN_OTP_MAX_LL_ITEMS_6x50,
	.shadow_ram_support = true,
	.shadow_reg_enable = true,
	.bt_session_2 = false,
	.bt_sco_disable = true,
	.additional_nic_config = true,
	.regulatory_bands = iwn6000_regulatory_bands,
	.enhanced_TX_power = true,
	.calib_need =
	    (IWN_FLG_NEED_PHY_CALIB_LO
	    | IWN_FLG_NEED_PHY_CALIB_TX_IQ
	    | IWN_FLG_NEED_PHY_CALIB_BASE_BAND ),
	.support_hostap = false,
	.no_multi_vaps = true,
	.additional_gp_drv_bit = IWN_GP_DRIVER_NONE,
	.bt_mode = IWN_BT_SIMPLE,
	.plcp_err_threshold = IWN_PLCP_ERR_DEFAULT_THRESHOLD,
};
static const struct iwn_base_params iwn_6150_base_params = {
	.pll_cfg_val = 0,
	.max_ll_items = IWN_OTP_MAX_LL_ITEMS_6x50,
	.shadow_ram_support = true,
	.shadow_reg_enable = true,
	.bt_session_2 = false,
	.bt_sco_disable = true,
	.additional_nic_config = true,
	.regulatory_bands = iwn6000_regulatory_bands,
	.enhanced_TX_power = true,
	.calib_need =
	    (IWN_FLG_NEED_PHY_CALIB_LO
	    | IWN_FLG_NEED_PHY_CALIB_TX_IQ
	    | IWN_FLG_NEED_PHY_CALIB_BASE_BAND),
	.support_hostap = false,
	.no_multi_vaps = true,
	.additional_gp_drv_bit = IWN_GP_DRIVER_6050_1X2,
	.bt_mode = IWN_BT_SIMPLE,
	.plcp_err_threshold = IWN_PLCP_ERR_DEFAULT_THRESHOLD,
};

/* IWL_DEVICE_6035 & IWL_DEVICE_6030 */
static const struct iwn_base_params iwn_6000g2b_base_params = {
	.pll_cfg_val = 0,
	.max_ll_items = IWN_OTP_MAX_LL_ITEMS_6x00,
	.shadow_ram_support = true,
	.shadow_reg_enable = true,
	.bt_session_2 = false,
	.bt_sco_disable = true,
	.additional_nic_config = false,
	.regulatory_bands = iwn6000_regulatory_bands,
	.enhanced_TX_power = true,
	.calib_need =
	    (IWN_FLG_NEED_PHY_CALIB_DC
	    | IWN_FLG_NEED_PHY_CALIB_LO
	    | IWN_FLG_NEED_PHY_CALIB_TX_IQ
	    | IWN_FLG_NEED_PHY_CALIB_BASE_BAND
	    | IWN_FLG_NEED_PHY_CALIB_TEMP_OFFSET ),
	.support_hostap = false,
	.no_multi_vaps = true,
	.additional_gp_drv_bit = IWN_GP_DRIVER_NONE,
	.bt_mode = IWN_BT_ADVANCED,
	.plcp_err_threshold = IWN_PLCP_ERR_DEFAULT_THRESHOLD,
};

/*
 * 6235 series NICs.
 */
static const struct iwn_base_params iwn_6235_base_params = {
	.pll_cfg_val = 0,
	.max_ll_items = IWN_OTP_MAX_LL_ITEMS_6x00,
	.shadow_ram_support = true,
	.shadow_reg_enable = true,
	.bt_session_2 = false,
	.bt_sco_disable = true,
	.additional_nic_config = true,
	.regulatory_bands = iwn6000_regulatory_bands,
	.enhanced_TX_power = true,
	.calib_need =
	    (IWN_FLG_NEED_PHY_CALIB_DC
	    | IWN_FLG_NEED_PHY_CALIB_LO
	    | IWN_FLG_NEED_PHY_CALIB_TX_IQ
	    | IWN_FLG_NEED_PHY_CALIB_BASE_BAND
	    | IWN_FLG_NEED_PHY_CALIB_TEMP_OFFSET ),
	.support_hostap = false,
	.no_multi_vaps = true,
	.additional_gp_drv_bit = 0,
	.bt_mode = IWN_BT_ADVANCED,
	.plcp_err_threshold = IWN_PLCP_ERR_DEFAULT_THRESHOLD,
};

static const struct iwn_base_params iwn_5x50_base_params = {
	.pll_cfg_val = IWN_ANA_PLL_INIT,
	.max_ll_items = IWN_OTP_MAX_LL_ITEMS_6x00,
	.shadow_ram_support = true,
	.shadow_reg_enable = false,
	.bt_session_2 = false,
	.bt_sco_disable = true,
	.additional_nic_config = false,
	.regulatory_bands = iwn5000_regulatory_bands,
	.enhanced_TX_power =false,
	.calib_need =
	    (IWN_FLG_NEED_PHY_CALIB_DC
	    | IWN_FLG_NEED_PHY_CALIB_LO
	    | IWN_FLG_NEED_PHY_CALIB_TX_IQ
	    | IWN_FLG_NEED_PHY_CALIB_BASE_BAND ),
	.support_hostap = false,
	.no_multi_vaps = true,
	.additional_gp_drv_bit = IWN_GP_DRIVER_NONE,
	.bt_mode = IWN_BT_SIMPLE,
	.plcp_err_threshold = IWN_PLCP_ERR_LONG_THRESHOLD,
};

#endif	/* __IF_IWN_CHIP_CFG_H__ */
