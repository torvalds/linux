/*	$OpenBSD: ber.c,v 1.26 2023/11/10 12:12:02 martijn Exp $ */

/*
 * Copyright (c) 2007, 2012 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2006, 2007 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2006, 2007 Marc Balmer <mbalmer@openbsd.org>
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

#include <sys/types.h>

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <err.h>	/* XXX for debug output */
#include <stdio.h>	/* XXX for debug output */
#include <string.h>
#include <unistd.h>
#include <stdarg.h>

#include "ber.h"

#define BER_TYPE_CONSTRUCTED	0x20	/* otherwise primitive */
#define BER_TYPE_SINGLE_MAX	30
#define BER_TAG_MASK		0x1f
#define BER_TAG_MORE		0x80	/* more subsequent octets */
#define BER_TAG_TYPE_MASK	0x7f
#define BER_CLASS_SHIFT		6

static int	ober_dump_element(struct ber *ber, struct ber_element *root);
static void	ober_dump_header(struct ber *ber, struct ber_element *root);
static void	ober_putc(struct ber *ber, u_char c);
static void	ober_write(struct ber *ber, void *buf, size_t len);
static ssize_t	get_id(struct ber *b, unsigned int *tag, int *class,
    int *cstruct);
static ssize_t	get_len(struct ber *b, ssize_t *len);
static ssize_t	ober_read_element(struct ber *ber, struct ber_element *elm);
static ssize_t	ober_getc(struct ber *b, u_char *c);
static ssize_t	ober_read(struct ber *ber, void *buf, size_t len);

#ifdef DEBUG
#define DPRINTF(...)	printf(__VA_ARGS__)
#else
#define DPRINTF(...)	do { } while (0)
#endif

struct ber_element *
ober_get_element(unsigned int encoding)
{
	struct ber_element *elm;

	if ((elm = calloc(1, sizeof(*elm))) == NULL)
		return NULL;

	elm->be_encoding = encoding;
	ober_set_header(elm, BER_CLASS_UNIVERSAL, BER_TYPE_DEFAULT);

	return elm;
}

void
ober_set_header(struct ber_element *elm, int class, unsigned int type)
{
	elm->be_class = class & BER_CLASS_MASK;
	if (type == BER_TYPE_DEFAULT)
		type = elm->be_encoding;
	elm->be_type = type;
}

void
ober_link_elements(struct ber_element *prev, struct ber_element *elm)
{
	if (prev != NULL) {
		if ((prev->be_encoding == BER_TYPE_SEQUENCE ||
		    prev->be_encoding == BER_TYPE_SET) &&
		    prev->be_sub == NULL)
			prev->be_sub = elm;
		else
			prev->be_next = elm;
	}
}

struct ber_element *
ober_unlink_elements(struct ber_element *prev)
{
	struct ber_element *elm;

	if ((prev->be_encoding == BER_TYPE_SEQUENCE ||
	    prev->be_encoding == BER_TYPE_SET) &&
	    prev->be_sub != NULL) {
		elm = prev->be_sub;
		prev->be_sub = NULL;
	} else {
		elm = prev->be_next;
		prev->be_next = NULL;
	}

	return (elm);
}

void
ober_replace_elements(struct ber_element *prev, struct ber_element *new)
{
	struct ber_element *ber, *next;

	ber = ober_unlink_elements(prev);
	next = ober_unlink_elements(ber);
	ober_link_elements(new, next);
	ober_link_elements(prev, new);

	/* cleanup old element */
	ober_free_elements(ber);
}

struct ber_element *
ober_add_sequence(struct ber_element *prev)
{
	struct ber_element *elm;

	if ((elm = ober_get_element(BER_TYPE_SEQUENCE)) == NULL)
		return NULL;

	ober_link_elements(prev, elm);

	return elm;
}

struct ber_element *
ober_add_set(struct ber_element *prev)
{
	struct ber_element *elm;

	if ((elm = ober_get_element(BER_TYPE_SET)) == NULL)
		return NULL;

	ober_link_elements(prev, elm);

	return elm;
}

struct ber_element *
ober_add_enumerated(struct ber_element *prev, long long val)
{
	struct ber_element *elm;
	u_int i, len = 0;
	u_char cur, last = 0;

	if ((elm = ober_get_element(BER_TYPE_ENUMERATED)) == NULL)
		return NULL;

	elm->be_numeric = val;

	for (i = 0; i < sizeof(long long); i++) {
		cur = val & 0xff;
		if (cur != 0 && cur != 0xff)
			len = i;
		if ((cur == 0 && last & 0x80) ||
		    (cur == 0xff && (last & 0x80) == 0))
			len = i;
		val >>= 8;
		last = cur;
	}
	elm->be_len = len + 1;

	ober_link_elements(prev, elm);

	return elm;
}

struct ber_element *
ober_add_integer(struct ber_element *prev, long long val)
{
	struct ber_element *elm;
	u_int i, len = 0;
	u_char cur, last = 0;

	if ((elm = ober_get_element(BER_TYPE_INTEGER)) == NULL)
		return NULL;

	elm->be_numeric = val;

	for (i = 0; i < sizeof(long long); i++) {
		cur = val & 0xff;
		if (cur != 0 && cur != 0xff)
			len = i;
		if ((cur == 0 && last & 0x80) ||
		    (cur == 0xff && (last & 0x80) == 0))
			len = i;
		val >>= 8;
		last = cur;
	}
	elm->be_len = len + 1;

	ober_link_elements(prev, elm);

	return elm;
}

int
ober_get_integer(struct ber_element *elm, long long *n)
{
	if (elm->be_encoding != BER_TYPE_INTEGER)
		return -1;

	if (n != NULL)
		*n = elm->be_numeric;
	return 0;
}

int
ober_get_enumerated(struct ber_element *elm, long long *n)
{
	if (elm->be_encoding != BER_TYPE_ENUMERATED)
		return -1;

	if (n != NULL)
		*n = elm->be_numeric;
	return 0;
}

struct ber_element *
ober_add_boolean(struct ber_element *prev, int bool)
{
	struct ber_element *elm;

	if ((elm = ober_get_element(BER_TYPE_BOOLEAN)) == NULL)
		return NULL;

	elm->be_numeric = bool ? 0xff : 0;
	elm->be_len = 1;

	ober_link_elements(prev, elm);

	return elm;
}

int
ober_get_boolean(struct ber_element *elm, int *b)
{
	if (elm->be_encoding != BER_TYPE_BOOLEAN)
		return -1;

	if (b != NULL)
		*b = !(elm->be_numeric == 0);
	return 0;
}

struct ber_element *
ober_add_string(struct ber_element *prev, const char *string)
{
	return ober_add_nstring(prev, string, strlen(string));
}

struct ber_element *
ober_add_nstring(struct ber_element *prev, const char *string0, size_t len)
{
	struct ber_element *elm;
	char *string;

	if ((string = calloc(1, len + 1)) == NULL)
		return NULL;
	if ((elm = ober_get_element(BER_TYPE_OCTETSTRING)) == NULL) {
		free(string);
		return NULL;
	}

	bcopy(string0, string, len);
	elm->be_val = string;
	elm->be_len = len;
	elm->be_free = 1;		/* free string on cleanup */

	ober_link_elements(prev, elm);

	return elm;
}

struct ber_element *
ober_add_ostring(struct ber_element *prev, struct ber_octetstring *s)
{
	return ober_add_nstring(prev, s->ostr_val, s->ostr_len);
}

int
ober_get_string(struct ber_element *elm, char **s)
{
	if (elm->be_encoding != BER_TYPE_OCTETSTRING)
		return -1;
	/* XXX some components use getstring on binary data containing \0 */
#if 0
	if (memchr(elm->be_val, '\0', elm->be_len) != NULL)
		return -1;
#endif

	if (s != NULL)
		*s = elm->be_val;
	return 0;
}

int
ober_get_nstring(struct ber_element *elm, void **p, size_t *len)
{
	if (elm->be_encoding != BER_TYPE_OCTETSTRING)
		return -1;

	if (len != NULL)
		*len = elm->be_len;
	if (p != NULL) {
		if (len != NULL)
			*p = elm->be_val;
		else
			*p = NULL;
	}
	return 0;
}

int
ober_get_ostring(struct ber_element *elm, struct ber_octetstring *s)
{
	if (elm->be_encoding != BER_TYPE_OCTETSTRING)
		return -1;

	if (s != NULL) {
		s->ostr_val = elm->be_val;
		s->ostr_len = elm->be_len;
	}
	return 0;
}

struct ber_element *
ober_add_bitstring(struct ber_element *prev, const void *v0, size_t len)
{
	struct ber_element *elm;
	void *v;

	if ((v = calloc(1, len)) == NULL)
		return NULL;
	if ((elm = ober_get_element(BER_TYPE_BITSTRING)) == NULL) {
		free(v);
		return NULL;
	}

	bcopy(v0, v, len);
	elm->be_val = v;
	elm->be_len = len;
	elm->be_free = 1;		/* free string on cleanup */

	ober_link_elements(prev, elm);

	return elm;
}

int
ober_get_bitstring(struct ber_element *elm, void **v, size_t *len)
{
	if (elm->be_encoding != BER_TYPE_BITSTRING)
		return -1;

	if (len != NULL)
		*len = elm->be_len;
	if (v != NULL) {
		if (len != NULL)
			*v = elm->be_val;
		else
			*v = NULL;
	}
	return 0;
}

struct ber_element *
ober_add_null(struct ber_element *prev)
{
	struct ber_element *elm;

	if ((elm = ober_get_element(BER_TYPE_NULL)) == NULL)
		return NULL;

	ober_link_elements(prev, elm);

	return elm;
}

int
ober_get_null(struct ber_element *elm)
{
	if (elm->be_encoding != BER_TYPE_NULL)
		return -1;

	return 0;
}

struct ber_element *
ober_add_eoc(struct ber_element *prev)
{
	struct ber_element *elm;

	if ((elm = ober_get_element(BER_TYPE_EOC)) == NULL)
		return NULL;

	ober_link_elements(prev, elm);

	return elm;
}

int
ober_get_eoc(struct ber_element *elm)
{
	if (elm->be_encoding != BER_TYPE_EOC)
		return -1;

	return 0;
}

size_t
ober_oid2ber(struct ber_oid *o, u_int8_t *buf, size_t len)
{
	u_int32_t	 v;
	u_int		 i, j = 0, k;

	if (o->bo_n < BER_MIN_OID_LEN || o->bo_n > BER_MAX_OID_LEN ||
	    o->bo_id[0] > 2 || o->bo_id[1] > 40)
		return (0);

	v = (o->bo_id[0] * 40) + o->bo_id[1];
	for (i = 2, j = 0; i <= o->bo_n; v = o->bo_id[i], i++) {
		for (k = 28; k >= 7; k -= 7) {
			if (v >= (u_int)(1 << k)) {
				if (len)
					buf[j] = v >> k | BER_TAG_MORE;
				j++;
			}
		}
		if (len)
			buf[j] = v & BER_TAG_TYPE_MASK;
		j++;
	}

	return (j);
}

int
ober_string2oid(const char *oidstr, struct ber_oid *o)
{
	char			*sp, *p, str[BUFSIZ];
	const char		*errstr;

	if (strlcpy(str, oidstr, sizeof(str)) >= sizeof(str))
		return (-1);
	memset(o, 0, sizeof(*o));

	/* Parse OID strings in the common forms n.n.n, n_n_n_n, or n-n-n */
	for (p = sp = str; p != NULL; sp = p) {
		if ((p = strpbrk(p, "._-")) != NULL)
			*p++ = '\0';
		o->bo_id[o->bo_n++] = strtonum(sp, 0, UINT_MAX, &errstr);
		if (errstr || o->bo_n > BER_MAX_OID_LEN)
			return (-1);
	}

	return (0);
}

int
ober_oid_cmp(struct ber_oid *a, struct ber_oid *b)
{
	size_t	 i, min;

	min = a->bo_n < b->bo_n ? a->bo_n : b->bo_n;
	for (i = 0; i < min; i++) {
		if (a->bo_id[i] < b->bo_id[i])
			return (-1);
		if (a->bo_id[i] > b->bo_id[i])
			return (1);
	}
	/* a is parent of b */
	if (a->bo_n < b->bo_n)
		return (-2);
	/* a is child of b */
	if (a->bo_n > b->bo_n)
		return 2;
	return (0);
}

struct ber_element *
ober_add_oid(struct ber_element *prev, struct ber_oid *o)
{
	struct ber_element	*elm;
	u_int8_t		*buf;
	size_t			 len;

	if ((elm = ober_get_element(BER_TYPE_OBJECT)) == NULL)
		return (NULL);

	if ((len = ober_oid2ber(o, NULL, 0)) == 0)
		goto fail;

	if ((buf = calloc(1, len)) == NULL)
		goto fail;

	elm->be_val = buf;
	elm->be_len = len;
	elm->be_free = 1;

	if (ober_oid2ber(o, buf, len) != len)
		goto fail;

	ober_link_elements(prev, elm);

	return (elm);

 fail:
	ober_free_elements(elm);
	return (NULL);
}

struct ber_element *
ober_add_noid(struct ber_element *prev, struct ber_oid *o, int n)
{
	struct ber_oid		 no;

	if (n > BER_MAX_OID_LEN)
		return (NULL);
	no.bo_n = n;
	bcopy(&o->bo_id, &no.bo_id, sizeof(no.bo_id));

	return (ober_add_oid(prev, &no));
}

struct ber_element *
ober_add_oidstring(struct ber_element *prev, const char *oidstr)
{
	struct ber_oid		 o;

	if (ober_string2oid(oidstr, &o) == -1)
		return (NULL);

	return (ober_add_oid(prev, &o));
}

int
ober_get_oid(struct ber_element *elm, struct ber_oid *o)
{
	u_int8_t	*buf;
	size_t		 len, i = 0, j = 0;

	if (elm->be_encoding != BER_TYPE_OBJECT)
		return (-1);

	if (o == NULL)
		return 0;

	buf = elm->be_val;
	len = elm->be_len;

	memset(o, 0, sizeof(*o));
	o->bo_id[j++] = buf[i] / 40;
	o->bo_id[j++] = buf[i++] % 40;
	for (; i < len && j < BER_MAX_OID_LEN; i++) {
		o->bo_id[j] = (o->bo_id[j] << 7) + (buf[i] & ~0x80);
		if (buf[i] & 0x80)
			continue;
		j++;
	}
	o->bo_n = j;

	return (0);
}

#define _MAX_SEQ		 128
struct ber_element *
ober_printf_elements(struct ber_element *ber, char *fmt, ...)
{
	va_list			 ap;
	int			 d, class, level = 0;
	size_t			 len;
	unsigned int		 type;
	long long		 i;
	char			*s;
	void			*p;
	struct ber_oid		*o;
	struct ber_element	*parent[_MAX_SEQ], *e;
	struct ber_element	*origber = ber, *firstber = NULL;

	memset(parent, 0, sizeof(struct ber_element *) * _MAX_SEQ);

	va_start(ap, fmt);
	
	while (*fmt) {
		switch (*fmt++) {
		case 'B':
			p = va_arg(ap, void *);
			len = va_arg(ap, size_t);
			if ((ber = ober_add_bitstring(ber, p, len)) == NULL)
				goto fail;
			break;
		case 'b':
			d = va_arg(ap, int);
			if ((ber = ober_add_boolean(ber, d)) == NULL)
				goto fail;
			break;
		case 'd':
			d = va_arg(ap, int);
			if ((ber = ober_add_integer(ber, d)) == NULL)
				goto fail;
			break;
		case 'e':
			e = va_arg(ap, struct ber_element *);
			ober_link_elements(ber, e);
			break;
		case 'E':
			i = va_arg(ap, long long);
			if ((ber = ober_add_enumerated(ber, i)) == NULL)
				goto fail;
			break;
		case 'i':
			i = va_arg(ap, long long);
			if ((ber = ober_add_integer(ber, i)) == NULL)
				goto fail;
			break;
		case 'O':
			o = va_arg(ap, struct ber_oid *);
			if ((ber = ober_add_oid(ber, o)) == NULL)
				goto fail;
			break;
		case 'o':
			s = va_arg(ap, char *);
			if ((ber = ober_add_oidstring(ber, s)) == NULL)
				goto fail;
			break;
		case 's':
			s = va_arg(ap, char *);
			if ((ber = ober_add_string(ber, s)) == NULL)
				goto fail;
			break;
		case 't':
			class = va_arg(ap, int);
			type = va_arg(ap, unsigned int);
			ober_set_header(ber, class, type);
			break;
		case 'x':
			s = va_arg(ap, char *);
			len = va_arg(ap, size_t);
			if ((ber = ober_add_nstring(ber, s, len)) == NULL)
				goto fail;
			break;
		case '0':
			if ((ber = ober_add_null(ber)) == NULL)
				goto fail;
			break;
		case '{':
			if (level >= _MAX_SEQ-1)
				goto fail;
			if ((ber= ober_add_sequence(ber)) == NULL)
				goto fail;
			parent[level++] = ber;
			break;
		case '(':
			if (level >= _MAX_SEQ-1)
				goto fail;
			if ((ber = ober_add_set(ber)) == NULL)
				goto fail;
			parent[level++] = ber;
			break;
		case '}':
		case ')':
			if (level <= 0 || parent[level - 1] == NULL)
				goto fail;
			ber = parent[--level];
			break;
		case '.':
			if ((e = ober_add_eoc(ber)) == NULL)
				goto fail;
			ber = e;
			break;
		default:
			break;
		}
		if (firstber == NULL)
			firstber = ber;
	}
	va_end(ap);

	return (ber);
 fail:
	if (origber != NULL)
		ober_unlink_elements(origber);
	ober_free_elements(firstber);
	return (NULL);
}

int
ober_scanf_elements(struct ber_element *ber, char *fmt, ...)
{
	va_list			 ap;
	int			*d, level = -1;
	unsigned int		*t;
	long long		*i, l;
	void			**ptr;
	size_t			*len, ret = 0, n = strlen(fmt);
	char			**s;
	off_t			*pos;
	struct ber_oid		*o;
	struct ber_element	*parent[_MAX_SEQ], **e;

	memset(parent, 0, sizeof(struct ber_element *) * _MAX_SEQ);

	va_start(ap, fmt);
	while (*fmt) {
		if (ber == NULL && *fmt != '$' && *fmt != '}' && *fmt != ')')
			goto fail;
		switch (*fmt++) {
		case '$':
			if (ber != NULL)
				goto fail;
			ret++;
			continue;
		case 'B':
			ptr = va_arg(ap, void **);
			len = va_arg(ap, size_t *);
			if (ober_get_bitstring(ber, ptr, len) == -1)
				goto fail;
			ret++;
			break;
		case 'b':
			d = va_arg(ap, int *);
			if (ober_get_boolean(ber, d) == -1)
				goto fail;
			ret++;
			break;
		case 'd':
			d = va_arg(ap, int *);
			if (ober_get_integer(ber, &l) == -1)
				goto fail;
			if (d != NULL)
				*d = l;
			ret++;
			break;
		case 'e':
			e = va_arg(ap, struct ber_element **);
			*e = ber;
			ret++;
			continue;
		case 'E':
			i = va_arg(ap, long long *);
			if (ober_get_enumerated(ber, i) == -1)
				goto fail;
			ret++;
			break;
		case 'i':
			i = va_arg(ap, long long *);
			if (ober_get_integer(ber, i) == -1)
				goto fail;
			ret++;
			break;
		case 'o':
			o = va_arg(ap, struct ber_oid *);
			if (ober_get_oid(ber, o) == -1)
				goto fail;
			ret++;
			break;
		case 'S':
			ret++;
			break;
		case 's':
			s = va_arg(ap, char **);
			if (ober_get_string(ber, s) == -1)
				goto fail;
			ret++;
			break;
		case 't':
			d = va_arg(ap, int *);
			t = va_arg(ap, unsigned int *);
			if (d != NULL)
				*d = ber->be_class;
			if (t != NULL)
				*t = ber->be_type;
			ret++;
			continue;
		case 'x':
			ptr = va_arg(ap, void **);
			len = va_arg(ap, size_t *);
			if (ober_get_nstring(ber, ptr, len) == -1)
				goto fail;
			ret++;
			break;
		case '0':
			if (ber->be_encoding != BER_TYPE_NULL)
				goto fail;
			ret++;
			break;
		case '.':
			if (ber->be_encoding != BER_TYPE_EOC)
				goto fail;
			ret++;
			break;
		case 'p':
			pos = va_arg(ap, off_t *);
			*pos = ober_getpos(ber);
			ret++;
			continue;
		case '{':
		case '(':
			if (ber->be_encoding != BER_TYPE_SEQUENCE &&
			    ber->be_encoding != BER_TYPE_SET)
				goto fail;
			if (level >= _MAX_SEQ-1)
				goto fail;
			parent[++level] = ber;
			ber = ber->be_sub;
			ret++;
			continue;
		case '}':
		case ')':
			if (level < 0 || parent[level] == NULL)
				goto fail;
			ber = parent[level--];
			ret++;
			break;
		default:
			goto fail;
		}

		ber = ber->be_next;
	}
	va_end(ap);
	return (ret == n ? 0 : -1);

 fail:
	va_end(ap);
	return (-1);

}

ssize_t
ober_get_writebuf(struct ber *b, void **buf)
{
	if (b->br_wbuf == NULL)
		return -1;
	*buf = b->br_wbuf;
	return (b->br_wend - b->br_wbuf);
}

/*
 * write ber elements to the write buffer
 *
 * params:
 *	ber	holds the destination write buffer byte stream
 *	root	fully populated element tree
 *
 * returns:
 *	>=0	number of bytes written
 *	-1	on failure and sets errno
 */
ssize_t
ober_write_elements(struct ber *ber, struct ber_element *root)
{
	size_t len;

	/* calculate length because only the definite form is required */
	len = ober_calc_len(root);
	DPRINTF("write ber element of %zd bytes length\n", len);

	if (ber->br_wbuf != NULL && ber->br_wbuf + len > ber->br_wend) {
		free(ber->br_wbuf);
		ber->br_wbuf = NULL;
	}
	if (ber->br_wbuf == NULL) {
		if ((ber->br_wbuf = malloc(len)) == NULL)
			return -1;
		ber->br_wend = ber->br_wbuf + len;
	}

	/* reset write pointer */
	ber->br_wptr = ber->br_wbuf;

	if (ober_dump_element(ber, root) == -1)
		return -1;

	return (len);
}

void
ober_set_readbuf(struct ber *b, void *buf, size_t len)
{
	b->br_rbuf = b->br_rptr = buf;
	b->br_rend = (u_int8_t *)buf + len;
}

/*
 * read ber elements from the read buffer
 *
 * params:
 *	ber	holds a fully populated read buffer byte stream
 *	root	if NULL, build up an element tree from what we receive on
 *		the wire. If not null, use the specified encoding for the
 *		elements received.
 *
 * returns:
 *	!=NULL, elements read and store in the ber_element tree
 *	NULL, type mismatch or read error
 */
struct ber_element *
ober_read_elements(struct ber *ber, struct ber_element *elm)
{
	struct ber_element *root = elm;

	if (root == NULL) {
		if ((root = ober_get_element(0)) == NULL)
			return NULL;
	}

	DPRINTF("read ber elements, root %p\n", root);

	if (ober_read_element(ber, root) == -1) {
		/* Cleanup if root was allocated by us */
		if (elm == NULL)
			ober_free_elements(root);
		return NULL;
	}

	return root;
}

off_t
ober_getpos(struct ber_element *elm)
{
	return elm->be_offs;
}

struct ber_element *
ober_dup(struct ber_element *orig)
{
	struct ber_element *new;

	if ((new = malloc(sizeof(*new))) == NULL)
		return NULL;
	memcpy(new, orig, sizeof(*new));
	new->be_next = NULL;
	new->be_sub = NULL;

	if (orig->be_next != NULL) {
		if ((new->be_next = ober_dup(orig->be_next)) == NULL)
			goto fail;
	}
	if (orig->be_encoding == BER_TYPE_SEQUENCE ||
	    orig->be_encoding == BER_TYPE_SET) {
		if (orig->be_sub != NULL) {
			if ((new->be_sub = ober_dup(orig->be_sub)) == NULL)
				goto fail;
		}
	} else if (orig->be_encoding == BER_TYPE_OCTETSTRING ||
	    orig->be_encoding == BER_TYPE_BITSTRING ||
	    orig->be_encoding == BER_TYPE_OBJECT) {
		if (orig->be_val != NULL) {
			if ((new->be_val = malloc(orig->be_len)) == NULL)
				goto fail;
			memcpy(new->be_val, orig->be_val, orig->be_len);
		}
	} else
		new->be_numeric = orig->be_numeric;
	return new;
 fail:
	ober_free_elements(new);
	return NULL;
}

void
ober_free_element(struct ber_element *root)
{
	if (root->be_sub && (root->be_encoding == BER_TYPE_SEQUENCE ||
	    root->be_encoding == BER_TYPE_SET))
		ober_free_elements(root->be_sub);
	if (root->be_free && (root->be_encoding == BER_TYPE_OCTETSTRING ||
	    root->be_encoding == BER_TYPE_BITSTRING ||
	    root->be_encoding == BER_TYPE_OBJECT))
		free(root->be_val);
	free(root);
}

void
ober_free_elements(struct ber_element *root)
{
	if (root == NULL)
		return;
	if (root->be_sub && (root->be_encoding == BER_TYPE_SEQUENCE ||
	    root->be_encoding == BER_TYPE_SET))
		ober_free_elements(root->be_sub);
	if (root->be_next)
		ober_free_elements(root->be_next);
	if (root->be_free && (root->be_encoding == BER_TYPE_OCTETSTRING ||
	    root->be_encoding == BER_TYPE_BITSTRING ||
	    root->be_encoding == BER_TYPE_OBJECT))
		free(root->be_val);
	free(root);
}

size_t
ober_calc_len(struct ber_element *root)
{
	unsigned int t;
	size_t s;
	size_t size = 2;	/* minimum 1 byte head and 1 byte size */

	/* calculate the real length of a sequence or set */
	if (root->be_sub && (root->be_encoding == BER_TYPE_SEQUENCE ||
	    root->be_encoding == BER_TYPE_SET))
		root->be_len = ober_calc_len(root->be_sub);

	/* fix header length for extended types */
	if (root->be_type > BER_TYPE_SINGLE_MAX)
		for (t = root->be_type; t > 0; t >>= 7)
			size++;
	if (root->be_len >= BER_TAG_MORE)
		for (s = root->be_len; s > 0; s >>= 8)
			size++;

	/* calculate the length of the following elements */
	if (root->be_next)
		size += ober_calc_len(root->be_next);

	/* This is an empty element, do not use a minimal size */
	if (root->be_class == BER_CLASS_UNIVERSAL &&
	    root->be_type == BER_TYPE_EOC && root->be_len == 0)
		return (0);

	return (root->be_len + size);
}

void
ober_set_application(struct ber *b, unsigned int (*cb)(struct ber_element *))
{
	b->br_application = cb;
}

void
ober_set_writecallback(struct ber_element *elm, void (*cb)(void *, size_t),
    void *arg)
{
	elm->be_cb = cb;
	elm->be_cbarg = arg;
}

void
ober_free(struct ber *b)
{
	free(b->br_wbuf);
}

/*
 * internal functions
 */

static int
ober_dump_element(struct ber *ber, struct ber_element *root)
{
	unsigned long long l;
	int i;
	uint8_t u;

	ober_dump_header(ber, root);
	if (root->be_cb)
		root->be_cb(root->be_cbarg, ber->br_wptr - ber->br_wbuf);

	switch (root->be_encoding) {
	case BER_TYPE_BOOLEAN:
	case BER_TYPE_INTEGER:
	case BER_TYPE_ENUMERATED:
		l = (unsigned long long)root->be_numeric;
		for (i = root->be_len; i > 0; i--) {
			u = (l >> ((i - 1) * 8)) & 0xff;
			ober_putc(ber, u);
		}
		break;
	case BER_TYPE_BITSTRING:
	case BER_TYPE_OCTETSTRING:
	case BER_TYPE_OBJECT:
		ober_write(ber, root->be_val, root->be_len);
		break;
	case BER_TYPE_NULL:	/* no payload */
	case BER_TYPE_EOC:
		break;
	case BER_TYPE_SEQUENCE:
	case BER_TYPE_SET:
		if (root->be_sub && ober_dump_element(ber, root->be_sub) == -1)
			return -1;
		break;
	}

	if (root->be_next == NULL)
		return 0;
	return ober_dump_element(ber, root->be_next);
}

static void
ober_dump_header(struct ber *ber, struct ber_element *root)
{
	u_char	id = 0, t, buf[5];
	unsigned int type;
	size_t size;

	/* class universal, type encoding depending on type value */
	/* length encoding */
	if (root->be_type <= BER_TYPE_SINGLE_MAX) {
		id = root->be_type | (root->be_class << BER_CLASS_SHIFT);
		if (root->be_encoding == BER_TYPE_SEQUENCE ||
		    root->be_encoding == BER_TYPE_SET)
			id |= BER_TYPE_CONSTRUCTED;

		ober_putc(ber, id);
	} else {
		id = BER_TAG_MASK | (root->be_class << BER_CLASS_SHIFT);
		if (root->be_encoding == BER_TYPE_SEQUENCE ||
		    root->be_encoding == BER_TYPE_SET)
			id |= BER_TYPE_CONSTRUCTED;

		ober_putc(ber, id);

		for (t = 0, type = root->be_type; type > 0; type >>= 7)
			buf[t++] = type & ~BER_TAG_MORE;

		while (t-- > 0) {
			if (t > 0)
				buf[t] |= BER_TAG_MORE;
			ober_putc(ber, buf[t]);
		}
	}

	if (root->be_len < BER_TAG_MORE) {
		/* short form */
		ober_putc(ber, root->be_len);
	} else {
		for (t = 0, size = root->be_len; size > 0; size >>= 8)
			buf[t++] = size & 0xff;

		ober_putc(ber, t | BER_TAG_MORE);

		while (t > 0)
			ober_putc(ber, buf[--t]);
	}
}

static void
ober_putc(struct ber *ber, u_char c)
{
	if (ber->br_wptr + 1 <= ber->br_wend)
		*ber->br_wptr = c;
	ber->br_wptr++;
}

static void
ober_write(struct ber *ber, void *buf, size_t len)
{
	if (ber->br_wptr + len <= ber->br_wend)
		bcopy(buf, ber->br_wptr, len);
	ber->br_wptr += len;
}

/*
 * extract a BER encoded tag. There are two types, a short and long form.
 */
static ssize_t
get_id(struct ber *b, unsigned int *tag, int *class, int *cstruct)
{
	u_char u;
	size_t i = 0;
	unsigned int t = 0;

	if (ober_getc(b, &u) == -1)
		return -1;

	*class = (u >> BER_CLASS_SHIFT) & BER_CLASS_MASK;
	*cstruct = (u & BER_TYPE_CONSTRUCTED) == BER_TYPE_CONSTRUCTED;

	if ((u & BER_TAG_MASK) != BER_TAG_MASK) {
		*tag = u & BER_TAG_MASK;
		return 1;
	}

	do {
		if (ober_getc(b, &u) == -1)
			return -1;

		/* enforce minimal number of octets for tag > 30 */
		if (i == 0 && (u & ~BER_TAG_MORE) == 0) {
			errno = EINVAL;
			return -1;
		}

		t = (t << 7) | (u & ~BER_TAG_MORE);
		i++;
		if (i > sizeof(unsigned int)) {
			errno = ERANGE;
			return -1;
		}
	} while (u & BER_TAG_MORE);

	*tag = t;
	return i + 1;
}

/*
 * extract length of a ber object -- if length is unknown an error is returned.
 */
static ssize_t
get_len(struct ber *b, ssize_t *len)
{
	u_char	u, n;
	ssize_t	s, r;

	if (ober_getc(b, &u) == -1)
		return -1;
	if ((u & BER_TAG_MORE) == 0) {
		/* short form */
		*len = u;
		return 1;
	}

	if (u == 0x80) {
		/* Indefinite length not supported. */
		errno = EINVAL;
		return -1;
	}

	if (u == 0xff) {
		/* Reserved for future use. */
		errno = EINVAL;
		return -1;
	}

	n = u & ~BER_TAG_MORE;
	/*
	 * Limit to a decent size that works on all of our architectures.
	 */
	if (sizeof(int32_t) < n) {
		errno = ERANGE;
		return -1;
	}
	r = n + 1;

	for (s = 0; n > 0; n--) {
		if (ober_getc(b, &u) == -1)
			return -1;
		s = (s << 8) | u;
	}

	if (s < 0) {
		/* overflow */
		errno = ERANGE;
		return -1;
	}

	*len = s;
	return r;
}

static ssize_t
ober_read_element(struct ber *ber, struct ber_element *elm)
{
	long long val = 0;
	struct ber_element *next;
	unsigned int type;
	int i, class, cstruct, elements = 0;
	ssize_t len, r, totlen = 0;
	u_char c, last = 0;

	if ((r = get_id(ber, &type, &class, &cstruct)) == -1)
		return -1;
	DPRINTF("ber read got class %d type %u, %s\n",
	    class, type, cstruct ? "constructed" : "primitive");
	totlen += r;
	if ((r = get_len(ber, &len)) == -1)
		return -1;
	DPRINTF("ber read element size %zd\n", len);
	totlen += r + len;

	/* The encoding of boolean, integer, enumerated, and null values
	 * must be primitive. */
	if (class == BER_CLASS_UNIVERSAL)
		if (type == BER_TYPE_BOOLEAN ||
		    type == BER_TYPE_INTEGER ||
		    type == BER_TYPE_ENUMERATED ||
		    type == BER_TYPE_NULL)
			if (cstruct) {
				errno = EINVAL;
				return -1;
			}

	/* If the total size of the element is larger than the buffer
	 * don't bother to continue. */
	if (len > ber->br_rend - ber->br_rptr) {
		errno = ECANCELED;
		return -1;
	}

	elm->be_type = type;
	elm->be_len = len;
	elm->be_offs = ber->br_offs;	/* element position within stream */
	elm->be_class = class;

	if (elm->be_encoding == 0) {
		/* try to figure out the encoding via class, type and cstruct */
		if (cstruct)
			elm->be_encoding = BER_TYPE_SEQUENCE;
		else if (class == BER_CLASS_UNIVERSAL)
			elm->be_encoding = type;
		else if (ber->br_application != NULL) {
			/*
			 * Ask the application to map the encoding to a
			 * universal type. For example, a SMI IpAddress
			 * type is defined as 4 byte OCTET STRING.
			 */
			elm->be_encoding = (*ber->br_application)(elm);
		} else
			/* last resort option */
			elm->be_encoding = BER_TYPE_NULL;
	}

	switch (elm->be_encoding) {
	case BER_TYPE_EOC:	/* End-Of-Content */
		break;
	case BER_TYPE_BOOLEAN:
		if (len != 1) {
			errno = EINVAL;
			return -1;
		}
	case BER_TYPE_INTEGER:
	case BER_TYPE_ENUMERATED:
		if (len < 1) {
			errno = EINVAL;
			return -1;
		}
		if (len > (ssize_t)sizeof(long long)) {
			errno = ERANGE;
			return -1;
		}
		for (i = 0; i < len; i++) {
			if (ober_getc(ber, &c) != 1)
				return -1;

			/* smallest number of contents octets only */
			if ((i == 1 && last == 0 && (c & 0x80) == 0) ||
			    (i == 1 && last == 0xff && (c & 0x80) != 0)) {
				errno = EINVAL;
				return -1;
			}

			val <<= 8;
			val |= c;
			last = c;
		}

		/* sign extend if MSB is set */
		if (len < (ssize_t)sizeof(long long) &&
		    (val >> ((len - 1) * 8) & 0x80))
			val |= ULLONG_MAX << (len * 8);
		elm->be_numeric = val;
		break;
	case BER_TYPE_BITSTRING:
		elm->be_val = malloc(len);
		if (elm->be_val == NULL)
			return -1;
		elm->be_free = 1;
		elm->be_len = len;
		ober_read(ber, elm->be_val, len);
		break;
	case BER_TYPE_OCTETSTRING:
	case BER_TYPE_OBJECT:
		elm->be_val = malloc(len + 1);
		if (elm->be_val == NULL)
			return -1;
		elm->be_free = 1;
		elm->be_len = len;
		ober_read(ber, elm->be_val, len);
		((u_char *)elm->be_val)[len] = '\0';
		break;
	case BER_TYPE_NULL:	/* no payload */
		if (len != 0) {
			errno = EINVAL;
			return -1;
		}
		break;
	case BER_TYPE_SEQUENCE:
	case BER_TYPE_SET:
		if (len > 0 && elm->be_sub == NULL) {
			if ((elm->be_sub = ober_get_element(0)) == NULL)
				return -1;
		}
		next = elm->be_sub;
		while (len > 0) {
			/*
			 * Prevent stack overflow from excessive recursion
			 * depth in ober_free_elements().
			 */
			if (elements >= BER_MAX_SEQ_ELEMENTS) {
				errno = ERANGE;
				return -1;
			}
			r = ober_read_element(ber, next);
			if (r == -1) {
				/* sub-element overflows sequence/set */
				if (errno == ECANCELED)
					errno = EINVAL;
				return -1;
			}
			if (r > len) {
				errno = EINVAL;
				return -1;
			}
			elements++;
			len -= r;
			if (len > 0 && next->be_next == NULL) {
				next->be_next = ober_get_element(0);
				if (next->be_next == NULL)
					return -1;
			}
			next = next->be_next;
		}
		break;
	}
	return totlen;
}

static ssize_t
ober_getc(struct ber *b, u_char *c)
{
	return ober_read(b, c, 1);
}

static ssize_t
ober_read(struct ber *ber, void *buf, size_t len)
{
	size_t	sz;

	if (ber->br_rbuf == NULL) {
		errno = ENOBUFS;
		return -1;
	}

	sz = ber->br_rend - ber->br_rptr;
	if (len > sz) {
		errno = ECANCELED;
		return -1;	/* parser wants more data than available */
	}

	bcopy(ber->br_rptr, buf, len);
	ber->br_rptr += len;
	ber->br_offs += len;

	return len;
}
