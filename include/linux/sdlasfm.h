/*****************************************************************************
* sdlasfm.h	WANPIPE(tm) Multiprotocol WAN Link Driver.
*		Definitions for the SDLA Firmware Module (SFM).
*
* Author: 	Gideon Hack 	
*
* Copyright:	(c) 1995-1999 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Jun 02, 1999  Gideon Hack	Added support for the S514 adapter.
* Dec 11, 1996	Gene Kozin	Cosmetic changes
* Apr 16, 1996	Gene Kozin	Changed adapter & firmware IDs. Version 2
* Dec 15, 1995	Gene Kozin	Structures chaned
* Nov 09, 1995	Gene Kozin	Initial version.
*****************************************************************************/
#ifndef	_SDLASFM_H
#define	_SDLASFM_H

/****** Defines *************************************************************/

#define	SFM_VERSION	2
#define	SFM_SIGNATURE	"SFM - Sangoma SDLA Firmware Module"

/* min/max */
#define	SFM_IMAGE_SIZE	0x8000	/* max size of SDLA code image file */
#define	SFM_DESCR_LEN	256	/* max length of description string */
#define	SFM_MAX_SDLA	16	/* max number of compatible adapters */

/* Adapter types */
#define SDLA_S502A	5020
#define SDLA_S502E	5021
#define SDLA_S503	5030
#define SDLA_S508	5080
#define SDLA_S507	5070
#define SDLA_S509	5090
#define SDLA_S514	5140

/* S514 PCI adapter CPU numbers */
#define S514_CPU_A	'A'
#define S514_CPU_B	'B'


/* Firmware identification numbers:
 *    0  ..  999	Test & Diagnostics
 *  1000 .. 1999	Streaming HDLC
 *  2000 .. 2999	Bisync
 *  3000 .. 3999	SDLC
 *  4000 .. 4999	HDLC
 *  5000 .. 5999	X.25
 *  6000 .. 6999	Frame Relay
 *  7000 .. 7999	PPP
 *  8000 .. 8999        Cisco HDLC
 */
#define	SFID_CALIB502	 200
#define	SFID_STRM502	1200
#define	SFID_STRM508	1800
#define	SFID_BSC502	2200
#define	SFID_SDLC502	3200
#define	SFID_HDLC502	4200
#define	SFID_HDLC508	4800
#define	SFID_X25_502	5200
#define	SFID_X25_508	5800
#define	SFID_FR502	6200
#define	SFID_FR508	6800
#define	SFID_PPP502	7200
#define	SFID_PPP508	7800
#define SFID_PPP514	7140
#define	SFID_CHDLC508	8800
#define SFID_CHDLC514	8140

/****** Data Types **********************************************************/

typedef struct	sfm_info		/* firmware module information */
{
	unsigned short	codeid;		/* firmware ID */
	unsigned short	version;	/* firmaware version number */
	unsigned short	adapter[SFM_MAX_SDLA]; /* compatible adapter types */
	unsigned long	memsize;	/* minimum memory size */
	unsigned short	reserved[2];	/* reserved */
	unsigned short	startoffs;	/* entry point offset */
	unsigned short	winoffs;	/* dual-port memory window offset */
	unsigned short	codeoffs;	/* code load offset */
	unsigned short	codesize;	/* code size */
	unsigned short	dataoffs;	/* configuration data load offset */
	unsigned short	datasize;	/* configuration data size */
} sfm_info_t;

typedef struct sfm			/* SDLA firmware file structire */
{
	char		signature[80];	/* SFM file signature */
	unsigned short	version;	/* file format version */
	unsigned short	checksum;	/* info + image */
	unsigned short	reserved[6];	/* reserved */
	char		descr[SFM_DESCR_LEN]; /* description string */
	sfm_info_t	info;		/* firmware module info */
	unsigned char	image[1];	/* code image (variable size) */
} sfm_t;

#endif	/* _SDLASFM_H */

