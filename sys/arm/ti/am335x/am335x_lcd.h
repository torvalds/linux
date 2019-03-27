/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Oleksandr Tymoshenko <gonzo@freebsd.org>
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
#ifndef __AM335X_LCD_H__
#define __AM335X_LCD_H__

struct panel_info {
	/* Timing part */
	uint32_t panel_width;
	uint32_t panel_height;
	uint32_t panel_hfp;
	uint32_t panel_hbp;
	uint32_t panel_hsw;
	uint32_t panel_vfp;
	uint32_t panel_vbp;
	uint32_t panel_vsw;
	uint32_t hsync_active;
	uint32_t vsync_active;
	uint32_t panel_pxl_clk;

	uint32_t ac_bias;
	uint32_t ac_bias_intrpt;
	uint32_t dma_burst_sz;
	uint32_t bpp;
	uint32_t fdd;
	uint32_t sync_edge;
	uint32_t sync_ctrl;
	uint32_t pixelclk_active;
};

int am335x_lcd_syscons_setup(vm_offset_t vaddr, vm_paddr_t paddr,
    struct panel_info *panel);

#endif /* __AM335X_LCD_H__ */
