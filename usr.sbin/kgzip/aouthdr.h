/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Robert Nordier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <a.out.h>
#include "kgz.h"

/* Relocatable header: part 0 */
struct kgz_aouthdr0 {
    struct exec a;
};

/* Symbol table entries */
#define KGZ__STNUM		2

/* Symbol table strings */
#define KGZ__STR_KGZ		"_kgz"
#define KGZ__STR_KGZ_NDATA	"_kgz_ndata"

/* String table */
struct kgz__strtab {
    unsigned long length;
    char kgz[sizeof(KGZ__STR_KGZ)];
    char kgz_ndata[sizeof(KGZ__STR_KGZ_NDATA)];
};

/* Relocatable header: part 1 */
struct kgz_aouthdr1 {
    struct nlist st[KGZ__STNUM];
    struct kgz__strtab strtab;
};

extern const struct kgz_aouthdr0 aouthdr0;
extern const struct kgz_aouthdr1 aouthdr1;
