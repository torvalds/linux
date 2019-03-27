/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2009-2012 Semihalf
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

#ifndef _NANDSIM_SWAP_CHIP_H_
#define _NANDSIM_SWAP_CHIP_H_

struct block_space {
	SLIST_ENTRY(block_space)	free_link;
	STAILQ_ENTRY(block_space)	used_link;
	struct block_state		*blk_state;
	uint8_t				*blk_ptr;
};

#define	BLOCK_ALLOCATED	0x1
#define	BLOCK_SWAPPED	0x2
#define	BLOCK_DIRTY	0x4

struct block_state {
	struct block_space	*blk_sp;
	uint32_t		offset;
	uint8_t			status;
};

struct chip_swap {
	struct block_state		*blk_state;
	SLIST_HEAD(,block_space)	free_bs;
	STAILQ_HEAD(,block_space)	used_bs;
	struct ucred			*swap_cred;
	struct vnode			*swap_vp;
	uint32_t			swap_offset;
	uint32_t			blk_size;
	uint32_t			nof_blks;
};

struct chip_swap *nandsim_swap_init(const char *, uint32_t, uint32_t);
void nandsim_swap_destroy(struct chip_swap *);
struct block_space *get_bs(struct chip_swap *, uint32_t, uint8_t);

#endif /* _NANDSIM_SWAP_CHIP_H_ */
