/*	$OpenBSD: fw_cfg.h,v 1.3 2025/06/12 21:04:37 dv Exp $	*/
/*
 * Copyright (c) 2018 Claudio Jeker <claudio@openbsd.org>
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

#include "vmd.h"

#ifndef _FW_CFG_H_
#define _FW_CFG_H_

#define	FW_CFG_IO_SELECT	0x510
#define	FW_CFG_IO_DATA		0x511
#define	FW_CFG_IO_DMA_ADDR_HIGH	0x514
#define	FW_CFG_IO_DMA_ADDR_LOW	0x518

void	fw_cfg_init(struct vmop_create_params *);
uint8_t	vcpu_exit_fw_cfg(struct vm_run_params *);
uint8_t	vcpu_exit_fw_cfg_dma(struct vm_run_params *);
void	fw_cfg_add_file(const char *, const void *, size_t);

#endif /* _FW_CFG_H_ */
