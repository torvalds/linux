
/* Install modified versions of certain ANSI-incompatible system header
   files which are fixed to work correctly with ANSI C and placed in a
   directory that GCC will search.

   Copyright (C) 1999, 2000, 2001, 2004 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "fixlib.h"

/* * * * * * * * * * * * *
 
   load_file_data loads all the contents of a file into malloc-ed memory.
   Its argument is the file pointer of the file to read in; the returned
   result is the NUL terminated contents of the file.  The file
   is presumed to be an ASCII text file containing no NULs.  */

char *
load_file_data (FILE* fp)
{
  char *pz_data = (char*)NULL;
  int    space_left = -1;  /* allow for terminating NUL */
  size_t space_used = 0;

  if (fp == (FILE*)NULL)
    return pz_data;

  do
    {
      size_t  size_read;

      if (space_left < 1024)
        {
          space_left += 4096;
	  pz_data = XRESIZEVEC (char, pz_data, space_left + space_used + 1 );
        }
      size_read = fread (pz_data + space_used, 1, space_left, fp);

      if (size_read == 0)
        {
          if (feof (fp))
            break;

          if (ferror (fp))
            {
              int err = errno;
              if (err != EISDIR)
                fprintf (stderr, "error %d (%s) reading input\n", err,
                         xstrerror (err));
              free ((void *) pz_data);
              return (char *) NULL;
            }
        }

      space_left -= size_read;
      space_used += size_read;
    } while (! feof (fp));

  pz_data = XRESIZEVEC (char, pz_data, space_used+1 );
  pz_data[ space_used ] = NUL;

  return pz_data;
}

#ifdef IS_CXX_HEADER_NEEDED
t_bool
is_cxx_header (tCC* fname, tCC* text)
{
  /*  First, check to see if the file is in a C++ directory */
  for (;;)
    {
      switch (*(fname++))
        {
        case 'C': /* check for "CC/" */
          if ((fname[0] == 'C') && (fname[1] == '/'))
            return BOOL_TRUE;
          break;

        case 'x': /* check for "xx/" */
          if ((fname[0] == 'x') && (fname[1] == '/'))
            return BOOL_TRUE;
          break;

        case '+': /* check for "++" */
          if (fname[0] == '+')
            return BOOL_TRUE;
          break;

        case NUL:
          goto not_cxx_name;
        }
    } not_cxx_name:;

  /* Or it might contain one of several phrases which indicate C++ code.
     Currently recognized are:
     extern "C++"
     -*- (Mode: )? C++ -*-   (emacs mode marker)
     template <
   */
    {
      tSCC cxxpat[] = "\
extern[ \t]*\"C\\+\\+\"|\
-\\*-[ \t]*([mM]ode:[ \t]*)?[cC]\\+\\+[; \t]*-\\*-|\
template[ \t]*<|\
^[ \t]*class[ \t]|\
(public|private|protected):|\
^[ \t]*#[ \t]*pragma[ \t]+(interface|implementation)\
";
      static regex_t cxxre;
      static int compiled;

      if (!compiled)
	compile_re (cxxpat, &cxxre, 0, "contents check", "is_cxx_header");

      if (xregexec (&cxxre, text, 0, 0, 0) == 0)
	return BOOL_TRUE;
    }
		   
  return BOOL_FALSE;
}
#endif /* CXX_TYPE_NEEDED */

#ifdef SKIP_QUOTE_NEEDED
/*
 *  Skip over a quoted string.  Single quote strings may
 *  contain multiple characters if the first character is
 *  a backslash.  Especially a backslash followed by octal digits.
 *  We are not doing a correctness syntax check here.
 */
tCC*
skip_quote(char q, char* text )
{
  for (;;)
    {
      char ch = *(text++);
      switch (ch)
        {
        case '\\':
          text++; /* skip over whatever character follows */
          break;

        case '"':
        case '\'':
          if (ch != q)
            break;
          /*FALLTHROUGH*/

        case '\n':
        case NUL:
          goto skip_done;
        }
    } skip_done:;

  return text;
}
#endif /* SKIP_QUOTE_NEEDED */

/* * * * * * * * * * * * *
 
   Compile one regular expression pattern for later use.  PAT contains
   the pattern, RE points to a regex_t structure (which should have
   been bzeroed).  MATCH is 1 if we need to know where the regex
   matched, 0 if not. If xregcomp fails, prints an error message and
   aborts; E1 and E2 are strings to shove into the error message.

   The patterns we search for are all egrep patterns.
   REG_EXTENDED|REG_NEWLINE produces identical regex syntax/semantics
   to egrep (verified from 4.4BSD Programmer's Reference Manual).  */
void
compile_re( tCC* pat, regex_t* re, int match, tCC* e1, tCC* e2 )
{
  tSCC z_bad_comp[] = "fixincl ERROR:  cannot compile %s regex for %s\n\
\texpr = `%s'\n\terror %s\n";
  int flags, err;

  flags = (match ? REG_EXTENDED|REG_NEWLINE
	   : REG_EXTENDED|REG_NEWLINE|REG_NOSUB);
  err = xregcomp (re, pat, flags);

  if (err)
    {
      char rerrbuf[1024];
      regerror (err, re, rerrbuf, 1024);
      fprintf (stderr, z_bad_comp, e1, e2, pat, rerrbuf);
      exit (EXIT_FAILURE);
    }
}

/* * * * * * * * * * * * *

   Helper routine and data for the machine_name test and fix.  */

tSCC mn_label_pat[] = "^[ \t]*#[ \t]*(if|ifdef|ifndef)[ \t]+";
static regex_t mn_label_re;
static regex_t mn_name_re;

static int mn_compiled = 0;

t_bool
mn_get_regexps(regex_t** label_re, regex_t** name_re, tCC* who )
{
  if (! pz_mn_name_pat)
    return BOOL_FALSE;

  if (! mn_compiled)
    {
      compile_re (mn_label_pat, &mn_label_re, 1, "label pattern", who);
      compile_re (pz_mn_name_pat, &mn_name_re, 1, "name pattern", who);
      mn_compiled++;
    }
  *label_re = &mn_label_re;
  *name_re = &mn_name_re;
  return BOOL_TRUE;
}


#ifdef SEPARATE_FIX_PROC

char*
make_raw_shell_str( char* pz_d, tCC* pz_s, size_t smax )
{
  tSCC zQ[] = "'\\''";
  size_t     dtaSize;
  char*      pz_d_start = pz_d;

  smax--; /* adjust for trailing NUL */

  dtaSize = strlen( pz_s ) + 3;

  {
    const char* pz = pz_s - 1;

    for (;;) {
      pz = strchr( pz+1, '\'' );
      if (pz == (char*)NULL)
        break;
      dtaSize += sizeof( zQ )-1;
    }
  }
  if (dtaSize > smax)
    return (char*)NULL;

  *(pz_d++) = '\'';

  for (;;) {
    if (pz_d - pz_d_start >= smax)
      return (char*)NULL;
    switch (*(pz_d++) = *(pz_s++)) {
    case NUL:
      goto loopDone;

    case '\'':
      if (pz_d - pz_d_start >= smax - sizeof( zQ )-1)
	return (char*)NULL;
      strcpy( pz_d-1, zQ );
      pz_d += sizeof( zQ )-2;
    }
  } loopDone:;
  pz_d[-1] = '\'';
  *pz_d    = NUL;

  return pz_d;
}

#endif
