/*	$OpenBSD: files.c,v 1.20 2015/01/16 06:40:16 deraadt Exp $	*/
/*	$NetBSD: files.c,v 1.6 1996/03/17 13:18:17 cgd Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratories.
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
 *
 *	from: @(#)files.c	8.1 (Berkeley) 6/6/93
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

extern const char *yyfile;

/*
 * We check that each full path name is unique.  File base names
 * should generally also be unique, e.g., having both a net/xx.c and
 * a kern/xx.c (or, worse, a net/xx.c and a new/xx.c++) is probably
 * wrong, but is permitted under some conditions.
 */
static struct hashtab *basetab;		/* file base names */
static struct hashtab *pathtab;		/* full path names */

static struct files **nextfile;
static struct files **unchecked;

static struct objects **nextobject;

static int	checkaux(const char *, void *);
static int	fixcount(const char *, void *);
static int	fixfsel(const char *, void *);
static int	fixsel(const char *, void *);
static int	expr_eval(struct nvlist *,
		    int (*)(const char *, void *), void *);
static void	expr_free(struct nvlist *);

#ifdef DEBUG
static void	pr0();
#endif

void
initfiles(void)
{

	basetab = ht_new();
	pathtab = ht_new();
	nextfile = &allfiles;
	unchecked = &allfiles;
	nextobject = &allobjects;
}

void
addfile(struct nvlist *nvpath, struct nvlist *optx, int flags, const char *rule)
{
	struct files *fi;
	const char *dotp, *dotp1, *tail, *path, *tail1 = NULL;
	struct nvlist *nv;
	size_t baselen;
	int needc, needf;
	char base[200];

	/* check various errors */
	needc = flags & FI_NEEDSCOUNT;
	needf = flags & FI_NEEDSFLAG;
	if (needc && needf) {
		error("cannot mix needs-count and needs-flag");
		goto bad;
	}
	if (optx == NULL && (needc || needf)) {
		error("nothing to %s", needc ? "count" : "flag");
		goto bad;
	}

	for (nv = nvpath; nv; nv = nv->nv_next) {
		path = nv->nv_name;

		/* find last part of pathname, and same without trailing suffix */
		tail = strrchr(path, '/');
		if (tail == NULL)
			tail = path;
		else
			tail++;
		dotp = strrchr(tail, '.');
		if (dotp == NULL || dotp[1] == 0 ||
		    (baselen = dotp - tail) >= sizeof(base)) {
			error("invalid pathname `%s'", path);
			goto bad;
		}

		/*
		 * Ensure all tailnames are identical, because .o
		 * filenames must be identical too.
		 */
		if (tail1 &&
		    (dotp - tail != dotp1 - tail1 ||
		    strncmp(tail1, tail, dotp - tail)))
			error("different production from %s %s",
			    nvpath->nv_name, tail);
		tail1 = tail;
		dotp1 = dotp;
	}

	/*
	 * Commit this file to memory.  We will decide later whether it
	 * will be used after all.
	 */
	fi = emalloc(sizeof *fi);
	if (ht_insert(pathtab, path, fi)) {
		free(fi);
		if ((fi = ht_lookup(pathtab, path)) == NULL)
			panic("addfile: ht_lookup(%s)", path);
		error("duplicate file %s", path);
		xerror(fi->fi_srcfile, fi->fi_srcline,
		    "here is the original definition");
	}
	memcpy(base, tail, baselen);
	base[baselen] = 0;
	fi->fi_next = NULL;
	fi->fi_srcfile = yyfile;
	fi->fi_srcline = currentline();
	fi->fi_flags = flags;
	fi->fi_nvpath = nvpath;
	fi->fi_base = intern(base);
	fi->fi_optx = optx;
	fi->fi_optf = NULL;
	fi->fi_mkrule = rule;
	*nextfile = fi;
	nextfile = &fi->fi_next;
	return;
bad:
	expr_free(optx);
}

void
addobject(const char *path, struct nvlist *optx, int flags)
{
	struct objects *oi;

	/*
	 * Commit this object to memory.  We will decide later whether it
	 * will be used after all.
	 */
	oi = emalloc(sizeof *oi);
	if (ht_insert(pathtab, path, oi)) {
		free(oi);
		if ((oi = ht_lookup(pathtab, path)) == NULL)
			panic("addfile: ht_lookup(%s)", path);
		error("duplicate file %s", path);
		xerror(oi->oi_srcfile, oi->oi_srcline,
		    "here is the original definition");
	}
	oi->oi_next = NULL;
	oi->oi_srcfile = yyfile;
	oi->oi_srcline = currentline();
	oi->oi_flags = flags;
	oi->oi_path = path;
	oi->oi_optx = optx;
	oi->oi_optf = NULL;
	*nextobject = oi;
	nextobject = &oi->oi_next;
}

/*
 * We have finished reading some "files" file, either ../../conf/files
 * or ./files.$machine.  Make sure that everything that is flagged as
 * needing a count is reasonable.  (This prevents ../../conf/files from
 * depending on some machine-specific device.)
 */
void
checkfiles(void)
{
	struct files *fi, *last;

	last = NULL;
	for (fi = *unchecked; fi != NULL; last = fi, fi = fi->fi_next)
		if ((fi->fi_flags & FI_NEEDSCOUNT) != 0)
			(void)expr_eval(fi->fi_optx, checkaux, fi);
	if (last != NULL)
		unchecked = &last->fi_next;
}

/*
 * Auxiliary function for checkfiles, called from expr_eval.
 * We are not actually interested in the expression's value.
 */
static int
checkaux(const char *name, void *context)
{
	struct files *fi = context;

	if (ht_lookup(devbasetab, name) == NULL) {
		xerror(fi->fi_srcfile, fi->fi_srcline,
		    "`%s' is not a countable device",
		    name);
		/* keep fixfiles() from complaining again */
		fi->fi_flags |= FI_HIDDEN;
	}
	return (0);
}

/*
 * We have finished reading everything.  Tack the files down: calculate
 * selection and counts as needed.  Check that the object files built
 * from the selected sources do not collide.
 */
int
fixfiles(void)
{
	struct files *fi, *ofi;
	struct nvlist *flathead, **flatp;
	int err, sel;

	err = 0;
	for (fi = allfiles; fi != NULL; fi = fi->fi_next) {
		/* Skip files that generated counted-device complaints. */
		if (fi->fi_flags & FI_HIDDEN)
			continue;

		/* Optional: see if it is to be included. */
		if (fi->fi_optx != NULL) {
			flathead = NULL;
			flatp = &flathead;
			sel = expr_eval(fi->fi_optx,
			    fi->fi_flags & FI_NEEDSCOUNT ? fixcount :
			    fi->fi_flags & FI_NEEDSFLAG ? fixfsel :
			    fixsel,
			    &flatp);
			fi->fi_optf = flathead;
			if (!sel)
				continue;
		}

		/* We like this file.  Make sure it generates a unique .o. */
		if (ht_insert(basetab, fi->fi_base, fi)) {
			if ((ofi = ht_lookup(basetab, fi->fi_base)) == NULL)
				panic("fixfiles ht_lookup(%s)", fi->fi_base);
			/*
			 * If the new file comes from a different source,
			 * allow the new one to override the old one.
			 */
			if (fi->fi_nvpath != ofi->fi_nvpath) {
				if (ht_replace(basetab, fi->fi_base, fi) != 1)
					panic("fixfiles ht_replace(%s)",
					    fi->fi_base);
				ofi->fi_flags &= ~FI_SEL;
				ofi->fi_flags |= FI_HIDDEN;
			} else {
				xerror(fi->fi_srcfile, fi->fi_srcline,
				    "object file collision on %s.o, from %s",
				    fi->fi_base, fi->fi_nvpath->nv_name);
				xerror(ofi->fi_srcfile, ofi->fi_srcline,
				    "here is the previous file: %s",
				    ofi->fi_nvpath->nv_name);
				err = 1;
			}
		}
		fi->fi_flags |= FI_SEL;
	}
	return (err);
}

/*
 * We have finished reading everything.  Tack the objects down: calculate
 * selection.
 */
int
fixobjects(void)
{
	struct objects *oi;
	struct nvlist *flathead, **flatp;
	int err, sel;

	err = 0;
	for (oi = allobjects; oi != NULL; oi = oi->oi_next) {
		/* Optional: see if it is to be included. */
		if (oi->oi_optx != NULL) {
			flathead = NULL;
			flatp = &flathead;
			sel = expr_eval(oi->oi_optx,
			    oi->oi_flags & OI_NEEDSFLAG ? fixfsel :
			    fixsel,
			    &flatp);
			oi->oi_optf = flathead;
			if (!sel)
				continue;
		}

		oi->oi_flags |= OI_SEL;
	}
	return (err);
}

/*
 * Called when evaluating a needs-count expression.  Make sure the
 * atom is a countable device.  The expression succeeds iff there
 * is at least one of them (note that while `xx*' will not always
 * set xx's d_umax > 0, you cannot mix '*' and needs-count).  The
 * mkheaders() routine wants a flattened, in-order list of the
 * atoms for `#define name value' lines, so we build that as we
 * are called to eval each atom.
 */
static int
fixcount(const char *name, void *context)
{
	struct nvlist ***p = context;
	struct devbase *dev;
	struct nvlist *nv;

	dev = ht_lookup(devbasetab, name);
	if (dev == NULL)	/* cannot occur here; we checked earlier */
		panic("fixcount(%s)", name);
	nv = newnv(name, NULL, NULL, dev->d_umax, NULL);
	**p = nv;
	*p = &nv->nv_next;
	(void)ht_insert(needcnttab, name, nv);
	return (dev->d_umax != 0);
}

/*
 * Called from fixfiles when eval'ing a selection expression for a
 * file that will generate a .h with flags.  We will need the flat list.
 */
static int
fixfsel(const char *name, void *context)
{
	struct nvlist ***p = context;
	struct nvlist *nv;
	int sel;

	sel = ht_lookup(selecttab, name) != NULL;
	nv = newnv(name, NULL, NULL, sel, NULL);
	**p = nv;
	*p = &nv->nv_next;
	return (sel);
}

/*
 * As for fixfsel above, but we do not need the flat list.
 */
static int
fixsel(const char *name, void *context)
{

	return (ht_lookup(selecttab, name) != NULL);
}

/*
 * Eval an expression tree.  Calls the given function on each node,
 * passing it the given context & the name; return value is &/|/! of
 * results of evaluating atoms.
 *
 * No short circuiting ever occurs.  fn must return 0 or 1 (otherwise
 * our mixing of C's bitwise & boolean here may give surprises).
 */
static int
expr_eval(struct nvlist *expr, int (*fn)(const char *, void *), void *context)
{
	int lhs, rhs;

	switch (expr->nv_int) {

	case FX_ATOM:
		return ((*fn)(expr->nv_name, context));

	case FX_NOT:
		return (!expr_eval(expr->nv_next, fn, context));

	case FX_AND:
		lhs = expr_eval(expr->nv_ptr, fn, context);
		rhs = expr_eval(expr->nv_next, fn, context);
		return (lhs & rhs);

	case FX_OR:
		lhs = expr_eval(expr->nv_ptr, fn, context);
		rhs = expr_eval(expr->nv_next, fn, context);
		return (lhs | rhs);
	}
	panic("expr_eval %d", expr->nv_int);
	return (0);
}

/*
 * Free an expression tree.
 */
static void
expr_free(struct nvlist *expr)
{
	struct nvlist *rhs;

	/* This loop traverses down the RHS of each subexpression. */
	for (; expr != NULL; expr = rhs) {
		switch (expr->nv_int) {

		/* Atoms and !-exprs have no left hand side. */
		case FX_ATOM:
		case FX_NOT:
			break;

		/* For AND and OR nodes, free the LHS. */
		case FX_AND:
		case FX_OR:
			expr_free(expr->nv_ptr);
			break;

		default:
			panic("expr_free %d", expr->nv_int);
		}
		rhs = expr->nv_next;
		nvfree(expr);
	}
}

#ifdef DEBUG
/*
 * Print expression tree.
 */
void
prexpr(struct nvlist *expr)
{
	printf("expr =");
	pr0(expr);
	printf("\n");
	(void)fflush(stdout);
}

static void
pr0(struct nvlist *e)
{

	switch (e->nv_int) {
	case FX_ATOM:
		printf(" %s", e->nv_name);
		return;
	case FX_NOT:
		printf(" (!");
		break;
	case FX_AND:
		printf(" (&");
		break;
	case FX_OR:
		printf(" (|");
		break;
	default:
		printf(" (?%d?", e->nv_int);
		break;
	}
	if (e->nv_ptr)
		pr0(e->nv_ptr);
	pr0(e->nv_next);
	printf(")");
}
#endif
