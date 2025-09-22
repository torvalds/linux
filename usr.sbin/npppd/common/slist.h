/*	$OpenBSD: slist.h,v 1.5 2015/12/17 08:01:55 tb Exp $ */
/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
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
#ifndef SLIST_H
#define SLIST_H 1

typedef struct {
	void **list;
	int last_idx;
	int first_idx;
	int list_size;

	int itr_next;
	int itr_curr;
} slist;

#ifdef __cplusplus
extern "C" {
#endif

void  slist_init (slist *);
void  slist_fini (slist *);
int   slist_length (slist *);
int   slist_set_size (slist *, int);
void  *slist_add (slist *, void *);
int   slist_add_all (slist *, slist *);
void  slist_remove_all (slist *);
void  *slist_get (slist *, int);
int   slist_set (slist *, int, void *);
void  *slist_remove_first (slist *);
void  *slist_remove_last (slist *);
void  slist_swap (slist *, int, int);
void  *slist_remove (slist *, int);
void  slist_shuffle (slist *);
void  slist_itr_first (slist *);
int   slist_itr_has_next (slist *);
void  *slist_itr_next (slist *);
void  *slist_itr_remove (slist *);
void  slist_qsort (slist *, int (*compar)(const void *, const void *));

#ifdef __cplusplus
}
#endif

#endif
