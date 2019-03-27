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

/* feeder_volume, a long 'Lost Technology' rather than a new feature. */

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

typedef void (*feed_volume_t)(int *, int *, uint32_t, uint8_t *, uint32_t);

#define FEEDVOLUME_CALC8(s, v)	(SND_VOL_CALC_SAMPLE((intpcm_t)		\
				 (s) << 8, v) >> 8)
#define FEEDVOLUME_CALC16(s, v)	SND_VOL_CALC_SAMPLE((intpcm_t)(s), v)
#define FEEDVOLUME_CALC24(s, v)	SND_VOL_CALC_SAMPLE((intpcm64_t)(s), v)
#define FEEDVOLUME_CALC32(s, v)	SND_VOL_CALC_SAMPLE((intpcm64_t)(s), v)

#define FEEDVOLUME_DECLARE(SIGN, BIT, ENDIAN)				\
static void								\
feed_volume_##SIGN##BIT##ENDIAN(int *vol, int *matrix,			\
    uint32_t channels, uint8_t *dst, uint32_t count)			\
{									\
	intpcm##BIT##_t v;						\
	intpcm_t x;							\
	uint32_t i;							\
									\
	dst += count * PCM_##BIT##_BPS * channels;			\
	do {								\
		i = channels;						\
		do {							\
			dst -= PCM_##BIT##_BPS;				\
			i--;						\
			x = PCM_READ_##SIGN##BIT##_##ENDIAN(dst);	\
			v = FEEDVOLUME_CALC##BIT(x, vol[matrix[i]]);	\
			x = PCM_CLAMP_##SIGN##BIT(v);			\
			_PCM_WRITE_##SIGN##BIT##_##ENDIAN(dst, x);	\
		} while (i != 0);					\
	} while (--count != 0);						\
}

#if BYTE_ORDER == LITTLE_ENDIAN || defined(SND_FEEDER_MULTIFORMAT)
FEEDVOLUME_DECLARE(S, 16, LE)
FEEDVOLUME_DECLARE(S, 32, LE)
#endif
#if BYTE_ORDER == BIG_ENDIAN || defined(SND_FEEDER_MULTIFORMAT)
FEEDVOLUME_DECLARE(S, 16, BE)
FEEDVOLUME_DECLARE(S, 32, BE)
#endif
#ifdef SND_FEEDER_MULTIFORMAT
FEEDVOLUME_DECLARE(S,  8, NE)
FEEDVOLUME_DECLARE(S, 24, LE)
FEEDVOLUME_DECLARE(S, 24, BE)
FEEDVOLUME_DECLARE(U,  8, NE)
FEEDVOLUME_DECLARE(U, 16, LE)
FEEDVOLUME_DECLARE(U, 24, LE)
FEEDVOLUME_DECLARE(U, 32, LE)
FEEDVOLUME_DECLARE(U, 16, BE)
FEEDVOLUME_DECLARE(U, 24, BE)
FEEDVOLUME_DECLARE(U, 32, BE)
#endif

struct feed_volume_info {
	uint32_t bps, channels;
	feed_volume_t apply;
	int volume_class;
	int state;
	int matrix[SND_CHN_MAX];
};

#define FEEDVOLUME_ENTRY(SIGN, BIT, ENDIAN)				\
	{								\
		AFMT_##SIGN##BIT##_##ENDIAN,				\
		feed_volume_##SIGN##BIT##ENDIAN				\
	}

static const struct {
	uint32_t format;
	feed_volume_t apply;
} feed_volume_info_tab[] = {
#if BYTE_ORDER == LITTLE_ENDIAN || defined(SND_FEEDER_MULTIFORMAT)
	FEEDVOLUME_ENTRY(S, 16, LE),
	FEEDVOLUME_ENTRY(S, 32, LE),
#endif
#if BYTE_ORDER == BIG_ENDIAN || defined(SND_FEEDER_MULTIFORMAT)
	FEEDVOLUME_ENTRY(S, 16, BE),
	FEEDVOLUME_ENTRY(S, 32, BE),
#endif
#ifdef SND_FEEDER_MULTIFORMAT
	FEEDVOLUME_ENTRY(S,  8, NE),
	FEEDVOLUME_ENTRY(S, 24, LE),
	FEEDVOLUME_ENTRY(S, 24, BE),
	FEEDVOLUME_ENTRY(U,  8, NE),
	FEEDVOLUME_ENTRY(U, 16, LE),
	FEEDVOLUME_ENTRY(U, 24, LE),
	FEEDVOLUME_ENTRY(U, 32, LE),
	FEEDVOLUME_ENTRY(U, 16, BE),
	FEEDVOLUME_ENTRY(U, 24, BE),
	FEEDVOLUME_ENTRY(U, 32, BE)
#endif
};

#define FEEDVOLUME_TAB_SIZE	((int32_t)				\
				 (sizeof(feed_volume_info_tab) /	\
				  sizeof(feed_volume_info_tab[0])))

static int
feed_volume_init(struct pcm_feeder *f)
{
	struct feed_volume_info *info;
	struct pcmchan_matrix *m;
	uint32_t i;
	int ret;

	if (f->desc->in != f->desc->out ||
	    AFMT_CHANNEL(f->desc->in) > SND_CHN_MAX)
		return (EINVAL);

	for (i = 0; i < FEEDVOLUME_TAB_SIZE; i++) {
		if (AFMT_ENCODING(f->desc->in) ==
		    feed_volume_info_tab[i].format) {
			info = malloc(sizeof(*info), M_DEVBUF,
			    M_NOWAIT | M_ZERO);
			if (info == NULL)
				return (ENOMEM);

			info->bps = AFMT_BPS(f->desc->in);
			info->channels = AFMT_CHANNEL(f->desc->in);
			info->apply = feed_volume_info_tab[i].apply;
			info->volume_class = SND_VOL_C_PCM;
			info->state = FEEDVOLUME_ENABLE;

			f->data = info;
			m = feeder_matrix_default_channel_map(info->channels);
			if (m == NULL) {
				free(info, M_DEVBUF);
				return (EINVAL);
			}

			ret = feeder_volume_apply_matrix(f, m);
			if (ret != 0)
				free(info, M_DEVBUF);

			return (ret);
		}
	}

	return (EINVAL);
}

static int
feed_volume_free(struct pcm_feeder *f)
{
	struct feed_volume_info *info;

	info = f->data;
	if (info != NULL)
		free(info, M_DEVBUF);

	f->data = NULL;

	return (0);
}

static int
feed_volume_set(struct pcm_feeder *f, int what, int value)
{
	struct feed_volume_info *info;
	struct pcmchan_matrix *m;
	int ret;

	info = f->data;
	ret = 0;

	switch (what) {
	case FEEDVOLUME_CLASS:
		if (value < SND_VOL_C_BEGIN || value > SND_VOL_C_END)
			return (EINVAL);
		info->volume_class = value;
		break;
	case FEEDVOLUME_CHANNELS:
		if (value < SND_CHN_MIN || value > SND_CHN_MAX)
			return (EINVAL);
		m = feeder_matrix_default_channel_map(value);
		if (m == NULL)
			return (EINVAL);
		ret = feeder_volume_apply_matrix(f, m);
		break;
	case FEEDVOLUME_STATE:
		if (!(value == FEEDVOLUME_ENABLE || value == FEEDVOLUME_BYPASS))
			return (EINVAL);
		info->state = value;
		break;
	default:
		return (EINVAL);
		break;
	}

	return (ret);
}

static int
feed_volume_feed(struct pcm_feeder *f, struct pcm_channel *c, uint8_t *b,
    uint32_t count, void *source)
{
	struct feed_volume_info *info;
	uint32_t j, align;
	int i, *vol, *matrix;
	uint8_t *dst;

	/*
	 * Fetch filter data operation.
	 */
	info = f->data;

	if (info->state == FEEDVOLUME_BYPASS)
		return (FEEDER_FEED(f->source, c, b, count, source));

	vol = c->volume[SND_VOL_C_VAL(info->volume_class)];
	matrix = info->matrix;

	/*
	 * First, let see if we really need to apply gain at all.
	 */
	j = 0;
	i = info->channels;
	do {
		if (vol[matrix[--i]] != SND_VOL_FLAT) {
			j = 1;
			break;
		}
	} while (i != 0);

	/* Nope, just bypass entirely. */
	if (j == 0)
		return (FEEDER_FEED(f->source, c, b, count, source));

	dst = b;
	align = info->bps * info->channels;

	do {
		if (count < align)
			break;

		j = SND_FXDIV(FEEDER_FEED(f->source, c, dst, count, source),
		    align);
		if (j == 0)
			break;

		info->apply(vol, matrix, info->channels, dst, j);

		j *= align;
		dst += j;
		count -= j;

	} while (count != 0);

	return (dst - b);
}

static struct pcm_feederdesc feeder_volume_desc[] = {
	{ FEEDER_VOLUME, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0 }
};

static kobj_method_t feeder_volume_methods[] = {
	KOBJMETHOD(feeder_init,		feed_volume_init),
	KOBJMETHOD(feeder_free,		feed_volume_free),
	KOBJMETHOD(feeder_set,		feed_volume_set),
	KOBJMETHOD(feeder_feed,		feed_volume_feed),
	KOBJMETHOD_END
};

FEEDER_DECLARE(feeder_volume, NULL);

/* Extern */

/*
 * feeder_volume_apply_matrix(): For given matrix map, apply its configuration
 *                               to feeder_volume matrix structure. There are
 *                               possibilites that feeder_volume be inserted
 *                               before or after feeder_matrix, which in this
 *                               case feeder_volume must be in a good terms
 *                               with _current_ matrix.
 */
int
feeder_volume_apply_matrix(struct pcm_feeder *f, struct pcmchan_matrix *m)
{
	struct feed_volume_info *info;
	uint32_t i;

	if (f == NULL || f->desc == NULL || f->desc->type != FEEDER_VOLUME ||
	    f->data == NULL || m == NULL || m->channels < SND_CHN_MIN ||
	    m->channels > SND_CHN_MAX)
		return (EINVAL);

	info = f->data;

	for (i = 0; i < (sizeof(info->matrix) / sizeof(info->matrix[0])); i++) {
		if (i < m->channels)
			info->matrix[i] = m->map[i].type;
		else
			info->matrix[i] = SND_CHN_T_FL;
	}

	info->channels = m->channels;

	return (0);
}
