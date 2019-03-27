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
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>

#include "feeder_if.h"

SND_DECLARE_FILE("$FreeBSD$");

static MALLOC_DEFINE(M_FEEDER, "feeder", "pcm feeder");

#define MAXFEEDERS 	256
#undef FEEDER_DEBUG

struct feedertab_entry {
	SLIST_ENTRY(feedertab_entry) link;
	struct feeder_class *feederclass;
	struct pcm_feederdesc *desc;

	int idx;
};
static SLIST_HEAD(, feedertab_entry) feedertab;

/*****************************************************************************/

void
feeder_register(void *p)
{
	static int feedercnt = 0;

	struct feeder_class *fc = p;
	struct feedertab_entry *fte;
	int i;

	if (feedercnt == 0) {
		KASSERT(fc->desc == NULL, ("first feeder not root: %s", fc->name));

		SLIST_INIT(&feedertab);
		fte = malloc(sizeof(*fte), M_FEEDER, M_NOWAIT | M_ZERO);
		if (fte == NULL) {
			printf("can't allocate memory for root feeder: %s\n",
			    fc->name);

			return;
		}
		fte->feederclass = fc;
		fte->desc = NULL;
		fte->idx = feedercnt;
		SLIST_INSERT_HEAD(&feedertab, fte, link);
		feedercnt++;

		/* initialize global variables */

		if (snd_verbose < 0 || snd_verbose > 4)
			snd_verbose = 1;

		/* initialize unit numbering */
		snd_unit_init();
		if (snd_unit < 0 || snd_unit > PCMMAXUNIT)
			snd_unit = -1;
		
		if (snd_maxautovchans < 0 ||
		    snd_maxautovchans > SND_MAXVCHANS)
			snd_maxautovchans = 0;

		if (chn_latency < CHN_LATENCY_MIN ||
		    chn_latency > CHN_LATENCY_MAX)
			chn_latency = CHN_LATENCY_DEFAULT;

		if (chn_latency_profile < CHN_LATENCY_PROFILE_MIN ||
		    chn_latency_profile > CHN_LATENCY_PROFILE_MAX)
			chn_latency_profile = CHN_LATENCY_PROFILE_DEFAULT;

		if (feeder_rate_min < FEEDRATE_MIN ||
			    feeder_rate_max < FEEDRATE_MIN ||
			    feeder_rate_min > FEEDRATE_MAX ||
			    feeder_rate_max > FEEDRATE_MAX ||
			    !(feeder_rate_min < feeder_rate_max)) {
			feeder_rate_min = FEEDRATE_RATEMIN;
			feeder_rate_max = FEEDRATE_RATEMAX;
		}

		if (feeder_rate_round < FEEDRATE_ROUNDHZ_MIN ||
		    	    feeder_rate_round > FEEDRATE_ROUNDHZ_MAX)
			feeder_rate_round = FEEDRATE_ROUNDHZ;

		if (bootverbose)
			printf("%s: snd_unit=%d snd_maxautovchans=%d "
			    "latency=%d "
			    "feeder_rate_min=%d feeder_rate_max=%d "
			    "feeder_rate_round=%d\n",
			    __func__, snd_unit, snd_maxautovchans,
			    chn_latency,
			    feeder_rate_min, feeder_rate_max,
			    feeder_rate_round);

		/* we've got our root feeder so don't veto pcm loading anymore */
		pcm_veto_load = 0;

		return;
	}

	KASSERT(fc->desc != NULL, ("feeder '%s' has no descriptor", fc->name));

	/* beyond this point failure is non-fatal but may result in some translations being unavailable */
	i = 0;
	while ((feedercnt < MAXFEEDERS) && (fc->desc[i].type > 0)) {
		/* printf("adding feeder %s, %x -> %x\n", fc->name, fc->desc[i].in, fc->desc[i].out); */
		fte = malloc(sizeof(*fte), M_FEEDER, M_NOWAIT | M_ZERO);
		if (fte == NULL) {
			printf("can't allocate memory for feeder '%s', %x -> %x\n", fc->name, fc->desc[i].in, fc->desc[i].out);

			return;
		}
		fte->feederclass = fc;
		fte->desc = &fc->desc[i];
		fte->idx = feedercnt;
		fte->desc->idx = feedercnt;
		SLIST_INSERT_HEAD(&feedertab, fte, link);
		i++;
	}
	feedercnt++;
	if (feedercnt >= MAXFEEDERS)
		printf("MAXFEEDERS (%d >= %d) exceeded\n", feedercnt, MAXFEEDERS);
}

static void
feeder_unregisterall(void *p)
{
	struct feedertab_entry *fte, *next;

	next = SLIST_FIRST(&feedertab);
	while (next != NULL) {
		fte = next;
		next = SLIST_NEXT(fte, link);
		free(fte, M_FEEDER);
	}
}

static int
cmpdesc(struct pcm_feederdesc *n, struct pcm_feederdesc *m)
{
	return ((n->type == m->type) &&
		((n->in == 0) || (n->in == m->in)) &&
		((n->out == 0) || (n->out == m->out)) &&
		(n->flags == m->flags));
}

static void
feeder_destroy(struct pcm_feeder *f)
{
	FEEDER_FREE(f);
	kobj_delete((kobj_t)f, M_FEEDER);
}

static struct pcm_feeder *
feeder_create(struct feeder_class *fc, struct pcm_feederdesc *desc)
{
	struct pcm_feeder *f;
	int err;

	f = (struct pcm_feeder *)kobj_create((kobj_class_t)fc, M_FEEDER, M_NOWAIT | M_ZERO);
	if (f == NULL)
		return NULL;

	f->data = fc->data;
	f->source = NULL;
	f->parent = NULL;
	f->class = fc;
	f->desc = &(f->desc_static);

	if (desc) {
		*(f->desc) = *desc;
	} else {
		f->desc->type = FEEDER_ROOT;
		f->desc->in = 0;
		f->desc->out = 0;
		f->desc->flags = 0;
		f->desc->idx = 0;
	}

	err = FEEDER_INIT(f);
	if (err) {
		printf("feeder_init(%p) on %s returned %d\n", f, fc->name, err);
		feeder_destroy(f);

		return NULL;
	}

	return f;
}

struct feeder_class *
feeder_getclass(struct pcm_feederdesc *desc)
{
	struct feedertab_entry *fte;

	SLIST_FOREACH(fte, &feedertab, link) {
		if ((desc == NULL) && (fte->desc == NULL))
			return fte->feederclass;
		if ((fte->desc != NULL) && (desc != NULL) && cmpdesc(desc, fte->desc))
			return fte->feederclass;
	}
	return NULL;
}

int
chn_addfeeder(struct pcm_channel *c, struct feeder_class *fc, struct pcm_feederdesc *desc)
{
	struct pcm_feeder *nf;

	nf = feeder_create(fc, desc);
	if (nf == NULL)
		return ENOSPC;

	nf->source = c->feeder;

	if (c->feeder != NULL)
		c->feeder->parent = nf;
	c->feeder = nf;

	return 0;
}

int
chn_removefeeder(struct pcm_channel *c)
{
	struct pcm_feeder *f;

	if (c->feeder == NULL)
		return -1;
	f = c->feeder;
	c->feeder = c->feeder->source;
	feeder_destroy(f);

	return 0;
}

struct pcm_feeder *
chn_findfeeder(struct pcm_channel *c, u_int32_t type)
{
	struct pcm_feeder *f;

	f = c->feeder;
	while (f != NULL) {
		if (f->desc->type == type)
			return f;
		f = f->source;
	}

	return NULL;
}

/*
 * 14bit format scoring
 * --------------------
 *
 *  13  12  11  10   9   8        2        1   0    offset
 * +---+---+---+---+---+---+-------------+---+---+
 * | X | X | X | X | X | X | X X X X X X | X | X |
 * +---+---+---+---+---+---+-------------+---+---+
 *   |   |   |   |   |   |        |        |   |
 *   |   |   |   |   |   |        |        |   +--> signed?
 *   |   |   |   |   |   |        |        |
 *   |   |   |   |   |   |        |        +------> bigendian?
 *   |   |   |   |   |   |        |
 *   |   |   |   |   |   |        +---------------> total channels
 *   |   |   |   |   |   |
 *   |   |   |   |   |   +------------------------> AFMT_A_LAW
 *   |   |   |   |   |
 *   |   |   |   |   +----------------------------> AFMT_MU_LAW
 *   |   |   |   |
 *   |   |   |   +--------------------------------> AFMT_8BIT
 *   |   |   |
 *   |   |   +------------------------------------> AFMT_16BIT
 *   |   |
 *   |   +----------------------------------------> AFMT_24BIT
 *   |
 *   +--------------------------------------------> AFMT_32BIT
 */
#define score_signeq(s1, s2)	(((s1) & 0x1) == ((s2) & 0x1))
#define score_endianeq(s1, s2)	(((s1) & 0x2) == ((s2) & 0x2))
#define score_cheq(s1, s2)	(((s1) & 0xfc) == ((s2) & 0xfc))
#define score_chgt(s1, s2)	(((s1) & 0xfc) > ((s2) & 0xfc))
#define score_chlt(s1, s2)	(((s1) & 0xfc) < ((s2) & 0xfc))
#define score_val(s1)		((s1) & 0x3f00)
#define score_cse(s1)		((s1) & 0x7f)

u_int32_t
snd_fmtscore(u_int32_t fmt)
{
	u_int32_t ret;

	ret = 0;
	if (fmt & AFMT_SIGNED)
		ret |= 1 << 0;
	if (fmt & AFMT_BIGENDIAN)
		ret |= 1 << 1;
	/*if (fmt & AFMT_STEREO)
		ret |= (2 & 0x3f) << 2;
	else
		ret |= (1 & 0x3f) << 2;*/
	ret |= (AFMT_CHANNEL(fmt) & 0x3f) << 2;
	if (fmt & AFMT_A_LAW)
		ret |= 1 << 8;
	else if (fmt & AFMT_MU_LAW)
		ret |= 1 << 9;
	else if (fmt & AFMT_8BIT)
		ret |= 1 << 10;
	else if (fmt & AFMT_16BIT)
		ret |= 1 << 11;
	else if (fmt & AFMT_24BIT)
		ret |= 1 << 12;
	else if (fmt & AFMT_32BIT)
		ret |= 1 << 13;

	return ret;
}

static u_int32_t
snd_fmtbestfunc(u_int32_t fmt, u_int32_t *fmts, int cheq)
{
	u_int32_t best, score, score2, oldscore;
	int i;

	if (fmt == 0 || fmts == NULL || fmts[0] == 0)
		return 0;

	if (snd_fmtvalid(fmt, fmts))
		return fmt;

	best = 0;
	score = snd_fmtscore(fmt);
	oldscore = 0;
	for (i = 0; fmts[i] != 0; i++) {
		score2 = snd_fmtscore(fmts[i]);
		if (cheq && !score_cheq(score, score2) &&
		    (score_chlt(score2, score) ||
		    (oldscore != 0 && score_chgt(score2, oldscore))))
				continue;
		if (oldscore == 0 ||
			    (score_val(score2) == score_val(score)) ||
			    (score_val(score2) == score_val(oldscore)) ||
			    (score_val(score2) > score_val(oldscore) &&
			    score_val(score2) < score_val(score)) ||
			    (score_val(score2) < score_val(oldscore) &&
			    score_val(score2) > score_val(score)) ||
			    (score_val(oldscore) < score_val(score) &&
			    score_val(score2) > score_val(oldscore))) {
			if (score_val(oldscore) != score_val(score2) ||
				    score_cse(score) == score_cse(score2) ||
				    ((score_cse(oldscore) != score_cse(score) &&
				    !score_endianeq(score, oldscore) &&
				    (score_endianeq(score, score2) ||
				    (!score_signeq(score, oldscore) &&
				    score_signeq(score, score2)))))) {
				best = fmts[i];
				oldscore = score2;
			}
		}
	}
	return best;
}

u_int32_t
snd_fmtbestbit(u_int32_t fmt, u_int32_t *fmts)
{
	return snd_fmtbestfunc(fmt, fmts, 0);
}

u_int32_t
snd_fmtbestchannel(u_int32_t fmt, u_int32_t *fmts)
{
	return snd_fmtbestfunc(fmt, fmts, 1);
}

u_int32_t
snd_fmtbest(u_int32_t fmt, u_int32_t *fmts)
{
	u_int32_t best1, best2;
	u_int32_t score, score1, score2;

	if (snd_fmtvalid(fmt, fmts))
		return fmt;

	best1 = snd_fmtbestchannel(fmt, fmts);
	best2 = snd_fmtbestbit(fmt, fmts);

	if (best1 != 0 && best2 != 0 && best1 != best2) {
		/*if (fmt & AFMT_STEREO)*/
		if (AFMT_CHANNEL(fmt) > 1)
			return best1;
		else {
			score = score_val(snd_fmtscore(fmt));
			score1 = score_val(snd_fmtscore(best1));
			score2 = score_val(snd_fmtscore(best2));
			if (score1 == score2 || score1 == score)
				return best1;
			else if (score2 == score)
				return best2;
			else if (score1 > score2)
				return best1;
			return best2;
		}
	} else if (best2 == 0)
		return best1;
	else
		return best2;
}

void
feeder_printchain(struct pcm_feeder *head)
{
	struct pcm_feeder *f;

	printf("feeder chain (head @%p)\n", head);
	f = head;
	while (f != NULL) {
		printf("%s/%d @ %p\n", f->class->name, f->desc->idx, f);
		f = f->source;
	}
	printf("[end]\n\n");
}

/*****************************************************************************/

static int
feed_root(struct pcm_feeder *feeder, struct pcm_channel *ch, u_int8_t *buffer, u_int32_t count, void *source)
{
	struct snd_dbuf *src = source;
	int l, offset;

	KASSERT(count > 0, ("feed_root: count == 0"));

	if (++ch->feedcount == 0)
		ch->feedcount = 2;

	l = min(count, sndbuf_getready(src));

	/* When recording only return as much data as available */
	if (ch->direction == PCMDIR_REC) {
		sndbuf_dispose(src, buffer, l);
		return l;
	}


	offset = count - l;

	if (offset > 0) {
		if (snd_verbose > 3)
			printf("%s: (%s) %spending %d bytes "
			    "(count=%d l=%d feed=%d)\n",
			    __func__,
			    (ch->flags & CHN_F_VIRTUAL) ? "virtual" : "hardware",
			    (ch->feedcount == 1) ? "pre" : "ap",
			    offset, count, l, ch->feedcount);

		if (ch->feedcount == 1) {
			memset(buffer,
			    sndbuf_zerodata(sndbuf_getfmt(src)),
			    offset);
			if (l > 0)
				sndbuf_dispose(src, buffer + offset, l);
			else
				ch->feedcount--;
		} else {
			if (l > 0)
				sndbuf_dispose(src, buffer, l);
			memset(buffer + l,
			    sndbuf_zerodata(sndbuf_getfmt(src)),
			    offset);
			if (!(ch->flags & CHN_F_CLOSING))
				ch->xruns++;
		}
	} else if (l > 0)
		sndbuf_dispose(src, buffer, l);

	return count;
}

static kobj_method_t feeder_root_methods[] = {
    	KOBJMETHOD(feeder_feed,		feed_root),
	KOBJMETHOD_END
};
static struct feeder_class feeder_root_class = {
	.name =		"feeder_root",
	.methods =	feeder_root_methods,
	.size =		sizeof(struct pcm_feeder),
	.desc =		NULL,
	.data =		NULL,
};
SYSINIT(feeder_root, SI_SUB_DRIVERS, SI_ORDER_FIRST, feeder_register, &feeder_root_class);
SYSUNINIT(feeder_root, SI_SUB_DRIVERS, SI_ORDER_FIRST, feeder_unregisterall, NULL);
