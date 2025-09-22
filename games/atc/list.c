/*	$OpenBSD: list.c,v 1.9 2015/12/31 16:50:29 mestre Exp $	*/
/*	$NetBSD: list.c,v 1.3 1995/03/21 15:04:18 cgd Exp $	*/

/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ed James.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

/*
 * Copyright (c) 1987 by Ed James, UC Berkeley.  All rights reserved.
 *
 * Copy permission is hereby granted provided that this notice is
 * retained on all partial or complete copies.
 *
 * For more info on this and all of my stuff, mail edjames@berkeley.edu.
 */

#include <stdlib.h>

#include "extern.h"

PLANE	*
newplane(void)
{
	PLANE *p;
	
	if ((p = calloc(1, sizeof (PLANE))) == NULL)
		loser(NULL, "Out of memory");
	return (p);
}

void
append(LIST *l, PLANE *p)
{
	PLANE 	*q = NULL, *r = NULL;

	if (l->head == NULL) {
		p->next = p->prev = NULL;
		l->head = l->tail = p;
	} else {
		q = l -> head;

		while (q != NULL && q->plane_no < p->plane_no) {
			r = q;
			q = q -> next;
		}

		if (q) {
			if (r) {
				p->prev = r;
				r->next = p;
				p->next = q;
				q->prev = p;
			} else {
				p->next = q;
				p->prev = NULL;
				q->prev = p;
				l->head = p;
			}
		} else {
			l->tail->next = p;
			p->next = NULL;
			p->prev = l->tail;
			l->tail = p;
		}
	}
}

void
delete(LIST *l, PLANE *p)
{
	if (l->head == NULL)
		loser(p, "deleted a non-existent plane! Get help!");
	
	if (l->head == p && l->tail == p)
		l->head = l->tail = NULL;
	else if (l->head == p) {
		l->head = p->next;
		l->head->prev = NULL;
	} else if (l->tail == p) {
		l->tail = p->prev;
		l->tail->next = NULL;
	} else {
		p->prev->next = p->next;
		p->next->prev = p->prev;
	}
}
