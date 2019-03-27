/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2019 Yandex LLC
 * Copyright (c) 2015-2019 Andrey V. Elsukov <ae@FreeBSD.org>
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

#ifndef	_IP_FW_NAT64_H_
#define	_IP_FW_NAT64_H_

#define	DPRINTF(mask, fmt, ...)	\
    if (V_nat64_debug & (mask))	\
	printf("NAT64: %s: " fmt "\n", __func__, ## __VA_ARGS__)
#define	DP_GENERIC	0x0001
#define	DP_OBJ		0x0002
#define	DP_JQUEUE	0x0004
#define	DP_STATE	0x0008
#define	DP_DROPS	0x0010
#define	DP_ALL		0xFFFF

VNET_DECLARE(int, nat64_debug);
#define	V_nat64_debug		VNET(nat64_debug)

#if 0
#define	NAT64NOINLINE	__noinline
#else
#define	NAT64NOINLINE
#endif

int	nat64stl_init(struct ip_fw_chain *ch, int first);
void	nat64stl_uninit(struct ip_fw_chain *ch, int last);
int	nat64lsn_init(struct ip_fw_chain *ch, int first);
void	nat64lsn_uninit(struct ip_fw_chain *ch, int last);
int	nat64clat_init(struct ip_fw_chain *ch, int first);
void	nat64clat_uninit(struct ip_fw_chain *ch, int last);

#endif /* _IP_FW_NAT64_H_ */
