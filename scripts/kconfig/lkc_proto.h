/* SPDX-License-Identifier: GPL-2.0 */
#include <stdarg.h>

/* confdata.c */
void conf_parse(const char *name);
int conf_read(const char *name);
int conf_read_simple(const char *name, int);
int conf_write_defconfig(const char *name);
int conf_write(const char *name);
int conf_write_autoconf(int overwrite);
bool conf_get_changed(void);
void conf_set_changed_callback(void (*fn)(void));
void conf_set_message_callback(void (*fn)(const char *s));

/* symbol.c */
extern struct symbol * symbol_hash[SYMBOL_HASHSIZE];

struct symbol * sym_lookup(const char *name, int flags);
struct symbol * sym_find(const char *name);
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
bool sym_is_changeable(struct symbol *sym);
struct property * sym_get_choice_prop(struct symbol *sym);
const char * sym_get_string_value(struct symbol *sym);

const char * prop_get_type_name(enum prop_type type);

/* preprocess.c */
enum variable_flavor {
	VAR_SIMPLE,
	VAR_RECURSIVE,
	VAR_APPEND,
};
void env_write_dep(FILE *f, const char *auto_conf_name);
void variable_add(const char *name, const char *value,
		  enum variable_flavor flavor);
void variable_all_del(void);
char *expand_dollar(const char **str);
char *expand_one_token(const char **str);

/* expr.c */
void expr_print(struct expr *e, void (*fn)(void *, struct symbol *, const char *), void *data, int prevtoken);
