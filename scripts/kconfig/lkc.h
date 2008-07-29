/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Released under the terms of the GNU GPL v2.0.
 */

#ifndef LKC_H
#define LKC_H

#include "expr.h"

#ifndef KBUILD_NO_NLS
# include <libintl.h>
#else
static inline const char *gettext(const char *txt) { return txt; }
static inline void textdomain(const char *domainname) {}
static inline void bindtextdomain(const char *name, const char *dir) {}
#endif

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


#define TF_COMMAND	0x0001
#define TF_PARAM	0x0002
#define TF_OPTION	0x0004

enum conf_def_mode {
	def_default,
	def_yes,
	def_mod,
	def_no,
	def_random
};

#define T_OPT_MODULES		1
#define T_OPT_DEFCONFIG_LIST	2
#define T_OPT_ENV		3

struct kconf_id {
	int name;
	int token;
	unsigned int flags;
	enum symbol_type stype;
};

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
const char *conf_get_configname(void);
char *conf_get_default_confname(void);
void sym_set_change_count(int count);
void sym_add_change_count(int count);
void conf_set_all_new_symbols(enum conf_def_mode mode);

/* kconfig_load.c */
void kconfig_load(void);

/* menu.c */
void menu_init(void);
void menu_warn(struct menu *menu, const char *fmt, ...);
struct menu *menu_add_menu(void);
void menu_end_menu(void);
void menu_add_entry(struct symbol *sym);
void menu_end_entry(void);
void menu_add_dep(struct expr *dep);
struct property *menu_add_prop(enum prop_type type, char *prompt, struct expr *expr, struct expr *dep);
struct property *menu_add_prompt(enum prop_type type, char *prompt, struct expr *dep);
void menu_add_expr(enum prop_type type, struct expr *expr, struct expr *dep);
void menu_add_symbol(enum prop_type type, struct symbol *sym, struct expr *dep);
void menu_add_option(int token, char *arg);
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
extern struct expr *sym_env_list;

void sym_init(void);
void sym_clear_all_valid(void);
void sym_set_all_changed(void);
void sym_set_changed(struct symbol *sym);
struct symbol *sym_check_deps(struct symbol *sym);
struct property *prop_alloc(enum prop_type type, struct symbol *sym);
struct symbol *prop_get_symbol(struct property *prop);
struct property *sym_get_env_prop(struct symbol *sym);

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
	return sym->flags & SYMBOL_DEF_USER ? true : false;
}

#ifdef __cplusplus
}
#endif

#endif /* LKC_H */
