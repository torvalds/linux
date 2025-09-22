/****************************************************************************
 * Copyright 2018,2020 Thomas E. Dickey                                     *
 * Copyright 2009-2012,2014 Free Software Foundation, Inc.                  *
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
 *  Author: Juergen Pfeifer                                                 *
 *                                                                          *
 ****************************************************************************/

#include <curses.priv.h>

MODULE_ID("$Id: lib_driver.c,v 1.1 2023/10/17 09:52:08 nicm Exp $")

#ifndef EXP_WIN32_DRIVER
typedef struct DriverEntry {
    const char *name;
    TERM_DRIVER *driver;
} DRIVER_ENTRY;

static DRIVER_ENTRY DriverTable[] =
{
#ifdef _WIN32
    {"win32console", &_nc_WIN_DRIVER},
#endif
    {"tinfo", &_nc_TINFO_DRIVER}	/* must be last */
};

NCURSES_EXPORT(int)
_nc_get_driver(TERMINAL_CONTROL_BLOCK * TCB, const char *name, int *errret)
{
    int code = ERR;
    size_t i;
    TERM_DRIVER *res = (TERM_DRIVER *) 0;
    TERM_DRIVER *use = 0;

    T((T_CALLED("_nc_get_driver(%p, %s, %p)"),
       (void *) TCB, NonNull(name), (void *) errret));

    assert(TCB != 0);

    for (i = 0; i < SIZEOF(DriverTable); i++) {
	res = DriverTable[i].driver;
	if (strcmp(DriverTable[i].name, res->td_name(TCB)) == 0) {
	    if (res->td_CanHandle(TCB, name, errret)) {
		use = res;
		break;
	    }
	}
    }
    if (use != 0) {
	TCB->drv = use;
	code = OK;
    }
    returnCode(code);
}
#endif /* !EXP_WIN32_DRIVER */

NCURSES_EXPORT(int)
NCURSES_SP_NAME(has_key) (SCREEN *sp, int keycode)
{
    T((T_CALLED("has_key(%p, %d)"), (void *) sp, keycode));
    returnCode(IsValidTIScreen(sp) ? CallDriver_1(sp, td_kyExist, keycode) : FALSE);
}

NCURSES_EXPORT(int)
has_key(int keycode)
{
    return NCURSES_SP_NAME(has_key) (CURRENT_SCREEN, keycode);
}

NCURSES_EXPORT(int)
NCURSES_SP_NAME(_nc_mcprint) (SCREEN *sp, char *data, int len)
{
    int code = ERR;

    if (0 != TerminalOf(sp))
	code = CallDriver_2(sp, td_print, data, len);
    return (code);
}

NCURSES_EXPORT(int)
mcprint(char *data, int len)
{
    return NCURSES_SP_NAME(_nc_mcprint) (CURRENT_SCREEN, data, len);
}

NCURSES_EXPORT(int)
NCURSES_SP_NAME(doupdate) (SCREEN *sp)
{
    int code = ERR;

    T((T_CALLED("doupdate(%p)"), (void *) sp));

    if (IsValidScreen(sp))
	code = CallDriver(sp, td_update);

    returnCode(code);
}

NCURSES_EXPORT(int)
doupdate(void)
{
    return NCURSES_SP_NAME(doupdate) (CURRENT_SCREEN);
}

NCURSES_EXPORT(int)
NCURSES_SP_NAME(mvcur) (SCREEN *sp, int yold, int xold, int ynew, int xnew)
{
    int code = ERR;
    TR(TRACE_CALLS | TRACE_MOVE, (T_CALLED("mvcur(%p,%d,%d,%d,%d)"),
				  (void *) sp, yold, xold, ynew, xnew));
    if (HasTerminal(sp)) {
	code = CallDriver_4(sp, td_hwcur, yold, xold, ynew, xnew);
    }
    returnCode(code);
}

NCURSES_EXPORT(int)
mvcur(int yold, int xold, int ynew, int xnew)
/* optimized cursor move from (yold, xold) to (ynew, xnew) */
{
    return NCURSES_SP_NAME(mvcur) (CURRENT_SCREEN, yold, xold, ynew, xnew);
}
