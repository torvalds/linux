/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 Alexander Motin <mav@FreeBSD.org>
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/endian.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ctld.h"
#include "isns.h"

struct isns_req *
isns_req_alloc(void)
{
	struct isns_req *req;

	req = calloc(sizeof(struct isns_req), 1);
	if (req == NULL) {
		log_err(1, "calloc");
		return (NULL);
	}
	req->ir_buflen = sizeof(struct isns_hdr);
	req->ir_usedlen = 0;
	req->ir_buf = calloc(req->ir_buflen, 1);
	if (req->ir_buf == NULL) {
		free(req);
		log_err(1, "calloc");
		return (NULL);
	}
	return (req);
}

struct isns_req *
isns_req_create(uint16_t func, uint16_t flags)
{
	struct isns_req *req;
	struct isns_hdr *hdr;

	req = isns_req_alloc();
	req->ir_usedlen = sizeof(struct isns_hdr);
	hdr = (struct isns_hdr *)req->ir_buf;
	be16enc(hdr->ih_version, ISNS_VERSION);
	be16enc(hdr->ih_function, func);
	be16enc(hdr->ih_flags, flags);
	return (req);
}

void
isns_req_free(struct isns_req *req)
{

	free(req->ir_buf);
	free(req);
}

static int
isns_req_getspace(struct isns_req *req, uint32_t len)
{
	void *newbuf;
	int newlen;

	if (req->ir_usedlen + len <= req->ir_buflen)
		return (0);
	newlen = 1 << flsl(req->ir_usedlen + len);
	newbuf = realloc(req->ir_buf, newlen);
	if (newbuf == NULL) {
		log_err(1, "realloc");
		return (1);
	}
	req->ir_buf = newbuf;
	req->ir_buflen = newlen;
	return (0);
}

void
isns_req_add(struct isns_req *req, uint32_t tag, uint32_t len,
    const void *value)
{
	struct isns_tlv *tlv;
	uint32_t vlen;

	vlen = len + ((len & 3) ? (4 - (len & 3)) : 0);
	isns_req_getspace(req, sizeof(*tlv) + vlen);
	tlv = (struct isns_tlv *)&req->ir_buf[req->ir_usedlen];
	be32enc(tlv->it_tag, tag);
	be32enc(tlv->it_length, vlen);
	memcpy(tlv->it_value, value, len);
	if (vlen != len)
		memset(&tlv->it_value[len], 0, vlen - len);
	req->ir_usedlen += sizeof(*tlv) + vlen;
}

void
isns_req_add_delim(struct isns_req *req)
{

	isns_req_add(req, 0, 0, NULL);
}

void
isns_req_add_str(struct isns_req *req, uint32_t tag, const char *value)
{

	isns_req_add(req, tag, strlen(value) + 1, value);
}

void
isns_req_add_32(struct isns_req *req, uint32_t tag, uint32_t value)
{
	uint32_t beval;

	be32enc(&beval, value);
	isns_req_add(req, tag, sizeof(value), &beval);
}

void
isns_req_add_addr(struct isns_req *req, uint32_t tag, struct addrinfo *ai)
{
	struct sockaddr_in *in4;
	struct sockaddr_in6 *in6;
	uint8_t buf[16];

	switch (ai->ai_addr->sa_family) {
	case AF_INET:
		in4 = (struct sockaddr_in *)(void *)ai->ai_addr;
		memset(buf, 0, 10);
		buf[10] = 0xff;
		buf[11] = 0xff;
		memcpy(&buf[12], &in4->sin_addr, sizeof(in4->sin_addr));
		isns_req_add(req, tag, sizeof(buf), buf);
		break;
	case AF_INET6:
		in6 = (struct sockaddr_in6 *)(void *)ai->ai_addr;
		isns_req_add(req, tag, sizeof(in6->sin6_addr), &in6->sin6_addr);
		break;
	default:
		log_errx(1, "Unsupported address family %d",
		    ai->ai_addr->sa_family);
	}
}

void
isns_req_add_port(struct isns_req *req, uint32_t tag, struct addrinfo *ai)
{
	struct sockaddr_in *in4;
	struct sockaddr_in6 *in6;
	uint32_t buf;

	switch (ai->ai_addr->sa_family) {
	case AF_INET:
		in4 = (struct sockaddr_in *)(void *)ai->ai_addr;
		be32enc(&buf, ntohs(in4->sin_port));
		isns_req_add(req, tag, sizeof(buf), &buf);
		break;
	case AF_INET6:
		in6 = (struct sockaddr_in6 *)(void *)ai->ai_addr;
		be32enc(&buf, ntohs(in6->sin6_port));
		isns_req_add(req, tag, sizeof(buf), &buf);
		break;
	default:
		log_errx(1, "Unsupported address family %d",
		    ai->ai_addr->sa_family);
	}
}

int
isns_req_send(int s, struct isns_req *req)
{
	struct isns_hdr *hdr;
	int res;

	hdr = (struct isns_hdr *)req->ir_buf;
	be16enc(hdr->ih_length, req->ir_usedlen - sizeof(*hdr));
	be16enc(hdr->ih_flags, be16dec(hdr->ih_flags) |
	    ISNS_FLAG_LAST | ISNS_FLAG_FIRST);
	be16enc(hdr->ih_transaction, 0);
	be16enc(hdr->ih_sequence, 0);

	res = write(s, req->ir_buf, req->ir_usedlen);
	return ((res < 0) ? -1 : 0);
}

int
isns_req_receive(int s, struct isns_req *req)
{
	struct isns_hdr *hdr;
	ssize_t res, len;

	req->ir_usedlen = 0;
	isns_req_getspace(req, sizeof(*hdr));
	res = read(s, req->ir_buf, sizeof(*hdr));
	if (res < (ssize_t)sizeof(*hdr))
		return (-1);
	req->ir_usedlen = sizeof(*hdr);
	hdr = (struct isns_hdr *)req->ir_buf;
	if (be16dec(hdr->ih_version) != ISNS_VERSION)
		return (-1);
	if ((be16dec(hdr->ih_flags) & (ISNS_FLAG_LAST | ISNS_FLAG_FIRST)) !=
	    (ISNS_FLAG_LAST | ISNS_FLAG_FIRST))
		return (-1);
	len = be16dec(hdr->ih_length);
	isns_req_getspace(req, len);
	res = read(s, &req->ir_buf[req->ir_usedlen], len);
	if (res < len)
		return (-1);
	req->ir_usedlen += len;
	return (0);
}

uint32_t
isns_req_get_status(struct isns_req *req)
{

	if (req->ir_usedlen < sizeof(struct isns_hdr) + 4)
		return (-1);
	return (be32dec(&req->ir_buf[sizeof(struct isns_hdr)]));
}
