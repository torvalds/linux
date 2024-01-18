/*
 * TC956X PCIe Logging and Statistics header file.
 *
 * tc956x_pcie_logstat.h
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
 *  VERSION     : 00-01
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 *  14 Oct 2021 : 1. Moving common Macros to common header file 
 *  VERSION     : 01-00-16
 */

#ifndef __TC956X_PCIE_LOGSTAT_H__
#define __TC956X_PCIE_LOGSTAT_H__

/* ===================================
 * Include
 * ===================================
 */
#include "common.h"
#include "tc956xmac.h"
#include "tc956xmac_ioctl.h"

#ifdef TC956X_PCIE_LOGSTAT

/* ===================================
 * Macros
 * ===================================
 */
/* Configuration Register Address */
#ifdef TC956X
#define TC956X_CONF_REG_NPCIEUSPLOGCFG				(0x00001320U)
#define TC956X_CONF_REG_NPCIEUSPLOGCTRL				(0x00001324U)
#define TC956X_CONF_REG_NPCIEUSPLOGST				(0x00001328U)
#define TC956X_CONF_REG_NPCIEUSPLOGRDCTRL			(0x0000132CU)
#define TC956X_CONF_REG_NPCIEUSPLOGD				(0x00001330U)

#define LTSSM_CONF_REG_OFFSET					(0x20U)
#endif /* TC956X */
 /* LTSSM Enable Bit Mask Value*/
#define LTSSM_BIT_MASK						(0x00000001U)
#define LTSSM_PORT_EN_SHIFT					(0x4U)
/* Common NPCIEUSPLOGCFG, NPCIEDSP1LOGCFG, NPCIEDSP2LOGCFG, NPCIEEPLOGCFG
 * register Logging Configuration Bit Mask and Shift Value
 */
#define STOP_COUNT_VALUE_MASK					(0x00000FF0U)
#define STOP_COUNT_VALUE_SHIFT					(4U)
#define LINKWIDTH_DOWN_ST_MASK					(0x00000008U)
#define LINKWIDTH_DOWN_ST_SHIFT					(3U)
#define LINKSPEED_DOWN_ST_MASK					(0x00000004U)
#define LINKSPEED_DOWN_ST_SHIFT					(2U)
#define TIMEOUT_STOP_MASK					(0x00000002U)
#define TIMEOUT_STOP_SHIFT					(1U)
#define L0S_MASK_MASK						(0x00000001U)
#define L0S_MASK_SHIFT						(0U)
#define STATE_LOGGING_ENABLE_MASK				(0x00000001U)
#define STATE_LOGGING_ENABLE_SHIFT				(0U)
#define FIFO_READ_POINTER_MASK					(0x0000001FU)
#define FIFO_READ_POINTER_SHIFT					(0U)
#define STOP_STATUS_MASK					(0x00000001U)
#define STOP_STATUS_SHIFT					(0U)

#define COUNT_LTSSM_REG_STATES					(28U)

/* Common NPCIEUSPLOGD, NPCIEDSP1LOGD, NPCIEDSP2LOGD, NPCIEEPLOGD
 * Register Logging Read Data Bit Mask and Shift Value
 */
#define FIFO_READ_VALUE8_MASK					(0x20000000U)
#define FIFO_READ_VALUE8_SHIFT					(29U)
#define FIFO_READ_VALUE7_MASK					(0x10000000U)
#define FIFO_READ_VALUE7_SHIFT					(28U)
#define FIFO_READ_VALUE6_MASK					(0x03000000U)
#define FIFO_READ_VALUE6_SHIFT					(24U)
#define FIFO_READ_VALUE5_MASK					(0x00F00000U)
#define FIFO_READ_VALUE5_SHIFT					(20U)
#define FIFO_READ_VALUE4_MASK					(0x00070000U)
#define FIFO_READ_VALUE4_SHIFT					(16U)
#define FIFO_READ_VALUE3_MASK					(0x0000C000U)
#define FIFO_READ_VALUE3_SHIFT					(14U)
#define FIFO_READ_VALUE2_MASK					(0x00003000U)
#define FIFO_READ_VALUE2_SHIFT					(12U)
#define FIFO_READ_VALUE1_MASK					(0x00000300U)
#define FIFO_READ_VALUE1_SHIFT					(8U)
#define FIFO_READ_VALUE0_MASK					(0x0000001FU)
#define FIFO_READ_VALUE0_SHIFT					(0U)
#define STOP_STATUS_MASK					(0x00000001U)
#define STOP_STATUS_SHIFT					(0U)
/* Common NPCIEUSPLOGD, NPCIEDSP1LOGD, NPCIEDSP2LOGD, NPCIEEPLOGD
 * Register, Different Lanes Bit Mask Values
 */
#define LANE0_MASK						(0x1U)
#define LANE0_SHIFT						(0U)
#define LANE1_MASK						(0x2U)
#define LANE1_SHIFT						(1U)
#define LANE2_MASK						(0x4U)
#define LANE2_SHIFT						(2U)
#define LANE3_MASK						(0x8U)
#define LANE3_SHIFT						(3U)

#define MAX_STOP_CNT						(0xFFU)
#define MAX_FIFO_POINTER					(31U)

#define STATE_LOG_REG_OFFSET					(0x20U)
#define GLUE_REG_LTSSM_OFFSET					(0x40U)

#define STATE_LOG_STOP						(1U)
#define MAX_FIFO_READ_POINTER					(0x1F)
#define INVALID_STATE_LOG					(0x33F7F31FU)

#define LTSSM_TIMEOUT_NOT_OCCURRED				(0U)
#define LTSSM_TIMEOUT_OCCURRED					(1U)
#define DL_ACTIVE						(1U)
#define DL_NOT_ACTIVE						(0U)
#define ALL_LANES_INACTIVE					(0U)
#define INACTIVE_L0s						(0U)
#define EQ_PHASE0						(0U)
#define INACTIVE_L1						(0U)

#define LTSSM_MAX_VALUE						(0x1A)
#define LOGSTAT_DUMMY_VALUE					(0xFF)
#define ACTIVE_SINGLE_LANE_MASK					(1)
#define ACTIVE_SINGLE_LANE_SHIFT				(1)
#define ACTIVE_ALL_LANE_MASK					(0xF)
/* ===================================
 * Enumeration
 * ===================================
 */

/* ===================================
 * Structure/Union
 * ===================================
 */
union tc956x_logstat_State_Log_Data {
	struct {
		unsigned char fifo_read_value0 :5;
		unsigned char reserved1 :3;
		unsigned char fifo_read_value1 :2;
		unsigned char reserved2 :2;
		unsigned char fifo_read_value2 :2;
		unsigned char fifo_read_value3 :2;
		unsigned char fifo_read_value4 :3;
		unsigned char reserved3 :1;
		unsigned char fifo_read_value5 :4;
		unsigned char fifo_read_value6 :2;
		unsigned char reserved4 :2;
		unsigned char fifo_read_value7 :1;
		unsigned char fifo_read_value8 :1;
		unsigned char reserved5 :2;
	} bitfield;
	unsigned int reg_val;
};

/* ===================================
 * Function Declaration
 * ===================================
 */
int tc956x_pcie_ioctl_state_log_summary(const struct tc956xmac_priv *priv, void __user *data);
int tc956x_pcie_ioctl_get_pcie_link_params(const struct tc956xmac_priv *priv, void __user *data);
int tc956x_pcie_ioctl_state_log_enable(const struct tc956xmac_priv *priv, void __user *data);
int tc956x_logstat_state_log_summary(void __iomem *pbase_addr, enum ports nport);
int tc956x_logstat_get_state_log_stop_status(void __iomem *pbase_addr, enum ports nport, uint8_t *pstop_status);
int tc956x_logstat_set_state_log_fifo_ptr(void __iomem *pbase_addr, enum ports nport, uint8_t fifo_pointer);
int tc956x_logstat_get_state_log_data(void __iomem *pbase_addr, enum ports nport, uint32_t *pstate_log_data);
int tc956x_logstat_state_log_analyze(unsigned int cur_state);
int tc956x_logstat_get_pcie_cur_ltssm(void __iomem *pbase_addr, enum ports nport, uint8_t *pltssm);
int tc956x_logstat_get_pcie_cur_dll(void __iomem *pbase_addr, enum ports nport, uint8_t *pdll);
int tc956x_logstat_get_pcie_cur_speed(void __iomem *pbase_addr, enum ports nport, uint8_t *pspeed_val);
int tc956x_logstat_get_pcie_cur_width(void __iomem *pbase_addr, enum ports nport, uint8_t *plane_width_val);
int tc956x_logstat_set_state_log_enable(void __iomem *pbase_addr, enum ports nport, enum state_log_enable enable);
#endif /* #ifdef TC956X_PCIE_LOGSTAT */
#endif /* __TC956X_PCIE_LOGSTAT_H__ */
