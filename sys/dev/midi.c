/*	$OpenBSD: midi.c,v 1.58 2024/12/30 02:46:00 guenther Exp $	*/

/*
 * Copyright (c) 2003, 2004 Alexandre Ratchov
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
#include <sys/fcntl.h>
#include <sys/systm.h>
#include <sys/ioctl.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/timeout.h>
#include <sys/vnode.h>
#include <sys/signalvar.h>
#include <sys/device.h>

#include <dev/midi_if.h>
#include <dev/audio_if.h>
#include <dev/midivar.h>

#define DEVNAME(sc)		((sc)->dev.dv_xname)

int	midiopen(dev_t, int, int, struct proc *);
int	midiclose(dev_t, int, int, struct proc *);
int	midiread(dev_t, struct uio *, int);
int	midiwrite(dev_t, struct uio *, int);
int	midikqfilter(dev_t, struct knote *);
int	midiioctl(dev_t, u_long, caddr_t, int, struct proc *);
int	midiprobe(struct device *, void *, void *);
void	midiattach(struct device *, struct device *, void *);
int	mididetach(struct device *, int);
int	midiprint(void *, const char *);

void	midi_iintr(void *, int);
void 	midi_ointr(void *);
void	midi_timeout(void *);
void	midi_out_start(struct midi_softc *);
void	midi_out_stop(struct midi_softc *);
void	midi_out_do(struct midi_softc *);


const struct cfattach midi_ca = {
	sizeof(struct midi_softc), midiprobe, midiattach, mididetach
};

struct cfdriver midi_cd = {
	NULL, "midi", DV_DULL
};


void filt_midiwdetach(struct knote *);
int filt_midiwrite(struct knote *, long);
int filt_midimodify(struct kevent *, struct knote *);
int filt_midiprocess(struct knote *, struct kevent *);

const struct filterops midiwrite_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_midiwdetach,
	.f_event	= filt_midiwrite,
	.f_modify	= filt_midimodify,
	.f_process	= filt_midiprocess,
};

void filt_midirdetach(struct knote *);
int filt_midiread(struct knote *, long);

const struct filterops midiread_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_midirdetach,
	.f_event	= filt_midiread,
	.f_modify	= filt_midimodify,
	.f_process	= filt_midiprocess,
};

void
midi_buf_wakeup(struct midi_buffer *buf)
{
	if (buf->blocking) {
		wakeup(&buf->blocking);
		buf->blocking = 0;
	}
	knote_locked(&buf->klist, 0);
}

void
midi_iintr(void *addr, int data)
{
	struct midi_softc  *sc = (struct midi_softc *)addr;
	struct midi_buffer *mb = &sc->inbuf;

	MUTEX_ASSERT_LOCKED(&audio_lock);
	if (!(sc->dev.dv_flags & DVF_ACTIVE) || !(sc->flags & FREAD))
		return;

	if (MIDIBUF_ISFULL(mb))
		return; /* discard data */

	MIDIBUF_WRITE(mb, data);

	midi_buf_wakeup(mb);
}

int
midiread(dev_t dev, struct uio *uio, int ioflag)
{
	struct midi_softc *sc;
	struct midi_buffer *mb;
	size_t count;
	int error;

	sc = (struct midi_softc *)device_lookup(&midi_cd, minor(dev));
	if (sc == NULL)
		return ENXIO;
	if (!(sc->flags & FREAD)) {
		error = ENXIO;
		goto done;
	}
	mb = &sc->inbuf;

	/* if there is no data then sleep (unless IO_NDELAY flag is set) */
	error = 0;
	mtx_enter(&audio_lock);
	while (MIDIBUF_ISEMPTY(mb)) {
		if (ioflag & IO_NDELAY) {
			error = EWOULDBLOCK;
			goto done_mtx;
		}
		sc->inbuf.blocking = 1;
		error = msleep_nsec(&sc->inbuf.blocking, &audio_lock,
		    PWAIT | PCATCH, "mid_rd", INFSLP);
		if (!(sc->dev.dv_flags & DVF_ACTIVE))
			error = EIO;
		if (error)
			goto done_mtx;
	}

	/* at this stage, there is at least 1 byte */

	while (uio->uio_resid > 0 && mb->used > 0) {
		count = MIDIBUF_SIZE - mb->start;
		if (count > mb->used)
			count = mb->used;
		if (count > uio->uio_resid)
			count = uio->uio_resid;
		mtx_leave(&audio_lock);
		error = uiomove(mb->data + mb->start, count, uio);
		if (error)
			goto done;
		mtx_enter(&audio_lock);
		MIDIBUF_REMOVE(mb, count);
	}

done_mtx:
	mtx_leave(&audio_lock);
done:
	device_unref(&sc->dev);
	return error;
}

void
midi_ointr(void *addr)
{
	struct midi_softc *sc = (struct midi_softc *)addr;
	struct midi_buffer *mb;

	MUTEX_ASSERT_LOCKED(&audio_lock);
	if (!(sc->dev.dv_flags & DVF_ACTIVE) || !(sc->flags & FWRITE))
		return;
	
	mb = &sc->outbuf;
	if (mb->used > 0) {
#ifdef MIDI_DEBUG
		if (!sc->isbusy) {
			printf("midi_ointr: output must be busy\n");
		}
#endif
		midi_out_do(sc);
	} else if (sc->isbusy)
		midi_out_stop(sc);
}

void
midi_timeout(void *addr)
{
	mtx_enter(&audio_lock);
	midi_ointr(addr);
	mtx_leave(&audio_lock);
}

void
midi_out_start(struct midi_softc *sc)
{
	if (!sc->isbusy) {
		sc->isbusy = 1;
		midi_out_do(sc);
	}
}

void
midi_out_stop(struct midi_softc *sc)
{
	sc->isbusy = 0;
	midi_buf_wakeup(&sc->outbuf);
}

void
midi_out_do(struct midi_softc *sc)
{
	struct midi_buffer *mb = &sc->outbuf;

	while (mb->used > 0) {
		if (!sc->hw_if->output(sc->hw_hdl, mb->data[mb->start]))
			break;
		MIDIBUF_REMOVE(mb, 1);
		if (MIDIBUF_ISEMPTY(mb)) {
			if (sc->hw_if->flush != NULL)
				sc->hw_if->flush(sc->hw_hdl);
			midi_out_stop(sc);
			return;
		}
	}

	if (!(sc->props & MIDI_PROP_OUT_INTR)) {
		if (MIDIBUF_ISEMPTY(mb))
			midi_out_stop(sc);
		else
			timeout_add(&sc->timeo, 1);
	}
}

int
midiwrite(dev_t dev, struct uio *uio, int ioflag)
{
	struct midi_softc *sc;
	struct midi_buffer *mb;
	size_t count;
	int error;

	sc = (struct midi_softc *)device_lookup(&midi_cd, minor(dev));
	if (sc == NULL)
		return ENXIO;
	if (!(sc->flags & FWRITE)) {
		error = ENXIO;
		goto done;
	}
	mb = &sc->outbuf;

	/*
	 * If IO_NDELAY flag is set then check if there is enough room
	 * in the buffer to store at least one byte. If not then dont
	 * start the write process.
	 */
	error = 0;
	mtx_enter(&audio_lock);
	if ((ioflag & IO_NDELAY) && MIDIBUF_ISFULL(mb) && (uio->uio_resid > 0)) {
		error = EWOULDBLOCK;
		goto done_mtx;
	}

	while (uio->uio_resid > 0) {
		while (MIDIBUF_ISFULL(mb)) {
			if (ioflag & IO_NDELAY) {
				/*
				 * At this stage at least one byte is already
				 * moved so we do not return EWOULDBLOCK
				 */
				goto done_mtx;
			}
			sc->outbuf.blocking = 1;
			error = msleep_nsec(&sc->outbuf.blocking, &audio_lock,
			    PWAIT | PCATCH, "mid_wr", INFSLP);
			if (!(sc->dev.dv_flags & DVF_ACTIVE))
				error = EIO;
			if (error)
				goto done_mtx;
		}

		count = MIDIBUF_SIZE - MIDIBUF_END(mb);
		if (count > MIDIBUF_AVAIL(mb))
			count = MIDIBUF_AVAIL(mb);
		if (count > uio->uio_resid)
			count = uio->uio_resid;
		mtx_leave(&audio_lock);
		error = uiomove(mb->data + MIDIBUF_END(mb), count, uio);
		if (error)
			goto done;
		mtx_enter(&audio_lock);
		mb->used += count;
		midi_out_start(sc);
	}

done_mtx:
	mtx_leave(&audio_lock);
done:
	device_unref(&sc->dev);
	return error;
}

int
midikqfilter(dev_t dev, struct knote *kn)
{
	struct midi_softc *sc;
	struct klist 	  *klist;
	int error;

	sc = (struct midi_softc *)device_lookup(&midi_cd, minor(dev));
	if (sc == NULL)
		return ENXIO;
	error = 0;
	switch (kn->kn_filter) {
	case EVFILT_READ:
		klist = &sc->inbuf.klist;
		kn->kn_fop = &midiread_filtops;
		break;
	case EVFILT_WRITE:
		klist = &sc->outbuf.klist;
		kn->kn_fop = &midiwrite_filtops;
		break;
	default:
		error = EINVAL;
		goto done;
	}
	kn->kn_hook = (void *)sc;

	klist_insert(klist, kn);
done:
	device_unref(&sc->dev);
	return error;
}

void
filt_midirdetach(struct knote *kn)
{
	struct midi_softc *sc = (struct midi_softc *)kn->kn_hook;

	klist_remove(&sc->inbuf.klist, kn);
}

int
filt_midiread(struct knote *kn, long hint)
{
	struct midi_softc *sc = (struct midi_softc *)kn->kn_hook;

	return (!MIDIBUF_ISEMPTY(&sc->inbuf));
}

void
filt_midiwdetach(struct knote *kn)
{
	struct midi_softc *sc = (struct midi_softc *)kn->kn_hook;

	klist_remove(&sc->outbuf.klist, kn);
}

int
filt_midiwrite(struct knote *kn, long hint)
{
	struct midi_softc *sc = (struct midi_softc *)kn->kn_hook;

	return (!MIDIBUF_ISFULL(&sc->outbuf));
}

int
filt_midimodify(struct kevent *kev, struct knote *kn)
{
	int active;

	mtx_enter(&audio_lock);
	active = knote_modify(kev, kn);
	mtx_leave(&audio_lock);

	return active;
}

int
filt_midiprocess(struct knote *kn, struct kevent *kev)
{
	int active;

	mtx_enter(&audio_lock);
	active = knote_process(kn, kev);
	mtx_leave(&audio_lock);

	return active;
}

int
midiioctl(dev_t dev, u_long cmd, caddr_t addr, int flag, struct proc *p)
{
	struct midi_softc *sc;
	int error;

	sc = (struct midi_softc *)device_lookup(&midi_cd, minor(dev));
	if (sc == NULL)
		return ENXIO;
	error = 0;
	switch(cmd) {
	default:
		error = ENOTTY;
	}
	device_unref(&sc->dev);
	return error;
}

int
midiopen(dev_t dev, int flags, int mode, struct proc *p)
{
	struct midi_softc *sc;
	int error;

	sc = (struct midi_softc *)device_lookup(&midi_cd, minor(dev));
	if (sc == NULL)
		return ENXIO;
	error = 0;
	if (sc->flags) {
		error = EBUSY;
		goto done;
	}
	MIDIBUF_INIT(&sc->inbuf);
	MIDIBUF_INIT(&sc->outbuf);
	sc->isbusy = 0;
	sc->inbuf.blocking = sc->outbuf.blocking = 0;
	sc->flags = flags;
	error = sc->hw_if->open(sc->hw_hdl, flags, midi_iintr, midi_ointr, sc);
	if (error)
		sc->flags = 0;
done:
	device_unref(&sc->dev);
	return error;
}

int
midiclose(dev_t dev, int fflag, int devtype, struct proc *p)
{
	struct midi_softc *sc;
	struct midi_buffer *mb;
	int error;

	sc = (struct midi_softc *)device_lookup(&midi_cd, minor(dev));
	if (sc == NULL)
		return ENXIO;

	/* start draining output buffer */
	error = 0;
	mb = &sc->outbuf;
	mtx_enter(&audio_lock);
	if (!MIDIBUF_ISEMPTY(mb))
		midi_out_start(sc);
	while (sc->isbusy) {
		sc->outbuf.blocking = 1;
		error = msleep_nsec(&sc->outbuf.blocking, &audio_lock,
		    PWAIT, "mid_dr", SEC_TO_NSEC(5));
		if (!(sc->dev.dv_flags & DVF_ACTIVE))
			error = EIO;
		if (error)
			break;
	}
	mtx_leave(&audio_lock);

	/*
	 * some hw_if->close() reset immediately the midi uart
	 * which flushes the internal buffer of the uart device,
	 * so we may lose some (important) data. To avoid this,
	 * sleep 20ms (around 64 bytes) to give the time to the
	 * uart to drain its internal buffers.
	 */
	tsleep_nsec(&sc->outbuf.blocking, PWAIT, "mid_cl", MSEC_TO_NSEC(20));
	sc->hw_if->close(sc->hw_hdl);
	sc->flags = 0;
	device_unref(&sc->dev);
	return 0;
}

int
midiprobe(struct device *parent, void *match, void *aux)
{
	struct audio_attach_args *sa = aux;

	return (sa != NULL && (sa->type == AUDIODEV_TYPE_MIDI) ? 1 : 0);
}

void
midiattach(struct device *parent, struct device *self, void *aux)
{
	struct midi_info	  mi;
	struct midi_softc        *sc = (struct midi_softc *)self;
	struct audio_attach_args *sa = (struct audio_attach_args *)aux;
	const struct midi_hw_if  *hwif = sa->hwif;
	void  			 *hdl = sa->hdl;

#ifdef DIAGNOSTIC
	if (hwif == 0 ||
	    hwif->open == 0 ||
	    hwif->close == 0 ||
	    hwif->output == 0 ||
	    hwif->getinfo == 0) {
		printf("%s: missing method\n", DEVNAME(sc));
		return;
	}
#endif

	klist_init_mutex(&sc->inbuf.klist, &audio_lock);
	klist_init_mutex(&sc->outbuf.klist, &audio_lock);

	sc->hw_if = hwif;
	sc->hw_hdl = hdl;
	sc->hw_if->getinfo(sc->hw_hdl, &mi);
	sc->props = mi.props;
	sc->flags = 0;
	timeout_set(&sc->timeo, midi_timeout, sc);
	printf(": <%s>\n", mi.name);
}

int
mididetach(struct device *self, int flags)
{
	struct midi_softc *sc = (struct midi_softc *)self;
	int maj, mn;

	/* locate the major number */
	for (maj = 0; maj < nchrdev; maj++) {
		if (cdevsw[maj].d_open == midiopen) {
			/* Nuke the vnodes for any open instances (calls close). */
			mn = self->dv_unit;
			vdevgone(maj, mn, mn, VCHR);
		}
	}

	/*
	 * The close() method did nothing (device_lookup() returns
	 * NULL), so quickly halt transfers (normally parent is already
	 * gone, and code below is no-op), and wake-up user-land blocked
	 * in read/write/ioctl, which return EIO.
	 */
	if (sc->flags) {
		KERNEL_ASSERT_LOCKED();
		if (sc->flags & FREAD)
			wakeup(&sc->inbuf.blocking);
		if (sc->flags & FWRITE)
			wakeup(&sc->outbuf.blocking);
		sc->hw_if->close(sc->hw_hdl);
		sc->flags = 0;
	}

	klist_invalidate(&sc->inbuf.klist);
	klist_invalidate(&sc->outbuf.klist);
	klist_free(&sc->inbuf.klist);
	klist_free(&sc->outbuf.klist);

	return 0;
}

int
midiprint(void *aux, const char *pnp)
{
	if (pnp)
		printf("midi at %s", pnp);
	return (UNCONF);
}

struct device *
midi_attach_mi(const struct midi_hw_if *hwif, void *hdl, struct device *dev)
{
	struct audio_attach_args arg;

	arg.type = AUDIODEV_TYPE_MIDI;
	arg.hwif = hwif;
	arg.hdl = hdl;
	return config_found(dev, &arg, midiprint);
}
