/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2009 Ariff Abdullah <ariff@FreeBSD.org>
 * Copyright (c) 2001 Cameron Grant <cg@FreeBSD.org>
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

#ifndef _SND_VCHAN_H_
#define _SND_VCHAN_H_

int vchan_create(struct pcm_channel *, int);
int vchan_destroy(struct pcm_channel *);

#ifdef SND_DEBUG
int vchan_passthrough(struct pcm_channel *, const char *);
#define vchan_sync(c)		vchan_passthrough(c, __func__)
#else
int vchan_sync(struct pcm_channel *);
#endif

#define VCHAN_SYNC_REQUIRED(c)						\
	(((c)->flags & CHN_F_VIRTUAL) && (((c)->flags & CHN_F_DIRTY) ||	\
	sndbuf_getfmt((c)->bufhard) != (c)->parentchannel->format ||	\
	sndbuf_getspd((c)->bufhard) != (c)->parentchannel->speed))

void vchan_initsys(device_t);

/*
 * Default format / rate
 */
#define VCHAN_DEFAULT_FORMAT	SND_FORMAT(AFMT_S16_LE, 2, 0)
#define VCHAN_DEFAULT_RATE	48000

#define VCHAN_PLAY		0
#define VCHAN_REC		1

/*
 * Offset by +/- 1 so we can distinguish bogus pointer.
 */
#define VCHAN_SYSCTL_DATA(x, y)						\
		((void *)((intptr_t)(((((x) + 1) & 0xfff) << 2) |	\
		(((VCHAN_##y) + 1) & 0x3))))

#define VCHAN_SYSCTL_DATA_SIZE	sizeof(void *)
#define VCHAN_SYSCTL_UNIT(x)	((int)(((intptr_t)(x) >> 2) & 0xfff) - 1)
#define VCHAN_SYSCTL_DIR(x)	((int)((intptr_t)(x) & 0x3) - 1)

#endif	/* _SND_VCHAN_H_ */
