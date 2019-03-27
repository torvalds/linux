/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2019 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov <kib@FreeBSD.org>
 * under sponsorship from the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef	_SYS_RANGESET_H
#define	_SYS_RANGESET_H

#ifdef	_KERNEL

#include <sys/_rangeset.h>

typedef bool (*rs_pred_t)(void *ctx, void *r);

/*
 * This structure must be embedded at the start of the rangeset element.
 */
struct rs_el {
	uint64_t	re_start;	/* pctrie key */
	uint64_t	re_end;
};

void	rangeset_init(struct rangeset *rs, rs_dup_data_t dup_data,
	    rs_free_data_t free_data, void *rs_data_ctx, u_int alloc_flags);
void	rangeset_fini(struct rangeset *rs);

bool	rangeset_check_empty(struct rangeset *rs, uint64_t start,
	    uint64_t end);

/*
 * r point to the app data with struct rs_el at the beginning.
 */
int	rangeset_insert(struct rangeset *rs, uint64_t start, uint64_t end,
	    void *r);

/*
 * Guarantees that on error the rangeset is not modified.  Remove
 * might need to split element if its start/end completely cover the
 * removed range, in which case ENOMEM might be returned.
 */
void	rangeset_remove_all(struct rangeset *rs);
int	rangeset_remove(struct rangeset *rs, uint64_t start, uint64_t end);
int	rangeset_remove_pred(struct rangeset *rs, uint64_t start,
	    uint64_t end, rs_pred_t pred);

/*
 * Really returns the pointer to the data with struct rs_el embedded
 * at the beginning.
 */
void	*rangeset_lookup(struct rangeset *rs, uint64_t place);

/*
 * Copies src_rs entries into dst_rs.  dst_rs must be empty.
 * Leaves dst_rs empty on failure.
 */
int	rangeset_copy(struct rangeset *dst_rs, struct rangeset *src_rs);

#endif

#endif
