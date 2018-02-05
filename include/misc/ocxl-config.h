// SPDX-License-Identifier: GPL-2.0+
// Copyright 2017 IBM Corp.
#ifndef _OCXL_CONFIG_H_
#define _OCXL_CONFIG_H_

/*
 * This file lists the various constants used to read the
 * configuration space of an opencapi adapter.
 *
 * It follows the specification for opencapi 3.0
 */

#define OCXL_EXT_CAP_ID_DVSEC                 0x23

#define OCXL_DVSEC_VENDOR_OFFSET              0x4
#define OCXL_DVSEC_ID_OFFSET                  0x8
#define OCXL_DVSEC_TL_ID                      0xF000
#define   OCXL_DVSEC_TL_BACKOFF_TIMERS          0x10
#define   OCXL_DVSEC_TL_RECV_CAP                0x18
#define   OCXL_DVSEC_TL_SEND_CAP                0x20
#define   OCXL_DVSEC_TL_RECV_RATE               0x30
#define   OCXL_DVSEC_TL_SEND_RATE               0x50
#define OCXL_DVSEC_FUNC_ID                    0xF001
#define   OCXL_DVSEC_FUNC_OFF_INDEX             0x08
#define   OCXL_DVSEC_FUNC_OFF_ACTAG             0x0C
#define OCXL_DVSEC_AFU_INFO_ID                0xF003
#define   OCXL_DVSEC_AFU_INFO_AFU_IDX           0x0A
#define   OCXL_DVSEC_AFU_INFO_OFF               0x0C
#define   OCXL_DVSEC_AFU_INFO_DATA              0x10
#define OCXL_DVSEC_AFU_CTRL_ID                0xF004
#define   OCXL_DVSEC_AFU_CTRL_AFU_IDX           0x0A
#define   OCXL_DVSEC_AFU_CTRL_TERM_PASID        0x0C
#define   OCXL_DVSEC_AFU_CTRL_ENABLE            0x0F
#define   OCXL_DVSEC_AFU_CTRL_PASID_SUP         0x10
#define   OCXL_DVSEC_AFU_CTRL_PASID_EN          0x11
#define   OCXL_DVSEC_AFU_CTRL_PASID_BASE        0x14
#define   OCXL_DVSEC_AFU_CTRL_ACTAG_SUP         0x18
#define   OCXL_DVSEC_AFU_CTRL_ACTAG_EN          0x1A
#define   OCXL_DVSEC_AFU_CTRL_ACTAG_BASE        0x1C
#define OCXL_DVSEC_VENDOR_ID                  0xF0F0
#define   OCXL_DVSEC_VENDOR_CFG_VERS            0x0C
#define   OCXL_DVSEC_VENDOR_TLX_VERS            0x10
#define   OCXL_DVSEC_VENDOR_DLX_VERS            0x20

#endif /* _OCXL_CONFIG_H_ */
