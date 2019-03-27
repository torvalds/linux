/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Mathew Kanner
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

#ifndef MIDI_H
#define MIDI_H

#include <sys/types.h>
#include <sys/malloc.h>

MALLOC_DECLARE(M_MIDI);

#define M_RX		0x01
#define M_TX		0x02
#define M_RXEN		0x04
#define M_TXEN		0x08

#define MIDI_TYPE unsigned char

struct snd_midi;

struct snd_midi *
midi_init(kobj_class_t _mpu_cls, int _unit, int _channel, void *cookie);
int	midi_uninit(struct snd_midi *_m);
int	midi_out(struct snd_midi *_m, MIDI_TYPE *_buf, int _size);
int	midi_in(struct snd_midi *_m, MIDI_TYPE *_buf, int _size);

kobj_t	midimapper_addseq(void *arg1, int *unit, void **cookie);
int	midimapper_open(void *arg1, void **cookie);
int	midimapper_close(void *arg1, void *cookie);
kobj_t	midimapper_fetch_synth(void *arg, void *cookie, int unit);

#endif
