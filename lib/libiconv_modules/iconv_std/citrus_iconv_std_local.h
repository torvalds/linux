/* $FreeBSD$ */
/*	$NetBSD: citrus_iconv_std_local.h,v 1.2 2003/07/01 09:42:16 tshiozak Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c)2003 Citrus Project,
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

#ifndef _CITRUS_ICONV_STD_LOCAL_H_
#define _CITRUS_ICONV_STD_LOCAL_H_

/*
 * encoding
 */
struct _citrus_iconv_std_encoding {
	struct _citrus_stdenc	*se_handle;
	void			*se_ps;
	void			*se_pssaved;
};

/*
 * dst
 */
struct _citrus_iconv_std_dst {
	TAILQ_ENTRY(_citrus_iconv_std_dst)	 sd_entry;
	struct _citrus_csmapper			*sd_mapper;
	_citrus_csid_t				 sd_csid;
	unsigned long				 sd_norm;
};
TAILQ_HEAD(_citrus_iconv_std_dst_list, _citrus_iconv_std_dst);

/*
 * src
 */
struct _citrus_iconv_std_src {
	TAILQ_ENTRY(_citrus_iconv_std_src)	 ss_entry;
	struct _citrus_iconv_std_dst_list	 ss_dsts;
	_citrus_csid_t				 ss_csid;
};
TAILQ_HEAD(_citrus_iconv_std_src_list, _citrus_iconv_std_src);

/*
 * iconv_std handle
 */
struct _citrus_iconv_std_shared {
	struct _citrus_stdenc			*is_dst_encoding;
	struct _citrus_stdenc			*is_src_encoding;
	struct _citrus_iconv_std_src_list	 is_srcs;
	_citrus_wc_t				 is_invalid;
	int					 is_use_invalid;
};

/*
 * iconv_std context
 */
struct _citrus_iconv_std_context {
	struct _citrus_iconv_std_encoding	 sc_dst_encoding;
	struct _citrus_iconv_std_encoding	 sc_src_encoding;
};

#endif
