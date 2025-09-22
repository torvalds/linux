/*-
 * Copyright (c) 2010, 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by David A. Holland.
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
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "utils.h"

#define MALLOCDEBUG

const char ws[] =
	" \t\f\v"
;
const char alnum[] = 
	"0123456789"
	"abcdefghijklmnopqrstuvwxyz"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"_"
;

////////////////////////////////////////////////////////////
// malloc

#define ROUNDUP(len, size) ((size) * (((len) + (size) - 1) / (size)))

#ifdef MALLOCDEBUG

struct mallocheader {
	struct mallocheader *self;
	size_t len;
};

static
size_t
adjustsize(size_t len)
{
	const size_t sz = sizeof(struct mallocheader);
	return ROUNDUP(len, sz) + 2*sz;
}

static
void *
placeheaders(void *block, size_t len)
{
	struct mallocheader *bothdr, *tophdr;
	size_t roundedlen;
	void *ret;

	roundedlen = ROUNDUP(len, sizeof(struct mallocheader));
	bothdr = block;
	bothdr->len = len;
	bothdr->self = block;
	ret = bothdr + 1;
	tophdr = (void *)(((unsigned char *)ret) + roundedlen);
	tophdr->len = len;
	tophdr->self = bothdr;
	return ret;
}

static
void *
checkheaders(void *block, size_t len)
{
	struct mallocheader *bothdr, *tophdr;
	size_t roundedlen;

	if (block == NULL) {
		assert(len == 0);
		return block;
	}

	roundedlen = ROUNDUP(len, sizeof(struct mallocheader));
	bothdr = block;
	bothdr--;
	assert(bothdr->self == bothdr);
	assert(bothdr->len == len);
	tophdr = (void *)(((unsigned char *)(bothdr + 1)) + roundedlen);
	assert(tophdr->self == bothdr);
	assert(tophdr->len == len);
	return bothdr;
}

#else

#define adjustsize(len) (len)
#define placeheaders(block, len) ((void)(len), (block))
#define checkheaders(ptr, len) ((void)(len), (ptr))

#endif /* MALLOCDEBUG */

void *
domalloc(size_t len)
{
	void *ret;
	size_t blocklen;

	blocklen = adjustsize(len);
	ret = malloc(blocklen);
	if (ret == NULL) {
		complain(NULL, "Out of memory");
		die();
	}

	return placeheaders(ret, len);
}

void *
dorealloc(void *ptr, size_t oldlen, size_t newlen)
{
	void *ret;
	void *blockptr;
	size_t newblocklen;

	blockptr = checkheaders(ptr, oldlen);
	newblocklen = adjustsize(newlen);

	ret = realloc(blockptr, newblocklen);
	if (ret == NULL) {
		complain(NULL, "Out of memory");
		die();
	}

	return placeheaders(ret, newlen);
}

void
dofree(void *ptr, size_t len)
{
	void *blockptr;

	blockptr = checkheaders(ptr, len);
	free(blockptr);
}

////////////////////////////////////////////////////////////
// string allocators

char *
dostrdup(const char *s)
{
	char *ret;
	size_t len;

	len = strlen(s);
	ret = domalloc(len+1);
	strlcpy(ret, s, len+1);
	return ret;
}

char *
dostrdup2(const char *s, const char *t)
{
	char *ret;
	size_t len;

	len = strlen(s) + strlen(t);
	ret = domalloc(len+1);
	snprintf(ret, len+1, "%s%s", s, t);
	return ret;
}

char *
dostrdup3(const char *s, const char *t, const char *u)
{
	char *ret;
	size_t len;

	len = strlen(s) + strlen(t) + strlen(u);
	ret = domalloc(len+1);
	snprintf(ret, len+1, "%s%s%s", s, t, u);
	return ret;
}

char *
dostrndup(const char *s, size_t len)
{
	char *ret;

	ret = domalloc(len+1);
	memcpy(ret, s, len);
	ret[len] = '\0';
	return ret;
}

void
dostrfree(char *s)
{
	dofree(s, strlen(s)+1);
}

////////////////////////////////////////////////////////////
// other stuff

size_t
notrailingws(char *buf, size_t len)
{
	while (len > 0 && strchr(ws, buf[len-1])) {
		buf[--len] = '\0';
	}
	return len;
}

bool
is_identifier(const char *str)
{
	size_t len;

	len = strlen(str);
	if (len != strspn(str, alnum)) {
		return false;
	}
	if (str[0] >= '0' && str[0] <= '9') {
		return false;
	}
	return true;
}
