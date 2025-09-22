/*	$OpenBSD: ami.c,v 1.264 2024/09/20 02:00:46 jsg Exp $	*/

/*
 * Copyright (c) 2001 Michael Shalayeff
 * Copyright (c) 2005 Marco Peereboom
 * Copyright (c) 2006 David Gwynne
 * All rights reserved.
 *
 * The SCSI emulation layer is derived from gdt(4) driver,
 * Copyright (c) 1999, 2000 Niklas Hallqvist. All rights reserved.
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
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * American Megatrends Inc. MegaRAID controllers driver
 *
 * This driver was made because these ppl and organizations
 * donated hardware and provided documentation:
 *
 * - 428 model card
 *	John Kerbawy, Stephan Matis, Mark Stovall;
 *
 * - 467 and 475 model cards, docs
 *	American Megatrends Inc.;
 *
 * - uninterruptible electric power for cvs
 *	Theo de Raadt.
 */

#include "bio.h"

/* #define	AMI_DEBUG */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/ioctl.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/rwlock.h>
#include <sys/pool.h>
#include <sys/sensors.h>

#include <machine/bus.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

#include <dev/biovar.h>
#include <dev/ic/amireg.h>
#include <dev/ic/amivar.h>

#ifdef AMI_DEBUG
#define	AMI_DPRINTF(m,a)	do { if (ami_debug & (m)) printf a; } while (0)
#define	AMI_D_CMD	0x0001
#define	AMI_D_INTR	0x0002
#define	AMI_D_MISC	0x0004
#define	AMI_D_DMA	0x0008
#define	AMI_D_IOCTL	0x0010
int ami_debug = 0
/*	| AMI_D_CMD */
/*	| AMI_D_INTR */
/*	| AMI_D_MISC */
/*	| AMI_D_DMA */
/*	| AMI_D_IOCTL */
	;
#else
#define	AMI_DPRINTF(m,a)	/* m, a */
#endif

struct cfdriver ami_cd = {
	NULL, "ami", DV_DULL
};

void	ami_scsi_cmd(struct scsi_xfer *);
int	ami_scsi_ioctl(struct scsi_link *, u_long, caddr_t, int);

const struct scsi_adapter ami_switch = {
	ami_scsi_cmd, NULL, NULL, NULL, ami_scsi_ioctl
};

void	ami_scsi_raw_cmd(struct scsi_xfer *);

const struct scsi_adapter ami_raw_switch = {
	ami_scsi_raw_cmd, NULL, NULL, NULL, NULL
};

void *		ami_get_ccb(void *);
void		ami_put_ccb(void *, void *);

u_int32_t	ami_read(struct ami_softc *, bus_size_t);
void		ami_write(struct ami_softc *, bus_size_t, u_int32_t);

void		ami_copyhds(struct ami_softc *, const u_int32_t *,
		    const u_int8_t *, const u_int8_t *);
struct ami_mem	*ami_allocmem(struct ami_softc *, size_t);
void		ami_freemem(struct ami_softc *, struct ami_mem *);
int		ami_alloc_ccbs(struct ami_softc *, int);

int		ami_poll(struct ami_softc *, struct ami_ccb *);
void		ami_start(struct ami_softc *, struct ami_ccb *);
void		ami_complete(struct ami_softc *, struct ami_ccb *, int);
void		ami_runqueue_tick(void *);
void		ami_runqueue(struct ami_softc *);

void 		ami_start_xs(struct ami_softc *sc, struct ami_ccb *,
		    struct scsi_xfer *);
void		ami_done_xs(struct ami_softc *, struct ami_ccb *);
void		ami_done_pt(struct ami_softc *, struct ami_ccb *);
void		ami_done_flush(struct ami_softc *, struct ami_ccb *);
void		ami_done_sysflush(struct ami_softc *, struct ami_ccb *);

void		ami_done_dummy(struct ami_softc *, struct ami_ccb *);
void		ami_done_ioctl(struct ami_softc *, struct ami_ccb *);
void		ami_done_init(struct ami_softc *, struct ami_ccb *);

int		ami_load_ptmem(struct ami_softc*, struct ami_ccb *,
		    void *, size_t, int, int);

#if NBIO > 0
int		ami_mgmt(struct ami_softc *, u_int8_t, u_int8_t, u_int8_t,
		    u_int8_t, size_t, void *);
int		ami_drv_pt(struct ami_softc *, u_int8_t, u_int8_t, u_int8_t *,
		    int, int, void *);
int		ami_drv_readcap(struct ami_softc *, u_int8_t, u_int8_t,
		    daddr_t *);
int		ami_drv_inq(struct ami_softc *, u_int8_t, u_int8_t, u_int8_t,
		    void *);
int		ami_ioctl(struct device *, u_long, caddr_t);
int		ami_ioctl_inq(struct ami_softc *, struct bioc_inq *);
int		ami_vol(struct ami_softc *, struct bioc_vol *,
		    struct ami_big_diskarray *);
int		ami_disk(struct ami_softc *, struct bioc_disk *,
		    struct ami_big_diskarray *);
int		ami_ioctl_vol(struct ami_softc *, struct bioc_vol *);
int		ami_ioctl_disk(struct ami_softc *, struct bioc_disk *);
int		ami_ioctl_alarm(struct ami_softc *, struct bioc_alarm *);
int		ami_ioctl_setstate(struct ami_softc *, struct bioc_setstate *);

#ifndef SMALL_KERNEL
int		ami_create_sensors(struct ami_softc *);
void		ami_refresh_sensors(void *);
#endif
#endif /* NBIO > 0 */

#define DEVNAME(_s)	((_s)->sc_dev.dv_xname)

void *
ami_get_ccb(void *xsc)
{
	struct ami_softc *sc = xsc;
	struct ami_ccb *ccb;

	mtx_enter(&sc->sc_ccb_freeq_mtx);
	ccb = TAILQ_FIRST(&sc->sc_ccb_freeq);
	if (ccb != NULL) {
		TAILQ_REMOVE(&sc->sc_ccb_freeq, ccb, ccb_link);
		ccb->ccb_state = AMI_CCB_READY;
	}
	mtx_leave(&sc->sc_ccb_freeq_mtx);

	return (ccb);
}

void
ami_put_ccb(void *xsc, void *xccb)
{
	struct ami_softc *sc = xsc;
	struct ami_ccb *ccb = xccb;

	ccb->ccb_state = AMI_CCB_FREE;
	ccb->ccb_xs = NULL;
	ccb->ccb_flags = 0;
	ccb->ccb_done = NULL;

	mtx_enter(&sc->sc_ccb_freeq_mtx);
	TAILQ_INSERT_TAIL(&sc->sc_ccb_freeq, ccb, ccb_link);
	mtx_leave(&sc->sc_ccb_freeq_mtx);
}

u_int32_t
ami_read(struct ami_softc *sc, bus_size_t r)
{
	u_int32_t rv;

	bus_space_barrier(sc->sc_iot, sc->sc_ioh, r, 4,
	    BUS_SPACE_BARRIER_READ);
	rv = bus_space_read_4(sc->sc_iot, sc->sc_ioh, r);

	AMI_DPRINTF(AMI_D_CMD, ("ari 0x%lx 0x08%x ", r, rv));
	return (rv);
}

void
ami_write(struct ami_softc *sc, bus_size_t r, u_int32_t v)
{
	AMI_DPRINTF(AMI_D_CMD, ("awo 0x%lx 0x%08x ", r, v));

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, r, v);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, r, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

struct ami_mem *
ami_allocmem(struct ami_softc *sc, size_t size)
{
	struct ami_mem		*am;
	int			nsegs;

	am = malloc(sizeof(struct ami_mem), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (am == NULL)
		return (NULL);

	am->am_size = size;

	if (bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &am->am_map) != 0)
		goto amfree;

	if (bus_dmamem_alloc(sc->sc_dmat, size, PAGE_SIZE, 0, &am->am_seg, 1,
	    &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO) != 0)
		goto destroy;

	if (bus_dmamem_map(sc->sc_dmat, &am->am_seg, nsegs, size, &am->am_kva,
	    BUS_DMA_NOWAIT) != 0)
		goto free;

	if (bus_dmamap_load(sc->sc_dmat, am->am_map, am->am_kva, size, NULL,
	    BUS_DMA_NOWAIT) != 0)
		goto unmap;

	return (am);

unmap:
	bus_dmamem_unmap(sc->sc_dmat, am->am_kva, size);
free:
	bus_dmamem_free(sc->sc_dmat, &am->am_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, am->am_map);
amfree:
	free(am, M_DEVBUF, sizeof *am);

	return (NULL);
}

void
ami_freemem(struct ami_softc *sc, struct ami_mem *am)
{
	bus_dmamap_unload(sc->sc_dmat, am->am_map);
	bus_dmamem_unmap(sc->sc_dmat, am->am_kva, am->am_size);
	bus_dmamem_free(sc->sc_dmat, &am->am_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, am->am_map);
	free(am, M_DEVBUF, sizeof *am);
}

void
ami_copyhds(struct ami_softc *sc, const u_int32_t *sizes,
    const u_int8_t *props, const u_int8_t *stats)
{
	int i;

	for (i = 0; i < sc->sc_nunits; i++) {
		sc->sc_hdr[i].hd_present = 1;
		sc->sc_hdr[i].hd_is_logdrv = 1;
		sc->sc_hdr[i].hd_size = letoh32(sizes[i]);
		sc->sc_hdr[i].hd_prop = props[i];
		sc->sc_hdr[i].hd_stat = stats[i];
	}
}

int
ami_alloc_ccbs(struct ami_softc *sc, int nccbs)
{
	struct ami_ccb *ccb;
	struct ami_ccbmem *ccbmem, *mem;
	int i, error;

	sc->sc_ccbs = mallocarray(nccbs, sizeof(struct ami_ccb),
	    M_DEVBUF, M_NOWAIT);
	if (sc->sc_ccbs == NULL) {
		printf(": unable to allocate ccbs\n");
		return (1);
	}

	sc->sc_ccbmem_am = ami_allocmem(sc, sizeof(struct ami_ccbmem) * nccbs);
	if (sc->sc_ccbmem_am == NULL) {
		printf(": unable to allocate ccb dmamem\n");
		goto free_ccbs;
	}
	ccbmem = AMIMEM_KVA(sc->sc_ccbmem_am);

	TAILQ_INIT(&sc->sc_ccb_freeq);
	mtx_init(&sc->sc_ccb_freeq_mtx, IPL_BIO);
	TAILQ_INIT(&sc->sc_ccb_preq);
	TAILQ_INIT(&sc->sc_ccb_runq);
	timeout_set(&sc->sc_run_tmo, ami_runqueue_tick, sc);

	scsi_iopool_init(&sc->sc_iopool, sc, ami_get_ccb, ami_put_ccb);

	for (i = 0; i < nccbs; i++) {
		ccb = &sc->sc_ccbs[i];
		mem = &ccbmem[i];

		error = bus_dmamap_create(sc->sc_dmat, AMI_MAXFER,
		    AMI_MAXOFFSETS, AMI_MAXFER, 0,
		    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &ccb->ccb_dmamap);
		if (error) {
			printf(": cannot create ccb dmamap (%d)\n", error);
			goto free_list;
		}

		ccb->ccb_sc = sc;

		ccb->ccb_cmd.acc_id = i + 1;
		ccb->ccb_offset = sizeof(struct ami_ccbmem) * i;

		ccb->ccb_pt = &mem->cd_pt;
		ccb->ccb_ptpa = htole32(AMIMEM_DVA(sc->sc_ccbmem_am) +
		    ccb->ccb_offset);

		ccb->ccb_sglist = mem->cd_sg;
		ccb->ccb_sglistpa = htole32(AMIMEM_DVA(sc->sc_ccbmem_am) +
		    ccb->ccb_offset + sizeof(struct ami_passthrough));

		/* override last command for management */
		if (i == nccbs - 1) {
			ccb->ccb_cmd.acc_id = 0xfe;
			sc->sc_mgmtccb = ccb;
		} else {
			ami_put_ccb(sc, ccb);
		}
	}

	return (0);

free_list:
	while ((ccb = ami_get_ccb(sc)) != NULL)
		bus_dmamap_destroy(sc->sc_dmat, ccb->ccb_dmamap);

	ami_freemem(sc, sc->sc_ccbmem_am);
free_ccbs:
	free(sc->sc_ccbs, M_DEVBUF, 0);

	return (1);
}

int
ami_attach(struct ami_softc *sc)
{
	struct scsibus_attach_args saa;
	struct ami_rawsoftc *rsc;
	struct ami_ccb iccb;
	struct ami_iocmd *cmd;
	struct ami_mem *am;
	struct ami_inquiry *inq;
	struct ami_fc_einquiry *einq;
	struct ami_fc_prodinfo *pi;
	const char *p;
	paddr_t	pa;

	mtx_init(&sc->sc_cmd_mtx, IPL_BIO);

	am = ami_allocmem(sc, NBPG);
	if (am == NULL) {
		printf(": unable to allocate init data\n");
		return (1);
	}
	pa = htole32(AMIMEM_DVA(am));

	sc->sc_mbox_am = ami_allocmem(sc, sizeof(struct ami_iocmd));
	if (sc->sc_mbox_am == NULL) {
		printf(": unable to allocate mbox\n");
		goto free_idata;
	}
	sc->sc_mbox = (volatile struct ami_iocmd *)AMIMEM_KVA(sc->sc_mbox_am);
	sc->sc_mbox_pa = htole32(AMIMEM_DVA(sc->sc_mbox_am));
	AMI_DPRINTF(AMI_D_CMD, ("mbox=%p ", sc->sc_mbox));
	AMI_DPRINTF(AMI_D_CMD, ("mbox_pa=0x%llx ", (long long)sc->sc_mbox_pa));

	/* create a spartan ccb for use with ami_poll */
	bzero(&iccb, sizeof(iccb));
	iccb.ccb_sc = sc;
	iccb.ccb_done = ami_done_init;
	cmd = &iccb.ccb_cmd;

	(sc->sc_init)(sc);

	/* try FC inquiry first */
	cmd->acc_cmd = AMI_FCOP;
	cmd->acc_io.aio_channel = AMI_FC_EINQ3;
	cmd->acc_io.aio_param = AMI_FC_EINQ3_SOLICITED_FULL;
	cmd->acc_io.aio_data = pa;
	if (ami_poll(sc, &iccb) == 0) {
		einq = AMIMEM_KVA(am);
		pi = AMIMEM_KVA(am);

		sc->sc_nunits = einq->ain_nlogdrv;
		sc->sc_drvinscnt = einq->ain_drvinscnt + 1; /* force scan */
		ami_copyhds(sc, einq->ain_ldsize, einq->ain_ldprop,
		    einq->ain_ldstat);

		cmd->acc_cmd = AMI_FCOP;
		cmd->acc_io.aio_channel = AMI_FC_PRODINF;
		cmd->acc_io.aio_param = 0;
		cmd->acc_io.aio_data = pa;
		if (ami_poll(sc, &iccb) == 0) {
			sc->sc_maxunits = AMI_BIG_MAX_LDRIVES;

			bcopy (pi->api_fwver, sc->sc_fwver, 16);
			sc->sc_fwver[15] = '\0';
			bcopy (pi->api_biosver, sc->sc_biosver, 16);
			sc->sc_biosver[15] = '\0';
			sc->sc_channels = pi->api_channels;
			sc->sc_targets = pi->api_fcloops;
			sc->sc_memory = letoh16(pi->api_ramsize);
			sc->sc_maxcmds = pi->api_maxcmd;
			p = "FC loop";
		}
	}

	if (sc->sc_maxunits == 0) {
		inq = AMIMEM_KVA(am);

		cmd->acc_cmd = AMI_EINQUIRY;
		cmd->acc_io.aio_channel = 0;
		cmd->acc_io.aio_param = 0;
		cmd->acc_io.aio_data = pa;
		if (ami_poll(sc, &iccb) != 0) {
			cmd->acc_cmd = AMI_INQUIRY;
			cmd->acc_io.aio_channel = 0;
			cmd->acc_io.aio_param = 0;
			cmd->acc_io.aio_data = pa;
			if (ami_poll(sc, &iccb) != 0) {
				printf(": cannot do inquiry\n");
				goto free_mbox;
			}
		}

		sc->sc_maxunits = AMI_MAX_LDRIVES;
		sc->sc_nunits = inq->ain_nlogdrv;
		ami_copyhds(sc, inq->ain_ldsize, inq->ain_ldprop,
		    inq->ain_ldstat);

		bcopy (inq->ain_fwver, sc->sc_fwver, 4);
		sc->sc_fwver[4] = '\0';
		bcopy (inq->ain_biosver, sc->sc_biosver, 4);
		sc->sc_biosver[4] = '\0';
		sc->sc_channels = inq->ain_channels;
		sc->sc_targets = inq->ain_targets;
		sc->sc_memory = inq->ain_ramsize;
		sc->sc_maxcmds = inq->ain_maxcmd;
		sc->sc_drvinscnt = inq->ain_drvinscnt + 1; /* force scan */
		p = "target";
	}

	if (sc->sc_flags & AMI_BROKEN) {
		sc->sc_maxcmds = 1;
		sc->sc_maxunits = 1;
	} else {
		sc->sc_maxunits = AMI_BIG_MAX_LDRIVES;
		if (sc->sc_maxcmds > AMI_MAXCMDS)
			sc->sc_maxcmds = AMI_MAXCMDS;
		/*
		 * Reserve ccb's for ioctl's and raw commands to
		 * processors/enclosures by lowering the number of
		 * openings available for logical units.
		 */
		sc->sc_maxcmds -= AMI_MAXIOCTLCMDS + AMI_MAXPROCS *
		    AMI_MAXRAWCMDS * sc->sc_channels;
	}

	if (ami_alloc_ccbs(sc, AMI_MAXCMDS + 1) != 0) {
		/* error already printed */
		goto free_mbox;
	}

	ami_freemem(sc, am);

	/* hack for hp netraid version encoding */
	if ('A' <= sc->sc_fwver[2] && sc->sc_fwver[2] <= 'Z' &&
	    sc->sc_fwver[1] < ' ' && sc->sc_fwver[0] < ' ' &&
	    'A' <= sc->sc_biosver[2] && sc->sc_biosver[2] <= 'Z' &&
	    sc->sc_biosver[1] < ' ' && sc->sc_biosver[0] < ' ') {

		snprintf(sc->sc_fwver, sizeof sc->sc_fwver, "%c.%02d.%02d",
		    sc->sc_fwver[2], sc->sc_fwver[1], sc->sc_fwver[0]);
		snprintf(sc->sc_biosver, sizeof sc->sc_biosver, "%c.%02d.%02d",
		    sc->sc_biosver[2], sc->sc_biosver[1], sc->sc_biosver[0]);
	}

	/* TODO: fetch & print cache strategy */
	/* TODO: fetch & print scsi and raid info */

#ifdef AMI_DEBUG
	printf(", FW %s, BIOS v%s, %dMB RAM\n"
	    "%s: %d channels, %d %ss, %d logical drives, "
	    "max commands %d, quirks: %04x\n",
	    sc->sc_fwver, sc->sc_biosver, sc->sc_memory, DEVNAME(sc),
	    sc->sc_channels, sc->sc_targets, p, sc->sc_nunits,
	    sc->sc_maxcmds, sc->sc_flags);
#else
	printf(", FW %s, BIOS v%s, %dMB RAM\n"
	    "%s: %d channels, %d %ss, %d logical drives\n",
	    sc->sc_fwver, sc->sc_biosver, sc->sc_memory, DEVNAME(sc),
	    sc->sc_channels, sc->sc_targets, p, sc->sc_nunits);
#endif /* AMI_DEBUG */

	if (sc->sc_flags & AMI_BROKEN && sc->sc_nunits > 1)
		printf("%s: firmware buggy, limiting access to first logical "
		    "disk\n", DEVNAME(sc));

	/* lock around ioctl requests */
	rw_init(&sc->sc_lock, NULL);

	saa.saa_adapter_softc = sc;
	saa.saa_adapter = &ami_switch;
	saa.saa_adapter_target = SDEV_NO_ADAPTER_TARGET;
	saa.saa_adapter_buswidth = sc->sc_maxunits;
	saa.saa_luns = 8;
	saa.saa_openings = sc->sc_maxcmds;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	sc->sc_scsibus = (struct scsibus_softc *)config_found(&sc->sc_dev, &saa,
	    scsiprint);

	/* can't do bioctls, sensors, or pass-through on broken devices */
	if (sc->sc_flags & AMI_BROKEN)
		return (0);

#if NBIO > 0
	if (bio_register(&sc->sc_dev, ami_ioctl) != 0)
		printf("%s: controller registration failed\n", DEVNAME(sc));
	else
		sc->sc_ioctl = ami_ioctl;

#ifndef SMALL_KERNEL
	if (ami_create_sensors(sc) != 0)
		printf("%s: unable to create sensors\n", DEVNAME(sc));
#endif
#endif

	rsc = mallocarray(sc->sc_channels, sizeof(struct ami_rawsoftc),
	    M_DEVBUF, M_NOWAIT|M_ZERO);
	if (!rsc) {
		printf("%s: no memory for raw interface\n", DEVNAME(sc));
		return (0);
	}

	for (sc->sc_rawsoftcs = rsc;
	     rsc < &sc->sc_rawsoftcs[sc->sc_channels]; rsc++) {

		struct scsibus_softc *ptbus;
		struct scsi_link *proclink;
		struct device *procdev;

		rsc->sc_softc = sc;
		rsc->sc_channel = rsc - sc->sc_rawsoftcs;
		rsc->sc_proctarget = -1;

		/* TODO fetch adapter_target from the controller */

		saa.saa_adapter_softc = rsc;
		saa.saa_adapter = &ami_raw_switch;
		saa.saa_adapter_target = SDEV_NO_ADAPTER_TARGET;
		saa.saa_adapter_buswidth = 16;
		saa.saa_luns = 8;
		saa.saa_openings = sc->sc_maxcmds;
		saa.saa_pool = &sc->sc_iopool;
		saa.saa_quirks = saa.saa_flags = 0;
		saa.saa_wwpn = saa.saa_wwnn = 0;

		ptbus = (struct scsibus_softc *)config_found(&sc->sc_dev,
		    &saa, scsiprint);

		if (ptbus == NULL || rsc->sc_proctarget == -1)
			continue;

		proclink = scsi_get_link(ptbus, rsc->sc_proctarget, 0);
		if (proclink == NULL)
			continue;

		procdev = proclink->device_softc;
		strlcpy(rsc->sc_procdev, procdev->dv_xname,
		    sizeof(rsc->sc_procdev));
	}

	return (0);

free_mbox:
	ami_freemem(sc, sc->sc_mbox_am);
free_idata:
	ami_freemem(sc, am);

	return (1);
}

int
ami_quartz_init(struct ami_softc *sc)
{
	ami_write(sc, AMI_QIDB, 0);

	return (0);
}

int
ami_quartz_exec(struct ami_softc *sc, struct ami_iocmd *cmd)
{
	if (sc->sc_mbox->acc_busy) {
		AMI_DPRINTF(AMI_D_CMD, ("mbox_busy "));
		return (EBUSY);
	}

	memcpy((struct ami_iocmd *)sc->sc_mbox, cmd, 16);
	bus_dmamap_sync(sc->sc_dmat, AMIMEM_MAP(sc->sc_mbox_am), 0,
	    sizeof(struct ami_iocmd), BUS_DMASYNC_PREWRITE|BUS_DMASYNC_PREREAD);

	sc->sc_mbox->acc_busy = 1;
	sc->sc_mbox->acc_poll = 0;
	sc->sc_mbox->acc_ack = 0;

	ami_write(sc, AMI_QIDB, sc->sc_mbox_pa | htole32(AMI_QIDB_EXEC));

	return (0);
}

int
ami_quartz_done(struct ami_softc *sc, struct ami_iocmd *mbox)
{
	u_int32_t i, n;
	u_int8_t nstat, status;
	u_int8_t completed[AMI_MAXSTATACK];

	if (ami_read(sc, AMI_QODB) != AMI_QODB_READY)
		return (0); /* nothing to do */

	ami_write(sc, AMI_QODB, AMI_QODB_READY);

	/*
	 * The following sequence is not supposed to have a timeout clause
	 * since the firmware has a "guarantee" that all commands will
	 * complete.  The choice is either panic or hoping for a miracle
	 * and that the IOs will complete much later.
	 */
	i = 0;
	while ((nstat = sc->sc_mbox->acc_nstat) == 0xff) {
		bus_dmamap_sync(sc->sc_dmat, AMIMEM_MAP(sc->sc_mbox_am), 0,
		    sizeof(struct ami_iocmd), BUS_DMASYNC_POSTREAD);
		delay(1);
		if (i++ > 1000000)
			return (0); /* nothing to do */
	}
	sc->sc_mbox->acc_nstat = 0xff;
	bus_dmamap_sync(sc->sc_dmat, AMIMEM_MAP(sc->sc_mbox_am), 0,
	    sizeof(struct ami_iocmd), BUS_DMASYNC_POSTWRITE);

	/* wait until fw wrote out all completions */
	i = 0;
	AMI_DPRINTF(AMI_D_CMD, ("aqd %d ", nstat));
	for (n = 0; n < nstat; n++) {
		bus_dmamap_sync(sc->sc_dmat, AMIMEM_MAP(sc->sc_mbox_am), 0,
		    sizeof(struct ami_iocmd), BUS_DMASYNC_PREREAD);
		while ((completed[n] = sc->sc_mbox->acc_cmplidl[n]) == 0xff) {
			delay(1);
			if (i++ > 1000000)
				return (0); /* nothing to do */
		}
		sc->sc_mbox->acc_cmplidl[n] = 0xff;
		bus_dmamap_sync(sc->sc_dmat, AMIMEM_MAP(sc->sc_mbox_am), 0,
		    sizeof(struct ami_iocmd), BUS_DMASYNC_POSTWRITE);
	}

	/* this should never happen, someone screwed up the completion status */
	if ((status = sc->sc_mbox->acc_status) == 0xff)
		panic("%s: status 0xff from the firmware", DEVNAME(sc));

	sc->sc_mbox->acc_status = 0xff;

	/* copy mailbox to temporary one and fixup other changed values */
	bus_dmamap_sync(sc->sc_dmat, AMIMEM_MAP(sc->sc_mbox_am), 0, 16,
	    BUS_DMASYNC_POSTWRITE);
	memcpy(mbox, (struct ami_iocmd *)sc->sc_mbox, 16);
	mbox->acc_nstat = nstat;
	mbox->acc_status = status;
	for (n = 0; n < nstat; n++)
		mbox->acc_cmplidl[n] = completed[n];

	/* ack interrupt */
	ami_write(sc, AMI_QIDB, AMI_QIDB_ACK);

	return (1);	/* ready to complete all IOs in acc_cmplidl */
}

int
ami_quartz_poll(struct ami_softc *sc, struct ami_iocmd *cmd)
{
	/* struct scsi_xfer *xs = ccb->ccb_xs; */
	u_int32_t i;
	u_int8_t status;

	splassert(IPL_BIO);

	if (sc->sc_dis_poll)
		return (-1); /* fail */

	i = 0;
	while (sc->sc_mbox->acc_busy && (i < AMI_MAX_BUSYWAIT)) {
		delay(1);
		i++;
	}
	if (sc->sc_mbox->acc_busy) {
		AMI_DPRINTF(AMI_D_CMD, ("mbox_busy "));
		return (-1);
	}

	memcpy((struct ami_iocmd *)sc->sc_mbox, cmd, 16);
	bus_dmamap_sync(sc->sc_dmat, AMIMEM_MAP(sc->sc_mbox_am), 0, 16,
	    BUS_DMASYNC_PREWRITE|BUS_DMASYNC_PREREAD);

	sc->sc_mbox->acc_id = 0xfe;
	sc->sc_mbox->acc_busy = 1;
	sc->sc_mbox->acc_poll = 0;
	sc->sc_mbox->acc_ack = 0;
	sc->sc_mbox->acc_nstat = 0xff;
	sc->sc_mbox->acc_status = 0xff;

	/* send command to firmware */
	ami_write(sc, AMI_QIDB, sc->sc_mbox_pa | htole32(AMI_QIDB_EXEC));

	i = 0;
	while ((sc->sc_mbox->acc_nstat == 0xff) && (i < AMI_MAX_POLLWAIT)) {
		delay(1);
		i++;
	}
	if (i >= AMI_MAX_POLLWAIT) {
		printf("%s: command not accepted, polling disabled\n",
		    DEVNAME(sc));
		sc->sc_dis_poll = 1;
		return (-1);
	}

	/* poll firmware */
	i = 0;
	while ((sc->sc_mbox->acc_poll != 0x77) && (i < AMI_MAX_POLLWAIT)) {
		delay(1);
		i++;
	}
	if (i >= AMI_MAX_POLLWAIT) {
		printf("%s: firmware didn't reply, polling disabled\n",
		    DEVNAME(sc));
		sc->sc_dis_poll = 1;
		return (-1);
	}

	/* ack */
	ami_write(sc, AMI_QIDB, sc->sc_mbox_pa | htole32(AMI_QIDB_ACK));

	i = 0;
	while((ami_read(sc, AMI_QIDB) & AMI_QIDB_ACK) &&
	    (i < AMI_MAX_POLLWAIT)) {
		delay(1);
		i++;
	}
	if (i >= AMI_MAX_POLLWAIT) {
		printf("%s: firmware didn't ack the ack, polling disabled\n",
		    DEVNAME(sc));
		sc->sc_dis_poll = 1;
		return (-1);
	}

	sc->sc_mbox->acc_poll = 0;
	sc->sc_mbox->acc_ack = 0x77;
	status = sc->sc_mbox->acc_status;
	sc->sc_mbox->acc_nstat = 0xff;
	sc->sc_mbox->acc_status = 0xff;

	for (i = 0; i < AMI_MAXSTATACK; i++)
		sc->sc_mbox->acc_cmplidl[i] = 0xff;

	return (status);
}

int
ami_schwartz_init(struct ami_softc *sc)
{
	u_int32_t a = (u_int32_t)sc->sc_mbox_pa;

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, AMI_SMBADDR, a);
	/* XXX 40bit address ??? */
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, AMI_SMBENA, 0);

	bus_space_write_1(sc->sc_iot, sc->sc_ioh, AMI_SCMD, AMI_SCMD_ACK);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, AMI_SIEM, AMI_SEIM_ENA |
	    bus_space_read_1(sc->sc_iot, sc->sc_ioh, AMI_SIEM));

	return (0);
}

int
ami_schwartz_exec(struct ami_softc *sc, struct ami_iocmd *cmd)
{
	if (bus_space_read_1(sc->sc_iot, sc->sc_ioh, AMI_SMBSTAT) &
	    AMI_SMBST_BUSY) {
		AMI_DPRINTF(AMI_D_CMD, ("mbox_busy "));
		return (EBUSY);
	}

	memcpy((struct ami_iocmd *)sc->sc_mbox, cmd, 16);
	sc->sc_mbox->acc_busy = 1;
	sc->sc_mbox->acc_poll = 0;
	sc->sc_mbox->acc_ack = 0;

	bus_space_write_1(sc->sc_iot, sc->sc_ioh, AMI_SCMD, AMI_SCMD_EXEC);
	return (0);
}

int
ami_schwartz_done(struct ami_softc *sc, struct ami_iocmd *mbox)
{
	u_int8_t stat;

#if 0
	/* do not scramble the busy mailbox */
	if (sc->sc_mbox->acc_busy)
		return (0);
#endif
	if (bus_space_read_1(sc->sc_iot, sc->sc_ioh, AMI_SMBSTAT) &
	    AMI_SMBST_BUSY)
		return (0);

	stat = bus_space_read_1(sc->sc_iot, sc->sc_ioh, AMI_ISTAT);
	if (stat & AMI_ISTAT_PEND) {
		bus_space_write_1(sc->sc_iot, sc->sc_ioh, AMI_ISTAT, stat);

		*mbox = *sc->sc_mbox;
		AMI_DPRINTF(AMI_D_CMD, ("asd %d ", mbox->acc_nstat));

		bus_space_write_1(sc->sc_iot, sc->sc_ioh, AMI_SCMD,
		    AMI_SCMD_ACK);

		return (1);
	}

	return (0);
}

int
ami_schwartz_poll(struct ami_softc *sc, struct ami_iocmd *mbox)
{
	u_int8_t status;
	u_int32_t i;
	int rv;

	splassert(IPL_BIO);

	if (sc->sc_dis_poll)
		return (-1); /* fail */

	for (i = 0; i < AMI_MAX_POLLWAIT; i++) {
		if (!(bus_space_read_1(sc->sc_iot, sc->sc_ioh, AMI_SMBSTAT) &
		    AMI_SMBST_BUSY))
			break;
		delay(1);
	}
	if (i >= AMI_MAX_POLLWAIT) {
		AMI_DPRINTF(AMI_D_CMD, ("mbox_busy "));
		return (-1);
	}

	memcpy((struct ami_iocmd *)sc->sc_mbox, mbox, 16);
	bus_dmamap_sync(sc->sc_dmat, AMIMEM_MAP(sc->sc_mbox_am), 0, 16,
	    BUS_DMASYNC_PREWRITE|BUS_DMASYNC_PREREAD);

	sc->sc_mbox->acc_busy = 1;
	sc->sc_mbox->acc_poll = 0;
	sc->sc_mbox->acc_ack = 0;
	/* send command to firmware */
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, AMI_SCMD, AMI_SCMD_EXEC);

	/* wait until no longer busy */
	for (i = 0; i < AMI_MAX_POLLWAIT; i++) {
		if (!(bus_space_read_1(sc->sc_iot, sc->sc_ioh, AMI_SMBSTAT) &
		    AMI_SMBST_BUSY))
			break;
		delay(1);
	}
	if (i >= AMI_MAX_POLLWAIT) {
		printf("%s: command not accepted, polling disabled\n",
		    DEVNAME(sc));
		sc->sc_dis_poll = 1;
		return (-1);
	}

	/* wait for interrupt bit */
	for (i = 0; i < AMI_MAX_POLLWAIT; i++) {
		status = bus_space_read_1(sc->sc_iot, sc->sc_ioh, AMI_ISTAT);
		if (status & AMI_ISTAT_PEND)
			break;
		delay(1);
	}
	if (i >= AMI_MAX_POLLWAIT) {
		printf("%s: interrupt didn't arrive, polling disabled\n",
		    DEVNAME(sc));
		sc->sc_dis_poll = 1;
		return (-1);
	}

	/* write ststus back to firmware */
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, AMI_ISTAT, status);

	/* copy mailbox and status back */
	bus_dmamap_sync(sc->sc_dmat, AMIMEM_MAP(sc->sc_mbox_am), 0,
	    sizeof(struct ami_iocmd), BUS_DMASYNC_PREREAD);
	*mbox = *sc->sc_mbox;
	rv = sc->sc_mbox->acc_status;

	/* ack interrupt */
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, AMI_SCMD, AMI_SCMD_ACK);

	return (rv);
}

void
ami_start_xs(struct ami_softc *sc, struct ami_ccb *ccb, struct scsi_xfer *xs)
{
	if (xs->flags & SCSI_POLL)
		ami_complete(sc, ccb, xs->timeout);
	else
		ami_start(sc, ccb);
}

void
ami_start(struct ami_softc *sc, struct ami_ccb *ccb)
{
	mtx_enter(&sc->sc_cmd_mtx);
	ccb->ccb_state = AMI_CCB_PREQUEUED;
	TAILQ_INSERT_TAIL(&sc->sc_ccb_preq, ccb, ccb_link);
	mtx_leave(&sc->sc_cmd_mtx);

	ami_runqueue(sc);
}

void
ami_runqueue_tick(void *arg)
{
	ami_runqueue(arg);
}

void
ami_runqueue(struct ami_softc *sc)
{
	struct ami_ccb *ccb;
	int add = 0;

	mtx_enter(&sc->sc_cmd_mtx);
	if (!sc->sc_drainio) {
		while ((ccb = TAILQ_FIRST(&sc->sc_ccb_preq)) != NULL) {
			if (sc->sc_exec(sc, &ccb->ccb_cmd) != 0) {
				add = 1;
				break;
			}

			TAILQ_REMOVE(&sc->sc_ccb_preq, ccb, ccb_link);
			ccb->ccb_state = AMI_CCB_QUEUED;
			TAILQ_INSERT_TAIL(&sc->sc_ccb_runq, ccb, ccb_link);
		}
	}
	mtx_leave(&sc->sc_cmd_mtx);

	if (add)
		timeout_add(&sc->sc_run_tmo, 1);
}

int
ami_poll(struct ami_softc *sc, struct ami_ccb *ccb)
{
	int error;

	mtx_enter(&sc->sc_cmd_mtx);
	error = sc->sc_poll(sc, &ccb->ccb_cmd);
	if (error == -1)
		ccb->ccb_flags |= AMI_CCB_F_ERR;
	mtx_leave(&sc->sc_cmd_mtx);

	ccb->ccb_done(sc, ccb);

	return (error);
}

void
ami_complete(struct ami_softc *sc, struct ami_ccb *ccb, int timeout)
{
	void (*done)(struct ami_softc *, struct ami_ccb *);
	int ready;
	int i = 0;
	int s;

	done = ccb->ccb_done;
	ccb->ccb_done = ami_done_dummy;

	/*
	 * since exec will return if the mbox is busy we have to busy wait
	 * ourselves. once its in, jam it into the runq.
	 */
	mtx_enter(&sc->sc_cmd_mtx);
	while (i < AMI_MAX_BUSYWAIT) {
		if (sc->sc_exec(sc, &ccb->ccb_cmd) == 0) {
			ccb->ccb_state = AMI_CCB_QUEUED;
			TAILQ_INSERT_TAIL(&sc->sc_ccb_runq, ccb, ccb_link);
			break;
		}
		DELAY(1000);
		i++;
	}
	ready = (ccb->ccb_state == AMI_CCB_QUEUED);
	mtx_leave(&sc->sc_cmd_mtx);

	if (!ready) {
		ccb->ccb_flags |= AMI_CCB_F_ERR;
		ccb->ccb_state = AMI_CCB_READY;
		goto done;
	}

	/*
	 * Override timeout for PERC3.  The first command triggers a chip
	 * reset on the QL12160 chip which causes the firmware to reload.
	 * 30000 is slightly less than double of how long it takes for the
	 * firmware to be up again.  After the first two commands the
	 * timeouts are as expected.
	 */
	timeout = MAX(30000, timeout); /* timeout */

	while (ccb->ccb_state == AMI_CCB_QUEUED) {
		s = splbio(); /* interrupt handlers are called at their IPL */
		ready = ami_intr(sc);
		splx(s);

		if (ready == 0) {
			if (timeout-- == 0) {
				/* XXX */
				printf("%s: timeout\n", DEVNAME(sc));
				return;
			}

			delay(1000);
			continue;
		}
	}

done:
	done(sc, ccb);
}

void
ami_done_pt(struct ami_softc *sc, struct ami_ccb *ccb)
{
	struct scsi_xfer *xs = ccb->ccb_xs;
	struct scsi_link *link = xs->sc_link;
	struct ami_rawsoftc *rsc = link->bus->sb_adapter_softc;
	u_int8_t target = link->target, type;

	bus_dmamap_sync(sc->sc_dmat, AMIMEM_MAP(sc->sc_ccbmem_am),
	    ccb->ccb_offset, sizeof(struct ami_ccbmem),
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	if (xs->data != NULL) {
		bus_dmamap_sync(sc->sc_dmat, ccb->ccb_dmamap, 0,
		    ccb->ccb_dmamap->dm_mapsize,
		    (xs->flags & SCSI_DATA_IN) ?
		    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);

		bus_dmamap_unload(sc->sc_dmat, ccb->ccb_dmamap);
	}

	xs->resid = 0;

	if (ccb->ccb_flags & AMI_CCB_F_ERR)
		xs->error = XS_DRIVER_STUFFUP;
 	else if (ccb->ccb_status != 0x00)
		xs->error = XS_DRIVER_STUFFUP;
	else if (xs->flags & SCSI_POLL && xs->cmd.opcode == INQUIRY) {
		type = ((struct scsi_inquiry_data *)xs->data)->device &
		    SID_TYPE;
		if (!(type == T_PROCESSOR || type == T_ENCLOSURE))
			xs->error = XS_DRIVER_STUFFUP;
		else
			rsc->sc_proctarget = target;
	}

	scsi_done(xs);
}

void
ami_done_xs(struct ami_softc *sc, struct ami_ccb *ccb)
{
	struct scsi_xfer *xs = ccb->ccb_xs;

	if (xs->data != NULL) {
		bus_dmamap_sync(sc->sc_dmat, ccb->ccb_dmamap, 0,
		    ccb->ccb_dmamap->dm_mapsize,
		    (xs->flags & SCSI_DATA_IN) ?
		    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);

		bus_dmamap_sync(sc->sc_dmat, AMIMEM_MAP(sc->sc_ccbmem_am),
		    ccb->ccb_offset, sizeof(struct ami_ccbmem),
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

		bus_dmamap_unload(sc->sc_dmat, ccb->ccb_dmamap);
	}

	xs->resid = 0;

	if (ccb->ccb_flags & AMI_CCB_F_ERR)
		xs->error = XS_DRIVER_STUFFUP;

	scsi_done(xs);
}

void
ami_done_flush(struct ami_softc *sc, struct ami_ccb *ccb)
{
	struct scsi_xfer *xs = ccb->ccb_xs;
	struct ami_iocmd *cmd = &ccb->ccb_cmd;

	if (ccb->ccb_flags & AMI_CCB_F_ERR) {
		xs->error = XS_DRIVER_STUFFUP;
		xs->resid = 0;

		scsi_done(xs);
		return;
	}

	/* reuse the ccb for the sysflush command */
	ccb->ccb_done = ami_done_sysflush;
	cmd->acc_cmd = AMI_SYSFLUSH;

	ami_start_xs(sc, ccb, xs);
}

void
ami_done_sysflush(struct ami_softc *sc, struct ami_ccb *ccb)
{
	struct scsi_xfer *xs = ccb->ccb_xs;

	xs->resid = 0;
	if (ccb->ccb_flags & AMI_CCB_F_ERR)
		xs->error = XS_DRIVER_STUFFUP;

	scsi_done(xs);
}

void
ami_done_dummy(struct ami_softc *sc, struct ami_ccb *ccb)
{
}

void
ami_done_ioctl(struct ami_softc *sc, struct ami_ccb *ccb)
{
	wakeup(ccb);
}

void
ami_done_init(struct ami_softc *sc, struct ami_ccb *ccb)
{
	/* the ccb is going to be reused, so do nothing with it */
}

void
ami_scsi_raw_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct ami_rawsoftc *rsc = link->bus->sb_adapter_softc;
	struct ami_softc *sc = rsc->sc_softc;
	u_int8_t channel = rsc->sc_channel, target = link->target;
	struct ami_ccb *ccb;

	AMI_DPRINTF(AMI_D_CMD, ("ami_scsi_raw_cmd "));

	if (xs->cmdlen > AMI_MAX_CDB) {
		AMI_DPRINTF(AMI_D_CMD, ("CDB too big %p ", xs));
		bzero(&xs->sense, sizeof(xs->sense));
		xs->sense.error_code = SSD_ERRCODE_VALID | SSD_ERRCODE_CURRENT;
		xs->sense.flags = SKEY_ILLEGAL_REQUEST;
		xs->sense.add_sense_code = 0x20; /* illcmd, 0x24 illfield */
		xs->error = XS_SENSE;
		scsi_done(xs);
		return;
	}

	xs->error = XS_NOERROR;

	ccb = xs->io;

	memset(ccb->ccb_pt, 0, sizeof(struct ami_passthrough));

	ccb->ccb_xs = xs;
	ccb->ccb_done = ami_done_pt;

	ccb->ccb_cmd.acc_cmd = AMI_PASSTHRU;
	ccb->ccb_cmd.acc_passthru.apt_data = ccb->ccb_ptpa;

	ccb->ccb_pt->apt_param = AMI_PTPARAM(AMI_TIMEOUT_6,1,0);
	ccb->ccb_pt->apt_channel = channel;
	ccb->ccb_pt->apt_target = target;
	bcopy(&xs->cmd, ccb->ccb_pt->apt_cdb, AMI_MAX_CDB);
	ccb->ccb_pt->apt_ncdb = xs->cmdlen;
	ccb->ccb_pt->apt_nsense = AMI_MAX_SENSE;
	ccb->ccb_pt->apt_datalen = xs->datalen;
	ccb->ccb_pt->apt_data = 0;

	if (ami_load_ptmem(sc, ccb, xs->data, xs->datalen,
	    xs->flags & SCSI_DATA_IN, xs->flags & SCSI_NOSLEEP) != 0) {
		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		return;
	}

	ami_start_xs(sc, ccb, xs);
}

int
ami_load_ptmem(struct ami_softc *sc, struct ami_ccb *ccb, void *data,
    size_t len, int read, int nowait)
{
	bus_dmamap_t dmap = ccb->ccb_dmamap;
	bus_dma_segment_t *sgd;
	int error, i;

	if (data != NULL) {
		error = bus_dmamap_load(sc->sc_dmat, dmap, data, len, NULL,
		    nowait ? BUS_DMA_NOWAIT : BUS_DMA_WAITOK);
		if (error) {
			if (error == EFBIG)
				printf("more than %d dma segs\n",
				    AMI_MAXOFFSETS);
			else
				printf("error %d loading dma map\n", error);

			return (1);
		}

		sgd = dmap->dm_segs;
		if (dmap->dm_nsegs > 1) {
			struct ami_sgent *sgl = ccb->ccb_sglist;

			ccb->ccb_pt->apt_nsge = dmap->dm_nsegs;
			ccb->ccb_pt->apt_data = ccb->ccb_sglistpa;

			for (i = 0; i < dmap->dm_nsegs; i++) {
				sgl[i].asg_addr = htole32(sgd[i].ds_addr);
				sgl[i].asg_len = htole32(sgd[i].ds_len);
			}
		} else {
			ccb->ccb_pt->apt_nsge = 0;
			ccb->ccb_pt->apt_data = htole32(sgd->ds_addr);
		}

		bus_dmamap_sync(sc->sc_dmat, dmap, 0, dmap->dm_mapsize,
		    read ? BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);
	}

	bus_dmamap_sync(sc->sc_dmat, AMIMEM_MAP(sc->sc_ccbmem_am),
	    ccb->ccb_offset, sizeof(struct ami_ccbmem),
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

void
ami_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct ami_softc *sc = link->bus->sb_adapter_softc;
	struct device *dev = link->device_softc;
	struct ami_ccb *ccb;
	struct ami_iocmd *cmd;
	struct scsi_inquiry_data inq;
	struct scsi_sense_data sd;
	struct scsi_read_cap_data rcd;
	u_int8_t target = link->target;
	u_int32_t blockno, blockcnt;
	struct scsi_rw *rw;
	struct scsi_rw_10 *rw10;
	bus_dma_segment_t *sgd;
	int error;
	int i;

	AMI_DPRINTF(AMI_D_CMD, ("ami_scsi_cmd "));

	if (target >= sc->sc_nunits || !sc->sc_hdr[target].hd_present ||
	    link->lun != 0) {
		AMI_DPRINTF(AMI_D_CMD, ("no target %d ", target));
		/* XXX should be XS_SENSE and sense filled out */
		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		return;
	}

	xs->error = XS_NOERROR;

	switch (xs->cmd.opcode) {
	case READ_COMMAND:
	case READ_10:
	case WRITE_COMMAND:
	case WRITE_10:
		/* deal with io outside the switch */
		break;

	case SYNCHRONIZE_CACHE:
		ccb = xs->io;

		ccb->ccb_xs = xs;
		ccb->ccb_done = ami_done_flush;
		if (xs->timeout < 30000)
			xs->timeout = 30000;	/* at least 30sec */

		cmd = &ccb->ccb_cmd;
		cmd->acc_cmd = AMI_FLUSH;

		ami_start_xs(sc, ccb, xs);
		return;

	case TEST_UNIT_READY:
		/* save off sd? after autoconf */
		if (!cold)	/* XXX bogus */
			strlcpy(sc->sc_hdr[target].dev, dev->dv_xname,
			    sizeof(sc->sc_hdr[target].dev));
	case START_STOP:
#if 0
	case VERIFY:
#endif
	case PREVENT_ALLOW:
		AMI_DPRINTF(AMI_D_CMD, ("opc %d tgt %d ", xs->cmd.opcode,
		    target));
		xs->error = XS_NOERROR;
		scsi_done(xs);
		return;

	case REQUEST_SENSE:
		AMI_DPRINTF(AMI_D_CMD, ("REQUEST SENSE tgt %d ", target));
		bzero(&sd, sizeof(sd));
		sd.error_code = SSD_ERRCODE_CURRENT;
		sd.segment = 0;
		sd.flags = SKEY_NO_SENSE;
		*(u_int32_t*)sd.info = htole32(0);
		sd.extra_len = 0;
		scsi_copy_internal_data(xs, &sd, sizeof(sd));

		xs->error = XS_NOERROR;
		scsi_done(xs);
		return;

	case INQUIRY:
		if (ISSET(((struct scsi_inquiry *)&xs->cmd)->flags, SI_EVPD)) {
			xs->error = XS_DRIVER_STUFFUP;
			scsi_done(xs);
			return;
		}

		AMI_DPRINTF(AMI_D_CMD, ("INQUIRY tgt %d ", target));
		bzero(&inq, sizeof(inq));
		inq.device = T_DIRECT;
		inq.dev_qual2 = 0;
		inq.version = SCSI_REV_2;
		inq.response_format = SID_SCSI2_RESPONSE;
		inq.additional_length = SID_SCSI2_ALEN;
		inq.flags |= SID_CmdQue;
		strlcpy(inq.vendor, "AMI    ", sizeof(inq.vendor));
		snprintf(inq.product, sizeof(inq.product),
		    "Host drive  #%02d", target);
		strlcpy(inq.revision, "   ", sizeof(inq.revision));
		scsi_copy_internal_data(xs, &inq, sizeof(inq));

		xs->error = XS_NOERROR;
		scsi_done(xs);
		return;

	case READ_CAPACITY:
		AMI_DPRINTF(AMI_D_CMD, ("READ CAPACITY tgt %d ", target));
		bzero(&rcd, sizeof(rcd));
		_lto4b(sc->sc_hdr[target].hd_size - 1, rcd.addr);
		_lto4b(AMI_SECTOR_SIZE, rcd.length);
		scsi_copy_internal_data(xs, &rcd, sizeof(rcd));

		xs->error = XS_NOERROR;
		scsi_done(xs);
		return;

	default:
		AMI_DPRINTF(AMI_D_CMD, ("unsupported scsi command %#x tgt %d ",
		    xs->cmd.opcode, target));

		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		return;
	}

	/* A read or write operation. */
	if (xs->cmdlen == 6) {
		rw = (struct scsi_rw *)&xs->cmd;
		blockno = _3btol(rw->addr) & (SRW_TOPADDR << 16 | 0xffff);
		blockcnt = rw->length ? rw->length : 0x100;
	} else {
		rw10 = (struct scsi_rw_10 *)&xs->cmd;
		blockno = _4btol(rw10->addr);
		blockcnt = _2btol(rw10->length);
	}

	if (blockno >= sc->sc_hdr[target].hd_size ||
	    blockno + blockcnt > sc->sc_hdr[target].hd_size) {
		printf("%s: out of bounds %u-%u >= %u\n", DEVNAME(sc),
		    blockno, blockcnt, sc->sc_hdr[target].hd_size);
		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		return;
	}

	ccb = xs->io;

	ccb->ccb_xs = xs;
	ccb->ccb_done = ami_done_xs;

	cmd = &ccb->ccb_cmd;
	cmd->acc_cmd = (xs->flags & SCSI_DATA_IN) ? AMI_READ : AMI_WRITE;
	cmd->acc_mbox.amb_nsect = htole16(blockcnt);
	cmd->acc_mbox.amb_lba = htole32(blockno);
	cmd->acc_mbox.amb_ldn = target;

	error = bus_dmamap_load(sc->sc_dmat, ccb->ccb_dmamap,
	    xs->data, xs->datalen, NULL,
	    (xs->flags & SCSI_NOSLEEP) ? BUS_DMA_NOWAIT : BUS_DMA_WAITOK);
	if (error) {
		if (error == EFBIG)
			printf("more than %d dma segs\n", AMI_MAXOFFSETS);
		else
			printf("error %d loading dma map\n", error);

		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		return;
	}

	sgd = ccb->ccb_dmamap->dm_segs;
	if (ccb->ccb_dmamap->dm_nsegs > 1) {
		struct ami_sgent *sgl = ccb->ccb_sglist;

		cmd->acc_mbox.amb_nsge = ccb->ccb_dmamap->dm_nsegs;
		cmd->acc_mbox.amb_data = ccb->ccb_sglistpa;

		for (i = 0; i < ccb->ccb_dmamap->dm_nsegs; i++) {
			sgl[i].asg_addr = htole32(sgd[i].ds_addr);
			sgl[i].asg_len = htole32(sgd[i].ds_len);
		}
	} else {
		cmd->acc_mbox.amb_nsge = 0;
		cmd->acc_mbox.amb_data = htole32(sgd->ds_addr);
	}

	bus_dmamap_sync(sc->sc_dmat, AMIMEM_MAP(sc->sc_ccbmem_am),
	    ccb->ccb_offset, sizeof(struct ami_ccbmem),
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	bus_dmamap_sync(sc->sc_dmat, ccb->ccb_dmamap, 0,
	    ccb->ccb_dmamap->dm_mapsize, (xs->flags & SCSI_DATA_IN) ?
	    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);

	ami_start_xs(sc, ccb, xs);
}

int
ami_intr(void *v)
{
	struct ami_iocmd mbox;
	struct ami_softc *sc = v;
	struct ami_ccb *ccb;
	int i, rv = 0, ready;

	mtx_enter(&sc->sc_cmd_mtx);
	while (!TAILQ_EMPTY(&sc->sc_ccb_runq) && sc->sc_done(sc, &mbox)) {
		AMI_DPRINTF(AMI_D_CMD, ("got#%d ", mbox.acc_nstat));
		for (i = 0; i < mbox.acc_nstat; i++ ) {
			ready = mbox.acc_cmplidl[i] - 1;
			AMI_DPRINTF(AMI_D_CMD, ("ready=%d ", ready));

			ccb = &sc->sc_ccbs[ready];
			ccb->ccb_status = mbox.acc_status;
			ccb->ccb_state = AMI_CCB_READY;
			TAILQ_REMOVE(&ccb->ccb_sc->sc_ccb_runq, ccb, ccb_link);

			mtx_leave(&sc->sc_cmd_mtx);
			ccb->ccb_done(sc, ccb);
			mtx_enter(&sc->sc_cmd_mtx);

			rv = 1;
		}
	}
	ready = (sc->sc_drainio && TAILQ_EMPTY(&sc->sc_ccb_runq));
	mtx_leave(&sc->sc_cmd_mtx);

	if (ready)
		wakeup(sc);
	else if (rv)
		ami_runqueue(sc);

	AMI_DPRINTF(AMI_D_INTR, ("exit "));
	return (rv);
}

int
ami_scsi_ioctl(struct scsi_link *link, u_long cmd, caddr_t addr, int flag)
{
	struct ami_softc *sc = link->bus->sb_adapter_softc;
	/* struct device *dev = (struct device *)link->device_softc; */
	/* u_int8_t target = link->target; */

	if (sc->sc_ioctl)
		return (sc->sc_ioctl(&sc->sc_dev, cmd, addr));
	else
		return (ENOTTY);
}

#if NBIO > 0
int
ami_ioctl(struct device *dev, u_long cmd, caddr_t addr)
{
	struct ami_softc *sc = (struct ami_softc *)dev;
	int error = 0;

	AMI_DPRINTF(AMI_D_IOCTL, ("%s: ioctl ", DEVNAME(sc)));

	if (sc->sc_flags & AMI_BROKEN)
		return (ENODEV); /* can't do this to broken device for now */

	switch (cmd) {
	case BIOCINQ:
		AMI_DPRINTF(AMI_D_IOCTL, ("inq "));
		error = ami_ioctl_inq(sc, (struct bioc_inq *)addr);
		break;

	case BIOCVOL:
		AMI_DPRINTF(AMI_D_IOCTL, ("vol "));
		error = ami_ioctl_vol(sc, (struct bioc_vol *)addr);
		break;

	case BIOCDISK:
		AMI_DPRINTF(AMI_D_IOCTL, ("disk "));
		error = ami_ioctl_disk(sc, (struct bioc_disk *)addr);
		break;

	case BIOCALARM:
		AMI_DPRINTF(AMI_D_IOCTL, ("alarm "));
		error = ami_ioctl_alarm(sc, (struct bioc_alarm *)addr);
		break;

	case BIOCSETSTATE:
		AMI_DPRINTF(AMI_D_IOCTL, ("setstate "));
		error = ami_ioctl_setstate(sc, (struct bioc_setstate *)addr);
		break;

	default:
		AMI_DPRINTF(AMI_D_IOCTL, (" invalid ioctl\n"));
		error = ENOTTY;
	}

	return (error);
}

int
ami_drv_pt(struct ami_softc *sc, u_int8_t ch, u_int8_t tg, u_int8_t *cmd,
    int clen, int blen, void *buf)
{
	struct ami_ccb *ccb;
	struct ami_passthrough *pt;
	int error = 0;

	rw_enter_write(&sc->sc_lock);

	ccb = scsi_io_get(&sc->sc_iopool, 0);
	if (ccb == NULL) {
		error = ENOMEM;
		goto err;
	}

	ccb->ccb_done = ami_done_ioctl;

	ccb->ccb_cmd.acc_cmd = AMI_PASSTHRU;
	ccb->ccb_cmd.acc_passthru.apt_data = ccb->ccb_ptpa;

	pt = ccb->ccb_pt;
	memset(pt, 0, sizeof *pt);
	pt->apt_channel = ch;
	pt->apt_target = tg;
	pt->apt_ncdb = clen;
	pt->apt_nsense = sizeof(struct scsi_sense_data);
	pt->apt_datalen = blen;
	pt->apt_data = 0;

	bcopy(cmd, pt->apt_cdb, clen);

	if (ami_load_ptmem(sc, ccb, buf, blen, 1, 0) != 0) {
		error = ENOMEM;
		goto ptmemerr;
	}

	ami_start(sc, ccb);

	while (ccb->ccb_state != AMI_CCB_READY)
		tsleep_nsec(ccb, PRIBIO, "ami_drv_pt", INFSLP);

	bus_dmamap_sync(sc->sc_dmat, ccb->ccb_dmamap, 0,
	    ccb->ccb_dmamap->dm_mapsize, BUS_DMASYNC_POSTREAD);
	bus_dmamap_sync(sc->sc_dmat, AMIMEM_MAP(sc->sc_ccbmem_am),
	    ccb->ccb_offset, sizeof(struct ami_ccbmem),
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->sc_dmat, ccb->ccb_dmamap);

	if (ccb->ccb_flags & AMI_CCB_F_ERR)
		error = EIO;
	else if (pt->apt_scsistat != 0x00)
		error = EIO;

ptmemerr:
	scsi_io_put(&sc->sc_iopool, ccb);

err:
	rw_exit_write(&sc->sc_lock);
	return (error);
}

int
ami_drv_inq(struct ami_softc *sc, u_int8_t ch, u_int8_t tg, u_int8_t page,
    void *inqbuf)
{
	struct scsi_inquiry_data *inq = inqbuf;
	u_int8_t cdb[6];
	int error = 0;

	bzero(&cdb, sizeof cdb);

	cdb[0] = INQUIRY;
	cdb[1] = 0;
	cdb[2] = 0;
	cdb[3] = 0;
	cdb[4] = sizeof(struct scsi_inquiry_data);
	cdb[5] = 0;
	if (page != 0) {
		cdb[1] = SI_EVPD;
		cdb[2] = page;
	}

	error = ami_drv_pt(sc, ch, tg, cdb, 6, sizeof *inq, inqbuf);
	if (error)
		return (error);

	if ((inq->device & SID_TYPE) != T_DIRECT)
		error = EINVAL;

	return (error);
}

int
ami_drv_readcap(struct ami_softc *sc, u_int8_t ch, u_int8_t tg, daddr_t *sz)
{
	struct scsi_read_cap_data *rcd = NULL;
	struct scsi_read_cap_data_16 *rcd16 = NULL;
	u_int8_t cdb[16];
	u_int32_t blksz;
	daddr_t noblk;
	int error = 0;

	bzero(&cdb, sizeof cdb);
	cdb[0] = READ_CAPACITY;
	rcd = dma_alloc(sizeof(*rcd), PR_WAITOK);

	error = ami_drv_pt(sc, ch, tg, cdb, 10, sizeof(*rcd), rcd);
	if (error)
		goto fail;

	noblk = _4btol(rcd->addr);
	if (noblk == 0xffffffffllu) {
		/* huge disk */
		bzero(&cdb, sizeof cdb);
		cdb[0] = READ_CAPACITY_16;
		rcd16 = dma_alloc(sizeof(*rcd16), PR_WAITOK);

		error = ami_drv_pt(sc, ch, tg, cdb, 16, sizeof(*rcd16), rcd16);
		if (error)
			goto fail;

		noblk = _8btol(rcd16->addr);
		blksz = _4btol(rcd16->length);
	} else
		blksz = _4btol(rcd->length);

	if (blksz == 0)
		blksz = 512;
	*sz = noblk * blksz;

fail:
	if (rcd16)
		dma_free(rcd16, sizeof(*rcd16));
	dma_free(rcd, sizeof(*rcd));
	return (error);
}

int
ami_mgmt(struct ami_softc *sc, u_int8_t opcode, u_int8_t par1, u_int8_t par2,
    u_int8_t par3, size_t size, void *buffer)
{
	struct ami_ccb *ccb;
	struct ami_iocmd *cmd;
	struct ami_mem *am = NULL;
	char *idata = NULL;
	int error = 0;

	rw_enter_write(&sc->sc_lock);

	if (opcode != AMI_CHSTATE) {
		ccb = scsi_io_get(&sc->sc_iopool, 0);
		if (ccb == NULL) {
			error = ENOMEM;
			goto err;
		}
		ccb->ccb_done = ami_done_ioctl;
	} else
		ccb = sc->sc_mgmtccb;

	if (size) {
		if ((am = ami_allocmem(sc, size)) == NULL) {
			error = ENOMEM;
			goto memerr;
		}
		idata = AMIMEM_KVA(am);
	}

	cmd = &ccb->ccb_cmd;
	cmd->acc_cmd = opcode;

	/*
	 * some commands require data to be written to idata before sending
	 * command to fw
	 */
	switch (opcode) {
	case AMI_SPEAKER:
		*idata = par1;
		break;
	default:
		cmd->acc_io.aio_channel = par1;
		cmd->acc_io.aio_param = par2;
		cmd->acc_io.aio_pad[0] = par3;
		break;
	}

	cmd->acc_io.aio_data = am ? htole32(AMIMEM_DVA(am)) : 0;

	if (opcode != AMI_CHSTATE) {
		ami_start(sc, ccb);
		mtx_enter(&sc->sc_cmd_mtx);
		while (ccb->ccb_state != AMI_CCB_READY)
			msleep_nsec(ccb, &sc->sc_cmd_mtx, PRIBIO, "ami_mgmt",
			    INFSLP);
		mtx_leave(&sc->sc_cmd_mtx);
	} else {
		/* change state must be run with id 0xfe and MUST be polled */
		mtx_enter(&sc->sc_cmd_mtx);
		sc->sc_drainio = 1;
		while (!TAILQ_EMPTY(&sc->sc_ccb_runq)) {
			if (msleep_nsec(sc, &sc->sc_cmd_mtx, PRIBIO,
			    "amimgmt", SEC_TO_NSEC(60)) == EWOULDBLOCK) {
				printf("%s: drain io timeout\n", DEVNAME(sc));
				ccb->ccb_flags |= AMI_CCB_F_ERR;
				goto restartio;
			}
		}

		error = sc->sc_poll(sc, &ccb->ccb_cmd);
		if (error == -1)
			ccb->ccb_flags |= AMI_CCB_F_ERR;

restartio:
		/* restart io */
		sc->sc_drainio = 0;
		mtx_leave(&sc->sc_cmd_mtx);
		ami_runqueue(sc);
	}

	if (ccb->ccb_flags & AMI_CCB_F_ERR)
		error = EIO;
	else if (buffer && size)
		memcpy(buffer, idata, size);

	if (am)
		ami_freemem(sc, am);
memerr:
	if (opcode != AMI_CHSTATE) {
		scsi_io_put(&sc->sc_iopool, ccb);
	} else {
		ccb->ccb_flags = 0;
		ccb->ccb_state = AMI_CCB_FREE;
	}

err:
	rw_exit_write(&sc->sc_lock);
	return (error);
}

int
ami_ioctl_inq(struct ami_softc *sc, struct bioc_inq *bi)
{
	struct ami_big_diskarray *p; /* struct too large for stack */
	struct scsi_inquiry_data *inqbuf;
	struct ami_fc_einquiry einq;
	int ch, tg;
	int i, s, t, off;
	int error = 0, changes = 0;

	if ((error = ami_mgmt(sc, AMI_FCOP, AMI_FC_EINQ3,
	    AMI_FC_EINQ3_SOLICITED_FULL, 0, sizeof einq, &einq)))
		return (EINVAL);

	inqbuf = dma_alloc(sizeof(*inqbuf), PR_WAITOK);

	if (einq.ain_drvinscnt == sc->sc_drvinscnt) {
		/* poke existing known drives to make sure they aren't gone */
		for(i = 0; i < sc->sc_channels * 16; i++) {
			if (sc->sc_plist[i] == 0)
				continue;

			ch = (i & 0xf0) >> 4;
			tg = i & 0x0f;
			if (ami_drv_inq(sc, ch, tg, 0, inqbuf)) {
				/* drive is gone, force rescan */
				changes = 1;
				break;
			}
		}
		if (changes == 0) {
			bcopy(&sc->sc_bi, bi, sizeof *bi);
			goto done;
		}
	}

	sc->sc_drvinscnt = einq.ain_drvinscnt;

	p = malloc(sizeof *p, M_DEVBUF, M_NOWAIT);
	if (!p) {
		error = ENOMEM;
		goto done;
	}

	if ((error = ami_mgmt(sc, AMI_FCOP, AMI_FC_RDCONF, 0, 0, sizeof *p,
	    p))) {
		error = EINVAL;
		goto bail;
	}

	bzero(sc->sc_plist, sizeof sc->sc_plist);

	bi->bi_novol = p->ada_nld;
	bi->bi_nodisk = 0;
	strlcpy(bi->bi_dev, DEVNAME(sc), sizeof(bi->bi_dev));

	/* count used disks, including failed ones */
	for (i = 0; i < p->ada_nld; i++)
		for (s = 0; s < p->ald[i].adl_spandepth; s++)
			for (t = 0; t < p->ald[i].adl_nstripes; t++) {
				off = p->ald[i].asp[s].adv[t].add_channel *
				    AMI_MAX_TARGET +
				    p->ald[i].asp[s].adv[t].add_target;

				/* account for multi raid vol on same disk */
				if (!sc->sc_plist[off]) {
					sc->sc_plist[off] = 1;
					bi->bi_nodisk++;
				}
			}

	/* count unused disks */
	for(i = 0; i < sc->sc_channels * 16; i++) {
	    	if (sc->sc_plist[i])
			continue; /* skip claimed drives */

		/*
		 * hack to invalidate device type, needed for initiator id
		 * on an unconnected channel.
		 * XXX find out if we can determine this differently
		 */
		memset(inqbuf, 0xff, sizeof(*inqbuf));

		ch = (i & 0xf0) >> 4;
		tg = i & 0x0f;
		if (!ami_drv_inq(sc, ch, tg, 0, inqbuf)) {
			if ((inqbuf->device & SID_TYPE) != T_DIRECT)
				continue;
			bi->bi_novol++;
			bi->bi_nodisk++;
			sc->sc_plist[i] = 2;
		} else
			sc->sc_plist[i] = 0;
	}

	bcopy(bi, &sc->sc_bi, sizeof sc->sc_bi);
	error = 0;
bail:
	free(p, M_DEVBUF, sizeof *p);
done:
	dma_free(inqbuf, sizeof(*inqbuf));
	return (error);
}

int
ami_vol(struct ami_softc *sc, struct bioc_vol *bv, struct ami_big_diskarray *p)
{
	int i, ld = p->ada_nld, error = EINVAL;

	for(i = 0; i < sc->sc_channels * 16; i++) {
	    	/* skip claimed/unused drives */
	    	if (sc->sc_plist[i] != 2)
			continue;

		/* are we it? */
		if (ld != bv->bv_volid) {
			ld++;
			continue;
		}

		bv->bv_status = BIOC_SVONLINE;
		bv->bv_size = (uint64_t)p->apd[i].adp_size *
		    (uint64_t)512;
		bv->bv_nodisk = 1;
		strlcpy(bv->bv_dev,
		    sc->sc_hdr[bv->bv_volid].dev,
		    sizeof(bv->bv_dev));

		if (p->apd[i].adp_ostatus == AMI_PD_HOTSPARE
		    && p->apd[i].adp_type == 0)
			bv->bv_level = -1;
		else
			bv->bv_level = -2;

		error = 0;
		goto bail;
	}

bail:
	return (error);
}

int
ami_disk(struct ami_softc *sc, struct bioc_disk *bd,
    struct ami_big_diskarray *p)
{
	char vend[8+16+4+1], *vendp;
	char ser[32 + 1];
	struct scsi_inquiry_data *inqbuf;
	struct scsi_vpd_serial *vpdbuf;
	int i, ld = p->ada_nld, error = EINVAL;
	u_int8_t ch, tg;
	daddr_t sz = 0;

	inqbuf = dma_alloc(sizeof(*inqbuf), PR_WAITOK);
	vpdbuf = dma_alloc(sizeof(*vpdbuf), PR_WAITOK);

	for(i = 0; i < sc->sc_channels * 16; i++) {
	    	/* skip claimed/unused drives */
	    	if (sc->sc_plist[i] != 2)
			continue;

		/* are we it? */
		if (ld != bd->bd_volid) {
			ld++;
			continue;
		}

		ch = (i & 0xf0) >> 4;
		tg = i & 0x0f;
		if (ami_drv_inq(sc, ch, tg, 0, inqbuf))
			goto bail;

		vendp = inqbuf->vendor;
		bcopy(vendp, vend, sizeof vend - 1);

		vend[sizeof vend - 1] = '\0';
		strlcpy(bd->bd_vendor, vend, sizeof(bd->bd_vendor));

		if (!ami_drv_inq(sc, ch, tg, 0x80, vpdbuf)) {
			bcopy(vpdbuf->serial, ser, sizeof ser - 1);
			ser[sizeof ser - 1] = '\0';
			if (_2btol(vpdbuf->hdr.page_length) < sizeof ser)
				ser[_2btol(vpdbuf->hdr.page_length)] = '\0';
			strlcpy(bd->bd_serial, ser, sizeof(bd->bd_serial));
		}

		error = ami_drv_readcap(sc, ch, tg, &sz);
		if (error)
			goto bail;

		bd->bd_size = sz;
		bd->bd_channel = ch;
		bd->bd_target = tg;

		strlcpy(bd->bd_procdev, sc->sc_rawsoftcs[ch].sc_procdev,
		    sizeof(bd->bd_procdev));

		if (p->apd[i].adp_ostatus == AMI_PD_HOTSPARE)
			bd->bd_status = BIOC_SDHOTSPARE;
		else
			bd->bd_status = BIOC_SDUNUSED;

#ifdef AMI_DEBUG
		if (p->apd[i].adp_type != 0)
			printf("invalid disk type: %d %d %x inquiry type: %x\n",
			    ch, tg, p->apd[i].adp_type, inqbuf->device);
#endif /* AMI_DEBUG */

		error = 0;
		goto bail;
	}

bail:
	dma_free(inqbuf, sizeof(*inqbuf));
	dma_free(vpdbuf, sizeof(*vpdbuf));
	return (error);
}

int
ami_ioctl_vol(struct ami_softc *sc, struct bioc_vol *bv)
{
	struct ami_big_diskarray *p; /* struct too large for stack */
	int i, s, t, off;
	int error = 0;
	struct ami_progress perc;
	u_int8_t bgi[5]; /* 40 LD, 1 bit per LD if BGI is active */

	p = malloc(sizeof *p, M_DEVBUF, M_NOWAIT);
	if (!p)
		return (ENOMEM);

	if ((error = ami_mgmt(sc, AMI_FCOP, AMI_FC_RDCONF, 0, 0, sizeof *p, p)))
		goto bail;

	if (bv->bv_volid >= p->ada_nld) {
		error = ami_vol(sc, bv, p);
		goto bail;
	}

	i = bv->bv_volid;

	switch (p->ald[i].adl_status) {
	case AMI_RDRV_OFFLINE:
		bv->bv_status = BIOC_SVOFFLINE;
		break;

	case AMI_RDRV_DEGRADED:
		bv->bv_status = BIOC_SVDEGRADED;
		break;

	case AMI_RDRV_OPTIMAL:
		bv->bv_status = BIOC_SVONLINE;
		bv->bv_percent = -1;

		/* get BGI progress here and over-ride status if so */
		memset(bgi, 0, sizeof bgi);
		if (ami_mgmt(sc, AMI_MISC, AMI_GET_BGI, 0, 0, sizeof bgi, &bgi))
			break;

		if ((bgi[i / 8] & (1 << i % 8)) == 0)
			break;

		if (!ami_mgmt(sc, AMI_GCHECKPROGR, i, 0, 0, sizeof perc, &perc))
		    	if (perc.apr_progress < 100) {
				bv->bv_status = BIOC_SVSCRUB;
				bv->bv_percent = perc.apr_progress >= 100 ? -1 :
				    perc.apr_progress;
			}
		break;

	default:
		bv->bv_status = BIOC_SVINVALID;
	}

	/* over-ride status if a pd is in rebuild status for this ld */
	for (s = 0; s < p->ald[i].adl_spandepth; s++)
		for (t = 0; t < p->ald[i].adl_nstripes; t++) {
			off = p->ald[i].asp[s].adv[t].add_channel *
			    AMI_MAX_TARGET +
			    p->ald[i].asp[s].adv[t].add_target;

			if (p->apd[off].adp_ostatus != AMI_PD_RBLD)
				continue;

			/* get rebuild progress from pd 0 */
			bv->bv_status = BIOC_SVREBUILD;
			if (ami_mgmt(sc, AMI_GRBLDPROGR,
			    p->ald[i].asp[s].adv[t].add_channel,
			    p->ald[i].asp[s].adv[t].add_target, 0,
			    sizeof perc, &perc))
				bv->bv_percent = -1;
			else
				bv->bv_percent = perc.apr_progress >= 100 ? -1 :
				    perc.apr_progress;
			break;
		}

	bv->bv_size = 0;
	bv->bv_level = p->ald[i].adl_raidlvl;
	bv->bv_nodisk = 0;

	for (s = 0; s < p->ald[i].adl_spandepth; s++) {
		for (t = 0; t < p->ald[i].adl_nstripes; t++)
			bv->bv_nodisk++;

		switch (bv->bv_level) {
		case 0:
			bv->bv_size += p->ald[i].asp[s].ads_length *
			    p->ald[i].adl_nstripes;
			break;

		case 1:
			bv->bv_size += p->ald[i].asp[s].ads_length;
			break;

		case 5:
			bv->bv_size += p->ald[i].asp[s].ads_length *
			    (p->ald[i].adl_nstripes - 1);
			break;
		}
	}

	if (p->ald[i].adl_spandepth > 1)
		bv->bv_level *= 10;

	bv->bv_size *= (uint64_t)512;

	strlcpy(bv->bv_dev, sc->sc_hdr[i].dev, sizeof(bv->bv_dev));

bail:
	free(p, M_DEVBUF, sizeof *p);

	return (error);
}

int
ami_ioctl_disk(struct ami_softc *sc, struct bioc_disk *bd)
{
	struct scsi_inquiry_data *inqbuf;
	struct scsi_vpd_serial *vpdbuf;
	struct ami_big_diskarray *p; /* struct too large for stack */
	int i, s, t, d;
	int off;
	int error = EINVAL;
	u_int16_t ch, tg;
	char vend[8+16+4+1], *vendp;
	char ser[32 + 1];

	inqbuf = dma_alloc(sizeof(*inqbuf), PR_WAITOK);
	vpdbuf = dma_alloc(sizeof(*inqbuf), PR_WAITOK);
	p = malloc(sizeof *p, M_DEVBUF, M_WAITOK);

	if ((error = ami_mgmt(sc, AMI_FCOP, AMI_FC_RDCONF, 0, 0, sizeof *p, p)))
		goto bail;

	if (bd->bd_volid >= p->ada_nld) {
		error = ami_disk(sc, bd, p);
		goto bail;
	}

	i = bd->bd_volid;
	for (s = 0, d = 0; s < p->ald[i].adl_spandepth; s++)
		for (t = 0; t < p->ald[i].adl_nstripes; t++) {
			if (d != bd->bd_diskid) {
				d++;
				continue;
			}

			off = p->ald[i].asp[s].adv[t].add_channel *
			    AMI_MAX_TARGET +
			    p->ald[i].asp[s].adv[t].add_target;

			bd->bd_size = (uint64_t)p->apd[off].adp_size *
			    (uint64_t)512;

			switch (p->apd[off].adp_ostatus) {
			case AMI_PD_UNCNF:
				bd->bd_status = BIOC_SDUNUSED;
				break;

			case AMI_PD_ONLINE:
				bd->bd_status = BIOC_SDONLINE;
				break;

			case AMI_PD_FAILED:
				bd->bd_status = BIOC_SDFAILED;
				bd->bd_size = 0;
				break;

			case AMI_PD_RBLD:
				bd->bd_status = BIOC_SDREBUILD;
				break;

			case AMI_PD_HOTSPARE:
				bd->bd_status = BIOC_SDHOTSPARE;
				break;

			default:
				bd->bd_status = BIOC_SDINVALID;
				bd->bd_size = 0;
			}


			ch = p->ald[i].asp[s].adv[t].add_target >> 4;
			tg = p->ald[i].asp[s].adv[t].add_target & 0x0f;

			bd->bd_channel = ch;
			bd->bd_target = tg;
			strlcpy(bd->bd_procdev, sc->sc_rawsoftcs[ch].sc_procdev,
			    sizeof(bd->bd_procdev));

			/* if we are failed don't query drive */
			if (bd->bd_size == 0) {
				bzero(&bd->bd_vendor, sizeof(bd->bd_vendor));
				bzero(&bd->bd_serial, sizeof(bd->bd_serial));
				goto done;
			}

			if (!ami_drv_inq(sc, ch, tg, 0, inqbuf)) {
				vendp = inqbuf->vendor;
				bcopy(vendp, vend, sizeof vend - 1);
				vend[sizeof vend - 1] = '\0';
				strlcpy(bd->bd_vendor, vend,
				    sizeof(bd->bd_vendor));
			}

			if (!ami_drv_inq(sc, ch, tg, 0x80, vpdbuf)) {
				bcopy(vpdbuf->serial, ser, sizeof ser - 1);
				ser[sizeof ser - 1] = '\0';
				if (_2btol(vpdbuf->hdr.page_length) <
				    sizeof(ser))
					ser[_2btol(vpdbuf->hdr.page_length)] =
					    '\0';
				strlcpy(bd->bd_serial, ser,
				    sizeof(bd->bd_serial));
			}
			goto done;
		}

done:
	error = 0;
bail:
	free(p, M_DEVBUF, sizeof *p);
	dma_free(vpdbuf, sizeof(*vpdbuf));
	dma_free(inqbuf, sizeof(*inqbuf));

	return (error);
}

int
ami_ioctl_alarm(struct ami_softc *sc, struct bioc_alarm *ba)
{
	int error = 0;
	u_int8_t func, ret;

	switch(ba->ba_opcode) {
	case BIOC_SADISABLE:
		func = AMI_SPKR_OFF;
		break;

	case BIOC_SAENABLE:
		func = AMI_SPKR_ON;
		break;

	case BIOC_SASILENCE:
		func = AMI_SPKR_SHUT;
		break;

	case BIOC_GASTATUS:
		func = AMI_SPKR_GVAL;
		break;

	case BIOC_SATEST:
		func = AMI_SPKR_TEST;
		break;

	default:
		AMI_DPRINTF(AMI_D_IOCTL, ("%s: biocalarm invalid opcode %x\n",
		    DEVNAME(sc), ba->ba_opcode));
		return (EINVAL);
	}

	if (!(error = ami_mgmt(sc, AMI_SPEAKER, func, 0, 0, sizeof ret,
	    &ret))) {
		if (ba->ba_opcode == BIOC_GASTATUS)
			ba->ba_status = ret;
		else
			ba->ba_status = 0;
	}

	return (error);
}

int
ami_ioctl_setstate(struct ami_softc *sc, struct bioc_setstate *bs)
{
	struct scsi_inquiry_data *inqbuf;
	int func, error = 0;

	inqbuf = dma_alloc(sizeof(*inqbuf), PR_WAITOK);

	switch (bs->bs_status) {
	case BIOC_SSONLINE:
		func = AMI_STATE_ON;
		break;

	case BIOC_SSOFFLINE:
		func = AMI_STATE_FAIL;
		break;

	case BIOC_SSHOTSPARE:
		if (ami_drv_inq(sc, bs->bs_channel, bs->bs_target, 0,
		    inqbuf)) {
			error = EINVAL;
			goto done;
		}

		func = AMI_STATE_SPARE;
		break;

	default:
		AMI_DPRINTF(AMI_D_IOCTL, ("%s: biocsetstate invalid opcode %x\n"
		    , DEVNAME(sc), bs->bs_status));
		error = EINVAL;
		goto done;
	}

	if ((error = ami_mgmt(sc, AMI_CHSTATE, bs->bs_channel, bs->bs_target,
	    func, 0, NULL)))
		goto done;

done:
	dma_free(inqbuf, sizeof(*inqbuf));
	return (error);
}

#ifndef SMALL_KERNEL
int
ami_create_sensors(struct ami_softc *sc)
{
	struct device *dev;
	struct scsibus_softc *ssc = NULL;
	struct scsi_link *link;
	int i;

	TAILQ_FOREACH(dev, &alldevs, dv_list) {
		if (dev->dv_parent != &sc->sc_dev)
			continue;

		/* check if this is the scsibus for the logical disks */
		ssc = (struct scsibus_softc *)dev;
		if (ssc == sc->sc_scsibus)
			break;
	}

	if (ssc == NULL)
		return (1);

	sc->sc_sensors = mallocarray(sc->sc_nunits, sizeof(struct ksensor),
	    M_DEVBUF, M_WAITOK|M_CANFAIL|M_ZERO);
	if (sc->sc_sensors == NULL)
		return (1);

	strlcpy(sc->sc_sensordev.xname, DEVNAME(sc),
	    sizeof(sc->sc_sensordev.xname));

	for (i = 0; i < sc->sc_nunits; i++) {
		link = scsi_get_link(ssc, i, 0);
		if (link == NULL)
			goto bad;

		dev = link->device_softc;

		sc->sc_sensors[i].type = SENSOR_DRIVE;
		sc->sc_sensors[i].status = SENSOR_S_UNKNOWN;

		strlcpy(sc->sc_sensors[i].desc, dev->dv_xname,
		    sizeof(sc->sc_sensors[i].desc));

		sensor_attach(&sc->sc_sensordev, &sc->sc_sensors[i]);
	}

	sc->sc_bd = malloc(sizeof(*sc->sc_bd), M_DEVBUF, M_WAITOK|M_CANFAIL);
	if (sc->sc_bd == NULL)
		goto bad;

	if (sensor_task_register(sc, ami_refresh_sensors, 10) == NULL)
		goto freebd;

	sensordev_install(&sc->sc_sensordev);

	return (0);

freebd:
	free(sc->sc_bd, M_DEVBUF, sizeof(*sc->sc_bd));
bad:
	free(sc->sc_sensors, M_DEVBUF, sc->sc_nunits * sizeof(struct ksensor));

	return (1);
}

void
ami_refresh_sensors(void *arg)
{
	struct ami_softc *sc = arg;
	int i;

	if (ami_mgmt(sc, AMI_FCOP, AMI_FC_RDCONF, 0, 0, sizeof(*sc->sc_bd),
	    sc->sc_bd)) {
		for (i = 0; i < sc->sc_nunits; i++) {
			sc->sc_sensors[i].value = 0; /* unknown */
			sc->sc_sensors[i].status = SENSOR_S_UNKNOWN;
		}
		return;
	}

	for (i = 0; i < sc->sc_nunits; i++) {
		switch (sc->sc_bd->ald[i].adl_status) {
		case AMI_RDRV_OFFLINE:
			sc->sc_sensors[i].value = SENSOR_DRIVE_FAIL;
			sc->sc_sensors[i].status = SENSOR_S_CRIT;
			break;

		case AMI_RDRV_DEGRADED:
			sc->sc_sensors[i].value = SENSOR_DRIVE_PFAIL;
			sc->sc_sensors[i].status = SENSOR_S_WARN;
			break;

		case AMI_RDRV_OPTIMAL:
			sc->sc_sensors[i].value = SENSOR_DRIVE_ONLINE;
			sc->sc_sensors[i].status = SENSOR_S_OK;
			break;

		default:
			sc->sc_sensors[i].value = 0; /* unknown */
			sc->sc_sensors[i].status = SENSOR_S_UNKNOWN;
		}
	}
}
#endif /* SMALL_KERNEL */
#endif /* NBIO > 0 */

#ifdef AMI_DEBUG
void
ami_print_mbox(struct ami_iocmd *mbox)
{
	int i;

	printf("acc_cmd: %d  aac_id: %d  acc_busy: %d  acc_nstat: %d  ",
	    mbox->acc_cmd, mbox->acc_id, mbox->acc_busy, mbox->acc_nstat);
	printf("acc_status: %d  acc_poll: %d  acc_ack: %d\n",
	    mbox->acc_status, mbox->acc_poll, mbox->acc_ack);

	printf("acc_cmplidl: ");
	for (i = 0; i < AMI_MAXSTATACK; i++) {
		printf("[%d] = %d  ", i, mbox->acc_cmplidl[i]);
	}

	printf("\n");
}
#endif /* AMI_DEBUG */
