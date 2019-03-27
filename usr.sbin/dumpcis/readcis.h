/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1995 Andrew McRae.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

struct tuple {
	struct tuple *next;
	unsigned char code;
	unsigned char length;
	unsigned char *data;
};

struct tuple_list {
	struct tuple_list *next;
	struct tuple *tuples;
	off_t   offs;
	int     flags;
};

struct tuple_info {
	const char   *name;
	unsigned char code;
	unsigned char length;		/* 255 means variable length */
};

#define	tpl32(tp)	((*((tp) + 3) << 24) | \
			 (*((tp) + 2) << 16) | \
			 (*((tp) + 1) << 8)  | *(tp))
#define	tpl24(tp)	((*((tp) + 2) << 16) | \
			 (*((tp) + 1) << 8)  | *(tp))
#define	tpl16(tp)	((*((tp) + 1) << 8)  | *(tp))

void    dumpcis(struct tuple_list *);
void    freecis(struct tuple_list *);
struct tuple_list *readcis(int);

const char *tuple_name(unsigned char);
u_int   parse_num(int, u_char *, u_char **, int);
