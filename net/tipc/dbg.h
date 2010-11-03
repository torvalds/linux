/*
 * net/tipc/dbg.h: Include file for TIPC print buffer routines
 *
 * Copyright (c) 1997-2006, Ericsson AB
 * Copyright (c) 2005-2007, Wind River Systems
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _TIPC_DBG_H
#define _TIPC_DBG_H

/**
 * struct print_buf - TIPC print buffer structure
 * @buf: pointer to character array containing print buffer contents
 * @size: size of character array
 * @crs: pointer to first unused space in character array (i.e. final NUL)
 * @echo: echo output to system console if non-zero
 */

struct print_buf {
	char *buf;
	u32 size;
	char *crs;
	int echo;
};

#define TIPC_PB_MIN_SIZE 64	/* minimum size for a print buffer's array */
#define TIPC_PB_MAX_STR 512	/* max printable string (with trailing NUL) */

void tipc_printbuf_init(struct print_buf *pb, char *buf, u32 size);
int  tipc_printbuf_validate(struct print_buf *pb);

int tipc_log_resize(int log_size);

struct sk_buff *tipc_log_resize_cmd(const void *req_tlv_area,
				    int req_tlv_space);
struct sk_buff *tipc_log_dump(void);

#endif
