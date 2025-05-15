/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Lexical analysis for genksyms.
 * Copyright 1996, 1997 Linux International.
 *
 * New implementation contributed by Richard Henderson <rth@tamu.edu>
 * Based on original work by Bjorn Ekwall <bj0rn@blox.se>
 *
 * Taken from Linux modutils 2.4.22.
 */

%{

#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "genksyms.h"
#include "parse.tab.h"

/* We've got a two-level lexer here.  We let flex do basic tokenization
   and then we categorize those basic tokens in the second stage.  */
#define YY_DECL		static int yylex1(void)

%}

IDENT			[A-Za-z_\$][A-Za-z0-9_\$]*

O_INT			0[0-7]*
D_INT			[1-9][0-9]*
X_INT			0[Xx][0-9A-Fa-f]+
I_SUF			[Uu]|[Ll]|[Uu][Ll]|[Ll][Uu]
INT			({O_INT}|{D_INT}|{X_INT}){I_SUF}?

FRAC			([0-9]*\.[0-9]+)|([0-9]+\.)
EXP			[Ee][+-]?[0-9]+
F_SUF			[FfLl]
REAL			({FRAC}{EXP}?{F_SUF}?)|([0-9]+{EXP}{F_SUF}?)

STRING			L?\"([^\\\"]*\\.)*[^\\\"]*\"
CHAR			L?\'([^\\\']*\\.)*[^\\\']*\'

MC_TOKEN		([~%^&*+=|<>/-]=)|(&&)|("||")|(->)|(<<)|(>>)

/* We don't do multiple input files.  */
%option noyywrap

%option noinput

%%

u?int(8|16|32|64)x(1|2|4|8|16)_t	return BUILTIN_INT_KEYW;

 /* Keep track of our location in the original source files.  */
^#[ \t]+{INT}[ \t]+\"[^\"\n]+\".*\n	return FILENAME;
^#.*\n					cur_line++;
\n					cur_line++;

 /* Ignore all other whitespace.  */
[ \t\f\v\r]+				;


{STRING}				return STRING;
{CHAR}					return CHAR;
{IDENT}					return IDENT;

 /* The Pedant requires that the other C multi-character tokens be
    recognized as tokens.  We don't actually use them since we don't
    parse expressions, but we do want whitespace to be arranged
    around them properly.  */
{MC_TOKEN}				return OTHER;
{INT}					return INT;
{REAL}					return REAL;

"..."					return DOTS;

 /* All other tokens are single characters.  */
.					return yytext[0];


%%

/* Bring in the keyword recognizer.  */

#include "keywords.c"


/* Macros to append to our phrase collection list.  */

/*
 * We mark any token, that that equals to a known enumerator, as
 * SYM_ENUM_CONST. The parser will change this for struct and union tags later,
 * the only problem is struct and union members:
 *    enum e { a, b }; struct s { int a, b; }
 * but in this case, the only effect will be, that the ABI checksums become
 * more volatile, which is acceptable. Also, such collisions are quite rare,
 * so far it was only observed in include/linux/telephony.h.
 */
#define _APP(T,L)	do {						   \
			  cur_node = next_node;				   \
			  next_node = xmalloc(sizeof(*next_node));	   \
			  next_node->next = cur_node;			   \
			  cur_node->string = memcpy(xmalloc(L+1), T, L+1); \
			  cur_node->tag =				   \
			    find_symbol(cur_node->string, SYM_ENUM_CONST, 1)?\
			    SYM_ENUM_CONST : SYM_NORMAL ;		   \
			  cur_node->in_source_file = in_source_file;       \
			} while (0)

#define APP		_APP(yytext, yyleng)


/* The second stage lexer.  Here we incorporate knowledge of the state
   of the parser to tailor the tokens that are returned.  */

/*
 * The lexer cannot distinguish whether a typedef'ed string is a TYPE or an
 * IDENT. We need a hint from the parser to handle this accurately.
 */
bool dont_want_type_specifier;

int
yylex(void)
{
  static enum {
    ST_NOTSTARTED, ST_NORMAL, ST_ATTRIBUTE, ST_ASM, ST_TYPEOF, ST_TYPEOF_1,
    ST_BRACKET, ST_BRACE, ST_EXPRESSION, ST_STATIC_ASSERT,
  } lexstate = ST_NOTSTARTED;

  static int suppress_type_lookup, dont_want_brace_phrase;
  static struct string_list *next_node;
  static char *source_file;

  int token, count = 0;
  struct string_list *cur_node;

  if (lexstate == ST_NOTSTARTED)
    {
      next_node = xmalloc(sizeof(*next_node));
      next_node->next = NULL;
      lexstate = ST_NORMAL;
    }

repeat:
  token = yylex1();

  if (token == 0)
    return 0;
  else if (token == FILENAME)
    {
      char *file, *e;

      /* Save the filename and line number for later error messages.  */

      if (cur_filename)
	free(cur_filename);

      file = strchr(yytext, '\"')+1;
      e = strchr(file, '\"');
      *e = '\0';
      cur_filename = memcpy(xmalloc(e-file+1), file, e-file+1);
      cur_line = atoi(yytext+2);

      if (!source_file) {
        source_file = xstrdup(cur_filename);
        in_source_file = 1;
      } else {
        in_source_file = (strcmp(cur_filename, source_file) == 0);
      }

      goto repeat;
    }

  switch (lexstate)
    {
    case ST_NORMAL:
      APP;
      switch (token)
	{
	case IDENT:
	  {
	    int r = is_reserved_word(yytext, yyleng);
	    if (r >= 0)
	      {
		switch (token = r)
		  {
		  case ATTRIBUTE_KEYW:
		    lexstate = ST_ATTRIBUTE;
		    count = 0;
		    goto repeat;
		  case ASM_KEYW:
		    lexstate = ST_ASM;
		    count = 0;
		    goto repeat;
		  case TYPEOF_KEYW:
		    lexstate = ST_TYPEOF;
		    count = 0;
		    goto repeat;

		  case STRUCT_KEYW:
		  case UNION_KEYW:
		  case ENUM_KEYW:
		    dont_want_brace_phrase = 3;
		    suppress_type_lookup = 2;
		    goto fini;

		  case EXPORT_SYMBOL_KEYW:
		      goto fini;

		  case STATIC_ASSERT_KEYW:
		    lexstate = ST_STATIC_ASSERT;
		    count = 0;
		    goto repeat;
		  }
	      }
	    if (!suppress_type_lookup && !dont_want_type_specifier)
	      {
		if (find_symbol(yytext, SYM_TYPEDEF, 1))
		  token = TYPE;
	      }
	  }
	  break;

	case '[':
	  lexstate = ST_BRACKET;
	  count = 1;
	  goto repeat;

	case '{':
	  if (dont_want_brace_phrase)
	    break;
	  lexstate = ST_BRACE;
	  count = 1;
	  goto repeat;

	case '=': case ':':
	  lexstate = ST_EXPRESSION;
	  break;

	default:
	  break;
	}
      break;

    case ST_ATTRIBUTE:
      APP;
      switch (token)
	{
	case '(':
	  ++count;
	  goto repeat;
	case ')':
	  if (--count == 0)
	    {
	      lexstate = ST_NORMAL;
	      token = ATTRIBUTE_PHRASE;
	      break;
	    }
	  goto repeat;
	default:
	  goto repeat;
	}
      break;

    case ST_ASM:
      APP;
      switch (token)
	{
	case '(':
	  ++count;
	  goto repeat;
	case ')':
	  if (--count == 0)
	    {
	      lexstate = ST_NORMAL;
	      token = ASM_PHRASE;
	      break;
	    }
	  goto repeat;
	default:
	  goto repeat;
	}
      break;

    case ST_TYPEOF_1:
      if (token == IDENT)
	{
	  if (is_reserved_word(yytext, yyleng) >= 0
	      || find_symbol(yytext, SYM_TYPEDEF, 1))
	    {
	      yyless(0);
	      unput('(');
	      lexstate = ST_NORMAL;
	      token = TYPEOF_KEYW;
	      break;
	    }
	  _APP("(", 1);
	}
	lexstate = ST_TYPEOF;
	/* FALLTHRU */

    case ST_TYPEOF:
      switch (token)
	{
	case '(':
	  if ( ++count == 1 )
	    lexstate = ST_TYPEOF_1;
	  else
	    APP;
	  goto repeat;
	case ')':
	  APP;
	  if (--count == 0)
	    {
	      lexstate = ST_NORMAL;
	      token = TYPEOF_PHRASE;
	      break;
	    }
	  goto repeat;
	default:
	  APP;
	  goto repeat;
	}
      break;

    case ST_BRACKET:
      APP;
      switch (token)
	{
	case '[':
	  ++count;
	  goto repeat;
	case ']':
	  if (--count == 0)
	    {
	      lexstate = ST_NORMAL;
	      token = BRACKET_PHRASE;
	      break;
	    }
	  goto repeat;
	default:
	  goto repeat;
	}
      break;

    case ST_BRACE:
      APP;
      switch (token)
	{
	case '{':
	  ++count;
	  goto repeat;
	case '}':
	  if (--count == 0)
	    {
	      lexstate = ST_NORMAL;
	      token = BRACE_PHRASE;
	      break;
	    }
	  goto repeat;
	default:
	  goto repeat;
	}
      break;

    case ST_EXPRESSION:
      switch (token)
	{
	case '(': case '[': case '{':
	  ++count;
	  APP;
	  goto repeat;
	case '}':
	  /* is this the last line of an enum declaration? */
	  if (count == 0)
	    {
	      /* Put back the token we just read so's we can find it again
		 after registering the expression.  */
	      unput(token);

	      lexstate = ST_NORMAL;
	      token = EXPRESSION_PHRASE;
	      break;
	    }
	  /* FALLTHRU */
	case ')': case ']':
	  --count;
	  APP;
	  goto repeat;
	case ',': case ';':
	  if (count == 0)
	    {
	      /* Put back the token we just read so's we can find it again
		 after registering the expression.  */
	      unput(token);

	      lexstate = ST_NORMAL;
	      token = EXPRESSION_PHRASE;
	      break;
	    }
	  APP;
	  goto repeat;
	default:
	  APP;
	  goto repeat;
	}
      break;

    case ST_STATIC_ASSERT:
      APP;
      switch (token)
	{
	case '(':
	  ++count;
	  goto repeat;
	case ')':
	  if (--count == 0)
	    {
	      lexstate = ST_NORMAL;
	      token = STATIC_ASSERT_PHRASE;
	      break;
	    }
	  goto repeat;
	default:
	  goto repeat;
	}
      break;

    default:
      exit(1);
    }
fini:

  if (suppress_type_lookup > 0)
    --suppress_type_lookup;

  /*
   *  __attribute__() can be placed immediately after the 'struct' keyword.
   *  e.g.) struct __attribute__((__packed__)) foo { ... };
   */
  if (token != ATTRIBUTE_PHRASE && dont_want_brace_phrase > 0)
    --dont_want_brace_phrase;

  yylval = &next_node->next;

  return token;
}
