/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 */
%{

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "lkc.h"
#include "internal.h"
#include "preprocess.h"

#define printd(mask, fmt...) if (cdebug & (mask)) printf(fmt)

#define PRINTD		0x0001
#define DEBUG_PARSE	0x0002

int cdebug = PRINTD;

static void yyerror(const char *err);
static void zconfprint(const char *err, ...);
static void zconf_error(const char *err, ...);
static bool zconf_endtoken(const char *tokenname,
			   const char *expected_tokenname);

struct menu *current_menu, *current_entry;

%}

%union
{
	char *string;
	struct symbol *symbol;
	struct expr *expr;
	struct menu *menu;
	enum symbol_type type;
	enum variable_flavor flavor;
}

%token <string> T_HELPTEXT
%token <string> T_WORD
%token <string> T_WORD_QUOTE
%token T_BOOL
%token T_CHOICE
%token T_CLOSE_PAREN
%token T_COLON_EQUAL
%token T_COMMENT
%token T_CONFIG
%token T_DEFAULT
%token T_DEF_BOOL
%token T_DEF_TRISTATE
%token T_DEPENDS
%token T_ENDCHOICE
%token T_ENDIF
%token T_ENDMENU
%token T_HELP
%token T_HEX
%token T_IF
%token T_IMPLY
%token T_INT
%token T_MAINMENU
%token T_MENU
%token T_MENUCONFIG
%token T_MODULES
%token T_ON
%token T_OPEN_PAREN
%token T_OPTIONAL
%token T_PLUS_EQUAL
%token T_PROMPT
%token T_RANGE
%token T_SELECT
%token T_SOURCE
%token T_STRING
%token T_TRISTATE
%token T_VISIBLE
%token T_EOL
%token <string> T_ASSIGN_VAL

%left T_OR
%left T_AND
%left T_EQUAL T_UNEQUAL
%left T_LESS T_LESS_EQUAL T_GREATER T_GREATER_EQUAL
%nonassoc T_NOT

%type <symbol> nonconst_symbol
%type <symbol> symbol
%type <type> type logic_type default
%type <expr> expr
%type <expr> if_expr
%type <string> end
%type <menu> if_entry menu_entry choice_entry
%type <string> assign_val
%type <flavor> assign_op

%destructor {
	fprintf(stderr, "%s:%d: missing end statement for this entry\n",
		$$->filename, $$->lineno);
	if (current_menu == $$)
		menu_end_menu();
} if_entry menu_entry choice_entry

%%
input: mainmenu_stmt stmt_list | stmt_list;

/* mainmenu entry */

mainmenu_stmt: T_MAINMENU T_WORD_QUOTE T_EOL
{
	menu_add_prompt(P_MENU, $2, NULL);
};

stmt_list:
	  /* empty */
	| stmt_list assignment_stmt
	| stmt_list choice_stmt
	| stmt_list comment_stmt
	| stmt_list config_stmt
	| stmt_list if_stmt
	| stmt_list menu_stmt
	| stmt_list menuconfig_stmt
	| stmt_list source_stmt
	| stmt_list T_WORD error T_EOL	{ zconf_error("unknown statement \"%s\"", $2); }
	| stmt_list error T_EOL		{ zconf_error("invalid statement"); }
;

stmt_list_in_choice:
	  /* empty */
	| stmt_list_in_choice comment_stmt
	| stmt_list_in_choice config_stmt
	| stmt_list_in_choice if_stmt_in_choice
	| stmt_list_in_choice error T_EOL	{ zconf_error("invalid statement"); }
;

/* config/menuconfig entry */

config_entry_start: T_CONFIG nonconst_symbol T_EOL
{
	$2->flags |= SYMBOL_OPTIONAL;
	menu_add_entry($2);
	printd(DEBUG_PARSE, "%s:%d:config %s\n", cur_filename, cur_lineno, $2->name);
};

config_stmt: config_entry_start config_option_list
{
	printd(DEBUG_PARSE, "%s:%d:endconfig\n", cur_filename, cur_lineno);
};

menuconfig_entry_start: T_MENUCONFIG nonconst_symbol T_EOL
{
	$2->flags |= SYMBOL_OPTIONAL;
	menu_add_entry($2);
	printd(DEBUG_PARSE, "%s:%d:menuconfig %s\n", cur_filename, cur_lineno, $2->name);
};

menuconfig_stmt: menuconfig_entry_start config_option_list
{
	if (current_entry->prompt)
		current_entry->prompt->type = P_MENU;
	else
		zconfprint("warning: menuconfig statement without prompt");
	printd(DEBUG_PARSE, "%s:%d:endconfig\n", cur_filename, cur_lineno);
};

config_option_list:
	  /* empty */
	| config_option_list config_option
	| config_option_list depends
	| config_option_list help
;

config_option: type prompt_stmt_opt T_EOL
{
	menu_set_type($1);
	printd(DEBUG_PARSE, "%s:%d:type(%u)\n", cur_filename, cur_lineno, $1);
};

config_option: T_PROMPT T_WORD_QUOTE if_expr T_EOL
{
	menu_add_prompt(P_PROMPT, $2, $3);
	printd(DEBUG_PARSE, "%s:%d:prompt\n", cur_filename, cur_lineno);
};

config_option: default expr if_expr T_EOL
{
	menu_add_expr(P_DEFAULT, $2, $3);
	if ($1 != S_UNKNOWN)
		menu_set_type($1);
	printd(DEBUG_PARSE, "%s:%d:default(%u)\n", cur_filename, cur_lineno,
		$1);
};

config_option: T_SELECT nonconst_symbol if_expr T_EOL
{
	menu_add_symbol(P_SELECT, $2, $3);
	printd(DEBUG_PARSE, "%s:%d:select\n", cur_filename, cur_lineno);
};

config_option: T_IMPLY nonconst_symbol if_expr T_EOL
{
	menu_add_symbol(P_IMPLY, $2, $3);
	printd(DEBUG_PARSE, "%s:%d:imply\n", cur_filename, cur_lineno);
};

config_option: T_RANGE symbol symbol if_expr T_EOL
{
	menu_add_expr(P_RANGE, expr_alloc_comp(E_RANGE,$2, $3), $4);
	printd(DEBUG_PARSE, "%s:%d:range\n", cur_filename, cur_lineno);
};

config_option: T_MODULES T_EOL
{
	if (modules_sym)
		zconf_error("symbol '%s' redefines option 'modules' already defined by symbol '%s'",
			    current_entry->sym->name, modules_sym->name);
	modules_sym = current_entry->sym;
};

/* choice entry */

choice: T_CHOICE T_EOL
{
	struct symbol *sym = sym_lookup(NULL, SYMBOL_CHOICE);
	sym->flags |= SYMBOL_NO_WRITE;
	menu_add_entry(sym);
	menu_add_expr(P_CHOICE, NULL, NULL);
	printd(DEBUG_PARSE, "%s:%d:choice\n", cur_filename, cur_lineno);
};

choice_entry: choice choice_option_list
{
	if (!current_entry->prompt) {
		fprintf(stderr, "%s:%d: error: choice must have a prompt\n",
			current_entry->filename, current_entry->lineno);
		yynerrs++;
	}

	$$ = menu_add_menu();
};

choice_end: end
{
	if (zconf_endtoken($1, "choice")) {
		menu_end_menu();
		printd(DEBUG_PARSE, "%s:%d:endchoice\n", cur_filename, cur_lineno);
	}
};

choice_stmt: choice_entry stmt_list_in_choice choice_end
;

choice_option_list:
	  /* empty */
	| choice_option_list choice_option
	| choice_option_list depends
	| choice_option_list help
;

choice_option: T_PROMPT T_WORD_QUOTE if_expr T_EOL
{
	menu_add_prompt(P_PROMPT, $2, $3);
	printd(DEBUG_PARSE, "%s:%d:prompt\n", cur_filename, cur_lineno);
};

choice_option: logic_type prompt_stmt_opt T_EOL
{
	menu_set_type($1);
	printd(DEBUG_PARSE, "%s:%d:type(%u)\n", cur_filename, cur_lineno, $1);
};

choice_option: T_OPTIONAL T_EOL
{
	current_entry->sym->flags |= SYMBOL_OPTIONAL;
	printd(DEBUG_PARSE, "%s:%d:optional\n", cur_filename, cur_lineno);
};

choice_option: T_DEFAULT nonconst_symbol if_expr T_EOL
{
	menu_add_symbol(P_DEFAULT, $2, $3);
	printd(DEBUG_PARSE, "%s:%d:default\n", cur_filename, cur_lineno);
};

type:
	  logic_type
	| T_INT			{ $$ = S_INT; }
	| T_HEX			{ $$ = S_HEX; }
	| T_STRING		{ $$ = S_STRING; }

logic_type:
	  T_BOOL		{ $$ = S_BOOLEAN; }
	| T_TRISTATE		{ $$ = S_TRISTATE; }

default:
	  T_DEFAULT		{ $$ = S_UNKNOWN; }
	| T_DEF_BOOL		{ $$ = S_BOOLEAN; }
	| T_DEF_TRISTATE	{ $$ = S_TRISTATE; }

/* if entry */

if_entry: T_IF expr T_EOL
{
	printd(DEBUG_PARSE, "%s:%d:if\n", cur_filename, cur_lineno);
	menu_add_entry(NULL);
	menu_add_dep($2);
	$$ = menu_add_menu();
};

if_end: end
{
	if (zconf_endtoken($1, "if")) {
		menu_end_menu();
		printd(DEBUG_PARSE, "%s:%d:endif\n", cur_filename, cur_lineno);
	}
};

if_stmt: if_entry stmt_list if_end
;

if_stmt_in_choice: if_entry stmt_list_in_choice if_end
;

/* menu entry */

menu: T_MENU T_WORD_QUOTE T_EOL
{
	menu_add_entry(NULL);
	menu_add_prompt(P_MENU, $2, NULL);
	printd(DEBUG_PARSE, "%s:%d:menu\n", cur_filename, cur_lineno);
};

menu_entry: menu menu_option_list
{
	$$ = menu_add_menu();
};

menu_end: end
{
	if (zconf_endtoken($1, "menu")) {
		menu_end_menu();
		printd(DEBUG_PARSE, "%s:%d:endmenu\n", cur_filename, cur_lineno);
	}
};

menu_stmt: menu_entry stmt_list menu_end
;

menu_option_list:
	  /* empty */
	| menu_option_list visible
	| menu_option_list depends
;

source_stmt: T_SOURCE T_WORD_QUOTE T_EOL
{
	printd(DEBUG_PARSE, "%s:%d:source %s\n", cur_filename, cur_lineno, $2);
	zconf_nextfile($2);
	free($2);
};

/* comment entry */

comment: T_COMMENT T_WORD_QUOTE T_EOL
{
	menu_add_entry(NULL);
	menu_add_prompt(P_COMMENT, $2, NULL);
	printd(DEBUG_PARSE, "%s:%d:comment\n", cur_filename, cur_lineno);
};

comment_stmt: comment comment_option_list
;

comment_option_list:
	  /* empty */
	| comment_option_list depends
;

/* help option */

help_start: T_HELP T_EOL
{
	printd(DEBUG_PARSE, "%s:%d:help\n", cur_filename, cur_lineno);
	zconf_starthelp();
};

help: help_start T_HELPTEXT
{
	if (current_entry->help) {
		free(current_entry->help);
		zconfprint("warning: '%s' defined with more than one help text -- only the last one will be used",
			   current_entry->sym->name ?: "<choice>");
	}

	/* Is the help text empty or all whitespace? */
	if ($2[strspn($2, " \f\n\r\t\v")] == '\0')
		zconfprint("warning: '%s' defined with blank help text",
			   current_entry->sym->name ?: "<choice>");

	current_entry->help = $2;
};

/* depends option */

depends: T_DEPENDS T_ON expr T_EOL
{
	menu_add_dep($3);
	printd(DEBUG_PARSE, "%s:%d:depends on\n", cur_filename, cur_lineno);
};

/* visibility option */
visible: T_VISIBLE if_expr T_EOL
{
	menu_add_visibility($2);
};

/* prompt statement */

prompt_stmt_opt:
	  /* empty */
	| T_WORD_QUOTE if_expr
{
	menu_add_prompt(P_PROMPT, $1, $2);
};

end:	  T_ENDMENU T_EOL	{ $$ = "menu"; }
	| T_ENDCHOICE T_EOL	{ $$ = "choice"; }
	| T_ENDIF T_EOL		{ $$ = "if"; }
;

if_expr:  /* empty */			{ $$ = NULL; }
	| T_IF expr			{ $$ = $2; }
;

expr:	  symbol				{ $$ = expr_alloc_symbol($1); }
	| symbol T_LESS symbol			{ $$ = expr_alloc_comp(E_LTH, $1, $3); }
	| symbol T_LESS_EQUAL symbol		{ $$ = expr_alloc_comp(E_LEQ, $1, $3); }
	| symbol T_GREATER symbol		{ $$ = expr_alloc_comp(E_GTH, $1, $3); }
	| symbol T_GREATER_EQUAL symbol		{ $$ = expr_alloc_comp(E_GEQ, $1, $3); }
	| symbol T_EQUAL symbol			{ $$ = expr_alloc_comp(E_EQUAL, $1, $3); }
	| symbol T_UNEQUAL symbol		{ $$ = expr_alloc_comp(E_UNEQUAL, $1, $3); }
	| T_OPEN_PAREN expr T_CLOSE_PAREN	{ $$ = $2; }
	| T_NOT expr				{ $$ = expr_alloc_one(E_NOT, $2); }
	| expr T_OR expr			{ $$ = expr_alloc_two(E_OR, $1, $3); }
	| expr T_AND expr			{ $$ = expr_alloc_two(E_AND, $1, $3); }
;

/* For symbol definitions, selects, etc., where quotes are not accepted */
nonconst_symbol: T_WORD { $$ = sym_lookup($1, 0); free($1); };

symbol:	  nonconst_symbol
	| T_WORD_QUOTE	{ $$ = sym_lookup($1, SYMBOL_CONST); free($1); }
;

/* assignment statement */

assignment_stmt:  T_WORD assign_op assign_val T_EOL	{ variable_add($1, $3, $2); free($1); free($3); }

assign_op:
	  T_EQUAL	{ $$ = VAR_RECURSIVE; }
	| T_COLON_EQUAL	{ $$ = VAR_SIMPLE; }
	| T_PLUS_EQUAL	{ $$ = VAR_APPEND; }
;

assign_val:
	/* empty */		{ $$ = xstrdup(""); };
	| T_ASSIGN_VAL
;

%%

void conf_parse(const char *name)
{
	struct menu *menu;

	autoconf_cmd = str_new();

	str_printf(&autoconf_cmd, "\ndeps_config := \\\n");

	zconf_initscan(name);

	_menu_init();

	if (getenv("ZCONF_DEBUG"))
		yydebug = 1;
	yyparse();

	/*
	 * FIXME:
	 * cur_filename and cur_lineno are used even after yyparse();
	 * menu_finalize() calls menu_add_symbol(). This should be fixed.
	 */
	cur_filename = "<none>";
	cur_lineno = 0;

	str_printf(&autoconf_cmd,
		   "\n"
		   "$(autoconfig): $(deps_config)\n"
		   "$(deps_config): ;\n");

	env_write_dep(&autoconf_cmd);

	/* Variables are expanded in the parse phase. We can free them here. */
	variable_all_del();

	if (yynerrs)
		exit(1);
	if (!modules_sym)
		modules_sym = &symbol_no;

	if (!menu_has_prompt(&rootmenu)) {
		current_entry = &rootmenu;
		menu_add_prompt(P_MENU, "Main menu", NULL);
	}

	menu_finalize();

	menu = &rootmenu;
	while (menu) {
		if (menu->sym && sym_check_deps(menu->sym))
			yynerrs++;

		if (menu->list) {
			menu = menu->list;
			continue;
		}

		while (!menu->next && menu->parent)
			menu = menu->parent;

		menu = menu->next;
	}

	if (yynerrs)
		exit(1);
	conf_set_changed(true);
}

static bool zconf_endtoken(const char *tokenname,
			   const char *expected_tokenname)
{
	if (strcmp(tokenname, expected_tokenname)) {
		zconf_error("unexpected '%s' within %s block",
			    tokenname, expected_tokenname);
		yynerrs++;
		return false;
	}
	if (strcmp(current_menu->filename, cur_filename)) {
		zconf_error("'%s' in different file than '%s'",
			    tokenname, expected_tokenname);
		fprintf(stderr, "%s:%d: location of the '%s'\n",
			current_menu->filename, current_menu->lineno,
			expected_tokenname);
		yynerrs++;
		return false;
	}
	return true;
}

static void zconfprint(const char *err, ...)
{
	va_list ap;

	fprintf(stderr, "%s:%d: ", cur_filename, cur_lineno);
	va_start(ap, err);
	vfprintf(stderr, err, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

static void zconf_error(const char *err, ...)
{
	va_list ap;

	yynerrs++;
	fprintf(stderr, "%s:%d: ", cur_filename, cur_lineno);
	va_start(ap, err);
	vfprintf(stderr, err, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

static void yyerror(const char *err)
{
	fprintf(stderr, "%s:%d: %s\n", cur_filename, cur_lineno, err);
}

static void print_quoted_string(FILE *out, const char *str)
{
	const char *p;
	int len;

	putc('"', out);
	while ((p = strchr(str, '"'))) {
		len = p - str;
		if (len)
			fprintf(out, "%.*s", len, str);
		fputs("\\\"", out);
		str = p + 1;
	}
	fputs(str, out);
	putc('"', out);
}

static void print_symbol(FILE *out, struct menu *menu)
{
	struct symbol *sym = menu->sym;
	struct property *prop;

	if (sym_is_choice(sym))
		fprintf(out, "\nchoice\n");
	else
		fprintf(out, "\nconfig %s\n", sym->name);
	switch (sym->type) {
	case S_BOOLEAN:
		fputs("  bool\n", out);
		break;
	case S_TRISTATE:
		fputs("  tristate\n", out);
		break;
	case S_STRING:
		fputs("  string\n", out);
		break;
	case S_INT:
		fputs("  integer\n", out);
		break;
	case S_HEX:
		fputs("  hex\n", out);
		break;
	default:
		fputs("  ???\n", out);
		break;
	}
	for (prop = sym->prop; prop; prop = prop->next) {
		if (prop->menu != menu)
			continue;
		switch (prop->type) {
		case P_PROMPT:
			fputs("  prompt ", out);
			print_quoted_string(out, prop->text);
			if (!expr_is_yes(prop->visible.expr)) {
				fputs(" if ", out);
				expr_fprint(prop->visible.expr, out);
			}
			fputc('\n', out);
			break;
		case P_DEFAULT:
			fputs( "  default ", out);
			expr_fprint(prop->expr, out);
			if (!expr_is_yes(prop->visible.expr)) {
				fputs(" if ", out);
				expr_fprint(prop->visible.expr, out);
			}
			fputc('\n', out);
			break;
		case P_CHOICE:
			fputs("  #choice value\n", out);
			break;
		case P_SELECT:
			fputs( "  select ", out);
			expr_fprint(prop->expr, out);
			fputc('\n', out);
			break;
		case P_IMPLY:
			fputs( "  imply ", out);
			expr_fprint(prop->expr, out);
			fputc('\n', out);
			break;
		case P_RANGE:
			fputs( "  range ", out);
			expr_fprint(prop->expr, out);
			fputc('\n', out);
			break;
		case P_MENU:
			fputs( "  menu ", out);
			print_quoted_string(out, prop->text);
			fputc('\n', out);
			break;
		case P_SYMBOL:
			fputs( "  symbol ", out);
			fprintf(out, "%s\n", prop->menu->sym->name);
			break;
		default:
			fprintf(out, "  unknown prop %d!\n", prop->type);
			break;
		}
	}
	if (menu->help) {
		int len = strlen(menu->help);
		while (menu->help[--len] == '\n')
			menu->help[len] = 0;
		fprintf(out, "  help\n%s\n", menu->help);
	}
}

void zconfdump(FILE *out)
{
	struct property *prop;
	struct symbol *sym;
	struct menu *menu;

	menu = rootmenu.list;
	while (menu) {
		if ((sym = menu->sym))
			print_symbol(out, menu);
		else if ((prop = menu->prompt)) {
			switch (prop->type) {
			case P_COMMENT:
				fputs("\ncomment ", out);
				print_quoted_string(out, prop->text);
				fputs("\n", out);
				break;
			case P_MENU:
				fputs("\nmenu ", out);
				print_quoted_string(out, prop->text);
				fputs("\n", out);
				break;
			default:
				;
			}
			if (!expr_is_yes(prop->visible.expr)) {
				fputs("  depends ", out);
				expr_fprint(prop->visible.expr, out);
				fputc('\n', out);
			}
		}

		if (menu->list)
			menu = menu->list;
		else if (menu->next)
			menu = menu->next;
		else while ((menu = menu->parent)) {
			if (menu->prompt && menu->prompt->type == P_MENU)
				fputs("\nendmenu\n", out);
			if (menu->next) {
				menu = menu->next;
				break;
			}
		}
	}
}
