/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2009 Ariff Abdullah <ariff@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

struct pcm_feederdesc {
	u_int32_t type;
	u_int32_t in, out;
	u_int32_t flags;
	int idx;
};

struct feeder_class {
	KOBJ_CLASS_FIELDS;
	struct pcm_feederdesc *desc;
	void *data;
};

struct pcm_feeder {
    	KOBJ_FIELDS;
	int align;
	struct pcm_feederdesc *desc, desc_static;
	void *data;
	struct feeder_class *class;
	struct pcm_feeder *source, *parent;

};

void feeder_register(void *p);
struct feeder_class *feeder_getclass(struct pcm_feederdesc *desc);

u_int32_t snd_fmtscore(u_int32_t fmt);
u_int32_t snd_fmtbestbit(u_int32_t fmt, u_int32_t *fmts);
u_int32_t snd_fmtbestchannel(u_int32_t fmt, u_int32_t *fmts);
u_int32_t snd_fmtbest(u_int32_t fmt, u_int32_t *fmts);

int chn_addfeeder(struct pcm_channel *c, struct feeder_class *fc,
    struct pcm_feederdesc *desc);
int chn_removefeeder(struct pcm_channel *c);
struct pcm_feeder *chn_findfeeder(struct pcm_channel *c, u_int32_t type);
void feeder_printchain(struct pcm_feeder *head);
int feeder_chain(struct pcm_channel *);

#define FEEDER_DECLARE(feeder, pdata)					\
static struct feeder_class feeder ## _class = {				\
	.name =		#feeder,					\
	.methods =	feeder ## _methods,				\
	.size =		sizeof(struct pcm_feeder),			\
	.desc =		feeder ## _desc,				\
	.data =		pdata,						\
};									\
SYSINIT(feeder, SI_SUB_DRIVERS, SI_ORDER_ANY, feeder_register,		\
    &feeder ## _class)

enum {
	FEEDER_ROOT,
	FEEDER_FORMAT,
	FEEDER_MIXER,
	FEEDER_RATE,
	FEEDER_EQ,
	FEEDER_VOLUME,
	FEEDER_MATRIX,
	FEEDER_LAST,
};

/* feeder_format */
enum {
	FEEDFORMAT_CHANNELS
};

/* feeder_mixer */
enum {
	FEEDMIXER_CHANNELS
};

/* feeder_rate */
enum {
	FEEDRATE_SRC,
	FEEDRATE_DST,
	FEEDRATE_QUALITY,
	FEEDRATE_CHANNELS
};

#define FEEDRATE_RATEMIN	1
#define FEEDRATE_RATEMAX	2016000		/* 48000 * 42 */
#define FEEDRATE_MIN		1
#define FEEDRATE_MAX		0x7fffff	/* sign 24bit ~ 8ghz ! */
#define FEEDRATE_ROUNDHZ	25
#define FEEDRATE_ROUNDHZ_MIN	0
#define FEEDRATE_ROUNDHZ_MAX	500

extern int feeder_rate_min;
extern int feeder_rate_max;
extern int feeder_rate_round;
extern int feeder_rate_quality;

/* feeder_eq */
enum {
	FEEDEQ_CHANNELS,
	FEEDEQ_RATE,
	FEEDEQ_TREBLE,
	FEEDEQ_BASS,
	FEEDEQ_PREAMP,
	FEEDEQ_STATE,
	FEEDEQ_DISABLE,
	FEEDEQ_ENABLE,
	FEEDEQ_BYPASS,
	FEEDEQ_UNKNOWN
};

int feeder_eq_validrate(uint32_t);
void feeder_eq_initsys(device_t);

/* feeder_volume */
enum {
	FEEDVOLUME_CLASS,
	FEEDVOLUME_CHANNELS,
	FEEDVOLUME_STATE,
	FEEDVOLUME_ENABLE,
	FEEDVOLUME_BYPASS
};

int feeder_volume_apply_matrix(struct pcm_feeder *, struct pcmchan_matrix *);

/* feeder_matrix */
int feeder_matrix_default_id(uint32_t);
struct pcmchan_matrix *feeder_matrix_default_channel_map(uint32_t);

uint32_t feeder_matrix_default_format(uint32_t);

int feeder_matrix_format_id(uint32_t);
struct pcmchan_matrix *feeder_matrix_format_map(uint32_t);

struct pcmchan_matrix *feeder_matrix_id_map(int);

int feeder_matrix_setup(struct pcm_feeder *, struct pcmchan_matrix *,
    struct pcmchan_matrix *);
int feeder_matrix_compare(struct pcmchan_matrix *, struct pcmchan_matrix *);

/* 4Front OSS stuffs */
int feeder_matrix_oss_get_channel_order(struct pcmchan_matrix *,
    unsigned long long *);
int feeder_matrix_oss_set_channel_order(struct pcmchan_matrix *,
    unsigned long long *);

#if 0
/* feeder_matrix */
enum {
	FEEDMATRIX_TYPE,
	FEEDMATRIX_RESET,
	FEEDMATRIX_CHANNELS_IN,
	FEEDMATRIX_CHANNELS_OUT,
	FEEDMATRIX_SET_MAP
};

enum {
	FEEDMATRIX_TYPE_NONE,
	FEEDMATRIX_TYPE_AUTO,
	FEEDMATRIX_TYPE_2X1,
	FEEDMATRIX_TYPE_1X2,
	FEEDMATRIX_TYPE_2X2
};

#define FEEDMATRIX_TYPE_STEREO_TO_MONO	FEEDMATRIX_TYPE_2X1
#define FEEDMATRIX_TYPE_MONO_TO_STEREO	FEEDMATRIX_TYPE_1X2
#define FEEDMATRIX_TYPE_SWAP_STEREO	FEEDMATRIX_TYPE_2X2
#define FEEDMATRIX_MAP(x, y)		((((x) & 0x3f) << 6) | ((y) & 0x3f))
#define FEEDMATRIX_MAP_SRC(x)		((x) & 0x3f)
#define FEEDMATRIX_MAP_DST(x)		(((x) >> 6) & 0x3f)
#endif

/*
 * By default, various feeders only deal with sign 16/32 bit native-endian
 * since it should provide the fastest processing path. Processing 8bit samples
 * is too noisy due to limited dynamic range, while 24bit is quite slow due to
 * unnatural per-byte read/write. However, for debugging purposes, ensuring
 * implementation correctness and torture test, the following can be defined:
 *
 *      SND_FEEDER_MULTIFORMAT - Compile all type of converters, but force
 *                               8bit samples to be converted to 16bit
 *                               native-endian for better dynamic range.
 *                               Process 24bit samples natively.
 * SND_FEEDER_FULL_MULTIFORMAT - Ditto, but process 8bit samples natively.
 */
#ifdef SND_FEEDER_FULL_MULTIFORMAT
#undef SND_FEEDER_MULTIFORMAT
#define SND_FEEDER_MULTIFORMAT	1
#endif
