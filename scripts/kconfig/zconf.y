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

#define printd(mask, fmt...) if (cdebug & (mask)) printf(fmt)

#define PRINTD		0x0001
#define DEBUG_PARSE	0x0002

int cdebug = PRINTD;

extern int zconflex(void);
static void zconfprint(const char *err, ...);
static void zconferror(const char *err);
static bool zconf_endtoken(int token, int starttoken, int endtoken);

struct symbol *symbol_hash[257];

static struct menu *current_menu, *current_entry;

#define YYERROR_VERBOSE
%}
%expect 40

%union
{
	int token;
	char *string;
	struct symbol *symbol;
	struct expr *expr;
	struct menu *menu;
}

%token T_MAINMENU
%token T_MENU
%token T_ENDMENU
%token T_SOURCE
%token T_CHOICE
%token T_ENDCHOICE
%token T_COMMENT
%token T_CONFIG
%token T_MENUCONFIG
%token T_HELP
%token <string> T_HELPTEXT
%token T_IF
%token T_ENDIF
%token T_DEPENDS
%token T_REQUIRES
%token T_OPTIONAL
%token T_PROMPT
%token T_DEFAULT
%token T_TRISTATE
%token T_DEF_TRISTATE
%token T_BOOLEAN
%token T_DEF_BOOLEAN
%token T_STRING
%token T_INT
%token T_HEX
%token <string> T_WORD
%token <string> T_WORD_QUOTE
%token T_UNEQUAL
%token T_EOF
%token T_EOL
%token T_CLOSE_PAREN
%token T_OPEN_PAREN
%token T_ON
%token T_SELECT
%token T_RANGE

%left T_OR
%left T_AND
%left T_EQUAL T_UNEQUAL
%nonassoc T_NOT

%type <string> prompt
%type <string> source
%type <symbol> symbol
%type <expr> expr
%type <expr> if_expr
%type <token> end

%{
#define LKC_DIRECT_LINK
#include "lkc.h"
%}
%%
input:	  /* empty */
	| input block
;

block:	  common_block
	| choice_stmt
	| menu_stmt
	| T_MAINMENU prompt nl_or_eof
	| T_ENDMENU		{ zconfprint("unexpected 'endmenu' statement"); }
	| T_ENDIF		{ zconfprint("unexpected 'endif' statement"); }
	| T_ENDCHOICE		{ zconfprint("unexpected 'endchoice' statement"); }
	| error nl_or_eof	{ zconfprint("syntax error"); yyerrok; }
;

common_block:
	  if_stmt
	| comment_stmt
	| config_stmt
	| menuconfig_stmt
	| source_stmt
	| nl_or_eof
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
	| config_option_list depends
	| config_option_list help
	| config_option_list T_EOL
;

config_option: T_TRISTATE prompt_stmt_opt T_EOL
{
	menu_set_type(S_TRISTATE);
	printd(DEBUG_PARSE, "%s:%d:tristate\n", zconf_curname(), zconf_lineno());
};

config_option: T_DEF_TRISTATE expr if_expr T_EOL
{
	menu_add_expr(P_DEFAULT, $2, $3);
	menu_set_type(S_TRISTATE);
	printd(DEBUG_PARSE, "%s:%d:def_boolean\n", zconf_curname(), zconf_lineno());
};

config_option: T_BOOLEAN prompt_stmt_opt T_EOL
{
	menu_set_type(S_BOOLEAN);
	printd(DEBUG_PARSE, "%s:%d:boolean\n", zconf_curname(), zconf_lineno());
};

config_option: T_DEF_BOOLEAN expr if_expr T_EOL
{
	menu_add_expr(P_DEFAULT, $2, $3);
	menu_set_type(S_BOOLEAN);
	printd(DEBUG_PARSE, "%s:%d:def_boolean\n", zconf_curname(), zconf_lineno());
};

config_option: T_INT prompt_stmt_opt T_EOL
{
	menu_set_type(S_INT);
	printd(DEBUG_PARSE, "%s:%d:int\n", zconf_curname(), zconf_lineno());
};

config_option: T_HEX prompt_stmt_opt T_EOL
{
	menu_set_type(S_HEX);
	printd(DEBUG_PARSE, "%s:%d:hex\n", zconf_curname(), zconf_lineno());
};

config_option: T_STRING prompt_stmt_opt T_EOL
{
	menu_set_type(S_STRING);
	printd(DEBUG_PARSE, "%s:%d:string\n", zconf_curname(), zconf_lineno());
};

config_option: T_PROMPT prompt if_expr T_EOL
{
	menu_add_prompt(P_PROMPT, $2, $3);
	printd(DEBUG_PARSE, "%s:%d:prompt\n", zconf_curname(), zconf_lineno());
};

config_option: T_DEFAULT expr if_expr T_EOL
{
	menu_add_expr(P_DEFAULT, $2, $3);
	printd(DEBUG_PARSE, "%s:%d:default\n", zconf_curname(), zconf_lineno());
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
	menu_end_entry();
	menu_add_menu();
};

choice_end: end
{
	if (zconf_endtoken($1, T_CHOICE, T_ENDCHOICE)) {
		menu_end_menu();
		printd(DEBUG_PARSE, "%s:%d:endchoice\n", zconf_curname(), zconf_lineno());
	}
};

choice_stmt:
	  choice_entry choice_block choice_end
	| choice_entry choice_block
{
	printf("%s:%d: missing 'endchoice' for this 'choice' statement\n", current_menu->file->name, current_menu->lineno);
	zconfnerrs++;
};

choice_option_list:
	  /* empty */
	| choice_option_list choice_option
	| choice_option_list depends
	| choice_option_list help
	| choice_option_list T_EOL
;

choice_option: T_PROMPT prompt if_expr T_EOL
{
	menu_add_prompt(P_PROMPT, $2, $3);
	printd(DEBUG_PARSE, "%s:%d:prompt\n", zconf_curname(), zconf_lineno());
};

choice_option: T_TRISTATE prompt_stmt_opt T_EOL
{
	menu_set_type(S_TRISTATE);
	printd(DEBUG_PARSE, "%s:%d:tristate\n", zconf_curname(), zconf_lineno());
};

choice_option: T_BOOLEAN prompt_stmt_opt T_EOL
{
	menu_set_type(S_BOOLEAN);
	printd(DEBUG_PARSE, "%s:%d:boolean\n", zconf_curname(), zconf_lineno());
};

choice_option: T_OPTIONAL T_EOL
{
	current_entry->sym->flags |= SYMBOL_OPTIONAL;
	printd(DEBUG_PARSE, "%s:%d:optional\n", zconf_curname(), zconf_lineno());
};

choice_option: T_DEFAULT T_WORD if_expr T_EOL
{
	menu_add_symbol(P_DEFAULT, sym_lookup($2, 0), $3);
	printd(DEBUG_PARSE, "%s:%d:default\n", zconf_curname(), zconf_lineno());
};

choice_block:
	  /* empty */
	| choice_block common_block
;

/* if entry */

if: T_IF expr T_EOL
{
	printd(DEBUG_PARSE, "%s:%d:if\n", zconf_curname(), zconf_lineno());
	menu_add_entry(NULL);
	menu_add_dep($2);
	menu_end_entry();
	menu_add_menu();
};

if_end: end
{
	if (zconf_endtoken($1, T_IF, T_ENDIF)) {
		menu_end_menu();
		printd(DEBUG_PARSE, "%s:%d:endif\n", zconf_curname(), zconf_lineno());
	}
};

if_stmt:
	  if if_block if_end
	| if if_block
{
	printf("%s:%d: missing 'endif' for this 'if' statement\n", current_menu->file->name, current_menu->lineno);
	zconfnerrs++;
};

if_block:
	  /* empty */
	| if_block common_block
	| if_block menu_stmt
	| if_block choice_stmt
;

/* menu entry */

menu: T_MENU prompt T_EOL
{
	menu_add_entry(NULL);
	menu_add_prop(P_MENU, $2, NULL, NULL);
	printd(DEBUG_PARSE, "%s:%d:menu\n", zconf_curname(), zconf_lineno());
};

menu_entry: menu depends_list
{
	menu_end_entry();
	menu_add_menu();
};

menu_end: end
{
	if (zconf_endtoken($1, T_MENU, T_ENDMENU)) {
		menu_end_menu();
		printd(DEBUG_PARSE, "%s:%d:endmenu\n", zconf_curname(), zconf_lineno());
	}
};

menu_stmt:
	  menu_entry menu_block menu_end
	| menu_entry menu_block
{
	printf("%s:%d: missing 'endmenu' for this 'menu' statement\n", current_menu->file->name, current_menu->lineno);
	zconfnerrs++;
};

menu_block:
	  /* empty */
	| menu_block common_block
	| menu_block menu_stmt
	| menu_block choice_stmt
	| menu_block error T_EOL		{ zconfprint("invalid menu option"); yyerrok; }
;

source: T_SOURCE prompt T_EOL
{
	$$ = $2;
	printd(DEBUG_PARSE, "%s:%d:source %s\n", zconf_curname(), zconf_lineno(), $2);
};

source_stmt: source
{
	zconf_nextfile($1);
};

/* comment entry */

comment: T_COMMENT prompt T_EOL
{
	menu_add_entry(NULL);
	menu_add_prop(P_COMMENT, $2, NULL, NULL);
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
	current_entry->sym->help = $2;
};

/* depends option */

depends_list:	  /* empty */
		| depends_list depends
		| depends_list T_EOL
;

depends: T_DEPENDS T_ON expr T_EOL
{
	menu_add_dep($3);
	printd(DEBUG_PARSE, "%s:%d:depends on\n", zconf_curname(), zconf_lineno());
}
	| T_DEPENDS expr T_EOL
{
	menu_add_dep($2);
	printd(DEBUG_PARSE, "%s:%d:depends\n", zconf_curname(), zconf_lineno());
}
	| T_REQUIRES expr T_EOL
{
	menu_add_dep($2);
	printd(DEBUG_PARSE, "%s:%d:requires\n", zconf_curname(), zconf_lineno());
};

/* prompt statement */

prompt_stmt_opt:
	  /* empty */
	| prompt if_expr
{
	menu_add_prop(P_PROMPT, $1, NULL, $2);
};

prompt:	  T_WORD
	| T_WORD_QUOTE
;

end:	  T_ENDMENU nl_or_eof	{ $$ = T_ENDMENU; }
	| T_ENDCHOICE nl_or_eof	{ $$ = T_ENDCHOICE; }
	| T_ENDIF nl_or_eof	{ $$ = T_ENDIF; }
;

nl_or_eof:
	T_EOL | T_EOF;

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
	modules_sym = sym_lookup("MODULES", 0);
	rootmenu.prompt = menu_add_prop(P_MENU, "Linux Kernel Configuration", NULL, NULL);

	//zconfdebug = 1;
	zconfparse();
	if (zconfnerrs)
		exit(1);
	menu_finalize(&rootmenu);
	for_all_symbols(i, sym) {
                if (!(sym->flags & SYMBOL_CHECKED) && sym_check_deps(sym))
                        printf("\n");
		else
			sym->flags |= SYMBOL_CHECK_DONE;
        }

	sym_change_count = 1;
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
	}
	return "<token>";
}

static bool zconf_endtoken(int token, int starttoken, int endtoken)
{
	if (token != endtoken) {
		zconfprint("unexpected '%s' within %s block", zconf_tokenname(token), zconf_tokenname(starttoken));
		zconfnerrs++;
		return false;
	}
	if (current_menu->file != current_file) {
		zconfprint("'%s' in different file than '%s'", zconf_tokenname(token), zconf_tokenname(starttoken));
		zconfprint("location of the '%s'", zconf_tokenname(starttoken));
		zconfnerrs++;
		return false;
	}
	return true;
}

static void zconfprint(const char *err, ...)
{
	va_list ap;

	fprintf(stderr, "%s:%d: ", zconf_curname(), zconf_lineno() + 1);
	va_start(ap, err);
	vfprintf(stderr, err, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

static void zconferror(const char *err)
{
	fprintf(stderr, "%s:%d: %s\n", zconf_curname(), zconf_lineno() + 1, err);
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
	if (sym->help) {
		int len = strlen(sym->help);
		while (sym->help[--len] == '\n')
			sym->help[len] = 0;
		fprintf(out, "  help\n%s\n", sym->help);
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
