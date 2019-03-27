/*	$NetBSD: cache_mipsNN.h,v 1.4 2003/02/17 11:35:02 simonb Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright 2002 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Simon Burge for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef	_MACHINE_CACHE_MIPSNN_H_
#define	_MACHINE_CACHE_MIPSNN_H_

void	mipsNN_cache_init(struct mips_cpuinfo *);

void	mipsNN_icache_sync_all_16(void);
void	mipsNN_icache_sync_all_32(void);
void	mipsNN_icache_sync_all_64(void);
void	mipsNN_icache_sync_all_128(void);
void	mipsNN_icache_sync_range_16(vm_offset_t, vm_size_t);
void	mipsNN_icache_sync_range_32(vm_offset_t, vm_size_t);
void	mipsNN_icache_sync_range_64(vm_offset_t, vm_size_t);
void	mipsNN_icache_sync_range_128(vm_offset_t, vm_size_t);
void	mipsNN_icache_sync_range_index_16(vm_offset_t, vm_size_t);
void	mipsNN_icache_sync_range_index_32(vm_offset_t, vm_size_t);
void	mipsNN_icache_sync_range_index_64(vm_offset_t, vm_size_t);
void	mipsNN_icache_sync_range_index_128(vm_offset_t, vm_size_t);
void	mipsNN_pdcache_wbinv_all_16(void);
void	mipsNN_pdcache_wbinv_all_32(void);
void	mipsNN_pdcache_wbinv_all_64(void);
void	mipsNN_pdcache_wbinv_all_128(void);
void	mipsNN_pdcache_wbinv_range_16(vm_offset_t, vm_size_t);
void	mipsNN_pdcache_wbinv_range_32(vm_offset_t, vm_size_t);
void	mipsNN_pdcache_wbinv_range_64(vm_offset_t, vm_size_t);
void	mipsNN_pdcache_wbinv_range_128(vm_offset_t, vm_size_t);
void	mipsNN_pdcache_wbinv_range_index_16(vm_offset_t, vm_size_t);
void	mipsNN_pdcache_wbinv_range_index_32(vm_offset_t, vm_size_t);
void	mipsNN_pdcache_wbinv_range_index_64(vm_offset_t, vm_size_t);
void	mipsNN_pdcache_wbinv_range_index_128(vm_offset_t, vm_size_t);
void	mipsNN_pdcache_inv_range_16(vm_offset_t, vm_size_t);
void	mipsNN_pdcache_inv_range_32(vm_offset_t, vm_size_t);
void	mipsNN_pdcache_inv_range_64(vm_offset_t, vm_size_t);
void	mipsNN_pdcache_inv_range_128(vm_offset_t, vm_size_t);
void	mipsNN_pdcache_wb_range_16(vm_offset_t, vm_size_t);
void	mipsNN_pdcache_wb_range_32(vm_offset_t, vm_size_t);
void	mipsNN_pdcache_wb_range_64(vm_offset_t, vm_size_t);
void	mipsNN_pdcache_wb_range_128(vm_offset_t, vm_size_t);
void	mipsNN_sdcache_wbinv_all_32(void);
void	mipsNN_sdcache_wbinv_all_64(void);
void	mipsNN_sdcache_wbinv_all_128(void);
void	mipsNN_sdcache_wbinv_range_32(vm_offset_t, vm_size_t);
void	mipsNN_sdcache_wbinv_range_64(vm_offset_t, vm_size_t);
void	mipsNN_sdcache_wbinv_range_128(vm_offset_t, vm_size_t);
void	mipsNN_sdcache_wbinv_range_index_32(vm_offset_t, vm_size_t);
void	mipsNN_sdcache_wbinv_range_index_64(vm_offset_t, vm_size_t);
void	mipsNN_sdcache_wbinv_range_index_128(vm_offset_t, vm_size_t);
void	mipsNN_sdcache_inv_range_32(vm_offset_t, vm_size_t);
void	mipsNN_sdcache_inv_range_64(vm_offset_t, vm_size_t);
void	mipsNN_sdcache_inv_range_128(vm_offset_t, vm_size_t);
void	mipsNN_sdcache_wb_range_32(vm_offset_t, vm_size_t);
void	mipsNN_sdcache_wb_range_64(vm_offset_t, vm_size_t);
void	mipsNN_sdcache_wb_range_128(vm_offset_t, vm_size_t);

#endif	/* _MACHINE_CACHE_MIPSNN_H_ */
