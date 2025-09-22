/* Various declarations for language-independent pretty-print subroutines.
   Copyright (C) 2002, 2003, 2004 Free Software Foundation, Inc.
   Contributed by Gabriel Dos Reis <gdr@integrable-solutions.net>

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

#ifndef GCC_PRETTY_PRINT_H
#define GCC_PRETTY_PRINT_H

#include "obstack.h"
#include "input.h"

/* Maximum number of format string arguments.  */
#define PP_NL_ARGMAX   30

/* The type of a text to be formatted according a format specification
   along with a list of things.  */
typedef struct
{
  const char *format_spec;
  va_list *args_ptr;
  int err_no;  /* for %m */
  location_t *locus;
} text_info;

/* How often diagnostics are prefixed by their locations:
   o DIAGNOSTICS_SHOW_PREFIX_NEVER: never - not yet supported;
   o DIAGNOSTICS_SHOW_PREFIX_ONCE: emit only once;
   o DIAGNOSTICS_SHOW_PREFIX_EVERY_LINE: emit each time a physical
   line is started.  */
typedef enum
{
  DIAGNOSTICS_SHOW_PREFIX_ONCE       = 0x0,
  DIAGNOSTICS_SHOW_PREFIX_NEVER      = 0x1,
  DIAGNOSTICS_SHOW_PREFIX_EVERY_LINE = 0x2
} diagnostic_prefixing_rule_t;

/* The chunk_info data structure forms a stack of the results from the
   first phase of formatting (pp_base_format) which have not yet been
   output (pp_base_output_formatted_text).  A stack is necessary because
   the diagnostic starter may decide to generate its own output by way
   of the formatter.  */
struct chunk_info
{
  /* Pointer to previous chunk on the stack.  */
  struct chunk_info *prev;

  /* Array of chunks to output.  Each chunk is a NUL-terminated string.
     In the first phase of formatting, even-numbered chunks are
     to be output verbatim, odd-numbered chunks are format specifiers.
     The second phase replaces all odd-numbered chunks with formatted
     text, and the third phase simply emits all the chunks in sequence
     with appropriate line-wrapping.  */
  const char *args[PP_NL_ARGMAX * 2];
};

/* The output buffer datatype.  This is best seen as an abstract datatype
   whose fields should not be accessed directly by clients.  */
typedef struct 
{
  /* Obstack where the text is built up.  */  
  struct obstack formatted_obstack;

  /* Obstack containing a chunked representation of the format
     specification plus arguments.  */
  struct obstack chunk_obstack;

  /* Currently active obstack: one of the above two.  This is used so
     that the text formatters don't need to know which phase we're in.  */
  struct obstack *obstack;

  /* Stack of chunk arrays.  These come from the chunk_obstack.  */
  struct chunk_info *cur_chunk_array;

  /* Where to output formatted text.  */
  FILE *stream;

  /* The amount of characters output so far.  */  
  int line_length;

  /* This must be large enough to hold any printed integer or
     floating-point value.  */
  char digit_buffer[128];
} output_buffer;

/* The type of pretty-printer flags passed to clients.  */
typedef unsigned int pp_flags;

typedef enum
{
  pp_none, pp_before, pp_after
} pp_padding;

/* Structure for switching in and out of verbatim mode in a convenient
   manner.  */
typedef struct
{
  /* Current prefixing rule.  */
  diagnostic_prefixing_rule_t rule;

  /* The ideal upper bound of number of characters per line, as suggested
     by front-end.  */  
  int line_cutoff;
} pp_wrapping_mode_t;

/* Maximum characters per line in automatic line wrapping mode.
   Zero means don't wrap lines.  */
#define pp_line_cutoff(PP)  pp_base (PP)->wrapping.line_cutoff

/* Prefixing rule used in formatting a diagnostic message.  */
#define pp_prefixing_rule(PP)  pp_base (PP)->wrapping.rule

/* Get or set the wrapping mode as a single entity.  */
#define pp_wrapping_mode(PP) pp_base (PP)->wrapping

/* The type of a hook that formats client-specific data onto a pretty_pinter.
   A client-supplied formatter returns true if everything goes well,
   otherwise it returns false.  */
typedef struct pretty_print_info pretty_printer;
typedef bool (*printer_fn) (pretty_printer *, text_info *, const char *,
			    int, bool, bool, bool);

/* Client supplied function used to decode formats.  */
#define pp_format_decoder(PP) pp_base (PP)->format_decoder

/* TRUE if a newline character needs to be added before further
   formatting.  */
#define pp_needs_newline(PP)  pp_base (PP)->need_newline 

/* True if PRETTY-PTINTER is in line-wrapping mode.  */
#define pp_is_wrapping_line(PP) (pp_line_cutoff (PP) > 0)

/* The amount of whitespace to be emitted when starting a new line.  */
#define pp_indentation(PP) pp_base (PP)->indent_skip

/* The data structure that contains the bare minimum required to do
   proper pretty-printing.  Clients may derived from this structure
   and add additional fields they need.  */
struct pretty_print_info
{
  /* Where we print external representation of ENTITY.  */
  output_buffer *buffer;

  /* The prefix for each new line.  */
  const char *prefix;
  
  /* Where to put whitespace around the entity being formatted.  */
  pp_padding padding;
  
  /* The real upper bound of number of characters per line, taking into
     account the case of a very very looong prefix.  */  
  int maximum_length;

  /* Indentation count.  */
  int indent_skip;

  /* Current wrapping mode.  */
  pp_wrapping_mode_t wrapping;

  /* If non-NULL, this function formats a TEXT into the BUFFER.  When called,
     TEXT->format_spec points to a format code.  FORMAT_DECODER should call
     pp_string (and related functions) to add data to the BUFFER.
     FORMAT_DECODER can read arguments from *TEXT->args_pts using VA_ARG.
     If the BUFFER needs additional characters from the format string, it
     should advance the TEXT->format_spec as it goes.  When FORMAT_DECODER
     returns, TEXT->format_spec should point to the last character processed.
  */
  printer_fn format_decoder;

  /* Nonzero if current PREFIX was emitted at least once.  */
  bool emitted_prefix;

  /* Nonzero means one should emit a newline before outputting anything.  */
  bool need_newline;
};

#define pp_set_line_maximum_length(PP, L) \
   pp_base_set_line_maximum_length (pp_base (PP), L)
#define pp_set_prefix(PP, P)    pp_base_set_prefix (pp_base (PP), P)
#define pp_destroy_prefix(PP)   pp_base_destroy_prefix (pp_base (PP))
#define pp_remaining_character_count_for_line(PP) \
  pp_base_remaining_character_count_for_line (pp_base (PP))
#define pp_clear_output_area(PP) \
  pp_base_clear_output_area (pp_base (PP))
#define pp_formatted_text(PP)   pp_base_formatted_text (pp_base (PP))
#define pp_last_position_in_text(PP) \
  pp_base_last_position_in_text (pp_base (PP))
#define pp_emit_prefix(PP)      pp_base_emit_prefix (pp_base (PP))
#define pp_append_text(PP, B, E) \
  pp_base_append_text (pp_base (PP), B, E)
#define pp_flush(PP)            pp_base_flush (pp_base (PP))
#define pp_format(PP, TI)       pp_base_format (pp_base (PP), TI)
#define pp_output_formatted_text(PP) \
  pp_base_output_formatted_text (pp_base (PP))
#define pp_format_verbatim(PP, TI) \
  pp_base_format_verbatim (pp_base (PP), TI)

#define pp_character(PP, C)     pp_base_character (pp_base (PP), C)
#define pp_string(PP, S)        pp_base_string (pp_base (PP), S)
#define pp_newline(PP)          pp_base_newline (pp_base (PP))

#define pp_space(PP)            pp_character (PP, ' ')
#define pp_left_paren(PP)       pp_character (PP, '(')
#define pp_right_paren(PP)      pp_character (PP, ')')
#define pp_left_bracket(PP)     pp_character (PP, '[')
#define pp_right_bracket(PP)    pp_character (PP, ']')
#define pp_left_brace(PP)       pp_character (PP, '{')
#define pp_right_brace(PP)      pp_character (PP, '}')
#define pp_semicolon(PP)        pp_character (PP, ';')
#define pp_comma(PP)            pp_string (PP, ", ")
#define pp_dot(PP)              pp_character (PP, '.')
#define pp_colon(PP)            pp_character (PP, ':')
#define pp_colon_colon(PP)      pp_string (PP, "::")
#define pp_arrow(PP)            pp_string (PP, "->")
#define pp_equal(PP)            pp_character (PP, '=')
#define pp_question(PP)         pp_character (PP, '?')
#define pp_bar(PP)              pp_character (PP, '|')
#define pp_carret(PP)           pp_character (PP, '^')
#define pp_ampersand(PP)        pp_character (PP, '&')
#define pp_less(PP)             pp_character (PP, '<')
#define pp_greater(PP)          pp_character (PP, '>')
#define pp_plus(PP)             pp_character (PP, '+')
#define pp_minus(PP)            pp_character (PP, '-')
#define pp_star(PP)             pp_character (PP, '*')
#define pp_slash(PP)            pp_character (PP, '/')
#define pp_modulo(PP)           pp_character (PP, '%')
#define pp_exclamation(PP)      pp_character (PP, '!')
#define pp_complement(PP)       pp_character (PP, '~')
#define pp_quote(PP)            pp_character (PP, '\'')
#define pp_backquote(PP)        pp_character (PP, '`')
#define pp_doublequote(PP)      pp_character (PP, '"')
#define pp_newline_and_indent(PP, N) \
  do {                               \
    pp_indentation (PP) += N;        \
    pp_newline (PP);                 \
    pp_base_indent (pp_base (PP));   \
    pp_needs_newline (PP) = false;   \
  } while (0)
#define pp_maybe_newline_and_indent(PP, N) \
  if (pp_needs_newline (PP)) pp_newline_and_indent (PP, N)
#define pp_maybe_space(PP)   pp_base_maybe_space (pp_base (PP))
#define pp_separate_with(PP, C)     \
   do {                             \
     pp_character (PP, C);          \
     pp_space (PP);                 \
   } while (0)
#define pp_scalar(PP, FORMAT, SCALAR)	                      \
  do					        	      \
    {			         			      \
      sprintf (pp_buffer (PP)->digit_buffer, FORMAT, SCALAR); \
      pp_string (PP, pp_buffer (PP)->digit_buffer);           \
    }						              \
  while (0)
#define pp_decimal_int(PP, I)  pp_scalar (PP, "%d", I)
#define pp_wide_integer(PP, I) \
   pp_scalar (PP, HOST_WIDE_INT_PRINT_DEC, (HOST_WIDE_INT) I)
#define pp_widest_integer(PP, I) \
   pp_scalar (PP, HOST_WIDEST_INT_PRINT_DEC, (HOST_WIDEST_INT) I)
#define pp_pointer(PP, P)      pp_scalar (PP, "%p", P)

#define pp_identifier(PP, ID)  pp_string (PP, ID)
#define pp_tree_identifier(PP, T)                      \
  pp_append_text(PP, IDENTIFIER_POINTER (T), \
                 IDENTIFIER_POINTER (T) + IDENTIFIER_LENGTH (T))

#define pp_unsupported_tree(PP, T)                         \
  pp_verbatim (pp_base (PP), "#%qs not supported by %s#", \
               tree_code_name[(int) TREE_CODE (T)], __FUNCTION__)


#define pp_buffer(PP) pp_base (PP)->buffer
/* Clients that directly derive from pretty_printer need to override
   this macro to return a pointer to the base pretty_printer structure.  */
#define pp_base(PP) (PP)

extern void pp_construct (pretty_printer *, const char *, int);
extern void pp_base_set_line_maximum_length (pretty_printer *, int);
extern void pp_base_set_prefix (pretty_printer *, const char *);
extern void pp_base_destroy_prefix (pretty_printer *);
extern int pp_base_remaining_character_count_for_line (pretty_printer *);
extern void pp_base_clear_output_area (pretty_printer *);
extern const char *pp_base_formatted_text (pretty_printer *);
extern const char *pp_base_last_position_in_text (const pretty_printer *);
extern void pp_base_emit_prefix (pretty_printer *);
extern void pp_base_append_text (pretty_printer *, const char *, const char *);

/* This header may be included before toplev.h, hence the duplicate
   definitions to allow for GCC-specific formats.  */
#if GCC_VERSION >= 3005
#define ATTRIBUTE_GCC_PPDIAG(m, n) __attribute__ ((__format__ (__gcc_diag__, m ,n))) ATTRIBUTE_NONNULL(m)
#else
#define ATTRIBUTE_GCC_PPDIAG(m, n) ATTRIBUTE_NONNULL(m)
#endif
extern void pp_printf (pretty_printer *, const char *, ...)
     ATTRIBUTE_GCC_PPDIAG(2,3);

extern void pp_verbatim (pretty_printer *, const char *, ...)
     ATTRIBUTE_GCC_PPDIAG(2,3);
extern void pp_base_flush (pretty_printer *);
extern void pp_base_format (pretty_printer *, text_info *);
extern void pp_base_output_formatted_text (pretty_printer *);
extern void pp_base_format_verbatim (pretty_printer *, text_info *);

extern void pp_base_indent (pretty_printer *);
extern void pp_base_newline (pretty_printer *);
extern void pp_base_character (pretty_printer *, int);
extern void pp_base_string (pretty_printer *, const char *);
extern void pp_write_text_to_stream (pretty_printer *pp);
extern void pp_base_maybe_space (pretty_printer *);

/* Switch into verbatim mode and return the old mode.  */
static inline pp_wrapping_mode_t
pp_set_verbatim_wrapping_ (pretty_printer *pp)
{
  pp_wrapping_mode_t oldmode = pp_wrapping_mode (pp);
  pp_line_cutoff (pp) = 0;
  pp_prefixing_rule (pp) = DIAGNOSTICS_SHOW_PREFIX_NEVER;
  return oldmode;
}
#define pp_set_verbatim_wrapping(PP) pp_set_verbatim_wrapping_ (pp_base (PP))

#endif /* GCC_PRETTY_PRINT_H */
