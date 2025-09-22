/*	$OpenBSD: radius_local.h,v 1.2 2024/07/24 08:19:16 yasuoka Exp $ */

/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef RADIUS_LOCAL_H
#define RADIUS_LOCAL_H

#ifndef countof
#define countof(x) (sizeof(x)/sizeof((x)[0]))
#endif

typedef struct _RADIUS_PACKET_DATA {
	uint8_t code;
	uint8_t id;
	uint16_t length;
	uint8_t authenticator[16];
	char attributes[0];
}                   RADIUS_PACKET_DATA;
#pragma pack(1)
typedef struct _RADIUS_ATTRIBUTE {
	uint8_t type;
	uint8_t length;
	char data[0];
	uint32_t vendor;
	uint8_t vtype;
	uint8_t vlength;
	char vdata[0];
}                 RADIUS_ATTRIBUTE;
#pragma pack()

struct _RADIUS_PACKET {
	RADIUS_PACKET_DATA *pdata;
	size_t capacity;
	const RADIUS_PACKET *request;
};
#define RADIUS_PACKET_CAPACITY_INITIAL   64
#define RADIUS_PACKET_CAPACITY_INCREMENT 64

#define ATTRS_BEGIN(pdata) ((RADIUS_ATTRIBUTE*)pdata->attributes)

#define ATTRS_END(pdata) \
    ((RADIUS_ATTRIBUTE*)(((char*)pdata) + ntohs(pdata->length)))

#define ATTRS_NEXT(x) ((RADIUS_ATTRIBUTE*)(((char*)x) + x->length))

/*
 * must be expression rather than statement
 * to be used in third expression of for statement.
 */
#define ATTRS_ADVANCE(x) (x = ATTRS_NEXT(x))

int radius_ensure_add_capacity(RADIUS_PACKET * packet, size_t capacity);
int radius_unshift_raw_attr(RADIUS_PACKET * packet, uint8_t type,
    const void *buf, size_t length);

#define ROUNDUP(a, b)	((((a) + (b) - 1) / (b)) * (b))
#define	MINIMUM(a, b)	(((a) < (b))? (a) : (b))

#endif				/* RADIUS_LOCAL_H */
