/* $Id: os_bsd.h,v 1.20 2010/05/11 03:12:11 lcn Exp $ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * HighPoint RAID Driver for FreeBSD
 * Copyright (C) 2005-2011 HighPoint Technologies, Inc. All Rights Reserved.
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
 * $FreeBSD$
 */

#include <dev/hptnr/hptnr_config.h>

#ifndef _OS_BSD_H
#define _OS_BSD_H

#ifndef DBG
#define  DBG	0
#endif

#include <sys/param.h>
#include <sys/types.h>
#include <sys/cons.h>
#include <sys/time.h>
#include <sys/systm.h> 

#include <sys/stat.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/libkern.h>
#include <sys/kernel.h>

#include <sys/kthread.h>
#include <sys/mutex.h>
#include <sys/module.h>

#include <sys/eventhandler.h>
#include <sys/bus.h>
#include <sys/taskqueue.h>
#include <sys/ioccom.h>

#include <machine/resource.h>
#include <machine/pci_cfgreg.h>
#include <machine/bus.h>
#include <machine/stdarg.h>
#include <sys/rman.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_debug.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_periph.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>


typedef struct _INQUIRYDATA {
	u_char DeviceType : 5;
	u_char DeviceTypeQualifier : 3;
	u_char DeviceTypeModifier : 7;
	u_char RemovableMedia : 1;
	u_char Versions;
	u_char ResponseDataFormat;
	u_char AdditionalLength;
	u_char Reserved[2];
	u_char SoftReset : 1;
	u_char CommandQueue : 1;
	u_char Reserved2 : 1;
	u_char LinkedCommands : 1;
	u_char Synchronous : 1;
	u_char Wide16Bit : 1;
	u_char Wide32Bit : 1;
	u_char RelativeAddressing : 1;
	u_char VendorId[8];
	u_char ProductId[16];
	u_char ProductRevisionLevel[4];
	u_char VendorSpecific[20];
	u_char Reserved3[40];
} 
__attribute__((packed))
INQUIRYDATA, *PINQUIRYDATA;

#endif

/* private headers */

#include <dev/hptnr/osm.h>
#include <dev/hptnr/him.h>
#include <dev/hptnr/ldm.h>

/* driver parameters */
extern const char driver_name[];
extern const char driver_name_long[];
extern const char driver_ver[];
extern int  osm_max_targets;

/*
 * adapter/vbus extensions:
 * each physical controller has an adapter_ext, passed to him.create_adapter()
 * each vbus has a vbus_ext passed to ldm_create_vbus().
 */
#define EXT_TYPE_HBA  1
#define EXT_TYPE_VBUS 2

typedef struct _hba {
	int               ext_type;
	LDM_ADAPTER       ldm_adapter;
	device_t          pcidev;
	PCI_ADDRESS       pciaddr;
	struct _vbus_ext *vbus_ext;
	struct _hba      *next;
	
	struct {
		struct resource *res;
		int type;
		int rid;
		void *base;
	}
	pcibar[6];

	struct resource  *irq_res;
	void             *irq_handle;
}
HBA, *PHBA;

typedef struct _os_cmdext {
	struct _vbus_ext  *vbus_ext;
	struct _os_cmdext *next;
	union ccb         *ccb;
	bus_dmamap_t       dma_map;
	struct callout     timeout;
	SG                 psg[os_max_sg_descriptors];
}
OS_CMDEXT, *POS_CMDEXT;

typedef struct _vbus_ext {
	int               ext_type;
	struct _vbus_ext *next;
	PHBA              hba_list;
	struct freelist  *freelist_head;
	struct freelist  *freelist_dma_head;
	
	struct cam_sim   *sim;    /* sim for this vbus */
	struct cam_path  *path;   /* peripheral, path, tgt, lun with this vbus */
	struct mtx        lock; /* general purpose lock */
	bus_dma_tag_t     io_dmat; /* I/O buffer DMA tag */
	
	POS_CMDEXT        cmdext_list;

	OSM_TASK         *tasks;
	struct task       worker;
	
	struct callout    timer;

	eventhandler_tag  shutdown_eh;
	
	/* the LDM vbus instance continues */
	unsigned long vbus[0] __attribute__((aligned(sizeof(unsigned long))));
}
VBUS_EXT, *PVBUS_EXT;

#define hpt_lock_vbus(vbus_ext)   mtx_lock(&(vbus_ext)->lock)
#define hpt_unlock_vbus(vbus_ext) mtx_unlock(&(vbus_ext)->lock)
#define	hpt_assert_vbus_locked(vbus_ext)	mtx_assert(&(vbus_ext)->lock, MA_OWNED)


#define HPT_OSM_TIMEOUT (120*hz)  /* timeout value for OS commands */

#define HPT_DO_IOCONTROL	_IOW('H', 0, HPT_IOCTL_PARAM)

#define HPT_SCAN_BUS		_IO('H', 1)

#define TASK_ENQUEUE(task)	taskqueue_enqueue(taskqueue_swi,(task));

static	__inline	int hpt_sleep(PVBUS_EXT vbus_ext, void *ident, int priority, const char *wmesg, int timo)
{
	return	msleep(ident, &vbus_ext->lock, priority, wmesg, timo);
}
