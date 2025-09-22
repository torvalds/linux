/*	$OpenBSD: fld_info.c,v 1.7 2023/10/17 09:52:10 nicm Exp $	*/
/****************************************************************************
 * Copyright 2020,2021 Thomas E. Dickey                                     *
 * Copyright 1998-2004,2010 Free Software Foundation, Inc.                  *
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

MODULE_ID("$Id: fld_info.c,v 1.7 2023/10/17 09:52:10 nicm Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int field_info(const FIELD *field,
|                                   int *rows, int *cols,
|                                   int *frow, int *fcol,
|                                   int *nrow, int *nbuf)
|
|   Description   :  Retrieve information about the field's creation parameters.
|
|   Return Values :  E_OK           - success
|                    E_BAD_ARGUMENT - invalid field pointer
+--------------------------------------------------------------------------*/
FORM_EXPORT(int)
field_info(const FIELD *field,
	   int *rows, int *cols,
	   int *frow, int *fcol,
	   int *nrow, int *nbuf)
{
  T((T_CALLED("field_info(%p,%p,%p,%p,%p,%p,%p)"),
     (const void *)field,
     (void *)rows, (void *)cols,
     (void *)frow, (void *)fcol,
     (void *)nrow, (void *)nbuf));

  if (!field)
    RETURN(E_BAD_ARGUMENT);

  if (rows)
    *rows = field->rows;
  if (cols)
    *cols = field->cols;
  if (frow)
    *frow = field->frow;
  if (fcol)
    *fcol = field->fcol;
  if (nrow)
    *nrow = field->nrow;
  if (nbuf)
    *nbuf = field->nbuf;
  RETURN(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int dynamic_field_info(const FIELD *field,
|                                           int *drows, int *dcols,
|                                           int *maxgrow)
|
|   Description   :  Retrieve information about a dynamic field's current
|                    dynamic parameters.
|
|   Return Values :  E_OK           - success
|                    E_BAD_ARGUMENT - invalid argument
+--------------------------------------------------------------------------*/
FORM_EXPORT(int)
dynamic_field_info(const FIELD *field, int *drows, int *dcols, int *maxgrow)
{
  T((T_CALLED("dynamic_field_info(%p,%p,%p,%p)"),
     (const void *)field,
     (void *)drows,
     (void *)dcols,
     (void *)maxgrow));

  if (!field)
    RETURN(E_BAD_ARGUMENT);

  if (drows)
    *drows = field->drows;
  if (dcols)
    *dcols = field->dcols;
  if (maxgrow)
    *maxgrow = field->maxgrow;

  RETURN(E_OK);
}

/* fld_info.c ends here */
