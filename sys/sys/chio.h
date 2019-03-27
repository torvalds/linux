/*	$NetBSD: chio.h,v 1.9 1997/09/29 17:32:26 mjacob Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1996 Jason R. Thorpe <thorpej@and.com>
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgements:
 *	This product includes software developed by Jason R. Thorpe
 *	for And Communications, http://www.and.com/
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_SYS_CHIO_H_
#define	_SYS_CHIO_H_

#ifndef _KERNEL
#include <sys/types.h>
#endif
#include <sys/ioccom.h>

/*
 * Element types.  Used as "to" and "from" type indicators in move
 * and exchange operations.
 *
 * Note that code in sys/scsi/ch.c relies on these values (uses them
 * as offsets in an array, and other evil), so don't muck with them
 * unless you know what you're doing.
 */
#define CHET_MT		0	/* medium transport (picker) */
#define CHET_ST		1	/* storage transport (slot) */
#define CHET_IE		2	/* import/export (portal) */
#define CHET_DT		3	/* data transfer (drive) */
#define CHET_MAX	CHET_DT

/*
 * Maximum length of a volume identification string
 */
#define CH_VOLTAG_MAXLEN	32

/*
 * Structure used to execute a MOVE MEDIUM command.
 */
struct changer_move {
	u_int16_t	cm_fromtype;	/* element type to move from */
	u_int16_t	cm_fromunit;	/* logical unit of from element */
	u_int16_t	cm_totype;	/* element type to move to */
	u_int16_t	cm_tounit;	/* logical unit of to element */
	u_int16_t	cm_flags;	/* misc. flags */
};

/* cm_flags */
#define CM_INVERT	0x01	/* invert media */

/*
 * Structure used to execute an EXCHANGE MEDIUM command.  In an
 * exchange operation, the following steps occur:
 *
 *	- media from source is moved to first destination.
 *
 *	- media previously occupying first destination is moved
 *	  to the second destination.
 *
 * The second destination may or may not be the same as the source.
 * In the case of a simple exchange, the source and second destination
 * are the same.
 */
struct changer_exchange {
	u_int16_t	ce_srctype;	/* element type of source */
	u_int16_t	ce_srcunit;	/* logical unit of source */
	u_int16_t	ce_fdsttype;	/* element type of first destination */
	u_int16_t	ce_fdstunit;	/* logical unit of first destination */
	u_int16_t	ce_sdsttype;	/* element type of second destination */
	u_int16_t	ce_sdstunit;	/* logical unit of second destination */
	u_int16_t	ce_flags;	/* misc. flags */
};

/* ce_flags */
#define CE_INVERT1	0x01	/* invert media 1 */
#define CE_INVERT2	0x02	/* invert media 2 */

/*
 * Structure used to execute a POSITION TO ELEMENT command.  This
 * moves the current picker in front of the specified element.
 */
struct changer_position {
	u_int16_t	cp_type;	/* element type */
	u_int16_t	cp_unit;	/* logical unit of element */
	u_int16_t	cp_flags;	/* misc. flags */
};

/* cp_flags */
#define CP_INVERT	0x01	/* invert picker */

/*
 * Data returned by CHIOGPARAMS.
 */
struct changer_params {
	u_int16_t	cp_npickers;	/* number of pickers */
	u_int16_t	cp_nslots;	/* number of slots */
	u_int16_t	cp_nportals;	/* number of import/export portals */
	u_int16_t	cp_ndrives;	/* number of drives */
};

/*
 * Command used to get element status.
 */

struct changer_voltag {
	u_char		cv_volid[CH_VOLTAG_MAXLEN+1];
	u_int16_t	cv_serial;
};

typedef struct changer_voltag changer_voltag_t;

/*
 * Flags definitions for ces_status
 * Not all flags have meaning for all element types.
 */
typedef enum {
	CES_STATUS_FULL	  = 0x001,	/* element is full */
	CES_STATUS_IMPEXP = 0x002,	/* media deposited by operator */
	CES_STATUS_EXCEPT = 0x004,	/* element in abnormal state */
	CES_PICKER_MASK	  = 0x005,	/* flags valid for pickers */
	CES_STATUS_ACCESS = 0x008,	/* media accessible by picker */
	CES_SLOT_MASK	  = 0x00c,	/* flags valid for slots */
	CES_DRIVE_MASK	  = 0x00c,	/* flags valid for drives */
	CES_STATUS_EXENAB = 0x010,	/* element supports exporting */
	CES_STATUS_INENAB = 0x020,	/* element supports importing */
	CES_PORTAL_MASK	  = 0x03f,	/* flags valid for portals */
	CES_INVERT	  = 0x040,	/* invert bit */
	CES_SOURCE_VALID  = 0x080,	/* source address (ces_source) valid */
	CES_SCSIID_VALID  = 0x100,	/* ces_scsi_id is valid */
	CES_LUN_VALID	  = 0x200,	/* ces_scsi_lun is valid */
	CES_PIV		  = 0x400	/* ces_protocol_id is valid */
} ces_status_flags;

struct changer_element_status {
	u_int8_t		ces_type;	  /* element type */
	u_int16_t		ces_addr;	  /* logical element address */
	u_int16_t		ces_int_addr;	  /* changer element address */
	ces_status_flags	ces_flags;	  /* 
						   * see CESTATUS definitions
						   * below 
						   */ 
	u_int8_t		ces_sensecode;	  /* 
						   * additional sense
						   * code for element */
	u_int8_t		ces_sensequal;	  /*
						   * additional sense
						   * code qualifier 
						   */
	u_int8_t		ces_source_type;  /* 
						   * element type of
						   * source address 
						   */
	u_int16_t		ces_source_addr;  /* 
						   * source address of medium
						   */
	changer_voltag_t     	ces_pvoltag;	  /* primary volume tag */
	changer_voltag_t	ces_avoltag;	  /* alternate volume tag */
	u_int8_t		ces_scsi_id;	  /* SCSI id of element */
	u_int8_t		ces_scsi_lun;	  /* SCSI lun of element */

	/*
	 * Data members for SMC3 and later versions
	 */
	u_int8_t		ces_medium_type;
#define	CES_MEDIUM_TYPE_UNKNOWN		0	/* Medium type unspecified */
#define	CES_MEDIUM_TYPE_DATA		1	/* Data medium */
#define	CES_MEDIUM_TYPE_CLEANING	2	/* Cleaning medium */
#define	CES_MEDIUM_TYPE_DIAGNOSTIC	3	/* Diagnostic medium */
#define	CES_MEDIUM_TYPE_WORM		4	/* WORM medium */
#define	CES_MEDIUM_TYPE_MICROCODE	5	/* Microcode image medium */

	u_int8_t		ces_protocol_id;
#define	CES_PROTOCOL_ID_FCP_4	0	/* Fiber channel */
#define	CES_PROTOCOL_ID_SPI_5	1	/* Parallel SCSI */
#define	CES_PROTOCOL_ID_SSA_S3P	2	/* SSA */
#define	CES_PROTOCOL_ID_SBP_3	3	/* IEEE 1394 */
#define	CES_PROTOCOL_ID_SRP	4	/* SCSI Remote DMA */
#define	CES_PROTOCOL_ID_ISCSI	5	/* iSCSI */
#define	CES_PROTOCOL_ID_SPL	6	/* SAS */
#define	CES_PROTOCOL_ID_ADT_2	7	/* Automation/Drive Interface */
#define	CES_PROTOCOL_ID_ACS_2	8	/* ATA */

	u_int8_t		ces_assoc;
#define	CES_ASSOC_LOGICAL_UNIT	0
#define	CES_ASSOC_TARGET_PORT	1
#define	CES_ASSOC_TARGET_DEVICE	2

	u_int8_t		ces_designator_type;
#define	CES_DESIGNATOR_TYPE_VENDOR_SPECIFIC	0
#define	CES_DESIGNATOR_TYPE_T10_VENDOR_ID	1
#define	CES_DESIGNATOR_TYPE_EUI_64		2
#define	CES_DESIGNATOR_TYPE_NAA			3
#define	CES_DESIGNATOR_TYPE_TARGET_PORT_ID	4
#define	CES_DESIGNATOR_TYPE_TARGET_PORT_GRP	5
#define	CES_DESIGNATOR_TYPE_LOGICAL_UNIT_GRP	6
#define	CES_DESIGNATOR_TYPE_MD5_LOGICAL_UNIT_ID	7
#define	CES_DESIGNATOR_TYPE_SCSI_NAME_STRING	8

	u_int8_t		ces_code_set;
#define	CES_CODE_SET_RESERVED	0
#define	CES_CODE_SET_BINARY	1
#define	CES_CODE_SET_ASCII	2
#define	CES_CODE_SET_UTF_8	3

	u_int8_t		ces_designator_length;

#define	CES_MAX_DESIGNATOR_LENGTH (1 << 8)
	u_int8_t		ces_designator[CES_MAX_DESIGNATOR_LENGTH + 1];
};

struct changer_element_status_request {
	u_int16_t			cesr_element_type;
	u_int16_t			cesr_element_base;
	u_int16_t			cesr_element_count;

	u_int16_t			cesr_flags;
#define	CESR_VOLTAGS	0x01

	struct changer_element_status	*cesr_element_status;
};


struct changer_set_voltag_request {
	u_int16_t		csvr_type;
	u_int16_t		csvr_addr;

	u_int16_t		csvr_flags;
#define	CSVR_MODE_MASK		0x0f	/* mode mask, acceptable modes below: */
#define	CSVR_MODE_SET		0x00	/* set volume tag if not set */
#define	CSVR_MODE_REPLACE	0x01	/* unconditionally replace volume tag */
#define	CSVR_MODE_CLEAR		0x02	/* clear volume tag */

#define	CSVR_ALTERNATE		0x10	/* set to work with alternate voltag */

	changer_voltag_t     	csvr_voltag;
};


#define	CESTATUS_BITS	\
	"\20\6INENAB\5EXENAB\4ACCESS\3EXCEPT\2IMPEXP\1FULL"

#define	CHIOMOVE	_IOW('c', 0x01, struct changer_move)
#define	CHIOEXCHANGE	_IOW('c', 0x02, struct changer_exchange)
#define	CHIOPOSITION	_IOW('c', 0x03, struct changer_position)
#define	CHIOGPICKER	_IOR('c', 0x04, int)
#define	CHIOSPICKER	_IOW('c', 0x05, int)
#define	CHIOGPARAMS	_IOR('c', 0x06, struct changer_params)
#define	CHIOIELEM	_IOW('c', 0x07, u_int32_t)
#define	OCHIOGSTATUS	_IOW('c', 0x08, struct changer_element_status_request)
#define	CHIOSETVOLTAG	_IOW('c', 0x09, struct changer_set_voltag_request)
#define	CHIOGSTATUS	_IOW('c', 0x0A, struct changer_element_status_request)

#endif /* !_SYS_CHIO_H_ */
