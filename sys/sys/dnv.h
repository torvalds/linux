/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#ifndef	_DNV_H_
#define	_DNV_H_

#include <sys/cdefs.h>

#ifndef _KERNEL
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

#ifndef	_NVLIST_T_DECLARED
#define	_NVLIST_T_DECLARED
struct nvlist;

typedef struct nvlist nvlist_t;
#endif

__BEGIN_DECLS

/*
 * The dnvlist_get functions returns value associated with the given name.
 * If it returns a pointer, the pointer represents internal buffer and should
 * not be freed by the caller.
 * If no element of the given name and type exists, the function will return
 * provided default value.
 */

bool dnvlist_get_bool(const nvlist_t *nvl, const char *name, bool defval);
uint64_t dnvlist_get_number(const nvlist_t *nvl, const char *name, uint64_t defval);
const char *dnvlist_get_string(const nvlist_t *nvl, const char *name, const char *defval);
const nvlist_t *dnvlist_get_nvlist(const nvlist_t *nvl, const char *name, const nvlist_t *defval);
int dnvlist_get_descriptor(const nvlist_t *nvl, const char *name, int defval);
const void *dnvlist_get_binary(const nvlist_t *nvl, const char *name, size_t *sizep, const void *defval, size_t defsize);

/*
 * The dnvlist_take functions returns value associated with the given name and
 * remove corresponding nvpair.
 * If it returns a pointer, the caller has to free it.
 * If no element of the given name and type exists, the function will return
 * provided default value.
 */

bool dnvlist_take_bool(nvlist_t *nvl, const char *name, bool defval);
uint64_t dnvlist_take_number(nvlist_t *nvl, const char *name, uint64_t defval);
char *dnvlist_take_string(nvlist_t *nvl, const char *name, char *defval);
nvlist_t *dnvlist_take_nvlist(nvlist_t *nvl, const char *name, nvlist_t *defval);
int dnvlist_take_descriptor(nvlist_t *nvl, const char *name, int defval);
void *dnvlist_take_binary(nvlist_t *nvl, const char *name, size_t *sizep, void *defval, size_t defsize);

__END_DECLS

#endif	/* !_DNV_H_ */
