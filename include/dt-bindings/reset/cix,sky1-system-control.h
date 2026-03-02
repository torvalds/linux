/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Author: Jerry Zhu <jerry.zhu@cixtech.com> */
#ifndef DT_BINDING_RESET_CIX_SKY1_SYSTEM_CONTROL_H
#define DT_BINDING_RESET_CIX_SKY1_SYSTEM_CONTROL_H

/* func reset for sky1 fch */
#define SW_I3C0_RST_FUNC_G_N	0
#define SW_I3C0_RST_FUNC_I_N	1
#define SW_I3C1_RST_FUNC_G_N	2
#define SW_I3C1_RST_FUNC_I_N	3
#define SW_UART0_RST_FUNC_N	4
#define SW_UART1_RST_FUNC_N	5
#define SW_UART2_RST_FUNC_N	6
#define SW_UART3_RST_FUNC_N	7
#define SW_TIMER_RST_FUNC_N	8

/* apb reset for sky1 fch */
#define SW_I3C0_RST_APB_N	9
#define SW_I3C1_RST_APB_N	10
#define SW_DMA_RST_AXI_N	11
#define SW_UART0_RST_APB_N	12
#define SW_UART1_RST_APB_N	13
#define SW_UART2_RST_APB_N	14
#define SW_UART3_RST_APB_N	15
#define SW_SPI0_RST_APB_N	16
#define SW_SPI1_RST_APB_N	17
#define SW_I2C0_RST_APB_N	18
#define SW_I2C1_RST_APB_N	19
#define SW_I2C2_RST_APB_N	20
#define SW_I2C3_RST_APB_N	21
#define SW_I2C4_RST_APB_N	22
#define SW_I2C5_RST_APB_N	23
#define SW_I2C6_RST_APB_N	24
#define SW_I2C7_RST_APB_N	25
#define SW_GPIO_RST_APB_N	26

/* fch rst for xspi */
#define SW_XSPI_REG_RST_N	27
#define SW_XSPI_SYS_RST_N	28

#endif
