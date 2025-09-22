/*	$OpenBSD: setlocale.c,v 1.31 2024/08/18 02:20:29 guenther Exp $	*/
/*
 * Copyright (c) 2017 Ingo Schwarze <schwarze@openbsd.org>
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

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rune.h"

static void
freegl(char **oldgl)
{
	int ic;

	if (oldgl == NULL)
		return;
	for (ic = LC_ALL; ic < _LC_LAST; ic++)
		free(oldgl[ic]);
	free(oldgl);
}

static char **
dupgl(char **oldgl)
{
	char **newgl;
	int ic;

	if ((newgl = calloc(_LC_LAST, sizeof(*newgl))) == NULL)	
		return NULL;
	for (ic = LC_ALL; ic < _LC_LAST; ic++) {
		if ((newgl[ic] = strdup(ic == LC_ALL ? "" :
		    oldgl == NULL ? "C" : oldgl[ic])) == NULL) {
			freegl(newgl);
			return NULL;
		}
	}
	return newgl;
}

static int
changegl(int category, const char *locname, char **gl)
{
	char *cp;

	if ((locname = _get_locname(category, locname)) == NULL ||
	    (cp = strdup(locname)) == NULL)
		return -1;

	free(gl[category]);
	gl[category] = cp;
	return 0;
}

char *
setlocale(int category, const char *locname)
{
	/*
	 * Even though only LC_CTYPE has any effect in the OpenBSD
	 * base system, store complete information about the global
	 * locale, such that third-party software can access it,
	 * both via setlocale(3) and via locale(1).
	 */
	static char	  global_locname[256];
	static char	**global_locale;

	char **newgl, *firstname, *nextname;
	int ic;

	if (category < LC_ALL || category >= _LC_LAST)
		return NULL;

	/*
	 * Change the global locale.
	 */
	if (locname != NULL) {
		if ((newgl = dupgl(global_locale)) == NULL)
			return NULL;
		if (category == LC_ALL && strchr(locname, '/') != NULL) {

			/* One value for each category. */
			if ((firstname = strdup(locname)) == NULL) {
				freegl(newgl);
				return NULL;
			}
			nextname = firstname;
			for (ic = 1; ic < _LC_LAST; ic++)
				if (nextname == NULL || changegl(ic,
				    strsep(&nextname, "/"), newgl) == -1)
					break;
			free(firstname);
			if (ic < _LC_LAST || nextname != NULL) {
				freegl(newgl);
				return NULL;
			}
		} else {

			/* One value only. */
			if (changegl(category, locname, newgl) == -1) {
				freegl(newgl);
				return NULL;
			}

			/* One common value for all categories. */
			if (category == LC_ALL) {
				for (ic = 1; ic < _LC_LAST; ic++) {
					if (changegl(ic, locname,
					    newgl) == -1) {
						freegl(newgl);
						return NULL;
					}
				}
			}
		}
	} else
		newgl = global_locale;

	/*
	 * Assemble a string representation of the globale locale.
	 */

	/* setlocale(3) was never called with a non-NULL argument. */
	if (newgl == NULL) {
		(void)strlcpy(global_locname, "C", sizeof(global_locname));
		goto done;
	}

	/* Individual category, or LC_ALL uniformly set. */
	if (category > LC_ALL || newgl[LC_ALL][0] != '\0') {
		if (strlcpy(global_locname, newgl[category],
		    sizeof(global_locname)) >= sizeof(global_locname))
			global_locname[0] = '\0';
		goto done;
	}

	/*
	 * Check whether all categories agree and return either
	 * the single common name for all categories or a string
	 * listing the names for all categories.
	 */
	for (ic = 2; ic < _LC_LAST; ic++)
		if (strcmp(newgl[ic], newgl[1]) != 0)
			break;
	if (ic == _LC_LAST) {
		if (strlcpy(global_locname, newgl[1],
		    sizeof(global_locname)) >= sizeof(global_locname))
			global_locname[0] = '\0';
	} else {
		ic = snprintf(global_locname, sizeof(global_locname),
		    "%s/%s/%s/%s/%s/%s", newgl[1], newgl[2], newgl[3],
		    newgl[4], newgl[5], newgl[6]);
		if (ic < 0 || ic >= sizeof(global_locname))
			global_locname[0] = '\0';
	}

done:
	if (locname != NULL) {
		/*
		 * We can't replace the global locale earlier
		 * because we first have to make sure that we
		 * also have the memory required to report success.
		 */
		if (global_locname[0] != '\0') {
			freegl(global_locale);
			global_locale = newgl;
			if (category == LC_ALL || category == LC_CTYPE)
				_GlobalRuneLocale =
				    strchr(newgl[LC_CTYPE], '.') == NULL ?
				    &_DefaultRuneLocale : _Utf8RuneLocale;
		} else {
			freegl(newgl);
			return NULL;
		}
	}
	return global_locname;
}
DEF_STRONG(setlocale);
