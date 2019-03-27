/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef __OPENCORE_I2C_H__
#define __OPENCORE_I2C_H__

/* I2C specific registers */
#define OC_I2C_PRESCALE_LO_REG		0x00
#define OC_I2C_PRESCALE_HI_REG		0x01
#define OC_I2C_CTRL_REG			0x02
#define OC_I2C_TRANSMIT_REG		0x03  /* tx and rx - same reg */
#define OC_I2C_RECV_REG			0x03  /* tx and rx - same reg */
#define OC_I2C_DATA_REG			0x03  /* tx and rx - same reg */
#define OC_I2C_CMD_REG			0x04  /* cmd and status - same reg */
#define OC_I2C_STATUS_REG		0x04  /* cmd and status - same reg */

#define XLP_I2C_CLKFREQ			133333333 /* XLP 133 MHz IO clock */
#define XLP_I2C_FREQ			100000	/* default 100kHz */
#define I2C_TIMEOUT			500000

/*
 * These defines pertain to the OpenCores
 * I2C Master Host Controller used in XLP
 */

#define OC_PRESCALER_LO			0
#define OC_PRESCALER_HI			1

#define OC_CONTROL			2
#define OC_CONTROL_EN			0x80
#define OC_CONTROL_IEN			0x40

#define OC_DATA				3	/* Data TX & RX Reg */

#define OC_COMMAND			4
#define OC_COMMAND_START		0x90
#define OC_COMMAND_STOP			0x40
#define OC_COMMAND_READ			0x20
#define OC_COMMAND_WRITE		0x10
#define OC_COMMAND_RDACK		0x20
#define OC_COMMAND_RDNACK		0x28
#define OC_COMMAND_IACK			0x01	/* Not used */

#define OC_STATUS			4	/* Same as 'command' */
#define OC_STATUS_NACK			0x80	/* Did not get an ACK */
#define OC_STATUS_BUSY			0x40
#define OC_STATUS_AL			0x20	/* Arbitration Lost */
#define OC_STATUS_TIP			0x02	/* Transfer in Progress  */
#define OC_STATUS_IF			0x01	/* Intr. Pending Flag */

#endif
