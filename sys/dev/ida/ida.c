/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999,2000 Jonathan Lemon
 * All rights reserved.
 *
 # Derived from the original IDA Compaq RAID driver, which is
 * Copyright (c) 1996, 1997, 1998, 1999
 *    Mark Dawson and David James. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Generic driver for Compaq SMART RAID adapters.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/stat.h>

#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/endian.h>

#include <machine/bus.h>
#include <sys/rman.h>

#include <geom/geom_disk.h>

#include <dev/ida/idareg.h>
#include <dev/ida/idavar.h>
#include <dev/ida/idaio.h>

/* prototypes */
static int ida_alloc_qcbs(struct ida_softc *ida);
static void ida_done(struct ida_softc *ida, struct ida_qcb *qcb);
static void ida_start(struct ida_softc *ida);
static void ida_startio(struct ida_softc *ida);
static void ida_startup(void *arg);
static void ida_timeout(void *arg);
static int ida_wait(struct ida_softc *ida, struct ida_qcb *qcb);

static d_ioctl_t ida_ioctl;
static struct cdevsw ida_cdevsw = {
	.d_version =	D_VERSION,
	.d_ioctl =	ida_ioctl,
	.d_name =	"ida",
};

void
ida_free(struct ida_softc *ida)
{
	int i;

	if (ida->ih != NULL)
		bus_teardown_intr(ida->dev, ida->irq, ida->ih);

	mtx_lock(&ida->lock);
	callout_stop(&ida->ch);
	mtx_unlock(&ida->lock);
	callout_drain(&ida->ch);

	if (ida->buffer_dmat) {
		for (i = 0; i < IDA_QCB_MAX; i++)
			bus_dmamap_destroy(ida->buffer_dmat, ida->qcbs[i].dmamap);
		bus_dma_tag_destroy(ida->buffer_dmat);
	}

	if (ida->hwqcb_dmat) {
		if (ida->hwqcb_busaddr)
			bus_dmamap_unload(ida->hwqcb_dmat, ida->hwqcb_dmamap);
		if (ida->hwqcbs)
			bus_dmamem_free(ida->hwqcb_dmat, ida->hwqcbs,
			    ida->hwqcb_dmamap);
		bus_dma_tag_destroy(ida->hwqcb_dmat);
	}

	if (ida->qcbs != NULL)
		free(ida->qcbs, M_DEVBUF);

	if (ida->irq != NULL)
		bus_release_resource(ida->dev, ida->irq_res_type,
		    0, ida->irq);

	if (ida->parent_dmat != NULL)
		bus_dma_tag_destroy(ida->parent_dmat);

	if (ida->regs != NULL)
		bus_release_resource(ida->dev, ida->regs_res_type,
		    ida->regs_res_id, ida->regs);

	mtx_destroy(&ida->lock);
}

/*
 * record bus address from bus_dmamap_load
 */
static void
ida_dma_map_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *baddr;

	baddr = (bus_addr_t *)arg;
	*baddr = segs->ds_addr;
}

static __inline struct ida_qcb *
ida_get_qcb(struct ida_softc *ida)
{
	struct ida_qcb *qcb;

	if ((qcb = SLIST_FIRST(&ida->free_qcbs)) != NULL) {
		SLIST_REMOVE_HEAD(&ida->free_qcbs, link.sle);
		bzero(qcb->hwqcb, sizeof(struct ida_hdr) + sizeof(struct ida_req));
	}
	return (qcb);
}

static __inline void
ida_free_qcb(struct ida_softc *ida, struct ida_qcb *qcb)
{

	qcb->state = QCB_FREE;
	qcb->buf = NULL;
	qcb->error = 0;
	SLIST_INSERT_HEAD(&ida->free_qcbs, qcb, link.sle);
}

static __inline bus_addr_t
idahwqcbvtop(struct ida_softc *ida, struct ida_hardware_qcb *hwqcb)
{
	return (ida->hwqcb_busaddr +
	    ((bus_addr_t)hwqcb - (bus_addr_t)ida->hwqcbs));
}

static __inline struct ida_qcb *
idahwqcbptov(struct ida_softc *ida, bus_addr_t hwqcb_addr)
{
	struct ida_hardware_qcb *hwqcb;

	hwqcb = (struct ida_hardware_qcb *)
	    ((bus_addr_t)ida->hwqcbs + (hwqcb_addr - ida->hwqcb_busaddr));
	return (hwqcb->qcb);
}

static int
ida_alloc_qcbs(struct ida_softc *ida)
{
	struct ida_qcb *qcb;
	int error, i;

	for (i = 0; i < IDA_QCB_MAX; i++) {
		qcb = &ida->qcbs[i];

		error = bus_dmamap_create(ida->buffer_dmat, /*flags*/0, &qcb->dmamap);
		if (error != 0)
			return (error);

		qcb->ida = ida;
		qcb->flags = QCB_FREE;
		qcb->hwqcb = &ida->hwqcbs[i];
		qcb->hwqcb->qcb = qcb;
		qcb->hwqcb_busaddr = idahwqcbvtop(ida, qcb->hwqcb);
		SLIST_INSERT_HEAD(&ida->free_qcbs, qcb, link.sle);
	}
	return (0);
}

int
ida_setup(struct ida_softc *ida)
{
	struct ida_controller_info cinfo;
	device_t child;
	int error, i, unit;

	SLIST_INIT(&ida->free_qcbs);
	STAILQ_INIT(&ida->qcb_queue);
	bioq_init(&ida->bio_queue);

	ida->qcbs = (struct ida_qcb *)
	    malloc(IDA_QCB_MAX * sizeof(struct ida_qcb), M_DEVBUF,
		M_NOWAIT | M_ZERO);
	if (ida->qcbs == NULL)
		return (ENOMEM);

	/*
	 * Create our DMA tags
	 */

	/* DMA tag for our hardware QCB structures */
	error = bus_dma_tag_create(
		/* parent	*/ ida->parent_dmat,
		/* alignment	*/ 1,
		/* boundary	*/ 0,
		/* lowaddr	*/ BUS_SPACE_MAXADDR,
		/* highaddr	*/ BUS_SPACE_MAXADDR,
		/* filter	*/ NULL,
		/* filterarg	*/ NULL,
		/* maxsize	*/ IDA_QCB_MAX * sizeof(struct ida_hardware_qcb),
		/* nsegments	*/ 1,
		/* maxsegsz	*/ BUS_SPACE_MAXSIZE_32BIT,
		/* flags	*/ 0,
		/* lockfunc	*/ NULL,
		/* lockarg	*/ NULL,
		&ida->hwqcb_dmat);
	if (error)
		return (ENOMEM);

	/* DMA tag for mapping buffers into device space */
	error = bus_dma_tag_create(
		/* parent 	*/ ida->parent_dmat,
		/* alignment	*/ 1,
		/* boundary	*/ 0,
		/* lowaddr	*/ BUS_SPACE_MAXADDR,
		/* highaddr	*/ BUS_SPACE_MAXADDR,
		/* filter	*/ NULL,
		/* filterarg	*/ NULL,
		/* maxsize	*/ DFLTPHYS,
		/* nsegments	*/ IDA_NSEG,
		/* maxsegsz	*/ BUS_SPACE_MAXSIZE_32BIT,
		/* flags	*/ 0,
		/* lockfunc	*/ busdma_lock_mutex,
		/* lockarg	*/ &Giant,
		&ida->buffer_dmat);
	if (error)
		return (ENOMEM);

	/* Allocation of hardware QCBs */
	/* XXX allocation is rounded to hardware page size */
	error = bus_dmamem_alloc(ida->hwqcb_dmat,
	    (void **)&ida->hwqcbs, BUS_DMA_NOWAIT, &ida->hwqcb_dmamap);
	if (error)
		return (ENOMEM);

	/* And permanently map them in */
	bus_dmamap_load(ida->hwqcb_dmat, ida->hwqcb_dmamap,
	    ida->hwqcbs, IDA_QCB_MAX * sizeof(struct ida_hardware_qcb),
	    ida_dma_map_cb, &ida->hwqcb_busaddr, /*flags*/0);

	bzero(ida->hwqcbs, IDA_QCB_MAX * sizeof(struct ida_hardware_qcb));

	error = ida_alloc_qcbs(ida);
	if (error)
		return (error);

	mtx_lock(&ida->lock);
	ida->cmd.int_enable(ida, 0);

	error = ida_command(ida, CMD_GET_CTRL_INFO, &cinfo, sizeof(cinfo),
	    IDA_CONTROLLER, 0, DMA_DATA_IN);
	if (error) {
		mtx_unlock(&ida->lock);
		device_printf(ida->dev, "CMD_GET_CTRL_INFO failed.\n");
		return (error);
	}

	device_printf(ida->dev, "drives=%d firm_rev=%c%c%c%c\n",
	    cinfo.num_drvs, cinfo.firm_rev[0], cinfo.firm_rev[1],
	    cinfo.firm_rev[2], cinfo.firm_rev[3]);

	if (ida->flags & IDA_FIRMWARE) {
		int data;

		error = ida_command(ida, CMD_START_FIRMWARE,
		    &data, sizeof(data), IDA_CONTROLLER, 0, DMA_DATA_IN);
		if (error) {
			mtx_unlock(&ida->lock);
			device_printf(ida->dev, "CMD_START_FIRMWARE failed.\n");
			return (error);
		}
	}
	
	ida->cmd.int_enable(ida, 1);
	ida->flags |= IDA_ATTACHED;
	mtx_unlock(&ida->lock);

	for (i = 0; i < cinfo.num_drvs; i++) {
		child = device_add_child(ida->dev, /*"idad"*/NULL, -1);
		if (child != NULL)
			device_set_ivars(child, (void *)(intptr_t)i);
	}

	ida->ich.ich_func = ida_startup;
	ida->ich.ich_arg = ida;
	if (config_intrhook_establish(&ida->ich) != 0) {
		device_delete_children(ida->dev);
		device_printf(ida->dev, "Cannot establish configuration hook\n");
		return (error);
	}

	unit = device_get_unit(ida->dev);
	ida->ida_dev_t = make_dev(&ida_cdevsw, unit,
				 UID_ROOT, GID_OPERATOR, S_IRUSR | S_IWUSR,
				 "ida%d", unit);
	ida->ida_dev_t->si_drv1 = ida;

	return (0);
}

static void
ida_startup(void *arg)
{
	struct ida_softc *ida;

	ida = arg;

	config_intrhook_disestablish(&ida->ich);

	mtx_lock(&Giant);
	bus_generic_attach(ida->dev);
	mtx_unlock(&Giant);
}

int
ida_detach(device_t dev)
{
	struct ida_softc *ida;
	int error;

	ida = (struct ida_softc *)device_get_softc(dev);

	error = bus_generic_detach(dev);
	if (error)
		return (error);
	error = device_delete_children(dev);
	if (error)
		return (error);

	/*
	 * XXX
	 * before detaching, we must make sure that the system is
	 * quiescent; nothing mounted, no pending activity.
	 */

	/*
	 * XXX
	 * now, how are we supposed to maintain a list of our drives?
	 * iterate over our "child devices"?
	 */

	destroy_dev(ida->ida_dev_t);
	ida_free(ida);
	return (error);
}

static void
ida_data_cb(void *arg, bus_dma_segment_t *segs, int nsegments, int error)
{
	struct ida_hardware_qcb *hwqcb;
	struct ida_softc *ida;
	struct ida_qcb *qcb;
	bus_dmasync_op_t op;
	int i;

	qcb = arg;
	ida = qcb->ida;
	if (!dumping)
		mtx_assert(&ida->lock, MA_OWNED);
	if (error) {
		qcb->error = error;
		ida_done(ida, qcb);
		return;
	}

	hwqcb = qcb->hwqcb;
	hwqcb->hdr.size = htole16((sizeof(struct ida_req) +
	    sizeof(struct ida_sgb) * IDA_NSEG) >> 2);

	for (i = 0; i < nsegments; i++) {
		hwqcb->seg[i].addr = htole32(segs[i].ds_addr);
		hwqcb->seg[i].length = htole32(segs[i].ds_len);
	}
	hwqcb->req.sgcount = nsegments;
	if (qcb->flags & DMA_DATA_TRANSFER) {
		switch (qcb->flags & DMA_DATA_TRANSFER) {
		case DMA_DATA_TRANSFER:
			op = BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE;
			break;
		case DMA_DATA_IN:
			op = BUS_DMASYNC_PREREAD;
			break;
		default:
			KASSERT((qcb->flags & DMA_DATA_TRANSFER) ==
			    DMA_DATA_OUT, ("bad DMA data flags"));
			op = BUS_DMASYNC_PREWRITE;
			break;
		}
		bus_dmamap_sync(ida->buffer_dmat, qcb->dmamap, op);
	}
	bus_dmamap_sync(ida->hwqcb_dmat, ida->hwqcb_dmamap,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

	STAILQ_INSERT_TAIL(&ida->qcb_queue, qcb, link.stqe);
	ida_start(ida);
	ida->flags &= ~IDA_QFROZEN;
}

static int
ida_map_qcb(struct ida_softc *ida, struct ida_qcb *qcb, void *data,
    bus_size_t datasize)
{
	int error, flags;

	if (ida->flags & IDA_INTERRUPTS)
		flags = BUS_DMA_WAITOK;
	else
		flags = BUS_DMA_NOWAIT;
	error = bus_dmamap_load(ida->buffer_dmat, qcb->dmamap, data, datasize,
	    ida_data_cb, qcb, flags);
	if (error == EINPROGRESS) {
		ida->flags |= IDA_QFROZEN;
		error = 0;
	}
	return (error);
}

int
ida_command(struct ida_softc *ida, int command, void *data, int datasize,
	int drive, u_int32_t pblkno, int flags)
{
	struct ida_hardware_qcb *hwqcb;
	struct ida_qcb *qcb;
	int error;

	if (!dumping)
		mtx_assert(&ida->lock, MA_OWNED);
	qcb = ida_get_qcb(ida);

	if (qcb == NULL) {
		device_printf(ida->dev, "out of QCBs\n");
		return (EAGAIN);
	}

	qcb->flags = flags | IDA_COMMAND;
	hwqcb = qcb->hwqcb;
	hwqcb->hdr.drive = drive;
	hwqcb->req.blkno = htole32(pblkno);
	hwqcb->req.bcount = htole16(howmany(datasize, DEV_BSIZE));
	hwqcb->req.command = command;

	error = ida_map_qcb(ida, qcb, data, datasize);
	if (error == 0) {
		error = ida_wait(ida, qcb);
		/* Don't free QCB on a timeout in case it later completes. */
		if (error)
			return (error);
		error = qcb->error;
	}

	/* XXX should have status returned here? */
	/* XXX have "status pointer" area in QCB? */

	ida_free_qcb(ida, qcb);
	return (error);
}

void
ida_submit_buf(struct ida_softc *ida, struct bio *bp)
{
	mtx_lock(&ida->lock);
	bioq_insert_tail(&ida->bio_queue, bp);
	ida_startio(ida);
	mtx_unlock(&ida->lock);
}

static void
ida_startio(struct ida_softc *ida)
{
	struct ida_hardware_qcb *hwqcb;
	struct ida_qcb *qcb;
	struct idad_softc *drv;
	struct bio *bp;
	int error;

	mtx_assert(&ida->lock, MA_OWNED);
	for (;;) {
		if (ida->flags & IDA_QFROZEN)
			return;
		bp = bioq_first(&ida->bio_queue);
		if (bp == NULL)
			return;				/* no more buffers */

		qcb = ida_get_qcb(ida);
		if (qcb == NULL)
			return;				/* out of resources */

		bioq_remove(&ida->bio_queue, bp);
		qcb->buf = bp;
		qcb->flags = bp->bio_cmd == BIO_READ ? DMA_DATA_IN : DMA_DATA_OUT;

		hwqcb = qcb->hwqcb;
		drv = bp->bio_driver1;
		hwqcb->hdr.drive = drv->drive;
		hwqcb->req.blkno = bp->bio_pblkno;
		hwqcb->req.bcount = howmany(bp->bio_bcount, DEV_BSIZE);
		hwqcb->req.command = bp->bio_cmd == BIO_READ ? CMD_READ : CMD_WRITE;

		error = ida_map_qcb(ida, qcb, bp->bio_data, bp->bio_bcount);
		if (error) {
			qcb->error = error;
			ida_done(ida, qcb);
		}
	}
}

static void
ida_start(struct ida_softc *ida)
{
	struct ida_qcb *qcb;

	if (!dumping)
		mtx_assert(&ida->lock, MA_OWNED);
	while ((qcb = STAILQ_FIRST(&ida->qcb_queue)) != NULL) {
		if (ida->cmd.fifo_full(ida))
			break;
		STAILQ_REMOVE_HEAD(&ida->qcb_queue, link.stqe);
		/*
		 * XXX
		 * place the qcb on an active list?
		 */

		/* Set a timeout. */
		if (!ida->qactive && !dumping)
			callout_reset(&ida->ch, hz * 5, ida_timeout, ida);
		ida->qactive++;

		qcb->state = QCB_ACTIVE;
		ida->cmd.submit(ida, qcb);
	}
}

static int
ida_wait(struct ida_softc *ida, struct ida_qcb *qcb)
{
	struct ida_qcb *qcb_done = NULL;
	bus_addr_t completed;
	int delay;

	if (!dumping)
		mtx_assert(&ida->lock, MA_OWNED);
	if (ida->flags & IDA_INTERRUPTS) {
		if (mtx_sleep(qcb, &ida->lock, PRIBIO, "idacmd", 5 * hz)) {
			qcb->state = QCB_TIMEDOUT;
			return (ETIMEDOUT);
		}
		return (0);
	}

again:
	delay = 5 * 1000 * 100;			/* 5 sec delay */
	while ((completed = ida->cmd.done(ida)) == 0) {
		if (delay-- == 0) {
			qcb->state = QCB_TIMEDOUT;
			return (ETIMEDOUT);
		}
		DELAY(10);
	}

	qcb_done = idahwqcbptov(ida, completed & ~3);
	if (qcb_done != qcb)
		goto again;
	ida_done(ida, qcb);
	return (0);
}

void
ida_intr(void *data)
{
	struct ida_softc *ida;
	struct ida_qcb *qcb;
	bus_addr_t completed;

	ida = (struct ida_softc *)data;

	mtx_lock(&ida->lock);
	if (ida->cmd.int_pending(ida) == 0) {
		mtx_unlock(&ida->lock);
		return;				/* not our interrupt */
	}

	while ((completed = ida->cmd.done(ida)) != 0) {
		qcb = idahwqcbptov(ida, completed & ~3);

		if (qcb == NULL || qcb->state != QCB_ACTIVE) {
			device_printf(ida->dev,
			    "ignoring completion %jx\n", (intmax_t)completed);
			continue;
		}
		/* Handle "Bad Command List" errors. */
		if ((completed & 3) && (qcb->hwqcb->req.error == 0))
			qcb->hwqcb->req.error = CMD_REJECTED;
		ida_done(ida, qcb);
	}
	ida_startio(ida);
	mtx_unlock(&ida->lock);
}

/*
 * should switch out command type; may be status, not just I/O.
 */
static void
ida_done(struct ida_softc *ida, struct ida_qcb *qcb)
{
	bus_dmasync_op_t op;
	int active, error = 0;

	/*
	 * finish up command
	 */
	if (!dumping)
		mtx_assert(&ida->lock, MA_OWNED);
	active = (qcb->state != QCB_FREE);
	if (qcb->flags & DMA_DATA_TRANSFER && active) {
		switch (qcb->flags & DMA_DATA_TRANSFER) {
		case DMA_DATA_TRANSFER:
			op = BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE;
			break;
		case DMA_DATA_IN:
			op = BUS_DMASYNC_POSTREAD;
			break;
		default:
			KASSERT((qcb->flags & DMA_DATA_TRANSFER) ==
			    DMA_DATA_OUT, ("bad DMA data flags"));
			op = BUS_DMASYNC_POSTWRITE;
			break;
		}
		bus_dmamap_sync(ida->buffer_dmat, qcb->dmamap, op);
		bus_dmamap_unload(ida->buffer_dmat, qcb->dmamap);
	}
	if (active)
		bus_dmamap_sync(ida->hwqcb_dmat, ida->hwqcb_dmamap,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	if (qcb->hwqcb->req.error & SOFT_ERROR) {
		if (qcb->buf)
			device_printf(ida->dev, "soft %s error\n",
				qcb->buf->bio_cmd == BIO_READ ?
					"read" : "write");
		else
			device_printf(ida->dev, "soft error\n");
	}
	if (qcb->hwqcb->req.error & HARD_ERROR) {
		error = 1;
		if (qcb->buf)
			device_printf(ida->dev, "hard %s error\n",
				qcb->buf->bio_cmd == BIO_READ ?
					"read" : "write");
		else
			device_printf(ida->dev, "hard error\n");
	}
	if (qcb->hwqcb->req.error & CMD_REJECTED) {
		error = 1;
		device_printf(ida->dev, "invalid request\n");
	}
	if (qcb->error) {
		error = 1;
		device_printf(ida->dev, "request failed to map: %d\n", qcb->error);
	}

	if (qcb->flags & IDA_COMMAND) {
		if (ida->flags & IDA_INTERRUPTS)
			wakeup(qcb);
		if (qcb->state == QCB_TIMEDOUT)
			ida_free_qcb(ida, qcb);
	} else {
		KASSERT(qcb->buf != NULL, ("ida_done(): qcb->buf is NULL!"));
		if (error)
			qcb->buf->bio_flags |= BIO_ERROR;
		idad_intr(qcb->buf);
		ida_free_qcb(ida, qcb);
	}

	if (!active)
		return;

	ida->qactive--;
	/* Reschedule or cancel timeout */
	if (ida->qactive)
		callout_reset(&ida->ch, hz * 5, ida_timeout, ida);
	else
		callout_stop(&ida->ch);
}

static void
ida_timeout(void *arg)
{
	struct ida_softc *ida;

	ida = (struct ida_softc *)arg;
	device_printf(ida->dev, "%s() qactive %d\n", __func__, ida->qactive);

	if (ida->flags & IDA_INTERRUPTS)
		device_printf(ida->dev, "IDA_INTERRUPTS\n");

	device_printf(ida->dev,	"\t   R_CMD_FIFO: %08x\n"
				"\t  R_DONE_FIFO: %08x\n"
				"\t   R_INT_MASK: %08x\n"
				"\t     R_STATUS: %08x\n"
				"\tR_INT_PENDING: %08x\n",
					ida_inl(ida, R_CMD_FIFO),
					ida_inl(ida, R_DONE_FIFO),
					ida_inl(ida, R_INT_MASK),
					ida_inl(ida, R_STATUS),
					ida_inl(ida, R_INT_PENDING));

	return;
}

/*
 * IOCTL stuff follows.
 */
struct cmd_info {
	int	cmd;
	int	len;
	int	flags;
};
static struct cmd_info *ida_cmd_lookup(int);

static int
ida_ioctl (struct cdev *dev, u_long cmd, caddr_t addr, int32_t flag, struct thread *td)
{
	struct ida_softc *sc;
	struct ida_user_command *uc;
	struct cmd_info *ci;
	int len;
	int flags;
	int error;
	int data;
	void *daddr;

	sc = (struct ida_softc *)dev->si_drv1;
	uc = (struct ida_user_command *)addr;
	error = 0;

	switch (cmd) {
	case IDAIO_COMMAND:
		ci = ida_cmd_lookup(uc->command);
		if (ci == NULL) {
			error = EINVAL;
			break;
		}
		len = ci->len;
		flags = ci->flags;
		if (len)
			daddr = &uc->d.buf;
		else {
			daddr = &data;
			len = sizeof(data);
		}
		mtx_lock(&sc->lock);
		error = ida_command(sc, uc->command, daddr, len,
				    uc->drive, uc->blkno, flags);
		mtx_unlock(&sc->lock);
		break;
	default:
		error = ENOIOCTL;
		break;
	}
	return (error);
}

static struct cmd_info ci_list[] = {
	{ CMD_GET_LOG_DRV_INFO,
			sizeof(struct ida_drive_info), DMA_DATA_IN },
	{ CMD_GET_CTRL_INFO,
			sizeof(struct ida_controller_info), DMA_DATA_IN },
	{ CMD_SENSE_DRV_STATUS,
			sizeof(struct ida_drive_status), DMA_DATA_IN },
	{ CMD_START_RECOVERY,		0, 0 },
	{ CMD_GET_PHYS_DRV_INFO,
			sizeof(struct ida_phys_drv_info), DMA_DATA_TRANSFER },
	{ CMD_BLINK_DRV_LEDS,
			sizeof(struct ida_blink_drv_leds), DMA_DATA_OUT },
	{ CMD_SENSE_DRV_LEDS,
			sizeof(struct ida_blink_drv_leds), DMA_DATA_IN },
	{ CMD_GET_LOG_DRV_EXT,
			sizeof(struct ida_drive_info_ext), DMA_DATA_IN },
	{ CMD_RESET_CTRL,		0, 0 },
	{ CMD_GET_CONFIG,		0, 0 },
	{ CMD_SET_CONFIG,		0, 0 },
	{ CMD_LABEL_LOG_DRV,
			sizeof(struct ida_label_logical), DMA_DATA_OUT },
	{ CMD_SET_SURFACE_DELAY,	0, 0 },
	{ CMD_SENSE_BUS_PARAMS,		0, 0 },
	{ CMD_SENSE_SUBSYS_INFO,	0, 0 },
	{ CMD_SENSE_SURFACE_ATS,	0, 0 },
	{ CMD_PASSTHROUGH,		0, 0 },
	{ CMD_RESET_SCSI_DEV,		0, 0 },
	{ CMD_PAUSE_BG_ACT,		0, 0 },
	{ CMD_RESUME_BG_ACT,		0, 0 },
	{ CMD_START_FIRMWARE,		0, 0 },
	{ CMD_SENSE_DRV_ERR_LOG,	0, 0 },
	{ CMD_START_CPM,		0, 0 },
	{ CMD_SENSE_CP,			0, 0 },
	{ CMD_STOP_CPM,			0, 0 },
	{ CMD_FLUSH_CACHE,		0, 0 },
	{ CMD_ACCEPT_MEDIA_EXCH,	0, 0 },
	{ 0, 0, 0 }
};

static struct cmd_info *
ida_cmd_lookup (int command)
{
	struct cmd_info *ci;

	ci = ci_list;
	while (ci->cmd) {
		if (ci->cmd == command)
			return (ci);
		ci++;
	}
	return (NULL);
}
