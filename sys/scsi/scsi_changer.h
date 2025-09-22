/*	$OpenBSD: scsi_changer.h,v 1.11 2023/04/11 00:45:09 jsg Exp $	*/
/*	$NetBSD: scsi_changer.h,v 1.7 1996/04/03 00:25:48 thorpej Exp $	*/

/*
 * Copyright (c) 1996 Jason R. Thorpe <thorpej@and.com>
 * All rights reserved.
 *
 * Partially based on an autochanger driver written by Stefan Grefen
 * and on an autochanger driver written by the Systems Programming Group
 * at the University of Utah Computer Science Department.
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
 */

/*
 * SCSI changer interface description
 */

/*
 * Partially derived from software written by Stefan Grefen
 * (grefen@goofy.zdv.uni-mainz.de soon grefen@convex.com)
 * based on the SCSI System by written Julian Elischer (julian@tfs.com)
 * for TRW Financial Systems.
 *
 * TRW Financial Systems, in accordance with their agreement with Carnegie
 * Mellon University, makes this software available to CMU to distribute
 * or use in any manner that they see fit as long as this message is kept with
 * the software. For this reason TFS also grants any other persons or
 * organisations permission to use or modify this software.
 *
 * TFS supplies this software to be publicly redistributed
 * on the understanding that TFS is not responsible for the correct
 * functioning of this software in any circumstances.
 *
 * Ported to run under 386BSD by Julian Elischer (julian@tfs.com) Sept 1992
 */

#ifndef _SCSI_SCSI_CHANGER_H
#define _SCSI_SCSI_CHANGER_H

/*
 * SCSI command format
 */

/*
 * Exchange the medium in the source element with the medium
 * located at the destination element.
 */
struct scsi_exchange_medium {
	u_int8_t	opcode;
#define EXCHANGE_MEDIUM		0xa6
	u_int8_t	byte2;
	u_int8_t	tea[2];	/* transport element address */
	u_int8_t	src[2];	/* source address */
	u_int8_t	fdst[2]; /* first destination address */
	u_int8_t	sdst[2]; /* second destination address */
	u_int8_t	flags;
#define EXCHANGE_MEDIUM_INV1	0x01
#define EXCHANGE_MEDIUM_INV2	0x02
	u_int8_t	control;
};

/*
 * Cause the medium changer to check all elements for medium and any
 * other status relevant to the element.
 */
struct scsi_initialize_elememt_status {
	u_int8_t	opcode;
#define INITIALIZE_ELEMENT_STATUS	0x07
	u_int8_t	byte2;
	u_int8_t	reserved[3];
	u_int8_t	control;
};

/*
 * Request the changer to move a unit of media from the source element
 * to the destination element.
 */
struct scsi_move_medium {
	u_int8_t	opcode;
#define MOVE_MEDIUM	0xa5
	u_int8_t	byte2;
	u_int8_t	tea[2];	/* transport element address */
	u_int8_t	src[2];	/* source element address */
	u_int8_t	dst[2];	/* destination element address */
	u_int8_t	reserved[2];
	u_int8_t	flags;
#define MOVE_MEDIUM_INVERT	0x01
	u_int8_t	control;
};

/*
 * Position the specified transport element (picker) in front of
 * the destination element specified.
 */
struct scsi_position_to_element {
	u_int8_t	opcode;
#define POSITION_TO_ELEMENT	0x2b
	u_int8_t	byte2;
	u_int8_t	tea[2];	/* transport element address */
	u_int8_t	dst[2];	/* destination element address */
	u_int8_t	reserved[2];
	u_int8_t	flags;
#define POSITION_TO_ELEMENT_INVERT	0x01
	u_int8_t	control;
};

/*
 * Request that the changer report the status of its internal elements.
 */
struct scsi_read_element_status {
	u_int8_t	opcode;
#define READ_ELEMENT_STATUS	0xb8
	u_int8_t	byte2;
#define READ_ELEMENT_STATUS_VOLTAG	0x10	/* report volume tag info */
	/* ...next 4 bits are an element type code... */
	u_int8_t	sea[2];	/* starting element address */
	u_int8_t	count[2]; /* number of elements */
	u_int8_t	reserved0;
	u_int8_t	len[3];	/* length of data buffer */
	u_int8_t	reserved1;
	u_int8_t	control;
};

struct scsi_request_volume_element_address {
	u_int8_t	opcode;
#define REQUEST_VOLUME_ELEMENT_ADDRESS	0xb5
	u_int8_t	byte2;
#define REQUEST_VOLUME_ELEMENT_ADDRESS_VOLTAG	0x10
	/* ...next 4 bits are an element type code... */
	u_int8_t	eaddr[2];	/* element address */
	u_int8_t	count[2];	/* number of elements */
	u_int8_t	reserved0;
	u_int8_t	len[3];		/* length of data buffer */
	u_int8_t	reserved1;
	u_int8_t	control;
};

/* XXX scsi_release */

/*
 * Data returned by READ ELEMENT STATUS consists of an 8-byte header
 * followed by one or more read_element_status_pages.
 */
struct read_element_status_header {
	u_int8_t	fear[2];  /* first element address reported */
	u_int8_t	count[2]; /* number of elements available */
	u_int8_t	reserved;
	u_int8_t	nbytes[3]; /* byte count of all pages */
};

struct read_element_status_page_header {
	u_int8_t	type;	/* element type code; see type codes below */
	u_int8_t	flags;
#define READ_ELEMENT_STATUS_AVOLTAG	0x40
#define READ_ELEMENT_STATUS_PVOLTAG	0x80
	u_int8_t	edl[2];	/* element descriptor length */
	u_int8_t	reserved;
	u_int8_t	nbytes[3]; /* byte count of all descriptors */
};

/*
 * Format of a volume tag
 */

struct volume_tag {
        u_int8_t        vif[32];        /* volume identification field */
        u_int8_t        reserved[2];
        u_int8_t        vsn[2];         /* volume sequence number */
};

struct read_element_status_descriptor {
	u_int8_t	eaddr[2];	/* element address */
	u_int8_t	flags1;

#define READ_ELEMENT_STATUS_FULL	0x01
#define READ_ELEMENT_STATUS_IMPEXP	0x02
#define READ_ELEMENT_STATUS_EXCEPT	0x04
#define READ_ELEMENT_STATUS_ACCESS	0x08
#define READ_ELEMENT_STATUS_EXENAB	0x10
#define READ_ELEMENT_STATUS_INENAB	0x20

#define READ_ELEMENT_STATUS_MT_MASK1	0x05
#define READ_ELEMENT_STATUS_ST_MASK1	0x0c
#define READ_ELEMENT_STATUS_IE_MASK1	0x3f
#define READ_ELEMENT_STATUS_DT_MASK1	0x0c

	u_int8_t	reserved0;
	u_int8_t	sense_code;
	u_int8_t	sense_qual;

	/*
	 * dt_scsi_flags and dt_scsi_addr are valid only on data transport
	 * elements.  These bytes are undefined for all other element types.
	 */
	u_int8_t	dt_scsi_flags;

#define READ_ELEMENT_STATUS_DT_LUNMASK	0x07
#define READ_ELEMENT_STATUS_DT_LUVALID	0x10
#define READ_ELEMENT_STATUS_DT_IDVALID	0x20
#define READ_ELEMENT_STATUS_DT_NOTBUS	0x80

	u_int8_t	dt_scsi_addr;

	u_int8_t	reserved1;

	u_int8_t	flags2;
#define READ_ELEMENT_STATUS_INVERT	0x40
#define READ_ELEMENT_STATUS_SVALID	0x80
	u_int8_t	ssea[2];	/* source storage element address */

	/*
	 * bytes 12-47:	Primary volume tag information.
	 *		(field omitted if PVOLTAG = 0)
	 *
	 * bytes 48-83:	Alternate volume tag information.
	 *		(field omitted if AVOLTAG = 0)
	 */

	struct volume_tag pvoltag;      /* omitted if PVOLTAG == 0 */
	struct volume_tag avoltag;      /* omitted if AVOLTAG == 0 */

	/*
	 * bytes 84-87:	Reserved (moved up if either of the above fields
	 *		are omitted)
	 *
	 * bytes 88-end: Vendor-specific: (moved up if either of the
	 *		 above fields are missing)
	 */
};

/* XXX add data returned by REQUEST VOLUME ELEMENT ADDRESS */

/* Element type codes */
#define ELEMENT_TYPE_MASK	0x0f	/* Note: these aren't bits */
#define ELEMENT_TYPE_ALL	0x00
#define ELEMENT_TYPE_MT		0x01
#define ELEMENT_TYPE_ST		0x02
#define ELEMENT_TYPE_IE		0x03
#define ELEMENT_TYPE_DT		0x04

/*
 * XXX The following definitions should be common to all SCSI device types.
 */
#define PGCODE_MASK	0x3f	/* valid page number bits in pg_code */
#define PGCODE_PS	0x80	/* indicates page is savable */

/*
 * Device capabilities page.
 *
 * This page defines characteristics of the element types in the
 * medium changer device.
 *
 * Note in the definitions below, the following abbreviations are
 * used:
 *		MT	Medium transport element (picker)
 *		ST	Storage transport element (slot)
 *		IE	Import/export element (portal)
 *		DT	Data transfer element (tape/disk drive)
 */
#define	CAP_PAGE	0x1f
struct page_device_capabilities {
	u_int8_t	pg_code;	/* page code (0x1f) */
	u_int8_t	pg_length;	/* page length (0x12) */

	/*
	 * The STOR_xx bits indicate that an element of a given
	 * type may provide independent storage for a unit of
	 * media.  The top four bits of this value are reserved.
	 */
	u_int8_t	stor;
#define STOR_MT		0x01
#define STOR_ST		0x02
#define STOR_IE		0x04
#define STOR_DT		0x08

	u_int8_t	reserved0;

	/*
	 * The MOVE_TO_yy bits indicate the changer supports
	 * moving a unit of medium from an element of a given type to an
	 * element of type yy.  This is used to determine if a given
	 * MOVE MEDIUM command is legal.  The top four bits of each
	 * of these values are reserved.
	 */
	u_int8_t	move_from_mt;
	u_int8_t	move_from_st;
	u_int8_t	move_from_ie;
	u_int8_t	move_from_dt;
#define MOVE_TO_MT	0x01
#define MOVE_TO_ST	0x02
#define MOVE_TO_IE	0x04
#define MOVE_TO_DT	0x08

	u_int8_t	reserved1[2];

	/*
	 * Similar to above, but for EXCHANGE MEDIUM.
	 */
	u_int8_t	exchange_with_mt;
	u_int8_t	exchange_with_st;
	u_int8_t	exchange_with_ie;
	u_int8_t	exchange_with_dt;
#define EXCHANGE_WITH_MT	0x01
#define EXCHANGE_WITH_ST	0x02
#define EXCHANGE_WITH_IE	0x04
#define EXCHANGE_WITH_DT	0x08
};

/*
 * Medium changer element address assignment page.
 *
 * Some of these fields can be a little confusing, so an explanation
 * is in order.
 *
 * Each component within a medium changer apparatus is called an
 * "element".
 *
 * The "medium transport element address" is the address of the first
 * picker (robotic arm).  "Number of medium transport elements" tells
 * us how many pickers exist in the changer.
 *
 * The "first storage element address" is the address of the first
 * slot in the tape or disk magazine.  "Number of storage elements" tells
 * us how many slots exist in the changer.
 *
 * The "first import/export element address" is the address of the first
 * medium portal accessible both by the medium changer and an outside
 * human operator.  This is where the changer might deposit tapes destined
 * for some vault.  The "number of import/export elements" tells us
 * not many of these portals exist in the changer.  NOTE: this number may
 * be 0.
 *
 * The "first data transfer element address" is the address of the first
 * tape or disk drive in the changer.  "Number of data transfer elements"
 * tells us how many drives exist in the changer.
 */
#define	EA_PAGE		0x1d
struct page_element_address_assignment {
	u_int8_t	pg_code;	/* page code (0x1d) */
	u_int8_t	pg_length;	/* page length (0x12) */

	/* Medium transport element address */
	u_int8_t	mtea[2];

	/* Number of medium transport elements */
	u_int8_t	nmte[2];

	/* First storage element address */
	u_int8_t	fsea[2];

	/* Number of storage elements */
	u_int8_t	nse[2];

	/* First import/export element address */
	u_int8_t	fieea[2];

	/* Number of import/export elements */
	u_int8_t	niee[2];

	/* First data transfer element address */
	u_int8_t	fdtea[2];

	/* Number of data transfer elements */
	u_int8_t	ndte[2];

	u_int8_t	reserved[2];
};

/*
 * Transport geometry parameters page.
 *
 * Defines whether each medium transport element is a member of a set of
 * elements that share a common robotics subsystem and whether the element
 * is capable of media rotation.  One transport geometry descriptor is
 * transferred for each medium transport element, beginning with the first
 * medium transport element (other than the default transport element address
 * of 0).
 */
#define	TGP_PAGE	0x1e
struct page_transport_geometry_parameters {
	u_int8_t	pg_code;	/* page code (0x1e) */
	u_int8_t	pg_length;	/* page length; variable */

	/* Transport geometry descriptor(s) are here. */

	u_int8_t	misc;
#define CAN_ROTATE	0x01

	/* Member number in transport element set. */
	u_int8_t	member;
};

#endif /* _SCSI_SCSI_CHANGER_H */
