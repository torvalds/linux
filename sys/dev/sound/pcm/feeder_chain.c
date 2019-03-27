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

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>

#include "feeder_if.h"

SND_DECLARE_FILE("$FreeBSD$");

/* chain state */
struct feeder_chain_state {
	uint32_t afmt;				/* audio format */
	uint32_t rate;				/* sampling rate */
	struct pcmchan_matrix *matrix;		/* matrix map */
};

/*
 * chain descriptor that will be passed around from the beginning until the
 * end of chain process.
 */
struct feeder_chain_desc {
	struct feeder_chain_state origin;	/* original state */
	struct feeder_chain_state current;	/* current state */
	struct feeder_chain_state target;	/* target state */
	struct pcm_feederdesc desc;		/* feeder descriptor */
	uint32_t afmt_ne;			/* prefered native endian */
	int mode;				/* chain mode */
	int use_eq;				/* need EQ? */
	int use_matrix;				/* need channel matrixing? */
	int use_volume;				/* need softpcmvol? */
	int dummy;				/* dummy passthrough */
	int expensive;				/* possibly expensive */
};

#define FEEDER_CHAIN_LEAN		0
#define FEEDER_CHAIN_16			1
#define FEEDER_CHAIN_32			2
#define FEEDER_CHAIN_MULTI		3
#define FEEDER_CHAIN_FULLMULTI		4
#define FEEDER_CHAIN_LAST		5

#if defined(SND_FEEDER_FULL_MULTIFORMAT)
#define FEEDER_CHAIN_DEFAULT		FEEDER_CHAIN_FULLMULTI
#elif defined(SND_FEEDER_MULTIFORMAT)
#define FEEDER_CHAIN_DEFAULT		FEEDER_CHAIN_MULTI
#else
#define FEEDER_CHAIN_DEFAULT		FEEDER_CHAIN_LEAN
#endif

/*
 * List of prefered formats that might be required during
 * processing. It will be decided through snd_fmtbest().
 */

/* 'Lean' mode, signed 16 or 32 bit native endian. */
static uint32_t feeder_chain_formats_lean[] = {
	AFMT_S16_NE, AFMT_S32_NE,
	0
};

/* Force everything to signed 16 bit native endian. */
static uint32_t feeder_chain_formats_16[] = {
	AFMT_S16_NE,
	0
};

/* Force everything to signed 32 bit native endian. */
static uint32_t feeder_chain_formats_32[] = {
	AFMT_S32_NE,
	0
};

/* Multiple choices, all except 8 bit. */
static uint32_t feeder_chain_formats_multi[] = {
	AFMT_S16_LE, AFMT_S16_BE, AFMT_U16_LE, AFMT_U16_BE,
	AFMT_S24_LE, AFMT_S24_BE, AFMT_U24_LE, AFMT_U24_BE,
	AFMT_S32_LE, AFMT_S32_BE, AFMT_U32_LE, AFMT_U32_BE,
	0
};

/* Everything that is convertible. */
static uint32_t feeder_chain_formats_fullmulti[] = {
	AFMT_S8, AFMT_U8,
	AFMT_S16_LE, AFMT_S16_BE, AFMT_U16_LE, AFMT_U16_BE,
	AFMT_S24_LE, AFMT_S24_BE, AFMT_U24_LE, AFMT_U24_BE,
	AFMT_S32_LE, AFMT_S32_BE, AFMT_U32_LE, AFMT_U32_BE,
	0
};

static uint32_t *feeder_chain_formats[FEEDER_CHAIN_LAST] = {
	[FEEDER_CHAIN_LEAN]      = feeder_chain_formats_lean,
	[FEEDER_CHAIN_16]        = feeder_chain_formats_16,
	[FEEDER_CHAIN_32]        = feeder_chain_formats_32,
	[FEEDER_CHAIN_MULTI]     = feeder_chain_formats_multi,
	[FEEDER_CHAIN_FULLMULTI] = feeder_chain_formats_fullmulti
};

static int feeder_chain_mode = FEEDER_CHAIN_DEFAULT;

#if defined(_KERNEL) && defined(SND_DEBUG) && defined(SND_FEEDER_FULL_MULTIFORMAT)
SYSCTL_INT(_hw_snd, OID_AUTO, feeder_chain_mode, CTLFLAG_RWTUN,
    &feeder_chain_mode, 0,
    "feeder chain mode "
    "(0=lean, 1=16bit, 2=32bit, 3=multiformat, 4=fullmultiformat)");
#endif

/*
 * feeder_build_format(): Chain any format converter.
 */
static int
feeder_build_format(struct pcm_channel *c, struct feeder_chain_desc *cdesc)
{
	struct feeder_class *fc;
	struct pcm_feederdesc *desc;
	int ret;

	desc = &(cdesc->desc);
	desc->type = FEEDER_FORMAT;
	desc->in = 0;
	desc->out = 0;
	desc->flags = 0;

	fc = feeder_getclass(desc);
	if (fc == NULL) {
		device_printf(c->dev,
		    "%s(): can't find feeder_format\n", __func__);
		return (ENOTSUP);
	}

	desc->in = cdesc->current.afmt;
	desc->out = cdesc->target.afmt;

	ret = chn_addfeeder(c, fc, desc);
	if (ret != 0) {
		device_printf(c->dev,
		    "%s(): can't add feeder_format\n", __func__);
		return (ret);
	}

	c->feederflags |= 1 << FEEDER_FORMAT;

	cdesc->current.afmt = cdesc->target.afmt;

	return (0);
}

/*
 * feeder_build_formatne(): Chain format converter that suite best for native
 *                          endian format.
 */
static int
feeder_build_formatne(struct pcm_channel *c, struct feeder_chain_desc *cdesc)
{
	struct feeder_chain_state otarget;
	int ret;

	if (cdesc->afmt_ne == 0 ||
	    AFMT_ENCODING(cdesc->current.afmt) == cdesc->afmt_ne)
		return (0);

	otarget = cdesc->target;
	cdesc->target = cdesc->current;
	cdesc->target.afmt = SND_FORMAT(cdesc->afmt_ne,
	    cdesc->current.matrix->channels, cdesc->current.matrix->ext);

	ret = feeder_build_format(c, cdesc);
	if (ret != 0)
		return (ret);

	cdesc->target = otarget;

	return (0);
}

/*
 * feeder_build_rate(): Chain sample rate converter.
 */
static int
feeder_build_rate(struct pcm_channel *c, struct feeder_chain_desc *cdesc)
{
	struct feeder_class *fc;
	struct pcm_feeder *f;
	struct pcm_feederdesc *desc;
	int ret;

	ret = feeder_build_formatne(c, cdesc);
	if (ret != 0)
		return (ret);

	desc = &(cdesc->desc);
	desc->type = FEEDER_RATE;
	desc->in = 0;
	desc->out = 0;
	desc->flags = 0;

	fc = feeder_getclass(desc);
	if (fc == NULL) {
		device_printf(c->dev,
		    "%s(): can't find feeder_rate\n", __func__);
		return (ENOTSUP);
	}

	desc->in = cdesc->current.afmt;
	desc->out = desc->in;

	ret = chn_addfeeder(c, fc, desc);
	if (ret != 0) {
		device_printf(c->dev,
		    "%s(): can't add feeder_rate\n", __func__);
		return (ret);
	}

	f = c->feeder;

	/*
	 * If in 'dummy' mode (possibly due to passthrough mode), set the
	 * conversion quality to the lowest possible (should be fastest) since
	 * listener won't be hearing anything. Theoretically we can just
	 * disable it, but that will cause weird runtime behaviour:
	 * application appear to play something that is either too fast or too
	 * slow.
	 */
	if (cdesc->dummy != 0) {
		ret = FEEDER_SET(f, FEEDRATE_QUALITY, 0);
		if (ret != 0) {
			device_printf(c->dev,
			    "%s(): can't set resampling quality\n", __func__);
			return (ret);
		}
	}

	ret = FEEDER_SET(f, FEEDRATE_SRC, cdesc->current.rate);
	if (ret != 0) {
		device_printf(c->dev,
		    "%s(): can't set source rate\n", __func__);
		return (ret);
	}

	ret = FEEDER_SET(f, FEEDRATE_DST, cdesc->target.rate);
	if (ret != 0) {
		device_printf(c->dev,
		    "%s(): can't set destination rate\n", __func__);
		return (ret);
	}

	c->feederflags |= 1 << FEEDER_RATE;

	cdesc->current.rate = cdesc->target.rate;

	return (0);
}

/*
 * feeder_build_matrix(): Chain channel matrixing converter.
 */
static int
feeder_build_matrix(struct pcm_channel *c, struct feeder_chain_desc *cdesc)
{
	struct feeder_class *fc;
	struct pcm_feeder *f;
	struct pcm_feederdesc *desc;
	int ret;

	ret = feeder_build_formatne(c, cdesc);
	if (ret != 0)
		return (ret);

	desc = &(cdesc->desc);
	desc->type = FEEDER_MATRIX;
	desc->in = 0;
	desc->out = 0;
	desc->flags = 0;

	fc = feeder_getclass(desc);
	if (fc == NULL) {
		device_printf(c->dev,
		    "%s(): can't find feeder_matrix\n", __func__);
		return (ENOTSUP);
	}

	desc->in = cdesc->current.afmt;
	desc->out = SND_FORMAT(cdesc->current.afmt,
	    cdesc->target.matrix->channels, cdesc->target.matrix->ext);

	ret = chn_addfeeder(c, fc, desc);
	if (ret != 0) {
		device_printf(c->dev,
		    "%s(): can't add feeder_matrix\n", __func__);
		return (ret);
	}

	f = c->feeder;
	ret = feeder_matrix_setup(f, cdesc->current.matrix,
	    cdesc->target.matrix);
	if (ret != 0) {
		device_printf(c->dev,
		    "%s(): feeder_matrix_setup() failed\n", __func__);
		return (ret);
	}

	c->feederflags |= 1 << FEEDER_MATRIX;

	cdesc->current.afmt = desc->out;
	cdesc->current.matrix = cdesc->target.matrix;
	cdesc->use_matrix = 0;

	return (0);
}

/*
 * feeder_build_volume(): Chain soft volume.
 */
static int
feeder_build_volume(struct pcm_channel *c, struct feeder_chain_desc *cdesc)
{
	struct feeder_class *fc;
	struct pcm_feeder *f;
	struct pcm_feederdesc *desc;
	int ret;

	ret = feeder_build_formatne(c, cdesc);
	if (ret != 0)
		return (ret);

	desc = &(cdesc->desc);
	desc->type = FEEDER_VOLUME;
	desc->in = 0;
	desc->out = 0;
	desc->flags = 0;

	fc = feeder_getclass(desc);
	if (fc == NULL) {
		device_printf(c->dev,
		    "%s(): can't find feeder_volume\n", __func__);
		return (ENOTSUP);
	}

	desc->in = cdesc->current.afmt;
	desc->out = desc->in;

	ret = chn_addfeeder(c, fc, desc);
	if (ret != 0) {
		device_printf(c->dev,
		    "%s(): can't add feeder_volume\n", __func__);
		return (ret);
	}

	f = c->feeder;

	/*
	 * If in 'dummy' mode (possibly due to passthrough mode), set BYPASS
	 * mode since listener won't be hearing anything. Theoretically we can
	 * just disable it, but that will confuse volume per channel mixer.
	 */
	if (cdesc->dummy != 0) {
		ret = FEEDER_SET(f, FEEDVOLUME_STATE, FEEDVOLUME_BYPASS);
		if (ret != 0) {
			device_printf(c->dev,
			    "%s(): can't set volume bypass\n", __func__);
			return (ret);
		}
	}

	ret = feeder_volume_apply_matrix(f, cdesc->current.matrix);
	if (ret != 0) {
		device_printf(c->dev,
		    "%s(): feeder_volume_apply_matrix() failed\n", __func__);
		return (ret);
	}

	c->feederflags |= 1 << FEEDER_VOLUME;

	cdesc->use_volume = 0;

	return (0);
}

/*
 * feeder_build_eq(): Chain parametric software equalizer.
 */
static int
feeder_build_eq(struct pcm_channel *c, struct feeder_chain_desc *cdesc)
{
	struct feeder_class *fc;
	struct pcm_feeder *f;
	struct pcm_feederdesc *desc;
	int ret;

	ret = feeder_build_formatne(c, cdesc);
	if (ret != 0)
		return (ret);

	desc = &(cdesc->desc);
	desc->type = FEEDER_EQ;
	desc->in = 0;
	desc->out = 0;
	desc->flags = 0;

	fc = feeder_getclass(desc);
	if (fc == NULL) {
		device_printf(c->dev,
		    "%s(): can't find feeder_eq\n", __func__);
		return (ENOTSUP);
	}

	desc->in = cdesc->current.afmt;
	desc->out = desc->in;

	ret = chn_addfeeder(c, fc, desc);
	if (ret != 0) {
		device_printf(c->dev,
		    "%s(): can't add feeder_eq\n", __func__);
		return (ret);
	}

	f = c->feeder;

	ret = FEEDER_SET(f, FEEDEQ_RATE, cdesc->current.rate);
	if (ret != 0) {
		device_printf(c->dev,
		    "%s(): can't set rate on feeder_eq\n", __func__);
		return (ret);
	}

	c->feederflags |= 1 << FEEDER_EQ;

	cdesc->use_eq = 0;

	return (0);
}

/*
 * feeder_build_root(): Chain root feeder, the top, father of all.
 */
static int
feeder_build_root(struct pcm_channel *c, struct feeder_chain_desc *cdesc)
{
	struct feeder_class *fc;
	int ret;

	fc = feeder_getclass(NULL);
	if (fc == NULL) {
		device_printf(c->dev,
		    "%s(): can't find feeder_root\n", __func__);
		return (ENOTSUP);
	}

	ret = chn_addfeeder(c, fc, NULL);
	if (ret != 0) {
		device_printf(c->dev,
		    "%s(): can't add feeder_root\n", __func__);
		return (ret);
	}

	c->feederflags |= 1 << FEEDER_ROOT;

	c->feeder->desc->in = cdesc->current.afmt;
	c->feeder->desc->out = cdesc->current.afmt;

	return (0);
}

/*
 * feeder_build_mixer(): Chain software mixer for virtual channels.
 */
static int
feeder_build_mixer(struct pcm_channel *c, struct feeder_chain_desc *cdesc)
{
	struct feeder_class *fc;
	struct pcm_feederdesc *desc;
	int ret;

	desc = &(cdesc->desc);
	desc->type = FEEDER_MIXER;
	desc->in = 0;
	desc->out = 0;
	desc->flags = 0;

	fc = feeder_getclass(desc);
	if (fc == NULL) {
		device_printf(c->dev,
		    "%s(): can't find feeder_mixer\n", __func__);
		return (ENOTSUP);
	}

	desc->in = cdesc->current.afmt;
	desc->out = desc->in;

	ret = chn_addfeeder(c, fc, desc);
	if (ret != 0) {
		device_printf(c->dev,
		    "%s(): can't add feeder_mixer\n", __func__);
		return (ret);
	}

	c->feederflags |= 1 << FEEDER_MIXER;

	return (0);
}

/* Macrosses to ease our job doing stuffs later. */
#define FEEDER_BW(c, t)		((c)->t.matrix->channels * (c)->t.rate)

#define FEEDRATE_UP(c)		((c)->target.rate > (c)->current.rate)
#define FEEDRATE_DOWN(c)	((c)->target.rate < (c)->current.rate)
#define FEEDRATE_REQUIRED(c)	(FEEDRATE_UP(c) || FEEDRATE_DOWN(c))

#define FEEDMATRIX_UP(c)	((c)->target.matrix->channels >		\
				 (c)->current.matrix->channels)
#define FEEDMATRIX_DOWN(c)	((c)->target.matrix->channels <		\
				 (c)->current.matrix->channels)
#define FEEDMATRIX_REQUIRED(c)	(FEEDMATRIX_UP(c) ||			\
				 FEEDMATRIX_DOWN(c) || (c)->use_matrix != 0)

#define FEEDFORMAT_REQUIRED(c)	(AFMT_ENCODING((c)->current.afmt) !=	\
				 AFMT_ENCODING((c)->target.afmt))

#define FEEDVOLUME_REQUIRED(c)	((c)->use_volume != 0)

#define FEEDEQ_VALIDRATE(c, t)	(feeder_eq_validrate((c)->t.rate) != 0)
#define FEEDEQ_ECONOMY(c)	(FEEDER_BW(c, current) < FEEDER_BW(c, target))
#define FEEDEQ_REQUIRED(c)	((c)->use_eq != 0 &&			\
				 FEEDEQ_VALIDRATE(c, current))

#define FEEDFORMAT_NE_REQUIRED(c)					\
	((c)->afmt_ne != AFMT_S32_NE &&					\
	(((c)->mode == FEEDER_CHAIN_16 &&				\
	AFMT_ENCODING((c)->current.afmt) != AFMT_S16_NE) ||		\
	((c)->mode == FEEDER_CHAIN_32 &&				\
	AFMT_ENCODING((c)->current.afmt) != AFMT_S32_NE) ||		\
	(c)->mode == FEEDER_CHAIN_FULLMULTI ||				\
	((c)->mode == FEEDER_CHAIN_MULTI &&				\
	((c)->current.afmt & AFMT_8BIT)) ||				\
	((c)->mode == FEEDER_CHAIN_LEAN &&				\
	!((c)->current.afmt & (AFMT_S16_NE | AFMT_S32_NE)))))

static void
feeder_default_matrix(struct pcmchan_matrix *m, uint32_t fmt, int id)
{
	int x;

	memset(m, 0, sizeof(*m));

	m->id = id;
	m->channels = AFMT_CHANNEL(fmt);
	m->ext = AFMT_EXTCHANNEL(fmt);
	for (x = 0; x != SND_CHN_T_MAX; x++)
		m->offset[x] = -1;
}

int
feeder_chain(struct pcm_channel *c)
{
	struct snddev_info *d;
	struct pcmchan_caps *caps;
	struct feeder_chain_desc cdesc;
	struct pcmchan_matrix *hwmatrix, *softmatrix;
	uint32_t hwfmt, softfmt;
	int ret;

	CHN_LOCKASSERT(c);

	/* Remove everything first. */
	while (chn_removefeeder(c) == 0)
		;

	KASSERT(c->feeder == NULL, ("feeder chain not empty"));

	/* clear and populate chain descriptor. */
	bzero(&cdesc, sizeof(cdesc));

	switch (feeder_chain_mode) {
	case FEEDER_CHAIN_LEAN:
	case FEEDER_CHAIN_16:
	case FEEDER_CHAIN_32:
#if defined(SND_FEEDER_MULTIFORMAT) || defined(SND_FEEDER_FULL_MULTIFORMAT)
	case FEEDER_CHAIN_MULTI:
#endif
#if defined(SND_FEEDER_FULL_MULTIFORMAT)
	case FEEDER_CHAIN_FULLMULTI:
#endif
		break;
	default:
		feeder_chain_mode = FEEDER_CHAIN_DEFAULT;
		break;
	}

	cdesc.mode = feeder_chain_mode;
	cdesc.expensive = 1;	/* XXX faster.. */

#define VCHAN_PASSTHROUGH(c)	(((c)->flags & (CHN_F_VIRTUAL |		\
				 CHN_F_PASSTHROUGH)) ==			\
				 (CHN_F_VIRTUAL | CHN_F_PASSTHROUGH))

	/* Get the best possible hardware format. */
	if (VCHAN_PASSTHROUGH(c))
		hwfmt = c->parentchannel->format;
	else {
		caps = chn_getcaps(c);
		if (caps == NULL || caps->fmtlist == NULL) {
			device_printf(c->dev,
			    "%s(): failed to get channel caps\n", __func__);
			return (ENODEV);
		}

		if ((c->format & AFMT_PASSTHROUGH) &&
		    !snd_fmtvalid(c->format, caps->fmtlist))
			return (ENODEV);

		hwfmt = snd_fmtbest(c->format, caps->fmtlist);
		if (hwfmt == 0 || !snd_fmtvalid(hwfmt, caps->fmtlist)) {
			device_printf(c->dev,
			    "%s(): invalid hardware format 0x%08x\n",
			    __func__, hwfmt);
			{
				int i;
				for (i = 0; caps->fmtlist[i] != 0; i++)
					printf("0x%08x\n", caps->fmtlist[i]);
				printf("Req: 0x%08x\n", c->format);
			}
			return (ENODEV);
		}
	}

	/*
	 * The 'hardware' possibly have different intepretation of channel
	 * matrixing, so get it first .....
	 */
	hwmatrix = CHANNEL_GETMATRIX(c->methods, c->devinfo, hwfmt);
	if (hwmatrix == NULL) {
		/* setup a default matrix */
		hwmatrix = &c->matrix_scratch;
		feeder_default_matrix(hwmatrix, hwfmt,
		    SND_CHN_MATRIX_UNKNOWN);
	}
	/* ..... and rebuild hwfmt. */
	hwfmt = SND_FORMAT(hwfmt, hwmatrix->channels, hwmatrix->ext);

	/* Reset and rebuild default channel format/matrix map. */
	softfmt = c->format;
	softmatrix = &c->matrix;
	if (softmatrix->channels != AFMT_CHANNEL(softfmt) ||
	    softmatrix->ext != AFMT_EXTCHANNEL(softfmt)) {
		softmatrix = feeder_matrix_format_map(softfmt);
		if (softmatrix == NULL) {
			/* setup a default matrix */
		  	softmatrix = &c->matrix;
			feeder_default_matrix(softmatrix, softfmt,
			    SND_CHN_MATRIX_PCMCHANNEL);
		} else {
			c->matrix = *softmatrix;
			c->matrix.id = SND_CHN_MATRIX_PCMCHANNEL;
		}
	}
	softfmt = SND_FORMAT(softfmt, softmatrix->channels, softmatrix->ext);
	if (softfmt != c->format)
		device_printf(c->dev,
		    "%s(): WARNING: %s Soft format 0x%08x -> 0x%08x\n",
		    __func__, CHN_DIRSTR(c), c->format, softfmt);

	/*
	 * PLAY and REC are opposite.
	 */
	if (c->direction == PCMDIR_PLAY) {
		cdesc.origin.afmt    = softfmt;
		cdesc.origin.matrix  = softmatrix;
		cdesc.origin.rate    = c->speed;
		cdesc.target.afmt    = hwfmt;
		cdesc.target.matrix  = hwmatrix;
		cdesc.target.rate    = sndbuf_getspd(c->bufhard);
	} else {
		cdesc.origin.afmt    = hwfmt;
		cdesc.origin.matrix  = hwmatrix;
		cdesc.origin.rate    = sndbuf_getspd(c->bufhard);
		cdesc.target.afmt    = softfmt;
		cdesc.target.matrix  = softmatrix;
		cdesc.target.rate    = c->speed;
	}

	d = c->parentsnddev;

	/*
	 * If channel is in bitperfect or passthrough mode, make it appear
	 * that 'origin' and 'target' identical, skipping mostly chain
	 * procedures.
	 */
	if (CHN_BITPERFECT(c) || (c->format & AFMT_PASSTHROUGH)) {
		if (c->direction == PCMDIR_PLAY)
			cdesc.origin = cdesc.target;
		else
			cdesc.target = cdesc.origin;
		c->format = cdesc.target.afmt;
		c->speed  = cdesc.target.rate;
	} else {
		/* hwfmt is not convertible, so 'dummy' it. */
		if (hwfmt & AFMT_PASSTHROUGH)
			cdesc.dummy = 1;

		if ((softfmt & AFMT_CONVERTIBLE) &&
		    (((d->flags & SD_F_VPC) && !(c->flags & CHN_F_HAS_VCHAN)) ||
		    (!(d->flags & SD_F_VPC) && (d->flags & SD_F_SOFTPCMVOL) &&
		    !(c->flags & CHN_F_VIRTUAL))))
			cdesc.use_volume = 1;

		if (feeder_matrix_compare(cdesc.origin.matrix,
		    cdesc.target.matrix) != 0)
			cdesc.use_matrix = 1;

		/* Soft EQ only applicable for PLAY. */
		if (cdesc.dummy == 0 &&
		    c->direction == PCMDIR_PLAY && (d->flags & SD_F_EQ) &&
		    (((d->flags & SD_F_EQ_PC) &&
		    !(c->flags & CHN_F_HAS_VCHAN)) ||
		    (!(d->flags & SD_F_EQ_PC) && !(c->flags & CHN_F_VIRTUAL))))
			cdesc.use_eq = 1;

		if (FEEDFORMAT_NE_REQUIRED(&cdesc)) {
			cdesc.afmt_ne =
			    (cdesc.dummy != 0) ?
			    snd_fmtbest(AFMT_ENCODING(softfmt),
			    feeder_chain_formats[cdesc.mode]) :
			    snd_fmtbest(AFMT_ENCODING(cdesc.target.afmt),
			    feeder_chain_formats[cdesc.mode]);
			if (cdesc.afmt_ne == 0) {
				device_printf(c->dev,
				    "%s(): snd_fmtbest failed!\n", __func__);
				cdesc.afmt_ne =
				    (((cdesc.dummy != 0) ? softfmt :
				    cdesc.target.afmt) &
				    (AFMT_24BIT | AFMT_32BIT)) ?
				    AFMT_S32_NE : AFMT_S16_NE;
			}
		}
	}

	cdesc.current = cdesc.origin;

	/* Build everything. */

	c->feederflags = 0;

#define FEEDER_BUILD(t)	do {						\
	ret = feeder_build_##t(c, &cdesc);				\
	if (ret != 0)							\
		return (ret);						\
	} while (0)

	if (!(c->flags & CHN_F_HAS_VCHAN) || c->direction == PCMDIR_REC)
		FEEDER_BUILD(root);
	else if (c->direction == PCMDIR_PLAY && (c->flags & CHN_F_HAS_VCHAN))
		FEEDER_BUILD(mixer);
	else
		return (ENOTSUP);

	/*
	 * The basic idea is: The smaller the bandwidth, the cheaper the
	 * conversion process, with following constraints:-
	 *
	 * 1) Almost all feeders work best in 16/32 native endian.
	 * 2) Try to avoid 8bit feeders due to poor dynamic range.
	 * 3) Avoid volume, format, matrix and rate in BITPERFECT or
	 *    PASSTHROUGH mode.
	 * 4) Try putting volume before EQ or rate. Should help to
	 *    avoid/reduce possible clipping.
	 * 5) EQ require specific, valid rate, unless it allow sloppy
	 *    conversion.
	 */
	if (FEEDMATRIX_UP(&cdesc)) {
		if (FEEDEQ_REQUIRED(&cdesc) &&
		    (!FEEDEQ_VALIDRATE(&cdesc, target) ||
		    (cdesc.expensive == 0 && FEEDEQ_ECONOMY(&cdesc))))
			FEEDER_BUILD(eq);
		if (FEEDRATE_REQUIRED(&cdesc))
			FEEDER_BUILD(rate);
		FEEDER_BUILD(matrix);
		if (FEEDVOLUME_REQUIRED(&cdesc))
			FEEDER_BUILD(volume);
		if (FEEDEQ_REQUIRED(&cdesc))
			FEEDER_BUILD(eq);
	} else if (FEEDMATRIX_DOWN(&cdesc)) {
		FEEDER_BUILD(matrix);
		if (FEEDVOLUME_REQUIRED(&cdesc))
			FEEDER_BUILD(volume);
		if (FEEDEQ_REQUIRED(&cdesc) &&
		    (!FEEDEQ_VALIDRATE(&cdesc, target) ||
		    FEEDEQ_ECONOMY(&cdesc)))
			FEEDER_BUILD(eq);
		if (FEEDRATE_REQUIRED(&cdesc))
			FEEDER_BUILD(rate);
		if (FEEDEQ_REQUIRED(&cdesc))
			FEEDER_BUILD(eq);
	} else {
		if (FEEDRATE_DOWN(&cdesc)) {
			if (FEEDEQ_REQUIRED(&cdesc) &&
			    !FEEDEQ_VALIDRATE(&cdesc, target)) {
				if (FEEDVOLUME_REQUIRED(&cdesc))
					FEEDER_BUILD(volume);
				FEEDER_BUILD(eq);
			}
			FEEDER_BUILD(rate);
		}
		if (FEEDMATRIX_REQUIRED(&cdesc))
			FEEDER_BUILD(matrix);
		if (FEEDVOLUME_REQUIRED(&cdesc))
			FEEDER_BUILD(volume);
		if (FEEDRATE_UP(&cdesc)) {
			if (FEEDEQ_REQUIRED(&cdesc) &&
			    !FEEDEQ_VALIDRATE(&cdesc, target))
				FEEDER_BUILD(eq);
			FEEDER_BUILD(rate);
		}
		if (FEEDEQ_REQUIRED(&cdesc))
			FEEDER_BUILD(eq);
	}

	if (FEEDFORMAT_REQUIRED(&cdesc))
		FEEDER_BUILD(format);

	if (c->direction == PCMDIR_REC && (c->flags & CHN_F_HAS_VCHAN))
		FEEDER_BUILD(mixer);

	sndbuf_setfmt(c->bufsoft, c->format);
	sndbuf_setspd(c->bufsoft, c->speed);

	sndbuf_setfmt(c->bufhard, hwfmt);

	chn_syncstate(c);

	return (0);
}
