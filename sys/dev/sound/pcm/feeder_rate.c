/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2009 Ariff Abdullah <ariff@FreeBSD.org>
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
 * feeder_rate: (Codename: Z Resampler), which means any effort to create
 *              future replacement for this resampler are simply absurd unless
 *              the world decide to add new alphabet after Z.
 *
 * FreeBSD bandlimited sinc interpolator, technically based on
 * "Digital Audio Resampling" by Julius O. Smith III
 *  - http://ccrma.stanford.edu/~jos/resample/
 *
 * The Good:
 * + all out fixed point integer operations, no soft-float or anything like
 *   that.
 * + classic polyphase converters with high quality coefficient's polynomial
 *   interpolators.
 * + fast, faster, or the fastest of its kind.
 * + compile time configurable.
 * + etc etc..
 *
 * The Bad:
 * - The z, z_, and Z_ . Due to mental block (or maybe just 0x7a69), I
 *   couldn't think of anything simpler than that (feeder_rate_xxx is just
 *   too long). Expect possible clashes with other zitizens (any?).
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

#include "feeder_rate_gen.h"

#if !defined(_KERNEL) && defined(SND_DIAGNOSTIC)
#undef Z_DIAGNOSTIC
#define Z_DIAGNOSTIC		1
#elif defined(_KERNEL)
#undef Z_DIAGNOSTIC
#endif

#ifndef Z_QUALITY_DEFAULT
#define Z_QUALITY_DEFAULT	Z_QUALITY_LINEAR
#endif

#define Z_RESERVOIR		2048
#define Z_RESERVOIR_MAX		131072

#define Z_SINC_MAX		0x3fffff
#define Z_SINC_DOWNMAX		48		/* 384000 / 8000 */

#ifdef _KERNEL
#define Z_POLYPHASE_MAX		183040		/* 286 taps, 640 phases */
#else
#define Z_POLYPHASE_MAX		1464320		/* 286 taps, 5120 phases */
#endif

#define Z_RATE_DEFAULT		48000

#define Z_RATE_MIN		FEEDRATE_RATEMIN
#define Z_RATE_MAX		FEEDRATE_RATEMAX
#define Z_ROUNDHZ		FEEDRATE_ROUNDHZ
#define Z_ROUNDHZ_MIN		FEEDRATE_ROUNDHZ_MIN
#define Z_ROUNDHZ_MAX		FEEDRATE_ROUNDHZ_MAX

#define Z_RATE_SRC		FEEDRATE_SRC
#define Z_RATE_DST		FEEDRATE_DST
#define Z_RATE_QUALITY		FEEDRATE_QUALITY
#define Z_RATE_CHANNELS		FEEDRATE_CHANNELS

#define Z_PARANOID		1

#define Z_MULTIFORMAT		1

#ifdef _KERNEL
#undef Z_USE_ALPHADRIFT
#define Z_USE_ALPHADRIFT	1
#endif

#define Z_FACTOR_MIN		1
#define Z_FACTOR_MAX		Z_MASK
#define Z_FACTOR_SAFE(v)	(!((v) < Z_FACTOR_MIN || (v) > Z_FACTOR_MAX))

struct z_info;

typedef void (*z_resampler_t)(struct z_info *, uint8_t *);

struct z_info {
	int32_t rsrc, rdst;	/* original source / destination rates */
	int32_t src, dst;	/* rounded source / destination rates */
	int32_t channels;	/* total channels */
	int32_t bps;		/* bytes-per-sample */
	int32_t quality;	/* resampling quality */

	int32_t z_gx, z_gy;	/* interpolation / decimation ratio */
	int32_t z_alpha;	/* output sample time phase / drift */
	uint8_t *z_delay;	/* FIR delay line / linear buffer */
	int32_t *z_coeff;	/* FIR coefficients */
	int32_t *z_dcoeff;	/* FIR coefficients differences */
	int32_t *z_pcoeff;	/* FIR polyphase coefficients */
	int32_t z_scale;	/* output scaling */
	int32_t z_dx;		/* input sample drift increment */
	int32_t z_dy;		/* output sample drift increment */
#ifdef Z_USE_ALPHADRIFT
	int32_t z_alphadrift;	/* alpha drift rate */
	int32_t z_startdrift;	/* buffer start position drift rate */
#endif
	int32_t z_mask;		/* delay line full length mask */
	int32_t z_size;		/* half width of FIR taps */
	int32_t z_full;		/* full size of delay line */
	int32_t z_alloc;	/* largest allocated full size of delay line */
	int32_t z_start;	/* buffer processing start position */
	int32_t z_pos;		/* current position for the next feed */
#ifdef Z_DIAGNOSTIC
	uint32_t z_cycle;	/* output cycle, purely for statistical */
#endif
	int32_t z_maxfeed;	/* maximum feed to avoid 32bit overflow */

	z_resampler_t z_resample;
};

int feeder_rate_min = Z_RATE_MIN;
int feeder_rate_max = Z_RATE_MAX;
int feeder_rate_round = Z_ROUNDHZ;
int feeder_rate_quality = Z_QUALITY_DEFAULT;

static int feeder_rate_polyphase_max = Z_POLYPHASE_MAX;

#ifdef _KERNEL
static char feeder_rate_presets[] = FEEDER_RATE_PRESETS;
SYSCTL_STRING(_hw_snd, OID_AUTO, feeder_rate_presets, CTLFLAG_RD,
    &feeder_rate_presets, 0, "compile-time rate presets");
SYSCTL_INT(_hw_snd, OID_AUTO, feeder_rate_polyphase_max, CTLFLAG_RWTUN,
    &feeder_rate_polyphase_max, 0, "maximum allowable polyphase entries");

static int
sysctl_hw_snd_feeder_rate_min(SYSCTL_HANDLER_ARGS)
{
	int err, val;

	val = feeder_rate_min;
	err = sysctl_handle_int(oidp, &val, 0, req);

	if (err != 0 || req->newptr == NULL || val == feeder_rate_min)
		return (err);

	if (!(Z_FACTOR_SAFE(val) && val < feeder_rate_max))
		return (EINVAL);

	feeder_rate_min = val;

	return (0);
}
SYSCTL_PROC(_hw_snd, OID_AUTO, feeder_rate_min, CTLTYPE_INT | CTLFLAG_RWTUN,
    0, sizeof(int), sysctl_hw_snd_feeder_rate_min, "I",
    "minimum allowable rate");

static int
sysctl_hw_snd_feeder_rate_max(SYSCTL_HANDLER_ARGS)
{
	int err, val;

	val = feeder_rate_max;
	err = sysctl_handle_int(oidp, &val, 0, req);

	if (err != 0 || req->newptr == NULL || val == feeder_rate_max)
		return (err);

	if (!(Z_FACTOR_SAFE(val) && val > feeder_rate_min))
		return (EINVAL);

	feeder_rate_max = val;

	return (0);
}
SYSCTL_PROC(_hw_snd, OID_AUTO, feeder_rate_max, CTLTYPE_INT | CTLFLAG_RWTUN,
    0, sizeof(int), sysctl_hw_snd_feeder_rate_max, "I",
    "maximum allowable rate");

static int
sysctl_hw_snd_feeder_rate_round(SYSCTL_HANDLER_ARGS)
{
	int err, val;

	val = feeder_rate_round;
	err = sysctl_handle_int(oidp, &val, 0, req);

	if (err != 0 || req->newptr == NULL || val == feeder_rate_round)
		return (err);

	if (val < Z_ROUNDHZ_MIN || val > Z_ROUNDHZ_MAX)
		return (EINVAL);

	feeder_rate_round = val - (val % Z_ROUNDHZ);

	return (0);
}
SYSCTL_PROC(_hw_snd, OID_AUTO, feeder_rate_round, CTLTYPE_INT | CTLFLAG_RWTUN,
    0, sizeof(int), sysctl_hw_snd_feeder_rate_round, "I",
    "sample rate converter rounding threshold");

static int
sysctl_hw_snd_feeder_rate_quality(SYSCTL_HANDLER_ARGS)
{
	struct snddev_info *d;
	struct pcm_channel *c;
	struct pcm_feeder *f;
	int i, err, val;

	val = feeder_rate_quality;
	err = sysctl_handle_int(oidp, &val, 0, req);

	if (err != 0 || req->newptr == NULL || val == feeder_rate_quality)
		return (err);

	if (val < Z_QUALITY_MIN || val > Z_QUALITY_MAX)
		return (EINVAL);

	feeder_rate_quality = val;

	/*
	 * Traverse all available channels on each device and try to
	 * set resampler quality if and only if it is exist as
	 * part of feeder chains and the channel is idle.
	 */
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
			f = chn_findfeeder(c, FEEDER_RATE);
			if (f == NULL || f->data == NULL || CHN_STARTED(c)) {
				CHN_UNLOCK(c);
				continue;
			}
			(void)FEEDER_SET(f, FEEDRATE_QUALITY, val);
			CHN_UNLOCK(c);
		}
		PCM_RELEASE(d);
		PCM_UNLOCK(d);
	}

	return (0);
}
SYSCTL_PROC(_hw_snd, OID_AUTO, feeder_rate_quality, CTLTYPE_INT | CTLFLAG_RWTUN,
    0, sizeof(int), sysctl_hw_snd_feeder_rate_quality, "I",
    "sample rate converter quality ("__XSTRING(Z_QUALITY_MIN)"=low .. "
    __XSTRING(Z_QUALITY_MAX)"=high)");
#endif	/* _KERNEL */


/*
 * Resampler type.
 */
#define Z_IS_ZOH(i)		((i)->quality == Z_QUALITY_ZOH)
#define Z_IS_LINEAR(i)		((i)->quality == Z_QUALITY_LINEAR)
#define Z_IS_SINC(i)		((i)->quality > Z_QUALITY_LINEAR)

/*
 * Macroses for accurate sample time drift calculations.
 *
 * gy2gx : given the amount of output, return the _exact_ required amount of
 *         input.
 * gx2gy : given the amount of input, return the _maximum_ amount of output
 *         that will be generated.
 * drift : given the amount of input and output, return the elapsed
 *         sample-time.
 */
#define _Z_GCAST(x)		((uint64_t)(x))

#if defined(__GNUCLIKE_ASM) && defined(__i386__)
/*
 * This is where i386 being beaten to a pulp. Fortunately this function is
 * rarely being called and if it is, it will decide the best (hopefully)
 * fastest way to do the division. If we can ensure that everything is dword
 * aligned, letting the compiler to call udivdi3 to do the division can be
 * faster compared to this.
 *
 * amd64 is the clear winner here, no question about it.
 */
static __inline uint32_t
Z_DIV(uint64_t v, uint32_t d)
{
	uint32_t hi, lo, quo, rem;

	hi = v >> 32;
	lo = v & 0xffffffff;

	/*
	 * As much as we can, try to avoid long division like a plague.
	 */
	if (hi == 0)
		quo = lo / d;
	else
		__asm("divl %2"
		    : "=a" (quo), "=d" (rem)
		    : "r" (d), "0" (lo), "1" (hi));

	return (quo);
}
#else
#define Z_DIV(x, y)		((x) / (y))
#endif

#define _Z_GY2GX(i, a, v)						\
	Z_DIV(((_Z_GCAST((i)->z_gx) * (v)) + ((i)->z_gy - (a) - 1)),	\
	(i)->z_gy)

#define _Z_GX2GY(i, a, v)						\
	Z_DIV(((_Z_GCAST((i)->z_gy) * (v)) + (a)), (i)->z_gx)

#define _Z_DRIFT(i, x, y)						\
	((_Z_GCAST((i)->z_gy) * (x)) - (_Z_GCAST((i)->z_gx) * (y)))

#define z_gy2gx(i, v)		_Z_GY2GX(i, (i)->z_alpha, v)
#define z_gx2gy(i, v)		_Z_GX2GY(i, (i)->z_alpha, v)
#define z_drift(i, x, y)	_Z_DRIFT(i, x, y)

/*
 * Macroses for SINC coefficients table manipulations.. whatever.
 */
#define Z_SINC_COEFF_IDX(i)	((i)->quality - Z_QUALITY_LINEAR - 1)

#define Z_SINC_LEN(i)							\
	((int32_t)(((uint64_t)z_coeff_tab[Z_SINC_COEFF_IDX(i)].len <<	\
	    Z_SHIFT) / (i)->z_dy))

#define Z_SINC_BASE_LEN(i)						\
	((z_coeff_tab[Z_SINC_COEFF_IDX(i)].len - 1) >> (Z_DRIFT_SHIFT - 1))

/*
 * Macroses for linear delay buffer operations. Alignment is not
 * really necessary since we're not using true circular buffer, but it
 * will help us guard against possible trespasser. To be honest,
 * the linear block operations does not need guarding at all due to
 * accurate drifting!
 */
#define z_align(i, v)		((v) & (i)->z_mask)
#define z_next(i, o, v)		z_align(i, (o) + (v))
#define z_prev(i, o, v)		z_align(i, (o) - (v))
#define z_fetched(i)		(z_align(i, (i)->z_pos - (i)->z_start) - 1)
#define z_free(i)		((i)->z_full - (i)->z_pos)

/*
 * Macroses for Bla Bla .. :)
 */
#define z_copy(src, dst, sz)	(void)memcpy(dst, src, sz)
#define z_feed(...)		FEEDER_FEED(__VA_ARGS__)

static __inline uint32_t
z_min(uint32_t x, uint32_t y)
{

	return ((x < y) ? x : y);
}

static int32_t
z_gcd(int32_t x, int32_t y)
{
	int32_t w;

	while (y != 0) {
		w = x % y;
		x = y;
		y = w;
	}

	return (x);
}

static int32_t
z_roundpow2(int32_t v)
{
	int32_t i;

	i = 1;

	/*
	 * Let it overflow at will..
	 */
	while (i > 0 && i < v)
		i <<= 1;

	return (i);
}

/*
 * Zero Order Hold, the worst of the worst, an insult against quality,
 * but super fast.
 */
static void
z_feed_zoh(struct z_info *info, uint8_t *dst)
{
#if 0
	z_copy(info->z_delay +
	    (info->z_start * info->channels * info->bps), dst,
	    info->channels * info->bps);
#else
	uint32_t cnt;
	uint8_t *src;

	cnt = info->channels * info->bps;
	src = info->z_delay + (info->z_start * cnt);

	/*
	 * This is a bit faster than doing bcopy() since we're dealing
	 * with possible unaligned samples.
	 */
	do {
		*dst++ = *src++;
	} while (--cnt != 0);
#endif
}

/*
 * Linear Interpolation. This at least sounds better (perceptually) and fast,
 * but without any proper filtering which means aliasing still exist and
 * could become worst with a right sample. Interpolation centered within
 * Z_LINEAR_ONE between the present and previous sample and everything is
 * done with simple 32bit scaling arithmetic.
 */
#define Z_DECLARE_LINEAR(SIGN, BIT, ENDIAN)					\
static void									\
z_feed_linear_##SIGN##BIT##ENDIAN(struct z_info *info, uint8_t *dst)		\
{										\
	int32_t z;								\
	intpcm_t x, y;								\
	uint32_t ch;								\
	uint8_t *sx, *sy;							\
										\
	z = ((uint32_t)info->z_alpha * info->z_dx) >> Z_LINEAR_UNSHIFT;		\
										\
	sx = info->z_delay + (info->z_start * info->channels *			\
	    PCM_##BIT##_BPS);							\
	sy = sx - (info->channels * PCM_##BIT##_BPS);				\
										\
	ch = info->channels;							\
										\
	do {									\
		x = _PCM_READ_##SIGN##BIT##_##ENDIAN(sx);			\
		y = _PCM_READ_##SIGN##BIT##_##ENDIAN(sy);			\
		x = Z_LINEAR_INTERPOLATE_##BIT(z, x, y);			\
		_PCM_WRITE_##SIGN##BIT##_##ENDIAN(dst, x);			\
		sx += PCM_##BIT##_BPS;						\
		sy += PCM_##BIT##_BPS;						\
		dst += PCM_##BIT##_BPS;						\
	} while (--ch != 0);							\
}

/*
 * Userland clipping diagnostic check, not enabled in kernel compilation.
 * While doing sinc interpolation, unrealistic samples like full scale sine
 * wav will clip, but for other things this will not make any noise at all.
 * Everybody should learn how to normalized perceived loudness of their own
 * music/sounds/samples (hint: ReplayGain).
 */
#ifdef Z_DIAGNOSTIC
#define Z_CLIP_CHECK(v, BIT)	do {					\
	if ((v) > PCM_S##BIT##_MAX) {					\
		fprintf(stderr, "Overflow: v=%jd, max=%jd\n",		\
		    (intmax_t)(v), (intmax_t)PCM_S##BIT##_MAX);		\
	} else if ((v) < PCM_S##BIT##_MIN) {				\
		fprintf(stderr, "Underflow: v=%jd, min=%jd\n",		\
		    (intmax_t)(v), (intmax_t)PCM_S##BIT##_MIN);		\
	}								\
} while (0)
#else
#define Z_CLIP_CHECK(...)
#endif

#define Z_CLAMP(v, BIT)							\
	(((v) > PCM_S##BIT##_MAX) ? PCM_S##BIT##_MAX :			\
	(((v) < PCM_S##BIT##_MIN) ? PCM_S##BIT##_MIN : (v)))

/*
 * Sine Cardinal (SINC) Interpolation. Scaling is done in 64 bit, so
 * there's no point to hold the plate any longer. All samples will be
 * shifted to a full 32 bit, scaled and restored during write for
 * maximum dynamic range (only for downsampling).
 */
#define _Z_SINC_ACCUMULATE(SIGN, BIT, ENDIAN, adv)			\
	c += z >> Z_SHIFT;						\
	z &= Z_MASK;							\
	coeff = Z_COEFF_INTERPOLATE(z, z_coeff[c], z_dcoeff[c]);	\
	x = _PCM_READ_##SIGN##BIT##_##ENDIAN(p);			\
	v += Z_NORM_##BIT((intpcm64_t)x * coeff);			\
	z += info->z_dy;						\
	p adv##= info->channels * PCM_##BIT##_BPS

/* 
 * XXX GCC4 optimization is such a !@#$%, need manual unrolling.
 */
#if defined(__GNUC__) && __GNUC__ >= 4
#define Z_SINC_ACCUMULATE(...)	do {					\
	_Z_SINC_ACCUMULATE(__VA_ARGS__);				\
	_Z_SINC_ACCUMULATE(__VA_ARGS__);				\
} while (0)
#define Z_SINC_ACCUMULATE_DECR		2
#else
#define Z_SINC_ACCUMULATE(...)	do {					\
	_Z_SINC_ACCUMULATE(__VA_ARGS__);				\
} while (0)
#define Z_SINC_ACCUMULATE_DECR		1
#endif

#define Z_DECLARE_SINC(SIGN, BIT, ENDIAN)					\
static void									\
z_feed_sinc_##SIGN##BIT##ENDIAN(struct z_info *info, uint8_t *dst)		\
{										\
	intpcm64_t v;								\
	intpcm_t x;								\
	uint8_t *p;								\
	int32_t coeff, z, *z_coeff, *z_dcoeff;					\
	uint32_t c, center, ch, i;						\
										\
	z_coeff = info->z_coeff;						\
	z_dcoeff = info->z_dcoeff;						\
	center = z_prev(info, info->z_start, info->z_size);			\
	ch = info->channels * PCM_##BIT##_BPS;					\
	dst += ch;								\
										\
	do {									\
		dst -= PCM_##BIT##_BPS;						\
		ch -= PCM_##BIT##_BPS;						\
		v = 0;								\
		z = info->z_alpha * info->z_dx;					\
		c = 0;								\
		p = info->z_delay + (z_next(info, center, 1) *			\
		    info->channels * PCM_##BIT##_BPS) + ch;			\
		for (i = info->z_size; i != 0; i -= Z_SINC_ACCUMULATE_DECR) 	\
			Z_SINC_ACCUMULATE(SIGN, BIT, ENDIAN, +);		\
		z = info->z_dy - (info->z_alpha * info->z_dx);			\
		c = 0;								\
		p = info->z_delay + (center * info->channels *			\
		    PCM_##BIT##_BPS) + ch;					\
		for (i = info->z_size; i != 0; i -= Z_SINC_ACCUMULATE_DECR) 	\
			Z_SINC_ACCUMULATE(SIGN, BIT, ENDIAN, -);		\
		if (info->z_scale != Z_ONE)					\
			v = Z_SCALE_##BIT(v, info->z_scale);			\
		else								\
			v >>= Z_COEFF_SHIFT - Z_GUARD_BIT_##BIT;		\
		Z_CLIP_CHECK(v, BIT);						\
		_PCM_WRITE_##SIGN##BIT##_##ENDIAN(dst, Z_CLAMP(v, BIT));	\
	} while (ch != 0);							\
}

#define Z_DECLARE_SINC_POLYPHASE(SIGN, BIT, ENDIAN)				\
static void									\
z_feed_sinc_polyphase_##SIGN##BIT##ENDIAN(struct z_info *info, uint8_t *dst)	\
{										\
	intpcm64_t v;								\
	intpcm_t x;								\
	uint8_t *p;								\
	int32_t ch, i, start, *z_pcoeff;					\
										\
	ch = info->channels * PCM_##BIT##_BPS;					\
	dst += ch;								\
	start = z_prev(info, info->z_start, (info->z_size << 1) - 1) * ch;	\
										\
	do {									\
		dst -= PCM_##BIT##_BPS;						\
		ch -= PCM_##BIT##_BPS;						\
		v = 0;								\
		p = info->z_delay + start + ch;					\
		z_pcoeff = info->z_pcoeff +					\
		    ((info->z_alpha * info->z_size) << 1);			\
		for (i = info->z_size; i != 0; i--) {				\
			x = _PCM_READ_##SIGN##BIT##_##ENDIAN(p);		\
			v += Z_NORM_##BIT((intpcm64_t)x * *z_pcoeff);		\
			z_pcoeff++;						\
			p += info->channels * PCM_##BIT##_BPS;			\
			x = _PCM_READ_##SIGN##BIT##_##ENDIAN(p);		\
			v += Z_NORM_##BIT((intpcm64_t)x * *z_pcoeff);		\
			z_pcoeff++;						\
			p += info->channels * PCM_##BIT##_BPS;			\
		}								\
		if (info->z_scale != Z_ONE)					\
			v = Z_SCALE_##BIT(v, info->z_scale);			\
		else								\
			v >>= Z_COEFF_SHIFT - Z_GUARD_BIT_##BIT;		\
		Z_CLIP_CHECK(v, BIT);						\
		_PCM_WRITE_##SIGN##BIT##_##ENDIAN(dst, Z_CLAMP(v, BIT));	\
	} while (ch != 0);							\
}

#define Z_DECLARE(SIGN, BIT, ENDIAN)					\
	Z_DECLARE_LINEAR(SIGN, BIT, ENDIAN)				\
	Z_DECLARE_SINC(SIGN, BIT, ENDIAN)				\
	Z_DECLARE_SINC_POLYPHASE(SIGN, BIT, ENDIAN)

#if BYTE_ORDER == LITTLE_ENDIAN || defined(SND_FEEDER_MULTIFORMAT)
Z_DECLARE(S, 16, LE)
Z_DECLARE(S, 32, LE)
#endif
#if BYTE_ORDER == BIG_ENDIAN || defined(SND_FEEDER_MULTIFORMAT)
Z_DECLARE(S, 16, BE)
Z_DECLARE(S, 32, BE)
#endif
#ifdef SND_FEEDER_MULTIFORMAT
Z_DECLARE(S,  8, NE)
Z_DECLARE(S, 24, LE)
Z_DECLARE(S, 24, BE)
Z_DECLARE(U,  8, NE)
Z_DECLARE(U, 16, LE)
Z_DECLARE(U, 24, LE)
Z_DECLARE(U, 32, LE)
Z_DECLARE(U, 16, BE)
Z_DECLARE(U, 24, BE)
Z_DECLARE(U, 32, BE)
#endif

enum {
	Z_RESAMPLER_ZOH,
	Z_RESAMPLER_LINEAR,
	Z_RESAMPLER_SINC,
	Z_RESAMPLER_SINC_POLYPHASE,
	Z_RESAMPLER_LAST
};

#define Z_RESAMPLER_IDX(i)						\
	(Z_IS_SINC(i) ? Z_RESAMPLER_SINC : (i)->quality)

#define Z_RESAMPLER_ENTRY(SIGN, BIT, ENDIAN)					\
	{									\
	    AFMT_##SIGN##BIT##_##ENDIAN,					\
	    {									\
		[Z_RESAMPLER_ZOH]    = z_feed_zoh,				\
		[Z_RESAMPLER_LINEAR] = z_feed_linear_##SIGN##BIT##ENDIAN,	\
		[Z_RESAMPLER_SINC]   = z_feed_sinc_##SIGN##BIT##ENDIAN,		\
		[Z_RESAMPLER_SINC_POLYPHASE]   =				\
		    z_feed_sinc_polyphase_##SIGN##BIT##ENDIAN			\
	    }									\
	}

static const struct {
	uint32_t format;
	z_resampler_t resampler[Z_RESAMPLER_LAST];
} z_resampler_tab[] = {
#if BYTE_ORDER == LITTLE_ENDIAN || defined(SND_FEEDER_MULTIFORMAT)
	Z_RESAMPLER_ENTRY(S, 16, LE),
	Z_RESAMPLER_ENTRY(S, 32, LE),
#endif
#if BYTE_ORDER == BIG_ENDIAN || defined(SND_FEEDER_MULTIFORMAT)
	Z_RESAMPLER_ENTRY(S, 16, BE),
	Z_RESAMPLER_ENTRY(S, 32, BE),
#endif
#ifdef SND_FEEDER_MULTIFORMAT
	Z_RESAMPLER_ENTRY(S,  8, NE),
	Z_RESAMPLER_ENTRY(S, 24, LE),
	Z_RESAMPLER_ENTRY(S, 24, BE),
	Z_RESAMPLER_ENTRY(U,  8, NE),
	Z_RESAMPLER_ENTRY(U, 16, LE),
	Z_RESAMPLER_ENTRY(U, 24, LE),
	Z_RESAMPLER_ENTRY(U, 32, LE),
	Z_RESAMPLER_ENTRY(U, 16, BE),
	Z_RESAMPLER_ENTRY(U, 24, BE),
	Z_RESAMPLER_ENTRY(U, 32, BE),
#endif
};

#define Z_RESAMPLER_TAB_SIZE						\
	((int32_t)(sizeof(z_resampler_tab) / sizeof(z_resampler_tab[0])))

static void
z_resampler_reset(struct z_info *info)
{

	info->src = info->rsrc - (info->rsrc % ((feeder_rate_round > 0 &&
	    info->rsrc > feeder_rate_round) ? feeder_rate_round : 1));
	info->dst = info->rdst - (info->rdst % ((feeder_rate_round > 0 &&
	    info->rdst > feeder_rate_round) ? feeder_rate_round : 1));
	info->z_gx = 1;
	info->z_gy = 1;
	info->z_alpha = 0;
	info->z_resample = NULL;
	info->z_size = 1;
	info->z_coeff = NULL;
	info->z_dcoeff = NULL;
	if (info->z_pcoeff != NULL) {
		free(info->z_pcoeff, M_DEVBUF);
		info->z_pcoeff = NULL;
	}
	info->z_scale = Z_ONE;
	info->z_dx = Z_FULL_ONE;
	info->z_dy = Z_FULL_ONE;
#ifdef Z_DIAGNOSTIC
	info->z_cycle = 0;
#endif
	if (info->quality < Z_QUALITY_MIN)
		info->quality = Z_QUALITY_MIN;
	else if (info->quality > Z_QUALITY_MAX)
		info->quality = Z_QUALITY_MAX;
}

#ifdef Z_PARANOID
static int32_t
z_resampler_sinc_len(struct z_info *info)
{
	int32_t c, z, len, lmax;

	if (!Z_IS_SINC(info))
		return (1);

	/*
	 * A rather careful (or useless) way to calculate filter length.
	 * Z_SINC_LEN() itself is accurate enough to do its job. Extra
	 * sanity checking is not going to hurt though..
	 */
	c = 0;
	z = info->z_dy;
	len = 0;
	lmax = z_coeff_tab[Z_SINC_COEFF_IDX(info)].len;

	do {
		c += z >> Z_SHIFT;
		z &= Z_MASK;
		z += info->z_dy;
	} while (c < lmax && ++len > 0);

	if (len != Z_SINC_LEN(info)) {
#ifdef _KERNEL
		printf("%s(): sinc l=%d != Z_SINC_LEN=%d\n",
		    __func__, len, Z_SINC_LEN(info));
#else
		fprintf(stderr, "%s(): sinc l=%d != Z_SINC_LEN=%d\n",
		    __func__, len, Z_SINC_LEN(info));
		return (-1);
#endif
	}

	return (len);
}
#else
#define z_resampler_sinc_len(i)		(Z_IS_SINC(i) ? Z_SINC_LEN(i) : 1)
#endif

#define Z_POLYPHASE_COEFF_SHIFT		0

/*
 * Pick suitable polynomial interpolators based on filter oversampled ratio
 * (2 ^ Z_DRIFT_SHIFT).
 */
#if !(defined(Z_COEFF_INTERP_ZOH) || defined(Z_COEFF_INTERP_LINEAR) ||		\
    defined(Z_COEFF_INTERP_QUADRATIC) || defined(Z_COEFF_INTERP_HERMITE) ||	\
    defined(Z_COEFF_INTER_BSPLINE) || defined(Z_COEFF_INTERP_OPT32X) ||		\
    defined(Z_COEFF_INTERP_OPT16X) || defined(Z_COEFF_INTERP_OPT8X) ||		\
    defined(Z_COEFF_INTERP_OPT4X) || defined(Z_COEFF_INTERP_OPT2X))
#if Z_DRIFT_SHIFT >= 6
#define Z_COEFF_INTERP_BSPLINE		1
#elif Z_DRIFT_SHIFT >= 5
#define Z_COEFF_INTERP_OPT32X		1
#elif Z_DRIFT_SHIFT == 4
#define Z_COEFF_INTERP_OPT16X		1
#elif Z_DRIFT_SHIFT == 3
#define Z_COEFF_INTERP_OPT8X		1
#elif Z_DRIFT_SHIFT == 2
#define Z_COEFF_INTERP_OPT4X		1
#elif Z_DRIFT_SHIFT == 1
#define Z_COEFF_INTERP_OPT2X		1
#else
#error "Z_DRIFT_SHIFT screwed!"
#endif
#endif

/*
 * In classic polyphase mode, the actual coefficients for each phases need to
 * be calculated based on default prototype filters. For highly oversampled
 * filter, linear or quadradatic interpolator should be enough. Anything less
 * than that require 'special' interpolators to reduce interpolation errors.
 *
 * "Polynomial Interpolators for High-Quality Resampling of Oversampled Audio"
 *    by Olli Niemitalo
 *    - http://www.student.oulu.fi/~oniemita/dsp/deip.pdf
 *
 */
static int32_t
z_coeff_interpolate(int32_t z, int32_t *z_coeff)
{
	int32_t coeff;
#if defined(Z_COEFF_INTERP_ZOH)

	/* 1-point, 0th-order (Zero Order Hold) */
	z = z;
	coeff = z_coeff[0];
#elif defined(Z_COEFF_INTERP_LINEAR)
	int32_t zl0, zl1;

	/* 2-point, 1st-order Linear */
	zl0 = z_coeff[0];
	zl1 = z_coeff[1] - z_coeff[0];

	coeff = Z_RSHIFT((int64_t)zl1 * z, Z_SHIFT) + zl0;
#elif defined(Z_COEFF_INTERP_QUADRATIC)
	int32_t zq0, zq1, zq2;

	/* 3-point, 2nd-order Quadratic */
	zq0 = z_coeff[0];
	zq1 = z_coeff[1] - z_coeff[-1];
	zq2 = z_coeff[1] + z_coeff[-1] - (z_coeff[0] << 1);

	coeff = Z_RSHIFT((Z_RSHIFT((int64_t)zq2 * z, Z_SHIFT) +
	    zq1) * z, Z_SHIFT + 1) + zq0;
#elif defined(Z_COEFF_INTERP_HERMITE)
	int32_t zh0, zh1, zh2, zh3;

	/* 4-point, 3rd-order Hermite */
	zh0 = z_coeff[0];
	zh1 = z_coeff[1] - z_coeff[-1];
	zh2 = (z_coeff[-1] << 1) - (z_coeff[0] * 5) + (z_coeff[1] << 2) -
	    z_coeff[2];
	zh3 = z_coeff[2] - z_coeff[-1] + ((z_coeff[0] - z_coeff[1]) * 3);

	coeff = Z_RSHIFT((Z_RSHIFT((Z_RSHIFT((int64_t)zh3 * z, Z_SHIFT) +
	    zh2) * z, Z_SHIFT) + zh1) * z, Z_SHIFT + 1) + zh0;
#elif defined(Z_COEFF_INTERP_BSPLINE)
	int32_t zb0, zb1, zb2, zb3;

	/* 4-point, 3rd-order B-Spline */
	zb0 = Z_RSHIFT(0x15555555LL * (((int64_t)z_coeff[0] << 2) +
	    z_coeff[-1] + z_coeff[1]), 30);
	zb1 = z_coeff[1] - z_coeff[-1];
	zb2 = z_coeff[-1] + z_coeff[1] - (z_coeff[0] << 1);
	zb3 = Z_RSHIFT(0x15555555LL * (((z_coeff[0] - z_coeff[1]) * 3) +
	    z_coeff[2] - z_coeff[-1]), 30);

	coeff = (Z_RSHIFT((Z_RSHIFT((Z_RSHIFT((int64_t)zb3 * z, Z_SHIFT) +
	    zb2) * z, Z_SHIFT) + zb1) * z, Z_SHIFT) + zb0 + 1) >> 1;
#elif defined(Z_COEFF_INTERP_OPT32X)
	int32_t zoz, zoe1, zoe2, zoe3, zoo1, zoo2, zoo3;
	int32_t zoc0, zoc1, zoc2, zoc3, zoc4, zoc5;

	/* 6-point, 5th-order Optimal 32x */
	zoz = z - (Z_ONE >> 1);
	zoe1 = z_coeff[1] + z_coeff[0];
	zoe2 = z_coeff[2] + z_coeff[-1];
	zoe3 = z_coeff[3] + z_coeff[-2];
	zoo1 = z_coeff[1] - z_coeff[0];
	zoo2 = z_coeff[2] - z_coeff[-1];
	zoo3 = z_coeff[3] - z_coeff[-2];

	zoc0 = Z_RSHIFT((0x1ac2260dLL * zoe1) + (0x0526cdcaLL * zoe2) +
	    (0x00170c29LL * zoe3), 30);
	zoc1 = Z_RSHIFT((0x14f8a49aLL * zoo1) + (0x0d6d1109LL * zoo2) +
	    (0x008cd4dcLL * zoo3), 30);
	zoc2 = Z_RSHIFT((-0x0d3e94a4LL * zoe1) + (0x0bddded4LL * zoe2) +
	    (0x0160b5d0LL * zoe3), 30);
	zoc3 = Z_RSHIFT((-0x0de10cc4LL * zoo1) + (0x019b2a7dLL * zoo2) +
	    (0x01cfe914LL * zoo3), 30);
	zoc4 = Z_RSHIFT((0x02aa12d7LL * zoe1) + (-0x03ff1bb3LL * zoe2) +
	    (0x015508ddLL * zoe3), 30);
	zoc5 = Z_RSHIFT((0x051d29e5LL * zoo1) + (-0x028e7647LL * zoo2) +
	    (0x0082d81aLL * zoo3), 30);

	coeff = Z_RSHIFT((Z_RSHIFT((Z_RSHIFT((Z_RSHIFT((Z_RSHIFT(
	    (int64_t)zoc5 * zoz, Z_SHIFT) +
	    zoc4) * zoz, Z_SHIFT) + zoc3) * zoz, Z_SHIFT) +
	    zoc2) * zoz, Z_SHIFT) + zoc1) * zoz, Z_SHIFT) + zoc0;
#elif defined(Z_COEFF_INTERP_OPT16X)
	int32_t zoz, zoe1, zoe2, zoe3, zoo1, zoo2, zoo3;
	int32_t zoc0, zoc1, zoc2, zoc3, zoc4, zoc5;

	/* 6-point, 5th-order Optimal 16x */
	zoz = z - (Z_ONE >> 1);
	zoe1 = z_coeff[1] + z_coeff[0];
	zoe2 = z_coeff[2] + z_coeff[-1];
	zoe3 = z_coeff[3] + z_coeff[-2];
	zoo1 = z_coeff[1] - z_coeff[0];
	zoo2 = z_coeff[2] - z_coeff[-1];
	zoo3 = z_coeff[3] - z_coeff[-2];

	zoc0 = Z_RSHIFT((0x1ac2260dLL * zoe1) + (0x0526cdcaLL * zoe2) +
	    (0x00170c29LL * zoe3), 30);
	zoc1 = Z_RSHIFT((0x14f8a49aLL * zoo1) + (0x0d6d1109LL * zoo2) +
	    (0x008cd4dcLL * zoo3), 30);
	zoc2 = Z_RSHIFT((-0x0d3e94a4LL * zoe1) + (0x0bddded4LL * zoe2) +
	    (0x0160b5d0LL * zoe3), 30);
	zoc3 = Z_RSHIFT((-0x0de10cc4LL * zoo1) + (0x019b2a7dLL * zoo2) +
	    (0x01cfe914LL * zoo3), 30);
	zoc4 = Z_RSHIFT((0x02aa12d7LL * zoe1) + (-0x03ff1bb3LL * zoe2) +
	    (0x015508ddLL * zoe3), 30);
	zoc5 = Z_RSHIFT((0x051d29e5LL * zoo1) + (-0x028e7647LL * zoo2) +
	    (0x0082d81aLL * zoo3), 30);

	coeff = Z_RSHIFT((Z_RSHIFT((Z_RSHIFT((Z_RSHIFT((Z_RSHIFT(
	    (int64_t)zoc5 * zoz, Z_SHIFT) +
	    zoc4) * zoz, Z_SHIFT) + zoc3) * zoz, Z_SHIFT) +
	    zoc2) * zoz, Z_SHIFT) + zoc1) * zoz, Z_SHIFT) + zoc0;
#elif defined(Z_COEFF_INTERP_OPT8X)
	int32_t zoz, zoe1, zoe2, zoe3, zoo1, zoo2, zoo3;
	int32_t zoc0, zoc1, zoc2, zoc3, zoc4, zoc5;

	/* 6-point, 5th-order Optimal 8x */
	zoz = z - (Z_ONE >> 1);
	zoe1 = z_coeff[1] + z_coeff[0];
	zoe2 = z_coeff[2] + z_coeff[-1];
	zoe3 = z_coeff[3] + z_coeff[-2];
	zoo1 = z_coeff[1] - z_coeff[0];
	zoo2 = z_coeff[2] - z_coeff[-1];
	zoo3 = z_coeff[3] - z_coeff[-2];

	zoc0 = Z_RSHIFT((0x1aa9b47dLL * zoe1) + (0x053d9944LL * zoe2) +
	    (0x0018b23fLL * zoe3), 30);
	zoc1 = Z_RSHIFT((0x14a104d1LL * zoo1) + (0x0d7d2504LL * zoo2) +
	    (0x0094b599LL * zoo3), 30);
	zoc2 = Z_RSHIFT((-0x0d22530bLL * zoe1) + (0x0bb37a2cLL * zoe2) +
	    (0x016ed8e0LL * zoe3), 30);
	zoc3 = Z_RSHIFT((-0x0d744b1cLL * zoo1) + (0x01649591LL * zoo2) +
	    (0x01dae93aLL * zoo3), 30);
	zoc4 = Z_RSHIFT((0x02a7ee1bLL * zoe1) + (-0x03fbdb24LL * zoe2) +
	    (0x0153ed07LL * zoe3), 30);
	zoc5 = Z_RSHIFT((0x04cf9b6cLL * zoo1) + (-0x0266b378LL * zoo2) +
	    (0x007a7c26LL * zoo3), 30);

	coeff = Z_RSHIFT((Z_RSHIFT((Z_RSHIFT((Z_RSHIFT((Z_RSHIFT(
	    (int64_t)zoc5 * zoz, Z_SHIFT) +
	    zoc4) * zoz, Z_SHIFT) + zoc3) * zoz, Z_SHIFT) +
	    zoc2) * zoz, Z_SHIFT) + zoc1) * zoz, Z_SHIFT) + zoc0;
#elif defined(Z_COEFF_INTERP_OPT4X)
	int32_t zoz, zoe1, zoe2, zoe3, zoo1, zoo2, zoo3;
	int32_t zoc0, zoc1, zoc2, zoc3, zoc4, zoc5;

	/* 6-point, 5th-order Optimal 4x */
	zoz = z - (Z_ONE >> 1);
	zoe1 = z_coeff[1] + z_coeff[0];
	zoe2 = z_coeff[2] + z_coeff[-1];
	zoe3 = z_coeff[3] + z_coeff[-2];
	zoo1 = z_coeff[1] - z_coeff[0];
	zoo2 = z_coeff[2] - z_coeff[-1];
	zoo3 = z_coeff[3] - z_coeff[-2];

	zoc0 = Z_RSHIFT((0x1a8eda43LL * zoe1) + (0x0556ee38LL * zoe2) +
	    (0x001a3784LL * zoe3), 30);
	zoc1 = Z_RSHIFT((0x143d863eLL * zoo1) + (0x0d910e36LL * zoo2) +
	    (0x009ca889LL * zoo3), 30);
	zoc2 = Z_RSHIFT((-0x0d026821LL * zoe1) + (0x0b837773LL * zoe2) +
	    (0x017ef0c6LL * zoe3), 30);
	zoc3 = Z_RSHIFT((-0x0cef1502LL * zoo1) + (0x01207a8eLL * zoo2) +
	    (0x01e936dbLL * zoo3), 30);
	zoc4 = Z_RSHIFT((0x029fe643LL * zoe1) + (-0x03ef3fc8LL * zoe2) +
	    (0x014f5923LL * zoe3), 30);
	zoc5 = Z_RSHIFT((0x043a9d08LL * zoo1) + (-0x02154febLL * zoo2) +
	    (0x00670dbdLL * zoo3), 30);

	coeff = Z_RSHIFT((Z_RSHIFT((Z_RSHIFT((Z_RSHIFT((Z_RSHIFT(
	    (int64_t)zoc5 * zoz, Z_SHIFT) +
	    zoc4) * zoz, Z_SHIFT) + zoc3) * zoz, Z_SHIFT) +
	    zoc2) * zoz, Z_SHIFT) + zoc1) * zoz, Z_SHIFT) + zoc0;
#elif defined(Z_COEFF_INTERP_OPT2X)
	int32_t zoz, zoe1, zoe2, zoe3, zoo1, zoo2, zoo3;
	int32_t zoc0, zoc1, zoc2, zoc3, zoc4, zoc5;

	/* 6-point, 5th-order Optimal 2x */
	zoz = z - (Z_ONE >> 1);
	zoe1 = z_coeff[1] + z_coeff[0];
	zoe2 = z_coeff[2] + z_coeff[-1];
	zoe3 = z_coeff[3] + z_coeff[-2];
	zoo1 = z_coeff[1] - z_coeff[0];
	zoo2 = z_coeff[2] - z_coeff[-1];
	zoo3 = z_coeff[3] - z_coeff[-2];

	zoc0 = Z_RSHIFT((0x19edb6fdLL * zoe1) + (0x05ebd062LL * zoe2) +
	    (0x00267881LL * zoe3), 30);
	zoc1 = Z_RSHIFT((0x1223af76LL * zoo1) + (0x0de3dd6bLL * zoo2) +
	    (0x00d683cdLL * zoo3), 30);
	zoc2 = Z_RSHIFT((-0x0c3ee068LL * zoe1) + (0x0a5c3769LL * zoe2) +
	    (0x01e2aceaLL * zoe3), 30);
	zoc3 = Z_RSHIFT((-0x0a8ab614LL * zoo1) + (-0x0019522eLL * zoo2) +
	    (0x022cefc7LL * zoo3), 30);
	zoc4 = Z_RSHIFT((0x0276187dLL * zoe1) + (-0x03a801e8LL * zoe2) +
	    (0x0131d935LL * zoe3), 30);
	zoc5 = Z_RSHIFT((0x02c373f5LL * zoo1) + (-0x01275f83LL * zoo2) +
	    (0x0018ee79LL * zoo3), 30);

	coeff = Z_RSHIFT((Z_RSHIFT((Z_RSHIFT((Z_RSHIFT((Z_RSHIFT(
	    (int64_t)zoc5 * zoz, Z_SHIFT) +
	    zoc4) * zoz, Z_SHIFT) + zoc3) * zoz, Z_SHIFT) +
	    zoc2) * zoz, Z_SHIFT) + zoc1) * zoz, Z_SHIFT) + zoc0;
#else
#error "Interpolation type screwed!"
#endif

#if Z_POLYPHASE_COEFF_SHIFT > 0
	coeff = Z_RSHIFT(coeff, Z_POLYPHASE_COEFF_SHIFT);
#endif
	return (coeff);
}

static int
z_resampler_build_polyphase(struct z_info *info)
{
	int32_t alpha, c, i, z, idx;

	/* Let this be here first. */
	if (info->z_pcoeff != NULL) {
		free(info->z_pcoeff, M_DEVBUF);
		info->z_pcoeff = NULL;
	}

	if (feeder_rate_polyphase_max < 1)
		return (ENOTSUP);

	if (((int64_t)info->z_size * info->z_gy * 2) >
	    feeder_rate_polyphase_max) {
#ifndef _KERNEL
		fprintf(stderr, "Polyphase entries exceed: [%d/%d] %jd > %d\n",
		    info->z_gx, info->z_gy,
		    (intmax_t)info->z_size * info->z_gy * 2,
		    feeder_rate_polyphase_max);
#endif
		return (E2BIG);
	}

	info->z_pcoeff = malloc(sizeof(int32_t) *
	    info->z_size * info->z_gy * 2, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (info->z_pcoeff == NULL)
		return (ENOMEM);

	for (alpha = 0; alpha < info->z_gy; alpha++) {
		z = alpha * info->z_dx;
		c = 0;
		for (i = info->z_size; i != 0; i--) {
			c += z >> Z_SHIFT;
			z &= Z_MASK;
			idx = (alpha * info->z_size * 2) +
			    (info->z_size * 2) - i;
			info->z_pcoeff[idx] =
			    z_coeff_interpolate(z, info->z_coeff + c);
			z += info->z_dy;
		}
		z = info->z_dy - (alpha * info->z_dx);
		c = 0;
		for (i = info->z_size; i != 0; i--) {
			c += z >> Z_SHIFT;
			z &= Z_MASK;
			idx = (alpha * info->z_size * 2) + i - 1;
			info->z_pcoeff[idx] =
			    z_coeff_interpolate(z, info->z_coeff + c);
			z += info->z_dy;
		}
	}
	
#ifndef _KERNEL
	fprintf(stderr, "Polyphase: [%d/%d] %d entries\n",
	    info->z_gx, info->z_gy, info->z_size * info->z_gy * 2);
#endif

	return (0);
}

static int
z_resampler_setup(struct pcm_feeder *f)
{
	struct z_info *info;
	int64_t gy2gx_max, gx2gy_max;
	uint32_t format;
	int32_t align, i, z_scale;
	int adaptive;

	info = f->data;
	z_resampler_reset(info);

	if (info->src == info->dst)
		return (0);

	/* Shrink by greatest common divisor. */
	i = z_gcd(info->src, info->dst);
	info->z_gx = info->src / i;
	info->z_gy = info->dst / i;

	/* Too big, or too small. Bail out. */
	if (!(Z_FACTOR_SAFE(info->z_gx) && Z_FACTOR_SAFE(info->z_gy)))
		return (EINVAL);

	format = f->desc->in;
	adaptive = 0;
	z_scale = 0;

	/*
	 * Setup everything: filter length, conversion factor, etc.
	 */
	if (Z_IS_SINC(info)) {
		/*
		 * Downsampling, or upsampling scaling factor. As long as the
		 * factor can be represented by a fraction of 1 << Z_SHIFT,
		 * we're pretty much in business. Scaling is not needed for
		 * upsampling, so we just slap Z_ONE there.
		 */
		if (info->z_gx > info->z_gy)
			/*
			 * If the downsampling ratio is beyond sanity,
			 * enable semi-adaptive mode. Although handling
			 * extreme ratio is possible, the result of the
			 * conversion is just pointless, unworthy,
			 * nonsensical noises, etc.
			 */
			if ((info->z_gx / info->z_gy) > Z_SINC_DOWNMAX)
				z_scale = Z_ONE / Z_SINC_DOWNMAX;
			else
				z_scale = ((uint64_t)info->z_gy << Z_SHIFT) /
				    info->z_gx;
		else
			z_scale = Z_ONE;

		/*
		 * This is actually impossible, unless anything above
		 * overflow.
		 */
		if (z_scale < 1)
			return (E2BIG);

		/*
		 * Calculate sample time/coefficients index drift. It is
		 * a constant for upsampling, but downsampling require
		 * heavy duty filtering with possible too long filters.
		 * If anything goes wrong, revisit again and enable
		 * adaptive mode.
		 */
z_setup_adaptive_sinc:
		if (info->z_pcoeff != NULL) {
			free(info->z_pcoeff, M_DEVBUF);
			info->z_pcoeff = NULL;
		}

		if (adaptive == 0) {
			info->z_dy = z_scale << Z_DRIFT_SHIFT;
			if (info->z_dy < 1)
				return (E2BIG);
			info->z_scale = z_scale;
		} else {
			info->z_dy = Z_FULL_ONE;
			info->z_scale = Z_ONE;
		}

#if 0
#define Z_SCALE_DIV	10000
#define Z_SCALE_LIMIT(s, v)						\
	((((uint64_t)(s) * (v)) + (Z_SCALE_DIV >> 1)) / Z_SCALE_DIV)

		info->z_scale = Z_SCALE_LIMIT(info->z_scale, 9780);
#endif

		/* Smallest drift increment. */
		info->z_dx = info->z_dy / info->z_gy;

		/*
		 * Overflow or underflow. Try adaptive, let it continue and
		 * retry.
		 */
		if (info->z_dx < 1) {
			if (adaptive == 0) {
				adaptive = 1;
				goto z_setup_adaptive_sinc;
			}
			return (E2BIG);
		}

		/*
		 * Round back output drift.
		 */
		info->z_dy = info->z_dx * info->z_gy;

		for (i = 0; i < Z_COEFF_TAB_SIZE; i++) {
			if (Z_SINC_COEFF_IDX(info) != i)
				continue;
			/*
			 * Calculate required filter length and guard
			 * against possible abusive result. Note that
			 * this represents only 1/2 of the entire filter
			 * length.
			 */
			info->z_size = z_resampler_sinc_len(info);

			/*
			 * Multiple of 2 rounding, for better accumulator
			 * performance.
			 */
			info->z_size &= ~1;

			if (info->z_size < 2 || info->z_size > Z_SINC_MAX) {
				if (adaptive == 0) {
					adaptive = 1;
					goto z_setup_adaptive_sinc;
				}
				return (E2BIG);
			}
			info->z_coeff = z_coeff_tab[i].coeff + Z_COEFF_OFFSET;
			info->z_dcoeff = z_coeff_tab[i].dcoeff;
			break;
		}

		if (info->z_coeff == NULL || info->z_dcoeff == NULL)
			return (EINVAL);
	} else if (Z_IS_LINEAR(info)) {
		/*
		 * Don't put much effort if we're doing linear interpolation.
		 * Just center the interpolation distance within Z_LINEAR_ONE,
		 * and be happy about it.
		 */
		info->z_dx = Z_LINEAR_FULL_ONE / info->z_gy;
	}

	/*
	 * We're safe for now, lets continue.. Look for our resampler
	 * depending on configured format and quality.
	 */
	for (i = 0; i < Z_RESAMPLER_TAB_SIZE; i++) {
		int ridx;

		if (AFMT_ENCODING(format) != z_resampler_tab[i].format)
			continue;
		if (Z_IS_SINC(info) && adaptive == 0 &&
		    z_resampler_build_polyphase(info) == 0)
			ridx = Z_RESAMPLER_SINC_POLYPHASE;
		else
			ridx = Z_RESAMPLER_IDX(info);
		info->z_resample = z_resampler_tab[i].resampler[ridx];
		break;
	}

	if (info->z_resample == NULL)
		return (EINVAL);

	info->bps = AFMT_BPS(format);
	align = info->channels * info->bps;

	/*
	 * Calculate largest value that can be fed into z_gy2gx() and
	 * z_gx2gy() without causing (signed) 32bit overflow. z_gy2gx() will
	 * be called early during feeding process to determine how much input
	 * samples that is required to generate requested output, while
	 * z_gx2gy() will be called just before samples filtering /
	 * accumulation process based on available samples that has been
	 * calculated using z_gx2gy().
	 *
	 * Now that is damn confusing, I guess ;-) .
	 */
	gy2gx_max = (((uint64_t)info->z_gy * INT32_MAX) - info->z_gy + 1) /
	    info->z_gx;

	if ((gy2gx_max * align) > SND_FXDIV_MAX)
		gy2gx_max = SND_FXDIV_MAX / align;

	if (gy2gx_max < 1)
		return (E2BIG);

	gx2gy_max = (((uint64_t)info->z_gx * INT32_MAX) - info->z_gy) /
	    info->z_gy;

	if (gx2gy_max > INT32_MAX)
		gx2gy_max = INT32_MAX;

	if (gx2gy_max < 1)
		return (E2BIG);

	/*
	 * Ensure that z_gy2gx() at its largest possible calculated value
	 * (alpha = 0) will not cause overflow further late during z_gx2gy()
	 * stage.
	 */
	if (z_gy2gx(info, gy2gx_max) > _Z_GCAST(gx2gy_max))
		return (E2BIG);

	info->z_maxfeed = gy2gx_max * align;

#ifdef Z_USE_ALPHADRIFT
	info->z_startdrift = z_gy2gx(info, 1);
	info->z_alphadrift = z_drift(info, info->z_startdrift, 1);
#endif

	i = z_gy2gx(info, 1);
	info->z_full = z_roundpow2((info->z_size << 1) + i);

	/*
	 * Too big to be true, and overflowing left and right like mad ..
	 */
	if ((info->z_full * align) < 1) {
		if (adaptive == 0 && Z_IS_SINC(info)) {
			adaptive = 1;
			goto z_setup_adaptive_sinc;
		}
		return (E2BIG);
	}

	/*
	 * Increase full buffer size if its too small to reduce cyclic
	 * buffer shifting in main conversion/feeder loop.
	 */
	while (info->z_full < Z_RESERVOIR_MAX &&
	    (info->z_full - (info->z_size << 1)) < Z_RESERVOIR)
		info->z_full <<= 1;

	/* Initialize buffer position. */
	info->z_mask = info->z_full - 1;
	info->z_start = z_prev(info, info->z_size << 1, 1);
	info->z_pos = z_next(info, info->z_start, 1);

	/*
	 * Allocate or reuse delay line buffer, whichever makes sense.
	 */
	i = info->z_full * align;
	if (i < 1)
		return (E2BIG);

	if (info->z_delay == NULL || info->z_alloc < i ||
	    i <= (info->z_alloc >> 1)) {
		if (info->z_delay != NULL)
			free(info->z_delay, M_DEVBUF);
		info->z_delay = malloc(i, M_DEVBUF, M_NOWAIT | M_ZERO);
		if (info->z_delay == NULL)
			return (ENOMEM);
		info->z_alloc = i;
	}

	/*
	 * Zero out head of buffer to avoid pops and clicks.
	 */
	memset(info->z_delay, sndbuf_zerodata(f->desc->out),
	    info->z_pos * align);

#ifdef Z_DIAGNOSTIC
	/*
	 * XXX Debuging mess !@#$%^
	 */
#define dumpz(x)	fprintf(stderr, "\t%12s = %10u : %-11d\n",	\
			    "z_"__STRING(x), (uint32_t)info->z_##x,	\
			    (int32_t)info->z_##x)
	fprintf(stderr, "\n%s():\n", __func__);
	fprintf(stderr, "\tchannels=%d, bps=%d, format=0x%08x, quality=%d\n",
	    info->channels, info->bps, format, info->quality);
	fprintf(stderr, "\t%d (%d) -> %d (%d), ",
	    info->src, info->rsrc, info->dst, info->rdst);
	fprintf(stderr, "[%d/%d]\n", info->z_gx, info->z_gy);
	fprintf(stderr, "\tminreq=%d, ", z_gy2gx(info, 1));
	if (adaptive != 0)
		z_scale = Z_ONE;
	fprintf(stderr, "factor=0x%08x/0x%08x (%f)\n",
	    z_scale, Z_ONE, (double)z_scale / Z_ONE);
	fprintf(stderr, "\tbase_length=%d, ", Z_SINC_BASE_LEN(info));
	fprintf(stderr, "adaptive=%s\n", (adaptive != 0) ? "YES" : "NO");
	dumpz(size);
	dumpz(alloc);
	if (info->z_alloc < 1024)
		fprintf(stderr, "\t%15s%10d Bytes\n",
		    "", info->z_alloc);
	else if (info->z_alloc < (1024 << 10))
		fprintf(stderr, "\t%15s%10d KBytes\n",
		    "", info->z_alloc >> 10);
	else if (info->z_alloc < (1024 << 20))
		fprintf(stderr, "\t%15s%10d MBytes\n",
		    "", info->z_alloc >> 20);
	else
		fprintf(stderr, "\t%15s%10d GBytes\n",
		    "", info->z_alloc >> 30);
	fprintf(stderr, "\t%12s   %10d (min output samples)\n",
	    "",
	    (int32_t)z_gx2gy(info, info->z_full - (info->z_size << 1)));
	fprintf(stderr, "\t%12s   %10d (min allocated output samples)\n",
	    "",
	    (int32_t)z_gx2gy(info, (info->z_alloc / align) -
	    (info->z_size << 1)));
	fprintf(stderr, "\t%12s = %10d\n",
	    "z_gy2gx()", (int32_t)z_gy2gx(info, 1));
	fprintf(stderr, "\t%12s = %10d -> z_gy2gx() -> %d\n",
	    "Max", (int32_t)gy2gx_max, (int32_t)z_gy2gx(info, gy2gx_max));
	fprintf(stderr, "\t%12s = %10d\n",
	    "z_gx2gy()", (int32_t)z_gx2gy(info, 1));
	fprintf(stderr, "\t%12s = %10d -> z_gx2gy() -> %d\n",
	    "Max", (int32_t)gx2gy_max, (int32_t)z_gx2gy(info, gx2gy_max));
	dumpz(maxfeed);
	dumpz(full);
	dumpz(start);
	dumpz(pos);
	dumpz(scale);
	fprintf(stderr, "\t%12s   %10f\n", "",
	    (double)info->z_scale / Z_ONE);
	dumpz(dx);
	fprintf(stderr, "\t%12s   %10f\n", "",
	    (double)info->z_dx / info->z_dy);
	dumpz(dy);
	fprintf(stderr, "\t%12s   %10d (drift step)\n", "",
	    info->z_dy >> Z_SHIFT);
	fprintf(stderr, "\t%12s   %10d (scaling differences)\n", "",
	    (z_scale << Z_DRIFT_SHIFT) - info->z_dy);
	fprintf(stderr, "\t%12s = %u bytes\n",
	    "intpcm32_t", sizeof(intpcm32_t));
	fprintf(stderr, "\t%12s = 0x%08x, smallest=%.16lf\n",
	    "Z_ONE", Z_ONE, (double)1.0 / (double)Z_ONE);
#endif

	return (0);
}

static int
z_resampler_set(struct pcm_feeder *f, int what, int32_t value)
{
	struct z_info *info;
	int32_t oquality;

	info = f->data;

	switch (what) {
	case Z_RATE_SRC:
		if (value < feeder_rate_min || value > feeder_rate_max)
			return (E2BIG);
		if (value == info->rsrc)
			return (0);
		info->rsrc = value;
		break;
	case Z_RATE_DST:
		if (value < feeder_rate_min || value > feeder_rate_max)
			return (E2BIG);
		if (value == info->rdst)
			return (0);
		info->rdst = value;
		break;
	case Z_RATE_QUALITY:
		if (value < Z_QUALITY_MIN || value > Z_QUALITY_MAX)
			return (EINVAL);
		if (value == info->quality)
			return (0);
		/*
		 * If we failed to set the requested quality, restore
		 * the old one. We cannot afford leaving it broken since
		 * passive feeder chains like vchans never reinitialize
		 * itself.
		 */
		oquality = info->quality;
		info->quality = value;
		if (z_resampler_setup(f) == 0)
			return (0);
		info->quality = oquality;
		break;
	case Z_RATE_CHANNELS:
		if (value < SND_CHN_MIN || value > SND_CHN_MAX)
			return (EINVAL);
		if (value == info->channels)
			return (0);
		info->channels = value;
		break;
	default:
		return (EINVAL);
		break;
	}

	return (z_resampler_setup(f));
}

static int
z_resampler_get(struct pcm_feeder *f, int what)
{
	struct z_info *info;

	info = f->data;

	switch (what) {
	case Z_RATE_SRC:
		return (info->rsrc);
		break;
	case Z_RATE_DST:
		return (info->rdst);
		break;
	case Z_RATE_QUALITY:
		return (info->quality);
		break;
	case Z_RATE_CHANNELS:
		return (info->channels);
		break;
	default:
		break;
	}

	return (-1);
}

static int
z_resampler_init(struct pcm_feeder *f)
{
	struct z_info *info;
	int ret;

	if (f->desc->in != f->desc->out)
		return (EINVAL);

	info = malloc(sizeof(*info), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (info == NULL)
		return (ENOMEM);

	info->rsrc = Z_RATE_DEFAULT;
	info->rdst = Z_RATE_DEFAULT;
	info->quality = feeder_rate_quality;
	info->channels = AFMT_CHANNEL(f->desc->in);

	f->data = info;

	ret = z_resampler_setup(f);
	if (ret != 0) {
		if (info->z_pcoeff != NULL)
			free(info->z_pcoeff, M_DEVBUF);
		if (info->z_delay != NULL)
			free(info->z_delay, M_DEVBUF);
		free(info, M_DEVBUF);
		f->data = NULL;
	}

	return (ret);
}

static int
z_resampler_free(struct pcm_feeder *f)
{
	struct z_info *info;

	info = f->data;
	if (info != NULL) {
		if (info->z_pcoeff != NULL)
			free(info->z_pcoeff, M_DEVBUF);
		if (info->z_delay != NULL)
			free(info->z_delay, M_DEVBUF);
		free(info, M_DEVBUF);
	}

	f->data = NULL;

	return (0);
}

static uint32_t
z_resampler_feed_internal(struct pcm_feeder *f, struct pcm_channel *c,
    uint8_t *b, uint32_t count, void *source)
{
	struct z_info *info;
	int32_t alphadrift, startdrift, reqout, ocount, reqin, align;
	int32_t fetch, fetched, start, cp;
	uint8_t *dst;

	info = f->data;
	if (info->z_resample == NULL)
		return (z_feed(f->source, c, b, count, source));

	/*
	 * Calculate sample size alignment and amount of sample output.
	 * We will do everything in sample domain, but at the end we
	 * will jump back to byte domain.
	 */
	align = info->channels * info->bps;
	ocount = SND_FXDIV(count, align);
	if (ocount == 0)
		return (0);

	/*
	 * Calculate amount of input samples that is needed to generate
	 * exact amount of output.
	 */
	reqin = z_gy2gx(info, ocount) - z_fetched(info);

#ifdef Z_USE_ALPHADRIFT
	startdrift = info->z_startdrift;
	alphadrift = info->z_alphadrift;
#else
	startdrift = _Z_GY2GX(info, 0, 1);
	alphadrift = z_drift(info, startdrift, 1);
#endif

	dst = b;

	do {
		if (reqin != 0) {
			fetch = z_min(z_free(info), reqin);
			if (fetch == 0) {
				/*
				 * No more free spaces, so wind enough
				 * samples back to the head of delay line
				 * in byte domain.
				 */
				fetched = z_fetched(info);
				start = z_prev(info, info->z_start,
				    (info->z_size << 1) - 1);
				cp = (info->z_size << 1) + fetched;
				z_copy(info->z_delay + (start * align),
				    info->z_delay, cp * align);
				info->z_start =
				    z_prev(info, info->z_size << 1, 1);
				info->z_pos =
				    z_next(info, info->z_start, fetched + 1);
				fetch = z_min(z_free(info), reqin);
#ifdef Z_DIAGNOSTIC
				if (1) {
					static uint32_t kk = 0;
					fprintf(stderr,
					    "Buffer Move: "
					    "start=%d fetched=%d cp=%d "
					    "cycle=%u [%u]\r",
					    start, fetched, cp, info->z_cycle,
					    ++kk);
				}
				info->z_cycle = 0;
#endif
			}
			if (fetch != 0) {
				/*
				 * Fetch in byte domain and jump back
				 * to sample domain.
				 */
				fetched = SND_FXDIV(z_feed(f->source, c,
				    info->z_delay + (info->z_pos * align),
				    fetch * align, source), align);
				/*
				 * Prepare to convert fetched buffer,
				 * or mark us done if we cannot fulfill
				 * the request.
				 */
				reqin -= fetched;
				info->z_pos += fetched;
				if (fetched != fetch)
					reqin = 0;
			}
		}

		reqout = z_min(z_gx2gy(info, z_fetched(info)), ocount);
		if (reqout != 0) {
			ocount -= reqout;

			/*
			 * Drift.. drift.. drift..
			 *
			 * Notice that there are 2 methods of doing the drift
			 * operations: The former is much cleaner (in a sense
			 * of mathematical readings of my eyes), but slower
			 * due to integer division in z_gy2gx(). Nevertheless,
			 * both should give the same exact accurate drifting
			 * results, so the later is favourable.
			 */
			do {
				info->z_resample(info, dst);
#if 0
				startdrift = z_gy2gx(info, 1);
				alphadrift = z_drift(info, startdrift, 1);
				info->z_start += startdrift;
				info->z_alpha += alphadrift;
#else
				info->z_alpha += alphadrift;
				if (info->z_alpha < info->z_gy)
					info->z_start += startdrift;
				else {
					info->z_start += startdrift - 1;
					info->z_alpha -= info->z_gy;
				}
#endif
				dst += align;
#ifdef Z_DIAGNOSTIC
				info->z_cycle++;
#endif
			} while (--reqout != 0);
		}
	} while (reqin != 0 && ocount != 0);

	/*
	 * Back to byte domain..
	 */
	return (dst - b);
}

static int
z_resampler_feed(struct pcm_feeder *f, struct pcm_channel *c, uint8_t *b,
    uint32_t count, void *source)
{
	uint32_t feed, maxfeed, left;

	/*
	 * Split count to smaller chunks to avoid possible 32bit overflow.
	 */
	maxfeed = ((struct z_info *)(f->data))->z_maxfeed;
	left = count;

	do {
		feed = z_resampler_feed_internal(f, c, b,
		    z_min(maxfeed, left), source);
		b += feed;
		left -= feed;
	} while (left != 0 && feed != 0);

	return (count - left);
}

static struct pcm_feederdesc feeder_rate_desc[] = {
	{ FEEDER_RATE, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0 },
};

static kobj_method_t feeder_rate_methods[] = {
	KOBJMETHOD(feeder_init,		z_resampler_init),
	KOBJMETHOD(feeder_free,		z_resampler_free),
	KOBJMETHOD(feeder_set,		z_resampler_set),
	KOBJMETHOD(feeder_get,		z_resampler_get),
	KOBJMETHOD(feeder_feed,		z_resampler_feed),
	KOBJMETHOD_END
};

FEEDER_DECLARE(feeder_rate, NULL);
