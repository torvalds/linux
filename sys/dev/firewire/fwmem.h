/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 2002-2003
 * 	Hidetoshi Shimokawa. All rights reserved.
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
 *
 *	This product includes software developed by Hidetoshi Shimokawa.
 *
 * 4. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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

struct fw_xfer *fwmem_read_quad(struct fw_device *, caddr_t, uint8_t,
	uint16_t, uint32_t, void *, void (*)(struct fw_xfer *));
struct fw_xfer *fwmem_write_quad(struct fw_device *, caddr_t, uint8_t,
	uint16_t, uint32_t, void *, void (*)(struct fw_xfer *));
struct fw_xfer *fwmem_read_block(struct fw_device *, caddr_t, uint8_t,
	uint16_t, uint32_t, int, void *, void (*)(struct fw_xfer *));
struct fw_xfer *fwmem_write_block(struct fw_device *, caddr_t, uint8_t,
	uint16_t, uint32_t, int, void *, void (*)(struct fw_xfer *));

d_open_t	fwmem_open;
d_close_t	fwmem_close;
d_ioctl_t	fwmem_ioctl;
d_read_t	fwmem_read;
d_write_t	fwmem_write;
d_poll_t	fwmem_poll;
d_mmap_t	fwmem_mmap;
d_strategy_t	fwmem_strategy;
