/*
 * include/net/tipc.h: Include file for TIPC message header routines
 *
 * Copyright (c) 2017 Ericsson AB
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

#ifndef _TIPC_HDR_H
#define _TIPC_HDR_H

#include <linux/random.h>

#define KEEPALIVE_MSG_MASK 0x0e080000  /* LINK_PROTOCOL + MSG_IS_KEEPALIVE */

struct tipc_basic_hdr {
	__be32 w[4];
};

static inline __be32 tipc_hdr_rps_key(struct tipc_basic_hdr *hdr)
{
	u32 w0 = ntohl(hdr->w[0]);
	bool keepalive_msg = (w0 & KEEPALIVE_MSG_MASK) == KEEPALIVE_MSG_MASK;
	__be32 key;

	/* Return source node identity as key */
	if (likely(!keepalive_msg))
		return hdr->w[3];

	/* Spread PROBE/PROBE_REPLY messages across the cores */
	get_random_bytes(&key, sizeof(key));
	return key;
}

#endif
