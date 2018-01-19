/* SPDX-License-Identifier: GPL-2.0 */
#include <stdarg.h>

/* confdata.c */
void conf_parse(const char *name);
int conf_read(const char *name);
int conf_read_simple(const char *name, int);
int conf_write_defconfig(const char *name);
int conf_write(const char *name);
int conf_write_autoconf(void);
bool conf_get_changed(void);
void conf_set_changed_callback(void (*fn)(void));
void conf_set_message_callback(void (*fn)(const char *fmt, va_list ap));

/* menu.c */
extern struct menu rootmenu;

bool menu_is_empty(struct menu *menu);
bool menu_is_visible(struct menu *menu);
bool menu_has_prompt(struct menu *menu);
const char * menu_get_prompt(struct menu *menu);
struct menu * menu_get_root_menu(struct menu *menu);
struct menu * menu_get_parent_menu(struct menu *menu);
bool menu_has_help(struct menu *menu);
const char * menu_get_help(struct menu *menu);
struct gstr get_relations_str(struct symbol **sym_arr, struct list_head *head);
void menu_get_ext_help(struct menu *menu, struct gstr *help);

/* symbol.c */
extern struct symbol * symbol_hash[SYMBOL_HASHSIZE];

struct symbol * sym_lookup(const char *name, int flags);
struct symbol * sym_find(const char *name);
const char * sym_expand_string_value(const char *in);
const char * sym_escape_string_value(const char *in);
struct symbol ** sym_re_search(const char *pattern);
const char * sym_type_name(enum symbol_type type);
void sym_calc_value(struct symbol *sym);
enum symbol_type sym_get_type(struct symbol *sym);
bool sym_tristate_within_range(struct symbol *sym,tristate tri);
bool sym_set_tristate_value(struct symbol *sym,tristate tri);
tristate sym_toggle_tristate_value(struct symbol *sym);
bool sym_string_valid(struct symbol *sym, const char *newval);
bool sym_string_within_range(struct symbol *sym, const char *str);
bool sym_set_string_value(struct symbol *sym, const char *newval);
bool sym_is_changable(struct symbol *sym);
struct property * sym_get_choice_prop(struct symbol *sym);
const char * sym_get_string_value(struct symbol *sym);

const char * prop_get_type_name(enum prop_type type);

/* expr.c */
void expr_print(struct expr *e, void (*fn)(void *, struct symbol *, const char *), void *data, int prevtoken);
