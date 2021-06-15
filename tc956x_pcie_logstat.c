/*
 * TC956X PCIe Logging and Statistics driver.
 *
 * tc956x_pcie_logstat.c
 *
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  17 Sep 2020 : Base lined
 *  VERSION	 : 00-01
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 */

/* ===================================
 * Include Files
 * ===================================
 */
#include "tc956x_pcie_logstat.h"
/* Ensure header file containing TC956X_PCIE_LOGSTAT is included in
 * tc956x_pcie_logstat.h
 */
#ifdef TC956X_PCIE_LOGSTAT
/* ===================================
 * Global Variables
 * ===================================
 */
/*
 * Array containing Different Available Ports.
 */
static uint8_t pcie_port[4][20] = {
	"Upstream Port",
	"Downstream Port1",
	"Downstream Port2",
	"Endpoint Port",
};

/*
 * Array containing Enable and Disable Modes.
 */
static uint8_t pcie_conf_mode[2][10] = {
	"Disable",
	"Enable",
};


/*
 * Array containing different LTSSM states.
 */
static uint8_t ltssm_states[COUNT_LTSSM_REG_STATES][25] = {
	"Detect.Quiet",
	"Detect.Active",
	"Polling.Active",
	"Polling.Compliance",
	"Polling.Configuration",
	"Config.LinkwidthStart",
	"Config.LinkwidthAccept",
	"Config.LanenumWait",
	"Config.LanenumAccept",
	"Config.Complete",
	"Config.Idle",
	"Recovery.RcvrLock",
	"Recovery.Equalization",
	"Recovery.Speed",
	"Recovery.RcvrCfg",
	"Recovery.Idle",
	"L0",
	"L0s",
	"L1.Entry",
	"L1.Idle",
	"L2.Idle/L2.TransmitWake",
	"Reserved",
	"Disabled",
	"Loopback.Entry",
	"Loopback.Active",
	"Loopback.Exit",
	"Hot Reset",
};


/*
 * Array containing different Equalization Phases.
 */
static uint8_t eq_phase[4][8] = {
	"Phase 0",
	"Phase 1",
	"Phase 2",
	"Phase 3",
};

/*
 * Array containing different Receive L0s sub-states.
 */
static uint8_t rx_L0s_state[4][20] = {
	"RxL0s.Inactive",
	"RxL0s.Idle",
	"RxL0s.FTS",
	"RxL0s.OutRecovery",
};

/*
 * Array containing different Transmit L0s sub-states.
 */
static uint8_t tx_L0s_state[4][15] = {
	"TxL0s.Inactive",
	"TxL0s.Idle",
	"TxL0s.FTS",
	"TxL0s.OutL0",
};

/*
 * Array containing different L1 sub-states.
 */
static uint8_t state_L1[8][20] = {
	"Inactive",
	"L1.1",
	"L1.2 entry",
	"L1.2 idle",
	"L1.2 exit",
	"L1.0",
	"Entry",
	"Exit",
};

/*
 * Array containing Lane Status.
 */
static uint8_t active_lanes[2][20] = {
	"Inactive Lane",
	"Active Lane",
};

/*
 * Array containing different Link Speed.
 */
static uint8_t link_speed[4][15] = {
	"None",
	"2.5 GT/s",
	"5.0 GT/s",
	"8.0 GT/s",
};

/*
 * Array containing Data Layer Status.
 */
static uint8_t dl_state[2][20] = {
	"Non DL_Active",
	"DL_Active",
};

/*
 * Array containing LTSSM Timeout Status.
 */
static uint8_t ltssm_timeout_status[2][31] = {
	"LTSSM Timeout is not occurred.",
	"LTSSM Timeout is occurred.",
};

/*
 * Array containing LTSSM Logging Stop Status.
 */
static uint8_t ltssm_stop_status[2][25] = {
	"State Logging NotStop",
	"State Logging Stop",
};

/* ===================================
 * Function Definition
 * ===================================
 */
/**
 * tc956x_logstat_SetConf
 *
 * \brief Function to set and print debug configuration register.
 *
 * \details This is an internal function called by tc956x_pcie_ioctl_SetConf
 * whenever IOCTL TC956X_PCIE_SET_LOGSTAT_CONF is invoked by user.
 * This function
 * configures LTSSM State Logging Configuration, Control, Fifo Read pointer
 * Control register. In addition it also configures LTSSM enable
 * registers.
 *
 * \param[in] pconf_base_addr - pointer to TC956X Configuration base address.
 * \param[in] ppcie_base_addr - pointer to TC956X PCIE SFR base address.
 * \param[in] nport - port such Upstream, Downstream 1, Downstream 2 and
 *                   Endpoint Ports.
 * \param[in] plogstat_conf - pointer to tc956x_ltssm_conf structure
 *                            containing user configuration.
 *
 * \return 0 if OK, otherwise -1
 */
int tc956x_logstat_SetConf(void __iomem *pconf_base_addr,
			enum ports nport,
			struct tc956x_ltssm_conf *plogstat_conf)
{
	int ret = 0;
	uint32_t regval;
	uint32_t port_offset; /* Port Address Register Offset */

	if ((pconf_base_addr == NULL)
	|| (plogstat_conf == NULL)) {
		ret = -1;
		KPRINT_INFO("%s : NULL Pointer Arguments\n", __func__);
	} else {
		if ((plogstat_conf->logging_stop_cnt_val > MAX_STOP_CNT)
		|| (plogstat_conf->logging_stop_linkwdth_en > ENABLE)
		|| (plogstat_conf->logging_stop_linkspeed_en > ENABLE)
		|| (plogstat_conf->logging_stop_timeout_en > ENABLE)
		|| (plogstat_conf->logging_accept_txrxL0s_en > ENABLE)
		|| (plogstat_conf->logging_post_stop_enable > ENABLE)
		|| (plogstat_conf->ltssm_fifo_pointer > MAX_FIFO_POINTER)) {
			ret = -1;
			KPRINT_INFO("%s : Invalid Arguments\n", __func__);
		}
	}

	if (ret == 0) {
		/* LTSSM Register Offsets */
		port_offset = (LTSSM_CONF_REG_OFFSET * nport);

		KPRINT_INFO("%s : %s State Logging Configuration\n", __func__, pcie_port[nport]);
		regval = 0; /* Always overwritten */
		/* Set LTSSM State Logging Configuration Register */
		regval |= (((uint32_t)(plogstat_conf->logging_stop_cnt_val)
			<< STOP_COUNT_VALUE_SHIFT)
			& STOP_COUNT_VALUE_MASK);
		KPRINT_INFO("%s : Set Stop Count Value %d\n",
			__func__, plogstat_conf->logging_stop_cnt_val);
		regval |= (((uint32_t)(plogstat_conf->logging_stop_linkwdth_en)
			<< LINKWIDTH_DOWN_ST_SHIFT) & LINKWIDTH_DOWN_ST_MASK);
		KPRINT_INFO("%s : Set Small Link Width Stop : %d %s\n", __func__,
			plogstat_conf->logging_stop_linkwdth_en,
			pcie_conf_mode[plogstat_conf->logging_stop_linkwdth_en]);
		regval |= (((uint32_t)(plogstat_conf->logging_stop_linkspeed_en)
			<< LINKSPEED_DOWN_ST_SHIFT) & LINKSPEED_DOWN_ST_MASK);
		KPRINT_INFO("%s : Set Late Link Speed Stop : %d %s\n",
			__func__, plogstat_conf->logging_stop_linkspeed_en,
			pcie_conf_mode[plogstat_conf->logging_stop_linkspeed_en]);
		regval |= (((uint32_t)(plogstat_conf->logging_stop_timeout_en)
			<< TIMEOUT_STOP_SHIFT) & TIMEOUT_STOP_MASK);
		KPRINT_INFO("%s : Set LTSSM Timeout Stop : %d %s\n", __func__,
			plogstat_conf->logging_stop_timeout_en,
			pcie_conf_mode[plogstat_conf->logging_stop_timeout_en]);
		regval |= (((uint32_t)(plogstat_conf->logging_accept_txrxL0s_en)
			<< L0S_MASK_SHIFT) & L0S_MASK_MASK);
		KPRINT_INFO("%s : Set Accept TxL0s/RxL0s : %d %s\n", __func__,
			plogstat_conf->logging_accept_txrxL0s_en,
			pcie_conf_mode[plogstat_conf->logging_accept_txrxL0s_en]);
		writel(regval, (pconf_base_addr + TC956X_CONF_REG_NPCIEUSPLOGCFG + port_offset));

		/* Set LTSSM State Logging Control Register */
		regval = 0;
		regval |= (((uint32_t)(plogstat_conf->logging_post_stop_enable)
			<< STATE_LOGGING_ENABLE_SHIFT) & STATE_LOGGING_ENABLE_MASK);
		KPRINT_INFO("%s : Precedent Condition Logging Stop : %d %s\n", __func__,
			plogstat_conf->logging_post_stop_enable,
			pcie_conf_mode[plogstat_conf->logging_post_stop_enable]);
		writel(regval, pconf_base_addr + TC956X_CONF_REG_NPCIEUSPLOGCTRL + port_offset);

		/* Read LTSSM State Logging Read Control Register */
		regval = 0; /* Always overwritten */
		regval |= (((uint32_t)(plogstat_conf->ltssm_fifo_pointer)
			<< FIFO_READ_POINTER_SHIFT) & (FIFO_READ_POINTER_MASK));
		KPRINT_INFO("%s : Set FIFO Read Pointer : %d\n", __func__,
			plogstat_conf->ltssm_fifo_pointer);
		writel(regval, pconf_base_addr + TC956X_CONF_REG_NPCIEUSPLOGRDCTRL + port_offset);
	}
	return ret;
}

/**
 * tc956x_logstat_GetConf
 *
 * \brief Function to read and print debug configuration register.
 *
 * \details This is an internal function called by tc956x_pcie_ioctl_GetConf
 * whenever IOCTL TC956X_PCIE_GET_LOGSTAT_CONF is invoked by user.
 * This function
 * reads LTSSM State Logging Configuration, Control, Fifo Read pointer
 * Control register. In addition it also reads LTSSM enable
 * registers.
 *
 * \param[in] pconf_base_addr - pointer to TC956X Configuration base address.
 * \param[in] ppcie_base_addr - pointer to TC956X PCIE SFR base address.
 * \param[in] nport - port such Upstream, Downstream 1, Downstream 2 and
 *                   Endpoint Ports.
 * \param[in] plogstat_conf - pointer to empty tc956x_ltssm_conf structure.
 *                            Function will fill register configuration
 *                            information into the structure.
 *
 * \return 0 if OK, otherwise -1
 */
int tc956x_logstat_GetConf(void __iomem *pconf_base_addr,
			enum ports nport,
			struct tc956x_ltssm_conf *plogstat_conf)
{
	int ret = 0;
	uint32_t regval;
	uint32_t port_offset; /* Port Address Register Offset */

	if ((pconf_base_addr == NULL)
	|| (plogstat_conf == NULL)) {
		ret = -1;
		KPRINT_INFO("%s : NULL Pointer Arguments\n", __func__);
	}

	if (ret == 0) {
		/* LTSSM Register Offsets */
		port_offset = (LTSSM_CONF_REG_OFFSET * nport);

		KPRINT_INFO("%s : %s State Logging Configuration\n", __func__, pcie_port[nport]);

		/* Read LTSSM State Logging Configuration Register */
		regval = readl(pconf_base_addr + TC956X_CONF_REG_NPCIEUSPLOGCFG + port_offset);

		plogstat_conf->logging_stop_cnt_val = ((regval & STOP_COUNT_VALUE_MASK)
			>> STOP_COUNT_VALUE_SHIFT);
		KPRINT_INFO("%s : Get Stop Count Value %d\n",
			__func__, plogstat_conf->logging_stop_cnt_val);
		plogstat_conf->logging_stop_linkwdth_en = ((regval & LINKWIDTH_DOWN_ST_MASK)
			>> LINKWIDTH_DOWN_ST_SHIFT);
		KPRINT_INFO("%s : Get Small Link Width Stop : %d %s\n", __func__,
			plogstat_conf->logging_stop_linkwdth_en,
			pcie_conf_mode[plogstat_conf->logging_stop_linkwdth_en]);
		plogstat_conf->logging_stop_linkspeed_en = ((regval & LINKSPEED_DOWN_ST_MASK)
			>> LINKSPEED_DOWN_ST_SHIFT);
		KPRINT_INFO("%s : Get Late Link Speed Stop : %d %s\n", __func__,
			plogstat_conf->logging_stop_linkspeed_en,
			pcie_conf_mode[plogstat_conf->logging_stop_linkspeed_en]);
		plogstat_conf->logging_stop_timeout_en = ((regval
			& TIMEOUT_STOP_MASK) >> TIMEOUT_STOP_SHIFT);
		KPRINT_INFO("%s : Get LTSSM Timeout Stop : %d %s\n", __func__,
			plogstat_conf->logging_stop_timeout_en,
			pcie_conf_mode[plogstat_conf->logging_stop_timeout_en]);
		plogstat_conf->logging_accept_txrxL0s_en = ((regval & L0S_MASK_MASK)
			>> L0S_MASK_SHIFT);
		KPRINT_INFO("%s : Get Accept TxL0s/RxL0s : %d %s\n", __func__,
			plogstat_conf->logging_accept_txrxL0s_en,
			pcie_conf_mode[plogstat_conf->logging_accept_txrxL0s_en]);

		/* Read LTSSM State Logging Control Register */
		regval = readl(pconf_base_addr + TC956X_CONF_REG_NPCIEUSPLOGCTRL + port_offset);

		plogstat_conf->logging_post_stop_enable = ((regval & STATE_LOGGING_ENABLE_MASK)
			>> STATE_LOGGING_ENABLE_SHIFT);
		KPRINT_INFO("%s : Get Precedent Condition Logging Stop : %d %s\n", __func__,
			plogstat_conf->logging_post_stop_enable,
			pcie_conf_mode[plogstat_conf->logging_post_stop_enable]);

		/* Read LTSSM State Logging Read Control Register */
		regval = readl(pconf_base_addr + TC956X_CONF_REG_NPCIEUSPLOGRDCTRL + port_offset);

		plogstat_conf->ltssm_fifo_pointer = ((regval & FIFO_READ_POINTER_MASK)
			>> FIFO_READ_POINTER_SHIFT);
		KPRINT_INFO("%s : Get FIFO Read Pointer : %d\n", __func__,
			plogstat_conf->ltssm_fifo_pointer);
	}
	return ret;
}

/**
 * tc956x_logstat_GetLTSSMLogData
 *
 * \brief Function to read and print LTSSM Logging Data register.
 *
 * \details This is an internal function called by
 * tc956x_pcie_ioctl_GetLTSSMLogData
 * whenever IOCTL TC956X_PCIE_GET_LTSSM_LOG is invoked by user.
 * This function
 * read and print LTSSM Logging Data, State Logging Status Register
 * Information.
 *
 * \param[in] pbase_addr - pointer to base address.
 * \param[in] nport - port such Upstream, Downstream 1, Downstream 2 and
 *                   Endpoint Ports.
 * \param[in] plogstat_conf - pointer to empty tc956x_ltssm_log structure.
 *                            Function will fill register configuration
 *                            information into the structure.
 *
 * \return 0 if OK, otherwise -1
 */
int tc956x_logstat_GetLTSSMLogData(void __iomem *pbase_addr,
				enum ports nport,
				struct tc956x_ltssm_log *ltssm_logd)
{
	int ret = 0;
	uint32_t regval;
	uint32_t port_offset; /* Port Address Register Offset */

	if ((pbase_addr == NULL)
		|| (ltssm_logd == NULL)) {
		ret = -1;
		KPRINT_INFO("%s : NULL Pointer Arguments\n", __func__);
	}

	if (ret == 0) {
		/* LTSSM Register Offsets */
		port_offset = (LTSSM_CONF_REG_OFFSET * nport);

		KPRINT_INFO("%s : %s State Logging Configuration\n", __func__, pcie_port[nport]);

		/* Read LTSSM Log Data Register */
		regval = readl(pbase_addr + TC956X_CONF_REG_NPCIEUSPLOGD + port_offset);
		ltssm_logd->ltssm_state = ((regval & FIFO_READ_VALUE0_MASK)
			>> FIFO_READ_VALUE0_SHIFT);
		KPRINT_INFO("%s : LTSSM State: 0x%x %s\n", __func__, ltssm_logd->ltssm_state,
			ltssm_states[ltssm_logd->ltssm_state]);
		ltssm_logd->eq_phase = ((regval & FIFO_READ_VALUE1_MASK)
			>> FIFO_READ_VALUE1_SHIFT);
		KPRINT_INFO("%s : Equalization Phase: 0x%x %s\n", __func__, ltssm_logd->eq_phase,
			eq_phase[ltssm_logd->eq_phase]);
		ltssm_logd->rxL0s = ((regval & FIFO_READ_VALUE2_MASK)
			>> FIFO_READ_VALUE2_SHIFT);
		KPRINT_INFO("%s : Receive L0s: 0x%x %s\n", __func__, ltssm_logd->rxL0s,
			rx_L0s_state[ltssm_logd->rxL0s]);
		ltssm_logd->txL0s = ((regval & FIFO_READ_VALUE3_MASK)
			>> FIFO_READ_VALUE3_SHIFT);
		KPRINT_INFO("%s : Transmit L0s: 0x%x %s\n", __func__, ltssm_logd->txL0s,
			tx_L0s_state[ltssm_logd->txL0s]);
		ltssm_logd->substate_L1 = ((regval & FIFO_READ_VALUE4_MASK)
			>> FIFO_READ_VALUE4_SHIFT);
		KPRINT_INFO("%s : L1 Power Management Substate: 0x%x %s\n", __func__,
			ltssm_logd->substate_L1, state_L1[ltssm_logd->substate_L1]);
		ltssm_logd->active_lane = ((regval & FIFO_READ_VALUE5_MASK)
			>> FIFO_READ_VALUE5_SHIFT);
		KPRINT_INFO("%s : Active Lanes Lane0: 0x%x %s\n", __func__,
			((ltssm_logd->active_lane & LANE0_MASK) >> LANE0_SHIFT),
			active_lanes[(ltssm_logd->active_lane & LANE0_MASK)]);
		KPRINT_INFO("%s : Active Lanes Lane1: 0x%x %s\n", __func__,
			((ltssm_logd->active_lane & LANE1_MASK) >> LANE1_SHIFT),
			active_lanes[(ltssm_logd->active_lane & LANE1_MASK)]);
		KPRINT_INFO("%s : Active Lanes Lane2: 0x%x %s\n", __func__,
			((ltssm_logd->active_lane & LANE2_MASK) >> LANE2_SHIFT),
			active_lanes[(ltssm_logd->active_lane & LANE2_MASK)]);
		KPRINT_INFO("%s : Active Lanes Lane3: 0x%x %s\n", __func__,
			((ltssm_logd->active_lane & LANE3_MASK) >> LANE3_SHIFT),
			active_lanes[(ltssm_logd->active_lane & LANE3_MASK)]);
		ltssm_logd->link_speed = ((regval & FIFO_READ_VALUE6_MASK)
			>> FIFO_READ_VALUE6_SHIFT);
		KPRINT_INFO("%s : Link Speed: 0x%x %s\n", __func__, ltssm_logd->link_speed,
			link_speed[ltssm_logd->link_speed]);
		ltssm_logd->dl_active = ((regval & FIFO_READ_VALUE7_MASK)
			>> FIFO_READ_VALUE7_SHIFT);
		KPRINT_INFO("%s : Data Link Active: 0x%x %s\n", __func__,
			ltssm_logd->dl_active, dl_state[ltssm_logd->dl_active]);
		ltssm_logd->ltssm_timeout = ((regval & FIFO_READ_VALUE8_MASK)
			>> FIFO_READ_VALUE8_SHIFT);
		KPRINT_INFO("%s : LTSSM Timeout: 0x%x %s\n", __func__, ltssm_logd->ltssm_timeout,
			ltssm_timeout_status[ltssm_logd->ltssm_timeout]);

		regval = readl(pbase_addr + TC956X_CONF_REG_NPCIEUSPLOGST + port_offset);
		ltssm_logd->ltssm_stop_status = ((regval & STOP_STATUS_MASK) >> STOP_STATUS_SHIFT);
		KPRINT_INFO("%s : LTSSM Stop Status: 0x%x %s\n", __func__,
			ltssm_logd->ltssm_stop_status,
			ltssm_stop_status[ltssm_logd->ltssm_stop_status]);
	}
	return ret;
}


/**
 * tc956x_pcie_ioctl_SetDbgConf
 *
 * \brief IOCTL Function to set and print debug configuration register.
 *
 * \details This function is called whenever IOCTL
 * TC956X_PCIE_SET_LOGSTAT_CONF
 * is invoked by user. This function
 * configures LTSSM State Logging Configuration, Control, Fifo Read pointer
 * Control register. In addition it also configures LTSSM enable
 * registers.
 *
 * \param[in] priv - pointer to pcie private data.
 * \param[in] data - data passed from user space.
 *
 * \return -EFAULT in case of copy failure, otherwise 0
 */
int tc956x_pcie_ioctl_SetDbgConf(const struct tc956xmac_priv *priv,
				void __user *data)
{
	int ret = 0;
	struct tc956x_ioctl_logstatconf ioctl_data;
	struct tc956x_ltssm_conf logstat_conf;

	if ((priv == NULL) || (data == NULL))
		return -EFAULT;

	DBGPR_FUNC(priv->device, "--> %s\n", __func__);

	memset(&logstat_conf, 0, sizeof(logstat_conf));
	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		ret = -EFAULT;

	if (copy_from_user(&logstat_conf,
		(const void __user *)ioctl_data.logstat_conf,
		sizeof(logstat_conf)))
		ret = -EFAULT;

	if (ioctl_data.port > INTERNAL_ENDPOINT) {
		KPRINT_INFO("%s : Argument Port %d invalid", __func__, ioctl_data.port);
		ret = -EFAULT;
	}

	if (ret == 0) {
#ifdef TC956X
		ret = tc956x_logstat_SetConf(priv->ioaddr, ioctl_data.port, &logstat_conf);
#endif
	}

	return ret;
}

/**
 * tc956x_pcie_ioctl_GetDbgConf
 *
 * \brief IOCTL Function to read and print debug configuration register.
 *
 * \details This function is called whenever IOCTL
 * TC956X_PCIE_GET_LOGSTAT_CONF
 * is invoked by user. This function
 * reads LTSSM State Logging Configuration, Control, Fifo Read pointer
 * Control register. In addition it also reads LTSSM enable
 * registers.
 *
 * \param[in] priv - pointer to pcie private data.
 * \param[in] data - data passed from user space.
 *
 * \return -EFAULT in case of copy failure, otherwise 0
 */
int tc956x_pcie_ioctl_GetDbgConf(const struct tc956xmac_priv *priv,
				void __user *data)
{
	int ret = 0;
	struct tc956x_ioctl_logstatconf ioctl_data;
	struct tc956x_ltssm_conf logstat_conf;

	if ((priv == NULL) || (data == NULL))
		return -EFAULT;

	DBGPR_FUNC(priv->device, "--> %s\n", __func__);

	memset(&logstat_conf, 0, sizeof(logstat_conf));
	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		ret = -EFAULT;

	if (ioctl_data.port > INTERNAL_ENDPOINT) {
		KPRINT_INFO("%s : Argument Port %d invalid", __func__, ioctl_data.port);
		ret = -EFAULT;
	}

	if (ret == 0) {
#ifdef TC956X
		ret = tc956x_logstat_GetConf(priv->ioaddr, ioctl_data.port, &logstat_conf);
#endif
	}

	if (ret == 0) {
		if (copy_to_user((void __user *)ioctl_data.logstat_conf, &logstat_conf, sizeof(logstat_conf)))
			ret = -EFAULT;
	}
	return ret;
}

/**
 * tc956x_pcie_ioctl_GetLTSSMLogD
 *
 * \brief IOCTL Function to read and print LTSSM logging register.
 *
 * \details This function is called whenever IOCTL TC956X_PCIE_GET_LTSSM_LOG
 * is invoked by user. This function
 * read and print LTSSM Logging Data, State Logging Status Register
 * Information.
 *
 * \param[in] priv - pointer to pcie private data.
 * \param[in] data - data passed from user space.
 *
 * \return -EFAULT in case of copy failure, otherwise 0
 */
int tc956x_pcie_ioctl_GetLTSSMLogD(const struct tc956xmac_priv *priv,
				void __user *data)
{
	int ret = 0;
	struct tc956x_ioctl_ltssm ioctl_data;
	struct tc956x_ltssm_log ltssm_log;

	if ((priv == NULL) || (data == NULL))
		return -EFAULT;
	DBGPR_FUNC(priv->device, "--> %s\n", __func__);

	memset(&ltssm_log, 0, sizeof(ltssm_log));
	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		ret = -EFAULT;

	if (ioctl_data.port > INTERNAL_ENDPOINT) {
		KPRINT_INFO("%s : Argument Port %d invalid", __func__, ioctl_data.port);
		ret = -EFAULT;
	}

	if (ret == 0) {
#ifdef TC956X
		tc956x_logstat_GetLTSSMLogData(priv->ioaddr, ioctl_data.port, &(ltssm_log));
#endif

	}

	if (ret == 0) {
		if (copy_to_user((void __user *)ioctl_data.ltssm_logd, &ltssm_log, sizeof(ltssm_log)))
			ret = -EFAULT;
	}
	return ret;
}

#endif /* ifdef TC956X_PCIE_LOGSTAT */
