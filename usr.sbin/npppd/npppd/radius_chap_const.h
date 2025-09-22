/*	$OpenBSD: radius_chap_const.h,v 1.5 2021/03/29 03:54:39 yasuoka Exp $ */

/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
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
 */
#pragma pack(1)

struct RADIUS_CHAP_PASSWORD {
	u_int8_t ident;
	char response[16];
};

struct RADIUS_MS_CHAP_RESPONSE {
	u_int8_t ident;
	u_int8_t flags;
	char lmresponse[24];
	char ntresponse[24];
};

struct RADIUS_MS_CHAP2_RESPONSE {
	u_int8_t ident;
	u_int8_t flags;
	char peer_challenge[16];
	char reserved[8];
	char response[24];
};

struct RADIUS_MS_CHAP2_SUCCESS {
	u_int8_t ident;
	char str[42];
};

struct RADIUS_MPPE_KEY {
	char salt[2];
	char key[253];
	/*
	 * XXX: Having maximum size for RADIUS attribute is required to prevent
	 * XXX: overflow by radius_get_vs_raw_attr().
	 */
};

struct RADIUS_MS_CHAP2_ERROR {
	u_int8_t ident;
	char str[15];
};
#pragma pack()
