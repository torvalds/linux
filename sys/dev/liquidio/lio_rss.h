/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2017 Cavium, Inc.. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Cavium, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER(S) OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*$FreeBSD$*/

#ifndef __LIO_RSS_H__
#define __LIO_RSS_H__

#ifdef RSS

#include <net/rss_config.h>
#include <netinet/in_rss.h>

#define LIO_RSS_TABLE_SZ	128
#define LIO_RSS_KEY_SZ		40

struct lio_rss_params {
#define LIO_RSS_PARAM_SIZE	16
	struct param {
#if BYTE_ORDER == LITTLE_ENDIAN
		uint64_t	flags:16;
		uint64_t	hashinfo:32;
		uint64_t	itablesize:16;

		uint64_t	hashkeysize:16;
		uint64_t	reserved:48;
#elif BYTE_ORDER == BIG_ENDIAN
		uint64_t	itablesize:16;
		uint64_t	hashinfo:32;
		uint64_t	flags:16;

		uint64_t	reserved:48;
		uint64_t	hashkeysize:16;
#else
#error Undefined BYTE_ORDER
#endif
	}	param;

	uint8_t	itable[LIO_RSS_TABLE_SZ];
	uint8_t	key[LIO_RSS_KEY_SZ];

};

struct lio_rss_params_set {
	uint8_t		key[LIO_RSS_KEY_SZ];
	uint8_t		fw_itable[LIO_RSS_TABLE_SZ];
	uint64_t	hashinfo;

};

#endif	/* RSS */

#define LIO_RSS_HASH_IPV4		0x100
#define LIO_RSS_HASH_TCP_IPV4		0x200
#define LIO_RSS_HASH_IPV6		0x400
#define LIO_RSS_HASH_IPV6_EX		0x800
#define LIO_RSS_HASH_TCP_IPV6		0x1000
#define LIO_RSS_HASH_TCP_IPV6_EX	0x2000

#endif	/* __LIO_RSS_H__ */
