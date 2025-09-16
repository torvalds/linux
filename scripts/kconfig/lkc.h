/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 */

#ifndef LKC_H
#define LKC_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "expr.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "lkc_proto.h"

#define SRCTREE "srctree"

#ifndef CONFIG_
#define CONFIG_ "CONFIG_"
#endif
static inline const char *CONFIG_prefix(void)
{
	return getenv( "CONFIG_" ) ?: CONFIG_;
}
#undef CONFIG_
#define CONFIG_ CONFIG_prefix()

extern int yylineno;
void zconfdump(FILE *out);
void zconf_starthelp(void);
FILE *zconf_fopen(const char *name);
void zconf_initscan(const char *name);
void zconf_nextfile(const char *name);

/* confdata.c */
extern struct gstr autoconf_cmd;
const char *conf_get_configname(void);

/* confdata.c and expr.c */
static inline void xfwrite(const void *str, size_t len, size_t count, FILE *out)
{
	assert(len != 0);

	if (fwrite(str, len, count, out) != count)
		fprintf(stderr, "Error in writing or end of file.\n");
}

/* util.c */
const char *file_lookup(const char *name);

/* lexer.l */
int yylex(void);

struct gstr {
	size_t len;
	char  *s;
	/*
	* when max_width is not zero long lines in string s (if any) get
	* wrapped not to exceed the max_width value
	*/
	int max_width;
};
struct gstr str_new(void);
void str_free(struct gstr *gs);
void str_append(struct gstr *gs, const char *s);
void str_printf(struct gstr *gs, const char *fmt, ...);
char *str_get(const struct gstr *gs);

/* menu.c */
struct menu *menu_next(struct menu *menu, struct menu *root);
#define menu_for_each_sub_entry(menu, root) \
	for (menu = menu_next(root, root); menu; menu = menu_next(menu, root))
#define menu_for_each_entry(menu) \
	menu_for_each_sub_entry(menu, &rootmenu)
void _menu_init(void);
void menu_warn(const struct menu *menu, const char *fmt, ...);
struct menu *menu_add_menu(void);
void menu_end_menu(void);
void menu_add_entry(struct symbol *sym, enum menu_type type);
void menu_add_dep(struct expr *dep);
void menu_add_visibility(struct expr *dep);
struct property *menu_add_prompt(enum prop_type type, const char *prompt,
				 struct expr *dep);
void menu_add_expr(enum prop_type type, struct expr *expr, struct expr *dep);
void menu_add_symbol(enum prop_type type, struct symbol *sym, struct expr *dep);
void menu_finalize(void);
void menu_set_type(int type);

extern struct menu rootmenu;

bool menu_is_empty(struct menu *menu);
bool menu_is_visible(struct menu *menu);
bool menu_has_prompt(const struct menu *menu);
const char *menu_get_prompt(const struct menu *menu);
struct menu *menu_get_parent_menu(struct menu *menu);
struct menu *menu_get_menu_or_parent_menu(struct menu *menu);
int get_jump_key_char(void);
struct gstr get_relations_str(struct symbol **sym_arr, struct list_head *head);
void menu_get_ext_help(struct menu *menu, struct gstr *help);
void menu_dump(void);

/* symbol.c */
void sym_clear_all_valid(void);
struct symbol *sym_choice_default(struct menu *choice);
struct symbol *sym_calc_choice(struct menu *choice);
struct property *sym_get_range_prop(struct symbol *sym);
const char *sym_get_string_default(struct symbol *sym);
struct symbol *sym_check_deps(struct symbol *sym);
struct symbol *prop_get_symbol(const struct property *prop);

static inline tristate sym_get_tristate_value(const struct symbol *sym)
{
	return sym->curr.tri;
}

static inline bool sym_is_choice(const struct symbol *sym)
{
	/* A choice is a symbol with no name */
	return sym->name == NULL;
}

bool sym_is_choice_value(const struct symbol *sym);

static inline bool sym_has_value(const struct symbol *sym)
{
	return sym->flags & SYMBOL_DEF_USER ? true : false;
}

#ifdef __cplusplus
}
#endif

#endif /* LKC_H */
