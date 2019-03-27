/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2009 Ariff Abdullah <ariff@FreeBSD.org>
 * Portions Copyright (c) Ryan Beasley <ryan.beasley@gmail.com> - GSoC 2006
 * Copyright (c) 1999 Cameron Grant <cg@FreeBSD.org>
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

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>

#include "feeder_if.h"

#define SND_USE_FXDIV
#define	SND_DECLARE_FXDIV
#include "snd_fxdiv_gen.h"

SND_DECLARE_FILE("$FreeBSD$");

struct snd_dbuf *
sndbuf_create(device_t dev, char *drv, char *desc, struct pcm_channel *channel)
{
	struct snd_dbuf *b;

	b = malloc(sizeof(*b), M_DEVBUF, M_WAITOK | M_ZERO);
	snprintf(b->name, SNDBUF_NAMELEN, "%s:%s", drv, desc);
	b->dev = dev;
	b->channel = channel;

	return b;
}

void
sndbuf_destroy(struct snd_dbuf *b)
{
	sndbuf_free(b);
	free(b, M_DEVBUF);
}

bus_addr_t
sndbuf_getbufaddr(struct snd_dbuf *buf)
{
	return (buf->buf_addr);
}

static void
sndbuf_setmap(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct snd_dbuf *b = (struct snd_dbuf *)arg;

	if (snd_verbose > 3) {
		device_printf(b->dev, "sndbuf_setmap %lx, %lx; ",
		    (u_long)segs[0].ds_addr, (u_long)segs[0].ds_len);
		printf("%p -> %lx\n", b->buf, (u_long)segs[0].ds_addr);
	}
	if (error == 0)
		b->buf_addr = segs[0].ds_addr;
	else
		b->buf_addr = 0;
}

/*
 * Allocate memory for DMA buffer. If the device does not use DMA transfers,
 * the driver can call malloc(9) and sndbuf_setup() itself.
 */

int
sndbuf_alloc(struct snd_dbuf *b, bus_dma_tag_t dmatag, int dmaflags,
    unsigned int size)
{
	int ret;

	b->dmatag = dmatag;
	b->dmaflags = dmaflags | BUS_DMA_NOWAIT | BUS_DMA_COHERENT;
	b->maxsize = size;
	b->bufsize = b->maxsize;
	b->buf_addr = 0;
	b->flags |= SNDBUF_F_MANAGED;
	if (bus_dmamem_alloc(b->dmatag, (void **)&b->buf, b->dmaflags,
	    &b->dmamap)) {
		sndbuf_free(b);
		return (ENOMEM);
	}
	if (bus_dmamap_load(b->dmatag, b->dmamap, b->buf, b->maxsize,
	    sndbuf_setmap, b, 0) != 0 || b->buf_addr == 0) {
		sndbuf_free(b);
		return (ENOMEM);
	}

	ret = sndbuf_resize(b, 2, b->maxsize / 2);
	if (ret != 0)
		sndbuf_free(b);

	return (ret);
}

int
sndbuf_setup(struct snd_dbuf *b, void *buf, unsigned int size)
{
	b->flags &= ~SNDBUF_F_MANAGED;
	if (buf)
		b->flags |= SNDBUF_F_MANAGED;
	b->buf = buf;
	b->maxsize = size;
	b->bufsize = b->maxsize;
	return sndbuf_resize(b, 2, b->maxsize / 2);
}

void
sndbuf_free(struct snd_dbuf *b)
{
	if (b->tmpbuf)
		free(b->tmpbuf, M_DEVBUF);

	if (b->shadbuf)
		free(b->shadbuf, M_DEVBUF);

	if (b->buf) {
		if (b->flags & SNDBUF_F_MANAGED) {
			if (b->buf_addr)
				bus_dmamap_unload(b->dmatag, b->dmamap);
			if (b->dmatag)
				bus_dmamem_free(b->dmatag, b->buf, b->dmamap);
		} else
			free(b->buf, M_DEVBUF);
	}

	b->tmpbuf = NULL;
	b->shadbuf = NULL;
	b->buf = NULL;
	b->sl = 0;
	b->dmatag = NULL;
	b->dmamap = NULL;
}

#define SNDBUF_CACHE_SHIFT	5

int
sndbuf_resize(struct snd_dbuf *b, unsigned int blkcnt, unsigned int blksz)
{
	unsigned int bufsize, allocsize;
	u_int8_t *tmpbuf;

	CHN_LOCK(b->channel);
	if (b->maxsize == 0)
		goto out;
	if (blkcnt == 0)
		blkcnt = b->blkcnt;
	if (blksz == 0)
		blksz = b->blksz;
	if (blkcnt < 2 || blksz < 16 || (blkcnt * blksz) > b->maxsize) {
		CHN_UNLOCK(b->channel);
		return EINVAL;
	}
	if (blkcnt == b->blkcnt && blksz == b->blksz)
		goto out;

	bufsize = blkcnt * blksz;

	if (bufsize > b->allocsize ||
	    bufsize < (b->allocsize >> SNDBUF_CACHE_SHIFT)) {
		allocsize = round_page(bufsize);
		CHN_UNLOCK(b->channel);
		tmpbuf = malloc(allocsize, M_DEVBUF, M_WAITOK);
		CHN_LOCK(b->channel);
		if (snd_verbose > 3)
			printf("%s(): b=%p %p -> %p [%d -> %d : %d]\n",
			    __func__, b, b->tmpbuf, tmpbuf,
			    b->allocsize, allocsize, bufsize);
		if (b->tmpbuf != NULL)
			free(b->tmpbuf, M_DEVBUF);
		b->tmpbuf = tmpbuf;
		b->allocsize = allocsize;
	} else if (snd_verbose > 3)
		printf("%s(): b=%p %d [%d] NOCHANGE\n",
		    __func__, b, b->allocsize, b->bufsize);

	b->blkcnt = blkcnt;
	b->blksz = blksz;
	b->bufsize = bufsize;

	sndbuf_reset(b);
out:
	CHN_UNLOCK(b->channel);
	return 0;
}

int
sndbuf_remalloc(struct snd_dbuf *b, unsigned int blkcnt, unsigned int blksz)
{
        unsigned int bufsize, allocsize;
	u_int8_t *buf, *tmpbuf, *shadbuf;

	if (blkcnt < 2 || blksz < 16)
		return EINVAL;

	bufsize = blksz * blkcnt;

	if (bufsize > b->allocsize ||
	    bufsize < (b->allocsize >> SNDBUF_CACHE_SHIFT)) {
		allocsize = round_page(bufsize);
		CHN_UNLOCK(b->channel);
		buf = malloc(allocsize, M_DEVBUF, M_WAITOK);
		tmpbuf = malloc(allocsize, M_DEVBUF, M_WAITOK);
		shadbuf = malloc(allocsize, M_DEVBUF, M_WAITOK);
		CHN_LOCK(b->channel);
		if (b->buf != NULL)
			free(b->buf, M_DEVBUF);
		b->buf = buf;
		if (b->tmpbuf != NULL)
			free(b->tmpbuf, M_DEVBUF);
		b->tmpbuf = tmpbuf;
		if (b->shadbuf != NULL)
			free(b->shadbuf, M_DEVBUF);
		b->shadbuf = shadbuf;
		if (snd_verbose > 3)
			printf("%s(): b=%p %d -> %d [%d]\n",
			    __func__, b, b->allocsize, allocsize, bufsize);
		b->allocsize = allocsize;
	} else if (snd_verbose > 3)
		printf("%s(): b=%p %d [%d] NOCHANGE\n",
		    __func__, b, b->allocsize, b->bufsize);

	b->blkcnt = blkcnt;
	b->blksz = blksz;
	b->bufsize = bufsize;
	b->maxsize = bufsize;
	b->sl = bufsize;

	sndbuf_reset(b);

	return 0;
}

/**
 * @brief Zero out space in buffer free area
 *
 * This function clears a chunk of @c length bytes in the buffer free area
 * (i.e., where the next write will be placed).
 *
 * @param b		buffer context
 * @param length	number of bytes to blank
 */
void
sndbuf_clear(struct snd_dbuf *b, unsigned int length)
{
	int i;
	u_char data, *p;

	if (length == 0)
		return;
	if (length > b->bufsize)
		length = b->bufsize;

	data = sndbuf_zerodata(b->fmt);

	i = sndbuf_getfreeptr(b);
	p = sndbuf_getbuf(b);
	while (length > 0) {
		p[i] = data;
		length--;
		i++;
		if (i >= b->bufsize)
			i = 0;
	}
}

/**
 * @brief Zap buffer contents, resetting "ready area" fields
 *
 * @param b	buffer context
 */
void
sndbuf_fillsilence(struct snd_dbuf *b)
{
	if (b->bufsize > 0)
		memset(sndbuf_getbuf(b), sndbuf_zerodata(b->fmt), b->bufsize);
	b->rp = 0;
	b->rl = b->bufsize;
}

void
sndbuf_fillsilence_rl(struct snd_dbuf *b, u_int rl)
{
	if (b->bufsize > 0)
		memset(sndbuf_getbuf(b), sndbuf_zerodata(b->fmt), b->bufsize);
	b->rp = 0;
	b->rl = min(b->bufsize, rl);
}

/**
 * @brief Reset buffer w/o flushing statistics
 *
 * This function just zeroes out buffer contents and sets the "ready length"
 * to zero.  This was originally to facilitate minimal playback interruption
 * (i.e., dropped samples) in SNDCTL_DSP_SILENCE/SKIP ioctls.
 *
 * @param b	buffer context
 */
void
sndbuf_softreset(struct snd_dbuf *b)
{
	b->rl = 0;
	if (b->buf && b->bufsize > 0)
		sndbuf_clear(b, b->bufsize);
}

void
sndbuf_reset(struct snd_dbuf *b)
{
	b->hp = 0;
	b->rp = 0;
	b->rl = 0;
	b->dl = 0;
	b->prev_total = 0;
	b->total = 0;
	b->xrun = 0;
	if (b->buf && b->bufsize > 0)
		sndbuf_clear(b, b->bufsize);
	sndbuf_clearshadow(b);
}

u_int32_t
sndbuf_getfmt(struct snd_dbuf *b)
{
	return b->fmt;
}

int
sndbuf_setfmt(struct snd_dbuf *b, u_int32_t fmt)
{
	b->fmt = fmt;
	b->bps = AFMT_BPS(b->fmt);
	b->align = AFMT_ALIGN(b->fmt);
#if 0
	b->bps = AFMT_CHANNEL(b->fmt);
	if (b->fmt & AFMT_16BIT)
		b->bps <<= 1;
	else if (b->fmt & AFMT_24BIT)
		b->bps *= 3;
	else if (b->fmt & AFMT_32BIT)
		b->bps <<= 2;
#endif
	return 0;
}

unsigned int
sndbuf_getspd(struct snd_dbuf *b)
{
	return b->spd;
}

void
sndbuf_setspd(struct snd_dbuf *b, unsigned int spd)
{
	b->spd = spd;
}

unsigned int
sndbuf_getalign(struct snd_dbuf *b)
{
	return (b->align);
}

unsigned int
sndbuf_getblkcnt(struct snd_dbuf *b)
{
	return b->blkcnt;
}

void
sndbuf_setblkcnt(struct snd_dbuf *b, unsigned int blkcnt)
{
	b->blkcnt = blkcnt;
}

unsigned int
sndbuf_getblksz(struct snd_dbuf *b)
{
	return b->blksz;
}

void
sndbuf_setblksz(struct snd_dbuf *b, unsigned int blksz)
{
	b->blksz = blksz;
}

unsigned int
sndbuf_getbps(struct snd_dbuf *b)
{
	return b->bps;
}

void *
sndbuf_getbuf(struct snd_dbuf *b)
{
	return b->buf;
}

void *
sndbuf_getbufofs(struct snd_dbuf *b, unsigned int ofs)
{
	KASSERT(ofs < b->bufsize, ("%s: ofs invalid %d", __func__, ofs));

	return b->buf + ofs;
}

unsigned int
sndbuf_getsize(struct snd_dbuf *b)
{
	return b->bufsize;
}

unsigned int
sndbuf_getmaxsize(struct snd_dbuf *b)
{
	return b->maxsize;
}

unsigned int
sndbuf_getallocsize(struct snd_dbuf *b)
{
	return b->allocsize;
}

unsigned int
sndbuf_runsz(struct snd_dbuf *b)
{
	return b->dl;
}

void
sndbuf_setrun(struct snd_dbuf *b, int go)
{
	b->dl = go? b->blksz : 0;
}

struct selinfo *
sndbuf_getsel(struct snd_dbuf *b)
{
	return &b->sel;
}

/************************************************************/
unsigned int
sndbuf_getxrun(struct snd_dbuf *b)
{
	SNDBUF_LOCKASSERT(b);

	return b->xrun;
}

void
sndbuf_setxrun(struct snd_dbuf *b, unsigned int xrun)
{
	SNDBUF_LOCKASSERT(b);

	b->xrun = xrun;
}

unsigned int
sndbuf_gethwptr(struct snd_dbuf *b)
{
	SNDBUF_LOCKASSERT(b);

	return b->hp;
}

void
sndbuf_sethwptr(struct snd_dbuf *b, unsigned int ptr)
{
	SNDBUF_LOCKASSERT(b);

	b->hp = ptr;
}

unsigned int
sndbuf_getready(struct snd_dbuf *b)
{
	SNDBUF_LOCKASSERT(b);
	KASSERT((b->rl >= 0) && (b->rl <= b->bufsize), ("%s: b->rl invalid %d", __func__, b->rl));

	return b->rl;
}

unsigned int
sndbuf_getreadyptr(struct snd_dbuf *b)
{
	SNDBUF_LOCKASSERT(b);
	KASSERT((b->rp >= 0) && (b->rp <= b->bufsize), ("%s: b->rp invalid %d", __func__, b->rp));

	return b->rp;
}

unsigned int
sndbuf_getfree(struct snd_dbuf *b)
{
	SNDBUF_LOCKASSERT(b);
	KASSERT((b->rl >= 0) && (b->rl <= b->bufsize), ("%s: b->rl invalid %d", __func__, b->rl));

	return b->bufsize - b->rl;
}

unsigned int
sndbuf_getfreeptr(struct snd_dbuf *b)
{
	SNDBUF_LOCKASSERT(b);
	KASSERT((b->rp >= 0) && (b->rp <= b->bufsize), ("%s: b->rp invalid %d", __func__, b->rp));
	KASSERT((b->rl >= 0) && (b->rl <= b->bufsize), ("%s: b->rl invalid %d", __func__, b->rl));

	return (b->rp + b->rl) % b->bufsize;
}

u_int64_t
sndbuf_getblocks(struct snd_dbuf *b)
{
	SNDBUF_LOCKASSERT(b);

	return b->total / b->blksz;
}

u_int64_t
sndbuf_getprevblocks(struct snd_dbuf *b)
{
	SNDBUF_LOCKASSERT(b);

	return b->prev_total / b->blksz;
}

u_int64_t
sndbuf_gettotal(struct snd_dbuf *b)
{
	SNDBUF_LOCKASSERT(b);

	return b->total;
}

u_int64_t
sndbuf_getprevtotal(struct snd_dbuf *b)
{
	SNDBUF_LOCKASSERT(b);

	return b->prev_total;
}

void
sndbuf_updateprevtotal(struct snd_dbuf *b)
{
	SNDBUF_LOCKASSERT(b);

	b->prev_total = b->total;
}

unsigned int
sndbuf_xbytes(unsigned int v, struct snd_dbuf *from, struct snd_dbuf *to)
{
	if (from == NULL || to == NULL || v == 0)
		return 0;

	return snd_xbytes(v, sndbuf_getalign(from) * sndbuf_getspd(from),
	    sndbuf_getalign(to) * sndbuf_getspd(to));
}

u_int8_t
sndbuf_zerodata(u_int32_t fmt)
{
	if (fmt & (AFMT_SIGNED | AFMT_PASSTHROUGH))
		return (0x00);
	else if (fmt & AFMT_MU_LAW)
		return (0x7f);
	else if (fmt & AFMT_A_LAW)
		return (0x55);
	return (0x80);
}

/************************************************************/

/**
 * @brief Acquire buffer space to extend ready area
 *
 * This function extends the ready area length by @c count bytes, and may
 * optionally copy samples from another location stored in @c from.  The
 * counter @c snd_dbuf::total is also incremented by @c count bytes.
 *
 * @param b	audio buffer
 * @param from	sample source (optional)
 * @param count	number of bytes to acquire
 *
 * @retval 0	Unconditional
 */
int
sndbuf_acquire(struct snd_dbuf *b, u_int8_t *from, unsigned int count)
{
	int l;

	KASSERT(count <= sndbuf_getfree(b), ("%s: count %d > free %d", __func__, count, sndbuf_getfree(b)));
	KASSERT((b->rl >= 0) && (b->rl <= b->bufsize), ("%s: b->rl invalid %d", __func__, b->rl));
	b->total += count;
	if (from != NULL) {
		while (count > 0) {
			l = min(count, sndbuf_getsize(b) - sndbuf_getfreeptr(b));
			bcopy(from, sndbuf_getbufofs(b, sndbuf_getfreeptr(b)), l);
			from += l;
			b->rl += l;
			count -= l;
		}
	} else
		b->rl += count;
	KASSERT((b->rl >= 0) && (b->rl <= b->bufsize), ("%s: b->rl invalid %d, count %d", __func__, b->rl, count));

	return 0;
}

/**
 * @brief Dispose samples from channel buffer, increasing size of ready area
 *
 * This function discards samples from the supplied buffer by advancing the
 * ready area start pointer and decrementing the ready area length.  If 
 * @c to is not NULL, then the discard samples will be copied to the location
 * it points to.
 *
 * @param b	PCM channel sound buffer
 * @param to	destination buffer (optional)
 * @param count	number of bytes to discard
 *
 * @returns 0 unconditionally
 */
int
sndbuf_dispose(struct snd_dbuf *b, u_int8_t *to, unsigned int count)
{
	int l;

	KASSERT(count <= sndbuf_getready(b), ("%s: count %d > ready %d", __func__, count, sndbuf_getready(b)));
	KASSERT((b->rl >= 0) && (b->rl <= b->bufsize), ("%s: b->rl invalid %d", __func__, b->rl));
	if (to != NULL) {
		while (count > 0) {
			l = min(count, sndbuf_getsize(b) - sndbuf_getreadyptr(b));
			bcopy(sndbuf_getbufofs(b, sndbuf_getreadyptr(b)), to, l);
			to += l;
			b->rl -= l;
			b->rp = (b->rp + l) % b->bufsize;
			count -= l;
		}
	} else {
		b->rl -= count;
		b->rp = (b->rp + count) % b->bufsize;
	}
	KASSERT((b->rl >= 0) && (b->rl <= b->bufsize), ("%s: b->rl invalid %d, count %d", __func__, b->rl, count));

	return 0;
}

#ifdef SND_DIAGNOSTIC
static uint32_t snd_feeder_maxfeed = 0;
SYSCTL_UINT(_hw_snd, OID_AUTO, feeder_maxfeed, CTLFLAG_RD,
    &snd_feeder_maxfeed, 0, "maximum feeder count request");

static uint32_t snd_feeder_maxcycle = 0;
SYSCTL_UINT(_hw_snd, OID_AUTO, feeder_maxcycle, CTLFLAG_RD,
    &snd_feeder_maxcycle, 0, "maximum feeder cycle");
#endif

/* count is number of bytes we want added to destination buffer */
int
sndbuf_feed(struct snd_dbuf *from, struct snd_dbuf *to, struct pcm_channel *channel, struct pcm_feeder *feeder, unsigned int count)
{
	unsigned int cnt, maxfeed;
#ifdef SND_DIAGNOSTIC
	unsigned int cycle;

	if (count > snd_feeder_maxfeed)
		snd_feeder_maxfeed = count;

	cycle = 0;
#endif

	KASSERT(count > 0, ("can't feed 0 bytes"));

	if (sndbuf_getfree(to) < count)
		return (EINVAL);

	maxfeed = SND_FXROUND(SND_FXDIV_MAX, sndbuf_getalign(to));

	do {
		cnt = FEEDER_FEED(feeder, channel, to->tmpbuf,
		    min(count, maxfeed), from);
		if (cnt == 0)
			break;
		sndbuf_acquire(to, to->tmpbuf, cnt);
		count -= cnt;
#ifdef SND_DIAGNOSTIC
		cycle++;
#endif
	} while (count != 0);

#ifdef SND_DIAGNOSTIC
	if (cycle > snd_feeder_maxcycle)
		snd_feeder_maxcycle = cycle;
#endif

	return (0);
}

/************************************************************/

void
sndbuf_dump(struct snd_dbuf *b, char *s, u_int32_t what)
{
	printf("%s: [", s);
	if (what & 0x01)
		printf(" bufsize: %d, maxsize: %d", b->bufsize, b->maxsize);
	if (what & 0x02)
		printf(" dl: %d, rp: %d, rl: %d, hp: %d", b->dl, b->rp, b->rl, b->hp);
	if (what & 0x04)
		printf(" total: %ju, prev_total: %ju, xrun: %d", (uintmax_t)b->total, (uintmax_t)b->prev_total, b->xrun);
   	if (what & 0x08)
		printf(" fmt: 0x%x, spd: %d", b->fmt, b->spd);
	if (what & 0x10)
		printf(" blksz: %d, blkcnt: %d, flags: 0x%x", b->blksz, b->blkcnt, b->flags);
	printf(" ]\n");
}

/************************************************************/
u_int32_t
sndbuf_getflags(struct snd_dbuf *b)
{
	return b->flags;
}

void
sndbuf_setflags(struct snd_dbuf *b, u_int32_t flags, int on)
{
	b->flags &= ~flags;
	if (on)
		b->flags |= flags;
}

/**
 * @brief Clear the shadow buffer by filling with samples equal to zero.
 *
 * @param b buffer to clear
 */
void
sndbuf_clearshadow(struct snd_dbuf *b)
{
	KASSERT(b != NULL, ("b is a null pointer"));
	KASSERT(b->sl >= 0, ("illegal shadow length"));

	if ((b->shadbuf != NULL) && (b->sl > 0))
		memset(b->shadbuf, sndbuf_zerodata(b->fmt), b->sl);
}

#ifdef OSSV4_EXPERIMENT
/**
 * @brief Return peak value from samples in buffer ready area.
 *
 * Peak ranges from 0-32767.  If channel is monaural, most significant 16
 * bits will be zero.  For now, only expects to work with 1-2 channel
 * buffers.
 *
 * @note  Currently only operates with linear PCM formats.
 *
 * @param b buffer to analyze
 * @param lpeak pointer to store left peak value
 * @param rpeak pointer to store right peak value
 */
void
sndbuf_getpeaks(struct snd_dbuf *b, int *lp, int *rp)
{
	u_int32_t lpeak, rpeak;

	lpeak = 0;
	rpeak = 0;

	/**
	 * @todo fill this in later
	 */
}
#endif
