/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011
 *	Ben Gray <ben.r.gray@gmail.com>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _TI_I2C_H_
#define	_TI_I2C_H_

/**
 * Header file for the OMAP I2C driver.
 *
 * Simply contains register bit flags.
 */

/*
 * OMAP4 I2C Registers, Summary 1
 */
#define	I2C_REG_IE		0x84
#define		I2C_IE_XDR		(1UL << 14)   /* Transmit draining interrupt */
#define		I2C_IE_RDR		(1UL << 13)   /* Receive draining interrupt */
#define		I2C_IE_AAS		(1UL << 9)    /* Addressed as Slave interrupt */
#define		I2C_IE_BF		(1UL << 8)    /* Bus Free interrupt */
#define		I2C_IE_AERR		(1UL << 7)    /* Access Error interrupt */
#define		I2C_IE_STC		(1UL << 6)    /* Start Condition interrupt */
#define		I2C_IE_GC		(1UL << 5)    /* General Call interrupt */
#define		I2C_IE_XRDY		(1UL << 4)    /* Transmit Data Ready interrupt */
#define		I2C_IE_RRDY		(1UL << 3)    /* Receive Data Ready interrupt */
#define		I2C_IE_ARDY		(1UL << 2)    /* Register Access Ready interrupt */
#define		I2C_IE_NACK		(1UL << 1)    /* No Acknowledgment interrupt */
#define		I2C_IE_AL		(1UL << 0)    /* Arbitration Lost interrupt */
#define	I2C_REG_STAT		0x88
#define		I2C_STAT_XDR		(1UL << 14)
#define		I2C_STAT_RDR		(1UL << 13)
#define		I2C_STAT_BB		(1UL << 12)
#define		I2C_STAT_ROVR		(1UL << 11)
#define		I2C_STAT_XUDF		(1UL << 10)
#define		I2C_STAT_AAS		(1UL << 9)
#define		I2C_STAT_BF		(1UL << 8)
#define		I2C_STAT_AERR		(1UL << 7)
#define		I2C_STAT_STC		(1UL << 6)
#define		I2C_STAT_GC		(1UL << 5)
#define		I2C_STAT_XRDY		(1UL << 4)
#define		I2C_STAT_RRDY		(1UL << 3)
#define		I2C_STAT_ARDY		(1UL << 2)
#define		I2C_STAT_NACK		(1UL << 1)
#define		I2C_STAT_AL		(1UL << 0)
#define	I2C_REG_SYSS		0x90
#define		I2C_SYSS_RDONE		(1UL << 0)
#define	I2C_REG_BUF		0x94
#define		I2C_BUF_RXFIFO_CLR	(1UL << 14)
#define		I2C_BUF_TXFIFO_CLR	(1UL << 6)
#define		I2C_BUF_RXTRSH_SHIFT	8
#define		I2C_BUF_TRSH_MASK	0x3f
#define	I2C_REG_CNT		0x98
#define	I2C_REG_DATA		0x9c
#define	I2C_REG_CON		0xa4
#define		I2C_CON_I2C_EN		(1UL << 15)
#define		I2C_CON_OPMODE_STD	(0UL << 12)
#define		I2C_CON_OPMODE_HS	(1UL << 12)
#define		I2C_CON_OPMODE_SCCB	(2UL << 12)
#define		I2C_CON_OPMODE_MASK	(3UL << 13)
#define		I2C_CON_I2C_STB		(1UL << 11)
#define		I2C_CON_MST		(1UL << 10)
#define		I2C_CON_TRX		(1UL << 9)
#define		I2C_CON_XSA		(1UL << 8)
#define		I2C_CON_XOA0		(1UL << 7)
#define		I2C_CON_XOA1		(1UL << 6)
#define		I2C_CON_XOA2		(1UL << 5)
#define		I2C_CON_XOA3		(1UL << 4)
#define		I2C_CON_STP		(1UL << 1)
#define		I2C_CON_STT		(1UL << 0)
#define	I2C_REG_OA0		0xa8
#define	I2C_REG_SA		0xac
#define	I2C_REG_PSC		0xb0
#define		I2C_PSC_MASK		0xff
#define	I2C_REG_SCLL		0xb4
#define		I2C_SCLL_MASK		0xff
#define		I2C_HSSCLL_SHIFT	8
#define	I2C_REG_SCLH		0xb8
#define		I2C_SCLH_MASK		0xff
#define		I2C_HSSCLH_SHIFT	8
#define	I2C_REG_SYSTEST		0xbc
#define	I2C_REG_BUFSTAT		0xc0
#define		I2C_BUFSTAT_FIFODEPTH_MASK	0x3
#define		I2C_BUFSTAT_FIFODEPTH_SHIFT	14
#define	I2C_REG_OA1		0xc4
#define	I2C_REG_OA2		0xc8
#define	I2C_REG_OA3		0xcc
#define	I2C_REG_ACTOA		0xd0
#define	I2C_REG_SBLOCK		0xd4

/*
 * OMAP4 I2C Registers, Summary 2 
 */
#define	I2C_REG_REVNB_LO	0x00
#define	I2C_REG_REVNB_HI	0x04
#define	I2C_REG_SYSC		0x10
#define		I2C_REG_SYSC_SRST	(1UL << 1)
#define	I2C_REG_STATUS_RAW	0x24
#define	I2C_REG_STATUS		0x28
#define	I2C_REG_IRQENABLE_SET	0x2C
#define	I2C_REG_IRQENABLE_CLR	0x30

#define	I2C_CLK			96000000UL	/* 96MHz */
#define	I2C_ICLK		12000000UL	/* 12MHz */

#endif /* _TI_I2C_H_ */
