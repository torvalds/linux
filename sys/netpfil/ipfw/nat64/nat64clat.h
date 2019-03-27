/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2019 Yandex LLC
 * Copyright (c) 2019 Andrey V. Elsukov <ae@FreeBSD.org>
 * Copyright (c) 2019 Boris N. Lytochkin <lytboris@gmail.com>
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

#ifndef	_IP_FW_NAT64CLAT_H_
#define	_IP_FW_NAT64CLAT_H_

#include "ip_fw_nat64.h"
#include "nat64_translate.h"

struct nat64clat_cfg {
	struct named_object	no;

	struct nat64_config	base;
#define	NAT64CLAT_FLAGSMASK	\
	(NAT64_LOG | NAT64_ALLOW_PRIVATE) /* flags to pass to userland */
	char			name[64];
};

VNET_DECLARE(uint16_t, nat64clat_eid);
#define	V_nat64clat_eid	VNET(nat64clat_eid)
#define	IPFW_TLV_NAT64CLAT_NAME	IPFW_TLV_EACTION_NAME(V_nat64clat_eid)

int ipfw_nat64clat(struct ip_fw_chain *chain, struct ip_fw_args *args,
    ipfw_insn *cmd, int *done);

#endif
