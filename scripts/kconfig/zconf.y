%{
/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Released under the terms of the GNU GPL v2.0.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define LKC_DIRECT_LINK
#include "lkc.h"

#include "zconf.hash.c"

#define printd(mask, fmt...) if (cdebug & (mask)) printf(fmt)

#define PRINTD		0x0001
#define DEBUG_PARSE	0x0002

int cdebug = PRINTD;

extern int zconflex(void);
static void zconfprint(const char *err, ...);
static void zconf_error(const char *err, ...);
static void zconferror(const char *err);
static bool zconf_endtoken(struct kconf_id *id, int starttoken, int endtoken);

struct symbol *symbol_hash[257];

static struct menu *current_menu, *current_entry;

#define YYDEBUG 0
#if YYDEBUG
#define YYERROR_VERBOSE
#endif
%}
%expect 26

%union
{
	char *string;
	struct file *file;
	struct symbol *symbol;
	struct expr *expr;
	struct menu *menu;
	struct kconf_id *id;
}

%token <id>T_MAINMENU
%token <id>T_MENU
%token <id>T_ENDMENU
%token <id>T_SOURCE
%token <id>T_CHOICE
%token <id>T_ENDCHOICE
%token <id>T_COMMENT
%token <id>T_CONFIG
%token <id>T_MENUCONFIG
%token <id>T_HELP
%token <string> T_HELPTEXT
%token <id>T_IF
%token <id>T_ENDIF
%token <id>T_DEPENDS
%token <id>T_OPTIONAL
%token <id>T_PROMPT
%token <id>T_TYPE
%token <id>T_DEFAULT
%token <id>T_SELECT
%token <id>T_RANGE
%token <id>T_OPTION
%token <id>T_ON
%token <string> T_WORD
%token <string> T_WORD_QUOTE
%token T_UNEQUAL
%token T_CLOSE_PAREN
%token T_OPEN_PAREN
%token T_EOL

%left T_OR
%left T_AND
%left T_EQUAL T_UNEQUAL
%nonassoc T_NOT

%type <string> prompt
%type <symbol> symbol
%type <expr> expr
%type <expr> if_expr
%type <id> end
%type <id> option_name
%type <menu> if_entry menu_entry choice_entry
%type <string> symbol_option_arg

%destructor {
	fprintf(stderr, "%s:%d: missing end statement for this entry\n",
		$$->file->name, $$->lineno);
	if (current_menu == $$)
		menu_end_menu();
} if_entry menu_entry choice_entry

%%
input: stmt_list;

stmt_list:
	  /* empty */
	| stmt_list common_stmt
	| stmt_list choice_stmt
	| stmt_list menu_stmt
	| stmt_list T_MAINMENU prompt nl
	| stmt_list end			{ zconf_error("unexpected end statement"); }
	| stmt_list T_WORD error T_EOL	{ zconf_error("unknown statement \"%s\"", $2); }
	| stmt_list option_name error T_EOL
{
	zconf_error("unexpected option \"%s\"", kconf_id_strings + $2->name);
}
	| stmt_list error T_EOL		{ zconf_error("invalid statement"); }
;

option_name:
	T_DEPENDS | T_PROMPT | T_TYPE | T_SELECT | T_OPTIONAL | T_RANGE | T_DEFAULT
;

common_stmt:
	  T_EOL
	| if_stmt
	| comment_stmt
	| config_stmt
	| menuconfig_stmt
	| source_stmt
;

option_error:
	  T_WORD error T_EOL		{ zconf_error("unknown option \"%s\"", $1); }
	| error T_EOL			{ zconf_error("invalid option"); }
;


/* config/menuconfig entry */

config_entry_start: T_CONFIG T_WORD T_EOL
{
	struct symbol *sym = sym_lookup($2, 0);
	sym->flags |= SYMBOL_OPTIONAL;
	menu_add_entry(sym);
	printd(DEBUG_PARSE, "%s:%d:config %s\n", zconf_curname(), zconf_lineno(), $2);
};

config_stmt: config_entry_start config_option_list
{
	menu_end_entry();
	printd(DEBUG_PARSE, "%s:%d:endconfig\n", zconf_curname(), zconf_lineno());
};

menuconfig_entry_start: T_MENUCONFIG T_WORD T_EOL
{
	struct symbol *sym = sym_lookup($2, 0);
	sym->flags |= SYMBOL_OPTIONAL;
	menu_add_entry(sym);
	printd(DEBUG_PARSE, "%s:%d:menuconfig %s\n", zconf_curname(), zconf_lineno(), $2);
};

menuconfig_stmt: menuconfig_entry_start config_option_list
{
	if (current_entry->prompt)
		current_entry->prompt->type = P_MENU;
	else
		zconfprint("warning: menuconfig statement without prompt");
	menu_end_entry();
	printd(DEBUG_PARSE, "%s:%d:endconfig\n", zconf_curname(), zconf_lineno());
};

config_option_list:
	  /* empty */
	| config_option_list config_option
	| config_option_list symbol_option
	| config_option_list depends
	| config_option_list help
	| config_option_list option_error
	| config_option_list T_EOL
;

config_option: T_TYPE prompt_stmt_opt T_EOL
{
	menu_set_type($1->stype);
	printd(DEBUG_PARSE, "%s:%d:type(%u)\n",
		zconf_curname(), zconf_lineno(),
		$1->stype);
};

config_option: T_PROMPT prompt if_expr T_EOL
{
	menu_add_prompt(P_PROMPT, $2, $3);
	printd(DEBUG_PARSE, "%s:%d:prompt\n", zconf_curname(), zconf_lineno());
};

config_option: T_DEFAULT expr if_expr T_EOL
{
	menu_add_expr(P_DEFAULT, $2, $3);
	if ($1->stype != S_UNKNOWN)
		menu_set_type($1->stype);
	printd(DEBUG_PARSE, "%s:%d:default(%u)\n",
		zconf_curname(), zconf_lineno(),
		$1->stype);
};

config_option: T_SELECT T_WORD if_expr T_EOL
{
	menu_add_symbol(P_SELECT, sym_lookup($2, 0), $3);
	printd(DEBUG_PARSE, "%s:%d:select\n", zconf_curname(), zconf_lineno());
};

config_option: T_RANGE symbol symbol if_expr T_EOL
{
	menu_add_expr(P_RANGE, expr_alloc_comp(E_RANGE,$2, $3), $4);
	printd(DEBUG_PARSE, "%s:%d:range\n", zconf_curname(), zconf_lineno());
};

symbol_option: T_OPTION symbol_option_list T_EOL
;

symbol_option_list:
	  /* empty */
	| symbol_option_list T_WORD symbol_option_arg
{
	struct kconf_id *id = kconf_id_lookup($2, strlen($2));
	if (id && id->flags & TF_OPTION)
		menu_add_option(id->token, $3);
	else
		zconfprint("warning: ignoring unknown option %s", $2);
	free($2);
};

symbol_option_arg:
	  /* empty */		{ $$ = NULL; }
	| T_EQUAL prompt	{ $$ = $2; }
;

/* choice entry */

choice: T_CHOICE T_EOL
{
	struct symbol *sym = sym_lookup(NULL, 0);
	sym->flags |= SYMBOL_CHOICE;
	menu_add_entry(sym);
	menu_add_expr(P_CHOICE, NULL, NULL);
	printd(DEBUG_PARSE, "%s:%d:choice\n", zconf_curname(), zconf_lineno());
};

choice_entry: choice choice_option_list
{
	$$ = menu_add_menu();
};

choice_end: end
{
	if (zconf_endtoken($1, T_CHOICE, T_ENDCHOICE)) {
		menu_end_menu();
		printd(DEBUG_PARSE, "%s:%d:endchoice\n", zconf_curname(), zconf_lineno());
	}
};

choice_stmt: choice_entry choice_block choice_end
;

choice_option_list:
	  /* empty */
	| choice_option_list choice_option
	| choice_option_list depends
	| choice_option_list help
	| choice_option_list T_EOL
	| choice_option_list option_error
;

choice_option: T_PROMPT prompt if_expr T_EOL
{
	menu_add_prompt(P_PROMPT, $2, $3);
	printd(DEBUG_PARSE, "%s:%d:prompt\n", zconf_curname(), zconf_lineno());
};

choice_option: T_TYPE prompt_stmt_opt T_EOL
{
	if ($1->stype == S_BOOLEAN || $1->stype == S_TRISTATE) {
		menu_set_type($1->stype);
		printd(DEBUG_PARSE, "%s:%d:type(%u)\n",
			zconf_curname(), zconf_lineno(),
			$1->stype);
	} else
		YYERROR;
};

choice_option: T_OPTIONAL T_EOL
{
	current_entry->sym->flags |= SYMBOL_OPTIONAL;
	printd(DEBUG_PARSE, "%s:%d:optional\n", zconf_curname(), zconf_lineno());
};

choice_option: T_DEFAULT T_WORD if_expr T_EOL
{
	if ($1->stype == S_UNKNOWN) {
		menu_add_symbol(P_DEFAULT, sym_lookup($2, 0), $3);
		printd(DEBUG_PARSE, "%s:%d:default\n",
			zconf_curname(), zconf_lineno());
	} else
		YYERROR;
};

choice_block:
	  /* empty */
	| choice_block common_stmt
;

/* if entry */

if_entry: T_IF expr nl
{
	printd(DEBUG_PARSE, "%s:%d:if\n", zconf_curname(), zconf_lineno());
	menu_add_entry(NULL);
	menu_add_dep($2);
	$$ = menu_add_menu();
};

if_end: end
{
	if (zconf_endtoken($1, T_IF, T_ENDIF)) {
		menu_end_menu();
		printd(DEBUG_PARSE, "%s:%d:endif\n", zconf_curname(), zconf_lineno());
	}
};

if_stmt: if_entry if_block if_end
;

if_block:
	  /* empty */
	| if_block common_stmt
	| if_block menu_stmt
	| if_block choice_stmt
;

/* menu entry */

menu: T_MENU prompt T_EOL
{
	menu_add_entry(NULL);
	menu_add_prompt(P_MENU, $2, NULL);
	printd(DEBUG_PARSE, "%s:%d:menu\n", zconf_curname(), zconf_lineno());
};

menu_entry: menu depends_list
{
	$$ = menu_add_menu();
};

menu_end: end
{
	if (zconf_endtoken($1, T_MENU, T_ENDMENU)) {
		menu_end_menu();
		printd(DEBUG_PARSE, "%s:%d:endmenu\n", zconf_curname(), zconf_lineno());
	}
};

menu_stmt: menu_entry menu_block menu_end
;

menu_block:
	  /* empty */
	| menu_block common_stmt
	| menu_block menu_stmt
	| menu_block choice_stmt
;

source_stmt: T_SOURCE prompt T_EOL
{
	printd(DEBUG_PARSE, "%s:%d:source %s\n", zconf_curname(), zconf_lineno(), $2);
	zconf_nextfile($2);
};

/* comment entry */

comment: T_COMMENT prompt T_EOL
{
	menu_add_entry(NULL);
	menu_add_prompt(P_COMMENT, $2, NULL);
	printd(DEBUG_PARSE, "%s:%d:comment\n", zconf_curname(), zconf_lineno());
};

comment_stmt: comment depends_list
{
	menu_end_entry();
};

/* help option */

help_start: T_HELP T_EOL
{
	printd(DEBUG_PARSE, "%s:%d:help\n", zconf_curname(), zconf_lineno());
	zconf_starthelp();
};

help: help_start T_HELPTEXT
{
	current_entry->help = $2;
};

/* depends option */

depends_list:
	  /* empty */
	| depends_list depends
	| depends_list T_EOL
	| depends_list option_error
;

depends: T_DEPENDS T_ON expr T_EOL
{
	menu_add_dep($3);
	printd(DEBUG_PARSE, "%s:%d:depends on\n", zconf_curname(), zconf_lineno());
};

/* prompt statement */

prompt_stmt_opt:
	  /* empty */
	| prompt if_expr
{
	menu_add_prompt(P_PROMPT, $1, $2);
};

prompt:	  T_WORD
	| T_WORD_QUOTE
;

end:	  T_ENDMENU T_EOL	{ $$ = $1; }
	| T_ENDCHOICE T_EOL	{ $$ = $1; }
	| T_ENDIF T_EOL		{ $$ = $1; }
;

nl:
	  T_EOL
	| nl T_EOL
;

if_expr:  /* empty */			{ $$ = NULL; }
	| T_IF expr			{ $$ = $2; }
;

expr:	  symbol				{ $$ = expr_alloc_symbol($1); }
	| symbol T_EQUAL symbol			{ $$ = expr_alloc_comp(E_EQUAL, $1, $3); }
	| symbol T_UNEQUAL symbol		{ $$ = expr_alloc_comp(E_UNEQUAL, $1, $3); }
	| T_OPEN_PAREN expr T_CLOSE_PAREN	{ $$ = $2; }
	| T_NOT expr				{ $$ = expr_alloc_one(E_NOT, $2); }
	| expr T_OR expr			{ $$ = expr_alloc_two(E_OR, $1, $3); }
	| expr T_AND expr			{ $$ = expr_alloc_two(E_AND, $1, $3); }
;

symbol:	  T_WORD	{ $$ = sym_lookup($1, 0); free($1); }
	| T_WORD_QUOTE	{ $$ = sym_lookup($1, 1); free($1); }
;

%%

void conf_parse(const char *name)
{
	struct symbol *sym;
	int i;

	zconf_initscan(name);

	sym_init();
	menu_init();
	modules_sym = sym_lookup(NULL, 0);
	modules_sym->type = S_BOOLEAN;
	modules_sym->flags |= SYMBOL_AUTO;
	rootmenu.prompt = menu_add_prompt(P_MENU, "Linux Kernel Configuration", NULL);

#if YYDEBUG
	if (getenv("ZCONF_DEBUG"))
		zconfdebug = 1;
#endif
	zconfparse();
	if (zconfnerrs)
		exit(1);
	if (!modules_sym->prop) {
		struct property *prop;

		prop = prop_alloc(P_DEFAULT, modules_sym);
		prop->expr = expr_alloc_symbol(sym_lookup("MODULES", 0));
	}
	menu_finalize(&rootmenu);
	for_all_symbols(i, sym) {
		if (sym_check_deps(sym))
			zconfnerrs++;
        }
	if (zconfnerrs)
		exit(1);
	sym_set_change_count(1);
}

const char *zconf_tokenname(int token)
{
	switch (token) {
	case T_MENU:		return "menu";
	case T_ENDMENU:		return "endmenu";
	case T_CHOICE:		return "choice";
	case T_ENDCHOICE:	return "endchoice";
	case T_IF:		return "if";
	case T_ENDIF:		return "endif";
	case T_DEPENDS:		return "depends";
	}
	return "<token>";
}

static bool zconf_endtoken(struct kconf_id *id, int starttoken, int endtoken)
{
	if (id->token != endtoken) {
		zconf_error("unexpected '%s' within %s block",
			kconf_id_strings + id->name, zconf_tokenname(starttoken));
		zconfnerrs++;
		return false;
	}
	if (current_menu->file != current_file) {
		zconf_error("'%s' in different file than '%s'",
			kconf_id_strings + id->name, zconf_tokenname(starttoken));
		fprintf(stderr, "%s:%d: location of the '%s'\n",
			current_menu->file->name, current_menu->lineno,
			zconf_tokenname(starttoken));
		zconfnerrs++;
		return false;
	}
	return true;
}

static void zconfprint(const char *err, ...)
{
	va_list ap;

	fprintf(stderr, "%s:%d: ", zconf_curname(), zconf_lineno());
	va_start(ap, err);
	vfprintf(stderr, err, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

static void zconf_error(const char *err, ...)
{
	va_list ap;

	zconfnerrs++;
	fprintf(stderr, "%s:%d: ", zconf_curname(), zconf_lineno());
	va_start(ap, err);
	vfprintf(stderr, err, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

static void zconferror(const char *err)
{
#if YYDEBUG
	fprintf(stderr, "%s:%d: %s\n", zconf_curname(), zconf_lineno() + 1, err);
#endif
}

void print_quoted_string(FILE *out, const char *str)
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

void print_symbol(FILE *out, struct menu *menu)
{
	struct symbol *sym = menu->sym;
	struct property *prop;

	if (sym_is_choice(sym))
		fprintf(out, "choice\n");
	else
		fprintf(out, "config %s\n", sym->name);
	switch (sym->type) {
	case S_BOOLEAN:
		fputs("  boolean\n", out);
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
	fputc('\n', out);
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
			fputs("\n", out);
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

#include "lex.zconf.c"
#include "util.c"
#include "confdata.c"
#include "expr.c"
#include "symbol.c"
#include "menu.c"
