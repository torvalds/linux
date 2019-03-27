/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Alexey Zelkin <phantom@FreeBSD.org>
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

#include <stddef.h>

#include "ldpart.h"
#include "lmessages.h"

#define LCMESSAGES_SIZE_FULL (sizeof(struct lc_messages_T) / sizeof(char *))
#define LCMESSAGES_SIZE_MIN \
		(offsetof(struct lc_messages_T, yesstr) / sizeof(char *))

struct xlocale_messages {
	struct xlocale_component header;
	char *buffer;
	struct lc_messages_T locale;
};

struct xlocale_messages __xlocale_global_messages;

static char empty[] = "";

static const struct lc_messages_T _C_messages_locale = {
	"^[yY]" ,	/* yesexpr */
	"^[nN]" ,	/* noexpr */
	"yes" , 	/* yesstr */
	"no"		/* nostr */
};

static void destruct_messages(void *v)
{
	struct xlocale_messages *l = v;
	if (l->buffer)
		free(l->buffer);
	free(l);
}

static int
messages_load_locale(struct xlocale_messages *loc, int *using_locale, const char *name)
{
	int ret;
	struct lc_messages_T *l = &loc->locale;

	ret = __part_load_locale(name, using_locale,
		  &loc->buffer, "LC_MESSAGES",
		  LCMESSAGES_SIZE_FULL, LCMESSAGES_SIZE_MIN,
		  (const char **)l);
	if (ret == _LDP_LOADED) {
		if (l->yesstr == NULL)
			l->yesstr = empty;
		if (l->nostr == NULL)
			l->nostr = empty;
	}
	return (ret);
}
int
__messages_load_locale(const char *name)
{
	return messages_load_locale(&__xlocale_global_messages,
			&__xlocale_global_locale.using_messages_locale, name);
}
void *
__messages_load(const char *name, locale_t l)
{
	struct xlocale_messages *new = calloc(sizeof(struct xlocale_messages), 1);
	new->header.header.destructor = destruct_messages;
	if (messages_load_locale(new, &l->using_messages_locale, name) == _LDP_ERROR) {
		xlocale_release(new);
		return NULL;
	}
	return new;
}

struct lc_messages_T *
__get_current_messages_locale(locale_t loc)
{
	return (loc->using_messages_locale
		? &((struct xlocale_messages *)loc->components[XLC_MESSAGES])->locale
		: (struct lc_messages_T *)&_C_messages_locale);
}

#ifdef LOCALE_DEBUG
void
msgdebug() {
printf(	"yesexpr = %s\n"
	"noexpr = %s\n"
	"yesstr = %s\n"
	"nostr = %s\n",
	_messages_locale.yesexpr,
	_messages_locale.noexpr,
	_messages_locale.yesstr,
	_messages_locale.nostr
);
}
#endif /* LOCALE_DEBUG */
