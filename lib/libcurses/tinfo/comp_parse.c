/* $OpenBSD: comp_parse.c,v 1.13 2023/10/17 09:52:09 nicm Exp $ */

/****************************************************************************
 * Copyright 2018-2022,2023 Thomas E. Dickey                                *
 * Copyright 1998-2016,2017 Free Software Foundation, Inc.                  *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 *     and: Thomas E. Dickey                        1996-on                 *
 ****************************************************************************/

/*
 *	comp_parse.c -- parser driver loop and use handling.
 *
 *	Use this code by calling _nc_read_entry_source() on as many source
 *	files as you like (either terminfo or termcap syntax).  If you
 *	want use-resolution, call _nc_resolve_uses2().  To free the list
 *	storage, do _nc_free_entries().
 */

#include <curses.priv.h>

#include <ctype.h>

#include <tic.h>

MODULE_ID("$Id: comp_parse.c,v 1.13 2023/10/17 09:52:09 nicm Exp $")

static void sanity_check2(TERMTYPE2 *, bool);
NCURSES_IMPEXP void (NCURSES_API *_nc_check_termtype2) (TERMTYPE2 *, bool) = sanity_check2;

static void fixup_acsc(TERMTYPE2 *, int);

static void
enqueue(ENTRY * ep)
/* add an entry to the in-core list */
{
    ENTRY *newp;

    DEBUG(2, (T_CALLED("enqueue(ep=%p)"), (void *) ep));

    newp = _nc_copy_entry(ep);
    if (newp == 0)
	_nc_err_abort(MSG_NO_MEMORY);

    newp->last = _nc_tail;
    _nc_tail = newp;

    newp->next = 0;
    if (newp->last)
	newp->last->next = newp;
    DEBUG(2, (T_RETURN("")));
}

#define NAMEBUFFER_SIZE (MAX_NAME_SIZE + 2)

static char *
force_bar(char *dst, char *src)
{
    if (strchr(src, '|') == 0) {
	size_t len = strlen(src);
	if (len > MAX_NAME_SIZE)
	    len = MAX_NAME_SIZE;
	_nc_STRNCPY(dst, src, MAX_NAME_SIZE);
	_nc_STRCPY(dst + len, "|", NAMEBUFFER_SIZE - len);
	src = dst;
    }
    return src;
}
#define ForceBar(dst, src) ((strchr(src, '|') == 0) ? force_bar(dst, src) : src)

#if NCURSES_USE_TERMCAP && NCURSES_XNAMES
static char *
skip_index(char *name)
{
    char *bar = strchr(name, '|');

    if (bar != 0 && (bar - name) == 2)
	name = bar + 1;

    return name;
}
#endif

static bool
check_collisions(char *n1, char *n2, int counter)
{
    char *pstart, *qstart, *pend, *qend;
    char nc1[NAMEBUFFER_SIZE];
    char nc2[NAMEBUFFER_SIZE];

    n1 = ForceBar(nc1, n1);
    n2 = ForceBar(nc2, n2);

#if NCURSES_USE_TERMCAP && NCURSES_XNAMES
    if ((_nc_syntax == SYN_TERMCAP) && _nc_user_definable) {
	n1 = skip_index(n1);
	n2 = skip_index(n2);
    }
#endif

    for (pstart = n1; (pend = strchr(pstart, '|')); pstart = pend + 1) {
	for (qstart = n2; (qend = strchr(qstart, '|')); qstart = qend + 1) {
	    if ((pend - pstart == qend - qstart)
		&& memcmp(pstart, qstart, (size_t) (pend - pstart)) == 0) {
		if (counter > 0)
		    (void) fprintf(stderr, "Name collision '%.*s' between\n",
				   (int) (pend - pstart), pstart);
		return (TRUE);
	    }
	}
    }

    return (FALSE);
}

static char *
next_name(char *name)
{
    if (*name != '\0')
	++name;
    return name;
}

static char *
name_ending(char *name)
{
    if (*name == '\0') {
	name = 0;
    } else {
	while (*name != '\0' && *name != '|')
	    ++name;
    }
    return name;
}

/*
 * Essentially, find the conflict reported in check_collisions() and remove
 * it from the second name, unless that happens to be the last alias.
 */
static bool
remove_collision(char *n1, char *n2)
{
    char *p2 = n2;
    char *pstart, *qstart, *pend, *qend;
    bool removed = FALSE;

#if NCURSES_USE_TERMCAP && NCURSES_XNAMES
    if ((_nc_syntax == SYN_TERMCAP) && _nc_user_definable) {
	n1 = skip_index(n1);
	p2 = n2 = skip_index(n2);
    }
#endif

    for (pstart = n1; (pend = name_ending(pstart)); pstart = next_name(pend)) {
	for (qstart = n2; (qend = name_ending(qstart)); qstart = next_name(qend)) {
	    if ((pend - pstart == qend - qstart)
		&& memcmp(pstart, qstart, (size_t) (pend - pstart)) == 0) {
		if (qstart != p2 || *qend == '|') {
		    if (*qend == '|')
			++qend;
		    while ((*qstart++ = *qend++) != '\0') ;
		    fprintf(stderr, "...now\t%s\n", p2);
		    removed = TRUE;
		} else {
		    fprintf(stderr, "Cannot remove alias '%.*s'\n",
			    (int) (qend - qstart), qstart);
		}
		break;
	    }
	}
    }

    return removed;
}

/* do any of the aliases in a pair of terminal names match? */
NCURSES_EXPORT(bool)
_nc_entry_match(char *n1, char *n2)
{
    return check_collisions(n1, n2, 0);
}

/****************************************************************************
 *
 * Entry compiler and resolution logic
 *
 ****************************************************************************/

NCURSES_EXPORT(void)
_nc_read_entry_source(FILE *fp, char *buf,
		      int literal, bool silent,
		      bool(*hook) (ENTRY *))
/* slurp all entries in the given file into core */
{
    ENTRY thisentry;
    bool oldsuppress = _nc_suppress_warnings;
    int immediate = 0;

    DEBUG(2,
	  (T_CALLED("_nc_read_entry_source("
		    "file=%p, buf=%p, literal=%d, silent=%d, hook=%#"
		    PRIxPTR ")"),
	   (void *) fp, buf, literal, silent, CASTxPTR(hook)));

    if (silent)
	_nc_suppress_warnings = TRUE;	/* shut the lexer up, too */

    _nc_reset_input(fp, buf);
    for (;;) {
	memset(&thisentry, 0, sizeof(thisentry));
	if (_nc_parse_entry(&thisentry, literal, silent) == ERR)
	    break;
	if (!isalnum(UChar(thisentry.tterm.term_names[0])))
	    _nc_err_abort("terminal names must start with letter or digit");

	/*
	 * This can be used for immediate compilation of entries with no "use="
	 * references to disk.  That avoids consuming a lot of memory when the
	 * resolution code could fetch entries off disk.
	 */
	if (hook != NULLHOOK && (*hook) (&thisentry)) {
	    immediate++;
	} else {
	    enqueue(&thisentry);
	    /*
	     * The enqueued entry is copied with _nc_copy_termtype(), so we can
	     * free some of the data from thisentry, i.e., the arrays.
	     */
	    FreeIfNeeded(thisentry.tterm.Booleans);
	    FreeIfNeeded(thisentry.tterm.Numbers);
	    FreeIfNeeded(thisentry.tterm.Strings);
	    FreeIfNeeded(thisentry.tterm.str_table);
#if NCURSES_XNAMES
	    FreeIfNeeded(thisentry.tterm.ext_Names);
	    FreeIfNeeded(thisentry.tterm.ext_str_table);
#endif
	}
    }

    if (_nc_tail) {
	/* set up the head pointer */
	for (_nc_head = _nc_tail; _nc_head->last; _nc_head = _nc_head->last) {
	    /* EMPTY */ ;
	}

	DEBUG(2, ("head = %s", _nc_head->tterm.term_names));
	DEBUG(2, ("tail = %s", _nc_tail->tterm.term_names));
    }
#ifdef TRACE
    else if (!immediate)
	DEBUG(2, ("no entries parsed"));
#endif

    _nc_suppress_warnings = oldsuppress;
    DEBUG(2, (T_RETURN("")));
}

#if 0 && NCURSES_XNAMES
static unsigned
find_capname(TERMTYPE2 *p, const char *name)
{
    unsigned num_names = NUM_EXT_NAMES(p);
    unsigned n;
    if (name != 0) {
	for (n = 0; n < num_names; ++n) {
	    if (!strcmp(p->ext_Names[n], name))
		break;
	}
    } else {
	n = num_names + 1;
    }
    return n;
}

static int
extended_captype(TERMTYPE2 *p, unsigned which)
{
    int result = UNDEF;
    unsigned limit = 0;
    limit += p->ext_Booleans;
    if (limit != 0 && which < limit) {
	result = BOOLEAN;
    } else {
	limit += p->ext_Numbers;
	if (limit != 0 && which < limit) {
	    result = NUMBER;
	} else {
	    limit += p->ext_Strings;
	    if (limit != 0 && which < limit) {
		result = ((p->Strings[STRCOUNT + which] != CANCELLED_STRING)
			  ? STRING
			  : CANCEL);
	    } else if (which >= limit) {
		result = CANCEL;
	    }
	}
    }
    return result;
}

static const char *
name_of_captype(int which)
{
    const char *result = "?";
    switch (which) {
    case BOOLEAN:
	result = "boolean";
	break;
    case NUMBER:
	result = "number";
	break;
    case STRING:
	result = "string";
	break;
    }
    return result;
}

#define valid_TERMTYPE2(p) \
	((p) != 0 && \
	 (p)->term_names != 0 && \
	 (p)->ext_Names != 0)

/*
 * Disallow changing the type of an extended capability when doing a "use"
 * if one or the other is a string.
 */
static int
invalid_merge(TERMTYPE2 *to, TERMTYPE2 *from)
{
    int rc = FALSE;
    if (valid_TERMTYPE2(to)
	&& valid_TERMTYPE2(from)) {
	char *to_name = _nc_first_name(to->term_names);
	char *from_name = strdup(_nc_first_name(from->term_names));
	unsigned num_names = NUM_EXT_NAMES(from);
	unsigned n;

	for (n = 0; n < num_names; ++n) {
	    const char *capname = from->ext_Names[n];
	    int tt = extended_captype(to, find_capname(to, capname));
	    int tf = extended_captype(from, n);

	    if (tt <= STRING
		&& tf <= STRING
		&& (tt == STRING) != (tf == STRING)) {
		if (from_name != 0 && strcmp(to_name, from_name)) {
		    _nc_warning("merge of %s to %s changes type of %s from %s to %s",
				from_name,
				to_name,
				from->ext_Names[n],
				name_of_captype(tf),
				name_of_captype(tt));
		} else {
		    _nc_warning("merge of %s changes type of %s from %s to %s",
				to_name,
				from->ext_Names[n],
				name_of_captype(tf),
				name_of_captype(tt));
		}
		rc = TRUE;
	    }
	}
	free(from_name);
    }
    return rc;
}
#define validate_merge(p, q) \
	if (invalid_merge(&((p)->tterm), &((q)->tterm))) \
		return FALSE
#else
#define validate_merge(p, q)	/* nothing */
#endif

NCURSES_EXPORT(int)
_nc_resolve_uses2(bool fullresolve, bool literal)
/* try to resolve all use capabilities */
{
    ENTRY *qp, *rp, *lastread = 0;
    bool keepgoing;
    unsigned i, j;
    int unresolved, total_unresolved, multiples;

    DEBUG(2, (T_CALLED("_nc_resolve_uses2")));

    /*
     * Check for multiple occurrences of the same name.
     */
    multiples = 0;
    for_entry_list(qp) {
	int matchcount = 0;

	for_entry_list2(rp, qp->next) {
	    if (qp > rp
		&& check_collisions(qp->tterm.term_names,
				    rp->tterm.term_names,
				    matchcount + 1)) {
		if (!matchcount++) {
		    (void) fprintf(stderr, "\t%s\n", rp->tterm.term_names);
		}
		(void) fprintf(stderr, "and\t%s\n", qp->tterm.term_names);
		if (!remove_collision(rp->tterm.term_names,
				      qp->tterm.term_names)) {
		    ++multiples;
		}
	    }
	}
    }
    if (multiples > 0) {
	DEBUG(2, (T_RETURN("false")));
	return (FALSE);
    }

    DEBUG(2, ("NO MULTIPLE NAME OCCURRENCES"));

    /*
     * First resolution stage: compute link pointers corresponding to names.
     */
    total_unresolved = 0;
    _nc_curr_col = -1;
    for_entry_list(qp) {
	unresolved = 0;
	for (i = 0; i < qp->nuses; i++) {
	    bool foundit;
	    char *child = _nc_first_name(qp->tterm.term_names);
	    char *lookfor = qp->uses[i].name;
	    long lookline = qp->uses[i].line;

	    if (lookfor == 0)
		continue;

	    foundit = FALSE;

	    _nc_set_type(child);

	    /* first, try to resolve from in-core records */
	    for_entry_list(rp) {
		if (rp != qp
		    && _nc_name_match(rp->tterm.term_names, lookfor, "|")) {
		    DEBUG(2, ("%s: resolving use=%s %p (in core)",
			      child, lookfor, lookfor));

		    qp->uses[i].link = rp;
		    foundit = TRUE;

		    /* verify that there are no earlier uses */
		    for (j = 0; j < i; ++j) {
			if (qp->uses[j].link != NULL
			    && !strcmp(qp->uses[j].link->tterm.term_names,
				       rp->tterm.term_names)) {
			    _nc_warning("duplicate use=%s", lookfor);
			    break;
			}
		    }
		}
	    }

	    /* if that didn't work, try to merge in a compiled entry */
	    if (!foundit) {
		TERMTYPE2 thisterm;
		char filename[PATH_MAX];

		memset(&thisterm, 0, sizeof(thisterm));
		if (_nc_read_entry2(lookfor, filename, &thisterm) == 1) {
		    DEBUG(2, ("%s: resolving use=%s (compiled)",
			      child, lookfor));

		    TYPE_MALLOC(ENTRY, 1, rp);
		    rp->tterm = thisterm;
		    rp->nuses = 0;
		    rp->next = lastread;
		    lastread = rp;

		    qp->uses[i].link = rp;
		    foundit = TRUE;

		    /* verify that there are no earlier uses */
		    for (j = 0; j < i; ++j) {
			if (qp->uses[j].link != NULL
			    && !strcmp(qp->uses[j].link->tterm.term_names,
				       rp->tterm.term_names)) {
			    _nc_warning("duplicate use=%s", lookfor);
			    break;
			}
		    }
		}
	    }

	    /* no good, mark this one unresolvable and complain */
	    if (!foundit) {
		unresolved++;
		total_unresolved++;

		_nc_curr_line = (int) lookline;
		_nc_warning("resolution of use=%s failed", lookfor);
		qp->uses[i].link = 0;
	    }
	}
    }
    if (total_unresolved) {
	/* free entries read in off disk */
	_nc_free_entries(lastread);
	DEBUG(2, (T_RETURN("false")));
	return (FALSE);
    }

    DEBUG(2, ("NAME RESOLUTION COMPLETED OK"));

    /*
     * OK, at this point all (char *) references in `name' members
     * have been successfully converted to (ENTRY *) pointers in
     * `link' members.  Time to do the actual merges.
     */
    if (fullresolve) {
	do {
	    ENTRY merged;

	    keepgoing = FALSE;

	    for_entry_list(qp) {
		if (qp->nuses > 0) {
		    DEBUG(2, ("%s: attempting merge of %d entries",
			      _nc_first_name(qp->tterm.term_names),
			      qp->nuses));
		    /*
		     * If any of the use entries we're looking for is
		     * incomplete, punt.  We'll catch this entry on a
		     * subsequent pass.
		     */
		    for (i = 0; i < qp->nuses; i++) {
			if (qp->uses[i].link
			    && qp->uses[i].link->nuses) {
			    DEBUG(2, ("%s: use entry %d unresolved",
				      _nc_first_name(qp->tterm.term_names), i));
			    goto incomplete;
			}
		    }

		    /*
		     * First, make sure there is no garbage in the
		     * merge block.  As a side effect, copy into
		     * the merged entry the name field and string
		     * table pointer.
		     */
		    _nc_copy_termtype2(&(merged.tterm), &(qp->tterm));

		    /*
		     * Now merge in each use entry in the proper
		     * (reverse) order.
		     */
		    for (; qp->nuses; qp->nuses--) {
			int n = (int) (qp->nuses - 1);
			validate_merge(&merged, qp->uses[n].link);
			_nc_merge_entry(&merged, qp->uses[n].link);
			free(qp->uses[n].name);
		    }

		    /*
		     * Now merge in the original entry.
		     */
		    validate_merge(&merged, qp);
		    _nc_merge_entry(&merged, qp);

		    /*
		     * Replace the original entry with the merged one.
		     */
		    FreeIfNeeded(qp->tterm.Booleans);
		    FreeIfNeeded(qp->tterm.Numbers);
		    FreeIfNeeded(qp->tterm.Strings);
		    FreeIfNeeded(qp->tterm.str_table);
#if NCURSES_XNAMES
		    FreeIfNeeded(qp->tterm.ext_Names);
		    FreeIfNeeded(qp->tterm.ext_str_table);
#endif
		    qp->tterm = merged.tterm;
		    _nc_wrap_entry(qp, TRUE);

		    /*
		     * We know every entry is resolvable because name resolution
		     * didn't bomb.  So go back for another pass.
		     */
		    /* FALLTHRU */
		  incomplete:
		    keepgoing = TRUE;
		}
	    }
	} while
	    (keepgoing);

	DEBUG(2, ("MERGES COMPLETED OK"));
    }

    DEBUG(2, ("RESOLUTION FINISHED"));

    if (fullresolve) {
	_nc_curr_col = -1;
	for_entry_list(qp) {
	    _nc_curr_line = (int) qp->startline;
	    _nc_set_type(_nc_first_name(qp->tterm.term_names));
	    /*
	     * tic overrides this function pointer to provide more verbose
	     * checking.
	     */
	    if (_nc_check_termtype2 != sanity_check2) {
		SCREEN *save_SP = SP;
		SCREEN fake_sp;
		TERMINAL fake_tm;
		TERMINAL *save_tm = cur_term;

		/*
		 * Setup so that tic can use ordinary terminfo interface to
		 * obtain capability information.
		 */
		memset(&fake_sp, 0, sizeof(fake_sp));
		memset(&fake_tm, 0, sizeof(fake_tm));
		fake_sp._term = &fake_tm;
		TerminalType(&fake_tm) = qp->tterm;
		_nc_set_screen(&fake_sp);
		set_curterm(&fake_tm);

		_nc_check_termtype2(&qp->tterm, literal);

		/*
		 * Checking calls tparm, which can allocate memory.  Fix leaks.
		 */
#define TPS(name) fake_tm.tparm_state.name
		FreeAndNull(TPS(out_buff));
		FreeAndNull(TPS(fmt_buff));
#undef TPS

		_nc_set_screen(save_SP);
		set_curterm(save_tm);
	    } else {
		fixup_acsc(&qp->tterm, literal);
	    }
	}
	DEBUG(2, ("SANITY CHECK FINISHED"));
    }

    DEBUG(2, (T_RETURN("true")));
    return (TRUE);
}

/*
 * This bit of legerdemain turns all the terminfo variable names into
 * references to locations in the arrays Booleans, Numbers, and Strings ---
 * precisely what's needed.
 */

#undef CUR
#define CUR tp->

static void
fixup_acsc(TERMTYPE2 *tp, int literal)
{
    if (!literal) {
	if (acs_chars == ABSENT_STRING
	    && PRESENT(enter_alt_charset_mode)
	    && PRESENT(exit_alt_charset_mode))
	    acs_chars = strdup(VT_ACSC);
    }
}

static void
sanity_check2(TERMTYPE2 *tp, bool literal)
{
    if (!PRESENT(exit_attribute_mode)) {
#ifdef __UNUSED__		/* this casts too wide a net */
	bool terminal_entry = !strchr(tp->term_names, '+');
	if (terminal_entry &&
	    (PRESENT(set_attributes)
	     || PRESENT(enter_standout_mode)
	     || PRESENT(enter_underline_mode)
	     || PRESENT(enter_blink_mode)
	     || PRESENT(enter_bold_mode)
	     || PRESENT(enter_dim_mode)
	     || PRESENT(enter_secure_mode)
	     || PRESENT(enter_protected_mode)
	     || PRESENT(enter_reverse_mode)))
	    _nc_warning("no exit_attribute_mode");
#endif /* __UNUSED__ */
	PAIRED(enter_standout_mode, exit_standout_mode);
	PAIRED(enter_underline_mode, exit_underline_mode);
#if defined(enter_italics_mode) && defined(exit_italics_mode)
	PAIRED(enter_italics_mode, exit_italics_mode);
#endif
    }

    /* we do this check/fix in postprocess_termcap(), but some packagers
     * prefer to bypass it...
     */
    if (!literal) {
	fixup_acsc(tp, literal);
	ANDMISSING(enter_alt_charset_mode, acs_chars);
	ANDMISSING(exit_alt_charset_mode, acs_chars);
    }

    /* listed in structure-member order of first argument */
    PAIRED(enter_alt_charset_mode, exit_alt_charset_mode);
    ANDMISSING(enter_blink_mode, exit_attribute_mode);
    ANDMISSING(enter_bold_mode, exit_attribute_mode);
    PAIRED(exit_ca_mode, enter_ca_mode);
    PAIRED(enter_delete_mode, exit_delete_mode);
    ANDMISSING(enter_dim_mode, exit_attribute_mode);
    PAIRED(enter_insert_mode, exit_insert_mode);
    ANDMISSING(enter_secure_mode, exit_attribute_mode);
    ANDMISSING(enter_protected_mode, exit_attribute_mode);
    ANDMISSING(enter_reverse_mode, exit_attribute_mode);
    PAIRED(from_status_line, to_status_line);
    PAIRED(meta_off, meta_on);

    PAIRED(prtr_on, prtr_off);
    PAIRED(save_cursor, restore_cursor);
    PAIRED(enter_xon_mode, exit_xon_mode);
    PAIRED(enter_am_mode, exit_am_mode);
    ANDMISSING(label_off, label_on);
#if defined(display_clock) && defined(remove_clock)
    PAIRED(display_clock, remove_clock);
#endif
    ANDMISSING(set_color_pair, initialize_pair);
}

#if NO_LEAKS
NCURSES_EXPORT(void)
_nc_leaks_tic(void)
{
    T((T_CALLED("_nc_leaks_tic()")));
    _nc_globals.leak_checking = TRUE;
    _nc_alloc_entry_leaks();
    _nc_captoinfo_leaks();
    _nc_comp_scan_leaks();
#if BROKEN_LINKER || USE_REENTRANT
    _nc_names_leaks();
    _nc_codes_leaks();
#endif
    _nc_tic_expand(0, FALSE, 0);
    T((T_RETURN("")));
}

NCURSES_EXPORT(void)
_nc_free_tic(int code)
{
    T((T_CALLED("_nc_free_tic(%d)"), code));
    _nc_leaks_tic();
    exit_terminfo(code);
}
#endif
