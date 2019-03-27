/*
 ** Copyright (c) 2015, Asim Jamshed, Robin Sommer, Seth Hall
 ** and the International Computer Science Institute. All rights reserved.
 **
 ** Redistribution and use in source and binary forms, with or without
 ** modification, are permitted provided that the following conditions are met:
 **
 ** (1) Redistributions of source code must retain the above copyright
 **     notice, this list of conditions and the following disclaimer.
 **
 ** (2) Redistributions in binary form must reproduce the above copyright
 **     notice, this list of conditions and the following disclaimer in the
 **     documentation and/or other materials provided with the distribution.
 **
 **
 ** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 ** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 ** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ** ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 ** LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 ** CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 ** SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 ** INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 ** CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ** ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 ** POSSIBILITY OF SUCH DAMAGE.
 **/
/* $FreeBSD$ */
#ifndef LB_PKT_HASH_H
#define LB_PKT_HASH_H
/*---------------------------------------------------------------------*/
/**
 ** Packet header hashing function utility - This file contains functions
 ** that parse the packet headers and computes hash functions based on
 ** the header fields. Please see pkt_hash.c for more details...
 **/
/*---------------------------------------------------------------------*/
/* for type def'n */
#include <stdint.h>
/*---------------------------------------------------------------------*/
#ifdef __GNUC__
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)
#else
#define likely(x)       (x)
#define unlikely(x)     (x)
#endif

#define HTONS(n) (((((unsigned short)(n) & 0xFF)) << 8) | \
		  (((unsigned short)(n) & 0xFF00) >> 8))
#define NTOHS(n) (((((unsigned short)(n) & 0xFF)) << 8) | \
		  (((unsigned short)(n) & 0xFF00) >> 8))

#define HTONL(n) (((((unsigned long)(n) & 0xFF)) << 24) | \
        ((((unsigned long)(n) & 0xFF00)) << 8) | \
        ((((unsigned long)(n) & 0xFF0000)) >> 8) | \
		  ((((unsigned long)(n) & 0xFF000000)) >> 24))

#define NTOHL(n) (((((unsigned long)(n) & 0xFF)) << 24) | \
        ((((unsigned long)(n) & 0xFF00)) << 8) | \
        ((((unsigned long)(n) & 0xFF0000)) >> 8) | \
		  ((((unsigned long)(n) & 0xFF000000)) >> 24))
/*---------------------------------------------------------------------*/
typedef struct vlanhdr {
	uint16_t pri_cfi_vlan;
	uint16_t proto;
} vlanhdr;
/*---------------------------------------------------------------------*/
/**
 ** Analyzes the packet header of computes a corresponding
 ** hash function.
 **/
uint32_t
pkt_hdr_hash(const unsigned char *buffer,
	     uint8_t hash_split,
	     uint8_t seed);
/*---------------------------------------------------------------------*/
#endif /* LB_PKT_HASH_H */

