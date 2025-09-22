/*	$OpenBSD: gdt_common.c,v 1.85 2022/04/16 19:19:59 naddy Exp $	*/

/*
 * Copyright (c) 1999, 2000, 2003 Niklas Hallqvist.  All rights reserved.
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

/*
 * This driver would not have written if it was not for the hardware donations
 * from both ICP-Vortex and Öko.neT.  I want to thank them for their support.
 */

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/bus.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

#include <dev/biovar.h>
#include <dev/ic/gdtreg.h>
#include <dev/ic/gdtvar.h>

#include "bio.h"

#ifdef GDT_DEBUG
int gdt_maxcmds = GDT_MAXCMDS;
#undef GDT_MAXCMDS
#define GDT_MAXCMDS gdt_maxcmds
#endif

#define GDT_DRIVER_VERSION 1
#define GDT_DRIVER_SUBVERSION 2

int	gdt_async_event(struct gdt_softc *, int);
void	gdt_chain(struct gdt_softc *);
void	gdt_clear_events(struct gdt_softc *);
struct scsi_xfer *gdt_dequeue(struct gdt_softc *);
void	gdt_enqueue(struct gdt_softc *, struct scsi_xfer *, int);
void	gdt_enqueue_ccb(struct gdt_softc *, struct gdt_ccb *);
void	gdt_eval_mapping(u_int32_t, int *, int *, int *);
int	gdt_exec_ccb(struct gdt_ccb *);
void	gdt_ccb_free(void *, void *);
void   *gdt_ccb_alloc(void *);
void	gdt_internal_cache_cmd(struct scsi_xfer *);
int	gdt_internal_cmd(struct gdt_softc *, u_int8_t, u_int16_t,
    u_int32_t, u_int32_t, u_int32_t);
#if NBIO > 0
int	gdt_ioctl(struct device *, u_long, caddr_t);
int	gdt_ioctl_inq(struct gdt_softc *, struct bioc_inq *);
int	gdt_ioctl_vol(struct gdt_softc *, struct bioc_vol *);
int	gdt_ioctl_disk(struct gdt_softc *, struct bioc_disk *);
int	gdt_ioctl_alarm(struct gdt_softc *, struct bioc_alarm *);
int	gdt_ioctl_setstate(struct gdt_softc *, struct bioc_setstate *);
#endif /* NBIO > 0 */
void	gdt_scsi_cmd(struct scsi_xfer *);
void	gdt_start_ccbs(struct gdt_softc *);
int	gdt_sync_event(struct gdt_softc *, int, u_int8_t,
    struct scsi_xfer *);
void	gdt_timeout(void *);
int	gdt_wait(struct gdt_softc *, struct gdt_ccb *, int);
void	gdt_watchdog(void *);

struct cfdriver gdt_cd = {
	NULL, "gdt", DV_DULL
};

const struct scsi_adapter gdt_switch = {
	gdt_scsi_cmd, NULL, NULL, NULL, NULL
};

int gdt_cnt = 0;
u_int8_t gdt_polling;
u_int8_t gdt_from_wait;
struct gdt_softc *gdt_wait_gdt;
int	gdt_wait_index;
#ifdef GDT_DEBUG
int	gdt_debug = GDT_DEBUG;
#endif

int
gdt_attach(struct gdt_softc *sc)
{
	struct scsibus_attach_args saa;
	u_int16_t cdev_cnt;
	int i, id, drv_cyls, drv_hds, drv_secs, error, nsegs;

	gdt_polling = 1;
	gdt_from_wait = 0;

	if (bus_dmamem_alloc(sc->sc_dmat, GDT_SCRATCH_SZ, PAGE_SIZE, 0,
	    &sc->sc_scratch_seg, 1, &nsegs, BUS_DMA_NOWAIT))
	    panic("%s: bus_dmamem_alloc failed", DEVNAME(sc));
	if (bus_dmamem_map(sc->sc_dmat, &sc->sc_scratch_seg, 1,
	    GDT_SCRATCH_SZ, &sc->sc_scratch, BUS_DMA_NOWAIT))
	    panic("%s: bus_dmamem_map failed", DEVNAME(sc));

	gdt_clear_events(sc);

	TAILQ_INIT(&sc->sc_free_ccb);
	TAILQ_INIT(&sc->sc_ccbq);
	SIMPLEQ_INIT(&sc->sc_queue);

	mtx_init(&sc->sc_ccb_mtx, IPL_BIO);
	scsi_iopool_init(&sc->sc_iopool, sc, gdt_ccb_alloc, gdt_ccb_free);

	/* Initialize the ccbs */
	for (i = 0; i < GDT_MAXCMDS; i++) {
		sc->sc_ccbs[i].gc_cmd_index = i + 2;
		error = bus_dmamap_create(sc->sc_dmat,
		    (GDT_MAXOFFSETS - 1) << PGSHIFT, GDT_MAXOFFSETS,
		    (GDT_MAXOFFSETS - 1) << PGSHIFT, 0,
		    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
		    &sc->sc_ccbs[i].gc_dmamap_xfer);
		if (error) {
			printf("%s: cannot create ccb dmamap (%d)",
			    DEVNAME(sc), error);
			return (1);
		}
		(void)gdt_ccb_set_cmd(sc->sc_ccbs + i, GDT_GCF_UNUSED);
		TAILQ_INSERT_TAIL(&sc->sc_free_ccb, &sc->sc_ccbs[i],
		    gc_chain);
	}

	if (!gdt_internal_cmd(sc, GDT_SCREENSERVICE, GDT_INIT, 0, 0, 0)) {
		printf("screen service initialization error %d\n",
		     sc->sc_status);
		return (1);
	}

	if (!gdt_internal_cmd(sc, GDT_CACHESERVICE, GDT_INIT, GDT_LINUX_OS, 0,
	    0)) {
		printf("cache service initialization error %d\n",
		    sc->sc_status);
		return (1);
	}

	cdev_cnt = (u_int16_t)sc->sc_info;

	/* Detect number of busses */
	gdt_enc32(sc->sc_scratch + GDT_IOC_VERSION, GDT_IOC_NEWEST);
	sc->sc_scratch[GDT_IOC_LIST_ENTRIES] = GDT_MAXBUS;
	sc->sc_scratch[GDT_IOC_FIRST_CHAN] = 0;
	sc->sc_scratch[GDT_IOC_LAST_CHAN] = GDT_MAXBUS - 1;
	gdt_enc32(sc->sc_scratch + GDT_IOC_LIST_OFFSET, GDT_IOC_HDR_SZ);
	if (gdt_internal_cmd(sc, GDT_CACHESERVICE, GDT_IOCTL,
	    GDT_IOCHAN_RAW_DESC, GDT_INVALID_CHANNEL,
	    GDT_IOC_HDR_SZ + GDT_RAWIOC_SZ)) {
		sc->sc_bus_cnt = sc->sc_scratch[GDT_IOC_CHAN_COUNT];
		for (i = 0; i < sc->sc_bus_cnt; i++) {
			id = sc->sc_scratch[GDT_IOC_HDR_SZ +
			    i * GDT_RAWIOC_SZ + GDT_RAWIOC_PROC_ID];
			sc->sc_bus_id[id] = id < GDT_MAXBUS ? id : 0xff;
		}

	} else {
		/* New method failed, use fallback. */
		gdt_enc32(sc->sc_scratch + GDT_GETCH_CHANNEL_NO, i);
		for (i = 0; i < GDT_MAXBUS; i++) {
			if (!gdt_internal_cmd(sc, GDT_CACHESERVICE, GDT_IOCTL,
			    GDT_SCSI_CHAN_CNT | GDT_L_CTRL_PATTERN,
			    GDT_IO_CHANNEL | GDT_INVALID_CHANNEL,
			    GDT_GETCH_SZ)) {
				if (i == 0) {
					printf("cannot get channel count, "
					    "error %d\n", sc->sc_status);
					return (1);
				}
				break;
			}
			sc->sc_bus_id[i] =
			    (sc->sc_scratch[GDT_GETCH_SIOP_ID] < GDT_MAXID) ?
			    sc->sc_scratch[GDT_GETCH_SIOP_ID] : 0xff;
		}
		sc->sc_bus_cnt = i;
	}

	/* Read cache configuration */
	if (!gdt_internal_cmd(sc, GDT_CACHESERVICE, GDT_IOCTL, GDT_CACHE_INFO,
	    GDT_INVALID_CHANNEL, GDT_CINFO_SZ)) {
		printf("cannot get cache info, error %d\n", sc->sc_status);
		return (1);
	}
	sc->sc_cpar.cp_version =
	    gdt_dec32(sc->sc_scratch + GDT_CPAR_VERSION);
	sc->sc_cpar.cp_state = gdt_dec16(sc->sc_scratch + GDT_CPAR_STATE);
	sc->sc_cpar.cp_strategy =
	    gdt_dec16(sc->sc_scratch + GDT_CPAR_STRATEGY);
	sc->sc_cpar.cp_write_back =
	    gdt_dec16(sc->sc_scratch + GDT_CPAR_WRITE_BACK);
	sc->sc_cpar.cp_block_size =
	    gdt_dec16(sc->sc_scratch + GDT_CPAR_BLOCK_SIZE);

	/* Read board information and features */
	sc->sc_more_proc = 0;
	if (gdt_internal_cmd(sc, GDT_CACHESERVICE, GDT_IOCTL, GDT_BOARD_INFO,
	    GDT_INVALID_CHANNEL, GDT_BINFO_SZ)) {
		/* XXX A lot of these assignments can probably go later */
		sc->sc_binfo.bi_ser_no =
		    gdt_dec32(sc->sc_scratch + GDT_BINFO_SER_NO);
		bcopy(sc->sc_scratch + GDT_BINFO_OEM_ID,
		    sc->sc_binfo.bi_oem_id, sizeof sc->sc_binfo.bi_oem_id);
		sc->sc_binfo.bi_ep_flags =
		    gdt_dec16(sc->sc_scratch + GDT_BINFO_EP_FLAGS);
		sc->sc_binfo.bi_proc_id =
		    gdt_dec32(sc->sc_scratch + GDT_BINFO_PROC_ID);
		sc->sc_binfo.bi_memsize =
		    gdt_dec32(sc->sc_scratch + GDT_BINFO_MEMSIZE);
		sc->sc_binfo.bi_mem_banks =
		    sc->sc_scratch[GDT_BINFO_MEM_BANKS];
		sc->sc_binfo.bi_chan_type =
		    sc->sc_scratch[GDT_BINFO_CHAN_TYPE];
		sc->sc_binfo.bi_chan_count =
		    sc->sc_scratch[GDT_BINFO_CHAN_COUNT];
		sc->sc_binfo.bi_rdongle_pres =
		    sc->sc_scratch[GDT_BINFO_RDONGLE_PRES];
		sc->sc_binfo.bi_epr_fw_ver =
		    gdt_dec32(sc->sc_scratch + GDT_BINFO_EPR_FW_VER);
		sc->sc_binfo.bi_upd_fw_ver =
		    gdt_dec32(sc->sc_scratch + GDT_BINFO_UPD_FW_VER);
		sc->sc_binfo.bi_upd_revision =
		    gdt_dec32(sc->sc_scratch + GDT_BINFO_UPD_REVISION);
		bcopy(sc->sc_scratch + GDT_BINFO_TYPE_STRING,
		    sc->sc_binfo.bi_type_string,
		    sizeof sc->sc_binfo.bi_type_string);
		bcopy(sc->sc_scratch + GDT_BINFO_RAID_STRING,
		    sc->sc_binfo.bi_raid_string,
		    sizeof sc->sc_binfo.bi_raid_string);
		sc->sc_binfo.bi_update_pres =
		    sc->sc_scratch[GDT_BINFO_UPDATE_PRES];
		sc->sc_binfo.bi_xor_pres =
		    sc->sc_scratch[GDT_BINFO_XOR_PRES];
		sc->sc_binfo.bi_prom_type =
		    sc->sc_scratch[GDT_BINFO_PROM_TYPE];
		sc->sc_binfo.bi_prom_count =
		    sc->sc_scratch[GDT_BINFO_PROM_COUNT];
		sc->sc_binfo.bi_dup_pres =
		    gdt_dec32(sc->sc_scratch + GDT_BINFO_DUP_PRES);
		sc->sc_binfo.bi_chan_pres =
		    gdt_dec32(sc->sc_scratch + GDT_BINFO_CHAN_PRES);
		sc->sc_binfo.bi_mem_pres =
		    gdt_dec32(sc->sc_scratch + GDT_BINFO_MEM_PRES);
		sc->sc_binfo.bi_ft_bus_system =
		    sc->sc_scratch[GDT_BINFO_FT_BUS_SYSTEM];
		sc->sc_binfo.bi_subtype_valid =
		    sc->sc_scratch[GDT_BINFO_SUBTYPE_VALID];
		sc->sc_binfo.bi_board_subtype =
		    sc->sc_scratch[GDT_BINFO_BOARD_SUBTYPE];
		sc->sc_binfo.bi_rampar_pres =
		    sc->sc_scratch[GDT_BINFO_RAMPAR_PRES];

		if (gdt_internal_cmd(sc, GDT_CACHESERVICE, GDT_IOCTL,
		    GDT_BOARD_FEATURES, GDT_INVALID_CHANNEL, GDT_BFEAT_SZ)) {
			sc->sc_bfeat.bf_chaining =
			    sc->sc_scratch[GDT_BFEAT_CHAINING];
			sc->sc_bfeat.bf_striping =
			    sc->sc_scratch[GDT_BFEAT_STRIPING];
			sc->sc_bfeat.bf_mirroring =
			    sc->sc_scratch[GDT_BFEAT_MIRRORING];
			sc->sc_bfeat.bf_raid =
			    sc->sc_scratch[GDT_BFEAT_RAID];
			sc->sc_more_proc = 1;
		}
	} else {
		/* XXX Not implemented yet */
	}

	/* Read more information */
	if (sc->sc_more_proc) {
		int bus, j;
		/* physical drives, channel addresses */
		/* step 1: get magical bus number from firmware */
		gdt_enc32(sc->sc_scratch + GDT_IOC_VERSION, GDT_IOC_NEWEST);
		sc->sc_scratch[GDT_IOC_LIST_ENTRIES] = GDT_MAXBUS;
		sc->sc_scratch[GDT_IOC_FIRST_CHAN] = 0;
		sc->sc_scratch[GDT_IOC_LAST_CHAN] = GDT_MAXBUS - 1;
		gdt_enc32(sc->sc_scratch + GDT_IOC_LIST_OFFSET, GDT_IOC_HDR_SZ);
		if (gdt_internal_cmd(sc, GDT_CACHESERVICE, GDT_IOCTL,
		    GDT_IOCHAN_DESC, GDT_INVALID_CHANNEL,
		    GDT_IOC_HDR_SZ + GDT_IOC_SZ * GDT_MAXBUS)) {
			GDT_DPRINTF(GDT_D_INFO, ("method 1\n"));
			for (bus = 0; bus < sc->sc_bus_cnt; bus++) {
				sc->sc_raw[bus].ra_address =
				    gdt_dec32(sc->sc_scratch +
				    GDT_IOC_HDR_SZ +
				    GDT_IOC_SZ * bus +
				    GDT_IOC_ADDRESS);
				sc->sc_raw[bus].ra_local_no =
				    gdt_dec8(sc->sc_scratch +
				    GDT_IOC_HDR_SZ +
				    GDT_IOC_SZ * bus +
				    GDT_IOC_LOCAL_NO);
				GDT_DPRINTF(GDT_D_INFO, (
				    "bus: %d address: %x local: %x\n",
				    bus,
				    sc->sc_raw[bus].ra_address,
				    sc->sc_raw[bus].ra_local_no));
			}
		} else {
			GDT_DPRINTF(GDT_D_INFO, ("method 2\n"));
			for (bus = 0; bus < sc->sc_bus_cnt; bus++) {
				sc->sc_raw[bus].ra_address = GDT_IO_CHANNEL;
				sc->sc_raw[bus].ra_local_no = bus;
				GDT_DPRINTF(GDT_D_INFO, (
				    "bus: %d address: %x local: %x\n",
				    bus,
				    sc->sc_raw[bus].ra_address,
				    sc->sc_raw[bus].ra_local_no));
			}
		}
		/* step 2: use magical bus number to get nr of phys disks */
		for (bus = 0; bus < sc->sc_bus_cnt; bus++) {
			gdt_enc32(sc->sc_scratch + GDT_GETCH_CHANNEL_NO,
			    sc->sc_raw[bus].ra_local_no);
			if (gdt_internal_cmd(sc, GDT_CACHESERVICE, GDT_IOCTL,
			    GDT_SCSI_CHAN_CNT | GDT_L_CTRL_PATTERN,
			    sc->sc_raw[bus].ra_address | GDT_INVALID_CHANNEL,
			    GDT_GETCH_SZ)) {
				sc->sc_raw[bus].ra_phys_cnt =
				    gdt_dec32(sc->sc_scratch +
				    GDT_GETCH_DRIVE_CNT);
				GDT_DPRINTF(GDT_D_INFO, ("chan: %d disks: %d\n",
				    bus, sc->sc_raw[bus].ra_phys_cnt));
			}

			/* step 3: get scsi disk nr */
			if (sc->sc_raw[bus].ra_phys_cnt > 0) {
				gdt_enc32(sc->sc_scratch +
				    GDT_GETSCSI_CHAN,
				    sc->sc_raw[bus].ra_local_no);
				gdt_enc32(sc->sc_scratch +
				    GDT_GETSCSI_CNT,
				    sc->sc_raw[bus].ra_phys_cnt);
				if (gdt_internal_cmd(sc, GDT_CACHESERVICE,
				    GDT_IOCTL,
				    GDT_SCSI_DR_LIST | GDT_L_CTRL_PATTERN,
				    sc->sc_raw[bus].ra_address |
				    GDT_INVALID_CHANNEL,
				    GDT_GETSCSI_SZ))
					for (j = 0;
					    j < sc->sc_raw[bus].ra_phys_cnt;
					    j++) {
						sc->sc_raw[bus].ra_id_list[j] =
						    gdt_dec32(sc->sc_scratch +
						    GDT_GETSCSI_LIST +
						    GDT_GETSCSI_LIST_SZ * j);
						GDT_DPRINTF(GDT_D_INFO,
						    ("  diskid: %d\n",
						    sc->sc_raw[bus].ra_id_list[j]));
					}
				else
					sc->sc_raw[bus].ra_phys_cnt = 0;
			}
			/* add found disks to grand total */
			sc->sc_total_disks += sc->sc_raw[bus].ra_phys_cnt;
		}
	} /* if (sc->sc_more_proc) */

	if (!gdt_internal_cmd(sc, GDT_SCSIRAWSERVICE, GDT_INIT, 0, 0, 0)) {
		printf("raw service initialization error %d\n",
		    sc->sc_status);
		return (1);
	}

	/* Set/get features raw service (scatter/gather) */
	sc->sc_raw_feat = 0;
	if (gdt_internal_cmd(sc, GDT_SCSIRAWSERVICE, GDT_SET_FEAT,
	    GDT_SCATTER_GATHER, 0, 0))
		if (gdt_internal_cmd(sc, GDT_SCSIRAWSERVICE, GDT_GET_FEAT, 0,
		    0, 0))
			sc->sc_raw_feat = sc->sc_info;

	/* Set/get features cache service (scatter/gather) */
	sc->sc_cache_feat = 0;
	if (gdt_internal_cmd(sc, GDT_CACHESERVICE, GDT_SET_FEAT, 0,
	    GDT_SCATTER_GATHER, 0))
		if (gdt_internal_cmd(sc, GDT_CACHESERVICE, GDT_GET_FEAT, 0, 0,
		    0))
			sc->sc_cache_feat = sc->sc_info;

	/* XXX Linux reserve drives here, potentially */

	sc->sc_ndevs = 0;
	/* Scan for cache devices */
	for (i = 0; i < cdev_cnt && i < GDT_MAX_HDRIVES; i++)
		if (gdt_internal_cmd(sc, GDT_CACHESERVICE, GDT_INFO, i, 0,
		    0)) {
			sc->sc_hdr[i].hd_present = 1;
			sc->sc_hdr[i].hd_size = sc->sc_info;

			if (sc->sc_hdr[i].hd_size > 0)
				sc->sc_ndevs++;

			/*
			 * Evaluate mapping (sectors per head, heads per cyl)
			 */
			sc->sc_hdr[i].hd_size &= ~GDT_SECS32;
			if (sc->sc_info2 == 0)
				gdt_eval_mapping(sc->sc_hdr[i].hd_size,
				    &drv_cyls, &drv_hds, &drv_secs);
			else {
				drv_hds = sc->sc_info2 & 0xff;
				drv_secs = (sc->sc_info2 >> 8) & 0xff;
				drv_cyls = sc->sc_hdr[i].hd_size / drv_hds /
				    drv_secs;
			}
			sc->sc_hdr[i].hd_heads = drv_hds;
			sc->sc_hdr[i].hd_secs = drv_secs;
			/* Round the size */
			sc->sc_hdr[i].hd_size = drv_cyls * drv_hds * drv_secs;

			if (gdt_internal_cmd(sc, GDT_CACHESERVICE,
			    GDT_DEVTYPE, i, 0, 0))
				sc->sc_hdr[i].hd_devtype = sc->sc_info;
		}

	printf("dpmem %llx %d-bus %d cache device%s\n",
	    (long long)sc->sc_dpmembase,
	    sc->sc_bus_cnt, cdev_cnt, cdev_cnt == 1 ? "" : "s");
	printf("%s: ver %x, cache %s, strategy %d, writeback %s, blksz %d\n",
	    DEVNAME(sc), sc->sc_cpar.cp_version,
	    sc->sc_cpar.cp_state ? "on" : "off", sc->sc_cpar.cp_strategy,
	    sc->sc_cpar.cp_write_back ? "on" : "off",
	    sc->sc_cpar.cp_block_size);
#if 1
	printf("%s: raw feat %x cache feat %x\n", DEVNAME(sc),
	    sc->sc_raw_feat, sc->sc_cache_feat);
#endif

#if NBIO > 0
	if (bio_register(&sc->sc_dev, gdt_ioctl) != 0)
		panic("%s: controller registration failed", DEVNAME(sc));
#endif
	gdt_cnt++;

	saa.saa_adapter_softc = sc;
	saa.saa_adapter = &gdt_switch;
	saa.saa_adapter_buswidth =
	    (sc->sc_class & GDT_FC) ? GDT_MAXID : GDT_MAX_HDRIVES;
	saa.saa_adapter_target = SDEV_NO_ADAPTER_TARGET;
	saa.saa_luns = 8;
	if (sc->sc_ndevs == 0)
		saa.saa_openings = 0;
	else
		saa.saa_openings = (GDT_MAXCMDS - GDT_CMD_RESERVE) /
		    sc->sc_ndevs;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	config_found(&sc->sc_dev, &saa, scsiprint);

	gdt_polling = 0;
	return (0);
}

void
gdt_eval_mapping(u_int32_t size, int *cyls, int *heads, int *secs)
{
	*cyls = size / GDT_HEADS / GDT_SECS;
	if (*cyls < GDT_MAXCYLS) {
		*heads = GDT_HEADS;
		*secs = GDT_SECS;
	} else {
		/* Too high for 64 * 32 */
		*cyls = size / GDT_MEDHEADS / GDT_MEDSECS;
		if (*cyls < GDT_MAXCYLS) {
			*heads = GDT_MEDHEADS;
			*secs = GDT_MEDSECS;
		} else {
			/* Too high for 127 * 63 */
			*cyls = size / GDT_BIGHEADS / GDT_BIGSECS;
			*heads = GDT_BIGHEADS;
			*secs = GDT_BIGSECS;
		}
	}
}

/*
 * Insert a command into the driver queue, either at the front or at the tail.
 * It's ok to overload the freelist link as these structures are never on
 * the freelist at this time.
 */
void
gdt_enqueue(struct gdt_softc *sc, struct scsi_xfer *xs, int infront)
{
	if (infront)
		SIMPLEQ_INSERT_HEAD(&sc->sc_queue, xs, xfer_list);
	else
		SIMPLEQ_INSERT_TAIL(&sc->sc_queue, xs, xfer_list);
}

/*
 * Pull a command off the front of the driver queue.
 */
struct scsi_xfer *
gdt_dequeue(struct gdt_softc *sc)
{
	struct scsi_xfer *xs;

	xs = SIMPLEQ_FIRST(&sc->sc_queue);
	if (xs != NULL)
		SIMPLEQ_REMOVE_HEAD(&sc->sc_queue, xfer_list);

	return (xs);
}

/*
 * Start a SCSI operation on a cache device.
 * XXX Polled operation is not yet complete.  What kind of locking do we need?
 */
void
gdt_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct gdt_softc *sc = link->bus->sb_adapter_softc;
	u_int8_t target = link->target;
	struct gdt_ccb *ccb;
	u_int32_t blockno, blockcnt;
	struct scsi_rw *rw;
	struct scsi_rw_10 *rw10;
	bus_dmamap_t xfer;
	int error;
	int s;
	int polled;

	GDT_DPRINTF(GDT_D_CMD, ("gdt_scsi_cmd "));

	s = splbio();

	xs->error = XS_NOERROR;

	if (target >= GDT_MAX_HDRIVES || !sc->sc_hdr[target].hd_present ||
	    link->lun != 0) {
		/*
		 * XXX Should be XS_SENSE but that would require setting up a
		 * faked sense too.
		 */
		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		splx(s);
		return;
	}

	/* Don't double enqueue if we came from gdt_chain. */
	if (xs != SIMPLEQ_FIRST(&sc->sc_queue))
		gdt_enqueue(sc, xs, 0);

	while ((xs = gdt_dequeue(sc)) != NULL) {
		xs->error = XS_NOERROR;
		ccb = NULL;
		link = xs->sc_link;
		target = link->target;
		polled = ISSET(xs->flags, SCSI_POLL);

		if (!gdt_polling && !(xs->flags & SCSI_POLL) &&
		    sc->sc_test_busy(sc)) {
			/*
			 * Put it back in front.  XXX Should we instead
			 * set xs->error to XS_BUSY?
			 */
			gdt_enqueue(sc, xs, 1);
			break;
		}

		switch (xs->cmd.opcode) {
		case TEST_UNIT_READY:
		case REQUEST_SENSE:
		case INQUIRY:
		case MODE_SENSE:
		case START_STOP:
		case READ_CAPACITY:
#if 0
		case VERIFY:
#endif
			gdt_internal_cache_cmd(xs);
			scsi_done(xs);
			goto ready;

		case PREVENT_ALLOW:
			GDT_DPRINTF(GDT_D_CMD, ("PREVENT/ALLOW "));
			/* XXX Not yet implemented */
			xs->error = XS_NOERROR;
			scsi_done(xs);
			goto ready;

		default:
			GDT_DPRINTF(GDT_D_CMD,
			    ("unknown opc %d ", xs->cmd.opcode));
			/* XXX Not yet implemented */
			xs->error = XS_DRIVER_STUFFUP;
			scsi_done(xs);
			goto ready;

		case READ_COMMAND:
		case READ_10:
		case WRITE_COMMAND:
		case WRITE_10:
		case SYNCHRONIZE_CACHE:
			/*
			 * A new command chain, start from the beginning.
			 */
			sc->sc_cmd_off = 0;

			if (xs->cmd.opcode == SYNCHRONIZE_CACHE) {
				 blockno = blockcnt = 0;
			} else {
				/* A read or write operation. */
				if (xs->cmdlen == 6) {
					rw = (struct scsi_rw *)&xs->cmd;
					blockno = _3btol(rw->addr) &
					    (SRW_TOPADDR << 16 | 0xffff);
					blockcnt =
					    rw->length ? rw->length : 0x100;
				} else {
					rw10 = (struct scsi_rw_10 *)&xs->cmd;
					blockno = _4btol(rw10->addr);
					blockcnt = _2btol(rw10->length);
				}
				if (blockno >= sc->sc_hdr[target].hd_size ||
				    blockno + blockcnt >
				    sc->sc_hdr[target].hd_size) {
					printf(
					    "%s: out of bounds %u-%u >= %u\n",
					    DEVNAME(sc), blockno,
					    blockcnt,
					    sc->sc_hdr[target].hd_size);
					/*
					 * XXX Should be XS_SENSE but that
					 * would require setting up a faked
					 * sense too.
					 */
					xs->error = XS_DRIVER_STUFFUP;
					scsi_done(xs);
					goto ready;
				}
			}

			ccb = xs->io;
			ccb->gc_blockno = blockno;
			ccb->gc_blockcnt = blockcnt;
			ccb->gc_xs = xs;
			ccb->gc_timeout = xs->timeout;
			ccb->gc_service = GDT_CACHESERVICE;
			ccb->gc_flags = 0;
			gdt_ccb_set_cmd(ccb, GDT_GCF_SCSI);

			if (xs->cmd.opcode != SYNCHRONIZE_CACHE) {
				xfer = ccb->gc_dmamap_xfer;
				error = bus_dmamap_load(sc->sc_dmat, xfer,
				    xs->data, xs->datalen, NULL,
				    (xs->flags & SCSI_NOSLEEP) ?
				    BUS_DMA_NOWAIT : BUS_DMA_WAITOK);
				if (error) {
					printf("%s: gdt_scsi_cmd: ",
					    DEVNAME(sc));
					if (error == EFBIG)
						printf(
						    "more than %d dma segs\n",
						    GDT_MAXOFFSETS);
					else
						printf("error %d "
						    "loading dma map\n",
						    error);

					xs->error = XS_DRIVER_STUFFUP;
					scsi_done(xs);
					goto ready;
				}
				bus_dmamap_sync(sc->sc_dmat, xfer, 0,
				    xfer->dm_mapsize,
				    (xs->flags & SCSI_DATA_IN) ?
				    BUS_DMASYNC_PREREAD :
				    BUS_DMASYNC_PREWRITE);
			}

			gdt_enqueue_ccb(sc, ccb);
			/* XXX what if enqueue did not start a transfer? */
			if (gdt_polling || (xs->flags & SCSI_POLL)) {
				if (!gdt_wait(sc, ccb, ccb->gc_timeout)) {
					printf("%s: command %d timed out\n",
					    DEVNAME(sc),
					    ccb->gc_cmd_index);
					xs->error = XS_TIMEOUT;
					scsi_done(xs);
					splx(s);
					return;
				}
			}
		}

	ready:
		/*
		 * Don't process the queue if we are polling.
		 */
		if (polled) {
			break;
		}
	}

	splx(s);
}

/* XXX Currently only for cacheservice, returns 0 if busy */
int
gdt_exec_ccb(struct gdt_ccb *ccb)
{
	struct scsi_xfer *xs = ccb->gc_xs;
	struct scsi_link *link = xs->sc_link;
	struct gdt_softc *sc = link->bus->sb_adapter_softc;
	u_int8_t target = link->target;
	u_int32_t sg_canz;
	bus_dmamap_t xfer;
	int i;
#if 1 /* XXX */
	static int __level = 0;

	if (__level++ > 0)
		panic("level > 0");
#endif
	GDT_DPRINTF(GDT_D_CMD, ("gdt_exec_ccb(%p, %p) ", xs, ccb));

	sc->sc_cmd_cnt = 0;

	/*
	 * XXX Yeah I know it's an always-true condition, but that may change
	 * later.
	 */
	if (sc->sc_cmd_cnt == 0)
		sc->sc_set_sema0(sc);

	gdt_enc32(sc->sc_cmd + GDT_CMD_COMMANDINDEX, ccb->gc_cmd_index);
	gdt_enc32(sc->sc_cmd + GDT_CMD_BOARDNODE, GDT_LOCALBOARD);
	gdt_enc16(sc->sc_cmd + GDT_CMD_UNION + GDT_CACHE_DEVICENO,
	    target);

	switch (xs->cmd.opcode) {
	case PREVENT_ALLOW:
	case SYNCHRONIZE_CACHE:
		if (xs->cmd.opcode == PREVENT_ALLOW) {
			/* XXX PREVENT_ALLOW support goes here */
		} else {
			GDT_DPRINTF(GDT_D_CMD,
			    ("SYNCHRONIZE CACHE tgt %d ", target));
			sc->sc_cmd[GDT_CMD_OPCODE] = GDT_FLUSH;
		}
		gdt_enc32(sc->sc_cmd + GDT_CMD_UNION + GDT_CACHE_BLOCKNO,
		    1);
		sg_canz = 0;
		break;

	case WRITE_COMMAND:
	case WRITE_10:
		/* XXX WRITE_THR could be supported too */
		sc->sc_cmd[GDT_CMD_OPCODE] = GDT_WRITE;
		break;

	case READ_COMMAND:
	case READ_10:
		sc->sc_cmd[GDT_CMD_OPCODE] = GDT_READ;
		break;
	}

	if (xs->cmd.opcode != PREVENT_ALLOW &&
	    xs->cmd.opcode != SYNCHRONIZE_CACHE) {
		gdt_enc32(sc->sc_cmd + GDT_CMD_UNION + GDT_CACHE_BLOCKNO,
		    ccb->gc_blockno);
		gdt_enc32(sc->sc_cmd + GDT_CMD_UNION + GDT_CACHE_BLOCKCNT,
		    ccb->gc_blockcnt);

		xfer = ccb->gc_dmamap_xfer;
		if (sc->sc_cache_feat & GDT_SCATTER_GATHER) {
			gdt_enc32(
			    sc->sc_cmd + GDT_CMD_UNION + GDT_CACHE_DESTADDR,
			    0xffffffff);
			for (i = 0; i < xfer->dm_nsegs; i++) {
				gdt_enc32(sc->sc_cmd + GDT_CMD_UNION +
				    GDT_CACHE_SG_LST + i * GDT_SG_SZ +
				    GDT_SG_PTR,
				    xfer->dm_segs[i].ds_addr);
				gdt_enc32(sc->sc_cmd + GDT_CMD_UNION +
				    GDT_CACHE_SG_LST + i * GDT_SG_SZ +
				    GDT_SG_LEN,
				    xfer->dm_segs[i].ds_len);
				GDT_DPRINTF(GDT_D_IO,
				    ("#%d pa %lx len %lx\n", i,
				    xfer->dm_segs[i].ds_addr,
				    xfer->dm_segs[i].ds_len));
			}
			sg_canz = xfer->dm_nsegs;
			gdt_enc32(
			    sc->sc_cmd + GDT_CMD_UNION + GDT_CACHE_SG_LST +
			    sg_canz * GDT_SG_SZ + GDT_SG_LEN, 0);
		} else {
			/* XXX Hardly correct */
			gdt_enc32(
			    sc->sc_cmd + GDT_CMD_UNION + GDT_CACHE_DESTADDR,
			    xfer->dm_segs[0].ds_addr);
			sg_canz = 0;
		}
	}
	gdt_enc32(sc->sc_cmd + GDT_CMD_UNION + GDT_CACHE_SG_CANZ, sg_canz);

	sc->sc_cmd_len =
	    roundup(GDT_CMD_UNION + GDT_CACHE_SG_LST + sg_canz * GDT_SG_SZ,
	    sizeof (u_int32_t));

	if (sc->sc_cmd_cnt > 0 &&
	    sc->sc_cmd_off + sc->sc_cmd_len + GDT_DPMEM_COMMAND_OFFSET >
	    sc->sc_ic_all_size) {
		printf("%s: DPMEM overflow\n", DEVNAME(sc));
		xs->error = XS_BUSY;
#if 1 /* XXX */
		__level--;
#endif
		return (0);
	}

	sc->sc_copy_cmd(sc, ccb);
	sc->sc_release_event(sc, ccb);

	xs->error = XS_NOERROR;
	xs->resid = 0;
#if 1 /* XXX */
	__level--;
#endif
	return (1);
}

/* Emulated SCSI operation on cache device */
void
gdt_internal_cache_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct gdt_softc *sc = link->bus->sb_adapter_softc;
	struct scsi_inquiry_data inq;
	struct scsi_sense_data sd;
	struct scsi_read_cap_data rcd;
	u_int8_t target = link->target;

	GDT_DPRINTF(GDT_D_CMD, ("gdt_internal_cache_cmd "));

	switch (xs->cmd.opcode) {
	case TEST_UNIT_READY:
	case START_STOP:
#if 0
	case VERIFY:
#endif
		GDT_DPRINTF(GDT_D_CMD, ("opc %d tgt %d ", xs->cmd.opcode,
		    target));
		break;

	case REQUEST_SENSE:
		GDT_DPRINTF(GDT_D_CMD, ("REQUEST SENSE tgt %d ", target));
		bzero(&sd, sizeof sd);
		sd.error_code = SSD_ERRCODE_CURRENT;
		sd.segment = 0;
		sd.flags = SKEY_NO_SENSE;
		gdt_enc32(sd.info, 0);
		sd.extra_len = 0;
		scsi_copy_internal_data(xs, &sd, sizeof(sd));
		break;

	case INQUIRY:
		GDT_DPRINTF(GDT_D_CMD, ("INQUIRY tgt %d devtype %x ", target,
		    sc->sc_hdr[target].hd_devtype));
		bzero(&inq, sizeof inq);
		inq.device =
		    (sc->sc_hdr[target].hd_devtype & 4) ? T_CDROM : T_DIRECT;
		inq.dev_qual2 =
		    (sc->sc_hdr[target].hd_devtype & 1) ? SID_REMOVABLE : 0;
		inq.version = SCSI_REV_2;
		inq.response_format = SID_SCSI2_RESPONSE;
		inq.additional_length = SID_SCSI2_ALEN;
		inq.flags |= SID_CmdQue;
		strlcpy(inq.vendor, "ICP	   ", sizeof inq.vendor);
		snprintf(inq.product, sizeof inq.product, "Host drive  #%02d",
		    target);
		strlcpy(inq.revision, "	 ", sizeof inq.revision);
		scsi_copy_internal_data(xs, &inq, sizeof(inq));
		break;

	case READ_CAPACITY:
		GDT_DPRINTF(GDT_D_CMD, ("READ CAPACITY tgt %d ", target));
		bzero(&rcd, sizeof rcd);
		_lto4b(sc->sc_hdr[target].hd_size - 1, rcd.addr);
		_lto4b(GDT_SECTOR_SIZE, rcd.length);
		scsi_copy_internal_data(xs, &rcd, sizeof(rcd));
		break;

	default:
		GDT_DPRINTF(GDT_D_CMD, ("unsupported scsi command %#x tgt %d ",
		    xs->cmd.opcode, target));
		xs->error = XS_DRIVER_STUFFUP;
		return;
	}

	xs->error = XS_NOERROR;
}

void
gdt_clear_events(struct gdt_softc *sc)
{
	GDT_DPRINTF(GDT_D_MISC, ("gdt_clear_events(%p) ", sc));

	/* XXX To be implemented */
}

int
gdt_async_event(struct gdt_softc *sc, int service)
{
	GDT_DPRINTF(GDT_D_INTR, ("gdt_async_event(%p, %d) ", sc, service));

	if (service == GDT_SCREENSERVICE) {
		/* XXX To be implemented */
	} else {
		/* XXX To be implemented */
	}

	return (0);
}

int
gdt_sync_event(struct gdt_softc *sc, int service, u_int8_t index,
    struct scsi_xfer *xs)
{
	GDT_DPRINTF(GDT_D_INTR,
	    ("gdt_sync_event(%p, %d, %d, %p) ", sc, service, index, xs));

	if (service == GDT_SCREENSERVICE) {
		GDT_DPRINTF(GDT_D_INTR, ("service == GDT_SCREENSERVICE "));
		/* XXX To be implemented */
		return (0);
	} else {
		switch (sc->sc_status) {
		case GDT_S_OK:
			GDT_DPRINTF(GDT_D_INTR, ("sc_status == GDT_S_OK "));
			/* XXX To be implemented */
			break;
		case GDT_S_BSY:
			GDT_DPRINTF(GDT_D_INTR, ("sc_status == GDT_S_BSY "));
			/* XXX To be implemented */
			return (2);
		default:
			GDT_DPRINTF(GDT_D_INTR, ("sc_status is %d ",
			    sc->sc_status));
			/* XXX To be implemented */
			return (0);
		}
	}

	return (1);
}

int
gdt_intr(void *arg)
{
	struct gdt_softc *sc = arg;
	struct gdt_intr_ctx ctx;
	int chain = 1;
	int sync_val = 0;
	struct scsi_xfer *xs = NULL;
	int prev_cmd;
	struct gdt_ccb *ccb;

	GDT_DPRINTF(GDT_D_INTR, ("gdt_intr(%p) ", sc));

	/* If polling and we were not called from gdt_wait, just return */
	if (gdt_polling && !gdt_from_wait)
		return (0);

	ctx.istatus = sc->sc_get_status(sc);
	if (!ctx.istatus) {
		sc->sc_status = GDT_S_NO_STATUS;
		return (0);
	}

	gdt_wait_index = 0;
	ctx.service = ctx.info2 = 0;

	sc->sc_intr(sc, &ctx);

	sc->sc_status = ctx.cmd_status;
	sc->sc_info = ctx.info;
	sc->sc_info2 = ctx.info2;

	if (gdt_from_wait) {
		gdt_wait_gdt = sc;
		gdt_wait_index = ctx.istatus;
	}

	switch (ctx.istatus) {
	case GDT_ASYNCINDEX:
		gdt_async_event(sc, ctx.service);
		goto finish;

	case GDT_SPEZINDEX:
		printf("%s: uninitialized or unknown service (%d %d)\n",
		    DEVNAME(sc), ctx.info, ctx.info2);
		chain = 0;
		goto finish;
	}

	ccb = &sc->sc_ccbs[ctx.istatus - 2];
	xs = ccb->gc_xs;
	if (!gdt_polling)
		timeout_del(&xs->stimeout);
	ctx.service = ccb->gc_service;
	prev_cmd = ccb->gc_flags & GDT_GCF_CMD_MASK;
	if (xs && xs->cmd.opcode != PREVENT_ALLOW &&
	    xs->cmd.opcode != SYNCHRONIZE_CACHE) {
		bus_dmamap_sync(sc->sc_dmat, ccb->gc_dmamap_xfer, 0,
		    ccb->gc_dmamap_xfer->dm_mapsize,
		    (xs->flags & SCSI_DATA_IN) ? BUS_DMASYNC_POSTREAD :
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, ccb->gc_dmamap_xfer);
	}
	switch (prev_cmd) {
	case GDT_GCF_UNUSED:
		/* XXX Not yet implemented */
		chain = 0;
		goto finish;
	case GDT_GCF_INTERNAL:
		chain = 0;
		goto finish;
	}

	sync_val = gdt_sync_event(sc, ctx.service, ctx.istatus, xs);

 finish:
	switch (sync_val) {
	case 0:
		if (xs && gdt_from_wait)
			scsi_done(xs);
		break;
	case 1:
		scsi_done(xs);
		break;

	case 2:
		gdt_enqueue(sc, xs, 0);
	}

	if (chain)
		gdt_chain(sc);

	return (1);
}

int
gdt_wait(struct gdt_softc *sc, struct gdt_ccb *ccb, int timeout)
{
	int s, rslt, rv = 0;

	GDT_DPRINTF(GDT_D_MISC,
	    ("gdt_wait(%p, %p, %d) ", sc, ccb, timeout));

	gdt_from_wait = 1;
	do {
		s = splbio();
		rslt = gdt_intr(sc);
		splx(s);
		if (rslt && sc == gdt_wait_gdt &&
		    ccb->gc_cmd_index == gdt_wait_index) {
			rv = 1;
			break;
		}
		DELAY(1000); /* 1 millisecond */
	} while (--timeout);
	gdt_from_wait = 0;

	while (sc->sc_test_busy(sc))
		DELAY(0);		/* XXX correct? */

	return (rv);
}

int
gdt_internal_cmd(struct gdt_softc *sc, u_int8_t service, u_int16_t opcode,
    u_int32_t arg1, u_int32_t arg2, u_int32_t arg3)
{
	int retries, rslt;
	struct gdt_ccb *ccb;

	GDT_DPRINTF(GDT_D_CMD, ("gdt_internal_cmd(%p, %d, %d, %d, %d, %d) ",
	    sc, service, opcode, arg1, arg2, arg3));

	bzero(sc->sc_cmd, GDT_CMD_SZ);

	for (retries = GDT_RETRIES; ; ) {
		ccb = scsi_io_get(&sc->sc_iopool, SCSI_NOSLEEP);
		if (ccb == NULL) {
			printf("%s: no free command index found\n",
			    DEVNAME(sc));
			return (0);
		}
		ccb->gc_service = service;
		ccb->gc_xs = NULL;
		ccb->gc_blockno = ccb->gc_blockcnt = 0;
		ccb->gc_timeout = ccb->gc_flags = 0;
		ccb->gc_service = GDT_CACHESERVICE;
		gdt_ccb_set_cmd(ccb, GDT_GCF_INTERNAL);

		sc->sc_set_sema0(sc);
		gdt_enc32(sc->sc_cmd + GDT_CMD_COMMANDINDEX,
		    ccb->gc_cmd_index);
		gdt_enc16(sc->sc_cmd + GDT_CMD_OPCODE, opcode);
		gdt_enc32(sc->sc_cmd + GDT_CMD_BOARDNODE, GDT_LOCALBOARD);

		switch (service) {
		case GDT_CACHESERVICE:
			if (opcode == GDT_IOCTL) {
				gdt_enc32(sc->sc_cmd + GDT_CMD_UNION +
				    GDT_IOCTL_SUBFUNC, arg1);
				gdt_enc32(sc->sc_cmd + GDT_CMD_UNION +
				    GDT_IOCTL_CHANNEL, arg2);
				gdt_enc16(sc->sc_cmd + GDT_CMD_UNION +
				    GDT_IOCTL_PARAM_SIZE, (u_int16_t)arg3);
				gdt_enc32(sc->sc_cmd + GDT_CMD_UNION +
				    GDT_IOCTL_P_PARAM,
				    sc->sc_scratch_seg.ds_addr);
			} else {
				gdt_enc16(sc->sc_cmd + GDT_CMD_UNION +
				    GDT_CACHE_DEVICENO, (u_int16_t)arg1);
				gdt_enc32(sc->sc_cmd + GDT_CMD_UNION +
				    GDT_CACHE_BLOCKNO, arg2);
			}
			break;

		case GDT_SCSIRAWSERVICE:
			gdt_enc32(sc->sc_cmd + GDT_CMD_UNION +
			    GDT_RAW_DIRECTION, arg1);
			sc->sc_cmd[GDT_CMD_UNION + GDT_RAW_BUS] =
			    (u_int8_t)arg2;
			sc->sc_cmd[GDT_CMD_UNION + GDT_RAW_TARGET] =
			    (u_int8_t)arg3;
			sc->sc_cmd[GDT_CMD_UNION + GDT_RAW_LUN] =
			    (u_int8_t)(arg3 >> 8);
		}

		sc->sc_cmd_len = GDT_CMD_SZ;
		sc->sc_cmd_off = 0;
		sc->sc_cmd_cnt = 0;
		sc->sc_copy_cmd(sc, ccb);
		sc->sc_release_event(sc, ccb);
		DELAY(20);

		rslt = gdt_wait(sc, ccb, GDT_POLL_TIMEOUT);
		scsi_io_put(&sc->sc_iopool, ccb);

		if (!rslt)
			return (0);
		if (sc->sc_status != GDT_S_BSY || --retries == 0)
			break;
		DELAY(1);
	}
	return (sc->sc_status == GDT_S_OK);
}

void *
gdt_ccb_alloc(void *xsc)
{
	struct gdt_softc *sc = xsc;
	struct gdt_ccb *ccb;

	GDT_DPRINTF(GDT_D_QUEUE, ("gdt_ccb_alloc(%p) ", sc));

	mtx_enter(&sc->sc_ccb_mtx);
	ccb = TAILQ_FIRST(&sc->sc_free_ccb);
	if (ccb != NULL)
		TAILQ_REMOVE(&sc->sc_free_ccb, ccb, gc_chain);
	mtx_leave(&sc->sc_ccb_mtx);

	return (ccb);
}

void
gdt_ccb_free(void *xsc, void *xccb)
{
	struct gdt_softc *sc = xsc;
	struct gdt_ccb *ccb = xccb;
	int wake = 0;

	GDT_DPRINTF(GDT_D_QUEUE, ("gdt_ccb_free(%p, %p) ", sc, ccb));

	mtx_enter(&sc->sc_ccb_mtx);
	TAILQ_INSERT_HEAD(&sc->sc_free_ccb, ccb, gc_chain);
	/* If the free list was empty, wake up potential waiters. */
	if (TAILQ_NEXT(ccb, gc_chain) == NULL)
		wake = 1;
	mtx_leave(&sc->sc_ccb_mtx);

	if (wake)
		wakeup(&sc->sc_free_ccb);
}

void
gdt_enqueue_ccb(struct gdt_softc *sc, struct gdt_ccb *ccb)
{
	GDT_DPRINTF(GDT_D_QUEUE, ("gdt_enqueue_ccb(%p, %p) ", sc, ccb));

	timeout_set(&ccb->gc_xs->stimeout, gdt_timeout, ccb);
	TAILQ_INSERT_TAIL(&sc->sc_ccbq, ccb, gc_chain);
	gdt_start_ccbs(sc);
}

void
gdt_start_ccbs(struct gdt_softc *sc)
{
	struct gdt_ccb *ccb;
	struct scsi_xfer *xs;

	GDT_DPRINTF(GDT_D_QUEUE, ("gdt_start_ccbs(%p) ", sc));

	while ((ccb = TAILQ_FIRST(&sc->sc_ccbq)) != NULL) {

		xs = ccb->gc_xs;
		if (ccb->gc_flags & GDT_GCF_WATCHDOG)
			timeout_del(&xs->stimeout);

		if (gdt_exec_ccb(ccb) == 0) {
			ccb->gc_flags |= GDT_GCF_WATCHDOG;
			timeout_set(&ccb->gc_xs->stimeout, gdt_watchdog, ccb);
			timeout_add_msec(&xs->stimeout, GDT_WATCH_TIMEOUT);
			break;
		}
		TAILQ_REMOVE(&sc->sc_ccbq, ccb, gc_chain);

		if ((xs->flags & SCSI_POLL) == 0) {
			timeout_set(&ccb->gc_xs->stimeout, gdt_timeout, ccb);
			timeout_add_msec(&xs->stimeout, ccb->gc_timeout);
		}
	}
}

void
gdt_chain(struct gdt_softc *sc)
{
	GDT_DPRINTF(GDT_D_INTR, ("gdt_chain(%p) ", sc));

	if (!SIMPLEQ_EMPTY(&sc->sc_queue))
		gdt_scsi_cmd(SIMPLEQ_FIRST(&sc->sc_queue));
}

void
gdt_timeout(void *arg)
{
	struct gdt_ccb *ccb = arg;
	struct scsi_link *link = ccb->gc_xs->sc_link;
	struct gdt_softc *sc = link->bus->sb_adapter_softc;
	int s;

	sc_print_addr(link);
	printf("timed out\n");

	/* XXX Test for multiple timeouts */

	ccb->gc_xs->error = XS_TIMEOUT;
	s = splbio();
	gdt_enqueue_ccb(sc, ccb);
	splx(s);
}

void
gdt_watchdog(void *arg)
{
	struct gdt_ccb *ccb = arg;
	struct scsi_link *link = ccb->gc_xs->sc_link;
	struct gdt_softc *sc = link->bus->sb_adapter_softc;
	int s;

	s = splbio();
	ccb->gc_flags &= ~GDT_GCF_WATCHDOG;
	gdt_start_ccbs(sc);
	splx(s);
}

#if NBIO > 0
int
gdt_ioctl(struct device *dev, u_long cmd, caddr_t addr)
{
	struct gdt_softc *sc = (struct gdt_softc *)dev;
	int error = 0;

	GDT_DPRINTF(GDT_D_IOCTL, ("%s: ioctl ", DEVNAME(sc)));

	switch (cmd) {
	case BIOCINQ:
		GDT_DPRINTF(GDT_D_IOCTL, ("inq "));
		error = gdt_ioctl_inq(sc, (struct bioc_inq *)addr);
		break;

	case BIOCVOL:
		GDT_DPRINTF(GDT_D_IOCTL, ("vol "));
		error = gdt_ioctl_vol(sc, (struct bioc_vol *)addr);
		break;

	case BIOCDISK:
		GDT_DPRINTF(GDT_D_IOCTL, ("disk "));
		error = gdt_ioctl_disk(sc, (struct bioc_disk *)addr);
		break;

	case BIOCALARM:
		GDT_DPRINTF(GDT_D_IOCTL, ("alarm "));
		error = gdt_ioctl_alarm(sc, (struct bioc_alarm *)addr);
		break;

	case BIOCSETSTATE:
		GDT_DPRINTF(GDT_D_IOCTL, ("setstate "));
		error = gdt_ioctl_setstate(sc, (struct bioc_setstate *)addr);
		break;

	default:
		GDT_DPRINTF(GDT_D_IOCTL, (" invalid ioctl\n"));
		error = ENOTTY;
	}

	return (error);
}

int
gdt_ioctl_inq(struct gdt_softc *sc, struct bioc_inq *bi)
{
	bi->bi_novol = sc->sc_ndevs;
	bi->bi_nodisk = sc->sc_total_disks;

	strlcpy(bi->bi_dev, DEVNAME(sc), sizeof(bi->bi_dev));

	return (0);
}

int
gdt_ioctl_vol(struct gdt_softc *sc, struct bioc_vol *bv)
{
	return (1); /* XXX not yet */
}

int
gdt_ioctl_disk(struct gdt_softc *sc, struct bioc_disk *bd)
{
	return (1); /* XXX not yet */
}

int
gdt_ioctl_alarm(struct gdt_softc *sc, struct bioc_alarm *ba)
{
	return (1); /* XXX not yet */
}

int
gdt_ioctl_setstate(struct gdt_softc *sc, struct bioc_setstate *bs)
{
	return (1); /* XXX not yet */
}
#endif /* NBIO > 0 */
