/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2010 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 1995 Alex Tatmanjants <alex@elvisti.kiev.ua>
 *		at Electronni Visti IA, Kiev, Ukraine.
 *			All rights reserved.
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 * Portions of this software were developed by David Chisnall
 * under sponsorship from the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE
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

#ifndef _COLLATE_H_
#define	_COLLATE_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <limits.h>
#include "xlocale_private.h"

/*
 * Work around buildworld bootstrapping from older systems whose limits.h
 * sets COLL_WEIGHTS_MAX to 0.
 */
#if COLL_WEIGHTS_MAX == 0
#undef COLL_WEIGHTS_MAX
#define COLL_WEIGHTS_MAX 10
#endif

#define	COLLATE_STR_LEN		24		/* should be 64-bit multiple */
#define	COLLATE_VERSION		"BSD 1.0\n"

#define	COLLATE_MAX_PRIORITY	(0x7fffffff)	/* max signed value */
#define	COLLATE_SUBST_PRIORITY	(0x40000000)	/* bit indicates subst table */

#define	DIRECTIVE_UNDEF		0x00
#define	DIRECTIVE_FORWARD	0x01
#define	DIRECTIVE_BACKWARD	0x02
#define	DIRECTIVE_POSITION	0x04
#define	DIRECTIVE_UNDEFINED	0x08	/* special last weight for UNDEFINED */

#define	DIRECTIVE_DIRECTION_MASK (DIRECTIVE_FORWARD | DIRECTIVE_BACKWARD)

/*
 * The collate file format is as follows:
 *
 * char		version[COLLATE_STR_LEN];	// must be COLLATE_VERSION
 * collate_info_t	info;			// see below, includes padding
 * collate_char_pri_t	char_data[256];		// 8 bit char values
 * collate_subst_t	subst[*];		// 0 or more substitutions
 * collate_chain_pri_t	chains[*];		// 0 or more chains
 * collate_large_pri_t	large[*];		// extended char priorities
 *
 * Note that all structures must be 32-bit aligned, as each structure
 * contains 32-bit member fields.  The entire file is mmap'd, so its
 * critical that alignment be observed.  It is not generally safe to
 * use any 64-bit values in the structures.
 */

typedef struct collate_info {
	uint8_t directive_count;
	uint8_t directive[COLL_WEIGHTS_MAX];
	int32_t pri_count[COLL_WEIGHTS_MAX];
	int32_t flags;
	int32_t chain_count;
	int32_t large_count;
	int32_t subst_count[COLL_WEIGHTS_MAX];
	int32_t undef_pri[COLL_WEIGHTS_MAX];
} collate_info_t;

typedef struct collate_char {
	int32_t pri[COLL_WEIGHTS_MAX];
} collate_char_t;

typedef struct collate_chain {
	wchar_t str[COLLATE_STR_LEN];
	int32_t pri[COLL_WEIGHTS_MAX];
} collate_chain_t;

typedef struct collate_large {
	int32_t val;
	collate_char_t pri;
} collate_large_t;

typedef struct collate_subst {
	int32_t key;
	int32_t pri[COLLATE_STR_LEN];
} collate_subst_t;

struct xlocale_collate {
	struct xlocale_component header;
	int __collate_load_error;
	char * map;
	size_t maplen;

	collate_info_t	*info;
	collate_char_t	*char_pri_table;
	collate_large_t	*large_pri_table;
	collate_chain_t	*chain_pri_table;
	collate_subst_t	*subst_table[COLL_WEIGHTS_MAX];
};

__BEGIN_DECLS
int	__collate_load_tables(const char *);
int	__collate_equiv_value(locale_t, const wchar_t *, size_t);
void	_collate_lookup(struct xlocale_collate *,const wchar_t *, int *, int *,
	int, const int **);
int	__collate_range_cmp(char, char);
int	__wcollate_range_cmp(wchar_t, wchar_t);
size_t	_collate_wxfrm(struct xlocale_collate *, const wchar_t *, wchar_t *,
	size_t);
size_t	_collate_sxfrm(struct xlocale_collate *, const wchar_t *, char *,
	size_t);
__END_DECLS

#endif /* !_COLLATE_H_ */
