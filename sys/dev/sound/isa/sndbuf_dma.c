/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Cameron Grant <cg@freebsd.org>
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

#include <isa/isavar.h>

SND_DECLARE_FILE("$FreeBSD$");

int
sndbuf_dmasetup(struct snd_dbuf *b, struct resource *drq)
{
	/* should do isa_dma_acquire/isa_dma_release here */
	if (drq == NULL) {
		b->dmachan = -1;
	} else {
		sndbuf_setflags(b, SNDBUF_F_DMA, 1);
		b->dmachan = rman_get_start(drq);
	}
	return 0;
}

int
sndbuf_dmasetdir(struct snd_dbuf *b, int dir)
{
	KASSERT(b, ("sndbuf_dmasetdir called with b == NULL"));
	KASSERT(sndbuf_getflags(b) & SNDBUF_F_DMA, ("sndbuf_dmasetdir called on non-ISA buffer"));

	b->dir = (dir == PCMDIR_PLAY)? ISADMA_WRITE : ISADMA_READ;
	return 0;
}

void
sndbuf_dma(struct snd_dbuf *b, int go)
{
	KASSERT(b, ("sndbuf_dma called with b == NULL"));
	KASSERT(sndbuf_getflags(b) & SNDBUF_F_DMA, ("sndbuf_dma called on non-ISA buffer"));

	switch (go) {
	case PCMTRIG_START:
		/* isa_dmainit(b->chan, size); */
		isa_dmastart(b->dir | ISADMA_RAW, b->buf, b->bufsize, b->dmachan);
		break;

	case PCMTRIG_STOP:
	case PCMTRIG_ABORT:
		isa_dmastop(b->dmachan);
		isa_dmadone(b->dir | ISADMA_RAW, b->buf, b->bufsize, b->dmachan);
		break;
	}

	DEB(printf("buf 0x%p ISA DMA %s, channel %d\n",
		b,
		(go == PCMTRIG_START)? "started" : "stopped",
		b->dmachan));
}

int
sndbuf_dmaptr(struct snd_dbuf *b)
{
	int i;

	KASSERT(b, ("sndbuf_dmaptr called with b == NULL"));
	KASSERT(sndbuf_getflags(b) & SNDBUF_F_DMA, ("sndbuf_dmaptr called on non-ISA buffer"));

	if (!sndbuf_runsz(b))
		return 0;
	i = isa_dmastatus(b->dmachan);
	KASSERT(i >= 0, ("isa_dmastatus returned %d", i));
	return b->bufsize - i;
}

void
sndbuf_dmabounce(struct snd_dbuf *b)
{
	KASSERT(b, ("sndbuf_dmabounce called with b == NULL"));
	KASSERT(sndbuf_getflags(b) & SNDBUF_F_DMA, ("sndbuf_dmabounce called on non-ISA buffer"));

	/* tell isa_dma to bounce data in/out */
}
