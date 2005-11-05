/*  Architecture specific parts of HP's STI (framebuffer) driver.
 *  Structures are HP-UX compatible for XFree86 usage.
 * 
 *    Linux/PA-RISC Project (http://www.parisc-linux.org/)
 *    Copyright (C) 2001 Helge Deller (deller a parisc-linux org)
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __ASM_PARISC_GRFIOCTL_H
#define __ASM_PARISC_GRFIOCTL_H

/* upper 32 bits of graphics id (HP/UX identifier) */

#define GRFGATOR		8
#define S9000_ID_S300		9
#define GRFBOBCAT		9
#define	GRFCATSEYE		9
#define S9000_ID_98720		10
#define GRFRBOX			10
#define S9000_ID_98550		11
#define GRFFIREEYE		11
#define S9000_ID_A1096A		12
#define GRFHYPERION		12
#define S9000_ID_FRI		13
#define S9000_ID_98730		14
#define GRFDAVINCI		14
#define S9000_ID_98705		0x26C08070	/* Tigershark */
#define S9000_ID_98736		0x26D148AB
#define S9000_ID_A1659A		0x26D1482A	/* CRX 8 plane color (=ELK) */
#define S9000_ID_ELK		S9000_ID_A1659A
#define S9000_ID_A1439A		0x26D148EE	/* CRX24 = CRX+ (24-plane color) */
#define S9000_ID_A1924A		0x26D1488C	/* GRX gray-scale */
#define S9000_ID_ELM		S9000_ID_A1924A
#define S9000_ID_98765		0x27480DEF
#define S9000_ID_ELK_768	0x27482101
#define S9000_ID_STINGER	0x27A4A402
#define S9000_ID_TIMBER		0x27F12392	/* Bushmaster (710) Graphics */
#define S9000_ID_TOMCAT		0x27FCCB6D	/* dual-headed ELK (Dual CRX) */
#define S9000_ID_ARTIST		0x2B4DED6D	/* Artist (Gecko/712 & 715) onboard Graphics */
#define S9000_ID_HCRX		0x2BCB015A	/* Hyperdrive/Hyperbowl (A4071A) Graphics */
#define CRX24_OVERLAY_PLANES	0x920825AA	/* Overlay planes on CRX24 */

#define CRT_ID_ELK_1024		S9000_ID_ELK_768 /* Elk 1024x768  CRX */
#define CRT_ID_ELK_1280		S9000_ID_A1659A	/* Elk 1280x1024 CRX */
#define CRT_ID_ELK_1024DB	0x27849CA5      /* Elk 1024x768 double buffer */
#define CRT_ID_ELK_GS		S9000_ID_A1924A	/* Elk 1280x1024 GreyScale    */
#define CRT_ID_CRX24		S9000_ID_A1439A	/* Piranha */
#define CRT_ID_VISUALIZE_EG	0x2D08C0A7      /* Graffiti (built-in B132+/B160L) */
#define CRT_ID_THUNDER		0x2F23E5FC      /* Thunder 1 VISUALIZE 48*/
#define CRT_ID_THUNDER2		0x2F8D570E      /* Thunder 2 VISUALIZE 48 XP*/
#define CRT_ID_HCRX		S9000_ID_HCRX	/* Hyperdrive HCRX */
#define CRT_ID_CRX48Z		S9000_ID_STINGER /* Stinger */
#define CRT_ID_DUAL_CRX		S9000_ID_TOMCAT	/* Tomcat */
#define CRT_ID_PVRX		S9000_ID_98705	/* Tigershark */
#define CRT_ID_TIMBER		S9000_ID_TIMBER	/* Timber (710 builtin) */
#define CRT_ID_TVRX		S9000_ID_98765	/* TVRX (gto/falcon) */
#define CRT_ID_ARTIST		S9000_ID_ARTIST	/* Artist */
#define CRT_ID_SUMMIT		0x2FC1066B      /* Summit FX2, FX4, FX6 ... */
#define CRT_ID_LEGO		0x35ACDA30	/* Lego FX5, FX10 ... */
#define CRT_ID_PINNACLE		0x35ACDA16	/* Pinnacle FXe */ 

/* structure for ioctl(GCDESCRIBE) */

#define gaddr_t unsigned long	/* FIXME: PA2.0 (64bit) portable ? */

struct	grf_fbinfo {
	unsigned int	id;		/* upper 32 bits of graphics id */
	unsigned int	mapsize;	/* mapped size of framebuffer */
	unsigned int	dwidth, dlength;/* x and y sizes */
	unsigned int	width, length;	/* total x and total y size */
	unsigned int	xlen;		/* x pitch size */
	unsigned int	bpp, bppu;	/* bits per pixel and used bpp */
	unsigned int	npl, nplbytes;	/* # of planes and bytes per plane */
	char		name[32];	/* name of the device (from ROM) */
	unsigned int	attr;		/* attributes */
	gaddr_t 	fbbase, regbase;/* framebuffer and register base addr */
	gaddr_t		regions[6];	/* region bases */
};

#define	GCID		_IOR('G', 0, int)
#define	GCON		_IO('G', 1)
#define	GCOFF		_IO('G', 2)
#define	GCAON		_IO('G', 3)
#define	GCAOFF		_IO('G', 4)
#define	GCMAP		_IOWR('G', 5, int)
#define	GCUNMAP		_IOWR('G', 6, int)
#define	GCMAP_HPUX	_IO('G', 5)
#define	GCUNMAP_HPUX	_IO('G', 6)
#define	GCLOCK		_IO('G', 7)
#define	GCUNLOCK	_IO('G', 8)
#define	GCLOCK_MINIMUM	_IO('G', 9)
#define	GCUNLOCK_MINIMUM _IO('G', 10)
#define	GCSTATIC_CMAP	_IO('G', 11)
#define	GCVARIABLE_CMAP _IO('G', 12)
#define GCTERM		_IOWR('G',20,int)	/* multi-headed Tomcat */ 
#define GCDESCRIBE	_IOR('G', 21, struct grf_fbinfo)
#define GCFASTLOCK	_IO('G', 26)

#endif /* __ASM_PARISC_GRFIOCTL_H */

