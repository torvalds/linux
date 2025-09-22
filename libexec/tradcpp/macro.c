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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "union.h"
#include "array.h"
#include "mode.h"
#include "place.h"
#include "macro.h"
#include "output.h"

struct expansionitem {
	enum { EI_STRING, EI_PARAM, EI_FILE, EI_LINE } itemtype;
	union {
		char *ei_string;		/* for EI_STRING */
		unsigned ei_param;		/* for EI_PARAM */
	} UN;
};
DECLARRAY(expansionitem, static UNUSED);
DEFARRAY(expansionitem, static);

#ifdef NEED_UNION_ACCESSORS
#define ei_string un.ei_string
#define ei_param un.ei_param
#endif


struct macro {
	struct place defplace;
	struct place expansionplace;
	unsigned hash;
	char *name;
	bool hasparams;
	struct stringarray params;
	struct expansionitemarray expansion;
	bool inuse;
};
DECLARRAY(macro, static UNUSED);
DEFARRAY(macro, static);
DECLARRAY(macroarray, static UNUSED);
DEFARRAY(macroarray, static);

static struct macroarrayarray macros;
static unsigned total_macros;
static unsigned hashmask;

////////////////////////////////////////////////////////////
// macro structure ops

static
struct expansionitem *
expansionitem_create_string(const char *string)
{
	struct expansionitem *ei;

	ei = domalloc(sizeof(*ei));
	ei->itemtype = EI_STRING;
	ei->ei_string = dostrdup(string);
	return ei;
}

static
struct expansionitem *
expansionitem_create_stringlen(const char *string, size_t len)
{
	struct expansionitem *ei;

	ei = domalloc(sizeof(*ei));
	ei->itemtype = EI_STRING;
	ei->ei_string = dostrndup(string, len);
	return ei;
}

static
struct expansionitem *
expansionitem_create_param(unsigned param)
{
	struct expansionitem *ei;

	ei = domalloc(sizeof(*ei));
	ei->itemtype = EI_PARAM;
	ei->ei_param = param;
	return ei;
}

static
struct expansionitem *
expansionitem_create_file(void)
{
	struct expansionitem *ei;

	ei = domalloc(sizeof(*ei));
	ei->itemtype = EI_FILE;
	return ei;
}

static
struct expansionitem *
expansionitem_create_line(void)
{
	struct expansionitem *ei;

	ei = domalloc(sizeof(*ei));
	ei->itemtype = EI_LINE;
	return ei;
}

static
void
expansionitem_destroy(struct expansionitem *ei)
{
	switch (ei->itemtype) {
	    case EI_STRING:
		dostrfree(ei->ei_string);
		break;
	    case EI_PARAM:
	    case EI_FILE:
	    case EI_LINE:
		break;
	}
	dofree(ei, sizeof(*ei));
}

static
bool
expansionitem_eq(const struct expansionitem *ei1,
		 const struct expansionitem *ei2)
{
	if (ei1->itemtype != ei2->itemtype) {
		return false;
	}
	switch (ei1->itemtype) {
	    case EI_STRING:
		if (strcmp(ei1->ei_string, ei2->ei_string) != 0) {
			return false;
		}
		break;
	    case EI_PARAM:
		if (ei1->ei_param != ei2->ei_param) {
			return false;
		}
		break;
	    case EI_FILE:
	    case EI_LINE:
		break;
	}
	return true;
}

static
struct macro *
macro_create(struct place *p1, const char *name, unsigned hash,
	     struct place *p2)
{
	struct macro *m;

	m = domalloc(sizeof(*m));
	m->defplace = *p1;
	m->expansionplace = *p2;
	m->hash = hash;
	m->name = dostrdup(name);
	m->hasparams = false;
	stringarray_init(&m->params);
	expansionitemarray_init(&m->expansion);
	m->inuse = false;
	return m;
}

DESTROYALL_ARRAY(expansionitem, );

static
void
macro_destroy(struct macro *m)
{
	expansionitemarray_destroyall(&m->expansion);
	expansionitemarray_cleanup(&m->expansion);
	dostrfree(m->name);
	dofree(m, sizeof(*m));
}

static
bool
macro_eq(const struct macro *m1, const struct macro *m2)
{
	unsigned num1, num2, i;
	struct expansionitem *ei1, *ei2;
	const char *p1, *p2;

	if (strcmp(m1->name, m2->name) != 0) {
		return false;
	}

	if (m1->hasparams != m2->hasparams) {
		return false;
	}

	num1 = expansionitemarray_num(&m1->expansion);
	num2 = expansionitemarray_num(&m2->expansion);
	if (num1 != num2) {
		return false;
	}

	for (i=0; i<num1; i++) {
		ei1 = expansionitemarray_get(&m1->expansion, i);
		ei2 = expansionitemarray_get(&m2->expansion, i);
		if (!expansionitem_eq(ei1, ei2)) {
			return false;
		}
	}

	num1 = stringarray_num(&m1->params);
	num2 = stringarray_num(&m2->params);
	if (num1 != num2) {
		return false;
	}

	for (i=0; i<num1; i++) {
		p1 = stringarray_get(&m1->params, i);
		p2 = stringarray_get(&m2->params, i);
		if (strcmp(p1, p2) != 0) {
			return false;
		}
	}
	return true;
}

////////////////////////////////////////////////////////////
// macro table

/*
 * Unless I've screwed up, this is something called Fletcher's Checksum
 * that showed up in Dr. Dobbs in, according to my notes, May 1992. The
 * implementation is new.
 */
static
unsigned
hashfunc(const char *s, size_t len)
{
	uint16_t x1, x2, a;
	size_t i;

	x1 = (uint16_t) (len >> 16);
	x2 = (uint16_t) (len);
	if (x1==0) {
		x1++;
	}
	if (x2==0) {
		x2++;
	}

	for (i=0; i<len; i+=2) {
		if (i==len-1) {
			a = (unsigned char)s[i];
			/* don't run off the end of the array */
		}
		else {
			a = (unsigned char)s[i] +
				((uint16_t)(unsigned char)s[i+1] << 8);
		}
		x1 += a;
		if (x1 < a) {
			x1++;
		}
		x2 += x1;
		if (x2 < x1) {
			x2++;
		}
	}

	x1 ^= 0xffff;
	x2 ^= 0xffff;
	return ((uint32_t)x2)*65535U + x1;
}

static
void
macrotable_init(void)
{
	unsigned i;

	macroarrayarray_init(&macros);
	macroarrayarray_setsize(&macros, 4);
	for (i=0; i<4; i++) {
		macroarrayarray_set(&macros, i, NULL);
	}
	total_macros = 0;
	hashmask = 0x3;
}

DESTROYALL_ARRAY(macro, );

static
void
macrotable_cleanup(void)
{
	struct macroarray *bucket;
	unsigned numbuckets, i;

	numbuckets = macroarrayarray_num(&macros);
	for (i=0; i<numbuckets; i++) {
		bucket = macroarrayarray_get(&macros, i);
		if (bucket != NULL) {
			macroarray_destroyall(bucket);
			macroarray_destroy(bucket);
		}
	}
	macroarrayarray_setsize(&macros, 0);
	macroarrayarray_cleanup(&macros);
}

static
struct macro *
macrotable_findlen(const char *name, size_t len, bool remove_it)
{
	unsigned hash;
	struct macroarray *bucket;
	struct macro *m, *m2;
	unsigned i, num;
	size_t mlen;

	hash = hashfunc(name, len);
	bucket = macroarrayarray_get(&macros, hash & hashmask);
	if (bucket == NULL) {
		return NULL;
	}
	num = macroarray_num(bucket);
	for (i=0; i<num; i++) {
		m = macroarray_get(bucket, i);
		if (hash != m->hash) {
			continue;
		}
		mlen = strlen(m->name);
		if (len == mlen && !memcmp(name, m->name, len)) {
			if (remove_it) {
				if (i < num-1) {
					m2 = macroarray_get(bucket, num-1);
					macroarray_set(bucket, i, m2);
				}
				macroarray_setsize(bucket, num-1);
				total_macros--;
			}
			return m;
		}
	}
	return NULL;
}

static
struct macro *
macrotable_find(const char *name, bool remove_it)
{
	return macrotable_findlen(name, strlen(name), remove_it);
}

static
void
macrotable_rehash(void)
{
	struct macroarray *newbucket, *oldbucket;
	struct macro *m;
	unsigned newmask, tossbit;
	unsigned numbuckets, i;
	unsigned oldnum, j, k;

	numbuckets = macroarrayarray_num(&macros);
	macroarrayarray_setsize(&macros, numbuckets*2);

	assert(hashmask == numbuckets - 1);
	newmask = (hashmask << 1) | 1U;
	tossbit = newmask & ~hashmask;
	hashmask = newmask;

	for (i=0; i<numbuckets; i++) {
		newbucket = NULL;
		oldbucket = macroarrayarray_get(&macros, i);
		if (oldbucket == NULL) {
			macroarrayarray_set(&macros, numbuckets + i, NULL);
			continue;
		}
		oldnum = macroarray_num(oldbucket);
		for (j=0; j<oldnum; j++) {
			m = macroarray_get(oldbucket, j);
			if (m->hash & tossbit) {
				if (newbucket == NULL) {
					newbucket = macroarray_create();
				}
				macroarray_set(oldbucket, j, NULL);
				macroarray_add(newbucket, m, NULL);
			}
		}
		for (j=k=0; j<oldnum; j++) {
			m = macroarray_get(oldbucket, j);
			if (m != NULL) {
				if (k < j) {
					macroarray_set(oldbucket, k, m);
				}
				k++;
			}
		}
		macroarray_setsize(oldbucket, k);
		macroarrayarray_set(&macros, numbuckets + i, newbucket);
	}
}

static
void
macrotable_add(struct macro *m)
{
	unsigned hash;
	struct macroarray *bucket;
	unsigned numbuckets;

	numbuckets = macroarrayarray_num(&macros);
	if (total_macros > 0 && total_macros / numbuckets > 9) {
		macrotable_rehash();
	}

	hash = hashfunc(m->name, strlen(m->name));
	bucket = macroarrayarray_get(&macros, hash & hashmask);
	if (bucket == NULL) {
		bucket = macroarray_create();
		macroarrayarray_set(&macros, hash & hashmask, bucket);
	}
	macroarray_add(bucket, m, NULL);
	total_macros++;
}

////////////////////////////////////////////////////////////
// external macro definition interface

static
struct macro *
macro_define_common_start(struct place *p1, const char *macro,
			  struct place *p2)
{
	struct macro *m;
	unsigned hash;

	if (!is_identifier(macro)) {
		complain(p1, "Invalid macro name %s", macro);
		complain_fail();
	}

	hash = hashfunc(macro, strlen(macro));
	m = macro_create(p1, macro, hash, p2);
	return m;
}

static
void
macro_define_common_end(struct macro *m)
{
	struct macro *oldm;
	bool ok;

	oldm = macrotable_find(m->name, false);
	if (oldm != NULL) {
		ok = macro_eq(m, oldm);
		if (ok) {
			/* in traditional cpp this is silent */
			//complain(&m->defplace,
			//	 "Warning: redefinition of %s", m->name);
			//complain(&oldm->defplace,
			//	 "Previous definition was here");
			//if (mode.werror) {
			//	complain_fail();
			//}
		} else {
			complain(&m->defplace,
				 "Warning: non-identical redefinition of %s",
				 m->name);
			complain(&oldm->defplace,
				 "Previous definition was here");
			/* in traditional cpp this is not fatal */
			if (mode.werror) {
				complain_fail();
			}
		}
		macro_destroy(m);
		return;
	}
	macrotable_add(m);
}

static
void
macro_parse_parameters(struct macro *m, struct place *p, const char *params)
{
	size_t len;
	const char *s;
	char *param;

	while (params != NULL) {
		len = strspn(params, ws);
		params += len;
		place_addcolumns(p, len);
		s = strchr(params, ',');
		if (s) {
			len = s-params;
			param = dostrndup(params, len);
			s++;
		} else {
			len = strlen(params);
			param = dostrndup(params, len);
		}
		notrailingws(param, strlen(param));
		if (!is_identifier(param)) {
			complain(p, "Invalid macro parameter name %s", param);
			complain_fail();
		} else {
			stringarray_add(&m->params, param, NULL);
		}
		params = s;
		place_addcolumns(p, len);
	}
}

static
bool
isparam(struct macro *m, const char *name, size_t len, unsigned *num_ret)
{
	unsigned num, i;
	const char *param;

	num = stringarray_num(&m->params);
	for (i=0; i<num; i++) {
		param = stringarray_get(&m->params, i);
		if (strlen(param) == len && !memcmp(name, param, len)) {
			*num_ret = i;
			return true;
		}
	}
	return false;
}

static
void
macro_parse_expansion(struct macro *m, const char *buf)
{
	size_t blockstart, wordstart, pos;
	struct expansionitem *ei;
	unsigned param;

	pos = blockstart = 0;
	while (buf[pos] != '\0') {
		pos += strspn(buf+pos, ws);
		if (strchr(alnum, buf[pos])) {
			wordstart = pos;
			pos += strspn(buf+pos, alnum);
			if (isparam(m, buf+wordstart, pos-wordstart, &param)) {
				if (wordstart > blockstart) {
					ei = expansionitem_create_stringlen(
						buf + blockstart,
						wordstart - blockstart);
					expansionitemarray_add(&m->expansion,
							       ei, NULL);
				}
				ei = expansionitem_create_param(param);
				expansionitemarray_add(&m->expansion, ei,NULL);
				blockstart = pos;
				continue;
			}
			continue;
		}
		pos++;
	}
	if (pos > blockstart) {
		ei = expansionitem_create_stringlen(buf + blockstart,
						    pos - blockstart);
		expansionitemarray_add(&m->expansion, ei, NULL);
	}
}

void
macro_define_plain(struct place *p1, const char *macro,
		   struct place *p2, const char *expansion)
{
	struct macro *m;
	struct expansionitem *ei;

	m = macro_define_common_start(p1, macro, p2);
	ei = expansionitem_create_string(expansion);
	expansionitemarray_add(&m->expansion, ei, NULL);
	macro_define_common_end(m);
}

void
macro_define_params(struct place *p1, const char *macro,
		    struct place *p2, const char *params,
		    struct place *p3, const char *expansion)
{
	struct macro *m;

	m = macro_define_common_start(p1, macro, p3);
	m->hasparams = true;
	macro_parse_parameters(m, p2, params);
	macro_parse_expansion(m, expansion);
	macro_define_common_end(m);
}

void
macro_define_magic(struct place *p, const char *macro)
{
	struct macro *m;
	struct expansionitem *ei;

	m = macro_define_common_start(p, macro, p);
	if (!strcmp(macro, "__FILE__")) {
		ei = expansionitem_create_file();
	}
	else {
		assert(!strcmp(macro, "__LINE__"));
		ei = expansionitem_create_line();
	}
	expansionitemarray_add(&m->expansion, ei, NULL);
	macro_define_common_end(m);
}

void
macro_undef(const char *macro)
{
	struct macro *m;

	m = macrotable_find(macro, true);
	if (m) {
		macro_destroy(m);
	}
}

bool
macro_isdefined(const char *macro)
{
	struct macro *m;

	m = macrotable_find(macro, false);
	return m != NULL;
}

////////////////////////////////////////////////////////////
// macro expansion

struct expstate {
	bool honordefined;
	enum { ES_NORMAL, ES_WANTLPAREN, ES_NOARG, ES_HAVEARG } state;
	struct macro *curmacro;
	struct stringarray args;
	unsigned argparens;

	bool tobuf;
	char *buf;
	size_t bufpos, bufmax;
};

static struct expstate mainstate;

static void doexpand(struct expstate *es, struct place *p,
		     const char *buf, size_t len);

static
void
expstate_init(struct expstate *es, bool tobuf, bool honordefined)
{
	es->honordefined = honordefined;
	es->state = ES_NORMAL;
	es->curmacro = NULL;
	stringarray_init(&es->args);
	es->argparens = 0;
	es->tobuf = tobuf;
	es->buf = NULL;
	es->bufpos = 0;
	es->bufmax = 0;
}

static
void
expstate_cleanup(struct expstate *es)
{
	assert(es->state == ES_NORMAL);
	stringarray_cleanup(&es->args);
	if (es->buf) {
		dofree(es->buf, es->bufmax);
	}
}

static
void
expstate_destroyargs(struct expstate *es)
{
	unsigned i, num;

	num = stringarray_num(&es->args);
	for (i=0; i<num; i++) {
		dostrfree(stringarray_get(&es->args, i));
	}
	stringarray_setsize(&es->args, 0);
}

static
void
expand_send(struct expstate *es, struct place *p, const char *buf, size_t len)
{
	size_t oldmax;

	if (es->tobuf) {
		assert(es->bufpos <= es->bufmax);
		if (es->bufpos + len > es->bufmax) {
			oldmax = es->bufmax;
			if (es->bufmax == 0) {
				es->bufmax = 64;
			}
			while (es->bufpos + len > es->bufmax) {
				es->bufmax *= 2;
			}
			es->buf = dorealloc(es->buf, oldmax, es->bufmax);
		}
		memcpy(es->buf + es->bufpos, buf, len);
		es->bufpos += len;
		assert(es->bufpos <= es->bufmax);
	} else {
		output(p, buf, len);
	}
}

static
void
expand_send_eof(struct expstate *es, struct place *p)
{
	if (es->tobuf) {
		expand_send(es, p, "", 1);
		es->bufpos--;
	} else {
		output_eof();
	}
}

static
void
expand_newarg(struct expstate *es, const char *buf, size_t len)
{
	char *text;

	text = dostrndup(buf, len);
	stringarray_add(&es->args, text, NULL);
}

static
void
expand_appendarg(struct expstate *es, const char *buf, size_t len)
{
	unsigned num;
	char *text;
	size_t oldlen;

	num = stringarray_num(&es->args);
	assert(num > 0);

	text = stringarray_get(&es->args, num - 1);
	oldlen = strlen(text);
	text = dorealloc(text, oldlen + 1, oldlen + len + 1);
	memcpy(text + oldlen, buf, len);
	text[oldlen+len] = '\0';
	stringarray_set(&es->args, num - 1, text);
}

static
char *
expand_substitute(struct place *p, struct expstate *es)
{
	struct expansionitem *ei;
	unsigned i, num;
	size_t len;
	char *arg;
	char *ret;
	unsigned numargs, numparams;
	char numbuf[64];

	numargs = stringarray_num(&es->args);
	numparams = stringarray_num(&es->curmacro->params);

	if (numargs == 0 && numparams == 1) {
		/* no arguments <=> one empty argument */
		stringarray_add(&es->args, dostrdup(""), NULL);
		numargs++;
	}
	if (numargs != numparams) {
		complain(p, "Wrong number of arguments for macro %s; "
			 "found %u, expected %u",
			 es->curmacro->name, numargs, numparams);
		complain_fail();
		while (numargs < numparams) {
			stringarray_add(&es->args, dostrdup(""), NULL);
			numargs++;
		}
	}

	len = 0;
	num = expansionitemarray_num(&es->curmacro->expansion);
	for (i=0; i<num; i++) {
		ei = expansionitemarray_get(&es->curmacro->expansion, i);
		switch (ei->itemtype) {
		    case EI_STRING:
			len += strlen(ei->ei_string);
			break;
		    case EI_PARAM:
			arg = stringarray_get(&es->args, ei->ei_param);
			len += strlen(arg);
			break;
		    case EI_FILE:
			len += strlen(place_getname(p)) + 2;
			break;
		    case EI_LINE:
			len += snprintf(numbuf, sizeof(numbuf), "%u", p->line);
			break;
		}
	}

	ret = domalloc(len+1);
	*ret = '\0';
	for (i=0; i<num; i++) {
		ei = expansionitemarray_get(&es->curmacro->expansion, i);
		switch (ei->itemtype) {
		    case EI_STRING:
			strlcat(ret, ei->ei_string, len + 1);
			break;
		    case EI_PARAM:
			arg = stringarray_get(&es->args, ei->ei_param);
			strlcat(ret, arg, len + 1);
			break;
		    case EI_FILE:
			strlcat(ret, "\"", len + 1);
			strlcat(ret, place_getname(p), len + 1);
			strlcat(ret, "\"", len + 1);
			break;
		    case EI_LINE:
			snprintf(numbuf, sizeof(numbuf), "%u", p->line);
			strlcat(ret, numbuf, len + 1);
			break;
		}
	}

	return ret;
}

static
void
expand_domacro(struct expstate *es, struct place *p)
{
	struct macro *m;
	const char *name, *val;
	char *newbuf, *newbuf2;

	if (es->curmacro == NULL) {
		/* defined() */
		if (stringarray_num(&es->args) != 1) {
			complain(p, "Too many arguments for defined()");
			complain_fail();
			expand_send(es, p, "0", 1);
			return;
		}
		name = stringarray_get(&es->args, 0);
		m = macrotable_find(name, false);
		val = (m != NULL) ? "1" : "0";
		debuglog(p, "defined(%s): %s", name, val); 
		expand_send(es, p, val, 1);
		expstate_destroyargs(es);
		return;
	}

	m = es->curmacro;
	assert(m->inuse == false);
	m->inuse = true;

	debuglog(p, "Expanding macro %s", m->name);
	newbuf = expand_substitute(p, es);
	debuglog(p, "Substituting for %s: %s", m->name, newbuf);

	newbuf2 = macroexpand(p, newbuf, strlen(newbuf), false);
	dostrfree(newbuf);
	expstate_destroyargs(es);
	debuglog(p, "Complete expansion for %s: %s", m->name, newbuf2);

	doexpand(es, p, newbuf2, strlen(newbuf2));
	dostrfree(newbuf2);

	m->inuse = false;
}

/*
 * The traditional behavior if a function-like macro appears without
 * arguments is to pretend it isn't a macro; that is, just emit its
 * name.
 */
static
void
expand_missingargs(struct expstate *es, struct place *p, bool needspace)
{
	if (es->curmacro == NULL) {
		/* defined */
		expand_send(es, p, "defined", 7);
		return;
	}
	expand_send(es, p, es->curmacro->name, strlen(es->curmacro->name));
	/* send a space in case we ate whitespace after the macro name */
	if (needspace) {
		expand_send(es, p, " ", 1);
	}
}

static
void
expand_got_ws(struct expstate *es, struct place *p,
	      const char *buf, size_t len)
{
	switch (es->state) {
	    case ES_NORMAL:
		expand_send(es, p, buf, len);
		break;
	    case ES_WANTLPAREN:
		/* XXX notyet */
		//expand_send(es, p, buf, len);
		break;
	    case ES_NOARG:
		expand_newarg(es, buf, len);
		es->state = ES_HAVEARG;
		break;
	    case ES_HAVEARG:
		expand_appendarg(es, buf, len);
		break;
	}
}

static
void
expand_got_word(struct expstate *es, struct place *p,
		const char *buf, size_t len)
{
	struct macro *m;

	switch (es->state) {
	    case ES_NORMAL:
		if (es->honordefined &&
		    len == 7 && !memcmp(buf, "defined", 7)) {
			es->curmacro = NULL;
			es->state = ES_WANTLPAREN;
			break;
		}
		m = macrotable_findlen(buf, len, false);
		if (m == NULL || m->inuse) {
			expand_send(es, p, buf, len);
		} else if (!m->hasparams) {
			es->curmacro = m;
			expand_domacro(es, p);
		} else {
			es->curmacro = m;
			es->state = ES_WANTLPAREN;
		}
		break;
	    case ES_WANTLPAREN:
		if (es->curmacro != NULL) {
			expand_missingargs(es, p, true);
			es->state = ES_NORMAL;
			/* try again */
			expand_got_word(es, p, buf, len);
		} else {
			/* "defined foo" means "defined(foo)" */
			expand_newarg(es, buf, len);
			es->state = ES_NORMAL;
			expand_domacro(es, p);
		}
		break;
	    case ES_NOARG:
		expand_newarg(es, buf, len);
		es->state = ES_HAVEARG;
		break;
	    case ES_HAVEARG:
		expand_appendarg(es, buf, len);
		break;
	}
}

static
void
expand_got_lparen(struct expstate *es, struct place *p,
		  const char *buf, size_t len)
{
	switch (es->state) {
	    case ES_NORMAL:
		expand_send(es, p, buf, len);
		break;
	    case ES_WANTLPAREN:
		es->state = ES_NOARG;
		break;
	    case ES_NOARG:
		expand_newarg(es, buf, len);
		es->state = ES_HAVEARG;
		es->argparens++;
		break;
	    case ES_HAVEARG:
		expand_appendarg(es, buf, len);
		es->argparens++;
		break;
	}
}

static
void
expand_got_rparen(struct expstate *es, struct place *p,
		  const char *buf, size_t len)
{
	switch (es->state) {
	    case ES_NORMAL:
		expand_send(es, p, buf, len);
		break;
	    case ES_WANTLPAREN:
		expand_missingargs(es, p, false);
		es->state = ES_NORMAL;
		/* try again */
		expand_got_rparen(es, p, buf, len);
		break;
	    case ES_NOARG:
		assert(es->argparens == 0);
		if (stringarray_num(&es->args) > 0) {
			/* we are after a comma; enter an empty argument */
			expand_newarg(es, buf, 0);
		}
		es->state = ES_NORMAL;
		expand_domacro(es, p);
		break;
	    case ES_HAVEARG:
		if (es->argparens > 0) {
			es->argparens--;
			expand_appendarg(es, buf, len);
		} else {
			es->state = ES_NORMAL;
			expand_domacro(es, p);
		}
		break;
	}
}

static
void
expand_got_comma(struct expstate *es, struct place *p,
		 const char *buf, size_t len)
{
	switch (es->state) {
	    case ES_NORMAL:
		expand_send(es, p, buf, len);
		break;
	    case ES_WANTLPAREN:
		expand_missingargs(es, p, false);
		es->state = ES_NORMAL;
		/* try again */
		expand_got_comma(es, p, buf, len);
		break;
	    case ES_NOARG:
		assert(es->argparens == 0);
		expand_newarg(es, buf, 0);
		break;
	    case ES_HAVEARG:
		if (es->argparens > 0) {
			expand_appendarg(es, buf, len);
		} else {
			es->state = ES_NOARG;
		}
		break;
	}
}

static
void
expand_got_other(struct expstate *es, struct place *p,
		 const char *buf, size_t len)
{
	switch (es->state) {
	    case ES_NORMAL:
		expand_send(es, p, buf, len);
		break;
	    case ES_WANTLPAREN:
		expand_missingargs(es, p, false);
		es->state = ES_NORMAL;
		/* try again */
		expand_got_other(es, p, buf, len);
		break;
	    case ES_NOARG:
		expand_newarg(es, buf, len);
		es->state = ES_HAVEARG;
		break;
	    case ES_HAVEARG:
		expand_appendarg(es, buf, len);
		break;
	}
}

static
void
expand_got_eof(struct expstate *es, struct place *p)
{
	switch (es->state) {
	    case ES_NORMAL:
		break;
	    case ES_WANTLPAREN:
		expand_missingargs(es, p, false);
		break;
	    case ES_NOARG:
	    case ES_HAVEARG:
		if (es->curmacro) {
			complain(p, "Unclosed argument list for macro %s",
				 es->curmacro->name);
		} else {
			complain(p, "Unclosed argument list for defined()");
		}
		complain_fail();
		expstate_destroyargs(es);
		break;
	}
	expand_send_eof(es, p);
	es->state = ES_NORMAL;
	es->curmacro = NULL;
	es->argparens = 0;
}

static
void
doexpand(struct expstate *es, struct place *p, const char *buf, size_t len)
{
	char *s;
	size_t x;
	bool inquote = false;
	char quote = '\0';

	while (len > 0) {
		x = strspn(buf, ws);
		if (x > len) {
			/* XXX gross, need strnspn */
			x = len;
		}

		if (x > 0) {
			expand_got_ws(es, p, buf, x);
			buf += x;
			len -= x;
			continue;
		}

		x = strspn(buf, alnum);
		if (x > len) {
			/* XXX gross, need strnspn */
			x = len;
		}

		if (!inquote && x > 0) {
			expand_got_word(es, p, buf, x);
			buf += x;
			len -= x;
			continue;
		}

		if (!inquote && len > 1 && buf[0] == '/' && buf[1] == '*') {
			s = strstr(buf, "*/");
			if (s) {
				x = s - buf;
			} else {
				x = len;
			}
			expand_got_ws(es, p, buf, x);
			buf += x;
			len -= x;
			continue;
		}

		if (!inquote && buf[0] == '(') {
			expand_got_lparen(es, p, buf, 1);
			buf++;
			len--;
			continue;
		}

		if (!inquote && buf[0] == ')') {
			expand_got_rparen(es, p, buf, 1);
			buf++;
			len--;
			continue;
		}

		if (!inquote && buf[0] == ',') {
			expand_got_comma(es, p, buf, 1);
			buf++;
			len--;
			continue;
		}

		if (len > 1 && buf[0] == '\\' &&
		    (buf[1] == '"' || buf[1] == '\'')) {
			expand_got_other(es, p, buf, 2);
			buf += 2;
			len -= 2;
			continue;
		}
		if (!inquote && (buf[0] == '"' || buf[0] == '\'')) {
			inquote = true;
			quote = buf[0];
		} else if (inquote && buf[0] == quote) {
			inquote = false;
		}

		expand_got_other(es, p, buf, 1);
		buf++;
		len--;
	}
}

char *
macroexpand(struct place *p, const char *buf, size_t len, bool honordefined)
{
	struct expstate es;
	char *ret;

	expstate_init(&es, true, honordefined);
	doexpand(&es, p, buf, len);
	expand_got_eof(&es, p);

	/* trim to fit, so the malloc debugging won't complain */
	es.buf = dorealloc(es.buf, es.bufmax, strlen(es.buf) + 1);

	ret = es.buf;
	es.buf = NULL;
	es.bufpos = es.bufmax = 0;

	expstate_cleanup(&es);

	return ret;
}

void
macro_sendline(struct place *p, const char *buf, size_t len)
{
	doexpand(&mainstate, p, buf, len);
	switch (mainstate.state) {
	    case ES_NORMAL:
		/*
		 * If we were sent a blank line, don't emit a newline
		 * for it. This matches the prior behavior of tradcpp.
		 */
		if (len > 0) {
			output(p, "\n", 1);
		}
		break;
	    case ES_WANTLPAREN:
	    case ES_NOARG:
	    case ES_HAVEARG:
		/*
		 * Apparently to match gcc's -traditional behavior we
		 * need to emit a space for each newline that appears
		 * while processing macro args.
		 */
		expand_got_ws(&mainstate, p, " ", 1);
		break;
	}
}

void
macro_sendeof(struct place *p)
{
	expand_got_eof(&mainstate, p);
}

////////////////////////////////////////////////////////////
// module initialization

void
macros_init(void)
{
	macrotable_init();
	expstate_init(&mainstate, false, false);
}

void
macros_cleanup(void)
{
	expstate_cleanup(&mainstate);
	macrotable_cleanup();
}
