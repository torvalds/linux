/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This header provides macros for X2000 DMA bindings.
 *
 * Copyright (c) 2020 周琰杰 (Zhou Yanjie) <zhouyanjie@wanyeetech.com>
 */

#ifndef __DT_BINDINGS_DMA_X2000_DMA_H__
#define __DT_BINDINGS_DMA_X2000_DMA_H__

/*
 * Request type numbers for the X2000 DMA controller (written to the DRTn
 * register for the channel).
 */
#define X2000_DMA_AUTO		0x8
#define X2000_DMA_UART5_TX	0xa
#define X2000_DMA_UART5_RX	0xb
#define X2000_DMA_UART4_TX	0xc
#define X2000_DMA_UART4_RX	0xd
#define X2000_DMA_UART3_TX	0xe
#define X2000_DMA_UART3_RX	0xf
#define X2000_DMA_UART2_TX	0x10
#define X2000_DMA_UART2_RX	0x11
#define X2000_DMA_UART1_TX	0x12
#define X2000_DMA_UART1_RX	0x13
#define X2000_DMA_UART0_TX	0x14
#define X2000_DMA_UART0_RX	0x15
#define X2000_DMA_SSI0_TX	0x16
#define X2000_DMA_SSI0_RX	0x17
#define X2000_DMA_SSI1_TX	0x18
#define X2000_DMA_SSI1_RX	0x19
#define X2000_DMA_I2C0_TX	0x24
#define X2000_DMA_I2C0_RX	0x25
#define X2000_DMA_I2C1_TX	0x26
#define X2000_DMA_I2C1_RX	0x27
#define X2000_DMA_I2C2_TX	0x28
#define X2000_DMA_I2C2_RX	0x29
#define X2000_DMA_I2C3_TX	0x2a
#define X2000_DMA_I2C3_RX	0x2b
#define X2000_DMA_I2C4_TX	0x2c
#define X2000_DMA_I2C4_RX	0x2d
#define X2000_DMA_I2C5_TX	0x2e
#define X2000_DMA_I2C5_RX	0x2f
#define X2000_DMA_UART6_TX	0x30
#define X2000_DMA_UART6_RX	0x31
#define X2000_DMA_UART7_TX	0x32
#define X2000_DMA_UART7_RX	0x33
#define X2000_DMA_UART8_TX	0x34
#define X2000_DMA_UART8_RX	0x35
#define X2000_DMA_UART9_TX	0x36
#define X2000_DMA_UART9_RX	0x37
#define X2000_DMA_SADC_RX	0x38

#endif /* __DT_BINDINGS_DMA_X2000_DMA_H__ */
