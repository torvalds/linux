/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This header provides macros for JZ4775 DMA bindings.
 *
 * Copyright (c) 2020 周琰杰 (Zhou Yanjie) <zhouyanjie@wanyeetech.com>
 */

#ifndef __DT_BINDINGS_DMA_JZ4775_DMA_H__
#define __DT_BINDINGS_DMA_JZ4775_DMA_H__

/*
 * Request type numbers for the JZ4775 DMA controller (written to the DRTn
 * register for the channel).
 */
#define JZ4775_DMA_I2S0_TX	0x6
#define JZ4775_DMA_I2S0_RX	0x7
#define JZ4775_DMA_AUTO		0x8
#define JZ4775_DMA_SADC_RX	0x9
#define JZ4775_DMA_UART3_TX	0x0e
#define JZ4775_DMA_UART3_RX	0x0f
#define JZ4775_DMA_UART2_TX	0x10
#define JZ4775_DMA_UART2_RX	0x11
#define JZ4775_DMA_UART1_TX	0x12
#define JZ4775_DMA_UART1_RX	0x13
#define JZ4775_DMA_UART0_TX	0x14
#define JZ4775_DMA_UART0_RX	0x15
#define JZ4775_DMA_SSI0_TX	0x16
#define JZ4775_DMA_SSI0_RX	0x17
#define JZ4775_DMA_MSC0_TX	0x1a
#define JZ4775_DMA_MSC0_RX	0x1b
#define JZ4775_DMA_MSC1_TX	0x1c
#define JZ4775_DMA_MSC1_RX	0x1d
#define JZ4775_DMA_MSC2_TX	0x1e
#define JZ4775_DMA_MSC2_RX	0x1f
#define JZ4775_DMA_PCM0_TX	0x20
#define JZ4775_DMA_PCM0_RX	0x21
#define JZ4775_DMA_SMB0_TX	0x24
#define JZ4775_DMA_SMB0_RX	0x25
#define JZ4775_DMA_SMB1_TX	0x26
#define JZ4775_DMA_SMB1_RX	0x27
#define JZ4775_DMA_SMB2_TX	0x28
#define JZ4775_DMA_SMB2_RX	0x29

#endif /* __DT_BINDINGS_DMA_JZ4775_DMA_H__ */
