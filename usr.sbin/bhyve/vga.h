/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015 Tycho Nightingale <tycho.nightingale@pluribusnetworks.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
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

#ifndef _VGA_H_
#define	_VGA_H_

#define	VGA_IOPORT_START		0x3c0
#define	VGA_IOPORT_END			0x3df

/* General registers */
#define	GEN_INPUT_STS0_PORT		0x3c2
#define	GEN_FEATURE_CTRL_PORT		0x3ca
#define	GEN_MISC_OUTPUT_PORT		0x3cc
#define	GEN_INPUT_STS1_MONO_PORT	0x3ba
#define	GEN_INPUT_STS1_COLOR_PORT	0x3da
#define	GEN_IS1_VR			0x08	/* Vertical retrace */
#define	GEN_IS1_DE			0x01	/* Display enable not */

/* Attribute controller registers. */
#define	ATC_IDX_PORT			0x3c0
#define	ATC_DATA_PORT			0x3c1

#define	ATC_IDX_MASK			0x1f
#define	ATC_PALETTE0			0
#define	ATC_PALETTE15			15
#define	ATC_MODE_CONTROL		16
#define	ATC_MC_IPS			0x80	/* Internal palette size */
#define	ATC_MC_GA			0x01	/* Graphics/alphanumeric */
#define	ATC_OVERSCAN_COLOR		17
#define	ATC_COLOR_PLANE_ENABLE		18
#define	ATC_HORIZ_PIXEL_PANNING		19
#define	ATC_COLOR_SELECT		20
#define	ATC_CS_C67			0x0c	/* Color select bits 6+7 */
#define	ATC_CS_C45			0x03	/* Color select bits 4+5 */

/* Sequencer registers. */
#define	SEQ_IDX_PORT			0x3c4
#define	SEQ_DATA_PORT			0x3c5

#define	SEQ_RESET			0
#define	SEQ_RESET_ASYNC			0x1
#define	SEQ_RESET_SYNC			0x2
#define	SEQ_CLOCKING_MODE		1
#define	SEQ_CM_SO			0x20	/* Screen off */
#define	SEQ_CM_89			0x01	/* 8/9 dot clock */
#define	SEQ_MAP_MASK			2
#define	SEQ_CHAR_MAP_SELECT		3
#define	SEQ_CMS_SAH			0x20	/* Char map A bit 2 */
#define	SEQ_CMS_SAH_SHIFT		5
#define	SEQ_CMS_SA			0x0c	/* Char map A bits 0+1 */
#define	SEQ_CMS_SA_SHIFT		2
#define	SEQ_CMS_SBH			0x10	/* Char map B bit 2 */
#define	SEQ_CMS_SBH_SHIFT		4
#define	SEQ_CMS_SB			0x03	/* Char map B bits 0+1 */
#define	SEQ_CMS_SB_SHIFT		0
#define	SEQ_MEMORY_MODE			4
#define	SEQ_MM_C4			0x08	/* Chain 4 */
#define	SEQ_MM_OE			0x04	/* Odd/even */
#define	SEQ_MM_EM			0x02	/* Extended memory */

/* Graphics controller registers. */
#define	GC_IDX_PORT			0x3ce
#define	GC_DATA_PORT			0x3cf

#define	GC_SET_RESET			0
#define	GC_ENABLE_SET_RESET		1
#define	GC_COLOR_COMPARE		2
#define	GC_DATA_ROTATE			3
#define	GC_READ_MAP_SELECT		4
#define	GC_MODE				5
#define	GC_MODE_OE			0x10	/* Odd/even */
#define	GC_MODE_C4			0x04	/* Chain 4 */

#define	GC_MISCELLANEOUS		6
#define	GC_MISC_GM			0x01	/* Graphics/alphanumeric */
#define	GC_MISC_MM			0x0c	/* memory map */
#define	GC_MISC_MM_SHIFT		2
#define	GC_COLOR_DONT_CARE		7
#define	GC_BIT_MASK			8

/* CRT controller registers. */
#define	CRTC_IDX_MONO_PORT		0x3b4
#define	CRTC_DATA_MONO_PORT		0x3b5
#define	CRTC_IDX_COLOR_PORT		0x3d4
#define	CRTC_DATA_COLOR_PORT		0x3d5

#define	CRTC_HORIZ_TOTAL		0
#define	CRTC_HORIZ_DISP_END		1
#define	CRTC_START_HORIZ_BLANK		2
#define	CRTC_END_HORIZ_BLANK		3
#define	CRTC_START_HORIZ_RETRACE	4
#define	CRTC_END_HORIZ_RETRACE		5
#define	CRTC_VERT_TOTAL			6
#define	CRTC_OVERFLOW			7
#define	CRTC_OF_VRS9			0x80	/* VRS bit 9 */
#define	CRTC_OF_VRS9_SHIFT		7
#define	CRTC_OF_VDE9			0x40	/* VDE bit 9 */
#define	CRTC_OF_VDE9_SHIFT		6
#define	CRTC_OF_VRS8			0x04	/* VRS bit 8 */
#define	CRTC_OF_VRS8_SHIFT		2
#define	CRTC_OF_VDE8			0x02	/* VDE bit 8 */
#define	CRTC_OF_VDE8_SHIFT		1
#define	CRTC_PRESET_ROW_SCAN		8
#define	CRTC_MAX_SCAN_LINE		9
#define	CRTC_MSL_MSL			0x1f
#define	CRTC_CURSOR_START		10
#define	CRTC_CS_CO			0x20	/* Cursor off */
#define	CRTC_CS_CS			0x1f	/* Cursor start */
#define	CRTC_CURSOR_END			11
#define	CRTC_CE_CE			0x1f	/* Cursor end */
#define	CRTC_START_ADDR_HIGH		12
#define	CRTC_START_ADDR_LOW		13
#define	CRTC_CURSOR_LOC_HIGH		14
#define	CRTC_CURSOR_LOC_LOW		15
#define	CRTC_VERT_RETRACE_START		16
#define	CRTC_VERT_RETRACE_END		17
#define	CRTC_VRE_MASK			0xf
#define	CRTC_VERT_DISP_END		18
#define	CRTC_OFFSET			19
#define	CRTC_UNDERLINE_LOC		20
#define	CRTC_START_VERT_BLANK		21
#define	CRTC_END_VERT_BLANK		22
#define	CRTC_MODE_CONTROL		23
#define	CRTC_MC_TE			0x80	/* Timing enable */
#define	CRTC_LINE_COMPARE		24

/* DAC registers */
#define	DAC_MASK			0x3c6
#define	DAC_IDX_RD_PORT			0x3c7
#define	DAC_IDX_WR_PORT			0x3c8
#define	DAC_DATA_PORT			0x3c9

void	*vga_init(int io_only);

#endif /* _VGA_H_ */
