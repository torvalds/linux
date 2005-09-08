/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Released under the terms of the GNU GPL v2.0.
 */

#ifndef LKC_H
#define LKC_H

#include "expr.h"

#include <libintl.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef LKC_DIRECT_LINK
#define P(name,type,arg)	extern type name arg
#else
#include "lkc_defs.h"
#define P(name,type,arg)	extern type (*name ## _p) arg
#endif
#include "lkc_proto.h"
#undef P

#define SRCTREE "srctree"

#define PACKAGE "linux"
#define LOCALEDIR "/usr/share/locale"

#define _(text) gettext(text)
#define N_(text) (text)

int zconfparse(void);
void zconfdump(FILE *out);

extern int zconfdebug;
void zconf_starthelp(void);
FILE *zconf_fopen(const char *name);
void zconf_initscan(const char *name);
void zconf_nextfile(const char *name);
int zconf_lineno(void);
char *zconf_curname(void);

/* confdata.c */
extern const char conf_def_filename[];
extern char conf_filename[];

char *conf_get_default_confname(void);

/* kconfig_load.c */
void kconfig_load(void);

/* menu.c */
void menu_init(void);
void menu_add_menu(void);
void menu_end_menu(void);
void menu_add_entry(struct symbol *sym);
void menu_end_entry(void);
void menu_add_dep(struct expr *dep);
struct property *menu_add_prop(enum prop_type type, char *prompt, struct expr *expr, struct expr *dep);
struct property *menu_add_prompt(enum prop_type type, char *prompt, struct expr *dep);
void menu_add_expr(enum prop_type type, struct expr *expr, struct expr *dep);
void menu_add_symbol(enum prop_type type, struct symbol *sym, struct expr *dep);
void menu_finalize(struct menu *parent);
void menu_set_type(int type);

/* util.c */
struct file *file_lookup(const char *name);
int file_write_dep(const char *name);

struct gstr {
	size_t len;
	char  *s;
};
struct gstr str_new(void);
struct gstr str_assign(const char *s);
void str_free(struct gstr *gs);
void str_append(struct gstr *gs, const char *s);
void str_printf(struct gstr *gs, const char *fmt, ...);
const char *str_get(struct gstr *gs);

/* symbol.c */
void sym_init(void);
void sym_clear_all_valid(void);
void sym_set_changed(struct symbol *sym);
struct symbol *sym_check_deps(struct symbol *sym);
struct property *prop_alloc(enum prop_type type, struct symbol *sym);
struct symbol *prop_get_symbol(struct property *prop);

static inline tristate sym_get_tristate_value(struct symbol *sym)
{
	return sym->curr.tri;
}


static inline struct symbol *sym_get_choice_value(struct symbol *sym)
{
	return (struct symbol *)sym->curr.val;
}

static inline bool sym_set_choice_value(struct symbol *ch, struct symbol *chval)
{
	return sym_set_tristate_value(chval, yes);
}

static inline bool sym_is_choice(struct symbol *sym)
{
	return sym->flags & SYMBOL_CHOICE ? true : false;
}

static inline bool sym_is_choice_value(struct symbol *sym)
{
	return sym->flags & SYMBOL_CHOICEVAL ? true : false;
}

static inline bool sym_is_optional(struct symbol *sym)
{
	return sym->flags & SYMBOL_OPTIONAL ? true : false;
}

static inline bool sym_has_value(struct symbol *sym)
{
	return sym->flags & SYMBOL_NEW ? false : true;
}

#ifdef __cplusplus
}
#endif

#endif /* LKC_H */
