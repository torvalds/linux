/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Released under the terms of the GNU GPL v2.0.
 */

#ifndef EXPR_H
#define EXPR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

struct file {
	struct file *next;
	struct file *parent;
	const char *name;
	int lineno;
	int flags;
};

#define FILE_BUSY		0x0001
#define FILE_SCANNED		0x0002

typedef enum tristate {
	no, mod, yes
} tristate;

enum expr_type {
	E_NONE, E_OR, E_AND, E_NOT, E_EQUAL, E_UNEQUAL, E_LIST, E_SYMBOL, E_RANGE
};

union expr_data {
	struct expr *expr;
	struct symbol *sym;
};

struct expr {
	enum expr_type type;
	union expr_data left, right;
};

#define EXPR_OR(dep1, dep2)	(((dep1)>(dep2))?(dep1):(dep2))
#define EXPR_AND(dep1, dep2)	(((dep1)<(dep2))?(dep1):(dep2))
#define EXPR_NOT(dep)		(2-(dep))

#define expr_list_for_each_sym(l, e, s) \
	for (e = (l); e && (s = e->right.sym); e = e->left.expr)

struct expr_value {
	struct expr *expr;
	tristate tri;
};

struct symbol_value {
	void *val;
	tristate tri;
};

enum symbol_type {
	S_UNKNOWN, S_BOOLEAN, S_TRISTATE, S_INT, S_HEX, S_STRING, S_OTHER
};

/* enum values are used as index to symbol.def[] */
enum {
	S_DEF_USER,		/* main user value */
	S_DEF_AUTO,		/* values read from auto.conf */
	S_DEF_DEF3,		/* Reserved for UI usage */
	S_DEF_DEF4,		/* Reserved for UI usage */
	S_DEF_COUNT
};

struct symbol {
	struct symbol *next;
	char *name;
	enum symbol_type type;
	struct symbol_value curr;
	struct symbol_value def[S_DEF_COUNT];
	tristate visible;
	int flags;
	struct property *prop;
	struct expr_value dir_dep;
	struct expr_value rev_dep;
};

#define for_all_symbols(i, sym) for (i = 0; i < SYMBOL_HASHSIZE; i++) for (sym = symbol_hash[i]; sym; sym = sym->next) if (sym->type != S_OTHER)

#define SYMBOL_CONST      0x0001  /* symbol is const */
#define SYMBOL_CHECK      0x0008  /* used during dependency checking */
#define SYMBOL_CHOICE     0x0010  /* start of a choice block (null name) */
#define SYMBOL_CHOICEVAL  0x0020  /* used as a value in a choice block */
#define SYMBOL_VALID      0x0080  /* set when symbol.curr is calculated */
#define SYMBOL_OPTIONAL   0x0100  /* choice is optional - values can be 'n' */
#define SYMBOL_WRITE      0x0200  /* ? */
#define SYMBOL_CHANGED    0x0400  /* ? */
#define SYMBOL_AUTO       0x1000  /* value from environment variable */
#define SYMBOL_CHECKED    0x2000  /* used during dependency checking */
#define SYMBOL_WARNED     0x8000  /* warning has been issued */

/* Set when symbol.def[] is used */
#define SYMBOL_DEF        0x10000  /* First bit of SYMBOL_DEF */
#define SYMBOL_DEF_USER   0x10000  /* symbol.def[S_DEF_USER] is valid */
#define SYMBOL_DEF_AUTO   0x20000  /* symbol.def[S_DEF_AUTO] is valid */
#define SYMBOL_DEF3       0x40000  /* symbol.def[S_DEF_3] is valid */
#define SYMBOL_DEF4       0x80000  /* symbol.def[S_DEF_4] is valid */

#define SYMBOL_MAXLENGTH	256
#define SYMBOL_HASHSIZE		9973

/* A property represent the config options that can be associated
 * with a config "symbol".
 * Sample:
 * config FOO
 *         default y
 *         prompt "foo prompt"
 *         select BAR
 * config BAZ
 *         int "BAZ Value"
 *         range 1..255
 */
enum prop_type {
	P_UNKNOWN,
	P_PROMPT,   /* prompt "foo prompt" or "BAZ Value" */
	P_COMMENT,  /* text associated with a comment */
	P_MENU,     /* prompt associated with a menuconfig option */
	P_DEFAULT,  /* default y */
	P_CHOICE,   /* choice value */
	P_SELECT,   /* select BAR */
	P_RANGE,    /* range 7..100 (for a symbol) */
	P_ENV,      /* value from environment variable */
	P_SYMBOL,   /* where a symbol is defined */
};

struct property {
	struct property *next;     /* next property - null if last */
	struct symbol *sym;        /* the symbol for which the property is associated */
	enum prop_type type;       /* type of property */
	const char *text;          /* the prompt value - P_PROMPT, P_MENU, P_COMMENT */
	struct expr_value visible;
	struct expr *expr;         /* the optional conditional part of the property */
	struct menu *menu;         /* the menu the property are associated with
	                            * valid for: P_SELECT, P_RANGE, P_CHOICE,
	                            * P_PROMPT, P_DEFAULT, P_MENU, P_COMMENT */
	struct file *file;         /* what file was this property defined */
	int lineno;                /* what lineno was this property defined */
};

#define for_all_properties(sym, st, tok) \
	for (st = sym->prop; st; st = st->next) \
		if (st->type == (tok))
#define for_all_defaults(sym, st) for_all_properties(sym, st, P_DEFAULT)
#define for_all_choices(sym, st) for_all_properties(sym, st, P_CHOICE)
#define for_all_prompts(sym, st) \
	for (st = sym->prop; st; st = st->next) \
		if (st->text)

struct menu {
	struct menu *next;
	struct menu *parent;
	struct menu *list;
	struct symbol *sym;
	struct property *prompt;
	struct expr *visibility;
	struct expr *dep;
	unsigned int flags;
	char *help;
	struct file *file;
	int lineno;
	void *data;
};

#define MENU_CHANGED		0x0001
#define MENU_ROOT		0x0002

#ifndef SWIG

extern struct file *file_list;
extern struct file *current_file;
struct file *lookup_file(const char *name);

extern struct symbol symbol_yes, symbol_no, symbol_mod;
extern struct symbol *modules_sym;
extern struct symbol *sym_defconfig_list;
extern int cdebug;
struct expr *expr_alloc_symbol(struct symbol *sym);
struct expr *expr_alloc_one(enum expr_type type, struct expr *ce);
struct expr *expr_alloc_two(enum expr_type type, struct expr *e1, struct expr *e2);
struct expr *expr_alloc_comp(enum expr_type type, struct symbol *s1, struct symbol *s2);
struct expr *expr_alloc_and(struct expr *e1, struct expr *e2);
struct expr *expr_alloc_or(struct expr *e1, struct expr *e2);
struct expr *expr_copy(const struct expr *org);
void expr_free(struct expr *e);
int expr_eq(struct expr *e1, struct expr *e2);
void expr_eliminate_eq(struct expr **ep1, struct expr **ep2);
tristate expr_calc_value(struct expr *e);
struct expr *expr_eliminate_yn(struct expr *e);
struct expr *expr_trans_bool(struct expr *e);
struct expr *expr_eliminate_dups(struct expr *e);
struct expr *expr_transform(struct expr *e);
int expr_contains_symbol(struct expr *dep, struct symbol *sym);
bool expr_depends_symbol(struct expr *dep, struct symbol *sym);
struct expr *expr_extract_eq_and(struct expr **ep1, struct expr **ep2);
struct expr *expr_extract_eq_or(struct expr **ep1, struct expr **ep2);
void expr_extract_eq(enum expr_type type, struct expr **ep, struct expr **ep1, struct expr **ep2);
struct expr *expr_trans_compare(struct expr *e, enum expr_type type, struct symbol *sym);
struct expr *expr_simplify_unmet_dep(struct expr *e1, struct expr *e2);

void expr_fprint(struct expr *e, FILE *out);
struct gstr; /* forward */
void expr_gstr_print(struct expr *e, struct gstr *gs);

static inline int expr_is_yes(struct expr *e)
{
	return !e || (e->type == E_SYMBOL && e->left.sym == &symbol_yes);
}

static inline int expr_is_no(struct expr *e)
{
	return e && (e->type == E_SYMBOL && e->left.sym == &symbol_no);
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* EXPR_H */
