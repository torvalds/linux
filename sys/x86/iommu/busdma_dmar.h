/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 The FreeBSD Foundation
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

#ifndef __X86_IOMMU_BUSDMA_DMAR_H
#define __X86_IOMMU_BUSDMA_DMAR_H

struct dmar_map_entry;
TAILQ_HEAD(dmar_map_entries_tailq, dmar_map_entry);

struct bus_dma_tag_dmar {
	struct bus_dma_tag_common common;
	struct dmar_ctx *ctx;
	device_t owner;
	int map_count;
	bus_dma_segment_t *segments;
};

struct bus_dmamap_dmar {
	struct bus_dma_tag_dmar *tag;
	struct memdesc mem;
	bus_dmamap_callback_t *callback;
	void *callback_arg;
	struct dmar_map_entries_tailq map_entries;
	TAILQ_ENTRY(bus_dmamap_dmar) delay_link;
	bool locked;
	bool cansleep;
	int flags;
};

#define	BUS_DMAMAP_DMAR_MALLOC	0x0001
#define	BUS_DMAMAP_DMAR_KMEM_ALLOC 0x0002

extern struct bus_dma_impl bus_dma_dmar_impl;

bus_dma_tag_t dmar_get_dma_tag(device_t dev, device_t child);

#endif
