/*-
 * Copyright 1992,1993,1994,1995,1996,1997 by Kevin E. Martin, Chapel Hill, North Carolina.
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and that
 * both that copyright notice and this permission notice appear in
 * supporting documentation, and that the name of Kevin E. Martin not be
 * used in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  Kevin E. Martin
 * makes no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * KEVIN E. MARTIN, RICKARD E. FAITH, AND TIAGO GONS DISCLAIM ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
 * ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
 * ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 *
 * Modified for the Mach-8 by Rickard E. Faith (faith@cs.unc.edu)
 * Modified for the Mach32 by Kevin E. Martin (martin@cs.unc.edu)
 * Modified for the Mach64 by Kevin E. Martin (martin@cs.unc.edu)
 *
 *	from: NetBSD: machfbreg.h,v 1.1 2002/10/24 18:15:57 junyoung Exp
 *
 * $FreeBSD$
 */

#ifndef _DEV_FB_MACHFB_H_
#define	_DEV_FB_MACHFB_H_

/* NON-GUI MEMORY MAPPED Registers - expressed in BYTE offsets */

#define	CRTC_H_TOTAL_DISP	0x0000	/* Dword offset 00 */
#define	CRTC_H_SYNC_STRT_WID	0x0004	/* Dword offset 01 */
#define	CRTC_V_TOTAL_DISP	0x0008	/* Dword offset 02 */
#define	CRTC_V_SYNC_STRT_WID	0x000C	/* Dword offset 03 */
#define	CRTC_VLINE_CRNT_VLINE	0x0010	/* Dword offset 04 */
#define	CRTC_OFF_PITCH		0x0014	/* Dword offset 05 */
#define	CRTC_INT_CNTL		0x0018	/* Dword offset 06 */
#define	CRTC_GEN_CNTL		0x001C	/* Dword offset 07 */

#define	DSP_CONFIG		0x0020	/* Dword offset 08 */
#define	DSP_ON_OFF		0x0024	/* Dword offset 09 */

#define	SHARED_CNTL		0x0038	/* Dword offset 0E */

#define	OVR_CLR			0x0040	/* Dword offset 10 */
#define	OVR_WID_LEFT_RIGHT	0x0044	/* Dword offset 11 */
#define	OVR_WID_TOP_BOTTOM	0x0048	/* Dword offset 12 */

#define	CUR_CLR0		0x0060	/* Dword offset 18 */
#define	CUR_CLR1		0x0064	/* Dword offset 19 */
#define	CUR_OFFSET		0x0068	/* Dword offset 1A */
#define	CUR_HORZ_VERT_POSN	0x006C	/* Dword offset 1B */
#define	CUR_HORZ_VERT_OFF	0x0070	/* Dword offset 1C */

#define	HW_DEBUG		0x007C	/* Dword offset 1F */

#define	SCRATCH_REG0		0x0080	/* Dword offset 20 */
#define	SCRATCH_REG1		0x0084	/* Dword offset 21 */

#define	CLOCK_CNTL		0x0090	/* Dword offset 24 */

#define	BUS_CNTL		0x00A0	/* Dword offset 28 */

#define	LCD_INDEX		0x00A4	/* Dword offset 29 (LTPro) */
#define	LCD_DATA		0x00A8	/* Dword offset 2A (LTPro) */

#define	MEM_CNTL		0x00B0	/* Dword offset 2C */

#define	MEM_VGA_WP_SEL		0x00B4	/* Dword offset 2D */
#define	MEM_VGA_RP_SEL		0x00B8	/* Dword offset 2E */

#define	DAC_REGS		0x00C0	/* Dword offset 30 */
#define	DAC_WINDEX		0x00C0	/* Dword offset 30 */
#define	DAC_DATA		0x00C1	/* Dword offset 30 */
#define	DAC_MASK		0x00C2	/* Dword offset 30 */
#define	DAC_RINDEX		0x00C3	/* Dword offset 30 */
#define	DAC_CNTL		0x00C4	/* Dword offset 31 */

#define	HORZ_STRETCHING		0x00C8	/* Dword offset 32 (LT) */
#define	VERT_STRETCHING		0x00CC	/* Dword offset 33 (LT) */

#define	GEN_TEST_CNTL		0x00D0	/* Dword offset 34 */

#define	LCD_GEN_CNTL		0x00D4	/* Dword offset 35 (LT) */
#define	POWER_MANAGEMENT	0x00D8	/* Dword offset 36 (LT) */

#define	CONFIG_CNTL		0x00DC	/* Dword offset 37 (CT, ET, VT) */
#define	CONFIG_CHIP_ID		0x00E0	/* Dword offset 38 */
#define	CONFIG_STAT0		0x00E4	/* Dword offset 39 */
#define	CONFIG_STAT1		0x00E8	/* Dword offset 3A */


/* GUI MEMORY MAPPED Registers */

#define	DST_OFF_PITCH		0x0100	/* Dword offset 40 */
#define	DST_X			0x0104	/* Dword offset 41 */
#define	DST_Y			0x0108	/* Dword offset 42 */
#define	DST_Y_X			0x010C	/* Dword offset 43 */
#define	DST_WIDTH		0x0110	/* Dword offset 44 */
#define	DST_HEIGHT		0x0114	/* Dword offset 45 */
#define	DST_HEIGHT_WIDTH	0x0118	/* Dword offset 46 */
#define	DST_X_WIDTH		0x011C	/* Dword offset 47 */
#define	DST_BRES_LNTH		0x0120	/* Dword offset 48 */
#define	DST_BRES_ERR		0x0124	/* Dword offset 49 */
#define	DST_BRES_INC		0x0128	/* Dword offset 4A */
#define	DST_BRES_DEC		0x012C	/* Dword offset 4B */
#define	DST_CNTL		0x0130	/* Dword offset 4C */

#define	SRC_OFF_PITCH		0x0180	/* Dword offset 60 */
#define	SRC_X			0x0184	/* Dword offset 61 */
#define	SRC_Y			0x0188	/* Dword offset 62 */
#define	SRC_Y_X			0x018C	/* Dword offset 63 */
#define	SRC_WIDTH1		0x0190	/* Dword offset 64 */
#define	SRC_HEIGHT1		0x0194	/* Dword offset 65 */
#define	SRC_HEIGHT1_WIDTH1	0x0198	/* Dword offset 66 */
#define	SRC_X_START		0x019C	/* Dword offset 67 */
#define	SRC_Y_START		0x01A0	/* Dword offset 68 */
#define	SRC_Y_X_START		0x01A4	/* Dword offset 69 */
#define	SRC_WIDTH2		0x01A8	/* Dword offset 6A */
#define	SRC_HEIGHT2		0x01AC	/* Dword offset 6B */
#define	SRC_HEIGHT2_WIDTH2	0x01B0	/* Dword offset 6C */
#define	SRC_CNTL		0x01B4	/* Dword offset 6D */

#define	HOST_DATA0		0x0200	/* Dword offset 80 */
#define	HOST_DATA1		0x0204	/* Dword offset 81 */
#define	HOST_DATA2		0x0208	/* Dword offset 82 */
#define	HOST_DATA3		0x020C	/* Dword offset 83 */
#define	HOST_DATA4		0x0210	/* Dword offset 84 */
#define	HOST_DATA5		0x0214	/* Dword offset 85 */
#define	HOST_DATA6		0x0218	/* Dword offset 86 */
#define	HOST_DATA7		0x021C	/* Dword offset 87 */
#define	HOST_DATA8		0x0220	/* Dword offset 88 */
#define	HOST_DATA9		0x0224	/* Dword offset 89 */
#define	HOST_DATAA		0x0228	/* Dword offset 8A */
#define	HOST_DATAB		0x022C	/* Dword offset 8B */
#define	HOST_DATAC		0x0230	/* Dword offset 8C */
#define	HOST_DATAD		0x0234	/* Dword offset 8D */
#define	HOST_DATAE		0x0238	/* Dword offset 8E */
#define	HOST_DATAF		0x023C	/* Dword offset 8F */
#define	HOST_CNTL		0x0240	/* Dword offset 90 */

#define	PAT_REG0		0x0280	/* Dword offset A0 */
#define	PAT_REG1		0x0284	/* Dword offset A1 */
#define	PAT_CNTL		0x0288	/* Dword offset A2 */

#define	SC_LEFT			0x02A0	/* Dword offset A8 */
#define	SC_RIGHT		0x02A4	/* Dword offset A9 */
#define	SC_LEFT_RIGHT		0x02A8	/* Dword offset AA */
#define	SC_TOP			0x02AC	/* Dword offset AB */
#define	SC_BOTTOM		0x02B0	/* Dword offset AC */
#define	SC_TOP_BOTTOM		0x02B4	/* Dword offset AD */

#define	DP_BKGD_CLR		0x02C0	/* Dword offset B0 */
#define	DP_FRGD_CLR		0x02C4	/* Dword offset B1 */
#define	DP_WRITE_MASK		0x02C8	/* Dword offset B2 */
#define	DP_CHAIN_MASK		0x02CC	/* Dword offset B3 */
#define	DP_PIX_WIDTH		0x02D0	/* Dword offset B4 */
#define	DP_MIX			0x02D4	/* Dword offset B5 */
#define	DP_SRC			0x02D8	/* Dword offset B6 */

#define	CLR_CMP_CLR		0x0300	/* Dword offset C0 */
#define	CLR_CMP_MASK		0x0304	/* Dword offset C1 */
#define	CLR_CMP_CNTL		0x0308	/* Dword offset C2 */

#define	FIFO_STAT		0x0310	/* Dword offset C4 */

#define	CONTEXT_MASK		0x0320	/* Dword offset C8 */
#define	CONTEXT_LOAD_CNTL	0x032C	/* Dword offset CB */

#define	GUI_TRAJ_CNTL		0x0330	/* Dword offset CC */
#define	GUI_STAT		0x0338	/* Dword offset CE */


/* CRTC control values */

#define	CRTC_HSYNC_NEG		0x00200000
#define	CRTC_VSYNC_NEG		0x00200000

#define	CRTC_DBL_SCAN_EN	0x00000001
#define	CRTC_INTERLACE_EN	0x00000002
#define	CRTC_HSYNC_DIS		0x00000004
#define	CRTC_VSYNC_DIS		0x00000008
#define	CRTC_CSYNC_EN		0x00000010
#define	CRTC_PIX_BY_2_EN	0x00000020
#define	CRTC_DISPLAY_DIS	0x00000040
#define	CRTC_VGA_XOVERSCAN	0x00000080

#define	CRTC_PIX_WIDTH		0x00000700
#define	CRTC_PIX_WIDTH_4BPP	0x00000100
#define	CRTC_PIX_WIDTH_8BPP	0x00000200
#define	CRTC_PIX_WIDTH_15BPP	0x00000300
#define	CRTC_PIX_WIDTH_16BPP	0x00000400
#define	CRTC_PIX_WIDTH_24BPP	0x00000500
#define	CRTC_PIX_WIDTH_32BPP	0x00000600

#define	CRTC_BYTE_PIX_ORDER	0x00000800
#define	CRTC_PIX_ORDER_MSN_LSN	0x00000000
#define	CRTC_PIX_ORDER_LSN_MSN	0x00000800

#define	CRTC_FIFO_LWM		0x000f0000
#define	CRTC_LOCK_REGS		0x00400000
#define	CRTC_EXT_DISP_EN	0x01000000
#define	CRTC_EN			0x02000000
#define	CRTC_DISP_REQ_EN	0x04000000
#define	CRTC_VGA_LINEAR		0x08000000
#define	CRTC_VSYNC_FALL_EDGE	0x10000000
#define	CRTC_VGA_TEXT_132	0x20000000
#define	CRTC_CNT_EN		0x40000000
#define	CRTC_CUR_B_TEST		0x80000000

#define	CRTC_CRNT_VLINE		0x07f00000
#define	CRTC_VBLANK		0x00000001

/* DAC control values */

#define	DAC_EXT_SEL_RS2		0x01
#define	DAC_EXT_SEL_RS3		0x02
#define	DAC_8BIT_EN		0x00000100
#define	DAC_PIX_DLY_MASK	0x00000600
#define	DAC_PIX_DLY_0NS		0x00000000
#define	DAC_PIX_DLY_2NS		0x00000200
#define	DAC_PIX_DLY_4NS		0x00000400
#define	DAC_BLANK_ADJ_MASK	0x00001800
#define	DAC_BLANK_ADJ_0		0x00000000
#define	DAC_BLANK_ADJ_1		0x00000800
#define	DAC_BLANK_ADJ_2		0x00001000


/* Mix control values */

#define	MIX_NOT_DST		0x0000
#define	MIX_0			0x0001
#define	MIX_1			0x0002
#define	MIX_DST			0x0003
#define	MIX_NOT_SRC		0x0004
#define	MIX_XOR			0x0005
#define	MIX_XNOR		0x0006
#define	MIX_SRC			0x0007
#define	MIX_NAND		0x0008
#define	MIX_NOT_SRC_OR_DST	0x0009
#define	MIX_SRC_OR_NOT_DST	0x000a
#define	MIX_OR			0x000b
#define	MIX_AND			0x000c
#define	MIX_SRC_AND_NOT_DST	0x000d
#define	MIX_NOT_SRC_AND_DST	0x000e
#define	MIX_NOR			0x000f

/* Maximum engine dimensions */
#define	ENGINE_MIN_X		0
#define	ENGINE_MIN_Y		0
#define	ENGINE_MAX_X		4095
#define	ENGINE_MAX_Y		16383

/* Mach64 engine bit constants - these are typically ORed together */

/* HW_DEBUG register constants */
/* For RagePro only... */
#define	AUTO_FF_DIS		0x000001000
#define	AUTO_BLKWRT_DIS		0x000002000

/* BUS_CNTL register constants */
#define	BUS_FIFO_ERR_ACK	0x00200000
#define	BUS_HOST_ERR_ACK	0x00800000
#define	BUS_APER_REG_DIS	0x00000010

/* GEN_TEST_CNTL register constants */
#define	GEN_OVR_OUTPUT_EN	0x20
#define	HWCURSOR_ENABLE		0x80
#define	GUI_ENGINE_ENABLE	0x100
#define	BLOCK_WRITE_ENABLE	0x200

/* DSP_CONFIG register constants */
#define	DSP_XCLKS_PER_QW	0x00003fff
#define	DSP_LOOP_LATENCY	0x000f0000
#define	DSP_PRECISION		0x00700000

/* DSP_ON_OFF register constants */
#define	DSP_OFF			0x000007ff
#define	DSP_ON			0x07ff0000

/* SHARED_CNTL register constants */
#define	CTD_FIFO5		0x01000000

/* CLOCK_CNTL register constants */
#define	CLOCK_SEL		0x0f
#define	CLOCK_DIV		0x30
#define	CLOCK_DIV1		0x00
#define	CLOCK_DIV2		0x10
#define	CLOCK_DIV4		0x20
#define	CLOCK_STROBE		0x40
#define	PLL_WR_EN		0x02

/* PLL registers */
#define	PLL_MACRO_CNTL		0x01
#define	PLL_REF_DIV		0x02
#define	PLL_GEN_CNTL		0x03
#define	MCLK_FB_DIV		0x04
#define	PLL_VCLK_CNTL		0x05
#define	VCLK_POST_DIV		0x06
#define	VCLK0_FB_DIV		0x07
#define	VCLK1_FB_DIV		0x08
#define	VCLK2_FB_DIV		0x09
#define	VCLK3_FB_DIV		0x0A
#define	PLL_XCLK_CNTL		0x0B
#define	PLL_TEST_CTRL		0x0E
#define	PLL_TEST_COUNT		0x0F

/* Memory types for CT, ET, VT, GT */
#define	DRAM			1
#define	EDO_DRAM		2
#define	PSEUDO_EDO		3
#define	SDRAM			4
#define	SGRAM			5
#define	SGRAM32			6

#define	DAC_INTERNAL		0x00
#define	DAC_IBMRGB514		0x01
#define	DAC_ATI68875		0x02
#define	DAC_TVP3026_A		0x72
#define	DAC_BT476		0x03
#define	DAC_BT481		0x04
#define	DAC_ATT20C491		0x14
#define	DAC_SC15026		0x24
#define	DAC_MU9C1880		0x34
#define	DAC_IMSG174		0x44
#define	DAC_ATI68860_B		0x05
#define	DAC_ATI68860_C		0x15
#define	DAC_TVP3026_B		0x75
#define	DAC_STG1700		0x06
#define	DAC_ATT498		0x16
#define	DAC_STG1702		0x07
#define	DAC_SC15021		0x17
#define	DAC_ATT21C498		0x27
#define	DAC_STG1703		0x37
#define	DAC_CH8398		0x47
#define	DAC_ATT20C408		0x57

#define	CLK_ATI18818_0		0
#define	CLK_ATI18818_1		1
#define	CLK_STG1703		2
#define	CLK_CH8398		3
#define	CLK_INTERNAL		4
#define	CLK_ATT20C408		5
#define	CLK_IBMRGB514		6

/* DST_CNTL register constants */
#define	DST_X_RIGHT_TO_LEFT	0
#define	DST_X_LEFT_TO_RIGHT	1
#define	DST_Y_BOTTOM_TO_TOP	0
#define	DST_Y_TOP_TO_BOTTOM	2
#define	DST_X_MAJOR		0
#define	DST_Y_MAJOR		4
#define	DST_X_TILE		8
#define	DST_Y_TILE		0x10
#define	DST_LAST_PEL		0x20
#define	DST_POLYGON_ENABLE	0x40
#define	DST_24_ROTATION_ENABLE	0x80

/* SRC_CNTL register constants */
#define	SRC_PATTERN_ENABLE	1
#define	SRC_ROTATION_ENABLE	2
#define	SRC_LINEAR_ENABLE	4
#define	SRC_BYTE_ALIGN		8
#define	SRC_LINE_X_RIGHT_TO_LEFT	0
#define	SRC_LINE_X_LEFT_TO_RIGHT	0x10

/* HOST_CNTL register constants */
#define	HOST_BYTE_ALIGN		1

/* DP_CHAIN_MASK register constants */
#define	DP_CHAIN_4BPP		0x8888
#define	DP_CHAIN_7BPP		0xD2D2
#define	DP_CHAIN_8BPP		0x8080
#define	DP_CHAIN_8BPP_RGB	0x9292
#define	DP_CHAIN_15BPP		0x4210
#define	DP_CHAIN_16BPP		0x8410
#define	DP_CHAIN_24BPP		0x8080
#define	DP_CHAIN_32BPP		0x8080

/* DP_PIX_WIDTH register constants */
#define	DST_1BPP		0
#define	DST_4BPP		1
#define	DST_8BPP		2
#define	DST_15BPP		3
#define	DST_16BPP		4
#define	DST_32BPP		6
#define	SRC_1BPP		0
#define	SRC_4BPP		0x100
#define	SRC_8BPP		0x200
#define	SRC_15BPP		0x300
#define	SRC_16BPP		0x400
#define	SRC_32BPP		0x600
#define	HOST_1BPP		0
#define	HOST_4BPP		0x10000
#define	HOST_8BPP		0x20000
#define	HOST_15BPP		0x30000
#define	HOST_16BPP		0x40000
#define	HOST_32BPP		0x60000
#define	BYTE_ORDER_MSB_TO_LSB	0
#define	BYTE_ORDER_LSB_TO_MSB	0x1000000

/* DP_SRC register constants */
#define	BKGD_SRC_BKGD_CLR	0
#define	BKGD_SRC_FRGD_CLR	1
#define	BKGD_SRC_HOST		2
#define	BKGD_SRC_BLIT		3
#define	BKGD_SRC_PATTERN	4
#define	FRGD_SRC_BKGD_CLR	0
#define	FRGD_SRC_FRGD_CLR	0x100
#define	FRGD_SRC_HOST		0x200
#define	FRGD_SRC_BLIT		0x300
#define	FRGD_SRC_PATTERN	0x400
#define	MONO_SRC_ONE		0
#define	MONO_SRC_PATTERN	0x10000
#define	MONO_SRC_HOST		0x20000
#define	MONO_SRC_BLIT		0x30000

/* PCI IDs */
#define	ATI_VENDOR		0x1002
#define	ATI_MACH64_CT		0x4354	/* Mach64 CT */
#define	ATI_RAGE_PRO_AGP	0x4742	/* 3D Rage Pro (AGP) */
#define	ATI_RAGE_PRO_AGP1X	0x4744	/* 3D Rage Pro (AGP 1x) */
#define	ATI_RAGE_PRO_PCI_B	0x4749	/* 3D Rage Pro Turbo */
#define	ATI_RAGE_XC_PCI66	0x474c	/* Rage XC (PCI66) */
#define	ATI_RAGE_XL_AGP		0x474d	/* Rage XL (AGP) */
#define	ATI_RAGE_XC_AGP		0x474e	/* Rage XC (AGP) */
#define	ATI_RAGE_XL_PCI66	0x474f	/* Rage XL (PCI66) */
#define	ATI_RAGE_PRO_PCI_P	0x4750	/* 3D Rage Pro */
#define	ATI_RAGE_PRO_PCI_L	0x4751	/* 3D Rage Pro (limited 3D) */
#define	ATI_RAGE_XL_PCI		0x4752	/* Rage XL */
#define	ATI_RAGE_XC_PCI		0x4753	/* Rage XC */
#define	ATI_RAGE_II		0x4754	/* 3D Rage I/II */
#define	ATI_RAGE_IIP		0x4755	/* 3D Rage II+ */
#define	ATI_RAGE_IIC_PCI	0x4756	/* 3D Rage IIC */
#define	ATI_RAGE_IIC_AGP_B	0x4757	/* 3D Rage IIC (AGP) */
#define	ATI_RAGE_IIC_AGP_P	0x475a	/* 3D Rage IIC (AGP) */
#define	ATI_RAGE_LT_PRO_AGP	0x4c42	/* 3D Rage LT Pro (AGP 133MHz) */
#define	ATI_RAGE_MOB_M3_PCI	0x4c45	/* Rage Mobility M3 */
#define	ATI_RAGE_MOB_M3_AGP	0x4c46	/* Rage Mobility M3 (AGP) */
#define	ATI_RAGE_LT		0x4c47	/* 3D Rage LT */
#define	ATI_RAGE_LT_PRO_PCI	0x4c49	/* 3D Rage LT Pro */
#define	ATI_RAGE_MOBILITY	0x4c4d	/* Rage Mobility */
#define	ATI_RAGE_L_MOBILITY	0x4c4e	/* Rage L Mobility */
#define	ATI_RAGE_LT_PRO		0x4c50	/* 3D Rage LT Pro */
#define	ATI_RAGE_LT_PRO2	0x4c51	/* 3D Rage LT Pro */
#define	ATI_RAGE_MOB_M1_PCI	0x4c52	/* Rage Mobility M1 (PCI) */
#define	ATI_RAGE_L_MOB_M1_PCI	0x4c53	/* Rage L Mobility (PCI) */
#define	ATI_MACH64_VT		0x5654	/* Mach64 VT */
#define	ATI_MACH64_VTB		0x5655	/* Mach64 VTB */
#define	ATI_MACH64_VT4		0x5656	/* Mach64 VT4 */

#endif /* !_DEV_FB_MACHFB_H_ */
