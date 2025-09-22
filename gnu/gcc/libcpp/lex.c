/* CPP Library - lexical analysis.
   Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Per Bothner, 1994-95.
   Based on CCCP program by Paul Rubin, June 1986
   Adapted to ANSI C, Richard Stallman, Jan 1987
   Broken out to separate file, Zack Weinberg, Mar 2000

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
#include "cpplib.h"
#include "internal.h"

enum spell_type
{
  SPELL_OPERATOR = 0,
  SPELL_IDENT,
  SPELL_LITERAL,
  SPELL_NONE
};

struct token_spelling
{
  enum spell_type category;
  const unsigned char *name;
};

static const unsigned char *const digraph_spellings[] =
{ U"%:", U"%:%:", U"<:", U":>", U"<%", U"%>" };

#define OP(e, s) { SPELL_OPERATOR, U s  },
#define TK(e, s) { SPELL_ ## s,    U #e },
static const struct token_spelling token_spellings[N_TTYPES] = { TTYPE_TABLE };
#undef OP
#undef TK

#define TOKEN_SPELL(token) (token_spellings[(token)->type].category)
#define TOKEN_NAME(token) (token_spellings[(token)->type].name)

static void add_line_note (cpp_buffer *, const uchar *, unsigned int);
static int skip_line_comment (cpp_reader *);
static void skip_whitespace (cpp_reader *, cppchar_t);
static void lex_string (cpp_reader *, cpp_token *, const uchar *);
static void save_comment (cpp_reader *, cpp_token *, const uchar *, cppchar_t);
static void create_literal (cpp_reader *, cpp_token *, const uchar *,
			    unsigned int, enum cpp_ttype);
static bool warn_in_comment (cpp_reader *, _cpp_line_note *);
static int name_p (cpp_reader *, const cpp_string *);
static tokenrun *next_tokenrun (tokenrun *);

static _cpp_buff *new_buff (size_t);


/* Utility routine:

   Compares, the token TOKEN to the NUL-terminated string STRING.
   TOKEN must be a CPP_NAME.  Returns 1 for equal, 0 for unequal.  */
int
cpp_ideq (const cpp_token *token, const char *string)
{
  if (token->type != CPP_NAME)
    return 0;

  return !ustrcmp (NODE_NAME (token->val.node), (const uchar *) string);
}

/* Record a note TYPE at byte POS into the current cleaned logical
   line.  */
static void
add_line_note (cpp_buffer *buffer, const uchar *pos, unsigned int type)
{
  if (buffer->notes_used == buffer->notes_cap)
    {
      buffer->notes_cap = buffer->notes_cap * 2 + 200;
      buffer->notes = XRESIZEVEC (_cpp_line_note, buffer->notes,
                                  buffer->notes_cap);
    }

  buffer->notes[buffer->notes_used].pos = pos;
  buffer->notes[buffer->notes_used].type = type;
  buffer->notes_used++;
}

/* Returns with a logical line that contains no escaped newlines or
   trigraphs.  This is a time-critical inner loop.  */
void
_cpp_clean_line (cpp_reader *pfile)
{
  cpp_buffer *buffer;
  const uchar *s;
  uchar c, *d, *p;

  buffer = pfile->buffer;
  buffer->cur_note = buffer->notes_used = 0;
  buffer->cur = buffer->line_base = buffer->next_line;
  buffer->need_line = false;
  s = buffer->next_line - 1;

  if (!buffer->from_stage3)
    {
      /* Short circuit for the common case of an un-escaped line with
	 no trigraphs.  The primary win here is by not writing any
	 data back to memory until we have to.  */
      for (;;)
	{
	  c = *++s;
	  if (c == '\n' || c == '\r')
	    {
	      d = (uchar *) s;

	      if (s == buffer->rlimit)
		goto done;

	      /* DOS line ending? */
	      if (c == '\r' && s[1] == '\n')
		s++;

	      if (s == buffer->rlimit)
		goto done;

	      /* check for escaped newline */
	      p = d;
	      while (p != buffer->next_line && is_nvspace (p[-1]))
		p--;
	      if (p == buffer->next_line || p[-1] != '\\')
		goto done;

	      /* Have an escaped newline; process it and proceed to
		 the slow path.  */
	      add_line_note (buffer, p - 1, p != d ? ' ' : '\\');
	      d = p - 2;
	      buffer->next_line = p - 1;
	      break;
	    }
	  if (c == '?' && s[1] == '?' && _cpp_trigraph_map[s[2]])
	    {
	      /* Have a trigraph.  We may or may not have to convert
		 it.  Add a line note regardless, for -Wtrigraphs.  */
	      add_line_note (buffer, s, s[2]);
	      if (CPP_OPTION (pfile, trigraphs))
		{
		  /* We do, and that means we have to switch to the
		     slow path.  */
		  d = (uchar *) s;
		  *d = _cpp_trigraph_map[s[2]];
		  s += 2;
		  break;
		}
	    }
	}


      for (;;)
	{
	  c = *++s;
	  *++d = c;

	  if (c == '\n' || c == '\r')
	    {
		  /* Handle DOS line endings.  */
	      if (c == '\r' && s != buffer->rlimit && s[1] == '\n')
		s++;
	      if (s == buffer->rlimit)
		break;

	      /* Escaped?  */
	      p = d;
	      while (p != buffer->next_line && is_nvspace (p[-1]))
		p--;
	      if (p == buffer->next_line || p[-1] != '\\')
		break;

	      add_line_note (buffer, p - 1, p != d ? ' ': '\\');
	      d = p - 2;
	      buffer->next_line = p - 1;
	    }
	  else if (c == '?' && s[1] == '?' && _cpp_trigraph_map[s[2]])
	    {
	      /* Add a note regardless, for the benefit of -Wtrigraphs.  */
	      add_line_note (buffer, d, s[2]);
	      if (CPP_OPTION (pfile, trigraphs))
		{
		  *d = _cpp_trigraph_map[s[2]];
		  s += 2;
		}
	    }
	}
    }
  else
    {
      do
	s++;
      while (*s != '\n' && *s != '\r');
      d = (uchar *) s;

      /* Handle DOS line endings.  */
      if (*s == '\r' && s != buffer->rlimit && s[1] == '\n')
	s++;
    }

 done:
  *d = '\n';
  /* A sentinel note that should never be processed.  */
  add_line_note (buffer, d + 1, '\n');
  buffer->next_line = s + 1;
}

/* Return true if the trigraph indicated by NOTE should be warned
   about in a comment.  */
static bool
warn_in_comment (cpp_reader *pfile, _cpp_line_note *note)
{
  const uchar *p;

  /* Within comments we don't warn about trigraphs, unless the
     trigraph forms an escaped newline, as that may change
     behavior.  */
  if (note->type != '/')
    return false;

  /* If -trigraphs, then this was an escaped newline iff the next note
     is coincident.  */
  if (CPP_OPTION (pfile, trigraphs))
    return note[1].pos == note->pos;

  /* Otherwise, see if this forms an escaped newline.  */
  p = note->pos + 3;
  while (is_nvspace (*p))
    p++;

  /* There might have been escaped newlines between the trigraph and the
     newline we found.  Hence the position test.  */
  return (*p == '\n' && p < note[1].pos);
}

/* Process the notes created by add_line_note as far as the current
   location.  */
void
_cpp_process_line_notes (cpp_reader *pfile, int in_comment)
{
  cpp_buffer *buffer = pfile->buffer;

  for (;;)
    {
      _cpp_line_note *note = &buffer->notes[buffer->cur_note];
      unsigned int col;

      if (note->pos > buffer->cur)
	break;

      buffer->cur_note++;
      col = CPP_BUF_COLUMN (buffer, note->pos + 1);

      if (note->type == '\\' || note->type == ' ')
	{
	  if (note->type == ' ' && !in_comment)
	    cpp_error_with_line (pfile, CPP_DL_WARNING, pfile->line_table->highest_line, col,
				 "backslash and newline separated by space");

	  if (buffer->next_line > buffer->rlimit)
	    {
	      cpp_error_with_line (pfile, CPP_DL_PEDWARN, pfile->line_table->highest_line, col,
				   "backslash-newline at end of file");
	      /* Prevent "no newline at end of file" warning.  */
	      buffer->next_line = buffer->rlimit;
	    }

	  buffer->line_base = note->pos;
	  CPP_INCREMENT_LINE (pfile, 0);
	}
      else if (_cpp_trigraph_map[note->type])
	{
	  if (CPP_OPTION (pfile, warn_trigraphs)
	      && (!in_comment || warn_in_comment (pfile, note)))
	    {
	      if (CPP_OPTION (pfile, trigraphs))
		cpp_error_with_line (pfile, CPP_DL_WARNING, pfile->line_table->highest_line, col,
				     "trigraph ??%c converted to %c",
				     note->type,
				     (int) _cpp_trigraph_map[note->type]);
	      else
		{
		  cpp_error_with_line 
		    (pfile, CPP_DL_WARNING, pfile->line_table->highest_line, col,
		     "trigraph ??%c ignored, use -trigraphs to enable",
		     note->type);
		}
	    }
	}
      else
	abort ();
    }
}

/* Skip a C-style block comment.  We find the end of the comment by
   seeing if an asterisk is before every '/' we encounter.  Returns
   nonzero if comment terminated by EOF, zero otherwise.

   Buffer->cur points to the initial asterisk of the comment.  */
bool
_cpp_skip_block_comment (cpp_reader *pfile)
{
  cpp_buffer *buffer = pfile->buffer;
  const uchar *cur = buffer->cur;
  uchar c;

  cur++;
  if (*cur == '/')
    cur++;

  for (;;)
    {
      /* People like decorating comments with '*', so check for '/'
	 instead for efficiency.  */
      c = *cur++;

      if (c == '/')
	{
	  if (cur[-2] == '*')
	    break;

	  /* Warn about potential nested comments, but not if the '/'
	     comes immediately before the true comment delimiter.
	     Don't bother to get it right across escaped newlines.  */
	  if (CPP_OPTION (pfile, warn_comments)
	      && cur[0] == '*' && cur[1] != '/')
	    {
	      buffer->cur = cur;
	      cpp_error_with_line (pfile, CPP_DL_WARNING,
				   pfile->line_table->highest_line, CPP_BUF_COL (buffer),
				   "\"/*\" within comment");
	    }
	}
      else if (c == '\n')
	{
	  unsigned int cols;
	  buffer->cur = cur - 1;
	  _cpp_process_line_notes (pfile, true);
	  if (buffer->next_line >= buffer->rlimit)
	    return true;
	  _cpp_clean_line (pfile);

	  cols = buffer->next_line - buffer->line_base;
	  CPP_INCREMENT_LINE (pfile, cols);

	  cur = buffer->cur;
	}
    }

  buffer->cur = cur;
  _cpp_process_line_notes (pfile, true);
  return false;
}

/* Skip a C++ line comment, leaving buffer->cur pointing to the
   terminating newline.  Handles escaped newlines.  Returns nonzero
   if a multiline comment.  */
static int
skip_line_comment (cpp_reader *pfile)
{
  cpp_buffer *buffer = pfile->buffer;
  unsigned int orig_line = pfile->line_table->highest_line;

  while (*buffer->cur != '\n')
    buffer->cur++;

  _cpp_process_line_notes (pfile, true);
  return orig_line != pfile->line_table->highest_line;
}

/* Skips whitespace, saving the next non-whitespace character.  */
static void
skip_whitespace (cpp_reader *pfile, cppchar_t c)
{
  cpp_buffer *buffer = pfile->buffer;
  bool saw_NUL = false;

  do
    {
      /* Horizontal space always OK.  */
      if (c == ' ' || c == '\t')
	;
      /* Just \f \v or \0 left.  */
      else if (c == '\0')
	saw_NUL = true;
      else if (pfile->state.in_directive && CPP_PEDANTIC (pfile))
	cpp_error_with_line (pfile, CPP_DL_PEDWARN, pfile->line_table->highest_line,
			     CPP_BUF_COL (buffer),
			     "%s in preprocessing directive",
			     c == '\f' ? "form feed" : "vertical tab");

      c = *buffer->cur++;
    }
  /* We only want non-vertical space, i.e. ' ' \t \f \v \0.  */
  while (is_nvspace (c));

  if (saw_NUL)
    cpp_error (pfile, CPP_DL_WARNING, "null character(s) ignored");

  buffer->cur--;
}

/* See if the characters of a number token are valid in a name (no
   '.', '+' or '-').  */
static int
name_p (cpp_reader *pfile, const cpp_string *string)
{
  unsigned int i;

  for (i = 0; i < string->len; i++)
    if (!is_idchar (string->text[i]))
      return 0;

  return 1;
}

/* After parsing an identifier or other sequence, produce a warning about
   sequences not in NFC/NFKC.  */
static void
warn_about_normalization (cpp_reader *pfile, 
			  const cpp_token *token,
			  const struct normalize_state *s)
{
  if (CPP_OPTION (pfile, warn_normalize) < NORMALIZE_STATE_RESULT (s)
      && !pfile->state.skipping)
    {
      /* Make sure that the token is printed using UCNs, even
	 if we'd otherwise happily print UTF-8.  */
      unsigned char *buf = XNEWVEC (unsigned char, cpp_token_len (token));
      size_t sz;

      sz = cpp_spell_token (pfile, token, buf, false) - buf;
      if (NORMALIZE_STATE_RESULT (s) == normalized_C)
	cpp_error_with_line (pfile, CPP_DL_WARNING, token->src_loc, 0,
			     "`%.*s' is not in NFKC", (int) sz, buf);
      else
	cpp_error_with_line (pfile, CPP_DL_WARNING, token->src_loc, 0,
			     "`%.*s' is not in NFC", (int) sz, buf);
    }
}

/* Returns TRUE if the sequence starting at buffer->cur is invalid in
   an identifier.  FIRST is TRUE if this starts an identifier.  */
static bool
forms_identifier_p (cpp_reader *pfile, int first,
		    struct normalize_state *state)
{
  cpp_buffer *buffer = pfile->buffer;

  if (*buffer->cur == '$')
    {
      if (!CPP_OPTION (pfile, dollars_in_ident))
	return false;

      buffer->cur++;
      if (CPP_OPTION (pfile, warn_dollars) && !pfile->state.skipping)
	{
	  CPP_OPTION (pfile, warn_dollars) = 0;
	  cpp_error (pfile, CPP_DL_PEDWARN, "'$' in identifier or number");
	}

      return true;
    }

  /* Is this a syntactically valid UCN?  */
  if (CPP_OPTION (pfile, extended_identifiers)
      && *buffer->cur == '\\'
      && (buffer->cur[1] == 'u' || buffer->cur[1] == 'U'))
    {
      buffer->cur += 2;
      if (_cpp_valid_ucn (pfile, &buffer->cur, buffer->rlimit, 1 + !first,
			  state))
	return true;
      buffer->cur -= 2;
    }

  return false;
}

/* Lex an identifier starting at BUFFER->CUR - 1.  */
static cpp_hashnode *
lex_identifier (cpp_reader *pfile, const uchar *base, bool starts_ucn,
		struct normalize_state *nst)
{
  cpp_hashnode *result;
  const uchar *cur;
  unsigned int len;
  unsigned int hash = HT_HASHSTEP (0, *base);

  cur = pfile->buffer->cur;
  if (! starts_ucn)
    while (ISIDNUM (*cur))
      {
	hash = HT_HASHSTEP (hash, *cur);
	cur++;
      }
  pfile->buffer->cur = cur;
  if (starts_ucn || forms_identifier_p (pfile, false, nst))
    {
      /* Slower version for identifiers containing UCNs (or $).  */
      do {
	while (ISIDNUM (*pfile->buffer->cur))
	  {
	    pfile->buffer->cur++;
	    NORMALIZE_STATE_UPDATE_IDNUM (nst);
	  }
      } while (forms_identifier_p (pfile, false, nst));
      result = _cpp_interpret_identifier (pfile, base,
					  pfile->buffer->cur - base);
    }
  else
    {
      len = cur - base;
      hash = HT_HASHFINISH (hash, len);

      result = (cpp_hashnode *)
	ht_lookup_with_hash (pfile->hash_table, base, len, hash, HT_ALLOC);
    }

  /* Rarely, identifiers require diagnostics when lexed.  */
  if (__builtin_expect ((result->flags & NODE_DIAGNOSTIC)
			&& !pfile->state.skipping, 0))
    {
      /* It is allowed to poison the same identifier twice.  */
      if ((result->flags & NODE_POISONED) && !pfile->state.poisoned_ok)
	cpp_error (pfile, CPP_DL_ERROR, "attempt to use poisoned \"%s\"",
		   NODE_NAME (result));

      /* Constraint 6.10.3.5: __VA_ARGS__ should only appear in the
	 replacement list of a variadic macro.  */
      if (result == pfile->spec_nodes.n__VA_ARGS__
	  && !pfile->state.va_args_ok)
	cpp_error (pfile, CPP_DL_PEDWARN,
		   "__VA_ARGS__ can only appear in the expansion"
		   " of a C99 variadic macro");
    }

  return result;
}

/* Lex a number to NUMBER starting at BUFFER->CUR - 1.  */
static void
lex_number (cpp_reader *pfile, cpp_string *number,
	    struct normalize_state *nst)
{
  const uchar *cur;
  const uchar *base;
  uchar *dest;

  base = pfile->buffer->cur - 1;
  do
    {
      cur = pfile->buffer->cur;

      /* N.B. ISIDNUM does not include $.  */
      while (ISIDNUM (*cur) || *cur == '.' || VALID_SIGN (*cur, cur[-1]))
	{
	  cur++;
	  NORMALIZE_STATE_UPDATE_IDNUM (nst);
	}

      pfile->buffer->cur = cur;
    }
  while (forms_identifier_p (pfile, false, nst));

  number->len = cur - base;
  dest = _cpp_unaligned_alloc (pfile, number->len + 1);
  memcpy (dest, base, number->len);
  dest[number->len] = '\0';
  number->text = dest;
}

/* Create a token of type TYPE with a literal spelling.  */
static void
create_literal (cpp_reader *pfile, cpp_token *token, const uchar *base,
		unsigned int len, enum cpp_ttype type)
{
  uchar *dest = _cpp_unaligned_alloc (pfile, len + 1);

  memcpy (dest, base, len);
  dest[len] = '\0';
  token->type = type;
  token->val.str.len = len;
  token->val.str.text = dest;
}

/* Lexes a string, character constant, or angle-bracketed header file
   name.  The stored string contains the spelling, including opening
   quote and leading any leading 'L'.  It returns the type of the
   literal, or CPP_OTHER if it was not properly terminated.

   The spelling is NUL-terminated, but it is not guaranteed that this
   is the first NUL since embedded NULs are preserved.  */
static void
lex_string (cpp_reader *pfile, cpp_token *token, const uchar *base)
{
  bool saw_NUL = false;
  const uchar *cur;
  cppchar_t terminator;
  enum cpp_ttype type;

  cur = base;
  terminator = *cur++;
  if (terminator == 'L')
    terminator = *cur++;
  if (terminator == '\"')
    type = *base == 'L' ? CPP_WSTRING: CPP_STRING;
  else if (terminator == '\'')
    type = *base == 'L' ? CPP_WCHAR: CPP_CHAR;
  else
    terminator = '>', type = CPP_HEADER_NAME;

  for (;;)
    {
      cppchar_t c = *cur++;

      /* In #include-style directives, terminators are not escapable.  */
      if (c == '\\' && !pfile->state.angled_headers && *cur != '\n')
	cur++;
      else if (c == terminator)
	break;
      else if (c == '\n')
	{
	  cur--;
	  type = CPP_OTHER;
	  break;
	}
      else if (c == '\0')
	saw_NUL = true;
    }

  if (saw_NUL && !pfile->state.skipping)
    cpp_error (pfile, CPP_DL_WARNING,
	       "null character(s) preserved in literal");

  if (type == CPP_OTHER && CPP_OPTION (pfile, lang) != CLK_ASM)
    cpp_error (pfile, CPP_DL_PEDWARN, "missing terminating %c character",
	       (int) terminator);

  pfile->buffer->cur = cur;
  create_literal (pfile, token, base, cur - base, type);
}

/* The stored comment includes the comment start and any terminator.  */
static void
save_comment (cpp_reader *pfile, cpp_token *token, const unsigned char *from,
	      cppchar_t type)
{
  unsigned char *buffer;
  unsigned int len, clen;

  len = pfile->buffer->cur - from + 1; /* + 1 for the initial '/'.  */

  /* C++ comments probably (not definitely) have moved past a new
     line, which we don't want to save in the comment.  */
  if (is_vspace (pfile->buffer->cur[-1]))
    len--;

  /* If we are currently in a directive, then we need to store all
     C++ comments as C comments internally, and so we need to
     allocate a little extra space in that case.

     Note that the only time we encounter a directive here is
     when we are saving comments in a "#define".  */
  clen = (pfile->state.in_directive && type == '/') ? len + 2 : len;

  buffer = _cpp_unaligned_alloc (pfile, clen);

  token->type = CPP_COMMENT;
  token->val.str.len = clen;
  token->val.str.text = buffer;

  buffer[0] = '/';
  memcpy (buffer + 1, from, len - 1);

  /* Finish conversion to a C comment, if necessary.  */
  if (pfile->state.in_directive && type == '/')
    {
      buffer[1] = '*';
      buffer[clen - 2] = '*';
      buffer[clen - 1] = '/';
    }
}

/* Allocate COUNT tokens for RUN.  */
void
_cpp_init_tokenrun (tokenrun *run, unsigned int count)
{
  run->base = XNEWVEC (cpp_token, count);
  run->limit = run->base + count;
  run->next = NULL;
}

/* Returns the next tokenrun, or creates one if there is none.  */
static tokenrun *
next_tokenrun (tokenrun *run)
{
  if (run->next == NULL)
    {
      run->next = XNEW (tokenrun);
      run->next->prev = run;
      _cpp_init_tokenrun (run->next, 250);
    }

  return run->next;
}

/* Allocate a single token that is invalidated at the same time as the
   rest of the tokens on the line.  Has its line and col set to the
   same as the last lexed token, so that diagnostics appear in the
   right place.  */
cpp_token *
_cpp_temp_token (cpp_reader *pfile)
{
  cpp_token *old, *result;

  old = pfile->cur_token - 1;
  if (pfile->cur_token == pfile->cur_run->limit)
    {
      pfile->cur_run = next_tokenrun (pfile->cur_run);
      pfile->cur_token = pfile->cur_run->base;
    }

  result = pfile->cur_token++;
  result->src_loc = old->src_loc;
  return result;
}

/* Lex a token into RESULT (external interface).  Takes care of issues
   like directive handling, token lookahead, multiple include
   optimization and skipping.  */
const cpp_token *
_cpp_lex_token (cpp_reader *pfile)
{
  cpp_token *result;

  for (;;)
    {
      if (pfile->cur_token == pfile->cur_run->limit)
	{
	  pfile->cur_run = next_tokenrun (pfile->cur_run);
	  pfile->cur_token = pfile->cur_run->base;
	}

      if (pfile->lookaheads)
	{
	  pfile->lookaheads--;
	  result = pfile->cur_token++;
	}
      else
	result = _cpp_lex_direct (pfile);

      if (result->flags & BOL)
	{
	  /* Is this a directive.  If _cpp_handle_directive returns
	     false, it is an assembler #.  */
	  if (result->type == CPP_HASH
	      /* 6.10.3 p 11: Directives in a list of macro arguments
		 gives undefined behavior.  This implementation
		 handles the directive as normal.  */
	      && pfile->state.parsing_args != 1)
	    {
	      if (_cpp_handle_directive (pfile, result->flags & PREV_WHITE))
		{
		  if (pfile->directive_result.type == CPP_PADDING)
		    continue;
		  result = &pfile->directive_result;
		}
	    }
	  else if (pfile->state.in_deferred_pragma)
	    result = &pfile->directive_result;

	  if (pfile->cb.line_change && !pfile->state.skipping)
	    pfile->cb.line_change (pfile, result, pfile->state.parsing_args);
	}

      /* We don't skip tokens in directives.  */
      if (pfile->state.in_directive || pfile->state.in_deferred_pragma)
	break;

      /* Outside a directive, invalidate controlling macros.  At file
	 EOF, _cpp_lex_direct takes care of popping the buffer, so we never
	 get here and MI optimization works.  */
      pfile->mi_valid = false;

      if (!pfile->state.skipping || result->type == CPP_EOF)
	break;
    }

  return result;
}

/* Returns true if a fresh line has been loaded.  */
bool
_cpp_get_fresh_line (cpp_reader *pfile)
{
  int return_at_eof;

  /* We can't get a new line until we leave the current directive.  */
  if (pfile->state.in_directive)
    return false;

  for (;;)
    {
      cpp_buffer *buffer = pfile->buffer;

      if (!buffer->need_line)
	return true;

      if (buffer->next_line < buffer->rlimit)
	{
	  _cpp_clean_line (pfile);
	  return true;
	}

      /* First, get out of parsing arguments state.  */
      if (pfile->state.parsing_args)
	return false;

      /* End of buffer.  Non-empty files should end in a newline.  */
      if (buffer->buf != buffer->rlimit
	  && buffer->next_line > buffer->rlimit
	  && !buffer->from_stage3)
	{
	  /* Only warn once.  */
	  buffer->next_line = buffer->rlimit;
	}

      return_at_eof = buffer->return_at_eof;
      _cpp_pop_buffer (pfile);
      if (pfile->buffer == NULL || return_at_eof)
	return false;
    }
}

#define IF_NEXT_IS(CHAR, THEN_TYPE, ELSE_TYPE)		\
  do							\
    {							\
      result->type = ELSE_TYPE;				\
      if (*buffer->cur == CHAR)				\
	buffer->cur++, result->type = THEN_TYPE;	\
    }							\
  while (0)

/* Lex a token into pfile->cur_token, which is also incremented, to
   get diagnostics pointing to the correct location.

   Does not handle issues such as token lookahead, multiple-include
   optimization, directives, skipping etc.  This function is only
   suitable for use by _cpp_lex_token, and in special cases like
   lex_expansion_token which doesn't care for any of these issues.

   When meeting a newline, returns CPP_EOF if parsing a directive,
   otherwise returns to the start of the token buffer if permissible.
   Returns the location of the lexed token.  */
cpp_token *
_cpp_lex_direct (cpp_reader *pfile)
{
  cppchar_t c;
  cpp_buffer *buffer;
  const unsigned char *comment_start;
  cpp_token *result = pfile->cur_token++;

 fresh_line:
  result->flags = 0;
  buffer = pfile->buffer;
  if (buffer->need_line)
    {
      if (pfile->state.in_deferred_pragma)
	{
	  result->type = CPP_PRAGMA_EOL;
	  pfile->state.in_deferred_pragma = false;
	  if (!pfile->state.pragma_allow_expansion)
	    pfile->state.prevent_expansion--;
	  return result;
	}
      if (!_cpp_get_fresh_line (pfile))
	{
	  result->type = CPP_EOF;
	  if (!pfile->state.in_directive)
	    {
	      /* Tell the compiler the line number of the EOF token.  */
	      result->src_loc = pfile->line_table->highest_line;
	      result->flags = BOL;
	    }
	  return result;
	}
      if (!pfile->keep_tokens)
	{
	  pfile->cur_run = &pfile->base_run;
	  result = pfile->base_run.base;
	  pfile->cur_token = result + 1;
	}
      result->flags = BOL;
      if (pfile->state.parsing_args == 2)
	result->flags |= PREV_WHITE;
    }
  buffer = pfile->buffer;
 update_tokens_line:
  result->src_loc = pfile->line_table->highest_line;

 skipped_white:
  if (buffer->cur >= buffer->notes[buffer->cur_note].pos
      && !pfile->overlaid_buffer)
    {
      _cpp_process_line_notes (pfile, false);
      result->src_loc = pfile->line_table->highest_line;
    }
  c = *buffer->cur++;

  LINEMAP_POSITION_FOR_COLUMN (result->src_loc, pfile->line_table,
			       CPP_BUF_COLUMN (buffer, buffer->cur));

  switch (c)
    {
    case ' ': case '\t': case '\f': case '\v': case '\0':
      result->flags |= PREV_WHITE;
      skip_whitespace (pfile, c);
      goto skipped_white;

    case '\n':
      if (buffer->cur < buffer->rlimit)
	CPP_INCREMENT_LINE (pfile, 0);
      buffer->need_line = true;
      goto fresh_line;

    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      {
	struct normalize_state nst = INITIAL_NORMALIZE_STATE;
	result->type = CPP_NUMBER;
	lex_number (pfile, &result->val.str, &nst);
	warn_about_normalization (pfile, result, &nst);
	break;
      }

    case 'L':
      /* 'L' may introduce wide characters or strings.  */
      if (*buffer->cur == '\'' || *buffer->cur == '"')
	{
	  lex_string (pfile, result, buffer->cur - 1);
	  break;
	}
      /* Fall through.  */

    case '_':
    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
    case 'g': case 'h': case 'i': case 'j': case 'k': case 'l':
    case 'm': case 'n': case 'o': case 'p': case 'q': case 'r':
    case 's': case 't': case 'u': case 'v': case 'w': case 'x':
    case 'y': case 'z':
    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
    case 'G': case 'H': case 'I': case 'J': case 'K':
    case 'M': case 'N': case 'O': case 'P': case 'Q': case 'R':
    case 'S': case 'T': case 'U': case 'V': case 'W': case 'X':
    case 'Y': case 'Z':
      result->type = CPP_NAME;
      {
	struct normalize_state nst = INITIAL_NORMALIZE_STATE;
	result->val.node = lex_identifier (pfile, buffer->cur - 1, false,
					   &nst);
	warn_about_normalization (pfile, result, &nst);
      }

      /* Convert named operators to their proper types.  */
      if (result->val.node->flags & NODE_OPERATOR)
	{
	  result->flags |= NAMED_OP;
	  result->type = (enum cpp_ttype) result->val.node->directive_index;
	}
      break;

    case '\'':
    case '"':
      lex_string (pfile, result, buffer->cur - 1);
      break;

    case '/':
      /* A potential block or line comment.  */
      comment_start = buffer->cur;
      c = *buffer->cur;
      
      if (c == '*')
	{
	  if (_cpp_skip_block_comment (pfile))
	    cpp_error (pfile, CPP_DL_ERROR, "unterminated comment");
	}
      else if (c == '/' && (CPP_OPTION (pfile, cplusplus_comments)
			    || cpp_in_system_header (pfile)))
	{
	  /* Warn about comments only if pedantically GNUC89, and not
	     in system headers.  */
	  if (CPP_OPTION (pfile, lang) == CLK_GNUC89 && CPP_PEDANTIC (pfile)
	      && ! buffer->warned_cplusplus_comments)
	    {
	      cpp_error (pfile, CPP_DL_PEDWARN,
			 "C++ style comments are not allowed in ISO C90");
	      cpp_error (pfile, CPP_DL_PEDWARN,
			 "(this will be reported only once per input file)");
	      buffer->warned_cplusplus_comments = 1;
	    }

	  if (skip_line_comment (pfile) && CPP_OPTION (pfile, warn_comments))
	    cpp_error (pfile, CPP_DL_WARNING, "multi-line comment");
	}
      else if (c == '=')
	{
	  buffer->cur++;
	  result->type = CPP_DIV_EQ;
	  break;
	}
      else
	{
	  result->type = CPP_DIV;
	  break;
	}

      if (!pfile->state.save_comments)
	{
	  result->flags |= PREV_WHITE;
	  goto update_tokens_line;
	}

      /* Save the comment as a token in its own right.  */
      save_comment (pfile, result, comment_start, c);
      break;

    case '<':
      if (pfile->state.angled_headers)
	{
	  lex_string (pfile, result, buffer->cur - 1);
	  break;
	}

      result->type = CPP_LESS;
      if (*buffer->cur == '=')
	buffer->cur++, result->type = CPP_LESS_EQ;
      else if (*buffer->cur == '<')
	{
	  buffer->cur++;
	  IF_NEXT_IS ('=', CPP_LSHIFT_EQ, CPP_LSHIFT);
	}
      else if (CPP_OPTION (pfile, digraphs))
	{
	  if (*buffer->cur == ':')
	    {
	      buffer->cur++;
	      result->flags |= DIGRAPH;
	      result->type = CPP_OPEN_SQUARE;
	    }
	  else if (*buffer->cur == '%')
	    {
	      buffer->cur++;
	      result->flags |= DIGRAPH;
	      result->type = CPP_OPEN_BRACE;
	    }
	}
      break;

    case '>':
      result->type = CPP_GREATER;
      if (*buffer->cur == '=')
	buffer->cur++, result->type = CPP_GREATER_EQ;
      else if (*buffer->cur == '>')
	{
	  buffer->cur++;
	  IF_NEXT_IS ('=', CPP_RSHIFT_EQ, CPP_RSHIFT);
	}
      break;

    case '%':
      result->type = CPP_MOD;
      if (*buffer->cur == '=')
	buffer->cur++, result->type = CPP_MOD_EQ;
      else if (CPP_OPTION (pfile, digraphs))
	{
	  if (*buffer->cur == ':')
	    {
	      buffer->cur++;
	      result->flags |= DIGRAPH;
	      result->type = CPP_HASH;
	      if (*buffer->cur == '%' && buffer->cur[1] == ':')
		buffer->cur += 2, result->type = CPP_PASTE;
	    }
	  else if (*buffer->cur == '>')
	    {
	      buffer->cur++;
	      result->flags |= DIGRAPH;
	      result->type = CPP_CLOSE_BRACE;
	    }
	}
      break;

    case '.':
      result->type = CPP_DOT;
      if (ISDIGIT (*buffer->cur))
	{
	  struct normalize_state nst = INITIAL_NORMALIZE_STATE;
	  result->type = CPP_NUMBER;
	  lex_number (pfile, &result->val.str, &nst);
	  warn_about_normalization (pfile, result, &nst);
	}
      else if (*buffer->cur == '.' && buffer->cur[1] == '.')
	buffer->cur += 2, result->type = CPP_ELLIPSIS;
      else if (*buffer->cur == '*' && CPP_OPTION (pfile, cplusplus))
	buffer->cur++, result->type = CPP_DOT_STAR;
      break;

    case '+':
      result->type = CPP_PLUS;
      if (*buffer->cur == '+')
	buffer->cur++, result->type = CPP_PLUS_PLUS;
      else if (*buffer->cur == '=')
	buffer->cur++, result->type = CPP_PLUS_EQ;
      break;

    case '-':
      result->type = CPP_MINUS;
      if (*buffer->cur == '>')
	{
	  buffer->cur++;
	  result->type = CPP_DEREF;
	  if (*buffer->cur == '*' && CPP_OPTION (pfile, cplusplus))
	    buffer->cur++, result->type = CPP_DEREF_STAR;
	}
      else if (*buffer->cur == '-')
	buffer->cur++, result->type = CPP_MINUS_MINUS;
      else if (*buffer->cur == '=')
	buffer->cur++, result->type = CPP_MINUS_EQ;
      break;

    case '&':
      result->type = CPP_AND;
      if (*buffer->cur == '&')
	buffer->cur++, result->type = CPP_AND_AND;
      else if (*buffer->cur == '=')
	buffer->cur++, result->type = CPP_AND_EQ;
      break;

    case '|':
      result->type = CPP_OR;
      if (*buffer->cur == '|')
	buffer->cur++, result->type = CPP_OR_OR;
      else if (*buffer->cur == '=')
	buffer->cur++, result->type = CPP_OR_EQ;
      break;

    case ':':
      result->type = CPP_COLON;
      if (*buffer->cur == ':' && CPP_OPTION (pfile, cplusplus))
	buffer->cur++, result->type = CPP_SCOPE;
      else if (*buffer->cur == '>' && CPP_OPTION (pfile, digraphs))
	{
	  buffer->cur++;
	  result->flags |= DIGRAPH;
	  result->type = CPP_CLOSE_SQUARE;
	}
      break;

    case '*': IF_NEXT_IS ('=', CPP_MULT_EQ, CPP_MULT); break;
    case '=': IF_NEXT_IS ('=', CPP_EQ_EQ, CPP_EQ); break;
    case '!': IF_NEXT_IS ('=', CPP_NOT_EQ, CPP_NOT); break;
    case '^': IF_NEXT_IS ('=', CPP_XOR_EQ, CPP_XOR); break;
    case '#': IF_NEXT_IS ('#', CPP_PASTE, CPP_HASH); break;

    case '?': result->type = CPP_QUERY; break;
    case '~': result->type = CPP_COMPL; break;
    case ',': result->type = CPP_COMMA; break;
    case '(': result->type = CPP_OPEN_PAREN; break;
    case ')': result->type = CPP_CLOSE_PAREN; break;
    case '[': result->type = CPP_OPEN_SQUARE; break;
    case ']': result->type = CPP_CLOSE_SQUARE; break;
    case '{': result->type = CPP_OPEN_BRACE; break;
    case '}': result->type = CPP_CLOSE_BRACE; break;
    case ';': result->type = CPP_SEMICOLON; break;

      /* @ is a punctuator in Objective-C.  */
    case '@': result->type = CPP_ATSIGN; break;

    case '$':
    case '\\':
      {
	const uchar *base = --buffer->cur;
	struct normalize_state nst = INITIAL_NORMALIZE_STATE;

	if (forms_identifier_p (pfile, true, &nst))
	  {
	    result->type = CPP_NAME;
	    result->val.node = lex_identifier (pfile, base, true, &nst);
	    warn_about_normalization (pfile, result, &nst);
	    break;
	  }
	buffer->cur++;
      }

    default:
      create_literal (pfile, result, buffer->cur - 1, 1, CPP_OTHER);
      break;
    }

  return result;
}

/* An upper bound on the number of bytes needed to spell TOKEN.
   Does not include preceding whitespace.  */
unsigned int
cpp_token_len (const cpp_token *token)
{
  unsigned int len;

  switch (TOKEN_SPELL (token))
    {
    default:		len = 4;				break;
    case SPELL_LITERAL:	len = token->val.str.len;		break;
    case SPELL_IDENT:	len = NODE_LEN (token->val.node) * 10;	break;
    }

  return len;
}

/* Parse UTF-8 out of NAMEP and place a \U escape in BUFFER.
   Return the number of bytes read out of NAME.  (There are always
   10 bytes written to BUFFER.)  */

static size_t
utf8_to_ucn (unsigned char *buffer, const unsigned char *name)
{
  int j;
  int ucn_len = 0;
  int ucn_len_c;
  unsigned t;
  unsigned long utf32;
  
  /* Compute the length of the UTF-8 sequence.  */
  for (t = *name; t & 0x80; t <<= 1)
    ucn_len++;
  
  utf32 = *name & (0x7F >> ucn_len);
  for (ucn_len_c = 1; ucn_len_c < ucn_len; ucn_len_c++)
    {
      utf32 = (utf32 << 6) | (*++name & 0x3F);
      
      /* Ill-formed UTF-8.  */
      if ((*name & ~0x3F) != 0x80)
	abort ();
    }
  
  *buffer++ = '\\';
  *buffer++ = 'U';
  for (j = 7; j >= 0; j--)
    *buffer++ = "0123456789abcdef"[(utf32 >> (4 * j)) & 0xF];
  return ucn_len;
}


/* Write the spelling of a token TOKEN to BUFFER.  The buffer must
   already contain the enough space to hold the token's spelling.
   Returns a pointer to the character after the last character written.
   FORSTRING is true if this is to be the spelling after translation
   phase 1 (this is different for UCNs).
   FIXME: Would be nice if we didn't need the PFILE argument.  */
unsigned char *
cpp_spell_token (cpp_reader *pfile, const cpp_token *token,
		 unsigned char *buffer, bool forstring)
{
  switch (TOKEN_SPELL (token))
    {
    case SPELL_OPERATOR:
      {
	const unsigned char *spelling;
	unsigned char c;

	if (token->flags & DIGRAPH)
	  spelling
	    = digraph_spellings[(int) token->type - (int) CPP_FIRST_DIGRAPH];
	else if (token->flags & NAMED_OP)
	  goto spell_ident;
	else
	  spelling = TOKEN_NAME (token);

	while ((c = *spelling++) != '\0')
	  *buffer++ = c;
      }
      break;

    spell_ident:
    case SPELL_IDENT:
      if (forstring)
	{
	  memcpy (buffer, NODE_NAME (token->val.node),
		  NODE_LEN (token->val.node));
	  buffer += NODE_LEN (token->val.node);
	}
      else
	{
	  size_t i;
	  const unsigned char * name = NODE_NAME (token->val.node);
	  
	  for (i = 0; i < NODE_LEN (token->val.node); i++)
	    if (name[i] & ~0x7F)
	      {
		i += utf8_to_ucn (buffer, name + i) - 1;
		buffer += 10;
	      }
	    else
	      *buffer++ = NODE_NAME (token->val.node)[i];
	}
      break;

    case SPELL_LITERAL:
      memcpy (buffer, token->val.str.text, token->val.str.len);
      buffer += token->val.str.len;
      break;

    case SPELL_NONE:
      cpp_error (pfile, CPP_DL_ICE,
		 "unspellable token %s", TOKEN_NAME (token));
      break;
    }

  return buffer;
}

/* Returns TOKEN spelt as a null-terminated string.  The string is
   freed when the reader is destroyed.  Useful for diagnostics.  */
unsigned char *
cpp_token_as_text (cpp_reader *pfile, const cpp_token *token)
{ 
  unsigned int len = cpp_token_len (token) + 1;
  unsigned char *start = _cpp_unaligned_alloc (pfile, len), *end;

  end = cpp_spell_token (pfile, token, start, false);
  end[0] = '\0';

  return start;
}

/* Used by C front ends, which really should move to using
   cpp_token_as_text.  */
const char *
cpp_type2name (enum cpp_ttype type)
{
  return (const char *) token_spellings[type].name;
}

/* Writes the spelling of token to FP, without any preceding space.
   Separated from cpp_spell_token for efficiency - to avoid stdio
   double-buffering.  */
void
cpp_output_token (const cpp_token *token, FILE *fp)
{
  switch (TOKEN_SPELL (token))
    {
    case SPELL_OPERATOR:
      {
	const unsigned char *spelling;
	int c;

	if (token->flags & DIGRAPH)
	  spelling
	    = digraph_spellings[(int) token->type - (int) CPP_FIRST_DIGRAPH];
	else if (token->flags & NAMED_OP)
	  goto spell_ident;
	else
	  spelling = TOKEN_NAME (token);

	c = *spelling;
	do
	  putc (c, fp);
	while ((c = *++spelling) != '\0');
      }
      break;

    spell_ident:
    case SPELL_IDENT:
      {
	size_t i;
	const unsigned char * name = NODE_NAME (token->val.node);
	
	for (i = 0; i < NODE_LEN (token->val.node); i++)
	  if (name[i] & ~0x7F)
	    {
	      unsigned char buffer[10];
	      i += utf8_to_ucn (buffer, name + i) - 1;
	      fwrite (buffer, 1, 10, fp);
	    }
	  else
	    fputc (NODE_NAME (token->val.node)[i], fp);
      }
      break;

    case SPELL_LITERAL:
      fwrite (token->val.str.text, 1, token->val.str.len, fp);
      break;

    case SPELL_NONE:
      /* An error, most probably.  */
      break;
    }
}

/* Compare two tokens.  */
int
_cpp_equiv_tokens (const cpp_token *a, const cpp_token *b)
{
  if (a->type == b->type && a->flags == b->flags)
    switch (TOKEN_SPELL (a))
      {
      default:			/* Keep compiler happy.  */
      case SPELL_OPERATOR:
	return 1;
      case SPELL_NONE:
	return (a->type != CPP_MACRO_ARG || a->val.arg_no == b->val.arg_no);
      case SPELL_IDENT:
	return a->val.node == b->val.node;
      case SPELL_LITERAL:
	return (a->val.str.len == b->val.str.len
		&& !memcmp (a->val.str.text, b->val.str.text,
			    a->val.str.len));
      }

  return 0;
}

/* Returns nonzero if a space should be inserted to avoid an
   accidental token paste for output.  For simplicity, it is
   conservative, and occasionally advises a space where one is not
   needed, e.g. "." and ".2".  */
int
cpp_avoid_paste (cpp_reader *pfile, const cpp_token *token1,
		 const cpp_token *token2)
{
  enum cpp_ttype a = token1->type, b = token2->type;
  cppchar_t c;

  if (token1->flags & NAMED_OP)
    a = CPP_NAME;
  if (token2->flags & NAMED_OP)
    b = CPP_NAME;

  c = EOF;
  if (token2->flags & DIGRAPH)
    c = digraph_spellings[(int) b - (int) CPP_FIRST_DIGRAPH][0];
  else if (token_spellings[b].category == SPELL_OPERATOR)
    c = token_spellings[b].name[0];

  /* Quickly get everything that can paste with an '='.  */
  if ((int) a <= (int) CPP_LAST_EQ && c == '=')
    return 1;

  switch (a)
    {
    case CPP_GREATER:	return c == '>';
    case CPP_LESS:	return c == '<' || c == '%' || c == ':';
    case CPP_PLUS:	return c == '+';
    case CPP_MINUS:	return c == '-' || c == '>';
    case CPP_DIV:	return c == '/' || c == '*'; /* Comments.  */
    case CPP_MOD:	return c == ':' || c == '>';
    case CPP_AND:	return c == '&';
    case CPP_OR:	return c == '|';
    case CPP_COLON:	return c == ':' || c == '>';
    case CPP_DEREF:	return c == '*';
    case CPP_DOT:	return c == '.' || c == '%' || b == CPP_NUMBER;
    case CPP_HASH:	return c == '#' || c == '%'; /* Digraph form.  */
    case CPP_NAME:	return ((b == CPP_NUMBER
				 && name_p (pfile, &token2->val.str))
				|| b == CPP_NAME
				|| b == CPP_CHAR || b == CPP_STRING); /* L */
    case CPP_NUMBER:	return (b == CPP_NUMBER || b == CPP_NAME
				|| c == '.' || c == '+' || c == '-');
				      /* UCNs */
    case CPP_OTHER:	return ((token1->val.str.text[0] == '\\'
				 && b == CPP_NAME)
				|| (CPP_OPTION (pfile, objc)
				    && token1->val.str.text[0] == '@'
				    && (b == CPP_NAME || b == CPP_STRING)));
    default:		break;
    }

  return 0;
}

/* Output all the remaining tokens on the current line, and a newline
   character, to FP.  Leading whitespace is removed.  If there are
   macros, special token padding is not performed.  */
void
cpp_output_line (cpp_reader *pfile, FILE *fp)
{
  const cpp_token *token;

  token = cpp_get_token (pfile);
  while (token->type != CPP_EOF)
    {
      cpp_output_token (token, fp);
      token = cpp_get_token (pfile);
      if (token->flags & PREV_WHITE)
	putc (' ', fp);
    }

  putc ('\n', fp);
}

/* Memory buffers.  Changing these three constants can have a dramatic
   effect on performance.  The values here are reasonable defaults,
   but might be tuned.  If you adjust them, be sure to test across a
   range of uses of cpplib, including heavy nested function-like macro
   expansion.  Also check the change in peak memory usage (NJAMD is a
   good tool for this).  */
#define MIN_BUFF_SIZE 8000
#define BUFF_SIZE_UPPER_BOUND(MIN_SIZE) (MIN_BUFF_SIZE + (MIN_SIZE) * 3 / 2)
#define EXTENDED_BUFF_SIZE(BUFF, MIN_EXTRA) \
	(MIN_EXTRA + ((BUFF)->limit - (BUFF)->cur) * 2)

#if MIN_BUFF_SIZE > BUFF_SIZE_UPPER_BOUND (0)
  #error BUFF_SIZE_UPPER_BOUND must be at least as large as MIN_BUFF_SIZE!
#endif

/* Create a new allocation buffer.  Place the control block at the end
   of the buffer, so that buffer overflows will cause immediate chaos.  */
static _cpp_buff *
new_buff (size_t len)
{
  _cpp_buff *result;
  unsigned char *base;

  if (len < MIN_BUFF_SIZE)
    len = MIN_BUFF_SIZE;
  len = CPP_ALIGN (len);

  base = XNEWVEC (unsigned char, len + sizeof (_cpp_buff));
  result = (_cpp_buff *) (base + len);
  result->base = base;
  result->cur = base;
  result->limit = base + len;
  result->next = NULL;
  return result;
}

/* Place a chain of unwanted allocation buffers on the free list.  */
void
_cpp_release_buff (cpp_reader *pfile, _cpp_buff *buff)
{
  _cpp_buff *end = buff;

  while (end->next)
    end = end->next;
  end->next = pfile->free_buffs;
  pfile->free_buffs = buff;
}

/* Return a free buffer of size at least MIN_SIZE.  */
_cpp_buff *
_cpp_get_buff (cpp_reader *pfile, size_t min_size)
{
  _cpp_buff *result, **p;

  for (p = &pfile->free_buffs;; p = &(*p)->next)
    {
      size_t size;

      if (*p == NULL)
	return new_buff (min_size);
      result = *p;
      size = result->limit - result->base;
      /* Return a buffer that's big enough, but don't waste one that's
         way too big.  */
      if (size >= min_size && size <= BUFF_SIZE_UPPER_BOUND (min_size))
	break;
    }

  *p = result->next;
  result->next = NULL;
  result->cur = result->base;
  return result;
}

/* Creates a new buffer with enough space to hold the uncommitted
   remaining bytes of BUFF, and at least MIN_EXTRA more bytes.  Copies
   the excess bytes to the new buffer.  Chains the new buffer after
   BUFF, and returns the new buffer.  */
_cpp_buff *
_cpp_append_extend_buff (cpp_reader *pfile, _cpp_buff *buff, size_t min_extra)
{
  size_t size = EXTENDED_BUFF_SIZE (buff, min_extra);
  _cpp_buff *new_buff = _cpp_get_buff (pfile, size);

  buff->next = new_buff;
  memcpy (new_buff->base, buff->cur, BUFF_ROOM (buff));
  return new_buff;
}

/* Creates a new buffer with enough space to hold the uncommitted
   remaining bytes of the buffer pointed to by BUFF, and at least
   MIN_EXTRA more bytes.  Copies the excess bytes to the new buffer.
   Chains the new buffer before the buffer pointed to by BUFF, and
   updates the pointer to point to the new buffer.  */
void
_cpp_extend_buff (cpp_reader *pfile, _cpp_buff **pbuff, size_t min_extra)
{
  _cpp_buff *new_buff, *old_buff = *pbuff;
  size_t size = EXTENDED_BUFF_SIZE (old_buff, min_extra);

  new_buff = _cpp_get_buff (pfile, size);
  memcpy (new_buff->base, old_buff->cur, BUFF_ROOM (old_buff));
  new_buff->next = old_buff;
  *pbuff = new_buff;
}

/* Free a chain of buffers starting at BUFF.  */
void
_cpp_free_buff (_cpp_buff *buff)
{
  _cpp_buff *next;

  for (; buff; buff = next)
    {
      next = buff->next;
      free (buff->base);
    }
}

/* Allocate permanent, unaligned storage of length LEN.  */
unsigned char *
_cpp_unaligned_alloc (cpp_reader *pfile, size_t len)
{
  _cpp_buff *buff = pfile->u_buff;
  unsigned char *result = buff->cur;

  if (len > (size_t) (buff->limit - result))
    {
      buff = _cpp_get_buff (pfile, len);
      buff->next = pfile->u_buff;
      pfile->u_buff = buff;
      result = buff->cur;
    }

  buff->cur = result + len;
  return result;
}

/* Allocate permanent, unaligned storage of length LEN from a_buff.
   That buffer is used for growing allocations when saving macro
   replacement lists in a #define, and when parsing an answer to an
   assertion in #assert, #unassert or #if (and therefore possibly
   whilst expanding macros).  It therefore must not be used by any
   code that they might call: specifically the lexer and the guts of
   the macro expander.

   All existing other uses clearly fit this restriction: storing
   registered pragmas during initialization.  */
unsigned char *
_cpp_aligned_alloc (cpp_reader *pfile, size_t len)
{
  _cpp_buff *buff = pfile->a_buff;
  unsigned char *result = buff->cur;

  if (len > (size_t) (buff->limit - result))
    {
      buff = _cpp_get_buff (pfile, len);
      buff->next = pfile->a_buff;
      pfile->a_buff = buff;
      result = buff->cur;
    }

  buff->cur = result + len;
  return result;
}

/* Say which field of TOK is in use.  */

enum cpp_token_fld_kind
cpp_token_val_index (cpp_token *tok)
{
  switch (TOKEN_SPELL (tok))
    {
    case SPELL_IDENT:
      return CPP_TOKEN_FLD_NODE;
    case SPELL_LITERAL:
      return CPP_TOKEN_FLD_STR;
    case SPELL_NONE:
      if (tok->type == CPP_MACRO_ARG)
	return CPP_TOKEN_FLD_ARG_NO;
      else if (tok->type == CPP_PADDING)
	return CPP_TOKEN_FLD_SOURCE;
      else if (tok->type == CPP_PRAGMA)
	return CPP_TOKEN_FLD_PRAGMA;
      /* else fall through */
    default:
      return CPP_TOKEN_FLD_NONE;
    }
}
