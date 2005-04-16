/* -*- linux-c -*-
 *  drivers/char/vio.h
 *
 *  iSeries Virtual I/O Message Path header
 *
 *  Authors: Dave Boutcher <boutcher@us.ibm.com>
 *           Ryan Arnold <ryanarn@us.ibm.com>
 *           Colin Devilbiss <devilbis@us.ibm.com>
 *
 * (C) Copyright 2000 IBM Corporation
 * 
 * This header file is used by the iSeries virtual I/O device
 * drivers.  It defines the interfaces to the common functions
 * (implemented in drivers/char/viopath.h) as well as defining
 * common functions and structures.  Currently (at the time I 
 * wrote this comment) the iSeries virtual I/O device drivers
 * that use this are 
 *   drivers/block/viodasd.c 
 *   drivers/char/viocons.c
 *   drivers/char/viotape.c
 *   drivers/cdrom/viocd.c
 *
 * The iSeries virtual ethernet support (veth.c) uses a whole
 * different set of functions.
 * 
 * This program is free software;  you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) anyu later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.  
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */
#ifndef _ISERIES_VIO_H
#define _ISERIES_VIO_H

#include <asm/iSeries/HvTypes.h>
#include <asm/iSeries/HvLpEvent.h>

/* iSeries virtual I/O events use the subtype field in
 * HvLpEvent to figure out what kind of vio event is coming
 * in.  We use a table to route these, and this defines
 * the maximum number of distinct subtypes
 */
#define VIO_MAX_SUBTYPES 8

/* Each subtype can register a handler to process their events.
 * The handler must have this interface.
 */
typedef void (vio_event_handler_t) (struct HvLpEvent * event);

int viopath_open(HvLpIndex remoteLp, int subtype, int numReq);
int viopath_close(HvLpIndex remoteLp, int subtype, int numReq);
int vio_setHandler(int subtype, vio_event_handler_t * beh);
int vio_clearHandler(int subtype);
int viopath_isactive(HvLpIndex lp);
HvLpInstanceId viopath_sourceinst(HvLpIndex lp);
HvLpInstanceId viopath_targetinst(HvLpIndex lp);
void vio_set_hostlp(void);
void *vio_get_event_buffer(int subtype);
void vio_free_event_buffer(int subtype, void *buffer);

extern HvLpIndex viopath_hostLp;
extern HvLpIndex viopath_ourLp;

#define VIOCHAR_MAX_DATA 200

#define VIOMAJOR_SUBTYPE_MASK 0xff00
#define VIOMINOR_SUBTYPE_MASK 0x00ff
#define VIOMAJOR_SUBTYPE_SHIFT 8

#define VIOVERSION            0x0101

/*
 * This is the general structure for VIO errors; each module should have
 * a table of them, and each table should be terminated by an entry of
 * { 0, 0, NULL }.  Then, to find a specific error message, a module
 * should pass its local table and the return code.
 */
struct vio_error_entry {
	u16 rc;
	int errno;
	const char *msg;
};
const struct vio_error_entry *vio_lookup_rc(const struct vio_error_entry
					    *local_table, u16 rc);

enum viosubtypes {
	viomajorsubtype_monitor = 0x0100,
	viomajorsubtype_blockio = 0x0200,
	viomajorsubtype_chario = 0x0300,
	viomajorsubtype_config = 0x0400,
	viomajorsubtype_cdio = 0x0500,
	viomajorsubtype_tape = 0x0600,
	viomajorsubtype_scsi = 0x0700
};


enum vioconfigsubtype {
	vioconfigget = 0x0001,
};

enum viorc {
	viorc_good = 0x0000,
	viorc_noConnection = 0x0001,
	viorc_noReceiver = 0x0002,
	viorc_noBufferAvailable = 0x0003,
	viorc_invalidMessageType = 0x0004,
	viorc_invalidRange = 0x0201,
	viorc_invalidToken = 0x0202,
	viorc_DMAError = 0x0203,
	viorc_useError = 0x0204,
	viorc_releaseError = 0x0205,
	viorc_invalidDisk = 0x0206,
	viorc_openRejected = 0x0301
};

struct device;

extern struct device *iSeries_vio_dev;

#endif /* _ISERIES_VIO_H */
