/*-
 * Copyright (c) 2016 Stanislav Galabov.
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

#ifndef	__MTKSWITCH_RT3050_H__
#define	__MTKSWITCH_RT3050_H__

#define	MTKSWITCH_PVID(p)	((((p) >> 1) * 4) + 0x40)
#define		PVID_OFF(p)	(((p) & 1) ? 12 : 0)
#define		PVID_MASK	0xfff

#define	MTKSWITCH_VLANI(v)	((((v) >> 1) * 4) + 0x50)
#define		VLANI_OFF(v)	(((v) & 1) ? 12 : 0)
#define		VLANI_MASK	0xfff

#define MTKSWITCH_VMSC(x)	((((x) >> 2) * 4) + 0x70)
#define		VMSC_OFF(x)	((x & 3) * 8)
#define		VMSC_MASK	0xff

#define	MTKSWITCH_POA		0x0080
#define		POA_PRT_DPX(x)	((1<<9)<<(x))
#define		POA_FE_SPEED(x) ((1<<0)<<(x))
#define		POA_GE_SPEED(v, x)	((((v)>>5)>>(((x)-5)*2)) & 0x3)
#define		POA_FE_XFC(x)	((1<<16)<<(x))
#define		POA_GE_XFC(v, x)	((((v)>>21)>>(((x)-5)*2)) & 0x3)
#define		POA_PRT_LINK(x)	((1<<25)<<(x))
#define		POA_GE_XFC_TX_MSK	0x2
#define		POA_GE_XFC_RX_MSK	0x1
#define		POA_GE_SPEED_10		0x0
#define		POA_GE_SPEED_100	0x1
#define		POA_GE_SPEED_1000	0x2

#define	MTKSWITCH_FPA		0x0084
#define		FPA_ALL_AUTO	0x00000000

#define	MTKSWITCH_POC2		0x0098
#define		POC2_UNTAG_PORT(x)	(1 << (x))
#define		POC2_UNTAG_VLAN		(1 << 15)

#define	MTKSWITCH_STRT		0x00a0
#define		STRT_RESET	0xffffffff

#define	MTKSWITCH_PCR0		0x00c0
#define		PCR0_WRITE	(1<<13)
#define		PCR0_READ	(1<<14)
#define		PCR0_ACTIVE	(PCR0_WRITE | PCR0_READ)
#define		PCR0_REG(x)	(((x) & 0x1f) << 8)
#define		PCR0_PHY(x)	((x) & 0x1f)
#define		PCR0_DATA(x)	(((x) & 0xffff) << 16)

#define	MTKSWITCH_PCR1		0x00c4
#define		PCR1_DATA_OFF	16
#define		PCR1_DATA_MASK	0xffff

#define	MTKSWITCH_SGC2		0x00e4
#define		SGC2_DOUBLE_TAG_PORT(x)	(1 << (x))

#define	MTKSWITCH_VUB(x)	((((x) >> 2) * 4) + 0x100)
#define		VUB_OFF(x)	((x & 3) * 7)
#define		VUB_MASK	0x7f

#define	MTKSWITCH_PORT_IS_100M(x)	((x) < 5)

#endif	/* __MTKSWITCH_RT3050_H__ */
