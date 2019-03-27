/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2009 Ariff Abdullah <ariff@FreeBSD.org>
 * Portions Copyright (c) Ryan Beasley <ryan.beasley@gmail.com> - GSoC 2006
 * Copyright (c) 1999 Cameron Grant <cg@FreeBSD.org>
 * Portions Copyright (c) Luigi Rizzo <luigi@FreeBSD.org> - 1997-99
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

#include "opt_isa.h"

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/pcm/vchan.h>

#include "feeder_if.h"

SND_DECLARE_FILE("$FreeBSD$");

int report_soft_formats = 1;
SYSCTL_INT(_hw_snd, OID_AUTO, report_soft_formats, CTLFLAG_RW,
	&report_soft_formats, 0, "report software-emulated formats");

int report_soft_matrix = 1;
SYSCTL_INT(_hw_snd, OID_AUTO, report_soft_matrix, CTLFLAG_RW,
	&report_soft_matrix, 0, "report software-emulated channel matrixing");

int chn_latency = CHN_LATENCY_DEFAULT;

static int
sysctl_hw_snd_latency(SYSCTL_HANDLER_ARGS)
{
	int err, val;

	val = chn_latency;
	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err != 0 || req->newptr == NULL)
		return err;
	if (val < CHN_LATENCY_MIN || val > CHN_LATENCY_MAX)
		err = EINVAL;
	else
		chn_latency = val;

	return err;
}
SYSCTL_PROC(_hw_snd, OID_AUTO, latency, CTLTYPE_INT | CTLFLAG_RWTUN,
	0, sizeof(int), sysctl_hw_snd_latency, "I",
	"buffering latency (0=low ... 10=high)");

int chn_latency_profile = CHN_LATENCY_PROFILE_DEFAULT;

static int
sysctl_hw_snd_latency_profile(SYSCTL_HANDLER_ARGS)
{
	int err, val;

	val = chn_latency_profile;
	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err != 0 || req->newptr == NULL)
		return err;
	if (val < CHN_LATENCY_PROFILE_MIN || val > CHN_LATENCY_PROFILE_MAX)
		err = EINVAL;
	else
		chn_latency_profile = val;

	return err;
}
SYSCTL_PROC(_hw_snd, OID_AUTO, latency_profile, CTLTYPE_INT | CTLFLAG_RWTUN,
	0, sizeof(int), sysctl_hw_snd_latency_profile, "I",
	"buffering latency profile (0=aggressive 1=safe)");

static int chn_timeout = CHN_TIMEOUT;

static int
sysctl_hw_snd_timeout(SYSCTL_HANDLER_ARGS)
{
	int err, val;

	val = chn_timeout;
	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err != 0 || req->newptr == NULL)
		return err;
	if (val < CHN_TIMEOUT_MIN || val > CHN_TIMEOUT_MAX)
		err = EINVAL;
	else
		chn_timeout = val;

	return err;
}
SYSCTL_PROC(_hw_snd, OID_AUTO, timeout, CTLTYPE_INT | CTLFLAG_RWTUN,
	0, sizeof(int), sysctl_hw_snd_timeout, "I",
	"interrupt timeout (1 - 10) seconds");

static int chn_vpc_autoreset = 1;
SYSCTL_INT(_hw_snd, OID_AUTO, vpc_autoreset, CTLFLAG_RWTUN,
	&chn_vpc_autoreset, 0, "automatically reset channels volume to 0db");

static int chn_vol_0db_pcm = SND_VOL_0DB_PCM;

static void
chn_vpc_proc(int reset, int db)
{
	struct snddev_info *d;
	struct pcm_channel *c;
	int i;

	for (i = 0; pcm_devclass != NULL &&
	    i < devclass_get_maxunit(pcm_devclass); i++) {
		d = devclass_get_softc(pcm_devclass, i);
		if (!PCM_REGISTERED(d))
			continue;
		PCM_LOCK(d);
		PCM_WAIT(d);
		PCM_ACQUIRE(d);
		CHN_FOREACH(c, d, channels.pcm) {
			CHN_LOCK(c);
			CHN_SETVOLUME(c, SND_VOL_C_PCM, SND_CHN_T_VOL_0DB, db);
			if (reset != 0)
				chn_vpc_reset(c, SND_VOL_C_PCM, 1);
			CHN_UNLOCK(c);
		}
		PCM_RELEASE(d);
		PCM_UNLOCK(d);
	}
}

static int
sysctl_hw_snd_vpc_0db(SYSCTL_HANDLER_ARGS)
{
	int err, val;

	val = chn_vol_0db_pcm;
	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);
	if (val < SND_VOL_0DB_MIN || val > SND_VOL_0DB_MAX)
		return (EINVAL);

	chn_vol_0db_pcm = val;
	chn_vpc_proc(0, val);

	return (0);
}
SYSCTL_PROC(_hw_snd, OID_AUTO, vpc_0db, CTLTYPE_INT | CTLFLAG_RWTUN,
	0, sizeof(int), sysctl_hw_snd_vpc_0db, "I",
	"0db relative level");

static int
sysctl_hw_snd_vpc_reset(SYSCTL_HANDLER_ARGS)
{
	int err, val;

	val = 0;
	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err != 0 || req->newptr == NULL || val == 0)
		return (err);

	chn_vol_0db_pcm = SND_VOL_0DB_PCM;
	chn_vpc_proc(1, SND_VOL_0DB_PCM);

	return (0);
}
SYSCTL_PROC(_hw_snd, OID_AUTO, vpc_reset, CTLTYPE_INT | CTLFLAG_RW,
	0, sizeof(int), sysctl_hw_snd_vpc_reset, "I",
	"reset volume on all channels");

static int chn_usefrags = 0;
static int chn_syncdelay = -1;

SYSCTL_INT(_hw_snd, OID_AUTO, usefrags, CTLFLAG_RWTUN,
	&chn_usefrags, 0, "prefer setfragments() over setblocksize()");
SYSCTL_INT(_hw_snd, OID_AUTO, syncdelay, CTLFLAG_RWTUN,
	&chn_syncdelay, 0,
	"append (0-1000) millisecond trailing buffer delay on each sync");

/**
 * @brief Channel sync group lock
 *
 * Clients should acquire this lock @b without holding any channel locks
 * before touching syncgroups or the main syncgroup list.
 */
struct mtx snd_pcm_syncgroups_mtx;
MTX_SYSINIT(pcm_syncgroup, &snd_pcm_syncgroups_mtx, "PCM channel sync group lock", MTX_DEF);
/**
 * @brief syncgroups' master list
 *
 * Each time a channel syncgroup is created, it's added to this list.  This
 * list should only be accessed with @sa snd_pcm_syncgroups_mtx held.
 *
 * See SNDCTL_DSP_SYNCGROUP for more information.
 */
struct pcm_synclist snd_pcm_syncgroups = SLIST_HEAD_INITIALIZER(snd_pcm_syncgroups);

static void
chn_lockinit(struct pcm_channel *c, int dir)
{
	switch (dir) {
	case PCMDIR_PLAY:
		c->lock = snd_mtxcreate(c->name, "pcm play channel");
		cv_init(&c->intr_cv, "pcmwr");
		break;
	case PCMDIR_PLAY_VIRTUAL:
		c->lock = snd_mtxcreate(c->name, "pcm virtual play channel");
		cv_init(&c->intr_cv, "pcmwrv");
		break;
	case PCMDIR_REC:
		c->lock = snd_mtxcreate(c->name, "pcm record channel");
		cv_init(&c->intr_cv, "pcmrd");
		break;
	case PCMDIR_REC_VIRTUAL:
		c->lock = snd_mtxcreate(c->name, "pcm virtual record channel");
		cv_init(&c->intr_cv, "pcmrdv");
		break;
	default:
		panic("%s(): Invalid direction=%d", __func__, dir);
		break;
	}

	cv_init(&c->cv, "pcmchn");
}

static void
chn_lockdestroy(struct pcm_channel *c)
{
	CHN_LOCKASSERT(c);

	CHN_BROADCAST(&c->cv);
	CHN_BROADCAST(&c->intr_cv);

	cv_destroy(&c->cv);
	cv_destroy(&c->intr_cv);

	snd_mtxfree(c->lock);
}

/**
 * @brief Determine channel is ready for I/O
 *
 * @retval 1 = ready for I/O
 * @retval 0 = not ready for I/O
 */
static int
chn_polltrigger(struct pcm_channel *c)
{
	struct snd_dbuf *bs = c->bufsoft;
	u_int delta;

	CHN_LOCKASSERT(c);

	if (c->flags & CHN_F_MMAP) {
		if (sndbuf_getprevtotal(bs) < c->lw)
			delta = c->lw;
		else
			delta = sndbuf_gettotal(bs) - sndbuf_getprevtotal(bs);
	} else {
		if (c->direction == PCMDIR_PLAY)
			delta = sndbuf_getfree(bs);
		else
			delta = sndbuf_getready(bs);
	}

	return ((delta < c->lw) ? 0 : 1);
}

static void
chn_pollreset(struct pcm_channel *c)
{

	CHN_LOCKASSERT(c);
	sndbuf_updateprevtotal(c->bufsoft);
}

static void
chn_wakeup(struct pcm_channel *c)
{
	struct snd_dbuf *bs;
	struct pcm_channel *ch;

	CHN_LOCKASSERT(c);

	bs = c->bufsoft;

	if (CHN_EMPTY(c, children.busy)) {
		if (SEL_WAITING(sndbuf_getsel(bs)) && chn_polltrigger(c))
			selwakeuppri(sndbuf_getsel(bs), PRIBIO);
		if (c->flags & CHN_F_SLEEPING) {
			/*
			 * Ok, I can just panic it right here since it is
			 * quite obvious that we never allow multiple waiters
			 * from userland. I'm too generous...
			 */
			CHN_BROADCAST(&c->intr_cv);
		}
	} else {
		CHN_FOREACH(ch, c, children.busy) {
			CHN_LOCK(ch);
			chn_wakeup(ch);
			CHN_UNLOCK(ch);
		}
	}
}

static int
chn_sleep(struct pcm_channel *c, int timeout)
{
	int ret;

	CHN_LOCKASSERT(c);

	if (c->flags & CHN_F_DEAD)
		return (EINVAL);

	c->flags |= CHN_F_SLEEPING;
	ret = cv_timedwait_sig(&c->intr_cv, c->lock, timeout);
	c->flags &= ~CHN_F_SLEEPING;

	return ((c->flags & CHN_F_DEAD) ? EINVAL : ret);
}

/*
 * chn_dmaupdate() tracks the status of a dma transfer,
 * updating pointers.
 */

static unsigned int
chn_dmaupdate(struct pcm_channel *c)
{
	struct snd_dbuf *b = c->bufhard;
	unsigned int delta, old, hwptr, amt;

	KASSERT(sndbuf_getsize(b) > 0, ("bufsize == 0"));
	CHN_LOCKASSERT(c);

	old = sndbuf_gethwptr(b);
	hwptr = chn_getptr(c);
	delta = (sndbuf_getsize(b) + hwptr - old) % sndbuf_getsize(b);
	sndbuf_sethwptr(b, hwptr);

	if (c->direction == PCMDIR_PLAY) {
		amt = min(delta, sndbuf_getready(b));
		amt -= amt % sndbuf_getalign(b);
		if (amt > 0)
			sndbuf_dispose(b, NULL, amt);
	} else {
		amt = min(delta, sndbuf_getfree(b));
		amt -= amt % sndbuf_getalign(b);
		if (amt > 0)
		       sndbuf_acquire(b, NULL, amt);
	}
	if (snd_verbose > 3 && CHN_STARTED(c) && delta == 0) {
		device_printf(c->dev, "WARNING: %s DMA completion "
			"too fast/slow ! hwptr=%u, old=%u "
			"delta=%u amt=%u ready=%u free=%u\n",
			CHN_DIRSTR(c), hwptr, old, delta, amt,
			sndbuf_getready(b), sndbuf_getfree(b));
	}

	return delta;
}

static void
chn_wrfeed(struct pcm_channel *c)
{
    	struct snd_dbuf *b = c->bufhard;
    	struct snd_dbuf *bs = c->bufsoft;
	unsigned int amt, want, wasfree;

	CHN_LOCKASSERT(c);

	if ((c->flags & CHN_F_MMAP) && !(c->flags & CHN_F_CLOSING))
		sndbuf_acquire(bs, NULL, sndbuf_getfree(bs));

	wasfree = sndbuf_getfree(b);
	want = min(sndbuf_getsize(b),
	    imax(0, sndbuf_xbytes(sndbuf_getsize(bs), bs, b) -
	     sndbuf_getready(b)));
	amt = min(wasfree, want);
	if (amt > 0)
		sndbuf_feed(bs, b, c, c->feeder, amt);

	/*
	 * Possible xruns. There should be no empty space left in buffer.
	 */
	if (sndbuf_getready(b) < want)
		c->xruns++;

	if (sndbuf_getfree(b) < wasfree)
		chn_wakeup(c);
}

#if 0
static void
chn_wrupdate(struct pcm_channel *c)
{

	CHN_LOCKASSERT(c);
	KASSERT(c->direction == PCMDIR_PLAY, ("%s(): bad channel", __func__));

	if ((c->flags & (CHN_F_MMAP | CHN_F_VIRTUAL)) || CHN_STOPPED(c))
		return;
	chn_dmaupdate(c);
	chn_wrfeed(c);
	/* tell the driver we've updated the primary buffer */
	chn_trigger(c, PCMTRIG_EMLDMAWR);
}
#endif

static void
chn_wrintr(struct pcm_channel *c)
{

	CHN_LOCKASSERT(c);
	/* update pointers in primary buffer */
	chn_dmaupdate(c);
	/* ...and feed from secondary to primary */
	chn_wrfeed(c);
	/* tell the driver we've updated the primary buffer */
	chn_trigger(c, PCMTRIG_EMLDMAWR);
}

/*
 * user write routine - uiomove data into secondary buffer, trigger if necessary
 * if blocking, sleep, rinse and repeat.
 *
 * called externally, so must handle locking
 */

int
chn_write(struct pcm_channel *c, struct uio *buf)
{
	struct snd_dbuf *bs = c->bufsoft;
	void *off;
	int ret, timeout, sz, t, p;

	CHN_LOCKASSERT(c);

	ret = 0;
	timeout = chn_timeout * hz;

	while (ret == 0 && buf->uio_resid > 0) {
		sz = min(buf->uio_resid, sndbuf_getfree(bs));
		if (sz > 0) {
			/*
			 * The following assumes that the free space in
			 * the buffer can never be less around the
			 * unlock-uiomove-lock sequence.
			 */
			while (ret == 0 && sz > 0) {
				p = sndbuf_getfreeptr(bs);
				t = min(sz, sndbuf_getsize(bs) - p);
				off = sndbuf_getbufofs(bs, p);
				CHN_UNLOCK(c);
				ret = uiomove(off, t, buf);
				CHN_LOCK(c);
				sz -= t;
				sndbuf_acquire(bs, NULL, t);
			}
			ret = 0;
			if (CHN_STOPPED(c) && !(c->flags & CHN_F_NOTRIGGER)) {
				ret = chn_start(c, 0);
				if (ret != 0)
					c->flags |= CHN_F_DEAD;
			}
		} else if (c->flags & (CHN_F_NBIO | CHN_F_NOTRIGGER)) {
			/**
			 * @todo Evaluate whether EAGAIN is truly desirable.
			 * 	 4Front drivers behave like this, but I'm
			 * 	 not sure if it at all violates the "write
			 * 	 should be allowed to block" model.
			 *
			 * 	 The idea is that, while set with CHN_F_NOTRIGGER,
			 * 	 a channel isn't playing, *but* without this we
			 * 	 end up with "interrupt timeout / channel dead".
			 */
			ret = EAGAIN;
		} else {
   			ret = chn_sleep(c, timeout);
			if (ret == EAGAIN) {
				ret = EINVAL;
				c->flags |= CHN_F_DEAD;
				device_printf(c->dev, "%s(): %s: "
				    "play interrupt timeout, channel dead\n",
				    __func__, c->name);
			} else if (ret == ERESTART || ret == EINTR)
				c->flags |= CHN_F_ABORTING;
		}
	}

	return (ret);
}

/*
 * Feed new data from the read buffer. Can be called in the bottom half.
 */
static void
chn_rdfeed(struct pcm_channel *c)
{
    	struct snd_dbuf *b = c->bufhard;
    	struct snd_dbuf *bs = c->bufsoft;
	unsigned int amt;

	CHN_LOCKASSERT(c);

	if (c->flags & CHN_F_MMAP)
		sndbuf_dispose(bs, NULL, sndbuf_getready(bs));

	amt = sndbuf_getfree(bs);
	if (amt > 0)
		sndbuf_feed(b, bs, c, c->feeder, amt);

	amt = sndbuf_getready(b);
	if (amt > 0) {
		c->xruns++;
		sndbuf_dispose(b, NULL, amt);
	}

	if (sndbuf_getready(bs) > 0)
		chn_wakeup(c);
}

#if 0
static void
chn_rdupdate(struct pcm_channel *c)
{

	CHN_LOCKASSERT(c);
	KASSERT(c->direction == PCMDIR_REC, ("chn_rdupdate on bad channel"));

	if ((c->flags & (CHN_F_MMAP | CHN_F_VIRTUAL)) || CHN_STOPPED(c))
		return;
	chn_trigger(c, PCMTRIG_EMLDMARD);
	chn_dmaupdate(c);
	chn_rdfeed(c);
}
#endif

/* read interrupt routine. Must be called with interrupts blocked. */
static void
chn_rdintr(struct pcm_channel *c)
{

	CHN_LOCKASSERT(c);
	/* tell the driver to update the primary buffer if non-dma */
	chn_trigger(c, PCMTRIG_EMLDMARD);
	/* update pointers in primary buffer */
	chn_dmaupdate(c);
	/* ...and feed from primary to secondary */
	chn_rdfeed(c);
}

/*
 * user read routine - trigger if necessary, uiomove data from secondary buffer
 * if blocking, sleep, rinse and repeat.
 *
 * called externally, so must handle locking
 */

int
chn_read(struct pcm_channel *c, struct uio *buf)
{
	struct snd_dbuf *bs = c->bufsoft;
	void *off;
	int ret, timeout, sz, t, p;

	CHN_LOCKASSERT(c);

	if (CHN_STOPPED(c) && !(c->flags & CHN_F_NOTRIGGER)) {
		ret = chn_start(c, 0);
		if (ret != 0) {
			c->flags |= CHN_F_DEAD;
			return (ret);
		}
	}

	ret = 0;
	timeout = chn_timeout * hz;

	while (ret == 0 && buf->uio_resid > 0) {
		sz = min(buf->uio_resid, sndbuf_getready(bs));
		if (sz > 0) {
			/*
			 * The following assumes that the free space in
			 * the buffer can never be less around the
			 * unlock-uiomove-lock sequence.
			 */
			while (ret == 0 && sz > 0) {
				p = sndbuf_getreadyptr(bs);
				t = min(sz, sndbuf_getsize(bs) - p);
				off = sndbuf_getbufofs(bs, p);
				CHN_UNLOCK(c);
				ret = uiomove(off, t, buf);
				CHN_LOCK(c);
				sz -= t;
				sndbuf_dispose(bs, NULL, t);
			}
			ret = 0;
		} else if (c->flags & (CHN_F_NBIO | CHN_F_NOTRIGGER))
			ret = EAGAIN;
		else {
   			ret = chn_sleep(c, timeout);
			if (ret == EAGAIN) {
				ret = EINVAL;
				c->flags |= CHN_F_DEAD;
				device_printf(c->dev, "%s(): %s: "
				    "record interrupt timeout, channel dead\n",
				    __func__, c->name);
			} else if (ret == ERESTART || ret == EINTR)
				c->flags |= CHN_F_ABORTING;
		}
	}

	return (ret);
}

void
chn_intr_locked(struct pcm_channel *c)
{

	CHN_LOCKASSERT(c);

	c->interrupts++;

	if (c->direction == PCMDIR_PLAY)
		chn_wrintr(c);
	else
		chn_rdintr(c);
}

void
chn_intr(struct pcm_channel *c)
{

	if (CHN_LOCKOWNED(c)) {
		chn_intr_locked(c);
		return;
	}

	CHN_LOCK(c);
	chn_intr_locked(c);
	CHN_UNLOCK(c);
}

u_int32_t
chn_start(struct pcm_channel *c, int force)
{
	u_int32_t i, j;
	struct snd_dbuf *b = c->bufhard;
	struct snd_dbuf *bs = c->bufsoft;
	int err;

	CHN_LOCKASSERT(c);
	/* if we're running, or if we're prevented from triggering, bail */
	if (CHN_STARTED(c) || ((c->flags & CHN_F_NOTRIGGER) && !force))
		return (EINVAL);

	err = 0;

	if (force) {
		i = 1;
		j = 0;
	} else {
		if (c->direction == PCMDIR_REC) {
			i = sndbuf_getfree(bs);
			j = (i > 0) ? 1 : sndbuf_getready(b);
		} else {
			if (sndbuf_getfree(bs) == 0) {
				i = 1;
				j = 0;
			} else {
				struct snd_dbuf *pb;

				pb = CHN_BUF_PARENT(c, b);
				i = sndbuf_xbytes(sndbuf_getready(bs), bs, pb);
				j = sndbuf_getalign(pb);
			}
		}
		if (snd_verbose > 3 && CHN_EMPTY(c, children))
			device_printf(c->dev, "%s(): %s (%s) threshold "
			    "i=%d j=%d\n", __func__, CHN_DIRSTR(c),
			    (c->flags & CHN_F_VIRTUAL) ? "virtual" :
			    "hardware", i, j);
	}

	if (i >= j) {
		c->flags |= CHN_F_TRIGGERED;
		sndbuf_setrun(b, 1);
		if (c->flags & CHN_F_CLOSING)
			c->feedcount = 2;
		else {
			c->feedcount = 0;
			c->interrupts = 0;
			c->xruns = 0;
		}
		if (c->parentchannel == NULL) {
			if (c->direction == PCMDIR_PLAY)
				sndbuf_fillsilence_rl(b,
				    sndbuf_xbytes(sndbuf_getsize(bs), bs, b));
			if (snd_verbose > 3)
				device_printf(c->dev,
				    "%s(): %s starting! (%s/%s) "
				    "(ready=%d force=%d i=%d j=%d "
				    "intrtimeout=%u latency=%dms)\n",
				    __func__,
				    (c->flags & CHN_F_HAS_VCHAN) ?
				    "VCHAN PARENT" : "HW", CHN_DIRSTR(c),
				    (c->flags & CHN_F_CLOSING) ? "closing" :
				    "running",
				    sndbuf_getready(b),
				    force, i, j, c->timeout,
				    (sndbuf_getsize(b) * 1000) /
				    (sndbuf_getalign(b) * sndbuf_getspd(b)));
		}
		err = chn_trigger(c, PCMTRIG_START);
	}

	return (err);
}

void
chn_resetbuf(struct pcm_channel *c)
{
	struct snd_dbuf *b = c->bufhard;
	struct snd_dbuf *bs = c->bufsoft;

	c->blocks = 0;
	sndbuf_reset(b);
	sndbuf_reset(bs);
}

/*
 * chn_sync waits until the space in the given channel goes above
 * a threshold. The threshold is checked against fl or rl respectively.
 * Assume that the condition can become true, do not check here...
 */
int
chn_sync(struct pcm_channel *c, int threshold)
{
    	struct snd_dbuf *b, *bs;
	int ret, count, hcount, minflush, resid, residp, syncdelay, blksz;
	u_int32_t cflag;

	CHN_LOCKASSERT(c);

	if (c->direction != PCMDIR_PLAY)
		return (EINVAL);

	bs = c->bufsoft;

	if ((c->flags & (CHN_F_DEAD | CHN_F_ABORTING)) ||
	    (threshold < 1 && sndbuf_getready(bs) < 1))
		return (0);

	/* if we haven't yet started and nothing is buffered, else start*/
	if (CHN_STOPPED(c)) {
		if (threshold > 0 || sndbuf_getready(bs) > 0) {
			ret = chn_start(c, 1);
			if (ret != 0)
				return (ret);
		} else
			return (0);
	}

	b = CHN_BUF_PARENT(c, c->bufhard);

	minflush = threshold + sndbuf_xbytes(sndbuf_getready(b), b, bs);

	syncdelay = chn_syncdelay;

	if (syncdelay < 0 && (threshold > 0 || sndbuf_getready(bs) > 0))
		minflush += sndbuf_xbytes(sndbuf_getsize(b), b, bs);

	/*
	 * Append (0-1000) millisecond trailing buffer (if needed)
	 * for slower / high latency hardwares (notably USB audio)
	 * to avoid audible truncation.
	 */
	if (syncdelay > 0)
		minflush += (sndbuf_getalign(bs) * sndbuf_getspd(bs) *
		    ((syncdelay > 1000) ? 1000 : syncdelay)) / 1000;

	minflush -= minflush % sndbuf_getalign(bs);

	if (minflush > 0) {
		threshold = min(minflush, sndbuf_getfree(bs));
		sndbuf_clear(bs, threshold);
		sndbuf_acquire(bs, NULL, threshold);
		minflush -= threshold;
	}

	resid = sndbuf_getready(bs);
	residp = resid;
	blksz = sndbuf_getblksz(b);
	if (blksz < 1) {
		device_printf(c->dev,
		    "%s(): WARNING: blksz < 1 ! maxsize=%d [%d/%d/%d]\n",
		    __func__, sndbuf_getmaxsize(b), sndbuf_getsize(b),
		    sndbuf_getblksz(b), sndbuf_getblkcnt(b));
		if (sndbuf_getblkcnt(b) > 0)
			blksz = sndbuf_getsize(b) / sndbuf_getblkcnt(b);
		if (blksz < 1)
			blksz = 1;
	}
	count = sndbuf_xbytes(minflush + resid, bs, b) / blksz;
	hcount = count;
	ret = 0;

	if (snd_verbose > 3)
		device_printf(c->dev, "%s(): [begin] timeout=%d count=%d "
		    "minflush=%d resid=%d\n", __func__, c->timeout, count,
		    minflush, resid);

	cflag = c->flags & CHN_F_CLOSING;
	c->flags |= CHN_F_CLOSING;
	while (count > 0 && (resid > 0 || minflush > 0)) {
		ret = chn_sleep(c, c->timeout);
    		if (ret == ERESTART || ret == EINTR) {
			c->flags |= CHN_F_ABORTING;
			break;
		} else if (ret == 0 || ret == EAGAIN) {
			resid = sndbuf_getready(bs);
			if (resid == residp) {
				--count;
				if (snd_verbose > 3)
					device_printf(c->dev,
					    "%s(): [stalled] timeout=%d "
					    "count=%d hcount=%d "
					    "resid=%d minflush=%d\n",
					    __func__, c->timeout, count,
					    hcount, resid, minflush);
			} else if (resid < residp && count < hcount) {
				++count;
				if (snd_verbose > 3)
					device_printf(c->dev,
					    "%s((): [resume] timeout=%d "
					    "count=%d hcount=%d "
					    "resid=%d minflush=%d\n",
					    __func__, c->timeout, count,
					    hcount, resid, minflush);
			}
			if (minflush > 0 && sndbuf_getfree(bs) > 0) {
				threshold = min(minflush,
				    sndbuf_getfree(bs));
				sndbuf_clear(bs, threshold);
				sndbuf_acquire(bs, NULL, threshold);
				resid = sndbuf_getready(bs);
				minflush -= threshold;
			}
			residp = resid;
		} else
			break;
	}
	c->flags &= ~CHN_F_CLOSING;
	c->flags |= cflag;

	if (snd_verbose > 3)
		device_printf(c->dev,
		    "%s(): timeout=%d count=%d hcount=%d resid=%d residp=%d "
		    "minflush=%d ret=%d\n",
		    __func__, c->timeout, count, hcount, resid, residp,
		    minflush, ret);

    	return (0);
}

/* called externally, handle locking */
int
chn_poll(struct pcm_channel *c, int ev, struct thread *td)
{
	struct snd_dbuf *bs = c->bufsoft;
	int ret;

	CHN_LOCKASSERT(c);

    	if (!(c->flags & (CHN_F_MMAP | CHN_F_TRIGGERED))) {
		ret = chn_start(c, 1);
		if (ret != 0)
			return (0);
	}

	ret = 0;
	if (chn_polltrigger(c)) {
		chn_pollreset(c);
		ret = ev;
	} else
		selrecord(td, sndbuf_getsel(bs));

	return (ret);
}

/*
 * chn_abort terminates a running dma transfer.  it may sleep up to 200ms.
 * it returns the number of bytes that have not been transferred.
 *
 * called from: dsp_close, dsp_ioctl, with channel locked
 */
int
chn_abort(struct pcm_channel *c)
{
    	int missing = 0;
    	struct snd_dbuf *b = c->bufhard;
    	struct snd_dbuf *bs = c->bufsoft;

	CHN_LOCKASSERT(c);
	if (CHN_STOPPED(c))
		return 0;
	c->flags |= CHN_F_ABORTING;

	c->flags &= ~CHN_F_TRIGGERED;
	/* kill the channel */
	chn_trigger(c, PCMTRIG_ABORT);
	sndbuf_setrun(b, 0);
	if (!(c->flags & CHN_F_VIRTUAL))
		chn_dmaupdate(c);
    	missing = sndbuf_getready(bs);

	c->flags &= ~CHN_F_ABORTING;
	return missing;
}

/*
 * this routine tries to flush the dma transfer. It is called
 * on a close of a playback channel.
 * first, if there is data in the buffer, but the dma has not yet
 * begun, we need to start it.
 * next, we wait for the play buffer to drain
 * finally, we stop the dma.
 *
 * called from: dsp_close, not valid for record channels.
 */

int
chn_flush(struct pcm_channel *c)
{
    	struct snd_dbuf *b = c->bufhard;

	CHN_LOCKASSERT(c);
	KASSERT(c->direction == PCMDIR_PLAY, ("chn_flush on bad channel"));
    	DEB(printf("chn_flush: c->flags 0x%08x\n", c->flags));

	c->flags |= CHN_F_CLOSING;
	chn_sync(c, 0);
	c->flags &= ~CHN_F_TRIGGERED;
	/* kill the channel */
	chn_trigger(c, PCMTRIG_ABORT);
	sndbuf_setrun(b, 0);

    	c->flags &= ~CHN_F_CLOSING;
    	return 0;
}

int
snd_fmtvalid(uint32_t fmt, uint32_t *fmtlist)
{
	int i;

	for (i = 0; fmtlist[i] != 0; i++) {
		if (fmt == fmtlist[i] ||
		    ((fmt & AFMT_PASSTHROUGH) &&
		    (AFMT_ENCODING(fmt) & fmtlist[i])))
			return (1);
	}

	return (0);
}

static const struct {
	char *name, *alias1, *alias2;
	uint32_t afmt;
} afmt_tab[] = {
	{  "alaw",  NULL, NULL, AFMT_A_LAW  },
	{ "mulaw",  NULL, NULL, AFMT_MU_LAW },
	{    "u8",   "8", NULL, AFMT_U8     },
	{    "s8",  NULL, NULL, AFMT_S8     },
#if BYTE_ORDER == LITTLE_ENDIAN
	{ "s16le", "s16", "16", AFMT_S16_LE },
	{ "s16be",  NULL, NULL, AFMT_S16_BE },
#else
	{ "s16le",  NULL, NULL, AFMT_S16_LE },
	{ "s16be", "s16", "16", AFMT_S16_BE },
#endif
	{ "u16le",  NULL, NULL, AFMT_U16_LE },
	{ "u16be",  NULL, NULL, AFMT_U16_BE },
	{ "s24le",  NULL, NULL, AFMT_S24_LE },
	{ "s24be",  NULL, NULL, AFMT_S24_BE },
	{ "u24le",  NULL, NULL, AFMT_U24_LE },
	{ "u24be",  NULL, NULL, AFMT_U24_BE },
#if BYTE_ORDER == LITTLE_ENDIAN
	{ "s32le", "s32", "32", AFMT_S32_LE },
	{ "s32be",  NULL, NULL, AFMT_S32_BE },
#else
	{ "s32le",  NULL, NULL, AFMT_S32_LE },
	{ "s32be", "s32", "32", AFMT_S32_BE },
#endif
	{ "u32le",  NULL, NULL, AFMT_U32_LE },
	{ "u32be",  NULL, NULL, AFMT_U32_BE },
	{   "ac3",  NULL, NULL, AFMT_AC3    },
	{    NULL,  NULL, NULL, 0           }
};

uint32_t
snd_str2afmt(const char *req)
{
	int ext;
	int ch;
	int i;
	char b1[8];
	char b2[8];

	memset(b1, 0, sizeof(b1));
	memset(b2, 0, sizeof(b2));

	i = sscanf(req, "%5[^:]:%6s", b1, b2);

	if (i == 1) {
		if (strlen(req) != strlen(b1))
			return (0);
		strlcpy(b2, "2.0", sizeof(b2));
	} else if (i == 2) {
		if (strlen(req) != (strlen(b1) + 1 + strlen(b2)))
			return (0);
	} else
		return (0);

	i = sscanf(b2, "%d.%d", &ch, &ext);

	if (i == 0) {
		if (strcasecmp(b2, "mono") == 0) {
			ch = 1;
			ext = 0;
		} else if (strcasecmp(b2, "stereo") == 0) {
			ch = 2;
			ext = 0;
		} else if (strcasecmp(b2, "quad") == 0) {
			ch = 4;
			ext = 0;
		} else
			return (0);
	} else if (i == 1) {
		if (ch < 1 || ch > AFMT_CHANNEL_MAX)
			return (0);
		ext = 0;
	} else if (i == 2) {
		if (ext < 0 || ext > AFMT_EXTCHANNEL_MAX)
			return (0);
		if (ch < 1 || (ch + ext) > AFMT_CHANNEL_MAX)
			return (0);
	} else
		return (0);

	for (i = 0; afmt_tab[i].name != NULL; i++) {
		if (strcasecmp(afmt_tab[i].name, b1) != 0) {
			if (afmt_tab[i].alias1 == NULL)
				continue;
			if (strcasecmp(afmt_tab[i].alias1, b1) != 0) {
				if (afmt_tab[i].alias2 == NULL)
					continue;
				if (strcasecmp(afmt_tab[i].alias2, b1) != 0)
					continue;
			}
		}
		/* found a match */
		return (SND_FORMAT(afmt_tab[i].afmt, ch + ext, ext));	
	}
	/* not a valid format */
	return (0);
}

uint32_t
snd_afmt2str(uint32_t afmt, char *buf, size_t len)
{
	uint32_t enc;
	uint32_t ext;
	uint32_t ch;
	int i;

	if (buf == NULL || len < AFMTSTR_LEN)
		return (0);

	memset(buf, 0, len);

	enc = AFMT_ENCODING(afmt);
	ch = AFMT_CHANNEL(afmt);
	ext = AFMT_EXTCHANNEL(afmt);
	/* check there is at least one channel */
	if (ch <= ext)
		return (0);
	for (i = 0; afmt_tab[i].name != NULL; i++) {
		if (enc != afmt_tab[i].afmt)
			continue;
		/* found a match */
		snprintf(buf, len, "%s:%d.%d",
		    afmt_tab[i].name, ch - ext, ext);
		return (SND_FORMAT(enc, ch, ext));
	}
	return (0);
}

int
chn_reset(struct pcm_channel *c, uint32_t fmt, uint32_t spd)
{
	int r;

	CHN_LOCKASSERT(c);
	c->feedcount = 0;
	c->flags &= CHN_F_RESET;
	c->interrupts = 0;
	c->timeout = 1;
	c->xruns = 0;

	c->flags |= (pcm_getflags(c->dev) & SD_F_BITPERFECT) ?
	    CHN_F_BITPERFECT : 0;

	r = CHANNEL_RESET(c->methods, c->devinfo);
	if (r == 0 && fmt != 0 && spd != 0) {
		r = chn_setparam(c, fmt, spd);
		fmt = 0;
		spd = 0;
	}
	if (r == 0 && fmt != 0)
		r = chn_setformat(c, fmt);
	if (r == 0 && spd != 0)
		r = chn_setspeed(c, spd);
	if (r == 0)
		r = chn_setlatency(c, chn_latency);
	if (r == 0) {
		chn_resetbuf(c);
		r = CHANNEL_RESETDONE(c->methods, c->devinfo);
	}
	return r;
}

int
chn_init(struct pcm_channel *c, void *devinfo, int dir, int direction)
{
	struct feeder_class *fc;
	struct snd_dbuf *b, *bs;
	int i, ret;

	if (chn_timeout < CHN_TIMEOUT_MIN || chn_timeout > CHN_TIMEOUT_MAX)
		chn_timeout = CHN_TIMEOUT;

	chn_lockinit(c, dir);

	b = NULL;
	bs = NULL;
	CHN_INIT(c, children);
	CHN_INIT(c, children.busy);
	c->devinfo = NULL;
	c->feeder = NULL;
	c->latency = -1;
	c->timeout = 1;

	ret = ENOMEM;
	b = sndbuf_create(c->dev, c->name, "primary", c);
	if (b == NULL)
		goto out;
	bs = sndbuf_create(c->dev, c->name, "secondary", c);
	if (bs == NULL)
		goto out;

	CHN_LOCK(c);

	ret = EINVAL;
	fc = feeder_getclass(NULL);
	if (fc == NULL)
		goto out;
	if (chn_addfeeder(c, fc, NULL))
		goto out;

	/*
	 * XXX - sndbuf_setup() & sndbuf_resize() expect to be called
	 *	 with the channel unlocked because they are also called
	 *	 from driver methods that don't know about locking
	 */
	CHN_UNLOCK(c);
	sndbuf_setup(bs, NULL, 0);
	CHN_LOCK(c);
	c->bufhard = b;
	c->bufsoft = bs;
	c->flags = 0;
	c->feederflags = 0;
	c->sm = NULL;
	c->format = SND_FORMAT(AFMT_U8, 1, 0);
	c->speed = DSP_DEFAULT_SPEED;

	c->matrix = *feeder_matrix_id_map(SND_CHN_MATRIX_1_0);
	c->matrix.id = SND_CHN_MATRIX_PCMCHANNEL;

	for (i = 0; i < SND_CHN_T_MAX; i++) {
		c->volume[SND_VOL_C_MASTER][i] = SND_VOL_0DB_MASTER;
	}

	c->volume[SND_VOL_C_MASTER][SND_CHN_T_VOL_0DB] = SND_VOL_0DB_MASTER;
	c->volume[SND_VOL_C_PCM][SND_CHN_T_VOL_0DB] = chn_vol_0db_pcm;

	chn_vpc_reset(c, SND_VOL_C_PCM, 1);

	ret = ENODEV;
	CHN_UNLOCK(c); /* XXX - Unlock for CHANNEL_INIT() malloc() call */
	c->devinfo = CHANNEL_INIT(c->methods, devinfo, b, c, direction);
	CHN_LOCK(c);
	if (c->devinfo == NULL)
		goto out;

	ret = ENOMEM;
	if ((sndbuf_getsize(b) == 0) && ((c->flags & CHN_F_VIRTUAL) == 0))
		goto out;

	ret = 0;
	c->direction = direction;

	sndbuf_setfmt(b, c->format);
	sndbuf_setspd(b, c->speed);
	sndbuf_setfmt(bs, c->format);
	sndbuf_setspd(bs, c->speed);

	/**
	 * @todo Should this be moved somewhere else?  The primary buffer
	 * 	 is allocated by the driver or via DMA map setup, and tmpbuf
	 * 	 seems to only come into existence in sndbuf_resize().
	 */
	if (c->direction == PCMDIR_PLAY) {
		bs->sl = sndbuf_getmaxsize(bs);
		bs->shadbuf = malloc(bs->sl, M_DEVBUF, M_NOWAIT);
		if (bs->shadbuf == NULL) {
			ret = ENOMEM;
			goto out;
		}
	}

out:
	CHN_UNLOCK(c);
	if (ret) {
		if (c->devinfo) {
			if (CHANNEL_FREE(c->methods, c->devinfo))
				sndbuf_free(b);
		}
		if (bs)
			sndbuf_destroy(bs);
		if (b)
			sndbuf_destroy(b);
		CHN_LOCK(c);
		c->flags |= CHN_F_DEAD;
		chn_lockdestroy(c);

		return ret;
	}

	return 0;
}

int
chn_kill(struct pcm_channel *c)
{
    	struct snd_dbuf *b = c->bufhard;
    	struct snd_dbuf *bs = c->bufsoft;

	if (CHN_STARTED(c)) {
		CHN_LOCK(c);
		chn_trigger(c, PCMTRIG_ABORT);
		CHN_UNLOCK(c);
	}
	while (chn_removefeeder(c) == 0)
		;
	if (CHANNEL_FREE(c->methods, c->devinfo))
		sndbuf_free(b);
	sndbuf_destroy(bs);
	sndbuf_destroy(b);
	CHN_LOCK(c);
	c->flags |= CHN_F_DEAD;
	chn_lockdestroy(c);

	return (0);
}

/* XXX Obsolete. Use *_matrix() variant instead. */
int
chn_setvolume(struct pcm_channel *c, int left, int right)
{
	int ret;

	ret = chn_setvolume_matrix(c, SND_VOL_C_MASTER, SND_CHN_T_FL, left);
	ret |= chn_setvolume_matrix(c, SND_VOL_C_MASTER, SND_CHN_T_FR,
	    right) << 8;

	return (ret);
}

int
chn_setvolume_multi(struct pcm_channel *c, int vc, int left, int right,
    int center)
{
	int i, ret;

	ret = 0;

	for (i = 0; i < SND_CHN_T_MAX; i++) {
		if ((1 << i) & SND_CHN_LEFT_MASK)
			ret |= chn_setvolume_matrix(c, vc, i, left);
		else if ((1 << i) & SND_CHN_RIGHT_MASK)
			ret |= chn_setvolume_matrix(c, vc, i, right) << 8;
		else
			ret |= chn_setvolume_matrix(c, vc, i, center) << 16;
	}

	return (ret);
}

int
chn_setvolume_matrix(struct pcm_channel *c, int vc, int vt, int val)
{
	int i;

	KASSERT(c != NULL && vc >= SND_VOL_C_MASTER && vc < SND_VOL_C_MAX &&
	    (vc == SND_VOL_C_MASTER || (vc & 1)) &&
	    (vt == SND_CHN_T_VOL_0DB || (vt >= SND_CHN_T_BEGIN &&
	    vt <= SND_CHN_T_END)) && (vt != SND_CHN_T_VOL_0DB ||
	    (val >= SND_VOL_0DB_MIN && val <= SND_VOL_0DB_MAX)),
	    ("%s(): invalid volume matrix c=%p vc=%d vt=%d val=%d",
	    __func__, c, vc, vt, val));
	CHN_LOCKASSERT(c);

	if (val < 0)
		val = 0;
	if (val > 100)
		val = 100;

	c->volume[vc][vt] = val;

	/*
	 * Do relative calculation here and store it into class + 1
	 * to ease the job of feeder_volume.
	 */
	if (vc == SND_VOL_C_MASTER) {
		for (vc = SND_VOL_C_BEGIN; vc <= SND_VOL_C_END;
		    vc += SND_VOL_C_STEP)
			c->volume[SND_VOL_C_VAL(vc)][vt] =
			    SND_VOL_CALC_VAL(c->volume, vc, vt);
	} else if (vc & 1) {
		if (vt == SND_CHN_T_VOL_0DB)
			for (i = SND_CHN_T_BEGIN; i <= SND_CHN_T_END;
			    i += SND_CHN_T_STEP) {
				c->volume[SND_VOL_C_VAL(vc)][i] =
				    SND_VOL_CALC_VAL(c->volume, vc, i);
			}
		else
			c->volume[SND_VOL_C_VAL(vc)][vt] =
			    SND_VOL_CALC_VAL(c->volume, vc, vt);
	}

	return (val);
}

int
chn_getvolume_matrix(struct pcm_channel *c, int vc, int vt)
{
	KASSERT(c != NULL && vc >= SND_VOL_C_MASTER && vc < SND_VOL_C_MAX &&
	    (vt == SND_CHN_T_VOL_0DB ||
	    (vt >= SND_CHN_T_BEGIN && vt <= SND_CHN_T_END)),
	    ("%s(): invalid volume matrix c=%p vc=%d vt=%d",
	    __func__, c, vc, vt));
	CHN_LOCKASSERT(c);

	return (c->volume[vc][vt]);
}

struct pcmchan_matrix *
chn_getmatrix(struct pcm_channel *c)
{

	KASSERT(c != NULL, ("%s(): NULL channel", __func__));
	CHN_LOCKASSERT(c);

	if (!(c->format & AFMT_CONVERTIBLE))
		return (NULL);

	return (&c->matrix);
}

int
chn_setmatrix(struct pcm_channel *c, struct pcmchan_matrix *m)
{

	KASSERT(c != NULL && m != NULL,
	    ("%s(): NULL channel or matrix", __func__));
	CHN_LOCKASSERT(c);

	if (!(c->format & AFMT_CONVERTIBLE))
		return (EINVAL);

	c->matrix = *m;
	c->matrix.id = SND_CHN_MATRIX_PCMCHANNEL;

	return (chn_setformat(c, SND_FORMAT(c->format, m->channels, m->ext)));
}

/*
 * XXX chn_oss_* exists for the sake of compatibility.
 */
int
chn_oss_getorder(struct pcm_channel *c, unsigned long long *map)
{

	KASSERT(c != NULL && map != NULL,
	    ("%s(): NULL channel or map", __func__));
	CHN_LOCKASSERT(c);

	if (!(c->format & AFMT_CONVERTIBLE))
		return (EINVAL);

	return (feeder_matrix_oss_get_channel_order(&c->matrix, map));
}

int
chn_oss_setorder(struct pcm_channel *c, unsigned long long *map)
{
	struct pcmchan_matrix m;
	int ret;

	KASSERT(c != NULL && map != NULL,
	    ("%s(): NULL channel or map", __func__));
	CHN_LOCKASSERT(c);

	if (!(c->format & AFMT_CONVERTIBLE))
		return (EINVAL);

	m = c->matrix;
	ret = feeder_matrix_oss_set_channel_order(&m, map);
	if (ret != 0)
		return (ret);

	return (chn_setmatrix(c, &m));
}

#define SND_CHN_OSS_FRONT	(SND_CHN_T_MASK_FL | SND_CHN_T_MASK_FR)
#define SND_CHN_OSS_SURR	(SND_CHN_T_MASK_SL | SND_CHN_T_MASK_SR)
#define SND_CHN_OSS_CENTER_LFE	(SND_CHN_T_MASK_FC | SND_CHN_T_MASK_LF)
#define SND_CHN_OSS_REAR	(SND_CHN_T_MASK_BL | SND_CHN_T_MASK_BR)

int
chn_oss_getmask(struct pcm_channel *c, uint32_t *retmask)
{
	struct pcmchan_matrix *m;
	struct pcmchan_caps *caps;
	uint32_t i, format;

	KASSERT(c != NULL && retmask != NULL,
	    ("%s(): NULL channel or retmask", __func__));
	CHN_LOCKASSERT(c);

	caps = chn_getcaps(c);
	if (caps == NULL || caps->fmtlist == NULL)
		return (ENODEV);

	for (i = 0; caps->fmtlist[i] != 0; i++) {
		format = caps->fmtlist[i];
		if (!(format & AFMT_CONVERTIBLE)) {
			*retmask |= DSP_BIND_SPDIF;
			continue;
		}
		m = CHANNEL_GETMATRIX(c->methods, c->devinfo, format);
		if (m == NULL)
			continue;
		if (m->mask & SND_CHN_OSS_FRONT)
			*retmask |= DSP_BIND_FRONT;
		if (m->mask & SND_CHN_OSS_SURR)
			*retmask |= DSP_BIND_SURR;
		if (m->mask & SND_CHN_OSS_CENTER_LFE)
			*retmask |= DSP_BIND_CENTER_LFE;
		if (m->mask & SND_CHN_OSS_REAR)
			*retmask |= DSP_BIND_REAR;
	}

	/* report software-supported binding mask */
	if (!CHN_BITPERFECT(c) && report_soft_matrix)
		*retmask |= DSP_BIND_FRONT | DSP_BIND_SURR |
		    DSP_BIND_CENTER_LFE | DSP_BIND_REAR;

	return (0);
}

void
chn_vpc_reset(struct pcm_channel *c, int vc, int force)
{
	int i;

	KASSERT(c != NULL && vc >= SND_VOL_C_BEGIN && vc <= SND_VOL_C_END,
	    ("%s(): invalid reset c=%p vc=%d", __func__, c, vc));
	CHN_LOCKASSERT(c);

	if (force == 0 && chn_vpc_autoreset == 0)
		return;

	for (i = SND_CHN_T_BEGIN; i <= SND_CHN_T_END; i += SND_CHN_T_STEP)
		CHN_SETVOLUME(c, vc, i, c->volume[vc][SND_CHN_T_VOL_0DB]);
}

static u_int32_t
round_pow2(u_int32_t v)
{
	u_int32_t ret;

	if (v < 2)
		v = 2;
	ret = 0;
	while (v >> ret)
		ret++;
	ret = 1 << (ret - 1);
	while (ret < v)
		ret <<= 1;
	return ret;
}

static u_int32_t
round_blksz(u_int32_t v, int round)
{
	u_int32_t ret, tmp;

	if (round < 1)
		round = 1;

	ret = min(round_pow2(v), CHN_2NDBUFMAXSIZE >> 1);

	if (ret > v && (ret >> 1) > 0 && (ret >> 1) >= ((v * 3) >> 2))
		ret >>= 1;

	tmp = ret - (ret % round);
	while (tmp < 16 || tmp < round) {
		ret <<= 1;
		tmp = ret - (ret % round);
	}

	return ret;
}

/*
 * 4Front call it DSP Policy, while we call it "Latency Profile". The idea
 * is to keep 2nd buffer short so that it doesn't cause long queue during
 * buffer transfer.
 *
 *    Latency reference table for 48khz stereo 16bit: (PLAY)
 *
 *      +---------+------------+-----------+------------+
 *      | Latency | Blockcount | Blocksize | Buffersize |
 *      +---------+------------+-----------+------------+
 *      |     0   |       2    |   64      |    128     |
 *      +---------+------------+-----------+------------+
 *      |     1   |       4    |   128     |    512     |
 *      +---------+------------+-----------+------------+
 *      |     2   |       8    |   512     |    4096    |
 *      +---------+------------+-----------+------------+
 *      |     3   |      16    |   512     |    8192    |
 *      +---------+------------+-----------+------------+
 *      |     4   |      32    |   512     |    16384   |
 *      +---------+------------+-----------+------------+
 *      |     5   |      32    |   1024    |    32768   |
 *      +---------+------------+-----------+------------+
 *      |     6   |      16    |   2048    |    32768   |
 *      +---------+------------+-----------+------------+
 *      |     7   |       8    |   4096    |    32768   |
 *      +---------+------------+-----------+------------+
 *      |     8   |       4    |   8192    |    32768   |
 *      +---------+------------+-----------+------------+
 *      |     9   |       2    |   16384   |    32768   |
 *      +---------+------------+-----------+------------+
 *      |    10   |       2    |   32768   |    65536   |
 *      +---------+------------+-----------+------------+
 *
 * Recording need a different reference table. All we care is
 * gobbling up everything within reasonable buffering threshold.
 *
 *    Latency reference table for 48khz stereo 16bit: (REC)
 *
 *      +---------+------------+-----------+------------+
 *      | Latency | Blockcount | Blocksize | Buffersize |
 *      +---------+------------+-----------+------------+
 *      |     0   |     512    |   32      |    16384   |
 *      +---------+------------+-----------+------------+
 *      |     1   |     256    |   64      |    16384   |
 *      +---------+------------+-----------+------------+
 *      |     2   |     128    |   128     |    16384   |
 *      +---------+------------+-----------+------------+
 *      |     3   |      64    |   256     |    16384   |
 *      +---------+------------+-----------+------------+
 *      |     4   |      32    |   512     |    16384   |
 *      +---------+------------+-----------+------------+
 *      |     5   |      32    |   1024    |    32768   |
 *      +---------+------------+-----------+------------+
 *      |     6   |      16    |   2048    |    32768   |
 *      +---------+------------+-----------+------------+
 *      |     7   |       8    |   4096    |    32768   |
 *      +---------+------------+-----------+------------+
 *      |     8   |       4    |   8192    |    32768   |
 *      +---------+------------+-----------+------------+
 *      |     9   |       2    |   16384   |    32768   |
 *      +---------+------------+-----------+------------+
 *      |    10   |       2    |   32768   |    65536   |
 *      +---------+------------+-----------+------------+
 *
 * Calculations for other data rate are entirely based on these reference
 * tables. For normal operation, Latency 5 seems give the best, well
 * balanced performance for typical workload. Anything below 5 will
 * eat up CPU to keep up with increasing context switches because of
 * shorter buffer space and usually require the application to handle it
 * aggresively through possibly real time programming technique.
 *
 */
#define CHN_LATENCY_PBLKCNT_REF				\
	{{1, 2, 3, 4, 5, 5, 4, 3, 2, 1, 1},		\
	{1, 2, 3, 4, 5, 5, 4, 3, 2, 1, 1}}
#define CHN_LATENCY_PBUFSZ_REF				\
	{{7, 9, 12, 13, 14, 15, 15, 15, 15, 15, 16},	\
	{11, 12, 13, 14, 15, 16, 16, 16, 16, 16, 17}}

#define CHN_LATENCY_RBLKCNT_REF				\
	{{9, 8, 7, 6, 5, 5, 4, 3, 2, 1, 1},		\
	{9, 8, 7, 6, 5, 5, 4, 3, 2, 1, 1}}
#define CHN_LATENCY_RBUFSZ_REF				\
	{{14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 16},	\
	{15, 15, 15, 15, 15, 16, 16, 16, 16, 16, 17}}

#define CHN_LATENCY_DATA_REF	192000 /* 48khz stereo 16bit ~ 48000 x 2 x 2 */

static int
chn_calclatency(int dir, int latency, int bps, u_int32_t datarate,
				u_int32_t max, int *rblksz, int *rblkcnt)
{
	static int pblkcnts[CHN_LATENCY_PROFILE_MAX + 1][CHN_LATENCY_MAX + 1] =
	    CHN_LATENCY_PBLKCNT_REF;
	static int  pbufszs[CHN_LATENCY_PROFILE_MAX + 1][CHN_LATENCY_MAX + 1] =
	    CHN_LATENCY_PBUFSZ_REF;
	static int rblkcnts[CHN_LATENCY_PROFILE_MAX + 1][CHN_LATENCY_MAX + 1] =
	    CHN_LATENCY_RBLKCNT_REF;
	static int  rbufszs[CHN_LATENCY_PROFILE_MAX + 1][CHN_LATENCY_MAX + 1] =
	    CHN_LATENCY_RBUFSZ_REF;
	u_int32_t bufsz;
	int lprofile, blksz, blkcnt;

	if (latency < CHN_LATENCY_MIN || latency > CHN_LATENCY_MAX ||
	    bps < 1 || datarate < 1 ||
	    !(dir == PCMDIR_PLAY || dir == PCMDIR_REC)) {
		if (rblksz != NULL)
			*rblksz = CHN_2NDBUFMAXSIZE >> 1;
		if (rblkcnt != NULL)
			*rblkcnt = 2;
		printf("%s(): FAILED dir=%d latency=%d bps=%d "
		    "datarate=%u max=%u\n",
		    __func__, dir, latency, bps, datarate, max);
		return CHN_2NDBUFMAXSIZE;
	}

	lprofile = chn_latency_profile;

	if (dir == PCMDIR_PLAY) {
		blkcnt = pblkcnts[lprofile][latency];
		bufsz = pbufszs[lprofile][latency];
	} else {
		blkcnt = rblkcnts[lprofile][latency];
		bufsz = rbufszs[lprofile][latency];
	}

	bufsz = round_pow2(snd_xbytes(1 << bufsz, CHN_LATENCY_DATA_REF,
	    datarate));
	if (bufsz > max)
		bufsz = max;
	blksz = round_blksz(bufsz >> blkcnt, bps);

	if (rblksz != NULL)
		*rblksz = blksz;
	if (rblkcnt != NULL)
		*rblkcnt = 1 << blkcnt;

	return blksz << blkcnt;
}

static int
chn_resizebuf(struct pcm_channel *c, int latency,
					int blkcnt, int blksz)
{
	struct snd_dbuf *b, *bs, *pb;
	int sblksz, sblkcnt, hblksz, hblkcnt, limit = 0, nsblksz, nsblkcnt;
	int ret;

	CHN_LOCKASSERT(c);

	if ((c->flags & (CHN_F_MMAP | CHN_F_TRIGGERED)) ||
	    !(c->direction == PCMDIR_PLAY || c->direction == PCMDIR_REC))
		return EINVAL;

	if (latency == -1) {
		c->latency = -1;
		latency = chn_latency;
	} else if (latency == -2) {
		latency = c->latency;
		if (latency < CHN_LATENCY_MIN || latency > CHN_LATENCY_MAX)
			latency = chn_latency;
	} else if (latency < CHN_LATENCY_MIN || latency > CHN_LATENCY_MAX)
		return EINVAL;
	else {
		c->latency = latency;
	}

	bs = c->bufsoft;
	b = c->bufhard;

	if (!(blksz == 0 || blkcnt == -1) &&
	    (blksz < 16 || blksz < sndbuf_getalign(bs) || blkcnt < 2 ||
	    (blksz * blkcnt) > CHN_2NDBUFMAXSIZE))
		return EINVAL;

	chn_calclatency(c->direction, latency, sndbuf_getalign(bs),
	    sndbuf_getalign(bs) * sndbuf_getspd(bs), CHN_2NDBUFMAXSIZE,
	    &sblksz, &sblkcnt);

	if (blksz == 0 || blkcnt == -1) {
		if (blkcnt == -1)
			c->flags &= ~CHN_F_HAS_SIZE;
		if (c->flags & CHN_F_HAS_SIZE) {
			blksz = sndbuf_getblksz(bs);
			blkcnt = sndbuf_getblkcnt(bs);
		}
	} else
		c->flags |= CHN_F_HAS_SIZE;

	if (c->flags & CHN_F_HAS_SIZE) {
		/*
		 * The application has requested their own blksz/blkcnt.
		 * Just obey with it, and let them toast alone. We can
		 * clamp it to the nearest latency profile, but that would
		 * defeat the purpose of having custom control. The least
		 * we can do is round it to the nearest ^2 and align it.
		 */
		sblksz = round_blksz(blksz, sndbuf_getalign(bs));
		sblkcnt = round_pow2(blkcnt);
	}

	if (c->parentchannel != NULL) {
		pb = c->parentchannel->bufsoft;
		CHN_UNLOCK(c);
		CHN_LOCK(c->parentchannel);
		chn_notify(c->parentchannel, CHN_N_BLOCKSIZE);
		CHN_UNLOCK(c->parentchannel);
		CHN_LOCK(c);
		if (c->direction == PCMDIR_PLAY) {
			limit = (pb != NULL) ?
			    sndbuf_xbytes(sndbuf_getsize(pb), pb, bs) : 0;
		} else {
			limit = (pb != NULL) ?
			    sndbuf_xbytes(sndbuf_getblksz(pb), pb, bs) * 2 : 0;
		}
	} else {
		hblkcnt = 2;
		if (c->flags & CHN_F_HAS_SIZE) {
			hblksz = round_blksz(sndbuf_xbytes(sblksz, bs, b),
			    sndbuf_getalign(b));
			hblkcnt = round_pow2(sndbuf_getblkcnt(bs));
		} else
			chn_calclatency(c->direction, latency,
			    sndbuf_getalign(b),
			    sndbuf_getalign(b) * sndbuf_getspd(b),
			    CHN_2NDBUFMAXSIZE, &hblksz, &hblkcnt);

		if ((hblksz << 1) > sndbuf_getmaxsize(b))
			hblksz = round_blksz(sndbuf_getmaxsize(b) >> 1,
			    sndbuf_getalign(b));

		while ((hblksz * hblkcnt) > sndbuf_getmaxsize(b)) {
			if (hblkcnt < 4)
				hblksz >>= 1;
			else
				hblkcnt >>= 1;
		}

		hblksz -= hblksz % sndbuf_getalign(b);

#if 0
		hblksz = sndbuf_getmaxsize(b) >> 1;
		hblksz -= hblksz % sndbuf_getalign(b);
		hblkcnt = 2;
#endif

		CHN_UNLOCK(c);
		if (chn_usefrags == 0 ||
		    CHANNEL_SETFRAGMENTS(c->methods, c->devinfo,
		    hblksz, hblkcnt) != 0)
			sndbuf_setblksz(b, CHANNEL_SETBLOCKSIZE(c->methods,
			    c->devinfo, hblksz));
		CHN_LOCK(c);

		if (!CHN_EMPTY(c, children)) {
			nsblksz = round_blksz(
			    sndbuf_xbytes(sndbuf_getblksz(b), b, bs),
			    sndbuf_getalign(bs));
			nsblkcnt = sndbuf_getblkcnt(b);
			if (c->direction == PCMDIR_PLAY) {
				do {
					nsblkcnt--;
				} while (nsblkcnt >= 2 &&
				    nsblksz * nsblkcnt >= sblksz * sblkcnt);
				nsblkcnt++;
			}
			sblksz = nsblksz;
			sblkcnt = nsblkcnt;
			limit = 0;
		} else
			limit = sndbuf_xbytes(sndbuf_getblksz(b), b, bs) * 2;
	}

	if (limit > CHN_2NDBUFMAXSIZE)
		limit = CHN_2NDBUFMAXSIZE;

#if 0
	while (limit > 0 && (sblksz * sblkcnt) > limit) {
		if (sblkcnt < 4)
			break;
		sblkcnt >>= 1;
	}
#endif

	while ((sblksz * sblkcnt) < limit)
		sblkcnt <<= 1;

	while ((sblksz * sblkcnt) > CHN_2NDBUFMAXSIZE) {
		if (sblkcnt < 4)
			sblksz >>= 1;
		else
			sblkcnt >>= 1;
	}

	sblksz -= sblksz % sndbuf_getalign(bs);

	if (sndbuf_getblkcnt(bs) != sblkcnt || sndbuf_getblksz(bs) != sblksz ||
	    sndbuf_getsize(bs) != (sblkcnt * sblksz)) {
		ret = sndbuf_remalloc(bs, sblkcnt, sblksz);
		if (ret != 0) {
			device_printf(c->dev, "%s(): Failed: %d %d\n",
			    __func__, sblkcnt, sblksz);
			return ret;
		}
	}

	/*
	 * Interrupt timeout
	 */
	c->timeout = ((u_int64_t)hz * sndbuf_getsize(bs)) /
	    ((u_int64_t)sndbuf_getspd(bs) * sndbuf_getalign(bs));
	if (c->parentchannel != NULL)
		c->timeout = min(c->timeout, c->parentchannel->timeout);
	if (c->timeout < 1)
		c->timeout = 1;

	/*
	 * OSSv4 docs: "By default OSS will set the low water level equal
	 * to the fragment size which is optimal in most cases."
	 */
	c->lw = sndbuf_getblksz(bs);
	chn_resetbuf(c);

	if (snd_verbose > 3)
		device_printf(c->dev, "%s(): %s (%s) timeout=%u "
		    "b[%d/%d/%d] bs[%d/%d/%d] limit=%d\n",
		    __func__, CHN_DIRSTR(c),
		    (c->flags & CHN_F_VIRTUAL) ? "virtual" : "hardware",
		    c->timeout,
		    sndbuf_getsize(b), sndbuf_getblksz(b),
		    sndbuf_getblkcnt(b),
		    sndbuf_getsize(bs), sndbuf_getblksz(bs),
		    sndbuf_getblkcnt(bs), limit);

	return 0;
}

int
chn_setlatency(struct pcm_channel *c, int latency)
{
	CHN_LOCKASSERT(c);
	/* Destroy blksz/blkcnt, enforce latency profile. */
	return chn_resizebuf(c, latency, -1, 0);
}

int
chn_setblocksize(struct pcm_channel *c, int blkcnt, int blksz)
{
	CHN_LOCKASSERT(c);
	/* Destroy latency profile, enforce blksz/blkcnt */
	return chn_resizebuf(c, -1, blkcnt, blksz);
}

int
chn_setparam(struct pcm_channel *c, uint32_t format, uint32_t speed)
{
	struct pcmchan_caps *caps;
	uint32_t hwspeed, delta;
	int ret;

	CHN_LOCKASSERT(c);

	if (speed < 1 || format == 0 || CHN_STARTED(c))
		return (EINVAL);

	c->format = format;
	c->speed = speed;

	caps = chn_getcaps(c);

	hwspeed = speed;
	RANGE(hwspeed, caps->minspeed, caps->maxspeed);

	sndbuf_setspd(c->bufhard, CHANNEL_SETSPEED(c->methods, c->devinfo,
	    hwspeed));
	hwspeed = sndbuf_getspd(c->bufhard);

	delta = (hwspeed > speed) ? (hwspeed - speed) : (speed - hwspeed);

	if (delta <= feeder_rate_round)
		c->speed = hwspeed;

	ret = feeder_chain(c);

	if (ret == 0)
		ret = CHANNEL_SETFORMAT(c->methods, c->devinfo,
		    sndbuf_getfmt(c->bufhard));

	if (ret == 0)
		ret = chn_resizebuf(c, -2, 0, 0);

	return (ret);
}

int
chn_setspeed(struct pcm_channel *c, uint32_t speed)
{
	uint32_t oldformat, oldspeed, format;
	int ret;

#if 0
	/* XXX force 48k */
	if (c->format & AFMT_PASSTHROUGH)
		speed = AFMT_PASSTHROUGH_RATE;
#endif

	oldformat = c->format;
	oldspeed = c->speed;
	format = oldformat;

	ret = chn_setparam(c, format, speed);
	if (ret != 0) {
		if (snd_verbose > 3)
			device_printf(c->dev,
			    "%s(): Setting speed %d failed, "
			    "falling back to %d\n",
			    __func__, speed, oldspeed);
		chn_setparam(c, c->format, oldspeed);
	}

	return (ret);
}

int
chn_setformat(struct pcm_channel *c, uint32_t format)
{
	uint32_t oldformat, oldspeed, speed;
	int ret;

	/* XXX force stereo */
	if ((format & AFMT_PASSTHROUGH) && AFMT_CHANNEL(format) < 2) {
		format = SND_FORMAT(format, AFMT_PASSTHROUGH_CHANNEL,
		    AFMT_PASSTHROUGH_EXTCHANNEL);
	}

	oldformat = c->format;
	oldspeed = c->speed;
	speed = oldspeed;

	ret = chn_setparam(c, format, speed);
	if (ret != 0) {
		if (snd_verbose > 3)
			device_printf(c->dev,
			    "%s(): Format change 0x%08x failed, "
			    "falling back to 0x%08x\n",
			    __func__, format, oldformat);
		chn_setparam(c, oldformat, oldspeed);
	}

	return (ret);
}

void
chn_syncstate(struct pcm_channel *c)
{
	struct snddev_info *d;
	struct snd_mixer *m;

	d = (c != NULL) ? c->parentsnddev : NULL;
	m = (d != NULL && d->mixer_dev != NULL) ? d->mixer_dev->si_drv1 :
	    NULL;

	if (d == NULL || m == NULL)
		return;

	CHN_LOCKASSERT(c);

	if (c->feederflags & (1 << FEEDER_VOLUME)) {
		uint32_t parent;
		int vol, pvol, left, right, center;

		if (c->direction == PCMDIR_PLAY &&
		    (d->flags & SD_F_SOFTPCMVOL)) {
			/* CHN_UNLOCK(c); */
			vol = mix_get(m, SOUND_MIXER_PCM);
			parent = mix_getparent(m, SOUND_MIXER_PCM);
			if (parent != SOUND_MIXER_NONE)
				pvol = mix_get(m, parent);
			else
				pvol = 100 | (100 << 8);
			/* CHN_LOCK(c); */
		} else {
			vol = 100 | (100 << 8);
			pvol = vol;
		}

		if (vol == -1) {
			device_printf(c->dev,
			    "Soft PCM Volume: Failed to read pcm "
			    "default value\n");
			vol = 100 | (100 << 8);
		}

		if (pvol == -1) {
			device_printf(c->dev,
			    "Soft PCM Volume: Failed to read parent "
			    "default value\n");
			pvol = 100 | (100 << 8);
		}

		left = ((vol & 0x7f) * (pvol & 0x7f)) / 100;
		right = (((vol >> 8) & 0x7f) * ((pvol >> 8) & 0x7f)) / 100;
		center = (left + right) >> 1;

		chn_setvolume_multi(c, SND_VOL_C_MASTER, left, right, center);
	}

	if (c->feederflags & (1 << FEEDER_EQ)) {
		struct pcm_feeder *f;
		int treble, bass, state;

		/* CHN_UNLOCK(c); */
		treble = mix_get(m, SOUND_MIXER_TREBLE);
		bass = mix_get(m, SOUND_MIXER_BASS);
		/* CHN_LOCK(c); */

		if (treble == -1)
			treble = 50;
		else
			treble = ((treble & 0x7f) +
			    ((treble >> 8) & 0x7f)) >> 1;

		if (bass == -1)
			bass = 50;
		else
			bass = ((bass & 0x7f) + ((bass >> 8) & 0x7f)) >> 1;

		f = chn_findfeeder(c, FEEDER_EQ);
		if (f != NULL) {
			if (FEEDER_SET(f, FEEDEQ_TREBLE, treble) != 0)
				device_printf(c->dev,
				    "EQ: Failed to set treble -- %d\n",
				    treble);
			if (FEEDER_SET(f, FEEDEQ_BASS, bass) != 0)
				device_printf(c->dev,
				    "EQ: Failed to set bass -- %d\n",
				    bass);
			if (FEEDER_SET(f, FEEDEQ_PREAMP, d->eqpreamp) != 0)
				device_printf(c->dev,
				    "EQ: Failed to set preamp -- %d\n",
				    d->eqpreamp);
			if (d->flags & SD_F_EQ_BYPASSED)
				state = FEEDEQ_BYPASS;
			else if (d->flags & SD_F_EQ_ENABLED)
				state = FEEDEQ_ENABLE;
			else
				state = FEEDEQ_DISABLE;
			if (FEEDER_SET(f, FEEDEQ_STATE, state) != 0)
				device_printf(c->dev,
				    "EQ: Failed to set state -- %d\n", state);
		}
	}
}

int
chn_trigger(struct pcm_channel *c, int go)
{
#ifdef DEV_ISA
    	struct snd_dbuf *b = c->bufhard;
#endif
	struct snddev_info *d = c->parentsnddev;
	int ret;

	CHN_LOCKASSERT(c);
#ifdef DEV_ISA
	if (SND_DMA(b) && (go == PCMTRIG_EMLDMAWR || go == PCMTRIG_EMLDMARD))
		sndbuf_dmabounce(b);
#endif
	if (!PCMTRIG_COMMON(go))
		return (CHANNEL_TRIGGER(c->methods, c->devinfo, go));

	if (go == c->trigger)
		return (0);

	ret = CHANNEL_TRIGGER(c->methods, c->devinfo, go);
	if (ret != 0)
		return (ret);

	switch (go) {
	case PCMTRIG_START:
		if (snd_verbose > 3)
			device_printf(c->dev,
			    "%s() %s: calling go=0x%08x , "
			    "prev=0x%08x\n", __func__, c->name, go,
			    c->trigger);
		if (c->trigger != PCMTRIG_START) {
			c->trigger = go;
			CHN_UNLOCK(c);
			PCM_LOCK(d);
			CHN_INSERT_HEAD(d, c, channels.pcm.busy);
			PCM_UNLOCK(d);
			CHN_LOCK(c);
			chn_syncstate(c);
		}
		break;
	case PCMTRIG_STOP:
	case PCMTRIG_ABORT:
		if (snd_verbose > 3)
			device_printf(c->dev,
			    "%s() %s: calling go=0x%08x , "
			    "prev=0x%08x\n", __func__, c->name, go,
			    c->trigger);
		if (c->trigger == PCMTRIG_START) {
			c->trigger = go;
			CHN_UNLOCK(c);
			PCM_LOCK(d);
			CHN_REMOVE(d, c, channels.pcm.busy);
			PCM_UNLOCK(d);
			CHN_LOCK(c);
		}
		break;
	default:
		break;
	}

	return (0);
}

/**
 * @brief Queries sound driver for sample-aligned hardware buffer pointer index
 *
 * This function obtains the hardware pointer location, then aligns it to
 * the current bytes-per-sample value before returning.  (E.g., a channel
 * running in 16 bit stereo mode would require 4 bytes per sample, so a
 * hwptr value ranging from 32-35 would be returned as 32.)
 *
 * @param c	PCM channel context	
 * @returns 	sample-aligned hardware buffer pointer index
 */
int
chn_getptr(struct pcm_channel *c)
{
	int hwptr;

	CHN_LOCKASSERT(c);
	hwptr = (CHN_STARTED(c)) ? CHANNEL_GETPTR(c->methods, c->devinfo) : 0;
	return (hwptr - (hwptr % sndbuf_getalign(c->bufhard)));
}

struct pcmchan_caps *
chn_getcaps(struct pcm_channel *c)
{
	CHN_LOCKASSERT(c);
	return CHANNEL_GETCAPS(c->methods, c->devinfo);
}

u_int32_t
chn_getformats(struct pcm_channel *c)
{
	u_int32_t *fmtlist, fmts;
	int i;

	fmtlist = chn_getcaps(c)->fmtlist;
	fmts = 0;
	for (i = 0; fmtlist[i]; i++)
		fmts |= fmtlist[i];

	/* report software-supported formats */
	if (!CHN_BITPERFECT(c) && report_soft_formats)
		fmts |= AFMT_CONVERTIBLE;

	return (AFMT_ENCODING(fmts));
}

int
chn_notify(struct pcm_channel *c, u_int32_t flags)
{
	struct pcm_channel *ch;
	struct pcmchan_caps *caps;
	uint32_t bestformat, bestspeed, besthwformat, *vchanformat, *vchanrate;
	uint32_t vpflags;
	int dirty, err, run, nrun;

	CHN_LOCKASSERT(c);

	if (CHN_EMPTY(c, children))
		return (ENODEV);

	err = 0;

	/*
	 * If the hwchan is running, we can't change its rate, format or
	 * blocksize
	 */
	run = (CHN_STARTED(c)) ? 1 : 0;
	if (run)
		flags &= CHN_N_VOLUME | CHN_N_TRIGGER;

	if (flags & CHN_N_RATE) {
		/*
		 * XXX I'll make good use of this someday.
		 *     However this is currently being superseded by
		 *     the availability of CHN_F_VCHAN_DYNAMIC.
		 */
	}

	if (flags & CHN_N_FORMAT) {
		/*
		 * XXX I'll make good use of this someday.
		 *     However this is currently being superseded by
		 *     the availability of CHN_F_VCHAN_DYNAMIC.
		 */
	}

	if (flags & CHN_N_VOLUME) {
		/*
		 * XXX I'll make good use of this someday, though
		 *     soft volume control is currently pretty much
		 *     integrated.
		 */
	}

	if (flags & CHN_N_BLOCKSIZE) {
		/*
		 * Set to default latency profile
		 */
		chn_setlatency(c, chn_latency);
	}

	if ((flags & CHN_N_TRIGGER) && !(c->flags & CHN_F_VCHAN_DYNAMIC)) {
		nrun = CHN_EMPTY(c, children.busy) ? 0 : 1;
		if (nrun && !run)
			err = chn_start(c, 1);
		if (!nrun && run)
			chn_abort(c);
		flags &= ~CHN_N_TRIGGER;
	}

	if (flags & CHN_N_TRIGGER) {
		if (c->direction == PCMDIR_PLAY) {
			vchanformat = &c->parentsnddev->pvchanformat;
			vchanrate = &c->parentsnddev->pvchanrate;
		} else {
			vchanformat = &c->parentsnddev->rvchanformat;
			vchanrate = &c->parentsnddev->rvchanrate;
		}

		/* Dynamic Virtual Channel */
		if (!(c->flags & CHN_F_VCHAN_ADAPTIVE)) {
			bestformat = *vchanformat;
			bestspeed = *vchanrate;
		} else {
			bestformat = 0;
			bestspeed = 0;
		}

		besthwformat = 0;
		nrun = 0;
		caps = chn_getcaps(c);
		dirty = 0;
		vpflags = 0;

		CHN_FOREACH(ch, c, children.busy) {
			CHN_LOCK(ch);
			if ((ch->format & AFMT_PASSTHROUGH) &&
			    snd_fmtvalid(ch->format, caps->fmtlist)) {
				bestformat = ch->format;
				bestspeed = ch->speed;
				CHN_UNLOCK(ch);
				vpflags = CHN_F_PASSTHROUGH;
				nrun++;
				break;
			}
			if ((ch->flags & CHN_F_EXCLUSIVE) && vpflags == 0) {
				if (c->flags & CHN_F_VCHAN_ADAPTIVE) {
					bestspeed = ch->speed;
					RANGE(bestspeed, caps->minspeed,
					    caps->maxspeed);
					besthwformat = snd_fmtbest(ch->format,
					    caps->fmtlist);
					if (besthwformat != 0)
						bestformat = besthwformat;
				}
				CHN_UNLOCK(ch);
				vpflags = CHN_F_EXCLUSIVE;
				nrun++;
				continue;
			}
			if (!(c->flags & CHN_F_VCHAN_ADAPTIVE) ||
			    vpflags != 0) {
				CHN_UNLOCK(ch);
				nrun++;
				continue;
			}
			if (ch->speed > bestspeed) {
				bestspeed = ch->speed;
				RANGE(bestspeed, caps->minspeed,
				    caps->maxspeed);
			}
			besthwformat = snd_fmtbest(ch->format, caps->fmtlist);
			if (!(besthwformat & AFMT_VCHAN)) {
				CHN_UNLOCK(ch);
				nrun++;
				continue;
			}
			if (AFMT_CHANNEL(besthwformat) >
			    AFMT_CHANNEL(bestformat))
				bestformat = besthwformat;
			else if (AFMT_CHANNEL(besthwformat) ==
			    AFMT_CHANNEL(bestformat) &&
			    AFMT_BIT(besthwformat) > AFMT_BIT(bestformat))
				bestformat = besthwformat;
			CHN_UNLOCK(ch);
			nrun++;
		}

		if (bestformat == 0)
			bestformat = c->format;
		if (bestspeed == 0)
			bestspeed = c->speed;

		if (bestformat != c->format || bestspeed != c->speed)
			dirty = 1;

		c->flags &= ~(CHN_F_PASSTHROUGH | CHN_F_EXCLUSIVE);
		c->flags |= vpflags;

		if (nrun && !run) {
			if (dirty) {
				bestspeed = CHANNEL_SETSPEED(c->methods,
				    c->devinfo, bestspeed);
				err = chn_reset(c, bestformat, bestspeed);
			}
			if (err == 0 && dirty) {
				CHN_FOREACH(ch, c, children.busy) {
					CHN_LOCK(ch);
					if (VCHAN_SYNC_REQUIRED(ch))
						vchan_sync(ch);
					CHN_UNLOCK(ch);
				}
			}
			if (err == 0) {
				if (dirty)
					c->flags |= CHN_F_DIRTY;
				err = chn_start(c, 1);
			}
		}

		if (nrun && run && dirty) {
			chn_abort(c);
			bestspeed = CHANNEL_SETSPEED(c->methods, c->devinfo,
			    bestspeed);
			err = chn_reset(c, bestformat, bestspeed);
			if (err == 0) {
				CHN_FOREACH(ch, c, children.busy) {
					CHN_LOCK(ch);
					if (VCHAN_SYNC_REQUIRED(ch))
						vchan_sync(ch);
					CHN_UNLOCK(ch);
				}
			}
			if (err == 0) {
				c->flags |= CHN_F_DIRTY;
				err = chn_start(c, 1);
			}
		}

		if (err == 0 && !(bestformat & AFMT_PASSTHROUGH) &&
		    (bestformat & AFMT_VCHAN)) {
			*vchanformat = bestformat;
			*vchanrate = bestspeed;
		}

		if (!nrun && run) {
			c->flags &= ~(CHN_F_PASSTHROUGH | CHN_F_EXCLUSIVE);
			bestformat = *vchanformat;
			bestspeed = *vchanrate;
			chn_abort(c);
			if (c->format != bestformat || c->speed != bestspeed)
				chn_reset(c, bestformat, bestspeed);
		}
	}

	return (err);
}

/**
 * @brief Fetch array of supported discrete sample rates
 *
 * Wrapper for CHANNEL_GETRATES.  Please see channel_if.m:getrates() for
 * detailed information.
 *
 * @note If the operation isn't supported, this function will just return 0
 *       (no rates in the array), and *rates will be set to NULL.  Callers
 *       should examine rates @b only if this function returns non-zero.
 *
 * @param c	pcm channel to examine
 * @param rates	pointer to array of integers; rate table will be recorded here
 *
 * @return number of rates in the array pointed to be @c rates
 */
int
chn_getrates(struct pcm_channel *c, int **rates)
{
	KASSERT(rates != NULL, ("rates is null"));
	CHN_LOCKASSERT(c);
	return CHANNEL_GETRATES(c->methods, c->devinfo, rates);
}

/**
 * @brief Remove channel from a sync group, if there is one.
 *
 * This function is initially intended for the following conditions:
 *   - Starting a syncgroup (@c SNDCTL_DSP_SYNCSTART ioctl)
 *   - Closing a device.  (A channel can't be destroyed if it's still in use.)
 *
 * @note Before calling this function, the syncgroup list mutex must be
 * held.  (Consider pcm_channel::sm protected by the SG list mutex
 * whether @c c is locked or not.)
 *
 * @param c	channel device to be started or closed
 * @returns	If this channel was the only member of a group, the group ID
 * 		is returned to the caller so that the caller can release it
 * 		via free_unr() after giving up the syncgroup lock.  Else it
 * 		returns 0.
 */
int
chn_syncdestroy(struct pcm_channel *c)
{
	struct pcmchan_syncmember *sm;
	struct pcmchan_syncgroup *sg;
	int sg_id;

	sg_id = 0;

	PCM_SG_LOCKASSERT(MA_OWNED);

	if (c->sm != NULL) {
		sm = c->sm;
		sg = sm->parent;
		c->sm = NULL;

		KASSERT(sg != NULL, ("syncmember has null parent"));

		SLIST_REMOVE(&sg->members, sm, pcmchan_syncmember, link);
		free(sm, M_DEVBUF);

		if (SLIST_EMPTY(&sg->members)) {
			SLIST_REMOVE(&snd_pcm_syncgroups, sg, pcmchan_syncgroup, link);
			sg_id = sg->id;
			free(sg, M_DEVBUF);
		}
	}

	return sg_id;
}

#ifdef OSSV4_EXPERIMENT
int
chn_getpeaks(struct pcm_channel *c, int *lpeak, int *rpeak)
{
	CHN_LOCKASSERT(c);
	return CHANNEL_GETPEAKS(c->methods, c->devinfo, lpeak, rpeak);
}
#endif
