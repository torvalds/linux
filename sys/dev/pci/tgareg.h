/* $OpenBSD: tgareg.h,v 1.5 2022/01/09 05:42:58 jsg Exp $ */
/* $NetBSD: tgareg.h,v 1.3 2000/03/04 10:28:00 elric Exp $ */

/*
 * Copyright (c) 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#ifndef _ALPHA_INCLUDE_TGAREG_H_
#define _ALPHA_INCLUDE_TGAREG_H_

/*
 * Device-specific PCI register offsets and contents.
 */

#define	TGA_PCIREG_PVRR	0x40		/* PCI Address Extension Register */

#define	TGA_PCIREG_PAER	0x44		/* PCI VGA Redirect Register */

/*
 * TGA Memory Space offsets
 */

#define	TGA_MEM_ALTROM	0x0000000	/* 0MB -- Alternate ROM space */
#define TGA2_MEM_EXTDEV	0x0000000	/* 0MB -- External Device Access */
#define	TGA_MEM_CREGS	0x0100000	/* 1MB -- Core Registers */
#define TGA_CREGS_SIZE	0x0100000 	/* Core registers occupy 1MB */
#define TGA_CREGS_ALIAS	0x0000400	/* Register copies every 1kB */

#define TGA2_MEM_CLOCK	0x0060000	/* TGA2 Clock access */
#define TGA2_MEM_RAMDAC	0x0080000	/* TGA2 RAMDAC access */

/* Display and Back Buffers mapped at config-dependent addresses */

/*
 * TGA Core Space register numbers and contents.
 */

typedef u_int32_t tga_reg_t;

#define	TGA_REG_GCBR0	0x000		/* Copy buffer 0 */
#define	TGA_REG_GCBR1	0x001		/* Copy buffer 1 */
#define	TGA_REG_GCBR2	0x002		/* Copy buffer 2 */
#define	TGA_REG_GCBR3	0x003		/* Copy buffer 3 */
#define	TGA_REG_GCBR4	0x004		/* Copy buffer 4 */
#define	TGA_REG_GCBR5	0x005		/* Copy buffer 5 */
#define	TGA_REG_GCBR6	0x006		/* Copy buffer 6 */
#define	TGA_REG_GCBR7	0x007		/* Copy buffer 7 */

#define	TGA_REG_GFGR	0x008		/* Foreground */
#define	TGA_REG_GBGR	0x009		/* Background */
#define	TGA_REG_GPMR	0x00a		/* Plane Mask */
#define	TGA_REG_GPXR_S	0x00b		/* Pixel Mask (one-shot) */
#define	TGA_REG_GMOR	0x00c		/* Mode */
#define	TGA_REG_GOPR	0x00d		/* Raster Operation */
#define	TGA_REG_GPSR	0x00e		/* Pixel Shift */
#define	TGA_REG_GADR	0x00f		/* Address */

#define	TGA_REG_GB1R	0x010		/* Bresenham 1 */
#define	TGA_REG_GB2R	0x011		/* Bresenham 2 */
#define	TGA_REG_GB3R	0x012		/* Bresenham 3 */

#define	TGA_REG_GCTR	0x013		/* Continue */
#define	TGA_REG_GDER	0x014		/* Deep */
#define TGA_REG_GREV	0x015		/* Start/Version on TGA,
					 * Revision on TGA2 */
#define	TGA_REG_GSMR	0x016		/* Stencil Mode */
#define	TGA_REG_GPXR_P	0x017		/* Pixel Mask (persistent) */
#define	TGA_REG_CCBR	0x018		/* Cursor Base Address */
#define	TGA_REG_VHCR	0x019		/* Horizontal Control */
#define	TGA_REG_VVCR	0x01a		/* Vertical Control */
#define	TGA_REG_VVBR	0x01b		/* Video Base Address */
#define	TGA_REG_VVVR	0x01c		/* Video Valid */
#define	TGA_REG_CXYR	0x01d		/* Cursor XY */
#define	TGA_REG_VSAR	0x01e		/* Video Shift Address */
#define	TGA_REG_SISR	0x01f		/* Interrupt Status */
#define	TGA_REG_GDAR	0x020		/* Data */
#define	TGA_REG_GRIR	0x021		/* Red Increment */
#define	TGA_REG_GGIR	0x022		/* Green Increment */
#define	TGA_REG_GBIR	0x023		/* Blue Increment */
#define	TGA_REG_GZIR_L	0x024		/* Z-increment Low */
#define	TGA_REG_GZIR_H	0x025		/* Z-Increment High */
#define	TGA_REG_GDBR	0x026		/* DMA Base Address */
#define	TGA_REG_GBWR	0x027		/* Bresenham Width */
#define	TGA_REG_GZVR_L	0x028		/* Z-value Low */
#define	TGA_REG_GZVR_H	0x029		/* Z-value High */
#define	TGA_REG_GZBR	0x02a		/* Z-base address */
/*	GADR alias	0x02b */
#define	TGA_REG_GRVR	0x02c		/* Red Value */
#define	TGA_REG_GGVR	0x02d		/* Green Value */
#define	TGA_REG_GBVR	0x02e		/* Blue Value */
#define	TGA_REG_GSWR	0x02f		/* Span Width */
#define	TGA_REG_EPSR	0x030		/* Palette and DAC Setup */

/*	reserved	0x031 - 0x3f */

#define	TGA_REG_GSNR0	0x040		/* Slope-no-go 0 */
#define	TGA_REG_GSNR1	0x041		/* Slope-no-go 1 */
#define	TGA_REG_GSNR2	0x042		/* Slope-no-go 2 */
#define	TGA_REG_GSNR3	0x043		/* Slope-no-go 3 */
#define	TGA_REG_GSNR4	0x044		/* Slope-no-go 4 */
#define	TGA_REG_GSNR5	0x045		/* Slope-no-go 5 */
#define	TGA_REG_GSNR6	0x046		/* Slope-no-go 6 */
#define	TGA_REG_GSNR7	0x047		/* Slope-no-go 7 */

#define	TGA_REG_GSLR0	0x048		/* Slope 0 */
#define	TGA_REG_GSLR1	0x049		/* Slope 1 */
#define	TGA_REG_GSLR2	0x04a		/* Slope 2 */
#define	TGA_REG_GSLR3	0x04b		/* Slope 3 */
#define	TGA_REG_GSLR4	0x04c		/* Slope 4 */
#define	TGA_REG_GSLR5	0x04d		/* Slope 5 */
#define	TGA_REG_GSLR6	0x04e		/* Slope 6 */
#define	TGA_REG_GSLR7	0x04f		/* Slope 7 */

#define	TGA_REG_GBCR0	0x050		/* Block Color 0 */
#define	TGA_REG_GBCR1	0x051		/* Block Color 1 */
#define	TGA_REG_GBCR2	0x052		/* Block Color 2 */
#define	TGA_REG_GBCR3	0x053		/* Block Color 3 */
#define	TGA_REG_GBCR4	0x054		/* Block Color 4 */
#define	TGA_REG_GBCR5	0x055		/* Block Color 5 */
#define	TGA_REG_GBCR6	0x056		/* Block Color 6 */
#define	TGA_REG_GBCR7	0x057		/* Block Color 7 */

#define	TGA_REG_GCSR	0x058		/* Copy 64 Source */
#define	TGA_REG_GCDR	0x059		/* Copy 64 Destination */
/*	GC[SD]R aliases 0x05a - 0x05f */

/*	reserved	0x060 - 0x077 */

#define	TGA_REG_ERWR	0x078		/* EEPROM write */

/*	reserved	0x079 */

#define	TGA_REG_ECGR	0x07a		/* Clock */

/*	reserved	0x07b */

#define	TGA_REG_EPDR	0x07c		/* Palette and DAC Data */

/*	reserved	0x07d */

#define	TGA_REG_SCSR	0x07e		/* Command Status */

/*	reserved	0x07f */

/*
 * Video Valid Register
 */
#define	VVR_VIDEOVALID	0x00000001	/* 0 VGA, 1 TGA2 (TGA2 only) */
#define	VVR_BLANK	0x00000002	/* 0 active, 1 blank */
#define	VVR_CURSOR	0x00000004	/* 0 disable, 1 enable (TGA2 R/O) */
#define	VVR_INTERLACE	0x00000008	/* 0 N/Int, 1 Int. (TGA2 R/O) */
#define	VVR_DPMS_MASK	0x00000030	/* See "DMPS mask" below */
#define	VVR_DPMS_SHIFT	4
#define	VVR_DDC		0x00000040	/* DDC-in pin value (R/O) */
#define	VVR_TILED	0x00000400	/* 0 linear, 1 tiled (not on TGA2) */
#define	VVR_LDDLY_MASK	0x01ff0000	/* load delay in quad pixel clock ticks
					   (not on TGA2) */
#define	VVR_LDDLY_SHIFT	16

#endif /* _ALPHA_INCLUDE_TGAREG_H_ */
