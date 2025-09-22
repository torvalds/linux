/*	$OpenBSD: fld_arg.c,v 1.7 2023/10/17 09:52:10 nicm Exp $	*/
/****************************************************************************
 * Copyright 2018,2020 Thomas E. Dickey                                     *
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

MODULE_ID("$Id: fld_arg.c,v 1.7 2023/10/17 09:52:10 nicm Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int set_fieldtype_arg(
|                            FIELDTYPE *typ,
|                            void * (* const make_arg)(va_list *),
|                            void * (* const copy_arg)(const void *),
|                            void   (* const free_arg)(void *) )
|
|   Description   :  Connects to the type additional arguments necessary
|                    for a set_field_type call. The various function pointer
|                    arguments are:
|                       make_arg : allocates a structure for the field
|                                  specific parameters.
|                       copy_arg : duplicate the structure created by
|                                  make_arg
|                       free_arg : Release the memory allocated by make_arg
|                                  or copy_arg
|
|                    At least make_arg must be non-NULL.
|                    You may pass NULL for copy_arg and free_arg if your
|                    make_arg function doesn't allocate memory and your
|                    arg fits into the storage for a (void*).
|
|   Return Values :  E_OK           - success
|                    E_BAD_ARGUMENT - invalid argument
+--------------------------------------------------------------------------*/
FORM_EXPORT(int)
set_fieldtype_arg(FIELDTYPE *typ,
		  void *(*const make_arg)(va_list *),
		  void *(*const copy_arg)(const void *),
		  void (*const free_arg) (void *))
{
  TR_FUNC_BFR(3);

  T((T_CALLED("set_fieldtype_arg(%p,%s,%s,%s)"),
     (void *)typ,
     TR_FUNC_ARG(0, make_arg),
     TR_FUNC_ARG(1, copy_arg),
     TR_FUNC_ARG(2, free_arg)));

  if (typ != 0 && make_arg != (void *)0)
    {
      SetStatus(typ, _HAS_ARGS);
      typ->makearg = make_arg;
      typ->copyarg = copy_arg;
      typ->freearg = free_arg;
      RETURN(E_OK);
    }
  RETURN(E_BAD_ARGUMENT);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  void *field_arg(const FIELD *field)
|
|   Description   :  Retrieve pointer to the field's argument structure.
|
|   Return Values :  Pointer to structure or NULL if none is defined.
+--------------------------------------------------------------------------*/
FORM_EXPORT(void *)
field_arg(const FIELD *field)
{
  T((T_CALLED("field_arg(%p)"), (const void *)field));
  returnVoidPtr(Normalize_Field(field)->arg);
}

/* fld_arg.c ends here */
