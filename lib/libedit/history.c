/*	$NetBSD: history.c,v 1.52 2016/02/17 19:47:49 christos Exp $	*/

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Christos Zoulas of Cornell University.
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

#include "config.h"
#if !defined(lint) && !defined(SCCSID)
#if 0
static char sccsid[] = "@(#)history.c	8.1 (Berkeley) 6/4/93";
#else
__RCSID("$NetBSD: history.c,v 1.52 2016/02/17 19:47:49 christos Exp $");
#endif
#endif /* not lint && not SCCSID */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * hist.c: TYPE(History) access functions
 */
#include <sys/stat.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <vis.h>

static const char hist_cookie[] = "_HiStOrY_V2_\n";

#include "histedit.h"
#include "chartype.h"

typedef int (*history_gfun_t)(void *, TYPE(HistEvent) *);
typedef int (*history_efun_t)(void *, TYPE(HistEvent) *, const Char *);
typedef void (*history_vfun_t)(void *, TYPE(HistEvent) *);
typedef int (*history_sfun_t)(void *, TYPE(HistEvent) *, const int);

struct TYPE(history) {
	void *h_ref;		/* Argument for history fcns	 */
	int h_ent;		/* Last entry point for history	 */
	history_gfun_t h_first;	/* Get the first element	 */
	history_gfun_t h_next;	/* Get the next element		 */
	history_gfun_t h_last;	/* Get the last element		 */
	history_gfun_t h_prev;	/* Get the previous element	 */
	history_gfun_t h_curr;	/* Get the current element	 */
	history_sfun_t h_set;	/* Set the current element	 */
	history_sfun_t h_del;	/* Set the given element	 */
	history_vfun_t h_clear;	/* Clear the history list	 */
	history_efun_t h_enter;	/* Add an element		 */
	history_efun_t h_add;	/* Append to an element		 */
};

#define	HNEXT(h, ev)		(*(h)->h_next)((h)->h_ref, ev)
#define	HFIRST(h, ev)		(*(h)->h_first)((h)->h_ref, ev)
#define	HPREV(h, ev)		(*(h)->h_prev)((h)->h_ref, ev)
#define	HLAST(h, ev)		(*(h)->h_last)((h)->h_ref, ev)
#define	HCURR(h, ev)		(*(h)->h_curr)((h)->h_ref, ev)
#define	HSET(h, ev, n)		(*(h)->h_set)((h)->h_ref, ev, n)
#define	HCLEAR(h, ev)		(*(h)->h_clear)((h)->h_ref, ev)
#define	HENTER(h, ev, str)	(*(h)->h_enter)((h)->h_ref, ev, str)
#define	HADD(h, ev, str)	(*(h)->h_add)((h)->h_ref, ev, str)
#define	HDEL(h, ev, n)		(*(h)->h_del)((h)->h_ref, ev, n)

#define	h_strdup(a)	Strdup(a)
#define	h_malloc(a)	malloc(a)
#define	h_realloc(a, b)	realloc((a), (b))
#define	h_free(a)	free(a)

typedef struct {
    int		num;
    Char	*str;
} HistEventPrivate;



private int history_setsize(TYPE(History) *, TYPE(HistEvent) *, int);
private int history_getsize(TYPE(History) *, TYPE(HistEvent) *);
private int history_setunique(TYPE(History) *, TYPE(HistEvent) *, int);
private int history_getunique(TYPE(History) *, TYPE(HistEvent) *);
private int history_set_fun(TYPE(History) *, TYPE(History) *);
private int history_load(TYPE(History) *, const char *);
private int history_save(TYPE(History) *, const char *);
private int history_save_fp(TYPE(History) *, FILE *);
private int history_prev_event(TYPE(History) *, TYPE(HistEvent) *, int);
private int history_next_event(TYPE(History) *, TYPE(HistEvent) *, int);
private int history_next_string(TYPE(History) *, TYPE(HistEvent) *, const Char *);
private int history_prev_string(TYPE(History) *, TYPE(HistEvent) *, const Char *);


/***********************************************************************/

/*
 * Builtin- history implementation
 */
typedef struct hentry_t {
	TYPE(HistEvent) ev;		/* What we return		 */
	void *data;		/* data				 */
	struct hentry_t *next;	/* Next entry			 */
	struct hentry_t *prev;	/* Previous entry		 */
} hentry_t;

typedef struct history_t {
	hentry_t list;		/* Fake list header element	*/
	hentry_t *cursor;	/* Current element in the list	*/
	int max;		/* Maximum number of events	*/
	int cur;		/* Current number of events	*/
	int eventid;		/* For generation of unique event id	 */
	int flags;		/* TYPE(History) flags		*/
#define H_UNIQUE	1	/* Store only unique elements	*/
} history_t;

private int history_def_next(void *, TYPE(HistEvent) *);
private int history_def_first(void *, TYPE(HistEvent) *);
private int history_def_prev(void *, TYPE(HistEvent) *);
private int history_def_last(void *, TYPE(HistEvent) *);
private int history_def_curr(void *, TYPE(HistEvent) *);
private int history_def_set(void *, TYPE(HistEvent) *, const int);
private void history_def_clear(void *, TYPE(HistEvent) *);
private int history_def_enter(void *, TYPE(HistEvent) *, const Char *);
private int history_def_add(void *, TYPE(HistEvent) *, const Char *);
private int history_def_del(void *, TYPE(HistEvent) *, const int);

private int history_def_init(void **, TYPE(HistEvent) *, int);
private int history_def_insert(history_t *, TYPE(HistEvent) *, const Char *);
private void history_def_delete(history_t *, TYPE(HistEvent) *, hentry_t *);

private int history_deldata_nth(history_t *, TYPE(HistEvent) *, int, void **);
private int history_set_nth(void *, TYPE(HistEvent) *, int);

#define	history_def_setsize(p, num)(void) (((history_t *)p)->max = (num))
#define	history_def_getsize(p)  (((history_t *)p)->cur)
#define	history_def_getunique(p) (((((history_t *)p)->flags) & H_UNIQUE) != 0)
#define	history_def_setunique(p, uni) \
    if (uni) \
	(((history_t *)p)->flags) |= H_UNIQUE; \
    else \
	(((history_t *)p)->flags) &= ~H_UNIQUE

#define	he_strerror(code)	he_errlist[code]
#define	he_seterrev(evp, code)	{\
				    evp->num = code;\
				    evp->str = he_strerror(code);\
				}

/* error messages */
static const Char *const he_errlist[] = {
	STR("OK"),
	STR("unknown error"),
	STR("malloc() failed"),
	STR("first event not found"),
	STR("last event not found"),
	STR("empty list"),
	STR("no next event"),
	STR("no previous event"),
	STR("current event is invalid"),
	STR("event not found"),
	STR("can't read history from file"),
	STR("can't write history"),
	STR("required parameter(s) not supplied"),
	STR("history size negative"),
	STR("function not allowed with other history-functions-set the default"),
	STR("bad parameters")
};
/* error codes */
#define	_HE_OK                   0
#define	_HE_UNKNOWN		 1
#define	_HE_MALLOC_FAILED        2
#define	_HE_FIRST_NOTFOUND       3
#define	_HE_LAST_NOTFOUND        4
#define	_HE_EMPTY_LIST           5
#define	_HE_END_REACHED          6
#define	_HE_START_REACHED	 7
#define	_HE_CURR_INVALID	 8
#define	_HE_NOT_FOUND		 9
#define	_HE_HIST_READ		10
#define	_HE_HIST_WRITE		11
#define	_HE_PARAM_MISSING	12
#define	_HE_SIZE_NEGATIVE	13
#define	_HE_NOT_ALLOWED		14
#define	_HE_BAD_PARAM		15

/* history_def_first():
 *	Default function to return the first event in the history.
 */
private int
history_def_first(void *p, TYPE(HistEvent) *ev)
{
	history_t *h = (history_t *) p;

	h->cursor = h->list.next;
	if (h->cursor != &h->list)
		*ev = h->cursor->ev;
	else {
		he_seterrev(ev, _HE_FIRST_NOTFOUND);
		return -1;
	}

	return 0;
}


/* history_def_last():
 *	Default function to return the last event in the history.
 */
private int
history_def_last(void *p, TYPE(HistEvent) *ev)
{
	history_t *h = (history_t *) p;

	h->cursor = h->list.prev;
	if (h->cursor != &h->list)
		*ev = h->cursor->ev;
	else {
		he_seterrev(ev, _HE_LAST_NOTFOUND);
		return -1;
	}

	return 0;
}


/* history_def_next():
 *	Default function to return the next event in the history.
 */
private int
history_def_next(void *p, TYPE(HistEvent) *ev)
{
	history_t *h = (history_t *) p;

	if (h->cursor == &h->list) {
		he_seterrev(ev, _HE_EMPTY_LIST);
		return -1;
	}

	if (h->cursor->next == &h->list) {
		he_seterrev(ev, _HE_END_REACHED);
		return -1;
	}

        h->cursor = h->cursor->next;
        *ev = h->cursor->ev;

	return 0;
}


/* history_def_prev():
 *	Default function to return the previous event in the history.
 */
private int
history_def_prev(void *p, TYPE(HistEvent) *ev)
{
	history_t *h = (history_t *) p;

	if (h->cursor == &h->list) {
		he_seterrev(ev,
		    (h->cur > 0) ? _HE_END_REACHED : _HE_EMPTY_LIST);
		return -1;
	}

	if (h->cursor->prev == &h->list) {
		he_seterrev(ev, _HE_START_REACHED);
		return -1;
	}

        h->cursor = h->cursor->prev;
        *ev = h->cursor->ev;

	return 0;
}


/* history_def_curr():
 *	Default function to return the current event in the history.
 */
private int
history_def_curr(void *p, TYPE(HistEvent) *ev)
{
	history_t *h = (history_t *) p;

	if (h->cursor != &h->list)
		*ev = h->cursor->ev;
	else {
		he_seterrev(ev,
		    (h->cur > 0) ? _HE_CURR_INVALID : _HE_EMPTY_LIST);
		return -1;
	}

	return 0;
}


/* history_def_set():
 *	Default function to set the current event in the history to the
 *	given one.
 */
private int
history_def_set(void *p, TYPE(HistEvent) *ev, const int n)
{
	history_t *h = (history_t *) p;

	if (h->cur == 0) {
		he_seterrev(ev, _HE_EMPTY_LIST);
		return -1;
	}
	if (h->cursor == &h->list || h->cursor->ev.num != n) {
		for (h->cursor = h->list.next; h->cursor != &h->list;
		    h->cursor = h->cursor->next)
			if (h->cursor->ev.num == n)
				break;
	}
	if (h->cursor == &h->list) {
		he_seterrev(ev, _HE_NOT_FOUND);
		return -1;
	}
	return 0;
}


/* history_set_nth():
 *	Default function to set the current event in the history to the
 *	n-th one.
 */
private int
history_set_nth(void *p, TYPE(HistEvent) *ev, int n)
{
	history_t *h = (history_t *) p;

	if (h->cur == 0) {
		he_seterrev(ev, _HE_EMPTY_LIST);
		return -1;
	}
	for (h->cursor = h->list.prev; h->cursor != &h->list;
	    h->cursor = h->cursor->prev)
		if (n-- <= 0)
			break;
	if (h->cursor == &h->list) {
		he_seterrev(ev, _HE_NOT_FOUND);
		return -1;
	}
	return 0;
}


/* history_def_add():
 *	Append string to element
 */
private int
history_def_add(void *p, TYPE(HistEvent) *ev, const Char *str)
{
	history_t *h = (history_t *) p;
	size_t len;
	Char *s;
	HistEventPrivate *evp = (void *)&h->cursor->ev;

	if (h->cursor == &h->list)
		return history_def_enter(p, ev, str);
	len = Strlen(evp->str) + Strlen(str) + 1;
	s = h_malloc(len * sizeof(*s));
	if (s == NULL) {
		he_seterrev(ev, _HE_MALLOC_FAILED);
		return -1;
	}
	(void) Strncpy(s, h->cursor->ev.str, len);
        s[len - 1] = '\0';
	(void) Strncat(s, str, len - Strlen(s) - 1);
	h_free(evp->str);
	evp->str = s;
	*ev = h->cursor->ev;
	return 0;
}


private int
history_deldata_nth(history_t *h, TYPE(HistEvent) *ev,
    int num, void **data)
{
	if (history_set_nth(h, ev, num) != 0)
		return -1;
	/* magic value to skip delete (just set to n-th history) */
	if (data == (void **)-1)
		return 0;
	ev->str = Strdup(h->cursor->ev.str);
	ev->num = h->cursor->ev.num;
	if (data)
		*data = h->cursor->data;
	history_def_delete(h, ev, h->cursor);
	return 0;
}


/* history_def_del():
 *	Delete element hp of the h list
 */
/* ARGSUSED */
private int
history_def_del(void *p, TYPE(HistEvent) *ev __attribute__((__unused__)),
    const int num)
{
	history_t *h = (history_t *) p;
	if (history_def_set(h, ev, num) != 0)
		return -1;
	ev->str = Strdup(h->cursor->ev.str);
	ev->num = h->cursor->ev.num;
	history_def_delete(h, ev, h->cursor);
	return 0;
}


/* history_def_delete():
 *	Delete element hp of the h list
 */
/* ARGSUSED */
private void
history_def_delete(history_t *h,
		   TYPE(HistEvent) *ev __attribute__((__unused__)), hentry_t *hp)
{
	HistEventPrivate *evp = (void *)&hp->ev;
	if (hp == &h->list)
		abort();
	if (h->cursor == hp) {
		h->cursor = hp->prev;
		if (h->cursor == &h->list)
			h->cursor = hp->next;
	}
	hp->prev->next = hp->next;
	hp->next->prev = hp->prev;
	h_free(evp->str);
	h_free(hp);
	h->cur--;
}


/* history_def_insert():
 *	Insert element with string str in the h list
 */
private int
history_def_insert(history_t *h, TYPE(HistEvent) *ev, const Char *str)
{
	hentry_t *c;

	c = h_malloc(sizeof(*c));
	if (c == NULL)
		goto oomem;
	if ((c->ev.str = h_strdup(str)) == NULL) {
		h_free(c);
		goto oomem;
	}
	c->data = NULL;
	c->ev.num = ++h->eventid;
	c->next = h->list.next;
	c->prev = &h->list;
	h->list.next->prev = c;
	h->list.next = c;
	h->cur++;
	h->cursor = c;

	*ev = c->ev;
	return 0;
oomem:
	he_seterrev(ev, _HE_MALLOC_FAILED);
	return -1;
}


/* history_def_enter():
 *	Default function to enter an item in the history
 */
private int
history_def_enter(void *p, TYPE(HistEvent) *ev, const Char *str)
{
	history_t *h = (history_t *) p;

	if ((h->flags & H_UNIQUE) != 0 && h->list.next != &h->list &&
	    Strcmp(h->list.next->ev.str, str) == 0)
	    return 0;

	if (history_def_insert(h, ev, str) == -1)
		return -1;	/* error, keep error message */

	/*
         * Always keep at least one entry.
         * This way we don't have to check for the empty list.
         */
	while (h->cur > h->max && h->cur > 0)
		history_def_delete(h, ev, h->list.prev);

	return 1;
}


/* history_def_init():
 *	Default history initialization function
 */
/* ARGSUSED */
private int
history_def_init(void **p, TYPE(HistEvent) *ev __attribute__((__unused__)), int n)
{
	history_t *h = (history_t *) h_malloc(sizeof(*h));
	if (h == NULL)
		return -1;

	if (n <= 0)
		n = 0;
	h->eventid = 0;
	h->cur = 0;
	h->max = n;
	h->list.next = h->list.prev = &h->list;
	h->list.ev.str = NULL;
	h->list.ev.num = 0;
	h->cursor = &h->list;
	h->flags = 0;
	*p = h;
	return 0;
}


/* history_def_clear():
 *	Default history cleanup function
 */
private void
history_def_clear(void *p, TYPE(HistEvent) *ev)
{
	history_t *h = (history_t *) p;

	while (h->list.prev != &h->list)
		history_def_delete(h, ev, h->list.prev);
	h->cursor = &h->list;
	h->eventid = 0;
	h->cur = 0;
}




/************************************************************************/

/* history_init():
 *	Initialization function.
 */
public TYPE(History) *
FUN(history,init)(void)
{
	TYPE(HistEvent) ev;
	TYPE(History) *h = (TYPE(History) *) h_malloc(sizeof(*h));
	if (h == NULL)
		return NULL;

	if (history_def_init(&h->h_ref, &ev, 0) == -1) {
		h_free(h);
		return NULL;
	}
	h->h_ent = -1;
	h->h_next = history_def_next;
	h->h_first = history_def_first;
	h->h_last = history_def_last;
	h->h_prev = history_def_prev;
	h->h_curr = history_def_curr;
	h->h_set = history_def_set;
	h->h_clear = history_def_clear;
	h->h_enter = history_def_enter;
	h->h_add = history_def_add;
	h->h_del = history_def_del;

	return h;
}


/* history_end():
 *	clean up history;
 */
public void
FUN(history,end)(TYPE(History) *h)
{
	TYPE(HistEvent) ev;

	if (h->h_next == history_def_next)
		history_def_clear(h->h_ref, &ev);
	h_free(h->h_ref);
	h_free(h);
}



/* history_setsize():
 *	Set history number of events
 */
private int
history_setsize(TYPE(History) *h, TYPE(HistEvent) *ev, int num)
{

	if (h->h_next != history_def_next) {
		he_seterrev(ev, _HE_NOT_ALLOWED);
		return -1;
	}
	if (num < 0) {
		he_seterrev(ev, _HE_BAD_PARAM);
		return -1;
	}
	history_def_setsize(h->h_ref, num);
	return 0;
}


/* history_getsize():
 *      Get number of events currently in history
 */
private int
history_getsize(TYPE(History) *h, TYPE(HistEvent) *ev)
{
	if (h->h_next != history_def_next) {
		he_seterrev(ev, _HE_NOT_ALLOWED);
		return -1;
	}
	ev->num = history_def_getsize(h->h_ref);
	if (ev->num < -1) {
		he_seterrev(ev, _HE_SIZE_NEGATIVE);
		return -1;
	}
	return 0;
}


/* history_setunique():
 *	Set if adjacent equal events should not be entered in history.
 */
private int
history_setunique(TYPE(History) *h, TYPE(HistEvent) *ev, int uni)
{

	if (h->h_next != history_def_next) {
		he_seterrev(ev, _HE_NOT_ALLOWED);
		return -1;
	}
	history_def_setunique(h->h_ref, uni);
	return 0;
}


/* history_getunique():
 *	Get if adjacent equal events should not be entered in history.
 */
private int
history_getunique(TYPE(History) *h, TYPE(HistEvent) *ev)
{
	if (h->h_next != history_def_next) {
		he_seterrev(ev, _HE_NOT_ALLOWED);
		return -1;
	}
	ev->num = history_def_getunique(h->h_ref);
	return 0;
}


/* history_set_fun():
 *	Set history functions
 */
private int
history_set_fun(TYPE(History) *h, TYPE(History) *nh)
{
	TYPE(HistEvent) ev;

	if (nh->h_first == NULL || nh->h_next == NULL || nh->h_last == NULL ||
	    nh->h_prev == NULL || nh->h_curr == NULL || nh->h_set == NULL ||
	    nh->h_enter == NULL || nh->h_add == NULL || nh->h_clear == NULL ||
	    nh->h_del == NULL || nh->h_ref == NULL) {
		if (h->h_next != history_def_next) {
			if (history_def_init(&h->h_ref, &ev, 0) == -1)
				return -1;
			h->h_first = history_def_first;
			h->h_next = history_def_next;
			h->h_last = history_def_last;
			h->h_prev = history_def_prev;
			h->h_curr = history_def_curr;
			h->h_set = history_def_set;
			h->h_clear = history_def_clear;
			h->h_enter = history_def_enter;
			h->h_add = history_def_add;
			h->h_del = history_def_del;
		}
		return -1;
	}
	if (h->h_next == history_def_next)
		history_def_clear(h->h_ref, &ev);

	h->h_ent = -1;
	h->h_first = nh->h_first;
	h->h_next = nh->h_next;
	h->h_last = nh->h_last;
	h->h_prev = nh->h_prev;
	h->h_curr = nh->h_curr;
	h->h_set = nh->h_set;
	h->h_clear = nh->h_clear;
	h->h_enter = nh->h_enter;
	h->h_add = nh->h_add;
	h->h_del = nh->h_del;

	return 0;
}


/* history_load():
 *	TYPE(History) load function
 */
private int
history_load(TYPE(History) *h, const char *fname)
{
	FILE *fp;
	char *line;
	size_t llen;
	ssize_t sz;
	size_t max_size;
	char *ptr;
	int i = -1;
	TYPE(HistEvent) ev;
#ifdef WIDECHAR
	static ct_buffer_t conv;
#endif

	if ((fp = fopen(fname, "r")) == NULL)
		return i;

	line = NULL;
	llen = 0;
	if ((sz = getline(&line, &llen, fp)) == -1)
		goto done;

	if (strncmp(line, hist_cookie, (size_t)sz) != 0)
		goto done;

	ptr = h_malloc((max_size = 1024) * sizeof(*ptr));
	if (ptr == NULL)
		goto done;
	for (i = 0; (sz = getline(&line, &llen, fp)) != -1; i++) {
		if (sz > 0 && line[sz - 1] == '\n')
			line[--sz] = '\0';
		if (max_size < (size_t)sz) {
			char *nptr;
			max_size = ((size_t)sz + 1024) & (size_t)~1023;
			nptr = h_realloc(ptr, max_size * sizeof(*ptr));
			if (nptr == NULL) {
				i = -1;
				goto oomem;
			}
			ptr = nptr;
		}
		(void) strunvis(ptr, line);
		if (HENTER(h, &ev, ct_decode_string(ptr, &conv)) == -1) {
			i = -1;
			goto oomem;
		}
	}
oomem:
	h_free(ptr);
done:
	free(line);
	(void) fclose(fp);
	return i;
}


/* history_save_fp():
 *	TYPE(History) save function
 */
private int
history_save_fp(TYPE(History) *h, FILE *fp)
{
	TYPE(HistEvent) ev;
	int i = -1, retval;
	size_t len, max_size;
	char *ptr;
	const char *str;
#ifdef WIDECHAR
	static ct_buffer_t conv;
#endif

	if (fchmod(fileno(fp), S_IRUSR|S_IWUSR) == -1)
		goto done;
	if (fputs(hist_cookie, fp) == EOF)
		goto done;
	ptr = h_malloc((max_size = 1024) * sizeof(*ptr));
	if (ptr == NULL)
		goto done;
	for (i = 0, retval = HLAST(h, &ev);
	    retval != -1;
	    retval = HPREV(h, &ev), i++) {
		str = ct_encode_string(ev.str, &conv);
		len = strlen(str) * 4 + 1;
		if (len > max_size) {
			char *nptr;
			max_size = (len + 1024) & (size_t)~1023;
			nptr = h_realloc(ptr, max_size * sizeof(*ptr));
			if (nptr == NULL) {
				i = -1;
				goto oomem;
			}
			ptr = nptr;
		}
		(void) strvis(ptr, str, VIS_WHITE);
		(void) fprintf(fp, "%s\n", ptr);
	}
oomem:
	h_free(ptr);
done:
	return i;
}


/* history_save():
 *    History save function
 */
private int
history_save(TYPE(History) *h, const char *fname)
{
    FILE *fp;
    int i;

    if ((fp = fopen(fname, "w")) == NULL)
	return -1;

    i = history_save_fp(h, fp);

    (void) fclose(fp);
    return i;
}


/* history_prev_event():
 *	Find the previous event, with number given
 */
private int
history_prev_event(TYPE(History) *h, TYPE(HistEvent) *ev, int num)
{
	int retval;

	for (retval = HCURR(h, ev); retval != -1; retval = HPREV(h, ev))
		if (ev->num == num)
			return 0;

	he_seterrev(ev, _HE_NOT_FOUND);
	return -1;
}


private int
history_next_evdata(TYPE(History) *h, TYPE(HistEvent) *ev, int num, void **d)
{
	int retval;

	for (retval = HCURR(h, ev); retval != -1; retval = HPREV(h, ev))
		if (ev->num == num) {
			if (d)
				*d = ((history_t *)h->h_ref)->cursor->data;
			return 0;
		}

	he_seterrev(ev, _HE_NOT_FOUND);
	return -1;
}


/* history_next_event():
 *	Find the next event, with number given
 */
private int
history_next_event(TYPE(History) *h, TYPE(HistEvent) *ev, int num)
{
	int retval;

	for (retval = HCURR(h, ev); retval != -1; retval = HNEXT(h, ev))
		if (ev->num == num)
			return 0;

	he_seterrev(ev, _HE_NOT_FOUND);
	return -1;
}


/* history_prev_string():
 *	Find the previous event beginning with string
 */
private int
history_prev_string(TYPE(History) *h, TYPE(HistEvent) *ev, const Char *str)
{
	size_t len = Strlen(str);
	int retval;

	for (retval = HCURR(h, ev); retval != -1; retval = HNEXT(h, ev))
		if (Strncmp(str, ev->str, len) == 0)
			return 0;

	he_seterrev(ev, _HE_NOT_FOUND);
	return -1;
}


/* history_next_string():
 *	Find the next event beginning with string
 */
private int
history_next_string(TYPE(History) *h, TYPE(HistEvent) *ev, const Char *str)
{
	size_t len = Strlen(str);
	int retval;

	for (retval = HCURR(h, ev); retval != -1; retval = HPREV(h, ev))
		if (Strncmp(str, ev->str, len) == 0)
			return 0;

	he_seterrev(ev, _HE_NOT_FOUND);
	return -1;
}


/* history():
 *	User interface to history functions.
 */
int
FUNW(history)(TYPE(History) *h, TYPE(HistEvent) *ev, int fun, ...)
{
	va_list va;
	const Char *str;
	int retval;

	va_start(va, fun);

	he_seterrev(ev, _HE_OK);

	switch (fun) {
	case H_GETSIZE:
		retval = history_getsize(h, ev);
		break;

	case H_SETSIZE:
		retval = history_setsize(h, ev, va_arg(va, int));
		break;

	case H_GETUNIQUE:
		retval = history_getunique(h, ev);
		break;

	case H_SETUNIQUE:
		retval = history_setunique(h, ev, va_arg(va, int));
		break;

	case H_ADD:
		str = va_arg(va, const Char *);
		retval = HADD(h, ev, str);
		break;

	case H_DEL:
		retval = HDEL(h, ev, va_arg(va, const int));
		break;

	case H_ENTER:
		str = va_arg(va, const Char *);
		if ((retval = HENTER(h, ev, str)) != -1)
			h->h_ent = ev->num;
		break;

	case H_APPEND:
		str = va_arg(va, const Char *);
		if ((retval = HSET(h, ev, h->h_ent)) != -1)
			retval = HADD(h, ev, str);
		break;

	case H_FIRST:
		retval = HFIRST(h, ev);
		break;

	case H_NEXT:
		retval = HNEXT(h, ev);
		break;

	case H_LAST:
		retval = HLAST(h, ev);
		break;

	case H_PREV:
		retval = HPREV(h, ev);
		break;

	case H_CURR:
		retval = HCURR(h, ev);
		break;

	case H_SET:
		retval = HSET(h, ev, va_arg(va, const int));
		break;

	case H_CLEAR:
		HCLEAR(h, ev);
		retval = 0;
		break;

	case H_LOAD:
		retval = history_load(h, va_arg(va, const char *));
		if (retval == -1)
			he_seterrev(ev, _HE_HIST_READ);
		break;

	case H_SAVE:
		retval = history_save(h, va_arg(va, const char *));
		if (retval == -1)
			he_seterrev(ev, _HE_HIST_WRITE);
		break;

	case H_SAVE_FP:
		retval = history_save_fp(h, va_arg(va, FILE *));
		if (retval == -1)
		    he_seterrev(ev, _HE_HIST_WRITE);
		break;

	case H_PREV_EVENT:
		retval = history_prev_event(h, ev, va_arg(va, int));
		break;

	case H_NEXT_EVENT:
		retval = history_next_event(h, ev, va_arg(va, int));
		break;

	case H_PREV_STR:
		retval = history_prev_string(h, ev, va_arg(va, const Char *));
		break;

	case H_NEXT_STR:
		retval = history_next_string(h, ev, va_arg(va, const Char *));
		break;

	case H_FUNC:
	{
		TYPE(History) hf;

		hf.h_ref = va_arg(va, void *);
		h->h_ent = -1;
		hf.h_first = va_arg(va, history_gfun_t);
		hf.h_next = va_arg(va, history_gfun_t);
		hf.h_last = va_arg(va, history_gfun_t);
		hf.h_prev = va_arg(va, history_gfun_t);
		hf.h_curr = va_arg(va, history_gfun_t);
		hf.h_set = va_arg(va, history_sfun_t);
		hf.h_clear = va_arg(va, history_vfun_t);
		hf.h_enter = va_arg(va, history_efun_t);
		hf.h_add = va_arg(va, history_efun_t);
		hf.h_del = va_arg(va, history_sfun_t);

		if ((retval = history_set_fun(h, &hf)) == -1)
			he_seterrev(ev, _HE_PARAM_MISSING);
		break;
	}

	case H_END:
		FUN(history,end)(h);
		retval = 0;
		break;

	case H_NEXT_EVDATA:
	{
		int num = va_arg(va, int);
		void **d = va_arg(va, void **);
		retval = history_next_evdata(h, ev, num, d);
		break;
	}

	case H_DELDATA:
	{
		int num = va_arg(va, int);
		void **d = va_arg(va, void **);
		retval = history_deldata_nth((history_t *)h->h_ref, ev, num, d);
		break;
	}

	case H_REPLACE: /* only use after H_NEXT_EVDATA */
	{
		const Char *line = va_arg(va, const Char *);
		void *d = va_arg(va, void *);
		const Char *s;
		if(!line || !(s = Strdup(line))) {
			retval = -1;
			break;
		}
		((history_t *)h->h_ref)->cursor->ev.str = s;
		((history_t *)h->h_ref)->cursor->data = d;
		retval = 0;
		break;
	}

	default:
		retval = -1;
		he_seterrev(ev, _HE_UNKNOWN);
		break;
	}
	va_end(va);
	return retval;
}
