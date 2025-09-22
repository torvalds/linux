/* TREELANG Compiler almost main (tree1)
   Called by GCC's toplev.c

   Copyright (C) 1986, 87, 89, 92-96, 1997, 1999, 2000, 2001, 2002, 2003, 2004
   Free Software Foundation, Inc.

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
   Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.

   In other words, you are welcome to use, share and improve this program.
   You are forbidden to forbid anyone else to use, share and improve
   what you give them.   Help stamp out software-hoarding!  

   ---------------------------------------------------------------------------

   Written by Tim Josling 1999, 2000, 2001, based in part on other
   parts of the GCC compiler.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "flags.h"
#include "toplev.h"
#include "version.h"

#include "ggc.h"
#include "tree.h"
#include "cgraph.h"
#include "diagnostic.h"

#include "treelang.h"
#include "treetree.h"
#include "opts.h"
#include "options.h"

extern int yyparse (void);

/* Linked list of symbols - all must be unique in treelang.  */

static GTY(()) struct prod_token_parm_item *symbol_table = NULL;

/* Language for usage for messages.  */

const char *const language_string = "TREELANG - sample front end for GCC ";

/* Local prototypes.  */

void version (void);

/* Global variables.  */

extern struct cbl_tree_struct_parse_tree_top* parse_tree_top;

/* 
   Options. 
*/

/* Trace the parser.  */
unsigned int option_parser_trace = 0;

/* Trace the lexical analysis.  */

unsigned int option_lexer_trace = 0;

/* Warning levels.  */

/* Local variables.  */

/* This is 1 if we have output the version string.  */

static int version_done = 0;

/* Variable nesting level.  */

static unsigned int work_nesting_level = 0;

/* Prepare to handle switches.  */
unsigned int
treelang_init_options (unsigned int argc ATTRIBUTE_UNUSED,
		       const char **argv ATTRIBUTE_UNUSED)
{
  return CL_Treelang;
}

/* Process a switch - called by opts.c.  */
int
treelang_handle_option (size_t scode, const char *arg ATTRIBUTE_UNUSED,
			int value)
{
  enum opt_code code = (enum opt_code) scode;

  switch (code)
    {
    case OPT_v:
      if (!version_done)
	{
	  fputs (language_string, stdout);
	  fputs (version_string, stdout);
	  fputs ("\n", stdout);
	  version_done = 1;
	}
      break;

    case OPT_y:
      option_lexer_trace = 1;
      option_parser_trace = 1;
      break;

    case OPT_fparser_trace:
      option_parser_trace = value;
      break;

    case OPT_flexer_trace:
      option_lexer_trace = value;
      break;

    default:
      gcc_unreachable ();
    }

  return 1;
}

/* Language dependent parser setup.  */

bool
treelang_init (void)
{
#ifndef USE_MAPPED_LOCATION
  input_filename = main_input_filename;
#else
  linemap_add (&line_table, LC_ENTER, false, main_input_filename, 1);
#endif

  /* This error will not happen from GCC as it will always create a
     fake input file.  */
  if (!input_filename || input_filename[0] == ' ' || !input_filename[0]) 
    {
      if (!version_done)
        {
          fprintf (stderr, "No input file specified, try --help for help\n");
          exit (1);
        }

      return false;
    }

  yyin = fopen (input_filename, "r");
  if (!yyin)
    {
      fprintf (stderr, "Unable to open input file %s\n", input_filename);
      exit (1);
    }

#ifdef USE_MAPPED_LOCATION
  linemap_add (&line_table, LC_RENAME, false, "<built-in>", 1);
  linemap_line_start (&line_table, 0, 1);
#endif

  /* Init decls, etc.  */
  treelang_init_decl_processing ();

  return true;
}

/* Language dependent wrapup.  */

void 
treelang_finish (void)
{
  fclose (yyin);
}

/* Parse a file.  Debug flag doesn't seem to work. */

void
treelang_parse_file (int debug_flag ATTRIBUTE_UNUSED)
{
#ifdef USE_MAPPED_LOCATION
  source_location s;
  linemap_add (&line_table, LC_RENAME, false, main_input_filename, 1);
  s = linemap_line_start (&line_table, 1, 80);
  input_location = s;
#else
  input_line = 1;
#endif

  treelang_debug ();
  yyparse ();
  cgraph_finalize_compilation_unit ();
#ifdef USE_MAPPED_LOCATION
  linemap_add (&line_table, LC_LEAVE, false, NULL, 0);
#endif
  cgraph_optimize ();
}

/* Allocate SIZE bytes and clear them.  Not to be used for strings
   which must go in stringpool.  */

void *
my_malloc (size_t size)
{
  void *mem;
  mem = ggc_alloc (size);
  if (!mem)
    {
      fprintf (stderr, "\nOut of memory\n");
      abort ();
    }
  memset (mem, 0, size);
  return mem;
}

/* Look up a name in PROD->SYMBOL_TABLE_NAME in the symbol table;
   return the symbol table entry from the symbol table if found there,
   else 0.  */

struct prod_token_parm_item*
lookup_tree_name (struct prod_token_parm_item *prod)
{
  struct prod_token_parm_item *this;
  struct prod_token_parm_item *this_tok;
  struct prod_token_parm_item *tok;

  sanity_check (prod);
  
  tok = SYMBOL_TABLE_NAME (prod);
  sanity_check (tok);
  
  for (this = symbol_table; this; this = this->tp.pro.next)
    {
      sanity_check (this);
      this_tok = this->tp.pro.main_token;
      sanity_check (this_tok);
      if (tok->tp.tok.length != this_tok->tp.tok.length) 
        continue;
      if (memcmp (tok->tp.tok.chars, this_tok->tp.tok.chars,
		  this_tok->tp.tok.length))
        continue;

      if (option_parser_trace)
        fprintf (stderr, "Found symbol %s (%i:%i) as %i \n",
		 tok->tp.tok.chars, LOCATION_LINE (tok->tp.tok.location),
		 tok->tp.tok.charno, NUMERIC_TYPE (this));
      return this;
    }

  if (option_parser_trace)
    fprintf (stderr, "Not found symbol %s (%i:%i) as %i \n",
	     tok->tp.tok.chars, LOCATION_LINE (tok->tp.tok.location),
	     tok->tp.tok.charno, tok->type);
  return NULL;
}

/* Insert name PROD into the symbol table.  Return 1 if duplicate, 0 if OK.  */

int
insert_tree_name (struct prod_token_parm_item *prod)
{
  struct prod_token_parm_item *tok;
  tok = SYMBOL_TABLE_NAME (prod);
  sanity_check (prod);
  if (lookup_tree_name (prod))
    {
      error ("%HDuplicate name %q.*s.", &tok->tp.tok.location,
	     tok->tp.tok.length, tok->tp.tok.chars);
      return 1;
    }
  prod->tp.pro.next = symbol_table;
  NESTING_LEVEL (prod) = work_nesting_level;
  symbol_table = prod;
  return 0;
}

/* Create a struct productions of type TYPE, main token MAIN_TOK.  */

struct prod_token_parm_item *
make_production (int type, struct prod_token_parm_item *main_tok)
{
  struct prod_token_parm_item *prod;
  prod = my_malloc (sizeof (struct prod_token_parm_item));
  prod->category = production_category;
  prod->type = type;
  prod->tp.pro.main_token = main_tok;
  return prod;
} 

/* Abort if ITEM is not a valid structure, based on 'category'.  */

void
sanity_check (struct prod_token_parm_item *item)
{
  switch (item->category)
    {
    case token_category:
    case production_category:
    case parameter_category:
      break;
      
    default:
      gcc_unreachable ();
    }
}  

/* New garbage collection regime see gty.texi.  */
#include "gt-treelang-tree1.h"
/*#include "gt-treelang-treelang.h"*/
#include "gtype-treelang.h"
