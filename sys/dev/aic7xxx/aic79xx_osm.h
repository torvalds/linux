/*-
 * FreeBSD platform specific driver option settings, data structures,
 * function declarations and includes.
 *
 * Copyright (c) 1994-2001 Justin T. Gibbs.
 * Copyright (c) 2001-2002 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU Public License ("GPL").
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
 * $Id: //depot/aic7xxx/freebsd/dev/aic7xxx/aic79xx_osm.h#23 $
 *
 * $FreeBSD$
 */

#ifndef _AIC79XX_FREEBSD_H_
#define _AIC79XX_FREEBSD_H_

#include "opt_aic79xx.h"	/* for config options */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>		/* For device_t */
#if __FreeBSD_version >= 500000
#include <sys/endian.h>
#endif
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/queue.h>
#include <sys/sysctl.h>

#define AIC_PCI_CONFIG 1
#include <machine/bus.h>
#include <machine/endian.h>
#include <machine/resource.h>

#include <sys/rman.h>

#if __FreeBSD_version >= 500000
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#else
#include <pci/pcireg.h>
#include <pci/pcivar.h>
#endif

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_debug.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>
#include <cam/scsi/scsi_iu.h>

/****************************** Platform Macros *******************************/
#define	SIM_IS_SCSIBUS_B(ahd, sim)	\
	(0)
#define	SIM_CHANNEL(ahd, sim)	\
	('A')
#define	SIM_SCSI_ID(ahd, sim)	\
	(ahd->our_id)
#define	SIM_PATH(ahd, sim)	\
	(ahd->platform_data->path)
#define BUILD_SCSIID(ahd, sim, target_id, our_id) \
        ((((target_id) << TID_SHIFT) & TID) | (our_id))
        

#define SCB_GET_SIM(ahd, scb) \
	((ahd)->platform_data->sim)

#ifndef offsetof
#define offsetof(type, member)  ((size_t)(&((type *)0)->member))
#endif

/************************ Tunable Driver Parameters  **************************/
/*
 * The number of dma segments supported.  The sequencer can handle any number
 * of physically contiguous S/G entrys.  To reduce the driver's memory
 * consumption, we limit the number supported to be sufficient to handle
 * the largest mapping supported by the legacy kernel MAXPHYS setting of
 * 128K.  This can be increased once some testing is done.  Assuming the
 * transfer is as fragmented as possible and unaligned, this turns out to
 * be the number of paged sized transfers in MAXPHYS plus an extra element
 * to handle any unaligned residual.  The sequencer fetches SG elements
 * in cacheline sized chucks, so make the number per-transaction an even
 * multiple of 16 which should align us on even the largest of cacheline
 * boundaries. 
 */
#define AHD_MAXPHYS (128 * 1024)
#define AHD_NSEG (roundup(btoc(AHD_MAXPHYS) + 1, 16))

/* This driver supports target mode */
#ifdef NOT_YET
#define AHD_TARGET_MODE 1
#endif

/************************** Softc/SCB Platform Data ***************************/
struct ahd_platform_data {
	/*
	 * Hooks into the XPT.
	 */
	struct	cam_sim		*sim;
	struct	cam_path	*path;

	int			 regs_res_type[2];
	int			 regs_res_id[2];
	int			 irq_res_type;
	struct resource		*regs[2];
	struct resource		*irq;
	void			*ih;
	eventhandler_tag	 eh;
	struct proc		*recovery_thread;
	struct mtx		mtx;
};

struct scb_platform_data {
};

/***************************** Core Includes **********************************/
#ifdef AHD_REG_PRETTY_PRINT
#define AIC_DEBUG_REGISTERS 1
#else
#define AIC_DEBUG_REGISTERS 0
#endif
#define AIC_CORE_INCLUDE <dev/aic7xxx/aic79xx.h>
#define	AIC_LIB_PREFIX ahd
#define	AIC_CONST_PREFIX AHD
#include <dev/aic7xxx/aic_osm_lib.h>

/*************************** Device Access ************************************/
#define ahd_inb(ahd, port)					\
	bus_space_read_1((ahd)->tags[(port) >> 8],		\
			 (ahd)->bshs[(port) >> 8], (port) & 0xFF)

#define ahd_outb(ahd, port, value)				\
	bus_space_write_1((ahd)->tags[(port) >> 8],		\
			  (ahd)->bshs[(port) >> 8], (port) & 0xFF, value)

#define ahd_inw_atomic(ahd, port)				\
	aic_le16toh(bus_space_read_2((ahd)->tags[(port) >> 8],	\
				     (ahd)->bshs[(port) >> 8], (port) & 0xFF))

#define ahd_outw_atomic(ahd, port, value)			\
	bus_space_write_2((ahd)->tags[(port) >> 8],		\
			  (ahd)->bshs[(port) >> 8],		\
			  (port & 0xFF), aic_htole16(value))

#define ahd_outsb(ahd, port, valp, count)			\
	bus_space_write_multi_1((ahd)->tags[(port) >> 8],	\
				(ahd)->bshs[(port) >> 8],	\
				(port & 0xFF), valp, count)

#define ahd_insb(ahd, port, valp, count)			\
	bus_space_read_multi_1((ahd)->tags[(port) >> 8],	\
			       (ahd)->bshs[(port) >> 8],	\
			       (port & 0xFF), valp, count)

static __inline void ahd_flush_device_writes(struct ahd_softc *);

static __inline void
ahd_flush_device_writes(struct ahd_softc *ahd)
{
	/* XXX Is this sufficient for all architectures??? */
	ahd_inb(ahd, INTSTAT);
}

/**************************** Locking Primitives ******************************/
/* Lock protecting internal data structures */
static __inline void ahd_lockinit(struct ahd_softc *);
static __inline void ahd_lock(struct ahd_softc *);
static __inline void ahd_unlock(struct ahd_softc *);

static __inline void
ahd_lockinit(struct ahd_softc *ahd)
{
	mtx_init(&ahd->platform_data->mtx, "ahd_lock", NULL, MTX_DEF);
}

static __inline void
ahd_lock(struct ahd_softc *ahd)
{
	mtx_lock(&ahd->platform_data->mtx);
}

static __inline void
ahd_unlock(struct ahd_softc *ahd)
{
	mtx_unlock(&ahd->platform_data->mtx);
}

/********************************** PCI ***************************************/
int ahd_pci_map_registers(struct ahd_softc *ahd);
int ahd_pci_map_int(struct ahd_softc *ahd);

/************************** Transaction Operations ****************************/
static __inline void aic_freeze_simq(struct aic_softc*);
static __inline void aic_release_simq(struct aic_softc*);

static __inline void
aic_freeze_simq(struct aic_softc *aic)
{
	xpt_freeze_simq(aic->platform_data->sim, /*count*/1);
}

static __inline void
aic_release_simq(struct aic_softc *aic)
{
	xpt_release_simq(aic->platform_data->sim, /*run queue*/TRUE);
}
/********************************* Debug **************************************/
static __inline void	ahd_print_path(struct ahd_softc *, struct scb *);
static __inline void	ahd_platform_dump_card_state(struct ahd_softc *ahd);

static __inline void
ahd_print_path(struct ahd_softc *ahd, struct scb *scb)
{
	xpt_print_path(scb->io_ctx->ccb_h.path);
}

static __inline void
ahd_platform_dump_card_state(struct ahd_softc *ahd)
{
	/* Nothing to do here for FreeBSD */
}
/**************************** Transfer Settings *******************************/
void	  ahd_notify_xfer_settings_change(struct ahd_softc *,
					  struct ahd_devinfo *);
void	  ahd_platform_set_tags(struct ahd_softc *, struct ahd_devinfo *,
				int /*enable*/);

/************************* Initialization/Teardown ****************************/
int	  ahd_platform_alloc(struct ahd_softc *ahd, void *platform_arg);
void	  ahd_platform_free(struct ahd_softc *ahd);
int	  ahd_map_int(struct ahd_softc *ahd);
int	  ahd_attach(struct ahd_softc *);
int	  ahd_softc_comp(struct ahd_softc *lahd, struct ahd_softc *rahd);
void	  ahd_sysctl(struct ahd_softc *ahd);
int	  ahd_detach(device_t);
#define	ahd_platform_init(arg)


/****************************** Interrupts ************************************/
void			ahd_platform_intr(void *);
static __inline void	ahd_platform_flushwork(struct ahd_softc *ahd);
static __inline void
ahd_platform_flushwork(struct ahd_softc *ahd)
{
}

/************************ Misc Function Declarations **************************/
void	  ahd_done(struct ahd_softc *ahd, struct scb *scb);
void	  ahd_send_async(struct ahd_softc *, char /*channel*/,
			 u_int /*target*/, u_int /*lun*/, ac_code, void *arg);
#endif  /* _AIC79XX_FREEBSD_H_ */
