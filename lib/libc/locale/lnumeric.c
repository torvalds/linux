/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000, 2001 Alexey Zelkin <phantom@FreeBSD.org>
 * All rights reserved.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <limits.h>

#include "ldpart.h"
#include "lnumeric.h"

extern const char *__fix_locale_grouping_str(const char *);

#define LCNUMERIC_SIZE (sizeof(struct lc_numeric_T) / sizeof(char *))

static char	numempty[] = { CHAR_MAX, '\0' };

static const struct lc_numeric_T _C_numeric_locale = {
	".",     	/* decimal_point */
	"",     	/* thousands_sep */
	numempty	/* grouping */
};

static void
destruct_numeric(void *v)
{
	struct xlocale_numeric *l = v;
	if (l->buffer)
		free(l->buffer);
	free(l);
}

struct xlocale_numeric __xlocale_global_numeric;

static int
numeric_load_locale(struct xlocale_numeric *loc, int *using_locale, int *changed,
		const char *name)
{
	int ret;
	struct lc_numeric_T *l = &loc->locale;

	ret = __part_load_locale(name, using_locale,
		&loc->buffer, "LC_NUMERIC",
		LCNUMERIC_SIZE, LCNUMERIC_SIZE,
		(const char**)l);
	if (ret != _LDP_ERROR)
		*changed= 1;
	if (ret == _LDP_LOADED) {
		/* Can't be empty according to C99 */
		if (*l->decimal_point == '\0')
			l->decimal_point =
			    _C_numeric_locale.decimal_point;
		l->grouping =
		    __fix_locale_grouping_str(l->grouping);
	}
	return (ret);
}

int
__numeric_load_locale(const char *name)
{
	return numeric_load_locale(&__xlocale_global_numeric,
			&__xlocale_global_locale.using_numeric_locale,
			&__xlocale_global_locale.numeric_locale_changed, name);
}
void *
__numeric_load(const char *name, locale_t l)
{
	struct xlocale_numeric *new = calloc(sizeof(struct xlocale_numeric), 1);
	new->header.header.destructor = destruct_numeric;
	if (numeric_load_locale(new, &l->using_numeric_locale,
				&l->numeric_locale_changed, name) == _LDP_ERROR)
	{
		xlocale_release(new);
		return NULL;
	}
	return new;
}

struct lc_numeric_T *
__get_current_numeric_locale(locale_t loc)
{
	return (loc->using_numeric_locale
		? &((struct xlocale_numeric *)loc->components[XLC_NUMERIC])->locale
		: (struct lc_numeric_T *)&_C_numeric_locale);
}

#ifdef LOCALE_DEBUG
void
numericdebug(void) {
printf(	"decimal_point = %s\n"
	"thousands_sep = %s\n"
	"grouping = %s\n",
	_numeric_locale.decimal_point,
	_numeric_locale.thousands_sep,
	_numeric_locale.grouping
);
}
#endif /* LOCALE_DEBUG */
