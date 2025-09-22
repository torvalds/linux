/*	$OpenBSD: aic7xxx_openbsd.h,v 1.33 2024/06/26 01:40:49 jsg Exp $	*/
/*	$NetBSD: aic7xxx_osm.h,v 1.7 2003/11/02 11:07:44 wiz Exp $	*/

/*
 * OpenBSD platform specific driver option settings, data structures,
 * function declarations and includes.
 *
 * Copyright (c) 1994-2001 Justin T. Gibbs.
 * Copyright (c) 2001-2002 Steve Murphree, Jr.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author(s) may not be used to endorse or promote products
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
 * //depot/aic7xxx/freebsd/dev/aic7xxx/aic7xxx_osm.h#14 $
 *
 * $FreeBSD: /repoman/r/ncvs/src/sys/dev/aic7xxx/aic7xxx_osm.h,v 1.20 2002/12/04 22:51:29 scottl Exp $
 */
/*
 * Ported from FreeBSD by Pascal Renauld, Network Storage Solutions, Inc. - April 2003
 */

#ifndef _AIC7XXX_OPENBSD_H_
#define _AIC7XXX_OPENBSD_H_

#include "pci.h"		/* for config options */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include <sys/queue.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_message.h>
#include <scsi/scsiconf.h>

#include <uvm/uvm_extern.h>

#ifdef CAM_NEW_TRAN_CODE
#define AHC_NEW_TRAN_SETTINGS
#endif /* CAM_NEW_TRAN_CODE */

#if NPCI > 0
#define AHC_PCI_CONFIG 1
#endif

#if 0
#define AHC_DEBUG	AHC_SHOW_SENSE | AHC_SHOW_MISC | AHC_SHOW_CMDS
#endif

#ifdef DEBUG
#define bootverbose	1
#else
#define bootverbose	0
#endif
/****************************** Platform Macros *******************************/

#define	SCSI_IS_SCSIBUS_B(ahc, sc_link)	\
	(((ahc)->sc_child != NULL) && ((sc_link)->bus != (ahc)->sc_child))
#define	SCSI_SCSI_ID(ahc, sc_link)	\
	(SCSI_IS_SCSIBUS_B(ahc, sc_link) ? ahc->our_id_b : ahc->our_id)
#define	SCSI_CHANNEL(ahc, sc_link)	\
	(SCSI_IS_SCSIBUS_B(ahc, sc_link) ? 'B' : 'A')
#define BUILD_SCSIID(ahc, sc_link, target_id, our_id) \
        ((((target_id) << TID_SHIFT) & TID) | (our_id) \
        | (SCSI_IS_SCSIBUS_B(ahc, sc_link) ? TWIN_CHNLB : 0))

#ifndef offsetof
#define offsetof(type, member)  ((size_t)(&((type *)0)->member))
#endif
/************************* Forward Declarations *******************************/
typedef struct pci_attach_args * ahc_dev_softc_t;

/***************************** Bus Space/DMA **********************************/

/* XXX Need to update Bus DMA for partial map syncs */
#define ahc_dmamap_sync(ahc, dma_tag, dmamap, offset, len, op)		\
	bus_dmamap_sync(dma_tag, dmamap, offset, len, op)

/************************ Tunable Driver Parameters  **************************/
/*
 * The number of DMA segments supported.  The sequencer can handle any number
 * of physically contiguous S/G entries.  To reduce the driver's memory
 * consumption, we limit the number supported to be sufficient to handle
 * the largest mapping supported by the kernel, MAXPHYS.  Assuming the
 * transfer is as fragmented as possible and unaligned, this turns out to
 * be the number of paged sized transfers in MAXPHYS plus an extra element
 * to handle any unaligned residual.  The sequencer fetches SG elements
 * in cacheline sized chucks, so make the number per-transaction an even
 * multiple of 16 which should align us on even the largest of cacheline
 * boundaries.
 */
#define AHC_NSEG (roundup(atop(MAXPHYS) + 1, 16))

/* This driver supports target mode */
//#define AHC_TARGET_MODE 1

#include <dev/ic/aic7xxxvar.h>

/*
 * Some ISA devices (e.g. on a VLB) can perform 32-bit DMA.  This
 * flag is passed to bus_dmamap_create() to indicate that fact.
 */
#ifndef ISABUS_DMA_32BIT
#define ISABUS_DMA_32BIT	BUS_DMA_BUS1
#endif

/************************** Timer DataStructures ******************************/
typedef struct timeout ahc_timer_t;

/***************************** Core Includes **********************************/
#if AHC_REG_PRETTY_PRINT
#define AIC_DEBUG_REGISTERS 1
#else
#define AIC_DEBUG_REGISTERS 0
#endif

/***************************** Timer Facilities *******************************/
void ahc_timeout(void*);

#define ahc_timer_init callout_init
#define ahc_timer_stop callout_stop

static __inline void
ahc_timer_reset(ahc_timer_t *timer, u_int usec, ahc_callback_t *func, void *arg)
{
	callout_reset(timer, (usec * hz)/1000000, func, arg);
}

static __inline void
ahc_scb_timer_reset(struct scb *scb, u_int usec)
{
	if (!(scb->xs->xs_control & XS_CTL_POLL)) {
		callout_reset(&scb->xs->xs_callout,
			      (usec * hz)/1000000, ahc_timeout, scb);
	}
}

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
	bus_space_barrier(ahc->tag, ahc->bsh, 0, 0x100,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	ahc_inb(ahc, INTSTAT);
}

/**************************** Locking Primitives ******************************/

/****************************** OS Primitives *********************************/

/************************** Transaction Operations ****************************/
static __inline void ahc_set_transaction_status(struct scb *, uint32_t);
static __inline void ahc_set_scsi_status(struct scb *, uint32_t);
static __inline uint32_t ahc_get_transaction_status(struct scb *);
static __inline uint32_t ahc_get_scsi_status(struct scb *);
static __inline void ahc_set_transaction_tag(struct scb *, int, u_int);
static __inline u_long ahc_get_transfer_length(struct scb *);
static __inline int ahc_get_transfer_dir(struct scb *);
static __inline void ahc_set_residual(struct scb *, u_long);
static __inline void ahc_set_sense_residual(struct scb *, u_long);
static __inline u_long ahc_get_residual(struct scb *);
static __inline int ahc_perform_autosense(struct scb *);
static __inline uint32_t ahc_get_sense_bufsize(struct ahc_softc *,
    struct scb *);
static __inline void ahc_freeze_scb(struct scb *);

static __inline void
ahc_set_transaction_status(struct scb *scb, uint32_t status)
{
	scb->xs->error = status;
}

static __inline void
ahc_set_scsi_status(struct scb *scb, uint32_t status)
{
	scb->xs->status = status;
}

static __inline uint32_t
ahc_get_transaction_status(struct scb *scb)
{
	if (scb->xs->flags & ITSDONE)
		return CAM_REQ_CMP;
	else
		return scb->xs->error;
}

static __inline uint32_t
ahc_get_scsi_status(struct scb *scb)
{
	return (scb->xs->status);
}

static __inline void
ahc_set_transaction_tag(struct scb *scb, int enabled, u_int type)
{
}

static __inline u_long
ahc_get_transfer_length(struct scb *scb)
{
	return (scb->xs->datalen);
}

static __inline int
ahc_get_transfer_dir(struct scb *scb)
{
	return (scb->xs->flags & (SCSI_DATA_IN | SCSI_DATA_OUT));
}

static __inline void
ahc_set_residual(struct scb *scb, u_long resid)
{
	scb->xs->resid = resid;
}

static __inline void
ahc_set_sense_residual(struct scb *scb, u_long resid)
{
	scb->xs->resid = resid;
}

static __inline u_long
ahc_get_residual(struct scb *scb)
{
	return (scb->xs->resid);
}

static __inline int
ahc_perform_autosense(struct scb *scb)
{
	/* Return true for OpenBSD */
	return (1);
}

static __inline uint32_t
ahc_get_sense_bufsize(struct ahc_softc *ahc, struct scb *scb)
{
	return (sizeof(struct scsi_sense_data));
}

static __inline void
ahc_freeze_scb(struct scb *scb)
{
}

static __inline void
ahc_platform_scb_free(struct ahc_softc *ahc, struct scb *scb)
{
	int s;

	s = splbio();

	if ((ahc->flags & AHC_RESOURCE_SHORTAGE) != 0) {
		ahc->flags &= ~AHC_RESOURCE_SHORTAGE;
	}

	timeout_del(&scb->xs->stimeout);

	splx(s);
}

/********************************** PCI ***************************************/
#ifdef AHC_PCI_CONFIG
static __inline uint32_t ahc_pci_read_config(ahc_dev_softc_t, int, int);
static __inline void	 ahc_pci_write_config(ahc_dev_softc_t, int, uint32_t,
    int);
static __inline int	 ahc_get_pci_function(ahc_dev_softc_t);
static __inline int	 ahc_get_pci_slot(ahc_dev_softc_t);
static __inline int	 ahc_get_pci_bus(ahc_dev_softc_t);

int			 ahc_pci_map_registers(struct ahc_softc *);
int			 ahc_pci_map_int(struct ahc_softc *);

static __inline uint32_t
ahc_pci_read_config(ahc_dev_softc_t pci, int reg, int width)
{
	return (pci_conf_read(pci->pa_pc, pci->pa_tag, reg));
}

static __inline void
ahc_pci_write_config(ahc_dev_softc_t pci, int reg, uint32_t value, int width)
{
	pci_conf_write(pci->pa_pc, pci->pa_tag, reg, value);
}

static __inline int
ahc_get_pci_function(ahc_dev_softc_t pci)
{
	return (pci->pa_function);
}

static __inline int
ahc_get_pci_slot(ahc_dev_softc_t pci)
{
	return (pci->pa_device);
}

static __inline int
ahc_get_pci_bus(ahc_dev_softc_t pci)
{
	return (pci->pa_bus);
}

typedef enum
{
	AHC_POWER_STATE_D0,
	AHC_POWER_STATE_D1,
	AHC_POWER_STATE_D2,
	AHC_POWER_STATE_D3
} ahc_power_state;

void ahc_power_state_change(struct ahc_softc *, ahc_power_state);
#endif
/********************************* Debug **************************************/
static __inline void	ahc_print_path(struct ahc_softc *, struct scb *);
static __inline void	ahc_platform_dump_card_state(struct ahc_softc *);

static __inline void
ahc_print_path(struct ahc_softc *ahc, struct scb *scb)
{
	sc_print_addr(scb->xs->sc_link);
}

static __inline void
ahc_platform_dump_card_state(struct ahc_softc *ahc)
{
	/* Nothing to do here for OpenBSD */
	printf("FEATURES = 0x%x, FLAGS = 0x%x, CHIP = 0x%x BUGS =0x%x\n",
	       ahc->features, ahc->flags, ahc->chip, ahc->bugs);
}
/**************************** Transfer Settings *******************************/
void	  ahc_platform_set_tags(struct ahc_softc *, struct ahc_devinfo *, int);

/************************* Initialization/Teardown ****************************/
int	  ahc_attach(struct ahc_softc *);
int	  ahc_softc_comp(struct ahc_softc *, struct ahc_softc *);

/****************************** Interrupts ************************************/
int                     ahc_platform_intr(void *);

/************************ Misc Function Declarations **************************/
void	  ahc_done(struct ahc_softc *, struct scb *);
void	  ahc_send_async(struct ahc_softc *, char, u_int, u_int, ac_code,
    void *);
void	 *ahc_scb_alloc(void *);
void	  ahc_scb_free(void *, void *);

#endif  /* _AIC7XXX_OPENBSD_H_ */
