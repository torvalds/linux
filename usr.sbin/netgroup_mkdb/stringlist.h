/*	$OpenBSD: stringlist.h,v 1.2 2023/01/04 13:00:11 jsg Exp $	*/

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

#define _NG_STAR(s)	(((s) == NULL || *(s) == '\0') ? _ngstar : s)
#define _NG_EMPTY(s)	((s) == NULL ? "" : s)
#define _NG_ISSPACE(p)	(isspace((unsigned char) (p)) || (p) == '\n')

/*
 * Simple string list
 */
struct stringlist {
	char		**sl_str;
	size_t		  sl_max;
	size_t		  sl_cur;
};

struct stringlist *_ng_sl_init(void);
int	 _ng_sl_add(struct stringlist *, char *);
void	  _ng_sl_free(struct stringlist *, int);
char	*_ng_sl_find(struct stringlist *, char *);
char	*_ng_makekey(const char *, const char *, size_t);
int	 _ng_parse(char **, char **, struct netgroup **);
void	 _ng_print(char *, size_t, const struct netgroup *);
