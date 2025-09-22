/****************************************************************************
 * Copyright 2020 Thomas E. Dickey                                          *
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
 *  Author: Thomas E. Dickey                                                *
 ****************************************************************************/

#include <curses.priv.h>
#include <tic.h>

MODULE_ID("$Id: report_hashing.c,v 1.1 2023/10/17 09:52:08 nicm Exp $")

static void
check_names(const char *name, NCURSES_CONST char *const *table, int termcap)
{
    int errs = 0;
    int n;
    struct name_table_entry const *entry_ptr;
    const HashValue *hash_table = _nc_get_hash_table(termcap);

    printf("%s:\n", name);
    for (n = 0; table[n] != NULL; ++n) {
	entry_ptr = _nc_find_entry(table[n], hash_table);
	if (entry_ptr == 0) {
	    printf("  %s\n", table[n]);
	    errs++;
	}
    }
    if (errs)
	printf("%d errors\n", errs);
}

int
main(void)
{
#define CHECK_TI(name) check_names(#name, name, 0)
#define CHECK_TC(name) check_names(#name, name, 1)

    CHECK_TI(boolnames);
    CHECK_TI(numnames);
    CHECK_TI(strnames);

    CHECK_TC(boolcodes);
    CHECK_TC(numcodes);
    CHECK_TC(strcodes);

    return EXIT_SUCCESS;
}
