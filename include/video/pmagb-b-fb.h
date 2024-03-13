/*
 *	linux/include/video/pmagb-b-fb.h
 *
 *	TURBOchannel PMAGB-B Smart Frame Buffer (SFB) card support,
 *	Copyright (C) 1999, 2000, 2001 by
 *	Michael Engel <engel@unix-ag.org> and
 *	Karsten Merker <merker@linuxtag.org>
 *	Copyright (c) 2005  Maciej W. Rozycki
 *
 *	This file is subject to the terms and conditions of the GNU General
 *	Public License.  See the file COPYING in the main directory of this
 *	archive for more details.
 */

/* IOmem resource offsets.  */
#define PMAGB_B_ROM		0x000000	/* REX option ROM */
#define PMAGB_B_SFB		0x100000	/* SFB ASIC */
#define PMAGB_B_GP0		0x140000	/* general purpose output 0 */
#define PMAGB_B_GP1		0x180000	/* general purpose output 1 */
#define PMAGB_B_BT459		0x1c0000	/* Bt459 RAMDAC */
#define PMAGB_B_FBMEM		0x200000	/* frame buffer */
#define PMAGB_B_SIZE		0x400000	/* address space size */

/* IOmem register offsets.  */
#define SFB_REG_VID_HOR		0x64		/* video horizontal setup */
#define SFB_REG_VID_VER		0x68		/* video vertical setup */
#define SFB_REG_VID_BASE	0x6c		/* video base address */
#define SFB_REG_TCCLK_COUNT	0x78		/* TURBOchannel clock count */
#define SFB_REG_VIDCLK_COUNT	0x7c		/* video clock count */

/* Video horizontal setup register constants.  All bits are r/w.  */
#define SFB_VID_HOR_BP_SHIFT	0x15		/* back porch */
#define SFB_VID_HOR_BP_MASK	0x7f
#define SFB_VID_HOR_SYN_SHIFT	0x0e		/* sync pulse */
#define SFB_VID_HOR_SYN_MASK	0x7f
#define SFB_VID_HOR_FP_SHIFT	0x09		/* front porch */
#define SFB_VID_HOR_FP_MASK	0x1f
#define SFB_VID_HOR_PIX_SHIFT	0x00		/* active video */
#define SFB_VID_HOR_PIX_MASK	0x1ff

/* Video vertical setup register constants.  All bits are r/w.  */
#define SFB_VID_VER_BP_SHIFT	0x16		/* back porch */
#define SFB_VID_VER_BP_MASK	0x3f
#define SFB_VID_VER_SYN_SHIFT	0x10		/* sync pulse */
#define SFB_VID_VER_SYN_MASK	0x3f
#define SFB_VID_VER_FP_SHIFT	0x0b		/* front porch */
#define SFB_VID_VER_FP_MASK	0x1f
#define SFB_VID_VER_SL_SHIFT	0x00		/* active scan lines */
#define SFB_VID_VER_SL_MASK	0x7ff

/* Video base address register constants.  All bits are r/w.  */
#define SFB_VID_BASE_MASK	0x1ff		/* video base row address */

/* Bt459 register offsets, byte-wide registers.  */
#define BT459_ADDR_LO		0x0		/* address low */
#define BT459_ADDR_HI		0x4		/* address high */
#define BT459_DATA		0x8		/* data window register */
#define BT459_CMAP		0xc		/* color map window register */
