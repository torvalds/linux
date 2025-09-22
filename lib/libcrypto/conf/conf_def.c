/* $OpenBSD: conf_def.c,v 1.45 2025/05/10 05:54:38 tb Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
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
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

/* Part of the code in here was originally in conf.c, which is now removed */

#include <stdio.h>
#include <string.h>

#include <openssl/buffer.h>
#include <openssl/conf.h>
#include <openssl/lhash.h>
#include <openssl/stack.h>

#include "conf_def.h"
#include "conf_local.h"
#include "err_local.h"

#define MAX_CONF_VALUE_LENGTH 65536

static char *eat_ws(CONF *conf, char *p);
static char *eat_alpha_numeric(CONF *conf, char *p);
static void clear_comments(CONF *conf, char *p);
static int str_copy(CONF *conf, char *section, char **to, char *from);
static char *scan_quote(CONF *conf, char *p);
static char *scan_dquote(CONF *conf, char *p);
#define scan_esc(conf,p)	(((IS_EOF((conf),(p)[1]))?((p)+1):((p)+2)))

static CONF *
def_create(const CONF_METHOD *meth)
{
	CONF *ret;

	ret = calloc(1, sizeof(CONF) + sizeof(unsigned short *));
	if (ret)
		if (meth->init(ret) == 0) {
			free(ret);
			ret = NULL;
		}
	return ret;
}

static int
def_init_default(CONF *conf)
{
	if (conf == NULL)
		return 0;

	conf->meth = NCONF_default();
	conf->data = NULL;

	return 1;
}

static int
def_destroy_data(CONF *conf)
{
	if (conf == NULL)
		return 0;
	_CONF_free_data(conf);
	return 1;
}

static int
def_destroy(CONF *conf)
{
	if (def_destroy_data(conf)) {
		free(conf);
		return 1;
	}
	return 0;
}

static int
def_load_bio(CONF *conf, BIO *in, long *line)
{
/* The macro BUFSIZE conflicts with a system macro in VxWorks */
#define CONFBUFSIZE	512
	int bufnum = 0, i, ii;
	BUF_MEM *buff = NULL;
	char *s, *p, *end;
	int again;
	long eline = 0;
	CONF_VALUE *v = NULL, *tv;
	CONF_VALUE *sv = NULL;
	char *section = NULL, *buf;
	char *start, *psection, *pname;
	void *h = (void *)(conf->data);

	if ((buff = BUF_MEM_new()) == NULL) {
		CONFerror(ERR_R_BUF_LIB);
		goto err;
	}

	section = strdup("default");
	if (section == NULL) {
		CONFerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (_CONF_new_data(conf) == 0) {
		CONFerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	sv = _CONF_new_section(conf, section);
	if (sv == NULL) {
		CONFerror(CONF_R_UNABLE_TO_CREATE_NEW_SECTION);
		goto err;
	}

	bufnum = 0;
	again = 0;
	for (;;) {
		if (!BUF_MEM_grow(buff, bufnum + CONFBUFSIZE)) {
			CONFerror(ERR_R_BUF_LIB);
			goto err;
		}
		p = &(buff->data[bufnum]);
		*p = '\0';
		BIO_gets(in, p, CONFBUFSIZE - 1);
		p[CONFBUFSIZE - 1] = '\0';
		ii = i = strlen(p);
		if (i == 0 && !again)
			break;
		again = 0;
		while (i > 0) {
			if ((p[i - 1] != '\r') && (p[i - 1] != '\n'))
				break;
			else
				i--;
		}
		/* we removed some trailing stuff so there is a new
		 * line on the end. */
		if (ii && i == ii)
			again = 1; /* long line */
		else {
			p[i] = '\0';
			eline++; /* another input line */
		}

		/* we now have a line with trailing \r\n removed */

		/* i is the number of bytes */
		bufnum += i;

		v = NULL;
		/* check for line continuation */
		if (bufnum >= 1) {
			/* If we have bytes and the last char '\\' and
			 * second last char is not '\\' */
			p = &(buff->data[bufnum - 1]);
			if (IS_ESC(conf, p[0]) &&
			    ((bufnum <= 1) || !IS_ESC(conf, p[-1]))) {
				bufnum--;
				again = 1;
			}
		}
		if (again)
			continue;
		bufnum = 0;
		buf = buff->data;

		clear_comments(conf, buf);
		s = eat_ws(conf, buf);
		if (IS_EOF(conf, *s))
			continue; /* blank line */
		if (*s == '[') {
			char *ss;

			s++;
			start = eat_ws(conf, s);
			ss = start;
again:
			end = eat_alpha_numeric(conf, ss);
			p = eat_ws(conf, end);
			if (*p != ']') {
				if (*p != '\0' && ss != p) {
					ss = p;
					goto again;
				}
				CONFerror(CONF_R_MISSING_CLOSE_SQUARE_BRACKET);
				goto err;
			}
			*end = '\0';
			if (!str_copy(conf, NULL, &section, start))
				goto err;
			if ((sv = _CONF_get_section(conf, section)) == NULL)
				sv = _CONF_new_section(conf, section);
			if (sv == NULL) {
				CONFerror(CONF_R_UNABLE_TO_CREATE_NEW_SECTION);
				goto err;
			}
			continue;
		} else {
			pname = s;
			psection = NULL;
			end = eat_alpha_numeric(conf, s);
			if ((end[0] == ':') && (end[1] == ':')) {
				*end = '\0';
				end += 2;
				psection = pname;
				pname = end;
				end = eat_alpha_numeric(conf, end);
			}
			p = eat_ws(conf, end);
			if (*p != '=') {
				CONFerror(CONF_R_MISSING_EQUAL_SIGN);
				goto err;
			}
			*end = '\0';
			p++;
			start = eat_ws(conf, p);
			while (!IS_EOF(conf, *p))
				p++;
			p--;
			while ((p != start) && (IS_WS(conf, *p)))
				p--;
			p++;
			*p = '\0';

			if (!(v = malloc(sizeof(CONF_VALUE)))) {
				CONFerror(ERR_R_MALLOC_FAILURE);
				goto err;
			}
			if (psection == NULL)
				psection = section;
			v->name = strdup(pname);
			v->value = NULL;
			if (v->name == NULL) {
				CONFerror(ERR_R_MALLOC_FAILURE);
				goto err;
			}
			if (!str_copy(conf, psection, &(v->value), start))
				goto err;

			if (strcmp(psection, section) != 0) {
				if ((tv = _CONF_get_section(conf, psection))
					== NULL)
					tv = _CONF_new_section(conf, psection);
				if (tv == NULL) {
					CONFerror(CONF_R_UNABLE_TO_CREATE_NEW_SECTION);
					goto err;
				}
			} else
				tv = sv;

			if (_CONF_add_string(conf, tv, v) == 0) {
				CONFerror(ERR_R_MALLOC_FAILURE);
				goto err;
			}
			v = NULL;
		}
	}
	if (buff != NULL)
		BUF_MEM_free(buff);
	free(section);
	return (1);

err:
	if (buff != NULL)
		BUF_MEM_free(buff);
	free(section);
	if (line != NULL)
		*line = eline;
	ERR_asprintf_error_data("line %ld", eline);
	if ((h != conf->data) && (conf->data != NULL)) {
		CONF ctmp;

		CONF_set_nconf(&ctmp, conf->data);
		ctmp.meth->destroy_data(&ctmp);
		conf->data = NULL;
	}
	if (v != NULL) {
		free(v->name);
		free(v->value);
		free(v);
	}
	return (0);
}

static int
def_load(CONF *conf, const char *name, long *line)
{
	int ret;
	BIO *in = NULL;

	in = BIO_new_file(name, "rb");
	if (in == NULL) {
		if (ERR_GET_REASON(ERR_peek_last_error()) == BIO_R_NO_SUCH_FILE)
			CONFerror(CONF_R_NO_SUCH_FILE);
		else
			CONFerror(ERR_R_SYS_LIB);
		return 0;
	}

	ret = def_load_bio(conf, in, line);
	BIO_free(in);

	return ret;
}

static void
clear_comments(CONF *conf, char *p)
{
	for (;;) {
		if (IS_FCOMMENT(conf, *p)) {
			*p = '\0';
			return;
		}
		if (!IS_WS(conf, *p)) {
			break;
		}
		p++;
	}

	for (;;) {
		if (IS_COMMENT(conf, *p)) {
			*p = '\0';
			return;
		}
		if (IS_DQUOTE(conf, *p)) {
			p = scan_dquote(conf, p);
			continue;
		}
		if (IS_QUOTE(conf, *p)) {
			p = scan_quote(conf, p);
			continue;
		}
		if (IS_ESC(conf, *p)) {
			p = scan_esc(conf, p);
			continue;
		}
		if (IS_EOF(conf, *p))
			return;
		else
			p++;
	}
}

static int
str_copy(CONF *conf, char *section, char **pto, char *from)
{
	int q, r,rr = 0, to = 0, len = 0;
	char *s, *e, *rp, *p, *rrp, *np, *cp, v;
	size_t newsize;
	BUF_MEM *buf;

	if ((buf = BUF_MEM_new()) == NULL)
		return (0);

	len = strlen(from) + 1;
	if (!BUF_MEM_grow(buf, len))
		goto err;

	for (;;) {
		if (IS_QUOTE(conf, *from)) {
			q = *from;
			from++;
			while (!IS_EOF(conf, *from) && (*from != q)) {
				if (IS_ESC(conf, *from)) {
					from++;
					if (IS_EOF(conf, *from))
						break;
				}
				buf->data[to++] = *(from++);
			}
			if (*from == q)
				from++;
		} else if (IS_DQUOTE(conf, *from)) {
			q = *from;
			from++;
			while (!IS_EOF(conf, *from)) {
				if (*from == q) {
					if (*(from + 1) == q) {
						from++;
					} else {
						break;
					}
				}
				buf->data[to++] = *(from++);
			}
			if (*from == q)
				from++;
		} else if (IS_ESC(conf, *from)) {
			from++;
			v = *(from++);
			if (IS_EOF(conf, v))
				break;
			else if (v == 'r')
				v = '\r';
			else if (v == 'n')
				v = '\n';
			else if (v == 'b')
				v = '\b';
			else if (v == 't')
				v = '\t';
			buf->data[to++] = v;
		} else if (IS_EOF(conf, *from))
			break;
		else if (*from == '$') {
			/* try to expand it */
			rrp = NULL;
			s = &(from[1]);
			if (*s == '{')
				q = '}';
			else if (*s == '(')
				q = ')';
			else
				q = 0;

			if (q)
				s++;
			cp = section;
			e = np = s;
			while (IS_ALPHA_NUMERIC(conf, *e))
				e++;
			if ((e[0] == ':') && (e[1] == ':')) {
				cp = np;
				rrp = e;
				rr = *e;
				*rrp = '\0';
				e += 2;
				np = e;
				while (IS_ALPHA_NUMERIC(conf, *e))
					e++;
			}
			r = *e;
			*e = '\0';
			rp = e;
			if (q) {
				if (r != q) {
					CONFerror(CONF_R_NO_CLOSE_BRACE);
					goto err;
				}
				e++;
			}
			/* So at this point we have
			 * np which is the start of the name string which is
			 *   '\0' terminated.
			 * cp which is the start of the section string which is
			 *   '\0' terminated.
			 * e is the 'next point after'.
			 * r and rr are the chars replaced by the '\0'
			 * rp and rrp is where 'r' and 'rr' came from.
			 */
			p = _CONF_get_string(conf, cp, np);
			if (rrp != NULL)
				*rrp = rr;
			*rp = r;
			if (p == NULL) {
				CONFerror(CONF_R_VARIABLE_HAS_NO_VALUE);
				goto err;
			}
			newsize = strlen(p) + buf->length - (e - from);
			if (newsize > MAX_CONF_VALUE_LENGTH) {
				CONFerror(CONF_R_VARIABLE_EXPANSION_TOO_LONG);
				goto err;
			}
			if (!BUF_MEM_grow_clean(buf, newsize)) {
				CONFerror(CONF_R_MODULE_INITIALIZATION_ERROR);
				goto err;
			}
			while (*p)
				buf->data[to++] = *(p++);

			/* Since we change the pointer 'from', we also have
			   to change the perceived length of the string it
			   points at.  /RL */
			len -= e - from;
			from = e;

			/* In case there were no braces or parenthesis around
			   the variable reference, we have to put back the
			   character that was replaced with a '\0'.  /RL */
			*rp = r;
		} else
			buf->data[to++] = *(from++);
	}
	buf->data[to]='\0';
	free(*pto);
	*pto = buf->data;
	free(buf);
	return (1);

err:
	if (buf != NULL)
		BUF_MEM_free(buf);
	return (0);
}

static char *
eat_ws(CONF *conf, char *p)
{
	while (IS_WS(conf, *p) && (!IS_EOF(conf, *p)))
		p++;
	return (p);
}

static char *
eat_alpha_numeric(CONF *conf, char *p)
{
	for (;;) {
		if (IS_ESC(conf, *p)) {
			p = scan_esc(conf, p);
			continue;
		}
		if (!IS_ALPHA_NUMERIC_PUNCT(conf, *p))
			return (p);
		p++;
	}
}

static char *
scan_quote(CONF *conf, char *p)
{
	int q = *p;

	p++;
	while (!(IS_EOF(conf, *p)) && (*p != q)) {
		if (IS_ESC(conf, *p)) {
			p++;
			if (IS_EOF(conf, *p))
				return (p);
		}
		p++;
	}
	if (*p == q)
		p++;
	return (p);
}


static char *
scan_dquote(CONF *conf, char *p)
{
	int q = *p;

	p++;
	while (!(IS_EOF(conf, *p))) {
		if (*p == q) {
			if (*(p + 1) == q) {
				p++;
			} else {
				break;
			}
		}
		p++;
	}
	if (*p == q)
		p++;
	return (p);
}

static void
dump_value_doall_arg(CONF_VALUE *a, BIO *out)
{
	if (a->name)
		BIO_printf(out, "[%s] %s=%s\n", a->section, a->name, a->value);
	else
		BIO_printf(out, "[[%s]]\n", a->section);
}

static IMPLEMENT_LHASH_DOALL_ARG_FN(dump_value, CONF_VALUE, BIO)

static int
def_dump(const CONF *conf, BIO *out)
{
	lh_CONF_VALUE_doall_arg(conf->data, LHASH_DOALL_ARG_FN(dump_value),
	    BIO, out);
	return 1;
}

static int
def_is_number(const CONF *conf, char c)
{
	return IS_NUMBER(conf, c);
}

static int
def_to_int(const CONF *conf, char c)
{
	return c - '0';
}

static const CONF_METHOD default_method = {
	.name = "OpenSSL default",
	.create = def_create,
	.init = def_init_default,
	.destroy = def_destroy,
	.destroy_data = def_destroy_data,
	.load_bio = def_load_bio,
	.dump = def_dump,
	.is_number = def_is_number,
	.to_int = def_to_int,
	.load = def_load,
};

const CONF_METHOD *
NCONF_default(void)
{
	return &default_method;
}
