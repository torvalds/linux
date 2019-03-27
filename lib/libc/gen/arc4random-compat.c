/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Google LLC
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <stdbool.h>
#include <syslog.h>

/*
 * The following functions were removed from OpenBSD for good reasons:
 *
 *  - arc4random_stir()
 *  - arc4random_addrandom()
 *
 * On FreeBSD, for backward ABI compatibility, we provide two wrapper which
 * logs this event and returns.
 */

void __arc4random_stir_fbsd11(void);
void __arc4random_addrandom_fbsd11(u_char *, int);

void
__arc4random_stir_fbsd11(void)
{
	static bool warned = false;

	if (!warned)
		syslog(LOG_DEBUG, "Deprecated function arc4random_stir() called");
	warned = true;
}

void
__arc4random_addrandom_fbsd11(u_char * dummy1 __unused, int dummy2 __unused)
{
	static bool warned = false;

	if (!warned)
		syslog(LOG_DEBUG, "Deprecated function arc4random_addrandom() called");
	warned = true;
}

__sym_compat(arc4random_stir, __arc4random_stir_fbsd11, FBSD_1.0);
__sym_compat(arc4random_addrandom, __arc4random_addrandom_fbsd11, FBSD_1.0);
