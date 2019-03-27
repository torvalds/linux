/*-
 * Copyright (c) 2009-2012 Alexander Motin <mav@FreeBSD.org>
 * Copyright (c) 2017 Justin Hibbits <jhibbits@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/rman.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_debug.h>

#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include "fsl_sata.h"

struct fsl_sata_channel;
struct fsl_sata_slot;
enum fsl_sata_err_type;
struct fsl_sata_cmd_tab;


/* local prototypes */
static int fsl_sata_init(device_t dev);
static int fsl_sata_deinit(device_t dev);
static int fsl_sata_suspend(device_t dev);
static int fsl_sata_resume(device_t dev);
static void fsl_sata_pm(void *arg);
static void fsl_sata_intr(void *arg);
static void fsl_sata_intr_main(struct fsl_sata_channel *ch, uint32_t istatus);
static void fsl_sata_begin_transaction(struct fsl_sata_channel *ch, union ccb *ccb);
static void fsl_sata_dmasetprd(void *arg, bus_dma_segment_t *segs, int nsegs, int error);
static void fsl_sata_execute_transaction(struct fsl_sata_slot *slot);
static void fsl_sata_timeout(struct fsl_sata_slot *slot);
static void fsl_sata_end_transaction(struct fsl_sata_slot *slot, enum fsl_sata_err_type et);
static int fsl_sata_setup_fis(struct fsl_sata_channel *ch, struct fsl_sata_cmd_tab *ctp, union ccb *ccb, int tag);
static void fsl_sata_dmainit(device_t dev);
static void fsl_sata_dmasetupc_cb(void *xsc, bus_dma_segment_t *segs, int nsegs, int error);
static void fsl_sata_dmafini(device_t dev);
static void fsl_sata_slotsalloc(device_t dev);
static void fsl_sata_slotsfree(device_t dev);
static void fsl_sata_reset(struct fsl_sata_channel *ch);
static void fsl_sata_start(struct fsl_sata_channel *ch);
static void fsl_sata_stop(struct fsl_sata_channel *ch);

static void fsl_sata_issue_recovery(struct fsl_sata_channel *ch);
static void fsl_sata_process_read_log(struct fsl_sata_channel *ch, union ccb *ccb);
static void fsl_sata_process_request_sense(struct fsl_sata_channel *ch, union ccb *ccb);

static void fsl_sataaction(struct cam_sim *sim, union ccb *ccb);
static void fsl_satapoll(struct cam_sim *sim);

static MALLOC_DEFINE(M_FSL_SATA, "FSL SATA driver", "FSL SATA driver data buffers");

#define	recovery_type		spriv_field0
#define	RECOVERY_NONE		0
#define	RECOVERY_READ_LOG	1
#define	RECOVERY_REQUEST_SENSE	2
#define	recovery_slot		spriv_field1

#define	FSL_SATA_P_CQR		0x0
#define	FSL_SATA_P_CAR		0x4
#define	FSL_SATA_P_CCR		0x10
#define	FSL_SATA_P_CER			0x18
#define	FSL_SATA_P_DER		0x20
#define	FSL_SATA_P_CHBA		0x24
#define	FSL_SATA_P_HSTS		0x28
#define	  FSL_SATA_P_HSTS_HS_ON	  0x80000000
#define	  FSL_SATA_P_HSTS_ME	  0x00040000
#define	  FSL_SATA_P_HSTS_DLM	  0x00001000
#define	  FSL_SATA_P_HSTS_FOT	  0x00000200
#define	  FSL_SATA_P_HSTS_FOR	  0x00000100
#define	  FSL_SATA_P_HSTS_FE	  0x00000020
#define	  FSL_SATA_P_HSTS_PR	  0x00000010
#define	  FSL_SATA_P_HSTS_SNTFU	  0x00000004
#define	  FSL_SATA_P_HSTS_DE	  0x00000002
#define	FSL_SATA_P_HCTRL	0x2c
#define	  FSL_SATA_P_HCTRL_HC_ON  0x80000000
#define	  FSL_SATA_P_HCTRL_HC_FORCE_OFF  0x40000000
#define	  FSL_SATA_P_HCTRL_ENT	  0x10000000
#define	  FSL_SATA_P_HCTRL_SNOOP  0x00000400
#define	  FSL_SATA_P_HCTRL_PM	  0x00000200
#define	  FSL_SATA_P_HCTRL_FATAL  0x00000020
#define	  FSL_SATA_P_HCTRL_PHYRDY 0x00000010
#define	  FSL_SATA_P_HCTRL_SIG	  0x00000008
#define	  FSL_SATA_P_HCTRL_SNTFY  0x00000004
#define	  FSL_SATA_P_HCTRL_DE	  0x00000002
#define	  FSL_SATA_P_HCTRL_CC	  0x00000001
#define	  FSL_SATA_P_HCTRL_INT_MASK	0x0000003f
#define	FSL_SATA_P_CQPMP	0x30
#define	FSL_SATA_P_SIG		0x34
#define	FSL_SATA_P_ICC		0x38
#define	  FSL_SATA_P_ICC_ITC_M	  0x1f000000
#define	  FSL_SATA_P_ICC_ITC_S	  24
#define	  FSL_SATA_P_ICC_ITTCV_M	  0x0007ffff
#define	FSL_SATA_P_PCC		0x15c
#define	  FSL_SATA_P_PCC_SLUMBER	  0x0000000c
#define	  FSL_SATA_P_PCC_PARTIAL	  0x0000000a
#define	  FSL_SATA_PCC_LPB_EN		  0x0000000e

#define	FSL_SATA_MAX_SLOTS		16
/* FSL_SATA register defines */

#define	FSL_SATA_P_SSTS		0x100
#define	FSL_SATA_P_SERR		0x104
#define	FSL_SATA_P_SCTL		0x108
#define	FSL_SATA_P_SNTF		0x10c

/* Pessimistic prognosis on number of required S/G entries */
#define	FSL_SATA_SG_ENTRIES	63
/* Command list. 16 commands. First, 1Kbyte aligned. */
#define	FSL_SATA_CL_OFFSET	0
#define	FSL_SATA_CL_SIZE	16
/* Command tables. Up to 32 commands, Each, 4-byte aligned. */
#define	FSL_SATA_CT_OFFSET	(FSL_SATA_CL_OFFSET + FSL_SATA_CL_SIZE * FSL_SATA_MAX_SLOTS)
#define	FSL_SATA_CT_SIZE	(96 + FSL_SATA_SG_ENTRIES * 16)
/* Total main work area. */
#define	FSL_SATA_WORK_SIZE	(FSL_SATA_CT_OFFSET + FSL_SATA_CT_SIZE * FSL_SATA_MAX_SLOTS)
#define	FSL_SATA_MAX_XFER	(64 * 1024 * 1024)

/* Some convenience macros for getting the CTP and CLP */
#define	FSL_SATA_CTP_BUS(ch, slot)	\
    ((ch->dma.work_bus + FSL_SATA_CT_OFFSET + (FSL_SATA_CT_SIZE * slot->slot)))
#define FSL_SATA_PRD_OFFSET(prd) (96 + (prd) * 16)
#define	FSL_SATA_CTP(ch, slot)		\
    ((struct fsl_sata_cmd_tab *)(ch->dma.work + FSL_SATA_CT_OFFSET + \
     (FSL_SATA_CT_SIZE * slot->slot)))
#define	FSL_SATA_CLP(ch, slot)		\
	((struct fsl_sata_cmd_list *) (ch->dma.work + FSL_SATA_CL_OFFSET + \
	 (FSL_SATA_CL_SIZE * slot->slot)))

struct fsl_sata_dma_prd {
	uint32_t		dba;
	uint32_t		reserved;
	uint32_t		reserved2;
	uint32_t		dwc_flg;		/* 0 based */
#define	FSL_SATA_PRD_MASK		0x01fffffc	/* max 32MB */
#define	FSL_SATA_PRD_MAX		(FSL_SATA_PRD_MASK + 4)
#define	FSL_SATA_PRD_SNOOP		0x10000000
#define	FSL_SATA_PRD_EXT		0x80000000
} __packed;

struct fsl_sata_cmd_tab {
	uint8_t			cfis[32];
	uint8_t			sfis[32];
	uint8_t			acmd[16];
	uint8_t			reserved[16];
	struct fsl_sata_dma_prd	prd_tab[FSL_SATA_SG_ENTRIES];
#define	FSL_SATA_PRD_EXT_INDEX	15
#define FSL_SATA_PRD_MAX_DIRECT	16
} __packed;

struct fsl_sata_cmd_list {
	uint32_t			cda;		/* word aligned */
	uint16_t			fis_length;	/* length in bytes (aligned to words) */
	uint16_t			prd_length;	/* PRD entries */
	uint32_t			ttl;
	uint32_t			cmd_flags;
#define	FSL_SATA_CMD_TAG_MASK		0x001f
#define	FSL_SATA_CMD_ATAPI		0x0020
#define	FSL_SATA_CMD_BIST		0x0040
#define	FSL_SATA_CMD_RESET		0x0080
#define	FSL_SATA_CMD_QUEUED		0x0100
#define	FSL_SATA_CMD_SNOOP		0x0200
#define	FSL_SATA_CMD_VBIST		0x0400
#define	FSL_SATA_CMD_WRITE		0x0800

} __packed;

/* misc defines */
#define	ATA_IRQ_RID		0
#define	ATA_INTR_FLAGS		(INTR_MPSAFE|INTR_TYPE_BIO|INTR_ENTROPY)

struct ata_dmaslot {
	bus_dmamap_t		data_map;	/* data DMA map */
	int			nsegs;		/* Number of segs loaded */
};

/* structure holding DMA related information */
struct ata_dma {
	bus_dma_tag_t		 work_tag;	/* workspace DMA tag */
	bus_dmamap_t		 work_map;	/* workspace DMA map */
	uint8_t			*work;		/* workspace */
	bus_addr_t		 work_bus;	/* bus address of work */
	bus_dma_tag_t		data_tag;	/* data DMA tag */
};

enum fsl_sata_slot_states {
	FSL_SATA_SLOT_EMPTY,
	FSL_SATA_SLOT_LOADING,
	FSL_SATA_SLOT_RUNNING,
	FSL_SATA_SLOT_EXECUTING
};

struct fsl_sata_slot {
	struct fsl_sata_channel		*ch;		/* Channel */
	uint8_t				 slot;		/* Number of this slot */
	enum fsl_sata_slot_states	 state;	/* Slot state */
	union ccb			*ccb;		/* CCB occupying slot */
	struct ata_dmaslot		 dma;	/* DMA data of this slot */
	struct callout			 timeout;	/* Execution timeout */
	uint32_t			 ttl;
};

struct fsl_sata_device {
	int			revision;
	int			mode;
	u_int			bytecount;
	u_int			atapi;
	u_int			tags;
	u_int			caps;
};

/* structure describing an ATA channel */
struct fsl_sata_channel {
	device_t		dev;		/* Device handle */
	int			unit;		/* Physical channel */
	struct resource		*r_mem;		/* Memory of this channel */
	struct resource		*r_irq;		/* Interrupt of this channel */
	void			*ih;		/* Interrupt handle */
	struct ata_dma		dma;		/* DMA data */
	struct cam_sim		*sim;
	struct cam_path		*path;
	uint32_t		caps;		/* Controller capabilities */
	int			pm_level;	/* power management level */
	int			devices;	/* What is present */
	int			pm_present;	/* PM presence reported */

	union ccb		*hold[FSL_SATA_MAX_SLOTS];
	struct fsl_sata_slot	slot[FSL_SATA_MAX_SLOTS];
	uint32_t		oslots;		/* Occupied slots */
	uint32_t		rslots;		/* Running slots */
	uint32_t		aslots;		/* Slots with atomic commands  */
	uint32_t		eslots;		/* Slots in error */
	uint32_t		toslots;	/* Slots in timeout */
	int			lastslot;	/* Last used slot */
	int			taggedtarget;	/* Last tagged target */
	int			numrslots;	/* Number of running slots */
	int			numrslotspd[16];/* Number of running slots per dev */
	int			numtslots;	/* Number of tagged slots */
	int			numtslotspd[16];/* Number of tagged slots per dev */
	int			numhslots;	/* Number of held slots */
	int			recoverycmd;	/* Our READ LOG active */
	int			fatalerr;	/* Fatal error happend */
	int			resetting;	/* Hard-reset in progress. */
	int			resetpolldiv;	/* Hard-reset poll divider. */
	union ccb		*frozen;	/* Frozen command */
	struct callout		pm_timer;	/* Power management events */
	struct callout		reset_timer;	/* Hard-reset timeout */

	struct fsl_sata_device	user[16];	/* User-specified settings */
	struct fsl_sata_device	curr[16];	/* Current settings */

	struct mtx_padalign	mtx;		/* state lock */
	STAILQ_HEAD(, ccb_hdr)	doneq;		/* queue of completed CCBs */
	int			batch;		/* doneq is in use */
};

enum fsl_sata_err_type {
	FSL_SATA_ERR_NONE,		/* No error */
	FSL_SATA_ERR_INVALID,	/* Error detected by us before submitting. */
	FSL_SATA_ERR_INNOCENT,	/* Innocent victim. */
	FSL_SATA_ERR_TFE,		/* Task File Error. */
	FSL_SATA_ERR_SATA,		/* SATA error. */
	FSL_SATA_ERR_TIMEOUT,	/* Command execution timeout. */
	FSL_SATA_ERR_NCQ,		/* NCQ command error. CCB should be put on hold
				 * until READ LOG executed to reveal error. */
};

/* macros to hide busspace uglyness */
#define	ATA_INB(res, offset) \
	bus_read_1((res), (offset))
#define	ATA_INW(res, offset) \
	bus_read_2((res), (offset))
#define	ATA_INL(res, offset) \
	bus_read_4((res), (offset))
#define	ATA_INSW(res, offset, addr, count) \
	bus_read_multi_2((res), (offset), (addr), (count))
#define	ATA_INSW_STRM(res, offset, addr, count) \
	bus_read_multi_stream_2((res), (offset), (addr), (count))
#define	ATA_INSL(res, offset, addr, count) \
	bus_read_multi_4((res), (offset), (addr), (count))
#define	ATA_INSL_STRM(res, offset, addr, count) \
	bus_read_multi_stream_4((res), (offset), (addr), (count))
#define	ATA_OUTB(res, offset, value) \
	bus_write_1((res), (offset), (value))
#define	ATA_OUTW(res, offset, value) \
	bus_write_2((res), (offset), (value))
#define	ATA_OUTL(res, offset, value) \
	bus_write_4((res), (offset), (value))
#define	ATA_OUTSW(res, offset, addr, count) \
	bus_write_multi_2((res), (offset), (addr), (count))
#define	ATA_OUTSW_STRM(res, offset, addr, count) \
	bus_write_multi_stream_2((res), (offset), (addr), (count))
#define	ATA_OUTSL(res, offset, addr, count) \
	bus_write_multi_4((res), (offset), (addr), (count))
#define	ATA_OUTSL_STRM(res, offset, addr, count) \
	bus_write_multi_stream_4((res), (offset), (addr), (count))

static int
fsl_sata_probe(device_t dev)
{

	if (!ofw_bus_is_compatible(dev, "fsl,pq-sata-v2") &&
	    !ofw_bus_is_compatible(dev, "fsl,pq-sata"))
		return (ENXIO);

	device_set_desc_copy(dev, "Freescale Integrated SATA Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
fsl_sata_attach(device_t dev)
{
	struct fsl_sata_channel *ch = device_get_softc(dev);
	struct cam_devq *devq;
	int rid, error, i, sata_rev = 0;

	ch->dev = dev;
	ch->unit = (intptr_t)device_get_ivars(dev);
	mtx_init(&ch->mtx, "FSL SATA channel lock", NULL, MTX_DEF);
	ch->pm_level = 0;
	resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "pm_level", &ch->pm_level);
	STAILQ_INIT(&ch->doneq);
	if (ch->pm_level > 3)
		callout_init_mtx(&ch->pm_timer, &ch->mtx, 0);
	resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "sata_rev", &sata_rev);
	for (i = 0; i < 16; i++) {
		ch->user[i].revision = sata_rev;
		ch->user[i].mode = 0;
		ch->user[i].bytecount = 8192;
		ch->user[i].tags = FSL_SATA_MAX_SLOTS;
		ch->user[i].caps = 0;
		ch->curr[i] = ch->user[i];
		if (ch->pm_level) {
			ch->user[i].caps = CTS_SATA_CAPS_H_PMREQ |
			    CTS_SATA_CAPS_D_PMREQ;
		}
		ch->user[i].caps |= CTS_SATA_CAPS_H_AN;
	}
	rid = 0;
	if (!(ch->r_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &rid, RF_ACTIVE)))
		return (ENXIO);
	rman_set_bustag(ch->r_mem, &bs_le_tag);
	fsl_sata_dmainit(dev);
	fsl_sata_slotsalloc(dev);
	fsl_sata_init(dev);
	rid = ATA_IRQ_RID;
	if (!(ch->r_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &rid, RF_SHAREABLE | RF_ACTIVE))) {
		device_printf(dev, "Unable to map interrupt\n");
		error = ENXIO;
		goto err0;
	}
	if ((bus_setup_intr(dev, ch->r_irq, ATA_INTR_FLAGS, NULL,
	    fsl_sata_intr, ch, &ch->ih))) {
		device_printf(dev, "Unable to setup interrupt\n");
		error = ENXIO;
		goto err1;
	}
	mtx_lock(&ch->mtx);
	/* Create the device queue for our SIM. */
	devq = cam_simq_alloc(FSL_SATA_MAX_SLOTS);
	if (devq == NULL) {
		device_printf(dev, "Unable to allocate simq\n");
		error = ENOMEM;
		goto err1;
	}
	/* Construct SIM entry */
	ch->sim = cam_sim_alloc(fsl_sataaction, fsl_satapoll, "fslsata", ch,
	    device_get_unit(dev), (struct mtx *)&ch->mtx, 2, FSL_SATA_MAX_SLOTS,
	    devq);
	if (ch->sim == NULL) {
		cam_simq_free(devq);
		device_printf(dev, "unable to allocate sim\n");
		error = ENOMEM;
		goto err1;
	}
	if (xpt_bus_register(ch->sim, dev, 0) != CAM_SUCCESS) {
		device_printf(dev, "unable to register xpt bus\n");
		error = ENXIO;
		goto err2;
	}
	if (xpt_create_path(&ch->path, /*periph*/NULL, cam_sim_path(ch->sim),
	    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		device_printf(dev, "unable to create path\n");
		error = ENXIO;
		goto err3;
	}
	if (ch->pm_level > 3) {
		callout_reset(&ch->pm_timer,
		    (ch->pm_level == 4) ? hz / 1000 : hz / 8,
		    fsl_sata_pm, ch);
	}
	mtx_unlock(&ch->mtx);
	return (0);

err3:
	xpt_bus_deregister(cam_sim_path(ch->sim));
err2:
	cam_sim_free(ch->sim, /*free_devq*/TRUE);
err1:
	mtx_unlock(&ch->mtx);
	bus_release_resource(dev, SYS_RES_IRQ, ATA_IRQ_RID, ch->r_irq);
err0:
	bus_release_resource(dev, SYS_RES_MEMORY, ch->unit, ch->r_mem);
	mtx_destroy(&ch->mtx);
	return (error);
}

static int
fsl_sata_detach(device_t dev)
{
	struct fsl_sata_channel *ch = device_get_softc(dev);

	mtx_lock(&ch->mtx);
	xpt_async(AC_LOST_DEVICE, ch->path, NULL);

	xpt_free_path(ch->path);
	xpt_bus_deregister(cam_sim_path(ch->sim));
	cam_sim_free(ch->sim, /*free_devq*/TRUE);
	mtx_unlock(&ch->mtx);

	if (ch->pm_level > 3)
		callout_drain(&ch->pm_timer);
	bus_teardown_intr(dev, ch->r_irq, ch->ih);
	bus_release_resource(dev, SYS_RES_IRQ, ATA_IRQ_RID, ch->r_irq);

	fsl_sata_deinit(dev);
	fsl_sata_slotsfree(dev);
	fsl_sata_dmafini(dev);

	bus_release_resource(dev, SYS_RES_MEMORY, ch->unit, ch->r_mem);
	mtx_destroy(&ch->mtx);
	return (0);
}

static int
fsl_sata_wait_register(struct fsl_sata_channel *ch, bus_size_t off,
    unsigned int mask, unsigned int val, int t)
{
	int timeout = 0;
	uint32_t rval;

	while (((rval = ATA_INL(ch->r_mem, off)) & mask) != val) {
		if (timeout > t) {
			return (EBUSY);
		}
		DELAY(1000);
		timeout++;
	}
	return (0);
}

static int
fsl_sata_init(device_t dev)
{
	struct fsl_sata_channel *ch = device_get_softc(dev);
	uint64_t work;
	uint32_t r;

	/* Disable port interrupts */
	r = ATA_INL(ch->r_mem, FSL_SATA_P_HCTRL);
	r &= ~FSL_SATA_P_HCTRL_HC_ON;
	r |= FSL_SATA_P_HCTRL_HC_FORCE_OFF;
	ATA_OUTL(ch->r_mem, FSL_SATA_P_HCTRL, r & ~FSL_SATA_P_HCTRL_INT_MASK);
	fsl_sata_wait_register(ch, FSL_SATA_P_HSTS,
	    FSL_SATA_P_HSTS_HS_ON, 0, 1000);
	/* Setup work areas */
	work = ch->dma.work_bus + FSL_SATA_CL_OFFSET;
	ATA_OUTL(ch->r_mem, FSL_SATA_P_CHBA, work);
	r &= ~FSL_SATA_P_HCTRL_ENT;
	r &= ~FSL_SATA_P_HCTRL_PM;
	ATA_OUTL(ch->r_mem, FSL_SATA_P_HCTRL, r);
	r = ATA_INL(ch->r_mem, FSL_SATA_P_PCC);
	ATA_OUTL(ch->r_mem, FSL_SATA_P_PCC, r & ~FSL_SATA_PCC_LPB_EN);
	ATA_OUTL(ch->r_mem, FSL_SATA_P_ICC, (1 << FSL_SATA_P_ICC_ITC_S));
	fsl_sata_start(ch);
	return (0);
}

static int
fsl_sata_deinit(device_t dev)
{
	struct fsl_sata_channel *ch = device_get_softc(dev);
	uint32_t r;

	/* Disable port interrupts. */
	r = ATA_INL(ch->r_mem, FSL_SATA_P_HCTRL);
	ATA_OUTL(ch->r_mem, FSL_SATA_P_HCTRL, r & ~FSL_SATA_P_HCTRL_INT_MASK);
	/* Reset command register. */
	fsl_sata_stop(ch);
	/* Allow everything, including partial and slumber modes. */
	ATA_OUTL(ch->r_mem, FSL_SATA_P_SCTL, 0);
	DELAY(100);
	/* Disable PHY. */
	ATA_OUTL(ch->r_mem, FSL_SATA_P_SCTL, ATA_SC_DET_DISABLE);
	r = ATA_INL(ch->r_mem, FSL_SATA_P_HCTRL);
	/* Turn off the controller. */
	ATA_OUTL(ch->r_mem, FSL_SATA_P_HCTRL, r & ~FSL_SATA_P_HCTRL_HC_ON);
	return (0);
}

static int
fsl_sata_suspend(device_t dev)
{
	struct fsl_sata_channel *ch = device_get_softc(dev);

	mtx_lock(&ch->mtx);
	xpt_freeze_simq(ch->sim, 1);
	while (ch->oslots)
		msleep(ch, &ch->mtx, PRIBIO, "fsl_satasusp", hz/100);
	fsl_sata_deinit(dev);
	mtx_unlock(&ch->mtx);
	return (0);
}

static int
fsl_sata_resume(device_t dev)
{
	struct fsl_sata_channel *ch = device_get_softc(dev);

	mtx_lock(&ch->mtx);
	fsl_sata_init(dev);
	fsl_sata_reset(ch);
	xpt_release_simq(ch->sim, TRUE);
	mtx_unlock(&ch->mtx);
	return (0);
}

devclass_t fsl_satach_devclass;
static device_method_t fsl_satach_methods[] = {
	DEVMETHOD(device_probe,     fsl_sata_probe),
	DEVMETHOD(device_attach,    fsl_sata_attach),
	DEVMETHOD(device_detach,    fsl_sata_detach),
	DEVMETHOD(device_suspend,   fsl_sata_suspend),
	DEVMETHOD(device_resume,    fsl_sata_resume),
	DEVMETHOD_END
};
static driver_t fsl_satach_driver = {
	"fslsata",
	fsl_satach_methods,
	sizeof(struct fsl_sata_channel)
};
DRIVER_MODULE(fsl_satach, simplebus, fsl_satach_driver, fsl_satach_devclass, NULL, NULL);

struct fsl_sata_dc_cb_args {
	bus_addr_t maddr;
	int error;
};

static void
fsl_sata_dmainit(device_t dev)
{
	struct fsl_sata_channel *ch = device_get_softc(dev);
	struct fsl_sata_dc_cb_args dcba;

	/* Command area. */
	if (bus_dma_tag_create(bus_get_dma_tag(dev), 1024, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
	    NULL, NULL, FSL_SATA_WORK_SIZE, 1, FSL_SATA_WORK_SIZE,
	    0, NULL, NULL, &ch->dma.work_tag))
		goto error;
	if (bus_dmamem_alloc(ch->dma.work_tag, (void **)&ch->dma.work,
	    BUS_DMA_ZERO, &ch->dma.work_map))
		goto error;
	if (bus_dmamap_load(ch->dma.work_tag, ch->dma.work_map, ch->dma.work,
	    FSL_SATA_WORK_SIZE, fsl_sata_dmasetupc_cb, &dcba, 0) || dcba.error) {
		bus_dmamem_free(ch->dma.work_tag, ch->dma.work, ch->dma.work_map);
		goto error;
	}
	ch->dma.work_bus = dcba.maddr;
	/* Data area. */
	if (bus_dma_tag_create(bus_get_dma_tag(dev), 4, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
	    NULL, NULL, FSL_SATA_MAX_XFER,
	    FSL_SATA_SG_ENTRIES - 1, FSL_SATA_PRD_MAX,
	    0, busdma_lock_mutex, &ch->mtx, &ch->dma.data_tag)) {
		goto error;
	}
	if (bootverbose)
		device_printf(dev, "work area: %p\n", ch->dma.work);
	return;

error:
	device_printf(dev, "WARNING - DMA initialization failed\n");
	fsl_sata_dmafini(dev);
}

static void
fsl_sata_dmasetupc_cb(void *xsc, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct fsl_sata_dc_cb_args *dcba = (struct fsl_sata_dc_cb_args *)xsc;

	if (!(dcba->error = error))
		dcba->maddr = segs[0].ds_addr;
}

static void
fsl_sata_dmafini(device_t dev)
{
	struct fsl_sata_channel *ch = device_get_softc(dev);

	if (ch->dma.data_tag) {
		bus_dma_tag_destroy(ch->dma.data_tag);
		ch->dma.data_tag = NULL;
	}
	if (ch->dma.work_bus) {
		bus_dmamap_unload(ch->dma.work_tag, ch->dma.work_map);
		bus_dmamem_free(ch->dma.work_tag, ch->dma.work, ch->dma.work_map);
		ch->dma.work_bus = 0;
		ch->dma.work = NULL;
	}
	if (ch->dma.work_tag) {
		bus_dma_tag_destroy(ch->dma.work_tag);
		ch->dma.work_tag = NULL;
	}
}

static void
fsl_sata_slotsalloc(device_t dev)
{
	struct fsl_sata_channel *ch = device_get_softc(dev);
	int i;

	/* Alloc and setup command/dma slots */
	bzero(ch->slot, sizeof(ch->slot));
	for (i = 0; i < FSL_SATA_MAX_SLOTS; i++) {
		struct fsl_sata_slot *slot = &ch->slot[i];

		slot->ch = ch;
		slot->slot = i;
		slot->state = FSL_SATA_SLOT_EMPTY;
		slot->ccb = NULL;
		callout_init_mtx(&slot->timeout, &ch->mtx, 0);

		if (bus_dmamap_create(ch->dma.data_tag, 0, &slot->dma.data_map))
			device_printf(ch->dev, "FAILURE - create data_map\n");
	}
}

static void
fsl_sata_slotsfree(device_t dev)
{
	struct fsl_sata_channel *ch = device_get_softc(dev);
	int i;

	/* Free all dma slots */
	for (i = 0; i < FSL_SATA_MAX_SLOTS; i++) {
		struct fsl_sata_slot *slot = &ch->slot[i];

		callout_drain(&slot->timeout);
		if (slot->dma.data_map) {
			bus_dmamap_destroy(ch->dma.data_tag, slot->dma.data_map);
			slot->dma.data_map = NULL;
		}
	}
}

static int
fsl_sata_phy_check_events(struct fsl_sata_channel *ch, u_int32_t serr)
{

	if (((ch->pm_level == 0) && (serr & ATA_SE_PHY_CHANGED)) ||
	    ((ch->pm_level != 0) && (serr & ATA_SE_EXCHANGED))) {
		u_int32_t status = ATA_INL(ch->r_mem, FSL_SATA_P_SSTS);
		union ccb *ccb;

		if (bootverbose) {
			if ((status & ATA_SS_DET_MASK) != ATA_SS_DET_NO_DEVICE)
				device_printf(ch->dev, "CONNECT requested\n");
			else
				device_printf(ch->dev, "DISCONNECT requested\n");
		}
		/* Issue soft reset */
		xpt_async(AC_BUS_RESET, ch->path, NULL);
		if ((ccb = xpt_alloc_ccb_nowait()) == NULL)
			return (0);
		if (xpt_create_path(&ccb->ccb_h.path, NULL,
		    cam_sim_path(ch->sim),
		    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
			xpt_free_ccb(ccb);
			return (0);
		}
		xpt_rescan(ccb);
		return (1);
	}
	return (0);
}

static void
fsl_sata_notify_events(struct fsl_sata_channel *ch, u_int32_t status)
{
	struct cam_path *dpath;
	int i;

	ATA_OUTL(ch->r_mem, FSL_SATA_P_SNTF, status);
	if (bootverbose)
		device_printf(ch->dev, "SNTF 0x%04x\n", status);
	for (i = 0; i < 16; i++) {
		if ((status & (1 << i)) == 0)
			continue;
		if (xpt_create_path(&dpath, NULL,
		    xpt_path_path_id(ch->path), i, 0) == CAM_REQ_CMP) {
			xpt_async(AC_SCSI_AEN, dpath, NULL);
			xpt_free_path(dpath);
		}
	}
}

static void
fsl_sata_done(struct fsl_sata_channel *ch, union ccb *ccb)
{

	mtx_assert(&ch->mtx, MA_OWNED);
	if ((ccb->ccb_h.func_code & XPT_FC_QUEUED) == 0 ||
	    ch->batch == 0) {
		xpt_done(ccb);
		return;
	}

	STAILQ_INSERT_TAIL(&ch->doneq, &ccb->ccb_h, sim_links.stqe);
}

static void
fsl_sata_intr(void *arg)
{
	struct fsl_sata_channel *ch = (struct fsl_sata_channel *)arg;
	struct ccb_hdr *ccb_h;
	uint32_t istatus;
	STAILQ_HEAD(, ccb_hdr) tmp_doneq = STAILQ_HEAD_INITIALIZER(tmp_doneq);

	/* Read interrupt statuses. */
	istatus = ATA_INL(ch->r_mem, FSL_SATA_P_HSTS) & 0x7ffff;
	if ((istatus & 0x3f) == 0)
		return;

	mtx_lock(&ch->mtx);
	ch->batch = 1;
	fsl_sata_intr_main(ch, istatus);
	ch->batch = 0;
	/*
	 * Prevent the possibility of issues caused by processing the queue
	 * while unlocked below by moving the contents to a local queue.
	 */
	STAILQ_CONCAT(&tmp_doneq, &ch->doneq);
	mtx_unlock(&ch->mtx);
	while ((ccb_h = STAILQ_FIRST(&tmp_doneq)) != NULL) {
		STAILQ_REMOVE_HEAD(&tmp_doneq, sim_links.stqe);
		xpt_done_direct((union ccb *)ccb_h);
	}
	/* Clear interrupt statuses. */
	ATA_OUTL(ch->r_mem, FSL_SATA_P_HSTS, istatus & 0x3f);

}

static void
fsl_sata_pm(void *arg)
{
	struct fsl_sata_channel *ch = (struct fsl_sata_channel *)arg;
	uint32_t work;

	if (ch->numrslots != 0)
		return;
	work = ATA_INL(ch->r_mem, FSL_SATA_P_PCC) & ~FSL_SATA_PCC_LPB_EN;
	if (ch->pm_level == 4)
		work |= FSL_SATA_P_PCC_PARTIAL;
	else
		work |= FSL_SATA_P_PCC_SLUMBER;
	ATA_OUTL(ch->r_mem, FSL_SATA_P_PCC, work);
}

/* XXX: interrupt todo */
static void
fsl_sata_intr_main(struct fsl_sata_channel *ch, uint32_t istatus)
{
	uint32_t cer, der, serr = 0, sntf = 0, ok, err;
	enum fsl_sata_err_type et;
	int i;

	/* Complete all successful commands. */
	ok = ATA_INL(ch->r_mem, FSL_SATA_P_CCR);
	/* Mark all commands complete, to complete the interrupt. */
	ATA_OUTL(ch->r_mem, FSL_SATA_P_CCR, ok);
	if (ch->aslots == 0 && ok != 0) {
		for (i = 0; i < FSL_SATA_MAX_SLOTS; i++) {
			if (((ok >> i) & 1) && ch->slot[i].ccb != NULL)
				fsl_sata_end_transaction(&ch->slot[i],
				    FSL_SATA_ERR_NONE);
		}
	}
	/* Read command statuses. */
	if (istatus & FSL_SATA_P_HSTS_SNTFU)
		sntf = ATA_INL(ch->r_mem, FSL_SATA_P_SNTF);
	/* XXX: Process PHY events */
	serr = ATA_INL(ch->r_mem, FSL_SATA_P_SERR);
	ATA_OUTL(ch->r_mem, FSL_SATA_P_SERR, serr);
	if (istatus & (FSL_SATA_P_HSTS_PR)) {
		if (serr) {
			fsl_sata_phy_check_events(ch, serr);
		}
	}
	/* Process command errors */
	err = (istatus & (FSL_SATA_P_HSTS_FE | FSL_SATA_P_HSTS_DE));
	cer = ATA_INL(ch->r_mem, FSL_SATA_P_CER);
	ATA_OUTL(ch->r_mem, FSL_SATA_P_CER, cer);
	der = ATA_INL(ch->r_mem, FSL_SATA_P_DER);
	ATA_OUTL(ch->r_mem, FSL_SATA_P_DER, der);
	/* On error, complete the rest of commands with error statuses. */
	if (err) {
		if (ch->frozen) {
			union ccb *fccb = ch->frozen;
			ch->frozen = NULL;
			fccb->ccb_h.status = CAM_REQUEUE_REQ | CAM_RELEASE_SIMQ;
			if (!(fccb->ccb_h.status & CAM_DEV_QFRZN)) {
				xpt_freeze_devq(fccb->ccb_h.path, 1);
				fccb->ccb_h.status |= CAM_DEV_QFRZN;
			}
			fsl_sata_done(ch, fccb);
		}
		for (i = 0; i < FSL_SATA_MAX_SLOTS; i++) {
			if (ch->slot[i].ccb == NULL)
				continue;
			if ((cer & (1 << i)) != 0)
				et = FSL_SATA_ERR_TFE;
			else if ((der & (1 << ch->slot[i].ccb->ccb_h.target_id)) != 0)
				et = FSL_SATA_ERR_SATA;
			else
				et = FSL_SATA_ERR_INVALID;
			fsl_sata_end_transaction(&ch->slot[i], et);
		}
	}
	/* Process NOTIFY events */
	if (sntf)
		fsl_sata_notify_events(ch, sntf);
}

/* Must be called with channel locked. */
static int
fsl_sata_check_collision(struct fsl_sata_channel *ch, union ccb *ccb)
{
	int t = ccb->ccb_h.target_id;

	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA)) {
		/* Tagged command while we have no supported tag free. */
		if (((~ch->oslots) & (0xffff >> (16 - ch->curr[t].tags))) == 0)
			return (1);
		/* Tagged command while untagged are active. */
		if (ch->numrslotspd[t] != 0 && ch->numtslotspd[t] == 0)
			return (1);
	} else {
		/* Untagged command while tagged are active. */
		if (ch->numrslotspd[t] != 0 && ch->numtslotspd[t] != 0)
			return (1);
	}
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & (CAM_ATAIO_CONTROL | CAM_ATAIO_NEEDRESULT))) {
		/* Atomic command while anything active. */
		if (ch->numrslots != 0)
			return (1);
	}
	/* We have some atomic command running. */
	if (ch->aslots != 0)
		return (1);
	return (0);
}

/* Must be called with channel locked. */
static void
fsl_sata_begin_transaction(struct fsl_sata_channel *ch, union ccb *ccb)
{
	struct fsl_sata_slot *slot;
	int tag, tags;

	CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE,
	    ("fsl_sata_begin_transaction func_code=0x%x\n", ccb->ccb_h.func_code));
	/* Choose empty slot. */
	tags = FSL_SATA_MAX_SLOTS;
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA))
		tags = ch->curr[ccb->ccb_h.target_id].tags;
	if (ch->lastslot + 1 < tags)
		tag = ffs(~(ch->oslots >> (ch->lastslot + 1)));
	else
		tag = 0;
	if (tag == 0 || tag + ch->lastslot >= tags)
		tag = ffs(~ch->oslots) - 1;
	else
		tag += ch->lastslot;
	ch->lastslot = tag;
	/* Occupy chosen slot. */
	slot = &ch->slot[tag];
	slot->ccb = ccb;
	slot->ttl = 0;
	/* Stop PM timer. */
	if (ch->numrslots == 0 && ch->pm_level > 3)
		callout_stop(&ch->pm_timer);
	/* Update channel stats. */
	ch->oslots |= (1 << tag);
	ch->numrslots++;
	ch->numrslotspd[ccb->ccb_h.target_id]++;
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA)) {
		ch->numtslots++;
		ch->numtslotspd[ccb->ccb_h.target_id]++;
		ch->taggedtarget = ccb->ccb_h.target_id;
	}
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & (CAM_ATAIO_CONTROL | CAM_ATAIO_NEEDRESULT)))
		ch->aslots |= (1 << tag);
	if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
		slot->state = FSL_SATA_SLOT_LOADING;
		bus_dmamap_load_ccb(ch->dma.data_tag, slot->dma.data_map, ccb,
		    fsl_sata_dmasetprd, slot, 0);
	} else {
		slot->dma.nsegs = 0;
		fsl_sata_execute_transaction(slot);
	}

	CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE,
	    ("fsl_sata_begin_transaction exit\n"));
}

/* Locked by busdma engine. */
static void
fsl_sata_dmasetprd(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{    
	struct fsl_sata_slot *slot = arg;
	struct fsl_sata_channel *ch = slot->ch;
	struct fsl_sata_cmd_tab *ctp;
	struct fsl_sata_dma_prd *prd;
	int i, j, len, extlen;

	if (error) {
		device_printf(ch->dev, "DMA load error %d\n", error);
		fsl_sata_end_transaction(slot, FSL_SATA_ERR_INVALID);
		return;
	}
	KASSERT(nsegs <= FSL_SATA_SG_ENTRIES - 1,
	    ("too many DMA segment entries\n"));
	/* Get a piece of the workspace for this request */
	ctp = FSL_SATA_CTP(ch, slot);
	/* Fill S/G table */
	prd = &ctp->prd_tab[0];
	for (i = 0, j = 0; i < nsegs; i++, j++) {
		if (j == FSL_SATA_PRD_EXT_INDEX &&
		    FSL_SATA_PRD_MAX_DIRECT < nsegs) {
			prd[j].dba = htole32(FSL_SATA_CTP_BUS(ch, slot) +
				     FSL_SATA_PRD_OFFSET(j+1));
			j++;
			extlen = 0;
		}
		len = segs[i].ds_len;
		len = roundup2(len, sizeof(uint32_t));
		prd[j].dba = htole32((uint32_t)segs[i].ds_addr);
		prd[j].dwc_flg = htole32(FSL_SATA_PRD_SNOOP | len);
		slot->ttl += len;
		if (j > FSL_SATA_PRD_MAX_DIRECT)
			extlen += len;
	}
	slot->dma.nsegs = j;
	if (j > FSL_SATA_PRD_MAX_DIRECT)
		prd[FSL_SATA_PRD_EXT_INDEX].dwc_flg = 
		    htole32(FSL_SATA_PRD_SNOOP | FSL_SATA_PRD_EXT | extlen);
	bus_dmamap_sync(ch->dma.data_tag, slot->dma.data_map,
	    ((slot->ccb->ccb_h.flags & CAM_DIR_IN) ?
	    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE));
	fsl_sata_execute_transaction(slot);
}

/* Must be called with channel locked. */
static void
fsl_sata_execute_transaction(struct fsl_sata_slot *slot)
{
	struct fsl_sata_channel *ch = slot->ch;
	struct fsl_sata_cmd_tab *ctp;
	struct fsl_sata_cmd_list *clp;
	union ccb *ccb = slot->ccb;
	int port = ccb->ccb_h.target_id & 0x0f;
	int fis_size, i, softreset;
	uint32_t tmp;
	uint32_t cmd_flags = FSL_SATA_CMD_WRITE | FSL_SATA_CMD_SNOOP;

	softreset = 0;
	CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE,
	    ("fsl_sata_execute_transaction func_code=0x%x\n", ccb->ccb_h.func_code));
	/* Get a piece of the workspace for this request */
	ctp = FSL_SATA_CTP(ch, slot);
	/* Setup the FIS for this request */
	if (!(fis_size = fsl_sata_setup_fis(ch, ctp, ccb, slot->slot))) {
		device_printf(ch->dev, "Setting up SATA FIS failed\n");
		fsl_sata_end_transaction(slot, FSL_SATA_ERR_INVALID);
		return;
	}
	/* Setup the command list entry */
	clp = FSL_SATA_CLP(ch, slot);
	clp->fis_length = htole16(fis_size);
	clp->prd_length = htole16(slot->dma.nsegs);
	/* Special handling for Soft Reset command. */
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & CAM_ATAIO_CONTROL)) {
		if (ccb->ataio.cmd.control & ATA_A_RESET) {
			softreset = 1;
			cmd_flags |= FSL_SATA_CMD_RESET;
		} else {
			/* Prepare FIS receive area for check. */
			for (i = 0; i < 32; i++)
				ctp->sfis[i] = 0xff;
			softreset = 2;
		}
	}
	if (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA)
		cmd_flags |= FSL_SATA_CMD_QUEUED;
	clp->cmd_flags = htole32(cmd_flags |
	    (ccb->ccb_h.func_code == XPT_SCSI_IO ?  FSL_SATA_CMD_ATAPI : 0) |
	    slot->slot);
	clp->ttl = htole32(slot->ttl);
	clp->cda = htole32(FSL_SATA_CTP_BUS(ch, slot));
	bus_dmamap_sync(ch->dma.work_tag, ch->dma.work_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	/* Issue command to the controller. */
	slot->state = FSL_SATA_SLOT_RUNNING;
	ch->rslots |= (1 << slot->slot);
	ATA_OUTL(ch->r_mem, FSL_SATA_P_CQPMP, port);
	ATA_OUTL(ch->r_mem, FSL_SATA_P_CQR, (1 << slot->slot));
	/* Device reset commands don't interrupt. Poll them. */
	if (ccb->ccb_h.func_code == XPT_ATA_IO &&
	    (ccb->ataio.cmd.command == ATA_DEVICE_RESET || softreset)) {
		int count, timeout = ccb->ccb_h.timeout * 100;
		enum fsl_sata_err_type et = FSL_SATA_ERR_NONE;

		for (count = 0; count < timeout; count++) {
			DELAY(10);
			tmp = 0;
			if (softreset == 2) {
				tmp = ATA_INL(ch->r_mem, FSL_SATA_P_SIG);
				if (tmp != 0 && tmp != 0xffffffff)
					break;
				continue;
			}
			if ((ATA_INL(ch->r_mem, FSL_SATA_P_CCR) & (1 << slot->slot)) != 0)
				break;
		}

		if (timeout && (count >= timeout)) {
			device_printf(ch->dev, "Poll timeout on slot %d port %d (round %d)\n",
			    slot->slot, port, softreset);
			device_printf(ch->dev, "hsts %08x cqr %08x ccr %08x ss %08x "
			    "rs %08x cer %08x der %08x serr %08x car %08x sig %08x\n",
			    ATA_INL(ch->r_mem, FSL_SATA_P_HSTS),
			    ATA_INL(ch->r_mem, FSL_SATA_P_CQR),
			    ATA_INL(ch->r_mem, FSL_SATA_P_CCR),
			    ATA_INL(ch->r_mem, FSL_SATA_P_SSTS), ch->rslots,
			    ATA_INL(ch->r_mem, FSL_SATA_P_CER),
			    ATA_INL(ch->r_mem, FSL_SATA_P_DER),
			    ATA_INL(ch->r_mem, FSL_SATA_P_SERR),
			    ATA_INL(ch->r_mem, FSL_SATA_P_CAR),
			    ATA_INL(ch->r_mem, FSL_SATA_P_SIG));
			et = FSL_SATA_ERR_TIMEOUT;
		}

		fsl_sata_end_transaction(slot, et);
		return;
	}
	/* Start command execution timeout */
	callout_reset_sbt(&slot->timeout, SBT_1MS * ccb->ccb_h.timeout / 2,
	    0, (timeout_t*)fsl_sata_timeout, slot, 0);
	return;
}

/* Must be called with channel locked. */
static void
fsl_sata_process_timeout(struct fsl_sata_channel *ch)
{
	int i;

	mtx_assert(&ch->mtx, MA_OWNED);
	/* Handle the rest of commands. */
	for (i = 0; i < FSL_SATA_MAX_SLOTS; i++) {
		/* Do we have a running request on slot? */
		if (ch->slot[i].state < FSL_SATA_SLOT_RUNNING)
			continue;
		fsl_sata_end_transaction(&ch->slot[i], FSL_SATA_ERR_TIMEOUT);
	}
}

/* Must be called with channel locked. */
static void
fsl_sata_rearm_timeout(struct fsl_sata_channel *ch)
{
	int i;

	mtx_assert(&ch->mtx, MA_OWNED);
	for (i = 0; i < FSL_SATA_MAX_SLOTS; i++) {
		struct fsl_sata_slot *slot = &ch->slot[i];

		/* Do we have a running request on slot? */
		if (slot->state < FSL_SATA_SLOT_RUNNING)
			continue;
		if ((ch->toslots & (1 << i)) == 0)
			continue;
		callout_reset_sbt(&slot->timeout,
 	    	    SBT_1MS * slot->ccb->ccb_h.timeout / 2, 0,
		    (timeout_t*)fsl_sata_timeout, slot, 0);
	}
}

/* Locked by callout mechanism. */
static void
fsl_sata_timeout(struct fsl_sata_slot *slot)
{
	struct fsl_sata_channel *ch = slot->ch;
	device_t dev = ch->dev;
	uint32_t sstatus;

	/* Check for stale timeout. */
	if (slot->state < FSL_SATA_SLOT_RUNNING)
		return;

	/* Check if slot was not being executed last time we checked. */
	if (slot->state < FSL_SATA_SLOT_EXECUTING) {
		/* Check if slot started executing. */
		sstatus = ATA_INL(ch->r_mem, FSL_SATA_P_CAR);
		if ((sstatus & (1 << slot->slot)) != 0)
			slot->state = FSL_SATA_SLOT_EXECUTING;

		callout_reset_sbt(&slot->timeout,
	    	    SBT_1MS * slot->ccb->ccb_h.timeout / 2, 0,
		    (timeout_t*)fsl_sata_timeout, slot, 0);
		return;
	}

	device_printf(dev, "Timeout on slot %d port %d\n",
	    slot->slot, slot->ccb->ccb_h.target_id & 0x0f);

	/* Handle frozen command. */
	if (ch->frozen) {
		union ccb *fccb = ch->frozen;
		ch->frozen = NULL;
		fccb->ccb_h.status = CAM_REQUEUE_REQ | CAM_RELEASE_SIMQ;
		if (!(fccb->ccb_h.status & CAM_DEV_QFRZN)) {
			xpt_freeze_devq(fccb->ccb_h.path, 1);
			fccb->ccb_h.status |= CAM_DEV_QFRZN;
		}
		fsl_sata_done(ch, fccb);
	}
	if (ch->toslots == 0)
		xpt_freeze_simq(ch->sim, 1);
	ch->toslots |= (1 << slot->slot);
	if ((ch->rslots & ~ch->toslots) == 0)
		fsl_sata_process_timeout(ch);
	else
		device_printf(dev, " ... waiting for slots %08x\n",
		    ch->rslots & ~ch->toslots);
}

/* Must be called with channel locked. */
static void
fsl_sata_end_transaction(struct fsl_sata_slot *slot, enum fsl_sata_err_type et)
{
	struct fsl_sata_channel *ch = slot->ch;
	union ccb *ccb = slot->ccb;
	struct fsl_sata_cmd_list *clp;
	int lastto;
	uint32_t sig;

	bus_dmamap_sync(ch->dma.work_tag, ch->dma.work_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	clp = FSL_SATA_CLP(ch, slot);
	/* Read result registers to the result struct */
	if (ccb->ccb_h.func_code == XPT_ATA_IO) {
		struct ata_res *res = &ccb->ataio.res;

		if ((et == FSL_SATA_ERR_TFE) ||
		    (ccb->ataio.cmd.flags & CAM_ATAIO_NEEDRESULT)) {
			struct fsl_sata_cmd_tab *ctp = FSL_SATA_CTP(ch, slot);
			uint8_t *fis = ctp->sfis;

			res->status = fis[2];
			res->error = fis[3];
			res->lba_low = fis[4];
			res->lba_mid = fis[5];
			res->lba_high = fis[6];
			res->device = fis[7];
			res->lba_low_exp = fis[8];
			res->lba_mid_exp = fis[9];
			res->lba_high_exp = fis[10];
			res->sector_count = fis[12];
			res->sector_count_exp = fis[13];

			if ((ccb->ataio.cmd.flags & CAM_ATAIO_CONTROL) &&
			    (ccb->ataio.cmd.control & ATA_A_RESET) == 0) {
				sig = ATA_INL(ch->r_mem,  FSL_SATA_P_SIG);
				res->lba_high = sig >> 24;
				res->lba_mid = sig >> 16;
				res->lba_low = sig >> 8;
				res->sector_count = sig;
			}
		} else
			bzero(res, sizeof(*res));
		if ((ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA) == 0 &&
		    (ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
			ccb->ataio.resid =
			    ccb->ataio.dxfer_len - le32toh(clp->ttl);
		}
	} else {
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
			ccb->csio.resid =
			    ccb->csio.dxfer_len - le32toh(clp->ttl);
		}
	}
	if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
		bus_dmamap_sync(ch->dma.data_tag, slot->dma.data_map,
		    (ccb->ccb_h.flags & CAM_DIR_IN) ?
		    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(ch->dma.data_tag, slot->dma.data_map);
	}
	if (et != FSL_SATA_ERR_NONE)
		ch->eslots |= (1 << slot->slot);
	/* In case of error, freeze device for proper recovery. */
	if ((et != FSL_SATA_ERR_NONE) && (!ch->recoverycmd) &&
	    !(ccb->ccb_h.status & CAM_DEV_QFRZN)) {
		xpt_freeze_devq(ccb->ccb_h.path, 1);
		ccb->ccb_h.status |= CAM_DEV_QFRZN;
	}
	/* Set proper result status. */
	ccb->ccb_h.status &= ~CAM_STATUS_MASK;
	switch (et) {
	case FSL_SATA_ERR_NONE:
		ccb->ccb_h.status |= CAM_REQ_CMP;
		if (ccb->ccb_h.func_code == XPT_SCSI_IO)
			ccb->csio.scsi_status = SCSI_STATUS_OK;
		break;
	case FSL_SATA_ERR_INVALID:
		ch->fatalerr = 1;
		ccb->ccb_h.status |= CAM_REQ_INVALID;
		break;
	case FSL_SATA_ERR_INNOCENT:
		ccb->ccb_h.status |= CAM_REQUEUE_REQ;
		break;
	case FSL_SATA_ERR_TFE:
	case FSL_SATA_ERR_NCQ:
		if (ccb->ccb_h.func_code == XPT_SCSI_IO) {
			ccb->ccb_h.status |= CAM_SCSI_STATUS_ERROR;
			ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
		} else {
			ccb->ccb_h.status |= CAM_ATA_STATUS_ERROR;
		}
		break;
	case FSL_SATA_ERR_SATA:
		ch->fatalerr = 1;
		if (!ch->recoverycmd) {
			xpt_freeze_simq(ch->sim, 1);
			ccb->ccb_h.status &= ~CAM_STATUS_MASK;
			ccb->ccb_h.status |= CAM_RELEASE_SIMQ;
		}
		ccb->ccb_h.status |= CAM_UNCOR_PARITY;
		break;
	case FSL_SATA_ERR_TIMEOUT:
		if (!ch->recoverycmd) {
			xpt_freeze_simq(ch->sim, 1);
			ccb->ccb_h.status &= ~CAM_STATUS_MASK;
			ccb->ccb_h.status |= CAM_RELEASE_SIMQ;
		}
		ccb->ccb_h.status |= CAM_CMD_TIMEOUT;
		break;
	default:
		ch->fatalerr = 1;
		ccb->ccb_h.status |= CAM_REQ_CMP_ERR;
	}
	/* Free slot. */
	ch->oslots &= ~(1 << slot->slot);
	ch->rslots &= ~(1 << slot->slot);
	ch->aslots &= ~(1 << slot->slot);
	slot->state = FSL_SATA_SLOT_EMPTY;
	slot->ccb = NULL;
	/* Update channel stats. */
	ch->numrslots--;
	ch->numrslotspd[ccb->ccb_h.target_id]--;
	ATA_OUTL(ch->r_mem, FSL_SATA_P_CCR, 1 << slot->slot);
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA)) {
		ch->numtslots--;
		ch->numtslotspd[ccb->ccb_h.target_id]--;
	}
	/* Cancel timeout state if request completed normally. */
	if (et != FSL_SATA_ERR_TIMEOUT) {
		lastto = (ch->toslots == (1 << slot->slot));
		ch->toslots &= ~(1 << slot->slot);
		if (lastto)
			xpt_release_simq(ch->sim, TRUE);
	}
	/* If it was first request of reset sequence and there is no error,
	 * proceed to second request. */
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & CAM_ATAIO_CONTROL) &&
	    (ccb->ataio.cmd.control & ATA_A_RESET) &&
	    et == FSL_SATA_ERR_NONE) {
		ccb->ataio.cmd.control &= ~ATA_A_RESET;
		fsl_sata_begin_transaction(ch, ccb);
		return;
	}
	/* If it was our READ LOG command - process it. */
	if (ccb->ccb_h.recovery_type == RECOVERY_READ_LOG) {
		fsl_sata_process_read_log(ch, ccb);
	/* If it was our REQUEST SENSE command - process it. */
	} else if (ccb->ccb_h.recovery_type == RECOVERY_REQUEST_SENSE) {
		fsl_sata_process_request_sense(ch, ccb);
	/* If it was NCQ or ATAPI command error, put result on hold. */
	} else if (et == FSL_SATA_ERR_NCQ ||
	    ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_SCSI_STATUS_ERROR &&
	     (ccb->ccb_h.flags & CAM_DIS_AUTOSENSE) == 0)) {
		ch->hold[slot->slot] = ccb;
		ch->numhslots++;
	} else
		fsl_sata_done(ch, ccb);
	/* If we have no other active commands, ... */
	if (ch->rslots == 0) {
		/* if there was fatal error - reset port. */
		if (ch->toslots != 0 || ch->fatalerr) {
			fsl_sata_reset(ch);
		} else {
			/* if we have slots in error, we can reinit port. */
			if (ch->eslots != 0) {
				fsl_sata_stop(ch);
				fsl_sata_start(ch);
			}
			/* if there commands on hold, we can do READ LOG. */
			if (!ch->recoverycmd && ch->numhslots)
				fsl_sata_issue_recovery(ch);
		}
	/* If all the rest of commands are in timeout - give them chance. */
	} else if ((ch->rslots & ~ch->toslots) == 0 &&
	    et != FSL_SATA_ERR_TIMEOUT)
		fsl_sata_rearm_timeout(ch);
	/* Unfreeze frozen command. */
	if (ch->frozen && !fsl_sata_check_collision(ch, ch->frozen)) {
		union ccb *fccb = ch->frozen;
		ch->frozen = NULL;
		fsl_sata_begin_transaction(ch, fccb);
		xpt_release_simq(ch->sim, TRUE);
	}
	/* Start PM timer. */
	if (ch->numrslots == 0 && ch->pm_level > 3 &&
	    (ch->curr[ch->pm_present ? 15 : 0].caps & CTS_SATA_CAPS_D_PMREQ)) {
		callout_schedule(&ch->pm_timer,
		    (ch->pm_level == 4) ? hz / 1000 : hz / 8);
	}
}

static void
fsl_sata_issue_recovery(struct fsl_sata_channel *ch)
{
	union ccb *ccb;
	struct ccb_ataio *ataio;
	struct ccb_scsiio *csio;
	int i;

	/* Find some held command. */
	for (i = 0; i < FSL_SATA_MAX_SLOTS; i++) {
		if (ch->hold[i])
			break;
	}
	ccb = xpt_alloc_ccb_nowait();
	if (ccb == NULL) {
		device_printf(ch->dev, "Unable to allocate recovery command\n");
completeall:
		/* We can't do anything -- complete held commands. */
		for (i = 0; i < FSL_SATA_MAX_SLOTS; i++) {
			if (ch->hold[i] == NULL)
				continue;
			ch->hold[i]->ccb_h.status &= ~CAM_STATUS_MASK;
			ch->hold[i]->ccb_h.status |= CAM_RESRC_UNAVAIL;
			fsl_sata_done(ch, ch->hold[i]);
			ch->hold[i] = NULL;
			ch->numhslots--;
		}
		fsl_sata_reset(ch);
		return;
	}
	ccb->ccb_h = ch->hold[i]->ccb_h;	/* Reuse old header. */
	if (ccb->ccb_h.func_code == XPT_ATA_IO) {
		/* READ LOG */
		ccb->ccb_h.recovery_type = RECOVERY_READ_LOG;
		ccb->ccb_h.func_code = XPT_ATA_IO;
		ccb->ccb_h.flags = CAM_DIR_IN;
		ccb->ccb_h.timeout = 1000;	/* 1s should be enough. */
		ataio = &ccb->ataio;
		ataio->data_ptr = malloc(512, M_FSL_SATA, M_NOWAIT);
		if (ataio->data_ptr == NULL) {
			xpt_free_ccb(ccb);
			device_printf(ch->dev,
			    "Unable to allocate memory for READ LOG command\n");
			goto completeall;
		}
		ataio->dxfer_len = 512;
		bzero(&ataio->cmd, sizeof(ataio->cmd));
		ataio->cmd.flags = CAM_ATAIO_48BIT;
		ataio->cmd.command = 0x2F;	/* READ LOG EXT */
		ataio->cmd.sector_count = 1;
		ataio->cmd.sector_count_exp = 0;
		ataio->cmd.lba_low = 0x10;
		ataio->cmd.lba_mid = 0;
		ataio->cmd.lba_mid_exp = 0;
	} else {
		/* REQUEST SENSE */
		ccb->ccb_h.recovery_type = RECOVERY_REQUEST_SENSE;
		ccb->ccb_h.recovery_slot = i;
		ccb->ccb_h.func_code = XPT_SCSI_IO;
		ccb->ccb_h.flags = CAM_DIR_IN;
		ccb->ccb_h.status = 0;
		ccb->ccb_h.timeout = 1000;	/* 1s should be enough. */
		csio = &ccb->csio;
		csio->data_ptr = (void *)&ch->hold[i]->csio.sense_data;
		csio->dxfer_len = ch->hold[i]->csio.sense_len;
		csio->cdb_len = 6;
		bzero(&csio->cdb_io, sizeof(csio->cdb_io));
		csio->cdb_io.cdb_bytes[0] = 0x03;
		csio->cdb_io.cdb_bytes[4] = csio->dxfer_len;
	}
	/* Freeze SIM while doing recovery. */
	ch->recoverycmd = 1;
	xpt_freeze_simq(ch->sim, 1);
	fsl_sata_begin_transaction(ch, ccb);
}

static void
fsl_sata_process_read_log(struct fsl_sata_channel *ch, union ccb *ccb)
{
	uint8_t *data;
	struct ata_res *res;
	int i;

	ch->recoverycmd = 0;

	data = ccb->ataio.data_ptr;
	if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP &&
	    (data[0] & 0x80) == 0) {
		for (i = 0; i < FSL_SATA_MAX_SLOTS; i++) {
			if (!ch->hold[i])
				continue;
			if (ch->hold[i]->ccb_h.func_code != XPT_ATA_IO)
				continue;
			if ((data[0] & 0x1F) == i) {
				res = &ch->hold[i]->ataio.res;
				res->status = data[2];
				res->error = data[3];
				res->lba_low = data[4];
				res->lba_mid = data[5];
				res->lba_high = data[6];
				res->device = data[7];
				res->lba_low_exp = data[8];
				res->lba_mid_exp = data[9];
				res->lba_high_exp = data[10];
				res->sector_count = data[12];
				res->sector_count_exp = data[13];
			} else {
				ch->hold[i]->ccb_h.status &= ~CAM_STATUS_MASK;
				ch->hold[i]->ccb_h.status |= CAM_REQUEUE_REQ;
			}
			fsl_sata_done(ch, ch->hold[i]);
			ch->hold[i] = NULL;
			ch->numhslots--;
		}
	} else {
		if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)
			device_printf(ch->dev, "Error while READ LOG EXT\n");
		else if ((data[0] & 0x80) == 0) {
			device_printf(ch->dev, "Non-queued command error in READ LOG EXT\n");
		}
		for (i = 0; i < FSL_SATA_MAX_SLOTS; i++) {
			if (!ch->hold[i])
				continue;
			if (ch->hold[i]->ccb_h.func_code != XPT_ATA_IO)
				continue;
			fsl_sata_done(ch, ch->hold[i]);
			ch->hold[i] = NULL;
			ch->numhslots--;
		}
	}
	free(ccb->ataio.data_ptr, M_FSL_SATA);
	xpt_free_ccb(ccb);
	xpt_release_simq(ch->sim, TRUE);
}

static void
fsl_sata_process_request_sense(struct fsl_sata_channel *ch, union ccb *ccb)
{
	int i;

	ch->recoverycmd = 0;

	i = ccb->ccb_h.recovery_slot;
	if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) {
		ch->hold[i]->ccb_h.status |= CAM_AUTOSNS_VALID;
	} else {
		ch->hold[i]->ccb_h.status &= ~CAM_STATUS_MASK;
		ch->hold[i]->ccb_h.status |= CAM_AUTOSENSE_FAIL;
	}
	fsl_sata_done(ch, ch->hold[i]);
	ch->hold[i] = NULL;
	ch->numhslots--;
	xpt_free_ccb(ccb);
	xpt_release_simq(ch->sim, TRUE);
}

static void
fsl_sata_start(struct fsl_sata_channel *ch)
{
	u_int32_t cmd;

	/* Clear SATA error register */
	ATA_OUTL(ch->r_mem, FSL_SATA_P_SERR, 0xFFFFFFFF);
	/* Clear any interrupts pending on this channel */
	ATA_OUTL(ch->r_mem, FSL_SATA_P_HSTS, 0x3F);
	ATA_OUTL(ch->r_mem, FSL_SATA_P_CER, 0xFFFF);
	ATA_OUTL(ch->r_mem, FSL_SATA_P_DER, 0xFFFF);
	/* Start operations on this channel */
	cmd = ATA_INL(ch->r_mem, FSL_SATA_P_HCTRL);
	cmd |= FSL_SATA_P_HCTRL_HC_ON | FSL_SATA_P_HCTRL_SNOOP;
	cmd &= ~FSL_SATA_P_HCTRL_HC_FORCE_OFF;
	ATA_OUTL(ch->r_mem, FSL_SATA_P_HCTRL, cmd | 
	    (ch->pm_present ? FSL_SATA_P_HCTRL_PM : 0));
	fsl_sata_wait_register(ch, FSL_SATA_P_HSTS,
	    FSL_SATA_P_HSTS_PR, FSL_SATA_P_HSTS_PR, 500);
	ATA_OUTL(ch->r_mem, FSL_SATA_P_HSTS,
	    ATA_INL(ch->r_mem, FSL_SATA_P_HSTS) & FSL_SATA_P_HSTS_PR);
}

static void
fsl_sata_stop(struct fsl_sata_channel *ch)
{
	uint32_t cmd;
	int i;

	/* Kill all activity on this channel */
	cmd = ATA_INL(ch->r_mem, FSL_SATA_P_HCTRL);
	cmd &= ~FSL_SATA_P_HCTRL_HC_ON;

	for (i = 0; i < 2; i++) {
		ATA_OUTL(ch->r_mem, FSL_SATA_P_HCTRL, cmd);
		if (fsl_sata_wait_register(ch, FSL_SATA_P_HSTS,
		    FSL_SATA_P_HSTS_HS_ON, 0, 500)) {
			if (i != 0)
				device_printf(ch->dev,
				    "stopping FSL SATA engine failed\n");
			cmd |= FSL_SATA_P_HCTRL_HC_FORCE_OFF;
		} else
			break;
	}
	ch->eslots = 0;
}

static void
fsl_sata_reset(struct fsl_sata_channel *ch)
{
	uint32_t ctrl;
	int i;

	xpt_freeze_simq(ch->sim, 1);
	if (bootverbose)
		device_printf(ch->dev, "FSL SATA reset...\n");

	/* Requeue freezed command. */
	if (ch->frozen) {
		union ccb *fccb = ch->frozen;
		ch->frozen = NULL;
		fccb->ccb_h.status = CAM_REQUEUE_REQ | CAM_RELEASE_SIMQ;
		if (!(fccb->ccb_h.status & CAM_DEV_QFRZN)) {
			xpt_freeze_devq(fccb->ccb_h.path, 1);
			fccb->ccb_h.status |= CAM_DEV_QFRZN;
		}
		fsl_sata_done(ch, fccb);
	}
	/* Kill the engine and requeue all running commands. */
	fsl_sata_stop(ch);
	DELAY(1000);	/* sleep for 1ms */
	for (i = 0; i < FSL_SATA_MAX_SLOTS; i++) {
		/* Do we have a running request on slot? */
		if (ch->slot[i].state < FSL_SATA_SLOT_RUNNING)
			continue;
		/* XXX; Commands in loading state. */
		fsl_sata_end_transaction(&ch->slot[i], FSL_SATA_ERR_INNOCENT);
	}
	for (i = 0; i < FSL_SATA_MAX_SLOTS; i++) {
		if (!ch->hold[i])
			continue;
		fsl_sata_done(ch, ch->hold[i]);
		ch->hold[i] = NULL;
		ch->numhslots--;
	}
	if (ch->toslots != 0)
		xpt_release_simq(ch->sim, TRUE);
	ch->eslots = 0;
	ch->toslots = 0;
	ch->fatalerr = 0;
	/* Tell the XPT about the event */
	xpt_async(AC_BUS_RESET, ch->path, NULL);
	/* Disable port interrupts */
	ATA_OUTL(ch->r_mem, FSL_SATA_P_HCTRL,
	    ATA_INL(ch->r_mem, FSL_SATA_P_HCTRL) & ~0x3f);
	/* Reset and reconnect PHY, */
	fsl_sata_start(ch);
	if (fsl_sata_wait_register(ch, FSL_SATA_P_HSTS, 0x08, 0x08, 500)) {
		if (bootverbose)
			device_printf(ch->dev,
			    "FSL SATA reset: device not found\n");
		ch->devices = 0;
		/* Enable wanted port interrupts */
		ATA_OUTL(ch->r_mem, FSL_SATA_P_HCTRL,
		    ATA_INL(ch->r_mem, FSL_SATA_P_HCTRL) | FSL_SATA_P_HCTRL_PHYRDY);
		xpt_release_simq(ch->sim, TRUE);
		return;
	}
	if (bootverbose)
		device_printf(ch->dev, "FSL SATA reset: device found\n");
	ch->devices = 1;
	/* Enable wanted port interrupts */
	ctrl = ATA_INL(ch->r_mem, FSL_SATA_P_HCTRL) & ~0x3f;
	ATA_OUTL(ch->r_mem, FSL_SATA_P_HCTRL,
	    ctrl | FSL_SATA_P_HCTRL_FATAL | FSL_SATA_P_HCTRL_PHYRDY |
	    FSL_SATA_P_HCTRL_SIG | FSL_SATA_P_HCTRL_SNTFY |
	    FSL_SATA_P_HCTRL_DE | FSL_SATA_P_HCTRL_CC);
	xpt_release_simq(ch->sim, TRUE);
}

static int
fsl_sata_setup_fis(struct fsl_sata_channel *ch, struct fsl_sata_cmd_tab *ctp, union ccb *ccb, int tag)
{
	uint8_t *fis = &ctp->cfis[0];

	bzero(fis, 32);
	fis[0] = 0x27;  		/* host to device */
	fis[1] = (ccb->ccb_h.target_id & 0x0f);
	if (ccb->ccb_h.func_code == XPT_SCSI_IO) {
		fis[1] |= 0x80;
		fis[2] = ATA_PACKET_CMD;
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE &&
		    ch->curr[ccb->ccb_h.target_id].mode >= ATA_DMA)
			fis[3] = ATA_F_DMA;
		else {
			fis[5] = ccb->csio.dxfer_len;
			fis[6] = ccb->csio.dxfer_len >> 8;
		}
		fis[7] = ATA_D_LBA;
		fis[15] = ATA_A_4BIT;
		bcopy((ccb->ccb_h.flags & CAM_CDB_POINTER) ?
		    ccb->csio.cdb_io.cdb_ptr : ccb->csio.cdb_io.cdb_bytes,
		    ctp->acmd, ccb->csio.cdb_len);
		bzero(ctp->acmd + ccb->csio.cdb_len, 32 - ccb->csio.cdb_len);
	} else if ((ccb->ataio.cmd.flags & CAM_ATAIO_CONTROL) == 0) {
		fis[1] |= 0x80;
		fis[2] = ccb->ataio.cmd.command;
		fis[3] = ccb->ataio.cmd.features;
		fis[4] = ccb->ataio.cmd.lba_low;
		fis[5] = ccb->ataio.cmd.lba_mid;
		fis[6] = ccb->ataio.cmd.lba_high;
		fis[7] = ccb->ataio.cmd.device;
		fis[8] = ccb->ataio.cmd.lba_low_exp;
		fis[9] = ccb->ataio.cmd.lba_mid_exp;
		fis[10] = ccb->ataio.cmd.lba_high_exp;
		fis[11] = ccb->ataio.cmd.features_exp;
		if (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA) {
			fis[12] = tag << 3;
			fis[13] = 0;
		} else {
			fis[12] = ccb->ataio.cmd.sector_count;
			fis[13] = ccb->ataio.cmd.sector_count_exp;
		}
		fis[15] = ATA_A_4BIT;
	} else {
		fis[15] = ccb->ataio.cmd.control;
	}
	return (20);
}

static int
fsl_sata_check_ids(struct fsl_sata_channel *ch, union ccb *ccb)
{

	if (ccb->ccb_h.target_id > 15) {
		ccb->ccb_h.status = CAM_TID_INVALID;
		fsl_sata_done(ch, ccb);
		return (-1);
	}
	if (ccb->ccb_h.target_lun != 0) {
		ccb->ccb_h.status = CAM_LUN_INVALID;
		fsl_sata_done(ch, ccb);
		return (-1);
	}
	return (0);
}

static void
fsl_sataaction(struct cam_sim *sim, union ccb *ccb)
{
	struct fsl_sata_channel *ch;

	CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE,
	    ("fsl_sataaction func_code=0x%x\n", ccb->ccb_h.func_code));

	ch = (struct fsl_sata_channel *)cam_sim_softc(sim);
	switch (ccb->ccb_h.func_code) {
	/* Common cases first */
	case XPT_ATA_IO:	/* Execute the requested I/O operation */
	case XPT_SCSI_IO:
		if (fsl_sata_check_ids(ch, ccb))
			return;
		if (ch->devices == 0 ||
		    (ch->pm_present == 0 &&
		     ccb->ccb_h.target_id > 0 && ccb->ccb_h.target_id < 15)) {
			ccb->ccb_h.status = CAM_SEL_TIMEOUT;
			break;
		}
		ccb->ccb_h.recovery_type = RECOVERY_NONE;
		/* Check for command collision. */
		if (fsl_sata_check_collision(ch, ccb)) {
			/* Freeze command. */
			ch->frozen = ccb;
			/* We have only one frozen slot, so freeze simq also. */
			xpt_freeze_simq(ch->sim, 1);
			return;
		}
		fsl_sata_begin_transaction(ch, ccb);
		return;
	case XPT_ABORT:			/* Abort the specified CCB */
		/* XXX Implement */
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	case XPT_SET_TRAN_SETTINGS:
	{
		struct	ccb_trans_settings *cts = &ccb->cts;
		struct	fsl_sata_device *d; 

		if (fsl_sata_check_ids(ch, ccb))
			return;
		if (cts->type == CTS_TYPE_CURRENT_SETTINGS)
			d = &ch->curr[ccb->ccb_h.target_id];
		else
			d = &ch->user[ccb->ccb_h.target_id];
		if (cts->xport_specific.sata.valid & CTS_SATA_VALID_REVISION)
			d->revision = cts->xport_specific.sata.revision;
		if (cts->xport_specific.sata.valid & CTS_SATA_VALID_MODE)
			d->mode = cts->xport_specific.sata.mode;
		if (cts->xport_specific.sata.valid & CTS_SATA_VALID_BYTECOUNT)
			d->bytecount = min(8192, cts->xport_specific.sata.bytecount);
		if (cts->xport_specific.sata.valid & CTS_SATA_VALID_TAGS)
			d->tags = min(FSL_SATA_MAX_SLOTS, cts->xport_specific.sata.tags);
		if (cts->xport_specific.sata.valid & CTS_SATA_VALID_PM)
			ch->pm_present = cts->xport_specific.sata.pm_present;
		if (cts->xport_specific.sata.valid & CTS_SATA_VALID_ATAPI)
			d->atapi = cts->xport_specific.sata.atapi;
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_GET_TRAN_SETTINGS:
	/* Get default/user set transfer settings for the target */
	{
		struct	ccb_trans_settings *cts = &ccb->cts;
		struct  fsl_sata_device *d;
		uint32_t status;

		if (fsl_sata_check_ids(ch, ccb))
			return;
		if (cts->type == CTS_TYPE_CURRENT_SETTINGS)
			d = &ch->curr[ccb->ccb_h.target_id];
		else
			d = &ch->user[ccb->ccb_h.target_id];
		cts->protocol = PROTO_UNSPECIFIED;
		cts->protocol_version = PROTO_VERSION_UNSPECIFIED;
		cts->transport = XPORT_SATA;
		cts->transport_version = XPORT_VERSION_UNSPECIFIED;
		cts->proto_specific.valid = 0;
		cts->xport_specific.sata.valid = 0;
		if (cts->type == CTS_TYPE_CURRENT_SETTINGS &&
		    (ccb->ccb_h.target_id == 15 ||
		    (ccb->ccb_h.target_id == 0 && !ch->pm_present))) {
			status = ATA_INL(ch->r_mem, FSL_SATA_P_SSTS) & ATA_SS_SPD_MASK;
			if (status & 0x0f0) {
				cts->xport_specific.sata.revision =
				    (status & 0x0f0) >> 4;
				cts->xport_specific.sata.valid |=
				    CTS_SATA_VALID_REVISION;
			}
			cts->xport_specific.sata.caps = d->caps & CTS_SATA_CAPS_D;
			if (ch->pm_level) {
				cts->xport_specific.sata.caps |= CTS_SATA_CAPS_H_PMREQ;
			}
			cts->xport_specific.sata.caps |= CTS_SATA_CAPS_H_AN;
			cts->xport_specific.sata.caps &=
			    ch->user[ccb->ccb_h.target_id].caps;
			cts->xport_specific.sata.valid |= CTS_SATA_VALID_CAPS;
		} else {
			cts->xport_specific.sata.revision = d->revision;
			cts->xport_specific.sata.valid |= CTS_SATA_VALID_REVISION;
			cts->xport_specific.sata.caps = d->caps;
			cts->xport_specific.sata.valid |= CTS_SATA_VALID_CAPS;
		}
		cts->xport_specific.sata.mode = d->mode;
		cts->xport_specific.sata.valid |= CTS_SATA_VALID_MODE;
		cts->xport_specific.sata.bytecount = d->bytecount;
		cts->xport_specific.sata.valid |= CTS_SATA_VALID_BYTECOUNT;
		cts->xport_specific.sata.pm_present = ch->pm_present;
		cts->xport_specific.sata.valid |= CTS_SATA_VALID_PM;
		cts->xport_specific.sata.tags = d->tags;
		cts->xport_specific.sata.valid |= CTS_SATA_VALID_TAGS;
		cts->xport_specific.sata.atapi = d->atapi;
		cts->xport_specific.sata.valid |= CTS_SATA_VALID_ATAPI;
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_RESET_BUS:		/* Reset the specified SCSI bus */
	case XPT_RESET_DEV:	/* Bus Device Reset the specified SCSI device */
		fsl_sata_reset(ch);
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	case XPT_TERM_IO:		/* Terminate the I/O process */
		/* XXX Implement */
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	case XPT_PATH_INQ:		/* Path routing inquiry */
	{
		struct ccb_pathinq *cpi = &ccb->cpi;

		cpi->version_num = 1; /* XXX??? */
		cpi->hba_inquiry = PI_SDTR_ABLE;
		cpi->hba_inquiry |= PI_TAG_ABLE;
#if 0
		/*
		 * XXX: CAM tries to reset port 15 if it sees port multiplier
		 * support.  Disable it for now.
		 */
		cpi->hba_inquiry |= PI_SATAPM;
#endif
		cpi->target_sprt = 0;
		cpi->hba_misc = PIM_SEQSCAN | PIM_UNMAPPED;
		cpi->hba_eng_cnt = 0;
		/*
		 * XXX: This should be 15, since hardware *does* support a port
		 * multiplier.  See above.
		 */
		cpi->max_target = 0;
		cpi->max_lun = 0;
		cpi->initiator_id = 0;
		cpi->bus_id = cam_sim_bus(sim);
		cpi->base_transfer_speed = 150000;
		strncpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strncpy(cpi->hba_vid, "FSL SATA", HBA_IDLEN);
		strncpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);
		cpi->transport = XPORT_SATA;
		cpi->transport_version = XPORT_VERSION_UNSPECIFIED;
		cpi->protocol = PROTO_ATA;
		cpi->protocol_version = PROTO_VERSION_UNSPECIFIED;
		cpi->maxio = MAXPHYS;
		cpi->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	default:
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	}
	fsl_sata_done(ch, ccb);
}

static void
fsl_satapoll(struct cam_sim *sim)
{
	struct fsl_sata_channel *ch = (struct fsl_sata_channel *)cam_sim_softc(sim);
	uint32_t istatus;

	/* Read interrupt statuses and process if any. */
	istatus = ATA_INL(ch->r_mem, FSL_SATA_P_HSTS);
	if (istatus != 0)
		fsl_sata_intr_main(ch, istatus);
}
MODULE_VERSION(fsl_sata, 1);
MODULE_DEPEND(fsl_sata, cam, 1, 1, 1);
