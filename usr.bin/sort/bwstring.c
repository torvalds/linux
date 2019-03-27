/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2009 Gabor Kovesdan <gabor@FreeBSD.org>
 * Copyright (C) 2012 Oleg Moskalenko <mom040267@gmail.com>
 * All rights reserved.
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

#include <ctype.h>
#include <errno.h>
#include <err.h>
#include <langinfo.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "bwstring.h"
#include "sort.h"

bool byte_sort;

static wchar_t **wmonths;
static unsigned char **cmonths;

/* initialise months */

void
initialise_months(void)
{
	const nl_item item[12] = { ABMON_1, ABMON_2, ABMON_3, ABMON_4,
	    ABMON_5, ABMON_6, ABMON_7, ABMON_8, ABMON_9, ABMON_10,
	    ABMON_11, ABMON_12 };
	unsigned char *tmp;
	size_t len;

	if (MB_CUR_MAX == 1) {
		if (cmonths == NULL) {
			unsigned char *m;

			cmonths = sort_malloc(sizeof(unsigned char*) * 12);
			for (int i = 0; i < 12; i++) {
				cmonths[i] = NULL;
				tmp = (unsigned char *) nl_langinfo(item[i]);
				if (debug_sort)
					printf("month[%d]=%s\n", i, tmp);
				if (*tmp == '\0')
					continue;
				m = sort_strdup(tmp);
				len = strlen(tmp);
				for (unsigned int j = 0; j < len; j++)
					m[j] = toupper(m[j]);
				cmonths[i] = m;
			}
		}

	} else {
		if (wmonths == NULL) {
			wchar_t *m;

			wmonths = sort_malloc(sizeof(wchar_t *) * 12);
			for (int i = 0; i < 12; i++) {
				wmonths[i] = NULL;
				tmp = (unsigned char *) nl_langinfo(item[i]);
				if (debug_sort)
					printf("month[%d]=%s\n", i, tmp);
				if (*tmp == '\0')
					continue;
				len = strlen(tmp);
				m = sort_malloc(SIZEOF_WCHAR_STRING(len + 1));
				if (mbstowcs(m, (char*)tmp, len) ==
				    ((size_t) - 1)) {
					sort_free(m);
					continue;
				}
				m[len] = L'\0';
				for (unsigned int j = 0; j < len; j++)
					m[j] = towupper(m[j]);
				wmonths[i] = m;
			}
		}
	}
}

/*
 * Compare two wide-character strings
 */
static int
wide_str_coll(const wchar_t *s1, const wchar_t *s2)
{
	int ret = 0;

	errno = 0;
	ret = wcscoll(s1, s2);
	if (errno == EILSEQ) {
		errno = 0;
		ret = wcscmp(s1, s2);
		if (errno != 0) {
			for (size_t i = 0; ; ++i) {
				wchar_t c1 = s1[i];
				wchar_t c2 = s2[i];
				if (c1 == L'\0')
					return ((c2 == L'\0') ? 0 : -1);
				if (c2 == L'\0')
					return (+1);
				if (c1 == c2)
					continue;
				return ((int)(c1 - c2));
			}
		}
	}
	return (ret);
}

/* counterparts of wcs functions */

void
bwsprintf(FILE *f, struct bwstring *bws, const char *prefix, const char *suffix)
{

	if (MB_CUR_MAX == 1)
		fprintf(f, "%s%s%s", prefix, bws->data.cstr, suffix);
	else
		fprintf(f, "%s%S%s", prefix, bws->data.wstr, suffix);
}

const void* bwsrawdata(const struct bwstring *bws)
{

	return (&(bws->data));
}

size_t bwsrawlen(const struct bwstring *bws)
{

	return ((MB_CUR_MAX == 1) ? bws->len : SIZEOF_WCHAR_STRING(bws->len));
}

size_t
bws_memsize(const struct bwstring *bws)
{

	return ((MB_CUR_MAX == 1) ? (bws->len + 2 + sizeof(struct bwstring)) :
	    (SIZEOF_WCHAR_STRING(bws->len + 1) + sizeof(struct bwstring)));
}

void
bws_setlen(struct bwstring *bws, size_t newlen)
{

	if (bws && newlen != bws->len && newlen <= bws->len) {
		bws->len = newlen;
		if (MB_CUR_MAX == 1)
			bws->data.cstr[newlen] = '\0';
		else
			bws->data.wstr[newlen] = L'\0';
	}
}

/*
 * Allocate a new binary string of specified size
 */
struct bwstring *
bwsalloc(size_t sz)
{
	struct bwstring *ret;

	if (MB_CUR_MAX == 1)
		ret = sort_malloc(sizeof(struct bwstring) + 1 + sz);
	else
		ret = sort_malloc(sizeof(struct bwstring) +
		    SIZEOF_WCHAR_STRING(sz + 1));
	ret->len = sz;

	if (MB_CUR_MAX == 1)
		ret->data.cstr[ret->len] = '\0';
	else
		ret->data.wstr[ret->len] = L'\0';

	return (ret);
}

/*
 * Create a copy of binary string.
 * New string size equals the length of the old string.
 */
struct bwstring *
bwsdup(const struct bwstring *s)
{

	if (s == NULL)
		return (NULL);
	else {
		struct bwstring *ret = bwsalloc(s->len);

		if (MB_CUR_MAX == 1)
			memcpy(ret->data.cstr, s->data.cstr, (s->len));
		else
			memcpy(ret->data.wstr, s->data.wstr,
			    SIZEOF_WCHAR_STRING(s->len));

		return (ret);
	}
}

/*
 * Create a new binary string from a wide character buffer.
 */
struct bwstring *
bwssbdup(const wchar_t *str, size_t len)
{

	if (str == NULL)
		return ((len == 0) ? bwsalloc(0) : NULL);
	else {
		struct bwstring *ret;

		ret = bwsalloc(len);

		if (MB_CUR_MAX == 1)
			for (size_t i = 0; i < len; ++i)
				ret->data.cstr[i] = (unsigned char) str[i];
		else
			memcpy(ret->data.wstr, str, SIZEOF_WCHAR_STRING(len));

		return (ret);
	}
}

/*
 * Create a new binary string from a raw binary buffer.
 */
struct bwstring *
bwscsbdup(const unsigned char *str, size_t len)
{
	struct bwstring *ret;

	ret = bwsalloc(len);

	if (str) {
		if (MB_CUR_MAX == 1)
			memcpy(ret->data.cstr, str, len);
		else {
			mbstate_t mbs;
			const char *s;
			size_t charlen, chars, cptr;

			chars = 0;
			cptr = 0;
			s = (const char *) str;

			memset(&mbs, 0, sizeof(mbs));

			while (cptr < len) {
				size_t n = MB_CUR_MAX;

				if (n > len - cptr)
					n = len - cptr;
				charlen = mbrlen(s + cptr, n, &mbs);
				switch (charlen) {
				case 0:
					/* FALLTHROUGH */
				case (size_t) -1:
					/* FALLTHROUGH */
				case (size_t) -2:
					ret->data.wstr[chars++] =
					    (unsigned char) s[cptr];
					++cptr;
					break;
				default:
					n = mbrtowc(ret->data.wstr + (chars++),
					    s + cptr, charlen, &mbs);
					if ((n == (size_t)-1) || (n == (size_t)-2))
						/* NOTREACHED */
						err(2, "mbrtowc error");
					cptr += charlen;
				}
			}

			ret->len = chars;
			ret->data.wstr[ret->len] = L'\0';
		}
	}
	return (ret);
}

/*
 * De-allocate object memory
 */
void
bwsfree(const struct bwstring *s)
{

	if (s)
		sort_free(s);
}

/*
 * Copy content of src binary string to dst.
 * If the capacity of the dst string is not sufficient,
 * then the data is truncated.
 */
size_t
bwscpy(struct bwstring *dst, const struct bwstring *src)
{
	size_t nums = src->len;

	if (nums > dst->len)
		nums = dst->len;
	dst->len = nums;

	if (MB_CUR_MAX == 1) {
		memcpy(dst->data.cstr, src->data.cstr, nums);
		dst->data.cstr[dst->len] = '\0';
	} else {
		memcpy(dst->data.wstr, src->data.wstr,
		    SIZEOF_WCHAR_STRING(nums + 1));
		dst->data.wstr[dst->len] = L'\0';
	}

	return (nums);
}

/*
 * Copy content of src binary string to dst,
 * with specified number of symbols to be copied.
 * If the capacity of the dst string is not sufficient,
 * then the data is truncated.
 */
struct bwstring *
bwsncpy(struct bwstring *dst, const struct bwstring *src, size_t size)
{
	size_t nums = src->len;

	if (nums > dst->len)
		nums = dst->len;
	if (nums > size)
		nums = size;
	dst->len = nums;

	if (MB_CUR_MAX == 1) {
		memcpy(dst->data.cstr, src->data.cstr, nums);
		dst->data.cstr[dst->len] = '\0';
	} else {
		memcpy(dst->data.wstr, src->data.wstr,
		    SIZEOF_WCHAR_STRING(nums + 1));
		dst->data.wstr[dst->len] = L'\0';
	}

	return (dst);
}

/*
 * Copy content of src binary string to dst,
 * with specified number of symbols to be copied.
 * An offset value can be specified, from the start of src string.
 * If the capacity of the dst string is not sufficient,
 * then the data is truncated.
 */
struct bwstring *
bwsnocpy(struct bwstring *dst, const struct bwstring *src, size_t offset,
    size_t size)
{

	if (offset >= src->len) {
		dst->data.wstr[0] = 0;
		dst->len = 0;
	} else {
		size_t nums = src->len - offset;

		if (nums > dst->len)
			nums = dst->len;
		if (nums > size)
			nums = size;
		dst->len = nums;
		if (MB_CUR_MAX == 1) {
			memcpy(dst->data.cstr, src->data.cstr + offset,
			    (nums));
			dst->data.cstr[dst->len] = '\0';
		} else {
			memcpy(dst->data.wstr, src->data.wstr + offset,
			    SIZEOF_WCHAR_STRING(nums));
			dst->data.wstr[dst->len] = L'\0';
		}
	}
	return (dst);
}

/*
 * Write binary string to the file.
 * The output is ended either with '\n' (nl == true)
 * or '\0' (nl == false).
 */
size_t
bwsfwrite(struct bwstring *bws, FILE *f, bool zero_ended)
{

	if (MB_CUR_MAX == 1) {
		size_t len = bws->len;

		if (!zero_ended) {
			bws->data.cstr[len] = '\n';

			if (fwrite(bws->data.cstr, len + 1, 1, f) < 1)
				err(2, NULL);

			bws->data.cstr[len] = '\0';
		} else if (fwrite(bws->data.cstr, len + 1, 1, f) < 1)
			err(2, NULL);

		return (len + 1);

	} else {
		wchar_t eols;
		size_t printed = 0;

		eols = zero_ended ? btowc('\0') : btowc('\n');

		while (printed < BWSLEN(bws)) {
			const wchar_t *s = bws->data.wstr + printed;

			if (*s == L'\0') {
				int nums;

				nums = fwprintf(f, L"%lc", *s);

				if (nums != 1)
					err(2, NULL);
				++printed;
			} else {
				int nums;

				nums = fwprintf(f, L"%ls", s);

				if (nums < 1)
					err(2, NULL);
				printed += nums;
			}
		}
		fwprintf(f, L"%lc", eols);
		return (printed + 1);
	}
}

/*
 * Allocate and read a binary string from file.
 * The strings are nl-ended or zero-ended, depending on the sort setting.
 */
struct bwstring *
bwsfgetln(FILE *f, size_t *len, bool zero_ended, struct reader_buffer *rb)
{
	wint_t eols;

	eols = zero_ended ? btowc('\0') : btowc('\n');

	if (!zero_ended && (MB_CUR_MAX > 1)) {
		wchar_t *ret;

		ret = fgetwln(f, len);

		if (ret == NULL) {
			if (!feof(f))
				err(2, NULL);
			return (NULL);
		}
		if (*len > 0) {
			if (ret[*len - 1] == (wchar_t)eols)
				--(*len);
		}
		return (bwssbdup(ret, *len));

	} else if (!zero_ended && (MB_CUR_MAX == 1)) {
		char *ret;

		ret = fgetln(f, len);

		if (ret == NULL) {
			if (!feof(f))
				err(2, NULL);
			return (NULL);
		}
		if (*len > 0) {
			if (ret[*len - 1] == '\n')
				--(*len);
		}
		return (bwscsbdup((unsigned char*)ret, *len));

	} else {
		*len = 0;

		if (feof(f))
			return (NULL);

		if (2 >= rb->fgetwln_z_buffer_size) {
			rb->fgetwln_z_buffer_size += 256;
			rb->fgetwln_z_buffer = sort_realloc(rb->fgetwln_z_buffer,
			    sizeof(wchar_t) * rb->fgetwln_z_buffer_size);
		}
		rb->fgetwln_z_buffer[*len] = 0;

		if (MB_CUR_MAX == 1)
			while (!feof(f)) {
				int c;

				c = fgetc(f);

				if (c == EOF) {
					if (*len == 0)
						return (NULL);
					goto line_read_done;
				}
				if (c == eols)
					goto line_read_done;

				if (*len + 1 >= rb->fgetwln_z_buffer_size) {
					rb->fgetwln_z_buffer_size += 256;
					rb->fgetwln_z_buffer = sort_realloc(rb->fgetwln_z_buffer,
					    SIZEOF_WCHAR_STRING(rb->fgetwln_z_buffer_size));
				}

				rb->fgetwln_z_buffer[*len] = c;
				rb->fgetwln_z_buffer[++(*len)] = 0;
			}
		else
			while (!feof(f)) {
				wint_t c = 0;

				c = fgetwc(f);

				if (c == WEOF) {
					if (*len == 0)
						return (NULL);
					goto line_read_done;
				}
				if (c == eols)
					goto line_read_done;

				if (*len + 1 >= rb->fgetwln_z_buffer_size) {
					rb->fgetwln_z_buffer_size += 256;
					rb->fgetwln_z_buffer = sort_realloc(rb->fgetwln_z_buffer,
					    SIZEOF_WCHAR_STRING(rb->fgetwln_z_buffer_size));
				}

				rb->fgetwln_z_buffer[*len] = c;
				rb->fgetwln_z_buffer[++(*len)] = 0;
			}

line_read_done:
		/* we do not count the last 0 */
		return (bwssbdup(rb->fgetwln_z_buffer, *len));
	}
}

int
bwsncmp(const struct bwstring *bws1, const struct bwstring *bws2,
    size_t offset, size_t len)
{
	size_t cmp_len, len1, len2;
	int res = 0;

	len1 = bws1->len;
	len2 = bws2->len;

	if (len1 <= offset) {
		return ((len2 <= offset) ? 0 : -1);
	} else {
		if (len2 <= offset)
			return (+1);
		else {
			len1 -= offset;
			len2 -= offset;

			cmp_len = len1;

			if (len2 < cmp_len)
				cmp_len = len2;

			if (len < cmp_len)
				cmp_len = len;

			if (MB_CUR_MAX == 1) {
				const unsigned char *s1, *s2;

				s1 = bws1->data.cstr + offset;
				s2 = bws2->data.cstr + offset;

				res = memcmp(s1, s2, cmp_len);

			} else {
				const wchar_t *s1, *s2;

				s1 = bws1->data.wstr + offset;
				s2 = bws2->data.wstr + offset;

				res = memcmp(s1, s2, SIZEOF_WCHAR_STRING(cmp_len));
			}
		}
	}

	if (res == 0) {
		if (len1 < cmp_len && len1 < len2)
			res = -1;
		else if (len2 < cmp_len && len2 < len1)
			res = +1;
	}

	return (res);
}

int
bwscmp(const struct bwstring *bws1, const struct bwstring *bws2, size_t offset)
{
	size_t len1, len2, cmp_len;
	int res;

	len1 = bws1->len;
	len2 = bws2->len;

	len1 -= offset;
	len2 -= offset;

	cmp_len = len1;

	if (len2 < cmp_len)
		cmp_len = len2;

	res = bwsncmp(bws1, bws2, offset, cmp_len);

	if (res == 0) {
		if( len1 < len2)
			res = -1;
		else if (len2 < len1)
			res = +1;
	}

	return (res);
}

int
bws_iterator_cmp(bwstring_iterator iter1, bwstring_iterator iter2, size_t len)
{
	wchar_t c1, c2;
	size_t i = 0;

	for (i = 0; i < len; ++i) {
		c1 = bws_get_iter_value(iter1);
		c2 = bws_get_iter_value(iter2);
		if (c1 != c2)
			return (c1 - c2);
		iter1 = bws_iterator_inc(iter1, 1);
		iter2 = bws_iterator_inc(iter2, 1);
	}

	return (0);
}

int
bwscoll(const struct bwstring *bws1, const struct bwstring *bws2, size_t offset)
{
	size_t len1, len2;

	len1 = bws1->len;
	len2 = bws2->len;

	if (len1 <= offset)
		return ((len2 <= offset) ? 0 : -1);
	else {
		if (len2 <= offset)
			return (+1);
		else {
			len1 -= offset;
			len2 -= offset;

			if (MB_CUR_MAX == 1) {
				const unsigned char *s1, *s2;

				s1 = bws1->data.cstr + offset;
				s2 = bws2->data.cstr + offset;

				if (byte_sort) {
					int res = 0;

					if (len1 > len2) {
						res = memcmp(s1, s2, len2);
						if (!res)
							res = +1;
					} else if (len1 < len2) {
						res = memcmp(s1, s2, len1);
						if (!res)
							res = -1;
					} else
						res = memcmp(s1, s2, len1);

					return (res);

				} else {
					int res = 0;
					size_t i, maxlen;

					i = 0;
					maxlen = len1;

					if (maxlen > len2)
						maxlen = len2;

					while (i < maxlen) {
						/* goto next non-zero part: */
						while ((i < maxlen) &&
						    !s1[i] && !s2[i])
							++i;

						if (i >= maxlen)
							break;

						if (s1[i] == 0) {
							if (s2[i] == 0)
								/* NOTREACHED */
								err(2, "bwscoll error 01");
							else
								return (-1);
						} else if (s2[i] == 0)
							return (+1);

						res = strcoll((const char*)(s1 + i), (const char*)(s2 + i));
						if (res)
							return (res);

						while ((i < maxlen) &&
						    s1[i] && s2[i])
							++i;

						if (i >= maxlen)
							break;

						if (s1[i] == 0) {
							if (s2[i] == 0) {
								++i;
								continue;
							} else
								return (-1);
						} else if (s2[i] == 0)
							return (+1);
						else
							/* NOTREACHED */
							err(2, "bwscoll error 02");
					}

					if (len1 < len2)
						return (-1);
					else if (len1 > len2)
						return (+1);

					return (0);
				}
			} else {
				const wchar_t *s1, *s2;
				size_t i, maxlen;
				int res = 0;

				s1 = bws1->data.wstr + offset;
				s2 = bws2->data.wstr + offset;

				i = 0;
				maxlen = len1;

				if (maxlen > len2)
					maxlen = len2;

				while (i < maxlen) {

					/* goto next non-zero part: */
					while ((i < maxlen) &&
					    !s1[i] && !s2[i])
						++i;

					if (i >= maxlen)
						break;

					if (s1[i] == 0) {
						if (s2[i] == 0)
							/* NOTREACHED */
							err(2, "bwscoll error 1");
						else
							return (-1);
					} else if (s2[i] == 0)
						return (+1);

					res = wide_str_coll(s1 + i, s2 + i);
					if (res)
						return (res);

					while ((i < maxlen) && s1[i] && s2[i])
						++i;

					if (i >= maxlen)
						break;

					if (s1[i] == 0) {
						if (s2[i] == 0) {
							++i;
							continue;
						} else
							return (-1);
					} else if (s2[i] == 0)
						return (+1);
					else
						/* NOTREACHED */
						err(2, "bwscoll error 2");
				}

				if (len1 < len2)
					return (-1);
				else if (len1 > len2)
					return (+1);

				return (0);
			}
		}
	}
}

/*
 * Correction of the system API
 */
double
bwstod(struct bwstring *s0, bool *empty)
{
	double ret = 0;

	if (MB_CUR_MAX == 1) {
		unsigned char *end, *s;
		char *ep;

		s = s0->data.cstr;
		end = s + s0->len;
		ep = NULL;

		while (isblank(*s) && s < end)
			++s;

		if (!isprint(*s)) {
			*empty = true;
			return (0);
		}

		ret = strtod((char*)s, &ep);
		if ((unsigned char*) ep == s) {
			*empty = true;
			return (0);
		}
	} else {
		wchar_t *end, *ep, *s;

		s = s0->data.wstr;
		end = s + s0->len;
		ep = NULL;

		while (iswblank(*s) && s < end)
			++s;

		if (!iswprint(*s)) {
			*empty = true;
			return (0);
		}

		ret = wcstod(s, &ep);
		if (ep == s) {
			*empty = true;
			return (0);
		}
	}

	*empty = false;
	return (ret);
}

/*
 * A helper function for monthcoll.  If a line matches
 * a month name, it returns (number of the month - 1),
 * while if there is no match, it just return -1.
 */

int
bws_month_score(const struct bwstring *s0)
{

	if (MB_CUR_MAX == 1) {
		const unsigned char *end, *s;

		s = s0->data.cstr;
		end = s + s0->len;

		while (isblank(*s) && s < end)
			++s;

		for (int i = 11; i >= 0; --i) {
			if (cmonths[i] &&
			    (s == (unsigned char*)strstr((const char*)s, (char*)(cmonths[i]))))
				return (i);
		}

	} else {
		const wchar_t *end, *s;

		s = s0->data.wstr;
		end = s + s0->len;

		while (iswblank(*s) && s < end)
			++s;

		for (int i = 11; i >= 0; --i) {
			if (wmonths[i] && (s == wcsstr(s, wmonths[i])))
				return (i);
		}
	}

	return (-1);
}

/*
 * Rips out leading blanks (-b).
 */
struct bwstring *
ignore_leading_blanks(struct bwstring *str)
{

	if (MB_CUR_MAX == 1) {
		unsigned char *dst, *end, *src;

		src = str->data.cstr;
		dst = src;
		end = src + str->len;

		while (src < end && isblank(*src))
			++src;

		if (src != dst) {
			size_t newlen;

			newlen = BWSLEN(str) - (src - dst);

			while (src < end) {
				*dst = *src;
				++dst;
				++src;
			}
			bws_setlen(str, newlen);
		}
	} else {
		wchar_t *dst, *end, *src;

		src = str->data.wstr;
		dst = src;
		end = src + str->len;

		while (src < end && iswblank(*src))
			++src;

		if (src != dst) {

			size_t newlen = BWSLEN(str) - (src - dst);

			while (src < end) {
				*dst = *src;
				++dst;
				++src;
			}
			bws_setlen(str, newlen);

		}
	}
	return (str);
}

/*
 * Rips out nonprinting characters (-i).
 */
struct bwstring *
ignore_nonprinting(struct bwstring *str)
{
	size_t newlen = str->len;

	if (MB_CUR_MAX == 1) {
		unsigned char *dst, *end, *src;
		unsigned char c;

		src = str->data.cstr;
		dst = src;
		end = src + str->len;

		while (src < end) {
			c = *src;
			if (isprint(c)) {
				*dst = c;
				++dst;
				++src;
			} else {
				++src;
				--newlen;
			}
		}
	} else {
		wchar_t *dst, *end, *src;
		wchar_t c;

		src = str->data.wstr;
		dst = src;
		end = src + str->len;

		while (src < end) {
			c = *src;
			if (iswprint(c)) {
				*dst = c;
				++dst;
				++src;
			} else {
				++src;
				--newlen;
			}
		}
	}
	bws_setlen(str, newlen);

	return (str);
}

/*
 * Rips out any characters that are not alphanumeric characters
 * nor blanks (-d).
 */
struct bwstring *
dictionary_order(struct bwstring *str)
{
	size_t newlen = str->len;

	if (MB_CUR_MAX == 1) {
		unsigned char *dst, *end, *src;
		unsigned char c;

		src = str->data.cstr;
		dst = src;
		end = src + str->len;

		while (src < end) {
			c = *src;
			if (isalnum(c) || isblank(c)) {
				*dst = c;
				++dst;
				++src;
			} else {
				++src;
				--newlen;
			}
		}
	} else {
		wchar_t *dst, *end, *src;
		wchar_t c;

		src = str->data.wstr;
		dst = src;
		end = src + str->len;

		while (src < end) {
			c = *src;
			if (iswalnum(c) || iswblank(c)) {
				*dst = c;
				++dst;
				++src;
			} else {
				++src;
				--newlen;
			}
		}
	}
	bws_setlen(str, newlen);

	return (str);
}

/*
 * Converts string to lower case(-f).
 */
struct bwstring *
ignore_case(struct bwstring *str)
{

	if (MB_CUR_MAX == 1) {
		unsigned char *end, *s;

		s = str->data.cstr;
		end = s + str->len;

		while (s < end) {
			*s = toupper(*s);
			++s;
		}
	} else {
		wchar_t *end, *s;

		s = str->data.wstr;
		end = s + str->len;

		while (s < end) {
			*s = towupper(*s);
			++s;
		}
	}
	return (str);
}

void
bws_disorder_warnx(struct bwstring *s, const char *fn, size_t pos)
{

	if (MB_CUR_MAX == 1)
		warnx("%s:%zu: disorder: %s", fn, pos + 1, s->data.cstr);
	else
		warnx("%s:%zu: disorder: %ls", fn, pos + 1, s->data.wstr);
}
