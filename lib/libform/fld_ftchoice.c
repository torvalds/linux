/*	$OpenBSD: fld_ftchoice.c,v 1.7 2023/10/17 09:52:10 nicm Exp $	*/
/****************************************************************************
 * Copyright 2018-2020,2021 Thomas E. Dickey                                *
 * Copyright 1998-2012,2016 Free Software Foundation, Inc.                  *
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

MODULE_ID("$Id: fld_ftchoice.c,v 1.7 2023/10/17 09:52:10 nicm Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int set_fieldtype_choice(
|                          FIELDTYPE *typ,
|                          bool (* const next_choice)(FIELD *,const void *),
|                          bool (* const prev_choice)(FIELD *,const void *))
|
|   Description   :  Define implementation of enumeration requests.
|
|   Return Values :  E_OK           - success
|                    E_BAD_ARGUMENT - invalid arguments
+--------------------------------------------------------------------------*/
FORM_EXPORT(int)
set_fieldtype_choice(FIELDTYPE *typ,
		     bool (*const next_choice) (FIELD *, const void *),
		     bool (*const prev_choice) (FIELD *, const void *))
{
  TR_FUNC_BFR(2);

  T((T_CALLED("set_fieldtype_choice(%p,%s,%s)"),
     (void *)typ,
     TR_FUNC_ARG(0, next_choice),
     TR_FUNC_ARG(1, prev_choice)));

  if (!typ || !next_choice || !prev_choice)
    RETURN(E_BAD_ARGUMENT);

  SetStatus(typ, _HAS_CHOICE);
#if NCURSES_INTEROP_FUNCS
  typ->enum_next.onext = next_choice;
  typ->enum_prev.oprev = prev_choice;
#else
  typ->next = next_choice;
  typ->prev = prev_choice;
#endif
  RETURN(E_OK);
}

/* fld_ftchoice.c ends here */
