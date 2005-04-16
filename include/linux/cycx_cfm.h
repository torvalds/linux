/*
* cycx_cfm.h	Cyclom 2X WAN Link Driver.
*		Definitions for the Cyclom 2X Firmware Module (CFM).
*
* Author:	Arnaldo Carvalho de Melo <acme@conectiva.com.br>
*
* Copyright:	(c) 1998-2003 Arnaldo Carvalho de Melo
*
* Based on sdlasfm.h by Gene Kozin <74604.152@compuserve.com>
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* 1998/08/08	acme		Initial version.
*/
#ifndef	_CYCX_CFM_H
#define	_CYCX_CFM_H

/* Defines */

#define	CFM_VERSION	2
#define	CFM_SIGNATURE	"CFM - Cyclades CYCX Firmware Module"

/* min/max */
#define	CFM_IMAGE_SIZE	0x20000	/* max size of CYCX code image file */
#define	CFM_DESCR_LEN	256	/* max length of description string */
#define	CFM_MAX_CYCX	1	/* max number of compatible adapters */
#define	CFM_LOAD_BUFSZ	0x400	/* buffer size for reset code (buffer_load) */

/* Firmware Commands */
#define GEN_POWER_ON	0x1280

#define GEN_SET_SEG	0x1401	/* boot segment setting. */
#define GEN_BOOT_DAT	0x1402	/* boot data. */
#define GEN_START	0x1403	/* board start. */
#define GEN_DEFPAR	0x1404	/* buffer length for boot. */

/* Adapter Types */
#define CYCX_2X		2
/* for now only the 2X is supported, no plans to support 8X or 16X */
#define CYCX_8X		8
#define CYCX_16X	16

#define	CFID_X25_2X	5200

/**
 *	struct cycx_fw_info - firmware module information.
 *	@codeid - firmware ID
 *	@version - firmware version number
 *	@adapter - compatible adapter types
 *	@memsize - minimum memory size
 *	@reserved - reserved
 *	@startoffs - entry point offset
 *	@winoffs - dual-port memory window offset
 *	@codeoffs - code load offset
 *	@codesize - code size
 *	@dataoffs - configuration data load offset
 *	@datasize - configuration data size
 */
struct cycx_fw_info {
	unsigned short	codeid;
	unsigned short	version;
	unsigned short	adapter[CFM_MAX_CYCX];
	unsigned long	memsize;
	unsigned short	reserved[2];
	unsigned short	startoffs;
	unsigned short	winoffs;
	unsigned short	codeoffs;
	unsigned long	codesize;
	unsigned short	dataoffs;
	unsigned long	datasize;
};

/**
 *	struct cycx_firmware - CYCX firmware file structure
 *	@signature - CFM file signature
 *	@version - file format version
 *	@checksum - info + image
 *	@reserved - reserved
 *	@descr - description string
 *	@info - firmware module info
 *	@image - code image (variable size)
 */
struct cycx_firmware {
	char		    signature[80];
	unsigned short	    version;
	unsigned short	    checksum;
	unsigned short	    reserved[6];
	char		    descr[CFM_DESCR_LEN];
	struct cycx_fw_info info;
	unsigned char	    image[0];
};

struct cycx_fw_header {
	unsigned long  reset_size;
	unsigned long  data_size;
	unsigned long  code_size;
};
#endif	/* _CYCX_CFM_H */
