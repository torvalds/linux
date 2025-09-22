/*	$OpenBSD: nvme.c,v 1.124 2024/10/08 19:41:23 kettenis Exp $ */

/*
 * Copyright (c) 2014 David Gwynne <dlg@openbsd.org>
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
#include <sys/ioctl.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <sys/mutex.h>
#include <sys/pool.h>
#include <sys/disk.h>

#include <sys/atomic.h>

#include <machine/bus.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>
#include <scsi/sdvar.h>

#include <dev/biovar.h>
#include <dev/ic/nvmereg.h>
#include <dev/ic/nvmevar.h>
#include <dev/ic/nvmeio.h>

struct cfdriver nvme_cd = {
	NULL,
	"nvme",
	DV_DULL
};

int	nvme_ready(struct nvme_softc *, u_int32_t);
int	nvme_enable(struct nvme_softc *);
int	nvme_disable(struct nvme_softc *);
int	nvme_shutdown(struct nvme_softc *);
int	nvme_resume(struct nvme_softc *);

void	nvme_dumpregs(struct nvme_softc *);
int	nvme_identify(struct nvme_softc *, u_int);
void	nvme_fill_identify(struct nvme_softc *, struct nvme_ccb *, void *);

#ifndef SMALL_KERNEL
void	nvme_refresh_sensors(void *);
#endif

int	nvme_ccbs_alloc(struct nvme_softc *, u_int);
void	nvme_ccbs_free(struct nvme_softc *, u_int);

void *	nvme_ccb_get(void *);
void	nvme_ccb_put(void *, void *);

int	nvme_poll(struct nvme_softc *, struct nvme_queue *, struct nvme_ccb *,
	    void (*)(struct nvme_softc *, struct nvme_ccb *, void *), u_int32_t);
void	nvme_poll_fill(struct nvme_softc *, struct nvme_ccb *, void *);
void	nvme_poll_done(struct nvme_softc *, struct nvme_ccb *,
	    struct nvme_cqe *);
void	nvme_sqe_fill(struct nvme_softc *, struct nvme_ccb *, void *);
void	nvme_empty_done(struct nvme_softc *, struct nvme_ccb *,
	    struct nvme_cqe *);

struct nvme_queue *
	nvme_q_alloc(struct nvme_softc *, u_int16_t, u_int, u_int);
int	nvme_q_create(struct nvme_softc *, struct nvme_queue *);
int	nvme_q_reset(struct nvme_softc *, struct nvme_queue *);
int	nvme_q_delete(struct nvme_softc *, struct nvme_queue *);
void	nvme_q_submit(struct nvme_softc *,
	    struct nvme_queue *, struct nvme_ccb *,
	    void (*)(struct nvme_softc *, struct nvme_ccb *, void *));
int	nvme_q_complete(struct nvme_softc *, struct nvme_queue *);
void	nvme_q_free(struct nvme_softc *, struct nvme_queue *);

void	nvme_scsi_cmd(struct scsi_xfer *);
void	nvme_minphys(struct buf *, struct scsi_link *);
int	nvme_scsi_probe(struct scsi_link *);
void	nvme_scsi_free(struct scsi_link *);
uint64_t nvme_scsi_size(const struct nvm_identify_namespace *);
int	nvme_scsi_ioctl(struct scsi_link *, u_long, caddr_t, int);
int	nvme_passthrough_cmd(struct nvme_softc *, struct nvme_pt_cmd *,
	int, int);

#ifdef HIBERNATE
#include <uvm/uvm_extern.h>
#include <sys/hibernate.h>
#include <sys/disklabel.h>

int	nvme_hibernate_io(dev_t, daddr_t, vaddr_t, size_t, int, void *);
#endif

#if NBIO > 0
void	nvme_bio_status(struct bio_status *, const char *, ...);

const char *nvme_bioctl_sdname(const struct nvme_softc *, int);

int	nvme_bioctl(struct device *, u_long, caddr_t);
int	nvme_bioctl_inq(struct nvme_softc *, struct bioc_inq *);
int	nvme_bioctl_vol(struct nvme_softc *, struct bioc_vol *);
int	nvme_bioctl_disk(struct nvme_softc *, struct bioc_disk *);
#endif	/* NBIO > 0 */

const struct scsi_adapter nvme_switch = {
	nvme_scsi_cmd, nvme_minphys, nvme_scsi_probe, nvme_scsi_free,
	nvme_scsi_ioctl
};

void	nvme_scsi_io(struct scsi_xfer *, int);
void	nvme_scsi_io_fill(struct nvme_softc *, struct nvme_ccb *, void *);
void	nvme_scsi_io_done(struct nvme_softc *, struct nvme_ccb *,
	    struct nvme_cqe *);

void	nvme_scsi_sync(struct scsi_xfer *);
void	nvme_scsi_sync_fill(struct nvme_softc *, struct nvme_ccb *, void *);
void	nvme_scsi_sync_done(struct nvme_softc *, struct nvme_ccb *,
	    struct nvme_cqe *);

void	nvme_scsi_inq(struct scsi_xfer *);
void	nvme_scsi_inquiry(struct scsi_xfer *);
void	nvme_scsi_capacity16(struct scsi_xfer *);
void	nvme_scsi_capacity(struct scsi_xfer *);

uint32_t	nvme_op_sq_enter(struct nvme_softc *,
		    struct nvme_queue *, struct nvme_ccb *);
void		nvme_op_sq_leave(struct nvme_softc *,
		    struct nvme_queue *, struct nvme_ccb *);
uint32_t	nvme_op_sq_enter_locked(struct nvme_softc *,
		    struct nvme_queue *, struct nvme_ccb *);
void		nvme_op_sq_leave_locked(struct nvme_softc *,
		    struct nvme_queue *, struct nvme_ccb *);

void		nvme_op_cq_done(struct nvme_softc *,
		    struct nvme_queue *, struct nvme_ccb *);

static const struct nvme_ops nvme_ops = {
	.op_sq_enter		= nvme_op_sq_enter,
	.op_sq_leave		= nvme_op_sq_leave,
	.op_sq_enter_locked	= nvme_op_sq_enter_locked,
	.op_sq_leave_locked	= nvme_op_sq_leave_locked,

	.op_cq_done		= nvme_op_cq_done,
};

#define NVME_TIMO_QOP			5000	/* ms to create/delete queue */
#define NVME_TIMO_PT			5000	/* ms to complete passthrough */
#define NVME_TIMO_IDENT			10000	/* ms to probe/identify */
#define NVME_TIMO_LOG_PAGE		5000	/* ms to read log pages */
#define NVME_TIMO_DELAYNS		10	/* ns to delay() in poll loop */

/*
 * Some controllers, at least Apple NVMe, always require split
 * transfers, so don't use bus_space_{read,write}_8() on LP64.
 */
u_int64_t
nvme_read8(struct nvme_softc *sc, bus_size_t r)
{
	u_int64_t v;

	v = (u_int64_t)nvme_read4(sc, r) |
	    (u_int64_t)nvme_read4(sc, r + 4) << 32;

	return (v);
}

void
nvme_write8(struct nvme_softc *sc, bus_size_t r, u_int64_t v)
{
	nvme_write4(sc, r, v);
	nvme_write4(sc, r + 4, v >> 32);
}

void
nvme_dumpregs(struct nvme_softc *sc)
{
	u_int64_t r8;
	u_int32_t r4;

	r8 = nvme_read8(sc, NVME_CAP);
	printf("%s: cap  0x%016llx\n", DEVNAME(sc), nvme_read8(sc, NVME_CAP));
	printf("%s:  mpsmax %u (%u)\n", DEVNAME(sc),
	    (u_int)NVME_CAP_MPSMAX(r8), (1 << NVME_CAP_MPSMAX(r8)));
	printf("%s:  mpsmin %u (%u)\n", DEVNAME(sc),
	    (u_int)NVME_CAP_MPSMIN(r8), (1 << NVME_CAP_MPSMIN(r8)));
	printf("%s:  css %llu\n", DEVNAME(sc), NVME_CAP_CSS(r8));
	printf("%s:  nssrs %llu\n", DEVNAME(sc), NVME_CAP_NSSRS(r8));
	printf("%s:  dstrd %u\n", DEVNAME(sc), NVME_CAP_DSTRD(r8));
	printf("%s:  to %llu msec\n", DEVNAME(sc), NVME_CAP_TO(r8));
	printf("%s:  ams %llu\n", DEVNAME(sc), NVME_CAP_AMS(r8));
	printf("%s:  cqr %llu\n", DEVNAME(sc), NVME_CAP_CQR(r8));
	printf("%s:  mqes %llu\n", DEVNAME(sc), NVME_CAP_MQES(r8));

	printf("%s: vs   0x%04x\n", DEVNAME(sc), nvme_read4(sc, NVME_VS));

	r4 = nvme_read4(sc, NVME_CC);
	printf("%s: cc   0x%04x\n", DEVNAME(sc), r4);
	printf("%s:  iocqes %u\n", DEVNAME(sc), NVME_CC_IOCQES_R(r4));
	printf("%s:  iosqes %u\n", DEVNAME(sc), NVME_CC_IOSQES_R(r4));
	printf("%s:  shn %u\n", DEVNAME(sc), NVME_CC_SHN_R(r4));
	printf("%s:  ams %u\n", DEVNAME(sc), NVME_CC_AMS_R(r4));
	printf("%s:  mps %u\n", DEVNAME(sc), NVME_CC_MPS_R(r4));
	printf("%s:  css %u\n", DEVNAME(sc), NVME_CC_CSS_R(r4));
	printf("%s:  en %u\n", DEVNAME(sc), ISSET(r4, NVME_CC_EN));

	printf("%s: csts 0x%08x\n", DEVNAME(sc), nvme_read4(sc, NVME_CSTS));
	printf("%s: aqa  0x%08x\n", DEVNAME(sc), nvme_read4(sc, NVME_AQA));
	printf("%s: asq  0x%016llx\n", DEVNAME(sc), nvme_read8(sc, NVME_ASQ));
	printf("%s: acq  0x%016llx\n", DEVNAME(sc), nvme_read8(sc, NVME_ACQ));
}

int
nvme_ready(struct nvme_softc *sc, u_int32_t rdy)
{
	u_int i = 0;

	while ((nvme_read4(sc, NVME_CSTS) & NVME_CSTS_RDY) != rdy) {
		if (i++ > sc->sc_rdy_to)
			return (1);

		delay(1000);
		nvme_barrier(sc, NVME_CSTS, 4, BUS_SPACE_BARRIER_READ);
	}

	return (0);
}

int
nvme_enable(struct nvme_softc *sc)
{
	u_int32_t cc;

	cc = nvme_read4(sc, NVME_CC);
	if (ISSET(cc, NVME_CC_EN))
		return (nvme_ready(sc, NVME_CSTS_RDY));

	if (sc->sc_ops->op_enable != NULL)
		sc->sc_ops->op_enable(sc);

	nvme_write4(sc, NVME_AQA, NVME_AQA_ACQS(sc->sc_admin_q->q_entries) |
	    NVME_AQA_ASQS(sc->sc_admin_q->q_entries));
	nvme_barrier(sc, 0, sc->sc_ios, BUS_SPACE_BARRIER_WRITE);

	nvme_write8(sc, NVME_ASQ, NVME_DMA_DVA(sc->sc_admin_q->q_sq_dmamem));
	nvme_barrier(sc, 0, sc->sc_ios, BUS_SPACE_BARRIER_WRITE);
	nvme_write8(sc, NVME_ACQ, NVME_DMA_DVA(sc->sc_admin_q->q_cq_dmamem));
	nvme_barrier(sc, 0, sc->sc_ios, BUS_SPACE_BARRIER_WRITE);

	CLR(cc, NVME_CC_IOCQES_MASK | NVME_CC_IOSQES_MASK | NVME_CC_SHN_MASK |
	    NVME_CC_AMS_MASK | NVME_CC_MPS_MASK | NVME_CC_CSS_MASK);
	SET(cc, NVME_CC_IOSQES(6));	/* Submission queue size == 2**6 (64) */
	SET(cc, NVME_CC_IOCQES(4));	/* Completion queue size == 2**4 (16) */
	SET(cc, NVME_CC_SHN(NVME_CC_SHN_NONE));
	SET(cc, NVME_CC_CSS(NVME_CC_CSS_NVM));
	SET(cc, NVME_CC_AMS(NVME_CC_AMS_RR));
	SET(cc, NVME_CC_MPS(ffs(sc->sc_mps) - 1));
	SET(cc, NVME_CC_EN);

	nvme_write4(sc, NVME_CC, cc);
	nvme_barrier(sc, 0, sc->sc_ios,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	return (nvme_ready(sc, NVME_CSTS_RDY));
}

int
nvme_disable(struct nvme_softc *sc)
{
	u_int32_t cc, csts;

	cc = nvme_read4(sc, NVME_CC);
	if (ISSET(cc, NVME_CC_EN)) {
		csts = nvme_read4(sc, NVME_CSTS);
		if (!ISSET(csts, NVME_CSTS_CFS) &&
		    nvme_ready(sc, NVME_CSTS_RDY) != 0)
			return (1);
	}

	CLR(cc, NVME_CC_EN);

	nvme_write4(sc, NVME_CC, cc);
	nvme_barrier(sc, 0, sc->sc_ios,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	return (nvme_ready(sc, 0));
}

int
nvme_attach(struct nvme_softc *sc)
{
	struct scsibus_attach_args saa;
	u_int64_t cap;
	u_int32_t reg;
	u_int nccbs = 0;

	mtx_init(&sc->sc_ccb_mtx, IPL_BIO);
	rw_init(&sc->sc_lock, "nvme_lock");
	SIMPLEQ_INIT(&sc->sc_ccb_list);
	scsi_iopool_init(&sc->sc_iopool, sc, nvme_ccb_get, nvme_ccb_put);
	if (sc->sc_ops == NULL)
		sc->sc_ops = &nvme_ops;
	if (sc->sc_openings == 0)
		sc->sc_openings = 64;

	reg = nvme_read4(sc, NVME_VS);
	if (reg == 0xffffffff) {
		printf("invalid mapping\n");
		return (1);
	}

	printf("NVMe %d.%d\n", NVME_VS_MJR(reg), NVME_VS_MNR(reg));

	cap = nvme_read8(sc, NVME_CAP);
	sc->sc_dstrd = NVME_CAP_DSTRD(cap);
	if (NVME_CAP_MPSMIN(cap) > PAGE_SHIFT) {
		printf("%s: NVMe minimum page size %u "
		    "is greater than CPU page size %u\n", DEVNAME(sc),
		    1 << NVME_CAP_MPSMIN(cap), 1 << PAGE_SHIFT);
		return (1);
	}
	if (NVME_CAP_MPSMAX(cap) < PAGE_SHIFT)
		sc->sc_mps = 1 << NVME_CAP_MPSMAX(cap);
	else
		sc->sc_mps = 1 << PAGE_SHIFT;

	sc->sc_rdy_to = NVME_CAP_TO(cap);
	sc->sc_mdts = MAXPHYS;
	sc->sc_max_prpl = sc->sc_mdts / sc->sc_mps;

	if (nvme_disable(sc) != 0) {
		printf("%s: unable to disable controller\n", DEVNAME(sc));
		return (1);
	}

	sc->sc_admin_q = nvme_q_alloc(sc, NVME_ADMIN_Q, 128, sc->sc_dstrd);
	if (sc->sc_admin_q == NULL) {
		printf("%s: unable to allocate admin queue\n", DEVNAME(sc));
		return (1);
	}

	if (nvme_ccbs_alloc(sc, 16) != 0) {
		printf("%s: unable to allocate initial ccbs\n", DEVNAME(sc));
		goto free_admin_q;
	}
	nccbs = 16;

	if (nvme_enable(sc) != 0) {
		printf("%s: unable to enable controller\n", DEVNAME(sc));
		goto free_ccbs;
	}

	if (nvme_identify(sc, NVME_CAP_MPSMIN(cap)) != 0) {
		printf("%s: unable to identify controller\n", DEVNAME(sc));
		goto disable;
	}

	/* We now know the real values of sc_mdts and sc_max_prpl. */
	nvme_ccbs_free(sc, nccbs);
	if (nvme_ccbs_alloc(sc, 64) != 0) {
		printf("%s: unable to allocate ccbs\n", DEVNAME(sc));
		goto free_admin_q;
	}
	nccbs = 64;

	sc->sc_q = nvme_q_alloc(sc, NVME_IO_Q, 128, sc->sc_dstrd);
	if (sc->sc_q == NULL) {
		printf("%s: unable to allocate io q\n", DEVNAME(sc));
		goto disable;
	}

	if (nvme_q_create(sc, sc->sc_q) != 0) {
		printf("%s: unable to create io q\n", DEVNAME(sc));
		goto free_q;
	}

#ifdef HIBERNATE
	sc->sc_hib_q = nvme_q_alloc(sc, NVME_HIB_Q, 4, sc->sc_dstrd);
	if (sc->sc_hib_q == NULL) {
		printf("%s: unable to allocate hibernate io queue\n", DEVNAME(sc));
		goto free_q;
	}
#endif

	nvme_write4(sc, NVME_INTMC, 1);

	sc->sc_namespaces = mallocarray(sc->sc_nn + 1,
	    sizeof(*sc->sc_namespaces), M_DEVBUF, M_WAITOK|M_ZERO);

	saa.saa_adapter = &nvme_switch;
	saa.saa_adapter_softc = sc;
	saa.saa_adapter_buswidth = sc->sc_nn + 1;
	saa.saa_luns = 1;
	saa.saa_adapter_target = 0;
	saa.saa_openings = sc->sc_openings;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	strlcpy(sc->sc_sensordev.xname, DEVNAME(sc), sizeof(sc->sc_sensordev.xname));

#ifndef SMALL_KERNEL
	sc->sc_temp_sensor.type = SENSOR_TEMP;
	sc->sc_temp_sensor.status = SENSOR_S_UNKNOWN;
	sensor_attach(&sc->sc_sensordev, &sc->sc_temp_sensor);

	sc->sc_usage_sensor.type = SENSOR_PERCENT;
	sc->sc_usage_sensor.status = SENSOR_S_UNKNOWN;
	strlcpy(sc->sc_usage_sensor.desc, "endurance used",
	    sizeof(sc->sc_usage_sensor.desc));
	sensor_attach(&sc->sc_sensordev, &sc->sc_usage_sensor);

	sc->sc_spare_sensor.type = SENSOR_PERCENT;
	sc->sc_spare_sensor.status = SENSOR_S_UNKNOWN;
	strlcpy(sc->sc_spare_sensor.desc, "available spare",
	    sizeof(sc->sc_spare_sensor.desc));
	sensor_attach(&sc->sc_sensordev, &sc->sc_spare_sensor);

	if (sensor_task_register(sc, nvme_refresh_sensors, 60) == NULL)
		goto free_q;

	sensordev_install(&sc->sc_sensordev);
#endif

	sc->sc_scsibus = (struct scsibus_softc *)config_found(&sc->sc_dev,
	    &saa, scsiprint);
#if NBIO > 0
	if (bio_register(&sc->sc_dev, nvme_bioctl) != 0)
		printf("%s: unable to register bioctl\n", DEVNAME(sc));
#endif	/* NBIO > 0 */

	return (0);

free_q:
	nvme_q_free(sc, sc->sc_q);
disable:
	nvme_disable(sc);
free_ccbs:
	nvme_ccbs_free(sc, nccbs);
free_admin_q:
	nvme_q_free(sc, sc->sc_admin_q);

	return (1);
}

int
nvme_resume(struct nvme_softc *sc)
{
	if (nvme_disable(sc) != 0) {
		printf("%s: unable to disable controller\n", DEVNAME(sc));
		return (1);
	}

	if (nvme_q_reset(sc, sc->sc_admin_q) != 0) {
		printf("%s: unable to reset admin queue\n", DEVNAME(sc));
		return (1);
	}

	if (nvme_enable(sc) != 0) {
		printf("%s: unable to enable controller\n", DEVNAME(sc));
		return (1);
	}

	sc->sc_q = nvme_q_alloc(sc, NVME_IO_Q, 128, sc->sc_dstrd);
	if (sc->sc_q == NULL) {
		printf("%s: unable to allocate io q\n", DEVNAME(sc));
		goto disable;
	}

	if (nvme_q_create(sc, sc->sc_q) != 0) {
		printf("%s: unable to create io q\n", DEVNAME(sc));
		goto free_q;
	}

	nvme_write4(sc, NVME_INTMC, 1);

	return (0);

free_q:
	nvme_q_free(sc, sc->sc_q);
disable:
	nvme_disable(sc);

	return (1);
}

int
nvme_scsi_probe(struct scsi_link *link)
{
	struct nvme_softc *sc = link->bus->sb_adapter_softc;
	struct nvme_sqe sqe;
	struct nvm_identify_namespace *identify;
	struct nvme_dmamem *mem;
	struct nvme_ccb *ccb;
	int rv;

	ccb = scsi_io_get(&sc->sc_iopool, 0);
	KASSERT(ccb != NULL);

	mem = nvme_dmamem_alloc(sc, sizeof(*identify));
	if (mem == NULL)
		return (ENOMEM);

	memset(&sqe, 0, sizeof(sqe));
	sqe.opcode = NVM_ADMIN_IDENTIFY;
	htolem32(&sqe.nsid, link->target);
	htolem64(&sqe.entry.prp[0], NVME_DMA_DVA(mem));
	htolem32(&sqe.cdw10, 0);

	ccb->ccb_done = nvme_empty_done;
	ccb->ccb_cookie = &sqe;

	nvme_dmamem_sync(sc, mem, BUS_DMASYNC_PREREAD);
	rv = nvme_poll(sc, sc->sc_admin_q, ccb, nvme_sqe_fill, NVME_TIMO_IDENT);
	nvme_dmamem_sync(sc, mem, BUS_DMASYNC_POSTREAD);

	scsi_io_put(&sc->sc_iopool, ccb);

	identify = NVME_DMA_KVA(mem);
	if (rv == 0) {
		if (nvme_scsi_size(identify) > 0) {
			/* Commit namespace if it has a size greater than zero. */
			identify = malloc(sizeof(*identify), M_DEVBUF, M_WAITOK);
			memcpy(identify, NVME_DMA_KVA(mem), sizeof(*identify));
			sc->sc_namespaces[link->target].ident = identify;
		} else {
			/* Don't attach a namespace if its size is zero. */
			rv = ENXIO;
		}
	}

	nvme_dmamem_free(sc, mem);

	return (rv);
}

int
nvme_shutdown(struct nvme_softc *sc)
{
	u_int32_t cc, csts;
	int i;

	nvme_write4(sc, NVME_INTMC, 0);

	if (nvme_q_delete(sc, sc->sc_q) != 0) {
		printf("%s: unable to delete q, disabling\n", DEVNAME(sc));
		goto disable;
	}

	cc = nvme_read4(sc, NVME_CC);
	CLR(cc, NVME_CC_SHN_MASK);
	SET(cc, NVME_CC_SHN(NVME_CC_SHN_NORMAL));
	nvme_write4(sc, NVME_CC, cc);

	for (i = 0; i < 4000; i++) {
		nvme_barrier(sc, 0, sc->sc_ios,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
		csts = nvme_read4(sc, NVME_CSTS);
		if ((csts & NVME_CSTS_SHST_MASK) == NVME_CSTS_SHST_DONE)
			return (0);

		delay(1000);
	}

	printf("%s: unable to shutdown, disabling\n", DEVNAME(sc));

disable:
	nvme_disable(sc);
	return (0);
}

int
nvme_activate(struct nvme_softc *sc, int act)
{
	int rv;

	switch (act) {
	case DVACT_POWERDOWN:
		rv = config_activate_children(&sc->sc_dev, act);
		nvme_shutdown(sc);
		break;
	case DVACT_RESUME:
		rv = nvme_resume(sc);
		if (rv == 0)
			rv = config_activate_children(&sc->sc_dev, act);
		break;
	default:
		rv = config_activate_children(&sc->sc_dev, act);
		break;
	}

	return (rv);
}

void
nvme_scsi_cmd(struct scsi_xfer *xs)
{
	switch (xs->cmd.opcode) {
	case READ_COMMAND:
	case READ_10:
	case READ_12:
	case READ_16:
		nvme_scsi_io(xs, SCSI_DATA_IN);
		return;
	case WRITE_COMMAND:
	case WRITE_10:
	case WRITE_12:
	case WRITE_16:
		nvme_scsi_io(xs, SCSI_DATA_OUT);
		return;

	case SYNCHRONIZE_CACHE:
		nvme_scsi_sync(xs);
		return;

	case INQUIRY:
		nvme_scsi_inq(xs);
		return;
	case READ_CAPACITY_16:
		nvme_scsi_capacity16(xs);
		return;
	case READ_CAPACITY:
		nvme_scsi_capacity(xs);
		return;

	case TEST_UNIT_READY:
	case PREVENT_ALLOW:
	case START_STOP:
		xs->error = XS_NOERROR;
		scsi_done(xs);
		return;

	default:
		break;
	}

	xs->error = XS_DRIVER_STUFFUP;
	scsi_done(xs);
}

void
nvme_minphys(struct buf *bp, struct scsi_link *link)
{
	struct nvme_softc *sc = link->bus->sb_adapter_softc;

	if (bp->b_bcount > sc->sc_mdts)
		bp->b_bcount = sc->sc_mdts;
}

void
nvme_scsi_io(struct scsi_xfer *xs, int dir)
{
	struct scsi_link *link = xs->sc_link;
	struct nvme_softc *sc = link->bus->sb_adapter_softc;
	struct nvme_ccb *ccb = xs->io;
	bus_dmamap_t dmap = ccb->ccb_dmamap;
	int i;

	if ((xs->flags & (SCSI_DATA_IN|SCSI_DATA_OUT)) != dir)
		goto stuffup;

	ccb->ccb_done = nvme_scsi_io_done;
	ccb->ccb_cookie = xs;

	if (bus_dmamap_load(sc->sc_dmat, dmap,
	    xs->data, xs->datalen, NULL, ISSET(xs->flags, SCSI_NOSLEEP) ?
	    BUS_DMA_NOWAIT : BUS_DMA_WAITOK) != 0)
		goto stuffup;

	bus_dmamap_sync(sc->sc_dmat, dmap, 0, dmap->dm_mapsize,
	    ISSET(xs->flags, SCSI_DATA_IN) ?
	    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);

	if (dmap->dm_nsegs > 2) {
		for (i = 1; i < dmap->dm_nsegs; i++) {
			htolem64(&ccb->ccb_prpl[i - 1],
			    dmap->dm_segs[i].ds_addr);
		}
		bus_dmamap_sync(sc->sc_dmat,
		    NVME_DMA_MAP(sc->sc_ccb_prpls),
		    ccb->ccb_prpl_off,
		    sizeof(*ccb->ccb_prpl) * (dmap->dm_nsegs - 1),
		    BUS_DMASYNC_PREWRITE);
	}

	if (ISSET(xs->flags, SCSI_POLL)) {
		nvme_poll(sc, sc->sc_q, ccb, nvme_scsi_io_fill, xs->timeout);
		return;
	}

	nvme_q_submit(sc, sc->sc_q, ccb, nvme_scsi_io_fill);
	return;

stuffup:
	xs->error = XS_DRIVER_STUFFUP;
	scsi_done(xs);
}

void
nvme_scsi_io_fill(struct nvme_softc *sc, struct nvme_ccb *ccb, void *slot)
{
	struct nvme_sqe_io *sqe = slot;
	struct scsi_xfer *xs = ccb->ccb_cookie;
	struct scsi_link *link = xs->sc_link;
	bus_dmamap_t dmap = ccb->ccb_dmamap;
	u_int64_t lba;
	u_int32_t blocks;

	scsi_cmd_rw_decode(&xs->cmd, &lba, &blocks);

	sqe->opcode = ISSET(xs->flags, SCSI_DATA_IN) ?
	    NVM_CMD_READ : NVM_CMD_WRITE;
	htolem32(&sqe->nsid, link->target);

	htolem64(&sqe->entry.prp[0], dmap->dm_segs[0].ds_addr);
	switch (dmap->dm_nsegs) {
	case 1:
		break;
	case 2:
		htolem64(&sqe->entry.prp[1], dmap->dm_segs[1].ds_addr);
		break;
	default:
		/* the prp list is already set up and synced */
		htolem64(&sqe->entry.prp[1], ccb->ccb_prpl_dva);
		break;
	}

	htolem64(&sqe->slba, lba);
	htolem16(&sqe->nlb, blocks - 1);
}

void
nvme_scsi_io_done(struct nvme_softc *sc, struct nvme_ccb *ccb,
    struct nvme_cqe *cqe)
{
	struct scsi_xfer *xs = ccb->ccb_cookie;
	bus_dmamap_t dmap = ccb->ccb_dmamap;
	u_int16_t flags;

	if (dmap->dm_nsegs > 2) {
		bus_dmamap_sync(sc->sc_dmat,
		    NVME_DMA_MAP(sc->sc_ccb_prpls),
		    ccb->ccb_prpl_off,
		    sizeof(*ccb->ccb_prpl) * (dmap->dm_nsegs - 1),
		    BUS_DMASYNC_POSTWRITE);
	}

	bus_dmamap_sync(sc->sc_dmat, dmap, 0, dmap->dm_mapsize,
	    ISSET(xs->flags, SCSI_DATA_IN) ?
	    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);

	bus_dmamap_unload(sc->sc_dmat, dmap);

	flags = lemtoh16(&cqe->flags);

	xs->error = (NVME_CQE_SC(flags) == NVME_CQE_SC_SUCCESS) ?
	    XS_NOERROR : XS_DRIVER_STUFFUP;
	xs->status = SCSI_OK;
	xs->resid = 0;
	scsi_done(xs);
}

void
nvme_scsi_sync(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct nvme_softc *sc = link->bus->sb_adapter_softc;
	struct nvme_ccb *ccb = xs->io;

	ccb->ccb_done = nvme_scsi_sync_done;
	ccb->ccb_cookie = xs;

	if (ISSET(xs->flags, SCSI_POLL)) {
		nvme_poll(sc, sc->sc_q, ccb, nvme_scsi_sync_fill, xs->timeout);
		return;
	}

	nvme_q_submit(sc, sc->sc_q, ccb, nvme_scsi_sync_fill);
}

void
nvme_scsi_sync_fill(struct nvme_softc *sc, struct nvme_ccb *ccb, void *slot)
{
	struct nvme_sqe *sqe = slot;
	struct scsi_xfer *xs = ccb->ccb_cookie;
	struct scsi_link *link = xs->sc_link;

	sqe->opcode = NVM_CMD_FLUSH;
	htolem32(&sqe->nsid, link->target);
}

void
nvme_scsi_sync_done(struct nvme_softc *sc, struct nvme_ccb *ccb,
    struct nvme_cqe *cqe)
{
	struct scsi_xfer *xs = ccb->ccb_cookie;
	u_int16_t flags;

	flags = lemtoh16(&cqe->flags);

	xs->error = (NVME_CQE_SC(flags) == NVME_CQE_SC_SUCCESS) ?
	    XS_NOERROR : XS_DRIVER_STUFFUP;
	xs->status = SCSI_OK;
	xs->resid = 0;
	scsi_done(xs);
}

void
nvme_scsi_inq(struct scsi_xfer *xs)
{
	struct scsi_inquiry *inq = (struct scsi_inquiry *)&xs->cmd;

	if (!ISSET(inq->flags, SI_EVPD)) {
		nvme_scsi_inquiry(xs);
		return;
	}

	switch (inq->pagecode) {
	default:
		/* printf("%s: %d\n", __func__, inq->pagecode); */
		break;
	}

	xs->error = XS_DRIVER_STUFFUP;
	scsi_done(xs);
}

void
nvme_scsi_inquiry(struct scsi_xfer *xs)
{
	struct scsi_inquiry_data inq;
	struct scsi_link *link = xs->sc_link;
	struct nvme_softc *sc = link->bus->sb_adapter_softc;
	struct nvm_identify_namespace *ns;

	ns = sc->sc_namespaces[link->target].ident;

	memset(&inq, 0, sizeof(inq));

	inq.device = T_DIRECT;
	inq.version = SCSI_REV_SPC4;
	inq.response_format = SID_SCSI2_RESPONSE;
	inq.additional_length = SID_SCSI2_ALEN;
	inq.flags |= SID_CmdQue;
	memcpy(inq.vendor, "NVMe    ", sizeof(inq.vendor));
	memcpy(inq.product, sc->sc_identify.mn, sizeof(inq.product));
	memcpy(inq.revision, sc->sc_identify.fr, sizeof(inq.revision));

	scsi_copy_internal_data(xs, &inq, sizeof(inq));

	xs->error = XS_NOERROR;
	scsi_done(xs);
}

void
nvme_scsi_capacity16(struct scsi_xfer *xs)
{
	struct scsi_read_cap_data_16 rcd;
	struct scsi_link *link = xs->sc_link;
	struct nvme_softc *sc = link->bus->sb_adapter_softc;
	struct nvm_identify_namespace *ns;
	struct nvm_namespace_format *f;
	u_int64_t addr;
	u_int16_t tpe = READ_CAP_16_TPE;

	ns = sc->sc_namespaces[link->target].ident;

	if (xs->cmdlen != sizeof(struct scsi_read_capacity_16)) {
		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		return;
	}

	addr = nvme_scsi_size(ns) - 1;
	f = &ns->lbaf[NVME_ID_NS_FLBAS(ns->flbas)];

	memset(&rcd, 0, sizeof(rcd));
	_lto8b(addr, rcd.addr);
	_lto4b(1 << f->lbads, rcd.length);
	_lto2b(tpe, rcd.lowest_aligned);

	memcpy(xs->data, &rcd, MIN(sizeof(rcd), xs->datalen));

	xs->error = XS_NOERROR;
	scsi_done(xs);
}

void
nvme_scsi_capacity(struct scsi_xfer *xs)
{
	struct scsi_read_cap_data rcd;
	struct scsi_link *link = xs->sc_link;
	struct nvme_softc *sc = link->bus->sb_adapter_softc;
	struct nvm_identify_namespace *ns;
	struct nvm_namespace_format *f;
	u_int64_t addr;

	ns = sc->sc_namespaces[link->target].ident;

	if (xs->cmdlen != sizeof(struct scsi_read_capacity)) {
		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		return;
	}

	addr = nvme_scsi_size(ns) - 1;
	if (addr > 0xffffffff)
		addr = 0xffffffff;

	f = &ns->lbaf[NVME_ID_NS_FLBAS(ns->flbas)];

	memset(&rcd, 0, sizeof(rcd));
	_lto4b(addr, rcd.addr);
	_lto4b(1 << f->lbads, rcd.length);

	memcpy(xs->data, &rcd, MIN(sizeof(rcd), xs->datalen));

	xs->error = XS_NOERROR;
	scsi_done(xs);
}

void
nvme_scsi_free(struct scsi_link *link)
{
	struct nvme_softc *sc = link->bus->sb_adapter_softc;
	struct nvm_identify_namespace *identify;

	identify = sc->sc_namespaces[link->target].ident;
	sc->sc_namespaces[link->target].ident = NULL;

	free(identify, M_DEVBUF, sizeof(*identify));
}

uint64_t
nvme_scsi_size(const struct nvm_identify_namespace *ns)
{
	uint64_t		ncap, nsze;

	ncap = lemtoh64(&ns->ncap); /* Max allowed allocation. */
	nsze = lemtoh64(&ns->nsze);

	if ((ns->nsfeat & NVME_ID_NS_NSFEAT_THIN_PROV) && ncap < nsze)
		return ncap;
	else
		return nsze;
}

int
nvme_passthrough_cmd(struct nvme_softc *sc, struct nvme_pt_cmd *pt, int dv_unit,
    int nsid)
{
	struct nvme_pt_status		 pt_status;
	struct nvme_sqe			 sqe;
	struct nvme_dmamem		*mem = NULL;
	struct nvme_ccb			*ccb = NULL;
	int				 flags;
	int				 rv = 0;

	ccb = nvme_ccb_get(sc);
	if (ccb == NULL)
		panic("nvme_passthrough_cmd: nvme_ccb_get returned NULL");

	memset(&sqe, 0, sizeof(sqe));
	sqe.opcode = pt->pt_opcode;
	htolem32(&sqe.nsid, pt->pt_nsid);
	htolem32(&sqe.cdw10, pt->pt_cdw10);
	htolem32(&sqe.cdw11, pt->pt_cdw11);
	htolem32(&sqe.cdw12, pt->pt_cdw12);
	htolem32(&sqe.cdw13, pt->pt_cdw13);
	htolem32(&sqe.cdw14, pt->pt_cdw14);
	htolem32(&sqe.cdw15, pt->pt_cdw15);

	ccb->ccb_done = nvme_empty_done;
	ccb->ccb_cookie = &sqe;

	switch (pt->pt_opcode) {
	case NVM_ADMIN_IDENTIFY:
	case NVM_ADMIN_GET_LOG_PG:
	case NVM_ADMIN_SELFTEST:
		break;

	default:
		rv = ENOTTY;
		goto done;
	}

	if (pt->pt_databuflen > 0) {
		mem = nvme_dmamem_alloc(sc, pt->pt_databuflen);
		if (mem == NULL) {
			rv = ENOMEM;
			goto done;
		}
		htolem64(&sqe.entry.prp[0], NVME_DMA_DVA(mem));
		nvme_dmamem_sync(sc, mem, BUS_DMASYNC_PREREAD);
	}

	flags = nvme_poll(sc, sc->sc_admin_q, ccb, nvme_sqe_fill, NVME_TIMO_PT);

	if (pt->pt_databuflen > 0) {
		nvme_dmamem_sync(sc, mem, BUS_DMASYNC_POSTREAD);
		if (flags == 0)
			rv = copyout(NVME_DMA_KVA(mem), pt->pt_databuf,
			    pt->pt_databuflen);
	}

	if (rv == 0 && pt->pt_statuslen > 0) {
		pt_status.ps_dv_unit = dv_unit;
		pt_status.ps_nsid = nsid;
		pt_status.ps_flags = flags;
		pt_status.ps_cc = nvme_read4(sc, NVME_CC);
		pt_status.ps_csts = nvme_read4(sc, NVME_CSTS);
		rv = copyout(&pt_status, pt->pt_status, pt->pt_statuslen);
	}

 done:
	if (mem)
		nvme_dmamem_free(sc, mem);
	if (ccb)
		nvme_ccb_put(sc, ccb);

	return rv;
}

int
nvme_scsi_ioctl(struct scsi_link *link, u_long cmd, caddr_t addr, int flag)
{
	struct nvme_softc		*sc = link->bus->sb_adapter_softc;
	struct nvme_pt_cmd		*pt = (struct nvme_pt_cmd *)addr;
	int				 rv;

	switch (cmd) {
	case NVME_PASSTHROUGH_CMD:
		break;
	default:
		return ENOTTY;
	}

	if ((pt->pt_cdw10 & 0xff) == 0)
		pt->pt_nsid = link->target;

	rv = nvme_passthrough_cmd(sc, pt, sc->sc_dev.dv_unit, link->target);
	if (rv)
		goto done;

 done:
	return rv;
}

uint32_t
nvme_op_sq_enter(struct nvme_softc *sc,
    struct nvme_queue *q, struct nvme_ccb *ccb)
{
	mtx_enter(&q->q_sq_mtx);
	return (nvme_op_sq_enter_locked(sc, q, ccb));
}

uint32_t
nvme_op_sq_enter_locked(struct nvme_softc *sc,
    struct nvme_queue *q, struct nvme_ccb *ccb)
{
	return (q->q_sq_tail);
}

void
nvme_op_sq_leave_locked(struct nvme_softc *sc,
    struct nvme_queue *q, struct nvme_ccb *ccb)
{
	uint32_t tail;

	tail = ++q->q_sq_tail;
	if (tail >= q->q_entries)
		tail = 0;
	q->q_sq_tail = tail;
	nvme_write4(sc, q->q_sqtdbl, tail);
}

void
nvme_op_sq_leave(struct nvme_softc *sc,
    struct nvme_queue *q, struct nvme_ccb *ccb)
{
	nvme_op_sq_leave_locked(sc, q, ccb);
	mtx_leave(&q->q_sq_mtx);
}

void
nvme_q_submit(struct nvme_softc *sc, struct nvme_queue *q, struct nvme_ccb *ccb,
    void (*fill)(struct nvme_softc *, struct nvme_ccb *, void *))
{
	struct nvme_sqe *sqe = NVME_DMA_KVA(q->q_sq_dmamem);
	u_int32_t tail;

	tail = sc->sc_ops->op_sq_enter(sc, q, ccb);

	sqe += tail;

	bus_dmamap_sync(sc->sc_dmat, NVME_DMA_MAP(q->q_sq_dmamem),
	    sizeof(*sqe) * tail, sizeof(*sqe), BUS_DMASYNC_POSTWRITE);
	memset(sqe, 0, sizeof(*sqe));
	(*fill)(sc, ccb, sqe);
	sqe->cid = ccb->ccb_id;
	bus_dmamap_sync(sc->sc_dmat, NVME_DMA_MAP(q->q_sq_dmamem),
	    sizeof(*sqe) * tail, sizeof(*sqe), BUS_DMASYNC_PREWRITE);

	sc->sc_ops->op_sq_leave(sc, q, ccb);
}

struct nvme_poll_state {
	struct nvme_sqe s;
	struct nvme_cqe c;
};

int
nvme_poll(struct nvme_softc *sc, struct nvme_queue *q, struct nvme_ccb *ccb,
    void (*fill)(struct nvme_softc *, struct nvme_ccb *, void *), u_int32_t ms)
{
	struct nvme_poll_state state;
	void (*done)(struct nvme_softc *, struct nvme_ccb *, struct nvme_cqe *);
	void *cookie;
	int64_t us;
	u_int16_t flags;

	memset(&state, 0, sizeof(state));
	(*fill)(sc, ccb, &state.s);

	done = ccb->ccb_done;
	cookie = ccb->ccb_cookie;

	ccb->ccb_done = nvme_poll_done;
	ccb->ccb_cookie = &state;

	nvme_q_submit(sc, q, ccb, nvme_poll_fill);
	for (us = ms * 1000; ms == 0 || us > 0; us -= NVME_TIMO_DELAYNS) {
		if (ISSET(state.c.flags, htole16(NVME_CQE_PHASE)))
			break;
		if (nvme_q_complete(sc, q) == 0)
			delay(NVME_TIMO_DELAYNS);
		nvme_barrier(sc, NVME_CSTS, 4, BUS_SPACE_BARRIER_READ);
	}

	ccb->ccb_cookie = cookie;
	done(sc, ccb, &state.c);

	flags = lemtoh16(&state.c.flags);

	return (flags & ~NVME_CQE_PHASE);
}

void
nvme_poll_fill(struct nvme_softc *sc, struct nvme_ccb *ccb, void *slot)
{
	struct nvme_sqe *sqe = slot;
	struct nvme_poll_state *state = ccb->ccb_cookie;

	*sqe = state->s;
}

void
nvme_poll_done(struct nvme_softc *sc, struct nvme_ccb *ccb,
    struct nvme_cqe *cqe)
{
	struct nvme_poll_state *state = ccb->ccb_cookie;

	state->c = *cqe;
	SET(state->c.flags, htole16(NVME_CQE_PHASE));
}

void
nvme_sqe_fill(struct nvme_softc *sc, struct nvme_ccb *ccb, void *slot)
{
	struct nvme_sqe *src = ccb->ccb_cookie;
	struct nvme_sqe *dst = slot;

	*dst = *src;
}

void
nvme_empty_done(struct nvme_softc *sc, struct nvme_ccb *ccb,
    struct nvme_cqe *cqe)
{
}

void
nvme_op_cq_done(struct nvme_softc *sc,
    struct nvme_queue *q, struct nvme_ccb *ccb)
{
	/* nop */
}

int
nvme_q_complete(struct nvme_softc *sc, struct nvme_queue *q)
{
	struct nvme_ccb *ccb;
	struct nvme_cqe *ring = NVME_DMA_KVA(q->q_cq_dmamem), *cqe;
	u_int32_t head;
	u_int16_t flags;
	int rv = 0;

	if (!mtx_enter_try(&q->q_cq_mtx))
		return (-1);

	head = q->q_cq_head;

	nvme_dmamem_sync(sc, q->q_cq_dmamem, BUS_DMASYNC_POSTREAD);
	for (;;) {
		cqe = &ring[head];
		flags = lemtoh16(&cqe->flags);
		if ((flags & NVME_CQE_PHASE) != q->q_cq_phase)
			break;

		membar_consumer();

		ccb = &sc->sc_ccbs[cqe->cid];
		sc->sc_ops->op_cq_done(sc, q, ccb);
		ccb->ccb_done(sc, ccb, cqe);

		if (++head >= q->q_entries) {
			head = 0;
			q->q_cq_phase ^= NVME_CQE_PHASE;
		}

		rv = 1;
	}
	nvme_dmamem_sync(sc, q->q_cq_dmamem, BUS_DMASYNC_PREREAD);

	if (rv)
		nvme_write4(sc, q->q_cqhdbl, q->q_cq_head = head);
	mtx_leave(&q->q_cq_mtx);

	return (rv);
}

int
nvme_identify(struct nvme_softc *sc, u_int mpsmin)
{
	char sn[41], mn[81], fr[17];
	struct nvm_identify_controller *identify;
	struct nvme_dmamem *mem;
	struct nvme_ccb *ccb;
	int rv = 1;

	ccb = nvme_ccb_get(sc);
	if (ccb == NULL)
		panic("nvme_identify: nvme_ccb_get returned NULL");

	mem = nvme_dmamem_alloc(sc, sizeof(*identify));
	if (mem == NULL)
		return (1);

	ccb->ccb_done = nvme_empty_done;
	ccb->ccb_cookie = mem;

	nvme_dmamem_sync(sc, mem, BUS_DMASYNC_PREREAD);
	rv = nvme_poll(sc, sc->sc_admin_q, ccb, nvme_fill_identify,
	    NVME_TIMO_IDENT);
	nvme_dmamem_sync(sc, mem, BUS_DMASYNC_POSTREAD);

	nvme_ccb_put(sc, ccb);

	if (rv != 0)
		goto done;

	identify = NVME_DMA_KVA(mem);

	scsi_strvis(sn, identify->sn, sizeof(identify->sn));
	scsi_strvis(mn, identify->mn, sizeof(identify->mn));
	scsi_strvis(fr, identify->fr, sizeof(identify->fr));

	printf("%s: %s, firmware %s, serial %s\n", DEVNAME(sc), mn, fr, sn);

	if (identify->mdts > 0) {
		sc->sc_mdts = (1 << identify->mdts) * (1 << mpsmin);
		if (sc->sc_mdts > NVME_MAXPHYS)
			sc->sc_mdts = NVME_MAXPHYS;
		sc->sc_max_prpl = sc->sc_mdts / sc->sc_mps;
	}

	sc->sc_nn = lemtoh32(&identify->nn);

	/*
	 * At least one Apple NVMe device presents a second, bogus disk that is
	 * inaccessible, so cap targets at 1.
	 *
	 * sd1 at scsibus1 targ 2 lun 0: <NVMe, APPLE SSD AP0512, 16.1> [..]
	 * sd1: 0MB, 4096 bytes/sector, 2 sectors
	 */
	if (sc->sc_nn > 1 &&
	    mn[0] == 'A' && mn[1] == 'P' && mn[2] == 'P' && mn[3] == 'L' &&
	    mn[4] == 'E')
		sc->sc_nn = 1;

	memcpy(&sc->sc_identify, identify, sizeof(sc->sc_identify));

done:
	nvme_dmamem_free(sc, mem);

	return (rv);
}

int
nvme_q_create(struct nvme_softc *sc, struct nvme_queue *q)
{
	struct nvme_sqe_q sqe;
	struct nvme_ccb *ccb;
	int rv;

	ccb = scsi_io_get(&sc->sc_iopool, 0);
	KASSERT(ccb != NULL);

	ccb->ccb_done = nvme_empty_done;
	ccb->ccb_cookie = &sqe;

	memset(&sqe, 0, sizeof(sqe));
	sqe.opcode = NVM_ADMIN_ADD_IOCQ;
	htolem64(&sqe.prp1, NVME_DMA_DVA(q->q_cq_dmamem));
	htolem16(&sqe.qsize, q->q_entries - 1);
	htolem16(&sqe.qid, q->q_id);
	sqe.qflags = NVM_SQE_CQ_IEN | NVM_SQE_Q_PC;

	rv = nvme_poll(sc, sc->sc_admin_q, ccb, nvme_sqe_fill, NVME_TIMO_QOP);
	if (rv != 0)
		goto fail;

	ccb->ccb_done = nvme_empty_done;
	ccb->ccb_cookie = &sqe;

	memset(&sqe, 0, sizeof(sqe));
	sqe.opcode = NVM_ADMIN_ADD_IOSQ;
	htolem64(&sqe.prp1, NVME_DMA_DVA(q->q_sq_dmamem));
	htolem16(&sqe.qsize, q->q_entries - 1);
	htolem16(&sqe.qid, q->q_id);
	htolem16(&sqe.cqid, q->q_id);
	sqe.qflags = NVM_SQE_Q_PC;

	rv = nvme_poll(sc, sc->sc_admin_q, ccb, nvme_sqe_fill, NVME_TIMO_QOP);
	if (rv != 0)
		goto fail;

fail:
	scsi_io_put(&sc->sc_iopool, ccb);
	return (rv);
}

int
nvme_q_delete(struct nvme_softc *sc, struct nvme_queue *q)
{
	struct nvme_sqe_q sqe;
	struct nvme_ccb *ccb;
	int rv;

	ccb = scsi_io_get(&sc->sc_iopool, 0);
	KASSERT(ccb != NULL);

	ccb->ccb_done = nvme_empty_done;
	ccb->ccb_cookie = &sqe;

	memset(&sqe, 0, sizeof(sqe));
	sqe.opcode = NVM_ADMIN_DEL_IOSQ;
	htolem16(&sqe.qid, q->q_id);

	rv = nvme_poll(sc, sc->sc_admin_q, ccb, nvme_sqe_fill, NVME_TIMO_QOP);
	if (rv != 0)
		goto fail;

	ccb->ccb_done = nvme_empty_done;
	ccb->ccb_cookie = &sqe;

	memset(&sqe, 0, sizeof(sqe));
	sqe.opcode = NVM_ADMIN_DEL_IOCQ;
	htolem16(&sqe.qid, q->q_id);

	rv = nvme_poll(sc, sc->sc_admin_q, ccb, nvme_sqe_fill, NVME_TIMO_QOP);
	if (rv != 0)
		goto fail;

	nvme_q_free(sc, q);

fail:
	scsi_io_put(&sc->sc_iopool, ccb);
	return (rv);

}

void
nvme_fill_identify(struct nvme_softc *sc, struct nvme_ccb *ccb, void *slot)
{
	struct nvme_sqe *sqe = slot;
	struct nvme_dmamem *mem = ccb->ccb_cookie;

	sqe->opcode = NVM_ADMIN_IDENTIFY;
	htolem64(&sqe->entry.prp[0], NVME_DMA_DVA(mem));
	htolem32(&sqe->cdw10, 1);
}

int
nvme_ccbs_alloc(struct nvme_softc *sc, u_int nccbs)
{
	struct nvme_ccb *ccb;
	bus_addr_t off;
	u_int64_t *prpl;
	u_int i;

	sc->sc_ccbs = mallocarray(nccbs, sizeof(*ccb), M_DEVBUF,
	    M_WAITOK | M_CANFAIL);
	if (sc->sc_ccbs == NULL)
		return (1);

	sc->sc_ccb_prpls = nvme_dmamem_alloc(sc,
	    sizeof(*prpl) * sc->sc_max_prpl * nccbs);

	prpl = NVME_DMA_KVA(sc->sc_ccb_prpls);
	off = 0;

	for (i = 0; i < nccbs; i++) {
		ccb = &sc->sc_ccbs[i];

		if (bus_dmamap_create(sc->sc_dmat, sc->sc_mdts,
		    sc->sc_max_prpl + 1, /* we get a free prp in the sqe */
		    sc->sc_mps, sc->sc_mps,
		    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
		    &ccb->ccb_dmamap) != 0)
			goto free_maps;

		ccb->ccb_id = i;
		ccb->ccb_prpl = prpl;
		ccb->ccb_prpl_off = off;
		ccb->ccb_prpl_dva = NVME_DMA_DVA(sc->sc_ccb_prpls) + off;

		SIMPLEQ_INSERT_TAIL(&sc->sc_ccb_list, ccb, ccb_entry);

		prpl += sc->sc_max_prpl;
		off += sizeof(*prpl) * sc->sc_max_prpl;
	}

	return (0);

free_maps:
	nvme_ccbs_free(sc, nccbs);
	return (1);
}

void *
nvme_ccb_get(void *cookie)
{
	struct nvme_softc *sc = cookie;
	struct nvme_ccb *ccb;

	mtx_enter(&sc->sc_ccb_mtx);
	ccb = SIMPLEQ_FIRST(&sc->sc_ccb_list);
	if (ccb != NULL)
		SIMPLEQ_REMOVE_HEAD(&sc->sc_ccb_list, ccb_entry);
	mtx_leave(&sc->sc_ccb_mtx);

	return (ccb);
}

void
nvme_ccb_put(void *cookie, void *io)
{
	struct nvme_softc *sc = cookie;
	struct nvme_ccb *ccb = io;

	mtx_enter(&sc->sc_ccb_mtx);
	SIMPLEQ_INSERT_HEAD(&sc->sc_ccb_list, ccb, ccb_entry);
	mtx_leave(&sc->sc_ccb_mtx);
}

void
nvme_ccbs_free(struct nvme_softc *sc, unsigned int nccbs)
{
	struct nvme_ccb *ccb;

	while ((ccb = SIMPLEQ_FIRST(&sc->sc_ccb_list)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&sc->sc_ccb_list, ccb_entry);
		bus_dmamap_destroy(sc->sc_dmat, ccb->ccb_dmamap);
	}

	nvme_dmamem_free(sc, sc->sc_ccb_prpls);
	free(sc->sc_ccbs, M_DEVBUF, nccbs * sizeof(*ccb));
}

struct nvme_queue *
nvme_q_alloc(struct nvme_softc *sc, u_int16_t id, u_int entries, u_int dstrd)
{
	struct nvme_queue *q;

	q = malloc(sizeof(*q), M_DEVBUF, M_WAITOK | M_CANFAIL);
	if (q == NULL)
		return (NULL);

	q->q_sq_dmamem = nvme_dmamem_alloc(sc,
	    sizeof(struct nvme_sqe) * entries);
	if (q->q_sq_dmamem == NULL)
		goto free;

	q->q_cq_dmamem = nvme_dmamem_alloc(sc,
	    sizeof(struct nvme_cqe) * entries);
	if (q->q_cq_dmamem == NULL)
		goto free_sq;

	memset(NVME_DMA_KVA(q->q_sq_dmamem), 0, NVME_DMA_LEN(q->q_sq_dmamem));
	memset(NVME_DMA_KVA(q->q_cq_dmamem), 0, NVME_DMA_LEN(q->q_cq_dmamem));

	mtx_init(&q->q_sq_mtx, IPL_BIO);
	mtx_init(&q->q_cq_mtx, IPL_BIO);
	q->q_sqtdbl = NVME_SQTDBL(id, dstrd);
	q->q_cqhdbl = NVME_CQHDBL(id, dstrd);

	q->q_id = id;
	q->q_entries = entries;
	q->q_sq_tail = 0;
	q->q_cq_head = 0;
	q->q_cq_phase = NVME_CQE_PHASE;

	if (sc->sc_ops->op_q_alloc != NULL) {
		if (sc->sc_ops->op_q_alloc(sc, q) != 0)
			goto free_cq;
	}

	nvme_dmamem_sync(sc, q->q_sq_dmamem, BUS_DMASYNC_PREWRITE);
	nvme_dmamem_sync(sc, q->q_cq_dmamem, BUS_DMASYNC_PREREAD);

	return (q);

free_cq:
	nvme_dmamem_free(sc, q->q_cq_dmamem);
free_sq:
	nvme_dmamem_free(sc, q->q_sq_dmamem);
free:
	free(q, M_DEVBUF, sizeof *q);

	return (NULL);
}

int
nvme_q_reset(struct nvme_softc *sc, struct nvme_queue *q)
{
	memset(NVME_DMA_KVA(q->q_sq_dmamem), 0, NVME_DMA_LEN(q->q_sq_dmamem));
	memset(NVME_DMA_KVA(q->q_cq_dmamem), 0, NVME_DMA_LEN(q->q_cq_dmamem));

	q->q_sq_tail = 0;
	q->q_cq_head = 0;
	q->q_cq_phase = NVME_CQE_PHASE;

	nvme_dmamem_sync(sc, q->q_sq_dmamem, BUS_DMASYNC_PREWRITE);
	nvme_dmamem_sync(sc, q->q_cq_dmamem, BUS_DMASYNC_PREREAD);

	return (0);
}

void
nvme_q_free(struct nvme_softc *sc, struct nvme_queue *q)
{
	nvme_dmamem_sync(sc, q->q_cq_dmamem, BUS_DMASYNC_POSTREAD);
	nvme_dmamem_sync(sc, q->q_sq_dmamem, BUS_DMASYNC_POSTWRITE);

	if (sc->sc_ops->op_q_free != NULL)
		sc->sc_ops->op_q_free(sc, q);

	nvme_dmamem_free(sc, q->q_cq_dmamem);
	nvme_dmamem_free(sc, q->q_sq_dmamem);
	free(q, M_DEVBUF, sizeof *q);
}

int
nvme_intr(void *xsc)
{
	struct nvme_softc *sc = xsc;
	int rv = 0;

	if (nvme_q_complete(sc, sc->sc_q))
		rv = 1;
	if (nvme_q_complete(sc, sc->sc_admin_q))
		rv = 1;

	return (rv);
}

int
nvme_intr_intx(void *xsc)
{
	struct nvme_softc *sc = xsc;
	int rv;

	nvme_write4(sc, NVME_INTMS, 1);
	rv = nvme_intr(sc);
	nvme_write4(sc, NVME_INTMC, 1);

	return (rv);
}

struct nvme_dmamem *
nvme_dmamem_alloc(struct nvme_softc *sc, size_t size)
{
	struct nvme_dmamem *ndm;
	int nsegs;

	ndm = malloc(sizeof(*ndm), M_DEVBUF, M_WAITOK | M_ZERO);
	if (ndm == NULL)
		return (NULL);

	ndm->ndm_size = size;

	if (bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
	    &ndm->ndm_map) != 0)
		goto ndmfree;

	if (bus_dmamem_alloc(sc->sc_dmat, size, sc->sc_mps, 0, &ndm->ndm_seg,
	    1, &nsegs, BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_64BIT) != 0)
		goto destroy;

	if (bus_dmamem_map(sc->sc_dmat, &ndm->ndm_seg, nsegs, size,
	    &ndm->ndm_kva, BUS_DMA_WAITOK) != 0)
		goto free;

	if (bus_dmamap_load(sc->sc_dmat, ndm->ndm_map, ndm->ndm_kva, size,
	    NULL, BUS_DMA_WAITOK) != 0)
		goto unmap;

	return (ndm);

unmap:
	bus_dmamem_unmap(sc->sc_dmat, ndm->ndm_kva, size);
free:
	bus_dmamem_free(sc->sc_dmat, &ndm->ndm_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, ndm->ndm_map);
ndmfree:
	free(ndm, M_DEVBUF, sizeof *ndm);

	return (NULL);
}

void
nvme_dmamem_sync(struct nvme_softc *sc, struct nvme_dmamem *mem, int ops)
{
	bus_dmamap_sync(sc->sc_dmat, NVME_DMA_MAP(mem),
	    0, NVME_DMA_LEN(mem), ops);
}

void
nvme_dmamem_free(struct nvme_softc *sc, struct nvme_dmamem *ndm)
{
	bus_dmamap_unload(sc->sc_dmat, ndm->ndm_map);
	bus_dmamem_unmap(sc->sc_dmat, ndm->ndm_kva, ndm->ndm_size);
	bus_dmamem_free(sc->sc_dmat, &ndm->ndm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, ndm->ndm_map);
	free(ndm, M_DEVBUF, sizeof *ndm);
}

#ifdef HIBERNATE

int
nvme_hibernate_admin_cmd(struct nvme_softc *sc, struct nvme_sqe *sqe,
    struct nvme_cqe *cqe, int cid)
{
	struct nvme_sqe *asqe = NVME_DMA_KVA(sc->sc_admin_q->q_sq_dmamem);
	struct nvme_cqe *acqe = NVME_DMA_KVA(sc->sc_admin_q->q_cq_dmamem);
	struct nvme_queue *q = sc->sc_admin_q;
	int tail;
	u_int16_t flags;

	/* submit command */
	tail = sc->sc_ops->op_sq_enter_locked(sc, q, /* XXX ccb */ NULL);

	asqe += tail;
	bus_dmamap_sync(sc->sc_dmat, NVME_DMA_MAP(q->q_sq_dmamem),
	    sizeof(*sqe) * tail, sizeof(*sqe), BUS_DMASYNC_POSTWRITE);
	*asqe = *sqe;
	asqe->cid = cid;
	bus_dmamap_sync(sc->sc_dmat, NVME_DMA_MAP(q->q_sq_dmamem),
	    sizeof(*sqe) * tail, sizeof(*sqe), BUS_DMASYNC_PREWRITE);

	sc->sc_ops->op_sq_leave_locked(sc, q, /* XXX ccb */ NULL);

	/* wait for completion */
	acqe += q->q_cq_head;
	for (;;) {
		nvme_dmamem_sync(sc, q->q_cq_dmamem, BUS_DMASYNC_POSTREAD);
		flags = lemtoh16(&acqe->flags);
		if ((flags & NVME_CQE_PHASE) == q->q_cq_phase)
			break;

		delay(10);
	}

	if (++q->q_cq_head >= q->q_entries) {
		q->q_cq_head = 0;
		q->q_cq_phase ^= NVME_CQE_PHASE;
	}
	nvme_write4(sc, q->q_cqhdbl, q->q_cq_head);
	if ((NVME_CQE_SC(flags) != NVME_CQE_SC_SUCCESS) || (acqe->cid != cid))
		return (EIO);

	return (0);
}

int
nvme_hibernate_io(dev_t dev, daddr_t blkno, vaddr_t addr, size_t size,
    int op, void *page)
{
	struct nvme_hibernate_page {
		u_int64_t		prpl[MAXPHYS / PAGE_SIZE];

		struct nvme_softc	*sc;
		int			nsid;
		int			sq_tail;
		int			cq_head;
		int			cqe_phase;

		daddr_t			poffset;
		size_t			psize;
		u_int32_t		secsize;
	} *my = page;
	struct nvme_sqe_io *isqe;
	struct nvme_cqe *icqe;
	paddr_t data_phys, page_phys;
	u_int64_t data_bus_phys, page_bus_phys;
	u_int16_t flags;
	int i;
	int error;

	if (op == HIB_INIT) {
		struct device *disk;
		struct device *scsibus;
		struct nvm_identify_namespace *ns;
		struct nvm_namespace_format *f;
		extern struct cfdriver sd_cd;
		struct scsi_link *link;
		struct scsibus_softc *bus_sc;
		struct nvme_sqe_q qsqe;
		struct nvme_cqe qcqe;

		/* find nvme softc */
		disk = disk_lookup(&sd_cd, DISKUNIT(dev));
		scsibus = disk->dv_parent;
		my->sc = (struct nvme_softc *)disk->dv_parent->dv_parent;

		/* find scsi_link, which tells us the target */
		my->nsid = 0;
		bus_sc = (struct scsibus_softc *)scsibus;
		SLIST_FOREACH(link, &bus_sc->sc_link_list, bus_list) {
			if (link->device_softc == disk) {
				my->nsid = link->target;
				break;
			}
		}
		if (my->nsid == 0)
			return (EIO);
		ns = my->sc->sc_namespaces[my->nsid].ident;
		f = &ns->lbaf[NVME_ID_NS_FLBAS(ns->flbas)];

		my->poffset = blkno;
		my->psize = size;
		my->secsize = 1 << f->lbads;

		memset(NVME_DMA_KVA(my->sc->sc_hib_q->q_cq_dmamem), 0,
		    my->sc->sc_hib_q->q_entries * sizeof(struct nvme_cqe));
		memset(NVME_DMA_KVA(my->sc->sc_hib_q->q_sq_dmamem), 0,
		    my->sc->sc_hib_q->q_entries * sizeof(struct nvme_sqe));

		my->sq_tail = 0;
		my->cq_head = 0;
		my->cqe_phase = NVME_CQE_PHASE;

		memset(&qsqe, 0, sizeof(qsqe));
		qsqe.opcode = NVM_ADMIN_ADD_IOCQ;
		htolem64(&qsqe.prp1,
		    NVME_DMA_DVA(my->sc->sc_hib_q->q_cq_dmamem));
		htolem16(&qsqe.qsize, my->sc->sc_hib_q->q_entries - 1);
		htolem16(&qsqe.qid, my->sc->sc_hib_q->q_id);
		qsqe.qflags = NVM_SQE_CQ_IEN | NVM_SQE_Q_PC;
		if (nvme_hibernate_admin_cmd(my->sc, (struct nvme_sqe *)&qsqe,
		    &qcqe, 1) != 0)
			return (EIO);

		memset(&qsqe, 0, sizeof(qsqe));
		qsqe.opcode = NVM_ADMIN_ADD_IOSQ;
		htolem64(&qsqe.prp1,
		    NVME_DMA_DVA(my->sc->sc_hib_q->q_sq_dmamem));
		htolem16(&qsqe.qsize, my->sc->sc_hib_q->q_entries - 1);
		htolem16(&qsqe.qid, my->sc->sc_hib_q->q_id);
		htolem16(&qsqe.cqid, my->sc->sc_hib_q->q_id);
		qsqe.qflags = NVM_SQE_Q_PC;
		if (nvme_hibernate_admin_cmd(my->sc, (struct nvme_sqe *)&qsqe,
		    &qcqe, 2) != 0)
			return (EIO);

		return (0);
	}

	if (op != HIB_W)
		return (0);

	if (blkno + (size / DEV_BSIZE) > my->psize)
		return E2BIG;

	isqe = NVME_DMA_KVA(my->sc->sc_hib_q->q_sq_dmamem);
	isqe += my->sq_tail;
	if (++my->sq_tail == my->sc->sc_hib_q->q_entries)
		my->sq_tail = 0;

	memset(isqe, 0, sizeof(*isqe));
	isqe->opcode = NVM_CMD_WRITE;
	htolem32(&isqe->nsid, my->nsid);

	pmap_extract(pmap_kernel(), addr, &data_phys);
	data_bus_phys = data_phys;
	htolem64(&isqe->entry.prp[0], data_bus_phys);
	if ((size > my->sc->sc_mps) && (size <= my->sc->sc_mps * 2)) {
		htolem64(&isqe->entry.prp[1], data_bus_phys + my->sc->sc_mps);
	} else if (size > my->sc->sc_mps * 2) {
		pmap_extract(pmap_kernel(), (vaddr_t)page, &page_phys);
		page_bus_phys = page_phys;
		htolem64(&isqe->entry.prp[1], page_bus_phys +
		    offsetof(struct nvme_hibernate_page, prpl));
		for (i = 1; i < howmany(size, my->sc->sc_mps); i++) {
			htolem64(&my->prpl[i - 1], data_bus_phys +
			    (i * my->sc->sc_mps));
		}
	}

	isqe->slba = (blkno + my->poffset) / (my->secsize / DEV_BSIZE);
	isqe->nlb = (size / my->secsize) - 1;
	isqe->cid = blkno % 0xffff;

	nvme_write4(my->sc, NVME_SQTDBL(NVME_HIB_Q, my->sc->sc_dstrd),
	    my->sq_tail);
	nvme_barrier(my->sc, NVME_SQTDBL(NVME_HIB_Q, my->sc->sc_dstrd), 4,
	    BUS_SPACE_BARRIER_WRITE);

	error = 0;

	icqe = NVME_DMA_KVA(my->sc->sc_hib_q->q_cq_dmamem);
	icqe += my->cq_head;

	nvme_dmamem_sync(my->sc, my->sc->sc_hib_q->q_cq_dmamem,
	    BUS_DMASYNC_POSTREAD);
	for (;;) {
		flags = lemtoh16(&icqe->flags);
		if ((flags & NVME_CQE_PHASE) == my->cqe_phase) {
			if ((NVME_CQE_SC(flags) != NVME_CQE_SC_SUCCESS) ||
			    (icqe->cid != blkno % 0xffff))
				error = EIO;

			break;
		}

		delay(1);
		nvme_dmamem_sync(my->sc, my->sc->sc_hib_q->q_cq_dmamem,
		    BUS_DMASYNC_PREREAD|BUS_DMASYNC_POSTREAD);
	}
	nvme_dmamem_sync(my->sc, my->sc->sc_hib_q->q_cq_dmamem,
	    BUS_DMASYNC_PREREAD);

	if (++my->cq_head == my->sc->sc_hib_q->q_entries) {
		my->cq_head = 0;
		my->cqe_phase ^= NVME_CQE_PHASE;
	}

	nvme_write4(my->sc, NVME_CQHDBL(NVME_HIB_Q, my->sc->sc_dstrd),
	    my->cq_head);
	nvme_barrier(my->sc, NVME_CQHDBL(NVME_HIB_Q, my->sc->sc_dstrd), 4,
	    BUS_SPACE_BARRIER_WRITE);

	return (error);
}

#endif

#if NBIO > 0
int
nvme_bioctl(struct device *self, u_long cmd, caddr_t data)
{
	struct nvme_softc	*sc = (struct nvme_softc *)self;
	struct nvme_pt_cmd	*pt;
	int			 error = 0;

	rw_enter_write(&sc->sc_lock);

	switch (cmd) {
	case BIOCINQ:
		error = nvme_bioctl_inq(sc, (struct bioc_inq *)data);
		break;
	case BIOCVOL:
		error = nvme_bioctl_vol(sc, (struct bioc_vol *)data);
		break;
	case BIOCDISK:
		error = nvme_bioctl_disk(sc, (struct bioc_disk *)data);
		break;
	case NVME_PASSTHROUGH_CMD:
		pt = (struct nvme_pt_cmd *)data;
		error = nvme_passthrough_cmd(sc, pt, sc->sc_dev.dv_unit, -1);
		break;
	default:
		printf("nvme_bioctl() Unknown command (%lu)\n", cmd);
		error = ENOTTY;
	}

	rw_exit_write(&sc->sc_lock);

	return error;
}

void
nvme_bio_status(struct bio_status *bs, const char *fmt, ...)
{
	va_list			ap;

	va_start(ap, fmt);
	bio_status(bs, 0, BIO_MSG_INFO, fmt, &ap);
	va_end(ap);
}

const char *
nvme_bioctl_sdname(const struct nvme_softc *sc, int target)
{
	const struct scsi_link		*link;
	const struct sd_softc		*sd;

	link = scsi_get_link(sc->sc_scsibus, target, 0);
	if (link == NULL)
		return NULL;
	sd = (struct sd_softc *)(link->device_softc);
	if (ISSET(link->state, SDEV_S_DYING) || sd == NULL ||
	    ISSET(sd->flags, SDF_DYING))
		return NULL;

	if (nvme_read4(sc, NVME_VS) == 0xffffffff)
		return NULL;

	return DEVNAME(sd);
}

int
nvme_bioctl_inq(struct nvme_softc *sc, struct bioc_inq *bi)
{
	char				 sn[41], mn[81], fr[17];
	struct nvm_identify_controller	*idctrl = &sc->sc_identify;
	struct bio_status		*bs;
	unsigned int			 nn;
	uint32_t			 cc, csts, vs;

	/* Don't tell bioctl about namespaces > last configured namespace. */
	for (nn = sc->sc_nn; nn > 0; nn--) {
		if (sc->sc_namespaces[nn].ident)
			break;
	}
	bi->bi_novol = bi->bi_nodisk = nn;
	strlcpy(bi->bi_dev, DEVNAME(sc), sizeof(bi->bi_dev));

	bs = &bi->bi_bio.bio_status;
	bio_status_init(bs, &sc->sc_dev);
	bs->bs_status = BIO_STATUS_SUCCESS;

	scsi_strvis(sn, idctrl->sn, sizeof(idctrl->sn));
	scsi_strvis(mn, idctrl->mn, sizeof(idctrl->mn));
	scsi_strvis(fr, idctrl->fr, sizeof(idctrl->fr));

	nvme_bio_status(bs, "%s, %s, %s", mn, fr, sn);
	nvme_bio_status(bs, "Max i/o %zu bytes%s%s%s, Sanitize 0x%b",
	    sc->sc_mdts,
	    ISSET(idctrl->lpa, NVM_ID_CTRL_LPA_PE) ?
	    ", Persistent Event Log" : "",
	    ISSET(idctrl->fna, NVM_ID_CTRL_FNA_CRYPTOFORMAT) ?
	    ", CryptoFormat" : "",
	    ISSET(idctrl->vwc, NVM_ID_CTRL_VWC_PRESENT) ?
	    ", Volatile Write Cache" : "",
	    lemtoh32(&idctrl->sanicap), NVM_ID_CTRL_SANICAP_FMT
	);

	if (idctrl->ctratt != 0)
		nvme_bio_status(bs, "Features 0x%b", lemtoh32(&idctrl->ctratt),
		    NVM_ID_CTRL_CTRATT_FMT);

	if (idctrl->oacs || idctrl->oncs) {
		nvme_bio_status(bs, "Admin commands 0x%b, NVM commands 0x%b",
		    lemtoh16(&idctrl->oacs), NVM_ID_CTRL_OACS_FMT,
		    lemtoh16(&idctrl->oncs), NVM_ID_CTRL_ONCS_FMT);
	}

	cc = nvme_read4(sc, NVME_CC);
	csts = nvme_read4(sc, NVME_CSTS);
	vs = nvme_read4(sc, NVME_VS);

	if (vs == 0xffffffff) {
		nvme_bio_status(bs, "Invalid PCIe register mapping");
		return 0;
	}

	nvme_bio_status(bs, "NVMe %u.%u%s%s%sabled, %sReady%s%s%s%s",
	    NVME_VS_MJR(vs), NVME_VS_MNR(vs),
	    (NVME_CC_CSS_R(cc) == NVME_CC_CSS_NVM) ? ", NVM I/O command set" : "",
	    (NVME_CC_CSS_R(cc) == 0x7) ? ", Admin command set only" : "",
	    ISSET(cc, NVME_CC_EN) ? ", En" : "Dis",
	    ISSET(csts, NVME_CSTS_RDY) ? "" : "Not ",
	    ISSET(csts, NVME_CSTS_CFS) ? ", Fatal Error, " : "",
	    (NVME_CC_SHN_R(cc) == NVME_CC_SHN_NORMAL) ? ", Normal shutdown" : "",
	    (NVME_CC_SHN_R(cc) == NVME_CC_SHN_ABRUPT) ? ", Abrupt shutdown" : "",
	    ISSET(csts, NVME_CSTS_SHST_DONE) ? " complete" : "");

	return 0;
}

int
nvme_bioctl_vol(struct nvme_softc *sc, struct bioc_vol *bv)
{
	const struct nvm_identify_namespace	*idns;
	const char				*sd;
	int					 target;
	unsigned int 				 lbaf;

	target = bv->bv_volid + 1;
	if (target > sc->sc_nn) {
		bv->bv_status = BIOC_SVINVALID;
		return 0;
	}

	bv->bv_level = 'c';
	bv->bv_nodisk = 1;

	idns = sc->sc_namespaces[target].ident;
	if (idns == NULL) {
		bv->bv_status = BIOC_SVINVALID;
		return 0;
	}

	lbaf = NVME_ID_NS_FLBAS(idns->flbas);
	if (idns->nlbaf > 16)
		lbaf |= (idns->flbas >> 1) & 0x3f;
	bv->bv_size = nvme_scsi_size(idns) << idns->lbaf[lbaf].lbads;

	sd = nvme_bioctl_sdname(sc, target);
	if (sd) {
		strlcpy(bv->bv_dev, sd, sizeof(bv->bv_dev));
		bv->bv_status = BIOC_SVONLINE;
	} else
		bv->bv_status = BIOC_SVOFFLINE;

	return 0;
}

int
nvme_bioctl_disk(struct nvme_softc *sc, struct bioc_disk *bd)
{
	const char 			*rpdesc[4] = {
		" (Best)",
		" (Better)",
		" (Good)",
		" (Degraded)"
	};
	const char			*protection[4] = {
		"not enabled",
		"Type 1",
		"Type 2",
		"Type 3",
	};
	char				 buf[32], msg[BIO_MSG_LEN];
	struct nvm_identify_namespace	*idns;
	struct bio_status		*bs;
	uint64_t			 id1, id2;
	unsigned int			 i, lbaf, target;
	uint16_t			 ms;
	uint8_t				 dps;

	target = bd->bd_volid + 1;
	if (target > sc->sc_nn)
		return EINVAL;
	bd->bd_channel = sc->sc_scsibus->sc_dev.dv_unit;
	bd->bd_target = target;
	bd->bd_lun = 0;
	snprintf(bd->bd_procdev, sizeof(bd->bd_procdev), "Namespace %u", target);

	bs = &bd->bd_bio.bio_status;
	bs->bs_status = BIO_STATUS_SUCCESS;
	snprintf(bs->bs_controller, sizeof(bs->bs_controller), "%11u",
	    bd->bd_diskid);

	idns = sc->sc_namespaces[target].ident;
	if (idns == NULL) {
		bd->bd_status = BIOC_SDUNUSED;
		return 0;
	}

	lbaf = NVME_ID_NS_FLBAS(idns->flbas);
	if (idns->nlbaf > nitems(idns->lbaf))
		lbaf |= (idns->flbas >> 1) & 0x3f;
	bd->bd_size = lemtoh64(&idns->nsze) << idns->lbaf[lbaf].lbads;

	if (memcmp(idns->nguid, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16)) {
		memcpy(&id1, idns->nguid, sizeof(uint64_t));
		memcpy(&id2, idns->nguid + sizeof(uint64_t), sizeof(uint64_t));
		snprintf(bd->bd_serial, sizeof(bd->bd_serial), "%08llx%08llx",
		    id1, id2);
	} else if (memcmp(idns->eui64, "\0\0\0\0\0\0\0\0", 8)) {
		memcpy(&id1, idns->eui64, sizeof(uint64_t));
		snprintf(bd->bd_serial, sizeof(bd->bd_serial), "%08llx", id1);
	}

	msg[0] = '\0';
	for (i = 0; i <= idns->nlbaf; i++) {
		if (idns->lbaf[i].lbads == 0)
			continue;
		snprintf(buf, sizeof(buf), "%s%s%u",
		    strlen(msg) ? ", " : "", (i == lbaf) ? "*" : "",
		    1 << idns->lbaf[i].lbads);
		strlcat(msg, buf, sizeof(msg));
		ms = lemtoh16(&idns->lbaf[i].ms);
		if (ms) {
			snprintf(buf, sizeof(buf), "+%u", ms);
			strlcat(msg, buf, sizeof(msg));
		}
		strlcat(msg, rpdesc[idns->lbaf[i].rp], sizeof(msg));
	}
	nvme_bio_status(bs, "Formats %s", msg);

	if (idns->nsfeat)
		nvme_bio_status(bs, "Features 0x%b", idns->nsfeat,
		    NVME_ID_NS_NSFEAT_FMT);

	if (idns->dps) {
		dps = idns->dps;
		snprintf(msg, sizeof(msg), "Data Protection (0x%02x) "
		    "Protection Data in ", dps);
		if (ISSET(dps, NVME_ID_NS_DPS_PIP))
			strlcat(msg, "first", sizeof(msg));
		else
			strlcat(msg, "last", sizeof(msg));
		strlcat(msg, "bytes of metadata, Protection ", sizeof(msg));
		if (NVME_ID_NS_DPS_TYPE(dps) >= nitems(protection))
			strlcat(msg, "Type unknown", sizeof(msg));
		else
			strlcat(msg, protection[NVME_ID_NS_DPS_TYPE(dps)],
			    sizeof(msg));
		nvme_bio_status(bs, "%s", msg);
	}

	if (nvme_bioctl_sdname(sc, target) == NULL)
		bd->bd_status = BIOC_SDOFFLINE;
	else
		bd->bd_status = BIOC_SDONLINE;

	return 0;
}
#endif	/* NBIO > 0 */

#ifndef SMALL_KERNEL
void
nvme_refresh_sensors(void *arg)
{
	struct nvme_softc 		*sc = arg;
	struct nvme_sqe			 sqe;
	struct nvme_dmamem		*mem = NULL;
	struct nvme_ccb			*ccb = NULL;
	struct nvm_smart_health 	*health;
	uint32_t			 dwlen;
	uint8_t 			 cw;
	int				 flags;
	int64_t				 temp;

	ccb = nvme_ccb_get(sc);
	if (ccb == NULL)
		goto failed;

	mem = nvme_dmamem_alloc(sc, sizeof(*health));
	if (mem == NULL)
		goto failed;
	nvme_dmamem_sync(sc, mem, BUS_DMASYNC_PREREAD);

	dwlen = (sizeof(*health) >> 2) - 1;
	memset(&sqe, 0, sizeof(sqe));
	sqe.opcode = NVM_ADMIN_GET_LOG_PG;
	htolem32(&sqe.nsid, 0xffffffff);
	htolem32(&sqe.cdw10, (dwlen << 16 | NVM_LOG_PAGE_SMART_HEALTH));
	htolem64(&sqe.entry.prp[0], NVME_DMA_DVA(mem));

	ccb->ccb_done = nvme_empty_done;
	ccb->ccb_cookie = &sqe;
	flags = nvme_poll(sc, sc->sc_admin_q, ccb, nvme_sqe_fill, NVME_TIMO_LOG_PAGE);

	nvme_dmamem_sync(sc, mem, BUS_DMASYNC_POSTREAD);

	if (flags != 0)
		goto failed;

	health = NVME_DMA_KVA(mem); 
	cw = health->critical_warning;

	sc->sc_temp_sensor.status = (cw & NVM_HEALTH_CW_TEMP) ?
	    SENSOR_S_CRIT : SENSOR_S_OK;
	temp = letoh16(health->temperature);
	sc->sc_temp_sensor.value = (temp * 1000000) + 150000;

	sc->sc_spare_sensor.status = (cw & NVM_HEALTH_CW_SPARE) ?
	    SENSOR_S_CRIT : SENSOR_S_OK;
	sc->sc_spare_sensor.value = health->avail_spare * 1000;

	sc->sc_usage_sensor.status = SENSOR_S_OK;
	sc->sc_usage_sensor.value = health->percent_used * 1000;
	goto done;

 failed:
	sc->sc_temp_sensor.status = SENSOR_S_UNKNOWN;
	sc->sc_usage_sensor.status = SENSOR_S_UNKNOWN;
	sc->sc_spare_sensor.status = SENSOR_S_UNKNOWN;
 done:
	if (mem != NULL)
		nvme_dmamem_free(sc, mem);
	if (ccb != NULL)
		nvme_ccb_put(sc, ccb);
}
#endif /* SMALL_KERNEL */
