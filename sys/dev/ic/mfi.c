/* $OpenBSD: mfi.c,v 1.191 2023/11/28 09:29:20 jsg Exp $ */
/*
 * Copyright (c) 2006 Marco Peereboom <marco@peereboom.us>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "bio.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/rwlock.h>
#include <sys/sensors.h>
#include <sys/dkio.h>
#include <sys/pool.h>

#include <machine/bus.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

#include <dev/biovar.h>
#include <dev/ic/mfireg.h>
#include <dev/ic/mfivar.h>

#ifdef MFI_DEBUG
uint32_t	mfi_debug = 0
/*		    | MFI_D_CMD */
/*		    | MFI_D_INTR */
/*		    | MFI_D_MISC */
/*		    | MFI_D_DMA */
/*		    | MFI_D_IOCTL */
/*		    | MFI_D_RW */
/*		    | MFI_D_MEM */
/*		    | MFI_D_CCB */
		;
#endif

struct cfdriver mfi_cd = {
	NULL, "mfi", DV_DULL
};

void	mfi_scsi_cmd(struct scsi_xfer *);
int	mfi_scsi_ioctl(struct scsi_link *, u_long, caddr_t, int);
int	mfi_ioctl_cache(struct scsi_link *, u_long,  struct dk_cache *);

void	mfi_pd_scsi_cmd(struct scsi_xfer *);
int	mfi_pd_scsi_probe(struct scsi_link *);

const struct scsi_adapter mfi_switch = {
	mfi_scsi_cmd, NULL, NULL, NULL, mfi_scsi_ioctl
};

const struct scsi_adapter mfi_pd_switch = {
	mfi_pd_scsi_cmd, NULL, mfi_pd_scsi_probe, NULL, mfi_scsi_ioctl
};

void *		mfi_get_ccb(void *);
void		mfi_put_ccb(void *, void *);
void		mfi_scrub_ccb(struct mfi_ccb *);
int		mfi_init_ccb(struct mfi_softc *);

struct mfi_mem	*mfi_allocmem(struct mfi_softc *, size_t);
void		mfi_freemem(struct mfi_softc *, struct mfi_mem *);

int		mfi_transition_firmware(struct mfi_softc *);
int		mfi_initialize_firmware(struct mfi_softc *);
int		mfi_get_info(struct mfi_softc *);
uint32_t	mfi_read(struct mfi_softc *, bus_size_t);
void		mfi_write(struct mfi_softc *, bus_size_t, uint32_t);
void		mfi_poll(struct mfi_softc *, struct mfi_ccb *);
void		mfi_exec(struct mfi_softc *, struct mfi_ccb *);
void		mfi_exec_done(struct mfi_softc *, struct mfi_ccb *);
int		mfi_create_sgl(struct mfi_softc *, struct mfi_ccb *, int);
u_int		mfi_default_sgd_load(struct mfi_softc *, struct mfi_ccb *);
int		mfi_syspd(struct mfi_softc *);

/* commands */
int		mfi_scsi_ld(struct mfi_softc *sc, struct mfi_ccb *,
		    struct scsi_xfer *);
int		mfi_scsi_io(struct mfi_softc *sc, struct mfi_ccb *,
		    struct scsi_xfer *, uint64_t, uint32_t);
void		mfi_scsi_xs_done(struct mfi_softc *sc, struct mfi_ccb *);
int		mfi_mgmt(struct mfi_softc *, uint32_t, uint32_t, uint32_t,
		    void *, const union mfi_mbox *);
int		mfi_do_mgmt(struct mfi_softc *, struct mfi_ccb * , uint32_t,
		    uint32_t, uint32_t, void *, const union mfi_mbox *);
void		mfi_empty_done(struct mfi_softc *, struct mfi_ccb *);

#if NBIO > 0
int		mfi_ioctl(struct device *, u_long, caddr_t);
int		mfi_bio_getitall(struct mfi_softc *);
int		mfi_ioctl_inq(struct mfi_softc *, struct bioc_inq *);
int		mfi_ioctl_vol(struct mfi_softc *, struct bioc_vol *);
int		mfi_ioctl_disk(struct mfi_softc *, struct bioc_disk *);
int		mfi_ioctl_alarm(struct mfi_softc *, struct bioc_alarm *);
int		mfi_ioctl_blink(struct mfi_softc *sc, struct bioc_blink *);
int		mfi_ioctl_setstate(struct mfi_softc *, struct bioc_setstate *);
int		mfi_ioctl_patrol(struct mfi_softc *sc, struct bioc_patrol *);
int		mfi_bio_hs(struct mfi_softc *, int, int, void *);
#ifndef SMALL_KERNEL
int		mfi_create_sensors(struct mfi_softc *);
void		mfi_refresh_sensors(void *);
int		mfi_bbu(struct mfi_softc *);
#endif /* SMALL_KERNEL */
#endif /* NBIO > 0 */

void		mfi_start(struct mfi_softc *, struct mfi_ccb *);
void		mfi_done(struct mfi_softc *, struct mfi_ccb *);
u_int32_t	mfi_xscale_fw_state(struct mfi_softc *);
void		mfi_xscale_intr_ena(struct mfi_softc *);
int		mfi_xscale_intr(struct mfi_softc *);
void		mfi_xscale_post(struct mfi_softc *, struct mfi_ccb *);

static const struct mfi_iop_ops mfi_iop_xscale = {
	mfi_xscale_fw_state,
	mfi_xscale_intr_ena,
	mfi_xscale_intr,
	mfi_xscale_post,
	mfi_default_sgd_load,
	0,
};

u_int32_t	mfi_ppc_fw_state(struct mfi_softc *);
void		mfi_ppc_intr_ena(struct mfi_softc *);
int		mfi_ppc_intr(struct mfi_softc *);
void		mfi_ppc_post(struct mfi_softc *, struct mfi_ccb *);

static const struct mfi_iop_ops mfi_iop_ppc = {
	mfi_ppc_fw_state,
	mfi_ppc_intr_ena,
	mfi_ppc_intr,
	mfi_ppc_post,
	mfi_default_sgd_load,
	MFI_IDB,
	0
};

u_int32_t	mfi_gen2_fw_state(struct mfi_softc *);
void		mfi_gen2_intr_ena(struct mfi_softc *);
int		mfi_gen2_intr(struct mfi_softc *);
void		mfi_gen2_post(struct mfi_softc *, struct mfi_ccb *);

static const struct mfi_iop_ops mfi_iop_gen2 = {
	mfi_gen2_fw_state,
	mfi_gen2_intr_ena,
	mfi_gen2_intr,
	mfi_gen2_post,
	mfi_default_sgd_load,
	MFI_IDB,
	0
};

u_int32_t	mfi_skinny_fw_state(struct mfi_softc *);
void		mfi_skinny_intr_ena(struct mfi_softc *);
int		mfi_skinny_intr(struct mfi_softc *);
void		mfi_skinny_post(struct mfi_softc *, struct mfi_ccb *);
u_int		mfi_skinny_sgd_load(struct mfi_softc *, struct mfi_ccb *);

static const struct mfi_iop_ops mfi_iop_skinny = {
	mfi_skinny_fw_state,
	mfi_skinny_intr_ena,
	mfi_skinny_intr,
	mfi_skinny_post,
	mfi_skinny_sgd_load,
	MFI_SKINNY_IDB,
	MFI_IOP_F_SYSPD
};

#define mfi_fw_state(_s)	((_s)->sc_iop->mio_fw_state(_s))
#define mfi_intr_enable(_s)	((_s)->sc_iop->mio_intr_ena(_s))
#define mfi_my_intr(_s)		((_s)->sc_iop->mio_intr(_s))
#define mfi_post(_s, _c)	((_s)->sc_iop->mio_post((_s), (_c)))
#define mfi_sgd_load(_s, _c)	((_s)->sc_iop->mio_sgd_load((_s), (_c)))

void *
mfi_get_ccb(void *cookie)
{
	struct mfi_softc	*sc = cookie;
	struct mfi_ccb		*ccb;

	KERNEL_UNLOCK();

	mtx_enter(&sc->sc_ccb_mtx);
	ccb = SLIST_FIRST(&sc->sc_ccb_freeq);
	if (ccb != NULL) {
		SLIST_REMOVE_HEAD(&sc->sc_ccb_freeq, ccb_link);
		ccb->ccb_state = MFI_CCB_READY;
	}
	mtx_leave(&sc->sc_ccb_mtx);

	DNPRINTF(MFI_D_CCB, "%s: mfi_get_ccb: %p\n", DEVNAME(sc), ccb);
	KERNEL_LOCK();

	return (ccb);
}

void
mfi_put_ccb(void *cookie, void *io)
{
	struct mfi_softc	*sc = cookie;
	struct mfi_ccb		*ccb = io;

	DNPRINTF(MFI_D_CCB, "%s: mfi_put_ccb: %p\n", DEVNAME(sc), ccb);

	KERNEL_UNLOCK();
	mtx_enter(&sc->sc_ccb_mtx);
	SLIST_INSERT_HEAD(&sc->sc_ccb_freeq, ccb, ccb_link);
	mtx_leave(&sc->sc_ccb_mtx);
	KERNEL_LOCK();
}

void
mfi_scrub_ccb(struct mfi_ccb *ccb)
{
	struct mfi_frame_header	*hdr = &ccb->ccb_frame->mfr_header;

	hdr->mfh_cmd_status = 0x0;
	hdr->mfh_flags = 0x0;
	ccb->ccb_state = MFI_CCB_FREE;
	ccb->ccb_cookie = NULL;
	ccb->ccb_flags = 0;
	ccb->ccb_done = NULL;
	ccb->ccb_direction = 0;
	ccb->ccb_frame_size = 0;
	ccb->ccb_extra_frames = 0;
	ccb->ccb_sgl = NULL;
	ccb->ccb_data = NULL;
	ccb->ccb_len = 0;
}

int
mfi_init_ccb(struct mfi_softc *sc)
{
	struct mfi_ccb		*ccb;
	uint32_t		i;
	int			error;

	DNPRINTF(MFI_D_CCB, "%s: mfi_init_ccb\n", DEVNAME(sc));

	sc->sc_ccb = mallocarray(sc->sc_max_cmds, sizeof(struct mfi_ccb),
	    M_DEVBUF, M_WAITOK|M_ZERO);

	for (i = 0; i < sc->sc_max_cmds; i++) {
		ccb = &sc->sc_ccb[i];

		/* select i'th frame */
		ccb->ccb_frame = (union mfi_frame *)
		    (MFIMEM_KVA(sc->sc_frames) + sc->sc_frames_size * i);
		ccb->ccb_pframe =
		    MFIMEM_DVA(sc->sc_frames) + sc->sc_frames_size * i;
		ccb->ccb_pframe_offset = sc->sc_frames_size * i;
		ccb->ccb_frame->mfr_header.mfh_context = i;

		/* select i'th sense */
		ccb->ccb_sense = (struct mfi_sense *)
		    (MFIMEM_KVA(sc->sc_sense) + MFI_SENSE_SIZE * i);
		ccb->ccb_psense =
		    (MFIMEM_DVA(sc->sc_sense) + MFI_SENSE_SIZE * i);

		/* create a dma map for transfer */
		error = bus_dmamap_create(sc->sc_dmat,
		    MAXPHYS, sc->sc_max_sgl, MAXPHYS, 0,
		    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &ccb->ccb_dmamap);
		if (error) {
			printf("%s: cannot create ccb dmamap (%d)\n",
			    DEVNAME(sc), error);
			goto destroy;
		}

		DNPRINTF(MFI_D_CCB,
		    "ccb(%d): %p frame: %p (%#lx) sense: %p (%#lx) map: %p\n",
		    ccb->ccb_frame->mfr_header.mfh_context, ccb,
		    ccb->ccb_frame, ccb->ccb_pframe,
		    ccb->ccb_sense, ccb->ccb_psense,
		    ccb->ccb_dmamap);

		/* add ccb to queue */
		mfi_put_ccb(sc, ccb);
	}

	return (0);
destroy:
	/* free dma maps and ccb memory */
	while ((ccb = mfi_get_ccb(sc)) != NULL)
		bus_dmamap_destroy(sc->sc_dmat, ccb->ccb_dmamap);

	free(sc->sc_ccb, M_DEVBUF, 0);

	return (1);
}

uint32_t
mfi_read(struct mfi_softc *sc, bus_size_t r)
{
	uint32_t rv;

	bus_space_barrier(sc->sc_iot, sc->sc_ioh, r, 4,
	    BUS_SPACE_BARRIER_READ);
	rv = bus_space_read_4(sc->sc_iot, sc->sc_ioh, r);

	DNPRINTF(MFI_D_RW, "%s: mr 0x%lx 0x08%x ", DEVNAME(sc), r, rv);
	return (rv);
}

void
mfi_write(struct mfi_softc *sc, bus_size_t r, uint32_t v)
{
	DNPRINTF(MFI_D_RW, "%s: mw 0x%lx 0x%08x", DEVNAME(sc), r, v);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, r, v);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, r, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

struct mfi_mem *
mfi_allocmem(struct mfi_softc *sc, size_t size)
{
	struct mfi_mem		*mm;
	int			nsegs;

	DNPRINTF(MFI_D_MEM, "%s: mfi_allocmem: %zu\n", DEVNAME(sc),
	    size);

	mm = malloc(sizeof(struct mfi_mem), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (mm == NULL)
		return (NULL);

	mm->am_size = size;

	if (bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &mm->am_map) != 0)
		goto amfree;

	if (bus_dmamem_alloc(sc->sc_dmat, size, PAGE_SIZE, 0, &mm->am_seg, 1,
	    &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO) != 0)
		goto destroy;

	if (bus_dmamem_map(sc->sc_dmat, &mm->am_seg, nsegs, size, &mm->am_kva,
	    BUS_DMA_NOWAIT) != 0)
		goto free;

	if (bus_dmamap_load(sc->sc_dmat, mm->am_map, mm->am_kva, size, NULL,
	    BUS_DMA_NOWAIT) != 0)
		goto unmap;

	DNPRINTF(MFI_D_MEM, "  kva: %p  dva: %lx  map: %p\n",
	    mm->am_kva, mm->am_map->dm_segs[0].ds_addr, mm->am_map);

	return (mm);

unmap:
	bus_dmamem_unmap(sc->sc_dmat, mm->am_kva, size);
free:
	bus_dmamem_free(sc->sc_dmat, &mm->am_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, mm->am_map);
amfree:
	free(mm, M_DEVBUF, sizeof *mm);

	return (NULL);
}

void
mfi_freemem(struct mfi_softc *sc, struct mfi_mem *mm)
{
	DNPRINTF(MFI_D_MEM, "%s: mfi_freemem: %p\n", DEVNAME(sc), mm);

	bus_dmamap_unload(sc->sc_dmat, mm->am_map);
	bus_dmamem_unmap(sc->sc_dmat, mm->am_kva, mm->am_size);
	bus_dmamem_free(sc->sc_dmat, &mm->am_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, mm->am_map);
	free(mm, M_DEVBUF, sizeof *mm);
}

int
mfi_transition_firmware(struct mfi_softc *sc)
{
	int32_t			fw_state, cur_state;
	u_int32_t		idb = sc->sc_iop->mio_idb;
	int			max_wait, i;

	fw_state = mfi_fw_state(sc) & MFI_STATE_MASK;

	DNPRINTF(MFI_D_CMD, "%s: mfi_transition_firmware: %#x\n", DEVNAME(sc),
	    fw_state);

	while (fw_state != MFI_STATE_READY) {
		DNPRINTF(MFI_D_MISC,
		    "%s: waiting for firmware to become ready\n",
		    DEVNAME(sc));
		cur_state = fw_state;
		switch (fw_state) {
		case MFI_STATE_FAULT:
			printf("%s: firmware fault\n", DEVNAME(sc));
			return (1);
		case MFI_STATE_WAIT_HANDSHAKE:
			mfi_write(sc, idb, MFI_INIT_CLEAR_HANDSHAKE);
			max_wait = 2;
			break;
		case MFI_STATE_OPERATIONAL:
			mfi_write(sc, idb, MFI_INIT_READY);
			max_wait = 10;
			break;
		case MFI_STATE_UNDEFINED:
		case MFI_STATE_BB_INIT:
			max_wait = 2;
			break;
		case MFI_STATE_FW_INIT:
		case MFI_STATE_DEVICE_SCAN:
		case MFI_STATE_FLUSH_CACHE:
			max_wait = 20;
			break;
		default:
			printf("%s: unknown firmware state %d\n",
			    DEVNAME(sc), fw_state);
			return (1);
		}
		for (i = 0; i < (max_wait * 10); i++) {
			fw_state = mfi_fw_state(sc) & MFI_STATE_MASK;
			if (fw_state == cur_state)
				DELAY(100000);
			else
				break;
		}
		if (fw_state == cur_state) {
			printf("%s: firmware stuck in state %#x\n",
			    DEVNAME(sc), fw_state);
			return (1);
		}
	}

	return (0);
}

int
mfi_initialize_firmware(struct mfi_softc *sc)
{
	struct mfi_ccb		*ccb;
	struct mfi_init_frame	*init;
	struct mfi_init_qinfo	*qinfo;
	int			rv = 0;

	DNPRINTF(MFI_D_MISC, "%s: mfi_initialize_firmware\n", DEVNAME(sc));

	ccb = scsi_io_get(&sc->sc_iopool, 0);
	mfi_scrub_ccb(ccb);

	init = &ccb->ccb_frame->mfr_init;
	qinfo = (struct mfi_init_qinfo *)((uint8_t *)init + MFI_FRAME_SIZE);

	memset(qinfo, 0, sizeof(*qinfo));
	qinfo->miq_rq_entries = htole32(sc->sc_max_cmds + 1);

	qinfo->miq_rq_addr = htole64(MFIMEM_DVA(sc->sc_pcq) +
	    offsetof(struct mfi_prod_cons, mpc_reply_q));

	qinfo->miq_pi_addr = htole64(MFIMEM_DVA(sc->sc_pcq) +
	    offsetof(struct mfi_prod_cons, mpc_producer));

	qinfo->miq_ci_addr = htole64(MFIMEM_DVA(sc->sc_pcq) +
	    offsetof(struct mfi_prod_cons, mpc_consumer));

	init->mif_header.mfh_cmd = MFI_CMD_INIT;
	init->mif_header.mfh_data_len = htole32(sizeof(*qinfo));
	init->mif_qinfo_new_addr = htole64(ccb->ccb_pframe + MFI_FRAME_SIZE);

	bus_dmamap_sync(sc->sc_dmat, MFIMEM_MAP(sc->sc_pcq),
	    0, MFIMEM_LEN(sc->sc_pcq),
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	ccb->ccb_done = mfi_empty_done;
	mfi_poll(sc, ccb);
	if (init->mif_header.mfh_cmd_status != MFI_STAT_OK)
		rv = 1;

	mfi_put_ccb(sc, ccb);

	return (rv);
}

void
mfi_empty_done(struct mfi_softc *sc, struct mfi_ccb *ccb)
{
	/* nop */
}

int
mfi_get_info(struct mfi_softc *sc)
{
#ifdef MFI_DEBUG
	int i;
#endif
	DNPRINTF(MFI_D_MISC, "%s: mfi_get_info\n", DEVNAME(sc));

	if (mfi_mgmt(sc, MR_DCMD_CTRL_GET_INFO, MFI_DATA_IN,
	    sizeof(sc->sc_info), &sc->sc_info, NULL))
		return (1);

#ifdef MFI_DEBUG
	for (i = 0; i < sc->sc_info.mci_image_component_count; i++) {
		printf("%s: active FW %s Version %s date %s time %s\n",
		    DEVNAME(sc),
		    sc->sc_info.mci_image_component[i].mic_name,
		    sc->sc_info.mci_image_component[i].mic_version,
		    sc->sc_info.mci_image_component[i].mic_build_date,
		    sc->sc_info.mci_image_component[i].mic_build_time);
	}

	for (i = 0; i < sc->sc_info.mci_pending_image_component_count; i++) {
		printf("%s: pending FW %s Version %s date %s time %s\n",
		    DEVNAME(sc),
		    sc->sc_info.mci_pending_image_component[i].mic_name,
		    sc->sc_info.mci_pending_image_component[i].mic_version,
		    sc->sc_info.mci_pending_image_component[i].mic_build_date,
		    sc->sc_info.mci_pending_image_component[i].mic_build_time);
	}

	printf("%s: max_arms %d max_spans %d max_arrs %d max_lds %d name %s\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_max_arms,
	    sc->sc_info.mci_max_spans,
	    sc->sc_info.mci_max_arrays,
	    sc->sc_info.mci_max_lds,
	    sc->sc_info.mci_product_name);

	printf("%s: serial %s present %#x fw time %d max_cmds %d max_sg %d\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_serial_number,
	    sc->sc_info.mci_hw_present,
	    sc->sc_info.mci_current_fw_time,
	    sc->sc_info.mci_max_cmds,
	    sc->sc_info.mci_max_sg_elements);

	printf("%s: max_rq %d lds_pres %d lds_deg %d lds_off %d pd_pres %d\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_max_request_size,
	    sc->sc_info.mci_lds_present,
	    sc->sc_info.mci_lds_degraded,
	    sc->sc_info.mci_lds_offline,
	    sc->sc_info.mci_pd_present);

	printf("%s: pd_dsk_prs %d pd_dsk_pred_fail %d pd_dsk_fail %d\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_pd_disks_present,
	    sc->sc_info.mci_pd_disks_pred_failure,
	    sc->sc_info.mci_pd_disks_failed);

	printf("%s: nvram %d mem %d flash %d\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_nvram_size,
	    sc->sc_info.mci_memory_size,
	    sc->sc_info.mci_flash_size);

	printf("%s: ram_cor %d ram_uncor %d clus_all %d clus_act %d\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_ram_correctable_errors,
	    sc->sc_info.mci_ram_uncorrectable_errors,
	    sc->sc_info.mci_cluster_allowed,
	    sc->sc_info.mci_cluster_active);

	printf("%s: max_strps_io %d raid_lvl %#x adapt_ops %#x ld_ops %#x\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_max_strips_per_io,
	    sc->sc_info.mci_raid_levels,
	    sc->sc_info.mci_adapter_ops,
	    sc->sc_info.mci_ld_ops);

	printf("%s: strp_sz_min %d strp_sz_max %d pd_ops %#x pd_mix %#x\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_stripe_sz_ops.min,
	    sc->sc_info.mci_stripe_sz_ops.max,
	    sc->sc_info.mci_pd_ops,
	    sc->sc_info.mci_pd_mix_support);

	printf("%s: ecc_bucket %d pckg_prop %s\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_ecc_bucket_count,
	    sc->sc_info.mci_package_version);

	printf("%s: sq_nm %d prd_fail_poll %d intr_thrtl %d intr_thrtl_to %d\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_properties.mcp_seq_num,
	    sc->sc_info.mci_properties.mcp_pred_fail_poll_interval,
	    sc->sc_info.mci_properties.mcp_intr_throttle_cnt,
	    sc->sc_info.mci_properties.mcp_intr_throttle_timeout);

	printf("%s: rbld_rate %d patr_rd_rate %d bgi_rate %d cc_rate %d\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_properties.mcp_rebuild_rate,
	    sc->sc_info.mci_properties.mcp_patrol_read_rate,
	    sc->sc_info.mci_properties.mcp_bgi_rate,
	    sc->sc_info.mci_properties.mcp_cc_rate);

	printf("%s: rc_rate %d ch_flsh %d spin_cnt %d spin_dly %d clus_en %d\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_properties.mcp_recon_rate,
	    sc->sc_info.mci_properties.mcp_cache_flush_interval,
	    sc->sc_info.mci_properties.mcp_spinup_drv_cnt,
	    sc->sc_info.mci_properties.mcp_spinup_delay,
	    sc->sc_info.mci_properties.mcp_cluster_enable);

	printf("%s: coerc %d alarm %d dis_auto_rbld %d dis_bat_wrn %d ecc %d\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_properties.mcp_coercion_mode,
	    sc->sc_info.mci_properties.mcp_alarm_enable,
	    sc->sc_info.mci_properties.mcp_disable_auto_rebuild,
	    sc->sc_info.mci_properties.mcp_disable_battery_warn,
	    sc->sc_info.mci_properties.mcp_ecc_bucket_size);

	printf("%s: ecc_leak %d rest_hs %d exp_encl_dev %d\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_properties.mcp_ecc_bucket_leak_rate,
	    sc->sc_info.mci_properties.mcp_restore_hotspare_on_insertion,
	    sc->sc_info.mci_properties.mcp_expose_encl_devices);

	printf("%s: vendor %#x device %#x subvendor %#x subdevice %#x\n",
	    DEVNAME(sc),
	    sc->sc_info.mci_pci.mip_vendor,
	    sc->sc_info.mci_pci.mip_device,
	    sc->sc_info.mci_pci.mip_subvendor,
	    sc->sc_info.mci_pci.mip_subdevice);

	printf("%s: type %#x port_count %d port_addr ",
	    DEVNAME(sc),
	    sc->sc_info.mci_host.mih_type,
	    sc->sc_info.mci_host.mih_port_count);

	for (i = 0; i < 8; i++)
		printf("%.0llx ", sc->sc_info.mci_host.mih_port_addr[i]);
	printf("\n");

	printf("%s: type %.x port_count %d port_addr ",
	    DEVNAME(sc),
	    sc->sc_info.mci_device.mid_type,
	    sc->sc_info.mci_device.mid_port_count);

	for (i = 0; i < 8; i++)
		printf("%.0llx ", sc->sc_info.mci_device.mid_port_addr[i]);
	printf("\n");
#endif /* MFI_DEBUG */

	return (0);
}

int
mfi_attach(struct mfi_softc *sc, enum mfi_iop iop)
{
	struct scsibus_attach_args saa;
	uint32_t		status, frames, max_sgl;
	int			i;

	switch (iop) {
	case MFI_IOP_XSCALE:
		sc->sc_iop = &mfi_iop_xscale;
		break;
	case MFI_IOP_PPC:
		sc->sc_iop = &mfi_iop_ppc;
		break;
	case MFI_IOP_GEN2:
		sc->sc_iop = &mfi_iop_gen2;
		break;
	case MFI_IOP_SKINNY:
		sc->sc_iop = &mfi_iop_skinny;
		break;
	default:
		panic("%s: unknown iop %d", DEVNAME(sc), iop);
	}

	DNPRINTF(MFI_D_MISC, "%s: mfi_attach\n", DEVNAME(sc));

	if (mfi_transition_firmware(sc))
		return (1);

	SLIST_INIT(&sc->sc_ccb_freeq);
	mtx_init(&sc->sc_ccb_mtx, IPL_BIO);
	scsi_iopool_init(&sc->sc_iopool, sc, mfi_get_ccb, mfi_put_ccb);

	rw_init(&sc->sc_lock, "mfi_lock");

	status = mfi_fw_state(sc);
	sc->sc_max_cmds = status & MFI_STATE_MAXCMD_MASK;
	max_sgl = (status & MFI_STATE_MAXSGL_MASK) >> 16;
	if (sc->sc_64bit_dma) {
		sc->sc_max_sgl = min(max_sgl, (128 * 1024) / PAGE_SIZE + 1);
		sc->sc_sgl_size = sizeof(struct mfi_sg64);
		sc->sc_sgl_flags = MFI_FRAME_SGL64;
	} else {
		sc->sc_max_sgl = max_sgl;
		sc->sc_sgl_size = sizeof(struct mfi_sg32);
		sc->sc_sgl_flags = MFI_FRAME_SGL32;
	}
	if (iop == MFI_IOP_SKINNY)
		sc->sc_sgl_size = sizeof(struct mfi_sg_skinny);
	DNPRINTF(MFI_D_MISC, "%s: 64bit: %d max commands: %u, max sgl: %u\n",
	    DEVNAME(sc), sc->sc_64bit_dma, sc->sc_max_cmds, sc->sc_max_sgl);

	/* consumer/producer and reply queue memory */
	sc->sc_pcq = mfi_allocmem(sc, (sizeof(uint32_t) * sc->sc_max_cmds) +
	    sizeof(struct mfi_prod_cons));
	if (sc->sc_pcq == NULL) {
		printf("%s: unable to allocate reply queue memory\n",
		    DEVNAME(sc));
		goto nopcq;
	}

	/* frame memory */
	/* we are not doing 64 bit IO so only calculate # of 32 bit frames */
	frames = (sc->sc_sgl_size * sc->sc_max_sgl + MFI_FRAME_SIZE - 1) /
	    MFI_FRAME_SIZE + 1;
	sc->sc_frames_size = frames * MFI_FRAME_SIZE;
	sc->sc_frames = mfi_allocmem(sc, sc->sc_frames_size * sc->sc_max_cmds);
	if (sc->sc_frames == NULL) {
		printf("%s: unable to allocate frame memory\n", DEVNAME(sc));
		goto noframe;
	}
	/* XXX hack, fix this */
	if (MFIMEM_DVA(sc->sc_frames) & 0x3f) {
		printf("%s: improper frame alignment (%#lx) FIXME\n",
		    DEVNAME(sc), MFIMEM_DVA(sc->sc_frames));
		goto noframe;
	}

	/* sense memory */
	sc->sc_sense = mfi_allocmem(sc, sc->sc_max_cmds * MFI_SENSE_SIZE);
	if (sc->sc_sense == NULL) {
		printf("%s: unable to allocate sense memory\n", DEVNAME(sc));
		goto nosense;
	}

	/* now that we have all memory bits go initialize ccbs */
	if (mfi_init_ccb(sc)) {
		printf("%s: could not init ccb list\n", DEVNAME(sc));
		goto noinit;
	}

	/* kickstart firmware with all addresses and pointers */
	if (mfi_initialize_firmware(sc)) {
		printf("%s: could not initialize firmware\n", DEVNAME(sc));
		goto noinit;
	}

	if (mfi_get_info(sc)) {
		printf("%s: could not retrieve controller information\n",
		    DEVNAME(sc));
		goto noinit;
	}

	printf("%s: \"%s\", firmware %s", DEVNAME(sc),
	    sc->sc_info.mci_product_name, sc->sc_info.mci_package_version);
	if (letoh16(sc->sc_info.mci_memory_size) > 0)
		printf(", %uMB cache", letoh16(sc->sc_info.mci_memory_size));
	printf("\n");

	sc->sc_ld_cnt = sc->sc_info.mci_lds_present;
	for (i = 0; i < sc->sc_ld_cnt; i++)
		sc->sc_ld[i].ld_present = 1;

	saa.saa_adapter = &mfi_switch;
	saa.saa_adapter_softc = sc;
	saa.saa_adapter_buswidth = sc->sc_info.mci_max_lds;
	saa.saa_adapter_target = SDEV_NO_ADAPTER_TARGET;
	saa.saa_luns = 1;
	saa.saa_openings = sc->sc_max_cmds - 1;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	sc->sc_scsibus = (struct scsibus_softc *)
	    config_found(&sc->sc_dev, &saa, scsiprint);

	if (ISSET(sc->sc_iop->mio_flags, MFI_IOP_F_SYSPD))
		mfi_syspd(sc);

	/* enable interrupts */
	mfi_intr_enable(sc);

#if NBIO > 0
	if (bio_register(&sc->sc_dev, mfi_ioctl) != 0)
		panic("%s: controller registration failed", DEVNAME(sc));
	else
		sc->sc_ioctl = mfi_ioctl;

#ifndef SMALL_KERNEL
	if (mfi_create_sensors(sc) != 0)
		printf("%s: unable to create sensors\n", DEVNAME(sc));
#endif
#endif /* NBIO > 0 */

	return (0);
noinit:
	mfi_freemem(sc, sc->sc_sense);
nosense:
	mfi_freemem(sc, sc->sc_frames);
noframe:
	mfi_freemem(sc, sc->sc_pcq);
nopcq:
	return (1);
}

int
mfi_syspd(struct mfi_softc *sc)
{
	struct scsibus_attach_args saa;
	struct mfi_pd_link *pl;
	struct mfi_pd_list *pd;
	u_int npds, i;

	sc->sc_pd = malloc(sizeof(*sc->sc_pd), M_DEVBUF, M_WAITOK|M_ZERO);
	if (sc->sc_pd == NULL)
		return (1);

	pd = malloc(sizeof(*pd), M_TEMP, M_WAITOK|M_ZERO);
	if (pd == NULL)
		goto nopdsc;

	if (mfi_mgmt(sc, MR_DCMD_PD_GET_LIST, MFI_DATA_IN,
	    sizeof(*pd), pd, NULL) != 0)
		goto nopd;

	npds = letoh32(pd->mpl_no_pd);
	for (i = 0; i < npds; i++) {
		pl = malloc(sizeof(*pl), M_DEVBUF, M_WAITOK|M_ZERO);
		if (pl == NULL)
			goto nopl;

		pl->pd_id = pd->mpl_address[i].mpa_pd_id;
		sc->sc_pd->pd_links[i] = pl;
	}

	free(pd, M_TEMP, sizeof *pd);

	saa.saa_adapter = &mfi_pd_switch;
	saa.saa_adapter_softc = sc;
	saa.saa_adapter_buswidth = MFI_MAX_PD;
	saa.saa_adapter_target = SDEV_NO_ADAPTER_TARGET;
	saa.saa_luns = 8;
	saa.saa_openings = sc->sc_max_cmds - 1;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	sc->sc_pd->pd_scsibus = (struct scsibus_softc *)
	    config_found(&sc->sc_dev, &saa, scsiprint);

	return (0);
nopl:
	for (i = 0; i < npds; i++) {
		pl = sc->sc_pd->pd_links[i];
		if (pl == NULL)
			break;

		free(pl, M_DEVBUF, sizeof *pl);
	}
nopd:
	free(pd, M_TEMP, sizeof *pd);
nopdsc:
	free(sc->sc_pd, M_DEVBUF, sizeof *sc->sc_pd);
	return (1);
}

void
mfi_poll(struct mfi_softc *sc, struct mfi_ccb *ccb)
{
	struct mfi_frame_header *hdr;
	int to = 0;

	DNPRINTF(MFI_D_CMD, "%s: mfi_poll\n", DEVNAME(sc));

	hdr = &ccb->ccb_frame->mfr_header;
	hdr->mfh_cmd_status = 0xff;
	hdr->mfh_flags |= MFI_FRAME_DONT_POST_IN_REPLY_QUEUE;

	mfi_start(sc, ccb);

	for (;;) {
		delay(1000);

		bus_dmamap_sync(sc->sc_dmat, MFIMEM_MAP(sc->sc_frames),
		    ccb->ccb_pframe_offset, sc->sc_frames_size,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

		if (hdr->mfh_cmd_status != 0xff)
			break;

		if (to++ > 5000) {
			printf("%s: timeout on ccb %d\n", DEVNAME(sc),
			    hdr->mfh_context);
			ccb->ccb_flags |= MFI_CCB_F_ERR;
			break;
		}

		bus_dmamap_sync(sc->sc_dmat, MFIMEM_MAP(sc->sc_frames),
		    ccb->ccb_pframe_offset, sc->sc_frames_size,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}

	if (ccb->ccb_len > 0) {
		bus_dmamap_sync(sc->sc_dmat, ccb->ccb_dmamap, 0,
		    ccb->ccb_dmamap->dm_mapsize,
		    (ccb->ccb_direction & MFI_DATA_IN) ?
		    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);

		bus_dmamap_unload(sc->sc_dmat, ccb->ccb_dmamap);
	}

	ccb->ccb_done(sc, ccb);
}

void
mfi_exec(struct mfi_softc *sc, struct mfi_ccb *ccb)
{
	struct mutex m;

	mtx_init(&m, IPL_BIO);

#ifdef DIAGNOSTIC
	if (ccb->ccb_cookie != NULL || ccb->ccb_done != NULL)
		panic("mfi_exec called with cookie or done set");
#endif

	ccb->ccb_cookie = &m;
	ccb->ccb_done = mfi_exec_done;

	mfi_start(sc, ccb);

	mtx_enter(&m);
	while (ccb->ccb_cookie != NULL)
		msleep_nsec(ccb, &m, PRIBIO, "mfiexec", INFSLP);
	mtx_leave(&m);
}

void
mfi_exec_done(struct mfi_softc *sc, struct mfi_ccb *ccb)
{
	struct mutex *m = ccb->ccb_cookie;

	mtx_enter(m);
	ccb->ccb_cookie = NULL;
	wakeup_one(ccb);
	mtx_leave(m);
}

int
mfi_intr(void *arg)
{
	struct mfi_softc	*sc = arg;
	struct mfi_prod_cons	*pcq = MFIMEM_KVA(sc->sc_pcq);
	struct mfi_ccb		*ccb;
	uint32_t		producer, consumer, ctx;
	int			claimed = 0;

	if (!mfi_my_intr(sc))
		return (0);

	bus_dmamap_sync(sc->sc_dmat, MFIMEM_MAP(sc->sc_pcq),
	    0, MFIMEM_LEN(sc->sc_pcq),
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	producer = letoh32(pcq->mpc_producer);
	consumer = letoh32(pcq->mpc_consumer);

	DNPRINTF(MFI_D_INTR, "%s: mfi_intr %p %p\n", DEVNAME(sc), sc, pcq);

	while (consumer != producer) {
		DNPRINTF(MFI_D_INTR, "%s: mfi_intr pi %#x ci %#x\n",
		    DEVNAME(sc), producer, consumer);

		ctx = pcq->mpc_reply_q[consumer];
		pcq->mpc_reply_q[consumer] = MFI_INVALID_CTX;
		if (ctx == MFI_INVALID_CTX)
			printf("%s: invalid context, p: %d c: %d\n",
			    DEVNAME(sc), producer, consumer);
		else {
			/* XXX remove from queue and call scsi_done */
			ccb = &sc->sc_ccb[ctx];
			DNPRINTF(MFI_D_INTR, "%s: mfi_intr context %#x\n",
			    DEVNAME(sc), ctx);
			mfi_done(sc, ccb);

			claimed = 1;
		}
		consumer++;
		if (consumer == (sc->sc_max_cmds + 1))
			consumer = 0;
	}

	pcq->mpc_consumer = htole32(consumer);

	bus_dmamap_sync(sc->sc_dmat, MFIMEM_MAP(sc->sc_pcq),
	    0, MFIMEM_LEN(sc->sc_pcq),
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (claimed);
}

int
mfi_scsi_io(struct mfi_softc *sc, struct mfi_ccb *ccb,
    struct scsi_xfer *xs, uint64_t blockno, uint32_t blockcnt)
{
	struct scsi_link	*link = xs->sc_link;
	struct mfi_io_frame	*io;

	DNPRINTF(MFI_D_CMD, "%s: mfi_scsi_io: %d\n",
	    DEVNAME((struct mfi_softc *)link->bus->sb_adapter_softc), link->target);

	if (!xs->data)
		return (1);

	io = &ccb->ccb_frame->mfr_io;
	if (xs->flags & SCSI_DATA_IN) {
		io->mif_header.mfh_cmd = MFI_CMD_LD_READ;
		ccb->ccb_direction = MFI_DATA_IN;
	} else {
		io->mif_header.mfh_cmd = MFI_CMD_LD_WRITE;
		ccb->ccb_direction = MFI_DATA_OUT;
	}
	io->mif_header.mfh_target_id = link->target;
	io->mif_header.mfh_timeout = 0;
	io->mif_header.mfh_flags = 0;
	io->mif_header.mfh_sense_len = MFI_SENSE_SIZE;
	io->mif_header.mfh_data_len = htole32(blockcnt);
	io->mif_lba = htole64(blockno);
	io->mif_sense_addr = htole64(ccb->ccb_psense);

	ccb->ccb_done = mfi_scsi_xs_done;
	ccb->ccb_cookie = xs;
	ccb->ccb_frame_size = MFI_IO_FRAME_SIZE;
	ccb->ccb_sgl = &io->mif_sgl;
	ccb->ccb_data = xs->data;
	ccb->ccb_len = xs->datalen;

	if (mfi_create_sgl(sc, ccb, (xs->flags & SCSI_NOSLEEP) ?
	    BUS_DMA_NOWAIT : BUS_DMA_WAITOK))
		return (1);

	return (0);
}

void
mfi_scsi_xs_done(struct mfi_softc *sc, struct mfi_ccb *ccb)
{
	struct scsi_xfer	*xs = ccb->ccb_cookie;
	struct mfi_frame_header	*hdr = &ccb->ccb_frame->mfr_header;

	DNPRINTF(MFI_D_INTR, "%s: mfi_scsi_xs_done %p %p\n",
	    DEVNAME(sc), ccb, ccb->ccb_frame);

	switch (hdr->mfh_cmd_status) {
	case MFI_STAT_OK:
		xs->resid = 0;
		break;

	case MFI_STAT_SCSI_DONE_WITH_ERROR:
		xs->error = XS_SENSE;
		xs->resid = 0;
		memset(&xs->sense, 0, sizeof(xs->sense));
		memcpy(&xs->sense, ccb->ccb_sense, sizeof(xs->sense));
		break;

	case MFI_STAT_DEVICE_NOT_FOUND:
		xs->error = XS_SELTIMEOUT;
		break;

	default:
		xs->error = XS_DRIVER_STUFFUP;
		DNPRINTF(MFI_D_CMD,
		    "%s: mfi_scsi_xs_done stuffup %02x on %02x\n",
		    DEVNAME(sc), hdr->mfh_cmd_status, xs->cmd.opcode);

		if (hdr->mfh_scsi_status != 0) {
			DNPRINTF(MFI_D_INTR,
			    "%s: mfi_scsi_xs_done sense %#x %p %p\n",
			    DEVNAME(sc), hdr->mfh_scsi_status,
			    &xs->sense, ccb->ccb_sense);
			memset(&xs->sense, 0, sizeof(xs->sense));
			memcpy(&xs->sense, ccb->ccb_sense,
			    sizeof(struct scsi_sense_data));
			xs->error = XS_SENSE;
		}
		break;
	}

	KERNEL_LOCK();
	scsi_done(xs);
	KERNEL_UNLOCK();
}

int
mfi_scsi_ld(struct mfi_softc *sc, struct mfi_ccb *ccb, struct scsi_xfer *xs)
{
	struct scsi_link	*link = xs->sc_link;
	struct mfi_pass_frame	*pf;

	DNPRINTF(MFI_D_CMD, "%s: mfi_scsi_ld: %d\n",
	    DEVNAME((struct mfi_softc *)link->bus->sb_adapter_softc), link->target);

	pf = &ccb->ccb_frame->mfr_pass;
	pf->mpf_header.mfh_cmd = MFI_CMD_LD_SCSI_IO;
	pf->mpf_header.mfh_target_id = link->target;
	pf->mpf_header.mfh_lun_id = 0;
	pf->mpf_header.mfh_cdb_len = xs->cmdlen;
	pf->mpf_header.mfh_timeout = 0;
	pf->mpf_header.mfh_data_len = htole32(xs->datalen); /* XXX */
	pf->mpf_header.mfh_sense_len = MFI_SENSE_SIZE;

	pf->mpf_sense_addr = htole64(ccb->ccb_psense);

	memset(pf->mpf_cdb, 0, 16);
	memcpy(pf->mpf_cdb, &xs->cmd, xs->cmdlen);

	ccb->ccb_done = mfi_scsi_xs_done;
	ccb->ccb_cookie = xs;
	ccb->ccb_frame_size = MFI_PASS_FRAME_SIZE;
	ccb->ccb_sgl = &pf->mpf_sgl;

	if (xs->flags & (SCSI_DATA_IN | SCSI_DATA_OUT))
		ccb->ccb_direction = xs->flags & SCSI_DATA_IN ?
		    MFI_DATA_IN : MFI_DATA_OUT;
	else
		ccb->ccb_direction = MFI_DATA_NONE;

	if (xs->data) {
		ccb->ccb_data = xs->data;
		ccb->ccb_len = xs->datalen;

		if (mfi_create_sgl(sc, ccb, (xs->flags & SCSI_NOSLEEP) ?
		    BUS_DMA_NOWAIT : BUS_DMA_WAITOK))
			return (1);
	}

	return (0);
}

void
mfi_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link	*link = xs->sc_link;
	struct mfi_softc	*sc = link->bus->sb_adapter_softc;
	struct mfi_ccb		*ccb = xs->io;
	struct scsi_rw		*rw;
	struct scsi_rw_10	*rw10;
	struct scsi_rw_16	*rw16;
	uint64_t		blockno;
	uint32_t		blockcnt;
	uint8_t			target = link->target;
	union mfi_mbox		mbox;

	DNPRINTF(MFI_D_CMD, "%s: mfi_scsi_cmd opcode: %#x\n",
	    DEVNAME(sc), xs->cmd.opcode);

	KERNEL_UNLOCK();

	if (!sc->sc_ld[target].ld_present) {
		DNPRINTF(MFI_D_CMD, "%s: invalid target %d\n",
		    DEVNAME(sc), target);
		goto stuffup;
	}

	mfi_scrub_ccb(ccb);

	xs->error = XS_NOERROR;

	switch (xs->cmd.opcode) {
	/* IO path */
	case READ_10:
	case WRITE_10:
		rw10 = (struct scsi_rw_10 *)&xs->cmd;
		blockno = (uint64_t)_4btol(rw10->addr);
		blockcnt = _2btol(rw10->length);
		if (mfi_scsi_io(sc, ccb, xs, blockno, blockcnt))
			goto stuffup;
		break;

	case READ_COMMAND:
	case WRITE_COMMAND:
		rw = (struct scsi_rw *)&xs->cmd;
		blockno =
		    (uint64_t)(_3btol(rw->addr) & (SRW_TOPADDR << 16 | 0xffff));
		blockcnt = rw->length ? rw->length : 0x100;
		if (mfi_scsi_io(sc, ccb, xs, blockno, blockcnt))
			goto stuffup;
		break;

	case READ_16:
	case WRITE_16:
		rw16 = (struct scsi_rw_16 *)&xs->cmd;
		blockno = _8btol(rw16->addr);
		blockcnt = _4btol(rw16->length);
		if (mfi_scsi_io(sc, ccb, xs, blockno, blockcnt))
			goto stuffup;
		break;

	case SYNCHRONIZE_CACHE:
		mbox.b[0] = MR_FLUSH_CTRL_CACHE | MR_FLUSH_DISK_CACHE;
		if (mfi_do_mgmt(sc, ccb, MR_DCMD_CTRL_CACHE_FLUSH,
		    MFI_DATA_NONE, 0, NULL, &mbox))
			goto stuffup;

		goto complete;
		/* NOTREACHED */

	default:
		if (mfi_scsi_ld(sc, ccb, xs))
			goto stuffup;
		break;
	}

	DNPRINTF(MFI_D_CMD, "%s: start io %d\n", DEVNAME(sc), target);

	if (xs->flags & SCSI_POLL)
		mfi_poll(sc, ccb);
	else
		mfi_start(sc, ccb);

	KERNEL_LOCK();
	return;

stuffup:
	xs->error = XS_DRIVER_STUFFUP;
complete:
	KERNEL_LOCK();
	scsi_done(xs);
}

u_int
mfi_default_sgd_load(struct mfi_softc *sc, struct mfi_ccb *ccb)
{
	struct mfi_frame_header	*hdr = &ccb->ccb_frame->mfr_header;
	union mfi_sgl		*sgl = ccb->ccb_sgl;
	bus_dma_segment_t	*sgd = ccb->ccb_dmamap->dm_segs;
	int			 i;

	hdr->mfh_flags |= sc->sc_sgl_flags;

	for (i = 0; i < ccb->ccb_dmamap->dm_nsegs; i++) {
		if (sc->sc_64bit_dma) {
			sgl->sg64[i].addr = htole64(sgd[i].ds_addr);
			sgl->sg64[i].len = htole32(sgd[i].ds_len);
			DNPRINTF(MFI_D_DMA, "%s: addr: %#llx  len: %#x\n",
			    DEVNAME(sc), sgl->sg64[i].addr, sgl->sg64[i].len);
		} else {
			sgl->sg32[i].addr = htole32(sgd[i].ds_addr);
			sgl->sg32[i].len = htole32(sgd[i].ds_len);
			DNPRINTF(MFI_D_DMA, "%s: addr: %#x  len: %#x\n",
			    DEVNAME(sc), sgl->sg32[i].addr, sgl->sg32[i].len);
		}
	}

	return (ccb->ccb_dmamap->dm_nsegs *
	    (sc->sc_64bit_dma ? sizeof(sgl->sg64) : sizeof(sgl->sg32)));
}

int
mfi_create_sgl(struct mfi_softc *sc, struct mfi_ccb *ccb, int flags)
{
	struct mfi_frame_header	*hdr = &ccb->ccb_frame->mfr_header;
	int			error;

	DNPRINTF(MFI_D_DMA, "%s: mfi_create_sgl %p\n", DEVNAME(sc),
	    ccb->ccb_data);

	if (!ccb->ccb_data) {
		hdr->mfh_sg_count = 0;
		return (1);
	}

	error = bus_dmamap_load(sc->sc_dmat, ccb->ccb_dmamap,
	    ccb->ccb_data, ccb->ccb_len, NULL, flags);
	if (error) {
		if (error == EFBIG)
			printf("more than %d dma segs\n",
			    sc->sc_max_sgl);
		else
			printf("error %d loading dma map\n", error);
		return (1);
	}

	ccb->ccb_frame_size += mfi_sgd_load(sc, ccb);

	if (ccb->ccb_direction == MFI_DATA_IN) {
		hdr->mfh_flags |= MFI_FRAME_DIR_READ;
		bus_dmamap_sync(sc->sc_dmat, ccb->ccb_dmamap, 0,
		    ccb->ccb_dmamap->dm_mapsize, BUS_DMASYNC_PREREAD);
	} else {
		hdr->mfh_flags |= MFI_FRAME_DIR_WRITE;
		bus_dmamap_sync(sc->sc_dmat, ccb->ccb_dmamap, 0,
		    ccb->ccb_dmamap->dm_mapsize, BUS_DMASYNC_PREWRITE);
	}

	hdr->mfh_sg_count = ccb->ccb_dmamap->dm_nsegs;
	ccb->ccb_extra_frames = (ccb->ccb_frame_size - 1) / MFI_FRAME_SIZE;

	DNPRINTF(MFI_D_DMA, "%s: sg_count: %d  frame_size: %d  frames_size: %d"
	    "  dm_nsegs: %d  extra_frames: %d\n",
	    DEVNAME(sc),
	    hdr->mfh_sg_count,
	    ccb->ccb_frame_size,
	    sc->sc_frames_size,
	    ccb->ccb_dmamap->dm_nsegs,
	    ccb->ccb_extra_frames);

	return (0);
}

int
mfi_mgmt(struct mfi_softc *sc, uint32_t opc, uint32_t dir, uint32_t len,
    void *buf, const union mfi_mbox *mbox)
{
	struct mfi_ccb *ccb;
	int rv;

	ccb = scsi_io_get(&sc->sc_iopool, 0);
	mfi_scrub_ccb(ccb);
	rv = mfi_do_mgmt(sc, ccb, opc, dir, len, buf, mbox);
	scsi_io_put(&sc->sc_iopool, ccb);

	return (rv);
}

int
mfi_do_mgmt(struct mfi_softc *sc, struct mfi_ccb *ccb, uint32_t opc,
    uint32_t dir, uint32_t len, void *buf, const union mfi_mbox *mbox)
{
	struct mfi_dcmd_frame *dcmd;
	uint8_t *dma_buf = NULL;
	int rv = EINVAL;

	DNPRINTF(MFI_D_MISC, "%s: mfi_do_mgmt %#x\n", DEVNAME(sc), opc);

	dma_buf = dma_alloc(len, cold ? PR_NOWAIT : PR_WAITOK);
	if (dma_buf == NULL)
		goto done;

	dcmd = &ccb->ccb_frame->mfr_dcmd;
	memset(&dcmd->mdf_mbox, 0, sizeof(dcmd->mdf_mbox));
	dcmd->mdf_header.mfh_cmd = MFI_CMD_DCMD;
	dcmd->mdf_header.mfh_timeout = 0;

	dcmd->mdf_opcode = opc;
	dcmd->mdf_header.mfh_data_len = 0;
	ccb->ccb_direction = dir;

	ccb->ccb_frame_size = MFI_DCMD_FRAME_SIZE;

	/* handle special opcodes */
	if (mbox != NULL)
		memcpy(&dcmd->mdf_mbox, mbox, sizeof(dcmd->mdf_mbox));

	if (dir != MFI_DATA_NONE) {
		if (dir == MFI_DATA_OUT)
			memcpy(dma_buf, buf, len);
		dcmd->mdf_header.mfh_data_len = len;
		ccb->ccb_data = dma_buf;
		ccb->ccb_len = len;
		ccb->ccb_sgl = &dcmd->mdf_sgl;

		if (mfi_create_sgl(sc, ccb, cold ? BUS_DMA_NOWAIT :
		    BUS_DMA_WAITOK)) {
			rv = EINVAL;
			goto done;
		}
	}

	if (cold) {
		ccb->ccb_done = mfi_empty_done;
		mfi_poll(sc, ccb);
	} else
		mfi_exec(sc, ccb);

	if (dcmd->mdf_header.mfh_cmd_status != MFI_STAT_OK) {
		if (dcmd->mdf_header.mfh_cmd_status == MFI_STAT_WRONG_STATE)
			rv = ENXIO;
		else
			rv = EIO;
		goto done;
	}

	if (dir == MFI_DATA_IN)
		memcpy(buf, dma_buf, len);

	rv = 0;
done:
	if (dma_buf)
		dma_free(dma_buf, len);

	return (rv);
}

int
mfi_scsi_ioctl(struct scsi_link *link, u_long cmd, caddr_t addr, int flag)
{
	struct mfi_softc	*sc = link->bus->sb_adapter_softc;

	DNPRINTF(MFI_D_IOCTL, "%s: mfi_scsi_ioctl\n", DEVNAME(sc));

	switch (cmd) {
	case DIOCGCACHE:
	case DIOCSCACHE:
		return (mfi_ioctl_cache(link, cmd, (struct dk_cache *)addr));
		break;

	default:
		if (sc->sc_ioctl)
			return (sc->sc_ioctl(&sc->sc_dev, cmd, addr));
		break;
	}

	return (ENOTTY);
}

int
mfi_ioctl_cache(struct scsi_link *link, u_long cmd,  struct dk_cache *dc)
{
	struct mfi_softc	*sc = link->bus->sb_adapter_softc;
	int			 rv, wrenable, rdenable;
	struct mfi_ld_prop	 ldp;
	union mfi_mbox		 mbox;

	if (mfi_get_info(sc)) {
		rv = EIO;
		goto done;
	}

	if (!sc->sc_ld[link->target].ld_present) {
		rv = EIO;
		goto done;
	}

	memset(&mbox, 0, sizeof(mbox));
	mbox.b[0] = link->target;
	if ((rv = mfi_mgmt(sc, MR_DCMD_LD_GET_PROPERTIES, MFI_DATA_IN,
	    sizeof(ldp), &ldp, &mbox)) != 0)
		goto done;

	if (sc->sc_info.mci_memory_size > 0) {
		wrenable = ISSET(ldp.mlp_cur_cache_policy,
		    MR_LD_CACHE_ALLOW_WRITE_CACHE)? 1 : 0;
		rdenable = ISSET(ldp.mlp_cur_cache_policy,
		    MR_LD_CACHE_ALLOW_READ_CACHE)? 1 : 0;
	} else {
		wrenable = ISSET(ldp.mlp_diskcache_policy,
		    MR_LD_DISK_CACHE_ENABLE)? 1 : 0;
		rdenable = 0;
	}

	if (cmd == DIOCGCACHE) {
		dc->wrcache = wrenable;
		dc->rdcache = rdenable;
		goto done;
	} /* else DIOCSCACHE */

	if (((dc->wrcache) ? 1 : 0) == wrenable &&
	    ((dc->rdcache) ? 1 : 0) == rdenable)
		goto done;

	memset(&mbox, 0, sizeof(mbox));
	mbox.b[0] = ldp.mlp_ld.mld_target;
	mbox.b[1] = ldp.mlp_ld.mld_res;
	mbox.s[1] = ldp.mlp_ld.mld_seq;

	if (sc->sc_info.mci_memory_size > 0) {
		if (dc->rdcache)
			SET(ldp.mlp_cur_cache_policy,
			    MR_LD_CACHE_ALLOW_READ_CACHE);
		else
			CLR(ldp.mlp_cur_cache_policy,
			    MR_LD_CACHE_ALLOW_READ_CACHE);
		if (dc->wrcache)
			SET(ldp.mlp_cur_cache_policy,
			    MR_LD_CACHE_ALLOW_WRITE_CACHE);
		else
			CLR(ldp.mlp_cur_cache_policy,
			    MR_LD_CACHE_ALLOW_WRITE_CACHE);
	} else {
		if (dc->rdcache) {
			rv = EOPNOTSUPP;
			goto done;
		}
		if (dc->wrcache)
			ldp.mlp_diskcache_policy = MR_LD_DISK_CACHE_ENABLE;
		else
			ldp.mlp_diskcache_policy = MR_LD_DISK_CACHE_DISABLE;
	}

	rv = mfi_mgmt(sc, MR_DCMD_LD_SET_PROPERTIES, MFI_DATA_OUT, sizeof(ldp),
	    &ldp, &mbox);

done:
	return (rv);
}

#if NBIO > 0
int
mfi_ioctl(struct device *dev, u_long cmd, caddr_t addr)
{
	struct mfi_softc	*sc = (struct mfi_softc *)dev;
	int error = 0;

	DNPRINTF(MFI_D_IOCTL, "%s: mfi_ioctl ", DEVNAME(sc));

	rw_enter_write(&sc->sc_lock);

	switch (cmd) {
	case BIOCINQ:
		DNPRINTF(MFI_D_IOCTL, "inq\n");
		error = mfi_ioctl_inq(sc, (struct bioc_inq *)addr);
		break;

	case BIOCVOL:
		DNPRINTF(MFI_D_IOCTL, "vol\n");
		error = mfi_ioctl_vol(sc, (struct bioc_vol *)addr);
		break;

	case BIOCDISK:
		DNPRINTF(MFI_D_IOCTL, "disk\n");
		error = mfi_ioctl_disk(sc, (struct bioc_disk *)addr);
		break;

	case BIOCALARM:
		DNPRINTF(MFI_D_IOCTL, "alarm\n");
		error = mfi_ioctl_alarm(sc, (struct bioc_alarm *)addr);
		break;

	case BIOCBLINK:
		DNPRINTF(MFI_D_IOCTL, "blink\n");
		error = mfi_ioctl_blink(sc, (struct bioc_blink *)addr);
		break;

	case BIOCSETSTATE:
		DNPRINTF(MFI_D_IOCTL, "setstate\n");
		error = mfi_ioctl_setstate(sc, (struct bioc_setstate *)addr);
		break;

	case BIOCPATROL:
		DNPRINTF(MFI_D_IOCTL, "patrol\n");
		error = mfi_ioctl_patrol(sc, (struct bioc_patrol *)addr);
		break;

	default:
		DNPRINTF(MFI_D_IOCTL, " invalid ioctl\n");
		error = ENOTTY;
	}

	rw_exit_write(&sc->sc_lock);

	return (error);
}

int
mfi_bio_getitall(struct mfi_softc *sc)
{
	int			i, d, size, rv = EINVAL;
	union mfi_mbox		mbox;
	struct mfi_conf		*cfg = NULL;
	struct mfi_ld_details	*ld_det = NULL;

	/* get info */
	if (mfi_get_info(sc)) {
		DNPRINTF(MFI_D_IOCTL, "%s: mfi_get_info failed\n",
		    DEVNAME(sc));
		goto done;
	}

	/* send single element command to retrieve size for full structure */
	cfg = malloc(sizeof *cfg, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (cfg == NULL)
		goto done;
	if (mfi_mgmt(sc, MR_DCMD_CONF_GET, MFI_DATA_IN, sizeof *cfg, cfg,
	    NULL)) {
		free(cfg, M_DEVBUF, sizeof *cfg);
		goto done;
	}

	size = cfg->mfc_size;
	free(cfg, M_DEVBUF, sizeof *cfg);

	/* memory for read config */
	cfg = malloc(size, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (cfg == NULL)
		goto done;
	if (mfi_mgmt(sc, MR_DCMD_CONF_GET, MFI_DATA_IN, size, cfg, NULL)) {
		free(cfg, M_DEVBUF, size);
		goto done;
	}

	/* replace current pointer with new one */
	if (sc->sc_cfg)
		free(sc->sc_cfg, M_DEVBUF, 0);
	sc->sc_cfg = cfg;

	/* get all ld info */
	if (mfi_mgmt(sc, MR_DCMD_LD_GET_LIST, MFI_DATA_IN,
	    sizeof(sc->sc_ld_list), &sc->sc_ld_list, NULL))
		goto done;

	/* get memory for all ld structures */
	size = cfg->mfc_no_ld * sizeof(struct mfi_ld_details);
	if (sc->sc_ld_sz != size) {
		if (sc->sc_ld_details)
			free(sc->sc_ld_details, M_DEVBUF, 0);

		ld_det = malloc(size, M_DEVBUF, M_NOWAIT | M_ZERO);
		if (ld_det == NULL)
			goto done;
		sc->sc_ld_sz = size;
		sc->sc_ld_details = ld_det;
	}

	/* find used physical disks */
	size = sizeof(struct mfi_ld_details);
	for (i = 0, d = 0; i < cfg->mfc_no_ld; i++) {
		memset(&mbox, 0, sizeof(mbox));
		mbox.b[0] = sc->sc_ld_list.mll_list[i].mll_ld.mld_target;
		if (mfi_mgmt(sc, MR_DCMD_LD_GET_INFO, MFI_DATA_IN, size,
		    &sc->sc_ld_details[i], &mbox))
			goto done;

		d += sc->sc_ld_details[i].mld_cfg.mlc_parm.mpa_no_drv_per_span *
		    sc->sc_ld_details[i].mld_cfg.mlc_parm.mpa_span_depth;
	}
	sc->sc_no_pd = d;

	rv = 0;
done:
	return (rv);
}

int
mfi_ioctl_inq(struct mfi_softc *sc, struct bioc_inq *bi)
{
	int			rv = EINVAL;
	struct mfi_conf		*cfg = NULL;

	DNPRINTF(MFI_D_IOCTL, "%s: mfi_ioctl_inq\n", DEVNAME(sc));

	if (mfi_bio_getitall(sc)) {
		DNPRINTF(MFI_D_IOCTL, "%s: mfi_bio_getitall failed\n",
		    DEVNAME(sc));
		goto done;
	}

	/* count unused disks as volumes */
	if (sc->sc_cfg == NULL)
		goto done;
	cfg = sc->sc_cfg;

	bi->bi_nodisk = sc->sc_info.mci_pd_disks_present;
	bi->bi_novol = cfg->mfc_no_ld + cfg->mfc_no_hs;
#if notyet
	bi->bi_novol = cfg->mfc_no_ld + cfg->mfc_no_hs +
	    (bi->bi_nodisk - sc->sc_no_pd);
#endif
	/* tell bio who we are */
	strlcpy(bi->bi_dev, DEVNAME(sc), sizeof(bi->bi_dev));

	rv = 0;
done:
	return (rv);
}

int
mfi_ioctl_vol(struct mfi_softc *sc, struct bioc_vol *bv)
{
	int			i, per, rv = EINVAL;
	struct scsi_link	*link;
	struct device		*dev;

	DNPRINTF(MFI_D_IOCTL, "%s: mfi_ioctl_vol %#x\n",
	    DEVNAME(sc), bv->bv_volid);

	/* we really could skip and expect that inq took care of it */
	if (mfi_bio_getitall(sc)) {
		DNPRINTF(MFI_D_IOCTL, "%s: mfi_bio_getitall failed\n",
		    DEVNAME(sc));
		goto done;
	}

	if (bv->bv_volid >= sc->sc_ld_list.mll_no_ld) {
		/* go do hotspares & unused disks */
		rv = mfi_bio_hs(sc, bv->bv_volid, MFI_MGMT_VD, bv);
		goto done;
	}

	i = bv->bv_volid;
	link = scsi_get_link(sc->sc_scsibus, i, 0);
	if (link != NULL && link->device_softc != NULL) {
		dev = link->device_softc;
		strlcpy(bv->bv_dev, dev->dv_xname, sizeof(bv->bv_dev));
	}

	switch(sc->sc_ld_list.mll_list[i].mll_state) {
	case MFI_LD_OFFLINE:
		bv->bv_status = BIOC_SVOFFLINE;
		break;

	case MFI_LD_PART_DEGRADED:
	case MFI_LD_DEGRADED:
		bv->bv_status = BIOC_SVDEGRADED;
		break;

	case MFI_LD_ONLINE:
		bv->bv_status = BIOC_SVONLINE;
		break;

	default:
		bv->bv_status = BIOC_SVINVALID;
		DNPRINTF(MFI_D_IOCTL, "%s: invalid logical disk state %#x\n",
		    DEVNAME(sc),
		    sc->sc_ld_list.mll_list[i].mll_state);
	}

	/* additional status can modify MFI status */
	switch (sc->sc_ld_details[i].mld_progress.mlp_in_prog) {
	case MFI_LD_PROG_CC:
		bv->bv_status = BIOC_SVSCRUB;
		per = (int)sc->sc_ld_details[i].mld_progress.mlp_cc.mp_progress;
		bv->bv_percent = (per * 100) / 0xffff;
		bv->bv_seconds =
		    sc->sc_ld_details[i].mld_progress.mlp_cc.mp_elapsed_seconds;
		break;

	case MFI_LD_PROG_BGI:
		bv->bv_status = BIOC_SVSCRUB;
		per = (int)sc->sc_ld_details[i].mld_progress.mlp_bgi.mp_progress;
		bv->bv_percent = (per * 100) / 0xffff;
		bv->bv_seconds =
		    sc->sc_ld_details[i].mld_progress.mlp_bgi.mp_elapsed_seconds;
		break;

	case MFI_LD_PROG_FGI:
	case MFI_LD_PROG_RECONSTRUCT:
		/* nothing yet */
		break;
	}

	if (sc->sc_ld_details[i].mld_cfg.mlc_prop.mlp_cur_cache_policy & 0x01)
		bv->bv_cache = BIOC_CVWRITEBACK;
	else
		bv->bv_cache = BIOC_CVWRITETHROUGH;

	/*
	 * The RAID levels are determined per the SNIA DDF spec, this is only
	 * a subset that is valid for the MFI controller.
	 */
	bv->bv_level = sc->sc_ld_details[i].mld_cfg.mlc_parm.mpa_pri_raid;
	if (sc->sc_ld_details[i].mld_cfg.mlc_parm.mpa_sec_raid ==
	    MFI_DDF_SRL_SPANNED)
		bv->bv_level *= 10;

	bv->bv_nodisk = sc->sc_ld_details[i].mld_cfg.mlc_parm.mpa_no_drv_per_span *
	    sc->sc_ld_details[i].mld_cfg.mlc_parm.mpa_span_depth;

	bv->bv_size = sc->sc_ld_details[i].mld_size * 512; /* bytes per block */

	rv = 0;
done:
	return (rv);
}

int
mfi_ioctl_disk(struct mfi_softc *sc, struct bioc_disk *bd)
{
	struct mfi_conf		*cfg;
	struct mfi_array	*ar;
	struct mfi_ld_cfg	*ld;
	struct mfi_pd_details	*pd;
	struct mfi_pd_progress	*mfp;
	struct mfi_progress	*mp;
	struct scsi_inquiry_data *inqbuf;
	char			vend[8+16+4+1], *vendp;
	int			rv = EINVAL;
	int			arr, vol, disk, span;
	union mfi_mbox		mbox;

	DNPRINTF(MFI_D_IOCTL, "%s: mfi_ioctl_disk %#x\n",
	    DEVNAME(sc), bd->bd_diskid);

	/* we really could skip and expect that inq took care of it */
	if (mfi_bio_getitall(sc)) {
		DNPRINTF(MFI_D_IOCTL, "%s: mfi_bio_getitall failed\n",
		    DEVNAME(sc));
		return (rv);
	}
	cfg = sc->sc_cfg;

	pd = malloc(sizeof *pd, M_DEVBUF, M_WAITOK);

	ar = cfg->mfc_array;
	vol = bd->bd_volid;
	if (vol >= cfg->mfc_no_ld) {
		/* do hotspares */
		rv = mfi_bio_hs(sc, bd->bd_volid, MFI_MGMT_SD, bd);
		goto freeme;
	}

	/* calculate offset to ld structure */
	ld = (struct mfi_ld_cfg *)(
	    ((uint8_t *)cfg) + offsetof(struct mfi_conf, mfc_array) +
	    cfg->mfc_array_size * cfg->mfc_no_array);

	/* use span 0 only when raid group is not spanned */
	if (ld[vol].mlc_parm.mpa_span_depth > 1)
		span = bd->bd_diskid / ld[vol].mlc_parm.mpa_no_drv_per_span;
	else
		span = 0;
	arr = ld[vol].mlc_span[span].mls_index;

	/* offset disk into pd list */
	disk = bd->bd_diskid % ld[vol].mlc_parm.mpa_no_drv_per_span;
	bd->bd_target = ar[arr].pd[disk].mar_enc_slot;

	/* get status */
	switch (ar[arr].pd[disk].mar_pd_state){
	case MFI_PD_UNCONFIG_GOOD:
	case MFI_PD_FAILED:
		bd->bd_status = BIOC_SDFAILED;
		break;

	case MFI_PD_HOTSPARE: /* XXX dedicated hotspare part of array? */
		bd->bd_status = BIOC_SDHOTSPARE;
		break;

	case MFI_PD_OFFLINE:
		bd->bd_status = BIOC_SDOFFLINE;
		break;

	case MFI_PD_REBUILD:
		bd->bd_status = BIOC_SDREBUILD;
		break;

	case MFI_PD_ONLINE:
		bd->bd_status = BIOC_SDONLINE;
		break;

	case MFI_PD_UNCONFIG_BAD: /* XXX define new state in bio */
	default:
		bd->bd_status = BIOC_SDINVALID;
		break;
	}

	/* get the remaining fields */
	memset(&mbox, 0, sizeof(mbox));
	mbox.s[0] = ar[arr].pd[disk].mar_pd.mfp_id;
	if (mfi_mgmt(sc, MR_DCMD_PD_GET_INFO, MFI_DATA_IN,
	    sizeof *pd, pd, &mbox)) {
		/* disk is missing but succeed command */
		rv = 0;
		goto freeme;
	}

	bd->bd_size = pd->mpd_size * 512; /* bytes per block */

	/* if pd->mpd_enc_idx is 0 then it is not in an enclosure */
	bd->bd_channel = pd->mpd_enc_idx;

	inqbuf = (struct scsi_inquiry_data *)&pd->mpd_inq_data;
	vendp = inqbuf->vendor;
	memcpy(vend, vendp, sizeof vend - 1);
	vend[sizeof vend - 1] = '\0';
	strlcpy(bd->bd_vendor, vend, sizeof(bd->bd_vendor));

	/* XXX find a way to retrieve serial nr from drive */
	/* XXX find a way to get bd_procdev */

	mfp = &pd->mpd_progress;
	if (mfp->mfp_in_prog & MFI_PD_PROG_PR) {
		mp = &mfp->mfp_patrol_read;
		bd->bd_patrol.bdp_percent = (mp->mp_progress * 100) / 0xffff;
		bd->bd_patrol.bdp_seconds = mp->mp_elapsed_seconds;
	}

	rv = 0;
freeme:
	free(pd, M_DEVBUF, sizeof *pd);

	return (rv);
}

int
mfi_ioctl_alarm(struct mfi_softc *sc, struct bioc_alarm *ba)
{
	uint32_t		opc, dir = MFI_DATA_NONE;
	int			rv = 0;
	int8_t			ret;

	switch(ba->ba_opcode) {
	case BIOC_SADISABLE:
		opc = MR_DCMD_SPEAKER_DISABLE;
		break;

	case BIOC_SAENABLE:
		opc = MR_DCMD_SPEAKER_ENABLE;
		break;

	case BIOC_SASILENCE:
		opc = MR_DCMD_SPEAKER_SILENCE;
		break;

	case BIOC_GASTATUS:
		opc = MR_DCMD_SPEAKER_GET;
		dir = MFI_DATA_IN;
		break;

	case BIOC_SATEST:
		opc = MR_DCMD_SPEAKER_TEST;
		break;

	default:
		DNPRINTF(MFI_D_IOCTL, "%s: mfi_ioctl_alarm biocalarm invalid "
		    "opcode %x\n", DEVNAME(sc), ba->ba_opcode);
		return (EINVAL);
	}

	if (mfi_mgmt(sc, opc, dir, sizeof(ret), &ret, NULL))
		rv = EINVAL;
	else
		if (ba->ba_opcode == BIOC_GASTATUS)
			ba->ba_status = ret;
		else
			ba->ba_status = 0;

	return (rv);
}

int
mfi_ioctl_blink(struct mfi_softc *sc, struct bioc_blink *bb)
{
	int			i, found, rv = EINVAL;
	union mfi_mbox		mbox;
	uint32_t		cmd;
	struct mfi_pd_list	*pd;

	DNPRINTF(MFI_D_IOCTL, "%s: mfi_ioctl_blink %x\n", DEVNAME(sc),
	    bb->bb_status);

	/* channel 0 means not in an enclosure so can't be blinked */
	if (bb->bb_channel == 0)
		return (EINVAL);

	pd = malloc(sizeof(*pd), M_DEVBUF, M_WAITOK);

	if (mfi_mgmt(sc, MR_DCMD_PD_GET_LIST, MFI_DATA_IN,
	    sizeof(*pd), pd, NULL))
		goto done;

	for (i = 0, found = 0; i < pd->mpl_no_pd; i++)
		if (bb->bb_channel == pd->mpl_address[i].mpa_enc_index &&
		    bb->bb_target == pd->mpl_address[i].mpa_enc_slot) {
			found = 1;
			break;
		}

	if (!found)
		goto done;

	memset(&mbox, 0, sizeof(mbox));
	mbox.s[0] = pd->mpl_address[i].mpa_pd_id;

	switch (bb->bb_status) {
	case BIOC_SBUNBLINK:
		cmd = MR_DCMD_PD_UNBLINK;
		break;

	case BIOC_SBBLINK:
		cmd = MR_DCMD_PD_BLINK;
		break;

	case BIOC_SBALARM:
	default:
		DNPRINTF(MFI_D_IOCTL, "%s: mfi_ioctl_blink biocblink invalid "
		    "opcode %x\n", DEVNAME(sc), bb->bb_status);
		goto done;
	}


	rv = mfi_mgmt(sc, cmd, MFI_DATA_NONE, 0, NULL, &mbox);

done:
	free(pd, M_DEVBUF, sizeof *pd);
	return (rv);
}

int
mfi_ioctl_setstate(struct mfi_softc *sc, struct bioc_setstate *bs)
{
	struct mfi_pd_list	*pd;
	struct mfi_pd_details	*info;
	int			i, found, rv = EINVAL;
	union mfi_mbox		mbox;

	DNPRINTF(MFI_D_IOCTL, "%s: mfi_ioctl_setstate %x\n", DEVNAME(sc),
	    bs->bs_status);

	pd = malloc(sizeof(*pd), M_DEVBUF, M_WAITOK);
	info = malloc(sizeof *info, M_DEVBUF, M_WAITOK);

	if (mfi_mgmt(sc, MR_DCMD_PD_GET_LIST, MFI_DATA_IN,
	    sizeof(*pd), pd, NULL))
		goto done;

	for (i = 0, found = 0; i < pd->mpl_no_pd; i++)
		if (bs->bs_channel == pd->mpl_address[i].mpa_enc_index &&
		    bs->bs_target == pd->mpl_address[i].mpa_enc_slot) {
			found = 1;
			break;
		}

	if (!found)
		goto done;

	memset(&mbox, 0, sizeof(mbox));
	mbox.s[0] = pd->mpl_address[i].mpa_pd_id;

	if (mfi_mgmt(sc, MR_DCMD_PD_GET_INFO, MFI_DATA_IN,
	    sizeof *info, info, &mbox))
		goto done;

	mbox.s[0] = pd->mpl_address[i].mpa_pd_id;
	mbox.s[1] = info->mpd_pd.mfp_seq;

	switch (bs->bs_status) {
	case BIOC_SSONLINE:
		mbox.b[4] = MFI_PD_ONLINE;
		break;

	case BIOC_SSOFFLINE:
		mbox.b[4] = MFI_PD_OFFLINE;
		break;

	case BIOC_SSHOTSPARE:
		mbox.b[4] = MFI_PD_HOTSPARE;
		break;

	case BIOC_SSREBUILD:
		mbox.b[4] = MFI_PD_REBUILD;
		break;

	default:
		DNPRINTF(MFI_D_IOCTL, "%s: mfi_ioctl_setstate invalid "
		    "opcode %x\n", DEVNAME(sc), bs->bs_status);
		goto done;
	}

	rv = mfi_mgmt(sc, MR_DCMD_PD_SET_STATE, MFI_DATA_NONE, 0, NULL, &mbox);

done:
	free(pd, M_DEVBUF, sizeof *pd);
	free(info, M_DEVBUF, sizeof *info);
	return (rv);
}

int
mfi_ioctl_patrol(struct mfi_softc *sc, struct bioc_patrol *bp)
{
	uint32_t		opc, dir = MFI_DATA_NONE;
	int			rv = 0;
	struct mfi_pr_properties prop;
	struct mfi_pr_status	status;
	uint32_t		time, exec_freq;

	switch (bp->bp_opcode) {
	case BIOC_SPSTOP:
	case BIOC_SPSTART:
		if (bp->bp_opcode == BIOC_SPSTART)
			opc = MR_DCMD_PR_START;
		else
			opc = MR_DCMD_PR_STOP;
		dir = MFI_DATA_IN;
		if (mfi_mgmt(sc, opc, dir, 0, NULL, NULL))
			return (EINVAL);
		break;

	case BIOC_SPMANUAL:
	case BIOC_SPDISABLE:
	case BIOC_SPAUTO:
		/* Get device's time. */
		opc = MR_DCMD_TIME_SECS_GET;
		dir = MFI_DATA_IN;
		if (mfi_mgmt(sc, opc, dir, sizeof(time), &time, NULL))
			return (EINVAL);

		opc = MR_DCMD_PR_GET_PROPERTIES;
		dir = MFI_DATA_IN;
		if (mfi_mgmt(sc, opc, dir, sizeof(prop), &prop, NULL))
			return (EINVAL);

		switch (bp->bp_opcode) {
		case BIOC_SPMANUAL:
			prop.op_mode = MFI_PR_OPMODE_MANUAL;
			break;
		case BIOC_SPDISABLE:
			prop.op_mode = MFI_PR_OPMODE_DISABLED;
			break;
		case BIOC_SPAUTO:
			if (bp->bp_autoival != 0) {
				if (bp->bp_autoival == -1)
					/* continuously */
					exec_freq = 0xffffffffU;
				else if (bp->bp_autoival > 0)
					exec_freq = bp->bp_autoival;
				else
					return (EINVAL);
				prop.exec_freq = exec_freq;
			}
			if (bp->bp_autonext != 0) {
				if (bp->bp_autonext < 0)
					return (EINVAL);
				else
					prop.next_exec = time + bp->bp_autonext;
			}
			prop.op_mode = MFI_PR_OPMODE_AUTO;
			break;
		}

		opc = MR_DCMD_PR_SET_PROPERTIES;
		dir = MFI_DATA_OUT;
		if (mfi_mgmt(sc, opc, dir, sizeof(prop), &prop, NULL))
			return (EINVAL);

		break;

	case BIOC_GPSTATUS:
		opc = MR_DCMD_PR_GET_PROPERTIES;
		dir = MFI_DATA_IN;
		if (mfi_mgmt(sc, opc, dir, sizeof(prop), &prop, NULL))
			return (EINVAL);

		opc = MR_DCMD_PR_GET_STATUS;
		dir = MFI_DATA_IN;
		if (mfi_mgmt(sc, opc, dir, sizeof(status), &status, NULL))
			return (EINVAL);

		/* Get device's time. */
		opc = MR_DCMD_TIME_SECS_GET;
		dir = MFI_DATA_IN;
		if (mfi_mgmt(sc, opc, dir, sizeof(time), &time, NULL))
			return (EINVAL);

		switch (prop.op_mode) {
		case MFI_PR_OPMODE_AUTO:
			bp->bp_mode = BIOC_SPMAUTO;
			bp->bp_autoival = prop.exec_freq;
			bp->bp_autonext = prop.next_exec;
			bp->bp_autonow = time;
			break;
		case MFI_PR_OPMODE_MANUAL:
			bp->bp_mode = BIOC_SPMMANUAL;
			break;
		case MFI_PR_OPMODE_DISABLED:
			bp->bp_mode = BIOC_SPMDISABLED;
			break;
		default:
			printf("%s: unknown patrol mode %d\n",
			    DEVNAME(sc), prop.op_mode);
			break;
		}

		switch (status.state) {
		case MFI_PR_STATE_STOPPED:
			bp->bp_status = BIOC_SPSSTOPPED;
			break;
		case MFI_PR_STATE_READY:
			bp->bp_status = BIOC_SPSREADY;
			break;
		case MFI_PR_STATE_ACTIVE:
			bp->bp_status = BIOC_SPSACTIVE;
			break;
		case MFI_PR_STATE_ABORTED:
			bp->bp_status = BIOC_SPSABORTED;
			break;
		default:
			printf("%s: unknown patrol state %d\n",
			    DEVNAME(sc), status.state);
			break;
		}

		break;

	default:
		DNPRINTF(MFI_D_IOCTL, "%s: mfi_ioctl_patrol biocpatrol invalid "
		    "opcode %x\n", DEVNAME(sc), bp->bp_opcode);
		return (EINVAL);
	}

	return (rv);
}

int
mfi_bio_hs(struct mfi_softc *sc, int volid, int type, void *bio_hs)
{
	struct mfi_conf		*cfg;
	struct mfi_hotspare	*hs;
	struct mfi_pd_details	*pd;
	struct bioc_disk	*sdhs;
	struct bioc_vol		*vdhs;
	struct scsi_inquiry_data *inqbuf;
	char			vend[8+16+4+1], *vendp;
	int			i, rv = EINVAL;
	uint32_t		size;
	union mfi_mbox		mbox;

	DNPRINTF(MFI_D_IOCTL, "%s: mfi_vol_hs %d\n", DEVNAME(sc), volid);

	if (!bio_hs)
		return (EINVAL);

	pd = malloc(sizeof *pd, M_DEVBUF, M_WAITOK);

	/* send single element command to retrieve size for full structure */
	cfg = malloc(sizeof *cfg, M_DEVBUF, M_WAITOK);
	if (mfi_mgmt(sc, MR_DCMD_CONF_GET, MFI_DATA_IN, sizeof *cfg, cfg, NULL))
		goto freeme;

	size = cfg->mfc_size;
	free(cfg, M_DEVBUF, sizeof *cfg);

	/* memory for read config */
	cfg = malloc(size, M_DEVBUF, M_WAITOK|M_ZERO);
	if (mfi_mgmt(sc, MR_DCMD_CONF_GET, MFI_DATA_IN, size, cfg, NULL))
		goto freeme;

	/* calculate offset to hs structure */
	hs = (struct mfi_hotspare *)(
	    ((uint8_t *)cfg) + offsetof(struct mfi_conf, mfc_array) +
	    cfg->mfc_array_size * cfg->mfc_no_array +
	    cfg->mfc_ld_size * cfg->mfc_no_ld);

	if (volid < cfg->mfc_no_ld)
		goto freeme; /* not a hotspare */

	if (volid > (cfg->mfc_no_ld + cfg->mfc_no_hs))
		goto freeme; /* not a hotspare */

	/* offset into hotspare structure */
	i = volid - cfg->mfc_no_ld;

	DNPRINTF(MFI_D_IOCTL, "%s: mfi_vol_hs i %d volid %d no_ld %d no_hs %d "
	    "hs %p cfg %p id %02x\n", DEVNAME(sc), i, volid, cfg->mfc_no_ld,
	    cfg->mfc_no_hs, hs, cfg, hs[i].mhs_pd.mfp_id);

	/* get pd fields */
	memset(&mbox, 0, sizeof(mbox));
	mbox.s[0] = hs[i].mhs_pd.mfp_id;
	if (mfi_mgmt(sc, MR_DCMD_PD_GET_INFO, MFI_DATA_IN,
	    sizeof *pd, pd, &mbox)) {
		DNPRINTF(MFI_D_IOCTL, "%s: mfi_vol_hs illegal PD\n",
		    DEVNAME(sc));
		goto freeme;
	}

	switch (type) {
	case MFI_MGMT_VD:
		vdhs = bio_hs;
		vdhs->bv_status = BIOC_SVONLINE;
		vdhs->bv_size = pd->mpd_size / 2 * 1024; /* XXX why? */
		vdhs->bv_level = -1; /* hotspare */
		vdhs->bv_nodisk = 1;
		break;

	case MFI_MGMT_SD:
		sdhs = bio_hs;
		sdhs->bd_status = BIOC_SDHOTSPARE;
		sdhs->bd_size = pd->mpd_size / 2 * 1024; /* XXX why? */
		sdhs->bd_channel = pd->mpd_enc_idx;
		sdhs->bd_target = pd->mpd_enc_slot;
		inqbuf = (struct scsi_inquiry_data *)&pd->mpd_inq_data;
		vendp = inqbuf->vendor;
		memcpy(vend, vendp, sizeof vend - 1);
		vend[sizeof vend - 1] = '\0';
		strlcpy(sdhs->bd_vendor, vend, sizeof(sdhs->bd_vendor));
		break;

	default:
		goto freeme;
	}

	DNPRINTF(MFI_D_IOCTL, "%s: mfi_vol_hs 6\n", DEVNAME(sc));
	rv = 0;
freeme:
	free(pd, M_DEVBUF, sizeof *pd);
	free(cfg, M_DEVBUF, 0);

	return (rv);
}

#ifndef SMALL_KERNEL

static const char *mfi_bbu_indicators[] = {
	"pack missing",
	"voltage low",
	"temp high",
	"charge active",
	"discharge active",
	"learn cycle req'd",
	"learn cycle active",
	"learn cycle failed",
	"learn cycle timeout",
	"I2C errors",
	"replace pack",
	"low capacity",
	"periodic learn req'd"
};

#define MFI_BBU_SENSORS 4

int
mfi_bbu(struct mfi_softc *sc)
{
	struct mfi_bbu_status bbu;
	u_int32_t status;
	u_int32_t mask;
	u_int32_t soh_bad;
	int i;

	if (mfi_mgmt(sc, MR_DCMD_BBU_GET_STATUS, MFI_DATA_IN,
	    sizeof(bbu), &bbu, NULL) != 0) {
		for (i = 0; i < MFI_BBU_SENSORS; i++) {
			sc->sc_bbu[i].value = 0;
			sc->sc_bbu[i].status = SENSOR_S_UNKNOWN;
		}
		for (i = 0; i < nitems(mfi_bbu_indicators); i++) {
			sc->sc_bbu_status[i].value = 0;
			sc->sc_bbu_status[i].status = SENSOR_S_UNKNOWN;
		}
		return (-1);
	}

	switch (bbu.battery_type) {
	case MFI_BBU_TYPE_IBBU:
		mask = MFI_BBU_STATE_BAD_IBBU;
		soh_bad = 0;
		break;
	case MFI_BBU_TYPE_BBU:
		mask = MFI_BBU_STATE_BAD_BBU;
		soh_bad = (bbu.detail.bbu.is_SOH_good == 0);
		break;

	case MFI_BBU_TYPE_NONE:
	default:
		sc->sc_bbu[0].value = 0;
		sc->sc_bbu[0].status = SENSOR_S_CRIT;
		for (i = 1; i < MFI_BBU_SENSORS; i++) {
			sc->sc_bbu[i].value = 0;
			sc->sc_bbu[i].status = SENSOR_S_UNKNOWN;
		}
		for (i = 0; i < nitems(mfi_bbu_indicators); i++) {
			sc->sc_bbu_status[i].value = 0;
			sc->sc_bbu_status[i].status = SENSOR_S_UNKNOWN;
		}
		return (0);
	}

	status = letoh32(bbu.fw_status);

	sc->sc_bbu[0].value = ((status & mask) || soh_bad) ? 0 : 1;
	sc->sc_bbu[0].status = ((status & mask) || soh_bad) ? SENSOR_S_CRIT :
	    SENSOR_S_OK;

	sc->sc_bbu[1].value = letoh16(bbu.voltage) * 1000;
	sc->sc_bbu[2].value = (int16_t)letoh16(bbu.current) * 1000;
	sc->sc_bbu[3].value = letoh16(bbu.temperature) * 1000000 + 273150000;
	for (i = 1; i < MFI_BBU_SENSORS; i++)
		sc->sc_bbu[i].status = SENSOR_S_UNSPEC;

	for (i = 0; i < nitems(mfi_bbu_indicators); i++) {
		sc->sc_bbu_status[i].value = (status & (1 << i)) ? 1 : 0;
		sc->sc_bbu_status[i].status = SENSOR_S_UNSPEC;
	}

	return (0);
}

int
mfi_create_sensors(struct mfi_softc *sc)
{
	struct device		*dev;
	struct scsi_link	*link;
	int			i;

	strlcpy(sc->sc_sensordev.xname, DEVNAME(sc),
	    sizeof(sc->sc_sensordev.xname));

	if (ISSET(letoh32(sc->sc_info.mci_adapter_ops ), MFI_INFO_AOPS_BBU)) {
		sc->sc_bbu = mallocarray(4, sizeof(*sc->sc_bbu),
		    M_DEVBUF, M_WAITOK | M_ZERO);

		sc->sc_bbu[0].type = SENSOR_INDICATOR;
		sc->sc_bbu[0].status = SENSOR_S_UNKNOWN;
		strlcpy(sc->sc_bbu[0].desc, "bbu ok",
		    sizeof(sc->sc_bbu[0].desc));
		sensor_attach(&sc->sc_sensordev, &sc->sc_bbu[0]);

		sc->sc_bbu[1].type = SENSOR_VOLTS_DC;
		sc->sc_bbu[1].status = SENSOR_S_UNSPEC;
		sc->sc_bbu[2].type = SENSOR_AMPS;
		sc->sc_bbu[2].status = SENSOR_S_UNSPEC;
		sc->sc_bbu[3].type = SENSOR_TEMP;
		sc->sc_bbu[3].status = SENSOR_S_UNSPEC;
		for (i = 1; i < MFI_BBU_SENSORS; i++) {
			strlcpy(sc->sc_bbu[i].desc, "bbu",
			    sizeof(sc->sc_bbu[i].desc));
			sensor_attach(&sc->sc_sensordev, &sc->sc_bbu[i]);
		}

		sc->sc_bbu_status = malloc(sizeof(*sc->sc_bbu_status) *
		    sizeof(mfi_bbu_indicators), M_DEVBUF, M_WAITOK | M_ZERO);

		for (i = 0; i < nitems(mfi_bbu_indicators); i++) {
			sc->sc_bbu_status[i].type = SENSOR_INDICATOR;
			sc->sc_bbu_status[i].status = SENSOR_S_UNSPEC;
			strlcpy(sc->sc_bbu_status[i].desc,
			    mfi_bbu_indicators[i],
			    sizeof(sc->sc_bbu_status[i].desc));

			sensor_attach(&sc->sc_sensordev, &sc->sc_bbu_status[i]);
		}
	}

	sc->sc_sensors = mallocarray(sc->sc_ld_cnt, sizeof(struct ksensor),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->sc_sensors == NULL)
		return (1);

	for (i = 0; i < sc->sc_ld_cnt; i++) {
		link = scsi_get_link(sc->sc_scsibus, i, 0);
		if (link == NULL)
			goto bad;

		dev = link->device_softc;

		sc->sc_sensors[i].type = SENSOR_DRIVE;
		sc->sc_sensors[i].status = SENSOR_S_UNKNOWN;

		strlcpy(sc->sc_sensors[i].desc, dev->dv_xname,
		    sizeof(sc->sc_sensors[i].desc));

		sensor_attach(&sc->sc_sensordev, &sc->sc_sensors[i]);
	}

	if (sensor_task_register(sc, mfi_refresh_sensors, 10) == NULL)
		goto bad;

	sensordev_install(&sc->sc_sensordev);

	return (0);

bad:
	free(sc->sc_sensors, M_DEVBUF,
	    sc->sc_ld_cnt * sizeof(struct ksensor));

	return (1);
}

void
mfi_refresh_sensors(void *arg)
{
	struct mfi_softc	*sc = arg;
	int			i, rv;
	struct bioc_vol		bv;

	if (sc->sc_bbu != NULL && mfi_bbu(sc) != 0)
		return;

	for (i = 0; i < sc->sc_ld_cnt; i++) {
		bzero(&bv, sizeof(bv));
		bv.bv_volid = i;

		rw_enter_write(&sc->sc_lock);
		rv = mfi_ioctl_vol(sc, &bv);
		rw_exit_write(&sc->sc_lock);

		if (rv != 0)
			return;

		switch(bv.bv_status) {
		case BIOC_SVOFFLINE:
			sc->sc_sensors[i].value = SENSOR_DRIVE_FAIL;
			sc->sc_sensors[i].status = SENSOR_S_CRIT;
			break;

		case BIOC_SVDEGRADED:
			sc->sc_sensors[i].value = SENSOR_DRIVE_PFAIL;
			sc->sc_sensors[i].status = SENSOR_S_WARN;
			break;

		case BIOC_SVSCRUB:
		case BIOC_SVONLINE:
			sc->sc_sensors[i].value = SENSOR_DRIVE_ONLINE;
			sc->sc_sensors[i].status = SENSOR_S_OK;
			break;

		case BIOC_SVINVALID:
			/* FALLTHROUGH */
		default:
			sc->sc_sensors[i].value = 0; /* unknown */
			sc->sc_sensors[i].status = SENSOR_S_UNKNOWN;
			break;
		}
	}
}
#endif /* SMALL_KERNEL */
#endif /* NBIO > 0 */

void
mfi_start(struct mfi_softc *sc, struct mfi_ccb *ccb)
{
	bus_dmamap_sync(sc->sc_dmat, MFIMEM_MAP(sc->sc_frames),
	    ccb->ccb_pframe_offset, sc->sc_frames_size,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	mfi_post(sc, ccb);
}

void
mfi_done(struct mfi_softc *sc, struct mfi_ccb *ccb)
{
	bus_dmamap_sync(sc->sc_dmat, MFIMEM_MAP(sc->sc_frames),
	    ccb->ccb_pframe_offset, sc->sc_frames_size,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	if (ccb->ccb_len > 0) {
		bus_dmamap_sync(sc->sc_dmat, ccb->ccb_dmamap,
		    0, ccb->ccb_dmamap->dm_mapsize,
		    (ccb->ccb_direction == MFI_DATA_IN) ?
		    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);

		bus_dmamap_unload(sc->sc_dmat, ccb->ccb_dmamap);
	}

	ccb->ccb_done(sc, ccb);
}

u_int32_t
mfi_xscale_fw_state(struct mfi_softc *sc)
{
	return (mfi_read(sc, MFI_OMSG0));
}

void
mfi_xscale_intr_ena(struct mfi_softc *sc)
{
	mfi_write(sc, MFI_OMSK, MFI_ENABLE_INTR);
}

int
mfi_xscale_intr(struct mfi_softc *sc)
{
	u_int32_t status;

	status = mfi_read(sc, MFI_OSTS);
	if (!ISSET(status, MFI_OSTS_INTR_VALID))
		return (0);

	/* write status back to acknowledge interrupt */
	mfi_write(sc, MFI_OSTS, status);

	return (1);
}

void
mfi_xscale_post(struct mfi_softc *sc, struct mfi_ccb *ccb)
{
	mfi_write(sc, MFI_IQP, (ccb->ccb_pframe >> 3) |
	    ccb->ccb_extra_frames);
}

u_int32_t
mfi_ppc_fw_state(struct mfi_softc *sc)
{
	return (mfi_read(sc, MFI_OSP));
}

void
mfi_ppc_intr_ena(struct mfi_softc *sc)
{
	mfi_write(sc, MFI_ODC, 0xffffffff);
	mfi_write(sc, MFI_OMSK, ~0x80000004);
}

int
mfi_ppc_intr(struct mfi_softc *sc)
{
	u_int32_t status;

	status = mfi_read(sc, MFI_OSTS);
	if (!ISSET(status, MFI_OSTS_PPC_INTR_VALID))
		return (0);

	/* write status back to acknowledge interrupt */
	mfi_write(sc, MFI_ODC, status);

	return (1);
}

void
mfi_ppc_post(struct mfi_softc *sc, struct mfi_ccb *ccb)
{
	mfi_write(sc, MFI_IQP, 0x1 | ccb->ccb_pframe |
	    (ccb->ccb_extra_frames << 1));
}

u_int32_t
mfi_gen2_fw_state(struct mfi_softc *sc)
{
	return (mfi_read(sc, MFI_OSP));
}

void
mfi_gen2_intr_ena(struct mfi_softc *sc)
{
	mfi_write(sc, MFI_ODC, 0xffffffff);
	mfi_write(sc, MFI_OMSK, ~MFI_OSTS_GEN2_INTR_VALID);
}

int
mfi_gen2_intr(struct mfi_softc *sc)
{
	u_int32_t status;

	status = mfi_read(sc, MFI_OSTS);
	if (!ISSET(status, MFI_OSTS_GEN2_INTR_VALID))
		return (0);

	/* write status back to acknowledge interrupt */
	mfi_write(sc, MFI_ODC, status);

	return (1);
}

void
mfi_gen2_post(struct mfi_softc *sc, struct mfi_ccb *ccb)
{
	mfi_write(sc, MFI_IQP, 0x1 | ccb->ccb_pframe |
	    (ccb->ccb_extra_frames << 1));
}

u_int32_t
mfi_skinny_fw_state(struct mfi_softc *sc)
{
	return (mfi_read(sc, MFI_OSP));
}

void
mfi_skinny_intr_ena(struct mfi_softc *sc)
{
	mfi_write(sc, MFI_OMSK, ~0x00000001);
}

int
mfi_skinny_intr(struct mfi_softc *sc)
{
	u_int32_t status;

	status = mfi_read(sc, MFI_OSTS);
	if (!ISSET(status, MFI_OSTS_SKINNY_INTR_VALID))
		return (0);

	/* write status back to acknowledge interrupt */
	mfi_write(sc, MFI_OSTS, status);

	return (1);
}

void
mfi_skinny_post(struct mfi_softc *sc, struct mfi_ccb *ccb)
{
	mfi_write(sc, MFI_IQPL, 0x1 | ccb->ccb_pframe |
	    (ccb->ccb_extra_frames << 1));
	mfi_write(sc, MFI_IQPH, 0x00000000);
}

u_int
mfi_skinny_sgd_load(struct mfi_softc *sc, struct mfi_ccb *ccb)
{
	struct mfi_frame_header	*hdr = &ccb->ccb_frame->mfr_header;
	union mfi_sgl		*sgl = ccb->ccb_sgl;
	bus_dma_segment_t	*sgd = ccb->ccb_dmamap->dm_segs;
	int			 i;

	switch (hdr->mfh_cmd) {
	case MFI_CMD_LD_READ:
	case MFI_CMD_LD_WRITE:
	case MFI_CMD_PD_SCSI_IO:
		/* Use MF_FRAME_IEEE for some IO commands on skinny adapters */
		for (i = 0; i < ccb->ccb_dmamap->dm_nsegs; i++) {
			sgl->sg_skinny[i].addr = htole64(sgd[i].ds_addr);
			sgl->sg_skinny[i].len = htole32(sgd[i].ds_len);
			sgl->sg_skinny[i].flag = 0;
		}
		hdr->mfh_flags |= MFI_FRAME_IEEE | MFI_FRAME_SGL64;

		return (ccb->ccb_dmamap->dm_nsegs * sizeof(sgl->sg_skinny));
	default:
		return (mfi_default_sgd_load(sc, ccb));
	}
}

int
mfi_pd_scsi_probe(struct scsi_link *link)
{
	union mfi_mbox mbox;
	struct mfi_softc *sc = link->bus->sb_adapter_softc;
	struct mfi_pd_link *pl = sc->sc_pd->pd_links[link->target];

	if (link->lun > 0)
		return (0);

	if (pl == NULL)
		return (ENXIO);

	memset(&mbox, 0, sizeof(mbox));
	mbox.s[0] = pl->pd_id;

	if (mfi_mgmt(sc, MR_DCMD_PD_GET_INFO, MFI_DATA_IN,
	    sizeof(pl->pd_info), &pl->pd_info, &mbox))
		return (EIO);

	if (letoh16(pl->pd_info.mpd_fw_state) != MFI_PD_SYSTEM)
		return (ENXIO);

	return (0);
}

void
mfi_pd_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct mfi_softc *sc = link->bus->sb_adapter_softc;
	struct mfi_ccb *ccb = xs->io;
	struct mfi_pass_frame *pf = &ccb->ccb_frame->mfr_pass;
	struct mfi_pd_link *pl = sc->sc_pd->pd_links[link->target];

	KERNEL_UNLOCK();

	mfi_scrub_ccb(ccb);
	xs->error = XS_NOERROR;

	pf->mpf_header.mfh_cmd = MFI_CMD_PD_SCSI_IO;
	pf->mpf_header.mfh_target_id = pl->pd_id;
	pf->mpf_header.mfh_lun_id = link->lun;
	pf->mpf_header.mfh_cdb_len = xs->cmdlen;
	pf->mpf_header.mfh_timeout = 0;
	pf->mpf_header.mfh_data_len = htole32(xs->datalen); /* XXX */
	pf->mpf_header.mfh_sense_len = MFI_SENSE_SIZE;
	pf->mpf_sense_addr = htole64(ccb->ccb_psense);

	memset(pf->mpf_cdb, 0, sizeof(pf->mpf_cdb));
	memcpy(pf->mpf_cdb, &xs->cmd, xs->cmdlen);

	ccb->ccb_done = mfi_scsi_xs_done;
	ccb->ccb_cookie = xs;
	ccb->ccb_frame_size = MFI_PASS_FRAME_SIZE;
	ccb->ccb_sgl = &pf->mpf_sgl;

	if (xs->flags & (SCSI_DATA_IN | SCSI_DATA_OUT))
		ccb->ccb_direction = xs->flags & SCSI_DATA_IN ?
		    MFI_DATA_IN : MFI_DATA_OUT;
	else
		ccb->ccb_direction = MFI_DATA_NONE;

	if (xs->data) {
		ccb->ccb_data = xs->data;
		ccb->ccb_len = xs->datalen;

		if (mfi_create_sgl(sc, ccb, (xs->flags & SCSI_NOSLEEP) ?
		    BUS_DMA_NOWAIT : BUS_DMA_WAITOK))
			goto stuffup;
	}

	if (xs->flags & SCSI_POLL)
		mfi_poll(sc, ccb);
	else
		mfi_start(sc, ccb);

	KERNEL_LOCK();
	return;

stuffup:
	xs->error = XS_DRIVER_STUFFUP;
	KERNEL_LOCK();
	scsi_done(xs);
}
