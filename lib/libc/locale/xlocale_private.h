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

#ifndef _XLOCALE_PRIVATE__H_
#define _XLOCALE_PRIVATE__H_

#include <xlocale.h>
#include <locale.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <machine/atomic.h>
#include "setlocale.h"

/**
 * The XLC_ values are indexes into the components array.  They are defined in
 * the same order as the LC_ values in locale.h, but without the LC_ALL zero
 * value.  Translating from LC_X to XLC_X is done by subtracting one.
 *
 * Any reordering of this enum should ensure that these invariants are not
 * violated.
 */
enum {
	XLC_COLLATE = 0,
	XLC_CTYPE,
	XLC_MONETARY,
	XLC_NUMERIC,
	XLC_TIME,
	XLC_MESSAGES,
	XLC_LAST
};

_Static_assert(XLC_LAST - XLC_COLLATE == 6, "XLC values should be contiguous");
_Static_assert(XLC_COLLATE == LC_COLLATE - 1,
               "XLC_COLLATE doesn't match the LC_COLLATE value.");
_Static_assert(XLC_CTYPE == LC_CTYPE - 1,
               "XLC_CTYPE doesn't match the LC_CTYPE value.");
_Static_assert(XLC_MONETARY == LC_MONETARY - 1,
               "XLC_MONETARY doesn't match the LC_MONETARY value.");
_Static_assert(XLC_NUMERIC == LC_NUMERIC - 1,
               "XLC_NUMERIC doesn't match the LC_NUMERIC value.");
_Static_assert(XLC_TIME == LC_TIME - 1,
               "XLC_TIME doesn't match the LC_TIME value.");
_Static_assert(XLC_MESSAGES == LC_MESSAGES - 1,
               "XLC_MESSAGES doesn't match the LC_MESSAGES value.");

/**
 * Header used for objects that are reference counted.  Objects may optionally
 * have a destructor associated, which is responsible for destroying the
 * structure.  Global / static versions of the structure should have no
 * destructor set - they can then have their reference counts manipulated as
 * normal, but will not do anything with them.
 *
 * The header stores a retain count - objects are assumed to have a reference
 * count of 1 when they are created, but the retain count is 0.  When the
 * retain count is less than 0, they are freed.
 */
struct xlocale_refcounted {
	/** Number of references to this component.  */
	long retain_count;
	/** Function used to destroy this component, if one is required*/
	void(*destructor)(void*);
};
/**
 * Header for a locale component.  All locale components must begin with this
 * header.
 */
struct xlocale_component {
	struct xlocale_refcounted header;
	/** Name of the locale used for this component. */
	char locale[ENCODING_LEN+1];
};

/**
 * xlocale structure, stores per-thread locale information.  
 */
struct _xlocale {
	struct xlocale_refcounted header;
	/** Components for the locale.  */
	struct xlocale_component *components[XLC_LAST];
	/** Flag indicating if components[XLC_MONETARY] has changed since the
	 * last call to localeconv_l() with this locale. */
	int monetary_locale_changed;
	/** Flag indicating whether this locale is actually using a locale for
	 * LC_MONETARY (1), or if it should use the C default instead (0). */
	int using_monetary_locale;
	/** Flag indicating if components[XLC_NUMERIC] has changed since the
	 * last call to localeconv_l() with this locale. */
	int numeric_locale_changed;
	/** Flag indicating whether this locale is actually using a locale for
	 * LC_NUMERIC (1), or if it should use the C default instead (0). */
	int using_numeric_locale;
	/** Flag indicating whether this locale is actually using a locale for
	 * LC_TIME (1), or if it should use the C default instead (0). */
	int using_time_locale;
	/** Flag indicating whether this locale is actually using a locale for
	 * LC_MESSAGES (1), or if it should use the C default instead (0). */
	int using_messages_locale;
	/** The structure to be returned from localeconv_l() for this locale. */
	struct lconv lconv;
	/** Buffer used by nl_langinfo_l() */
	char *csym;
};

/**
 * Increments the reference count of a reference-counted structure.
 */
__attribute__((unused)) static void*
xlocale_retain(void *val)
{
	struct xlocale_refcounted *obj = val;
	atomic_add_long(&(obj->retain_count), 1);
	return (val);
}
/**
 * Decrements the reference count of a reference-counted structure, freeing it
 * if this is the last reference, calling its destructor if it has one.
 */
__attribute__((unused)) static void
xlocale_release(void *val)
{
	struct xlocale_refcounted *obj = val;
	long count;

	count = atomic_fetchadd_long(&(obj->retain_count), -1) - 1;
	if (count < 0 && obj->destructor != NULL)
		obj->destructor(obj);
}

/**
 * Load functions.  Each takes the name of a locale and a pointer to the data
 * to be initialised as arguments.  Two special values are allowed for the 
 */
extern void* __collate_load(const char*, locale_t);
extern void* __ctype_load(const char*, locale_t);
extern void* __messages_load(const char*, locale_t);
extern void* __monetary_load(const char*, locale_t);
extern void* __numeric_load(const char*, locale_t);
extern void* __time_load(const char*, locale_t);

extern struct _xlocale __xlocale_global_locale;
extern struct _xlocale __xlocale_C_locale;

/**
 * Caches the rune table in TLS for fast access.
 */
void __set_thread_rune_locale(locale_t loc);
/**
 * Flag indicating whether a per-thread locale has been set.  If no per-thread
 * locale has ever been set, then we always use the global locale.
 */
extern int __has_thread_locale;
#ifndef __NO_TLS
/**
 * The per-thread locale.  Avoids the need to use pthread lookup functions when
 * getting the per-thread locale.
 */
extern _Thread_local locale_t __thread_locale;

/**
 * Returns the current locale for this thread, or the global locale if none is
 * set.  The caller does not have to free the locale.  The return value from
 * this call is not guaranteed to remain valid after the locale changes.  As
 * such, this should only be called within libc functions.
 */
static inline locale_t __get_locale(void)
{

	if (!__has_thread_locale) {
		return (&__xlocale_global_locale);
	}
	return (__thread_locale ? __thread_locale : &__xlocale_global_locale);
}
#else
locale_t __get_locale(void);
#endif

/**
 * Two magic values are allowed for locale_t objects.  NULL and -1.  This
 * function maps those to the real locales that they represent.
 */
static inline locale_t get_real_locale(locale_t locale)
{
	switch ((intptr_t)locale) {
		case 0: return (&__xlocale_C_locale);
		case -1: return (&__xlocale_global_locale);
		default: return (locale);
	}
}

/**
 * Replace a placeholder locale with the real global or thread-local locale_t.
 */
#define FIX_LOCALE(l) (l = get_real_locale(l))

#endif
