/*	$OpenBSD: inout.c,v 1.18 2014/12/01 13:13:00 deraadt Exp $	*/

/*
 * Copyright (c) 2003, Otto Moerbeek <otto@drijf.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <openssl/ssl.h>
#include <ctype.h>
#include <err.h>
#include <string.h>

#include "extern.h"

#define MAX_CHARS_PER_LINE 68

static int	 lastchar;
static int	 charcount;

static int	 src_getcharstream(struct source *);
static void	 src_ungetcharstream(struct source *);
static char	*src_getlinestream(struct source *);
static int	 src_getcharstring(struct source *);
static void	 src_ungetcharstring(struct source *);
static char	*src_getlinestring(struct source *);
static void	 src_freestring(struct source *);
static void	 flushwrap(FILE *);
static void	 putcharwrap(FILE *, int);
static void	 printwrap(FILE *, const char *);
static char	*get_digit(u_long, int, u_int);

static struct vtable stream_vtable = {
	src_getcharstream,
	src_ungetcharstream,
	src_getlinestream,
	NULL
};

static struct vtable string_vtable = {
	src_getcharstring,
	src_ungetcharstring,
	src_getlinestring,
	src_freestring
};

void
src_setstream(struct source *src, FILE *stream)
{

	src->u.stream = stream;
	src->vtable = &stream_vtable;
}

void
src_setstring(struct source *src, char *p)
{

	src->u.string.buf = (u_char *)p;
	src->u.string.pos = 0;
	src->vtable = &string_vtable;
}

static int
src_getcharstream(struct source *src)
{

	return (src->lastchar = getc(src->u.stream));
}

static void
src_ungetcharstream(struct source *src)
{

	ungetc(src->lastchar, src->u.stream);
}

static char *
src_getlinestream(struct source *src)
{
	char buf[BUFSIZ];

	if (fgets(buf, BUFSIZ, src->u.stream) == NULL)
		return (bstrdup(""));
	return bstrdup(buf);
}

static int
src_getcharstring(struct source *src)
{

	src->lastchar = src->u.string.buf[src->u.string.pos];
	if (src->lastchar == '\0')
		return (EOF);
	else {
		src->u.string.pos++;
		return (src->lastchar);
	}
}

static void
src_ungetcharstring(struct source *src)
{

	if (src->u.string.pos > 0) {
		if (src->lastchar != '\0')
			--src->u.string.pos;
	}
}

static char *
src_getlinestring(struct source *src)
{
	char buf[BUFSIZ];
	int i, ch;

	i = 0;
	while (i < BUFSIZ-1) {
		ch = src_getcharstring(src);
		if (ch == EOF)
			break;
		buf[i++] = ch;
		if (ch == '\n')
			break;
	}
	buf[i] = '\0';
	return (bstrdup(buf));
}

static void
src_freestring(struct source *src)
{

	free(src->u.string.buf);
}

static void
flushwrap(FILE *f)
{

	if (lastchar != -1)
		putc(lastchar, f);
}

static void
putcharwrap(FILE *f, int ch)
{

	if (charcount >= MAX_CHARS_PER_LINE) {
		charcount = 0;
		fputs("\\\n", f);
	}
	if (lastchar != -1) {
		charcount++;
		putc(lastchar, f);
	}
	lastchar = ch;
}

static void
printwrap(FILE *f, const char *p)
{
	char *q;
	char buf[12];

	q = buf;
	strlcpy(buf, p, sizeof(buf));
	while (*q)
		putcharwrap(f, *q++);
}

struct number *
readnumber(struct source *src, u_int base, u_int bscale)
{
	struct number *n;
	BN_ULONG v;
	int ch;
	u_int iscale = 0;
	bool dot = false, sign = false;

	n = new_number();
	bn_check(BN_zero(n->number));

	while ((ch = (*src->vtable->readchar)(src)) != EOF) {

		if ('0' <= ch && ch <= '9')
			v = ch - '0';
		else if ('A' <= ch && ch <= 'F')
			v = ch - 'A' + 10;
		else if (ch == '_') {
			sign = true;
			continue;
		} else if (ch == '.') {
			if (dot)
				break;
			dot = true;
			continue;
		} else {
			(*src->vtable->unreadchar)(src);
			break;
		}
		if (dot)
			iscale++;

		bn_check(BN_mul_word(n->number, base));
		bn_check(BN_add_word(n->number, v));
	}
	if (base == 10) {
		n->scale = iscale;
	} else {
		/* At this point, the desired result is n->number / base^iscale*/
		struct number *quotient, *divisor, *_n;
		BIGNUM *base_n, *exponent;
		BN_CTX *ctx;

		ctx = BN_CTX_new();
		base_n = BN_new();
		exponent = BN_new();
		divisor = new_number();
		bn_check(BN_zero(base_n));
		bn_check(BN_zero(exponent));

		bn_check(BN_add_word(base_n, base));
		bn_check(BN_add_word(exponent, iscale));
		bn_check(BN_exp(divisor->number, base_n, exponent, ctx));
		divisor->scale = 0;
		quotient = div_number(n, divisor, bscale);
		_n = n;
		n = quotient;

		/* 
		 * Trim off trailing zeros to yield the smallest scale without
		 * loss of accuracy
		 */
		while ( n->scale > 0 &&
			BN_mod_word(n->number, 10) == 0) {
			normalize(n, n->scale - 1);
		}

		free_number(_n);
		free_number(divisor);
		BN_CTX_free(ctx);
		BN_free(base_n);
		BN_free(exponent);
	}
	if (sign)
		negate(n);
	return (n);
}

char *
read_string(struct source *src)
{
	char *p;
	int count, ch, i, new_sz, sz;
	bool escape;

	escape = false;
	count = 1;
	i = 0;
	sz = 15;
	p = bmalloc(sz + 1);

	while ((ch = (*src->vtable->readchar)(src)) != EOF) {
		if (!escape) {
			if (ch == '[')
				count++;
			else if (ch == ']')
				count--;
			if (count == 0)
				break;
		}
		if (ch == '\\' && !escape)
			escape = true;
		else {
			escape = false;
			if (i == sz) {
				new_sz = sz * 2;
				p = breallocarray(p, 1, new_sz + 1);
				sz = new_sz;
			}
			p[i++] = ch;
		}
	}
	p[i] = '\0';
	return (p);
}

static char *
get_digit(u_long num, int digits, u_int base)
{
	char *p;

	if (base <= 16) {
		p = bmalloc(2);
		p[0] = num >= 10 ? num + 'A' - 10 : num + '0';
		p[1] = '\0';
	} else {
		if (asprintf(&p, "%0*lu", digits, num) == -1)
			err(1, NULL);
	}
	return (p);
}

void
printnumber(FILE *f, const struct number *b, u_int base)
{
	struct number *fract_part, *int_part;
	struct stack stack;
	char *p;
	char buf[11];
	size_t sz;
	unsigned int i;
	int digits;

	charcount = 0;
	lastchar = -1;
	if (BN_is_zero(b->number))
		putcharwrap(f, '0');

	int_part = new_number();
	fract_part = new_number();
	fract_part->scale = b->scale;

	if (base <= 16)
		digits = 1;
	else {
		digits = snprintf(buf, sizeof(buf), "%u", base-1);
	}
	split_number(b, int_part->number, fract_part->number);

	i = 0;
	stack_init(&stack);
	while (!BN_is_zero(int_part->number)) {
		BN_ULONG rem = BN_div_word(int_part->number, base);
		stack_pushstring(&stack, get_digit(rem, digits, base));
		i++;
	}
	sz = i;
	if (BN_is_negative(b->number))
		putcharwrap(f, '-');
	for (i = 0; i < sz; i++) {
		p = stack_popstring(&stack);
		if (base > 16)
			putcharwrap(f, ' ');
		printwrap(f, p);
		free(p);
	}
	stack_clear(&stack);
	if (b->scale > 0) {
		struct number *num_base;
		BIGNUM *mult, *stop;

		putcharwrap(f, '.');
		num_base = new_number();
		bn_check(BN_set_word(num_base->number, base));
		mult = BN_new();
		bn_checkp(mult);
		bn_check(BN_one(mult));
		stop = BN_new();
		bn_checkp(stop);
		bn_check(BN_one(stop));
		scale_number(stop, b->scale);

		i = 0;
		while (BN_cmp(mult, stop) < 0) {
			u_long rem;

			if (i && base > 16)
				putcharwrap(f, ' ');
			i = 1;

			bmul_number(fract_part, fract_part, num_base,
			    bmachine_scale());
			split_number(fract_part, int_part->number, NULL);
			rem = BN_get_word(int_part->number);
			p = get_digit(rem, digits, base);
			int_part->scale = 0;
			normalize(int_part, fract_part->scale);
			bn_check(BN_sub(fract_part->number, fract_part->number,
			    int_part->number));
			printwrap(f, p);
			free(p);
			bn_check(BN_mul_word(mult, base));
		}
		free_number(num_base);
		BN_free(mult);
		BN_free(stop);
	}
	flushwrap(f);
	free_number(int_part);
	free_number(fract_part);
}

void
print_value(FILE *f, const struct value *value, const char *prefix, u_int base)
{

	fputs(prefix, f);
	switch (value->type) {
	case BCODE_NONE:
		if (value->array != NULL)
			fputs("<array>", f);
		break;
	case BCODE_NUMBER:
		printnumber(f, value->u.num, base);
		break;
	case BCODE_STRING:
		fputs(value->u.string, f);
		break;
	}
}

void
print_ascii(FILE *f, const struct number *n)
{
	BIGNUM *v;
	int ch, i, numbits;

	v = BN_dup(n->number);
	bn_checkp(v);

	if (BN_is_negative(v))
		BN_set_negative(v, 0);

	numbits = BN_num_bytes(v) * 8;
	while (numbits > 0) {
		ch = 0;
		for (i = 0; i < 8; i++)
			ch |= BN_is_bit_set(v, numbits-i-1) << (7 - i);
		putc(ch, f);
		numbits -= 8;
	}
	BN_free(v);
}
