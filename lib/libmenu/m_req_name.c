/* $OpenBSD: m_req_name.c,v 1.7 2023/10/17 09:52:10 nicm Exp $ */

/****************************************************************************
 * Copyright 2020,2021 Thomas E. Dickey                                     *
 * Copyright 1998-2012,2015 Free Software Foundation, Inc.                  *
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
* Module m_request_name                                                    *
* Routines to handle external names of menu requests                       *
***************************************************************************/

#include "menu.priv.h"

MODULE_ID("$Id: m_req_name.c,v 1.7 2023/10/17 09:52:10 nicm Exp $")

#define DATA(s) { s }

static const char request_names[MAX_MENU_COMMAND - MIN_MENU_COMMAND + 1][14] =
{
  DATA("LEFT_ITEM"),
  DATA("RIGHT_ITEM"),
  DATA("UP_ITEM"),
  DATA("DOWN_ITEM"),
  DATA("SCR_ULINE"),
  DATA("SCR_DLINE"),
  DATA("SCR_DPAGE"),
  DATA("SCR_UPAGE"),
  DATA("FIRST_ITEM"),
  DATA("LAST_ITEM"),
  DATA("NEXT_ITEM"),
  DATA("PREV_ITEM"),
  DATA("TOGGLE_ITEM"),
  DATA("CLEAR_PATTERN"),
  DATA("BACK_PATTERN"),
  DATA("NEXT_MATCH"),
  DATA("PREV_MATCH")
};

#define A_SIZE (sizeof(request_names)/sizeof(request_names[0]))

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu
|   Function      :  const char * menu_request_name (int request);
|
|   Description   :  Get the external name of a menu request.
|
|   Return Values :  Pointer to name      - on success
|                    NULL                 - on invalid request code
+--------------------------------------------------------------------------*/
MENU_EXPORT(const char *)
menu_request_name(int request)
{
  T((T_CALLED("menu_request_name(%d)"), request));
  if ((request < MIN_MENU_COMMAND) || (request > MAX_MENU_COMMAND))
    {
      SET_ERROR(E_BAD_ARGUMENT);
      returnCPtr((const char *)0);
    }
  else
    returnCPtr(request_names[request - MIN_MENU_COMMAND]);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu
|   Function      :  int menu_request_by_name (const char *str);
|
|   Description   :  Search for a request with this name.
|
|   Return Values :  Request Id       - on success
|                    E_NO_MATCH       - request not found
+--------------------------------------------------------------------------*/
MENU_EXPORT(int)
menu_request_by_name(const char *str)
{
  /* because the table is so small, it doesn't really hurt
     to run sequentially through it.
   */
  size_t i = 0;

  T((T_CALLED("menu_request_by_name(%s)"), _nc_visbuf(str)));

  if (str != 0 && (i = strlen(str)) != 0)
    {
      char buf[16];

      if (i > sizeof(buf) - 2)
	i = sizeof(buf) - 2;
      memcpy(buf, str, i);
      buf[i] = '\0';

      for (i = 0; buf[i] != '\0'; ++i)
	{
	  buf[i] = (char)toupper(UChar(buf[i]));
	}

      for (i = 0; i < A_SIZE; i++)
	{
	  if (strcmp(request_names[i], buf) == 0)
	    returnCode(MIN_MENU_COMMAND + (int)i);
	}
    }
  RETURN(E_NO_MATCH);
}

/* m_req_name.c ends here */
