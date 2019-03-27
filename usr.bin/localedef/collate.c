/*-
 * Copyright 2018 Nexenta Systems, Inc.
 * Copyright 2015 John Marino <draco@marino.st>
 *
 * This source code is derived from the illumos localedef command, and
 * provided under BSD-style license terms by Nexenta Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * LC_COLLATE database generation routines for localedef.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/tree.h>

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <limits.h>
#include "localedef.h"
#include "parser.h"
#include "collate.h"

/*
 * Design notes.
 *
 * It will be extremely helpful to the reader if they have access to
 * the localedef and locale file format specifications available.
 * Latest versions of these are available from www.opengroup.org.
 *
 * The design for the collation code is a bit complex.  The goal is a
 * single collation database as described in collate.h (in
 * libc/port/locale).  However, there are some other tidbits:
 *
 * a) The substitution entries are now a directly indexable array.  A
 * priority elsewhere in the table is taken as an index into the
 * substitution table if it has a high bit (COLLATE_SUBST_PRIORITY)
 * set.  (The bit is cleared and the result is the index into the
 * table.
 *
 * b) We eliminate duplicate entries into the substitution table.
 * This saves a lot of space.
 *
 * c) The priorities for each level are "compressed", so that each
 * sorting level has consecutively numbered priorities starting at 1.
 * (O is reserved for the ignore priority.)  This means sort levels
 * which only have a few distinct priorities can represent the
 * priority level in fewer bits, which makes the strxfrm output
 * smaller.
 *
 * d) We record the total number of priorities so that strxfrm can
 * figure out how many bytes to expand a numeric priority into.
 *
 * e) For the UNDEFINED pass (the last pass), we record the maximum
 * number of bits needed to uniquely prioritize these entries, so that
 * the last pass can also use smaller strxfrm output when possible.
 *
 * f) Priorities with the sign bit set are verboten.  This works out
 * because no active character set needs that bit to carry significant
 * information once the character is in wide form.
 *
 * To process the entire data to make the database, we actually run
 * multiple passes over the data.
 *
 * The first pass, which is done at parse time, identifies elements,
 * substitutions, and such, and records them in priority order.  As
 * some priorities can refer to other priorities, using forward
 * references, we use a table of references indicating whether the
 * priority's value has been resolved, or whether it is still a
 * reference.
 *
 * The second pass walks over all the items in priority order, noting
 * that they are used directly, and not just an indirect reference.
 * This is done by creating a "weight" structure for the item.  The
 * weights are stashed in an RB tree sorted by relative "priority".
 *
 * The third pass walks over all the weight structures, in priority
 * order, and assigns a new monotonically increasing (per sort level)
 * weight value to them.  These are the values that will actually be
 * written to the file.
 *
 * The fourth pass just writes the data out.
 */

/*
 * In order to resolve the priorities, we create a table of priorities.
 * Entries in the table can be in one of three states.
 *
 * UNKNOWN is for newly allocated entries, and indicates that nothing
 * is known about the priority.  (For example, when new entries are created
 * for collating-symbols, this is the value assigned for them until the
 * collating symbol's order has been determined.
 *
 * RESOLVED is used for an entry where the priority indicates the final
 * numeric weight.
 *
 * REFER is used for entries that reference other entries.  Typically
 * this is used for forward references.  A collating-symbol can never
 * have this value.
 *
 * The "pass" field is used during final resolution to aid in detection
 * of referencing loops.  (For example <A> depends on <B>, but <B> has its
 * priority dependent on <A>.)
 */
typedef enum {
	UNKNOWN,	/* priority is totally unknown */
	RESOLVED,	/* priority value fully resolved */
	REFER		/* priority is a reference (index) */
} res_t;

typedef struct weight {
	int32_t		pri;
	int		opt;
	RB_ENTRY(weight) entry;
} weight_t;

typedef struct priority {
	res_t		res;
	int32_t		pri;
	int		pass;
	int		lineno;
} collpri_t;

#define	NUM_WT	collinfo.directive_count

/*
 * These are the abstract collating symbols, which are just a symbolic
 * way to reference a priority.
 */
struct collsym {
	char		*name;
	int32_t		ref;
	RB_ENTRY(collsym) entry;
};

/*
 * These are also abstract collating symbols, but we allow them to have
 * different priorities at different levels.
 */
typedef struct collundef {
	char		*name;
	int32_t		ref[COLL_WEIGHTS_MAX];
	RB_ENTRY(collundef) entry;
} collundef_t;

/*
 * These are called "chains" in libc.  This records the fact that two
 * more characters should be treated as a single collating entity when
 * they appear together.  For example, in Spanish <C><h> gets collated
 * as a character between <C> and <D>.
 */
struct collelem {
	char		*symbol;
	wchar_t		*expand;
	int32_t		ref[COLL_WEIGHTS_MAX];
	RB_ENTRY(collelem) rb_bysymbol;
	RB_ENTRY(collelem) rb_byexpand;
};

/*
 * Individual characters have a sequence of weights as well.
 */
typedef struct collchar {
	wchar_t		wc;
	int32_t		ref[COLL_WEIGHTS_MAX];
	RB_ENTRY(collchar) entry;
} collchar_t;

/*
 * Substitution entries.  The key is itself a priority.  Note that
 * when we create one of these, we *automatically* wind up with a
 * fully resolved priority for the key, because creation of
 * substitutions creates a resolved priority at the same time.
 */
typedef struct subst{
	int32_t		key;
	int32_t		ref[COLLATE_STR_LEN];
	RB_ENTRY(subst)	entry;
	RB_ENTRY(subst)	entry_ref;
} subst_t;

static RB_HEAD(collsyms, collsym) collsyms;
static RB_HEAD(collundefs, collundef) collundefs;
static RB_HEAD(elem_by_symbol, collelem) elem_by_symbol;
static RB_HEAD(elem_by_expand, collelem) elem_by_expand;
static RB_HEAD(collchars, collchar) collchars;
static RB_HEAD(substs, subst) substs[COLL_WEIGHTS_MAX];
static RB_HEAD(substs_ref, subst) substs_ref[COLL_WEIGHTS_MAX];
static RB_HEAD(weights, weight) weights[COLL_WEIGHTS_MAX];
static int32_t		nweight[COLL_WEIGHTS_MAX];

/*
 * This is state tracking for the ellipsis token.  Note that we start
 * the initial values so that the ellipsis logic will think we got a
 * magic starting value of NUL.  It starts at minus one because the
 * starting point is exclusive -- i.e. the starting point is not
 * itself handled by the ellipsis code.
 */
static int currorder = EOF;
static int lastorder = EOF;
static collelem_t *currelem;
static collchar_t *currchar;
static collundef_t *currundef;
static wchar_t ellipsis_start = 0;
static int32_t ellipsis_weights[COLL_WEIGHTS_MAX];

/*
 * We keep a running tally of weights.
 */
static int nextpri = 1;
static int nextsubst[COLL_WEIGHTS_MAX] = { 0 };

/*
 * This array collects up the weights for each level.
 */
static int32_t order_weights[COLL_WEIGHTS_MAX];
static int curr_weight = 0;
static int32_t subst_weights[COLLATE_STR_LEN];
static int curr_subst = 0;

/*
 * Some initial priority values.
 */
static int32_t pri_undefined[COLL_WEIGHTS_MAX];
static int32_t pri_ignore;

static collate_info_t collinfo;
static int32_t subst_count[COLL_WEIGHTS_MAX];
static int32_t chain_count;
static int32_t large_count;

static collpri_t	*prilist = NULL;
static int		numpri = 0;
static int		maxpri = 0;

static void start_order(int);

static int32_t
new_pri(void)
{
	int i;

	if (numpri >= maxpri) {
		maxpri = maxpri ? maxpri * 2 : 1024;
		prilist = realloc(prilist, sizeof (collpri_t) * maxpri);
		if (prilist == NULL) {
			fprintf(stderr,"out of memory");
			return (-1);
		}
		for (i = numpri; i < maxpri; i++) {
			prilist[i].res = UNKNOWN;
			prilist[i].pri = 0;
			prilist[i].pass = 0;
		}
	}
	return (numpri++);
}

static collpri_t *
get_pri(int32_t ref)
{
	if ((ref < 0) || (ref > numpri)) {
		INTERR;
		return (NULL);
	}
	return (&prilist[ref]);
}

static void
set_pri(int32_t ref, int32_t v, res_t res)
{
	collpri_t	*pri;

	pri = get_pri(ref);

	if ((res == REFER) && ((v < 0) || (v >= numpri))) {
		INTERR;
	}

	/* Resolve self references */
	if ((res == REFER) && (ref == v)) {
		v = nextpri;
		res = RESOLVED;
	}

	if (pri->res != UNKNOWN) {
		warn("repeated item in order list (first on %d)",
		    pri->lineno);
		return;
	}
	pri->lineno = lineno;
	pri->pri = v;
	pri->res = res;
}

static int32_t
resolve_pri(int32_t ref)
{
	collpri_t	*pri;
	static int32_t	pass = 0;

	pri = get_pri(ref);
	pass++;
	while (pri->res == REFER) {
		if (pri->pass == pass) {
			/* report a line with the circular symbol */
			lineno = pri->lineno;
			fprintf(stderr,"circular reference in order list");
			return (-1);
		}
		if ((pri->pri < 0) || (pri->pri >= numpri)) {
			INTERR;
			return (-1);
		}
		pri->pass = pass;
		pri = &prilist[pri->pri];
	}

	if (pri->res == UNKNOWN) {
		return (-1);
	}
	if (pri->res != RESOLVED)
		INTERR;

	return (pri->pri);
}

static int
weight_compare(const void *n1, const void *n2)
{
	int32_t	k1 = ((const weight_t *)n1)->pri;
	int32_t	k2 = ((const weight_t *)n2)->pri;

	return (k1 < k2 ? -1 : k1 > k2 ? 1 : 0);
}

RB_GENERATE_STATIC(weights, weight, entry, weight_compare);

static int
collsym_compare(const void *n1, const void *n2)
{
	const collsym_t *c1 = n1;
	const collsym_t *c2 = n2;
	int rv;

	rv = strcmp(c1->name, c2->name);
	return ((rv < 0) ? -1 : (rv > 0) ? 1 : 0);
}

RB_GENERATE_STATIC(collsyms, collsym, entry, collsym_compare);

static int
collundef_compare(const void *n1, const void *n2)
{
	const collundef_t *c1 = n1;
	const collundef_t *c2 = n2;
	int rv;

	rv = strcmp(c1->name, c2->name);
	return ((rv < 0) ? -1 : (rv > 0) ? 1 : 0);
}

RB_GENERATE_STATIC(collundefs, collundef, entry, collundef_compare);

static int
element_compare_symbol(const void *n1, const void *n2)
{
	const collelem_t *c1 = n1;
	const collelem_t *c2 = n2;
	int rv;

	rv = strcmp(c1->symbol, c2->symbol);
	return ((rv < 0) ? -1 : (rv > 0) ? 1 : 0);
}

RB_GENERATE_STATIC(elem_by_symbol, collelem, rb_bysymbol, element_compare_symbol);

static int
element_compare_expand(const void *n1, const void *n2)
{
	const collelem_t *c1 = n1;
	const collelem_t *c2 = n2;
	int rv;

	rv = wcscmp(c1->expand, c2->expand);
	return ((rv < 0) ? -1 : (rv > 0) ? 1 : 0);
}

RB_GENERATE_STATIC(elem_by_expand, collelem, rb_byexpand, element_compare_expand);

static int
collchar_compare(const void *n1, const void *n2)
{
	wchar_t	k1 = ((const collchar_t *)n1)->wc;
	wchar_t	k2 = ((const collchar_t *)n2)->wc;

	return (k1 < k2 ? -1 : k1 > k2 ? 1 : 0);
}

RB_GENERATE_STATIC(collchars, collchar, entry, collchar_compare);

static int
subst_compare(const void *n1, const void *n2)
{
	int32_t	k1 = ((const subst_t *)n1)->key;
	int32_t	k2 = ((const subst_t *)n2)->key;

	return (k1 < k2 ? -1 : k1 > k2 ? 1 : 0);
}

RB_GENERATE_STATIC(substs, subst, entry, subst_compare);

static int
subst_compare_ref(const void *n1, const void *n2)
{
	const wchar_t *c1 = ((const subst_t *)n1)->ref;
	const wchar_t *c2 = ((const subst_t *)n2)->ref;
	int rv;

	rv = wcscmp(c1, c2);
	return ((rv < 0) ? -1 : (rv > 0) ? 1 : 0);
}

RB_GENERATE_STATIC(substs_ref, subst, entry_ref, subst_compare_ref);

void
init_collate(void)
{
	int i;

	RB_INIT(&collsyms);

	RB_INIT(&collundefs);

	RB_INIT(&elem_by_symbol);

	RB_INIT(&elem_by_expand);

	RB_INIT(&collchars);

	for (i = 0; i < COLL_WEIGHTS_MAX; i++) {
		RB_INIT(&substs[i]);
		RB_INIT(&substs_ref[i]);
		RB_INIT(&weights[i]);
		nweight[i] = 1;
	}

	(void) memset(&collinfo, 0, sizeof (collinfo));

	/* allocate some initial priorities */
	pri_ignore = new_pri();

	set_pri(pri_ignore, 0, RESOLVED);

	for (i = 0; i < COLL_WEIGHTS_MAX; i++) {
		pri_undefined[i] = new_pri();

		/* we will override this later */
		set_pri(pri_undefined[i], COLLATE_MAX_PRIORITY, UNKNOWN);
	}
}

void
define_collsym(char *name)
{
	collsym_t	*sym;

	if ((sym = calloc(1, sizeof(*sym))) == NULL) {
		fprintf(stderr,"out of memory");
		return;
	}
	sym->name = name;
	sym->ref = new_pri();

	if (RB_FIND(collsyms, &collsyms, sym) != NULL) {
		/*
		 * This should never happen because we are only called
		 * for undefined symbols.
		 */
		free(sym);
		INTERR;
		return;
	}
	RB_INSERT(collsyms, &collsyms, sym);
}

collsym_t *
lookup_collsym(char *name)
{
	collsym_t	srch;

	srch.name = name;
	return (RB_FIND(collsyms, &collsyms, &srch));
}

collelem_t *
lookup_collelem(char *symbol)
{
	collelem_t	srch;

	srch.symbol = symbol;
	return (RB_FIND(elem_by_symbol, &elem_by_symbol, &srch));
}

static collundef_t *
get_collundef(char *name)
{
	collundef_t	srch;
	collundef_t	*ud;
	int		i;

	srch.name = name;
	if ((ud = RB_FIND(collundefs, &collundefs, &srch)) == NULL) {
		if (((ud = calloc(1, sizeof(*ud))) == NULL) ||
		    ((ud->name = strdup(name)) == NULL)) {
			fprintf(stderr,"out of memory");
			free(ud);
			return (NULL);
		}
		for (i = 0; i < NUM_WT; i++) {
			ud->ref[i] = new_pri();
		}
		RB_INSERT(collundefs, &collundefs, ud);
	}
	add_charmap_undefined(name);
	return (ud);
}

static collchar_t *
get_collchar(wchar_t wc, int create)
{
	collchar_t	srch;
	collchar_t	*cc;
	int		i;

	srch.wc = wc;
	cc = RB_FIND(collchars, &collchars, &srch);
	if ((cc == NULL) && create) {
		if ((cc = calloc(1, sizeof(*cc))) == NULL) {
			fprintf(stderr, "out of memory");
			return (NULL);
		}
		for (i = 0; i < NUM_WT; i++) {
			cc->ref[i] = new_pri();
		}
		cc->wc = wc;
		RB_INSERT(collchars, &collchars, cc);
	}
	return (cc);
}

void
end_order_collsym(collsym_t *sym)
{
	start_order(T_COLLSYM);
	/* update the weight */

	set_pri(sym->ref, nextpri, RESOLVED);
	nextpri++;
}

void
end_order(void)
{
	int		i;
	int32_t		pri;
	int32_t		ref;
	collpri_t	*p;

	/* advance the priority/weight */
	pri = nextpri;

	switch (currorder) {
	case T_CHAR:
		for (i = 0; i < NUM_WT; i++) {
			if (((ref = order_weights[i]) < 0) ||
			    ((p = get_pri(ref)) == NULL) ||
			    (p->pri == -1)) {
				/* unspecified weight is a self reference */
				set_pri(currchar->ref[i], pri, RESOLVED);
			} else {
				set_pri(currchar->ref[i], ref, REFER);
			}
			order_weights[i] = -1;
		}

		/* leave a cookie trail in case next symbol is ellipsis */
		ellipsis_start = currchar->wc + 1;
		currchar = NULL;
		break;

	case T_ELLIPSIS:
		/* save off the weights were we can find them */
		for (i = 0; i < NUM_WT; i++) {
			ellipsis_weights[i] = order_weights[i];
			order_weights[i] = -1;
		}
		break;

	case T_COLLELEM:
		if (currelem == NULL) {
			INTERR;
		} else {
			for (i = 0; i < NUM_WT; i++) {

				if (((ref = order_weights[i]) < 0) ||
				    ((p = get_pri(ref)) == NULL) ||
				    (p->pri == -1)) {
					set_pri(currelem->ref[i], pri,
					    RESOLVED);
				} else {
					set_pri(currelem->ref[i], ref, REFER);
				}
				order_weights[i] = -1;
			}
		}
		break;

	case T_UNDEFINED:
		for (i = 0; i < NUM_WT; i++) {
			if (((ref = order_weights[i]) < 0) ||
			    ((p = get_pri(ref)) == NULL) ||
			    (p->pri == -1)) {
				set_pri(pri_undefined[i], -1, RESOLVED);
			} else {
				set_pri(pri_undefined[i], ref, REFER);
			}
			order_weights[i] = -1;
		}
		break;

	case T_SYMBOL:
		for (i = 0; i < NUM_WT; i++) {
			if (((ref = order_weights[i]) < 0) ||
			    ((p = get_pri(ref)) == NULL) ||
			    (p->pri == -1)) {
				set_pri(currundef->ref[i], pri, RESOLVED);
			} else {
				set_pri(currundef->ref[i], ref, REFER);
			}
			order_weights[i] = -1;
		}
		break;

	default:
		INTERR;
	}

	nextpri++;
}

static void
start_order(int type)
{
	int	i;

	lastorder = currorder;
	currorder = type;

	/* this is used to protect ELLIPSIS processing */
	if ((lastorder == T_ELLIPSIS) && (type != T_CHAR)) {
		fprintf(stderr, "character value expected");
	}

	for (i = 0; i < COLL_WEIGHTS_MAX; i++) {
		order_weights[i] = -1;
	}
	curr_weight = 0;
}

void
start_order_undefined(void)
{
	start_order(T_UNDEFINED);
}

void
start_order_symbol(char *name)
{
	currundef = get_collundef(name);
	start_order(T_SYMBOL);
}

void
start_order_char(wchar_t wc)
{
	collchar_t	*cc;
	int32_t		ref;

	start_order(T_CHAR);

	/*
	 * If we last saw an ellipsis, then we need to close the range.
	 * Handle that here.  Note that we have to be careful because the
	 * items *inside* the range are treated exclusiveley to the items
	 * outside of the range.  The ends of the range can have quite
	 * different weights than the range members.
	 */
	if (lastorder == T_ELLIPSIS) {
		int		i;

		if (wc < ellipsis_start) {
			fprintf(stderr, "malformed range!");
			return;
		}
		while (ellipsis_start < wc) {
			/*
			 * pick all of the saved weights for the
			 * ellipsis.  note that -1 encodes for the
			 * ellipsis itself, which means to take the
			 * current relative priority.
			 */
			if ((cc = get_collchar(ellipsis_start, 1)) == NULL) {
				INTERR;
				return;
			}
			for (i = 0; i < NUM_WT; i++) {
				collpri_t *p;
				if (((ref = ellipsis_weights[i]) == -1) ||
				    ((p = get_pri(ref)) == NULL) ||
				    (p->pri == -1)) {
					set_pri(cc->ref[i], nextpri, RESOLVED);
				} else {
					set_pri(cc->ref[i], ref, REFER);
				}
				ellipsis_weights[i] = 0;
			}
			ellipsis_start++;
			nextpri++;
		}
	}

	currchar = get_collchar(wc, 1);
}

void
start_order_collelem(collelem_t *e)
{
	start_order(T_COLLELEM);
	currelem = e;
}

void
start_order_ellipsis(void)
{
	int	i;

	start_order(T_ELLIPSIS);

	if (lastorder != T_CHAR) {
		fprintf(stderr, "illegal starting point for range");
		return;
	}

	for (i = 0; i < NUM_WT; i++) {
		ellipsis_weights[i] = order_weights[i];
	}
}

void
define_collelem(char *name, wchar_t *wcs)
{
	collelem_t	*e;
	int		i;

	if (wcslen(wcs) >= COLLATE_STR_LEN) {
		fprintf(stderr,"expanded collation element too long");
		return;
	}

	if ((e = calloc(1, sizeof(*e))) == NULL) {
		fprintf(stderr, "out of memory");
		return;
	}
	e->expand = wcs;
	e->symbol = name;

	/*
	 * This is executed before the order statement, so we don't
	 * know how many priorities we *really* need.  We allocate one
	 * for each possible weight.  Not a big deal, as collating-elements
	 * prove to be quite rare.
	 */
	for (i = 0; i < COLL_WEIGHTS_MAX; i++) {
		e->ref[i] = new_pri();
	}

	/* A character sequence can only reduce to one element. */
	if ((RB_FIND(elem_by_symbol, &elem_by_symbol, e) != NULL) ||
	    (RB_FIND(elem_by_expand, &elem_by_expand, e) != NULL)) {
		fprintf(stderr, "duplicate collating element definition");
		free(e);
		return;
	}
	RB_INSERT(elem_by_symbol, &elem_by_symbol, e);
	RB_INSERT(elem_by_expand, &elem_by_expand, e);
}

void
add_order_bit(int kw)
{
	uint8_t bit = DIRECTIVE_UNDEF;

	switch (kw) {
	case T_FORWARD:
		bit = DIRECTIVE_FORWARD;
		break;
	case T_BACKWARD:
		bit = DIRECTIVE_BACKWARD;
		break;
	case T_POSITION:
		bit = DIRECTIVE_POSITION;
		break;
	default:
		INTERR;
		break;
	}
	collinfo.directive[collinfo.directive_count] |= bit;
}

void
add_order_directive(void)
{
	if (collinfo.directive_count >= COLL_WEIGHTS_MAX) {
		fprintf(stderr,"too many directives (max %d)", COLL_WEIGHTS_MAX);
	}
	collinfo.directive_count++;
}

static void
add_order_pri(int32_t ref)
{
	if (curr_weight >= NUM_WT) {
		fprintf(stderr,"too many weights (max %d)", NUM_WT);
		return;
	}
	order_weights[curr_weight] = ref;
	curr_weight++;
}

void
add_order_collsym(collsym_t *s)
{
	add_order_pri(s->ref);
}

void
add_order_char(wchar_t wc)
{
	collchar_t *cc;

	if ((cc = get_collchar(wc, 1)) == NULL) {
		INTERR;
		return;
	}

	add_order_pri(cc->ref[curr_weight]);
}

void
add_order_collelem(collelem_t *e)
{
	add_order_pri(e->ref[curr_weight]);
}

void
add_order_ignore(void)
{
	add_order_pri(pri_ignore);
}

void
add_order_symbol(char *sym)
{
	collundef_t *c;
	if ((c = get_collundef(sym)) == NULL) {
		INTERR;
		return;
	}
	add_order_pri(c->ref[curr_weight]);
}

void
add_order_ellipsis(void)
{
	/* special NULL value indicates self reference */
	add_order_pri(0);
}

void
add_order_subst(void)
{
	subst_t srch;
	subst_t	*s;
	int i;

	(void) memset(&srch, 0, sizeof (srch));
	for (i = 0; i < curr_subst; i++) {
		srch.ref[i] = subst_weights[i];
		subst_weights[i] = 0;
	}
	s = RB_FIND(substs_ref, &substs_ref[curr_weight], &srch);

	if (s == NULL) {
		if ((s = calloc(1, sizeof(*s))) == NULL) {
			fprintf(stderr,"out of memory");
			return;
		}
		s->key = new_pri();

		/*
		 * We use a self reference for our key, but we set a
		 * high bit to indicate that this is a substitution
		 * reference.  This will expedite table lookups later,
		 * and prevent table lookups for situations that don't
		 * require it.  (In short, its a big win, because we
		 * can skip a lot of binary searching.)
		 */
		set_pri(s->key,
		    (nextsubst[curr_weight] | COLLATE_SUBST_PRIORITY),
		    RESOLVED);
		nextsubst[curr_weight] += 1;

		for (i = 0; i < curr_subst; i++) {
			s->ref[i] = srch.ref[i];
		}

		RB_INSERT(substs_ref, &substs_ref[curr_weight], s);

		if (RB_FIND(substs, &substs[curr_weight], s) != NULL) {
			INTERR;
			return;
		}
		RB_INSERT(substs, &substs[curr_weight], s);
	}
	curr_subst = 0;


	/*
	 * We are using the current (unique) priority as a search key
	 * in the substitution table.
	 */
	add_order_pri(s->key);
}

static void
add_subst_pri(int32_t ref)
{
	if (curr_subst >= COLLATE_STR_LEN) {
		fprintf(stderr,"substitution string is too long");
		return;
	}
	subst_weights[curr_subst] = ref;
	curr_subst++;
}

void
add_subst_char(wchar_t wc)
{
	collchar_t *cc;


	if (((cc = get_collchar(wc, 1)) == NULL) ||
	    (cc->wc != wc)) {
		INTERR;
		return;
	}
	/* we take the weight for the character at that position */
	add_subst_pri(cc->ref[curr_weight]);
}

void
add_subst_collelem(collelem_t *e)
{
	add_subst_pri(e->ref[curr_weight]);
}

void
add_subst_collsym(collsym_t *s)
{
	add_subst_pri(s->ref);
}

void
add_subst_symbol(char *ptr)
{
	collundef_t *cu;

	if ((cu = get_collundef(ptr)) != NULL) {
		add_subst_pri(cu->ref[curr_weight]);
	}
}

void
add_weight(int32_t ref, int pass)
{
	weight_t srch;
	weight_t *w;

	srch.pri = resolve_pri(ref);

	/* No translation of ignores */
	if (srch.pri == 0)
		return;

	/* Substitution priorities are not weights */
	if (srch.pri & COLLATE_SUBST_PRIORITY)
		return;

	if (RB_FIND(weights, &weights[pass], &srch) != NULL)
		return;

	if ((w = calloc(1, sizeof(*w))) == NULL) {
		fprintf(stderr, "out of memory");
		return;
	}
	w->pri = srch.pri;
	RB_INSERT(weights, &weights[pass], w);
}

void
add_weights(int32_t *refs)
{
	int i;
	for (i = 0; i < NUM_WT; i++) {
		add_weight(refs[i], i);
	}
}

int32_t
get_weight(int32_t ref, int pass)
{
	weight_t	srch;
	weight_t	*w;
	int32_t		pri;

	pri = resolve_pri(ref);
	if (pri & COLLATE_SUBST_PRIORITY) {
		return (pri);
	}
	if (pri <= 0) {
		return (pri);
	}
	srch.pri = pri;
	if ((w = RB_FIND(weights, &weights[pass], &srch)) == NULL) {
		INTERR;
		return (-1);
	}
	return (w->opt);
}

wchar_t *
wsncpy(wchar_t *s1, const wchar_t *s2, size_t n)
{
	wchar_t *os1 = s1;

	n++;
	while (--n > 0 && (*s1++ = htote(*s2++)) != 0)
		continue;
	if (n > 0)
		while (--n > 0)
			*s1++ = 0;
	return (os1);
}

#define RB_COUNT(x, name, head, cnt) do { \
	(cnt) = 0; \
	RB_FOREACH(x, name, (head)) { \
		(cnt)++; \
	} \
} while (0)

#define RB_NUMNODES(type, name, head, cnt) do { \
	type *t; \
	cnt = 0; \
	RB_FOREACH(t, name, head) { \
		cnt++; \
	} \
} while (0)

void
dump_collate(void)
{
	FILE			*f;
	int			i, j, n;
	size_t			sz;
	int32_t			pri;
	collelem_t		*ce;
	collchar_t		*cc;
	subst_t			*sb;
	char			vers[COLLATE_STR_LEN];
	collate_char_t		chars[UCHAR_MAX + 1];
	collate_large_t		*large;
	collate_subst_t		*subst[COLL_WEIGHTS_MAX];
	collate_chain_t		*chain;

	/*
	 * We have to run through a preliminary pass to identify all the
	 * weights that we use for each sorting level.
	 */
	for (i = 0; i < NUM_WT; i++) {
		add_weight(pri_ignore, i);
	}
	for (i = 0; i < NUM_WT; i++) {
		RB_FOREACH(sb, substs, &substs[i]) {
			for (j = 0; sb->ref[j]; j++) {
				add_weight(sb->ref[j], i);
			}
		}
	}
	RB_FOREACH(ce, elem_by_expand, &elem_by_expand) {
		add_weights(ce->ref);
	}
	RB_FOREACH(cc, collchars, &collchars) {
		add_weights(cc->ref);
	}

	/*
	 * Now we walk the entire set of weights, removing the gaps
	 * in the weights.  This gives us optimum usage.  The walk
	 * occurs in priority.
	 */
	for (i = 0; i < NUM_WT; i++) {
		weight_t *w;
		RB_FOREACH(w, weights, &weights[i]) {
			w->opt = nweight[i];
			nweight[i] += 1;
		}
	}

	(void) memset(&chars, 0, sizeof (chars));
	(void) memset(vers, 0, COLLATE_STR_LEN);
	(void) strlcpy(vers, COLLATE_VERSION, sizeof (vers));

	/*
	 * We need to make sure we arrange for the UNDEFINED field
	 * to show up.  Also, set the total weight counts.
	 */
	for (i = 0; i < NUM_WT; i++) {
		if (resolve_pri(pri_undefined[i]) == -1) {
			set_pri(pri_undefined[i], -1, RESOLVED);
			/* they collate at the end of everything else */
			collinfo.undef_pri[i] = htote(COLLATE_MAX_PRIORITY);
		}
		collinfo.pri_count[i] = htote(nweight[i]);
	}

	collinfo.pri_count[NUM_WT] = htote(max_wide());
	collinfo.undef_pri[NUM_WT] = htote(COLLATE_MAX_PRIORITY);
	collinfo.directive[NUM_WT] = DIRECTIVE_UNDEFINED;

	/*
	 * Ordinary character priorities
	 */
	for (i = 0; i <= UCHAR_MAX; i++) {
		if ((cc = get_collchar(i, 0)) != NULL) {
			for (j = 0; j < NUM_WT; j++) {
				chars[i].pri[j] =
				    htote(get_weight(cc->ref[j], j));
			}
		} else {
			for (j = 0; j < NUM_WT; j++) {
				chars[i].pri[j] =
				    htote(get_weight(pri_undefined[j], j));
			}
			/*
			 * Per POSIX, for undefined characters, we
			 * also have to add a last item, which is the
			 * character code.
			 */
			chars[i].pri[NUM_WT] = htote(i);
		}
	}

	/*
	 * Substitution tables
	 */
	for (i = 0; i < NUM_WT; i++) {
		collate_subst_t *st = NULL;
		subst_t *temp;
		RB_COUNT(temp, substs, &substs[i], n);
		subst_count[i] = n;
		if ((st = calloc(n, sizeof(collate_subst_t))) == NULL) {
			fprintf(stderr, "out of memory");
			return;
		}
		n = 0;
		RB_FOREACH(sb, substs, &substs[i]) {
			if ((st[n].key = resolve_pri(sb->key)) < 0) {
				/* by definition these resolve! */
				INTERR;
			}
			if (st[n].key != (n | COLLATE_SUBST_PRIORITY)) {
				INTERR;
			}
			st[n].key = htote(st[n].key);
			for (j = 0; sb->ref[j]; j++) {
				st[n].pri[j] = htote(get_weight(sb->ref[j],
				    i));
			}
			n++;
		}
		if (n != subst_count[i])
			INTERR;
		subst[i] = st;
	}


	/*
	 * Chains, i.e. collating elements
	 */
	RB_NUMNODES(collelem_t, elem_by_expand, &elem_by_expand, chain_count);
	chain = calloc(chain_count, sizeof(collate_chain_t));
	if (chain == NULL) {
		fprintf(stderr, "out of memory");
		return;
	}
	n = 0;
	RB_FOREACH(ce, elem_by_expand, &elem_by_expand) {
		(void) wsncpy(chain[n].str, ce->expand, COLLATE_STR_LEN);
		for (i = 0; i < NUM_WT; i++) {
			chain[n].pri[i] = htote(get_weight(ce->ref[i], i));
		}
		n++;
	}
	if (n != chain_count)
		INTERR;

	/*
	 * Large (> UCHAR_MAX) character priorities
	 */
	RB_NUMNODES(collchar_t, collchars, &collchars, n);
	large = calloc(n, sizeof(collate_large_t));
	if (large == NULL) {
		fprintf(stderr, "out of memory");
		return;
	}

	i = 0;
	RB_FOREACH(cc, collchars, &collchars) {
		int	undef = 0;
		/* we already gathered those */
		if (cc->wc <= UCHAR_MAX)
			continue;
		for (j = 0; j < NUM_WT; j++) {
			if ((pri = get_weight(cc->ref[j], j)) < 0) {
				undef = 1;
			}
			if (undef && (pri >= 0)) {
				/* if undefined, then all priorities are */
				INTERR;
			} else {
				large[i].pri.pri[j] = htote(pri);
			}
		}
		if (!undef) {
			large[i].val = htote(cc->wc);
			large_count = i++;
		}
	}

	if ((f = open_category()) == NULL) {
		return;
	}

	/* Time to write the entire data set out */

	for (i = 0; i < NUM_WT; i++)
		collinfo.subst_count[i] = htote(subst_count[i]);
	collinfo.chain_count = htote(chain_count);
	collinfo.large_count = htote(large_count);

	if ((wr_category(vers, COLLATE_STR_LEN, f) < 0) ||
	    (wr_category(&collinfo, sizeof (collinfo), f) < 0) ||
	    (wr_category(&chars, sizeof (chars), f) < 0)) {
		return;
	}

	for (i = 0; i < NUM_WT; i++) {
		sz = sizeof (collate_subst_t) * subst_count[i];
		if (wr_category(subst[i], sz, f) < 0) {
			return;
		}
	}
	sz = sizeof (collate_chain_t) * chain_count;
	if (wr_category(chain, sz, f) < 0) {
		return;
	}
	sz = sizeof (collate_large_t) * large_count;
	if (wr_category(large, sz, f) < 0) {
		return;
	}

	close_category(f);
}
