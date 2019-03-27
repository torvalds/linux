/*-
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
 * $FreeBSD$
 */

#ifndef	_ISNS_H
#define	_ISNS_H

#define	ISNS_VERSION		0x0001

#define	ISNS_FUNC_DEVATTRREG	0x0001
#define	ISNS_FUNC_DEVATTRQRY	0x0002
#define	ISNS_FUNC_DEVGETNEXT	0x0003
#define	ISNS_FUNC_DEVDEREG	0x0004
#define	ISNS_FUNC_SCNREG	0x0005
#define	ISNS_FUNC_SCNDEREG	0x0006
#define	ISNS_FUNC_SCNEVENT	0x0007
#define	ISNS_FUNC_SCN		0x0008
#define	ISNS_FUNC_DDREG		0x0009
#define	ISNS_FUNC_DDDEREG	0x000a
#define	ISNS_FUNC_DDSREG	0x000b
#define	ISNS_FUNC_DDSDEREG	0x000c
#define	ISNS_FUNC_ESI		0x000d
#define	ISNS_FUNC_HEARTBEAT	0x000e
#define	ISNS_FUNC_RESPONSE	0x8000

#define	ISNS_FLAG_CLIENT	0x8000
#define	ISNS_FLAG_SERVER	0x4000
#define	ISNS_FLAG_AUTH		0x2000
#define	ISNS_FLAG_REPLACE	0x1000
#define	ISNS_FLAG_LAST		0x0800
#define	ISNS_FLAG_FIRST		0x0400

struct isns_hdr {
	uint8_t	ih_version[2];
	uint8_t	ih_function[2];
	uint8_t	ih_length[2];
	uint8_t	ih_flags[2];
	uint8_t	ih_transaction[2];
	uint8_t	ih_sequence[2];
};

struct isns_tlv {
	uint8_t	it_tag[4];
	uint8_t	it_length[4];
	uint8_t	it_value[];
};

struct isns_req {
	u_int	ir_buflen;
	u_int	ir_usedlen;
	uint8_t	*ir_buf;
};

struct isns_req * isns_req_alloc(void);
struct isns_req * isns_req_create(uint16_t func, uint16_t flags);
void isns_req_free(struct isns_req *req);
void isns_req_add(struct isns_req *req, uint32_t tag, uint32_t len,
    const void *value);
void isns_req_add_delim(struct isns_req *req);
void isns_req_add_str(struct isns_req *req, uint32_t tag, const char *value);
void isns_req_add_32(struct isns_req *req, uint32_t tag, uint32_t value);
void isns_req_add_addr(struct isns_req *req, uint32_t tag, struct addrinfo *ai);
void isns_req_add_port(struct isns_req *req, uint32_t tag, struct addrinfo *ai);
int isns_req_send(int s, struct isns_req *req);
int isns_req_receive(int s, struct isns_req *req);
uint32_t isns_req_get_status(struct isns_req *req);

#endif /* _ISNS_H */
