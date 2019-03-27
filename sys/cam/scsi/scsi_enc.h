/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: (BSD-2-Clause-FreeBSD OR GPL-2.0)
 *
 * Copyright (c) 2000 by Matthew Jacob
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * the GNU Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#ifndef	_SCSI_ENC_H_
#define	_SCSI_ENC_H_

#include <cam/scsi/scsi_ses.h>

#define	ENCIOC			('s' - 040)
#define	ENCIOC_GETNELM		_IO(ENCIOC, 1)
#define	ENCIOC_GETELMMAP	_IO(ENCIOC, 2)
#define	ENCIOC_GETENCSTAT	_IO(ENCIOC, 3)
#define	ENCIOC_SETENCSTAT	_IO(ENCIOC, 4)
#define	ENCIOC_GETELMSTAT	_IO(ENCIOC, 5)
#define	ENCIOC_SETELMSTAT	_IO(ENCIOC, 6)
#define	ENCIOC_GETTEXT		_IO(ENCIOC, 7)
#define	ENCIOC_INIT		_IO(ENCIOC, 8)
#define	ENCIOC_GETELMDESC	_IO(ENCIOC, 9)
#define	ENCIOC_GETELMDEVNAMES	_IO(ENCIOC, 10)
#define	ENCIOC_GETSTRING	_IO(ENCIOC, 11)
#define	ENCIOC_SETSTRING	_IO(ENCIOC, 12)
#define	ENCIOC_GETENCNAME	_IO(ENCIOC, 13)
#define	ENCIOC_GETENCID		_IO(ENCIOC, 14)

/*
 * Platform Independent Definitions for enclosure devices.
 */
/*
 * SCSI Based Environmental Services Application Defines
 *
 * Based almost entirely on SCSI-3 ENC Revision 8A specification,
 * but slightly abstracted as the underlying device may in fact
 * be a SAF-TE or vendor unique device.
 */
/*
 * ENC Driver Operations:
 * (The defines themselves are platform and access method specific)
 *
 * ENCIOC_GETNELM
 * ENCIOC_GETELMMAP
 * ENCIOC_GETENCSTAT
 * ENCIOC_SETENCSTAT
 * ENCIOC_GETELMSTAT
 * ENCIOC_SETELMSTAT
 * ENCIOC_INIT
 *
 *
 * An application finds out how many elements an enclosure instance
 * is managing by performing a ENCIOC_GETNELM operation. It then
 * performs a ENCIOC_GETELMMAP to get the map that contains the
 * elment identifiers for all elements (see encioc_element_t below).
 * This information is static.
 * 
 * The application may perform ENCIOC_GETELMSTAT operations to retrieve
 * status on an element (see the enc_elm_status_t structure below),
 * ENCIOC_SETELMSTAT operations to set status for an element.
 *
 * Similarly, overall enclosure status me be fetched or set via
 * ENCIOC_GETENCSTAT or  ENCIOC_SETENCSTAT operations (see encioc_enc_status_t
 * below).
 *
 * Readers should note that there is nothing that requires either a set
 * or a clear operation to actually latch and do anything in the target.
 *
 * A ENCIOC_INIT operation causes the enclosure to be initialized.
 */

/* Element Types */
typedef enum {
	ELMTYP_UNSPECIFIED	= 0x00,
	ELMTYP_DEVICE		= 0x01,
	ELMTYP_POWER		= 0x02,
	ELMTYP_FAN		= 0x03,
	ELMTYP_THERM		= 0x04,
	ELMTYP_DOORLOCK		= 0x05,
	ELMTYP_ALARM		= 0x06,
	ELMTYP_ESCC		= 0x07,	/* Enclosure SCC */
	ELMTYP_SCC		= 0x08,	/* SCC */
	ELMTYP_NVRAM		= 0x09,
	ELMTYP_INV_OP_REASON    = 0x0a,
	ELMTYP_UPS		= 0x0b,
	ELMTYP_DISPLAY		= 0x0c,
	ELMTYP_KEYPAD		= 0x0d,
	ELMTYP_ENCLOSURE	= 0x0e,
	ELMTYP_SCSIXVR		= 0x0f,
	ELMTYP_LANGUAGE		= 0x10,
	ELMTYP_COMPORT		= 0x11,
	ELMTYP_VOM		= 0x12,
	ELMTYP_AMMETER		= 0x13,
	ELMTYP_SCSI_TGT		= 0x14,
	ELMTYP_SCSI_INI		= 0x15,
	ELMTYP_SUBENC		= 0x16,
	ELMTYP_ARRAY_DEV	= 0x17,
	ELMTYP_SAS_EXP		= 0x18, /* SAS expander */
	ELMTYP_SAS_CONN		= 0x19  /* SAS connector */
} elm_type_t;

typedef struct encioc_element {
	/* Element Index */
	unsigned int	elm_idx;	

	/* ID of SubEnclosure containing Element*/
	unsigned int	elm_subenc_id;

	/* Element Type */
	elm_type_t	elm_type;
} encioc_element_t;

/*
 * Overall Enclosure Status
 */
typedef unsigned char encioc_enc_status_t;

/*
 * Element Status
 */
typedef struct encioc_elm_status {
	unsigned int	elm_idx;
	unsigned char	cstat[4];
} encioc_elm_status_t;

/*
 * ENC String structure, for StringIn and StringOut commands; use this with
 * the ENCIOC_GETSTRING and ENCIOC_SETSTRING ioctls.
 */
typedef struct encioc_string {
	size_t bufsiz;		/* IN/OUT: length of string provided/returned */
#define	ENC_STRING_MAX	0xffff
	uint8_t *buf;		/* IN/OUT: string */
} encioc_string_t;

/*============================================================================*/

/* 
 * SES v2 r20 6.1.10 (pg 39) - Element Descriptor diagnostic page
 * Tables 21, 22, and 23
 */
typedef struct encioc_elm_desc {
	unsigned int	 elm_idx;       /* IN: elment requested */
	uint16_t	 elm_desc_len; /* IN: buffer size; OUT: bytes written */
	char		*elm_desc_str; /* IN/OUT: buffer for descriptor data */
} encioc_elm_desc_t;

/*
 * ENCIOC_GETELMDEVNAMES:
 * ioctl structure to get an element's device names, if available
 */
typedef struct  encioc_elm_devnames {
	unsigned int	 elm_idx;	/* IN: element index */
	size_t		 elm_names_size;/* IN: size of elm_devnames */
	size_t		 elm_names_len;	/* OUT: actual size returned */
	/*
	 * IN/OUT: comma separated list of peripheral driver
	 * instances servicing this element.
	 */
	char		*elm_devnames;
} encioc_elm_devnames_t;

/* ioctl structure for requesting FC info for a port */
typedef struct encioc_elm_fc_port {
	unsigned int		elm_idx;
	unsigned int		port_idx;
	struct ses_elm_fc_port	port_data;
} encioc_elm_fc_port_t;

/* ioctl structure for requesting SAS info for element phys */
typedef struct encioc_elm_sas_device_phy {
	unsigned int			elm_idx;
	unsigned int			phy_idx;
	struct ses_elm_sas_device_phy	phy_data;
} enioc_elm_sas_phy_t;

/* ioctl structure for requesting SAS info for an expander phy */
typedef struct encioc_elm_sas_expander_phy {
	unsigned int			elm_idx;
	unsigned int			phy_idx;
	struct ses_elm_sas_expander_phy phy_data;
} encioc_elm_sas_expander_phy_t;

/* ioctl structure for requesting SAS info for a port phy */
typedef struct encioc_elm_sas_port_phy {
	unsigned int			elm_idx;
	unsigned int			phy_idx;
	struct ses_elm_sas_port_phy	phy_data;
} enioc_elm_sas_port_phy_t;

/* ioctl structure for requesting additional status for an element */
typedef struct encioc_addl_status {
	unsigned int			   elm_idx;
	union ses_elm_addlstatus_descr_hdr addl_hdr;
	union ses_elm_addlstatus_proto_hdr proto_hdr;
} enioc_addl_status_t;

#endif /* _SCSI_ENC_H_ */
