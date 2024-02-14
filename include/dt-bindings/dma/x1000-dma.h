/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This header provides macros for X1000 DMA bindings.
 *
 * Copyright (c) 2019 Zhou Yanjie <zhouyanjie@zoho.com>
 */

#ifndef __DT_BINDINGS_DMA_X1000_DMA_H__
#define __DT_BINDINGS_DMA_X1000_DMA_H__

/*
 * Request type numbers for the X1000 DMA controller (written to the DRTn
 * register for the channel).
 */
#define X1000_DMA_DMIC_RX	0x5
#define X1000_DMA_I2S0_TX	0x6
#define X1000_DMA_I2S0_RX	0x7
#define X1000_DMA_AUTO		0x8
#define X1000_DMA_UART2_TX	0x10
#define X1000_DMA_UART2_RX	0x11
#define X1000_DMA_UART1_TX	0x12
#define X1000_DMA_UART1_RX	0x13
#define X1000_DMA_UART0_TX	0x14
#define X1000_DMA_UART0_RX	0x15
#define X1000_DMA_SSI0_TX	0x16
#define X1000_DMA_SSI0_RX	0x17
#define X1000_DMA_MSC0_TX	0x1a
#define X1000_DMA_MSC0_RX	0x1b
#define X1000_DMA_MSC1_TX	0x1c
#define X1000_DMA_MSC1_RX	0x1d
#define X1000_DMA_PCM0_TX	0x20
#define X1000_DMA_PCM0_RX	0x21
#define X1000_DMA_SMB0_TX	0x24
#define X1000_DMA_SMB0_RX	0x25
#define X1000_DMA_SMB1_TX	0x26
#define X1000_DMA_SMB1_RX	0x27
#define X1000_DMA_SMB2_TX	0x28
#define X1000_DMA_SMB2_RX	0x29

#endif /* __DT_BINDINGS_DMA_X1000_DMA_H__ */
