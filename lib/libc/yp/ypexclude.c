/*	$OpenBSD: ypexclude.c,v 1.2 2015/08/20 21:49:29 deraadt Exp $ */
/*
 * Copyright (c) 2008 Theo de Raadt
 * Copyright (c) 1995, 1996, Jason Downs.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University of California nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdlib.h>
#include <string.h>
#include "ypexclude.h"

int
__ypexclude_add(struct _ypexclude **headp, const char *name)
{
	struct _ypexclude *new;

	if (name[0] == '\0')    /* skip */
		return (0);

	new = malloc(sizeof(struct _ypexclude));
	if (new == NULL)
		return (1);
	new->name = strdup(name);
	if (new->name == NULL) {
		free(new);
		return (1);
	}

	new->next = *headp;
	*headp = new;
	return (0);
}

int
__ypexclude_is(struct _ypexclude **headp, const char *name)
{
	struct _ypexclude *curr;

	for (curr = *headp; curr; curr = curr->next) {
		if (strcmp(curr->name, name) == 0)
			return (1);     /* excluded */
	}
	return (0);
}

void
__ypexclude_free(struct _ypexclude **headp)
{
	struct _ypexclude *curr, *next;

	for (curr = *headp; curr; curr = next) {
		next = curr->next;
		free((void *)curr->name);
		free(curr);
	}
	*headp = NULL;
}
