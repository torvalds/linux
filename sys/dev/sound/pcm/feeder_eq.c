/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2009 Ariff Abdullah <ariff@FreeBSD.org>
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

/*
 * feeder_eq: Parametric (compile time) Software Equalizer. Though accidental,
 *            it proves good enough for educational and general consumption.
 *
 * "Cookbook formulae for audio EQ biquad filter coefficients"
 *    by Robert Bristow-Johnson  <rbj@audioimagination.com>
 *    -  http://www.musicdsp.org/files/Audio-EQ-Cookbook.txt
 */

#ifdef _KERNEL
#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif
#include <dev/sound/pcm/sound.h>
#include <dev/sound/pcm/pcm.h>
#include "feeder_if.h"

#define SND_USE_FXDIV
#include "snd_fxdiv_gen.h"

SND_DECLARE_FILE("$FreeBSD$");
#endif

#include "feeder_eq_gen.h"

#define FEEDEQ_LEVELS							\
	(((FEEDEQ_GAIN_MAX - FEEDEQ_GAIN_MIN) *				\
	(FEEDEQ_GAIN_DIV / FEEDEQ_GAIN_STEP)) + 1)

#define FEEDEQ_L2GAIN(v)						\
	((int)min(((v) * FEEDEQ_LEVELS) / 100, FEEDEQ_LEVELS - 1))

#define FEEDEQ_PREAMP_IPART(x)		(abs(x) >> FEEDEQ_GAIN_SHIFT)
#define FEEDEQ_PREAMP_FPART(x)		(abs(x) & FEEDEQ_GAIN_FMASK)
#define FEEDEQ_PREAMP_SIGNVAL(x)	((x) < 0 ? -1 : 1)
#define FEEDEQ_PREAMP_SIGNMARK(x)	(((x) < 0) ? '-' : '+')

#define FEEDEQ_PREAMP_IMIN	-192
#define FEEDEQ_PREAMP_IMAX	192
#define FEEDEQ_PREAMP_FMIN	0
#define FEEDEQ_PREAMP_FMAX	9

#define FEEDEQ_PREAMP_INVALID	INT_MAX

#define FEEDEQ_IF2PREAMP(i, f)						\
	((abs(i) << FEEDEQ_GAIN_SHIFT) |				\
	(((abs(f) / FEEDEQ_GAIN_STEP) * FEEDEQ_GAIN_STEP) &		\
	FEEDEQ_GAIN_FMASK))

#define FEEDEQ_PREAMP_MIN						\
	(FEEDEQ_PREAMP_SIGNVAL(FEEDEQ_GAIN_MIN) *			\
	FEEDEQ_IF2PREAMP(FEEDEQ_GAIN_MIN, 0))

#define FEEDEQ_PREAMP_MAX						\
	(FEEDEQ_PREAMP_SIGNVAL(FEEDEQ_GAIN_MAX) *			\
	FEEDEQ_IF2PREAMP(FEEDEQ_GAIN_MAX, 0))

#define FEEDEQ_PREAMP_DEFAULT	FEEDEQ_IF2PREAMP(0, 0)

#define FEEDEQ_PREAMP2IDX(v)						\
	((int32_t)((FEEDEQ_GAIN_MAX * (FEEDEQ_GAIN_DIV /		\
	FEEDEQ_GAIN_STEP)) + (FEEDEQ_PREAMP_SIGNVAL(v) *		\
	FEEDEQ_PREAMP_IPART(v) * (FEEDEQ_GAIN_DIV /			\
	FEEDEQ_GAIN_STEP)) + (FEEDEQ_PREAMP_SIGNVAL(v) *		\
	(FEEDEQ_PREAMP_FPART(v) / FEEDEQ_GAIN_STEP))))

static int feeder_eq_exact_rate = 0;

#ifdef _KERNEL
static char feeder_eq_presets[] = FEEDER_EQ_PRESETS;
SYSCTL_STRING(_hw_snd, OID_AUTO, feeder_eq_presets, CTLFLAG_RD,
    &feeder_eq_presets, 0, "compile-time eq presets");

SYSCTL_INT(_hw_snd, OID_AUTO, feeder_eq_exact_rate, CTLFLAG_RWTUN,
    &feeder_eq_exact_rate, 0, "force exact rate validation");
#endif

struct feed_eq_info;

typedef void (*feed_eq_t)(struct feed_eq_info *, uint8_t *, uint32_t);

struct feed_eq_tone {
	intpcm_t o1[SND_CHN_MAX];
	intpcm_t o2[SND_CHN_MAX];
	intpcm_t i1[SND_CHN_MAX];
	intpcm_t i2[SND_CHN_MAX];
	int gain;
};

struct feed_eq_info {
	struct feed_eq_tone treble;
	struct feed_eq_tone bass;
	struct feed_eq_coeff *coeff;
	feed_eq_t biquad;
	uint32_t channels;
	uint32_t rate;
	uint32_t align;
	int32_t preamp;
	int state;
};

#if !defined(_KERNEL) && defined(FEEDEQ_ERR_CLIP)
#define FEEDEQ_ERR_CLIP_CHECK(t, v)	do {				\
	if ((v) < PCM_S32_MIN || (v) > PCM_S32_MAX)			\
		errx(1, "\n\n%s(): ["#t"] Sample clipping: %jd\n",	\
		    __func__, (intmax_t)(v));				\
} while (0)
#else
#define FEEDEQ_ERR_CLIP_CHECK(...)
#endif

#define FEEDEQ_CLAMP(v)		(((v) > PCM_S32_MAX) ? PCM_S32_MAX :	\
				(((v) < PCM_S32_MIN) ? PCM_S32_MIN :	\
				  (v)))

#define FEEDEQ_DECLARE(SIGN, BIT, ENDIAN)					\
static void									\
feed_eq_biquad_##SIGN##BIT##ENDIAN(struct feed_eq_info *info,			\
    uint8_t *dst, uint32_t count)						\
{										\
	struct feed_eq_coeff_tone *treble, *bass;				\
	intpcm64_t w;								\
	intpcm_t v;								\
	uint32_t i, j;								\
	int32_t pmul, pshift;							\
										\
	pmul = feed_eq_preamp[info->preamp].mul;				\
	pshift = feed_eq_preamp[info->preamp].shift;				\
										\
	if (info->state == FEEDEQ_DISABLE) {					\
		j = count * info->channels;					\
		dst += j * PCM_##BIT##_BPS;					\
		do {								\
			dst -= PCM_##BIT##_BPS;					\
			v = _PCM_READ_##SIGN##BIT##_##ENDIAN(dst);		\
			v = ((intpcm64_t)pmul * v) >> pshift;			\
			_PCM_WRITE_##SIGN##BIT##_##ENDIAN(dst, v);		\
		} while (--j != 0);						\
										\
		return;								\
	}									\
										\
	treble = &(info->coeff[info->treble.gain].treble);			\
	bass   = &(info->coeff[info->bass.gain].bass);				\
										\
	do {									\
		i = 0;								\
		j = info->channels;						\
		do {								\
			v = _PCM_READ_##SIGN##BIT##_##ENDIAN(dst);		\
			v <<= 32 - BIT;						\
			v = ((intpcm64_t)pmul * v) >> pshift;			\
										\
			w  = (intpcm64_t)v * treble->b0;			\
			w += (intpcm64_t)info->treble.i1[i] * treble->b1;	\
			w += (intpcm64_t)info->treble.i2[i] * treble->b2;	\
			w -= (intpcm64_t)info->treble.o1[i] * treble->a1;	\
			w -= (intpcm64_t)info->treble.o2[i] * treble->a2;	\
			info->treble.i2[i] = info->treble.i1[i];		\
			info->treble.i1[i] = v;					\
			info->treble.o2[i] = info->treble.o1[i];		\
			w >>= FEEDEQ_COEFF_SHIFT;				\
			FEEDEQ_ERR_CLIP_CHECK(treble, w);			\
			v = FEEDEQ_CLAMP(w);					\
			info->treble.o1[i] = v;					\
										\
			w  = (intpcm64_t)v * bass->b0;				\
			w += (intpcm64_t)info->bass.i1[i] * bass->b1;		\
			w += (intpcm64_t)info->bass.i2[i] * bass->b2;		\
			w -= (intpcm64_t)info->bass.o1[i] * bass->a1;		\
			w -= (intpcm64_t)info->bass.o2[i] * bass->a2;		\
			info->bass.i2[i] = info->bass.i1[i];			\
			info->bass.i1[i] = v;					\
			info->bass.o2[i] = info->bass.o1[i];			\
			w >>= FEEDEQ_COEFF_SHIFT;				\
			FEEDEQ_ERR_CLIP_CHECK(bass, w);				\
			v = FEEDEQ_CLAMP(w);					\
			info->bass.o1[i] = v;					\
										\
			v >>= 32 - BIT;						\
			_PCM_WRITE_##SIGN##BIT##_##ENDIAN(dst, v);		\
			dst += PCM_##BIT##_BPS;					\
			i++;							\
		} while (--j != 0);						\
	} while (--count != 0);							\
}

#if BYTE_ORDER == LITTLE_ENDIAN || defined(SND_FEEDER_MULTIFORMAT)
FEEDEQ_DECLARE(S, 16, LE)
FEEDEQ_DECLARE(S, 32, LE)
#endif
#if BYTE_ORDER == BIG_ENDIAN || defined(SND_FEEDER_MULTIFORMAT)
FEEDEQ_DECLARE(S, 16, BE)
FEEDEQ_DECLARE(S, 32, BE)
#endif
#ifdef SND_FEEDER_MULTIFORMAT
FEEDEQ_DECLARE(S,  8, NE)
FEEDEQ_DECLARE(S, 24, LE)
FEEDEQ_DECLARE(S, 24, BE)
FEEDEQ_DECLARE(U,  8, NE)
FEEDEQ_DECLARE(U, 16, LE)
FEEDEQ_DECLARE(U, 24, LE)
FEEDEQ_DECLARE(U, 32, LE)
FEEDEQ_DECLARE(U, 16, BE)
FEEDEQ_DECLARE(U, 24, BE)
FEEDEQ_DECLARE(U, 32, BE)
#endif

#define FEEDEQ_ENTRY(SIGN, BIT, ENDIAN)					\
	{								\
		AFMT_##SIGN##BIT##_##ENDIAN,				\
		feed_eq_biquad_##SIGN##BIT##ENDIAN			\
	}


static const struct {
	uint32_t format;
	feed_eq_t biquad;
} feed_eq_biquad_tab[] = {
#if BYTE_ORDER == LITTLE_ENDIAN || defined(SND_FEEDER_MULTIFORMAT)
	FEEDEQ_ENTRY(S, 16, LE),
	FEEDEQ_ENTRY(S, 32, LE),
#endif
#if BYTE_ORDER == BIG_ENDIAN || defined(SND_FEEDER_MULTIFORMAT)
	FEEDEQ_ENTRY(S, 16, BE),
	FEEDEQ_ENTRY(S, 32, BE),
#endif
#ifdef SND_FEEDER_MULTIFORMAT
	FEEDEQ_ENTRY(S,  8, NE),
	FEEDEQ_ENTRY(S, 24, LE),
	FEEDEQ_ENTRY(S, 24, BE),
	FEEDEQ_ENTRY(U,  8, NE),
	FEEDEQ_ENTRY(U, 16, LE),
	FEEDEQ_ENTRY(U, 24, LE),
	FEEDEQ_ENTRY(U, 32, LE),
	FEEDEQ_ENTRY(U, 16, BE),
	FEEDEQ_ENTRY(U, 24, BE),
	FEEDEQ_ENTRY(U, 32, BE)
#endif
};

#define FEEDEQ_BIQUAD_TAB_SIZE						\
	((int32_t)(sizeof(feed_eq_biquad_tab) / sizeof(feed_eq_biquad_tab[0])))

static struct feed_eq_coeff *
feed_eq_coeff_rate(uint32_t rate)
{
	uint32_t spd, threshold;
	int i;

	if (rate < FEEDEQ_RATE_MIN || rate > FEEDEQ_RATE_MAX)
		return (NULL);

	/*
	 * Not all rates are supported. Choose the best rate that we can to
	 * allow 'sloppy' conversion. Good enough for naive listeners.
	 */
	for (i = 0; i < FEEDEQ_TAB_SIZE; i++) {
		spd = feed_eq_tab[i].rate;
		threshold = spd + ((i < (FEEDEQ_TAB_SIZE - 1) &&
		    feed_eq_tab[i + 1].rate > spd) ?
		    ((feed_eq_tab[i + 1].rate - spd) >> 1) : 0);
		if (rate == spd ||
		    (feeder_eq_exact_rate == 0 && rate <= threshold))
			return (feed_eq_tab[i].coeff);
	}

	return (NULL);
}

int
feeder_eq_validrate(uint32_t rate)
{

	if (feed_eq_coeff_rate(rate) != NULL)
		return (1);

	return (0);
}

static void
feed_eq_reset(struct feed_eq_info *info)
{
	uint32_t i;

	for (i = 0; i < info->channels; i++) {
		info->treble.i1[i] = 0;
		info->treble.i2[i] = 0;
		info->treble.o1[i] = 0;
		info->treble.o2[i] = 0;
		info->bass.i1[i] = 0;
		info->bass.i2[i] = 0;
		info->bass.o1[i] = 0;
		info->bass.o2[i] = 0;
	}
}

static int
feed_eq_setup(struct feed_eq_info *info)
{

	info->coeff = feed_eq_coeff_rate(info->rate);
	if (info->coeff == NULL)
		return (EINVAL);

	feed_eq_reset(info);

	return (0);
}

static int
feed_eq_init(struct pcm_feeder *f)
{
	struct feed_eq_info *info;
	feed_eq_t biquad_op;
	int i;

	if (f->desc->in != f->desc->out)
		return (EINVAL);

	biquad_op = NULL;

	for (i = 0; i < FEEDEQ_BIQUAD_TAB_SIZE && biquad_op == NULL; i++) {
		if (AFMT_ENCODING(f->desc->in) == feed_eq_biquad_tab[i].format)
			biquad_op = feed_eq_biquad_tab[i].biquad;
	}

	if (biquad_op == NULL)
		return (EINVAL);

	info = malloc(sizeof(*info), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (info == NULL)
		return (ENOMEM);

	info->channels = AFMT_CHANNEL(f->desc->in);
	info->align = info->channels * AFMT_BPS(f->desc->in);

	info->rate = FEEDEQ_RATE_MIN;
	info->treble.gain = FEEDEQ_L2GAIN(50);
	info->bass.gain = FEEDEQ_L2GAIN(50);
	info->preamp = FEEDEQ_PREAMP2IDX(FEEDEQ_PREAMP_DEFAULT);
	info->state = FEEDEQ_UNKNOWN;

	info->biquad = biquad_op;

	f->data = info;

	return (feed_eq_setup(info));
}

static int
feed_eq_set(struct pcm_feeder *f, int what, int value)
{
	struct feed_eq_info *info;

	info = f->data;

	switch (what) {
	case FEEDEQ_CHANNELS:
		if (value < SND_CHN_MIN || value > SND_CHN_MAX)
			return (EINVAL);
		info->channels = (uint32_t)value;
		info->align = info->channels * AFMT_BPS(f->desc->in);
		feed_eq_reset(info);
		break;
	case FEEDEQ_RATE:
		if (feeder_eq_validrate(value) == 0)
			return (EINVAL);
		info->rate = (uint32_t)value;
		if (info->state == FEEDEQ_UNKNOWN)
			info->state = FEEDEQ_ENABLE;
		return (feed_eq_setup(info));
		break;
	case FEEDEQ_TREBLE:
	case FEEDEQ_BASS:
		if (value < 0 || value > 100)
			return (EINVAL);
		if (what == FEEDEQ_TREBLE)
			info->treble.gain = FEEDEQ_L2GAIN(value);
		else
			info->bass.gain = FEEDEQ_L2GAIN(value);
		break;
	case FEEDEQ_PREAMP:
		if (value < FEEDEQ_PREAMP_MIN || value > FEEDEQ_PREAMP_MAX)
			return (EINVAL);
		info->preamp = FEEDEQ_PREAMP2IDX(value);
		break;
	case FEEDEQ_STATE:
		if (!(value == FEEDEQ_BYPASS || value == FEEDEQ_ENABLE ||
		    value == FEEDEQ_DISABLE))
			return (EINVAL);
		info->state = value;
		feed_eq_reset(info);
		break;
	default:
		return (EINVAL);
		break;
	}

	return (0);
}

static int
feed_eq_free(struct pcm_feeder *f)
{
	struct feed_eq_info *info;

	info = f->data;
	if (info != NULL)
		free(info, M_DEVBUF);

	f->data = NULL;

	return (0);
}

static int
feed_eq_feed(struct pcm_feeder *f, struct pcm_channel *c, uint8_t *b,
    uint32_t count, void *source)
{
	struct feed_eq_info *info;
	uint32_t j;
	uint8_t *dst;

	info = f->data;

	/*
	 * 3 major states:
	 * 	FEEDEQ_BYPASS  - Bypass entirely, nothing happened.
	 *      FEEDEQ_ENABLE  - Preamp+biquad filtering.
	 *      FEEDEQ_DISABLE - Preamp only.
	 */
	if (info->state == FEEDEQ_BYPASS)
		return (FEEDER_FEED(f->source, c, b, count, source));

	dst = b;
	count = SND_FXROUND(count, info->align);

	do {
		if (count < info->align)
			break;

		j = SND_FXDIV(FEEDER_FEED(f->source, c, dst, count, source),
		    info->align);
		if (j == 0)
			break;

		info->biquad(info, dst, j);

		j *= info->align;
		dst += j;
		count -= j;

	} while (count != 0);

	return (dst - b);
}

static struct pcm_feederdesc feeder_eq_desc[] = {
	{ FEEDER_EQ, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0 }
};

static kobj_method_t feeder_eq_methods[] = {
	KOBJMETHOD(feeder_init,		feed_eq_init),
	KOBJMETHOD(feeder_free,		feed_eq_free),
	KOBJMETHOD(feeder_set,		feed_eq_set),
	KOBJMETHOD(feeder_feed,		feed_eq_feed),
	KOBJMETHOD_END
};

FEEDER_DECLARE(feeder_eq, NULL);

static int32_t
feed_eq_scan_preamp_arg(const char *s)
{
	int r, i, f;
	size_t len;
	char buf[32];

	bzero(buf, sizeof(buf));

	/* XXX kind of ugly, but works for now.. */

	r = sscanf(s, "%d.%d", &i, &f);

	if (r == 1 && !(i < FEEDEQ_PREAMP_IMIN || i > FEEDEQ_PREAMP_IMAX)) {
		snprintf(buf, sizeof(buf), "%c%d",
		    FEEDEQ_PREAMP_SIGNMARK(i), abs(i));
		f = 0;
	} else if (r == 2 &&
	    !(i < FEEDEQ_PREAMP_IMIN || i > FEEDEQ_PREAMP_IMAX ||
	    f < FEEDEQ_PREAMP_FMIN || f > FEEDEQ_PREAMP_FMAX))
		snprintf(buf, sizeof(buf), "%c%d.%d",
		    FEEDEQ_PREAMP_SIGNMARK(i), abs(i), f);
	else
		return (FEEDEQ_PREAMP_INVALID);

	len = strlen(s);
	if (len > 2 && strcasecmp(s + len - 2, "dB") == 0)
		strlcat(buf, "dB", sizeof(buf));

	if (i == 0 && *s == '-')
		*buf = '-';

	if (strcasecmp(buf + ((*s >= '0' && *s <= '9') ? 1 : 0), s) != 0)
		return (FEEDEQ_PREAMP_INVALID);

	while ((f / FEEDEQ_GAIN_DIV) > 0)
		f /= FEEDEQ_GAIN_DIV;

	return (((i < 0 || *buf == '-') ? -1 : 1) * FEEDEQ_IF2PREAMP(i, f));
}

#ifdef _KERNEL
static int
sysctl_dev_pcm_eq(SYSCTL_HANDLER_ARGS)
{
	struct snddev_info *d;
	struct pcm_channel *c;
	struct pcm_feeder *f;
	int err, val, oval;

	d = oidp->oid_arg1;
	if (!PCM_REGISTERED(d))
		return (ENODEV);

	PCM_LOCK(d);
	PCM_WAIT(d);
	if (d->flags & SD_F_EQ_BYPASSED)
		val = 2;
	else if (d->flags & SD_F_EQ_ENABLED)
		val = 1;
	else
		val = 0;
	PCM_ACQUIRE(d);
	PCM_UNLOCK(d);

	oval = val;
	err = sysctl_handle_int(oidp, &val, 0, req);

	if (err == 0 && req->newptr != NULL && val != oval) {
		if (!(val == 0 || val == 1 || val == 2)) {
			PCM_RELEASE_QUICK(d);
			return (EINVAL);
		}

		PCM_LOCK(d);

		d->flags &= ~(SD_F_EQ_ENABLED | SD_F_EQ_BYPASSED);
		if (val == 2) {
			val = FEEDEQ_BYPASS;
			d->flags |= SD_F_EQ_BYPASSED;
		} else if (val == 1) {
			val = FEEDEQ_ENABLE;
			d->flags |= SD_F_EQ_ENABLED;
		} else
			val = FEEDEQ_DISABLE;

		CHN_FOREACH(c, d, channels.pcm.busy) {
			CHN_LOCK(c);
			f = chn_findfeeder(c, FEEDER_EQ);
			if (f != NULL)
				(void)FEEDER_SET(f, FEEDEQ_STATE, val);
			CHN_UNLOCK(c);
		}

		PCM_RELEASE(d);
		PCM_UNLOCK(d);
	} else
		PCM_RELEASE_QUICK(d);

	return (err);
}

static int
sysctl_dev_pcm_eq_preamp(SYSCTL_HANDLER_ARGS)
{
	struct snddev_info *d;
	struct pcm_channel *c;
	struct pcm_feeder *f;
	int err, val, oval;
	char buf[32];

	d = oidp->oid_arg1;
	if (!PCM_REGISTERED(d))
		return (ENODEV);

	PCM_LOCK(d);
	PCM_WAIT(d);
	val = d->eqpreamp;
	bzero(buf, sizeof(buf));
	(void)snprintf(buf, sizeof(buf), "%c%d.%ddB",
	    FEEDEQ_PREAMP_SIGNMARK(val), FEEDEQ_PREAMP_IPART(val),
	    FEEDEQ_PREAMP_FPART(val));
	PCM_ACQUIRE(d);
	PCM_UNLOCK(d);

	oval = val;
	err = sysctl_handle_string(oidp, buf, sizeof(buf), req);

	if (err == 0 && req->newptr != NULL) {
		val = feed_eq_scan_preamp_arg(buf);
		if (val == FEEDEQ_PREAMP_INVALID) {
			PCM_RELEASE_QUICK(d);
			return (EINVAL);
		}

		PCM_LOCK(d);

		if (val != oval) {
			if (val < FEEDEQ_PREAMP_MIN)
				val = FEEDEQ_PREAMP_MIN;
			else if (val > FEEDEQ_PREAMP_MAX)
				val = FEEDEQ_PREAMP_MAX;

			d->eqpreamp = val;

			CHN_FOREACH(c, d, channels.pcm.busy) {
				CHN_LOCK(c);
				f = chn_findfeeder(c, FEEDER_EQ);
				if (f != NULL)
					(void)FEEDER_SET(f, FEEDEQ_PREAMP, val);
				CHN_UNLOCK(c);
			}

		}

		PCM_RELEASE(d);
		PCM_UNLOCK(d);
	} else
		PCM_RELEASE_QUICK(d);

	return (err);
}

void
feeder_eq_initsys(device_t dev)
{
	struct snddev_info *d;
	const char *preamp;
	char buf[64];

	d = device_get_softc(dev);

	if (!(resource_string_value(device_get_name(dev), device_get_unit(dev),
	    "eq_preamp", &preamp) == 0 &&
	    (d->eqpreamp = feed_eq_scan_preamp_arg(preamp)) !=
	    FEEDEQ_PREAMP_INVALID))
		d->eqpreamp = FEEDEQ_PREAMP_DEFAULT;

	if (d->eqpreamp < FEEDEQ_PREAMP_MIN)
		d->eqpreamp = FEEDEQ_PREAMP_MIN;
	else if (d->eqpreamp > FEEDEQ_PREAMP_MAX)
		d->eqpreamp = FEEDEQ_PREAMP_MAX;

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "eq", CTLTYPE_INT | CTLFLAG_RWTUN, d, sizeof(d),
	    sysctl_dev_pcm_eq, "I",
	    "Bass/Treble Equalizer (0=disable, 1=enable, 2=bypass)");

	(void)snprintf(buf, sizeof(buf), "Bass/Treble Equalizer Preamp "
	    "(-/+ %d.0dB , %d.%ddB step)",
	    FEEDEQ_GAIN_MAX, FEEDEQ_GAIN_STEP / FEEDEQ_GAIN_DIV,
	    FEEDEQ_GAIN_STEP - ((FEEDEQ_GAIN_STEP / FEEDEQ_GAIN_DIV) *
	    FEEDEQ_GAIN_DIV));

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "eq_preamp", CTLTYPE_STRING | CTLFLAG_RWTUN, d, sizeof(d),
	    sysctl_dev_pcm_eq_preamp, "A", buf);
}
#endif
