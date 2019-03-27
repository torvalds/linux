/*-
 * Copyright (c) 2016 Jared McNeill <jmcneill@invisible.ca>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Ingenic JZ4780 LCD Controller
 */

#ifndef __JZ4780_LCD_H__
#define __JZ4780_LCD_H__

#define	LCDCFG			0x0000
#define	 LCDCFG_LCDPIN		(1 << 31)
#define	 LCDCFG_TVEPEH		(1 << 30)
#define	 LCDCFG_NEWDES		(1 << 28)
#define	 LCDCFG_PALBP		(1 << 27)
#define	 LCDCFG_TVEN		(1 << 26)
#define	 LCDCFG_RECOVER		(1 << 25)
#define	 LCDCFG_PSM		(1 << 23)
#define	 LCDCFG_CLSM		(1 << 22)
#define	 LCDCFG_SPLM		(1 << 21)
#define	 LCDCFG_REVM		(1 << 20)
#define	 LCDCFG_HSYNM		(1 << 19)
#define	 LCDCFG_VSYNM		(1 << 18)
#define	 LCDCFG_INVDAT		(1 << 17)
#define	 LCDCFG_SYNDIR		(1 << 16)
#define	 LCDCFG_PSP		(1 << 15)
#define	 LCDCFG_CLSP		(1 << 14)
#define	 LCDCFG_SPLP		(1 << 13)
#define	 LCDCFG_REVP		(1 << 12)
#define	 LCDCFG_HSP		(1 << 11)
#define	 LCDCFG_PCP		(1 << 10)
#define	 LCDCFG_DEP		(1 << 9)
#define	 LCDCFG_VSP		(1 << 8)
#define	 LCDCFG_18_16		(1 << 7)
#define	 LCDCFG_24		(1 << 6)
#define	 LCDCFG_MODE		(0xf << 0)
#define	LCDCTRL			0x0030
#define	 LCDCTRL_PINMD		(1 << 31)
#define	 LCDCTRL_BST		(0x7 << 28)
#define	  LCDCTRL_BST_4		(0 << 28)
#define	  LCDCTRL_BST_8		(1 << 28)
#define	  LCDCTRL_BST_16	(2 << 28)
#define	  LCDCTRL_BST_32	(3 << 28)
#define	  LCDCTRL_BST_64	(4 << 28)
#define	 LCDCTRL_OUTRGB		(1 << 27)
#define	 LCDCTRL_OFUP		(1 << 26)
#define	 LCDCTRL_DACTE		(1 << 14)
#define	 LCDCTRL_EOFM		(1 << 13)
#define	 LCDCTRL_SOFM		(1 << 12)
#define	 LCDCTRL_OFUM		(1 << 11)
#define	 LCDCTRL_IFUM0		(1 << 10)
#define	 LCDCTRL_IFUM1		(1 << 9)
#define	 LCDCTRL_LDDM		(1 << 8)
#define	 LCDCTRL_QDM		(1 << 7)
#define	 LCDCTRL_BEDN		(1 << 6)
#define	 LCDCTRL_PEDN		(1 << 5)
#define	 LCDCTRL_DIS		(1 << 4)
#define	 LCDCTRL_ENA		(1 << 3)
#define	 LCDCTRL_BPP0		(0x7 << 0)
#define	  LCDCTRL_BPP0_1	(0 << 0)
#define	  LCDCTRL_BPP0_2	(1 << 0)
#define	  LCDCTRL_BPP0_4	(2 << 0)
#define	  LCDCTRL_BPP0_8	(3 << 0)
#define	  LCDCTRL_BPP0_15_16	(4 << 0)
#define	  LCDCTRL_BPP0_18_24	(5 << 0)
#define	  LCDCTRL_BPP0_24_COMP	(6 << 0)
#define	  LCDCTRL_BPP0_30	(7 << 0)
#define	 LCDCTR
#define	LCDSTATE		0x0034
#define	 LCDSTATE_QD		(1 << 7)
#define	 LCDSTATE_EOF		(1 << 5)
#define	 LCDSTATE_SOF		(1 << 4)
#define	 LCDSTATE_OUT		(1 << 3)
#define	 LCDSTATE_IFU0		(1 << 2)
#define	 LCDSTATE_IFU1		(1 << 1)
#define	 LCDSTATE_LDD		(1 << 0)
#define	LCDOSDC			0x0100
#define	LCDOSDCTRL		0x0104
#define	LCDOSDS			0x0108
#define	LCDBGC0			0x010c
#define	LCDBGC1			0x02c4
#define	LCDKEY0			0x0110
#define	LCDKEY1			0x0114
#define	LCDALPHA		0x0118
#define	LCDIPUR			0x011c
#define	LCDRGBC			0x0090
#define	 LCDRGBC_RGBDM		(1 << 15)
#define	 LCDRGBC_DMM		(1 << 14)
#define	 LCDRGBC_422		(1 << 8)
#define	 LCDRGBC_RGBFMT		(1 << 7)
#define	 LCDRGBC_ODDRGB		(0x7 << 4)
#define	 LCDRGBC_EVENRGB	(0x7 << 0)
#define	LCDVAT			0x000c
#define	 LCDVAT_HT_SHIFT	16
#define	 LCDVAT_VT_SHIFT	0
#define	LCDDAH			0x0010
#define	 LCDDAH_HDS_SHIFT	16
#define	 LCDDAH_HDE_SHIFT	0
#define	LCDDAV			0x0014
#define	 LCDDAV_VDS_SHIFT	16
#define	 LCDDAV_VDE_SHIFT	0
#define	LCDXYP0			0x0120
#define	LCDXYP1			0x0124
#define	LCDSIZE0		0x0128
#define	LCDSIZE1		0x012c
#define	LCDVSYNC		0x0004
#define	LCDHSYNC		0x0008
#define	LCDPS			0x0018
#define	LCDCLS			0x001c
#define	LCDSPL			0x0020
#define	LCDREV			0x0024
#define	LCDIID			0x0038
#define	LCDDA0			0x0040
#define	LCDSA0			0x0044
#define	LCDFID0			0x0048
#define	LCDCMD0			0x004c
#define	 LCDCMD_SOFINT		(1 << 31)
#define	 LCDCMD_EOFINT		(1 << 30)
#define	 LCDCMD_CMD		(1 << 29)
#define	 LCDCMD_COMPE		(1 << 27)
#define	 LCDCMD_FRM_EN		(1 << 26)
#define	 LCDCMD_FIELD_SEL	(1 << 25)
#define	 LCDCMD_16X16BLOCK	(1 << 24)
#define	 LCDCMD_LEN		(0xffffff << 0)
#define	LCDOFFS0		0x0060
#define	LCDPW0			0x0064
#define	LCDCNUM0		0x0068
#define	LCDPOS0			LCDCNUM0
#define	 LCDPOS_ALPHAMD1	(1 << 31)
#define	 LCDPOS_RGB01		(1 << 30)
#define	 LCDPOS_BPP01		(0x7 << 27)
#define	  LCDPOS_BPP01_15_16	(4 << 27)
#define	  LCDPOS_BPP01_18_24	(5 << 27)
#define	  LCDPOS_BPP01_24_COMP	(6 << 27)
#define	  LCDPOS_BPP01_30	(7 << 27)
#define	  LCDPOS_PREMULTI01	(1 << 26)
#define	  LCDPOS_COEF_SLE01	(0x3 << 24)
#define	  LCDPOS_COEF_BLE01_1	(1 << 24)
#define	  LCDPOS_YPOS01		(0xfff << 12)
#define	  LCDPOS_XPOS01		(0xfff << 0)
#define	LCDDESSIZE0		0x006c
#define	 LCDDESSIZE_ALPHA	(0xff << 24)
#define	 LCDDESSIZE_HEIGHT	(0xfff << 12)
#define	 LCDDESSIZE_HEIGHT_SHIFT 12
#define	 LCDDESSIZE_WIDTH	(0xfff << 0)
#define	 LCDDESSIZE_WIDTH_SHIFT	0
#define	LCDDA1			0x0050
#define	LCDSA1			0x0054
#define	LCDFID1			0x0058
#define	LCDCMD1			0x005c
#define	LCDOFFS1		0x0070
#define	LCDPW1			0x0074
#define	LCDCNUM1		0x0078
#define	LCDPOS1			LCDCNUM1
#define	LCDDESSIZE1		0x007c
#define	LCDPCFG			0x02c0
#define	LCDDUALCTRL		0x02c8
#define	LCDENH_CFG		0x0400
#define	LCDENH_CSCCFG		0x0404
#define	LCDENH_LUMACFG		0x0408
#define	LCDENH_CHROCFG0		0x040c
#define	LCDENH_CHROCFG1		0x0410
#define	LCDENH_DITHERCFG	0x0414
#define	LCDENH_STATUS		0x0418
#define	LCDENH_GAMMA		0x0800	/* base */
#define	LCDENH_VEE		0x1000	/* base */

struct lcd_frame_descriptor {
	uint32_t	next;
	uint32_t	physaddr;
	uint32_t	id;
	uint32_t	cmd;
	uint32_t	offs;
	uint32_t	pw;
	uint32_t	cnum_pos;
	uint32_t	dessize;
} __packed;

#endif /* !__JZ4780_LCD_H__ */
