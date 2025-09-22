/*	$OpenBSD: nl_types.h,v 1.8 2008/06/26 05:42:04 ray Exp $	*/
/*	$NetBSD: nl_types.h,v 1.6 1996/05/13 23:11:15 jtc Exp $	*/

/*-
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by J.T. Conklin.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _NL_TYPES_H_
#define _NL_TYPES_H_
#include <sys/cdefs.h>

#ifdef _NLS_PRIVATE
/*
 * MESSAGE CATALOG FILE FORMAT.
 *
 * The NetBSD message catalog format is similar to the format used by
 * Svr4 systems.  The differences are:
 *   * fixed byte order (big endian)
 *   * fixed data field sizes
 *
 * A message catalog contains four data types: a catalog header, one
 * or more set headers, one or more message headers, and one or more
 * text strings.
 */

#define _NLS_MAGIC	0xff88ff89

struct _nls_cat_hdr {
	int32_t __magic;
	int32_t __nsets;
	int32_t __mem;
	int32_t __msg_hdr_offset;
	int32_t __msg_txt_offset;
} ;

struct _nls_set_hdr {
	int32_t __setno;	/* set number: 0 < x <= NL_SETMAX */
	int32_t __nmsgs;	/* number of messages in the set  */
	int32_t __index;	/* index of first msg_hdr in msg_hdr table */
} ;

struct _nls_msg_hdr {
	int32_t __msgno;	/* msg number: 0 < x <= NL_MSGMAX */
	int32_t __msglen;
	int32_t __offset;
} ;

#endif

#define	NL_SETD		1
#define NL_CAT_LOCALE   1

typedef struct _nl_catd {
	void	*__data;
	int	__size;
} *nl_catd;

typedef long	nl_item;

__BEGIN_DECLS
extern nl_catd 	catopen(const char *, int);
extern char    *catgets(nl_catd, int, int, const char *);
extern int	catclose(nl_catd);
__END_DECLS

#endif	/* _NL_TYPES_H_ */
