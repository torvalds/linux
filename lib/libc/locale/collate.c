/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2014 Garrett D'Amore <garrett@damore.org>
 * Copyright 2010 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 1995 Alex Tatmanjants <alex@elvisti.kiev.ua>
 *		at Electronni Visti IA, Kiev, Ukraine.
 *			All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Adapted to xlocale by John Marino <draco@marino.st>
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include "un-namespace.h"

#include "collate.h"
#include "setlocale.h"
#include "ldpart.h"
#include "libc_private.h"

struct xlocale_collate __xlocale_global_collate = {
	{{0}, "C"}, 1, 0, 0, 0
};

struct xlocale_collate __xlocale_C_collate = {
	{{0}, "C"}, 1, 0, 0, 0
};

static int
__collate_load_tables_l(const char *encoding, struct xlocale_collate *table);

static void
destruct_collate(void *t)
{
	struct xlocale_collate *table = t;
	if (table->map && (table->maplen > 0)) {
		(void) munmap(table->map, table->maplen);
	}
	free(t);
}

void *
__collate_load(const char *encoding, __unused locale_t unused)
{
	if (strcmp(encoding, "C") == 0 || strcmp(encoding, "POSIX") == 0 ||
	    strncmp(encoding, "C.", 2) == 0) {
		return &__xlocale_C_collate;
	}
	struct xlocale_collate *table = calloc(sizeof(struct xlocale_collate), 1);
	table->header.header.destructor = destruct_collate;
	// FIXME: Make sure that _LDP_CACHE is never returned.  We should be doing
	// the caching outside of this section
	if (__collate_load_tables_l(encoding, table) != _LDP_LOADED) {
		xlocale_release(table);
		return NULL;
	}
	return table;
}

/**
 * Load the collation tables for the specified encoding into the global table.
 */
int
__collate_load_tables(const char *encoding)
{

	return (__collate_load_tables_l(encoding, &__xlocale_global_collate));
}

int
__collate_load_tables_l(const char *encoding, struct xlocale_collate *table)
{
	int i, chains, z;
	char *buf;
	char *TMP;
	char *map;
	collate_info_t *info;
	struct stat sbuf;
	int fd;

	table->__collate_load_error = 1;

	/* 'encoding' must be already checked. */
	if (strcmp(encoding, "C") == 0 || strcmp(encoding, "POSIX") == 0 ||
	    strncmp(encoding, "C.", 2) == 0) {
		return (_LDP_CACHE);
	}

	if (asprintf(&buf, "%s/%s/LC_COLLATE", _PathLocale, encoding) == -1)
		return (_LDP_ERROR);

	if ((fd = _open(buf, O_RDONLY)) < 0) {
		free(buf);
		return (_LDP_ERROR);
	}
	free(buf);
	if (_fstat(fd, &sbuf) < 0) {
		(void) _close(fd);
		return (_LDP_ERROR);
	}
	if (sbuf.st_size < (COLLATE_STR_LEN + sizeof (info))) {
		(void) _close(fd);
		errno = EINVAL;
		return (_LDP_ERROR);
	}
	map = mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	(void) _close(fd);
	if ((TMP = map) == NULL) {
		return (_LDP_ERROR);
	}

	if (strncmp(TMP, COLLATE_VERSION, COLLATE_STR_LEN) != 0) {
		(void) munmap(map, sbuf.st_size);
		errno = EINVAL;
		return (_LDP_ERROR);
	}
	TMP += COLLATE_STR_LEN;

	info = (void *)TMP;
	TMP += sizeof (*info);

	if ((info->directive_count < 1) ||
	    (info->directive_count >= COLL_WEIGHTS_MAX) ||
	    ((chains = info->chain_count) < 0)) {
		(void) munmap(map, sbuf.st_size);
		errno = EINVAL;
		return (_LDP_ERROR);
	}

	i = (sizeof (collate_char_t) * (UCHAR_MAX + 1)) +
	    (sizeof (collate_chain_t) * chains) +
	    (sizeof (collate_large_t) * info->large_count);
	for (z = 0; z < info->directive_count; z++) {
		i += sizeof (collate_subst_t) * info->subst_count[z];
	}
	if (i != (sbuf.st_size - (TMP - map))) {
		(void) munmap(map, sbuf.st_size);
		errno = EINVAL;
		return (_LDP_ERROR);
	}

	table->info = info;
	table->char_pri_table = (void *)TMP;
	TMP += sizeof (collate_char_t) * (UCHAR_MAX + 1);

	for (z = 0; z < info->directive_count; z++) {
		if (info->subst_count[z] > 0) {
			table->subst_table[z] = (void *)TMP;
			TMP += info->subst_count[z] * sizeof (collate_subst_t);
		} else {
			table->subst_table[z] = NULL;
		}
	}

	if (chains > 0) {
		table->chain_pri_table = (void *)TMP;
		TMP += chains * sizeof (collate_chain_t);
	} else
		table->chain_pri_table = NULL;
	if (info->large_count > 0)
		table->large_pri_table = (void *)TMP;
	else
		table->large_pri_table = NULL;

	table->__collate_load_error = 0;
	return (_LDP_LOADED);
}

static const int32_t *
substsearch(struct xlocale_collate *table, const wchar_t key, int pass)
{
	const collate_subst_t *p;
	int n = table->info->subst_count[pass];

	if (n == 0)
		return (NULL);

	if (pass >= table->info->directive_count)
		return (NULL);

	if (!(key & COLLATE_SUBST_PRIORITY))
		return (NULL);

	p = table->subst_table[pass] + (key & ~COLLATE_SUBST_PRIORITY);
	assert(p->key == key);

	return (p->pri);
}

static collate_chain_t *
chainsearch(struct xlocale_collate *table, const wchar_t *key, int *len)
{
	int low = 0;
	int high = table->info->chain_count - 1;
	int next, compar, l;
	collate_chain_t *p;
	collate_chain_t *tab = table->chain_pri_table;

	if (high < 0)
		return (NULL);

	while (low <= high) {
		next = (low + high) / 2;
		p = tab + next;
		compar = *key - *p->str;
		if (compar == 0) {
			l = wcsnlen(p->str, COLLATE_STR_LEN);
			compar = wcsncmp(key, p->str, l);
			if (compar == 0) {
				*len = l;
				return (p);
			}
		}
		if (compar > 0)
			low = next + 1;
		else
			high = next - 1;
	}
	return (NULL);
}

static collate_large_t *
largesearch(struct xlocale_collate *table, const wchar_t key)
{
	int low = 0;
	int high = table->info->large_count - 1;
	int next, compar;
	collate_large_t *p;
	collate_large_t *tab = table->large_pri_table;

	if (high < 0)
		return (NULL);

	while (low <= high) {
		next = (low + high) / 2;
		p = tab + next;
		compar = key - p->val;
		if (compar == 0)
			return (p);
		if (compar > 0)
			low = next + 1;
		else
			high = next - 1;
	}
	return (NULL);
}

void
_collate_lookup(struct xlocale_collate *table, const wchar_t *t, int *len,
    int *pri, int which, const int **state)
{
	collate_chain_t *p2;
	collate_large_t *match;
	int p, l;
	const int *sptr;

	/*
	 * If this is the "last" pass for the UNDEFINED, then
	 * we just return the priority itself.
	 */
	if (which >= table->info->directive_count) {
		*pri = *t;
		*len = 1;
		*state = NULL;
		return;
	}

	/*
	 * If we have remaining substitution data from a previous
	 * call, consume it first.
	 */
	if ((sptr = *state) != NULL) {
		*pri = *sptr;
		sptr++;
		if ((sptr == *state) || (sptr == NULL))
			*state = NULL;
		else
			*state = sptr;
		*len = 0;
		return;
	}

	/* No active substitutions */
	*len = 1;

	/*
	 * Check for composites such as diphthongs that collate as a
	 * single element (aka chains or collating-elements).
	 */
	if (((p2 = chainsearch(table, t, &l)) != NULL) &&
	    ((p = p2->pri[which]) >= 0)) {

		*len = l;
		*pri = p;

	} else if (*t <= UCHAR_MAX) {

		/*
		 * Character is a small (8-bit) character.
		 * We just look these up directly for speed.
		 */
		*pri = table->char_pri_table[*t].pri[which];

	} else if ((table->info->large_count > 0) &&
	    ((match = largesearch(table, *t)) != NULL)) {

		/*
		 * Character was found in the extended table.
		 */
		*pri = match->pri.pri[which];

	} else {
		/*
		 * Character lacks a specific definition.
		 */
		if (table->info->directive[which] & DIRECTIVE_UNDEFINED) {
			/* Mask off sign bit to prevent ordering confusion. */
			*pri = (*t & COLLATE_MAX_PRIORITY);
		} else {
			*pri = table->info->undef_pri[which];
		}
		/* No substitutions for undefined characters! */
		return;
	}

	/*
	 * Try substituting (expanding) the character.  We are
	 * currently doing this *after* the chain compression.  I
	 * think it should not matter, but this way might be slightly
	 * faster.
	 *
	 * We do this after the priority search, as this will help us
	 * to identify a single key value.  In order for this to work,
	 * its important that the priority assigned to a given element
	 * to be substituted be unique for that level.  The localedef
	 * code ensures this for us.
	 */
	if ((sptr = substsearch(table, *pri, which)) != NULL) {
		if ((*pri = *sptr) > 0) {
			sptr++;
			*state = *sptr ? sptr : NULL;
		}
	}

}

/*
 * This is the meaty part of wcsxfrm & strxfrm.  Note that it does
 * NOT NULL terminate.  That is left to the caller.
 */
size_t
_collate_wxfrm(struct xlocale_collate *table, const wchar_t *src, wchar_t *xf,
    size_t room)
{
	int		pri;
	int		len;
	const wchar_t	*t;
	wchar_t		*tr = NULL;
	int		direc;
	int		pass;
	const int32_t 	*state;
	size_t		want = 0;
	size_t		need = 0;
	int		ndir = table->info->directive_count;

	assert(src);

	for (pass = 0; pass <= ndir; pass++) {

		state = NULL;

		if (pass != 0) {
			/* insert level separator from the previous pass */
			if (room) {
				*xf++ = 1;
				room--;
			}
			want++;
		}

		/* special pass for undefined */
		if (pass == ndir) {
			direc = DIRECTIVE_FORWARD | DIRECTIVE_UNDEFINED;
		} else {
			direc = table->info->directive[pass];
		}

		t = src;

		if (direc & DIRECTIVE_BACKWARD) {
			wchar_t *bp, *fp, c;
			free(tr);
			if ((tr = wcsdup(t)) == NULL) {
				errno = ENOMEM;
				goto fail;
			}
			bp = tr;
			fp = tr + wcslen(tr) - 1;
			while (bp < fp) {
				c = *bp;
				*bp++ = *fp;
				*fp-- = c;
			}
			t = (const wchar_t *)tr;
		}

		if (direc & DIRECTIVE_POSITION) {
			while (*t || state) {
				_collate_lookup(table, t, &len, &pri, pass, &state);
				t += len;
				if (pri <= 0) {
					if (pri < 0) {
						errno = EINVAL;
						goto fail;
					}
					state = NULL;
					pri = COLLATE_MAX_PRIORITY;
				}
				if (room) {
					*xf++ = pri;
					room--;
				}
				want++;
				need = want;
			}
		} else {
			while (*t || state) {
				_collate_lookup(table, t, &len, &pri, pass, &state);
				t += len;
				if (pri <= 0) {
					if (pri < 0) {
						errno = EINVAL;
						goto fail;
					}
					state = NULL;
					continue;
				}
				if (room) {
					*xf++ = pri;
					room--;
				}
				want++;
				need = want;
			}
		}
	}
	free(tr);
	return (need);

fail:
	free(tr);
	return ((size_t)(-1));
}

/*
 * In the non-POSIX case, we transform each character into a string of
 * characters representing the character's priority.  Since char is usually
 * signed, we are limited by 7 bits per byte.  To avoid zero, we need to add
 * XFRM_OFFSET, so we can't use a full 7 bits.  For simplicity, we choose 6
 * bits per byte.
 *
 * It turns out that we sometimes have real priorities that are
 * 31-bits wide.  (But: be careful using priorities where the high
 * order bit is set -- i.e. the priority is negative.  The sort order
 * may be surprising!)
 *
 * TODO: This would be a good area to optimize somewhat.  It turns out
 * that real prioririties *except for the last UNDEFINED pass* are generally
 * very small.  We need the localedef code to precalculate the max
 * priority for us, and ideally also give us a mask, and then we could
 * severely limit what we expand to.
 */
#define	XFRM_BYTES	6
#define	XFRM_OFFSET	('0')	/* make all printable characters */
#define	XFRM_SHIFT	6
#define	XFRM_MASK	((1 << XFRM_SHIFT) - 1)
#define	XFRM_SEP	('.')	/* chosen to be less than XFRM_OFFSET */

static int
xfrm(struct xlocale_collate *table, unsigned char *p, int pri, int pass)
{
	/* we use unsigned to ensure zero fill on right shift */
	uint32_t val = (uint32_t)table->info->pri_count[pass];
	int nc = 0;

	while (val) {
		*p = (pri & XFRM_MASK) + XFRM_OFFSET;
		pri >>= XFRM_SHIFT;
		val >>= XFRM_SHIFT;
		p++;
		nc++;
	}
	return (nc);
}

size_t
_collate_sxfrm(struct xlocale_collate *table, const wchar_t *src, char *xf,
    size_t room)
{
	int		pri;
	int		len;
	const wchar_t	*t;
	wchar_t		*tr = NULL;
	int		direc;
	int		pass;
	const int32_t 	*state;
	size_t		want = 0;
	size_t		need = 0;
	int		b;
	uint8_t		buf[XFRM_BYTES];
	int		ndir = table->info->directive_count;

	assert(src);

	for (pass = 0; pass <= ndir; pass++) {

		state = NULL;

		if (pass != 0) {
			/* insert level separator from the previous pass */
			if (room) {
				*xf++ = XFRM_SEP;
				room--;
			}
			want++;
		}

		/* special pass for undefined */
		if (pass == ndir) {
			direc = DIRECTIVE_FORWARD | DIRECTIVE_UNDEFINED;
		} else {
			direc = table->info->directive[pass];
		}

		t = src;

		if (direc & DIRECTIVE_BACKWARD) {
			wchar_t *bp, *fp, c;
			free(tr);
			if ((tr = wcsdup(t)) == NULL) {
				errno = ENOMEM;
				goto fail;
			}
			bp = tr;
			fp = tr + wcslen(tr) - 1;
			while (bp < fp) {
				c = *bp;
				*bp++ = *fp;
				*fp-- = c;
			}
			t = (const wchar_t *)tr;
		}

		if (direc & DIRECTIVE_POSITION) {
			while (*t || state) {

				_collate_lookup(table, t, &len, &pri, pass, &state);
				t += len;
				if (pri <= 0) {
					if (pri < 0) {
						errno = EINVAL;
						goto fail;
					}
					state = NULL;
					pri = COLLATE_MAX_PRIORITY;
				}

				b = xfrm(table, buf, pri, pass);
				want += b;
				if (room) {
					while (b) {
						b--;
						if (room) {
							*xf++ = buf[b];
							room--;
						}
					}
				}
				need = want;
			}
		} else {
			while (*t || state) {
				_collate_lookup(table, t, &len, &pri, pass, &state);
				t += len;
				if (pri <= 0) {
					if (pri < 0) {
						errno = EINVAL;
						goto fail;
					}
					state = NULL;
					continue;
				}

				b = xfrm(table, buf, pri, pass);
				want += b;
				if (room) {

					while (b) {
						b--;
						if (room) {
							*xf++ = buf[b];
							room--;
						}
					}
				}
				need = want;
			}
		}
	}
	free(tr);
	return (need);

fail:
	free(tr);
	return ((size_t)(-1));
}

/*
 * __collate_equiv_value returns the primary collation value for the given
 * collating symbol specified by str and len.  Zero or negative is returned
 * if the collating symbol was not found.  This function is used by bracket
 * code in the TRE regex library.
 */
int
__collate_equiv_value(locale_t locale, const wchar_t *str, size_t len)
{
	int32_t e;

	if (len < 1 || len >= COLLATE_STR_LEN)
		return (-1);

	FIX_LOCALE(locale);
	struct xlocale_collate *table =
		(struct xlocale_collate*)locale->components[XLC_COLLATE];

	if (table->__collate_load_error)
		return ((len == 1 && *str <= UCHAR_MAX) ? *str : -1);

	if (len == 1) {
		e = -1;
		if (*str <= UCHAR_MAX)
			e = table->char_pri_table[*str].pri[0];
		else if (table->info->large_count > 0) {
			collate_large_t *match_large;
			match_large = largesearch(table, *str);
			if (match_large)
				e = match_large->pri.pri[0];
		}
		if (e == 0)
			return (1);
		return (e > 0 ? e : 0);
	}
	if (table->info->chain_count > 0) {
		wchar_t name[COLLATE_STR_LEN];
		collate_chain_t *match_chain;
		int clen;

		wcsncpy (name, str, len);
		name[len] = 0;
		match_chain = chainsearch(table, name, &clen);
		if (match_chain) {
			e = match_chain->pri[0];
			if (e == 0)
				return (1);
			return (e < 0 ? -e : e);
		}
	}
	return (0);
}
