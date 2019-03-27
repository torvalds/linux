/*-
 * Copyright (c) 2005 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DEV_VT_HW_VGA_VGA_REG_H_
#define	_DEV_VT_HW_VGA_VGA_REG_H_

/*
 * The VGA adapter uses two I/O port blocks. One of these blocks, the CRT
 * controller registers, can be located either at 0x3B0 or at 0x3D0 in I/O
 * port space. This allows compatibility with the monochrome adapter, which
 * has the CRT controller registers at 0x3B0.
 *
 * It is assumed that compatibility with the monochrome adapter is not of
 * interest anymore. As such, the CRT controller can be located at 0x3D0 in
 * I/O port space unconditionally. This means that the 2 I/O blocks are
 * always adjacent and can therefore be treated as a single logical I/O port
 * range. In practical terms: there only has to be a single tag and handle
 * to access all registers.
 *
 * The following definitions are taken from or inspired by:
 *   Programmer's Guide to the EGA, VGA, and Super VGA Cards -- 3rd ed.,
 *     Richard F. Ferraro, Addison-Wesley, ISBN 0-201-62490-7
 */

#define	VGA_MEM_BASE	0xA0000
#define	VGA_MEM_SIZE	0x10000
#define	VGA_TXT_BASE	0xB8000
#define	VGA_TXT_SIZE	0x08000
#define	VGA_REG_BASE	0x3c0
#define	VGA_REG_SIZE	0x10+0x0c

/* Attribute controller registers. */
#define	VGA_AC_WRITE		0x00
#define	VGA_AC_READ		0x01
#define	VGA_AC_PALETTE(x)		(x)	/* 0 <= x <= 15 */
#define		VGA_AC_PAL_SR		0x20	/* Secondary red */
#define		VGA_AC_PAL_SG		0x10	/* Secondary green */
#define		VGA_AC_PAL_SB		0x08	/* Secondary blue */
#define		VGA_AC_PAL_R		0x04	/* Red */
#define		VGA_AC_PAL_G		0x02	/* Green */
#define		VGA_AC_PAL_B		0x01	/* Blue */
#define	VGA_AC_MODE_CONTROL		(32+16)
#define		VGA_AC_MC_IPS		0x80	/* Internal palette size */
#define		VGA_AC_MC_PCS		0x40	/* Pixel clock select */
#define		VGA_AC_MC_PPC		0x20	/* Pixel panning compat. */
#define		VGA_AC_MC_BI		0x08	/* Blink/intensity */
#define		VGA_AC_MC_ELG		0x04	/* Enable line graphics cc. */
#define		VGA_AC_MC_DT		0x02	/* Display type */
#define		VGA_AC_MC_GA		0x01	/* Graphics/alphanumeric */
#define	VGA_AC_OVERSCAN_COLOR		(32+17)
#define	VGA_AC_COLOR_PLANE_ENABLE	(32+18)
#define	VGA_AC_HORIZ_PIXEL_PANNING	(32+19)
#define	VGA_AC_COLOR_SELECT		(32+20)
#define		VGA_AC_CS_C67		0x0C	/* Color reg. addr. bits 6+7 */
#define		VGA_AC_CS_C45		0x03	/* Color reg. addr. bits 4+5 */

/* General registers. */
#define	VGA_GEN_MISC_OUTPUT_W	0x02		/* Write only. */
#define	VGA_GEN_MISC_OUTPUT_R	0x0c		/* Read only. */
#define		VGA_GEN_MO_VSP		0x80	/* Vertical sync. polarity */
#define		VGA_GEN_MO_HSP		0x40	/* Horiz. sync. polarity */
#define		VGA_GEN_MO_PB		0x20	/* Page bit for odd/even */
#define		VGA_GEN_MO_CS		0x0C	/* Clock select */
#define		VGA_GEN_MO_ER		0x02	/* Enable RAM */
#define		VGA_GEN_MO_IOA		0x01	/* Input/output address */
#define	VGA_GEN_INPUT_STAT_0	0x02		/* Read only. */
#define	VGA_GEN_FEATURE_CTRL_W	0x1a		/* Write only. */
#define	VGA_GEN_FEATURE_CTRL_R	0x0a		/* Read only. */
#define	VGA_GEN_INPUT_STAT_1	0x1a		/* Read only. */
#define		VGA_GEN_IS1_VR		0x08	/* Vertical retrace */
#define		VGA_GEN_IS1_DE		0x01	/* Display enable not */

/* Sequencer registers. */
#define	VGA_SEQ_ADDRESS		0x04
#define	VGA_SEQ_RESET			0
#define		VGA_SEQ_RST_SR		0x02	/* Synchronous reset */
#define		VGA_SEQ_RST_NAR		0x01	/* No async. reset */
#define	VGA_SEQ_CLOCKING_MODE		1
#define		VGA_SEQ_CM_SO		0x20	/* Screen off */
#define		VGA_SEQ_CM_S4		0x10	/* Shift four */
#define		VGA_SEQ_CM_DC		0x08	/* Dot clock */
#define		VGA_SEQ_CM_SL		0x04	/* Shift load */
#define		VGA_SEQ_CM_89		0x01	/* 8/9 Dot clocks */
#define	VGA_SEQ_MAP_MASK		2
#define		VGA_SEQ_MM_EM3		0x08	/* Enable memory plane 3 */
#define		VGA_SEQ_MM_EM2		0x04	/* Enable memory plane 2 */
#define		VGA_SEQ_MM_EM1		0x02	/* Enable memory plane 1 */
#define		VGA_SEQ_MM_EM0		0x01	/* Enable memory plane 0 */
#define	VGA_SEQ_CHAR_MAP_SELECT		3
#define		VGA_SEQ_CMS_SAH		0x20	/* Char. A (bit 2) */
#define		VGA_SEQ_CMS_SBH		0x10	/* Char. B (bit 2) */
#define		VGA_SEQ_CMS_SA		0x0C	/* Char. A (bit 0+1) */
#define		VGA_SEQ_CMS_SB		0x03	/* Char. B (bit 0+1) */
#define	VGA_SEQ_MEMORY_MODE		4
#define		VGA_SEQ_MM_C4		0x08	/* Chain four */
#define		VGA_SEQ_MM_OE		0x04	/* Odd/even */
#define		VGA_SEQ_MM_EM		0x02	/* Extended memory */
#define	VGA_SEQ_DATA		0x05

/* Color registers. */
#define	VGA_PEL_MASK		0x06
#define	VGA_PEL_ADDR_RD_MODE	0x07		/* Write only. */
#define	VGA_DAC_STATE		0x07		/* Read only. */
#define	VGA_PEL_ADDR_WR_MODE	0x08
#define	VGA_PEL_DATA		0x09

/* Graphics controller registers. */
#define	VGA_GC_ADDRESS		0x0e
#define	VGA_GC_SET_RESET		0
#define	VGA_GC_ENABLE_SET_RESET		1
#define	VGA_GC_COLOR_COMPARE		2
#define	VGA_GC_DATA_ROTATE		3
#define		VGA_GC_DR_FS_XOR	0x18	/* Function select - XOR */
#define		VGA_GC_DR_FS_OR		0x10	/* Function select - OR */
#define		VGA_GC_DR_FS_AND	0x08	/* Function select - AND */
#define		VGA_GC_DR_RC		0x07	/* Rotate count */
#define	VGA_GC_READ_MAP_SELECT		4
#define	VGA_GC_MODE			5
#define		VGA_GC_MODE_SR		0x60	/* Shift register */
#define		VGA_GC_MODE_OE		0x10	/* Odd/even */
#define		VGA_GC_MODE_RM		0x08	/* Read mode */
#define		VGA_GC_MODE_WM		0x03	/* Write mode */
#define	VGA_GC_MISCELLANEOUS		6
#define		VGA_GC_MISC_MM		0x0C	/* memory map */
#define		VGA_GC_MISC_COE		0x02	/* Chain odd/even */
#define		VGA_GC_MISC_GA		0x01	/* Graphics/text mode */
#define	VGA_GC_COLOR_DONT_CARE		7
#define	VGA_GC_BIT_MASK			8
#define	VGA_GC_DATA		0x0f

/* CRT controller registers. */
#define	VGA_CRTC_ADDRESS	0x14
#define	VGA_CRTC_HORIZ_TOTAL		0
#define	VGA_CRTC_HORIZ_DISP_END		1
#define	VGA_CRTC_START_HORIZ_BLANK	2
#define	VGA_CRTC_END_HORIZ_BLANK	3
#define		VGA_CRTC_EHB_CR		0x80	/* Compatible read */
#define		VGA_CRTC_EHB_DES	0x60	/* Display enable skew */
#define		VGA_CRTC_EHB_EHB	0x1F	/* End horizontal blank */
#define	VGA_CRTC_START_HORIZ_RETRACE	4
#define	VGA_CRTC_END_HORIZ_RETRACE	5
#define		VGA_CRTC_EHR_EHB	0x80	/* End horizontal blanking */
#define		VGA_CRTC_EHR_HRD	0x60	/* Horizontal retrace delay */
#define		VGA_CRTC_EHR_EHR	0x1F	/* End horizontal retrace */
#define	VGA_CRTC_VERT_TOTAL		6
#define	VGA_CRTC_OVERFLOW		7
#define		VGA_CRTC_OF_VRS9	0x80	/* Vertical retrace start */
#define		VGA_CRTC_OF_VDE9	0x40	/* Vertical disp. enable end */
#define		VGA_CRTC_OF_VT9		0x20	/* Vertical total (bit 9) */
#define		VGA_CRTC_OF_LC8		0x10	/* Line compare */
#define		VGA_CRTC_OF_VBS8	0x08	/* Start vertical blanking */
#define		VGA_CRTC_OF_VRS8	0x04	/* Vertical retrace start */
#define		VGA_CRTC_OF_VDE8	0x02	/* Vertical disp. enable end */
#define		VGA_CRTC_OF_VT8		0x01	/* Vertical total (bit 8) */
#define	VGA_CRTC_PRESET_ROW_SCAN	8
#define		VGA_CRTC_PRS_BP		0x60	/* Byte panning */
#define		VGA_CRTC_PRS_PRS	0x1F	/* Preset row scan */
#define	VGA_CRTC_MAX_SCAN_LINE		9
#define		VGA_CRTC_MSL_2T4	0x80	/* 200-to-400 line conversion */
#define		VGA_CRTC_MSL_LC9	0x40	/* Line compare (bit 9) */
#define		VGA_CRTC_MSL_VBS9	0x20	/* Start vertical blanking */
#define		VGA_CRTC_MSL_MSL	0x1F	/* Maximum scan line */
#define	VGA_CRTC_CURSOR_START		10
#define		VGA_CRTC_CS_COO		0x20	/* Cursor on/off */
#define		VGA_CRTC_CS_CS		0x1F	/* Cursor start */
#define	VGA_CRTC_CURSOR_END		11
#define		VGA_CRTC_CE_CSK		0x60	/* Cursor skew */
#define		VGA_CRTC_CE_CE		0x1F	/* Cursor end */
#define	VGA_CRTC_START_ADDR_HIGH	12
#define	VGA_CRTC_START_ADDR_LOW		13
#define	VGA_CRTC_CURSOR_LOC_HIGH	14
#define	VGA_CRTC_CURSOR_LOC_LOW		15
#define	VGA_CRTC_VERT_RETRACE_START	16
#define	VGA_CRTC_VERT_RETRACE_END	17
#define		VGA_CRTC_VRE_PR		0x80	/* Protect register 0-7 */
#define		VGA_CRTC_VRE_BW		0x40	/* Bandwidth */
#define		VGA_CRTC_VRE_VRE	0x1F	/* Vertical retrace end */
#define	VGA_CRTC_VERT_DISPLAY_END	18
#define	VGA_CRTC_OFFSET			19
#define	VGA_CRTC_UNDERLINE_LOC		20
#define		VGA_CRTC_UL_DW		0x40	/* Double word mode */
#define		VGA_CRTC_UL_CB4		0x20	/* Count by four */
#define		VGA_CRTC_UL_UL		0x1F	/* Underline location */
#define	VGA_CRTC_START_VERT_BLANK	21
#define	VGA_CRTC_END_VERT_BLANK		22
#define	VGA_CRTC_MODE_CONTROL		23
#define		VGA_CRTC_MC_HR		0x80	/* hardware reset */
#define		VGA_CRTC_MC_WB		0x40	/* Word/byte mode */
#define		VGA_CRTC_MC_AW		0x20	/* Address wrap */
#define		VGA_CRTC_MC_CBT		0x08	/* Count by two */
#define		VGA_CRTC_MC_HRS		0x04	/* Horizontal retrace select */
#define		VGA_CRTC_MC_SRS		0x02	/* Select row scan counter */
#define		VGA_CRTC_MC_CMS		0x01	/* Compatibility mode support */
#define	VGA_CRTC_LINE_COMPARE		24
#define	VGA_CRTC_DATA		0x15

#endif /* !_DEV_VT_HW_VGA_VGA_REG_H_ */
