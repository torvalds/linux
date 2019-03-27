/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Poul-Henning Kamp
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

#ifndef _PRINTF_H_
#define _PRINTF_H_

#include <stdio.h>
#include <wchar.h>

/*
 * The API defined by glibc allows a renderer to take multiple arguments
 * This is obviously usable for things like (ptr+len) pairs etc.
 * But the do not actually provide support for it at the end of the day,
 * they offer only one argument to the arginfo function, but do accept
 * >1 returns, although the do not check the types of those arguments
 * argument
 * Be compatible for now.
 */
#define __PRINTFMAXARG		2

struct printf_info {
	/* GLIBC compatible */
	int		prec;
	int		width;
	wchar_t		spec;
	unsigned 	is_long_double;
	unsigned 	is_char;
	unsigned	is_short;
	unsigned	is_long;
	unsigned	alt;
	unsigned	space;
	unsigned	left;
	unsigned	showsign;
	unsigned	group;
	unsigned	extra;
	unsigned	wide;
	wchar_t		pad;

	/* FreeBSD extensions */

	unsigned	is_quad;
	unsigned	is_intmax;
	unsigned	is_ptrdiff;
	unsigned	is_size;

	/* private */
	int		sofar;
	unsigned	get_width;
	unsigned	get_prec;
	const char	*begin;
	const char	*end;
	void 		*arg[__PRINTFMAXARG];
};

enum {
	PA_INT		= (1 << 0),	/* int */
	PA_CHAR		= (1 << 1),	/* int, cast to char */
	PA_WCHAR	= (1 << 2),	/* wide char */
	PA_STRING	= (1 << 3),	/* const char * (with '\0') */
	PA_WSTRING	= (1 << 4),	/* const wchar_t * */
	PA_POINTER	= (1 << 5),	/* void * */
	PA_FLOAT	= (1 << 6),	/* float */
	PA_DOUBLE	= (1 << 7) 	/* double */
};

#define	PA_FLAG_MASK		0xff0000
#define	PA_FLAG_LONG_LONG	(1 << 16)
#define	PA_FLAG_LONG		(1 << 17)
#define	PA_FLAG_SHORT		(1 << 18)
#define	PA_FLAG_PTR		(1 << 19)
#define	PA_FLAG_QUAD		(1 << 20)
#define	PA_FLAG_INTMAX		(1 << 21)
#define	PA_FLAG_SIZE		(1 << 22)
#define	PA_FLAG_PTRDIFF		(1 << 23)
#define	PA_FLAG_LONG_DOUBLE	PA_FLAG_LONG_LONG

typedef int printf_arginfo_function(const struct printf_info *, size_t, int *);
typedef int printf_function(FILE *, const struct printf_info *, const void *const *);

/* FreeBSD extension */
struct __printf_io;
typedef int printf_render(struct __printf_io *, const struct printf_info *, const void *const *);

/* vprintf.c */
extern const char __lowercase_hex[17];
extern const char __uppercase_hex[17];

void __printf_flush(struct __printf_io *io);
int __printf_puts(struct __printf_io *io, const void *ptr, int len);
int __printf_pad(struct __printf_io *io, int n, int zero);
int __printf_out(struct __printf_io *io, const struct printf_info *pi, const void *ptr, int len);

int __xvprintf(FILE *fp, const char *fmt0, va_list ap);
extern int __use_xprintf;

/* GLIBC compat */
int register_printf_function(int spec, printf_function *render, printf_arginfo_function *arginfo);

/* FreeBSD */
int register_printf_render(int spec, printf_render *render, printf_arginfo_function *arginfo);
int register_printf_render_std(const char *specs);

/* vprintf_errno.c */
printf_arginfo_function		__printf_arginfo_errno;
printf_render			__printf_render_errno;

/* vprintf_float.c */
printf_arginfo_function		__printf_arginfo_float;
printf_render			__printf_render_float;

/* vprintf_hexdump.c */
printf_arginfo_function		__printf_arginfo_hexdump;
printf_render 			__printf_render_hexdump;

/* vprintf_int.c */
printf_arginfo_function		__printf_arginfo_ptr;
printf_arginfo_function		__printf_arginfo_int;
printf_render			__printf_render_ptr;
printf_render			__printf_render_int;

/* vprintf_quoute.c */
printf_arginfo_function		__printf_arginfo_quote;
printf_render 			__printf_render_quote;

/* vprintf_str.c */
printf_arginfo_function		__printf_arginfo_chr;
printf_render			__printf_render_chr;
printf_arginfo_function		__printf_arginfo_str;
printf_render			__printf_render_str;

/* vprintf_time.c */
printf_arginfo_function		__printf_arginfo_time;
printf_render			__printf_render_time;

/* vprintf_vis.c */
printf_arginfo_function		__printf_arginfo_vis;
printf_render 			__printf_render_vis;

#endif /* !_PRINTF_H */
