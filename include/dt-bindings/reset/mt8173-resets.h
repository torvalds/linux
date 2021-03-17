/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Flora Fu, MediaTek
 */

#ifndef _DT_BINDINGS_RESET_CONTROLLER_MT8173
#define _DT_BINDINGS_RESET_CONTROLLER_MT8173

/* INFRACFG resets */
#define MT8173_INFRA_EMI_REG_RST        0
#define MT8173_INFRA_DRAMC0_A0_RST      1
#define MT8173_INFRA_APCIRQ_EINT_RST    3
#define MT8173_INFRA_APXGPT_RST         4
#define MT8173_INFRA_SCPSYS_RST         5
#define MT8173_INFRA_KP_RST             6
#define MT8173_INFRA_PMIC_WRAP_RST      7
#define MT8173_INFRA_MPIP_RST           8
#define MT8173_INFRA_CEC_RST            9
#define MT8173_INFRA_EMI_RST            32
#define MT8173_INFRA_DRAMC0_RST         34
#define MT8173_INFRA_APMIXEDSYS_RST     35
#define MT8173_INFRA_MIPI_DSI_RST       36
#define MT8173_INFRA_TRNG_RST           37
#define MT8173_INFRA_SYSIRQ_RST         38
#define MT8173_INFRA_MIPI_CSI_RST       39
#define MT8173_INFRA_GCE_FAXI_RST       40
#define MT8173_INFRA_MMIOMMURST         47


/*  PERICFG resets */
#define MT8173_PERI_UART0_SW_RST        0
#define MT8173_PERI_UART1_SW_RST        1
#define MT8173_PERI_UART2_SW_RST        2
#define MT8173_PERI_UART3_SW_RST        3
#define MT8173_PERI_IRRX_SW_RST         4
#define MT8173_PERI_PWM_SW_RST          8
#define MT8173_PERI_AUXADC_SW_RST       10
#define MT8173_PERI_DMA_SW_RST          11
#define MT8173_PERI_I2C6_SW_RST         13
#define MT8173_PERI_NFI_SW_RST          14
#define MT8173_PERI_THERM_SW_RST        16
#define MT8173_PERI_MSDC2_SW_RST        17
#define MT8173_PERI_MSDC3_SW_RST        18
#define MT8173_PERI_MSDC0_SW_RST        19
#define MT8173_PERI_MSDC1_SW_RST        20
#define MT8173_PERI_I2C0_SW_RST         22
#define MT8173_PERI_I2C1_SW_RST         23
#define MT8173_PERI_I2C2_SW_RST         24
#define MT8173_PERI_I2C3_SW_RST         25
#define MT8173_PERI_I2C4_SW_RST         26
#define MT8173_PERI_HDMI_SW_RST         29
#define MT8173_PERI_SPI0_SW_RST         33

#endif  /* _DT_BINDINGS_RESET_CONTROLLER_MT8173 */
