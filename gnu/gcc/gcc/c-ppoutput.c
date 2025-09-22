/* Preprocess only, using cpplib.
   Copyright (C) 1995, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004
   Free Software Foundation, Inc.
   Written by Per Bothner, 1994-95.

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
Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "cpplib.h"
#include "../libcpp/internal.h"
#include "tree.h"
#include "c-common.h"		/* For flags.  */
#include "c-pragma.h"		/* For parse_in.  */

/* Encapsulates state used to convert a stream of tokens into a text
   file.  */
static struct
{
  FILE *outf;			/* Stream to write to.  */
  const cpp_token *prev;	/* Previous token.  */
  const cpp_token *source;	/* Source token for spacing.  */
  int src_line;			/* Line number currently being written.  */
  unsigned char printed;	/* Nonzero if something output at line.  */
  bool first_time;		/* pp_file_change hasn't been called yet.  */
} print;

/* General output routines.  */
static void scan_translation_unit (cpp_reader *);
static void scan_translation_unit_trad (cpp_reader *);
static void account_for_newlines (const unsigned char *, size_t);
static int dump_macro (cpp_reader *, cpp_hashnode *, void *);

static void print_line (source_location, const char *);
static void maybe_print_line (source_location);

/* Callback routines for the parser.   Most of these are active only
   in specific modes.  */
static void cb_line_change (cpp_reader *, const cpp_token *, int);
static void cb_define (cpp_reader *, source_location, cpp_hashnode *);
static void cb_undef (cpp_reader *, source_location, cpp_hashnode *);
static void cb_include (cpp_reader *, source_location, const unsigned char *,
			const char *, int, const cpp_token **);
static void cb_ident (cpp_reader *, source_location, const cpp_string *);
static void cb_def_pragma (cpp_reader *, source_location);
static void cb_read_pch (cpp_reader *pfile, const char *name,
			 int fd, const char *orig_name);

/* Preprocess and output.  */
void
preprocess_file (cpp_reader *pfile)
{
  /* A successful cpp_read_main_file guarantees that we can call
     cpp_scan_nooutput or cpp_get_token next.  */
  if (flag_no_output)
    {
      /* Scan -included buffers, then the main file.  */
      while (pfile->buffer->prev)
	cpp_scan_nooutput (pfile);
      cpp_scan_nooutput (pfile);
    }
  else if (cpp_get_options (pfile)->traditional)
    scan_translation_unit_trad (pfile);
  else
    scan_translation_unit (pfile);

  /* -dM command line option.  Should this be elsewhere?  */
  if (flag_dump_macros == 'M')
    cpp_forall_identifiers (pfile, dump_macro, NULL);

  /* Flush any pending output.  */
  if (print.printed)
    putc ('\n', print.outf);
}

/* Set up the callbacks as appropriate.  */
void
init_pp_output (FILE *out_stream)
{
  cpp_callbacks *cb = cpp_get_callbacks (parse_in);

  if (!flag_no_output)
    {
      cb->line_change = cb_line_change;
      /* Don't emit #pragma or #ident directives if we are processing
	 assembly language; the assembler may choke on them.  */
      if (cpp_get_options (parse_in)->lang != CLK_ASM)
	{
	  cb->ident      = cb_ident;
	  cb->def_pragma = cb_def_pragma;
	}
    }

  if (flag_dump_includes)
    cb->include  = cb_include;

  if (flag_pch_preprocess)
    {
      cb->valid_pch = c_common_valid_pch;
      cb->read_pch = cb_read_pch;
    }

  if (flag_dump_macros == 'N' || flag_dump_macros == 'D')
    {
      cb->define = cb_define;
      cb->undef  = cb_undef;
    }

  /* Initialize the print structure.  Setting print.src_line to -1 here is
     a trick to guarantee that the first token of the file will cause
     a linemarker to be output by maybe_print_line.  */
  print.src_line = -1;
  print.printed = 0;
  print.prev = 0;
  print.outf = out_stream;
  print.first_time = 1;
}

/* Writes out the preprocessed file, handling spacing and paste
   avoidance issues.  */
static void
scan_translation_unit (cpp_reader *pfile)
{
  bool avoid_paste = false;

  print.source = NULL;
  for (;;)
    {
      const cpp_token *token = cpp_get_token (pfile);

      if (token->type == CPP_PADDING)
	{
	  avoid_paste = true;
	  if (print.source == NULL
	      || (!(print.source->flags & PREV_WHITE)
		  && token->val.source == NULL))
	    print.source = token->val.source;
	  continue;
	}

      if (token->type == CPP_EOF)
	break;

      /* Subtle logic to output a space if and only if necessary.  */
      if (avoid_paste)
	{
	  if (print.source == NULL)
	    print.source = token;
	  if (print.source->flags & PREV_WHITE
	      || (print.prev
		  && cpp_avoid_paste (pfile, print.prev, token))
	      || (print.prev == NULL && token->type == CPP_HASH))
	    putc (' ', print.outf);
	}
      else if (token->flags & PREV_WHITE)
	putc (' ', print.outf);

      avoid_paste = false;
      print.source = NULL;
      print.prev = token;
      cpp_output_token (token, print.outf);

      if (token->type == CPP_COMMENT)
	account_for_newlines (token->val.str.text, token->val.str.len);
    }
}

/* Adjust print.src_line for newlines embedded in output.  */
static void
account_for_newlines (const unsigned char *str, size_t len)
{
  while (len--)
    if (*str++ == '\n')
      print.src_line++;
}

/* Writes out a traditionally preprocessed file.  */
static void
scan_translation_unit_trad (cpp_reader *pfile)
{
  while (_cpp_read_logical_line_trad (pfile))
    {
      size_t len = pfile->out.cur - pfile->out.base;
      maybe_print_line (pfile->out.first_line);
      fwrite (pfile->out.base, 1, len, print.outf);
      print.printed = 1;
      if (!CPP_OPTION (pfile, discard_comments))
	account_for_newlines (pfile->out.base, len);
    }
}

/* If the token read on logical line LINE needs to be output on a
   different line to the current one, output the required newlines or
   a line marker, and return 1.  Otherwise return 0.  */
static void
maybe_print_line (source_location src_loc)
{
  const struct line_map *map = linemap_lookup (&line_table, src_loc);
  int src_line = SOURCE_LINE (map, src_loc);
  /* End the previous line of text.  */
  if (print.printed)
    {
      putc ('\n', print.outf);
      print.src_line++;
      print.printed = 0;
    }

  if (src_line >= print.src_line && src_line < print.src_line + 8)
    {
      while (src_line > print.src_line)
	{
	  putc ('\n', print.outf);
	  print.src_line++;
	}
    }
  else
    print_line (src_loc, "");
}

/* Output a line marker for logical line LINE.  Special flags are "1"
   or "2" indicating entering or leaving a file.  */
static void
print_line (source_location src_loc, const char *special_flags)
{
  /* End any previous line of text.  */
  if (print.printed)
    putc ('\n', print.outf);
  print.printed = 0;

  if (!flag_no_line_commands)
    {
      const struct line_map *map = linemap_lookup (&line_table, src_loc);

      size_t to_file_len = strlen (map->to_file);
      unsigned char *to_file_quoted =
         (unsigned char *) alloca (to_file_len * 4 + 1);
      unsigned char *p;

      print.src_line = SOURCE_LINE (map, src_loc);

      /* cpp_quote_string does not nul-terminate, so we have to do it
	 ourselves.  */
      p = cpp_quote_string (to_file_quoted,
			    (unsigned char *) map->to_file, to_file_len);
      *p = '\0';
      fprintf (print.outf, "# %u \"%s\"%s",
	       print.src_line == 0 ? 1 : print.src_line,
	       to_file_quoted, special_flags);

      if (map->sysp == 2)
	fputs (" 3 4", print.outf);
      else if (map->sysp == 1)
	fputs (" 3", print.outf);

      putc ('\n', print.outf);
    }
}

/* Called when a line of output is started.  TOKEN is the first token
   of the line, and at end of file will be CPP_EOF.  */
static void
cb_line_change (cpp_reader *pfile, const cpp_token *token,
		int parsing_args)
{
  source_location src_loc = token->src_loc;

  if (token->type == CPP_EOF || parsing_args)
    return;

  maybe_print_line (src_loc);
  print.prev = 0;
  print.source = 0;

  /* Supply enough spaces to put this token in its original column,
     one space per column greater than 2, since scan_translation_unit
     will provide a space if PREV_WHITE.  Don't bother trying to
     reconstruct tabs; we can't get it right in general, and nothing
     ought to care.  Some things do care; the fault lies with them.  */
  if (!CPP_OPTION (pfile, traditional))
    {
      const struct line_map *map = linemap_lookup (&line_table, src_loc);
      int spaces = SOURCE_COLUMN (map, src_loc) - 2;
      print.printed = 1;

      while (-- spaces >= 0)
	putc (' ', print.outf);
    }
}

static void
cb_ident (cpp_reader *pfile ATTRIBUTE_UNUSED, source_location line,
	  const cpp_string *str)
{
  maybe_print_line (line);
  fprintf (print.outf, "#ident %s\n", str->text);
  print.src_line++;
}

static void
cb_define (cpp_reader *pfile, source_location line, cpp_hashnode *node)
{
  maybe_print_line (line);
  fputs ("#define ", print.outf);

  /* 'D' is whole definition; 'N' is name only.  */
  if (flag_dump_macros == 'D')
    fputs ((const char *) cpp_macro_definition (pfile, node),
	   print.outf);
  else
    fputs ((const char *) NODE_NAME (node), print.outf);

  putc ('\n', print.outf);
  if (linemap_lookup (&line_table, line)->to_line != 0)
    print.src_line++;
}

static void
cb_undef (cpp_reader *pfile ATTRIBUTE_UNUSED, source_location line,
	  cpp_hashnode *node)
{
  maybe_print_line (line);
  fprintf (print.outf, "#undef %s\n", NODE_NAME (node));
  print.src_line++;
}

static void
cb_include (cpp_reader *pfile ATTRIBUTE_UNUSED, source_location line,
	    const unsigned char *dir, const char *header, int angle_brackets,
	    const cpp_token **comments)
{
  maybe_print_line (line);
  if (angle_brackets)
    fprintf (print.outf, "#%s <%s>", dir, header);
  else
    fprintf (print.outf, "#%s \"%s\"", dir, header);

  if (comments != NULL)
    {
      while (*comments != NULL)
	{
	  if ((*comments)->flags & PREV_WHITE)
	    putc (' ', print.outf);
	  cpp_output_token (*comments, print.outf);
	  ++comments;
	}
    }

  putc ('\n', print.outf);
  print.src_line++;
}

/* Callback called when -fworking-director and -E to emit working
   directory in cpp output file.  */

void
pp_dir_change (cpp_reader *pfile ATTRIBUTE_UNUSED, const char *dir)
{
  size_t to_file_len = strlen (dir);
  unsigned char *to_file_quoted =
     (unsigned char *) alloca (to_file_len * 4 + 1);
  unsigned char *p;

  /* cpp_quote_string does not nul-terminate, so we have to do it ourselves.  */
  p = cpp_quote_string (to_file_quoted, (unsigned char *) dir, to_file_len);
  *p = '\0';
  fprintf (print.outf, "# 1 \"%s//\"\n", to_file_quoted);
}

/* The file name, line number or system header flags have changed, as
   described in MAP.  */

void
pp_file_change (const struct line_map *map)
{
  const char *flags = "";

  if (flag_no_line_commands)
    return;

  if (map != NULL)
    {
      if (print.first_time)
	{
	  /* Avoid printing foo.i when the main file is foo.c.  */
	  if (!cpp_get_options (parse_in)->preprocessed)
	    print_line (map->start_location, flags);
	  print.first_time = 0;
	}
      else
	{
	  /* Bring current file to correct line when entering a new file.  */
	  if (map->reason == LC_ENTER)
	    {
	      const struct line_map *from = INCLUDED_FROM (&line_table, map);
	      maybe_print_line (LAST_SOURCE_LINE_LOCATION (from));
	    }
	  if (map->reason == LC_ENTER)
	    flags = " 1";
	  else if (map->reason == LC_LEAVE)
	    flags = " 2";
	  print_line (map->start_location, flags);
	}
    }
}

/* Copy a #pragma directive to the preprocessed output.  */
static void
cb_def_pragma (cpp_reader *pfile, source_location line)
{
  maybe_print_line (line);
  fputs ("#pragma ", print.outf);
  cpp_output_line (pfile, print.outf);
  print.src_line++;
}

/* Dump out the hash table.  */
static int
dump_macro (cpp_reader *pfile, cpp_hashnode *node, void *v ATTRIBUTE_UNUSED)
{
  if (node->type == NT_MACRO && !(node->flags & NODE_BUILTIN))
    {
      fputs ("#define ", print.outf);
      fputs ((const char *) cpp_macro_definition (pfile, node),
	     print.outf);
      putc ('\n', print.outf);
      print.src_line++;
    }

  return 1;
}

/* Load in the PCH file NAME, open on FD.  It was originally searched for
   by ORIG_NAME.  Also, print out a #include command so that the PCH
   file can be loaded when the preprocessed output is compiled.  */

static void
cb_read_pch (cpp_reader *pfile, const char *name,
	     int fd, const char *orig_name ATTRIBUTE_UNUSED)
{
  c_common_read_pch (pfile, name, fd, orig_name);

  fprintf (print.outf, "#pragma GCC pch_preprocess \"%s\"\n", name);
  print.src_line++;
}
