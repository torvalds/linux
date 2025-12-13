/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 */

#ifndef EXPR_H
#define EXPR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdio.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

#include <list_types.h>

typedef enum tristate {
	no, mod, yes
} tristate;

enum expr_type {
	E_NONE, E_OR, E_AND, E_NOT,
	E_EQUAL, E_UNEQUAL, E_LTH, E_LEQ, E_GTH, E_GEQ,
	E_SYMBOL, E_RANGE
};

union expr_data {
	struct expr * const expr;
	struct symbol * const sym;
	void *_initdata;
};

/**
 * struct expr - expression
 *
 * @node:  link node for the hash table
 * @type:  expressoin type
 * @val: calculated tristate value
 * @val_is_valid: indicate whether the value is valid
 * @left:  left node
 * @right: right node
 */
struct expr {
	struct hlist_node node;
	enum expr_type type;
	tristate val;
	bool val_is_valid;
	union expr_data left, right;
};

#define EXPR_OR(dep1, dep2)	(((dep1)>(dep2))?(dep1):(dep2))
#define EXPR_AND(dep1, dep2)	(((dep1)<(dep2))?(dep1):(dep2))
#define EXPR_NOT(dep)		(2-(dep))

struct expr_value {
	struct expr *expr;
	tristate tri;
};

struct symbol_value {
	void *val;
	tristate tri;
};

enum symbol_type {
	S_UNKNOWN, S_BOOLEAN, S_TRISTATE, S_INT, S_HEX, S_STRING
};

/* enum values are used as index to symbol.def[] */
enum {
	S_DEF_USER,		/* main user value */
	S_DEF_AUTO,		/* values read from auto.conf */
	S_DEF_DEF3,		/* Reserved for UI usage */
	S_DEF_DEF4,		/* Reserved for UI usage */
	S_DEF_COUNT
};

/*
 * Represents a configuration symbol.
 *
 * Choices are represented as a special kind of symbol with null name.
 *
 * @choice_link: linked to menu::choice_members
 */
struct symbol {
	/* link node for the hash table */
	struct hlist_node node;

	/* The name of the symbol, e.g. "FOO" for 'config FOO' */
	char *name;

	/* S_BOOLEAN, S_TRISTATE, ... */
	enum symbol_type type;

	/*
	 * The calculated value of the symbol. The SYMBOL_VALID bit is set in
	 * 'flags' when this is up to date. Note that this value might differ
	 * from the user value set in e.g. a .config file, due to visibility.
	 */
	struct symbol_value curr;

	/*
	 * Values for the symbol provided from outside. def[S_DEF_USER] holds
	 * the .config value.
	 */
	struct symbol_value def[S_DEF_COUNT];

	/*
	 * An upper bound on the tristate value the user can set for the symbol
	 * if it is a boolean or tristate. Calculated from prompt dependencies,
	 * which also inherit dependencies from enclosing menus, choices, and
	 * ifs. If 'n', the user value will be ignored.
	 *
	 * Symbols lacking prompts always have visibility 'n'.
	 */
	tristate visible;

	/* config entries associated with this symbol */
	struct list_head menus;

	struct list_head choice_link;

	/* SYMBOL_* flags */
	int flags;

	/* List of properties. See prop_type. */
	struct property *prop;

	/* Dependencies from enclosing menus, choices, and ifs */
	struct expr_value dir_dep;

	/* Reverse dependencies through being selected by other symbols */
	struct expr_value rev_dep;

	/*
	 * "Weak" reverse dependencies through being implied by other symbols
	 */
	struct expr_value implied;
};

#define SYMBOL_CONST      0x0001  /* symbol is const */
#define SYMBOL_CHECK      0x0008  /* used during dependency checking */
#define SYMBOL_VALID      0x0080  /* set when symbol.curr is calculated */
#define SYMBOL_TRANS      0x0100  /* symbol is transitional only (not visible)*/
#define SYMBOL_WRITE      0x0200  /* write symbol to file (KCONFIG_CONFIG) */
#define SYMBOL_WRITTEN    0x0800  /* track info to avoid double-write to .config */
#define SYMBOL_CHECKED    0x2000  /* used during dependency checking */
#define SYMBOL_WARNED     0x8000  /* warning has been issued */

/* Set when symbol.def[] is used */
#define SYMBOL_DEF        0x10000  /* First bit of SYMBOL_DEF */
#define SYMBOL_DEF_USER   0x10000  /* symbol.def[S_DEF_USER] is valid */
#define SYMBOL_DEF_AUTO   0x20000  /* symbol.def[S_DEF_AUTO] is valid */
#define SYMBOL_DEF3       0x40000  /* symbol.def[S_DEF_3] is valid */
#define SYMBOL_DEF4       0x80000  /* symbol.def[S_DEF_4] is valid */

#define SYMBOL_MAXLENGTH	256

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
 *
 * Please, also check parser.y:print_symbol() when modifying the
 * list of property types!
 */
enum prop_type {
	P_UNKNOWN,
	P_PROMPT,   /* prompt "foo prompt" or "BAZ Value" */
	P_COMMENT,  /* text associated with a comment */
	P_MENU,     /* prompt associated with a menu or menuconfig symbol */
	P_DEFAULT,  /* default y */
	P_SELECT,   /* select BAR */
	P_IMPLY,    /* imply BAR */
	P_RANGE,    /* range 7..100 (for a symbol) */
};

struct property {
	struct property *next;     /* next property - null if last */
	enum prop_type type;       /* type of property */
	const char *text;          /* the prompt value - P_PROMPT, P_MENU, P_COMMENT */
	struct expr_value visible;
	struct expr *expr;         /* the optional conditional part of the property */
	struct menu *menu;         /* the menu the property are associated with
	                            * valid for: P_SELECT, P_RANGE,
	                            * P_PROMPT, P_DEFAULT, P_MENU, P_COMMENT */
	const char *filename;      /* what file was this property defined */
	int lineno;                /* what lineno was this property defined */
};

#define for_all_properties(sym, st, tok) \
	for (st = sym->prop; st; st = st->next) \
		if (st->type == (tok))
#define for_all_defaults(sym, st) for_all_properties(sym, st, P_DEFAULT)
#define for_all_prompts(sym, st) \
	for (st = sym->prop; st; st = st->next) \
		if (st->text)

enum menu_type {
	M_CHOICE,  // "choice"
	M_COMMENT, // "comment"
	M_IF,      // "if"
	M_MENU,    // "mainmenu", "menu", "menuconfig"
	M_NORMAL,  // others, i.e., "config"
};

/*
 * Represents a node in the menu tree, as seen in e.g. menuconfig (though used
 * for all front ends). Each symbol, menu, etc. defined in the Kconfig files
 * gets a node. A symbol defined in multiple locations gets one node at each
 * location.
 *
 * @type: type of the menu entry
 * @choice_members: list of choice members with priority.
 */
struct menu {
	enum menu_type type;

	/* The next menu node at the same level */
	struct menu *next;

	/* The parent menu node, corresponding to e.g. a menu or choice */
	struct menu *parent;

	/* The first child menu node, for e.g. menus and choices */
	struct menu *list;

	/*
	 * The symbol associated with the menu node. Choices are implemented as
	 * a special kind of symbol. NULL for menus, comments, and ifs.
	 */
	struct symbol *sym;

	struct list_head link;	/* link to symbol::menus */

	struct list_head choice_members;

	/*
	 * The prompt associated with the node. This holds the prompt for a
	 * symbol as well as the text for a menu or comment, along with the
	 * type (P_PROMPT, P_MENU, etc.)
	 */
	struct property *prompt;

	/*
	 * 'visible if' dependencies. If more than one is given, they will be
	 * ANDed together.
	 */
	struct expr *visibility;

	/*
	 * Ordinary dependencies from e.g. 'depends on' and 'if', ANDed
	 * together
	 */
	struct expr *dep;

	/* MENU_* flags */
	unsigned int flags;

	/* Any help text associated with the node */
	char *help;

	/* The location where the menu node appears in the Kconfig files */
	const char *filename;
	int lineno;

	/* For use by front ends that need to store auxiliary data */
	void *data;
};

/*
 * Set on a menu node when the corresponding symbol changes state in some way.
 * Can be checked by front ends.
 */
#define MENU_CHANGED		0x0001

#define MENU_ROOT		0x0002

struct jump_key {
	struct list_head entries;
	size_t offset;
	struct menu *target;
};

extern struct symbol symbol_yes, symbol_no, symbol_mod;
extern struct symbol *modules_sym;
extern int cdebug;
struct expr *expr_alloc_symbol(struct symbol *sym);
struct expr *expr_alloc_one(enum expr_type type, struct expr *ce);
struct expr *expr_alloc_two(enum expr_type type, struct expr *e1, struct expr *e2);
struct expr *expr_alloc_comp(enum expr_type type, struct symbol *s1, struct symbol *s2);
struct expr *expr_alloc_and(struct expr *e1, struct expr *e2);
struct expr *expr_alloc_or(struct expr *e1, struct expr *e2);
void expr_eliminate_eq(struct expr **ep1, struct expr **ep2);
bool expr_eq(struct expr *e1, struct expr *e2);
tristate expr_calc_value(struct expr *e);
struct expr *expr_eliminate_dups(struct expr *e);
struct expr *expr_transform(struct expr *e);
bool expr_contains_symbol(struct expr *dep, struct symbol *sym);
bool expr_depends_symbol(struct expr *dep, struct symbol *sym);
struct expr *expr_trans_compare(struct expr *e, enum expr_type type, struct symbol *sym);

void expr_fprint(struct expr *e, FILE *out);
struct gstr; /* forward */
void expr_gstr_print(const struct expr *e, struct gstr *gs);
void expr_gstr_print_revdep(struct expr *e, struct gstr *gs,
			    tristate pr_type, const char *title);

static inline bool expr_is_yes(const struct expr *e)
{
	return !e || (e->type == E_SYMBOL && e->left.sym == &symbol_yes);
}

#ifdef __cplusplus
}
#endif

#endif /* EXPR_H */
