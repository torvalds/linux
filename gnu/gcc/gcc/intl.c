/* Message translation utilities.
   Copyright (C) 2001, 2003, 2004, 2005 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "intl.h"

#ifdef HAVE_LANGINFO_CODESET
#include <langinfo.h>
#endif

/* Opening quotation mark for diagnostics.  */
const char *open_quote = "'";

/* Closing quotation mark for diagnostics.  */
const char *close_quote = "'";

#ifdef ENABLE_NLS

/* Initialize the translation library for GCC.  This performs the
   appropriate sequence of calls - setlocale, bindtextdomain,
   textdomain.  LC_CTYPE determines the character set used by the
   terminal, so it has be set to output messages correctly.  */

void
gcc_init_libintl (void)
{
#ifdef HAVE_LC_MESSAGES
  setlocale (LC_CTYPE, "");
  setlocale (LC_MESSAGES, "");
#else
  setlocale (LC_ALL, "");
#endif

  (void) bindtextdomain ("gcc", LOCALEDIR);
  (void) textdomain ("gcc");

  /* Opening quotation mark.  */
  open_quote = _("`");

  /* Closing quotation mark.  */
  close_quote = _("'");

  if (!strcmp (open_quote, "`") && !strcmp (close_quote, "'"))
    {
#if defined HAVE_LANGINFO_CODESET
      const char *encoding;
#endif
      /* Untranslated quotes that it may be possible to replace with
	 U+2018 and U+2019; but otherwise use "'" instead of "`" as
	 opening quote.  */
      open_quote = "'";
#if defined HAVE_LANGINFO_CODESET
      encoding = nl_langinfo (CODESET);
      if (encoding != NULL
	  && (!strcasecmp (encoding, "utf-8")
	      || !strcasecmp (encoding, "utf8")))
	{
	  open_quote = "\xe2\x80\x98";
	  close_quote = "\xe2\x80\x99";
	}
#endif
    }
}

#if defined HAVE_WCHAR_H && defined HAVE_WORKING_MBSTOWCS && defined HAVE_WCSWIDTH
#include <wchar.h>

/* Returns the width in columns of MSGSTR, which came from gettext.
   This is for indenting subsequent output.  */

size_t
gcc_gettext_width (const char *msgstr)
{
  size_t nwcs = mbstowcs (0, msgstr, 0);
  wchar_t *wmsgstr = alloca ((nwcs + 1) * sizeof (wchar_t));

  mbstowcs (wmsgstr, msgstr, nwcs + 1);
  return wcswidth (wmsgstr, nwcs);
}

#else  /* no wcswidth */

/* We don't have any way of knowing how wide the string is.  Guess
   the length of the string.  */

size_t
gcc_gettext_width (const char *msgstr)
{
  return strlen (msgstr);
}

#endif

#endif /* ENABLE_NLS */
