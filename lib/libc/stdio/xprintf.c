/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2005 Poul-Henning Kamp
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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

#include "namespace.h"
#include <err.h>
#include <sys/types.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <locale.h>
#include <stdint.h>
#include <assert.h>
#include <stdarg.h>
#include <namespace.h>
#include <string.h>
#include <wchar.h>
#include "un-namespace.h"

#include "local.h"
#include "printf.h"
#include "fvwrite.h"

int __use_xprintf = -1;

/* private stuff -----------------------------------------------------*/

union arg {
	int			intarg;
	long			longarg;
	intmax_t 		intmaxarg;
#ifndef NO_FLOATING_POINT
	double			doublearg;
	long double 		longdoublearg;
#endif
	wint_t			wintarg;
	char			*pchararg;
	wchar_t			*pwchararg;
	void			*pvoidarg;
};

/*
 * Macros for converting digits to letters and vice versa
 */
#define	to_digit(c)	((c) - '0')
#define is_digit(c)	(((unsigned)to_digit(c)) <= 9)

/* various globals ---------------------------------------------------*/

const char __lowercase_hex[17] = "0123456789abcdef?";	/*lint !e784 */
const char __uppercase_hex[17] = "0123456789ABCDEF?";	/*lint !e784 */

#define PADSIZE 16
static char blanks[PADSIZE] =
	 {' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '};
static char zeroes[PADSIZE] =
	 {'0','0','0','0','0','0','0','0','0','0','0','0','0','0','0','0'};

/* printing and padding functions ------------------------------------*/

#define NIOV 8

struct __printf_io {
	FILE		*fp;
	struct __suio	uio;
	struct __siov	iov[NIOV];
	struct __siov	*iovp;
};

static void
__printf_init(struct __printf_io *io)
{

	io->uio.uio_iov = io->iovp = &io->iov[0];
	io->uio.uio_resid = 0;
	io->uio.uio_iovcnt = 0;
}

void
__printf_flush(struct __printf_io *io)
{

	__sfvwrite(io->fp, &io->uio);
	__printf_init(io);
}

int
__printf_puts(struct __printf_io *io, const void *ptr, int len)
{


	if (io->fp->_flags & __SERR)
		return (0);
	if (len == 0)
		return (0);
	io->iovp->iov_base = __DECONST(void *, ptr);
	io->iovp->iov_len = len;
	io->uio.uio_resid += len;
	io->iovp++;
	io->uio.uio_iovcnt++;
	if (io->uio.uio_iovcnt >= NIOV)
		__printf_flush(io);
	return (len);
}

int
__printf_pad(struct __printf_io *io, int howmany, int zero)
{
	int n;
	const char *with;
	int ret = 0;

	if (zero)
		with = zeroes;
	else
		with = blanks;

	if ((n = (howmany)) > 0) {
		while (n > PADSIZE) { 
			ret += __printf_puts(io, with, PADSIZE);
			n -= PADSIZE;
		}
		ret += __printf_puts(io, with, n);
	}
	return (ret);
}

int
__printf_out(struct __printf_io *io, const struct printf_info *pi, const void *ptr, int len)
{
	int ret = 0;

	if ((!pi->left) && pi->width > len)
		ret += __printf_pad(io, pi->width - len, pi->pad == '0');
	ret += __printf_puts(io, ptr, len);
	if (pi->left && pi->width > len)
		ret += __printf_pad(io, pi->width - len, pi->pad == '0');
	return (ret);
}


/* percent handling  -------------------------------------------------*/

static int
__printf_arginfo_pct(const struct printf_info *pi __unused, size_t n __unused, int *argt __unused)
{

	return (0);
}

static int
__printf_render_pct(struct __printf_io *io, const struct printf_info *pi __unused, const void *const *arg __unused)
{

	return (__printf_puts(io, "%", 1));
}

/* 'n' ---------------------------------------------------------------*/

static int
__printf_arginfo_n(const struct printf_info *pi, size_t n, int *argt)
{

	assert(n >= 1);
	argt[0] = PA_POINTER;
	return (1);
}

/*
 * This is a printf_render so that all output has been flushed before it
 * gets called.
 */

static int
__printf_render_n(FILE *io __unused, const struct printf_info *pi, const void *const *arg)
{

	if (pi->is_char)
		**((signed char **)arg[0]) = (signed char)pi->sofar;
	else if (pi->is_short)
		**((short **)arg[0]) = (short)pi->sofar;
	else if (pi->is_long)
		**((long **)arg[0]) = pi->sofar;
	else if (pi->is_long_double)
		**((long long **)arg[0]) = pi->sofar;
	else if (pi->is_intmax)
		**((intmax_t **)arg[0]) = pi->sofar;
	else if (pi->is_ptrdiff)
		**((ptrdiff_t **)arg[0]) = pi->sofar;
	else if (pi->is_quad)
		**((quad_t **)arg[0]) = pi->sofar;
	else if (pi->is_size)
		**((size_t **)arg[0]) = pi->sofar;
	else
		**((int **)arg[0]) = pi->sofar;

	return (0);
}

/* table -------------------------------------------------------------*/

/*lint -esym(785, printf_tbl) */
static struct {
	printf_arginfo_function	*arginfo;
	printf_function		*gnurender;
	printf_render		*render;
} printf_tbl[256] = {
	['%'] = { __printf_arginfo_pct,		NULL,	__printf_render_pct },
	['A'] = { __printf_arginfo_float,	NULL,	__printf_render_float },
	['C'] = { __printf_arginfo_chr,		NULL,	__printf_render_chr },
	['E'] = { __printf_arginfo_float,	NULL,	__printf_render_float },
	['F'] = { __printf_arginfo_float,	NULL,	__printf_render_float },
	['G'] = { __printf_arginfo_float,	NULL,	__printf_render_float },
	['S'] = { __printf_arginfo_str,		NULL,	__printf_render_str },
	['X'] = { __printf_arginfo_int,		NULL,	__printf_render_int },
	['a'] = { __printf_arginfo_float,	NULL,	__printf_render_float },
	['c'] = { __printf_arginfo_chr,		NULL,	__printf_render_chr },
	['d'] = { __printf_arginfo_int,		NULL,	__printf_render_int },
	['e'] = { __printf_arginfo_float,	NULL,	__printf_render_float },
	['f'] = { __printf_arginfo_float,	NULL,	__printf_render_float },
	['g'] = { __printf_arginfo_float,	NULL,	__printf_render_float },
	['i'] = { __printf_arginfo_int,		NULL,	__printf_render_int },
	['n'] = { __printf_arginfo_n,		__printf_render_n, NULL },
	['o'] = { __printf_arginfo_int,		NULL,	__printf_render_int },
	['p'] = { __printf_arginfo_ptr,		NULL,	__printf_render_ptr },
	['q'] = { __printf_arginfo_int,		NULL,	__printf_render_int },
	['s'] = { __printf_arginfo_str,		NULL,	__printf_render_str },
	['u'] = { __printf_arginfo_int,		NULL,	__printf_render_int },
	['x'] = { __printf_arginfo_int,		NULL,	__printf_render_int },
};


static int
__v2printf(FILE *fp, const char *fmt0, unsigned pct, va_list ap)
{
	struct printf_info	*pi, *pil;
	const char		*fmt;
	int			ch;
	struct printf_info	pia[pct + 10];
	int			argt[pct + 10];
	union arg		args[pct + 10];
	int			nextarg;
	int			maxarg;
	int			ret = 0;
	int			n;
	struct __printf_io	io;

	__printf_init(&io);
	io.fp = fp;

	fmt = fmt0;
	maxarg = 0;
	nextarg = 1;
	memset(argt, 0, sizeof argt);
	for (pi = pia; ; pi++) {
		memset(pi, 0, sizeof *pi);
		pil = pi;
		if (*fmt == '\0')
			break;
		pil = pi + 1;
		pi->prec = -1;
		pi->pad = ' ';
		pi->begin = pi->end = fmt;
		while (*fmt != '\0' && *fmt != '%')
			pi->end = ++fmt;
		if (*fmt == '\0') 
			break;
		fmt++;
		for (;;) {
			pi->spec = *fmt;
			switch (pi->spec) {
			case ' ':
				/*-
				 * ``If the space and + flags both appear, the space
				 * flag will be ignored.''
				 *      -- ANSI X3J11
				 */
				if (pi->showsign == 0)
					pi->showsign = ' ';
				fmt++;
				continue;
			case '#':
				pi->alt = 1;
				fmt++;
				continue;
			case '.':
				pi->prec = 0;
				fmt++;
				if (*fmt == '*') {
					fmt++;
					pi->get_prec = nextarg;
					argt[nextarg++] = PA_INT;
					continue;
				}
				while (*fmt != '\0' && is_digit(*fmt)) {
					pi->prec *= 10;
					pi->prec += to_digit(*fmt);
					fmt++;
				}
				continue;
			case '-':
				pi->left = 1;
				fmt++;
				continue;
			case '+':
				pi->showsign = '+';
				fmt++;
				continue;
			case '*':
				fmt++;
				pi->get_width = nextarg;
				argt[nextarg++] = PA_INT;
				continue;
			case '%':
				fmt++;
				break;
			case '\'':
				pi->group = 1;
				fmt++;
				continue;
			case '0':
				/*-
				 * ``Note that 0 is taken as a flag, not as the
				 * beginning of a field width.''
				 *      -- ANSI X3J11
				 */
				pi->pad = '0';
				fmt++;
				continue;
			case '1': case '2': case '3':
			case '4': case '5': case '6':
			case '7': case '8': case '9':
				n = 0;
				while (*fmt != '\0' && is_digit(*fmt)) {
					n *= 10;
					n += to_digit(*fmt);
					fmt++;
				}
				if (*fmt == '$') {
					if (nextarg > maxarg)
						maxarg = nextarg;
					nextarg = n;
					fmt++;
				} else 
					pi->width = n;
				continue;
			case 'D':
			case 'O':
			case 'U':
				pi->spec += ('a' - 'A');
				pi->is_intmax = 0;
				if (pi->is_long_double || pi->is_quad) {
					pi->is_long = 0;
					pi->is_long_double = 1;
				} else {
					pi->is_long = 1;
					pi->is_long_double = 0;
				}
				fmt++;
				break;
			case 'j':
				pi->is_intmax = 1;
				fmt++;
				continue;
			case 'q':
				pi->is_long = 0;
				pi->is_quad = 1;
				fmt++;
				continue;
			case 'L':
				pi->is_long_double = 1;
				fmt++;
				continue;
			case 'h':
				fmt++;
				if (*fmt == 'h') {
					fmt++;
					pi->is_char = 1;
				} else {
					pi->is_short = 1;
				}
				continue;
			case 'l':
				fmt++;
				if (*fmt == 'l') {
					fmt++;
					pi->is_long_double = 1;
					pi->is_quad = 0;
				} else {
					pi->is_quad = 0;
					pi->is_long = 1;
				}
				continue;
			case 't':
				pi->is_ptrdiff = 1;
				fmt++;
				continue;
			case 'z':
				pi->is_size = 1;
				fmt++;
				continue;
			default:
				fmt++;
				break;
			}
			if (printf_tbl[pi->spec].arginfo == NULL)
				errx(1, "arginfo[%c] = NULL", pi->spec);
			ch = printf_tbl[pi->spec].arginfo(
			    pi, __PRINTFMAXARG, &argt[nextarg]);
			if (ch > 0)
				pi->arg[0] = &args[nextarg];
			if (ch > 1)
				pi->arg[1] = &args[nextarg + 1];
			nextarg += ch;
			break;
		}
	}
	if (nextarg > maxarg)
		maxarg = nextarg;
#if 0
	fprintf(stderr, "fmt0 <%s>\n", fmt0);
	fprintf(stderr, "pil %p\n", pil);
#endif
	for (ch = 1; ch < maxarg; ch++) {
#if 0
		fprintf(stderr, "arg %d %x\n", ch, argt[ch]);
#endif
		switch(argt[ch]) {
		case PA_CHAR:
			args[ch].intarg = (char)va_arg (ap, int);
			break;
		case PA_INT:
			args[ch].intarg = va_arg (ap, int);
			break;
		case PA_INT | PA_FLAG_SHORT:
			args[ch].intarg = (short)va_arg (ap, int);
			break;
		case PA_INT | PA_FLAG_LONG:
			args[ch].longarg = va_arg (ap, long);
			break;
		case PA_INT | PA_FLAG_INTMAX:
			args[ch].intmaxarg = va_arg (ap, intmax_t);
			break;
		case PA_INT | PA_FLAG_QUAD:
			args[ch].intmaxarg = va_arg (ap, quad_t);
			break;
		case PA_INT | PA_FLAG_LONG_LONG:
			args[ch].intmaxarg = va_arg (ap, long long);
			break;
		case PA_INT | PA_FLAG_SIZE:
			args[ch].intmaxarg = va_arg (ap, size_t);
			break;
		case PA_INT | PA_FLAG_PTRDIFF:
			args[ch].intmaxarg = va_arg (ap, ptrdiff_t);
			break;
		case PA_WCHAR:
			args[ch].wintarg = va_arg (ap, wint_t);
			break;
		case PA_POINTER:
			args[ch].pvoidarg = va_arg (ap, void *);
			break;
		case PA_STRING:
			args[ch].pchararg = va_arg (ap, char *);
			break;
		case PA_WSTRING:
			args[ch].pwchararg = va_arg (ap, wchar_t *);
			break;
		case PA_DOUBLE:
#ifndef NO_FLOATING_POINT
			args[ch].doublearg = va_arg (ap, double);
#endif
			break;
		case PA_DOUBLE | PA_FLAG_LONG_DOUBLE:
#ifndef NO_FLOATING_POINT
			args[ch].longdoublearg = va_arg (ap, long double);
#endif
			break;
		default:
			errx(1, "argtype = %x (fmt = \"%s\")\n",
			    argt[ch], fmt0);
		}
	}
	for (pi = pia; pi < pil; pi++) {
#if 0
		fprintf(stderr, "pi %p", pi);
		fprintf(stderr, " spec '%c'", pi->spec);
		fprintf(stderr, " args %d",
		    ((uintptr_t)pi->arg[0] - (uintptr_t)args) / sizeof args[0]);
		if (pi->width) fprintf(stderr, " width %d", pi->width);
		if (pi->pad) fprintf(stderr, " pad 0x%x", pi->pad);
		if (pi->left) fprintf(stderr, " left");
		if (pi->showsign) fprintf(stderr, " showsign");
		if (pi->prec != -1) fprintf(stderr, " prec %d", pi->prec);
		if (pi->is_char) fprintf(stderr, " char");
		if (pi->is_short) fprintf(stderr, " short");
		if (pi->is_long) fprintf(stderr, " long");
		if (pi->is_long_double) fprintf(stderr, " long_double");
		fprintf(stderr, "\n");
		fprintf(stderr, "\t\"%.*s\"\n", pi->end - pi->begin, pi->begin);
#endif
		if (pi->get_width) {
			pi->width = args[pi->get_width].intarg;
			/*-
			 * ``A negative field width argument is taken as a
			 * - flag followed by a positive field width.''
			 *      -- ANSI X3J11
			 * They don't exclude field widths read from args.
			 */
			if (pi->width < 0) {
				pi->left = 1;
				pi->width = -pi->width;
			}
		}
		if (pi->get_prec) 
			pi->prec = args[pi->get_prec].intarg;
		ret += __printf_puts(&io, pi->begin, pi->end - pi->begin);
		if (printf_tbl[pi->spec].gnurender != NULL) {
			__printf_flush(&io);
			pi->sofar = ret;
			ret += printf_tbl[pi->spec].gnurender(
			    fp, pi, (const void *)pi->arg);
		} else if (printf_tbl[pi->spec].render != NULL) {
			pi->sofar = ret;
			n = printf_tbl[pi->spec].render(
			    &io, pi, (const void *)pi->arg);
			if (n < 0)
				io.fp->_flags |= __SERR;
			else
				ret += n;
		} else if (pi->begin == pi->end)
			errx(1, "render[%c] = NULL", *fmt);
	}
	__printf_flush(&io);
	return (ret);
}

extern int      __fflush(FILE *fp);

/*
 * Helper function for `fprintf to unbuffered unix file': creates a
 * temporary buffer.  We only work on write-only files; this avoids
 * worries about ungetc buffers and so forth.
 */
static int
__v3printf(FILE *fp, const char *fmt, int pct, va_list ap)
{
	int ret;
	FILE fake = FAKE_FILE;
	unsigned char buf[BUFSIZ];

	/* copy the important variables */
	fake._flags = fp->_flags & ~__SNBF;
	fake._file = fp->_file;
	fake._cookie = fp->_cookie;
	fake._write = fp->_write;
	fake._orientation = fp->_orientation;
	fake._mbstate = fp->_mbstate;

	/* set up the buffer */
	fake._bf._base = fake._p = buf;
	fake._bf._size = fake._w = sizeof(buf);
	fake._lbfsize = 0;	/* not actually used, but Just In Case */

	/* do the work, then copy any error status */
	ret = __v2printf(&fake, fmt, pct, ap);
	if (ret >= 0 && __fflush(&fake))
		ret = EOF;
	if (fake._flags & __SERR)
		fp->_flags |= __SERR;
	return (ret);
}

int
__xvprintf(FILE *fp, const char *fmt0, va_list ap)
{
	unsigned u;
	const char *p;

	/* Count number of '%' signs handling double '%' signs */
	for (p = fmt0, u = 0; *p; p++) {
		if (*p != '%')
			continue;
		u++;
		if (p[1] == '%')
			p++;
	}

	/* optimise fprintf(stderr) (and other unbuffered Unix files) */
	if ((fp->_flags & (__SNBF|__SWR|__SRW)) == (__SNBF|__SWR) &&
	    fp->_file >= 0)
		return (__v3printf(fp, fmt0, u, ap));
	else
		return (__v2printf(fp, fmt0, u, ap));
}

/* extending ---------------------------------------------------------*/

int
register_printf_function(int spec, printf_function *render, printf_arginfo_function *arginfo)
{

	if (spec > 255 || spec < 0)
		return (-1);
	printf_tbl[spec].gnurender = render;
	printf_tbl[spec].arginfo = arginfo;
	__use_xprintf = 1;
	return (0);
}

int
register_printf_render(int spec, printf_render *render, printf_arginfo_function *arginfo)
{

	if (spec > 255 || spec < 0)
		return (-1);
	printf_tbl[spec].render = render;
	printf_tbl[spec].arginfo = arginfo;
	__use_xprintf = 1;
	return (0);
}

int
register_printf_render_std(const char *specs)
{

	for (; *specs != '\0'; specs++) {
		switch (*specs) {
		case 'H':
			register_printf_render(*specs,
			    __printf_render_hexdump,
			    __printf_arginfo_hexdump);
			break;
		case 'M':
			register_printf_render(*specs,
			    __printf_render_errno,
			    __printf_arginfo_errno);
			break;
		case 'Q':
			register_printf_render(*specs,
			    __printf_render_quote,
			    __printf_arginfo_quote);
			break;
		case 'T':
			register_printf_render(*specs,
			    __printf_render_time,
			    __printf_arginfo_time);
			break;
		case 'V':
			register_printf_render(*specs,
			    __printf_render_vis,
			    __printf_arginfo_vis);
			break;
		default:
			return (-1);
		}
	}
	return (0);
}

