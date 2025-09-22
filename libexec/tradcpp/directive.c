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

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include "bool.h"
#include "utils.h"
#include "mode.h"
#include "place.h"
#include "files.h"
#include "directive.h"
#include "macro.h"
#include "eval.h"
#include "output.h"

struct ifstate {
	struct ifstate *prev;
	struct place startplace;
	bool curtrue;
	bool evertrue;
	bool seenelse;
};

static struct ifstate *ifstate;

////////////////////////////////////////////////////////////
// common parsing bits

static
void
uncomment(char *buf)
{
	char *s, *t, *u = NULL;
	bool incomment = false;
	bool inesc = false;
	bool inquote = false;
	char quote = '\0';

	for (s = t = buf; *s; s++) {
		if (incomment) {
			if (s[0] == '*' && s[1] == '/') {
				s++;
				incomment = false;
			}
		} else {
			if (!inquote && s[0] == '/' && s[1] == '*') {
				incomment = true;
			} else {
				if (inesc) {
					inesc = false;
				} else if (s[0] == '\\') {
					inesc = true;
				} else if (!inquote &&
					   (s[0] == '"' || s[0] == '\'')) {
					inquote = true;
					quote = s[0];
				} else if (inquote && s[0] == quote) {
					inquote = false;
				}

				if (t != s) {
					*t = *s;
				}
				if (!strchr(ws, *t)) {
					u = t;
				}
				t++;
			}
		}
	}
	if (u) {
		/* end string after last non-whitespace char */
		u[1] = '\0';
	} else {
		*t = '\0';
	}
}

static
void
oneword(const char *what, struct place *p2, char *line)
{
	size_t pos;

	pos = strcspn(line, ws);
	if (line[pos] != '\0') {
		place_addcolumns(p2, pos);
		complain(p2, "Garbage after %s argument", what);
		complain_fail();
		line[pos] = '\0';
	}
}

////////////////////////////////////////////////////////////
// if handling

static
struct ifstate *
ifstate_create(struct ifstate *prev, struct place *p, bool startstate)
{
	struct ifstate *is;

	is = domalloc(sizeof(*is));
	is->prev = prev;
	if (p != NULL) {
		is->startplace = *p;
	} else {
		place_setbuiltin(&is->startplace, 1);
	}
	is->curtrue = startstate;
	is->evertrue = is->curtrue;
	is->seenelse = false;
	return is;
}

static
void
ifstate_destroy(struct ifstate *is)
{
	dofree(is, sizeof(*is));
}

static
void
ifstate_push(struct place *p, bool startstate)
{
	struct ifstate *newstate;

	newstate = ifstate_create(ifstate, p, startstate);
	if (!ifstate->curtrue) {
		newstate->curtrue = false;
		newstate->evertrue = true;
	}
	ifstate = newstate;
}

static
void
ifstate_pop(void)
{
	struct ifstate *is;

	is = ifstate;
	ifstate = ifstate->prev;
	ifstate_destroy(is);
}

static
void
d_if(struct lineplace *lp, struct place *p2, char *line)
{
	bool doprint;
	char *expr;
	bool val;
	struct place p3 = *p2;
	size_t oldlen;

	doprint = ifstate->curtrue;

	expr = macroexpand(p2, line, strlen(line), true);

	oldlen = strlen(expr);
	uncomment(expr);
	/* trim to fit, so the malloc debugging won't complain */
	expr = dorealloc(expr, oldlen + 1, strlen(expr) + 1);

	if (ifstate->curtrue) {
		val = eval(&p3, expr);
	} else {
		val = 0;
	}
	ifstate_push(&lp->current, val);
	dostrfree(expr);

	if (doprint) {
		debuglog(&lp->current, "#if: %s",
			  ifstate->curtrue ? "taken" : "not taken");
	}
}

static
void
d_ifdef(struct lineplace *lp, struct place *p2, char *line)
{
	bool doprint;

	doprint = ifstate->curtrue;

	uncomment(line);
	oneword("#ifdef", p2, line);
	ifstate_push(&lp->current, macro_isdefined(line));

	if (doprint) {
		debuglog(&lp->current, "#ifdef %s: %s",
			 line, ifstate->curtrue ? "taken" : "not taken");
	}
}

static
void
d_ifndef(struct lineplace *lp, struct place *p2, char *line)
{
	bool doprint;

	doprint = ifstate->curtrue;

	uncomment(line);
	oneword("#ifndef", p2, line);
	ifstate_push(&lp->current, !macro_isdefined(line));

	if (doprint) {
		debuglog(&lp->current, "#ifndef %s: %s",
			 line, ifstate->curtrue ? "taken" : "not taken");
	}
}

static
void
d_elif(struct lineplace *lp, struct place *p2, char *line)
{
	bool doprint;
	char *expr;
	struct place p3 = *p2;
	size_t oldlen;

	if (ifstate->seenelse) {
		complain(&lp->current, "#elif after #else");
		complain_fail();
	}

	doprint = ifstate->curtrue;

	if (ifstate->evertrue) {
		ifstate->curtrue = false;
	} else {
		expr = macroexpand(p2, line, strlen(line), true);

		oldlen = strlen(expr);
		uncomment(expr);
		/* trim to fit, so the malloc debugging won't complain */
		expr = dorealloc(expr, oldlen + 1, strlen(expr) + 1);

		ifstate->curtrue = eval(&p3, expr);
		ifstate->evertrue = ifstate->curtrue;
		dostrfree(expr);
	}

	if (doprint) {
		debuglog2(&lp->current, &ifstate->startplace, "#elif: %s",
			  ifstate->curtrue ? "taken" : "not taken");
	}
}

static
void
d_else(struct lineplace *lp, struct place *p2, char *line)
{
	bool doprint;

	(void)p2;
	(void)line;

	if (ifstate->seenelse) {
		complain(&lp->current,
			 "Multiple #else directives in one conditional");
		complain_fail();
	}

	doprint = ifstate->curtrue;

	ifstate->curtrue = !ifstate->evertrue;
	ifstate->evertrue = true;
	ifstate->seenelse = true;

	if (doprint) {
		debuglog2(&lp->current, &ifstate->startplace, "#else: %s",
			  ifstate->curtrue ? "taken" : "not taken");
	}
}

static
void
d_endif(struct lineplace *lp, struct place *p2, char *line)
{
	(void)p2;
	(void)line;

	if (ifstate->prev == NULL) {
		complain(&lp->current, "Unmatched #endif");
		complain_fail();
	} else {
		debuglog2(&lp->current, &ifstate->startplace, "#endif");
		ifstate_pop();
	}
}

////////////////////////////////////////////////////////////
// macros

static
void
d_define(struct lineplace *lp, struct place *p2, char *line)
{
	size_t pos, argpos;
	struct place p3, p4;

	(void)lp;

	/*
	 * line may be:
	 *    macro expansion
	 *    macro(arg, arg, ...) expansion
	 */

	pos = strcspn(line, " \t\f\v(");
	if (line[pos] == '(') {
		line[pos++] = '\0';
		argpos = pos;
		pos = pos + strcspn(line+pos, "()");
		if (line[pos] == '(') {
			place_addcolumns(p2, pos);
			complain(p2, "Left parenthesis in macro parameters");
			complain_fail();
			return;
		}
		if (line[pos] != ')') {
			place_addcolumns(p2, pos);
			complain(p2, "Unclosed macro parameter list");
			complain_fail();
			return;
		}
		line[pos++] = '\0';
#if 0
		if (!strchr(ws, line[pos])) {
			p2->column += pos;
			complain(p2, "Trash after macro parameter list");
			complain_fail();
			return;
		}
#endif
	} else if (line[pos] == '\0') {
		argpos = 0;
	} else {
		line[pos++] = '\0';
		argpos = 0;
	}

	pos += strspn(line+pos, ws);

	p3 = *p2;
	place_addcolumns(&p3, argpos);

	p4 = *p2;
	place_addcolumns(&p4, pos);

	if (argpos) {
		debuglog(&lp->current, "Defining %s()", line);
		macro_define_params(p2, line, &p3,
				    line + argpos, &p4,
				    line + pos);
	} else {
		debuglog(&lp->current, "Defining %s", line);
		macro_define_plain(p2, line, &p4, line + pos);
	}
}

static
void
d_undef(struct lineplace *lp, struct place *p2, char *line)
{
	(void)lp;

	uncomment(line);
	oneword("#undef", p2, line);
	debuglog(&lp->current, "Undef %s", line);
	macro_undef(line);
}

////////////////////////////////////////////////////////////
// includes

static
bool
tryinclude(struct place *p, char *line)
{
	size_t len;

	len = strlen(line);
	if (len > 2 && line[0] == '"' && line[len-1] == '"') {
		line[len-1] = '\0';
		debuglog(p, "Entering include file \"%s\"", line+1);
		file_readquote(p, line+1);
		debuglog(p, "Leaving include file \"%s\"", line+1);
		line[len-1] = '"';
		return true;
	}
	if (len > 2 && line[0] == '<' && line[len-1] == '>') {
		line[len-1] = '\0';
		debuglog(p, "Entering include file <%s>", line+1);
		file_readbracket(p, line+1);
		debuglog(p, "Leaving include file <%s>", line+1);
		line[len-1] = '>';
		return true;
	}
	return false;
}

static
void
d_include(struct lineplace *lp, struct place *p2, char *line)
{
	char *text;
	size_t oldlen;

	uncomment(line);
	if (tryinclude(&lp->current, line)) {
		return;
	}
	text = macroexpand(p2, line, strlen(line), false);

	oldlen = strlen(text);
	uncomment(text);
	/* trim to fit, so the malloc debugging won't complain */
	text = dorealloc(text, oldlen + 1, strlen(text) + 1);

	if (tryinclude(&lp->current, text)) {
		dostrfree(text);
		return;
	}
	complain(&lp->current, "Illegal #include directive");
	complain(&lp->current, "Before macro expansion: #include %s", line);
	complain(&lp->current, "After macro expansion: #include %s", text);
	dostrfree(text);
	complain_fail();
}

static
void
d_line(struct lineplace *lp, struct place *p2, char *line)
{
	char *text;
	size_t oldlen;
	unsigned long val;
	char *moretext;
	size_t moretextlen;
	char *filename;

	text = macroexpand(p2, line, strlen(line), true);

	oldlen = strlen(text);
	uncomment(text);
	/* trim to fit, so the malloc debugging won't complain */
	text = dorealloc(text, oldlen + 1, strlen(text) + 1);

	/*
	 * What we should have here: either 1234 "file.c",
	 * or just 1234.
	 */

	errno = 0;
	val = strtoul(text, &moretext, 10);
	if (errno) {
		complain(&lp->current,
			 "Invalid line number in #line directive");
		goto fail;
	}
#if UINT_MAX < ULONG_MAX
	if (val > UINT_MAX) {
		complain(&lp->current,
			 "Line number in #line directive too large");
		goto fail;
	}
#endif
	moretext += strspn(moretext, ws);
	moretextlen = strlen(moretext);
	place_addcolumns(&lp->current, moretext - text);

	if (moretextlen > 2 &&
	    moretext[0] == '"' && moretext[moretextlen-1] == '"') {
		filename = dostrndup(moretext+1, moretextlen-2);
		place_changefile(&lp->nextline, filename);
		dostrfree(filename);
	}
	else if (moretextlen > 0) {
		complain(&lp->current,
			 "Invalid file name in #line directive");
		goto fail;
	}

	lp->nextline.line = val;
	dostrfree(text);
	return;

fail:
	complain(&lp->current, "Before macro expansion: #line %s", line);
	complain(&lp->current, "After macro expansion: #line %s", text);
	complain_fail();
	dostrfree(text);
}

////////////////////////////////////////////////////////////
// messages

static
void
d_warning(struct lineplace *lp, struct place *p2, char *line)
{
	char *msg;

	msg = macroexpand(p2, line, strlen(line), false);
	complain(&lp->current, "#warning: %s", msg);
	if (mode.werror) {
		complain_fail();
	}
	dostrfree(msg);
}

static
void
d_error(struct lineplace *lp, struct place *p2, char *line)
{
	char *msg;

	msg = macroexpand(p2, line, strlen(line), false);
	complain(&lp->current, "#error: %s", msg);
	complain_fail();
	dostrfree(msg);
}

////////////////////////////////////////////////////////////
// other

static
void
d_pragma(struct lineplace *lp, struct place *p2, char *line)
{
	(void)p2;

	complain(&lp->current, "#pragma %s", line);
	complain_fail();
}

////////////////////////////////////////////////////////////
// directive table

static const struct {
	const char *name;
	bool ifskip;
	void (*func)(struct lineplace *, struct place *, char *line);
} directives[] = {
	{ "define",  true,  d_define },
	{ "elif",    false, d_elif },
	{ "else",    false, d_else },
	{ "endif",   false, d_endif },
	{ "error",   true,  d_error },
	{ "if",      false, d_if },
	{ "ifdef",   false, d_ifdef },
	{ "ifndef",  false, d_ifndef },
	{ "include", true,  d_include },
	{ "line",    true,  d_line },
	{ "pragma",  true,  d_pragma },
	{ "undef",   true,  d_undef },
	{ "warning", true,  d_warning },
};
static const unsigned numdirectives = HOWMANY(directives);

static
void
directive_gotdirective(struct lineplace *lp, char *line)
{
	struct place p2;
	size_t len, skip;
	unsigned i;

	p2 = lp->current;
	for (i=0; i<numdirectives; i++) {
		len = strlen(directives[i].name);
		if (!strncmp(line, directives[i].name, len) &&
		    strchr(ws, line[len])) {
			if (directives[i].ifskip && !ifstate->curtrue) {
				return;
			}
			skip = len + strspn(line+len, ws);
			place_addcolumns(&p2, skip);
			line += skip;

			len = strlen(line);
			len = notrailingws(line, len);
			if (len < strlen(line)) {
				line[len] = '\0';
			}
			directives[i].func(lp, &p2, line);
			return;
		}
	}
	/* ugh. allow # by itself, including with a comment after it */
	uncomment(line);
	if (line[0] == '\0') {
		return;
	}

	skip = strcspn(line, ws);
	complain(&lp->current, "Unknown directive #%.*s", (int)skip, line);
	complain_fail();
}

/*
 * Check for nested comment delimiters in LINE.
 */
static
size_t
directive_scancomments(const struct lineplace *lp, char *line, size_t len)
{
	size_t pos;
	bool incomment;
	struct place p2;

	p2 = lp->current;
	incomment = 0;
	for (pos = 0; pos+1 < len; pos++) {
		if (line[pos] == '/' && line[pos+1] == '*') {
			if (incomment) {
				complain(&p2, "Warning: %c%c within comment",
					 '/', '*');
				if (mode.werror) {
					complain_failed();
				}
			} else {
				incomment = true;
			}
			pos++;
		} else if (line[pos] == '*' && line[pos+1] == '/') {
			if (incomment) {
				incomment = false;
			} else {
				/* stray end-comment; should we care? */
			}
			pos++;
		}
		if (line[pos] == '\n') {
			place_addlines(&p2, 1);
			p2.column = 0;
		} else {
			place_addcolumns(&p2, 1);
		}
	}

	/* multiline comments are supposed to arrive in a single buffer */
	assert(!incomment);
	return len;
}

void
directive_gotline(struct lineplace *lp, char *line, size_t len)
{
	size_t skip;

	if (warns.nestcomment) {
		directive_scancomments(lp, line, len);
	}

	/* check if we have a directive line (# exactly in column 0) */
	if (len > 0 && line[0] == '#') {
		skip = 1 + strspn(line + 1, ws);
		assert(skip <= len);
		place_addcolumns(&lp->current, skip);
		assert(line[len] == '\0');
		directive_gotdirective(lp, line+skip /*, length = len-skip */);
		place_addcolumns(&lp->current, len-skip);
	} else if (ifstate->curtrue) {
		macro_sendline(&lp->current, line, len);
		place_addcolumns(&lp->current, len);
	}
}

void
directive_goteof(struct place *p)
{
	while (ifstate->prev != NULL) {
		complain(p, "Missing #endif");
		complain(&ifstate->startplace, "...opened at this point");
		complain_failed();
		ifstate_pop();
	}
	macro_sendeof(p);
}

////////////////////////////////////////////////////////////
// module initialization

void
directive_init(void)
{
	ifstate = ifstate_create(NULL, NULL, true);
}

void
directive_cleanup(void)
{
	assert(ifstate->prev == NULL);
	ifstate_destroy(ifstate);
	ifstate = NULL;
}
