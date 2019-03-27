#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 MAEKAWA Masahide <bishop@rr.iij4u.or.jp>,
 *		      Nick Hibma <n_hibma@FreeBSD.org>
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
 *	$FreeBSD$
 *	$NetBSD: umass.c,v 1.28 2000/04/02 23:46:53 augustss Exp $
 */

/* Also already merged from NetBSD:
 *	$NetBSD: umass.c,v 1.67 2001/11/25 19:05:22 augustss Exp $
 *	$NetBSD: umass.c,v 1.90 2002/11/04 19:17:33 pooka Exp $
 *	$NetBSD: umass.c,v 1.108 2003/11/07 17:03:25 wiz Exp $
 *	$NetBSD: umass.c,v 1.109 2003/12/04 13:57:31 keihan Exp $
 */

/*
 * Universal Serial Bus Mass Storage Class specs:
 * http://www.usb.org/developers/devclass_docs/usb_msc_overview_1.2.pdf
 * http://www.usb.org/developers/devclass_docs/usbmassbulk_10.pdf
 * http://www.usb.org/developers/devclass_docs/usb_msc_cbi_1.1.pdf
 * http://www.usb.org/developers/devclass_docs/usbmass-ufi10.pdf
 */

/*
 * Ported to NetBSD by Lennart Augustsson <augustss@NetBSD.org>.
 * Parts of the code written by Jason R. Thorpe <thorpej@shagadelic.org>.
 */

/*
 * The driver handles 3 Wire Protocols
 * - Command/Bulk/Interrupt (CBI)
 * - Command/Bulk/Interrupt with Command Completion Interrupt (CBI with CCI)
 * - Mass Storage Bulk-Only (BBB)
 *   (BBB refers Bulk/Bulk/Bulk for Command/Data/Status phases)
 *
 * Over these wire protocols it handles the following command protocols
 * - SCSI
 * - UFI (floppy command set)
 * - 8070i (ATAPI)
 *
 * UFI and 8070i (ATAPI) are transformed versions of the SCSI command set. The
 * sc->sc_transform method is used to convert the commands into the appropriate
 * format (if at all necessary). For example, UFI requires all commands to be
 * 12 bytes in length amongst other things.
 *
 * The source code below is marked and can be split into a number of pieces
 * (in this order):
 *
 * - probe/attach/detach
 * - generic transfer routines
 * - BBB
 * - CBI
 * - CBI_I (in addition to functions from CBI)
 * - CAM (Common Access Method)
 * - SCSI
 * - UFI
 * - 8070i (ATAPI)
 *
 * The protocols are implemented using a state machine, for the transfers as
 * well as for the resets. The state machine is contained in umass_t_*_callback.
 * The state machine is started through either umass_command_start() or
 * umass_reset().
 *
 * The reason for doing this is a) CAM performs a lot better this way and b) it
 * avoids using tsleep from interrupt context (for example after a failed
 * transfer).
 */

/*
 * The SCSI related part of this driver has been derived from the
 * dev/ppbus/vpo.c driver, by Nicolas Souchu (nsouch@FreeBSD.org).
 *
 * The CAM layer uses so called actions which are messages sent to the host
 * adapter for completion. The actions come in through umass_cam_action. The
 * appropriate block of routines is called depending on the transport protocol
 * in use. When the transfer has finished, these routines call
 * umass_cam_cb again to complete the CAM command.
 */

#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/unistd.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/priv.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include "usbdevs.h"

#include <dev/usb/quirk/usb_quirk.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_da.h>

#include <cam/cam_periph.h>

#ifdef USB_DEBUG
#define	DIF(m, x)				\
  do {						\
    if (umass_debug & (m)) { x ; }		\
  } while (0)

#define	DPRINTF(sc, m, fmt, ...)			\
  do {							\
    if (umass_debug & (m)) {				\
        printf("%s:%s: " fmt,				\
	       (sc) ? (const char *)(sc)->sc_name :	\
	       (const char *)"umassX",			\
		__FUNCTION__ ,## __VA_ARGS__);		\
    }							\
  } while (0)

#define	UDMASS_GEN	0x00010000	/* general */
#define	UDMASS_SCSI	0x00020000	/* scsi */
#define	UDMASS_UFI	0x00040000	/* ufi command set */
#define	UDMASS_ATAPI	0x00080000	/* 8070i command set */
#define	UDMASS_CMD	(UDMASS_SCSI|UDMASS_UFI|UDMASS_ATAPI)
#define	UDMASS_USB	0x00100000	/* USB general */
#define	UDMASS_BBB	0x00200000	/* Bulk-Only transfers */
#define	UDMASS_CBI	0x00400000	/* CBI transfers */
#define	UDMASS_WIRE	(UDMASS_BBB|UDMASS_CBI)
#define	UDMASS_ALL	0xffff0000	/* all of the above */
static int umass_debug;
static int umass_throttle;

static SYSCTL_NODE(_hw_usb, OID_AUTO, umass, CTLFLAG_RW, 0, "USB umass");
SYSCTL_INT(_hw_usb_umass, OID_AUTO, debug, CTLFLAG_RWTUN,
    &umass_debug, 0, "umass debug level");
SYSCTL_INT(_hw_usb_umass, OID_AUTO, throttle, CTLFLAG_RWTUN,
    &umass_throttle, 0, "Forced delay between commands in milliseconds");
#else
#define	DIF(...) do { } while (0)
#define	DPRINTF(...) do { } while (0)
#endif

#define	UMASS_BULK_SIZE (1 << 17)
#define	UMASS_CBI_DIAGNOSTIC_CMDLEN 12	/* bytes */
#define	UMASS_MAX_CMDLEN MAX(12, CAM_MAX_CDBLEN)	/* bytes */

/* USB transfer definitions */

#define	UMASS_T_BBB_RESET1      0	/* Bulk-Only */
#define	UMASS_T_BBB_RESET2      1
#define	UMASS_T_BBB_RESET3      2
#define	UMASS_T_BBB_COMMAND     3
#define	UMASS_T_BBB_DATA_READ   4
#define	UMASS_T_BBB_DATA_RD_CS  5
#define	UMASS_T_BBB_DATA_WRITE  6
#define	UMASS_T_BBB_DATA_WR_CS  7
#define	UMASS_T_BBB_STATUS      8
#define	UMASS_T_BBB_MAX         9

#define	UMASS_T_CBI_RESET1      0	/* CBI */
#define	UMASS_T_CBI_RESET2      1
#define	UMASS_T_CBI_RESET3      2
#define	UMASS_T_CBI_COMMAND     3
#define	UMASS_T_CBI_DATA_READ   4
#define	UMASS_T_CBI_DATA_RD_CS  5
#define	UMASS_T_CBI_DATA_WRITE  6
#define	UMASS_T_CBI_DATA_WR_CS  7
#define	UMASS_T_CBI_STATUS      8
#define	UMASS_T_CBI_RESET4      9
#define	UMASS_T_CBI_MAX        10

#define	UMASS_T_MAX MAX(UMASS_T_CBI_MAX, UMASS_T_BBB_MAX)

/* Generic definitions */

/* Direction for transfer */
#define	DIR_NONE	0
#define	DIR_IN		1
#define	DIR_OUT		2

/* device name */
#define	DEVNAME		"umass"
#define	DEVNAME_SIM	"umass-sim"

/* Approximate maximum transfer speeds (assumes 33% overhead). */
#define	UMASS_FULL_TRANSFER_SPEED	1000
#define	UMASS_HIGH_TRANSFER_SPEED	40000
#define	UMASS_SUPER_TRANSFER_SPEED	400000
#define	UMASS_FLOPPY_TRANSFER_SPEED	20

#define	UMASS_TIMEOUT			5000	/* ms */

/* CAM specific definitions */

#define	UMASS_SCSIID_MAX	1	/* maximum number of drives expected */
#define	UMASS_SCSIID_HOST	UMASS_SCSIID_MAX

/* Bulk-Only features */

#define	UR_BBB_RESET		0xff	/* Bulk-Only reset */
#define	UR_BBB_GET_MAX_LUN	0xfe	/* Get maximum lun */

/* Command Block Wrapper */
typedef struct {
	uDWord	dCBWSignature;
#define	CBWSIGNATURE	0x43425355
	uDWord	dCBWTag;
	uDWord	dCBWDataTransferLength;
	uByte	bCBWFlags;
#define	CBWFLAGS_OUT	0x00
#define	CBWFLAGS_IN	0x80
	uByte	bCBWLUN;
	uByte	bCDBLength;
#define	CBWCDBLENGTH	16
	uByte	CBWCDB[CBWCDBLENGTH];
} __packed umass_bbb_cbw_t;

#define	UMASS_BBB_CBW_SIZE	31

/* Command Status Wrapper */
typedef struct {
	uDWord	dCSWSignature;
#define	CSWSIGNATURE	0x53425355
#define	CSWSIGNATURE_IMAGINATION_DBX1	0x43425355
#define	CSWSIGNATURE_OLYMPUS_C1	0x55425355
	uDWord	dCSWTag;
	uDWord	dCSWDataResidue;
	uByte	bCSWStatus;
#define	CSWSTATUS_GOOD	0x0
#define	CSWSTATUS_FAILED	0x1
#define	CSWSTATUS_PHASE	0x2
} __packed umass_bbb_csw_t;

#define	UMASS_BBB_CSW_SIZE	13

/* CBI features */

#define	UR_CBI_ADSC	0x00

typedef union {
	struct {
		uint8_t	type;
#define	IDB_TYPE_CCI		0x00
		uint8_t	value;
#define	IDB_VALUE_PASS		0x00
#define	IDB_VALUE_FAIL		0x01
#define	IDB_VALUE_PHASE		0x02
#define	IDB_VALUE_PERSISTENT	0x03
#define	IDB_VALUE_STATUS_MASK	0x03
	} __packed common;

	struct {
		uint8_t	asc;
		uint8_t	ascq;
	} __packed ufi;
} __packed umass_cbi_sbl_t;

struct umass_softc;			/* see below */

typedef void (umass_callback_t)(struct umass_softc *sc, union ccb *ccb,
    	uint32_t residue, uint8_t status);

#define	STATUS_CMD_OK		0	/* everything ok */
#define	STATUS_CMD_UNKNOWN	1	/* will have to fetch sense */
#define	STATUS_CMD_FAILED	2	/* transfer was ok, command failed */
#define	STATUS_WIRE_FAILED	3	/* couldn't even get command across */

typedef uint8_t (umass_transform_t)(struct umass_softc *sc, uint8_t *cmd_ptr,
    	uint8_t cmd_len);

/* Wire and command protocol */
#define	UMASS_PROTO_BBB		0x0001	/* USB wire protocol */
#define	UMASS_PROTO_CBI		0x0002
#define	UMASS_PROTO_CBI_I	0x0004
#define	UMASS_PROTO_WIRE	0x00ff	/* USB wire protocol mask */
#define	UMASS_PROTO_SCSI	0x0100	/* command protocol */
#define	UMASS_PROTO_ATAPI	0x0200
#define	UMASS_PROTO_UFI		0x0400
#define	UMASS_PROTO_RBC		0x0800
#define	UMASS_PROTO_COMMAND	0xff00	/* command protocol mask */

/* Device specific quirks */
#define	NO_QUIRKS		0x0000
	/*
	 * The drive does not support Test Unit Ready. Convert to Start Unit
	 */
#define	NO_TEST_UNIT_READY	0x0001
	/*
	 * The drive does not reset the Unit Attention state after REQUEST
	 * SENSE has been sent. The INQUIRY command does not reset the UA
	 * either, and so CAM runs in circles trying to retrieve the initial
	 * INQUIRY data.
	 */
#define	RS_NO_CLEAR_UA		0x0002
	/* The drive does not support START STOP.  */
#define	NO_START_STOP		0x0004
	/* Don't ask for full inquiry data (255b).  */
#define	FORCE_SHORT_INQUIRY	0x0008
	/* Needs to be initialised the Shuttle way */
#define	SHUTTLE_INIT		0x0010
	/* Drive needs to be switched to alternate iface 1 */
#define	ALT_IFACE_1		0x0020
	/* Drive does not do 1Mb/s, but just floppy speeds (20kb/s) */
#define	FLOPPY_SPEED		0x0040
	/* The device can't count and gets the residue of transfers wrong */
#define	IGNORE_RESIDUE		0x0080
	/* No GetMaxLun call */
#define	NO_GETMAXLUN		0x0100
	/* The device uses a weird CSWSIGNATURE. */
#define	WRONG_CSWSIG		0x0200
	/* Device cannot handle INQUIRY so fake a generic response */
#define	NO_INQUIRY		0x0400
	/* Device cannot handle INQUIRY EVPD, return CHECK CONDITION */
#define	NO_INQUIRY_EVPD		0x0800
	/* Pad all RBC requests to 12 bytes. */
#define	RBC_PAD_TO_12		0x1000
	/*
	 * Device reports number of sectors from READ_CAPACITY, not max
	 * sector number.
	 */
#define	READ_CAPACITY_OFFBY1	0x2000
	/*
	 * Device cannot handle a SCSI synchronize cache command.  Normally
	 * this quirk would be handled in the cam layer, but for IDE bridges
	 * we need to associate the quirk with the bridge and not the
	 * underlying disk device.  This is handled by faking a success
	 * result.
	 */
#define	NO_SYNCHRONIZE_CACHE	0x4000
	/* Device does not support 'PREVENT/ALLOW MEDIUM REMOVAL'. */
#define	NO_PREVENT_ALLOW	0x8000

struct umass_softc {

	struct scsi_sense cam_scsi_sense;
	struct scsi_test_unit_ready cam_scsi_test_unit_ready;
	struct mtx sc_mtx;
	struct {
		uint8_t *data_ptr;
		union ccb *ccb;
		umass_callback_t *callback;

		uint32_t data_len;	/* bytes */
		uint32_t data_rem;	/* bytes */
		uint32_t data_timeout;	/* ms */
		uint32_t actlen;	/* bytes */

		uint8_t	cmd_data[UMASS_MAX_CMDLEN];
		uint8_t	cmd_len;	/* bytes */
		uint8_t	dir;
		uint8_t	lun;
	}	sc_transfer;

	/* Bulk specific variables for transfers in progress */
	umass_bbb_cbw_t cbw;		/* command block wrapper */
	umass_bbb_csw_t csw;		/* command status wrapper */

	/* CBI specific variables for transfers in progress */
	umass_cbi_sbl_t sbl;		/* status block */

	device_t sc_dev;
	struct usb_device *sc_udev;
	struct cam_sim *sc_sim;		/* SCSI Interface Module */
	struct usb_xfer *sc_xfer[UMASS_T_MAX];

	/*
	 * The command transform function is used to convert the SCSI
	 * commands into their derivatives, like UFI, ATAPI, and friends.
	 */
	umass_transform_t *sc_transform;

	uint32_t sc_unit;
	uint32_t sc_quirks;		/* they got it almost right */
	uint32_t sc_proto;		/* wire and cmd protocol */

	uint8_t	sc_name[16];
	uint8_t	sc_iface_no;		/* interface number */
	uint8_t	sc_maxlun;		/* maximum LUN number, inclusive */
	uint8_t	sc_last_xfer_index;
	uint8_t	sc_status_try;
};

struct umass_probe_proto {
	uint32_t quirks;
	uint32_t proto;

	int	error;
};

/* prototypes */

static device_probe_t umass_probe;
static device_attach_t umass_attach;
static device_detach_t umass_detach;

static usb_callback_t umass_tr_error;
static usb_callback_t umass_t_bbb_reset1_callback;
static usb_callback_t umass_t_bbb_reset2_callback;
static usb_callback_t umass_t_bbb_reset3_callback;
static usb_callback_t umass_t_bbb_command_callback;
static usb_callback_t umass_t_bbb_data_read_callback;
static usb_callback_t umass_t_bbb_data_rd_cs_callback;
static usb_callback_t umass_t_bbb_data_write_callback;
static usb_callback_t umass_t_bbb_data_wr_cs_callback;
static usb_callback_t umass_t_bbb_status_callback;
static usb_callback_t umass_t_cbi_reset1_callback;
static usb_callback_t umass_t_cbi_reset2_callback;
static usb_callback_t umass_t_cbi_reset3_callback;
static usb_callback_t umass_t_cbi_reset4_callback;
static usb_callback_t umass_t_cbi_command_callback;
static usb_callback_t umass_t_cbi_data_read_callback;
static usb_callback_t umass_t_cbi_data_rd_cs_callback;
static usb_callback_t umass_t_cbi_data_write_callback;
static usb_callback_t umass_t_cbi_data_wr_cs_callback;
static usb_callback_t umass_t_cbi_status_callback;

static void	umass_cancel_ccb(struct umass_softc *);
static void	umass_init_shuttle(struct umass_softc *);
static void	umass_reset(struct umass_softc *);
static void	umass_t_bbb_data_clear_stall_callback(struct usb_xfer *,
		    uint8_t, uint8_t, usb_error_t);
static void	umass_command_start(struct umass_softc *, uint8_t, void *,
		    uint32_t, uint32_t, umass_callback_t *, union ccb *);
static uint8_t	umass_bbb_get_max_lun(struct umass_softc *);
static void	umass_cbi_start_status(struct umass_softc *);
static void	umass_t_cbi_data_clear_stall_callback(struct usb_xfer *,
		    uint8_t, uint8_t, usb_error_t);
static int	umass_cam_attach_sim(struct umass_softc *);
static void	umass_cam_attach(struct umass_softc *);
static void	umass_cam_detach_sim(struct umass_softc *);
static void	umass_cam_action(struct cam_sim *, union ccb *);
static void	umass_cam_poll(struct cam_sim *);
static void	umass_cam_cb(struct umass_softc *, union ccb *, uint32_t,
		    uint8_t);
static void	umass_cam_sense_cb(struct umass_softc *, union ccb *, uint32_t,
		    uint8_t);
static void	umass_cam_quirk_cb(struct umass_softc *, union ccb *, uint32_t,
		    uint8_t);
static uint8_t	umass_scsi_transform(struct umass_softc *, uint8_t *, uint8_t);
static uint8_t	umass_rbc_transform(struct umass_softc *, uint8_t *, uint8_t);
static uint8_t	umass_ufi_transform(struct umass_softc *, uint8_t *, uint8_t);
static uint8_t	umass_atapi_transform(struct umass_softc *, uint8_t *,
		    uint8_t);
static uint8_t	umass_no_transform(struct umass_softc *, uint8_t *, uint8_t);
static uint8_t	umass_std_transform(struct umass_softc *, union ccb *, uint8_t
		    *, uint8_t);

#ifdef USB_DEBUG
static void	umass_bbb_dump_cbw(struct umass_softc *, umass_bbb_cbw_t *);
static void	umass_bbb_dump_csw(struct umass_softc *, umass_bbb_csw_t *);
static void	umass_cbi_dump_cmd(struct umass_softc *, void *, uint8_t);
static void	umass_dump_buffer(struct umass_softc *, uint8_t *, uint32_t,
		    uint32_t);
#endif

static struct usb_config umass_bbb_config[UMASS_T_BBB_MAX] = {

	[UMASS_T_BBB_RESET1] = {
		.type = UE_CONTROL,
		.endpoint = 0x00,	/* Control pipe */
		.direction = UE_DIR_ANY,
		.bufsize = sizeof(struct usb_device_request),
		.callback = &umass_t_bbb_reset1_callback,
		.timeout = 5000,	/* 5 seconds */
		.interval = 500,	/* 500 milliseconds */
	},

	[UMASS_T_BBB_RESET2] = {
		.type = UE_CONTROL,
		.endpoint = 0x00,	/* Control pipe */
		.direction = UE_DIR_ANY,
		.bufsize = sizeof(struct usb_device_request),
		.callback = &umass_t_bbb_reset2_callback,
		.timeout = 5000,	/* 5 seconds */
		.interval = 50,	/* 50 milliseconds */
	},

	[UMASS_T_BBB_RESET3] = {
		.type = UE_CONTROL,
		.endpoint = 0x00,	/* Control pipe */
		.direction = UE_DIR_ANY,
		.bufsize = sizeof(struct usb_device_request),
		.callback = &umass_t_bbb_reset3_callback,
		.timeout = 5000,	/* 5 seconds */
		.interval = 50,	/* 50 milliseconds */
	},

	[UMASS_T_BBB_COMMAND] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = sizeof(umass_bbb_cbw_t),
		.callback = &umass_t_bbb_command_callback,
		.timeout = 5000,	/* 5 seconds */
	},

	[UMASS_T_BBB_DATA_READ] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = UMASS_BULK_SIZE,
		.flags = {.proxy_buffer = 1,.short_xfer_ok = 1,.ext_buffer=1,},
		.callback = &umass_t_bbb_data_read_callback,
		.timeout = 0,	/* overwritten later */
	},

	[UMASS_T_BBB_DATA_RD_CS] = {
		.type = UE_CONTROL,
		.endpoint = 0x00,	/* Control pipe */
		.direction = UE_DIR_ANY,
		.bufsize = sizeof(struct usb_device_request),
		.callback = &umass_t_bbb_data_rd_cs_callback,
		.timeout = 5000,	/* 5 seconds */
	},

	[UMASS_T_BBB_DATA_WRITE] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = UMASS_BULK_SIZE,
		.flags = {.proxy_buffer = 1,.short_xfer_ok = 1,.ext_buffer=1,},
		.callback = &umass_t_bbb_data_write_callback,
		.timeout = 0,	/* overwritten later */
	},

	[UMASS_T_BBB_DATA_WR_CS] = {
		.type = UE_CONTROL,
		.endpoint = 0x00,	/* Control pipe */
		.direction = UE_DIR_ANY,
		.bufsize = sizeof(struct usb_device_request),
		.callback = &umass_t_bbb_data_wr_cs_callback,
		.timeout = 5000,	/* 5 seconds */
	},

	[UMASS_T_BBB_STATUS] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = sizeof(umass_bbb_csw_t),
		.flags = {.short_xfer_ok = 1,},
		.callback = &umass_t_bbb_status_callback,
		.timeout = 5000,	/* ms */
	},
};

static struct usb_config umass_cbi_config[UMASS_T_CBI_MAX] = {

	[UMASS_T_CBI_RESET1] = {
		.type = UE_CONTROL,
		.endpoint = 0x00,	/* Control pipe */
		.direction = UE_DIR_ANY,
		.bufsize = (sizeof(struct usb_device_request) +
		    UMASS_CBI_DIAGNOSTIC_CMDLEN),
		.callback = &umass_t_cbi_reset1_callback,
		.timeout = 5000,	/* 5 seconds */
		.interval = 500,	/* 500 milliseconds */
	},

	[UMASS_T_CBI_RESET2] = {
		.type = UE_CONTROL,
		.endpoint = 0x00,	/* Control pipe */
		.direction = UE_DIR_ANY,
		.bufsize = sizeof(struct usb_device_request),
		.callback = &umass_t_cbi_reset2_callback,
		.timeout = 5000,	/* 5 seconds */
		.interval = 50,	/* 50 milliseconds */
	},

	[UMASS_T_CBI_RESET3] = {
		.type = UE_CONTROL,
		.endpoint = 0x00,	/* Control pipe */
		.direction = UE_DIR_ANY,
		.bufsize = sizeof(struct usb_device_request),
		.callback = &umass_t_cbi_reset3_callback,
		.timeout = 5000,	/* 5 seconds */
		.interval = 50,	/* 50 milliseconds */
	},

	[UMASS_T_CBI_COMMAND] = {
		.type = UE_CONTROL,
		.endpoint = 0x00,	/* Control pipe */
		.direction = UE_DIR_ANY,
		.bufsize = (sizeof(struct usb_device_request) +
		    UMASS_MAX_CMDLEN),
		.callback = &umass_t_cbi_command_callback,
		.timeout = 5000,	/* 5 seconds */
	},

	[UMASS_T_CBI_DATA_READ] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = UMASS_BULK_SIZE,
		.flags = {.proxy_buffer = 1,.short_xfer_ok = 1,.ext_buffer=1,},
		.callback = &umass_t_cbi_data_read_callback,
		.timeout = 0,	/* overwritten later */
	},

	[UMASS_T_CBI_DATA_RD_CS] = {
		.type = UE_CONTROL,
		.endpoint = 0x00,	/* Control pipe */
		.direction = UE_DIR_ANY,
		.bufsize = sizeof(struct usb_device_request),
		.callback = &umass_t_cbi_data_rd_cs_callback,
		.timeout = 5000,	/* 5 seconds */
	},

	[UMASS_T_CBI_DATA_WRITE] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = UMASS_BULK_SIZE,
		.flags = {.proxy_buffer = 1,.short_xfer_ok = 1,.ext_buffer=1,},
		.callback = &umass_t_cbi_data_write_callback,
		.timeout = 0,	/* overwritten later */
	},

	[UMASS_T_CBI_DATA_WR_CS] = {
		.type = UE_CONTROL,
		.endpoint = 0x00,	/* Control pipe */
		.direction = UE_DIR_ANY,
		.bufsize = sizeof(struct usb_device_request),
		.callback = &umass_t_cbi_data_wr_cs_callback,
		.timeout = 5000,	/* 5 seconds */
	},

	[UMASS_T_CBI_STATUS] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {.short_xfer_ok = 1,.no_pipe_ok = 1,},
		.bufsize = sizeof(umass_cbi_sbl_t),
		.callback = &umass_t_cbi_status_callback,
		.timeout = 5000,	/* ms */
	},

	[UMASS_T_CBI_RESET4] = {
		.type = UE_CONTROL,
		.endpoint = 0x00,	/* Control pipe */
		.direction = UE_DIR_ANY,
		.bufsize = sizeof(struct usb_device_request),
		.callback = &umass_t_cbi_reset4_callback,
		.timeout = 5000,	/* ms */
	},
};

/* If device cannot return valid inquiry data, fake it */
static const uint8_t fake_inq_data[SHORT_INQUIRY_LENGTH] = {
	0, /* removable */ 0x80, SCSI_REV_2, SCSI_REV_2,
	 /* additional_length */ 31, 0, 0, 0
};

#define	UFI_COMMAND_LENGTH	12	/* UFI commands are always 12 bytes */
#define	ATAPI_COMMAND_LENGTH	12	/* ATAPI commands are always 12 bytes */

static devclass_t umass_devclass;

static device_method_t umass_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, umass_probe),
	DEVMETHOD(device_attach, umass_attach),
	DEVMETHOD(device_detach, umass_detach),

	DEVMETHOD_END
};

static driver_t umass_driver = {
	.name = "umass",
	.methods = umass_methods,
	.size = sizeof(struct umass_softc),
};

static const STRUCT_USB_HOST_ID __used umass_devs[] = {
	/* generic mass storage class */
	{USB_IFACE_CLASS(UICLASS_MASS),},
};

DRIVER_MODULE(umass, uhub, umass_driver, umass_devclass, NULL, 0);
MODULE_DEPEND(umass, usb, 1, 1, 1);
MODULE_DEPEND(umass, cam, 1, 1, 1);
MODULE_VERSION(umass, 1);
USB_PNP_HOST_INFO(umass_devs);

/*
 * USB device probe/attach/detach
 */

static uint16_t
umass_get_proto(struct usb_interface *iface)
{
	struct usb_interface_descriptor *id;
	uint16_t retval;

	retval = 0;

	/* Check for a standards compliant device */
	id = usbd_get_interface_descriptor(iface);
	if ((id == NULL) ||
	    (id->bInterfaceClass != UICLASS_MASS)) {
		goto done;
	}
	switch (id->bInterfaceSubClass) {
	case UISUBCLASS_SCSI:
		retval |= UMASS_PROTO_SCSI;
		break;
	case UISUBCLASS_UFI:
		retval |= UMASS_PROTO_UFI;
		break;
	case UISUBCLASS_RBC:
		retval |= UMASS_PROTO_RBC;
		break;
	case UISUBCLASS_SFF8020I:
	case UISUBCLASS_SFF8070I:
		retval |= UMASS_PROTO_ATAPI;
		break;
	default:
		goto done;
	}

	switch (id->bInterfaceProtocol) {
	case UIPROTO_MASS_CBI:
		retval |= UMASS_PROTO_CBI;
		break;
	case UIPROTO_MASS_CBI_I:
		retval |= UMASS_PROTO_CBI_I;
		break;
	case UIPROTO_MASS_BBB_OLD:
	case UIPROTO_MASS_BBB:
		retval |= UMASS_PROTO_BBB;
		break;
	default:
		goto done;
	}
done:
	return (retval);
}

/*
 * Match the device we are seeing with the devices supported.
 */
static struct umass_probe_proto
umass_probe_proto(device_t dev, struct usb_attach_arg *uaa)
{
	struct umass_probe_proto ret;
	uint32_t quirks = NO_QUIRKS;
	uint32_t proto = umass_get_proto(uaa->iface);

	memset(&ret, 0, sizeof(ret));
	ret.error = BUS_PROBE_GENERIC;

	/* Search for protocol enforcement */

	if (usb_test_quirk(uaa, UQ_MSC_FORCE_WIRE_BBB)) {
		proto &= ~UMASS_PROTO_WIRE;
		proto |= UMASS_PROTO_BBB;
	} else if (usb_test_quirk(uaa, UQ_MSC_FORCE_WIRE_CBI)) {
		proto &= ~UMASS_PROTO_WIRE;
		proto |= UMASS_PROTO_CBI;
	} else if (usb_test_quirk(uaa, UQ_MSC_FORCE_WIRE_CBI_I)) {
		proto &= ~UMASS_PROTO_WIRE;
		proto |= UMASS_PROTO_CBI_I;
	}

	if (usb_test_quirk(uaa, UQ_MSC_FORCE_PROTO_SCSI)) {
		proto &= ~UMASS_PROTO_COMMAND;
		proto |= UMASS_PROTO_SCSI;
	} else if (usb_test_quirk(uaa, UQ_MSC_FORCE_PROTO_ATAPI)) {
		proto &= ~UMASS_PROTO_COMMAND;
		proto |= UMASS_PROTO_ATAPI;
	} else if (usb_test_quirk(uaa, UQ_MSC_FORCE_PROTO_UFI)) {
		proto &= ~UMASS_PROTO_COMMAND;
		proto |= UMASS_PROTO_UFI;
	} else if (usb_test_quirk(uaa, UQ_MSC_FORCE_PROTO_RBC)) {
		proto &= ~UMASS_PROTO_COMMAND;
		proto |= UMASS_PROTO_RBC;
	}

	/* Check if the protocol is invalid */

	if ((proto & UMASS_PROTO_COMMAND) == 0) {
		ret.error = ENXIO;
		goto done;
	}

	if ((proto & UMASS_PROTO_WIRE) == 0) {
		ret.error = ENXIO;
		goto done;
	}

	/* Search for quirks */

	if (usb_test_quirk(uaa, UQ_MSC_NO_TEST_UNIT_READY))
		quirks |= NO_TEST_UNIT_READY;
	if (usb_test_quirk(uaa, UQ_MSC_NO_RS_CLEAR_UA))
		quirks |= RS_NO_CLEAR_UA;
	if (usb_test_quirk(uaa, UQ_MSC_NO_START_STOP))
		quirks |= NO_START_STOP;
	if (usb_test_quirk(uaa, UQ_MSC_NO_GETMAXLUN))
		quirks |= NO_GETMAXLUN;
	if (usb_test_quirk(uaa, UQ_MSC_NO_INQUIRY))
		quirks |= NO_INQUIRY;
	if (usb_test_quirk(uaa, UQ_MSC_NO_INQUIRY_EVPD))
		quirks |= NO_INQUIRY_EVPD;
	if (usb_test_quirk(uaa, UQ_MSC_NO_PREVENT_ALLOW))
		quirks |= NO_PREVENT_ALLOW;
	if (usb_test_quirk(uaa, UQ_MSC_NO_SYNC_CACHE))
		quirks |= NO_SYNCHRONIZE_CACHE;
	if (usb_test_quirk(uaa, UQ_MSC_SHUTTLE_INIT))
		quirks |= SHUTTLE_INIT;
	if (usb_test_quirk(uaa, UQ_MSC_ALT_IFACE_1))
		quirks |= ALT_IFACE_1;
	if (usb_test_quirk(uaa, UQ_MSC_FLOPPY_SPEED))
		quirks |= FLOPPY_SPEED;
	if (usb_test_quirk(uaa, UQ_MSC_IGNORE_RESIDUE))
		quirks |= IGNORE_RESIDUE;
	if (usb_test_quirk(uaa, UQ_MSC_WRONG_CSWSIG))
		quirks |= WRONG_CSWSIG;
	if (usb_test_quirk(uaa, UQ_MSC_RBC_PAD_TO_12))
		quirks |= RBC_PAD_TO_12;
	if (usb_test_quirk(uaa, UQ_MSC_READ_CAP_OFFBY1))
		quirks |= READ_CAPACITY_OFFBY1;
	if (usb_test_quirk(uaa, UQ_MSC_FORCE_SHORT_INQ))
		quirks |= FORCE_SHORT_INQUIRY;

done:
	ret.quirks = quirks;
	ret.proto = proto;
	return (ret);
}

static int
umass_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct umass_probe_proto temp;

	if (uaa->usb_mode != USB_MODE_HOST) {
		return (ENXIO);
	}
	temp = umass_probe_proto(dev, uaa);

	return (temp.error);
}

static int
umass_attach(device_t dev)
{
	struct umass_softc *sc = device_get_softc(dev);
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct umass_probe_proto temp = umass_probe_proto(dev, uaa);
	struct usb_interface_descriptor *id;
	int err;

	/*
	 * NOTE: the softc struct is cleared in device_set_driver.
	 * We can safely call umass_detach without specifically
	 * initializing the struct.
	 */

	sc->sc_dev = dev;
	sc->sc_udev = uaa->device;
	sc->sc_proto = temp.proto;
	sc->sc_quirks = temp.quirks;
	sc->sc_unit = device_get_unit(dev);

	snprintf(sc->sc_name, sizeof(sc->sc_name),
	    "%s", device_get_nameunit(dev));

	device_set_usb_desc(dev);

        mtx_init(&sc->sc_mtx, device_get_nameunit(dev), 
	    NULL, MTX_DEF | MTX_RECURSE);

	/* get interface index */

	id = usbd_get_interface_descriptor(uaa->iface);
	if (id == NULL) {
		device_printf(dev, "failed to get "
		    "interface number\n");
		goto detach;
	}
	sc->sc_iface_no = id->bInterfaceNumber;

#ifdef USB_DEBUG
	device_printf(dev, " ");

	switch (sc->sc_proto & UMASS_PROTO_COMMAND) {
	case UMASS_PROTO_SCSI:
		printf("SCSI");
		break;
	case UMASS_PROTO_ATAPI:
		printf("8070i (ATAPI)");
		break;
	case UMASS_PROTO_UFI:
		printf("UFI");
		break;
	case UMASS_PROTO_RBC:
		printf("RBC");
		break;
	default:
		printf("(unknown 0x%02x)",
		    sc->sc_proto & UMASS_PROTO_COMMAND);
		break;
	}

	printf(" over ");

	switch (sc->sc_proto & UMASS_PROTO_WIRE) {
	case UMASS_PROTO_BBB:
		printf("Bulk-Only");
		break;
	case UMASS_PROTO_CBI:		/* uses Comand/Bulk pipes */
		printf("CBI");
		break;
	case UMASS_PROTO_CBI_I:	/* uses Comand/Bulk/Interrupt pipes */
		printf("CBI with CCI");
		break;
	default:
		printf("(unknown 0x%02x)",
		    sc->sc_proto & UMASS_PROTO_WIRE);
	}

	printf("; quirks = 0x%04x\n", sc->sc_quirks);
#endif

	if (sc->sc_quirks & ALT_IFACE_1) {
		err = usbd_set_alt_interface_index
		    (uaa->device, uaa->info.bIfaceIndex, 1);

		if (err) {
			DPRINTF(sc, UDMASS_USB, "could not switch to "
			    "Alt Interface 1\n");
			goto detach;
		}
	}
	/* allocate all required USB transfers */

	if (sc->sc_proto & UMASS_PROTO_BBB) {

		err = usbd_transfer_setup(uaa->device,
		    &uaa->info.bIfaceIndex, sc->sc_xfer, umass_bbb_config,
		    UMASS_T_BBB_MAX, sc, &sc->sc_mtx);

		/* skip reset first time */
		sc->sc_last_xfer_index = UMASS_T_BBB_COMMAND;

	} else if (sc->sc_proto & (UMASS_PROTO_CBI | UMASS_PROTO_CBI_I)) {

		err = usbd_transfer_setup(uaa->device,
		    &uaa->info.bIfaceIndex, sc->sc_xfer, umass_cbi_config,
		    UMASS_T_CBI_MAX, sc, &sc->sc_mtx);

		/* skip reset first time */
		sc->sc_last_xfer_index = UMASS_T_CBI_COMMAND;

	} else {
		err = USB_ERR_INVAL;
	}

	if (err) {
		device_printf(dev, "could not setup required "
		    "transfers, %s\n", usbd_errstr(err));
		goto detach;
	}
#ifdef USB_DEBUG
	if (umass_throttle > 0) {
		uint8_t x;
		int iv;

		iv = umass_throttle;

		if (iv < 1)
			iv = 1;
		else if (iv > 8000)
			iv = 8000;

		for (x = 0; x != UMASS_T_MAX; x++) {
			if (sc->sc_xfer[x] != NULL)
				usbd_xfer_set_interval(sc->sc_xfer[x], iv);
		}
	}
#endif
	sc->sc_transform =
	    (sc->sc_proto & UMASS_PROTO_SCSI) ? &umass_scsi_transform :
	    (sc->sc_proto & UMASS_PROTO_UFI) ? &umass_ufi_transform :
	    (sc->sc_proto & UMASS_PROTO_ATAPI) ? &umass_atapi_transform :
	    (sc->sc_proto & UMASS_PROTO_RBC) ? &umass_rbc_transform :
	    &umass_no_transform;

	/* from here onwards the device can be used. */

	if (sc->sc_quirks & SHUTTLE_INIT) {
		umass_init_shuttle(sc);
	}
	/* get the maximum LUN supported by the device */

	if (((sc->sc_proto & UMASS_PROTO_WIRE) == UMASS_PROTO_BBB) &&
	    !(sc->sc_quirks & NO_GETMAXLUN))
		sc->sc_maxlun = umass_bbb_get_max_lun(sc);
	else
		sc->sc_maxlun = 0;

	/* Prepare the SCSI command block */
	sc->cam_scsi_sense.opcode = REQUEST_SENSE;
	sc->cam_scsi_test_unit_ready.opcode = TEST_UNIT_READY;

	/* register the SIM */
	err = umass_cam_attach_sim(sc);
	if (err) {
		goto detach;
	}
	/* scan the SIM */
	umass_cam_attach(sc);

	DPRINTF(sc, UDMASS_GEN, "Attach finished\n");

	return (0);			/* success */

detach:
	umass_detach(dev);
	return (ENXIO);			/* failure */
}

static int
umass_detach(device_t dev)
{
	struct umass_softc *sc = device_get_softc(dev);

	DPRINTF(sc, UDMASS_USB, "\n");

	/* teardown our statemachine */

	usbd_transfer_unsetup(sc->sc_xfer, UMASS_T_MAX);

	mtx_lock(&sc->sc_mtx);

	/* cancel any leftover CCB's */

	umass_cancel_ccb(sc);

	umass_cam_detach_sim(sc);

	mtx_unlock(&sc->sc_mtx);

	mtx_destroy(&sc->sc_mtx);

	return (0);			/* success */
}

static void
umass_init_shuttle(struct umass_softc *sc)
{
	struct usb_device_request req;
	uint8_t status[2] = {0, 0};

	/*
	 * The Linux driver does this, but no one can tell us what the
	 * command does.
	 */
	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = 1;		/* XXX unknown command */
	USETW(req.wValue, 0);
	req.wIndex[0] = sc->sc_iface_no;
	req.wIndex[1] = 0;
	USETW(req.wLength, sizeof(status));
	usbd_do_request(sc->sc_udev, NULL, &req, &status);

	DPRINTF(sc, UDMASS_GEN, "Shuttle init returned 0x%02x%02x\n",
	    status[0], status[1]);
}

/*
 * Generic functions to handle transfers
 */

static void
umass_transfer_start(struct umass_softc *sc, uint8_t xfer_index)
{
	DPRINTF(sc, UDMASS_GEN, "transfer index = "
	    "%d\n", xfer_index);

	if (sc->sc_xfer[xfer_index]) {
		sc->sc_last_xfer_index = xfer_index;
		usbd_transfer_start(sc->sc_xfer[xfer_index]);
	} else {
		umass_cancel_ccb(sc);
	}
}

static void
umass_reset(struct umass_softc *sc)
{
	DPRINTF(sc, UDMASS_GEN, "resetting device\n");

	/*
	 * stop the last transfer, if not already stopped:
	 */
	usbd_transfer_stop(sc->sc_xfer[sc->sc_last_xfer_index]);
	umass_transfer_start(sc, 0);
}

static void
umass_cancel_ccb(struct umass_softc *sc)
{
	union ccb *ccb;

	USB_MTX_ASSERT(&sc->sc_mtx, MA_OWNED);

	ccb = sc->sc_transfer.ccb;
	sc->sc_transfer.ccb = NULL;
	sc->sc_last_xfer_index = 0;

	if (ccb) {
		(sc->sc_transfer.callback)
		    (sc, ccb, (sc->sc_transfer.data_len -
		    sc->sc_transfer.actlen), STATUS_WIRE_FAILED);
	}
}

static void
umass_tr_error(struct usb_xfer *xfer, usb_error_t error)
{
	struct umass_softc *sc = usbd_xfer_softc(xfer);

	if (error != USB_ERR_CANCELLED) {

		DPRINTF(sc, UDMASS_GEN, "transfer error, %s -> "
		    "reset\n", usbd_errstr(error));
	}
	umass_cancel_ccb(sc);
}

/*
 * BBB protocol specific functions
 */

static void
umass_t_bbb_reset1_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct umass_softc *sc = usbd_xfer_softc(xfer);
	struct usb_device_request req;
	struct usb_page_cache *pc;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		umass_transfer_start(sc, UMASS_T_BBB_RESET2);
		return;

	case USB_ST_SETUP:
		/*
		 * Reset recovery (5.3.4 in Universal Serial Bus Mass Storage Class)
		 *
		 * For Reset Recovery the host shall issue in the following order:
		 * a) a Bulk-Only Mass Storage Reset
		 * b) a Clear Feature HALT to the Bulk-In endpoint
		 * c) a Clear Feature HALT to the Bulk-Out endpoint
		 *
		 * This is done in 3 steps, using 3 transfers:
		 * UMASS_T_BBB_RESET1
		 * UMASS_T_BBB_RESET2
		 * UMASS_T_BBB_RESET3
		 */

		DPRINTF(sc, UDMASS_BBB, "BBB reset!\n");

		req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
		req.bRequest = UR_BBB_RESET;	/* bulk only reset */
		USETW(req.wValue, 0);
		req.wIndex[0] = sc->sc_iface_no;
		req.wIndex[1] = 0;
		USETW(req.wLength, 0);

		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_in(pc, 0, &req, sizeof(req));

		usbd_xfer_set_frame_len(xfer, 0, sizeof(req));
		usbd_xfer_set_frames(xfer, 1);
		usbd_transfer_submit(xfer);
		return;

	default:			/* Error */
		umass_tr_error(xfer, error);
		return;
	}
}

static void
umass_t_bbb_reset2_callback(struct usb_xfer *xfer, usb_error_t error)
{
	umass_t_bbb_data_clear_stall_callback(xfer, UMASS_T_BBB_RESET3,
	    UMASS_T_BBB_DATA_READ, error);
}

static void
umass_t_bbb_reset3_callback(struct usb_xfer *xfer, usb_error_t error)
{
	umass_t_bbb_data_clear_stall_callback(xfer, UMASS_T_BBB_COMMAND,
	    UMASS_T_BBB_DATA_WRITE, error);
}

static void
umass_t_bbb_data_clear_stall_callback(struct usb_xfer *xfer,
    uint8_t next_xfer, uint8_t stall_xfer, usb_error_t error)
{
	struct umass_softc *sc = usbd_xfer_softc(xfer);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
tr_transferred:
		umass_transfer_start(sc, next_xfer);
		return;

	case USB_ST_SETUP:
		if (usbd_clear_stall_callback(xfer, sc->sc_xfer[stall_xfer])) {
			goto tr_transferred;
		}
		return;

	default:			/* Error */
		umass_tr_error(xfer, error);
		return;
	}
}

static void
umass_t_bbb_command_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct umass_softc *sc = usbd_xfer_softc(xfer);
	union ccb *ccb = sc->sc_transfer.ccb;
	struct usb_page_cache *pc;
	uint32_t tag;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		umass_transfer_start
		    (sc, ((sc->sc_transfer.dir == DIR_IN) ? UMASS_T_BBB_DATA_READ :
		    (sc->sc_transfer.dir == DIR_OUT) ? UMASS_T_BBB_DATA_WRITE :
		    UMASS_T_BBB_STATUS));
		return;

	case USB_ST_SETUP:

		sc->sc_status_try = 0;

		if (ccb) {

			/*
		         * the initial value is not important,
		         * as long as the values are unique:
		         */
			tag = UGETDW(sc->cbw.dCBWTag) + 1;

			USETDW(sc->cbw.dCBWSignature, CBWSIGNATURE);
			USETDW(sc->cbw.dCBWTag, tag);

			/*
		         * dCBWDataTransferLength:
		         *   This field indicates the number of bytes of data that the host
		         *   intends to transfer on the IN or OUT Bulk endpoint(as indicated by
		         *   the Direction bit) during the execution of this command. If this
		         *   field is set to 0, the device will expect that no data will be
		         *   transferred IN or OUT during this command, regardless of the value
		         *   of the Direction bit defined in dCBWFlags.
		         */
			USETDW(sc->cbw.dCBWDataTransferLength, sc->sc_transfer.data_len);

			/*
		         * dCBWFlags:
		         *   The bits of the Flags field are defined as follows:
		         *     Bits 0-6  reserved
		         *     Bit  7    Direction - this bit shall be ignored if the
		         *                           dCBWDataTransferLength field is zero.
		         *               0 = data Out from host to device
		         *               1 = data In from device to host
		         */
			sc->cbw.bCBWFlags = ((sc->sc_transfer.dir == DIR_IN) ?
			    CBWFLAGS_IN : CBWFLAGS_OUT);
			sc->cbw.bCBWLUN = sc->sc_transfer.lun;

			if (sc->sc_transfer.cmd_len > sizeof(sc->cbw.CBWCDB)) {
				sc->sc_transfer.cmd_len = sizeof(sc->cbw.CBWCDB);
				DPRINTF(sc, UDMASS_BBB, "Truncating long command!\n");
			}
			sc->cbw.bCDBLength = sc->sc_transfer.cmd_len;

			/* copy SCSI command data */
			memcpy(sc->cbw.CBWCDB, sc->sc_transfer.cmd_data,
			    sc->sc_transfer.cmd_len);

			/* clear remaining command area */
			memset(sc->cbw.CBWCDB +
			    sc->sc_transfer.cmd_len, 0,
			    sizeof(sc->cbw.CBWCDB) -
			    sc->sc_transfer.cmd_len);

			DIF(UDMASS_BBB, umass_bbb_dump_cbw(sc, &sc->cbw));

			pc = usbd_xfer_get_frame(xfer, 0);
			usbd_copy_in(pc, 0, &sc->cbw, sizeof(sc->cbw));
			usbd_xfer_set_frame_len(xfer, 0, sizeof(sc->cbw));

			usbd_transfer_submit(xfer);
		}
		return;

	default:			/* Error */
		umass_tr_error(xfer, error);
		return;
	}
}

static void
umass_t_bbb_data_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct umass_softc *sc = usbd_xfer_softc(xfer);
	uint32_t max_bulk = usbd_xfer_max_len(xfer);
	int actlen, sumlen;

	usbd_xfer_status(xfer, &actlen, &sumlen, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		sc->sc_transfer.data_rem -= actlen;
		sc->sc_transfer.data_ptr += actlen;
		sc->sc_transfer.actlen += actlen;

		if (actlen < sumlen) {
			/* short transfer */
			sc->sc_transfer.data_rem = 0;
		}
	case USB_ST_SETUP:
		DPRINTF(sc, UDMASS_BBB, "max_bulk=%d, data_rem=%d\n",
		    max_bulk, sc->sc_transfer.data_rem);

		if (sc->sc_transfer.data_rem == 0) {
			umass_transfer_start(sc, UMASS_T_BBB_STATUS);
			return;
		}
		if (max_bulk > sc->sc_transfer.data_rem) {
			max_bulk = sc->sc_transfer.data_rem;
		}
		usbd_xfer_set_timeout(xfer, sc->sc_transfer.data_timeout);

		usbd_xfer_set_frame_data(xfer, 0, sc->sc_transfer.data_ptr,
		    max_bulk);

		usbd_transfer_submit(xfer);
		return;

	default:			/* Error */
		if (error == USB_ERR_CANCELLED) {
			umass_tr_error(xfer, error);
		} else {
			umass_transfer_start(sc, UMASS_T_BBB_DATA_RD_CS);
		}
		return;
	}
}

static void
umass_t_bbb_data_rd_cs_callback(struct usb_xfer *xfer, usb_error_t error)
{
	umass_t_bbb_data_clear_stall_callback(xfer, UMASS_T_BBB_STATUS,
	    UMASS_T_BBB_DATA_READ, error);
}

static void
umass_t_bbb_data_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct umass_softc *sc = usbd_xfer_softc(xfer);
	uint32_t max_bulk = usbd_xfer_max_len(xfer);
	int actlen, sumlen;

	usbd_xfer_status(xfer, &actlen, &sumlen, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		sc->sc_transfer.data_rem -= actlen;
		sc->sc_transfer.data_ptr += actlen;
		sc->sc_transfer.actlen += actlen;

		if (actlen < sumlen) {
			/* short transfer */
			sc->sc_transfer.data_rem = 0;
		}
	case USB_ST_SETUP:
		DPRINTF(sc, UDMASS_BBB, "max_bulk=%d, data_rem=%d\n",
		    max_bulk, sc->sc_transfer.data_rem);

		if (sc->sc_transfer.data_rem == 0) {
			umass_transfer_start(sc, UMASS_T_BBB_STATUS);
			return;
		}
		if (max_bulk > sc->sc_transfer.data_rem) {
			max_bulk = sc->sc_transfer.data_rem;
		}
		usbd_xfer_set_timeout(xfer, sc->sc_transfer.data_timeout);

		usbd_xfer_set_frame_data(xfer, 0, sc->sc_transfer.data_ptr,
		    max_bulk);

		usbd_transfer_submit(xfer);
		return;

	default:			/* Error */
		if (error == USB_ERR_CANCELLED) {
			umass_tr_error(xfer, error);
		} else {
			umass_transfer_start(sc, UMASS_T_BBB_DATA_WR_CS);
		}
		return;
	}
}

static void
umass_t_bbb_data_wr_cs_callback(struct usb_xfer *xfer, usb_error_t error)
{
	umass_t_bbb_data_clear_stall_callback(xfer, UMASS_T_BBB_STATUS,
	    UMASS_T_BBB_DATA_WRITE, error);
}

static void
umass_t_bbb_status_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct umass_softc *sc = usbd_xfer_softc(xfer);
	union ccb *ccb = sc->sc_transfer.ccb;
	struct usb_page_cache *pc;
	uint32_t residue;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		/*
		 * Do a full reset if there is something wrong with the CSW:
		 */
		sc->sc_status_try = 1;

		/* Zero missing parts of the CSW: */

		if (actlen < (int)sizeof(sc->csw))
			memset(&sc->csw, 0, sizeof(sc->csw));

		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, &sc->csw, actlen);

		DIF(UDMASS_BBB, umass_bbb_dump_csw(sc, &sc->csw));

		residue = UGETDW(sc->csw.dCSWDataResidue);

		if ((!residue) || (sc->sc_quirks & IGNORE_RESIDUE)) {
			residue = (sc->sc_transfer.data_len -
			    sc->sc_transfer.actlen);
		}
		if (residue > sc->sc_transfer.data_len) {
			DPRINTF(sc, UDMASS_BBB, "truncating residue from %d "
			    "to %d bytes\n", residue, sc->sc_transfer.data_len);
			residue = sc->sc_transfer.data_len;
		}
		/* translate weird command-status signatures: */
		if (sc->sc_quirks & WRONG_CSWSIG) {

			uint32_t temp = UGETDW(sc->csw.dCSWSignature);

			if ((temp == CSWSIGNATURE_OLYMPUS_C1) ||
			    (temp == CSWSIGNATURE_IMAGINATION_DBX1)) {
				USETDW(sc->csw.dCSWSignature, CSWSIGNATURE);
			}
		}
		/* check CSW and handle eventual error */
		if (UGETDW(sc->csw.dCSWSignature) != CSWSIGNATURE) {
			DPRINTF(sc, UDMASS_BBB, "bad CSW signature 0x%08x != 0x%08x\n",
			    UGETDW(sc->csw.dCSWSignature), CSWSIGNATURE);
			/*
			 * Invalid CSW: Wrong signature or wrong tag might
			 * indicate that we lost synchronization. Reset the
			 * device.
			 */
			goto tr_error;
		} else if (UGETDW(sc->csw.dCSWTag) != UGETDW(sc->cbw.dCBWTag)) {
			DPRINTF(sc, UDMASS_BBB, "Invalid CSW: tag 0x%08x should be "
			    "0x%08x\n", UGETDW(sc->csw.dCSWTag),
			    UGETDW(sc->cbw.dCBWTag));
			goto tr_error;
		} else if (sc->csw.bCSWStatus > CSWSTATUS_PHASE) {
			DPRINTF(sc, UDMASS_BBB, "Invalid CSW: status %d > %d\n",
			    sc->csw.bCSWStatus, CSWSTATUS_PHASE);
			goto tr_error;
		} else if (sc->csw.bCSWStatus == CSWSTATUS_PHASE) {
			DPRINTF(sc, UDMASS_BBB, "Phase error, residue = "
			    "%d\n", residue);
			goto tr_error;
		} else if (sc->sc_transfer.actlen > sc->sc_transfer.data_len) {
			DPRINTF(sc, UDMASS_BBB, "Buffer overrun %d > %d\n",
			    sc->sc_transfer.actlen, sc->sc_transfer.data_len);
			goto tr_error;
		} else if (sc->csw.bCSWStatus == CSWSTATUS_FAILED) {
			DPRINTF(sc, UDMASS_BBB, "Command failed, residue = "
			    "%d\n", residue);

			sc->sc_transfer.ccb = NULL;

			sc->sc_last_xfer_index = UMASS_T_BBB_COMMAND;

			(sc->sc_transfer.callback)
			    (sc, ccb, residue, STATUS_CMD_FAILED);
		} else {
			sc->sc_transfer.ccb = NULL;

			sc->sc_last_xfer_index = UMASS_T_BBB_COMMAND;

			(sc->sc_transfer.callback)
			    (sc, ccb, residue, STATUS_CMD_OK);
		}
		return;

	case USB_ST_SETUP:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		return;

	default:
tr_error:
		DPRINTF(sc, UDMASS_BBB, "Failed to read CSW: %s, try %d\n",
		    usbd_errstr(error), sc->sc_status_try);

		if ((error == USB_ERR_CANCELLED) ||
		    (sc->sc_status_try)) {
			umass_tr_error(xfer, error);
		} else {
			sc->sc_status_try = 1;
			umass_transfer_start(sc, UMASS_T_BBB_DATA_RD_CS);
		}
		return;
	}
}

static void
umass_command_start(struct umass_softc *sc, uint8_t dir,
    void *data_ptr, uint32_t data_len,
    uint32_t data_timeout, umass_callback_t *callback,
    union ccb *ccb)
{
	sc->sc_transfer.lun = ccb->ccb_h.target_lun;

	/*
	 * NOTE: assumes that "sc->sc_transfer.cmd_data" and
	 * "sc->sc_transfer.cmd_len" has been properly
	 * initialized.
	 */

	sc->sc_transfer.dir = data_len ? dir : DIR_NONE;
	sc->sc_transfer.data_ptr = data_ptr;
	sc->sc_transfer.data_len = data_len;
	sc->sc_transfer.data_rem = data_len;
	sc->sc_transfer.data_timeout = (data_timeout + UMASS_TIMEOUT);

	sc->sc_transfer.actlen = 0;
	sc->sc_transfer.callback = callback;
	sc->sc_transfer.ccb = ccb;

	if (sc->sc_xfer[sc->sc_last_xfer_index]) {
		usbd_transfer_start(sc->sc_xfer[sc->sc_last_xfer_index]);
	} else {
		umass_cancel_ccb(sc);
	}
}

static uint8_t
umass_bbb_get_max_lun(struct umass_softc *sc)
{
	struct usb_device_request req;
	usb_error_t err;
	uint8_t buf = 0;

	/* The Get Max Lun command is a class-specific request. */
	req.bmRequestType = UT_READ_CLASS_INTERFACE;
	req.bRequest = UR_BBB_GET_MAX_LUN;
	USETW(req.wValue, 0);
	req.wIndex[0] = sc->sc_iface_no;
	req.wIndex[1] = 0;
	USETW(req.wLength, 1);

	err = usbd_do_request(sc->sc_udev, NULL, &req, &buf);
	if (err) {
		buf = 0;

		/* Device doesn't support Get Max Lun request. */
		printf("%s: Get Max Lun not supported (%s)\n",
		    sc->sc_name, usbd_errstr(err));
	}
	return (buf);
}

/*
 * Command/Bulk/Interrupt (CBI) specific functions
 */

static void
umass_cbi_start_status(struct umass_softc *sc)
{
	if (sc->sc_xfer[UMASS_T_CBI_STATUS]) {
		umass_transfer_start(sc, UMASS_T_CBI_STATUS);
	} else {
		union ccb *ccb = sc->sc_transfer.ccb;

		sc->sc_transfer.ccb = NULL;

		sc->sc_last_xfer_index = UMASS_T_CBI_COMMAND;

		(sc->sc_transfer.callback)
		    (sc, ccb, (sc->sc_transfer.data_len -
		    sc->sc_transfer.actlen), STATUS_CMD_UNKNOWN);
	}
}

static void
umass_t_cbi_reset1_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct umass_softc *sc = usbd_xfer_softc(xfer);
	struct usb_device_request req;
	struct usb_page_cache *pc;
	uint8_t buf[UMASS_CBI_DIAGNOSTIC_CMDLEN];

	uint8_t i;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		umass_transfer_start(sc, UMASS_T_CBI_RESET2);
		break;

	case USB_ST_SETUP:
		/*
		 * Command Block Reset Protocol
		 *
		 * First send a reset request to the device. Then clear
		 * any possibly stalled bulk endpoints.
		 *
		 * This is done in 3 steps, using 3 transfers:
		 * UMASS_T_CBI_RESET1
		 * UMASS_T_CBI_RESET2
		 * UMASS_T_CBI_RESET3
		 * UMASS_T_CBI_RESET4 (only if there is an interrupt endpoint)
		 */

		DPRINTF(sc, UDMASS_CBI, "CBI reset!\n");

		req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
		req.bRequest = UR_CBI_ADSC;
		USETW(req.wValue, 0);
		req.wIndex[0] = sc->sc_iface_no;
		req.wIndex[1] = 0;
		USETW(req.wLength, UMASS_CBI_DIAGNOSTIC_CMDLEN);

		/*
		 * The 0x1d code is the SEND DIAGNOSTIC command. To
		 * distinguish between the two, the last 10 bytes of the CBL
		 * is filled with 0xff (section 2.2 of the CBI
		 * specification)
		 */
		buf[0] = 0x1d;		/* Command Block Reset */
		buf[1] = 0x04;

		for (i = 2; i < UMASS_CBI_DIAGNOSTIC_CMDLEN; i++) {
			buf[i] = 0xff;
		}

		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_in(pc, 0, &req, sizeof(req));
		pc = usbd_xfer_get_frame(xfer, 1);
		usbd_copy_in(pc, 0, buf, sizeof(buf));

		usbd_xfer_set_frame_len(xfer, 0, sizeof(req));
		usbd_xfer_set_frame_len(xfer, 1, sizeof(buf));
		usbd_xfer_set_frames(xfer, 2);
		usbd_transfer_submit(xfer);
		break;

	default:			/* Error */
		if (error == USB_ERR_CANCELLED)
			umass_tr_error(xfer, error);
		else
			umass_transfer_start(sc, UMASS_T_CBI_RESET2);
		break;
	}
}

static void
umass_t_cbi_reset2_callback(struct usb_xfer *xfer, usb_error_t error)
{
	umass_t_cbi_data_clear_stall_callback(xfer, UMASS_T_CBI_RESET3,
	    UMASS_T_CBI_DATA_READ, error);
}

static void
umass_t_cbi_reset3_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct umass_softc *sc = usbd_xfer_softc(xfer);

	umass_t_cbi_data_clear_stall_callback
	    (xfer, (sc->sc_xfer[UMASS_T_CBI_RESET4] &&
	    sc->sc_xfer[UMASS_T_CBI_STATUS]) ?
	    UMASS_T_CBI_RESET4 : UMASS_T_CBI_COMMAND,
	    UMASS_T_CBI_DATA_WRITE, error);
}

static void
umass_t_cbi_reset4_callback(struct usb_xfer *xfer, usb_error_t error)
{
	umass_t_cbi_data_clear_stall_callback(xfer, UMASS_T_CBI_COMMAND,
	    UMASS_T_CBI_STATUS, error);
}

static void
umass_t_cbi_data_clear_stall_callback(struct usb_xfer *xfer,
    uint8_t next_xfer, uint8_t stall_xfer, usb_error_t error)
{
	struct umass_softc *sc = usbd_xfer_softc(xfer);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
tr_transferred:
		if (next_xfer == UMASS_T_CBI_STATUS) {
			umass_cbi_start_status(sc);
		} else {
			umass_transfer_start(sc, next_xfer);
		}
		break;

	case USB_ST_SETUP:
		if (usbd_clear_stall_callback(xfer, sc->sc_xfer[stall_xfer])) {
			goto tr_transferred;	/* should not happen */
		}
		break;

	default:			/* Error */
		umass_tr_error(xfer, error);
		break;
	}
}

static void
umass_t_cbi_command_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct umass_softc *sc = usbd_xfer_softc(xfer);
	union ccb *ccb = sc->sc_transfer.ccb;
	struct usb_device_request req;
	struct usb_page_cache *pc;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		if (sc->sc_transfer.dir == DIR_NONE) {
			umass_cbi_start_status(sc);
		} else {
			umass_transfer_start
			    (sc, (sc->sc_transfer.dir == DIR_IN) ?
			    UMASS_T_CBI_DATA_READ : UMASS_T_CBI_DATA_WRITE);
		}
		break;

	case USB_ST_SETUP:

		if (ccb) {

			/*
		         * do a CBI transfer with cmd_len bytes from
		         * cmd_data, possibly a data phase of data_len
		         * bytes from/to the device and finally a status
		         * read phase.
		         */

			req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
			req.bRequest = UR_CBI_ADSC;
			USETW(req.wValue, 0);
			req.wIndex[0] = sc->sc_iface_no;
			req.wIndex[1] = 0;
			req.wLength[0] = sc->sc_transfer.cmd_len;
			req.wLength[1] = 0;

			pc = usbd_xfer_get_frame(xfer, 0);
			usbd_copy_in(pc, 0, &req, sizeof(req));
			pc = usbd_xfer_get_frame(xfer, 1);
			usbd_copy_in(pc, 0, sc->sc_transfer.cmd_data,
			    sc->sc_transfer.cmd_len);

			usbd_xfer_set_frame_len(xfer, 0, sizeof(req));
			usbd_xfer_set_frame_len(xfer, 1, sc->sc_transfer.cmd_len);
			usbd_xfer_set_frames(xfer,
			    sc->sc_transfer.cmd_len ? 2 : 1);

			DIF(UDMASS_CBI,
			    umass_cbi_dump_cmd(sc,
			    sc->sc_transfer.cmd_data,
			    sc->sc_transfer.cmd_len));

			usbd_transfer_submit(xfer);
		}
		break;

	default:			/* Error */
		/*
		 * STALL on the control pipe can be result of the command error.
		 * Attempt to clear this STALL same as for bulk pipe also
		 * results in command completion interrupt, but ASC/ASCQ there
		 * look like not always valid, so don't bother about it.
		 */
		if ((error == USB_ERR_STALLED) ||
		    (sc->sc_transfer.callback == &umass_cam_cb)) {
			sc->sc_transfer.ccb = NULL;
			(sc->sc_transfer.callback)
			    (sc, ccb, sc->sc_transfer.data_len,
			    STATUS_CMD_UNKNOWN);
		} else {
			umass_tr_error(xfer, error);
			/* skip reset */
			sc->sc_last_xfer_index = UMASS_T_CBI_COMMAND;
		}
		break;
	}
}

static void
umass_t_cbi_data_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct umass_softc *sc = usbd_xfer_softc(xfer);
	uint32_t max_bulk = usbd_xfer_max_len(xfer);
	int actlen, sumlen;

	usbd_xfer_status(xfer, &actlen, &sumlen, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		sc->sc_transfer.data_rem -= actlen;
		sc->sc_transfer.data_ptr += actlen;
		sc->sc_transfer.actlen += actlen;

		if (actlen < sumlen) {
			/* short transfer */
			sc->sc_transfer.data_rem = 0;
		}
	case USB_ST_SETUP:
		DPRINTF(sc, UDMASS_CBI, "max_bulk=%d, data_rem=%d\n",
		    max_bulk, sc->sc_transfer.data_rem);

		if (sc->sc_transfer.data_rem == 0) {
			umass_cbi_start_status(sc);
			break;
		}
		if (max_bulk > sc->sc_transfer.data_rem) {
			max_bulk = sc->sc_transfer.data_rem;
		}
		usbd_xfer_set_timeout(xfer, sc->sc_transfer.data_timeout);

		usbd_xfer_set_frame_data(xfer, 0, sc->sc_transfer.data_ptr,
		    max_bulk);

		usbd_transfer_submit(xfer);
		break;

	default:			/* Error */
		if ((error == USB_ERR_CANCELLED) ||
		    (sc->sc_transfer.callback != &umass_cam_cb)) {
			umass_tr_error(xfer, error);
		} else {
			umass_transfer_start(sc, UMASS_T_CBI_DATA_RD_CS);
		}
		break;
	}
}

static void
umass_t_cbi_data_rd_cs_callback(struct usb_xfer *xfer, usb_error_t error)
{
	umass_t_cbi_data_clear_stall_callback(xfer, UMASS_T_CBI_STATUS,
	    UMASS_T_CBI_DATA_READ, error);
}

static void
umass_t_cbi_data_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct umass_softc *sc = usbd_xfer_softc(xfer);
	uint32_t max_bulk = usbd_xfer_max_len(xfer);
	int actlen, sumlen;

	usbd_xfer_status(xfer, &actlen, &sumlen, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		sc->sc_transfer.data_rem -= actlen;
		sc->sc_transfer.data_ptr += actlen;
		sc->sc_transfer.actlen += actlen;

		if (actlen < sumlen) {
			/* short transfer */
			sc->sc_transfer.data_rem = 0;
		}
	case USB_ST_SETUP:
		DPRINTF(sc, UDMASS_CBI, "max_bulk=%d, data_rem=%d\n",
		    max_bulk, sc->sc_transfer.data_rem);

		if (sc->sc_transfer.data_rem == 0) {
			umass_cbi_start_status(sc);
			break;
		}
		if (max_bulk > sc->sc_transfer.data_rem) {
			max_bulk = sc->sc_transfer.data_rem;
		}
		usbd_xfer_set_timeout(xfer, sc->sc_transfer.data_timeout);

		usbd_xfer_set_frame_data(xfer, 0, sc->sc_transfer.data_ptr,
		    max_bulk);

		usbd_transfer_submit(xfer);
		break;

	default:			/* Error */
		if ((error == USB_ERR_CANCELLED) ||
		    (sc->sc_transfer.callback != &umass_cam_cb)) {
			umass_tr_error(xfer, error);
		} else {
			umass_transfer_start(sc, UMASS_T_CBI_DATA_WR_CS);
		}
		break;
	}
}

static void
umass_t_cbi_data_wr_cs_callback(struct usb_xfer *xfer, usb_error_t error)
{
	umass_t_cbi_data_clear_stall_callback(xfer, UMASS_T_CBI_STATUS,
	    UMASS_T_CBI_DATA_WRITE, error);
}

static void
umass_t_cbi_status_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct umass_softc *sc = usbd_xfer_softc(xfer);
	union ccb *ccb = sc->sc_transfer.ccb;
	struct usb_page_cache *pc;
	uint32_t residue;
	uint8_t status;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		if (actlen < (int)sizeof(sc->sbl)) {
			goto tr_setup;
		}
		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, &sc->sbl, sizeof(sc->sbl));

		residue = (sc->sc_transfer.data_len -
		    sc->sc_transfer.actlen);

		/* dissect the information in the buffer */

		if (sc->sc_proto & UMASS_PROTO_UFI) {

			/*
			 * Section 3.4.3.1.3 specifies that the UFI command
			 * protocol returns an ASC and ASCQ in the interrupt
			 * data block.
			 */

			DPRINTF(sc, UDMASS_CBI, "UFI CCI, ASC = 0x%02x, "
			    "ASCQ = 0x%02x\n", sc->sbl.ufi.asc,
			    sc->sbl.ufi.ascq);

			status = (((sc->sbl.ufi.asc == 0) &&
			    (sc->sbl.ufi.ascq == 0)) ?
			    STATUS_CMD_OK : STATUS_CMD_FAILED);

			sc->sc_transfer.ccb = NULL;

			sc->sc_last_xfer_index = UMASS_T_CBI_COMMAND;

			(sc->sc_transfer.callback)
			    (sc, ccb, residue, status);

			break;

		} else {

			/* Command Interrupt Data Block */

			DPRINTF(sc, UDMASS_CBI, "type=0x%02x, value=0x%02x\n",
			    sc->sbl.common.type, sc->sbl.common.value);

			if (sc->sbl.common.type == IDB_TYPE_CCI) {

				status = (sc->sbl.common.value & IDB_VALUE_STATUS_MASK);

				status = ((status == IDB_VALUE_PASS) ? STATUS_CMD_OK :
				    (status == IDB_VALUE_FAIL) ? STATUS_CMD_FAILED :
				    (status == IDB_VALUE_PERSISTENT) ? STATUS_CMD_FAILED :
				    STATUS_WIRE_FAILED);

				sc->sc_transfer.ccb = NULL;

				sc->sc_last_xfer_index = UMASS_T_CBI_COMMAND;

				(sc->sc_transfer.callback)
				    (sc, ccb, residue, status);

				break;
			}
		}

		/* fallthrough */

	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;

	default:			/* Error */
		DPRINTF(sc, UDMASS_CBI, "Failed to read CSW: %s\n",
		    usbd_errstr(error));
		umass_tr_error(xfer, error);
		break;
	}
}

/*
 * CAM specific functions (used by SCSI, UFI, 8070i (ATAPI))
 */

static int
umass_cam_attach_sim(struct umass_softc *sc)
{
	struct cam_devq *devq;		/* Per device Queue */

	/*
	 * A HBA is attached to the CAM layer.
	 *
	 * The CAM layer will then after a while start probing for devices on
	 * the bus. The number of SIMs is limited to one.
	 */

	devq = cam_simq_alloc(1 /* maximum openings */ );
	if (devq == NULL) {
		return (ENOMEM);
	}
	sc->sc_sim = cam_sim_alloc
	    (&umass_cam_action, &umass_cam_poll,
	    DEVNAME_SIM,
	    sc /* priv */ ,
	    sc->sc_unit /* unit number */ ,
	    &sc->sc_mtx /* mutex */ ,
	    1 /* maximum device openings */ ,
	    0 /* maximum tagged device openings */ ,
	    devq);

	if (sc->sc_sim == NULL) {
		cam_simq_free(devq);
		return (ENOMEM);
	}

	mtx_lock(&sc->sc_mtx);

	if (xpt_bus_register(sc->sc_sim, sc->sc_dev,
	    sc->sc_unit) != CAM_SUCCESS) {
		mtx_unlock(&sc->sc_mtx);
		return (ENOMEM);
	}
	mtx_unlock(&sc->sc_mtx);

	return (0);
}

static void
umass_cam_attach(struct umass_softc *sc)
{
#ifndef USB_DEBUG
	if (bootverbose)
#endif
		printf("%s:%d:%d: Attached to scbus%d\n",
		    sc->sc_name, cam_sim_path(sc->sc_sim),
		    sc->sc_unit, cam_sim_path(sc->sc_sim));
}

/* umass_cam_detach
 *	detach from the CAM layer
 */

static void
umass_cam_detach_sim(struct umass_softc *sc)
{
	if (sc->sc_sim != NULL) {
		if (xpt_bus_deregister(cam_sim_path(sc->sc_sim))) {
			/* accessing the softc is not possible after this */
			sc->sc_sim->softc = NULL;
			cam_sim_free(sc->sc_sim, /* free_devq */ TRUE);
		} else {
			panic("%s: CAM layer is busy\n",
			    sc->sc_name);
		}
		sc->sc_sim = NULL;
	}
}

/* umass_cam_action
 * 	CAM requests for action come through here
 */

static void
umass_cam_action(struct cam_sim *sim, union ccb *ccb)
{
	struct umass_softc *sc = (struct umass_softc *)sim->softc;

	if (sc == NULL) {
		ccb->ccb_h.status = CAM_SEL_TIMEOUT;
		xpt_done(ccb);
		return;
	}

	/* Perform the requested action */
	switch (ccb->ccb_h.func_code) {
	case XPT_SCSI_IO:
		{
			uint8_t *cmd;
			uint8_t dir;

			if (ccb->csio.ccb_h.flags & CAM_CDB_POINTER) {
				cmd = (uint8_t *)(ccb->csio.cdb_io.cdb_ptr);
			} else {
				cmd = (uint8_t *)(ccb->csio.cdb_io.cdb_bytes);
			}

			DPRINTF(sc, UDMASS_SCSI, "%d:%d:%jx:XPT_SCSI_IO: "
			    "cmd: 0x%02x, flags: 0x%02x, "
			    "%db cmd/%db data/%db sense\n",
			    cam_sim_path(sc->sc_sim), ccb->ccb_h.target_id,
			    (uintmax_t)ccb->ccb_h.target_lun, cmd[0],
			    ccb->ccb_h.flags & CAM_DIR_MASK, ccb->csio.cdb_len,
			    ccb->csio.dxfer_len, ccb->csio.sense_len);

			if (sc->sc_transfer.ccb) {
				DPRINTF(sc, UDMASS_SCSI, "%d:%d:%jx:XPT_SCSI_IO: "
				    "I/O in progress, deferring\n",
				    cam_sim_path(sc->sc_sim), ccb->ccb_h.target_id,
				    (uintmax_t)ccb->ccb_h.target_lun);
				ccb->ccb_h.status = CAM_SCSI_BUSY;
				xpt_done(ccb);
				goto done;
			}
			switch (ccb->ccb_h.flags & CAM_DIR_MASK) {
			case CAM_DIR_IN:
				dir = DIR_IN;
				break;
			case CAM_DIR_OUT:
				dir = DIR_OUT;
				DIF(UDMASS_SCSI,
				    umass_dump_buffer(sc, ccb->csio.data_ptr,
				    ccb->csio.dxfer_len, 48));
				break;
			default:
				dir = DIR_NONE;
			}

			ccb->ccb_h.status = CAM_REQ_INPROG | CAM_SIM_QUEUED;

			/*
			 * sc->sc_transform will convert the command to the
			 * command format needed by the specific command set
			 * and return the converted command in
			 * "sc->sc_transfer.cmd_data"
			 */
			if (umass_std_transform(sc, ccb, cmd, ccb->csio.cdb_len)) {

				if (sc->sc_transfer.cmd_data[0] == INQUIRY) {
					const char *pserial;

					pserial = usb_get_serial(sc->sc_udev);

					/*
					 * Umass devices don't generally report their serial numbers
					 * in the usual SCSI way.  Emulate it here.
					 */
					if ((sc->sc_transfer.cmd_data[1] & SI_EVPD) &&
					    (sc->sc_transfer.cmd_data[2] == SVPD_UNIT_SERIAL_NUMBER) &&
					    (pserial[0] != '\0')) {
						struct scsi_vpd_unit_serial_number *vpd_serial;

						vpd_serial = (struct scsi_vpd_unit_serial_number *)ccb->csio.data_ptr;
						vpd_serial->length = strlen(pserial);
						if (vpd_serial->length > sizeof(vpd_serial->serial_num))
							vpd_serial->length = sizeof(vpd_serial->serial_num);
						memcpy(vpd_serial->serial_num, pserial, vpd_serial->length);
						ccb->csio.scsi_status = SCSI_STATUS_OK;
						ccb->ccb_h.status = CAM_REQ_CMP;
						xpt_done(ccb);
						goto done;
					}

					/*
					 * Handle EVPD inquiry for broken devices first
					 * NO_INQUIRY also implies NO_INQUIRY_EVPD
					 */
					if ((sc->sc_quirks & (NO_INQUIRY_EVPD | NO_INQUIRY)) &&
					    (sc->sc_transfer.cmd_data[1] & SI_EVPD)) {

						scsi_set_sense_data(&ccb->csio.sense_data,
							/*sense_format*/ SSD_TYPE_NONE,
							/*current_error*/ 1,
							/*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
							/*asc*/ 0x24,
							/*ascq*/ 0x00,
							/*extra args*/ SSD_ELEM_NONE);
						ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
						ccb->ccb_h.status =
						    CAM_SCSI_STATUS_ERROR |
						    CAM_AUTOSNS_VALID |
						    CAM_DEV_QFRZN;
						xpt_freeze_devq(ccb->ccb_h.path, 1);
						xpt_done(ccb);
						goto done;
					}
					/*
					 * Return fake inquiry data for
					 * broken devices
					 */
					if (sc->sc_quirks & NO_INQUIRY) {
						memcpy(ccb->csio.data_ptr, &fake_inq_data,
						    sizeof(fake_inq_data));
						ccb->csio.scsi_status = SCSI_STATUS_OK;
						ccb->ccb_h.status = CAM_REQ_CMP;
						xpt_done(ccb);
						goto done;
					}
					if (sc->sc_quirks & FORCE_SHORT_INQUIRY) {
						ccb->csio.dxfer_len = SHORT_INQUIRY_LENGTH;
					}
				} else if (sc->sc_transfer.cmd_data[0] == PREVENT_ALLOW) {
					if (sc->sc_quirks & NO_PREVENT_ALLOW) {
						ccb->csio.scsi_status = SCSI_STATUS_OK;
						ccb->ccb_h.status = CAM_REQ_CMP;
						xpt_done(ccb);
						goto done;
					}
				} else if (sc->sc_transfer.cmd_data[0] == SYNCHRONIZE_CACHE) {
					if (sc->sc_quirks & NO_SYNCHRONIZE_CACHE) {
						ccb->csio.scsi_status = SCSI_STATUS_OK;
						ccb->ccb_h.status = CAM_REQ_CMP;
						xpt_done(ccb);
						goto done;
					}
				}
				umass_command_start(sc, dir, ccb->csio.data_ptr,
				    ccb->csio.dxfer_len,
				    ccb->ccb_h.timeout,
				    &umass_cam_cb, ccb);
			}
			break;
		}
	case XPT_PATH_INQ:
		{
			struct ccb_pathinq *cpi = &ccb->cpi;

			DPRINTF(sc, UDMASS_SCSI, "%d:%d:%jx:XPT_PATH_INQ:.\n",
			    sc ? cam_sim_path(sc->sc_sim) : -1, ccb->ccb_h.target_id,
			    (uintmax_t)ccb->ccb_h.target_lun);

			/* host specific information */
			cpi->version_num = 1;
			cpi->hba_inquiry = 0;
			cpi->target_sprt = 0;
			cpi->hba_misc = PIM_NO_6_BYTE;
			cpi->hba_eng_cnt = 0;
			cpi->max_target = UMASS_SCSIID_MAX;	/* one target */
			cpi->initiator_id = UMASS_SCSIID_HOST;
			strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
			strlcpy(cpi->hba_vid, "USB SCSI", HBA_IDLEN);
			strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
			cpi->unit_number = cam_sim_unit(sim);
			cpi->bus_id = sc->sc_unit;
			cpi->protocol = PROTO_SCSI;
			cpi->protocol_version = SCSI_REV_2;
			cpi->transport = XPORT_USB;
			cpi->transport_version = 0;

			if (sc == NULL) {
				cpi->base_transfer_speed = 0;
				cpi->max_lun = 0;
			} else {
				if (sc->sc_quirks & FLOPPY_SPEED) {
					cpi->base_transfer_speed =
					    UMASS_FLOPPY_TRANSFER_SPEED;
				} else {
					switch (usbd_get_speed(sc->sc_udev)) {
					case USB_SPEED_SUPER:
						cpi->base_transfer_speed =
						    UMASS_SUPER_TRANSFER_SPEED;
						cpi->maxio = MAXPHYS;
						break;
					case USB_SPEED_HIGH:
						cpi->base_transfer_speed =
						    UMASS_HIGH_TRANSFER_SPEED;
						break;
					default:
						cpi->base_transfer_speed =
						    UMASS_FULL_TRANSFER_SPEED;
						break;
					}
				}
				cpi->max_lun = sc->sc_maxlun;
			}

			cpi->ccb_h.status = CAM_REQ_CMP;
			xpt_done(ccb);
			break;
		}
	case XPT_RESET_DEV:
		{
			DPRINTF(sc, UDMASS_SCSI, "%d:%d:%jx:XPT_RESET_DEV:.\n",
			    cam_sim_path(sc->sc_sim), ccb->ccb_h.target_id,
			    (uintmax_t)ccb->ccb_h.target_lun);

			umass_reset(sc);

			ccb->ccb_h.status = CAM_REQ_CMP;
			xpt_done(ccb);
			break;
		}
	case XPT_GET_TRAN_SETTINGS:
		{
			struct ccb_trans_settings *cts = &ccb->cts;

			DPRINTF(sc, UDMASS_SCSI, "%d:%d:%jx:XPT_GET_TRAN_SETTINGS:.\n",
			    cam_sim_path(sc->sc_sim), ccb->ccb_h.target_id,
			    (uintmax_t)ccb->ccb_h.target_lun);

			cts->protocol = PROTO_SCSI;
			cts->protocol_version = SCSI_REV_2;
			cts->transport = XPORT_USB;
			cts->transport_version = 0;
			cts->xport_specific.valid = 0;

			ccb->ccb_h.status = CAM_REQ_CMP;
			xpt_done(ccb);
			break;
		}
	case XPT_SET_TRAN_SETTINGS:
		{
			DPRINTF(sc, UDMASS_SCSI, "%d:%d:%jx:XPT_SET_TRAN_SETTINGS:.\n",
			    cam_sim_path(sc->sc_sim), ccb->ccb_h.target_id,
			    (uintmax_t)ccb->ccb_h.target_lun);

			ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
			xpt_done(ccb);
			break;
		}
	case XPT_CALC_GEOMETRY:
		{
			cam_calc_geometry(&ccb->ccg, /* extended */ 1);
			xpt_done(ccb);
			break;
		}
	case XPT_NOOP:
		{
			DPRINTF(sc, UDMASS_SCSI, "%d:%d:%jx:XPT_NOOP:.\n",
			    sc ? cam_sim_path(sc->sc_sim) : -1, ccb->ccb_h.target_id,
			    (uintmax_t)ccb->ccb_h.target_lun);

			ccb->ccb_h.status = CAM_REQ_CMP;
			xpt_done(ccb);
			break;
		}
	default:
		DPRINTF(sc, UDMASS_SCSI, "%d:%d:%jx:func_code 0x%04x: "
		    "Not implemented\n",
		    sc ? cam_sim_path(sc->sc_sim) : -1, ccb->ccb_h.target_id,
		    (uintmax_t)ccb->ccb_h.target_lun, ccb->ccb_h.func_code);

		ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
		xpt_done(ccb);
		break;
	}

done:
	return;
}

static void
umass_cam_poll(struct cam_sim *sim)
{
	struct umass_softc *sc = (struct umass_softc *)sim->softc;

	if (sc == NULL)
		return;

	DPRINTF(sc, UDMASS_SCSI, "CAM poll\n");

	usbd_transfer_poll(sc->sc_xfer, UMASS_T_MAX);
}


/* umass_cam_cb
 *	finalise a completed CAM command
 */

static void
umass_cam_cb(struct umass_softc *sc, union ccb *ccb, uint32_t residue,
    uint8_t status)
{
	ccb->csio.resid = residue;

	switch (status) {
	case STATUS_CMD_OK:
		ccb->ccb_h.status = CAM_REQ_CMP;
		if ((sc->sc_quirks & READ_CAPACITY_OFFBY1) &&
		    (ccb->ccb_h.func_code == XPT_SCSI_IO) &&
		    (ccb->csio.cdb_io.cdb_bytes[0] == READ_CAPACITY)) {
			struct scsi_read_capacity_data *rcap;
			uint32_t maxsector;

			rcap = (void *)(ccb->csio.data_ptr);
			maxsector = scsi_4btoul(rcap->addr) - 1;
			scsi_ulto4b(maxsector, rcap->addr);
		}
		/*
		 * We have to add SVPD_UNIT_SERIAL_NUMBER to the list
		 * of pages supported by the device - otherwise, CAM
		 * will never ask us for the serial number if the
		 * device cannot handle that by itself.
		 */
		if (ccb->ccb_h.func_code == XPT_SCSI_IO &&
		    sc->sc_transfer.cmd_data[0] == INQUIRY &&
		    (sc->sc_transfer.cmd_data[1] & SI_EVPD) &&
		    sc->sc_transfer.cmd_data[2] == SVPD_SUPPORTED_PAGE_LIST &&
		    (usb_get_serial(sc->sc_udev)[0] != '\0')) {
			struct ccb_scsiio *csio;
			struct scsi_vpd_supported_page_list *page_list;

			csio = &ccb->csio;
			page_list = (struct scsi_vpd_supported_page_list *)csio->data_ptr;
			if (page_list->length + 1 < SVPD_SUPPORTED_PAGES_SIZE) {
				page_list->list[page_list->length] = SVPD_UNIT_SERIAL_NUMBER;
				page_list->length++;
			}
		}
		xpt_done(ccb);
		break;

	case STATUS_CMD_UNKNOWN:
	case STATUS_CMD_FAILED:

		/* fetch sense data */

		/* the rest of the command was filled in at attach */
		sc->cam_scsi_sense.length = ccb->csio.sense_len;

		DPRINTF(sc, UDMASS_SCSI, "Fetching %d bytes of "
		    "sense data\n", ccb->csio.sense_len);

		if (umass_std_transform(sc, ccb, &sc->cam_scsi_sense.opcode,
		    sizeof(sc->cam_scsi_sense))) {

			if ((sc->sc_quirks & FORCE_SHORT_INQUIRY) &&
			    (sc->sc_transfer.cmd_data[0] == INQUIRY)) {
				ccb->csio.sense_len = SHORT_INQUIRY_LENGTH;
			}
			umass_command_start(sc, DIR_IN, &ccb->csio.sense_data.error_code,
			    ccb->csio.sense_len, ccb->ccb_h.timeout,
			    &umass_cam_sense_cb, ccb);
		}
		break;

	default:
		/*
		 * The wire protocol failed and will hopefully have
		 * recovered. We return an error to CAM and let CAM
		 * retry the command if necessary.
		 */
		xpt_freeze_devq(ccb->ccb_h.path, 1);
		ccb->ccb_h.status = CAM_REQ_CMP_ERR | CAM_DEV_QFRZN;
		xpt_done(ccb);
		break;
	}
}

/*
 * Finalise a completed autosense operation
 */
static void
umass_cam_sense_cb(struct umass_softc *sc, union ccb *ccb, uint32_t residue,
    uint8_t status)
{
	uint8_t *cmd;

	switch (status) {
	case STATUS_CMD_OK:
	case STATUS_CMD_UNKNOWN:
	case STATUS_CMD_FAILED: {
		int key, sense_len;

		ccb->csio.sense_resid = residue;
		sense_len = ccb->csio.sense_len - ccb->csio.sense_resid;
		key = scsi_get_sense_key(&ccb->csio.sense_data, sense_len,
					 /*show_errors*/ 1);

		if (ccb->csio.ccb_h.flags & CAM_CDB_POINTER) {
			cmd = (uint8_t *)(ccb->csio.cdb_io.cdb_ptr);
		} else {
			cmd = (uint8_t *)(ccb->csio.cdb_io.cdb_bytes);
		}

		/*
		 * Getting sense data always succeeds (apart from wire
		 * failures):
		 */
		if ((sc->sc_quirks & RS_NO_CLEAR_UA) &&
		    (cmd[0] == INQUIRY) &&
		    (key == SSD_KEY_UNIT_ATTENTION)) {
			/*
			 * Ignore unit attention errors in the case where
			 * the Unit Attention state is not cleared on
			 * REQUEST SENSE. They will appear again at the next
			 * command.
			 */
			ccb->ccb_h.status = CAM_REQ_CMP;
		} else if (key == SSD_KEY_NO_SENSE) {
			/*
			 * No problem after all (in the case of CBI without
			 * CCI)
			 */
			ccb->ccb_h.status = CAM_REQ_CMP;
		} else if ((sc->sc_quirks & RS_NO_CLEAR_UA) &&
			    (cmd[0] == READ_CAPACITY) &&
		    (key == SSD_KEY_UNIT_ATTENTION)) {
			/*
			 * Some devices do not clear the unit attention error
			 * on request sense. We insert a test unit ready
			 * command to make sure we clear the unit attention
			 * condition, then allow the retry to proceed as
			 * usual.
			 */

			xpt_freeze_devq(ccb->ccb_h.path, 1);
			ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR
			    | CAM_AUTOSNS_VALID | CAM_DEV_QFRZN;
			ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;

#if 0
			DELAY(300000);
#endif
			DPRINTF(sc, UDMASS_SCSI, "Doing a sneaky"
			    "TEST_UNIT_READY\n");

			/* the rest of the command was filled in at attach */

			if ((sc->sc_transform)(sc,
			    &sc->cam_scsi_test_unit_ready.opcode,
			    sizeof(sc->cam_scsi_test_unit_ready)) == 1) {
				umass_command_start(sc, DIR_NONE, NULL, 0,
				    ccb->ccb_h.timeout,
				    &umass_cam_quirk_cb, ccb);
				break;
			}
		} else {
			xpt_freeze_devq(ccb->ccb_h.path, 1);
			if (key >= 0) {
				ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR
				    | CAM_AUTOSNS_VALID | CAM_DEV_QFRZN;
				ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
			} else
				ccb->ccb_h.status = CAM_AUTOSENSE_FAIL
				    | CAM_DEV_QFRZN;
		}
		xpt_done(ccb);
		break;
	}
	default:
		DPRINTF(sc, UDMASS_SCSI, "Autosense failed, "
		    "status %d\n", status);
		xpt_freeze_devq(ccb->ccb_h.path, 1);
		ccb->ccb_h.status = CAM_AUTOSENSE_FAIL | CAM_DEV_QFRZN;
		xpt_done(ccb);
	}
}

/*
 * This completion code just handles the fact that we sent a test-unit-ready
 * after having previously failed a READ CAPACITY with CHECK_COND.  The CCB
 * status for CAM is already set earlier.
 */
static void
umass_cam_quirk_cb(struct umass_softc *sc, union ccb *ccb, uint32_t residue,
    uint8_t status)
{
	DPRINTF(sc, UDMASS_SCSI, "Test unit ready "
	    "returned status %d\n", status);

	xpt_done(ccb);
}

/*
 * SCSI specific functions
 */

static uint8_t
umass_scsi_transform(struct umass_softc *sc, uint8_t *cmd_ptr,
    uint8_t cmd_len)
{
	if ((cmd_len == 0) ||
	    (cmd_len > sizeof(sc->sc_transfer.cmd_data))) {
		DPRINTF(sc, UDMASS_SCSI, "Invalid command "
		    "length: %d bytes\n", cmd_len);
		return (0);		/* failure */
	}
	sc->sc_transfer.cmd_len = cmd_len;

	switch (cmd_ptr[0]) {
	case TEST_UNIT_READY:
		if (sc->sc_quirks & NO_TEST_UNIT_READY) {
			DPRINTF(sc, UDMASS_SCSI, "Converted TEST_UNIT_READY "
			    "to START_UNIT\n");
			memset(sc->sc_transfer.cmd_data, 0, cmd_len);
			sc->sc_transfer.cmd_data[0] = START_STOP_UNIT;
			sc->sc_transfer.cmd_data[4] = SSS_START;
			return (1);
		}
		break;

	case INQUIRY:
		/*
		 * some drives wedge when asked for full inquiry
		 * information.
		 */
		if (sc->sc_quirks & FORCE_SHORT_INQUIRY) {
			memcpy(sc->sc_transfer.cmd_data, cmd_ptr, cmd_len);
			sc->sc_transfer.cmd_data[4] = SHORT_INQUIRY_LENGTH;
			return (1);
		}
		break;
	}

	memcpy(sc->sc_transfer.cmd_data, cmd_ptr, cmd_len);
	return (1);
}

static uint8_t
umass_rbc_transform(struct umass_softc *sc, uint8_t *cmd_ptr, uint8_t cmd_len)
{
	if ((cmd_len == 0) ||
	    (cmd_len > sizeof(sc->sc_transfer.cmd_data))) {
		DPRINTF(sc, UDMASS_SCSI, "Invalid command "
		    "length: %d bytes\n", cmd_len);
		return (0);		/* failure */
	}
	switch (cmd_ptr[0]) {
		/* these commands are defined in RBC: */
	case READ_10:
	case READ_CAPACITY:
	case START_STOP_UNIT:
	case SYNCHRONIZE_CACHE:
	case WRITE_10:
	case VERIFY_10:
	case INQUIRY:
	case MODE_SELECT_10:
	case MODE_SENSE_10:
	case TEST_UNIT_READY:
	case WRITE_BUFFER:
		/*
		 * The following commands are not listed in my copy of the
		 * RBC specs. CAM however seems to want those, and at least
		 * the Sony DSC device appears to support those as well
		 */
	case REQUEST_SENSE:
	case PREVENT_ALLOW:

		memcpy(sc->sc_transfer.cmd_data, cmd_ptr, cmd_len);

		if ((sc->sc_quirks & RBC_PAD_TO_12) && (cmd_len < 12)) {
			memset(sc->sc_transfer.cmd_data + cmd_len,
			    0, 12 - cmd_len);
			cmd_len = 12;
		}
		sc->sc_transfer.cmd_len = cmd_len;
		return (1);		/* success */

		/* All other commands are not legal in RBC */
	default:
		DPRINTF(sc, UDMASS_SCSI, "Unsupported RBC "
		    "command 0x%02x\n", cmd_ptr[0]);
		return (0);		/* failure */
	}
}

static uint8_t
umass_ufi_transform(struct umass_softc *sc, uint8_t *cmd_ptr,
    uint8_t cmd_len)
{
	if ((cmd_len == 0) ||
	    (cmd_len > sizeof(sc->sc_transfer.cmd_data))) {
		DPRINTF(sc, UDMASS_SCSI, "Invalid command "
		    "length: %d bytes\n", cmd_len);
		return (0);		/* failure */
	}
	/* An UFI command is always 12 bytes in length */
	sc->sc_transfer.cmd_len = UFI_COMMAND_LENGTH;

	/* Zero the command data */
	memset(sc->sc_transfer.cmd_data, 0, UFI_COMMAND_LENGTH);

	switch (cmd_ptr[0]) {
		/*
		 * Commands of which the format has been verified. They
		 * should work. Copy the command into the (zeroed out)
		 * destination buffer.
		 */
	case TEST_UNIT_READY:
		if (sc->sc_quirks & NO_TEST_UNIT_READY) {
			/*
			 * Some devices do not support this command. Start
			 * Stop Unit should give the same results
			 */
			DPRINTF(sc, UDMASS_UFI, "Converted TEST_UNIT_READY "
			    "to START_UNIT\n");

			sc->sc_transfer.cmd_data[0] = START_STOP_UNIT;
			sc->sc_transfer.cmd_data[4] = SSS_START;
			return (1);
		}
		break;

	case REZERO_UNIT:
	case REQUEST_SENSE:
	case FORMAT_UNIT:
	case INQUIRY:
	case START_STOP_UNIT:
	case SEND_DIAGNOSTIC:
	case PREVENT_ALLOW:
	case READ_CAPACITY:
	case READ_10:
	case WRITE_10:
	case POSITION_TO_ELEMENT:	/* SEEK_10 */
	case WRITE_AND_VERIFY:
	case VERIFY:
	case MODE_SELECT_10:
	case MODE_SENSE_10:
	case READ_12:
	case WRITE_12:
	case READ_FORMAT_CAPACITIES:
		break;

		/*
		 * SYNCHRONIZE_CACHE isn't supported by UFI, nor should it be
		 * required for UFI devices, so it is appropriate to fake
		 * success.
		 */
	case SYNCHRONIZE_CACHE:
		return (2);

	default:
		DPRINTF(sc, UDMASS_SCSI, "Unsupported UFI "
		    "command 0x%02x\n", cmd_ptr[0]);
		return (0);		/* failure */
	}

	memcpy(sc->sc_transfer.cmd_data, cmd_ptr, cmd_len);
	return (1);			/* success */
}

/*
 * 8070i (ATAPI) specific functions
 */
static uint8_t
umass_atapi_transform(struct umass_softc *sc, uint8_t *cmd_ptr,
    uint8_t cmd_len)
{
	if ((cmd_len == 0) ||
	    (cmd_len > sizeof(sc->sc_transfer.cmd_data))) {
		DPRINTF(sc, UDMASS_SCSI, "Invalid command "
		    "length: %d bytes\n", cmd_len);
		return (0);		/* failure */
	}
	/* An ATAPI command is always 12 bytes in length. */
	sc->sc_transfer.cmd_len = ATAPI_COMMAND_LENGTH;

	/* Zero the command data */
	memset(sc->sc_transfer.cmd_data, 0, ATAPI_COMMAND_LENGTH);

	switch (cmd_ptr[0]) {
		/*
		 * Commands of which the format has been verified. They
		 * should work. Copy the command into the destination
		 * buffer.
		 */
	case INQUIRY:
		/*
		 * some drives wedge when asked for full inquiry
		 * information.
		 */
		if (sc->sc_quirks & FORCE_SHORT_INQUIRY) {
			memcpy(sc->sc_transfer.cmd_data, cmd_ptr, cmd_len);

			sc->sc_transfer.cmd_data[4] = SHORT_INQUIRY_LENGTH;
			return (1);
		}
		break;

	case TEST_UNIT_READY:
		if (sc->sc_quirks & NO_TEST_UNIT_READY) {
			DPRINTF(sc, UDMASS_SCSI, "Converted TEST_UNIT_READY "
			    "to START_UNIT\n");
			sc->sc_transfer.cmd_data[0] = START_STOP_UNIT;
			sc->sc_transfer.cmd_data[4] = SSS_START;
			return (1);
		}
		break;

	case REZERO_UNIT:
	case REQUEST_SENSE:
	case START_STOP_UNIT:
	case SEND_DIAGNOSTIC:
	case PREVENT_ALLOW:
	case READ_CAPACITY:
	case READ_10:
	case WRITE_10:
	case POSITION_TO_ELEMENT:	/* SEEK_10 */
	case SYNCHRONIZE_CACHE:
	case MODE_SELECT_10:
	case MODE_SENSE_10:
	case READ_BUFFER:
	case 0x42:			/* READ_SUBCHANNEL */
	case 0x43:			/* READ_TOC */
	case 0x44:			/* READ_HEADER */
	case 0x47:			/* PLAY_MSF (Play Minute/Second/Frame) */
	case 0x48:			/* PLAY_TRACK */
	case 0x49:			/* PLAY_TRACK_REL */
	case 0x4b:			/* PAUSE */
	case 0x51:			/* READ_DISK_INFO */
	case 0x52:			/* READ_TRACK_INFO */
	case 0x54:			/* SEND_OPC */
	case 0x59:			/* READ_MASTER_CUE */
	case 0x5b:			/* CLOSE_TR_SESSION */
	case 0x5c:			/* READ_BUFFER_CAP */
	case 0x5d:			/* SEND_CUE_SHEET */
	case 0xa1:			/* BLANK */
	case 0xa5:			/* PLAY_12 */
	case 0xa6:			/* EXCHANGE_MEDIUM */
	case 0xad:			/* READ_DVD_STRUCTURE */
	case 0xbb:			/* SET_CD_SPEED */
	case 0xe5:			/* READ_TRACK_INFO_PHILIPS */
		break;

	case READ_12:
	case WRITE_12:
	default:
		DPRINTF(sc, UDMASS_SCSI, "Unsupported ATAPI "
		    "command 0x%02x - trying anyway\n",
		    cmd_ptr[0]);
		break;
	}

	memcpy(sc->sc_transfer.cmd_data, cmd_ptr, cmd_len);
	return (1);			/* success */
}

static uint8_t
umass_no_transform(struct umass_softc *sc, uint8_t *cmd,
    uint8_t cmdlen)
{
	return (0);			/* failure */
}

static uint8_t
umass_std_transform(struct umass_softc *sc, union ccb *ccb,
    uint8_t *cmd, uint8_t cmdlen)
{
	uint8_t retval;

	retval = (sc->sc_transform) (sc, cmd, cmdlen);

	if (retval == 2) {
		ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		return (0);
	} else if (retval == 0) {
		xpt_freeze_devq(ccb->ccb_h.path, 1);
		ccb->ccb_h.status = CAM_REQ_INVALID | CAM_DEV_QFRZN;
		xpt_done(ccb);
		return (0);
	}
	/* Command should be executed */
	return (1);
}

#ifdef USB_DEBUG
static void
umass_bbb_dump_cbw(struct umass_softc *sc, umass_bbb_cbw_t *cbw)
{
	uint8_t *c = cbw->CBWCDB;

	uint32_t dlen = UGETDW(cbw->dCBWDataTransferLength);
	uint32_t tag = UGETDW(cbw->dCBWTag);

	uint8_t clen = cbw->bCDBLength;
	uint8_t flags = cbw->bCBWFlags;
	uint8_t lun = cbw->bCBWLUN;

	DPRINTF(sc, UDMASS_BBB, "CBW %d: cmd = %db "
	    "(0x%02x%02x%02x%02x%02x%02x%s), "
	    "data = %db, lun = %d, dir = %s\n",
	    tag, clen,
	    c[0], c[1], c[2], c[3], c[4], c[5], (clen > 6 ? "..." : ""),
	    dlen, lun, (flags == CBWFLAGS_IN ? "in" :
	    (flags == CBWFLAGS_OUT ? "out" : "<invalid>")));
}

static void
umass_bbb_dump_csw(struct umass_softc *sc, umass_bbb_csw_t *csw)
{
	uint32_t sig = UGETDW(csw->dCSWSignature);
	uint32_t tag = UGETDW(csw->dCSWTag);
	uint32_t res = UGETDW(csw->dCSWDataResidue);
	uint8_t status = csw->bCSWStatus;

	DPRINTF(sc, UDMASS_BBB, "CSW %d: sig = 0x%08x (%s), tag = 0x%08x, "
	    "res = %d, status = 0x%02x (%s)\n",
	    tag, sig, (sig == CSWSIGNATURE ? "valid" : "invalid"),
	    tag, res,
	    status, (status == CSWSTATUS_GOOD ? "good" :
	    (status == CSWSTATUS_FAILED ? "failed" :
	    (status == CSWSTATUS_PHASE ? "phase" : "<invalid>"))));
}

static void
umass_cbi_dump_cmd(struct umass_softc *sc, void *cmd, uint8_t cmdlen)
{
	uint8_t *c = cmd;
	uint8_t dir = sc->sc_transfer.dir;

	DPRINTF(sc, UDMASS_BBB, "cmd = %db "
	    "(0x%02x%02x%02x%02x%02x%02x%s), "
	    "data = %db, dir = %s\n",
	    cmdlen,
	    c[0], c[1], c[2], c[3], c[4], c[5], (cmdlen > 6 ? "..." : ""),
	    sc->sc_transfer.data_len,
	    (dir == DIR_IN ? "in" :
	    (dir == DIR_OUT ? "out" :
	    (dir == DIR_NONE ? "no data phase" : "<invalid>"))));
}

static void
umass_dump_buffer(struct umass_softc *sc, uint8_t *buffer, uint32_t buflen,
    uint32_t printlen)
{
	uint32_t i, j;
	char s1[40];
	char s2[40];
	char s3[5];

	s1[0] = '\0';
	s3[0] = '\0';

	sprintf(s2, " buffer=%p, buflen=%d", buffer, buflen);
	for (i = 0; (i < buflen) && (i < printlen); i++) {
		j = i % 16;
		if (j == 0 && i != 0) {
			DPRINTF(sc, UDMASS_GEN, "0x %s%s\n",
			    s1, s2);
			s2[0] = '\0';
		}
		sprintf(&s1[j * 2], "%02x", buffer[i] & 0xff);
	}
	if (buflen > printlen)
		sprintf(s3, " ...");
	DPRINTF(sc, UDMASS_GEN, "0x %s%s%s\n",
	    s1, s2, s3);
}

#endif
