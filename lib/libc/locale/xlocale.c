/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by David Chisnall under sponsorship from
 * the FreeBSD Foundation.
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

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <runetype.h>
#include "libc_private.h"
#include "xlocale_private.h"

/**
 * Each locale loader declares a global component.  This is used by setlocale()
 * and also by xlocale with LC_GLOBAL_LOCALE..
 */
extern struct xlocale_component __xlocale_global_collate;
extern struct xlocale_component __xlocale_global_ctype;
extern struct xlocale_component __xlocale_global_monetary;
extern struct xlocale_component __xlocale_global_numeric;
extern struct xlocale_component __xlocale_global_time;
extern struct xlocale_component __xlocale_global_messages;
/*
 * And another version for the statically-allocated C locale.  We only have
 * components for the parts that are expected to be sensible.
 */
extern struct xlocale_component __xlocale_C_collate;
extern struct xlocale_component __xlocale_C_ctype;

#ifndef __NO_TLS
/*
 * The locale for this thread.
 */
_Thread_local locale_t __thread_locale;
#endif
/*
 * Flag indicating that one or more per-thread locales exist.
 */
int __has_thread_locale;
/*
 * Private functions in setlocale.c.
 */
const char *
__get_locale_env(int category);
int
__detect_path_locale(void);

struct _xlocale __xlocale_global_locale = {
	{0},
	{
		&__xlocale_global_collate,
		&__xlocale_global_ctype,
		&__xlocale_global_monetary,
		&__xlocale_global_numeric,
		&__xlocale_global_time,
		&__xlocale_global_messages
	},
	1,
	0,
	1,
	0
};

struct _xlocale __xlocale_C_locale = {
	{0},
	{
		&__xlocale_C_collate,
		&__xlocale_C_ctype,
		0, 0, 0, 0
	},
	1,
	0,
	1,
	0
};

static void*(*constructors[])(const char*, locale_t) =
{
	__collate_load,
	__ctype_load,
	__monetary_load,
	__numeric_load,
	__time_load,
	__messages_load
};

static pthread_key_t locale_info_key;
static int fake_tls;
static locale_t thread_local_locale;

static void init_key(void)
{

	pthread_key_create(&locale_info_key, xlocale_release);
	pthread_setspecific(locale_info_key, (void*)42);
	if (pthread_getspecific(locale_info_key) == (void*)42) {
		pthread_setspecific(locale_info_key, 0);
	} else {
		fake_tls = 1;
	}
	/* At least one per-thread locale has now been set. */
	__has_thread_locale = 1;
	__detect_path_locale();
}

static pthread_once_t once_control = PTHREAD_ONCE_INIT;

static locale_t
get_thread_locale(void)
{

	_once(&once_control, init_key);
	
	return (fake_tls ? thread_local_locale :
		pthread_getspecific(locale_info_key));
}

#ifdef __NO_TLS
locale_t
__get_locale(void)
{
	locale_t l = get_thread_locale();
	return (l ? l : &__xlocale_global_locale);

}
#endif

static void
set_thread_locale(locale_t loc)
{
	locale_t l = (loc == LC_GLOBAL_LOCALE) ? 0 : loc;

	_once(&once_control, init_key);
	
	if (NULL != l) {
		xlocale_retain((struct xlocale_refcounted*)l);
	}
	locale_t old = pthread_getspecific(locale_info_key);
	if ((NULL != old) && (l != old)) {
		xlocale_release((struct xlocale_refcounted*)old);
	}
	if (fake_tls) {
		thread_local_locale = l;
	} else {
		pthread_setspecific(locale_info_key, l);
	}
#ifndef __NO_TLS
	__thread_locale = l;
	__set_thread_rune_locale(loc);
#endif
}

/**
 * Clean up a locale, once its reference count reaches zero.  This function is
 * called by xlocale_release(), it should not be called directly.
 */
static void
destruct_locale(void *l)
{
	locale_t loc = l;

	for (int type=0 ; type<XLC_LAST ; type++) {
		if (loc->components[type]) {
			xlocale_release(loc->components[type]);
		}
	}
	if (loc->csym) {
		free(loc->csym);
	}
	free(l);
}

/**
 * Allocates a new, uninitialised, locale.
 */
static locale_t
alloc_locale(void)
{
	locale_t new = calloc(sizeof(struct _xlocale), 1);

	new->header.destructor = destruct_locale;
	new->monetary_locale_changed = 1;
	new->numeric_locale_changed = 1;
	return (new);
}
static void
copyflags(locale_t new, locale_t old)
{
	new->using_monetary_locale = old->using_monetary_locale;
	new->using_numeric_locale = old->using_numeric_locale;
	new->using_time_locale = old->using_time_locale;
	new->using_messages_locale = old->using_messages_locale;
}

static int dupcomponent(int type, locale_t base, locale_t new) 
{
	/* Always copy from the global locale, since it has mutable components.
	 */
	struct xlocale_component *src = base->components[type];

	if (&__xlocale_global_locale == base) {
		new->components[type] = constructors[type](src->locale, new);
		if (new->components[type]) {
			strncpy(new->components[type]->locale, src->locale,
			    ENCODING_LEN);
		}
	} else if (base->components[type]) {
		new->components[type] = xlocale_retain(base->components[type]);
	} else {
		/* If the component was NULL, return success - if base is a
		 * valid locale then the flag indicating that this isn't
		 * present should be set.  If it isn't a valid locale, then
		 * we're stuck anyway. */
		return 1;
	}
	return (0 != new->components[type]);
}

/*
 * Public interfaces.  These are the five public functions described by the
 * xlocale interface.  
 */

locale_t newlocale(int mask, const char *locale, locale_t base)
{
	int type;
	const char *realLocale = locale;
	int useenv = 0;
	int success = 1;

	_once(&once_control, init_key);

	locale_t new = alloc_locale();
	if (NULL == new) {
		return (NULL);
	}

	FIX_LOCALE(base);
	copyflags(new, base);

	if (NULL == locale) {
		realLocale = "C";
	} else if ('\0' == locale[0]) {
		useenv = 1;
	}

	for (type=0 ; type<XLC_LAST ; type++) {
		if (mask & 1) {
			if (useenv) {
				realLocale = __get_locale_env(type + 1);
			}
			new->components[type] =
			     constructors[type](realLocale, new);
			if (new->components[type]) {
				strncpy(new->components[type]->locale,
				     realLocale, ENCODING_LEN);
			} else {
				success = 0;
				break;
			}
		} else {
			if (!dupcomponent(type, base, new)) {
				success = 0;
				break;
			}
		}
		mask >>= 1;
	}
	if (0 == success) {
		xlocale_release(new);
		new = NULL;
	}

	return (new);
}

locale_t duplocale(locale_t base)
{
	locale_t new = alloc_locale();
	int type;

	_once(&once_control, init_key);

	if (NULL == new) {
		return (NULL);
	}
	
	FIX_LOCALE(base);
	copyflags(new, base);

	for (type=0 ; type<XLC_LAST ; type++) {
		dupcomponent(type, base, new);
	}

	return (new);
}

/*
 * Free a locale_t.  This is quite a poorly named function.  It actually
 * disclaims a reference to a locale_t, rather than freeing it.  
 */
void
freelocale(locale_t loc)
{

	/*
	 * Fail if we're passed something that isn't a locale. If we're
	 * passed the global locale, pretend that we freed it but don't
	 * actually do anything.
	 */
	if (loc != NULL && loc != LC_GLOBAL_LOCALE &&
	    loc != &__xlocale_global_locale)
		xlocale_release(loc);
}

/*
 * Returns the name of the locale for a particular component of a locale_t.
 */
const char *querylocale(int mask, locale_t loc)
{
	int type = ffs(mask) - 1;
	FIX_LOCALE(loc);
	if (type >= XLC_LAST)
		return (NULL);
	if (loc->components[type])
		return (loc->components[type]->locale);
	return ("C");
}

/*
 * Installs the specified locale_t as this thread's locale.
 */
locale_t uselocale(locale_t loc)
{
	locale_t old = get_thread_locale();
	if (NULL != loc) {
		set_thread_locale(loc);
	}
	return (old ? old : LC_GLOBAL_LOCALE);
}

