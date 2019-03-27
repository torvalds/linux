/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _NLMRSALIB_H_
#define _NLMRSALIB_H_

#define	RSA_ERROR(msg0)		(((msg0) >> 53) & 0x1f)

struct xlp_rsa_session {
};

struct xlp_rsa_command {
	struct xlp_rsa_session *ses;
	struct cryptkop *krp;
	uint8_t *rsasrc;
	uint32_t rsaopsize;
	uint32_t rsatype;
	uint32_t rsafn;
};

/*
 * Holds data specific to nlm security accelerators
 */
struct xlp_rsa_softc {
	device_t sc_dev;	/* device backpointer */
	uint64_t rsa_base;
	int sc_cid;
	int rsaecc_vc_start;
	int rsaecc_vc_end;
};

void
nlm_xlprsaecc_msgring_handler(int vc, int size, int code, int src_id,
    struct nlm_fmn_msg *msg, void *data);

#endif /* _NLMRSALIB_H_ */
