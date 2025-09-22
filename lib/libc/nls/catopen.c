/*	$OpenBSD: catopen.c,v 1.21 2017/04/27 23:54:08 millert Exp $ */
/*-
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by J.T. Conklin.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define _NLS_PRIVATE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <nl_types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))

#define NLS_DEFAULT_LANG "C"

static nl_catd	load_msgcat(const char *);
static int	verify_msgcat(nl_catd);

nl_catd
catopen(const char *name, int oflag)
{
	char tmppath[PATH_MAX];
	char *nlspath;
	char *lang;
	char *s, *t, *sep, *dot;
	const char *u;
	nl_catd catd;
		
	if (name == NULL || *name == '\0')
		return (nl_catd) -1;

	/* absolute or relative path? */
	if (strchr(name, '/'))
		return load_msgcat(name);

	if (issetugid() != 0 || (nlspath = getenv("NLSPATH")) == NULL)
		return (nl_catd) -1;

	lang = NULL;
	if (oflag & NL_CAT_LOCALE) {
		lang = getenv("LC_ALL");
		if (lang == NULL)
			lang = getenv("LC_MESSAGES");
	}
	if (lang == NULL)
		lang = getenv("LANG");
	if (lang == NULL)
		lang = NLS_DEFAULT_LANG;
	if (strcmp(lang, "POSIX") == 0)
		lang = NLS_DEFAULT_LANG;

	s = nlspath;
	t = tmppath;

	/*
	 * Locale names are of the form language[_territory][.codeset].
	 * See POSIX-1-2008 "8.2 Internationalization Variables"
	 */
	sep = strchr(lang, '_');
	dot = strrchr(lang, '.');
	if (dot && sep && dot < sep)
		dot = NULL; /* ignore dots preceeding _ */
	if (dot == NULL)
		lang = NLS_DEFAULT_LANG; /* no codeset specified */
	do {
		while (*s && *s != ':') {
			if (*s == '%') {
				switch (*(++s)) {
				case 'L':	/* LANG or LC_MESSAGES */
					u = lang;
					while (*u && t < tmppath + PATH_MAX-1)
						*t++ = *u++;
					break;
				case 'N':	/* value of name parameter */
					u = name;
					while (*u && t < tmppath + PATH_MAX-1)
						*t++ = *u++;
					break;
				case 'l':	/* language part */
					u = lang;
					while (*u && t < tmppath + PATH_MAX-1) {
						*t++ = *u++;
						if (sep && u >= sep)
							break;
						if (dot && u >= dot)
							break;
					}
					break;
				case 't':	/* territory part */
					if (sep == NULL)
						break;
					u = sep + 1;
					while (*u && t < tmppath + PATH_MAX-1) {
						*t++ = *u++;
						if (dot && u >= dot)
							break;
					}
					break;
				case 'c':	/* codeset part */
					if (dot == NULL)
						break;
					u = dot + 1;
					while (*u && t < tmppath + PATH_MAX-1)
						*t++ = *u++;
					break;
				default:
					if (t < tmppath + PATH_MAX-1)
						*t++ = *s;
				}
			} else {
				if (t < tmppath + PATH_MAX-1)
					*t++ = *s;
			}
			s++;
		}

		*t = '\0';
		catd = load_msgcat(tmppath);
		if (catd != (nl_catd) -1)
			return catd;

		if (*s)
			s++;
		t = tmppath;
	} while (*s);

	return (nl_catd) -1;
}
DEF_WEAK(catopen);

static nl_catd
load_msgcat(const char *path)
{
	struct stat st;
	nl_catd catd;
	void *data;
	int fd;

	catd = NULL;

	if ((fd = open(path, O_RDONLY|O_CLOEXEC)) == -1)
		return (nl_catd) -1;

	if (fstat(fd, &st) != 0) {
		close (fd);
		return (nl_catd) -1;
	}

	if (st.st_size > INT_MAX || st.st_size < sizeof (struct _nls_cat_hdr)) {
		errno = EINVAL;
		close (fd);
		return (nl_catd) -1;
	}

	data = mmap(0, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	close (fd);

	if (data == MAP_FAILED)
		return (nl_catd) -1;

	if (ntohl(((struct _nls_cat_hdr *) data)->__magic) != _NLS_MAGIC)
		goto invalid;

	if ((catd = malloc(sizeof (*catd))) == 0)
		goto invalid;

	catd->__data = data;
	catd->__size = st.st_size;

	if (verify_msgcat(catd))
		goto invalid;

	return catd;

invalid:
	free(catd);
	munmap(data, st.st_size);
	errno = EINVAL;
	return (nl_catd) -1;
}

static int
verify_msgcat(nl_catd catd)
{
	struct _nls_cat_hdr *cat;
	struct _nls_set_hdr *set;
	struct _nls_msg_hdr *msg;
	size_t remain;
	int hdr_offset, i, index, j, msgs, nmsgs, nsets, off, txt_offset;

	remain = catd->__size;
	cat = (struct _nls_cat_hdr *) catd->__data;

	hdr_offset = ntohl(cat->__msg_hdr_offset);
	nsets = ntohl(cat->__nsets);
	txt_offset = ntohl(cat->__msg_txt_offset);

	/* catalog must contain at least one set and no negative offsets */
	if (nsets < 1 || hdr_offset < 0 || txt_offset < 0)
		return (1);

	remain -= sizeof (*cat);

	/* check if offsets or set size overflow */
	if (remain <= hdr_offset || remain <= ntohl(cat->__msg_txt_offset) ||
	    remain / sizeof (*set) < nsets)
		return (1);

	set = (struct _nls_set_hdr *) ((char *) catd->__data + sizeof (*cat));

	/* make sure that msg has space for at least one index */
	if (remain - hdr_offset < sizeof(*msg))
		return (1);

	msg = (struct _nls_msg_hdr *) ((char *) catd->__data + sizeof (*cat)
	    + hdr_offset);

	/* validate and retrieve largest string offset from sets */
	off = 0;
	for (i = 0; i < nsets; i++) {
		index = ntohl(set[i].__index);
		nmsgs = ntohl(set[i].__nmsgs);
		/* set must contain at least one message */
		if (index < 0 || nmsgs < 1)
			return (1);

		if (INT_MAX - nmsgs < index)
			return (1);
		msgs = index + nmsgs;

		/* avoid msg index overflow */
		if ((remain - hdr_offset) / sizeof(*msg) < msgs)
			return (1);

		/* retrieve largest string offset */
		for (j = index; j < nmsgs; j++) {
			if (ntohl(msg[j].__offset) > INT_MAX)
				return (1);
			off = MAXIMUM(off, ntohl(msg[j].__offset));
		}
	}

	/* check if largest string offset is nul-terminated */
	if (remain - txt_offset < off ||
	    memchr((char *) catd->__data + sizeof(*cat) + txt_offset + off,
	    '\0', remain - txt_offset - off) == NULL)
		return (1);

	return (0);
}

