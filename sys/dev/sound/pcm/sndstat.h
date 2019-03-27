/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2009 Ariff Abdullah <ariff@FreeBSD.org>
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

#ifndef _SND_SNDSTAT_H_
#define _SND_SNDSTAT_H_

#define SNDSTAT_PREPARE_PCM_ARGS					\
	struct sbuf *s, device_t dev, int verbose

#define SNDSTAT_PREPARE_PCM_BEGIN()	do {				\
	struct snddev_info *d;						\
	struct pcm_channel *c;						\
	struct pcm_feeder *f;						\
									\
	d = device_get_softc(dev);					\
	PCM_BUSYASSERT(d);						\
									\
	if (CHN_EMPTY(d, channels.pcm)) {				\
		sbuf_printf(s, " (mixer only)");			\
		return (0);						\
	}								\
									\
	if (verbose < 1) {						\
		sbuf_printf(s, " (%s%s%s",				\
		    d->playcount ? "play" : "",				\
		    (d->playcount && d->reccount) ? "/" : "",		\
		    d->reccount ? "rec" : "");				\
	} else {							\
		sbuf_printf(s, " (%dp:%dv/%dr:%dv",			\
		    d->playcount, d->pvchancount,			\
		    d->reccount, d->rvchancount);			\
	}								\
	sbuf_printf(s, "%s)%s",						\
	    ((d->playcount != 0 && d->reccount != 0) &&			\
	    (d->flags & SD_F_SIMPLEX)) ? " simplex" : "",		\
	    (device_get_unit(dev) == snd_unit) ? " default" : "")


#define SNDSTAT_PREPARE_PCM_END()					\
	if (verbose <= 1)						\
		return (0);						\
									\
	sbuf_printf(s, "\n\t");						\
	sbuf_printf(s, "snddev flags=0x%b", d->flags, SD_F_BITS);	\
									\
	CHN_FOREACH(c, d, channels.pcm) {				\
									\
		KASSERT(c->bufhard != NULL && c->bufsoft != NULL,	\
		    ("hosed pcm channel setup"));			\
									\
		sbuf_printf(s, "\n\t");					\
									\
		sbuf_printf(s, "%s[%s]: ",				\
		    (c->parentchannel != NULL) ?			\
		    c->parentchannel->name : "", c->name);		\
		sbuf_printf(s, "spd %d", c->speed);			\
		if (c->speed != sndbuf_getspd(c->bufhard))		\
			sbuf_printf(s, "/%d",				\
			    sndbuf_getspd(c->bufhard));			\
		sbuf_printf(s, ", fmt 0x%08x", c->format);		\
		if (c->format != sndbuf_getfmt(c->bufhard))		\
			sbuf_printf(s, "/0x%08x",			\
			    sndbuf_getfmt(c->bufhard));			\
		sbuf_printf(s, ", flags 0x%08x, 0x%08x",		\
		    c->flags, c->feederflags);				\
		if (c->pid != -1)					\
			sbuf_printf(s, ", pid %d (%s)",			\
			    c->pid, c->comm);				\
		sbuf_printf(s, "\n\t");					\
									\
		sbuf_printf(s, "interrupts %d, ", c->interrupts);	\
									\
		if (c->direction == PCMDIR_REC)				\
			sbuf_printf(s,					\
			    "overruns %d, feed %u, hfree %d, "		\
			    "sfree %d [b:%d/%d/%d|bs:%d/%d/%d]",	\
				c->xruns, c->feedcount,			\
				sndbuf_getfree(c->bufhard),		\
				sndbuf_getfree(c->bufsoft),		\
				sndbuf_getsize(c->bufhard),		\
				sndbuf_getblksz(c->bufhard),		\
				sndbuf_getblkcnt(c->bufhard),		\
				sndbuf_getsize(c->bufsoft),		\
				sndbuf_getblksz(c->bufsoft),		\
				sndbuf_getblkcnt(c->bufsoft));		\
		else							\
			sbuf_printf(s,					\
			    "underruns %d, feed %u, ready %d "		\
			    "[b:%d/%d/%d|bs:%d/%d/%d]",			\
				c->xruns, c->feedcount,			\
				sndbuf_getready(c->bufsoft),		\
				sndbuf_getsize(c->bufhard),		\
				sndbuf_getblksz(c->bufhard),		\
				sndbuf_getblkcnt(c->bufhard),		\
				sndbuf_getsize(c->bufsoft),		\
				sndbuf_getblksz(c->bufsoft),		\
				sndbuf_getblkcnt(c->bufsoft));		\
		sbuf_printf(s, "\n\t");					\
									\
		sbuf_printf(s, "channel flags=0x%b", c->flags,		\
		    CHN_F_BITS);					\
		sbuf_printf(s, "\n\t");					\
									\
		sbuf_printf(s, "{%s}",					\
		    (c->direction == PCMDIR_REC) ? "hardware" :		\
		    "userland");					\
		sbuf_printf(s, " -> ");					\
		f = c->feeder;						\
		while (f->source != NULL)				\
			f = f->source;					\
		while (f != NULL) {					\
			sbuf_printf(s, "%s", f->class->name);		\
			if (f->desc->type == FEEDER_FORMAT)		\
				sbuf_printf(s, "(0x%08x -> 0x%08x)",	\
				    f->desc->in, f->desc->out);		\
			else if (f->desc->type == FEEDER_MATRIX)	\
				sbuf_printf(s, "(%d.%d -> %d.%d)",	\
				    AFMT_CHANNEL(f->desc->in) -		\
				    AFMT_EXTCHANNEL(f->desc->in),	\
				    AFMT_EXTCHANNEL(f->desc->in),	\
				    AFMT_CHANNEL(f->desc->out) -	\
				    AFMT_EXTCHANNEL(f->desc->out),	\
				    AFMT_EXTCHANNEL(f->desc->out));	\
			else if (f->desc->type == FEEDER_RATE)		\
				sbuf_printf(s,				\
				    "(0x%08x q:%d %d -> %d)",		\
				    f->desc->out,			\
				    FEEDER_GET(f, FEEDRATE_QUALITY),	\
				    FEEDER_GET(f, FEEDRATE_SRC),	\
				    FEEDER_GET(f, FEEDRATE_DST));	\
			else						\
				sbuf_printf(s, "(0x%08x)",		\
				    f->desc->out);			\
			sbuf_printf(s, " -> ");				\
			f = f->parent;					\
		}							\
		sbuf_printf(s, "{%s}",					\
		    (c->direction == PCMDIR_REC) ? "userland" :		\
		    "hardware");					\
	}								\
									\
	return (0);							\
} while (0)

#endif	/* !_SND_SNDSTAT_H_ */
