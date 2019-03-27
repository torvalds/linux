/* $FreeBSD$ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2002 Hiroyuki Aizu <aizu@navi.org>
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

/* prototypes from "uaudio.c" used by "uaudio_pcm.c" */

#ifndef _UAUDIO_H_
#define	_UAUDIO_H_

struct uaudio_chan;
struct uaudio_softc;
struct snd_dbuf;
struct snd_mixer;
struct pcm_channel;

extern int	uaudio_attach_sub(device_t dev, kobj_class_t mixer_class,
		    kobj_class_t chan_class);
extern int	uaudio_detach_sub(device_t dev);
extern void	*uaudio_chan_init(struct uaudio_softc *sc, struct snd_dbuf *b,
		    struct pcm_channel *c, int dir);
extern int	uaudio_chan_free(struct uaudio_chan *ch);
extern int	uaudio_chan_set_param_blocksize(struct uaudio_chan *ch,
		    uint32_t blocksize);
extern int	uaudio_chan_set_param_fragments(struct uaudio_chan *ch,
		    uint32_t blocksize, uint32_t blockcount);
extern int	uaudio_chan_set_param_speed(struct uaudio_chan *ch,
		    uint32_t speed);
extern int	uaudio_chan_getptr(struct uaudio_chan *ch);
extern struct	pcmchan_caps *uaudio_chan_getcaps(struct uaudio_chan *ch);
extern struct	pcmchan_matrix *uaudio_chan_getmatrix(struct uaudio_chan *ch,
		    uint32_t format);
extern int	uaudio_chan_set_param_format(struct uaudio_chan *ch,
		    uint32_t format);
extern void	uaudio_chan_start(struct uaudio_chan *ch);
extern void	uaudio_chan_stop(struct uaudio_chan *ch);
extern int	uaudio_mixer_init_sub(struct uaudio_softc *sc,
		    struct snd_mixer *m);
extern int	uaudio_mixer_uninit_sub(struct uaudio_softc *sc);
extern void	uaudio_mixer_set(struct uaudio_softc *sc, unsigned type,
		    unsigned left, unsigned right);
extern uint32_t	uaudio_mixer_setrecsrc(struct uaudio_softc *sc, uint32_t src);

int	uaudio_get_vendor(device_t dev);
int	uaudio_get_product(device_t dev);
int	uaudio_get_release(device_t dev);

#endif			/* _UAUDIO_H_ */
