/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 iopsys Software Solutions AB.
 * Copyright (C) 2025 Genexis AB.
 *
 * Author: Mikhail Kshevetskiy <mikhail.kshevetskiy@iopsys.eu>
 *
 * based on
 *   include/dt-bindings/reset/airoha,en7581-reset.h
 * by Lorenzo Bianconi <lorenzo@kernel.org>
 */

#ifndef __DT_BINDINGS_RESET_CONTROLLER_AIROHA_EN7523_H_
#define __DT_BINDINGS_RESET_CONTROLLER_AIROHA_EN7523_H_

/* RST_CTRL2 */
#define EN7523_XPON_PHY_RST		 0
#define EN7523_XSI_MAC_RST		 1
#define EN7523_XSI_PHY_RST		 2
#define EN7523_NPU_RST			 3
#define EN7523_I2S_RST			 4
#define EN7523_TRNG_RST			 5
#define EN7523_TRNG_MSTART_RST		 6
#define EN7523_DUAL_HSI0_RST		 7
#define EN7523_DUAL_HSI1_RST		 8
#define EN7523_HSI_RST			 9
#define EN7523_DUAL_HSI0_MAC_RST	10
#define EN7523_DUAL_HSI1_MAC_RST	11
#define EN7523_HSI_MAC_RST		12
#define EN7523_WDMA_RST			13
#define EN7523_WOE0_RST			14
#define EN7523_WOE1_RST			15
#define EN7523_HSDMA_RST		16
#define EN7523_I2C2RBUS_RST		17
#define EN7523_TDMA_RST			18
/* RST_CTRL1 */
#define EN7523_PCM1_ZSI_ISI_RST		19
#define EN7523_FE_PDMA_RST		20
#define EN7523_FE_QDMA_RST		21
#define EN7523_PCM_SPIWP_RST		22
#define EN7523_CRYPTO_RST		23
#define EN7523_TIMER_RST		24
#define EN7523_PCM1_RST			25
#define EN7523_UART_RST			26
#define EN7523_GPIO_RST			27
#define EN7523_GDMA_RST			28
#define EN7523_I2C_MASTER_RST		29
#define EN7523_PCM2_ZSI_ISI_RST		30
#define EN7523_SFC_RST			31
#define EN7523_UART2_RST		32
#define EN7523_GDMP_RST			33
#define EN7523_FE_RST			34
#define EN7523_USB_HOST_P0_RST		35
#define EN7523_GSW_RST			36
#define EN7523_SFC2_PCM_RST		37
#define EN7523_PCIE0_RST		38
#define EN7523_PCIE1_RST		39
#define EN7523_PCIE_HB_RST		40
#define EN7523_XPON_MAC_RST		41

#endif /* __DT_BINDINGS_RESET_CONTROLLER_AIROHA_EN7523_H_ */
