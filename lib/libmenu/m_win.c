/* $OpenBSD: m_win.c,v 1.8 2023/10/17 09:52:10 nicm Exp $ */

/****************************************************************************
 * Copyright 2020,2021 Thomas E. Dickey                                     *
 * Copyright 1998-2009,2010 Free Software Foundation, Inc.                  *
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
 *   Author:  Juergen Pfeifer, 1995,1997                                    *
 ****************************************************************************/

/***************************************************************************
* Module m_win                                                             *
* Menus window association routines                                        *
***************************************************************************/

#include "menu.priv.h"

MODULE_ID("$Id: m_win.c,v 1.8 2023/10/17 09:52:10 nicm Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu
|   Function      :  int set_menu_win(MENU *menu, WINDOW *win)
|
|   Description   :  Sets the window of the menu.
|
|   Return Values :  E_OK               - success
|                    E_POSTED           - menu is already posted
+--------------------------------------------------------------------------*/
MENU_EXPORT(int)
set_menu_win(MENU *menu, WINDOW *win)
{
  T((T_CALLED("set_menu_win(%p,%p)"), (void *)menu, (void *)win));

  if (menu)
    {
      if (menu->status & _POSTED)
	RETURN(E_POSTED);
      else
#if NCURSES_SP_FUNCS
	{
	  /* We ensure that userwin is never null. So even if a null
	     WINDOW parameter is passed, we store the SCREENS stdscr.
	     The only MENU that can have a null userwin is the static
	     _nc_default_Menu.
	   */
	  SCREEN *sp = _nc_screen_of(menu->userwin);

	  menu->userwin = win ? win : sp->_stdscr;
	  _nc_Calculate_Item_Length_and_Width(menu);
	}
#else
	menu->userwin = win;
#endif
    }
  else
    _nc_Default_Menu.userwin = win;

  RETURN(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu
|   Function      :  WINDOW* menu_win(const MENU*)
|
|   Description   :  Returns pointer to the window of the menu
|
|   Return Values :  NULL on error, otherwise pointer to window
+--------------------------------------------------------------------------*/
MENU_EXPORT(WINDOW *)
menu_win(const MENU *menu)
{
  const MENU *m = Normalize_Menu(menu);

  T((T_CALLED("menu_win(%p)"), (const void *)menu));
  returnWin(Get_Menu_UserWin(m));
}

/* m_win.c ends here */
