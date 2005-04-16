/*
 * Permedia2 framebuffer driver definitions.
 * Copyright (c) 1998-2000 Ilario Nardinocchi (nardinoc@CS.UniBO.IT)
 * --------------------------------------------------------------------------
 * $Id: pm2fb.h,v 1.26 2000/09/19 00:11:53 illo Exp $
 * --------------------------------------------------------------------------
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#ifndef PM2FB_H
#define PM2FB_H

#define PM2_REFERENCE_CLOCK	14318			/* in KHz */
#define PM2_MAX_PIXCLOCK	230000			/* in KHz */
#define PM2_REGS_SIZE		0x10000

#define PM2TAG(r) (u32 )(((r)-0x8000)>>3)

/*****************************************************************************
 * Permedia2 registers used in the framebuffer
 *****************************************************************************/
 
#define PM2R_RESET_STATUS				0x0000
#define PM2R_IN_FIFO_SPACE				0x0018
#define PM2R_OUT_FIFO_WORDS				0x0020
#define PM2R_APERTURE_ONE				0x0050
#define PM2R_APERTURE_TWO				0x0058
#define PM2R_FIFO_DISCON				0x0068
#define PM2R_CHIP_CONFIG				0x0070

#define PM2R_REBOOT					0x1000
#define PM2R_MEM_CONTROL				0x1040
#define PM2R_BOOT_ADDRESS				0x1080
#define PM2R_MEM_CONFIG					0x10c0
#define PM2R_BYPASS_WRITE_MASK				0x1100
#define PM2R_FRAMEBUFFER_WRITE_MASK			0x1140

#define PM2R_OUT_FIFO					0x2000

#define PM2R_SCREEN_BASE				0x3000
#define PM2R_SCREEN_STRIDE				0x3008
#define PM2R_H_TOTAL					0x3010
#define PM2R_HG_END					0x3018
#define PM2R_HB_END					0x3020
#define PM2R_HS_START					0x3028
#define PM2R_HS_END					0x3030
#define PM2R_V_TOTAL					0x3038
#define PM2R_VB_END					0x3040
#define PM2R_VS_START					0x3048
#define PM2R_VS_END					0x3050
#define PM2R_VIDEO_CONTROL				0x3058
#define PM2R_LINE_COUNT					0x3070
#define PM2R_FIFO_CONTROL				0x3078

#define PM2R_RD_PALETTE_WRITE_ADDRESS			0x4000
#define PM2R_RD_PALETTE_DATA				0x4008
#define PM2R_RD_PIXEL_MASK				0x4010
#define PM2R_RD_PALETTE_READ_ADDRESS			0x4018
#define PM2R_RD_INDEXED_DATA				0x4050

#define PM2R_START_X_DOM				0x8000
#define PM2R_D_X_DOM					0x8008
#define PM2R_START_X_SUB				0x8010
#define PM2R_D_X_SUB					0x8018
#define PM2R_START_Y					0x8020
#define PM2R_D_Y					0x8028
#define PM2R_COUNT					0x8030
#define PM2R_RENDER					0x8038
#define PM2R_RASTERIZER_MODE				0x80a0
#define PM2R_RECTANGLE_ORIGIN				0x80d0
#define PM2R_RECTANGLE_SIZE				0x80d8
#define PM2R_PACKED_DATA_LIMITS				0x8150
#define PM2R_SCISSOR_MODE				0x8180
#define PM2R_SCREEN_SIZE				0x8198
#define PM2R_AREA_STIPPLE_MODE				0x81a0
#define PM2R_WINDOW_ORIGIN				0x81c8
#define PM2R_TEXTURE_ADDRESS_MODE			0x8380
#define PM2R_TEXTURE_MAP_FORMAT				0x8588
#define PM2R_TEXTURE_DATA_FORMAT			0x8590
#define PM2R_TEXTURE_READ_MODE				0x8670
#define PM2R_TEXEL_LUT_MODE				0x8678
#define PM2R_TEXTURE_COLOR_MODE				0x8680
#define PM2R_FOG_MODE					0x8690
#define PM2R_COLOR_DDA_MODE				0x87e0
#define PM2R_ALPHA_BLEND_MODE				0x8810
#define PM2R_DITHER_MODE				0x8818
#define PM2R_FB_SOFT_WRITE_MASK				0x8820
#define PM2R_LOGICAL_OP_MODE				0x8828
#define PM2R_LB_READ_MODE				0x8880
#define PM2R_LB_READ_FORMAT				0x8888
#define PM2R_LB_SOURCE_OFFSET				0x8890
#define PM2R_LB_WINDOW_BASE				0x88b8
#define PM2R_LB_WRITE_FORMAT				0x88c8
#define PM2R_STENCIL_MODE				0x8988
#define PM2R_DEPTH_MODE					0x89a0
#define PM2R_FB_READ_MODE				0x8a80
#define PM2R_FB_SOURCE_OFFSET				0x8a88
#define PM2R_FB_PIXEL_OFFSET				0x8a90
#define PM2R_FB_WINDOW_BASE				0x8ab0
#define PM2R_FB_WRITE_MODE				0x8ab8
#define PM2R_FB_HARD_WRITE_MASK				0x8ac0
#define PM2R_FB_BLOCK_COLOR				0x8ac8
#define PM2R_FB_READ_PIXEL				0x8ad0
#define PM2R_FILTER_MODE				0x8c00
#define PM2R_SYNC					0x8c40
#define PM2R_YUV_MODE					0x8f00
#define PM2R_STATISTICS_MODE				0x8c08
#define PM2R_FB_SOURCE_DELTA				0x8d88
#define PM2R_CONFIG					0x8d90
#define PM2R_DELTA_MODE					0x9300

/* Permedia2v */
#define PM2VR_RD_INDEX_LOW				0x4020
#define PM2VR_RD_INDEX_HIGH				0x4028
#define PM2VR_RD_INDEXED_DATA				0x4030

/* Permedia2 RAMDAC indexed registers */
#define PM2I_RD_CURSOR_CONTROL				0x06
#define PM2I_RD_COLOR_MODE				0x18
#define PM2I_RD_MODE_CONTROL				0x19
#define PM2I_RD_MISC_CONTROL				0x1e
#define PM2I_RD_PIXEL_CLOCK_A1				0x20
#define PM2I_RD_PIXEL_CLOCK_A2				0x21
#define PM2I_RD_PIXEL_CLOCK_A3				0x22
#define PM2I_RD_PIXEL_CLOCK_STATUS			0x29
#define PM2I_RD_MEMORY_CLOCK_1				0x30
#define PM2I_RD_MEMORY_CLOCK_2				0x31
#define PM2I_RD_MEMORY_CLOCK_3				0x32
#define PM2I_RD_MEMORY_CLOCK_STATUS			0x33
#define PM2I_RD_COLOR_KEY_CONTROL			0x40
#define PM2I_RD_OVERLAY_KEY				0x41
#define PM2I_RD_RED_KEY					0x42
#define PM2I_RD_GREEN_KEY				0x43
#define PM2I_RD_BLUE_KEY				0x44

/* Permedia2v extensions */
#define PM2VI_RD_MISC_CONTROL				0x000
#define PM2VI_RD_SYNC_CONTROL				0x001
#define PM2VI_RD_DAC_CONTROL				0x002
#define PM2VI_RD_PIXEL_SIZE				0x003
#define PM2VI_RD_COLOR_FORMAT				0x004
#define PM2VI_RD_CURSOR_MODE				0x005
#define PM2VI_RD_CURSOR_X_LOW				0x007
#define PM2VI_RD_CURSOR_X_HIGH				0x008
#define PM2VI_RD_CURSOR_Y_LOW				0x009
#define PM2VI_RD_CURSOR_Y_HIGH				0x00A
#define PM2VI_RD_CURSOR_X_HOT				0x00B
#define PM2VI_RD_CURSOR_Y_HOT				0x00C
#define PM2VI_RD_CLK0_PRESCALE				0x201
#define PM2VI_RD_CLK0_FEEDBACK				0x202
#define PM2VI_RD_CLK0_POSTSCALE				0x203
#define PM2VI_RD_CLK1_PRESCALE				0x204
#define PM2VI_RD_CLK1_FEEDBACK				0x205
#define PM2VI_RD_CLK1_POSTSCALE				0x206
#define PM2VI_RD_CURSOR_PALETTE				0x303
#define PM2VI_RD_CURSOR_PATTERN				0x400

/* Fields and flags */
#define PM2F_RENDER_AREASTIPPLE				(1L<<0)
#define PM2F_RENDER_FASTFILL				(1L<<3)
#define PM2F_RENDER_PRIMITIVE_MASK			(3L<<6)
#define PM2F_RENDER_LINE				0
#define PM2F_RENDER_TRAPEZOID				(1L<<6)
#define PM2F_RENDER_POINT				(2L<<6)
#define PM2F_RENDER_RECTANGLE				(3L<<6)
#define PM2F_SYNCHRONIZATION				(1L<<10)
#define PM2F_PLL_LOCKED					0x10
#define PM2F_BEING_RESET				(1L<<31)
#define PM2F_DATATYPE_COLOR				0x8000
#define PM2F_VGA_ENABLE					0x02
#define PM2F_VGA_FIXED					0x04
#define PM2F_FB_WRITE_ENABLE				0x01
#define PM2F_FB_READ_SOURCE_ENABLE			0x0200
#define PM2F_RD_PALETTE_WIDTH_8				0x02
#define PM2F_PART_PROD_MASK				0x01ff
#define PM2F_SCREEN_SCISSOR_ENABLE			0x02
#define PM2F_DATA_64_ENABLE				0x00010000
#define PM2F_BLANK_LOW					0x02
#define PM2F_HSYNC_MASK					0x18
#define PM2F_VSYNC_MASK					0x60
#define PM2F_HSYNC_ACT_HIGH				0x08
#define PM2F_HSYNC_FORCED_LOW				0x10
#define PM2F_HSYNC_ACT_LOW				0x18
#define PM2F_VSYNC_ACT_HIGH				0x20
#define PM2F_VSYNC_FORCED_LOW				0x40
#define PM2F_VSYNC_ACT_LOW				0x60
#define PM2F_LINE_DOUBLE				0x04
#define PM2F_VIDEO_ENABLE				0x01
#define PM2F_RD_PIXELFORMAT_SVGA			0x01
#define PM2F_RD_PIXELFORMAT_RGB232OFFSET		0x02
#define PM2F_RD_PIXELFORMAT_RGBA2321			0x03
#define PM2F_RD_PIXELFORMAT_RGBA5551			0x04
#define PM2F_RD_PIXELFORMAT_RGBA4444			0x05
#define PM2F_RD_PIXELFORMAT_RGB565			0x06
#define PM2F_RD_PIXELFORMAT_RGBA8888			0x08
#define PM2F_RD_PIXELFORMAT_RGB888			0x09
#define PM2F_RD_GUI_ACTIVE				0x10
#define PM2F_RD_COLOR_MODE_RGB				0x20
#define PM2F_DELTA_ORDER_RGB				(1L<<18)
#define PM2F_RD_TRUECOLOR				0x80
#define PM2F_NO_ALPHA_BUFFER				0x10
#define PM2F_TEXTEL_SIZE_16				0x00080000
#define PM2F_TEXTEL_SIZE_32				0x00100000
#define PM2F_TEXTEL_SIZE_4				0x00180000
#define PM2F_TEXTEL_SIZE_24				0x00200000
#define PM2F_INCREASE_X					(1L<<21)
#define PM2F_INCREASE_Y					(1L<<22)
#define PM2F_CONFIG_FB_WRITE_ENABLE			(1L<<3)
#define PM2F_CONFIG_FB_PACKED_DATA			(1L<<2)
#define PM2F_CONFIG_FB_READ_DEST_ENABLE			(1L<<1)
#define PM2F_CONFIG_FB_READ_SOURCE_ENABLE		(1L<<0)
#define PM2F_COLOR_KEY_TEST_OFF				(1L<<4)
#define PM2F_MEM_CONFIG_RAM_MASK			(3L<<29)
#define PM2F_MEM_BANKS_1				0L
#define PM2F_MEM_BANKS_2				(1L<<29)
#define PM2F_MEM_BANKS_3				(2L<<29)
#define PM2F_MEM_BANKS_4				(3L<<29)
#define PM2F_APERTURE_STANDARD				0
#define PM2F_APERTURE_BYTESWAP				1
#define PM2F_APERTURE_HALFWORDSWAP			2

typedef enum {
	PM2_TYPE_PERMEDIA2,
	PM2_TYPE_PERMEDIA2V
} pm2type_t;

#endif /* PM2FB_H */

/*****************************************************************************
 * That's all folks!
 *****************************************************************************/
