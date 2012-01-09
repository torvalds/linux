/* C global declaration parser for genksyms.
   Copyright 1996, 1997 Linux International.

   New implementation contributed by Richard Henderson <rth@tamu.edu>
   Based on original work by Bjorn Ekwall <bj0rn@blox.se>

   This file is part of the Linux modutils.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2 of the License, or (at your
   option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */


%{

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "genksyms.h"

static int is_typedef;
static int is_extern;
static char *current_name;
static struct string_list *decl_spec;

static void yyerror(const char *);

static inline void
remove_node(struct string_list **p)
{
  struct string_list *node = *p;
  *p = node->next;
  free_node(node);
}

static inline void
remove_list(struct string_list **pb, struct string_list **pe)
{
  struct string_list *b = *pb, *e = *pe;
  *pb = e;
  free_list(b, e);
}

/* Record definition of a struct/union/enum */
static void record_compound(struct string_list **keyw,
		       struct string_list **ident,
		       struct string_list **body,
		       enum symbol_type type)
{
	struct string_list *b = *body, *i = *ident, *r;

	if (i->in_source_file) {
		remove_node(keyw);
		(*ident)->tag = type;
		remove_list(body, ident);
		return;
	}
	r = copy_node(i); r->tag = type;
	r->next = (*keyw)->next; *body = r; (*keyw)->next = NULL;
	add_symbol(i->string, type, b, is_extern);
}

%}

%token ASM_KEYW
%token ATTRIBUTE_KEYW
%token AUTO_KEYW
%token BOOL_KEYW
%token CHAR_KEYW
%token CONST_KEYW
%token DOUBLE_KEYW
%token ENUM_KEYW
%token EXTERN_KEYW
%token EXTENSION_KEYW
%token FLOAT_KEYW
%token INLINE_KEYW
%token INT_KEYW
%token LONG_KEYW
%token REGISTER_KEYW
%token RESTRICT_KEYW
%token SHORT_KEYW
%token SIGNED_KEYW
%token STATIC_KEYW
%token STRUCT_KEYW
%token TYPEDEF_KEYW
%token UNION_KEYW
%token UNSIGNED_KEYW
%token VOID_KEYW
%token VOLATILE_KEYW
%token TYPEOF_KEYW

%token EXPORT_SYMBOL_KEYW

%token ASM_PHRASE
%token ATTRIBUTE_PHRASE
%token BRACE_PHRASE
%token BRACKET_PHRASE
%token EXPRESSION_PHRASE

%token CHAR
%token DOTS
%token IDENT
%token INT
%token REAL
%token STRING
%token TYPE
%token OTHER
%token FILENAME

%%

declaration_seq:
	declaration
	| declaration_seq declaration
	;

declaration:
	{ is_typedef = 0; is_extern = 0; current_name = NULL; decl_spec = NULL; }
	declaration1
	{ free_list(*$2, NULL); *$2 = NULL; }
	;

declaration1:
	EXTENSION_KEYW TYPEDEF_KEYW { is_typedef = 1; } simple_declaration
		{ $$ = $4; }
	| TYPEDEF_KEYW { is_typedef = 1; } simple_declaration
		{ $$ = $3; }
	| simple_declaration
	| function_definition
	| asm_definition
	| export_definition
	| error ';'				{ $$ = $2; }
	| error '}'				{ $$ = $2; }
	;

simple_declaration:
	decl_specifier_seq_opt init_declarator_list_opt ';'
		{ if (current_name) {
		    struct string_list *decl = (*$3)->next;
		    (*$3)->next = NULL;
		    add_symbol(current_name,
			       is_typedef ? SYM_TYPEDEF : SYM_NORMAL,
			       decl, is_extern);
		    current_name = NULL;
		  }
		  $$ = $3;
		}
	;

init_declarator_list_opt:
	/* empty */				{ $$ = NULL; }
	| init_declarator_list
	;

init_declarator_list:
	init_declarator
		{ struct string_list *decl = *$1;
		  *$1 = NULL;
		  add_symbol(current_name,
			     is_typedef ? SYM_TYPEDEF : SYM_NORMAL, decl, is_extern);
		  current_name = NULL;
		  $$ = $1;
		}
	| init_declarator_list ',' init_declarator
		{ struct string_list *decl = *$3;
		  *$3 = NULL;
		  free_list(*$2, NULL);
		  *$2 = decl_spec;
		  add_symbol(current_name,
			     is_typedef ? SYM_TYPEDEF : SYM_NORMAL, decl, is_extern);
		  current_name = NULL;
		  $$ = $3;
		}
	;

init_declarator:
	declarator asm_phrase_opt attribute_opt initializer_opt
		{ $$ = $4 ? $4 : $3 ? $3 : $2 ? $2 : $1; }
	;

/* Hang on to the specifiers so that we can reuse them.  */
decl_specifier_seq_opt:
	/* empty */				{ decl_spec = NULL; }
	| decl_specifier_seq
	;

decl_specifier_seq:
	decl_specifier				{ decl_spec = *$1; }
	| decl_specifier_seq decl_specifier	{ decl_spec = *$2; }
	;

decl_specifier:
	storage_class_specifier
		{ /* Version 2 checksumming ignores storage class, as that
		     is really irrelevant to the linkage.  */
		  remove_node($1);
		  $$ = $1;
		}
	| type_specifier
	;

storage_class_specifier:
	AUTO_KEYW
	| REGISTER_KEYW
	| STATIC_KEYW
	| EXTERN_KEYW	{ is_extern = 1; $$ = $1; }
	| INLINE_KEYW	{ is_extern = 0; $$ = $1; }
	;

type_specifier:
	simple_type_specifier
	| cvar_qualifier
	| TYPEOF_KEYW '(' decl_specifier_seq '*' ')'
	| TYPEOF_KEYW '(' decl_specifier_seq ')'

	/* References to s/u/e's defined elsewhere.  Rearrange things
	   so that it is easier to expand the definition fully later.  */
	| STRUCT_KEYW IDENT
		{ remove_node($1); (*$2)->tag = SYM_STRUCT; $$ = $2; }
	| UNION_KEYW IDENT
		{ remove_node($1); (*$2)->tag = SYM_UNION; $$ = $2; }
	| ENUM_KEYW IDENT
		{ remove_node($1); (*$2)->tag = SYM_ENUM; $$ = $2; }

	/* Full definitions of an s/u/e.  Record it.  */
	| STRUCT_KEYW IDENT class_body
		{ record_compound($1, $2, $3, SYM_STRUCT); $$ = $3; }
	| UNION_KEYW IDENT class_body
		{ record_compound($1, $2, $3, SYM_UNION); $$ = $3; }
	| ENUM_KEYW IDENT enum_body
		{ record_compound($1, $2, $3, SYM_ENUM); $$ = $3; }
	/*
	 * Anonymous enum definition. Tell add_symbol() to restart its counter.
	 */
	| ENUM_KEYW enum_body
		{ add_symbol(NULL, SYM_ENUM, NULL, 0); $$ = $2; }
	/* Anonymous s/u definitions.  Nothing needs doing.  */
	| STRUCT_KEYW class_body			{ $$ = $2; }
	| UNION_KEYW class_body				{ $$ = $2; }
	;

simple_type_specifier:
	CHAR_KEYW
	| SHORT_KEYW
	| INT_KEYW
	| LONG_KEYW
	| SIGNED_KEYW
	| UNSIGNED_KEYW
	| FLOAT_KEYW
	| DOUBLE_KEYW
	| VOID_KEYW
	| BOOL_KEYW
	| TYPE			{ (*$1)->tag = SYM_TYPEDEF; $$ = $1; }
	;

ptr_operator:
	'*' cvar_qualifier_seq_opt
		{ $$ = $2 ? $2 : $1; }
	;

cvar_qualifier_seq_opt:
	/* empty */					{ $$ = NULL; }
	| cvar_qualifier_seq
	;

cvar_qualifier_seq:
	cvar_qualifier
	| cvar_qualifier_seq cvar_qualifier		{ $$ = $2; }
	;

cvar_qualifier:
	CONST_KEYW | VOLATILE_KEYW | ATTRIBUTE_PHRASE
	| RESTRICT_KEYW
		{ /* restrict has no effect in prototypes so ignore it */
		  remove_node($1);
		  $$ = $1;
		}
	;

declarator:
	ptr_operator declarator			{ $$ = $2; }
	| direct_declarator
	;

direct_declarator:
	IDENT
		{ if (current_name != NULL) {
		    error_with_pos("unexpected second declaration name");
		    YYERROR;
		  } else {
		    current_name = (*$1)->string;
		    $$ = $1;
		  }
		}
	| direct_declarator '(' parameter_declaration_clause ')'
		{ $$ = $4; }
	| direct_declarator '(' error ')'
		{ $$ = $4; }
	| direct_declarator BRACKET_PHRASE
		{ $$ = $2; }
	| '(' declarator ')'
		{ $$ = $3; }
	| '(' error ')'
		{ $$ = $3; }
	;

/* Nested declarators differ from regular declarators in that they do
   not record the symbols they find in the global symbol table.  */
nested_declarator:
	ptr_operator nested_declarator		{ $$ = $2; }
	| direct_nested_declarator
	;

direct_nested_declarator:
	IDENT
	| TYPE
	| direct_nested_declarator '(' parameter_declaration_clause ')'
		{ $$ = $4; }
	| direct_nested_declarator '(' error ')'
		{ $$ = $4; }
	| direct_nested_declarator BRACKET_PHRASE
		{ $$ = $2; }
	| '(' nested_declarator ')'
		{ $$ = $3; }
	| '(' error ')'
		{ $$ = $3; }
	;

parameter_declaration_clause:
	parameter_declaration_list_opt DOTS		{ $$ = $2; }
	| parameter_declaration_list_opt
	| parameter_declaration_list ',' DOTS		{ $$ = $3; }
	;

parameter_declaration_list_opt:
	/* empty */					{ $$ = NULL; }
	| parameter_declaration_list
	;

parameter_declaration_list:
	parameter_declaration
	| parameter_declaration_list ',' parameter_declaration
		{ $$ = $3; }
	;

parameter_declaration:
	decl_specifier_seq m_abstract_declarator
		{ $$ = $2 ? $2 : $1; }
	;

m_abstract_declarator:
	ptr_operator m_abstract_declarator
		{ $$ = $2 ? $2 : $1; }
	| direct_m_abstract_declarator
	;

direct_m_abstract_declarator:
	/* empty */					{ $$ = NULL; }
	| IDENT
		{ /* For version 2 checksums, we don't want to remember
		     private parameter names.  */
		  remove_node($1);
		  $$ = $1;
		}
	/* This wasn't really a typedef name but an identifier that
	   shadows one.  */
	| TYPE
		{ remove_node($1);
		  $$ = $1;
		}
	| direct_m_abstract_declarator '(' parameter_declaration_clause ')'
		{ $$ = $4; }
	| direct_m_abstract_declarator '(' error ')'
		{ $$ = $4; }
	| direct_m_abstract_declarator BRACKET_PHRASE
		{ $$ = $2; }
	| '(' m_abstract_declarator ')'
		{ $$ = $3; }
	| '(' error ')'
		{ $$ = $3; }
	;

function_definition:
	decl_specifier_seq_opt declarator BRACE_PHRASE
		{ struct string_list *decl = *$2;
		  *$2 = NULL;
		  add_symbol(current_name, SYM_NORMAL, decl, is_extern);
		  $$ = $3;
		}
	;

initializer_opt:
	/* empty */					{ $$ = NULL; }
	| initializer
	;

/* We never care about the contents of an initializer.  */
initializer:
	'=' EXPRESSION_PHRASE
		{ remove_list($2, &(*$1)->next); $$ = $2; }
	;

class_body:
	'{' member_specification_opt '}'		{ $$ = $3; }
	| '{' error '}'					{ $$ = $3; }
	;

member_specification_opt:
	/* empty */					{ $$ = NULL; }
	| member_specification
	;

member_specification:
	member_declaration
	| member_specification member_declaration	{ $$ = $2; }
	;

member_declaration:
	decl_specifier_seq_opt member_declarator_list_opt ';'
		{ $$ = $3; }
	| error ';'
		{ $$ = $2; }
	;

member_declarator_list_opt:
	/* empty */					{ $$ = NULL; }
	| member_declarator_list
	;

member_declarator_list:
	member_declarator
	| member_declarator_list ',' member_declarator	{ $$ = $3; }
	;

member_declarator:
	nested_declarator attribute_opt			{ $$ = $2 ? $2 : $1; }
	| IDENT member_bitfield_declarator		{ $$ = $2; }
	| member_bitfield_declarator
	;

member_bitfield_declarator:
	':' EXPRESSION_PHRASE				{ $$ = $2; }
	;

attribute_opt:
	/* empty */					{ $$ = NULL; }
	| attribute_opt ATTRIBUTE_PHRASE
	;

enum_body:
	'{' enumerator_list '}'				{ $$ = $3; }
	| '{' enumerator_list ',' '}'			{ $$ = $4; }
	 ;

enumerator_list:
	enumerator
	| enumerator_list ',' enumerator

enumerator:
	IDENT
		{
			const char *name = strdup((*$1)->string);
			add_symbol(name, SYM_ENUM_CONST, NULL, 0);
		}
	| IDENT '=' EXPRESSION_PHRASE
		{
			const char *name = strdup((*$1)->string);
			struct string_list *expr = copy_list_range(*$3, *$2);
			add_symbol(name, SYM_ENUM_CONST, expr, 0);
		}

asm_definition:
	ASM_PHRASE ';'					{ $$ = $2; }
	;

asm_phrase_opt:
	/* empty */					{ $$ = NULL; }
	| ASM_PHRASE
	;

export_definition:
	EXPORT_SYMBOL_KEYW '(' IDENT ')' ';'
		{ export_symbol((*$3)->string); $$ = $5; }
	;


%%

static void
yyerror(const char *e)
{
  error_with_pos("%s", e);
}
