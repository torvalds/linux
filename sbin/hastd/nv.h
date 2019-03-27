/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2010 The FreeBSD Foundation
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

#ifndef	_NV_H_
#define	_NV_H_

#include <sys/cdefs.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <ebuf.h>

struct nv;

struct nv *nv_alloc(void);
void nv_free(struct nv *nv);
int nv_error(const struct nv *nv);
int nv_set_error(struct nv *nv, int error);
int nv_validate(struct nv *nv, size_t *extrap);

struct ebuf *nv_hton(struct nv *nv);
struct nv *nv_ntoh(struct ebuf *eb);

void nv_add_int8(struct nv *nv, int8_t value, const char *namefmt, ...)
    __printflike(3, 4);
void nv_add_uint8(struct nv *nv, uint8_t value, const char *namefmt, ...)
    __printflike(3, 4);
void nv_add_int16(struct nv *nv, int16_t value, const char *namefmt, ...)
    __printflike(3, 4);
void nv_add_uint16(struct nv *nv, uint16_t value, const char *namefmt, ...)
    __printflike(3, 4);
void nv_add_int32(struct nv *nv, int32_t value, const char *namefmt, ...)
    __printflike(3, 4);
void nv_add_uint32(struct nv *nv, uint32_t value, const char *namefmt, ...)
    __printflike(3, 4);
void nv_add_int64(struct nv *nv, int64_t value, const char *namefmt, ...)
    __printflike(3, 4);
void nv_add_uint64(struct nv *nv, uint64_t value, const char *namefmt, ...)
    __printflike(3, 4);
void nv_add_int8_array(struct nv *nv, const int8_t *value, size_t size,
    const char *namefmt, ...) __printflike(4, 5);
void nv_add_uint8_array(struct nv *nv, const uint8_t *value, size_t size,
    const char *namefmt, ...) __printflike(4, 5);
void nv_add_int16_array(struct nv *nv, const int16_t *value, size_t size,
    const char *namefmt, ...) __printflike(4, 5);
void nv_add_uint16_array(struct nv *nv, const uint16_t *value, size_t size,
    const char *namefmt, ...) __printflike(4, 5);
void nv_add_int32_array(struct nv *nv, const int32_t *value, size_t size,
    const char *namefmt, ...) __printflike(4, 5);
void nv_add_uint32_array(struct nv *nv, const uint32_t *value, size_t size,
    const char *namefmt, ...) __printflike(4, 5);
void nv_add_int64_array(struct nv *nv, const int64_t *value, size_t size,
    const char *namefmt, ...) __printflike(4, 5);
void nv_add_uint64_array(struct nv *nv, const uint64_t *value, size_t size,
    const char *namefmt, ...) __printflike(4, 5);
void nv_add_string(struct nv *nv, const char *value, const char *namefmt, ...)
    __printflike(3, 4);
void nv_add_stringf(struct nv *nv, const char *name, const char *valuefmt, ...)
    __printflike(3, 4);
void nv_add_stringv(struct nv *nv, const char *name, const char *valuefmt,
    va_list valueap) __printflike(3, 0);

int8_t nv_get_int8(struct nv *nv, const char *namefmt, ...)
    __printflike(2, 3);
uint8_t nv_get_uint8(struct nv *nv, const char *namefmt, ...)
    __printflike(2, 3);
int16_t nv_get_int16(struct nv *nv, const char *namefmt, ...)
    __printflike(2, 3);
uint16_t nv_get_uint16(struct nv *nv, const char *namefmt, ...)
    __printflike(2, 3);
int32_t nv_get_int32(struct nv *nv, const char *namefmt, ...)
    __printflike(2, 3);
uint32_t nv_get_uint32(struct nv *nv, const char *namefmt, ...)
    __printflike(2, 3);
int64_t nv_get_int64(struct nv *nv, const char *namefmt, ...)
    __printflike(2, 3);
uint64_t nv_get_uint64(struct nv *nv, const char *namefmt, ...)
    __printflike(2, 3);
const int8_t *nv_get_int8_array(struct nv *nv, size_t *sizep,
    const char *namefmt, ...) __printflike(3, 4);
const uint8_t *nv_get_uint8_array(struct nv *nv, size_t *sizep,
    const char *namefmt, ...) __printflike(3, 4);
const int16_t *nv_get_int16_array(struct nv *nv, size_t *sizep,
    const char *namefmt, ...) __printflike(3, 4);
const uint16_t *nv_get_uint16_array(struct nv *nv, size_t *sizep,
    const char *namefmt, ...) __printflike(3, 4);
const int32_t *nv_get_int32_array(struct nv *nv, size_t *sizep,
    const char *namefmt, ...) __printflike(3, 4);
const uint32_t *nv_get_uint32_array(struct nv *nv, size_t *sizep,
    const char *namefmt, ...) __printflike(3, 4);
const int64_t *nv_get_int64_array(struct nv *nv, size_t *sizep,
    const char *namefmt, ...) __printflike(3, 4);
const uint64_t *nv_get_uint64_array(struct nv *nv, size_t *sizep,
    const char *namefmt, ...) __printflike(3, 4);
const char *nv_get_string(struct nv *nv, const char *namefmt, ...)
    __printflike(2, 3);

bool nv_exists(struct nv *nv, const char *namefmt, ...) __printflike(2, 3);
void nv_assert(struct nv *nv, const char *namefmt, ...) __printflike(2, 3);
void nv_dump(struct nv *nv);

#endif	/* !_NV_H_ */
