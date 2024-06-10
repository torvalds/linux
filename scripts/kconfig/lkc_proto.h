/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LKC_PROTO_H
#define LKC_PROTO_H

#include <stdarg.h>

/* confdata.c */
void conf_parse(const char *name);
int conf_read(const char *name);
int conf_read_simple(const char *name, int);
int conf_write_defconfig(const char *name);
int conf_write(const char *name);
int conf_write_autoconf(int overwrite);
void conf_set_changed(bool val);
bool conf_get_changed(void);
void conf_set_changed_callback(void (*fn)(void));
void conf_set_message_callback(void (*fn)(const char *s));
bool conf_errors(void);

/* symbol.c */
struct symbol * sym_lookup(const char *name, int flags);
struct symbol * sym_find(const char *name);
void print_symbol_for_listconfig(struct symbol *sym);
struct symbol ** sym_re_search(const char *pattern);
const char * sym_type_name(enum symbol_type type);
void sym_calc_value(struct symbol *sym);
bool sym_dep_errors(void);
enum symbol_type sym_get_type(struct symbol *sym);
bool sym_tristate_within_range(struct symbol *sym,tristate tri);
bool sym_set_tristate_value(struct symbol *sym,tristate tri);
tristate sym_toggle_tristate_value(struct symbol *sym);
bool sym_string_valid(struct symbol *sym, const char *newval);
bool sym_string_within_range(struct symbol *sym, const char *str);
bool sym_set_string_value(struct symbol *sym, const char *newval);
bool sym_is_changeable(struct symbol *sym);
struct property * sym_get_choice_prop(struct symbol *sym);
struct menu *sym_get_choice_menu(struct symbol *sym);
const char * sym_get_string_value(struct symbol *sym);

const char * prop_get_type_name(enum prop_type type);

/* expr.c */
void expr_print(struct expr *e, void (*fn)(void *, struct symbol *, const char *), void *data, int prevtoken);

#endif /* LKC_PROTO_H */
