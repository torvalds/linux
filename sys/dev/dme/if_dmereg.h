/*
 * Copyright (C) 2010 Andrew Turner
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef __IF_DMEREG_H__
#define __IF_DMEREG_H__

/*
 * DM9000 register definitions
 */
#define DME_NCR		0x00
#define  NCR_EXT_PHY	(1<<7)
#define  NCR_WAKEEN	(1<<6)
#define  NCR_FCOL	(1<<4)
#define  NCR_FDX	(1<<3)
#define  NCR_LBK_NORMAL	(0<<1)
#define  NCR_LBK_MAC	(1<<1)
#define  NCR_LBK_PHY	(2<<1)
#define  NCR_RST	(1<<0)
#define DME_NSR		0x01
#define  NSR_SPEED	(1<<7)
#define  NSR_LINKST	(1<<6)
#define  NSR_WAKEST	(1<<5)
#define  NSR_TX2END	(1<<3)
#define  NSR_TX1END	(1<<2)
#define  NSR_RXOV	(1<<1)
#define DME_TCR		0x02
#define  TCR_TJDIS	(1<<6)
#define  TCR_EXCECM	(1<<5)
#define  TCR_PAD_DIS2	(1<<4)
#define  TCR_PAD_CRC2	(1<<3)
#define  TCR_PAD_DIS1	(1<<2)
#define  TCR_PAD_CRC1	(1<<1)
#define  TCR_TXREQ	(1<<0)
#define DME_TSR1	0x03
#define DME_TSR2	0x04
#define DME_RCR		0x05
#define  RCR_WTDIS	(1<<6)
#define  RCR_DIS_LONG	(1<<5)
#define  RCR_DIS_CRC	(1<<4)
#define  RCR_ALL	(1<<3)
#define  RCR_RUNT	(1<<2)
#define  RCR_PRMSC	(1<<1)
#define  RCR_RXEN	(1<<0)
#define DME_RSR		0x06
#define DME_ROCR	0x07
#define DME_BPTR	0x08
#define  BPTR_BPHW(v)	(((v) & 0x0f) << 4)
#define  BPTR_JPT(v)	(((v) & 0x0f) << 0)
#define DME_FCTR	0x09
#define  FCTR_HWOT(v)	(((v) & 0x0f) << 4)
#define  FCTR_LWOT(v)	(((v) & 0x0f) << 0)
#define DME_FCR		0x0A
#define DME_EPCR	0x0B
#define  EPCR_REEP	(1<<5)
#define  EPCR_WEP	(1<<4)
#define  EPCR_EPOS	(1<<3)
#define  EPCR_ERPRR	(1<<2)
#define  EPCR_ERPRW	(1<<1)
#define  EPCR_ERRE	(1<<0)
#define DME_EPAR	0x0C
#define DME_EPDRL	0x0D
#define DME_EPDRH	0x0E
#define DME_WCR		0x0F
#define DME_PAR_BASE	0x10
#define DME_PAR(n)	(DME_PAR_BASE + n)
#define DME_MAR_BASE	0x16
#define DME_MAR(n)	(DME_MAR_BASE + n)
#define DME_GPCR	0x1E
#define DME_GPR		0x1F
#define DME_TRPAL	0x22
#define DME_TRPAH	0x23
#define DME_RWPAL	0x24
#define DME_RWPAH	0x25
#define DME_VIDL	0x28
#define DME_VIDH	0x29
#define DME_PIDL	0x2A
#define DME_PIDH	0x2B
#define DME_CHIPR	0x2C
#define DME_SMCR	0x2F
#define DME_MRCMDX	0xF0
#define DME_MRCMD	0xF2
#define DME_MRRL	0xF4
#define DME_MRRH	0xF5
#define DME_MWCMDX	0xF6
#define DME_MWCMD	0xF8
#define DME_MWRL	0xFA
#define DME_MWRH	0xFB
#define DME_TXPLL	0xFC
#define DME_TXPLH	0xFD
#define DME_ISR		0xFE
#define  ISR_LNKCHG	(1<<5)
#define  ISR_UDRUN	(1<<4)
#define  ISR_ROO	(1<<3)
#define  ISR_ROS	(1<<2)
#define  ISR_PT		(1<<1)
#define  ISR_PR		(1<<0)

#define DME_IMR		0xFF
#define  IMR_PAR	(1<<7)
#define  IMR_LNKCHGI	(1<<5)
#define  IMR_UDRUNI	(1<<4)
#define  IMR_ROOI	(1<<3)
#define  IMR_ROI	(1<<2)
#define  IMR_PTI	(1<<1)
#define  IMR_PRI	(1<<0)

/* Extra PHY register from DM9000B */
#define MII_DME_DSPCR	0x1B
#define DSPCR_INIT	0xE100

#endif /* __DMEREGS_H__ */

