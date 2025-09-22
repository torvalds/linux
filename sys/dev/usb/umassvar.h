/*	$OpenBSD: umassvar.h,v 1.16 2020/11/23 21:33:38 krw Exp $ */
/*	$NetBSD: umassvar.h,v 1.20 2003/09/08 19:31:01 mycroft Exp $	*/
/*-
 * Copyright (c) 1999 MAEKAWA Masahide <bishop@rr.iij4u.or.jp>,
 *		      Nick Hibma <n_hibma@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 *     $FreeBSD: src/sys/dev/usb/umass.c,v 1.13 2000/03/26 01:39:12 n_hibma Exp $
 */

#ifdef UMASS_DEBUG
#define DIF(m, x)	if (umassdebug & (m)) do { x ; } while (0)
#define DPRINTF(m, x)	do { if (umassdebug & (m)) printf x; } while (0)
#define UDMASS_UPPER	0x00008000	/* upper layer */
#define UDMASS_GEN	0x00010000	/* general */
#define UDMASS_SCSI	0x00020000	/* scsi */
#define UDMASS_UFI	0x00040000	/* ufi command set */
#define UDMASS_8070	0x00080000	/* 8070i command set */
#define UDMASS_USB	0x00100000	/* USB general */
#define UDMASS_BBB	0x00200000	/* Bulk-Only transfers */
#define UDMASS_CBI	0x00400000	/* CBI transfers */
#define UDMASS_ALL	0xffff0000	/* all of the above */

#define UDMASS_XFER	0x40000000	/* all transfers */
#define UDMASS_CMD	0x80000000

extern int umassdebug;
#else
#define DIF(m, x)	/* nop */
#define DPRINTF(m, x)	/* nop */
#endif

/* Generic definitions */

#define UFI_COMMAND_LENGTH 12

/* Direction for umass_*_transfer */
#define DIR_NONE	0
#define DIR_IN		1
#define DIR_OUT		2

/* Endpoints for umass */
#define	UMASS_BULKIN	0
#define	UMASS_BULKOUT	1
#define	UMASS_INTRIN	2
#define	UMASS_NEP	3

/* Bulk-Only features */

#define UR_BBB_RESET	0xff		/* Bulk-Only reset */
#define	UR_BBB_GET_MAX_LUN	0xfe

/* Command Block Wrapper */
struct umass_bbb_cbw {
	uDWord		dCBWSignature;
#define CBWSIGNATURE	0x43425355
	uDWord		dCBWTag;
	uDWord		dCBWDataTransferLength;
	uByte		bCBWFlags;
#define CBWFLAGS_OUT	0x00
#define CBWFLAGS_IN	0x80
	uByte		bCBWLUN;
	uByte		bCDBLength;
#define CBWCDBLENGTH	16
	uByte		CBWCDB[CBWCDBLENGTH];
};
#define UMASS_BBB_CBW_SIZE	31

/* Command Status Wrapper */
struct umass_bbb_csw {
	uDWord		dCSWSignature;
#define CSWSIGNATURE		0x53425355
#define CSWSIGNATURE_OLYMPUS_C1	0x55425355
	uDWord		dCSWTag;
	uDWord		dCSWDataResidue;
	uByte		bCSWStatus;
#define CSWSTATUS_GOOD	0x0
#define CSWSTATUS_FAILED 0x1
#define CSWSTATUS_PHASE	0x2
};
#define UMASS_BBB_CSW_SIZE	13

/* CBI features */

#define UR_CBI_ADSC	0x00

typedef unsigned char umass_cbi_cbl_t[16];	/* Command block */

typedef union {
	struct {
		uByte	type;
#define IDB_TYPE_CCI		0x00
		uByte	value;
#define IDB_VALUE_PASS		0x00
#define IDB_VALUE_FAIL		0x01
#define IDB_VALUE_PHASE		0x02
#define IDB_VALUE_PERSISTENT	0x03
#define IDB_VALUE_STATUS_MASK	0x03
	} common;

	struct {
		uByte	asc;
		uByte	ascq;
	} ufi;
} umass_cbi_sbl_t;

struct umass_softc;		/* see below */

typedef void (*umass_callback)(struct umass_softc *, void *, int, int);
#define STATUS_CMD_OK		0	/* everything ok */
#define STATUS_CMD_UNKNOWN	1	/* will have to fetch sense */
#define STATUS_CMD_FAILED	2	/* transfer was ok, command failed */
#define STATUS_WIRE_FAILED	3	/* couldn't even get command across */

typedef void (*umass_wire_xfer)(struct umass_softc *, int, void *, int, void *,
				int, int, u_int, umass_callback, void *);
typedef void (*umass_wire_reset)(struct umass_softc *, int);
typedef void (*umass_wire_state)(struct usbd_xfer *, void *, usbd_status);

struct umass_wire_methods {
	umass_wire_xfer		wire_xfer;
	umass_wire_reset	wire_reset;
	umass_wire_state	wire_state;
};

struct umass_scsi_softc;

/* the per device structure */
struct umass_softc {
	struct device		sc_dev;		/* base device */
	struct usbd_device	*sc_udev;	/* device */
	struct usbd_interface	*sc_iface;	/* interface */
	int			sc_ifaceno;	/* interface number */

	u_int8_t		sc_epaddr[UMASS_NEP];
	struct usbd_pipe	*sc_pipe[UMASS_NEP];
	usb_device_request_t	sc_req;

	const struct umass_wire_methods *sc_methods;

	u_int8_t		sc_wire;	/* wire protocol */
#define	UMASS_WPROTO_UNSPEC	0
#define	UMASS_WPROTO_BBB	1
#define	UMASS_WPROTO_CBI	2
#define	UMASS_WPROTO_CBI_I	3

	u_int8_t		sc_cmd;		/* command protocol */
#define	UMASS_CPROTO_UNSPEC	0
#define	UMASS_CPROTO_SCSI	1
#define	UMASS_CPROTO_ATAPI	2
#define	UMASS_CPROTO_UFI	3
#define	UMASS_CPROTO_RBC	4
#define UMASS_CPROTO_ISD_ATA	5

	u_int32_t		sc_quirks;
#define UMASS_QUIRK_WRONG_CSWSIG	0x00000001
#define UMASS_QUIRK_WRONG_CSWTAG	0x00000002
#define UMASS_QUIRK_IGNORE_RESIDUE	0x00000004

	u_int32_t		sc_busquirks;

	/* Bulk specific variables for transfers in progress */
	struct umass_bbb_cbw	cbw;	/* command block wrapper */
	struct umass_bbb_csw	csw;	/* command status wrapper*/
	/* CBI specific variables for transfers in progress */
	umass_cbi_cbl_t		cbl;	/* command block */
	umass_cbi_sbl_t		sbl;	/* status block */

	/* xfer handles
	 * Most of our operations are initiated from interrupt context, so
	 * we need to avoid using the one that is in use. We want to avoid
	 * allocating them in the interrupt context as well.
	 */
	/* indices into array below */
#define XFER_BBB_CBW		0	/* Bulk-Only */
#define XFER_BBB_DATA		1
#define XFER_BBB_DCLEAR		2
#define XFER_BBB_CSW1		3
#define XFER_BBB_CSW2		4
#define XFER_BBB_SCLEAR		5
#define XFER_BBB_RESET1		6
#define XFER_BBB_RESET2		7
#define XFER_BBB_RESET3		8

#define XFER_CBI_CB		0	/* CBI */
#define XFER_CBI_DATA		1
#define XFER_CBI_STATUS		2
#define XFER_CBI_DCLEAR		3
#define XFER_CBI_SCLEAR		4
#define XFER_CBI_RESET1		5
#define XFER_CBI_RESET2		6
#define XFER_CBI_RESET3		7

#define XFER_NR			9	/* maximum number */

	struct usbd_xfer	*transfer_xfer[XFER_NR]; /* for ctrl xfers */

	void			*data_buffer;

	int			transfer_dir;		/* data direction */
	void			*transfer_data;		/* data buffer */
	int			transfer_datalen;	/* (maximum) length */
	int			transfer_actlen;	/* actual length */
	umass_callback		transfer_cb;		/* callback */
	void			*transfer_priv;		/* for callback */
	int			transfer_status;

	int			transfer_state;
#define TSTATE_IDLE			0
#define TSTATE_BBB_COMMAND		1	/* CBW transfer */
#define TSTATE_BBB_DATA			2	/* Data transfer */
#define TSTATE_BBB_DCLEAR		3	/* clear endpt stall */
#define TSTATE_BBB_STATUS1		4	/* clear endpt stall */
#define TSTATE_BBB_SCLEAR		5	/* clear endpt stall */
#define TSTATE_BBB_STATUS2		6	/* CSW transfer */
#define TSTATE_BBB_RESET1		7	/* reset command */
#define TSTATE_BBB_RESET2		8	/* in clear stall */
#define TSTATE_BBB_RESET3		9	/* out clear stall */
#define TSTATE_CBI_COMMAND		10	/* command transfer */
#define TSTATE_CBI_DATA			11	/* data transfer */
#define TSTATE_CBI_STATUS		12	/* status transfer */
#define TSTATE_CBI_DCLEAR		13	/* clear ep stall */
#define TSTATE_CBI_SCLEAR		14	/* clear ep stall */
#define TSTATE_CBI_RESET1		15	/* reset command */
#define TSTATE_CBI_RESET2		16	/* in clear stall */
#define TSTATE_CBI_RESET3		17	/* out clear stall */
#define TSTATE_STATES			18	/* # of states above */


	int			timeout;		/* in msecs */

	u_int8_t		maxlun;			/* max lun supported */

#ifdef UMASS_DEBUG
	struct timeval tv;
#endif

	int			sc_xfer_flags;
	int			sc_refcnt;
	int			sc_sense;

	struct umass_scsi_softc	*bus;		 /* bus dependent data */

	/* For polled transfers */
	int			polling_depth;
	usbd_status		polled_xfer_status;
	struct usbd_xfer	*next_polled_xfer;
};

#define UMASS_MAX_TRANSFER_SIZE	MAXBSIZE
