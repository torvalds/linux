/*	$OpenBSD: frm_win.c,v 1.9 2023/10/17 09:52:10 nicm Exp $	*/
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

#include "form.priv.h"

MODULE_ID("$Id: frm_win.c,v 1.9 2023/10/17 09:52:10 nicm Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int set_form_win(FORM *form,WINDOW *win)
|
|   Description   :  Set the window of the form to win.
|
|   Return Values :  E_OK       - success
|                    E_POSTED   - form is posted
+--------------------------------------------------------------------------*/
FORM_EXPORT(int)
set_form_win(FORM *form, WINDOW *win)
{
  T((T_CALLED("set_form_win(%p,%p)"), (void *)form, (void *)win));

  if (form && (form->status & _POSTED))
    RETURN(E_POSTED);
  else
    {
#if NCURSES_SP_FUNCS
      FORM *f = Normalize_Form(form);

      f->win = win ? win : StdScreen(Get_Form_Screen(f));
      RETURN(E_OK);
#else
      Normalize_Form(form)->win = win;
      RETURN(E_OK);
#endif
    }
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  WINDOW *form_win(const FORM *)
|
|   Description   :  Retrieve the window of the form.
|
|   Return Values :  The pointer to the Window or stdscr if there is none.
+--------------------------------------------------------------------------*/
FORM_EXPORT(WINDOW *)
form_win(const FORM *form)
{
  WINDOW *result;
  const FORM *f;

  T((T_CALLED("form_win(%p)"), (const void *)form));

  f = Normalize_Form(form);
#if NCURSES_SP_FUNCS
  result = (f->win ? f->win : StdScreen(Get_Form_Screen(f)));
#else
  result = (f->win ? f->win : stdscr);
#endif
  returnWin(result);
}

/* frm_win.c ends here */
