/*-
 * Copyright 1992-2015 Michal Meloun
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
#ifndef _TEGRA_DC_REG_H_
#define	_TEGRA_DC_REG_H_

/*
 * !!! WARNING !!!
 * Tegra manual uses registers index (and not register addreses).
 * We follow the TRM notation and index is converted to offset in
 * WR4 / RD4 macros
 */

/* --------------------------- DC CMD -------------------------------------- */
#define	DC_CMD_GENERAL_INCR_SYNCPT		0x000
#define	DC_CMD_GENERAL_INCR_SYNCPT_CNTRL	0x001
#define	 SYNCPT_CNTRL_NO_STALL				(1 << 8)
#define	 SYNCPT_CNTRL_SOFT_RESET			(1 << 0)

#define	DC_CMD_GENERAL_INCR_SYNCPT_ERROR	0x002
#define	DC_CMD_WIN_A_INCR_SYNCPT		0x008
#define	DC_CMD_WIN_A_INCR_SYNCPT_CNTRL		0x009
#define	DC_CMD_WIN_A_INCR_SYNCPT_ERROR		0x00a
#define	DC_CMD_WIN_B_INCR_SYNCPT		0x010
#define	DC_CMD_WIN_B_INCR_SYNCPT_CNTRL		0x011
#define	DC_CMD_WIN_B_INCR_SYNCPT_ERROR		0x012
#define	DC_CMD_WIN_C_INCR_SYNCPT		0x018
#define	DC_CMD_WIN_C_INCR_SYNCPT_CNTRL		0x019
#define	DC_CMD_WIN_C_INCR_SYNCPT_ERROR		0x01a
#define	DC_CMD_CONT_SYNCPT_VSYNC		0x028
#define	 SYNCPT_VSYNC_ENABLE				(1 << 8)

#define	DC_CMD_CTXSW				0x030
#define	DC_CMD_DISPLAY_COMMAND_OPTION0		0x031
#define	DC_CMD_DISPLAY_COMMAND			0x032
#define	 DISPLAY_CTRL_MODE(x)				((x) << 5)
#define	   CTRL_MODE_STOP					0
#define	   CTRL_MODE_C_DISPLAY					1
#define	   CTRL_MODE_NC_DISPLAY					2

#define	DC_CMD_SIGNAL_RAISE			0x033
#define	DC_CMD_DISPLAY_POWER_CONTROL		0x036
#define	 PM1_ENABLE					(1 << 18)
#define	 PM0_ENABLE					(1 << 16)
#define	 PW4_ENABLE					(1 <<  8)
#define	 PW3_ENABLE					(1 <<  6)
#define	 PW2_ENABLE					(1 <<  4)
#define	 PW1_ENABLE					(1 <<  2)
#define	 PW0_ENABLE					(1 <<  0)

#define	DC_CMD_INT_STATUS			0x037
#define	DC_CMD_INT_MASK				0x038
#define	DC_CMD_INT_ENABLE			0x039
#define	DC_CMD_INT_TYPE				0x03a
#define	DC_CMD_INT_POLARITY			0x03b
#define	 WIN_T_UF_INT					(1 << 25)
#define	 WIN_D_UF_INT					(1 << 24)
#define	 HC_UF_INT					(1 << 23)
#define	 CMU_LUT_CONFLICT_INT				(1 << 22)
#define	 WIN_C_OF_INT					(1 << 16)
#define	 WIN_B_OF_INT					(1 << 15)
#define	 WIN_A_OF_INT					(1 << 14)
#define	 SSF_INT					(1 << 13)
#define	 MSF_INT					(1 << 12)
#define	 WIN_C_UF_INT					(1 << 10)
#define	 WIN_B_UF_INT					(1 << 9)
#define	 WIN_A_UF_INT					(1 << 8)
#define	 SPI_BUSY_INT					(1 << 6)
#define	 V_PULSE2_INT					(1 << 5)
#define	 V_PULSE3_INT					(1 << 4)
#define	 HBLANK_INT					(1 << 3)
#define	 VBLANK_INT					(1 << 2)
#define	 FRAME_END_INT					(1 << 1)

#define	DC_CMD_STATE_ACCESS			0x040
#define	 WRITE_MUX					(1 << 2)
#define	 READ_MUX					(1 << 0)

#define	DC_CMD_STATE_CONTROL			0x041
#define	 NC_HOST_TRIG					(1 << 24)
#define	 CURSOR_UPDATE					(1 << 15)
#define	 WIN_C_UPDATE					(1 << 11)
#define	 WIN_B_UPDATE					(1 << 10)
#define	 WIN_A_UPDATE					(1 <<  9)
#define	  WIN_UPDATE(x)					(1 <<  (9 + (x)))
#define	 GENERAL_UPDATE					(1 <<  8)
#define	 CURSOR_ACT_REQ					(1 <<  7)
#define	 WIN_D_ACT_REQ					(1 <<  4)
#define	 WIN_C_ACT_REQ					(1 <<  3)
#define	 WIN_B_ACT_REQ					(1 <<  2)
#define	 WIN_A_ACT_REQ					(1 <<  1)
#define	 WIN_ACT_REQ(x)					(1 <<  (1 + (x)))
#define	 GENERAL_ACT_REQ				(1 <<  0)

#define	DC_CMD_DISPLAY_WINDOW_HEADER		0x042
#define	 WINDOW_D_SELECT				(1 << 7)
#define	 WINDOW_C_SELECT				(1 << 6)
#define	 WINDOW_B_SELECT				(1 << 5)
#define	 WINDOW_A_SELECT				(1 << 4)
#define	 WINDOW_SELECT(x)				(1 << (4 + (x)))

#define	DC_CMD_REG_ACT_CONTROL			0x043
#define	DC_CMD_WIN_D_INCR_SYNCPT		0x04c
#define	DC_CMD_WIN_D_INCR_SYNCPT_CNTRL		0x04d
#define	DC_CMD_WIN_D_INCR_SYNCPT_ERROR		0x04e

/* ---------------------------- DC COM ------------------------------------- */

/* --------------------------- DC DISP ------------------------------------- */

#define	DC_DISP_DISP_SIGNAL_OPTIONS0		0x400
#define	 M1_ENABLE					(1 << 26)
#define	 M0_ENABLE					(1 << 24)
#define	 V_PULSE2_ENABLE				(1 << 18)
#define	 V_PULSE1_ENABLE				(1 << 16)
#define	 V_PULSE0_ENABLE				(1 << 14)
#define	 H_PULSE2_ENABLE				(1 << 12)
#define	 H_PULSE1_ENABLE				(1 << 10)
#define	 H_PULSE0_ENABLE				(1 <<  8)

#define	DC_DISP_DISP_SIGNAL_OPTIONS1		0x401

#define	DC_DISP_DISP_WIN_OPTIONS		0x402
#define	 HDMI_ENABLE					(1 << 30)
#define	 DSI_ENABLE					(1 << 29)
#define	 SOR1_TIMING_CYA				(1 << 27)
#define	 SOR1_ENABLE					(1 << 26)
#define	 SOR_ENABLE					(1 << 25)
#define	 CURSOR_ENABLE					(1 << 16)

#define	DC_DISP_DISP_TIMING_OPTIONS		0x405
#define	 VSYNC_H_POSITION(x)				(((x) & 0xfff) << 0)

#define	DC_DISP_REF_TO_SYNC			0x406
#define	DC_DISP_SYNC_WIDTH			0x407
#define	DC_DISP_BACK_PORCH			0x408
#define	DC_DISP_DISP_ACTIVE			0x409
#define	DC_DISP_FRONT_PORCH			0x40a
#define	DC_DISP_H_PULSE0_CONTROL		0x40b
#define	DC_DISP_H_PULSE0_POSITION_A		0x40c
#define	DC_DISP_H_PULSE0_POSITION_B		0x40d
#define	DC_DISP_H_PULSE0_POSITION_C		0x40e
#define	DC_DISP_H_PULSE0_POSITION_D		0x40f
#define	DC_DISP_H_PULSE1_CONTROL		0x410
#define	DC_DISP_H_PULSE1_POSITION_A		0x411
#define	DC_DISP_H_PULSE1_POSITION_B		0x412
#define	DC_DISP_H_PULSE1_POSITION_C		0x413
#define	DC_DISP_H_PULSE1_POSITION_D		0x414
#define	DC_DISP_H_PULSE2_CONTROL		0x415
#define	DC_DISP_H_PULSE2_POSITION_A		0x416
#define	DC_DISP_H_PULSE2_POSITION_B		0x417
#define	DC_DISP_H_PULSE2_POSITION_C		0x418
#define	DC_DISP_H_PULSE2_POSITION_D		0x419
#define	DC_DISP_V_PULSE0_CONTROL		0x41a
#define	DC_DISP_V_PULSE0_POSITION_A		0x41b
#define	DC_DISP_V_PULSE0_POSITION_B		0x41c
#define	DC_DISP_V_PULSE0_POSITION_C		0x41d
#define	DC_DISP_V_PULSE1_CONTROL		0x41e
#define	DC_DISP_V_PULSE1_POSITION_A		0x41f
#define	DC_DISP_V_PULSE1_POSITION_B		0x420
#define	DC_DISP_V_PULSE1_POSITION_C		0x421
#define	DC_DISP_V_PULSE2_CONTROL		0x422
#define	DC_DISP_V_PULSE2_POSITION_A		0x423
#define	DC_DISP_V_PULSE3_CONTROL		0x424
#define	 PULSE_CONTROL_LAST(x)				(((x) & 0x7f) << 8)
#define	  LAST_START_A						0
#define	  LAST_END_A						1
#define	  LAST_START_B						2
#define	  LAST_END_B						3
#define	  LAST_START_C						4
#define	  LAST_END_C						5
#define	  LAST_START_D						6
#define	  LAST_END_D						7
#define	 PULSE_CONTROL_QUAL(x)				(((x) & 0x3) << 8)
#define	  QUAL_ALWAYS						0
#define	  QUAL_VACTIVE						2
#define	  QUAL_VACTIVE1						3
#define	 PULSE_POLARITY					(1 << 4)
#define	 PULSE_MODE					(1 << 3)

#define	DC_DISP_V_PULSE3_POSITION_A		0x425
#define	 PULSE_END(x)					(((x) & 0xfff) << 16)
#define	 PULSE_START(x)					(((x) & 0xfff) <<  0)


#define	DC_DISP_DISP_CLOCK_CONTROL		0x42e
#define	 PIXEL_CLK_DIVIDER(x)				(((x) & 0xf) <<  8)
#define	  PCD1							 0
#define	  PCD1H							 1
#define	  PCD2							 2
#define	  PCD3							 3
#define	  PCD4							 4
#define	  PCD6							 5
#define	  PCD8							 6
#define	  PCD9							 7
#define	  PCD12							 8
#define	  PCD16							 9
#define	  PCD18							10
#define	  PCD24							11
#define	  PCD13							12
#define	 SHIFT_CLK_DIVIDER(x)				((x) & 0xff)

#define	DC_DISP_DISP_INTERFACE_CONTROL		0x42f
#define	 DISP_ORDER_BLUE_RED				( 1 << 9)
#define	 DISP_ALIGNMENT_LSB				( 1 << 8)
#define	 DISP_DATA_FORMAT(x)				(((x) & 0xf) <<  8)
#define	  DF1P1C						 0
#define	  DF1P2C24B						 1
#define	  DF1P2C18B						 2
#define	  DF1P2C16B						 3
#define	  DF1S							 4
#define	  DF2S							 5
#define	  DF3S							 6
#define	  DFSPI							 7
#define	  DF1P3C24B						 8
#define	  DF2P1C18B						 9
#define	  DFDUAL1P1C18B						10

#define	DC_DISP_DISP_COLOR_CONTROL		0x430
#define	 NON_BASE_COLOR					(1 << 18)
#define	 BLANK_COLOR					(1 << 17)
#define	 DISP_COLOR_SWAP				(1 << 16)
#define	 ORD_DITHER_ROTATION(x)				(((x) & 0x3) << 12)
#define	 DITHER_CONTROL(x)				(((x) & 0x3) <<  8)
#define	  DITHER_DISABLE					0
#define	  DITHER_ORDERED					2
#define	  DITHER_TEMPORAL					3
#define	 BASE_COLOR_SIZE(x)				(((x) & 0xF) <<  0)
#define	  SIZE_BASE666						0
#define	  SIZE_BASE111						1
#define	  SIZE_BASE222						2
#define	  SIZE_BASE333						3
#define	  SIZE_BASE444						4
#define	  SIZE_BASE555						5
#define	  SIZE_BASE565						6
#define	  SIZE_BASE332						7
#define	  SIZE_BASE888						8

#define	DC_DISP_CURSOR_START_ADDR		0x43e
#define	 CURSOR_CLIP(x)					(((x) & 0x3) << 28)
#define	  CC_DISPLAY						0
#define	  CC_WA							1
#define	  CC_WB							2
#define	  CC_WC							3
#define	 CURSOR_SIZE(x)					(((x) & 0x3) << 24)
#define	  C32x32						0
#define	  C64x64						1
#define	  C128x128						2
#define	  C256x256						3
#define	 CURSOR_START_ADDR(x)				(((x) >> 10) & 0x3FFFFF)

#define	DC_DISP_CURSOR_POSITION			0x440
#define	 CURSOR_POSITION(h, v)		((((h) & 0x3fff) <<  0) |	\
					 (((v) & 0x3fff) << 16))
#define	DC_DISP_CURSOR_UNDERFLOW_CTRL		0x4eb
#define	DC_DISP_BLEND_CURSOR_CONTROL		0x4f1
#define	 CURSOR_MODE_SELECT				(1 << 24)
#define	 CURSOR_DST_BLEND_FACTOR_SELECT(x)		(((x) & 0x3) << 16)
#define	  DST_BLEND_ZERO					0
#define	  DST_BLEND_K1						1
#define	  DST_NEG_K1_TIMES_SRC					2
#define	 CURSOR_SRC_BLEND_FACTOR_SELECT(x)		(((x) & 0x3) <<  8)
#define	  SRC_BLEND_K1						0
#define	  SRC_BLEND_K1_TIMES_SRC				1
#define	 CURSOR_ALPHA(x)				(((x) & 0xFF) << 0)

#define	DC_DISP_CURSOR_UFLOW_DBG_PIXEL		0x4f3
#define	 CURSOR_UFLOW_CYA				(1 << 7)
#define	 CURSOR_UFLOW_CTRL_DBG_MODE			(1 << 0)
/* --------------------------- DC WIN ------------------------------------- */

#define	DC_WINC_COLOR_PALETTE			0x500
#define	DC_WINC_CSC_YOF				0x611
#define	DC_WINC_CSC_KYRGB			0x612
#define	DC_WINC_CSC_KUR				0x613
#define	DC_WINC_CSC_KVR				0x614
#define	DC_WINC_CSC_KUG				0x615
#define	DC_WINC_CSC_KVG				0x616
#define	DC_WINC_CSC_KUB				0x617
#define	DC_WINC_CSC_KVB				0x618

#define	DC_WINC_WIN_OPTIONS			0x700
#define	 H_FILTER_MODE					(1U << 31)
#define	 WIN_ENABLE					(1 << 30)
#define	 INTERLACE_ENABLE				(1 << 23)
#define	 YUV_RANGE_EXPAND				(1 << 22)
#define	 DV_ENABLE					(1 << 20)
#define	 CSC_ENABLE					(1 << 18)
#define	 CP_ENABLE					(1 << 16)
#define	 V_FILTER_UV_ALIGN				(1 << 14)
#define	 V_FILTER_OPTIMIZE				(1 << 12)
#define	 V_FILTER_ENABLE				(1 << 10)
#define	 H_FILTER_ENABLE				(1 <<  8)
#define	 COLOR_EXPAND					(1 <<  6)
#define	 SCAN_COLUMN					(1 <<  4)
#define	 V_DIRECTION					(1 <<  2)
#define	 H_DIRECTION					(1 <<  0)

#define	DC_WIN_BYTE_SWAP			0x701
#define	 BYTE_SWAP(x)					(((x) & 0x7) << 0)
#define	  NOSWAP						0
#define	  SWAP2							1
#define	  SWAP4							2
#define	  SWAP4HW						3
#define	  SWAP02						4
#define	  SWAPLEFT						5

#define	DC_WIN_COLOR_DEPTH			0x703
#define	WIN_COLOR_DEPTH_P8					 3
#define	WIN_COLOR_DEPTH_B4G4R4A4				 4
#define	WIN_COLOR_DEPTH_B5G5R5A					 5
#define	WIN_COLOR_DEPTH_B5G6R5					 6
#define	WIN_COLOR_DEPTH_AB5G5R5					 7
#define	WIN_COLOR_DEPTH_B8G8R8A8				12
#define	WIN_COLOR_DEPTH_R8G8B8A8				13
#define	WIN_COLOR_DEPTH_YCbCr422				16
#define	WIN_COLOR_DEPTH_YUV422					17
#define	WIN_COLOR_DEPTH_YCbCr420P				18
#define	WIN_COLOR_DEPTH_YUV420P					19
#define	WIN_COLOR_DEPTH_YCbCr422P				20
#define	WIN_COLOR_DEPTH_YUV422P					21
#define	WIN_COLOR_DEPTH_YCbCr422R				22
#define	WIN_COLOR_DEPTH_YUV422R					23
#define	WIN_COLOR_DEPTH_YCbCr422RA				24
#define	WIN_COLOR_DEPTH_YUV422RA				25

#define	DC_WIN_POSITION				0x704
#define	 WIN_POSITION(h, v)		((((h) & 0x1fff) <<  0) |	\
					 (((v) & 0x1fff) << 16))

#define	DC_WIN_SIZE				0x705
#define	 WIN_SIZE(h, v)			((((h) & 0x1fff) <<  0) |	\
					 (((v) & 0x1fff) << 16))

#define	DC_WIN_PRESCALED_SIZE			0x706
#define	 WIN_PRESCALED_SIZE(h, v)	((((h) & 0x7fff) <<  0) |	\
					 (((v) & 0x1fff) << 16))


#define	DC_WIN_H_INITIAL_DDA			0x707
#define	DC_WIN_V_INITIAL_DDA			0x708
#define	DC_WIN_DDA_INCREMENT			0x709
#define	 WIN_DDA_INCREMENT(h, v)	((((h) & 0xffff) <<  0) |	\
					 (((v) & 0xffff) << 16))
#define	DC_WIN_LINE_STRIDE			0x70a

/* -------------------------- DC WINBUF ------------------------------------ */

#define	DC_WINBUF_START_ADDR			0x800
#define	DC_WINBUF_START_ADDR_NS			0x801
#define	DC_WINBUF_START_ADDR_U			0x802
#define	DC_WINBUF_START_ADDR_U_NS		0x803
#define	DC_WINBUF_START_ADDR_V			0x804
#define	DC_WINBUF_START_ADDR_V_NS		0x805
#define	DC_WINBUF_ADDR_H_OFFSET			0x806
#define	DC_WINBUF_ADDR_H_OFFSET_NS		0x807
#define	DC_WINBUF_ADDR_V_OFFSET			0x808
#define	DC_WINBUF_ADDR_V_OFFSET_NS		0x809
#define	DC_WINBUF_UFLOW_STATUS			0x80a
#define	DC_WINBUF_SURFACE_KIND			0x80b
#define	 SURFACE_KIND_BLOCK_HEIGHT(x) 			(((x) & 0x7) << 4)
#define	 SURFACE_KIND_PITCH				0
#define	 SURFACE_KIND_TILED				1
#define	 SURFACE_KIND_BL_16B2				2
#define	DC_WINBUF_SURFACE_WEIGHT		0x80c
#define	DC_WINBUF_START_ADDR_HI			0x80d
#define	DC_WINBUF_START_ADDR_HI_NS		0x80e
#define	DC_WINBUF_START_ADDR_U_HI		0x80f
#define	DC_WINBUF_START_ADDR_U_HI_NS		0x810
#define	DC_WINBUF_START_ADDR_V_HI		0x811
#define	DC_WINBUF_START_ADDR_V_HI_NS		0x812
#define	DC_WINBUF_UFLOW_CTRL			0x824
#define	 UFLOW_CTR_ENABLE				(1 << 0)
#define	DC_WINBUF_UFLOW_DBG_PIXEL		0x825

#endif /* _TEGRA_DC_REG_H_ */
