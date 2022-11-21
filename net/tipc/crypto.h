/* SPDX-License-Identifier: GPL-2.0 */
/*
 * net/tipc/crypto.h: Include file for TIPC crypto
 *
 * Copyright (c) 2019, Ericsson AB
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
#ifdef CONFIG_TIPC_CRYPTO
#ifndef _TIPC_CRYPTO_H
#define _TIPC_CRYPTO_H

#include "core.h"
#include "node.h"
#include "msg.h"
#include "bearer.h"

#define TIPC_EVERSION			7

/* AEAD aes(gcm) */
#define TIPC_AES_GCM_KEY_SIZE_128	16
#define TIPC_AES_GCM_KEY_SIZE_192	24
#define TIPC_AES_GCM_KEY_SIZE_256	32

#define TIPC_AES_GCM_SALT_SIZE		4
#define TIPC_AES_GCM_IV_SIZE		12
#define TIPC_AES_GCM_TAG_SIZE		16

/*
 * TIPC crypto modes:
 * - CLUSTER_KEY:
 *	One single key is used for both TX & RX in all nodes in the cluster.
 * - PER_NODE_KEY:
 *	Each nodes in the cluster has one TX key, for RX a node needs to know
 *	its peers' TX key for the decryption of messages from those nodes.
 */
enum {
	CLUSTER_KEY = 1,
	PER_NODE_KEY = (1 << 1),
};

extern int sysctl_tipc_max_tfms __read_mostly;
extern int sysctl_tipc_key_exchange_enabled __read_mostly;

/*
 * TIPC encryption message format:
 *
 *     3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0
 *     1 0 9 8 7 6 5 4|3 2 1 0 9 8 7 6|5 4 3 2 1 0 9 8|7 6 5 4 3 2 1 0
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * w0:|Ver=7| User  |D|TX |RX |K|M|N|             Rsvd                |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * w1:|                             Seqno                             |
 * w2:|                           (8 octets)                          |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * w3:\                            Prevnode                           \
 *    /                        (4 or 16 octets)                       /
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    \                                                               \
 *    /       Encrypted complete TIPC V2 header and user data         /
 *    \                                                               \
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                                                               |
 *    |                             AuthTag                           |
 *    |                           (16 octets)                         |
 *    |                                                               |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Word0:
 *	Ver	: = 7 i.e. TIPC encryption message version
 *	User	: = 7 (for LINK_PROTOCOL); = 13 (for LINK_CONFIG) or = 0
 *	D	: The destined bit i.e. the message's destination node is
 *	          "known" or not at the message encryption
 *	TX	: TX key used for the message encryption
 *	RX	: Currently RX active key corresponding to the destination
 *	          node's TX key (when the "D" bit is set)
 *	K	: Keep-alive bit (for RPS, LINK_PROTOCOL/STATE_MSG only)
 *	M       : Bit indicates if sender has master key
 *	N	: Bit indicates if sender has no RX keys corresponding to the
 *	          receiver's TX (when the "D" bit is set)
 *	Rsvd	: Reserved bit, field
 * Word1-2:
 *	Seqno	: The 64-bit sequence number of the encrypted message, also
 *		  part of the nonce used for the message encryption/decryption
 * Word3-:
 *	Prevnode: The source node address, or ID in case LINK_CONFIG only
 *	AuthTag	: The authentication tag for the message integrity checking
 *		  generated by the message encryption
 */
struct tipc_ehdr {
	union {
		struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
			__u8	destined:1,
				user:4,
				version:3;
			__u8	reserved_1:1,
				rx_nokey:1,
				master_key:1,
				keepalive:1,
				rx_key_active:2,
				tx_key:2;
#elif defined(__BIG_ENDIAN_BITFIELD)
			__u8	version:3,
				user:4,
				destined:1;
			__u8	tx_key:2,
				rx_key_active:2,
				keepalive:1,
				master_key:1,
				rx_nokey:1,
				reserved_1:1;
#else
#error  "Please fix <asm/byteorder.h>"
#endif
			__be16	reserved_2;
		} __packed;
		__be32 w0;
	};
	__be64 seqno;
	union {
		__be32 addr;
		__u8 id[NODE_ID_LEN]; /* For a LINK_CONFIG message only! */
	};
#define EHDR_SIZE	(offsetof(struct tipc_ehdr, addr) + sizeof(__be32))
#define EHDR_CFG_SIZE	(sizeof(struct tipc_ehdr))
#define EHDR_MIN_SIZE	(EHDR_SIZE)
#define EHDR_MAX_SIZE	(EHDR_CFG_SIZE)
#define EMSG_OVERHEAD	(EHDR_SIZE + TIPC_AES_GCM_TAG_SIZE)
} __packed;

int tipc_crypto_start(struct tipc_crypto **crypto, struct net *net,
		      struct tipc_node *node);
void tipc_crypto_stop(struct tipc_crypto **crypto);
void tipc_crypto_timeout(struct tipc_crypto *rx);
int tipc_crypto_xmit(struct net *net, struct sk_buff **skb,
		     struct tipc_bearer *b, struct tipc_media_addr *dst,
		     struct tipc_node *__dnode);
int tipc_crypto_rcv(struct net *net, struct tipc_crypto *rx,
		    struct sk_buff **skb, struct tipc_bearer *b);
int tipc_crypto_key_init(struct tipc_crypto *c, struct tipc_aead_key *ukey,
			 u8 mode, bool master_key);
void tipc_crypto_key_flush(struct tipc_crypto *c);
int tipc_crypto_key_distr(struct tipc_crypto *tx, u8 key,
			  struct tipc_node *dest);
void tipc_crypto_msg_rcv(struct net *net, struct sk_buff *skb);
void tipc_crypto_rekeying_sched(struct tipc_crypto *tx, bool changed,
				u32 new_intv);
int tipc_aead_key_validate(struct tipc_aead_key *ukey, struct genl_info *info);
bool tipc_ehdr_validate(struct sk_buff *skb);

static inline u32 msg_key_gen(struct tipc_msg *m)
{
	return msg_bits(m, 4, 16, 0xffff);
}

static inline void msg_set_key_gen(struct tipc_msg *m, u32 gen)
{
	msg_set_bits(m, 4, 16, 0xffff, gen);
}

static inline u32 msg_key_mode(struct tipc_msg *m)
{
	return msg_bits(m, 4, 0, 0xf);
}

static inline void msg_set_key_mode(struct tipc_msg *m, u32 mode)
{
	msg_set_bits(m, 4, 0, 0xf, mode);
}

#endif /* _TIPC_CRYPTO_H */
#endif
