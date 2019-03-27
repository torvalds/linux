/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by
 * Jacques A. Vidrine, Safeport Network Services, and Network
 * Associates Laboratories, the Security Research Division of Network
 * Associates, Inc. under DARPA/SPAWAR contract N66001-01-C-8035
 * ("CBOSS"), as part of the DARPA CHATS research program.
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
 *
 * Compatibility header for the GNU C Library-style nsswitch interface.
 */
#ifndef _NSS_H_
#define _NSS_H_

#include <nsswitch.h>

enum nss_status {
	NSS_STATUS_TRYAGAIN = -2,
	NSS_STATUS_UNAVAIL,
	NSS_STATUS_NOTFOUND,
	NSS_STATUS_SUCCESS,
	NSS_STATUS_RETURN
};

#define __nss_compat_result(rv, err)		\
((rv == NSS_STATUS_TRYAGAIN && err == ERANGE) ? NS_RETURN : \
 (rv == NSS_STATUS_TRYAGAIN) ? NS_TRYAGAIN :	\
 (rv == NSS_STATUS_UNAVAIL)  ? NS_UNAVAIL  :	\
 (rv == NSS_STATUS_NOTFOUND) ? NS_NOTFOUND :	\
 (rv == NSS_STATUS_SUCCESS)  ? NS_SUCCESS  :	\
 (rv == NSS_STATUS_RETURN)   ? NS_RETURN   : 0)

#endif
