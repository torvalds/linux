/* Language-independent diagnostic subroutines for the GNU Compiler Collection
   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by Gabriel Dos Reis <gdr@codesourcery.com>

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


/* This file implements the language independent aspect of diagnostic
   message module.  */

#include "config.h"
#undef FLOAT /* This is for hpux. They should change hpux.  */
#undef FFS  /* Some systems define this in param.h.  */
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "version.h"
#include "tm_p.h"
#include "flags.h"
#include "input.h"
#include "toplev.h"
#include "intl.h"
#include "diagnostic.h"
#include "langhooks.h"
#include "langhooks-def.h"
#include "opts.h"


/* Prototypes.  */
static char *build_message_string (const char *, ...) ATTRIBUTE_PRINTF_1;

static void default_diagnostic_starter (diagnostic_context *,
					diagnostic_info *);
static void default_diagnostic_finalizer (diagnostic_context *,
					  diagnostic_info *);

static void error_recursion (diagnostic_context *) ATTRIBUTE_NORETURN;
static bool diagnostic_count_diagnostic (diagnostic_context *,
					 diagnostic_info *);
static void diagnostic_action_after_output (diagnostic_context *,
					    diagnostic_info *);
static void real_abort (void) ATTRIBUTE_NORETURN;

/* A diagnostic_context surrogate for stderr.  */
static diagnostic_context global_diagnostic_context;
diagnostic_context *global_dc = &global_diagnostic_context;


/* Return a malloc'd string containing MSG formatted a la printf.  The
   caller is responsible for freeing the memory.  */
static char *
build_message_string (const char *msg, ...)
{
  char *str;
  va_list ap;

  va_start (ap, msg);
  vasprintf (&str, msg, ap);
  va_end (ap);

  return str;
}

/* Same as diagnostic_build_prefix, but only the source FILE is given.  */
char *
file_name_as_prefix (const char *f)
{
  return build_message_string ("%s: ", f);
}



/* Initialize the diagnostic message outputting machinery.  */
void
diagnostic_initialize (diagnostic_context *context)
{
  /* Allocate a basic pretty-printer.  Clients will replace this a
     much more elaborated pretty-printer if they wish.  */
  context->printer = XNEW (pretty_printer);
  pp_construct (context->printer, NULL, 0);
  /* By default, diagnostics are sent to stderr.  */
  context->printer->buffer->stream = stderr;
  /* By default, we emit prefixes once per message.  */
  context->printer->wrapping.rule = DIAGNOSTICS_SHOW_PREFIX_ONCE;

  memset (context->diagnostic_count, 0, sizeof context->diagnostic_count);
  context->issue_warnings_are_errors_message = true;
  context->warning_as_error_requested = false;
  memset (context->classify_diagnostic, DK_UNSPECIFIED,
	  sizeof context->classify_diagnostic);
  context->show_option_requested = false;
  context->abort_on_error = false;
  context->internal_error = NULL;
  diagnostic_starter (context) = default_diagnostic_starter;
  diagnostic_finalizer (context) = default_diagnostic_finalizer;
  context->last_module = 0;
  context->last_function = NULL;
  context->lock = 0;
}

/* Initialize DIAGNOSTIC, where the message MSG has already been
   translated.  */
void
diagnostic_set_info_translated (diagnostic_info *diagnostic, const char *msg,
				va_list *args, location_t location,
				diagnostic_t kind)
{
  diagnostic->message.err_no = errno;
  diagnostic->message.args_ptr = args;
  diagnostic->message.format_spec = msg;
  diagnostic->location = location;
  diagnostic->kind = kind;
  diagnostic->option_index = 0;
}

/* Initialize DIAGNOSTIC, where the message GMSGID has not yet been
   translated.  */
void
diagnostic_set_info (diagnostic_info *diagnostic, const char *gmsgid,
		     va_list *args, location_t location,
		     diagnostic_t kind)
{
  diagnostic_set_info_translated (diagnostic, _(gmsgid), args, location, kind);
}

/* Return a malloc'd string describing a location.  The caller is
   responsible for freeing the memory.  */
char *
diagnostic_build_prefix (diagnostic_info *diagnostic)
{
  static const char *const diagnostic_kind_text[] = {
#define DEFINE_DIAGNOSTIC_KIND(K, T) (T),
#include "diagnostic.def"
#undef DEFINE_DIAGNOSTIC_KIND
    "must-not-happen"
  };
  const char *text = _(diagnostic_kind_text[diagnostic->kind]);
  expanded_location s = expand_location (diagnostic->location);
  gcc_assert (diagnostic->kind < DK_LAST_DIAGNOSTIC_KIND);

  return
    (s.file == NULL
     ? build_message_string ("%s: %s", progname, text)
#ifdef USE_MAPPED_LOCATION
     : flag_show_column && s.column != 0
     ? build_message_string ("%s:%d:%d: %s", s.file, s.line, s.column, text)
#endif
     : build_message_string ("%s:%d: %s", s.file, s.line, text));
}

/* Count a diagnostic.  Return true if the message should be printed.  */
static bool
diagnostic_count_diagnostic (diagnostic_context *context,
			     diagnostic_info *diagnostic)
{
  diagnostic_t kind = diagnostic->kind;
  switch (kind)
    {
    default:
      gcc_unreachable ();

    case DK_ICE:
#ifndef ENABLE_CHECKING
      /* When not checking, ICEs are converted to fatal errors when an
	 error has already occurred.  This is counteracted by
	 abort_on_error.  */
      if ((diagnostic_kind_count (context, DK_ERROR) > 0
	   || diagnostic_kind_count (context, DK_SORRY) > 0)
	  && !context->abort_on_error)
	{
	  expanded_location s = expand_location (diagnostic->location);
	  fnotice (stderr, "%s:%d: confused by earlier errors, bailing out\n",
		   s.file, s.line);
	  exit (ICE_EXIT_CODE);
	}
#endif
      if (context->internal_error)
	(*context->internal_error) (diagnostic->message.format_spec,
				    diagnostic->message.args_ptr);
      /* Fall through.  */

    case DK_FATAL: case DK_SORRY:
    case DK_ANACHRONISM: case DK_NOTE:
      ++diagnostic_kind_count (context, kind);
      break;

    case DK_WARNING:
      if (!diagnostic_report_warnings_p ())
        return false;

      /* -Werror can reclassify warnings as errors, but
	 classify_diagnostic can reclassify it back to a warning.  The
	 second part of this test detects that case.  */
      if (!context->warning_as_error_requested
	  || (context->classify_diagnostic[diagnostic->option_index]
	      == DK_WARNING))
        {
          ++diagnostic_kind_count (context, DK_WARNING);
          break;
        }
      else if (context->issue_warnings_are_errors_message)
        {
	  pp_verbatim (context->printer,
                       "%s: warnings being treated as errors\n", progname);
          context->issue_warnings_are_errors_message = false;
        }

      /* And fall through.  */
    case DK_ERROR:
      ++diagnostic_kind_count (context, DK_ERROR);
      break;
    }

  return true;
}

/* Take any action which is expected to happen after the diagnostic
   is written out.  This function does not always return.  */
static void
diagnostic_action_after_output (diagnostic_context *context,
				diagnostic_info *diagnostic)
{
  switch (diagnostic->kind)
    {
    case DK_DEBUG:
    case DK_NOTE:
    case DK_ANACHRONISM:
    case DK_WARNING:
      break;

    case DK_ERROR:
    case DK_SORRY:
      if (context->abort_on_error)
	real_abort ();
      if (flag_fatal_errors)
	{
	  fnotice (stderr, "compilation terminated due to -Wfatal-errors.\n");
	  exit (FATAL_EXIT_CODE);
	}
      break;

    case DK_ICE:
      if (context->abort_on_error)
	real_abort ();

      fnotice (stderr, "Please submit a full bug report,\n"
	       "with preprocessed source if appropriate.\n"
	       "See %s for instructions.\n", bug_report_url);
      exit (ICE_EXIT_CODE);

    case DK_FATAL:
      if (context->abort_on_error)
	real_abort ();

      fnotice (stderr, "compilation terminated.\n");
      exit (FATAL_EXIT_CODE);

    default:
      gcc_unreachable ();
    }
}

/* Prints out, if necessary, the name of the current function
   that caused an error.  Called from all error and warning functions.  */
void
diagnostic_report_current_function (diagnostic_context *context)
{
  diagnostic_report_current_module (context);
  lang_hooks.print_error_function (context, input_filename);
}

void
diagnostic_report_current_module (diagnostic_context *context)
{
  struct file_stack *p;

  if (pp_needs_newline (context->printer))
    {
      pp_newline (context->printer);
      pp_needs_newline (context->printer) = false;
    }

  p = input_file_stack;
  if (p && diagnostic_last_module_changed (context))
    {
      expanded_location xloc = expand_location (p->location);
      pp_verbatim (context->printer,
                   "In file included from %s:%d",
		   xloc.file, xloc.line);
      while ((p = p->next) != NULL)
	{
	  xloc = expand_location (p->location);
	  pp_verbatim (context->printer,
		       ",\n                 from %s:%d",
		       xloc.file, xloc.line);
	}
      pp_verbatim (context->printer, ":");
      diagnostic_set_last_module (context);
      pp_newline (context->printer);
    }
}

static void
default_diagnostic_starter (diagnostic_context *context,
			    diagnostic_info *diagnostic)
{
  diagnostic_report_current_function (context);
  pp_set_prefix (context->printer, diagnostic_build_prefix (diagnostic));
}

static void
default_diagnostic_finalizer (diagnostic_context *context,
			      diagnostic_info *diagnostic ATTRIBUTE_UNUSED)
{
  pp_destroy_prefix (context->printer);
}

/* Interface to specify diagnostic kind overrides.  Returns the
   previous setting, or DK_UNSPECIFIED if the parameters are out of
   range.  */
diagnostic_t
diagnostic_classify_diagnostic (diagnostic_context *context,
				int option_index,
				diagnostic_t new_kind)
{
  diagnostic_t old_kind;

  if (option_index <= 0
      || option_index >= N_OPTS
      || new_kind >= DK_LAST_DIAGNOSTIC_KIND)
    return DK_UNSPECIFIED;

  old_kind = context->classify_diagnostic[option_index];
  context->classify_diagnostic[option_index] = new_kind;
  return old_kind;
}

/* Report a diagnostic message (an error or a warning) as specified by
   DC.  This function is *the* subroutine in terms of which front-ends
   should implement their specific diagnostic handling modules.  The
   front-end independent format specifiers are exactly those described
   in the documentation of output_format.  */

void
diagnostic_report_diagnostic (diagnostic_context *context,
			      diagnostic_info *diagnostic)
{
  if (context->lock > 0)
    {
      /* If we're reporting an ICE in the middle of some other error,
	 try to flush out the previous error, then let this one
	 through.  Don't do this more than once.  */
      if (diagnostic->kind == DK_ICE && context->lock == 1)
	pp_flush (context->printer);
      else
	error_recursion (context);
    }

  if (diagnostic->option_index)
    {
      /* This tests if the user provided the appropriate -Wfoo or
	 -Wno-foo option.  */
      if (! option_enabled (diagnostic->option_index))
	return;
      /* This tests if the user provided the appropriate -Werror=foo
	 option.  */
      if (context->classify_diagnostic[diagnostic->option_index] != DK_UNSPECIFIED)
	diagnostic->kind = context->classify_diagnostic[diagnostic->option_index];
      /* This allows for future extensions, like temporarily disabling
	 warnings for ranges of source code.  */
      if (diagnostic->kind == DK_IGNORED)
	return;
    }

  context->lock++;

  if (diagnostic_count_diagnostic (context, diagnostic))
    {
      const char *saved_format_spec = diagnostic->message.format_spec;

      if (context->show_option_requested && diagnostic->option_index)
	diagnostic->message.format_spec
	  = ACONCAT ((diagnostic->message.format_spec,
		      " [", cl_options[diagnostic->option_index].opt_text, "]", NULL));

      diagnostic->message.locus = &diagnostic->location;
      pp_format (context->printer, &diagnostic->message);
      (*diagnostic_starter (context)) (context, diagnostic);
      pp_output_formatted_text (context->printer);
      (*diagnostic_finalizer (context)) (context, diagnostic);
      pp_flush (context->printer);
      diagnostic_action_after_output (context, diagnostic);
      diagnostic->message.format_spec = saved_format_spec;
    }

  context->lock--;
}

/* Given a partial pathname as input, return another pathname that
   shares no directory elements with the pathname of __FILE__.  This
   is used by fancy_abort() to print `Internal compiler error in expr.c'
   instead of `Internal compiler error in ../../GCC/gcc/expr.c'.  */

const char *
trim_filename (const char *name)
{
  static const char this_file[] = __FILE__;
  const char *p = name, *q = this_file;

  /* First skip any "../" in each filename.  This allows us to give a proper
     reference to a file in a subdirectory.  */
  while (p[0] == '.' && p[1] == '.' && IS_DIR_SEPARATOR (p[2]))
    p += 3;

  while (q[0] == '.' && q[1] == '.' && IS_DIR_SEPARATOR (q[2]))
    q += 3;

  /* Now skip any parts the two filenames have in common.  */
  while (*p == *q && *p != 0 && *q != 0)
    p++, q++;

  /* Now go backwards until the previous directory separator.  */
  while (p > name && !IS_DIR_SEPARATOR (p[-1]))
    p--;

  return p;
}

/* Standard error reporting routines in increasing order of severity.
   All of these take arguments like printf.  */

/* Text to be emitted verbatim to the error message stream; this
   produces no prefix and disables line-wrapping.  Use rarely.  */
void
verbatim (const char *gmsgid, ...)
{
  text_info text;
  va_list ap;

  va_start (ap, gmsgid);
  text.err_no = errno;
  text.args_ptr = &ap;
  text.format_spec = _(gmsgid);
  text.locus = NULL;
  pp_format_verbatim (global_dc->printer, &text);
  pp_flush (global_dc->printer);
  va_end (ap);
}

/* An informative note.  Use this for additional details on an error
   message.  */
void
inform (const char *gmsgid, ...)
{
  diagnostic_info diagnostic;
  va_list ap;

  va_start (ap, gmsgid);
  diagnostic_set_info (&diagnostic, gmsgid, &ap, input_location, DK_NOTE);
  report_diagnostic (&diagnostic);
  va_end (ap);
}

/* A warning.  Use this for code which is correct according to the
   relevant language specification but is likely to be buggy anyway.  */
void
warning (int opt, const char *gmsgid, ...)
{
  diagnostic_info diagnostic;
  va_list ap;

  va_start (ap, gmsgid);
  diagnostic_set_info (&diagnostic, gmsgid, &ap, input_location, DK_WARNING);
  diagnostic.option_index = opt;

  report_diagnostic (&diagnostic);
  va_end (ap);
}

void
warning0 (const char *gmsgid, ...)
{
  diagnostic_info diagnostic;
  va_list ap;

  va_start (ap, gmsgid);
  diagnostic_set_info (&diagnostic, gmsgid, &ap, input_location, DK_WARNING);
  report_diagnostic (&diagnostic);
  va_end (ap);
}

/* A "pedantic" warning: issues a warning unless -pedantic-errors was
   given on the command line, in which case it issues an error.  Use
   this for diagnostics required by the relevant language standard,
   if you have chosen not to make them errors.

   Note that these diagnostics are issued independent of the setting
   of the -pedantic command-line switch.  To get a warning enabled
   only with that switch, write "if (pedantic) pedwarn (...);"  */
void
pedwarn (const char *gmsgid, ...)
{
  diagnostic_info diagnostic;
  va_list ap;

  va_start (ap, gmsgid);
  diagnostic_set_info (&diagnostic, gmsgid, &ap, input_location,
		       pedantic_error_kind ());
  report_diagnostic (&diagnostic);
  va_end (ap);
}

/* A hard error: the code is definitely ill-formed, and an object file
   will not be produced.  */
void
error (const char *gmsgid, ...)
{
  diagnostic_info diagnostic;
  va_list ap;

  va_start (ap, gmsgid);
  diagnostic_set_info (&diagnostic, gmsgid, &ap, input_location, DK_ERROR);
  report_diagnostic (&diagnostic);
  va_end (ap);
}

/* "Sorry, not implemented."  Use for a language feature which is
   required by the relevant specification but not implemented by GCC.
   An object file will not be produced.  */
void
sorry (const char *gmsgid, ...)
{
  diagnostic_info diagnostic;
  va_list ap;

  va_start (ap, gmsgid);
  diagnostic_set_info (&diagnostic, gmsgid, &ap, input_location, DK_SORRY);
  report_diagnostic (&diagnostic);
  va_end (ap);
}

/* An error which is severe enough that we make no attempt to
   continue.  Do not use this for internal consistency checks; that's
   internal_error.  Use of this function should be rare.  */
void
fatal_error (const char *gmsgid, ...)
{
  diagnostic_info diagnostic;
  va_list ap;

  va_start (ap, gmsgid);
  diagnostic_set_info (&diagnostic, gmsgid, &ap, input_location, DK_FATAL);
  report_diagnostic (&diagnostic);
  va_end (ap);

  gcc_unreachable ();
}

/* An internal consistency check has failed.  We make no attempt to
   continue.  Note that unless there is debugging value to be had from
   a more specific message, or some other good reason, you should use
   abort () instead of calling this function directly.  */
void
internal_error (const char *gmsgid, ...)
{
  diagnostic_info diagnostic;
  va_list ap;

  va_start (ap, gmsgid);
  diagnostic_set_info (&diagnostic, gmsgid, &ap, input_location, DK_ICE);
  report_diagnostic (&diagnostic);
  va_end (ap);

  gcc_unreachable ();
}

/* Special case error functions.  Most are implemented in terms of the
   above, or should be.  */

/* Print a diagnostic MSGID on FILE.  This is just fprintf, except it
   runs its second argument through gettext.  */
void
fnotice (FILE *file, const char *cmsgid, ...)
{
  va_list ap;

  va_start (ap, cmsgid);
  vfprintf (file, _(cmsgid), ap);
  va_end (ap);
}

/* Inform the user that an error occurred while trying to report some
   other error.  This indicates catastrophic internal inconsistencies,
   so give up now.  But do try to flush out the previous error.
   This mustn't use internal_error, that will cause infinite recursion.  */

static void
error_recursion (diagnostic_context *context)
{
  diagnostic_info diagnostic;

  if (context->lock < 3)
    pp_flush (context->printer);

  fnotice (stderr,
	   "Internal compiler error: Error reporting routines re-entered.\n");

  /* Call diagnostic_action_after_output to get the "please submit a bug
     report" message.  It only looks at the kind field of diagnostic_info.  */
  diagnostic.kind = DK_ICE;
  diagnostic_action_after_output (context, &diagnostic);

  /* Do not use gcc_unreachable here; that goes through internal_error
     and therefore would cause infinite recursion.  */
  real_abort ();
}

/* Report an internal compiler error in a friendly manner.  This is
   the function that gets called upon use of abort() in the source
   code generally, thanks to a special macro.  */

void
fancy_abort (const char *file, int line, const char *function)
{
  internal_error ("in %s, at %s:%d", function, trim_filename (file), line);
}

/* Really call the system 'abort'.  This has to go right at the end of
   this file, so that there are no functions after it that call abort
   and get the system abort instead of our macro.  */
#undef abort
static void
real_abort (void)
{
  abort ();
}
