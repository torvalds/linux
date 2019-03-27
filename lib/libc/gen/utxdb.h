/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Ed Schouten <ed@FreeBSD.org>
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

#ifndef _UTXDB_H_
#define	_UTXDB_H_

#include <stdint.h>

#define	_PATH_UTX_ACTIVE	"/var/run/utx.active"
#define	_PATH_UTX_LASTLOGIN	"/var/log/utx.lastlogin"
#define	_PATH_UTX_LOG		"/var/log/utx.log"

/*
 * Entries in struct futx are ordered by how often they are used.  In
 * utx.log only entries will be written until the last non-zero byte,
 * which means we want to put the hostname at the end. Most primitive
 * records only store a ut_type and ut_tv, which means we want to store
 * those at the front.
 */

struct utmpx;

struct futx {
	uint8_t		fu_type;
	uint64_t	fu_tv;
	char		fu_id[8];
	uint32_t	fu_pid;
	char		fu_user[32];
	char		fu_line[16];
	char		fu_host[128];
} __packed;

void	utx_to_futx(const struct utmpx *, struct futx *);
struct utmpx *futx_to_utx(const struct futx *);

#endif /* !_UTXDB_H_ */
