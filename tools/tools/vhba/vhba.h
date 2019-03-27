/*-
 * Copyright (c) 2010 by Panasas, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 */
/* $FreeBSD$ */
/*
 * Virtual HBA defines
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/taskqueue.h>
#include <sys/mutex.h>
#include <sys/condvar.h>

#include <sys/proc.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/stdarg.h>

#include <cam/cam.h>
#include <cam/cam_debug.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_debug.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>


#include <sys/unistd.h>
#include <sys/kthread.h>
#include <sys/conf.h>
#include <sys/module.h>
#include <sys/ioccom.h>
#include <sys/devicestat.h>
#include <cam/cam_periph.h>
#include <cam/cam_xpt_periph.h>

#define	VHBA_MAXTGT	64
#define	VHBA_MAXCMDS	256

typedef struct {
	struct mtx              lock;
	struct cam_sim *	sim;
	struct cam_devq *       devq;
	TAILQ_HEAD(, ccb_hdr)	actv;
	TAILQ_HEAD(, ccb_hdr)	done;
	void *			private;
} vhba_softc_t;

/*
 * Each different instantiation of a fake HBA needs to
 * provide these as function entry points. It's responsible
 * for setting up some thread or regular timeout that will
 * dequeue things from the actv queue and put done items
 * on the done queue.
 */
void vhba_init(vhba_softc_t *);
void vhba_fini(vhba_softc_t *);
void vhba_kick(vhba_softc_t *);

/*
 * Support functions
 */
void vhba_fill_sense(struct ccb_scsiio *, uint8_t, uint8_t, uint8_t);
int vhba_rwparm(uint8_t *, uint64_t *, uint32_t *, uint64_t, uint32_t);
void vhba_default_cmd(struct ccb_scsiio *, lun_id_t, uint8_t *);
void vhba_set_status(struct ccb_hdr *, cam_status);

/*
 * Common module loader function
 */
int vhba_modprobe(module_t, int, void *);

/*
 * retrofits
 */
#ifndef MODE_SENSE
#define	MODE_SENSE	0x1a
#endif
#ifndef	SMS_FORMAT_DEVICE_PAGE
#define	SMS_FORMAT_DEVICE_PAGE	0x03
#endif
#ifndef	SMS_GEOMETRY_PAGE
#define	SMS_GEOMETRY_PAGE	0x04
#endif
