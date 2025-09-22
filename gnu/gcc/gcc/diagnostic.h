/* Various declarations for language-independent diagnostics subroutines.
   Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005, 2006
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

#ifndef GCC_DIAGNOSTIC_H
#define GCC_DIAGNOSTIC_H

#include "pretty-print.h"
#include "options.h"

/* Constants used to discriminate diagnostics.  */
typedef enum
{
#define DEFINE_DIAGNOSTIC_KIND(K, msgid) K,  
#include "diagnostic.def"
#undef DEFINE_DIAGNOSTIC_KIND
  DK_LAST_DIAGNOSTIC_KIND
} diagnostic_t;

/* A diagnostic is described by the MESSAGE to send, the FILE and LINE of
   its context and its KIND (ice, error, warning, note, ...)  See complete
   list in diagnostic.def.  */
typedef struct
{
  text_info message;
  location_t location;
  /* The kind of diagnostic it is about.  */
  diagnostic_t kind;
  /* Which OPT_* directly controls this diagnostic.  */
  int option_index;
} diagnostic_info;

#define pedantic_error_kind() (flag_pedantic_errors ? DK_ERROR : DK_WARNING)


/*  Forward declarations.  */
typedef struct diagnostic_context diagnostic_context;
typedef void (*diagnostic_starter_fn) (diagnostic_context *,
				       diagnostic_info *);
typedef diagnostic_starter_fn diagnostic_finalizer_fn;

/* This data structure bundles altogether any information relevant to
   the context of a diagnostic message.  */
struct diagnostic_context
{
  /* Where most of the diagnostic formatting work is done.  */
  pretty_printer *printer;

  /* The number of times we have issued diagnostics.  */
  int diagnostic_count[DK_LAST_DIAGNOSTIC_KIND];

  /* True if we should display the "warnings are being tread as error"
     message, usually displayed once per compiler run.  */
  bool issue_warnings_are_errors_message;
  
  /* True if it has been requested that warnings be treated as errors.  */
  bool warning_as_error_requested;

  /* For each option index that can be passed to warning() et all
     (OPT_* from options.h), this array may contain a new kind that
     the diagnostic should be changed to before reporting, or
     DK_UNSPECIFIED to leave it as the reported kind, or DK_IGNORED to
     not report it at all.  N_OPTS is from <options.h>.  */
  char classify_diagnostic[N_OPTS];

  /* True if we should print the command line option which controls
     each diagnostic, if known.  */
  bool show_option_requested;

  /* True if we should raise a SIGABRT on errors.  */
  bool abort_on_error;

  /* This function is called before any message is printed out.  It is
     responsible for preparing message prefix and such.  For example, it
     might say:
     In file included from "/usr/local/include/curses.h:5:
                      from "/home/gdr/src/nifty_printer.h:56:
                      ...
  */
  diagnostic_starter_fn begin_diagnostic;

  /* This function is called after the diagnostic message is printed.  */
  diagnostic_finalizer_fn end_diagnostic;

  /* Client hook to report an internal error.  */
  void (*internal_error) (const char *, va_list *);

  /* Function of last diagnostic message; more generally, function such that
     if next diagnostic message is in it then we don't have to mention the
     function name.  */
  tree last_function;

  /* Used to detect when input_file_stack has changed since last described.  */
  int last_module;

  int lock;
};

/* Client supplied function to announce a diagnostic.  */
#define diagnostic_starter(DC) (DC)->begin_diagnostic

/* Client supplied function called after a diagnostic message is
   displayed.  */
#define diagnostic_finalizer(DC) (DC)->end_diagnostic

/* Extension hook for client.  */
#define diagnostic_auxiliary_data(DC) (DC)->x_data

/* Same as pp_format_decoder.  Works on 'diagnostic_context *'.  */
#define diagnostic_format_decoder(DC) ((DC)->printer->format_decoder)

/* Same as output_prefixing_rule.  Works on 'diagnostic_context *'.  */
#define diagnostic_prefixing_rule(DC) ((DC)->printer->wrapping.rule)

/* Maximum characters per line in automatic line wrapping mode.
   Zero means don't wrap lines.  */
#define diagnostic_line_cutoff(DC) ((DC)->printer->wrapping.line_cutoff)

#define diagnostic_flush_buffer(DC) pp_base_flush ((DC)->printer)

/* True if the last function in which a diagnostic was reported is
   different from the current one.  */
#define diagnostic_last_function_changed(DC) \
  ((DC)->last_function != current_function_decl)

/* Remember the current function as being the last one in which we report
   a diagnostic.  */
#define diagnostic_set_last_function(DC) \
  (DC)->last_function = current_function_decl

/* True if the last module or file in which a diagnostic was reported is
   different from the current one.  */
#define diagnostic_last_module_changed(DC) \
  ((DC)->last_module != input_file_stack_tick)

/* Remember the current module or file as being the last one in which we
   report a diagnostic.  */
#define diagnostic_set_last_module(DC) \
  (DC)->last_module = input_file_stack_tick

/* Raise SIGABRT on any diagnostic of severity DK_ERROR or higher.  */
#define diagnostic_abort_on_error(DC) \
  (DC)->abort_on_error = true

/* This diagnostic_context is used by front-ends that directly output
   diagnostic messages without going through `error', `warning',
   and similar functions.  */
extern diagnostic_context *global_dc;

/* The total count of a KIND of diagnostics emitted so far.  */
#define diagnostic_kind_count(DC, DK) (DC)->diagnostic_count[(int) (DK)]

/* The number of errors that have been issued so far.  Ideally, these
   would take a diagnostic_context as an argument.  */
#define errorcount diagnostic_kind_count (global_dc, DK_ERROR)
/* Similarly, but for warnings.  */
#define warningcount diagnostic_kind_count (global_dc, DK_WARNING)
/* Similarly, but for sorrys.  */
#define sorrycount diagnostic_kind_count (global_dc, DK_SORRY)

/* Returns nonzero if warnings should be emitted.  */
#define diagnostic_report_warnings_p()			\
  (!inhibit_warnings					\
   && !(in_system_header && !warn_system_headers))

#define report_diagnostic(D) diagnostic_report_diagnostic (global_dc, D)

/* Diagnostic related functions.  */
extern void diagnostic_initialize (diagnostic_context *);
extern void diagnostic_report_current_module (diagnostic_context *);
extern void diagnostic_report_current_function (diagnostic_context *);

/* Force diagnostics controlled by OPTIDX to be kind KIND.  */
extern diagnostic_t diagnostic_classify_diagnostic (diagnostic_context *,
						    int /* optidx */,
						    diagnostic_t /* kind */);
extern void diagnostic_report_diagnostic (diagnostic_context *,
					  diagnostic_info *);
#ifdef ATTRIBUTE_GCC_DIAG
extern void diagnostic_set_info (diagnostic_info *, const char *, va_list *,
				 location_t, diagnostic_t) ATTRIBUTE_GCC_DIAG(2,0);
extern void diagnostic_set_info_translated (diagnostic_info *, const char *,
					    va_list *, location_t,
					    diagnostic_t)
     ATTRIBUTE_GCC_DIAG(2,0);
#endif
extern char *diagnostic_build_prefix (diagnostic_info *);

/* Pure text formatting support functions.  */
extern char *file_name_as_prefix (const char *);

/* In tree-pretty-print.c  */
extern int dump_generic_node (pretty_printer *, tree, int, int, bool);
extern void print_generic_stmt (FILE *, tree, int);
extern void print_generic_stmt_indented (FILE *, tree, int, int);
extern void print_generic_expr (FILE *, tree, int);
extern void print_generic_decl (FILE *, tree, int);

extern void debug_generic_expr (tree);
extern void debug_generic_stmt (tree);
extern void debug_tree_chain (tree);
extern void debug_c_tree (tree);
#endif /* ! GCC_DIAGNOSTIC_H */
