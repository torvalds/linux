/*-
 * uuid.c
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: uuid.c,v 1.1 2004/12/09 18:20:26 max Exp $
 * $FreeBSD$
 */
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <sdp.h>
#include <uuid.h>
#include "uuid-private.h"

uint128_t	uuid_base = {
	.b = {
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00,
		0x10, 0x00,
		0x80, 0x00,
		0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb
	}
};

uint128_t	uuid_public_browse_group = {
	.b  = {
		0x00, 0x00, 0x10, 0x02,
		0x00, 0x00,
		0x10, 0x00,
		0x80, 0x00,
		0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb
	}
};

