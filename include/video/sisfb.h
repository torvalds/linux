/*
 * Copyright (C) 2001-2004 by Thomas Winischhofer, Vienna, Austria.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 */

#ifndef _LINUX_SISFB
#define _LINUX_SISFB

#include <asm/ioctl.h>
#include <asm/types.h>

/**********************************************/
/*                   PUBLIC                   */
/**********************************************/

/* vbflags */
#define CRT2_DEFAULT            0x00000001
#define CRT2_LCD                0x00000002  /* TW: Never change the order of the CRT2_XXX entries */
#define CRT2_TV                 0x00000004  /*     (see SISCycleCRT2Type())                       */
#define CRT2_VGA                0x00000008
#define TV_NTSC                 0x00000010
#define TV_PAL                  0x00000020
#define TV_HIVISION             0x00000040
#define TV_YPBPR                0x00000080
#define TV_AVIDEO               0x00000100
#define TV_SVIDEO               0x00000200
#define TV_SCART                0x00000400
#define VB_CONEXANT		0x00000800	/* 661 series only */
#define VB_TRUMPION		VB_CONEXANT	/* 300 series only */
#define TV_PALM                 0x00001000
#define TV_PALN                 0x00002000
#define TV_NTSCJ		0x00001000
#define VB_302ELV		0x00004000
#define TV_CHSCART              0x00008000
#define TV_CHYPBPR525I          0x00010000
#define CRT1_VGA		0x00000000
#define CRT1_LCDA		0x00020000
#define VGA2_CONNECTED          0x00040000
#define VB_DISPTYPE_CRT1	0x00080000  	/* CRT1 connected and used */
#define VB_301                  0x00100000	/* Video bridge type */
#define VB_301B                 0x00200000
#define VB_302B                 0x00400000
#define VB_30xBDH		0x00800000      /* 30xB DH version (w/o LCD support) */
#define VB_LVDS                 0x01000000
#define VB_CHRONTEL             0x02000000
#define VB_301LV                0x04000000
#define VB_302LV                0x08000000
#define VB_301C			0x10000000
#define VB_SINGLE_MODE          0x20000000   	/* CRT1 or CRT2; determined by DISPTYPE_CRTx */
#define VB_MIRROR_MODE		0x40000000   	/* CRT1 + CRT2 identical (mirror mode) */
#define VB_DUALVIEW_MODE	0x80000000   	/* CRT1 + CRT2 independent (dual head mode) */

/* Aliases: */
#define CRT2_ENABLE		(CRT2_LCD | CRT2_TV | CRT2_VGA)
#define TV_STANDARD             (TV_NTSC | TV_PAL | TV_PALM | TV_PALN | TV_NTSCJ)
#define TV_INTERFACE            (TV_AVIDEO|TV_SVIDEO|TV_SCART|TV_HIVISION|TV_YPBPR|TV_CHSCART|TV_CHYPBPR525I)

/* Only if TV_YPBPR is set: */
#define TV_YPBPR525I		TV_NTSC
#define TV_YPBPR525P		TV_PAL
#define TV_YPBPR750P		TV_PALM
#define TV_YPBPR1080I		TV_PALN
#define TV_YPBPRALL 		(TV_YPBPR525I | TV_YPBPR525P | TV_YPBPR750P | TV_YPBPR1080I)

#define VB_SISBRIDGE            (VB_301|VB_301B|VB_301C|VB_302B|VB_301LV|VB_302LV|VB_302ELV)
#define VB_SISTVBRIDGE          (VB_301|VB_301B|VB_301C|VB_302B|VB_301LV|VB_302LV)
#define VB_VIDEOBRIDGE		(VB_SISBRIDGE | VB_LVDS | VB_CHRONTEL | VB_CONEXANT)

#define VB_DISPTYPE_DISP2	CRT2_ENABLE
#define VB_DISPTYPE_CRT2	CRT2_ENABLE
#define VB_DISPTYPE_DISP1	VB_DISPTYPE_CRT1
#define VB_DISPMODE_SINGLE	VB_SINGLE_MODE
#define VB_DISPMODE_MIRROR	VB_MIRROR_MODE
#define VB_DISPMODE_DUAL	VB_DUALVIEW_MODE
#define VB_DISPLAY_MODE       	(SINGLE_MODE | MIRROR_MODE | DUALVIEW_MODE)

/* Structure argument for SISFB_GET_INFO ioctl  */
typedef struct _SISFB_INFO sisfb_info, *psisfb_info;

struct _SISFB_INFO {
	__u32   sisfb_id;         	/* for identifying sisfb */
#ifndef SISFB_ID
#define SISFB_ID	  0x53495346    /* Identify myself with 'SISF' */
#endif
 	__u32   chip_id;		/* PCI-ID of detected chip */
	__u32   memory;			/* video memory in KB which sisfb manages */
	__u32   heapstart;            	/* heap start (= sisfb "mem" argument) in KB */
	__u8    fbvidmode;		/* current sisfb mode */

	__u8    sisfb_version;
	__u8    sisfb_revision;
	__u8 	sisfb_patchlevel;

	__u8 	sisfb_caps;		/* sisfb capabilities */

	__u32	sisfb_tqlen;		/* turbo queue length (in KB) */

	__u32 	sisfb_pcibus;      	/* The card's PCI ID */
	__u32 	sisfb_pcislot;
	__u32 	sisfb_pcifunc;

	__u8 	sisfb_lcdpdc;		/* PanelDelayCompensation */

	__u8 	sisfb_lcda;		/* Detected status of LCDA for low res/text modes */

	__u32 	sisfb_vbflags;
	__u32 	sisfb_currentvbflags;

	__u32 	sisfb_scalelcd;
	__u32 	sisfb_specialtiming;

	__u8 	sisfb_haveemi;
	__u8 	sisfb_emi30,sisfb_emi31,sisfb_emi32,sisfb_emi33;
	__u8 	sisfb_haveemilcd;

	__u8 	sisfb_lcdpdca;		/* PanelDelayCompensation for LCD-via-CRT1 */

	__u16	sisfb_tvxpos, sisfb_tvypos;  /* Warning: Values + 32 ! */

	__u8 	reserved[208]; 		/* for future use */
};

/* Addtional IOCTLs for communication sisfb <> X driver                */
/* If changing this, vgatypes.h must also be changed (for X driver)    */

/* ioctl for identifying and giving some info (esp. memory heap start) */
#define SISFB_GET_INFO_SIZE  	_IOR(0xF3,0x00,__u32)
#define SISFB_GET_INFO	  	_IOR(0xF3,0x01,struct _SISFB_INFO)

/* ioctrl to get current vertical retrace status */
#define SISFB_GET_VBRSTATUS  	_IOR(0xF3,0x02,__u32)

/* ioctl to enable/disable panning auto-maximize (like nomax parameter) */
#define SISFB_GET_AUTOMAXIMIZE 	_IOR(0xF3,0x03,__u32)
#define SISFB_SET_AUTOMAXIMIZE 	_IOW(0xF3,0x03,__u32)

/* ioctls to relocate TV output (x=D[31:16], y=D[15:0], + 32)*/
#define SISFB_GET_TVPOSOFFSET   _IOR(0xF3,0x04,__u32)
#define SISFB_SET_TVPOSOFFSET   _IOW(0xF3,0x04,__u32)

/* ioctl for locking sisfb (no register access during lock) */
/* As of now, only used to avoid register access during
 * the ioctls listed above.
 */
#define SISFB_SET_LOCK  	_IOW(0xF3,0x06,__u32)

/* more to come soon */

/* ioctls 0xF3 up to 0x3F reserved for sisfb */

/****************************************************************/
/* The following are deprecated and should not be used anymore: */
/****************************************************************/
/* ioctl for identifying and giving some info (esp. memory heap start) */
#define SISFB_GET_INFO_OLD  	   _IOR('n',0xF8,__u32)
/* ioctrl to get current vertical retrace status */
#define SISFB_GET_VBRSTATUS_OLD	   _IOR('n',0xF9,__u32)
/* ioctl to enable/disable panning auto-maximize (like nomax parameter) */
#define SISFB_GET_AUTOMAXIMIZE_OLD _IOR('n',0xFA,__u32)
#define SISFB_SET_AUTOMAXIMIZE_OLD _IOW('n',0xFA,__u32)
/****************************************************************/
/*               End of deprecated ioctl numbers                */
/****************************************************************/

/* For fb memory manager (FBIO_ALLOC, FBIO_FREE) */
struct sis_memreq {
	__u32 	offset;
	__u32 	size;
};

/**********************************************/
/*                  PRIVATE                   */
/*         (for IN-KERNEL usage only)         */
/**********************************************/

#ifdef __KERNEL__
#define	UNKNOWN_VGA  0
#define	SIS_300_VGA  1
#define	SIS_315_VGA  2

extern void sis_malloc(struct sis_memreq *req);
extern void sis_free(u32 base);
#endif

#endif
