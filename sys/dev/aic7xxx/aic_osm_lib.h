/*-
 * FreeBSD platform specific, shared driver option settings, data structures,
 * function declarations and includes.
 *
 * Copyright (c) 1994-2001 Justin T. Gibbs.
 * Copyright (c) 2001-2003 Adaptec Inc.
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
 * $Id: //depot/aic7xxx/freebsd/dev/aic7xxx/aic_osm_lib.h#5 $
 *
 * $FreeBSD$
 */

/******************************** OS Includes *********************************/
#if __FreeBSD_version >= 500000
#include <sys/mutex.h>
#endif

/*************************** Library Symbol Mapping ***************************/
#define	AIC_LIB_ENTRY_CONCAT(x, prefix)	prefix ## x
#define	AIC_LIB_ENTRY_EXPAND(x, prefix)	AIC_LIB_ENTRY_CONCAT(x, prefix)
#define	AIC_LIB_ENTRY(x)		AIC_LIB_ENTRY_EXPAND(x, AIC_LIB_PREFIX)
#define	AIC_CONST_ENTRY(x)		AIC_LIB_ENTRY_EXPAND(x,AIC_CONST_PREFIX)

#define	aic_softc			AIC_LIB_ENTRY(_softc)
#define	aic_tailq			AIC_LIB_ENTRY(_tailq)
#define	aic_transinfo			AIC_LIB_ENTRY(_transinfo)
#define	aic_platform_data		AIC_LIB_ENTRY(_platform_data)
#define	aic_devinfo			AIC_LIB_ENTRY(_devinfo)
#define	aic_lock			AIC_LIB_ENTRY(_lock)
#define	aic_unlock			AIC_LIB_ENTRY(_unlock)
#define	aic_callback_t			AIC_LIB_ENTRY(_callback_t)
#define	aic_platform_freeze_devq	AIC_LIB_ENTRY(_platform_freeze_devq)
#define	aic_platform_abort_scbs		AIC_LIB_ENTRY(_platform_abort_scbs)
#define	aic_platform_timeout		AIC_LIB_ENTRY(_platform_timeout)
#define	aic_timeout			AIC_LIB_ENTRY(_timeout)
#define	aic_set_recoveryscb		AIC_LIB_ENTRY(_set_recoveryscb)
#define	aic_spawn_recovery_thread	AIC_LIB_ENTRY(_spawn_recovery_thread)
#define	aic_wakeup_recovery_thread	AIC_LIB_ENTRY(_wakeup_recovery_thread)
#define	aic_terminate_recovery_thread \
	AIC_LIB_ENTRY(_terminate_recovery_thread)
#define	aic_recovery_thread		AIC_LIB_ENTRY(_recovery_thread)
#define	aic_recover_commands		AIC_LIB_ENTRY(_recover_commands)
#define	aic_calc_geometry		AIC_LIB_ENTRY(_calc_geometry)

#define	AIC_RESOURCE_SHORTAGE		AIC_CONST_ENTRY(_RESOURCE_SHORTAGE)
#define	AIC_SHUTDOWN_RECOVERY		AIC_CONST_ENTRY(_SHUTDOWN_RECOVERY)

/********************************* Byte Order *********************************/
#if __FreeBSD_version >= 500000
#define aic_htobe16(x) htobe16(x)
#define aic_htobe32(x) htobe32(x)
#define aic_htobe64(x) htobe64(x)
#define aic_htole16(x) htole16(x)
#define aic_htole32(x) htole32(x)
#define aic_htole64(x) htole64(x)

#define aic_be16toh(x) be16toh(x)
#define aic_be32toh(x) be32toh(x)
#define aic_be64toh(x) be64toh(x)
#define aic_le16toh(x) le16toh(x)
#define aic_le32toh(x) le32toh(x)
#define aic_le64toh(x) le64toh(x)
#else
#define aic_htobe16(x) (x)
#define aic_htobe32(x) (x)
#define aic_htobe64(x) (x)
#define aic_htole16(x) (x)
#define aic_htole32(x) (x)
#define aic_htole64(x) (x)

#define aic_be16toh(x) (x)
#define aic_be32toh(x) (x)
#define aic_be64toh(x) (x)
#define aic_le16toh(x) (x)
#define aic_le32toh(x) (x)
#define aic_le64toh(x) (x)
#endif

/************************* Forward Declarations *******************************/
typedef device_t aic_dev_softc_t;
typedef union ccb *aic_io_ctx_t;
struct aic_softc;
struct scb;

/*************************** Timer DataStructures *****************************/
typedef struct callout aic_timer_t;

/****************************** Error Recovery ********************************/
void		aic_set_recoveryscb(struct aic_softc *aic, struct scb *scb);
timeout_t	aic_platform_timeout;
int		aic_spawn_recovery_thread(struct aic_softc *aic);
void		aic_terminate_recovery_thread(struct aic_softc *aic);

static __inline void	aic_wakeup_recovery_thread(struct aic_softc *aic);

static __inline void
aic_wakeup_recovery_thread(struct aic_softc *aic)
{
	wakeup(aic);
}

/****************************** Kernel Threads ********************************/
#if __FreeBSD_version > 500005
#if __FreeBSD_version > 800001
#define	aic_kthread_create(func, farg, proc_ptr, flags, stackpgs, fmtstr, arg) \
	kproc_create(func, farg, proc_ptr, flags, stackpgs, fmtstr, arg)
#else
#define	aic_kthread_create(func, farg, proc_ptr, flags, stackpgs, fmtstr, arg) \
	kthread_create(func, farg, proc_ptr, flags, stackpgs, fmtstr, arg)
#endif
#else
#define	aic_kthread_create(func, farg, proc_ptr, flags, stackpgs, fmtstr, arg) \
	kthread_create(func, farg, proc_ptr, fmtstr, arg)
#endif

/******************************* Bus Space/DMA ********************************/

#if __FreeBSD_version >= 501102
#define aic_dma_tag_create(aic, parent_tag, alignment, boundary,	\
			   lowaddr, highaddr, filter, filterarg,	\
			   maxsize, nsegments, maxsegsz, flags,		\
			   dma_tagp)					\
	bus_dma_tag_create(parent_tag, alignment, boundary,		\
			   lowaddr, highaddr, filter, filterarg,	\
			   maxsize, nsegments, maxsegsz, flags,		\
			   busdma_lock_mutex, &aic->platform_data->mtx,			\
			   dma_tagp)
#else
#define aic_dma_tag_create(aic, parent_tag, alignment, boundary,	\
			   lowaddr, highaddr, filter, filterarg,	\
			   maxsize, nsegments, maxsegsz, flags,		\
			   dma_tagp)					\
	bus_dma_tag_create(parent_tag, alignment, boundary,		\
			   lowaddr, highaddr, filter, filterarg,	\
			   maxsize, nsegments, maxsegsz, flags,		\
			   dma_tagp)
#endif

#define aic_dma_tag_destroy(aic, tag)					\
	bus_dma_tag_destroy(tag)

#define aic_dmamem_alloc(aic, dmat, vaddr, flags, mapp)			\
	bus_dmamem_alloc(dmat, vaddr, flags, mapp)

#define aic_dmamem_free(aic, dmat, vaddr, map)				\
	bus_dmamem_free(dmat, vaddr, map)

#define aic_dmamap_create(aic, tag, flags, mapp)			\
	bus_dmamap_create(tag, flags, mapp)

#define aic_dmamap_destroy(aic, tag, map)				\
	bus_dmamap_destroy(tag, map)

#define aic_dmamap_load(aic, dmat, map, addr, buflen, callback,		\
			callback_arg, flags)				\
	bus_dmamap_load(dmat, map, addr, buflen, callback, callback_arg, flags)

#define aic_dmamap_unload(aic, tag, map)				\
	bus_dmamap_unload(tag, map)

/* XXX Need to update Bus DMA for partial map syncs */
#define aic_dmamap_sync(aic, dma_tag, dmamap, offset, len, op)		\
	bus_dmamap_sync(dma_tag, dmamap, op)

/***************************** Core Includes **********************************/
#include AIC_CORE_INCLUDE

/***************************** Timer Facilities *******************************/
#if __FreeBSD_version >= 500000
#define aic_timer_init(timer) callout_init(timer, /*mpsafe*/1)
#else
#define aic_timer_init callout_init
#endif
#define aic_timer_stop callout_stop

static __inline void aic_timer_reset(aic_timer_t *, u_int,
				     aic_callback_t *, void *);
static __inline u_int aic_get_timeout(struct scb *);
static __inline void aic_scb_timer_reset(struct scb *, u_int);

static __inline void
aic_timer_reset(aic_timer_t *timer, u_int msec, aic_callback_t *func, void *arg)
{
	uint64_t time;

	time = msec;
	time *= hz;
	time /= 1000;
	callout_reset(timer, time, func, arg);
}

static __inline u_int
aic_get_timeout(struct scb *scb)
{
	return (scb->io_ctx->ccb_h.timeout);
}

static __inline void
aic_scb_timer_reset(struct scb *scb, u_int msec)
{
	uint64_t time;

	time = msec;
	time *= hz;
	time /= 1000;
	callout_reset(&scb->io_timer, time, aic_platform_timeout, scb);
}

static __inline void
aic_scb_timer_start(struct scb *scb)
{
	
	if (AIC_SCB_DATA(scb->aic_softc)->recovery_scbs == 0
	 && scb->io_ctx->ccb_h.timeout != CAM_TIME_INFINITY) {
		aic_scb_timer_reset(scb, scb->io_ctx->ccb_h.timeout);
	}
}

/************************** Transaction Operations ****************************/
static __inline void aic_set_transaction_status(struct scb *, uint32_t);
static __inline void aic_set_scsi_status(struct scb *, uint32_t);
static __inline uint32_t aic_get_transaction_status(struct scb *);
static __inline uint32_t aic_get_scsi_status(struct scb *);
static __inline void aic_set_transaction_tag(struct scb *, int, u_int);
static __inline u_long aic_get_transfer_length(struct scb *);
static __inline int aic_get_transfer_dir(struct scb *);
static __inline void aic_set_residual(struct scb *, u_long);
static __inline void aic_set_sense_residual(struct scb *, u_long);
static __inline u_long aic_get_residual(struct scb *);
static __inline int aic_perform_autosense(struct scb *);
static __inline uint32_t aic_get_sense_bufsize(struct aic_softc*, struct scb*);
static __inline void aic_freeze_ccb(union ccb *ccb);
static __inline void aic_freeze_scb(struct scb *scb);
static __inline void aic_platform_freeze_devq(struct aic_softc *, struct scb *);
static __inline int  aic_platform_abort_scbs(struct aic_softc *aic, int target,
					     char channel, int lun, u_int tag,
					     role_t role, uint32_t status);

static __inline
void aic_set_transaction_status(struct scb *scb, uint32_t status)
{
	scb->io_ctx->ccb_h.status &= ~CAM_STATUS_MASK;
	scb->io_ctx->ccb_h.status |= status;
}

static __inline
void aic_set_scsi_status(struct scb *scb, uint32_t status)
{
	scb->io_ctx->csio.scsi_status = status;
}

static __inline
uint32_t aic_get_transaction_status(struct scb *scb)
{
	return (scb->io_ctx->ccb_h.status & CAM_STATUS_MASK);
}

static __inline
uint32_t aic_get_scsi_status(struct scb *scb)
{
	return (scb->io_ctx->csio.scsi_status);
}

static __inline
void aic_set_transaction_tag(struct scb *scb, int enabled, u_int type)
{
	scb->io_ctx->csio.tag_action = type;
	if (enabled)
		scb->io_ctx->ccb_h.flags |= CAM_TAG_ACTION_VALID;
	else
		scb->io_ctx->ccb_h.flags &= ~CAM_TAG_ACTION_VALID;
}

static __inline
u_long aic_get_transfer_length(struct scb *scb)
{
	return (scb->io_ctx->csio.dxfer_len);
}

static __inline
int aic_get_transfer_dir(struct scb *scb)
{
	return (scb->io_ctx->ccb_h.flags & CAM_DIR_MASK);
}

static __inline
void aic_set_residual(struct scb *scb, u_long resid)
{
	scb->io_ctx->csio.resid = resid;
}

static __inline
void aic_set_sense_residual(struct scb *scb, u_long resid)
{
	scb->io_ctx->csio.sense_resid = resid;
}

static __inline
u_long aic_get_residual(struct scb *scb)
{
	return (scb->io_ctx->csio.resid);
}

static __inline
int aic_perform_autosense(struct scb *scb)
{
	return (!(scb->io_ctx->ccb_h.flags & CAM_DIS_AUTOSENSE));
}

static __inline uint32_t
aic_get_sense_bufsize(struct aic_softc *aic, struct scb *scb)
{
	return (sizeof(struct scsi_sense_data));
}

static __inline void
aic_freeze_ccb(union ccb *ccb)
{
	if ((ccb->ccb_h.status & CAM_DEV_QFRZN) == 0) {
		ccb->ccb_h.status |= CAM_DEV_QFRZN;
		xpt_freeze_devq(ccb->ccb_h.path, /*count*/1);
	}
}

static __inline void
aic_freeze_scb(struct scb *scb)
{
	aic_freeze_ccb(scb->io_ctx);
}

static __inline void
aic_platform_freeze_devq(struct aic_softc *aic, struct scb *scb)
{
	/* Nothing to do here for FreeBSD */
}

static __inline int
aic_platform_abort_scbs(struct aic_softc *aic, int target,
			char channel, int lun, u_int tag,
			role_t role, uint32_t status)
{
	/* Nothing to do here for FreeBSD */
	return (0);
}

static __inline void
aic_platform_scb_free(struct aic_softc *aic, struct scb *scb)
{
	/* What do we do to generically handle driver resource shortages??? */
	if ((aic->flags & AIC_RESOURCE_SHORTAGE) != 0
	 && scb->io_ctx != NULL
	 && (scb->io_ctx->ccb_h.status & CAM_RELEASE_SIMQ) == 0) {
		scb->io_ctx->ccb_h.status |= CAM_RELEASE_SIMQ;
		aic->flags &= ~AIC_RESOURCE_SHORTAGE;
	}
	scb->io_ctx = NULL;
}

/*************************** CAM CCB Operations *******************************/
void aic_calc_geometry(struct ccb_calc_geometry *ccg, int extended);

/****************************** OS Primitives *********************************/
#define aic_delay DELAY

/********************************** PCI ***************************************/
#ifdef AIC_PCI_CONFIG
static __inline uint32_t aic_pci_read_config(aic_dev_softc_t pci,
					     int reg, int width);
static __inline void	 aic_pci_write_config(aic_dev_softc_t pci,
					      int reg, uint32_t value,
					      int width);
static __inline int	 aic_get_pci_function(aic_dev_softc_t);
static __inline int	 aic_get_pci_slot(aic_dev_softc_t);
static __inline int	 aic_get_pci_bus(aic_dev_softc_t);


static __inline uint32_t
aic_pci_read_config(aic_dev_softc_t pci, int reg, int width)
{
	return (pci_read_config(pci, reg, width));
}

static __inline void
aic_pci_write_config(aic_dev_softc_t pci, int reg, uint32_t value, int width)
{
	pci_write_config(pci, reg, value, width);
}

static __inline int
aic_get_pci_function(aic_dev_softc_t pci)
{
	return (pci_get_function(pci));
}

static __inline int
aic_get_pci_slot(aic_dev_softc_t pci)
{
	return (pci_get_slot(pci));
}

static __inline int
aic_get_pci_bus(aic_dev_softc_t pci)
{
	return (pci_get_bus(pci));
}

typedef enum
{
	AIC_POWER_STATE_D0 = PCI_POWERSTATE_D0,
	AIC_POWER_STATE_D1 = PCI_POWERSTATE_D1,
	AIC_POWER_STATE_D2 = PCI_POWERSTATE_D2,
	AIC_POWER_STATE_D3 = PCI_POWERSTATE_D3
} aic_power_state;

static __inline int aic_power_state_change(struct aic_softc *aic,
					   aic_power_state new_state);

static __inline int
aic_power_state_change(struct aic_softc *aic, aic_power_state new_state)
{
	return (pci_set_powerstate(aic->dev_softc, new_state));
}
#endif
