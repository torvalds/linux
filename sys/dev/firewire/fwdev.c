/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2003 Hidetoshi Shimokawa
 * Copyright (c) 1998-2002 Katsushi Kobayashi and Hidetoshi Shimokawa
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the acknowledgement as bellow:
 *
 *    This product includes software developed by K. Kobayashi and H. Shimokawa
 *
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/mbuf.h>
#include <sys/bio.h>

#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/poll.h>

#include <sys/bus.h>
#include <sys/ctype.h>
#include <machine/bus.h>

#include <sys/ioccom.h>

#include <dev/firewire/firewire.h>
#include <dev/firewire/firewirereg.h>
#include <dev/firewire/fwdma.h>
#include <dev/firewire/fwmem.h>
#include <dev/firewire/iec68113.h>

#define	FWNODE_INVAL 0xffff

static	d_open_t	fw_open;
static	d_close_t	fw_close;
static	d_ioctl_t	fw_ioctl;
static	d_poll_t	fw_poll;
static	d_read_t	fw_read;	/* for Isochronous packet */
static	d_write_t	fw_write;
static	d_mmap_t	fw_mmap;
static	d_strategy_t	fw_strategy;

struct cdevsw firewire_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	fw_open,
	.d_close =	fw_close,
	.d_read =	fw_read,
	.d_write =	fw_write,
	.d_ioctl =	fw_ioctl,
	.d_poll =	fw_poll,
	.d_mmap =	fw_mmap,
	.d_strategy =	fw_strategy,
	.d_name =	"fw",
};

struct fw_drv1 {
	struct firewire_comm *fc;
	struct fw_xferq *ir;
	struct fw_xferq *it;
	struct fw_isobufreq bufreq;
	STAILQ_HEAD(, fw_bind) binds;
	STAILQ_HEAD(, fw_xfer) rq;
};

static int
fwdev_allocbuf(struct firewire_comm *fc, struct fw_xferq *q,
	struct fw_bufspec *b)
{
	int i;

	if (q->flag & (FWXFERQ_RUNNING | FWXFERQ_EXTBUF))
		return (EBUSY);

	q->bulkxfer = malloc(sizeof(struct fw_bulkxfer) * b->nchunk,
	    M_FW, M_WAITOK);

	b->psize = roundup2(b->psize, sizeof(uint32_t));
	q->buf = fwdma_malloc_multiseg(fc, sizeof(uint32_t),
	    b->psize, b->nchunk * b->npacket, BUS_DMA_WAITOK);

	if (q->buf == NULL) {
		free(q->bulkxfer, M_FW);
		q->bulkxfer = NULL;
		return (ENOMEM);
	}
	q->bnchunk = b->nchunk;
	q->bnpacket = b->npacket;
	q->psize = (b->psize + 3) & ~3;
	q->queued = 0;

	STAILQ_INIT(&q->stvalid);
	STAILQ_INIT(&q->stfree);
	STAILQ_INIT(&q->stdma);
	q->stproc = NULL;

	for (i = 0; i < q->bnchunk; i++) {
		q->bulkxfer[i].poffset = i * q->bnpacket;
		q->bulkxfer[i].mbuf = NULL;
		STAILQ_INSERT_TAIL(&q->stfree, &q->bulkxfer[i], link);
	}

	q->flag &= ~FWXFERQ_MODEMASK;
	q->flag |= FWXFERQ_STREAM;
	q->flag |= FWXFERQ_EXTBUF;

	return (0);
}

static int
fwdev_freebuf(struct fw_xferq *q)
{
	if (q->flag & FWXFERQ_EXTBUF) {
		if (q->buf != NULL)
			fwdma_free_multiseg(q->buf);
		q->buf = NULL;
		free(q->bulkxfer, M_FW);
		q->bulkxfer = NULL;
		q->flag &= ~FWXFERQ_EXTBUF;
		q->psize = 0;
		q->maxq = FWMAXQUEUE;
	}
	return (0);
}


static int
fw_open(struct cdev *dev, int flags, int fmt, fw_proc *td)
{
	int err = 0;
	int unit = DEV2UNIT(dev);
	struct fw_drv1 *d;
	struct firewire_softc *sc;

	if (DEV_FWMEM(dev))
		return fwmem_open(dev, flags, fmt, td);

	sc = devclass_get_softc(firewire_devclass, unit);
	if (sc == NULL)
		return (ENXIO);

	FW_GLOCK(sc->fc);
	if (dev->si_drv1 != NULL) {
		FW_GUNLOCK(sc->fc);
		return (EBUSY);
	}
	/* set dummy value for allocation */
	dev->si_drv1 = (void *)-1;
	FW_GUNLOCK(sc->fc);

	dev->si_drv1 = malloc(sizeof(struct fw_drv1), M_FW, M_WAITOK | M_ZERO);

	if ((dev->si_flags & SI_NAMED) == 0) {
		int unit = DEV2UNIT(dev);
		int sub = DEV2SUB(dev);

		make_dev(&firewire_cdevsw, dev2unit(dev),
		    UID_ROOT, GID_OPERATOR, 0660, "fw%d.%d", unit, sub);
	}

	d = dev->si_drv1;
	d->fc = sc->fc;
	STAILQ_INIT(&d->binds);
	STAILQ_INIT(&d->rq);

	return err;
}

static int
fw_close(struct cdev *dev, int flags, int fmt, fw_proc *td)
{
	struct firewire_comm *fc;
	struct fw_drv1 *d;
	struct fw_xfer *xfer;
	struct fw_bind *fwb;
	int err = 0;

	if (DEV_FWMEM(dev))
		return fwmem_close(dev, flags, fmt, td);

	d = dev->si_drv1;
	fc = d->fc;

	/* remove binding */
	for (fwb = STAILQ_FIRST(&d->binds); fwb != NULL;
	    fwb = STAILQ_FIRST(&d->binds)) {
		fw_bindremove(fc, fwb);
		STAILQ_REMOVE_HEAD(&d->binds, chlist);
		fw_xferlist_remove(&fwb->xferlist);
		free(fwb, M_FW);
	}
	if (d->ir != NULL) {
		struct fw_xferq *ir = d->ir;

		if ((ir->flag & FWXFERQ_OPEN) == 0)
			return (EINVAL);
		if (ir->flag & FWXFERQ_RUNNING) {
			ir->flag &= ~FWXFERQ_RUNNING;
			fc->irx_disable(fc, ir->dmach);
		}
		/* free extbuf */
		fwdev_freebuf(ir);
		/* drain receiving buffer */
		for (xfer = STAILQ_FIRST(&ir->q);
		    xfer != NULL; xfer = STAILQ_FIRST(&ir->q)) {
			ir->queued--;
			STAILQ_REMOVE_HEAD(&ir->q, link);

			xfer->resp = 0;
			fw_xfer_done(xfer);
		}
		ir->flag &= ~(FWXFERQ_OPEN | FWXFERQ_MODEMASK |
		    FWXFERQ_CHTAGMASK);
		d->ir = NULL;

	}
	if (d->it != NULL) {
		struct fw_xferq *it = d->it;

		if ((it->flag & FWXFERQ_OPEN) == 0)
			return (EINVAL);
		if (it->flag & FWXFERQ_RUNNING) {
			it->flag &= ~FWXFERQ_RUNNING;
			fc->itx_disable(fc, it->dmach);
		}
		/* free extbuf */
		fwdev_freebuf(it);
		it->flag &= ~(FWXFERQ_OPEN |
		    FWXFERQ_MODEMASK | FWXFERQ_CHTAGMASK);
		d->it = NULL;
	}
	free(dev->si_drv1, M_FW);
	dev->si_drv1 = NULL;

	return err;
}

static int
fw_read_async(struct fw_drv1 *d, struct uio *uio, int ioflag)
{
	int err = 0, s;
	struct fw_xfer *xfer;
	struct fw_bind *fwb;
	struct fw_pkt *fp;
	struct tcode_info *tinfo;

	FW_GLOCK(d->fc);
	while ((xfer = STAILQ_FIRST(&d->rq)) == NULL && err == 0)
		err = msleep(&d->rq, FW_GMTX(d->fc), FWPRI, "fwra", 0);

	if (err != 0) {
		FW_GUNLOCK(d->fc);
		return (err);
	}

	s = splfw();
	STAILQ_REMOVE_HEAD(&d->rq, link);
	FW_GUNLOCK(xfer->fc);
	splx(s);
	fp = &xfer->recv.hdr;
#if 0 /* for GASP ?? */
	if (fc->irx_post != NULL)
		fc->irx_post(fc, fp->mode.ld);
#endif
	tinfo = &xfer->fc->tcode[fp->mode.hdr.tcode];
	err = uiomove(fp, tinfo->hdr_len, uio);
	if (err)
		goto out;
	err = uiomove(xfer->recv.payload, xfer->recv.pay_len, uio);

out:
	/* recycle this xfer */
	fwb = (struct fw_bind *)xfer->sc;
	fw_xfer_unload(xfer);
	xfer->recv.pay_len = PAGE_SIZE;
	FW_GLOCK(xfer->fc);
	STAILQ_INSERT_TAIL(&fwb->xferlist, xfer, link);
	FW_GUNLOCK(xfer->fc);
	return (err);
}

/*
 * read request.
 */
static int
fw_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct fw_drv1 *d;
	struct fw_xferq *ir;
	struct firewire_comm *fc;
	int err = 0, s, slept = 0;
	struct fw_pkt *fp;

	if (DEV_FWMEM(dev))
		return (physio(dev, uio, ioflag));

	d = dev->si_drv1;
	fc = d->fc;
	ir = d->ir;

	if (ir == NULL)
		return (fw_read_async(d, uio, ioflag));

	if (ir->buf == NULL)
		return (EIO);

	FW_GLOCK(fc);
readloop:
	if (ir->stproc == NULL) {
		/* iso bulkxfer */
		ir->stproc = STAILQ_FIRST(&ir->stvalid);
		if (ir->stproc != NULL) {
			s = splfw();
			STAILQ_REMOVE_HEAD(&ir->stvalid, link);
			splx(s);
			ir->queued = 0;
		}
	}
	if (ir->stproc == NULL) {
		/* no data available */
		if (slept == 0) {
			slept = 1;
			ir->flag |= FWXFERQ_WAKEUP;
			err = msleep(ir, FW_GMTX(fc), FWPRI, "fw_read", hz);
			ir->flag &= ~FWXFERQ_WAKEUP;
			if (err == 0)
				goto readloop;
		} else if (slept == 1)
			err = EIO;
		FW_GUNLOCK(fc);
		return err;
	} else if (ir->stproc != NULL) {
		/* iso bulkxfer */
		FW_GUNLOCK(fc);
		fp = (struct fw_pkt *)fwdma_v_addr(ir->buf,
		    ir->stproc->poffset + ir->queued);
		if (fc->irx_post != NULL)
			fc->irx_post(fc, fp->mode.ld);
		if (fp->mode.stream.len == 0) {
			err = EIO;
			return err;
		}
		err = uiomove((caddr_t)fp,
			fp->mode.stream.len + sizeof(uint32_t), uio);
		ir->queued++;
		if (ir->queued >= ir->bnpacket) {
			s = splfw();
			STAILQ_INSERT_TAIL(&ir->stfree, ir->stproc, link);
			splx(s);
			fc->irx_enable(fc, ir->dmach);
			ir->stproc = NULL;
		}
		if (uio->uio_resid >= ir->psize) {
			slept = -1;
			FW_GLOCK(fc);
			goto readloop;
		}
	}
	return err;
}

static int
fw_write_async(struct fw_drv1 *d, struct uio *uio, int ioflag)
{
	struct fw_xfer *xfer;
	struct fw_pkt pkt;
	struct tcode_info *tinfo;
	int err;

	bzero(&pkt, sizeof(struct fw_pkt));
	if ((err = uiomove((caddr_t)&pkt, sizeof(uint32_t), uio)))
		return (err);
	tinfo = &d->fc->tcode[pkt.mode.hdr.tcode];
	if ((err = uiomove((caddr_t)&pkt + sizeof(uint32_t),
	    tinfo->hdr_len - sizeof(uint32_t), uio)))
		return (err);

	if ((xfer = fw_xfer_alloc_buf(M_FWXFER, uio->uio_resid,
	    PAGE_SIZE/*XXX*/)) == NULL)
		return (ENOMEM);

	bcopy(&pkt, &xfer->send.hdr, sizeof(struct fw_pkt));
	xfer->send.pay_len = uio->uio_resid;
	if (uio->uio_resid > 0) {
		if ((err = uiomove((caddr_t)&xfer->send.payload[0],
		    uio->uio_resid, uio)))
			goto out;
	}

	xfer->fc = d->fc;
	xfer->sc = NULL;
	xfer->hand = fw_xferwake;
	xfer->send.spd = 2 /* XXX */;

	if ((err = fw_asyreq(xfer->fc, -1, xfer)))
		goto out;

	if ((err = fw_xferwait(xfer)))
		goto out;

	if (xfer->resp != 0) {
		err = xfer->resp;
		goto out;
	}

	if (xfer->flag & FWXF_RCVD) {
		FW_GLOCK(xfer->fc);
		STAILQ_INSERT_TAIL(&d->rq, xfer, link);
		FW_GUNLOCK(xfer->fc);
		return (0);
	}

out:
	fw_xfer_free(xfer);
	return (err);
}

static int
fw_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	int err = 0;
	int s, slept = 0;
	struct fw_drv1 *d;
	struct fw_pkt *fp;
	struct firewire_comm *fc;
	struct fw_xferq *it;

	if (DEV_FWMEM(dev))
		return (physio(dev, uio, ioflag));

	d = dev->si_drv1;
	fc = d->fc;
	it = d->it;

	if (it == NULL)
		return (fw_write_async(d, uio, ioflag));

	if (it->buf == NULL)
		return (EIO);

	FW_GLOCK(fc);
isoloop:
	if (it->stproc == NULL) {
		it->stproc = STAILQ_FIRST(&it->stfree);
		if (it->stproc != NULL) {
			s = splfw();
			STAILQ_REMOVE_HEAD(&it->stfree, link);
			splx(s);
			it->queued = 0;
		} else if (slept == 0) {
			slept = 1;
#if 0	/* XXX to avoid lock recursion */
			err = fc->itx_enable(fc, it->dmach);
			if (err)
				goto out;
#endif
			err = msleep(it, FW_GMTX(fc), FWPRI, "fw_write", hz);
			if (err)
				goto out;
			goto isoloop;
		} else {
			err = EIO;
			goto out;
		}
	}
	FW_GUNLOCK(fc);
	fp = (struct fw_pkt *)fwdma_v_addr(it->buf,
			it->stproc->poffset + it->queued);
	err = uiomove((caddr_t)fp, sizeof(struct fw_isohdr), uio);
	err = uiomove((caddr_t)fp->mode.stream.payload,
				fp->mode.stream.len, uio);
	it->queued++;
	if (it->queued >= it->bnpacket) {
		s = splfw();
		STAILQ_INSERT_TAIL(&it->stvalid, it->stproc, link);
		splx(s);
		it->stproc = NULL;
		err = fc->itx_enable(fc, it->dmach);
	}
	if (uio->uio_resid >= sizeof(struct fw_isohdr)) {
		slept = 0;
		FW_GLOCK(fc);
		goto isoloop;
	}
	return err;

out:
	FW_GUNLOCK(fc);
	return err;
}

static void
fw_hand(struct fw_xfer *xfer)
{
	struct fw_bind *fwb;
	struct fw_drv1 *d;

	fwb = (struct fw_bind *)xfer->sc;
	d = fwb->sc;
	FW_GLOCK(xfer->fc);
	STAILQ_INSERT_TAIL(&d->rq, xfer, link);
	FW_GUNLOCK(xfer->fc);
	wakeup(&d->rq);
}

/*
 * ioctl support.
 */
int
fw_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag, fw_proc *td)
{
	struct firewire_comm *fc;
	struct fw_drv1 *d;
	int i, len, err = 0;
	struct fw_device *fwdev;
	struct fw_bind *fwb;
	struct fw_xferq *ir, *it;
	struct fw_xfer *xfer;
	struct fw_pkt *fp;
	struct fw_devinfo *devinfo;
	void *ptr;

	struct fw_devlstreq *fwdevlst = (struct fw_devlstreq *)data;
	struct fw_asyreq *asyreq = (struct fw_asyreq *)data;
	struct fw_isochreq *ichreq = (struct fw_isochreq *)data;
	struct fw_isobufreq *ibufreq = (struct fw_isobufreq *)data;
	struct fw_asybindreq *bindreq = (struct fw_asybindreq *)data;
	struct fw_crom_buf *crom_buf = (struct fw_crom_buf *)data;

	if (DEV_FWMEM(dev))
		return fwmem_ioctl(dev, cmd, data, flag, td);

	if (!data)
		return (EINVAL);

	d = dev->si_drv1;
	fc = d->fc;
	ir = d->ir;
	it = d->it;

	switch (cmd) {
	case FW_STSTREAM:
		if (it == NULL) {
			i = fw_open_isodma(fc, /* tx */1);
			if (i < 0) {
				err = EBUSY;
				break;
			}
			it = fc->it[i];
			err = fwdev_allocbuf(fc, it, &d->bufreq.tx);
			if (err) {
				it->flag &= ~FWXFERQ_OPEN;
				break;
			}
		}
		it->flag &= ~0xff;
		it->flag |= (0x3f & ichreq->ch);
		it->flag |= ((0x3 & ichreq->tag) << 6);
		d->it = it;
		break;
	case FW_GTSTREAM:
		if (it != NULL) {
			ichreq->ch = it->flag & 0x3f;
			ichreq->tag = it->flag >> 2 & 0x3;
		} else
			err = EINVAL;
		break;
	case FW_SRSTREAM:
		if (ir == NULL) {
			i = fw_open_isodma(fc, /* tx */0);
			if (i < 0) {
				err = EBUSY;
				break;
			}
			ir = fc->ir[i];
			err = fwdev_allocbuf(fc, ir, &d->bufreq.rx);
			if (err) {
				ir->flag &= ~FWXFERQ_OPEN;
				break;
			}
		}
		ir->flag &= ~0xff;
		ir->flag |= (0x3f & ichreq->ch);
		ir->flag |= ((0x3 & ichreq->tag) << 6);
		d->ir = ir;
		err = fc->irx_enable(fc, ir->dmach);
		break;
	case FW_GRSTREAM:
		if (d->ir != NULL) {
			ichreq->ch = ir->flag & 0x3f;
			ichreq->tag = ir->flag >> 2 & 0x3;
		} else
			err = EINVAL;
		break;
	case FW_SSTBUF:
		bcopy(ibufreq, &d->bufreq, sizeof(d->bufreq));
		break;
	case FW_GSTBUF:
		bzero(&ibufreq->rx, sizeof(ibufreq->rx));
		if (ir != NULL) {
			ibufreq->rx.nchunk = ir->bnchunk;
			ibufreq->rx.npacket = ir->bnpacket;
			ibufreq->rx.psize = ir->psize;
		}
		bzero(&ibufreq->tx, sizeof(ibufreq->tx));
		if (it != NULL) {
			ibufreq->tx.nchunk = it->bnchunk;
			ibufreq->tx.npacket = it->bnpacket;
			ibufreq->tx.psize = it->psize;
		}
		break;
	case FW_ASYREQ:
	{
		struct tcode_info *tinfo;
		int pay_len = 0;

		fp = &asyreq->pkt;
		tinfo = &fc->tcode[fp->mode.hdr.tcode];

		if ((tinfo->flag & FWTI_BLOCK_ASY) != 0)
			pay_len = MAX(0, asyreq->req.len - tinfo->hdr_len);

		xfer = fw_xfer_alloc_buf(M_FWXFER, pay_len, PAGE_SIZE/*XXX*/);
		if (xfer == NULL)
			return (ENOMEM);

		switch (asyreq->req.type) {
		case FWASREQNODE:
			break;
		case FWASREQEUI:
			fwdev = fw_noderesolve_eui64(fc,
						&asyreq->req.dst.eui);
			if (fwdev == NULL) {
				device_printf(fc->bdev,
					"cannot find node\n");
				err = EINVAL;
				goto out;
			}
			fp->mode.hdr.dst = FWLOCALBUS | fwdev->dst;
			break;
		case FWASRESTL:
			/* XXX what's this? */
			break;
		case FWASREQSTREAM:
			/* nothing to do */
			break;
		}

		bcopy(fp, (void *)&xfer->send.hdr, tinfo->hdr_len);
		if (pay_len > 0)
			bcopy((char *)fp + tinfo->hdr_len,
			    xfer->send.payload, pay_len);
		xfer->send.spd = asyreq->req.sped;
		xfer->hand = fw_xferwake;

		if ((err = fw_asyreq(fc, -1, xfer)) != 0)
			goto out;
		if ((err = fw_xferwait(xfer)) != 0)
			goto out;
		if (xfer->resp != 0) {
			err = EIO;
			goto out;
		}
		if ((tinfo->flag & FWTI_TLABEL) == 0)
			goto out;

		/* copy response */
		tinfo = &fc->tcode[xfer->recv.hdr.mode.hdr.tcode];
		if (xfer->recv.hdr.mode.hdr.tcode == FWTCODE_RRESB ||
		    xfer->recv.hdr.mode.hdr.tcode == FWTCODE_LRES) {
			pay_len = xfer->recv.pay_len;
			if (asyreq->req.len >= xfer->recv.pay_len + tinfo->hdr_len) {
				asyreq->req.len = xfer->recv.pay_len +
				    tinfo->hdr_len;
			} else {
				err = EINVAL;
				pay_len = 0;
			}
		} else {
			pay_len = 0;
		}
		bcopy(&xfer->recv.hdr, fp, tinfo->hdr_len);
		bcopy(xfer->recv.payload, (char *)fp + tinfo->hdr_len, pay_len);
out:
		fw_xfer_free_buf(xfer);
		break;
	}
	case FW_IBUSRST:
		fc->ibr(fc);
		break;
	case FW_CBINDADDR:
		fwb = fw_bindlookup(fc,
				bindreq->start.hi, bindreq->start.lo);
		if (fwb == NULL) {
			err = EINVAL;
			break;
		}
		fw_bindremove(fc, fwb);
		STAILQ_REMOVE(&d->binds, fwb, fw_bind, chlist);
		fw_xferlist_remove(&fwb->xferlist);
		free(fwb, M_FW);
		break;
	case FW_SBINDADDR:
		if (bindreq->len <= 0) {
			err = EINVAL;
			break;
		}
		if (bindreq->start.hi > 0xffff) {
			err = EINVAL;
			break;
		}
		fwb = malloc(sizeof(struct fw_bind), M_FW, M_WAITOK);
		fwb->start = ((u_int64_t)bindreq->start.hi << 32) |
		    bindreq->start.lo;
		fwb->end = fwb->start +  bindreq->len;
		fwb->sc = d;
		STAILQ_INIT(&fwb->xferlist);
		err = fw_bindadd(fc, fwb);
		if (err == 0) {
			fw_xferlist_add(&fwb->xferlist, M_FWXFER,
			    /* XXX */
			    PAGE_SIZE, PAGE_SIZE, 5,
			    fc, fwb, fw_hand);
			STAILQ_INSERT_TAIL(&d->binds, fwb, chlist);
		}
		break;
	case FW_GDEVLST:
		i = len = 1;
		/* myself */
		devinfo = &fwdevlst->dev[0];
		devinfo->dst = fc->nodeid;
		devinfo->status = 0;	/* XXX */
		devinfo->eui.hi = fc->eui.hi;
		devinfo->eui.lo = fc->eui.lo;
		STAILQ_FOREACH(fwdev, &fc->devices, link) {
			if (len < FW_MAX_DEVLST) {
				devinfo = &fwdevlst->dev[len++];
				devinfo->dst = fwdev->dst;
				devinfo->status =
					(fwdev->status == FWDEVINVAL) ? 0 : 1;
				devinfo->eui.hi = fwdev->eui.hi;
				devinfo->eui.lo = fwdev->eui.lo;
			}
			i++;
		}
		fwdevlst->n = i;
		fwdevlst->info_len = len;
		break;
	case FW_GTPMAP:
		bcopy(fc->topology_map, data,
		    (fc->topology_map->crc_len + 1) * 4);
		break;
	case FW_GCROM:
		STAILQ_FOREACH(fwdev, &fc->devices, link)
			if (FW_EUI64_EQUAL(fwdev->eui, crom_buf->eui))
				break;
		if (fwdev == NULL) {
			if (!FW_EUI64_EQUAL(fc->eui, crom_buf->eui)) {
				err = FWNODE_INVAL;
				break;
			}
			/* myself */
			ptr = malloc(CROMSIZE, M_FW, M_WAITOK);
			len = CROMSIZE;
			for (i = 0; i < CROMSIZE/4; i++)
				((uint32_t *)ptr)[i]
					= ntohl(fc->config_rom[i]);
		} else {
			/* found */
			ptr = (void *)&fwdev->csrrom[0];
			if (fwdev->rommax < CSRROMOFF)
				len = 0;
			else
				len = fwdev->rommax - CSRROMOFF + 4;
		}
		if (crom_buf->len < len)
			len = crom_buf->len;
		else
			crom_buf->len = len;
		err = copyout(ptr, crom_buf->ptr, len);
		if (fwdev == NULL)
			/* myself */
			free(ptr, M_FW);
		break;
	default:
		fc->ioctl(dev, cmd, data, flag, td);
		break;
	}
	return err;
}
int
fw_poll(struct cdev *dev, int events, fw_proc *td)
{
	struct fw_xferq *ir;
	int revents;
	int tmp;

	if (DEV_FWMEM(dev))
		return fwmem_poll(dev, events, td);

	ir = ((struct fw_drv1 *)dev->si_drv1)->ir;
	revents = 0;
	tmp = POLLIN | POLLRDNORM;
	if (events & tmp) {
		if (STAILQ_FIRST(&ir->q) != NULL)
			revents |= tmp;
		else
			selrecord(td, &ir->rsel);
	}
	tmp = POLLOUT | POLLWRNORM;
	if (events & tmp) {
		/* XXX should be fixed */
		revents |= tmp;
	}

	return revents;
}

static int
fw_mmap (struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int nproto, vm_memattr_t *memattr)
{

	if (DEV_FWMEM(dev))
		return fwmem_mmap(dev, offset, paddr, nproto, memattr);

	return EINVAL;
}

static void
fw_strategy(struct bio *bp)
{
	struct cdev *dev;

	dev = bp->bio_dev;
	if (DEV_FWMEM(dev)) {
		fwmem_strategy(bp);
		return;
	}

	bp->bio_error = EOPNOTSUPP;
	bp->bio_flags |= BIO_ERROR;
	bp->bio_resid = bp->bio_bcount;
	biodone(bp);
}

int
fwdev_makedev(struct firewire_softc *sc)
{
	int err = 0;

	struct cdev *d;
	int unit;

	unit = device_get_unit(sc->fc->bdev);
	sc->dev = make_dev(&firewire_cdevsw, MAKEMINOR(0, unit, 0),
	    UID_ROOT, GID_OPERATOR, 0660, "fw%d.%d", unit, 0);
	d = make_dev(&firewire_cdevsw, MAKEMINOR(FWMEM_FLAG, unit, 0),
	    UID_ROOT, GID_OPERATOR, 0660, "fwmem%d.%d", unit, 0);
	dev_depends(sc->dev, d);
	make_dev_alias(sc->dev, "fw%d", unit);
	make_dev_alias(d, "fwmem%d", unit);

	return (err);
}

int
fwdev_destroydev(struct firewire_softc *sc)
{
	int err = 0;

	destroy_dev(sc->dev);
	return (err);
}

#define NDEVTYPE 2
void
fwdev_clone(void *arg, struct ucred *cred, char *name, int namelen,
    struct cdev **dev)
{
	struct firewire_softc *sc;
	char *devnames[NDEVTYPE] = {"fw", "fwmem"};
	char *subp = NULL;
	int devflag[NDEVTYPE] = {0, FWMEM_FLAG};
	int i, unit = 0, sub = 0;

	if (*dev != NULL)
		return;

	for (i = 0; i < NDEVTYPE; i++)
		if (dev_stdclone(name, &subp, devnames[i], &unit) == 2)
			goto found;
	/* not match */
	return;
found:

	if (subp == NULL || *subp++ != '.')
		return;

	/* /dev/fwU.S */
	while (isdigit(*subp)) {
		sub *= 10;
		sub += *subp++ - '0';
	}
	if (*subp != '\0')
		return;

	sc = devclass_get_softc(firewire_devclass, unit);
	if (sc == NULL)
		return;
	*dev = make_dev_credf(MAKEDEV_REF, &firewire_cdevsw,
	    MAKEMINOR(devflag[i], unit, sub), cred, UID_ROOT, GID_OPERATOR,
	    0660, "%s%d.%d", devnames[i], unit, sub);
	dev_depends(sc->dev, *dev);
	return;
}
