/*
 * net/tipc/config.h: Include file for TIPC configuration service code
 * 
 * Copyright (c) 2003-2006, Ericsson AB
 * Copyright (c) 2005, Wind River Systems
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

#ifndef _TIPC_CONFIG_H
#define _TIPC_CONFIG_H

/* ---------------------------------------------------------------------- */

#include "core.h"
#include "link.h"

struct sk_buff *tipc_cfg_reply_alloc(int payload_size);
int tipc_cfg_append_tlv(struct sk_buff *buf, int tlv_type, 
			void *tlv_data, int tlv_data_size);
struct sk_buff *tipc_cfg_reply_unsigned_type(u16 tlv_type, u32 value);
struct sk_buff *tipc_cfg_reply_string_type(u16 tlv_type, char *string);

static inline struct sk_buff *tipc_cfg_reply_none(void)
{
	return tipc_cfg_reply_alloc(0);
}

static inline struct sk_buff *tipc_cfg_reply_unsigned(u32 value)
{
	return tipc_cfg_reply_unsigned_type(TIPC_TLV_UNSIGNED, value);
}

static inline struct sk_buff *tipc_cfg_reply_error_string(char *string)
{
	return tipc_cfg_reply_string_type(TIPC_TLV_ERROR_STRING, string);
}

static inline struct sk_buff *tipc_cfg_reply_ultra_string(char *string)
{
	return tipc_cfg_reply_string_type(TIPC_TLV_ULTRA_STRING, string);
}

struct sk_buff *tipc_cfg_do_cmd(u32 orig_node, u16 cmd, 
				const void *req_tlv_area, int req_tlv_space, 
				int headroom);

void tipc_cfg_link_event(u32 addr, char *name, int up);
int  tipc_cfg_init(void);
void tipc_cfg_stop(void);

#endif
