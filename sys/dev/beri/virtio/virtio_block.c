/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

/*
 * BERI virtio block backend driver
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <sys/endian.h>
#include <sys/disk.h>
#include <sys/vnode.h>
#include <sys/fcntl.h>
#include <sys/kthread.h>
#include <sys/buf.h>
#include <sys/mdioctl.h>
#include <sys/namei.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/beri/virtio/virtio.h>
#include <dev/beri/virtio/virtio_mmio_platform.h>
#include <dev/altera/pio/pio.h>
#include <dev/virtio/mmio/virtio_mmio.h>
#include <dev/virtio/block/virtio_blk.h>
#include <dev/virtio/virtio_ids.h>
#include <dev/virtio/virtio_config.h>
#include <dev/virtio/virtio_ring.h>

#include "pio_if.h"

#define DPRINTF(fmt, ...)

/* We use indirect descriptors */
#define	NUM_DESCS	1
#define	NUM_QUEUES	1

#define	VTBLK_BLK_ID_BYTES	20
#define	VTBLK_MAXSEGS		256

struct beri_vtblk_softc {
	struct resource		*res[1];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	struct cdev		*cdev;
	device_t		dev;
	int			opened;
	device_t		pio_recv;
	device_t		pio_send;
	struct vqueue_info	vs_queues[NUM_QUEUES];
	char			ident[VTBLK_BLK_ID_BYTES];
	struct ucred		*cred;
	struct vnode		*vnode;
	struct thread		*vtblk_ktd;
	struct sx		sc_mtx;
	int			beri_mem_offset;
	struct md_ioctl		*mdio;
	struct virtio_blk_config *cfg;
};

static struct resource_spec beri_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

static int
vtblk_rdwr(struct beri_vtblk_softc *sc, struct iovec *iov,
	int cnt, int offset, int operation, int iolen)
{
	struct vnode *vp;
	struct mount *mp;
	struct uio auio;
	int error;

	bzero(&auio, sizeof(auio));

	vp = sc->vnode;

	KASSERT(vp != NULL, ("file not opened"));

	auio.uio_iov = iov;
	auio.uio_iovcnt = cnt;
	auio.uio_offset = offset;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_rw = operation;
	auio.uio_resid = iolen;
	auio.uio_td = curthread;

	if (operation == 0) {
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		error = VOP_READ(vp, &auio, IO_DIRECT, sc->cred);
		VOP_UNLOCK(vp, 0);
	} else {
		(void) vn_start_write(vp, &mp, V_WAIT);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		error = VOP_WRITE(vp, &auio, IO_SYNC, sc->cred);
		VOP_UNLOCK(vp, 0);
		vn_finished_write(mp);
	}

	return (error);
}

static void
vtblk_proc(struct beri_vtblk_softc *sc, struct vqueue_info *vq)
{
	struct iovec iov[VTBLK_MAXSEGS + 2];
	uint16_t flags[VTBLK_MAXSEGS + 2];
	struct virtio_blk_outhdr *vbh;
	struct iovec *tiov;
	uint8_t *status;
	off_t offset;
	int iolen;
	int type;
	int i, n;
	int err;

	n = vq_getchain(sc->beri_mem_offset, vq, iov,
		VTBLK_MAXSEGS + 2, flags);
	KASSERT(n >= 2 && n <= VTBLK_MAXSEGS + 2,
		("wrong n value %d", n));

	tiov = getcopy(iov, n);
	vbh = iov[0].iov_base;

	status = iov[n-1].iov_base;
	KASSERT(iov[n-1].iov_len == 1,
		("iov_len == %d", iov[n-1].iov_len));

	type = be32toh(vbh->type) & ~VIRTIO_BLK_T_BARRIER;
	offset = be64toh(vbh->sector) * DEV_BSIZE;

	iolen = 0;
	for (i = 1; i < (n-1); i++) {
		iolen += iov[i].iov_len;
	}

	switch (type) {
	case VIRTIO_BLK_T_OUT:
	case VIRTIO_BLK_T_IN:
		err = vtblk_rdwr(sc, tiov + 1, i - 1,
			offset, type, iolen);
		break;
	case VIRTIO_BLK_T_GET_ID:
		/* Assume a single buffer */
		strncpy(iov[1].iov_base, sc->ident,
		    MIN(iov[1].iov_len, sizeof(sc->ident)));
		err = 0;
		break;
	case VIRTIO_BLK_T_FLUSH:
		/* Possible? */
	default:
		err = -ENOSYS;
		break;
	}

	if (err < 0) {
		if (err == -ENOSYS) {
			*status = VIRTIO_BLK_S_UNSUPP;
		} else
			*status = VIRTIO_BLK_S_IOERR;
	} else
		*status = VIRTIO_BLK_S_OK;

	free(tiov, M_DEVBUF);
	vq_relchain(vq, iov, n, 1);
}

static int
close_file(struct beri_vtblk_softc *sc, struct thread *td)
{
	int error;

	if (sc->vnode != NULL) {
		vn_lock(sc->vnode, LK_EXCLUSIVE | LK_RETRY);
		sc->vnode->v_vflag &= ~VV_MD;
		VOP_UNLOCK(sc->vnode, 0);
		error = vn_close(sc->vnode, (FREAD|FWRITE),
				sc->cred, td);
		if (error != 0)
			return (error);
		sc->vnode = NULL;
	}

	if (sc->cred != NULL)
		crfree(sc->cred);

	return (0);
}

static int
open_file(struct beri_vtblk_softc *sc, struct thread *td)
{
	struct nameidata nd;
	struct vattr vattr;
	int error;
	int flags;

	flags = (FREAD | FWRITE);
	NDINIT(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE,
		sc->mdio->md_file, td);
	error = vn_open(&nd, &flags, 0, NULL);
	if (error != 0)
		return (error);
	NDFREE(&nd, NDF_ONLY_PNBUF);

	if (nd.ni_vp->v_type != VREG) {
		return (EINVAL);
	}

	error = VOP_GETATTR(nd.ni_vp, &vattr, td->td_ucred);
	if (error != 0)
		return (error);

	if (VOP_ISLOCKED(nd.ni_vp) != LK_EXCLUSIVE) {
		vn_lock(nd.ni_vp, LK_UPGRADE | LK_RETRY);
		if (nd.ni_vp->v_iflag & VI_DOOMED) {
			return (1);
		}
	}
	nd.ni_vp->v_vflag |= VV_MD;
	VOP_UNLOCK(nd.ni_vp, 0);

	sc->vnode = nd.ni_vp;
	sc->cred = crhold(td->td_ucred);

	return (0);
}

static int
vtblk_notify(struct beri_vtblk_softc *sc)
{
	struct vqueue_info *vq;
	int queue;
	int reg;

	vq = &sc->vs_queues[0];
	if (!vq_ring_ready(vq))
		return (0);

	if (!sc->opened)
		return (0);

	reg = READ2(sc, VIRTIO_MMIO_QUEUE_NOTIFY);
	queue = be16toh(reg);

	KASSERT(queue == 0, ("we support single queue only"));

	/* Process new descriptors */
	vq = &sc->vs_queues[queue];
	vq->vq_save_used = be16toh(vq->vq_used->idx);
	while (vq_has_descs(vq))
		vtblk_proc(sc, vq);

	/* Interrupt the other side */
	if ((be16toh(vq->vq_avail->flags) & VRING_AVAIL_F_NO_INTERRUPT) == 0) {
		reg = htobe32(VIRTIO_MMIO_INT_VRING);
		WRITE4(sc, VIRTIO_MMIO_INTERRUPT_STATUS, reg);
		PIO_SET(sc->pio_send, Q_INTR, 1);
	}

	return (0);
}

static int
vq_init(struct beri_vtblk_softc *sc)
{
	struct vqueue_info *vq;
	uint8_t *base;
	int size;
	int reg;
	int pfn;

	vq = &sc->vs_queues[0];
	vq->vq_qsize = NUM_DESCS;

	reg = READ4(sc, VIRTIO_MMIO_QUEUE_PFN);
	pfn = be32toh(reg);
	vq->vq_pfn = pfn;

	size = vring_size(vq->vq_qsize, VRING_ALIGN);
	base = paddr_map(sc->beri_mem_offset,
		(pfn << PAGE_SHIFT), size);

	/* First pages are descriptors */
	vq->vq_desc = (struct vring_desc *)base;
	base += vq->vq_qsize * sizeof(struct vring_desc);

	/* Then avail ring */
	vq->vq_avail = (struct vring_avail *)base;
	base += (2 + vq->vq_qsize + 1) * sizeof(uint16_t);

	/* Then it's rounded up to the next page */
	base = (uint8_t *)roundup2((uintptr_t)base, VRING_ALIGN);

	/* And the last pages are the used ring */
	vq->vq_used = (struct vring_used *)base;

	/* Mark queue as allocated, and start at 0 when we use it. */
	vq->vq_flags = VQ_ALLOC;
	vq->vq_last_avail = 0;

	return (0);
}


static void
vtblk_thread(void *arg)
{
	struct beri_vtblk_softc *sc;
	int err;

	sc = arg;

	sx_xlock(&sc->sc_mtx);
	for (;;) {
		err = msleep(sc, &sc->sc_mtx, PCATCH | PZERO, "prd", hz);
		vtblk_notify(sc);
	}
	sx_xunlock(&sc->sc_mtx);

	kthread_exit();
}

static int
backend_info(struct beri_vtblk_softc *sc)
{
	struct virtio_blk_config *cfg;
	uint32_t *s;
	int reg;
	int i;

	/* Specify that we provide block device */
	reg = htobe32(VIRTIO_ID_BLOCK);
	WRITE4(sc, VIRTIO_MMIO_DEVICE_ID, reg);

	/* Queue size */
	reg = htobe32(NUM_DESCS);
	WRITE4(sc, VIRTIO_MMIO_QUEUE_NUM_MAX, reg);

	/* Our features */
	reg = htobe32(VIRTIO_RING_F_INDIRECT_DESC
	    | VIRTIO_BLK_F_BLK_SIZE
	    | VIRTIO_BLK_F_SEG_MAX);
	WRITE4(sc, VIRTIO_MMIO_HOST_FEATURES, reg);

	cfg = sc->cfg;
	cfg->capacity = htobe64(sc->mdio->md_mediasize / DEV_BSIZE);
	cfg->size_max = 0; /* not negotiated */
	cfg->seg_max = htobe32(VTBLK_MAXSEGS);
	cfg->blk_size = htobe32(DEV_BSIZE);

	s = (uint32_t *)cfg;

	for (i = 0; i < sizeof(struct virtio_blk_config); i+=4) {
		WRITE4(sc, VIRTIO_MMIO_CONFIG + i, *s);
		s+=1;
	}

	strncpy(sc->ident, "Virtio block backend", sizeof(sc->ident));

	return (0);
}

static void
vtblk_intr(void *arg)
{
	struct beri_vtblk_softc *sc;
	int pending;
	int reg;

	sc = arg;

	reg = PIO_READ(sc->pio_recv);

	/* Ack */
	PIO_SET(sc->pio_recv, reg, 0);

	pending = htobe32(reg);

	if (pending & Q_PFN) {
		vq_init(sc);
	}

	if (pending & Q_NOTIFY) {
		wakeup(sc);
	}
}

static int
beri_ioctl(struct cdev *dev, u_long cmd, caddr_t addr,
		int flags, struct thread *td)
{
	struct beri_vtblk_softc *sc;
	int err;

	sc = dev->si_drv1;

	switch (cmd) {
	case MDIOCATTACH:
		/* take file as argument */
		if (sc->vnode != NULL) {
			/* Already opened */
			return (1);
		}
		sc->mdio = (struct md_ioctl *)addr;
		backend_info(sc);
		DPRINTF("opening file, td 0x%08x\n", (int)td);
		err = open_file(sc, td);
		if (err)
			return (err);
		PIO_SETUP_IRQ(sc->pio_recv, vtblk_intr, sc);
		sc->opened = 1;
		break;
	case MDIOCDETACH:
		if (sc->vnode == NULL) {
			/* File not opened */
			return (1);
		}
		sc->opened = 0;
		DPRINTF("closing file, td 0x%08x\n", (int)td);
		err = close_file(sc, td);
		if (err)
			return (err);
		PIO_TEARDOWN_IRQ(sc->pio_recv);
		break;
	default:
		break;
	}

	return (0);
}

static struct cdevsw beri_cdevsw = {
	.d_version =	D_VERSION,
	.d_ioctl =	beri_ioctl,
	.d_name =	"virtio block backend",
};

static int
beri_vtblk_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "sri-cambridge,beri-vtblk"))
		return (ENXIO);

	device_set_desc(dev, "SRI-Cambridge BERI block");
	return (BUS_PROBE_DEFAULT);
}

static int
beri_vtblk_attach(device_t dev)
{
	struct beri_vtblk_softc *sc;
	int error;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, beri_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	sc->cfg = malloc(sizeof(struct virtio_blk_config),
		M_DEVBUF, M_NOWAIT|M_ZERO);

	sx_init(&sc->sc_mtx, device_get_nameunit(sc->dev));

	error = kthread_add(vtblk_thread, sc, NULL, &sc->vtblk_ktd,
		0, 0, "beri_virtio_block");
	if (error) {
		device_printf(dev, "cannot create kthread\n");
		return (ENXIO);
	}

	if (setup_offset(dev, &sc->beri_mem_offset) != 0)
		return (ENXIO);
	if (setup_pio(dev, "pio-send", &sc->pio_send) != 0)
		return (ENXIO);
	if (setup_pio(dev, "pio-recv", &sc->pio_recv) != 0)
		return (ENXIO);

	sc->cdev = make_dev(&beri_cdevsw, 0, UID_ROOT, GID_WHEEL,
	    S_IRWXU, "beri_vtblk");
	if (sc->cdev == NULL) {
		device_printf(dev, "Failed to create character device.\n");
		return (ENXIO);
	}

	sc->cdev->si_drv1 = sc;
	return (0);
}

static device_method_t beri_vtblk_methods[] = {
	DEVMETHOD(device_probe,		beri_vtblk_probe),
	DEVMETHOD(device_attach,	beri_vtblk_attach),
	{ 0, 0 }
};

static driver_t beri_vtblk_driver = {
	"beri_vtblk",
	beri_vtblk_methods,
	sizeof(struct beri_vtblk_softc),
};

static devclass_t beri_vtblk_devclass;

DRIVER_MODULE(beri_vtblk, simplebus, beri_vtblk_driver,
    beri_vtblk_devclass, 0, 0);
