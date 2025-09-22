/*
 * Copyright (c) 1994 Christos Zoulas
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Counted strings
 */
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "str.h"

/*
 * str_init(): Initialize string
 */
void
str_init(struct string *s)
{
	s->s_str = NULL;
	s->s_len = 0;
}


/*
 * str_append(): Append string allocating buffer as necessary
 */
void
str_append(struct string *buf, const char *str, int del)
{
	size_t          len = strlen(str) + 1;

	if (buf->s_str == NULL)
		buf->s_str = emalloc(len);
	else {
		buf->s_str = erealloc(buf->s_str, buf->s_len +
		    len + (del ? 2 : 1));
		if (del)
			buf->s_str[buf->s_len++] = del;
	}

	memcpy(&buf->s_str[buf->s_len], str, len);
	buf->s_len += len - 1;
}

/*
 * str_prepend(): Prepend string allocating buffer as necessary
 */
void
str_prepend(struct string *buf, const char *str, int del)
{
	char           *ptr, *sptr;
	size_t          len = strlen(str) + 1;

	sptr = ptr = emalloc(buf->s_len + len + (del ? 2 : 1));

	if (del)
		*ptr++ = del;

	memcpy(ptr, str, len);

	if (buf->s_str) {
		memcpy(&ptr[len - 1], buf->s_str, buf->s_len + 1);
		free(buf->s_str);
	}

	buf->s_str = sptr;
	buf->s_len += del ? len : len - 1;
}

/*
 * str_free(): Free a string
 */
void
str_free(struct string *s)
{
	free(s->s_str);
	s->s_str = NULL;
	s->s_len = 0;
}
