/*
 * include/net/tipc/tipc_msg.h: Include file for privileged access to TIPC message headers
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

#ifndef _NET_TIPC_MSG_H_
#define _NET_TIPC_MSG_H_

#ifdef __KERNEL__

struct tipc_msg {
	__be32 hdr[15];
};


/*
		TIPC user data message header format, version 2:


       1 0 9 8 7 6 5 4|3 2 1 0 9 8 7 6|5 4 3 2 1 0 9 8|7 6 5 4 3 2 1 0 
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   w0:|vers | user  |hdr sz |n|d|s|-|          message size           |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   w1:|mstyp| error |rer cnt|lsc|opt p|      broadcast ack no         |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   w2:|        link level ack no      |   broadcast/link level seq no |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   w3:|                       previous node                           |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   w4:|                      originating port                         |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   w5:|                      destination port                         |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+    
   w6:|                      originating node                         |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   w7:|                      destination node                         |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   w8:|            name type / transport sequence number              |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   w9:|              name instance/multicast lower bound              |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+    
   wA:|                    multicast upper bound                      |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+    
      /                                                               /
      \                           options                             \
      /                                                               /
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

*/

#define TIPC_CONN_MSG	0
#define TIPC_MCAST_MSG	1
#define TIPC_NAMED_MSG	2
#define TIPC_DIRECT_MSG	3


static inline u32 msg_word(struct tipc_msg *m, u32 pos)
{
	return ntohl(m->hdr[pos]);
}

static inline u32 msg_bits(struct tipc_msg *m, u32 w, u32 pos, u32 mask)
{
	return (msg_word(m, w) >> pos) & mask;
}

static inline u32 msg_importance(struct tipc_msg *m)
{
	return msg_bits(m, 0, 25, 0xf);
}

static inline u32 msg_hdr_sz(struct tipc_msg *m)
{
	return msg_bits(m, 0, 21, 0xf) << 2;
}

static inline int msg_short(struct tipc_msg *m)
{
	return msg_hdr_sz(m) == 24;
}

static inline u32 msg_size(struct tipc_msg *m)
{
	return msg_bits(m, 0, 0, 0x1ffff);
}

static inline u32 msg_data_sz(struct tipc_msg *m)
{
	return msg_size(m) - msg_hdr_sz(m);
}

static inline unchar *msg_data(struct tipc_msg *m)
{
	return ((unchar *)m) + msg_hdr_sz(m);
}

static inline u32 msg_type(struct tipc_msg *m)
{
	return msg_bits(m, 1, 29, 0x7);
}

static inline u32 msg_named(struct tipc_msg *m)
{
	return msg_type(m) == TIPC_NAMED_MSG;
}

static inline u32 msg_mcast(struct tipc_msg *m)
{
	return msg_type(m) == TIPC_MCAST_MSG;
}

static inline u32 msg_connected(struct tipc_msg *m)
{
	return msg_type(m) == TIPC_CONN_MSG;
}

static inline u32 msg_errcode(struct tipc_msg *m)
{
	return msg_bits(m, 1, 25, 0xf);
}

static inline u32 msg_prevnode(struct tipc_msg *m)
{
	return msg_word(m, 3);
}

static inline u32 msg_origport(struct tipc_msg *m)
{
	return msg_word(m, 4);
}

static inline u32 msg_destport(struct tipc_msg *m)
{
	return msg_word(m, 5);
}

static inline u32 msg_mc_netid(struct tipc_msg *m)
{
	return msg_word(m, 5);
}

static inline u32 msg_orignode(struct tipc_msg *m)
{
	if (likely(msg_short(m)))
		return msg_prevnode(m);
	return msg_word(m, 6);
}

static inline u32 msg_destnode(struct tipc_msg *m)
{
	return msg_word(m, 7);
}

static inline u32 msg_nametype(struct tipc_msg *m)
{
	return msg_word(m, 8);
}

static inline u32 msg_nameinst(struct tipc_msg *m)
{
	return msg_word(m, 9);
}

static inline u32 msg_namelower(struct tipc_msg *m)
{
	return msg_nameinst(m);
}

static inline u32 msg_nameupper(struct tipc_msg *m)
{
	return msg_word(m, 10);
}

#endif

#endif
