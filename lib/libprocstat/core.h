/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Mikolaj Golub <trociny@FreeBSD.org>
 * Copyright (c) 2017 Dell EMC
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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

#ifndef _CORE_H
#define _CORE_H

enum psc_type {
	PSC_TYPE_PROC,
	PSC_TYPE_FILES,
	PSC_TYPE_VMMAP,
	PSC_TYPE_GROUPS,
	PSC_TYPE_UMASK,
	PSC_TYPE_RLIMIT,
	PSC_TYPE_OSREL,
	PSC_TYPE_PSSTRINGS,
	PSC_TYPE_ARGV,
	PSC_TYPE_ENVV,
	PSC_TYPE_AUXV,
	PSC_TYPE_PTLWPINFO,
	PSC_TYPE_MAX
};

struct procstat_core;

void procstat_core_close(struct procstat_core *core);
void *procstat_core_get(struct procstat_core *core, enum psc_type type,
    void * buf, size_t *lenp);
int procstat_core_note_count(struct procstat_core *core, enum psc_type type);
struct procstat_core *procstat_core_open(const char *filename);

#endif 	/* !_CORE_H_ */
