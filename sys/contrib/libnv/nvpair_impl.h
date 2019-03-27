/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2013 The FreeBSD Foundation
 * Copyright (c) 2013-2015 Mariusz Zaborski <oshogbo@FreeBSD.org>
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

#ifndef	_NVPAIR_IMPL_H_
#define	_NVPAIR_IMPL_H_

#include <sys/nv.h>
#include <sys/queue.h>

#ifndef _KERNEL
#include <stdint.h>
#endif

TAILQ_HEAD(nvl_head, nvpair);

void nvpair_assert(const nvpair_t *nvp);
nvlist_t *nvpair_nvlist(const nvpair_t *nvp);
nvpair_t *nvpair_next(const nvpair_t *nvp);
nvpair_t *nvpair_prev(const nvpair_t *nvp);
void nvpair_insert(struct nvl_head *head, nvpair_t *nvp, nvlist_t *nvl);
void nvpair_remove(struct nvl_head *head, nvpair_t *nvp, const nvlist_t *nvl);
size_t nvpair_header_size(void);
size_t nvpair_size(const nvpair_t *nvp);
const unsigned char *nvpair_unpack(bool isbe, const unsigned char *ptr,
    size_t *leftp, nvpair_t **nvpp);
void nvpair_free_structure(nvpair_t *nvp);
void nvpair_init_datasize(nvpair_t *nvp);
const char *nvpair_type_string(int type);

/* Pack functions. */
unsigned char *nvpair_pack_header(const nvpair_t *nvp, unsigned char *ptr,
    size_t *leftp);
unsigned char *nvpair_pack_null(const nvpair_t *nvp, unsigned char *ptr,
    size_t *leftp);
unsigned char *nvpair_pack_bool(const nvpair_t *nvp, unsigned char *ptr,
    size_t *leftp);
unsigned char *nvpair_pack_number(const nvpair_t *nvp, unsigned char *ptr,
    size_t *leftp);
unsigned char *nvpair_pack_string(const nvpair_t *nvp, unsigned char *ptr,
    size_t *leftp);
unsigned char *nvpair_pack_descriptor(const nvpair_t *nvp, unsigned char *ptr,
    int64_t *fdidxp, size_t *leftp);
unsigned char *nvpair_pack_binary(const nvpair_t *nvp, unsigned char *ptr,
    size_t *leftp);
unsigned char *nvpair_pack_nvlist_up(unsigned char *ptr, size_t *leftp);
unsigned char *nvpair_pack_bool_array(const nvpair_t *nvp, unsigned char *ptr,
    size_t *leftp);
unsigned char *nvpair_pack_number_array(const nvpair_t *nvp, unsigned char *ptr,
    size_t *leftp);
unsigned char *nvpair_pack_string_array(const nvpair_t *nvp, unsigned char *ptr,
    size_t *leftp);
unsigned char *nvpair_pack_descriptor_array(const nvpair_t *nvp,
    unsigned char *ptr, int64_t *fdidxp, size_t *leftp);
unsigned char *nvpair_pack_nvlist_array_next(unsigned char *ptr, size_t *leftp);

/* Unpack data functions. */
const unsigned char *nvpair_unpack_header(bool isbe, nvpair_t *nvp,
    const unsigned char *ptr, size_t *leftp);
const unsigned char *nvpair_unpack_null(bool isbe, nvpair_t *nvp,
    const unsigned char *ptr, size_t *leftp);
const unsigned char *nvpair_unpack_bool(bool isbe, nvpair_t *nvp,
    const unsigned char *ptr, size_t *leftp);
const unsigned char *nvpair_unpack_number(bool isbe, nvpair_t *nvp,
    const unsigned char *ptr, size_t *leftp);
const unsigned char *nvpair_unpack_string(bool isbe, nvpair_t *nvp,
    const unsigned char *ptr, size_t *leftp);
const unsigned char *nvpair_unpack_nvlist(bool isbe, nvpair_t *nvp,
    const unsigned char *ptr, size_t *leftp, size_t nfds, nvlist_t **child);
const unsigned char *nvpair_unpack_descriptor(bool isbe, nvpair_t *nvp,
    const unsigned char *ptr, size_t *leftp, const int *fds, size_t nfds);
const unsigned char *nvpair_unpack_binary(bool isbe, nvpair_t *nvp,
    const unsigned char *ptr, size_t *leftp);
const unsigned char *nvpair_unpack_bool_array(bool isbe, nvpair_t *nvp,
    const unsigned char *ptr, size_t *leftp);
const unsigned char *nvpair_unpack_number_array(bool isbe, nvpair_t *nvp,
    const unsigned char *ptr, size_t *leftp);
const unsigned char *nvpair_unpack_string_array(bool isbe, nvpair_t *nvp,
    const unsigned char *ptr, size_t *leftp);
const unsigned char *nvpair_unpack_descriptor_array(bool isbe, nvpair_t *nvp,
    const unsigned char *ptr, size_t *leftp, const int *fds, size_t nfds);
const unsigned char *nvpair_unpack_nvlist_array(bool isbe, nvpair_t *nvp,
    const unsigned char *ptr, size_t *leftp, nvlist_t **firstel);

#endif	/* !_NVPAIR_IMPL_H_ */
