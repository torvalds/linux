/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Mathew Kanner
 * Copyright (c) 1999 Seigo Tanimura
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

/*
 * Include file for the midi sequence driver.
 */

#ifndef _SEQUENCER_H_
#define _SEQUENCER_H_


#define NSEQ_MAX	16

/*
 * many variables should be reduced to a range. Here define a macro
 */

#define RANGE(var, low, high) (var) = \
((var)<(low)?(low) : (var)>(high)?(high) : (var))

#ifdef _KERNEL

void	seq_timer(void *arg);

SYSCTL_DECL(_hw_midi_seq);

extern int seq_debug;

#define SEQ_DEBUG(y, x)			\
	do {				\
		if (seq_debug >= y) {	\
			(x);		\
		}			\
	} while (0)

SYSCTL_DECL(_hw_midi);

#endif					/* _KERNEL */

#define SYNTHPROP_MIDI		1
#define SYNTHPROP_SYNTH		2
#define SYNTHPROP_RX		4
#define SYNTHPROP_TX		8

struct _midi_cmdtab {
	int	cmd;
	char   *name;
};
typedef struct _midi_cmdtab midi_cmdtab;
extern midi_cmdtab cmdtab_seqevent[];
extern midi_cmdtab cmdtab_seqioctl[];
extern midi_cmdtab cmdtab_timer[];
extern midi_cmdtab cmdtab_seqcv[];
extern midi_cmdtab cmdtab_seqccmn[];

char   *midi_cmdname(int cmd, midi_cmdtab * tab);

enum {
	MORE,
	TIMERARMED,
	QUEUEFULL
};

#endif
