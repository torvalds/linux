/*-
 * FreeBSD platform specific driver option settings, data structures,
 * function declarations and includes.
 *
 * Copyright (c) 1994-2001 Justin T. Gibbs.
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
 * $Id: //depot/aic7xxx/freebsd/dev/aic7xxx/aic7xxx_osm.h#18 $
 *
 * $FreeBSD$
 */

#ifndef _AIC7XXX_FREEBSD_H_
#define _AIC7XXX_FREEBSD_H_

#include "opt_aic7xxx.h"	/* for config options */

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

#if __FreeBSD_version < 500000
#include <pci.h>
#else
#define NPCI 1
#endif

#if NPCI > 0
#define AIC_PCI_CONFIG 1
#endif
#include <machine/bus.h>
#include <machine/endian.h>
#include <machine/resource.h>

#include <sys/rman.h>

#if NPCI > 0
#if __FreeBSD_version >= 500000
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#else
#include <pci/pcireg.h>
#include <pci/pcivar.h>
#endif
#endif

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_debug.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>

/*************************** Attachment Bookkeeping ***************************/
extern devclass_t ahc_devclass;

/****************************** Platform Macros *******************************/
#define	SIM_IS_SCSIBUS_B(ahc, sim)	\
	((sim) == ahc->platform_data->sim_b)
#define	SIM_CHANNEL(ahc, sim)	\
	(((sim) == ahc->platform_data->sim_b) ? 'B' : 'A')
#define	SIM_SCSI_ID(ahc, sim)	\
	(((sim) == ahc->platform_data->sim_b) ? ahc->our_id_b : ahc->our_id)
#define	SIM_PATH(ahc, sim)	\
	(((sim) == ahc->platform_data->sim_b) ? ahc->platform_data->path_b \
					      : ahc->platform_data->path)
#define BUILD_SCSIID(ahc, sim, target_id, our_id) \
        ((((target_id) << TID_SHIFT) & TID) | (our_id) \
        | (SIM_IS_SCSIBUS_B(ahc, sim) ? TWIN_CHNLB : 0))

#define SCB_GET_SIM(ahc, scb) \
	(SCB_GET_CHANNEL(ahc, scb) == 'A' ? (ahc)->platform_data->sim \
					  : (ahc)->platform_data->sim_b)

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
 * be the number of paged sized transfers in MAXPHYS plus an extra element
 * to handle any unaligned residual.  The sequencer fetches SG elements
 * in cacheline sized chucks, so make the number per-transaction an even
 * multiple of 16 which should align us on even the largest of cacheline
 * boundaries. 
 */
#define AHC_MAXPHYS (128 * 1024)
#define AHC_NSEG (roundup(btoc(AHC_MAXPHYS) + 1, 16))

/* This driver supports target mode */
#define AHC_TARGET_MODE 1

/************************** Softc/SCB Platform Data ***************************/
struct ahc_platform_data {
	/*
	 * Hooks into the XPT.
	 */
	struct	cam_sim		*sim;
	struct	cam_sim		*sim_b;
	struct	cam_path	*path;
	struct	cam_path	*path_b;

	int			 regs_res_type;
	int			 regs_res_id;
	int			 irq_res_type;
	struct resource		*regs;
	struct resource		*irq;
	void			*ih;
	eventhandler_tag	 eh;
	struct proc		*recovery_thread;
	struct mtx		mtx;
};

struct scb_platform_data {
};

/***************************** Core Includes **********************************/
#ifdef AHC_REG_PRETTY_PRINT
#define AIC_DEBUG_REGISTERS 1
#else
#define AIC_DEBUG_REGISTERS 0
#endif
#define AIC_CORE_INCLUDE <dev/aic7xxx/aic7xxx.h>
#define	AIC_LIB_PREFIX ahc
#define	AIC_CONST_PREFIX AHC
#include <dev/aic7xxx/aic_osm_lib.h>

/*************************** Device Access ************************************/
#define ahc_inb(ahc, port)				\
	bus_space_read_1((ahc)->tag, (ahc)->bsh, port)

#define ahc_outb(ahc, port, value)			\
	bus_space_write_1((ahc)->tag, (ahc)->bsh, port, value)

#define ahc_outsb(ahc, port, valp, count)		\
	bus_space_write_multi_1((ahc)->tag, (ahc)->bsh, port, valp, count)

#define ahc_insb(ahc, port, valp, count)		\
	bus_space_read_multi_1((ahc)->tag, (ahc)->bsh, port, valp, count)

static __inline void ahc_flush_device_writes(struct ahc_softc *);

static __inline void
ahc_flush_device_writes(struct ahc_softc *ahc)
{
	/* XXX Is this sufficient for all architectures??? */
	ahc_inb(ahc, INTSTAT);
}

/**************************** Locking Primitives ******************************/
/* Lock protecting internal data structures */
static __inline void ahc_lockinit(struct ahc_softc *);
static __inline void ahc_lock(struct ahc_softc *);
static __inline void ahc_unlock(struct ahc_softc *);

static __inline void
ahc_lockinit(struct ahc_softc *ahc)
{
	mtx_init(&ahc->platform_data->mtx, "ahc_lock", NULL, MTX_DEF);
}

static __inline void
ahc_lock(struct ahc_softc *ahc)
{
	mtx_lock(&ahc->platform_data->mtx);
}

static __inline void
ahc_unlock(struct ahc_softc *ahc)
{
	mtx_unlock(&ahc->platform_data->mtx);
}

/************************* Initialization/Teardown ****************************/
int	  ahc_platform_alloc(struct ahc_softc *ahc, void *platform_arg);
void	  ahc_platform_free(struct ahc_softc *ahc);
int	  ahc_map_int(struct ahc_softc *ahc);
int	  ahc_attach(struct ahc_softc *);
int	  ahc_softc_comp(struct ahc_softc *lahc, struct ahc_softc *rahc);
int	  ahc_detach(device_t);

/********************************** PCI ***************************************/
#ifdef AIC_PCI_CONFIG
int ahc_pci_map_registers(struct ahc_softc *ahc);
#define ahc_pci_map_int ahc_map_int
#endif /*AIC_PCI_CONFIG*/

/******************************** VL/EISA/ISA *********************************/
int aic7770_map_registers(struct ahc_softc *ahc, u_int port);
static __inline int aic7770_map_int(struct ahc_softc *, int);

static __inline int
aic7770_map_int(struct ahc_softc *ahc, int irq)
{
	/*
	 * The IRQ is unused in the FreeBSD
	 * implementation since the ISA attachment
	 * registers the IRQ with newbus before
	 * the core is called.
	 */
	return ahc_map_int(ahc);
}

/********************************* Debug **************************************/
static __inline void	ahc_print_path(struct ahc_softc *, struct scb *);
static __inline void	ahc_platform_dump_card_state(struct ahc_softc *ahc);

static __inline void
ahc_print_path(struct ahc_softc *ahc, struct scb *scb)
{
	xpt_print_path(scb->io_ctx->ccb_h.path);
}

static __inline void
ahc_platform_dump_card_state(struct ahc_softc *ahc)
{
	/* Nothing to do here for FreeBSD */
}
/**************************** Transfer Settings *******************************/
void	  ahc_notify_xfer_settings_change(struct ahc_softc *,
					  struct ahc_devinfo *);
void	  ahc_platform_set_tags(struct ahc_softc *, struct ahc_devinfo *,
				int /*enable*/);

/****************************** Interrupts ************************************/
void			ahc_platform_intr(void *);
static __inline void	ahc_platform_flushwork(struct ahc_softc *ahc);
static __inline void
ahc_platform_flushwork(struct ahc_softc *ahc)
{
}

/************************ Misc Function Declarations **************************/
void	  ahc_done(struct ahc_softc *ahc, struct scb *scb);
void	  ahc_send_async(struct ahc_softc *, char /*channel*/,
			 u_int /*target*/, u_int /*lun*/, ac_code, void *arg);
#endif  /* _AIC7XXX_FREEBSD_H_ */
