/* $OpenBSD: mvmbusvar.h,v 1.2 2018/07/09 09:24:22 patrick Exp $ */
/*
 * Copyright (c) 2016 Patrick Wildt <patrick@blueri.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define MVMBUS_NO_REMAP			0xffffffff

struct mbus_dram_info {
	uint8_t			 targetid;
	int			 numcs;
	struct mbus_dram_window {
		uint8_t		 index;
		uint8_t		 attr;
		uint32_t	 base;
		uint32_t	 size;
	}			 cs[4];
};

extern struct mbus_dram_info *mvmbus_dram_info;
extern uint32_t mvmbus_pcie_mem_aperture[2];
extern uint32_t mvmbus_pcie_io_aperture[2];

void mvmbus_add_window(paddr_t, size_t, paddr_t, uint8_t, uint8_t);
void mvmbus_del_window(paddr_t, size_t);
