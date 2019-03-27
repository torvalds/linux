/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2004 The Aerospace Corporation.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions, and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions, and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  The name of The Aerospace Corporation may not be used to endorse or
 *     promote products derived from this software.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AEROSPACE CORPORATION "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AEROSPACE CORPORATION BE LIABLE
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
#ifndef _SYS_EUI64_H
#define _SYS_EUI64_H

/*
 * Size of the ASCII representation of an EUI-64.
 */
#define EUI64_SIZ	24

/*
 * The number of bytes in an EUI-64.
 */
#define EUI64_LEN	8

/*
 * Structure of an IEEE EUI-64.
 */
struct	eui64 {
	u_char octet[EUI64_LEN];
};

#ifndef _KERNEL
int	eui64_aton(const char *, struct eui64 *);
int	eui64_ntoa(const struct eui64 *, char *, size_t);
int	eui64_ntohost(char *, size_t, const struct eui64 *);
int	eui64_hostton(const char *, struct eui64 *);
#endif /* !_KERNEL */

#endif /* !_SYS_EUI64_H */
