/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Brian Somers <brian@Awfulhak.org>
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

#ifndef _RADLIB_VS_H_
#define _RADLIB_VS_H_

#include <sys/types.h>
#include <netinet/in.h>

#define	RAD_VENDOR_MICROSOFT	311		/* rfc2548 */
	#define	RAD_MICROSOFT_MS_CHAP_RESPONSE			1
	#define	RAD_MICROSOFT_MS_CHAP_ERROR			2
	#define	RAD_MICROSOFT_MS_CHAP_PW_1			3
	#define	RAD_MICROSOFT_MS_CHAP_PW_2			4
	#define	RAD_MICROSOFT_MS_CHAP_LM_ENC_PW			5
	#define	RAD_MICROSOFT_MS_CHAP_NT_ENC_PW			6
	#define	RAD_MICROSOFT_MS_MPPE_ENCRYPTION_POLICY		7
	#define	RAD_MICROSOFT_MS_MPPE_ENCRYPTION_TYPES		8
	#define	RAD_MICROSOFT_MS_RAS_VENDOR			9
	#define	RAD_MICROSOFT_MS_CHAP_DOMAIN			10
	#define	RAD_MICROSOFT_MS_CHAP_CHALLENGE			11
	#define	RAD_MICROSOFT_MS_CHAP_MPPE_KEYS			12
	#define	RAD_MICROSOFT_MS_BAP_USAGE			13
	#define	RAD_MICROSOFT_MS_LINK_UTILIZATION_THRESHOLD	14
	#define	RAD_MICROSOFT_MS_LINK_DROP_TIME_LIMIT		15
	#define	RAD_MICROSOFT_MS_MPPE_SEND_KEY			16
	#define	RAD_MICROSOFT_MS_MPPE_RECV_KEY			17
	#define	RAD_MICROSOFT_MS_RAS_VERSION			18
	#define	RAD_MICROSOFT_MS_OLD_ARAP_PASSWORD		19
	#define	RAD_MICROSOFT_MS_NEW_ARAP_PASSWORD		20
	#define	RAD_MICROSOFT_MS_ARAP_PASSWORD_CHANGE_REASON	21
	#define	RAD_MICROSOFT_MS_FILTER				22
	#define	RAD_MICROSOFT_MS_ACCT_AUTH_TYPE			23
	#define	RAD_MICROSOFT_MS_ACCT_EAP_TYPE			24
	#define	RAD_MICROSOFT_MS_CHAP2_RESPONSE			25
	#define	RAD_MICROSOFT_MS_CHAP2_SUCCESS			26
	#define	RAD_MICROSOFT_MS_CHAP2_PW			27
	#define	RAD_MICROSOFT_MS_PRIMARY_DNS_SERVER		28
	#define	RAD_MICROSOFT_MS_SECONDARY_DNS_SERVER		29
	#define	RAD_MICROSOFT_MS_PRIMARY_NBNS_SERVER		30
	#define	RAD_MICROSOFT_MS_SECONDARY_NBNS_SERVER		31
	#define	RAD_MICROSOFT_MS_ARAP_CHALLENGE			33

#define SALT_LEN    2

struct rad_handle;

__BEGIN_DECLS
int	 rad_get_vendor_attr(u_int32_t *, const void **, size_t *);
int	 rad_put_vendor_addr(struct rad_handle *, int, int, struct in_addr);
int	 rad_put_vendor_addr6(struct rad_handle *, int, int, struct in6_addr);
int	 rad_put_vendor_attr(struct rad_handle *, int, int, const void *,
	    size_t);
int	 rad_put_vendor_int(struct rad_handle *, int, int, u_int32_t);
int	 rad_put_vendor_string(struct rad_handle *, int, int, const char *);
u_char	*rad_demangle_mppe_key(struct rad_handle *, const void *, size_t,
	    size_t *);
__END_DECLS

#endif /* _RADLIB_VS_H_ */
