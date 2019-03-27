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
 * feeder_matrix: Generic any-to-any channel matrixing. Probably not the
 *                accurate way of doing things, but it should be fast and
 *                transparent enough, not to mention capable of handling
 *                possible non-standard way of multichannel interleaving
 *                order. In other words, it is tough to break.
 *
 * The Good:
 * + very generic and compact, provided that the supplied matrix map is in a
 *   sane form.
 * + should be fast enough.
 *
 * The Bad:
 * + somebody might disagree with it.
 * + 'matrix' is kind of 0x7a69, due to prolong mental block.
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

#define FEEDMATRIX_RESERVOIR	(SND_CHN_MAX * PCM_32_BPS)

#define SND_CHN_T_EOF		0x00e0fe0f
#define SND_CHN_T_NULL		0x0e0e0e0e

struct feed_matrix_info;

typedef void (*feed_matrix_t)(struct feed_matrix_info *, uint8_t *,
    uint8_t *, uint32_t);

struct feed_matrix_info {
	uint32_t bps;
	uint32_t ialign, oalign;
	uint32_t in, out;
	feed_matrix_t apply;
#ifdef FEEDMATRIX_GENERIC
	intpcm_read_t *rd;
	intpcm_write_t *wr;
#endif
	struct {
		int chn[SND_CHN_T_MAX + 1];
		int mul, shift;
	} matrix[SND_CHN_T_MAX + 1];
	uint8_t reservoir[FEEDMATRIX_RESERVOIR];
};

static struct pcmchan_matrix feeder_matrix_maps[SND_CHN_MATRIX_MAX] = {
	[SND_CHN_MATRIX_1_0] = SND_CHN_MATRIX_MAP_1_0,
	[SND_CHN_MATRIX_2_0] = SND_CHN_MATRIX_MAP_2_0,
	[SND_CHN_MATRIX_2_1] = SND_CHN_MATRIX_MAP_2_1,
	[SND_CHN_MATRIX_3_0] = SND_CHN_MATRIX_MAP_3_0,
	[SND_CHN_MATRIX_3_1] = SND_CHN_MATRIX_MAP_3_1,
	[SND_CHN_MATRIX_4_0] = SND_CHN_MATRIX_MAP_4_0,
	[SND_CHN_MATRIX_4_1] = SND_CHN_MATRIX_MAP_4_1,
	[SND_CHN_MATRIX_5_0] = SND_CHN_MATRIX_MAP_5_0,
	[SND_CHN_MATRIX_5_1] = SND_CHN_MATRIX_MAP_5_1,
	[SND_CHN_MATRIX_6_0] = SND_CHN_MATRIX_MAP_6_0,
	[SND_CHN_MATRIX_6_1] = SND_CHN_MATRIX_MAP_6_1,
	[SND_CHN_MATRIX_7_0] = SND_CHN_MATRIX_MAP_7_0,
	[SND_CHN_MATRIX_7_1] = SND_CHN_MATRIX_MAP_7_1
};

static int feeder_matrix_default_ids[9] = {
	[0] = SND_CHN_MATRIX_UNKNOWN,
	[1] = SND_CHN_MATRIX_1,
	[2] = SND_CHN_MATRIX_2,
	[3] = SND_CHN_MATRIX_3,
	[4] = SND_CHN_MATRIX_4,
	[5] = SND_CHN_MATRIX_5,
	[6] = SND_CHN_MATRIX_6,
	[7] = SND_CHN_MATRIX_7,
	[8] = SND_CHN_MATRIX_8
};

#ifdef _KERNEL
#define FEEDMATRIX_CLIP_CHECK(...)
#else
#define FEEDMATRIX_CLIP_CHECK(v, BIT)	do {				\
	if ((v) < PCM_S##BIT##_MIN || (v) > PCM_S##BIT##_MAX)		\
	    errx(1, "\n\n%s(): Sample clipping: %jd\n",			\
		__func__, (intmax_t)(v));				\
} while (0)
#endif

#define FEEDMATRIX_DECLARE(SIGN, BIT, ENDIAN)				\
static void								\
feed_matrix_##SIGN##BIT##ENDIAN(struct feed_matrix_info *info,		\
    uint8_t *src, uint8_t *dst, uint32_t count)				\
{									\
	intpcm64_t accum;						\
	intpcm_t v;							\
	int i, j;							\
									\
	do {								\
		for (i = 0; info->matrix[i].chn[0] != SND_CHN_T_EOF;	\
		    i++) {						\
			if (info->matrix[i].chn[0] == SND_CHN_T_NULL) {	\
				_PCM_WRITE_##SIGN##BIT##_##ENDIAN(dst,	\
				    0);					\
				dst += PCM_##BIT##_BPS;			\
				continue;				\
			} else if (info->matrix[i].chn[1] ==		\
			    SND_CHN_T_EOF) {				\
				v = _PCM_READ_##SIGN##BIT##_##ENDIAN(	\
				    src + info->matrix[i].chn[0]);	\
				_PCM_WRITE_##SIGN##BIT##_##ENDIAN(dst,	\
				    v);					\
				dst += PCM_##BIT##_BPS;			\
				continue;				\
			}						\
									\
			accum = 0;					\
			for (j = 0;					\
			    info->matrix[i].chn[j] != SND_CHN_T_EOF;	\
			    j++) {					\
				v = _PCM_READ_##SIGN##BIT##_##ENDIAN(	\
				    src + info->matrix[i].chn[j]);	\
				accum += v;				\
			}						\
									\
			accum = (accum * info->matrix[i].mul) >>	\
			    info->matrix[i].shift;			\
									\
			FEEDMATRIX_CLIP_CHECK(accum, BIT);		\
									\
			v = (accum > PCM_S##BIT##_MAX) ?		\
			    PCM_S##BIT##_MAX :				\
			    ((accum < PCM_S##BIT##_MIN) ?		\
			    PCM_S##BIT##_MIN :				\
			    accum);					\
			_PCM_WRITE_##SIGN##BIT##_##ENDIAN(dst, v);	\
			dst += PCM_##BIT##_BPS;				\
		}							\
		src += info->ialign;					\
	} while (--count != 0);						\
}

#if BYTE_ORDER == LITTLE_ENDIAN || defined(SND_FEEDER_MULTIFORMAT)
FEEDMATRIX_DECLARE(S, 16, LE)
FEEDMATRIX_DECLARE(S, 32, LE)
#endif
#if BYTE_ORDER == BIG_ENDIAN || defined(SND_FEEDER_MULTIFORMAT)
FEEDMATRIX_DECLARE(S, 16, BE)
FEEDMATRIX_DECLARE(S, 32, BE)
#endif
#ifdef SND_FEEDER_MULTIFORMAT
FEEDMATRIX_DECLARE(S,  8, NE)
FEEDMATRIX_DECLARE(S, 24, LE)
FEEDMATRIX_DECLARE(S, 24, BE)
FEEDMATRIX_DECLARE(U,  8, NE)
FEEDMATRIX_DECLARE(U, 16, LE)
FEEDMATRIX_DECLARE(U, 24, LE)
FEEDMATRIX_DECLARE(U, 32, LE)
FEEDMATRIX_DECLARE(U, 16, BE)
FEEDMATRIX_DECLARE(U, 24, BE)
FEEDMATRIX_DECLARE(U, 32, BE)
#endif

#define FEEDMATRIX_ENTRY(SIGN, BIT, ENDIAN)				\
	{								\
		AFMT_##SIGN##BIT##_##ENDIAN,				\
		feed_matrix_##SIGN##BIT##ENDIAN				\
	}

static const struct {
	uint32_t format;
	feed_matrix_t apply;
} feed_matrix_tab[] = {
#if BYTE_ORDER == LITTLE_ENDIAN || defined(SND_FEEDER_MULTIFORMAT)
	FEEDMATRIX_ENTRY(S, 16, LE),
	FEEDMATRIX_ENTRY(S, 32, LE),
#endif
#if BYTE_ORDER == BIG_ENDIAN || defined(SND_FEEDER_MULTIFORMAT)
	FEEDMATRIX_ENTRY(S, 16, BE),
	FEEDMATRIX_ENTRY(S, 32, BE),
#endif
#ifdef SND_FEEDER_MULTIFORMAT
	FEEDMATRIX_ENTRY(S,  8, NE),
	FEEDMATRIX_ENTRY(S, 24, LE),
	FEEDMATRIX_ENTRY(S, 24, BE),
	FEEDMATRIX_ENTRY(U,  8, NE),
	FEEDMATRIX_ENTRY(U, 16, LE),
	FEEDMATRIX_ENTRY(U, 24, LE),
	FEEDMATRIX_ENTRY(U, 32, LE),
	FEEDMATRIX_ENTRY(U, 16, BE),
	FEEDMATRIX_ENTRY(U, 24, BE),
	FEEDMATRIX_ENTRY(U, 32, BE)
#endif
};

static void
feed_matrix_reset(struct feed_matrix_info *info)
{
	uint32_t i, j;

	for (i = 0; i < (sizeof(info->matrix) / sizeof(info->matrix[0])); i++) {
		for (j = 0;
		    j < (sizeof(info->matrix[i].chn) /
		    sizeof(info->matrix[i].chn[0])); j++) {
			info->matrix[i].chn[j] = SND_CHN_T_EOF;
		}
		info->matrix[i].mul   = 1;
		info->matrix[i].shift = 0;
	}
}

#ifdef FEEDMATRIX_GENERIC
static void
feed_matrix_apply_generic(struct feed_matrix_info *info,
    uint8_t *src, uint8_t *dst, uint32_t count)
{
	intpcm64_t accum;
	intpcm_t v;
	int i, j;

	do {
		for (i = 0; info->matrix[i].chn[0] != SND_CHN_T_EOF;
		    i++) {
			if (info->matrix[i].chn[0] == SND_CHN_T_NULL) {
				info->wr(dst, 0);
				dst += info->bps;
				continue;
			} else if (info->matrix[i].chn[1] ==
			    SND_CHN_T_EOF) {
				v = info->rd(src + info->matrix[i].chn[0]);
				info->wr(dst, v);
				dst += info->bps;
				continue;
			}

			accum = 0;
			for (j = 0;
			    info->matrix[i].chn[j] != SND_CHN_T_EOF;
			    j++) {
				v = info->rd(src + info->matrix[i].chn[j]);
				accum += v;
			}

			accum = (accum * info->matrix[i].mul) >>
			    info->matrix[i].shift;

			FEEDMATRIX_CLIP_CHECK(accum, 32);

			v = (accum > PCM_S32_MAX) ? PCM_S32_MAX :
			    ((accum < PCM_S32_MIN) ? PCM_S32_MIN : accum);
			info->wr(dst, v);
			dst += info->bps;
		}
		src += info->ialign;
	} while (--count != 0);
}
#endif

static int
feed_matrix_setup(struct feed_matrix_info *info, struct pcmchan_matrix *m_in,
    struct pcmchan_matrix *m_out)
{
	uint32_t i, j, ch, in_mask, merge_mask;
	int mul, shift;


	if (info == NULL || m_in == NULL || m_out == NULL ||
	    AFMT_CHANNEL(info->in) != m_in->channels ||
	    AFMT_CHANNEL(info->out) != m_out->channels ||
	    m_in->channels < SND_CHN_MIN || m_in->channels > SND_CHN_MAX ||
	    m_out->channels < SND_CHN_MIN || m_out->channels > SND_CHN_MAX)
		return (EINVAL);

	feed_matrix_reset(info);

	/*
	 * If both in and out are part of standard matrix and identical, skip
	 * everything alltogether.
	 */
	if (m_in->id == m_out->id && !(m_in->id < SND_CHN_MATRIX_BEGIN ||
	    m_in->id > SND_CHN_MATRIX_END))
		return (0);

	/*
	 * Special case for mono input matrix. If the output supports
	 * possible 'center' channel, route it there. Otherwise, let it be
	 * matrixed to left/right.
	 */
	if (m_in->id == SND_CHN_MATRIX_1_0) {
		if (m_out->id == SND_CHN_MATRIX_1_0)
			in_mask = SND_CHN_T_MASK_FL;
		else if (m_out->mask & SND_CHN_T_MASK_FC)
			in_mask = SND_CHN_T_MASK_FC;
		else
			in_mask = SND_CHN_T_MASK_FL | SND_CHN_T_MASK_FR;
	} else
		in_mask = m_in->mask;

	/* Merge, reduce, expand all possibilites. */
	for (ch = SND_CHN_T_BEGIN; ch <= SND_CHN_T_END &&
	    m_out->map[ch].type != SND_CHN_T_MAX; ch += SND_CHN_T_STEP) {
		merge_mask = m_out->map[ch].members & in_mask;
		if (merge_mask == 0) {
			info->matrix[ch].chn[0] = SND_CHN_T_NULL;
			continue;
		}

		j = 0;
		for (i = SND_CHN_T_BEGIN; i <= SND_CHN_T_END;
		    i += SND_CHN_T_STEP) {
			if (merge_mask & (1 << i)) {
				if (m_in->offset[i] >= 0 &&
				    m_in->offset[i] < (int)m_in->channels)
					info->matrix[ch].chn[j++] =
					    m_in->offset[i] * info->bps;
				else {
					info->matrix[ch].chn[j++] =
					    SND_CHN_T_EOF;
					break;
				}
			}
		}

#define FEEDMATRIX_ATTN_SHIFT	16

		if (j > 1) {
			/*
			 * XXX For channel that require accumulation from
			 * multiple channels, apply a slight attenuation to
			 * avoid clipping.
			 */
			mul   = (1 << (FEEDMATRIX_ATTN_SHIFT - 1)) + 143 - j;
			shift = FEEDMATRIX_ATTN_SHIFT;
			while ((mul & 1) == 0 && shift > 0) {
				mul >>= 1;
				shift--;
			}
			info->matrix[ch].mul   = mul;
			info->matrix[ch].shift = shift;
		}
	}

#ifndef _KERNEL
	fprintf(stderr, "Total: %d\n", ch);

	for (i = 0; info->matrix[i].chn[0] != SND_CHN_T_EOF; i++) {
		fprintf(stderr, "%d: [", i);
		for (j = 0; info->matrix[i].chn[j] != SND_CHN_T_EOF; j++) {
			if (j != 0)
				fprintf(stderr, ", ");
			fprintf(stderr, "%d",
			    (info->matrix[i].chn[j] == SND_CHN_T_NULL) ?
			    0xffffffff : info->matrix[i].chn[j] / info->bps);
		}
		fprintf(stderr, "] attn: (x * %d) >> %d\n",
		    info->matrix[i].mul, info->matrix[i].shift);
	}
#endif

	return (0);
}

static int
feed_matrix_init(struct pcm_feeder *f)
{
	struct feed_matrix_info *info;
	struct pcmchan_matrix *m_in, *m_out;
	uint32_t i;
	int ret;

	if (AFMT_ENCODING(f->desc->in) != AFMT_ENCODING(f->desc->out))
		return (EINVAL);

	info = malloc(sizeof(*info), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (info == NULL)
		return (ENOMEM);

	info->in = f->desc->in;
	info->out = f->desc->out;
	info->bps = AFMT_BPS(info->in);
	info->ialign = AFMT_ALIGN(info->in);
	info->oalign = AFMT_ALIGN(info->out);
	info->apply = NULL;

	for (i = 0; info->apply == NULL &&
	    i < (sizeof(feed_matrix_tab) / sizeof(feed_matrix_tab[0])); i++) {
		if (AFMT_ENCODING(info->in) == feed_matrix_tab[i].format)
			info->apply = feed_matrix_tab[i].apply;
	}

	if (info->apply == NULL) {
#ifdef FEEDMATRIX_GENERIC
		info->rd = feeder_format_read_op(info->in);
		info->wr = feeder_format_write_op(info->out);
		if (info->rd == NULL || info->wr == NULL) {
			free(info, M_DEVBUF);
			return (EINVAL);
		}
		info->apply = feed_matrix_apply_generic;
#else
		free(info, M_DEVBUF);
		return (EINVAL);
#endif
	}

	m_in  = feeder_matrix_format_map(info->in);
	m_out = feeder_matrix_format_map(info->out);

	ret = feed_matrix_setup(info, m_in, m_out);
	if (ret != 0) {
		free(info, M_DEVBUF);
		return (ret);
	}

	f->data = info;

	return (0);
}

static int
feed_matrix_free(struct pcm_feeder *f)
{
	struct feed_matrix_info *info;

	info = f->data;
	if (info != NULL)
		free(info, M_DEVBUF);

	f->data = NULL;

	return (0);
}

static int
feed_matrix_feed(struct pcm_feeder *f, struct pcm_channel *c, uint8_t *b,
    uint32_t count, void *source)
{
	struct feed_matrix_info *info;
	uint32_t j, inmax;
	uint8_t *src, *dst;

	info = f->data;
	if (info->matrix[0].chn[0] == SND_CHN_T_EOF)
		return (FEEDER_FEED(f->source, c, b, count, source));

	dst = b;
	count = SND_FXROUND(count, info->oalign);
	inmax = info->ialign + info->oalign;

	/*
	 * This loop might look simmilar to other feeder_* loops, but be
	 * advised: matrixing might involve overlapping (think about
	 * swapping end to front or something like that). In this regard it
	 * might be simmilar to feeder_format, but feeder_format works on
	 * 'sample' domain where it can be fitted into single 32bit integer
	 * while matrixing works on 'sample frame' domain.
	 */
	do {
		if (count < info->oalign)
			break;

		if (count < inmax) {
			src = info->reservoir;
			j = info->ialign;
		} else {
			if (info->ialign == info->oalign)
				j = count - info->oalign;
			else if (info->ialign > info->oalign)
				j = SND_FXROUND(count - info->oalign,
				    info->ialign);
			else
				j = (SND_FXDIV(count, info->oalign) - 1) *
				    info->ialign;
			src = dst + count - j;
		}

		j = SND_FXDIV(FEEDER_FEED(f->source, c, src, j, source),
		    info->ialign);
		if (j == 0)
			break;

		info->apply(info, src, dst, j);

		j *= info->oalign;
		dst += j;
		count -= j;

	} while (count != 0);

	return (dst - b);
}

static struct pcm_feederdesc feeder_matrix_desc[] = {
	{ FEEDER_MATRIX, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0 }
};

static kobj_method_t feeder_matrix_methods[] = {
	KOBJMETHOD(feeder_init,		feed_matrix_init),
	KOBJMETHOD(feeder_free,		feed_matrix_free),
	KOBJMETHOD(feeder_feed,		feed_matrix_feed),
	KOBJMETHOD_END
};

FEEDER_DECLARE(feeder_matrix, NULL);

/* External */
int
feeder_matrix_setup(struct pcm_feeder *f, struct pcmchan_matrix *m_in,
    struct pcmchan_matrix *m_out)
{

	if (f == NULL || f->desc == NULL || f->desc->type != FEEDER_MATRIX ||
	    f->data == NULL)
		return (EINVAL);

	return (feed_matrix_setup(f->data, m_in, m_out));
}

/*
 * feeder_matrix_default_id(): For a given number of channels, return
 *                             default prefered id (example: both 5.1 and
 *                             6.0 are simply 6 channels, but 5.1 is more
 *                             preferable).
 */
int
feeder_matrix_default_id(uint32_t ch)
{

	if (ch < feeder_matrix_maps[SND_CHN_MATRIX_BEGIN].channels ||
	    ch > feeder_matrix_maps[SND_CHN_MATRIX_END].channels)
		return (SND_CHN_MATRIX_UNKNOWN);

	return (feeder_matrix_maps[feeder_matrix_default_ids[ch]].id);
}

/*
 * feeder_matrix_default_channel_map(): Ditto, but return matrix map
 *                                      instead.
 */
struct pcmchan_matrix *
feeder_matrix_default_channel_map(uint32_t ch)
{

	if (ch < feeder_matrix_maps[SND_CHN_MATRIX_BEGIN].channels ||
	    ch > feeder_matrix_maps[SND_CHN_MATRIX_END].channels)
		return (NULL);

	return (&feeder_matrix_maps[feeder_matrix_default_ids[ch]]);
}

/*
 * feeder_matrix_default_format(): For a given audio format, return the
 *                                 proper audio format based on preferable
 *                                 matrix.
 */
uint32_t
feeder_matrix_default_format(uint32_t format)
{
	struct pcmchan_matrix *m;
	uint32_t i, ch, ext;

	ch = AFMT_CHANNEL(format);
	ext = AFMT_EXTCHANNEL(format);

	if (ext != 0) {
		for (i = SND_CHN_MATRIX_BEGIN; i <= SND_CHN_MATRIX_END; i++) {
			if (feeder_matrix_maps[i].channels == ch &&
			    feeder_matrix_maps[i].ext == ext)
			return (SND_FORMAT(format, ch, ext));
		}
	}

	m = feeder_matrix_default_channel_map(ch);
	if (m == NULL)
		return (0x00000000);

	return (SND_FORMAT(format, ch, m->ext));
}

/*
 * feeder_matrix_format_id(): For a given audio format, return its matrix
 *                            id.
 */
int
feeder_matrix_format_id(uint32_t format)
{
	uint32_t i, ch, ext;

	ch = AFMT_CHANNEL(format);
	ext = AFMT_EXTCHANNEL(format);

	for (i = SND_CHN_MATRIX_BEGIN; i <= SND_CHN_MATRIX_END; i++) {
		if (feeder_matrix_maps[i].channels == ch &&
		    feeder_matrix_maps[i].ext == ext)
			return (feeder_matrix_maps[i].id);
	}

	return (SND_CHN_MATRIX_UNKNOWN);
}

/*
 * feeder_matrix_format_map(): For a given audio format, return its matrix
 *                             map.
 */
struct pcmchan_matrix *
feeder_matrix_format_map(uint32_t format)
{
	uint32_t i, ch, ext;

	ch = AFMT_CHANNEL(format);
	ext = AFMT_EXTCHANNEL(format);

	for (i = SND_CHN_MATRIX_BEGIN; i <= SND_CHN_MATRIX_END; i++) {
		if (feeder_matrix_maps[i].channels == ch &&
		    feeder_matrix_maps[i].ext == ext)
			return (&feeder_matrix_maps[i]);
	}

	return (NULL);
}

/*
 * feeder_matrix_id_map(): For a given matrix id, return its matrix map.
 */
struct pcmchan_matrix *
feeder_matrix_id_map(int id)
{

	if (id < SND_CHN_MATRIX_BEGIN || id > SND_CHN_MATRIX_END)
		return (NULL);

	return (&feeder_matrix_maps[id]);
}

/*
 * feeder_matrix_compare(): Compare the simmilarities of matrices.
 */
int
feeder_matrix_compare(struct pcmchan_matrix *m_in, struct pcmchan_matrix *m_out)
{
	uint32_t i;

	if (m_in == m_out)
		return (0);

	if (m_in->channels != m_out->channels || m_in->ext != m_out->ext ||
	    m_in->mask != m_out->mask)
		return (1);

	for (i = 0; i < (sizeof(m_in->map) / sizeof(m_in->map[0])); i++) {
		if (m_in->map[i].type != m_out->map[i].type)
			return (1);
		if (m_in->map[i].type == SND_CHN_T_MAX)
			break;
		if (m_in->map[i].members != m_out->map[i].members)
			return (1);
		if (i <= SND_CHN_T_END) {
			if (m_in->offset[m_in->map[i].type] !=
			    m_out->offset[m_out->map[i].type])
				return (1);
		}
	}

	return (0);
}

/*
 * XXX 4front intepretation of "surround" is ambigous and sort of
 *     conflicting with "rear"/"back". Map it to "side". Well.. 
 *     who cares?
 */
static int snd_chn_to_oss[SND_CHN_T_MAX] = {
	[SND_CHN_T_FL] = CHID_L,
	[SND_CHN_T_FR] = CHID_R,
	[SND_CHN_T_FC] = CHID_C,
	[SND_CHN_T_LF] = CHID_LFE,
	[SND_CHN_T_SL] = CHID_LS,
	[SND_CHN_T_SR] = CHID_RS,
	[SND_CHN_T_BL] = CHID_LR,
	[SND_CHN_T_BR] = CHID_RR
};

#define SND_CHN_OSS_VALIDMASK						\
			(SND_CHN_T_MASK_FL | SND_CHN_T_MASK_FR |	\
			 SND_CHN_T_MASK_FC | SND_CHN_T_MASK_LF |	\
			 SND_CHN_T_MASK_SL | SND_CHN_T_MASK_SR |	\
			 SND_CHN_T_MASK_BL | SND_CHN_T_MASK_BR)

#define SND_CHN_OSS_MAX		8
#define SND_CHN_OSS_BEGIN	CHID_L
#define SND_CHN_OSS_END		CHID_RR

static int oss_to_snd_chn[SND_CHN_OSS_END + 1] = {
	[CHID_L]   = SND_CHN_T_FL,
	[CHID_R]   = SND_CHN_T_FR,
	[CHID_C]   = SND_CHN_T_FC,
	[CHID_LFE] = SND_CHN_T_LF,
	[CHID_LS]  = SND_CHN_T_SL,
	[CHID_RS]  = SND_CHN_T_SR,
	[CHID_LR]  = SND_CHN_T_BL,
	[CHID_RR]  = SND_CHN_T_BR
};

/*
 * Used by SNDCTL_DSP_GET_CHNORDER.
 */
int
feeder_matrix_oss_get_channel_order(struct pcmchan_matrix *m,
    unsigned long long *map)
{
	unsigned long long tmpmap;
	uint32_t i;

	if (m == NULL || map == NULL || (m->mask & ~SND_CHN_OSS_VALIDMASK) ||
	    m->channels > SND_CHN_OSS_MAX)
		return (EINVAL);

	tmpmap = 0x0000000000000000ULL;

	for (i = 0; i < SND_CHN_OSS_MAX && m->map[i].type != SND_CHN_T_MAX;
	    i++) {
		if ((1 << m->map[i].type) & ~SND_CHN_OSS_VALIDMASK)
			return (EINVAL);
		tmpmap |=
		    (unsigned long long)snd_chn_to_oss[m->map[i].type] <<
		    (i * 4);
	}

	*map = tmpmap;

	return (0);
}

/*
 * Used by SNDCTL_DSP_SET_CHNORDER.
 */
int
feeder_matrix_oss_set_channel_order(struct pcmchan_matrix *m,
    unsigned long long *map)
{
	struct pcmchan_matrix tmp;
	uint32_t chmask, i;
	int ch, cheof;

	if (m == NULL || map == NULL || (m->mask & ~SND_CHN_OSS_VALIDMASK) ||
	    m->channels > SND_CHN_OSS_MAX || (*map & 0xffffffff00000000ULL))
		return (EINVAL);

	tmp = *m;
	tmp.channels = 0;
	tmp.ext = 0;
	tmp.mask = 0;
	memset(tmp.offset, -1, sizeof(tmp.offset));
	cheof = 0;

	for (i = 0; i < SND_CHN_OSS_MAX; i++) {
		ch = (*map >> (i * 4)) & 0xf;
		if (ch < SND_CHN_OSS_BEGIN) {
			if (cheof == 0 && m->map[i].type != SND_CHN_T_MAX)
				return (EINVAL);
			cheof++;
			tmp.map[i] = m->map[i];
			continue;
		} else if (ch > SND_CHN_OSS_END)
			return (EINVAL);
		else if (cheof != 0)
			return (EINVAL);
		ch = oss_to_snd_chn[ch];
		chmask = 1 << ch;
		/* channel not exist in matrix */
		if (!(chmask & m->mask))
			return (EINVAL);
		/* duplicated channel */
		if (chmask & tmp.mask)
			return (EINVAL);
		tmp.map[i] = m->map[m->offset[ch]];
		if (tmp.map[i].type != ch)
			return (EINVAL);
		tmp.offset[ch] = i;
		tmp.mask |= chmask;
		tmp.channels++;
		if (chmask & SND_CHN_T_MASK_LF)
			tmp.ext++;
	}

	if (tmp.channels != m->channels || tmp.ext != m->ext ||
	    tmp.mask != m->mask ||
	    tmp.map[m->channels].type != SND_CHN_T_MAX)
		return (EINVAL);

	*m = tmp;

	return (0);
}
