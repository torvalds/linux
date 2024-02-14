/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * AMD ACP 6.2 Register Documentation
 *
 * Copyright 2022 Advanced Micro Devices, Inc.
 */

#ifndef _rpl_acp6x_OFFSET_HEADER
#define _rpl_acp6x_OFFSET_HEADER

/* Registers from ACP_CLKRST block */
#define ACP_SOFT_RESET                                0x1241000
#define ACP_CONTROL                                   0x1241004
#define ACP_STATUS                                    0x1241008
#define ACP_DYNAMIC_CG_MASTER_CONTROL                 0x1241010
#define ACP_PGFSM_CONTROL                             0x124101C
#define ACP_PGFSM_STATUS                              0x1241020
#define ACP_CLKMUX_SEL                                0x1241024

/* Registers from ACP_AON block */
#define ACP_PME_EN                                    0x1241400
#define ACP_DEVICE_STATE                              0x1241404
#define AZ_DEVICE_STATE                               0x1241408
#define ACP_PIN_CONFIG                                0x1241440
#define ACP_PAD_PULLUP_CTRL                           0x1241444
#define ACP_PAD_PULLDOWN_CTRL                         0x1241448
#define ACP_PAD_DRIVE_STRENGTH_CTRL                   0x124144C
#define ACP_PAD_SCHMEN_CTRL                           0x1241450

#endif
