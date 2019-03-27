/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _SYS_UUID_H_
#define	_SYS_UUID_H_

#include <sys/cdefs.h>

/* Length of a node address (an IEEE 802 address). */
#define	_UUID_NODE_LEN		6

/*
 * See also:
 *      http://www.opengroup.org/dce/info/draft-leach-uuids-guids-01.txt
 *      http://www.opengroup.org/onlinepubs/009629399/apdxa.htm
 *
 * A DCE 1.1 compatible source representation of UUIDs.
 */
struct uuid {
	uint32_t	time_low;
	uint16_t	time_mid;
	uint16_t	time_hi_and_version;
	uint8_t		clock_seq_hi_and_reserved;
	uint8_t		clock_seq_low;
	uint8_t		node[_UUID_NODE_LEN];
};

#ifdef _KERNEL

#define	UUID_NODE_LEN	_UUID_NODE_LEN

struct sbuf;

struct uuid *kern_uuidgen(struct uuid *, size_t);

int uuid_ether_add(const uint8_t *);
int uuid_ether_del(const uint8_t *);

int snprintf_uuid(char *, size_t, struct uuid *);
int printf_uuid(struct uuid *);
int sbuf_printf_uuid(struct sbuf *, struct uuid *);
int parse_uuid(const char *, struct uuid *);
int uuidcmp(const struct uuid *, const struct uuid *);

void be_uuid_dec(void const *buf, struct uuid *uuid);
void be_uuid_enc(void *buf, struct uuid const *uuid);
void le_uuid_dec(void const *buf, struct uuid *uuid);
void le_uuid_enc(void *buf, struct uuid const *uuid);

#else	/* _KERNEL */

/* XXX namespace pollution? */
typedef struct uuid uuid_t;

__BEGIN_DECLS
int	uuidgen(struct uuid *, int);
__END_DECLS

#endif	/* _KERNEL */

#endif /* _SYS_UUID_H_ */
