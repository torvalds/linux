/* $OpenBSD: alloc_entry.c,v 1.8 2023/10/17 09:52:09 nicm Exp $ */

/****************************************************************************
 * Copyright 2018-2021,2022 Thomas E. Dickey                                *
 * Copyright 1998-2013,2017 Free Software Foundation, Inc.                  *
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
 * alloc_entry.c -- allocation functions for terminfo entries
 *
 *	_nc_copy_entry()
 *	_nc_init_entry()
 *	_nc_merge_entry()
 *	_nc_save_str()
 *	_nc_wrap_entry()
 *
 */

#include <curses.priv.h>

#include <tic.h>

MODULE_ID("$Id: alloc_entry.c,v 1.8 2023/10/17 09:52:09 nicm Exp $")

#define ABSENT_OFFSET    -1
#define CANCELLED_OFFSET -2

static char *stringbuf;		/* buffer for string capabilities */
static size_t next_free;	/* next free character in stringbuf */

NCURSES_EXPORT(void)
_nc_init_entry(ENTRY * const tp)
/* initialize a terminal type data block */
{
    DEBUG(2, (T_CALLED("_nc_init_entry(tp=%p)"), (void *) tp));

    if (tp == NULL) {
#if NO_LEAKS
	if (stringbuf != NULL) {
	    FreeAndNull(stringbuf);
	}
	return;
#else
	_nc_err_abort("_nc_init_entry called without initialization");
#endif
    }

    if (stringbuf == NULL)
	TYPE_CALLOC(char, (size_t) MAX_ENTRY_SIZE, stringbuf);

    next_free = 0;

    _nc_init_termtype(&(tp->tterm));

    DEBUG(2, (T_RETURN("")));
}

NCURSES_EXPORT(ENTRY *)
_nc_copy_entry(ENTRY * oldp)
{
    ENTRY *newp;

    DEBUG(2, (T_CALLED("_nc_copy_entry(oldp=%p)"), (void *) oldp));

    newp = typeCalloc(ENTRY, 1);
    if (newp != NULL) {
	*newp = *oldp;
	_nc_copy_termtype2(&(newp->tterm), &(oldp->tterm));
    }

    DEBUG(2, (T_RETURN("%p"), (void *) newp));
    return (newp);
}

/* save a copy of string in the string buffer */
NCURSES_EXPORT(char *)
_nc_save_str(const char *string)
{
    char *result = 0;
    size_t old_next_free = next_free;

    if (stringbuf != NULL) {
	size_t len;

	if (!VALID_STRING(string))
	    string = "";
	len = strlen(string) + 1;

	if (len == 1 && next_free != 0) {
	    /*
	     * Cheat a little by making an empty string point to the end of the
	     * previous string.
	     */
	    if (next_free < MAX_ENTRY_SIZE) {
		result = (stringbuf + next_free - 1);
	    }
	} else if (next_free + len < MAX_ENTRY_SIZE) {
	    _nc_STRCPY(&stringbuf[next_free], string, MAX_ENTRY_SIZE);
	    DEBUG(7, ("Saved string %s", _nc_visbuf(string)));
	    DEBUG(7, ("at location %d", (int) next_free));
	    next_free += len;
	    result = (stringbuf + old_next_free);
	} else {
	    _nc_warning("Too much data, some is lost: %s", string);
	}
    }
    return result;
}

NCURSES_EXPORT(void)
_nc_wrap_entry(ENTRY * const ep, bool copy_strings)
/* copy the string parts to allocated storage, preserving pointers to it */
{
    int offsets[MAX_ENTRY_SIZE / sizeof(short)];
    int useoffsets[MAX_USES];
    unsigned i, n;
    unsigned nuses;
    TERMTYPE2 *tp;

    DEBUG(2, (T_CALLED("_nc_wrap_entry(ep=%p, copy_strings=%d)"), (void *)
	      ep, copy_strings));
    if (ep == NULL || stringbuf == NULL)
	_nc_err_abort("_nc_wrap_entry called without initialization");

    nuses = ep->nuses;
    tp = &(ep->tterm);
    if (copy_strings) {
	next_free = 0;		/* clear static storage */

	/* copy term_names, Strings, uses */
	tp->term_names = _nc_save_str(tp->term_names);
	for_each_string(i, tp) {
	    if (tp->Strings[i] != ABSENT_STRING &&
		tp->Strings[i] != CANCELLED_STRING) {
		tp->Strings[i] = _nc_save_str(tp->Strings[i]);
	    }
	}

	for (i = 0; i < nuses; i++) {
	    if (ep->uses[i].name == 0) {
		ep->uses[i].name = _nc_save_str(ep->uses[i].name);
	    }
	}

	free(tp->str_table);
    }

    assert(tp->term_names >= stringbuf);
    n = (unsigned) (tp->term_names - stringbuf);
    for_each_string(i, &(ep->tterm)) {
	if (i < SIZEOF(offsets)) {
	    if (tp->Strings[i] == ABSENT_STRING) {
		offsets[i] = ABSENT_OFFSET;
	    } else if (tp->Strings[i] == CANCELLED_STRING) {
		offsets[i] = CANCELLED_OFFSET;
	    } else {
		offsets[i] = (int) (tp->Strings[i] - stringbuf);
	    }
	}
    }

    for (i = 0; i < nuses; i++) {
	if (ep->uses[i].name == 0)
	    useoffsets[i] = ABSENT_OFFSET;
	else
	    useoffsets[i] = (int) (ep->uses[i].name - stringbuf);
    }

    TYPE_MALLOC(char, next_free, tp->str_table);
    (void) memcpy(tp->str_table, stringbuf, next_free);

    tp->term_names = tp->str_table + n;
    for_each_string(i, &(ep->tterm)) {
	if (i < SIZEOF(offsets)) {
	    if (offsets[i] == ABSENT_OFFSET) {
		tp->Strings[i] = ABSENT_STRING;
	    } else if (offsets[i] == CANCELLED_OFFSET) {
		tp->Strings[i] = CANCELLED_STRING;
	    } else {
		tp->Strings[i] = tp->str_table + offsets[i];
	    }
	}
    }

#if NCURSES_XNAMES
    if (!copy_strings) {
	if ((n = (unsigned) NUM_EXT_NAMES(tp)) != 0) {
	    if (n < SIZEOF(offsets)) {
		size_t length = 0;
		size_t offset;
		for (i = 0; i < n; i++) {
		    length += strlen(tp->ext_Names[i]) + 1;
		    offsets[i] = (int) (tp->ext_Names[i] - stringbuf);
		}
		TYPE_MALLOC(char, length, tp->ext_str_table);
		for (i = 0, offset = 0; i < n; i++) {
		    tp->ext_Names[i] = tp->ext_str_table + offset;
		    _nc_STRCPY(tp->ext_Names[i],
			       stringbuf + offsets[i],
			       length - offset);
		    offset += strlen(tp->ext_Names[i]) + 1;
		}
	    }
	}
    }
#endif

    for (i = 0; i < nuses; i++) {
	if (useoffsets[i] == ABSENT_OFFSET) {
	    ep->uses[i].name = 0;
	} else {
	    ep->uses[i].name = strdup(tp->str_table + useoffsets[i]);
	}
    }
    DEBUG(2, (T_RETURN("")));
}

NCURSES_EXPORT(void)
_nc_merge_entry(ENTRY * const target, ENTRY * const source)
/* merge capabilities from `from' entry into `to' entry */
{
    TERMTYPE2 *to = &(target->tterm);
    TERMTYPE2 *from = &(source->tterm);
#if NCURSES_XNAMES
    TERMTYPE2 copy;
    size_t str_size, copy_size;
    char *str_table;
#endif
    unsigned i;

    if (source == 0 || from == 0 || target == 0 || to == 0)
	return;

#if NCURSES_XNAMES
    _nc_copy_termtype2(&copy, from);
    from = &copy;
    _nc_align_termtype(to, from);
    /*
     * compute the maximum size of the string-table.
     */
    str_size = strlen(to->term_names) + 1;
    for_each_string(i, from) {
	if (VALID_STRING(from->Strings[i]))
	    str_size += strlen(from->Strings[i]) + 1;
    }
    for_each_string(i, to) {
	if (VALID_STRING(to->Strings[i]))
	    str_size += strlen(to->Strings[i]) + 1;
    }
    /* allocate a string-table large enough for both source/target, and
     * copy all of the strings into that table.  In the merge, we will
     * select from the original source/target lists to construct a new
     * target list.
     */
    if (str_size != 0) {
	char *str_copied;
	if ((str_table = malloc(str_size)) == NULL)
	    _nc_err_abort(MSG_NO_MEMORY);
	str_copied = str_table;
	_nc_STRCPY(str_copied, to->term_names, str_size);
	to->term_names = str_copied;
	copy_size = strlen(str_copied) + 1;
	str_copied += copy_size;
	str_size -= copy_size;
	for_each_string(i, from) {
	    if (VALID_STRING(from->Strings[i])) {
		_nc_STRCPY(str_copied, from->Strings[i], str_size);
		from->Strings[i] = str_copied;
		copy_size = strlen(str_copied) + 1;
		str_copied += copy_size;
		str_size -= copy_size;
	    }
	}
	for_each_string(i, to) {
	    if (VALID_STRING(to->Strings[i])) {
		_nc_STRCPY(str_copied, to->Strings[i], str_size);
		to->Strings[i] = str_copied;
		copy_size = strlen(str_copied) + 1;
		str_copied += copy_size;
		str_size -= copy_size;
	    }
	}
	free(to->str_table);
	to->str_table = str_table;
	free(from->str_table);
    }
    /*
     * Do the same for the extended-strings (i.e., lists of capabilities).
     */
    str_size = 0;
    for (i = 0; i < NUM_EXT_NAMES(from); ++i) {
	if (VALID_STRING(from->ext_Names[i]))
	    str_size += strlen(from->ext_Names[i]) + 1;
    }
    for (i = 0; i < NUM_EXT_NAMES(to); ++i) {
	if (VALID_STRING(to->ext_Names[i]))
	    str_size += strlen(to->ext_Names[i]) + 1;
    }
    /* allocate a string-table large enough for both source/target, and
     * copy all of the strings into that table.  In the merge, we will
     * select from the original source/target lists to construct a new
     * target list.
     */
    if (str_size != 0) {
	char *str_copied;
	if ((str_table = malloc(str_size)) == NULL)
	    _nc_err_abort(MSG_NO_MEMORY);
	str_copied = str_table;
	for (i = 0; i < NUM_EXT_NAMES(from); ++i) {
	    if (VALID_STRING(from->ext_Names[i])) {
		_nc_STRCPY(str_copied, from->ext_Names[i], str_size);
		from->ext_Names[i] = str_copied;
		copy_size = strlen(str_copied) + 1;
		str_copied += copy_size;
		str_size -= copy_size;
	    }
	}
	for (i = 0; i < NUM_EXT_NAMES(to); ++i) {
	    if (VALID_STRING(to->ext_Names[i])) {
		_nc_STRCPY(str_copied, to->ext_Names[i], str_size);
		to->ext_Names[i] = str_copied;
		copy_size = strlen(str_copied) + 1;
		str_copied += copy_size;
		str_size -= copy_size;
	    }
	}
	free(to->ext_str_table);
	to->ext_str_table = str_table;
	free(from->ext_str_table);
    }
#endif
    for_each_boolean(i, from) {
	if (to->Booleans[i] != (NCURSES_SBOOL) CANCELLED_BOOLEAN) {
	    int mergebool = from->Booleans[i];

	    if (mergebool == CANCELLED_BOOLEAN)
		to->Booleans[i] = FALSE;
	    else if (mergebool == TRUE)
		to->Booleans[i] = (NCURSES_SBOOL) mergebool;
	}
    }

    for_each_number(i, from) {
	if (to->Numbers[i] != CANCELLED_NUMERIC) {
	    int mergenum = from->Numbers[i];

	    if (mergenum == CANCELLED_NUMERIC)
		to->Numbers[i] = ABSENT_NUMERIC;
	    else if (mergenum != ABSENT_NUMERIC)
		to->Numbers[i] = (NCURSES_INT2) mergenum;
	}
    }

    /*
     * Note: the copies of strings this makes don't have their own
     * storage.  This is OK right now, but will be a problem if we
     * we ever want to deallocate entries.
     */
    for_each_string(i, from) {
	if (to->Strings[i] != CANCELLED_STRING) {
	    char *mergestring = from->Strings[i];

	    if (mergestring == CANCELLED_STRING)
		to->Strings[i] = ABSENT_STRING;
	    else if (mergestring != ABSENT_STRING)
		to->Strings[i] = mergestring;
	}
    }
#if NCURSES_XNAMES
    /* cleanup */
    free(copy.Booleans);
    free(copy.Numbers);
    free(copy.Strings);
    free(copy.ext_Names);
#endif
}

#if NO_LEAKS
NCURSES_EXPORT(void)
_nc_alloc_entry_leaks(void)
{
    if (stringbuf != NULL) {
	FreeAndNull(stringbuf);
    }
    next_free = 0;
}
#endif
