/*	$OpenBSD: video.c,v 1.61 2025/04/15 06:44:37 kirill Exp $	*/

/*
 * Copyright (c) 2008 Robert Nagy <robert@openbsd.org>
 * Copyright (c) 2008 Marcus Glocker <mglocker@openbsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/event.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/device.h>
#include <sys/vnode.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/videoio.h>

#include <dev/video_if.h>

#include <uvm/uvm_extern.h>

/*
 * Locks used to protect struct members and global data
 *	a	atomic
 *	m	sc_mtx
 */

#ifdef VIDEO_DEBUG
int video_debug = 1;
#define DPRINTF(l, x...) do { if ((l) <= video_debug) printf(x); } while (0)
#else
#define DPRINTF(l, x...)
#endif

struct video_softc {
	struct device		 dev;
	void			*hw_hdl;	/* hardware driver handle */
	struct device		*sc_dev;	/* hardware device struct */
	const struct video_hw_if *hw_if;	/* hardware interface */
	char			 sc_dying;	/* device detached */
	struct process		*sc_owner;	/* owner process */
	uint8_t			 sc_open;	/* device opened */

	struct mutex		 sc_mtx;
	int			 sc_fsize;
	uint8_t			*sc_fbuffer;
	caddr_t			 sc_fbuffer_mmap;
	size_t			 sc_fbufferlen;
	int			 sc_vidmode;	/* access mode */
#define		VIDMODE_NONE	0
#define		VIDMODE_MMAP	1
#define		VIDMODE_READ	2
	int			 sc_frames_ready;	/* [m] */

	struct klist		 sc_rklist;		/* [m] read selector */
};

int	videoprobe(struct device *, void *, void *);
void	videoattach(struct device *, struct device *, void *);
int	videodetach(struct device *, int);
int	videoactivate(struct device *, int);
int	videoprint(void *, const char *);

void	video_intr(void *);
int	video_stop(struct video_softc *);
int	video_claim(struct video_softc *, struct process *);

const struct cfattach video_ca = {
	sizeof(struct video_softc), videoprobe, videoattach,
	videodetach, videoactivate
};

struct cfdriver video_cd = {
	NULL, "video", DV_DULL
};

/*
 * Global flag to control if video recording is enabled by kern.video.record.
 */
int video_record_enable = 0;	/* [a] */

int
videoprobe(struct device *parent, void *match, void *aux)
{
	return (1);
}

void
videoattach(struct device *parent, struct device *self, void *aux)
{
	struct video_softc *sc = (void *)self;
	struct video_attach_args *sa = aux;

	printf("\n");
	sc->hw_if = sa->hwif;
	sc->hw_hdl = sa->hdl;
	sc->sc_dev = parent;
	sc->sc_fbufferlen = 0;
	sc->sc_owner = NULL;
	mtx_init(&sc->sc_mtx, IPL_MPFLOOR);
	klist_init_mutex(&sc->sc_rklist, &sc->sc_mtx);

	if (sc->hw_if->get_bufsize)
		sc->sc_fbufferlen = (sc->hw_if->get_bufsize)(sc->hw_hdl);
	if (sc->sc_fbufferlen == 0) {
		printf("video: could not request frame buffer size\n");
		return;
	}

	sc->sc_fbuffer = malloc(sc->sc_fbufferlen, M_DEVBUF, M_NOWAIT);
	if (sc->sc_fbuffer == NULL) {
		printf("video: could not allocate frame buffer\n");
		return;
	}
}

int
videoopen(dev_t dev, int flags, int fmt, struct proc *p)
{
	int unit = VIDEOUNIT(dev);
	struct video_softc *sc;
	int error = 0;

	KERNEL_ASSERT_LOCKED();

	sc = (struct video_softc *)device_lookup(&video_cd, unit);
	if (sc == NULL)
		return (ENXIO);

	if (sc->sc_open) {
		DPRINTF(1, "%s: device already open\n", __func__);
		goto done;
	}

	sc->sc_vidmode = VIDMODE_NONE;
	sc->sc_frames_ready = 0;

	if (sc->hw_if->open != NULL) {
		error = sc->hw_if->open(sc->hw_hdl, flags, &sc->sc_fsize,
		    sc->sc_fbuffer, video_intr, sc);
	}
	if (error == 0) {
		sc->sc_open = 1;
		DPRINTF(1, "%s: set device to open\n", __func__);
	}

done:
	device_unref(&sc->dev);
	return (error);
}

int
videoclose(dev_t dev, int flags, int fmt, struct proc *p)
{
	int unit = VIDEOUNIT(dev);
	struct video_softc *sc;
	int error = 0;

	KERNEL_ASSERT_LOCKED();

	DPRINTF(1, "%s: last close\n", __func__);

	sc = (struct video_softc *)device_lookup(&video_cd, unit);
	if (sc == NULL)
		return (ENXIO);

	if (!sc->sc_open) {
		error = ENXIO;
		goto done;
	}

	error = video_stop(sc);
	sc->sc_open = 0;

done:
	device_unref(&sc->dev);
	return (error);
}

int
videoread(dev_t dev, struct uio *uio, int ioflag)
{
	int unit = VIDEOUNIT(dev);
	struct video_softc *sc;
	int error = 0;
	size_t size;

	KERNEL_ASSERT_LOCKED();

	sc = (struct video_softc *)device_lookup(&video_cd, unit);
	if (sc == NULL)
		return (ENXIO);

	if (sc->sc_dying) {
		error = EIO;
		goto done;
	}

	if (sc->sc_vidmode == VIDMODE_MMAP) {
		error = EBUSY;
		goto done;
	}

	if ((error = video_claim(sc, curproc->p_p)))
		goto done;

	/* start the stream if not already started */
	if (sc->sc_vidmode == VIDMODE_NONE && sc->hw_if->start_read) {
 		error = sc->hw_if->start_read(sc->hw_hdl);
 		if (error)
			goto done;
		sc->sc_vidmode = VIDMODE_READ;
 	}

	DPRINTF(1, "resid=%zu\n", uio->uio_resid);

	mtx_enter(&sc->sc_mtx);

	if (sc->sc_frames_ready < 1) {
		/* block userland read until a frame is ready */
		error = msleep_nsec(sc, &sc->sc_mtx, PWAIT | PCATCH,
		    "vid_rd", INFSLP);
		if (sc->sc_dying)
			error = EIO;
		if (error) {
			mtx_leave(&sc->sc_mtx);
			goto done;
		}
	}
	sc->sc_frames_ready--;

	mtx_leave(&sc->sc_mtx);

	/* move no more than 1 frame to userland, as per specification */
	size = ulmin(uio->uio_resid, sc->sc_fsize);
	if (!atomic_load_int(&video_record_enable))
		bzero(sc->sc_fbuffer, size);
	error = uiomove(sc->sc_fbuffer, size, uio);
	if (error)
		goto done;

	DPRINTF(1, "uiomove successfully done (%zu bytes)\n", size);

done:
	device_unref(&sc->dev);
	return (error);
}

int
videoioctl(dev_t dev, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	int unit = VIDEOUNIT(dev);
	struct video_softc *sc;
	struct v4l2_buffer *vb = (struct v4l2_buffer *)data;
	int error;

	KERNEL_ASSERT_LOCKED();

	sc = (struct video_softc *)device_lookup(&video_cd, unit);
	if (sc == NULL)
		return (ENXIO);

	if (sc->hw_if == NULL) {
		error = ENXIO;
		goto done;
	}

	DPRINTF(3, "video_ioctl(%zu, '%c', %zu)\n",
	    IOCPARM_LEN(cmd), (int) IOCGROUP(cmd), cmd & 0xff);

	error = EOPNOTSUPP;
	switch (cmd) {
	case VIDIOC_G_CTRL:
		if (sc->hw_if->g_ctrl)
			error = (sc->hw_if->g_ctrl)(sc->hw_hdl,
			    (struct v4l2_control *)data);
		break;
	case VIDIOC_S_CTRL:
		if (sc->hw_if->s_ctrl)
			error = (sc->hw_if->s_ctrl)(sc->hw_hdl,
			    (struct v4l2_control *)data);
		break;
	default:
		error = (ENOTTY);
	}
	if (error != ENOTTY)
		goto done;

	if ((error = video_claim(sc, p->p_p)))
		goto done;

	/*
	 * The following IOCTLs can only be called by the device owner.
	 * For further shared IOCTLs please move it up.
	 */
	error = EOPNOTSUPP;
	switch (cmd) {
	case VIDIOC_QUERYCAP:
		if (sc->hw_if->querycap)
			error = (sc->hw_if->querycap)(sc->hw_hdl,
			    (struct v4l2_capability *)data);
		break;
	case VIDIOC_ENUM_FMT:
		if (sc->hw_if->enum_fmt)
			error = (sc->hw_if->enum_fmt)(sc->hw_hdl,
			    (struct v4l2_fmtdesc *)data);
		break;
	case VIDIOC_ENUM_FRAMESIZES:
		if (sc->hw_if->enum_fsizes)
			error = (sc->hw_if->enum_fsizes)(sc->hw_hdl,
			    (struct v4l2_frmsizeenum *)data);
		break;
	case VIDIOC_ENUM_FRAMEINTERVALS:
		if (sc->hw_if->enum_fivals)
			error = (sc->hw_if->enum_fivals)(sc->hw_hdl,
			    (struct v4l2_frmivalenum *)data);
		break;
	case VIDIOC_S_FMT:
		if (!(flags & FWRITE))
			return (EACCES);
		if (sc->hw_if->s_fmt)
			error = (sc->hw_if->s_fmt)(sc->hw_hdl,
			    (struct v4l2_format *)data);
		break;
	case VIDIOC_G_FMT:
		if (sc->hw_if->g_fmt)
			error = (sc->hw_if->g_fmt)(sc->hw_hdl,
			    (struct v4l2_format *)data);
		break;
	case VIDIOC_S_PARM:
		if (sc->hw_if->s_parm)
			error = (sc->hw_if->s_parm)(sc->hw_hdl,
			    (struct v4l2_streamparm *)data);
		break;
	case VIDIOC_G_PARM:
		if (sc->hw_if->g_parm)
			error = (sc->hw_if->g_parm)(sc->hw_hdl,
			    (struct v4l2_streamparm *)data);
		break;
	case VIDIOC_ENUMINPUT:
		if (sc->hw_if->enum_input)
			error = (sc->hw_if->enum_input)(sc->hw_hdl,
			    (struct v4l2_input *)data);
		break;
	case VIDIOC_S_INPUT:
		if (sc->hw_if->s_input)
			error = (sc->hw_if->s_input)(sc->hw_hdl,
			    (int)*data);
		break;
	case VIDIOC_G_INPUT:
		if (sc->hw_if->g_input)
			error = (sc->hw_if->g_input)(sc->hw_hdl,
			    (int *)data);
		break;
	case VIDIOC_REQBUFS:
		if (sc->hw_if->reqbufs)
			error = (sc->hw_if->reqbufs)(sc->hw_hdl,
			    (struct v4l2_requestbuffers *)data);
		break;
	case VIDIOC_QUERYBUF:
		if (sc->hw_if->querybuf)
			error = (sc->hw_if->querybuf)(sc->hw_hdl,
			    (struct v4l2_buffer *)data);
		break;
	case VIDIOC_QBUF:
		if (sc->hw_if->qbuf)
			error = (sc->hw_if->qbuf)(sc->hw_hdl,
			    (struct v4l2_buffer *)data);
		break;
	case VIDIOC_DQBUF:
		if (!sc->hw_if->dqbuf)
			break;
		/* should have called mmap() before now */
		if (sc->sc_vidmode != VIDMODE_MMAP) {
			error = EINVAL;
			break;
		}
		error = (sc->hw_if->dqbuf)(sc->hw_hdl,
		    (struct v4l2_buffer *)data);
		if (!atomic_load_int(&video_record_enable))
			bzero(sc->sc_fbuffer_mmap + vb->m.offset, vb->length);
		mtx_enter(&sc->sc_mtx);
		sc->sc_frames_ready--;
		mtx_leave(&sc->sc_mtx);
		break;
	case VIDIOC_STREAMON:
		if (sc->hw_if->streamon)
			error = (sc->hw_if->streamon)(sc->hw_hdl,
			    (int)*data);
		break;
	case VIDIOC_STREAMOFF:
		if (sc->hw_if->streamoff)
			error = (sc->hw_if->streamoff)(sc->hw_hdl,
			    (int)*data);
		if (!error) {
			/* Release device ownership and streaming buffers. */
			error = video_stop(sc);
		}
		break;
	case VIDIOC_TRY_FMT:
		if (sc->hw_if->try_fmt)
			error = (sc->hw_if->try_fmt)(sc->hw_hdl,
			    (struct v4l2_format *)data);
		break;
	case VIDIOC_QUERYCTRL:
		if (sc->hw_if->queryctrl)
			error = (sc->hw_if->queryctrl)(sc->hw_hdl,
			    (struct v4l2_queryctrl *)data);
		break;
	default:
		error = (ENOTTY);
	}

done:
	device_unref(&sc->dev);
	return (error);
}

paddr_t
videommap(dev_t dev, off_t off, int prot)
{
	int unit = VIDEOUNIT(dev);
	struct video_softc *sc;
	caddr_t p;
	paddr_t pa;

	KERNEL_ASSERT_LOCKED();

	DPRINTF(2, "%s: off=%lld, prot=%d\n", __func__, off, prot);

	sc = (struct video_softc *)device_lookup(&video_cd, unit);
	if (sc == NULL)
		return (-1);

	if (sc->sc_dying)
		goto err;

	if (sc->hw_if->mappage == NULL)
		goto err;

	p = sc->hw_if->mappage(sc->hw_hdl, off, prot);
	if (p == NULL)
		goto err;
	if (pmap_extract(pmap_kernel(), (vaddr_t)p, &pa) == FALSE)
		panic("videommap: invalid page");
	sc->sc_vidmode = VIDMODE_MMAP;

	/* store frame buffer base address for later blanking */
	if (off == 0)
		sc->sc_fbuffer_mmap = p;

	device_unref(&sc->dev);
	return (pa);

err:
	device_unref(&sc->dev);
	return (-1);
}

void
filt_videodetach(struct knote *kn)
{
	struct video_softc *sc = kn->kn_hook;

	klist_remove(&sc->sc_rklist, kn);
}

int
filt_videoread(struct knote *kn, long hint)
{
	struct video_softc *sc = kn->kn_hook;

	if (sc->sc_frames_ready > 0)
		return (1);

	return (0);
}

int
filt_videomodify(struct kevent *kev, struct knote *kn)
{
	struct video_softc *sc = kn->kn_hook;
	int active;

	mtx_enter(&sc->sc_mtx);
	active = knote_modify(kev, kn); 
	mtx_leave(&sc->sc_mtx);

	return (active);
}

int
filt_videoprocess(struct knote *kn, struct kevent *kev)
{
	struct video_softc *sc = kn->kn_hook;
	int active;

	mtx_enter(&sc->sc_mtx);
	active = knote_process(kn, kev);
	mtx_leave(&sc->sc_mtx);

	return (active);
}

const struct filterops video_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_videodetach,
	.f_event	= filt_videoread,
	.f_modify	= filt_videomodify,
	.f_process	= filt_videoprocess,
};

int
videokqfilter(dev_t dev, struct knote *kn)
{
	int unit = VIDEOUNIT(dev);
	struct video_softc *sc;
	int error = 0;

	KERNEL_ASSERT_LOCKED();

	sc = (struct video_softc *)device_lookup(&video_cd, unit);
	if (sc == NULL)
		return (ENXIO);

	if (sc->sc_dying) {
		error = ENXIO;
		goto done;
	}

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &video_filtops;
		kn->kn_hook = sc;
		break;
	default:
		error  = EINVAL;
		goto done;
	}

	if ((error = video_claim(sc, curproc->p_p)))
		goto done;

	/*
	 * Start the stream in read() mode if not already started.  If
	 * the user wanted mmap() mode, he should have called mmap()
	 * before now.
	 */
	if (sc->sc_vidmode == VIDMODE_NONE && sc->hw_if->start_read) {
		if (sc->hw_if->start_read(sc->hw_hdl))
			return (ENXIO);
		sc->sc_vidmode = VIDMODE_READ;
	}

	klist_insert(&sc->sc_rklist, kn);

done:
	device_unref(&sc->dev);
	return (error);
}

int
video_submatch(struct device *parent, void *match, void *aux)
{
        struct cfdata *cf = match;

	return (cf->cf_driver == &video_cd);
}

/*
 * Called from hardware driver. This is where the MI video driver gets
 * probed/attached to the hardware driver
 */
struct device *
video_attach_mi(const struct video_hw_if *rhwp, void *hdlp, struct device *dev)
{
	struct video_attach_args arg;

	arg.hwif = rhwp;
	arg.hdl = hdlp;
	return (config_found_sm(dev, &arg, videoprint, video_submatch));
}

void
video_intr(void *addr)
{
	struct video_softc *sc = (struct video_softc *)addr;

	DPRINTF(3, "video_intr sc=%p\n", sc);
	mtx_enter(&sc->sc_mtx);
	if (sc->sc_vidmode != VIDMODE_NONE)
		sc->sc_frames_ready++;
	else
		printf("%s: interrupt but no streams!\n", __func__);
	if (sc->sc_vidmode == VIDMODE_READ)
		wakeup(sc);
	knote_locked(&sc->sc_rklist, 0);
	mtx_leave(&sc->sc_mtx);
}

int
video_stop(struct video_softc *sc)
{
	int error = 0;

	DPRINTF(1, "%s: stream close\n", __func__);

	if (sc->hw_if->close != NULL)
		error = sc->hw_if->close(sc->hw_hdl);

	sc->sc_vidmode = VIDMODE_NONE;
	mtx_enter(&sc->sc_mtx);
	sc->sc_frames_ready = 0;
	mtx_leave(&sc->sc_mtx);
	sc->sc_owner = NULL;

	return (error);
}

int
video_claim(struct video_softc *sc, struct process *pr)
{
	if (sc->sc_owner != NULL && sc->sc_owner != pr) {
		DPRINTF(1, "%s: already owned=%p\n", __func__, sc->sc_owner);
		return (EBUSY);
	}

	if (sc->sc_owner == NULL) {
		sc->sc_owner = pr;
		DPRINTF(1, "%s: new owner=%p\n", __func__, sc->sc_owner);
	}

	return (0);
}

int
videoprint(void *aux, const char *pnp)
{
	if (pnp != NULL)
		printf("video at %s", pnp);
	return (UNCONF);
}

int
videodetach(struct device *self, int flags)
{
	struct video_softc *sc = (struct video_softc *)self;
	int maj, mn;

	/* locate the major number */
	for (maj = 0; maj < nchrdev; maj++)
		if (cdevsw[maj].d_open == videoopen)
			break;

	/* Nuke the vnodes for any open instances (calls close). */
	mn = self->dv_unit;
	vdevgone(maj, mn, mn, VCHR);

	klist_invalidate(&sc->sc_rklist);
	klist_free(&sc->sc_rklist);

	free(sc->sc_fbuffer, M_DEVBUF, sc->sc_fbufferlen);

	return (0);
}

int
videoactivate(struct device *self, int act)
{
	struct video_softc *sc = (struct video_softc *)self;

	switch (act) {
	case DVACT_DEACTIVATE:
		sc->sc_dying = 1;
		break;
	}
	return (0);
}
