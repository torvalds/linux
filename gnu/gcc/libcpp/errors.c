/* Default error handlers for CPP Library.
   Copyright (C) 1986, 1987, 1989, 1992, 1993, 1994, 1995, 1998, 1999, 2000,
   2001, 2002, 2004 Free Software Foundation, Inc.
   Written by Per Bothner, 1994.
   Based on CCCP program by Paul Rubin, June 1986
   Adapted to ANSI C, Richard Stallman, Jan 1987

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

 In other words, you are welcome to use, share and improve this program.
 You are forbidden to forbid anyone else to use, share and improve
 what you give them.   Help stamp out software-hoarding!  */

#include "config.h"
#include "system.h"
#include "cpplib.h"
#include "internal.h"

static void print_location (cpp_reader *, source_location, unsigned int);

/* Print the logical file location (LINE, COL) in preparation for a
   diagnostic.  Outputs the #include chain if it has changed.  A line
   of zero suppresses the include stack, and outputs the program name
   instead.  */
static void
print_location (cpp_reader *pfile, source_location line, unsigned int col)
{
  if (line == 0)
    fprintf (stderr, "%s: ", progname);
  else
    {
      const struct line_map *map;
      unsigned int lin;

      map = linemap_lookup (pfile->line_table, line);
      linemap_print_containing_files (pfile->line_table, map);

      lin = SOURCE_LINE (map, line);
      if (col == 0)
	{
	  col = SOURCE_COLUMN (map, line);
	  if (col == 0)
	    col = 1;
	}

      if (lin == 0)
	fprintf (stderr, "%s:", map->to_file);
      else if (CPP_OPTION (pfile, show_column) == 0)
	fprintf (stderr, "%s:%u:", map->to_file, lin);
      else
	fprintf (stderr, "%s:%u:%u:", map->to_file, lin, col);

      fputc (' ', stderr);
    }
}

/* Set up for a diagnostic: print the file and line, bump the error
   counter, etc.  SRC_LOC is the logical line number; zero means to print
   at the location of the previously lexed token, which tends to be
   the correct place by default.  The column number can be specified either
   using COLUMN or (if COLUMN==0) extracting SOURCE_COLUMN from SRC_LOC.
   (This may seem redundant, but is useful when pre-scanning (cleaning) a line,
   when we haven't yet verified whether the current line_map has a
   big enough max_column_hint.)

   Returns 0 if the error has been suppressed.  */
int
_cpp_begin_message (cpp_reader *pfile, int code,
		    source_location src_loc, unsigned int column)
{
  int level = CPP_DL_EXTRACT (code);

  switch (level)
    {
    case CPP_DL_WARNING:
    case CPP_DL_PEDWARN:
      if (cpp_in_system_header (pfile)
	  && ! CPP_OPTION (pfile, warn_system_headers))
	return 0;
      /* Fall through.  */

    case CPP_DL_WARNING_SYSHDR:
      if (CPP_OPTION (pfile, warnings_are_errors)
	  || (level == CPP_DL_PEDWARN && CPP_OPTION (pfile, pedantic_errors)))
	{
	  if (CPP_OPTION (pfile, inhibit_errors))
	    return 0;
	  level = CPP_DL_ERROR;
	  pfile->errors++;
	}
      else if (CPP_OPTION (pfile, inhibit_warnings))
	return 0;
      break;

    case CPP_DL_ERROR:
      if (CPP_OPTION (pfile, inhibit_errors))
	return 0;
      /* ICEs cannot be inhibited.  */
    case CPP_DL_ICE:
      pfile->errors++;
      break;
    }

  print_location (pfile, src_loc, column);
  if (CPP_DL_WARNING_P (level))
    fputs (_("warning: "), stderr);
  else if (level == CPP_DL_ICE)
    fputs (_("internal error: "), stderr);
  else
    fputs (_("error: "), stderr);

  return 1;
}

/* Don't remove the blank before do, as otherwise the exgettext
   script will mistake this as a function definition */
#define v_message(msgid, ap) \
 do { vfprintf (stderr, _(msgid), ap); putc ('\n', stderr); } while (0)

/* Exported interface.  */

/* Print an error at the location of the previously lexed token.  */
void
cpp_error (cpp_reader * pfile, int level, const char *msgid, ...)
{
  source_location src_loc;
  va_list ap;
  
  va_start (ap, msgid);

  if (CPP_OPTION (pfile, client_diagnostic))
    pfile->cb.error (pfile, level, _(msgid), &ap);
  else
    {
      if (CPP_OPTION (pfile, traditional))
	{
	  if (pfile->state.in_directive)
	    src_loc = pfile->directive_line;
	  else
	    src_loc = pfile->line_table->highest_line;
	}
      else
	{
	  /* Find actual previous token.  */
	  cpp_token *t;

	  if (pfile->cur_token != pfile->cur_run->base)
	    t = pfile->cur_token - 1;
	  else
	    {
	      if (pfile->cur_run->prev != NULL)
	        t = pfile->cur_run->prev->limit;
	      else
	        t = NULL;
	    }
	  /* Retrieve corresponding source location, unless we failed.  */
	  src_loc = t ? t->src_loc : 0;
	}

      if (_cpp_begin_message (pfile, level, src_loc, 0))
	v_message (msgid, ap);
    }

  va_end (ap);
}

/* Print an error at a specific location.  */
void
cpp_error_with_line (cpp_reader *pfile, int level,
		     source_location src_loc, unsigned int column,
		     const char *msgid, ...)
{
  va_list ap;
  
  va_start (ap, msgid);

  if (_cpp_begin_message (pfile, level, src_loc, column))
    v_message (msgid, ap);

  va_end (ap);
}

void
cpp_errno (cpp_reader *pfile, int level, const char *msgid)
{
  if (msgid[0] == '\0')
    msgid = _("stdout");

  cpp_error (pfile, level, "%s: %s", msgid, xstrerror (errno));
}
