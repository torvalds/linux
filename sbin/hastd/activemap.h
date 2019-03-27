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

#ifndef	_ACTIVEMAP_H_
#define	_ACTIVEMAP_H_

#include <stdbool.h>
#include <stdint.h>

struct activemap;

int activemap_init(struct activemap **ampp, uint64_t mediasize,
    uint32_t extentsize, uint32_t sectorsize, uint32_t keepdirty);
void activemap_free(struct activemap *amp);

bool activemap_write_start(struct activemap *amp, off_t offset, off_t length);
bool activemap_write_complete(struct activemap *amp, off_t offset,
    off_t length);
bool activemap_extent_complete(struct activemap *amp, int extent);
uint64_t activemap_ndirty(const struct activemap *amp);

bool activemap_differ(const struct activemap *amp);
size_t activemap_size(const struct activemap *amp);
size_t activemap_ondisk_size(const struct activemap *amp);
void activemap_copyin(struct activemap *amp, const unsigned char *buf,
    size_t size);
void activemap_merge(struct activemap *amp, const unsigned char *buf,
    size_t size);
const unsigned char *activemap_bitmap(struct activemap *amp, size_t *sizep);

size_t activemap_calc_ondisk_size(uint64_t mediasize, uint32_t extentsize,
    uint32_t sectorsize);

void activemap_sync_rewind(struct activemap *amp);
off_t activemap_sync_offset(struct activemap *amp, off_t *lengthp,
    int *syncextp);
bool activemap_need_sync(struct activemap *amp, off_t offset, off_t length);

void activemap_dump(const struct activemap *amp);

#endif	/* !_ACTIVEMAP_H_ */
