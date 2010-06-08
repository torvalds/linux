/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Released under the terms of the GNU GPL v2.0.
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <sys/utsname.h>

#define LKC_DIRECT_LINK
#include "lkc.h"

struct symbol symbol_yes = {
	.name = "y",
	.curr = { "y", yes },
	.flags = SYMBOL_CONST|SYMBOL_VALID,
}, symbol_mod = {
	.name = "m",
	.curr = { "m", mod },
	.flags = SYMBOL_CONST|SYMBOL_VALID,
}, symbol_no = {
	.name = "n",
	.curr = { "n", no },
	.flags = SYMBOL_CONST|SYMBOL_VALID,
}, symbol_empty = {
	.name = "",
	.curr = { "", no },
	.flags = SYMBOL_VALID,
};

struct symbol *sym_defconfig_list;
struct symbol *modules_sym;
tristate modules_val;

struct expr *sym_env_list;

static void sym_add_default(struct symbol *sym, const char *def)
{
	struct property *prop = prop_alloc(P_DEFAULT, sym);

	prop->expr = expr_alloc_symbol(sym_lookup(def, SYMBOL_CONST));
}

void sym_init(void)
{
	struct symbol *sym;
	struct utsname uts;
	static bool inited = false;

	if (inited)
		return;
	inited = true;

	uname(&uts);

	sym = sym_lookup("UNAME_RELEASE", 0);
	sym->type = S_STRING;
	sym->flags |= SYMBOL_AUTO;
	sym_add_default(sym, uts.release);
}

enum symbol_type sym_get_type(struct symbol *sym)
{
	enum symbol_type type = sym->type;

	if (type == S_TRISTATE) {
		if (sym_is_choice_value(sym) && sym->visible == yes)
			type = S_BOOLEAN;
		else if (modules_val == no)
			type = S_BOOLEAN;
	}
	return type;
}

const char *sym_type_name(enum symbol_type type)
{
	switch (type) {
	case S_BOOLEAN:
		return "boolean";
	case S_TRISTATE:
		return "tristate";
	case S_INT:
		return "integer";
	case S_HEX:
		return "hex";
	case S_STRING:
		return "string";
	case S_UNKNOWN:
		return "unknown";
	case S_OTHER:
		break;
	}
	return "???";
}

struct property *sym_get_choice_prop(struct symbol *sym)
{
	struct property *prop;

	for_all_choices(sym, prop)
		return prop;
	return NULL;
}

struct property *sym_get_env_prop(struct symbol *sym)
{
	struct property *prop;

	for_all_properties(sym, prop, P_ENV)
		return prop;
	return NULL;
}

struct property *sym_get_default_prop(struct symbol *sym)
{
	struct property *prop;

	for_all_defaults(sym, prop) {
		prop->visible.tri = expr_calc_value(prop->visible.expr);
		if (prop->visible.tri != no)
			return prop;
	}
	return NULL;
}

static struct property *sym_get_range_prop(struct symbol *sym)
{
	struct property *prop;

	for_all_properties(sym, prop, P_RANGE) {
		prop->visible.tri = expr_calc_value(prop->visible.expr);
		if (prop->visible.tri != no)
			return prop;
	}
	return NULL;
}

static int sym_get_range_val(struct symbol *sym, int base)
{
	sym_calc_value(sym);
	switch (sym->type) {
	case S_INT:
		base = 10;
		break;
	case S_HEX:
		base = 16;
		break;
	default:
		break;
	}
	return strtol(sym->curr.val, NULL, base);
}

static void sym_validate_range(struct symbol *sym)
{
	struct property *prop;
	int base, val, val2;
	char str[64];

	switch (sym->type) {
	case S_INT:
		base = 10;
		break;
	case S_HEX:
		base = 16;
		break;
	default:
		return;
	}
	prop = sym_get_range_prop(sym);
	if (!prop)
		return;
	val = strtol(sym->curr.val, NULL, base);
	val2 = sym_get_range_val(prop->expr->left.sym, base);
	if (val >= val2) {
		val2 = sym_get_range_val(prop->expr->right.sym, base);
		if (val <= val2)
			return;
	}
	if (sym->type == S_INT)
		sprintf(str, "%d", val2);
	else
		sprintf(str, "0x%x", val2);
	sym->curr.val = strdup(str);
}

static void sym_calc_visibility(struct symbol *sym)
{
	struct property *prop;
	tristate tri;

	/* any prompt visible? */
	tri = no;
	for_all_prompts(sym, prop) {
		prop->visible.tri = expr_calc_value(prop->visible.expr);
		tri = EXPR_OR(tri, prop->visible.tri);
	}
	if (tri == mod && (sym->type != S_TRISTATE || modules_val == no))
		tri = yes;
	if (sym->visible != tri) {
		sym->visible = tri;
		sym_set_changed(sym);
	}
	if (sym_is_choice_value(sym))
		return;
	/* defaulting to "yes" if no explicit "depends on" are given */
	tri = yes;
	if (sym->dir_dep.expr)
		tri = expr_calc_value(sym->dir_dep.expr);
	if (tri == mod)
		tri = yes;
	if (sym->dir_dep.tri != tri) {
		sym->dir_dep.tri = tri;
		sym_set_changed(sym);
	}
	tri = no;
	if (sym->rev_dep.expr)
		tri = expr_calc_value(sym->rev_dep.expr);
	if (tri == mod && sym_get_type(sym) == S_BOOLEAN)
		tri = yes;
	if (sym->rev_dep.tri != tri) {
		sym->rev_dep.tri = tri;
		sym_set_changed(sym);
	}
}

static struct symbol *sym_calc_choice(struct symbol *sym)
{
	struct symbol *def_sym;
	struct property *prop;
	struct expr *e;

	/* is the user choice visible? */
	def_sym = sym->def[S_DEF_USER].val;
	if (def_sym) {
		sym_calc_visibility(def_sym);
		if (def_sym->visible != no)
			return def_sym;
	}

	/* any of the defaults visible? */
	for_all_defaults(sym, prop) {
		prop->visible.tri = expr_calc_value(prop->visible.expr);
		if (prop->visible.tri == no)
			continue;
		def_sym = prop_get_symbol(prop);
		sym_calc_visibility(def_sym);
		if (def_sym->visible != no)
			return def_sym;
	}

	/* just get the first visible value */
	prop = sym_get_choice_prop(sym);
	expr_list_for_each_sym(prop->expr, e, def_sym) {
		sym_calc_visibility(def_sym);
		if (def_sym->visible != no)
			return def_sym;
	}

	/* no choice? reset tristate value */
	sym->curr.tri = no;
	return NULL;
}

void sym_calc_value(struct symbol *sym)
{
	struct symbol_value newval, oldval;
	struct property *prop;
	struct expr *e;

	if (!sym)
		return;

	if (sym->flags & SYMBOL_VALID)
		return;
	sym->flags |= SYMBOL_VALID;

	oldval = sym->curr;

	switch (sym->type) {
	case S_INT:
	case S_HEX:
	case S_STRING:
		newval = symbol_empty.curr;
		break;
	case S_BOOLEAN:
	case S_TRISTATE:
		newval = symbol_no.curr;
		break;
	default:
		sym->curr.val = sym->name;
		sym->curr.tri = no;
		return;
	}
	if (!sym_is_choice_value(sym))
		sym->flags &= ~SYMBOL_WRITE;

	sym_calc_visibility(sym);

	/* set default if recursively called */
	sym->curr = newval;

	switch (sym_get_type(sym)) {
	case S_BOOLEAN:
	case S_TRISTATE:
		if (sym_is_choice_value(sym) && sym->visible == yes) {
			prop = sym_get_choice_prop(sym);
			newval.tri = (prop_get_symbol(prop)->curr.val == sym) ? yes : no;
		} else {
			if (sym->visible != no) {
				/* if the symbol is visible use the user value
				 * if available, otherwise try the default value
				 */
				sym->flags |= SYMBOL_WRITE;
				if (sym_has_value(sym)) {
					newval.tri = EXPR_AND(sym->def[S_DEF_USER].tri,
							      sym->visible);
					goto calc_newval;
				}
			}
			if (sym->rev_dep.tri != no)
				sym->flags |= SYMBOL_WRITE;
			if (!sym_is_choice(sym)) {
				prop = sym_get_default_prop(sym);
				if (prop) {
					sym->flags |= SYMBOL_WRITE;
					newval.tri = EXPR_AND(expr_calc_value(prop->expr),
							      prop->visible.tri);
				}
			}
		calc_newval:
			if (sym->dir_dep.tri == no && sym->rev_dep.tri != no) {
				fprintf(stderr, "warning: (");
				expr_fprint(sym->rev_dep.expr, stderr);
				fprintf(stderr, ") selects %s which has unmet direct dependencies (",
					sym->name);
				expr_fprint(sym->dir_dep.expr, stderr);
				fprintf(stderr, ")\n");
			}
			newval.tri = EXPR_OR(newval.tri, sym->rev_dep.tri);
		}
		if (newval.tri == mod && sym_get_type(sym) == S_BOOLEAN)
			newval.tri = yes;
		break;
	case S_STRING:
	case S_HEX:
	case S_INT:
		if (sym->visible != no) {
			sym->flags |= SYMBOL_WRITE;
			if (sym_has_value(sym)) {
				newval.val = sym->def[S_DEF_USER].val;
				break;
			}
		}
		prop = sym_get_default_prop(sym);
		if (prop) {
			struct symbol *ds = prop_get_symbol(prop);
			if (ds) {
				sym->flags |= SYMBOL_WRITE;
				sym_calc_value(ds);
				newval.val = ds->curr.val;
			}
		}
		break;
	default:
		;
	}

	sym->curr = newval;
	if (sym_is_choice(sym) && newval.tri == yes)
		sym->curr.val = sym_calc_choice(sym);
	sym_validate_range(sym);

	if (memcmp(&oldval, &sym->curr, sizeof(oldval))) {
		sym_set_changed(sym);
		if (modules_sym == sym) {
			sym_set_all_changed();
			modules_val = modules_sym->curr.tri;
		}
	}

	if (sym_is_choice(sym)) {
		struct symbol *choice_sym;
		int flags = sym->flags & (SYMBOL_CHANGED | SYMBOL_WRITE);

		prop = sym_get_choice_prop(sym);
		expr_list_for_each_sym(prop->expr, e, choice_sym) {
			choice_sym->flags |= flags;
			if (flags & SYMBOL_CHANGED)
				sym_set_changed(choice_sym);
		}
	}

	if (sym->flags & SYMBOL_AUTO)
		sym->flags &= ~SYMBOL_WRITE;
}

void sym_clear_all_valid(void)
{
	struct symbol *sym;
	int i;

	for_all_symbols(i, sym)
		sym->flags &= ~SYMBOL_VALID;
	sym_add_change_count(1);
	if (modules_sym)
		sym_calc_value(modules_sym);
}

void sym_set_changed(struct symbol *sym)
{
	struct property *prop;

	sym->flags |= SYMBOL_CHANGED;
	for (prop = sym->prop; prop; prop = prop->next) {
		if (prop->menu)
			prop->menu->flags |= MENU_CHANGED;
	}
}

void sym_set_all_changed(void)
{
	struct symbol *sym;
	int i;

	for_all_symbols(i, sym)
		sym_set_changed(sym);
}

bool sym_tristate_within_range(struct symbol *sym, tristate val)
{
	int type = sym_get_type(sym);

	if (sym->visible == no)
		return false;

	if (type != S_BOOLEAN && type != S_TRISTATE)
		return false;

	if (type == S_BOOLEAN && val == mod)
		return false;
	if (sym->visible <= sym->rev_dep.tri)
		return false;
	if (sym_is_choice_value(sym) && sym->visible == yes)
		return val == yes;
	return val >= sym->rev_dep.tri && val <= sym->visible;
}

bool sym_set_tristate_value(struct symbol *sym, tristate val)
{
	tristate oldval = sym_get_tristate_value(sym);

	if (oldval != val && !sym_tristate_within_range(sym, val))
		return false;

	if (!(sym->flags & SYMBOL_DEF_USER)) {
		sym->flags |= SYMBOL_DEF_USER;
		sym_set_changed(sym);
	}
	/*
	 * setting a choice value also resets the new flag of the choice
	 * symbol and all other choice values.
	 */
	if (sym_is_choice_value(sym) && val == yes) {
		struct symbol *cs = prop_get_symbol(sym_get_choice_prop(sym));
		struct property *prop;
		struct expr *e;

		cs->def[S_DEF_USER].val = sym;
		cs->flags |= SYMBOL_DEF_USER;
		prop = sym_get_choice_prop(cs);
		for (e = prop->expr; e; e = e->left.expr) {
			if (e->right.sym->visible != no)
				e->right.sym->flags |= SYMBOL_DEF_USER;
		}
	}

	sym->def[S_DEF_USER].tri = val;
	if (oldval != val)
		sym_clear_all_valid();

	return true;
}

tristate sym_toggle_tristate_value(struct symbol *sym)
{
	tristate oldval, newval;

	oldval = newval = sym_get_tristate_value(sym);
	do {
		switch (newval) {
		case no:
			newval = mod;
			break;
		case mod:
			newval = yes;
			break;
		case yes:
			newval = no;
			break;
		}
		if (sym_set_tristate_value(sym, newval))
			break;
	} while (oldval != newval);
	return newval;
}

bool sym_string_valid(struct symbol *sym, const char *str)
{
	signed char ch;

	switch (sym->type) {
	case S_STRING:
		return true;
	case S_INT:
		ch = *str++;
		if (ch == '-')
			ch = *str++;
		if (!isdigit(ch))
			return false;
		if (ch == '0' && *str != 0)
			return false;
		while ((ch = *str++)) {
			if (!isdigit(ch))
				return false;
		}
		return true;
	case S_HEX:
		if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))
			str += 2;
		ch = *str++;
		do {
			if (!isxdigit(ch))
				return false;
		} while ((ch = *str++));
		return true;
	case S_BOOLEAN:
	case S_TRISTATE:
		switch (str[0]) {
		case 'y': case 'Y':
		case 'm': case 'M':
		case 'n': case 'N':
			return true;
		}
		return false;
	default:
		return false;
	}
}

bool sym_string_within_range(struct symbol *sym, const char *str)
{
	struct property *prop;
	int val;

	switch (sym->type) {
	case S_STRING:
		return sym_string_valid(sym, str);
	case S_INT:
		if (!sym_string_valid(sym, str))
			return false;
		prop = sym_get_range_prop(sym);
		if (!prop)
			return true;
		val = strtol(str, NULL, 10);
		return val >= sym_get_range_val(prop->expr->left.sym, 10) &&
		       val <= sym_get_range_val(prop->expr->right.sym, 10);
	case S_HEX:
		if (!sym_string_valid(sym, str))
			return false;
		prop = sym_get_range_prop(sym);
		if (!prop)
			return true;
		val = strtol(str, NULL, 16);
		return val >= sym_get_range_val(prop->expr->left.sym, 16) &&
		       val <= sym_get_range_val(prop->expr->right.sym, 16);
	case S_BOOLEAN:
	case S_TRISTATE:
		switch (str[0]) {
		case 'y': case 'Y':
			return sym_tristate_within_range(sym, yes);
		case 'm': case 'M':
			return sym_tristate_within_range(sym, mod);
		case 'n': case 'N':
			return sym_tristate_within_range(sym, no);
		}
		return false;
	default:
		return false;
	}
}

bool sym_set_string_value(struct symbol *sym, const char *newval)
{
	const char *oldval;
	char *val;
	int size;

	switch (sym->type) {
	case S_BOOLEAN:
	case S_TRISTATE:
		switch (newval[0]) {
		case 'y': case 'Y':
			return sym_set_tristate_value(sym, yes);
		case 'm': case 'M':
			return sym_set_tristate_value(sym, mod);
		case 'n': case 'N':
			return sym_set_tristate_value(sym, no);
		}
		return false;
	default:
		;
	}

	if (!sym_string_within_range(sym, newval))
		return false;

	if (!(sym->flags & SYMBOL_DEF_USER)) {
		sym->flags |= SYMBOL_DEF_USER;
		sym_set_changed(sym);
	}

	oldval = sym->def[S_DEF_USER].val;
	size = strlen(newval) + 1;
	if (sym->type == S_HEX && (newval[0] != '0' || (newval[1] != 'x' && newval[1] != 'X'))) {
		size += 2;
		sym->def[S_DEF_USER].val = val = malloc(size);
		*val++ = '0';
		*val++ = 'x';
	} else if (!oldval || strcmp(oldval, newval))
		sym->def[S_DEF_USER].val = val = malloc(size);
	else
		return true;

	strcpy(val, newval);
	free((void *)oldval);
	sym_clear_all_valid();

	return true;
}

const char *sym_get_string_value(struct symbol *sym)
{
	tristate val;

	switch (sym->type) {
	case S_BOOLEAN:
	case S_TRISTATE:
		val = sym_get_tristate_value(sym);
		switch (val) {
		case no:
			return "n";
		case mod:
			return "m";
		case yes:
			return "y";
		}
		break;
	default:
		;
	}
	return (const char *)sym->curr.val;
}

bool sym_is_changable(struct symbol *sym)
{
	return sym->visible > sym->rev_dep.tri;
}

static unsigned strhash(const char *s)
{
	/* fnv32 hash */
	unsigned hash = 2166136261U;
	for (; *s; s++)
		hash = (hash ^ *s) * 0x01000193;
	return hash;
}

struct symbol *sym_lookup(const char *name, int flags)
{
	struct symbol *symbol;
	char *new_name;
	int hash;

	if (name) {
		if (name[0] && !name[1]) {
			switch (name[0]) {
			case 'y': return &symbol_yes;
			case 'm': return &symbol_mod;
			case 'n': return &symbol_no;
			}
		}
		hash = strhash(name) % SYMBOL_HASHSIZE;

		for (symbol = symbol_hash[hash]; symbol; symbol = symbol->next) {
			if (symbol->name &&
			    !strcmp(symbol->name, name) &&
			    (flags ? symbol->flags & flags
				   : !(symbol->flags & (SYMBOL_CONST|SYMBOL_CHOICE))))
				return symbol;
		}
		new_name = strdup(name);
	} else {
		new_name = NULL;
		hash = 0;
	}

	symbol = malloc(sizeof(*symbol));
	memset(symbol, 0, sizeof(*symbol));
	symbol->name = new_name;
	symbol->type = S_UNKNOWN;
	symbol->flags |= flags;

	symbol->next = symbol_hash[hash];
	symbol_hash[hash] = symbol;

	return symbol;
}

struct symbol *sym_find(const char *name)
{
	struct symbol *symbol = NULL;
	int hash = 0;

	if (!name)
		return NULL;

	if (name[0] && !name[1]) {
		switch (name[0]) {
		case 'y': return &symbol_yes;
		case 'm': return &symbol_mod;
		case 'n': return &symbol_no;
		}
	}
	hash = strhash(name) % SYMBOL_HASHSIZE;

	for (symbol = symbol_hash[hash]; symbol; symbol = symbol->next) {
		if (symbol->name &&
		    !strcmp(symbol->name, name) &&
		    !(symbol->flags & SYMBOL_CONST))
				break;
	}

	return symbol;
}

struct symbol **sym_re_search(const char *pattern)
{
	struct symbol *sym, **sym_arr = NULL;
	int i, cnt, size;
	regex_t re;

	cnt = size = 0;
	/* Skip if empty */
	if (strlen(pattern) == 0)
		return NULL;
	if (regcomp(&re, pattern, REG_EXTENDED|REG_NOSUB|REG_ICASE))
		return NULL;

	for_all_symbols(i, sym) {
		if (sym->flags & SYMBOL_CONST || !sym->name)
			continue;
		if (regexec(&re, sym->name, 0, NULL, 0))
			continue;
		if (cnt + 1 >= size) {
			void *tmp = sym_arr;
			size += 16;
			sym_arr = realloc(sym_arr, size * sizeof(struct symbol *));
			if (!sym_arr) {
				free(tmp);
				return NULL;
			}
		}
		sym_calc_value(sym);
		sym_arr[cnt++] = sym;
	}
	if (sym_arr)
		sym_arr[cnt] = NULL;
	regfree(&re);

	return sym_arr;
}


static struct symbol *sym_check_expr_deps(struct expr *e)
{
	struct symbol *sym;

	if (!e)
		return NULL;
	switch (e->type) {
	case E_OR:
	case E_AND:
		sym = sym_check_expr_deps(e->left.expr);
		if (sym)
			return sym;
		return sym_check_expr_deps(e->right.expr);
	case E_NOT:
		return sym_check_expr_deps(e->left.expr);
	case E_EQUAL:
	case E_UNEQUAL:
		sym = sym_check_deps(e->left.sym);
		if (sym)
			return sym;
		return sym_check_deps(e->right.sym);
	case E_SYMBOL:
		return sym_check_deps(e->left.sym);
	default:
		break;
	}
	printf("Oops! How to check %d?\n", e->type);
	return NULL;
}

/* return NULL when dependencies are OK */
static struct symbol *sym_check_sym_deps(struct symbol *sym)
{
	struct symbol *sym2;
	struct property *prop;

	sym2 = sym_check_expr_deps(sym->rev_dep.expr);
	if (sym2)
		return sym2;

	for (prop = sym->prop; prop; prop = prop->next) {
		if (prop->type == P_CHOICE || prop->type == P_SELECT)
			continue;
		sym2 = sym_check_expr_deps(prop->visible.expr);
		if (sym2)
			break;
		if (prop->type != P_DEFAULT || sym_is_choice(sym))
			continue;
		sym2 = sym_check_expr_deps(prop->expr);
		if (sym2)
			break;
	}

	return sym2;
}

static struct symbol *sym_check_choice_deps(struct symbol *choice)
{
	struct symbol *sym, *sym2;
	struct property *prop;
	struct expr *e;

	prop = sym_get_choice_prop(choice);
	expr_list_for_each_sym(prop->expr, e, sym)
		sym->flags |= (SYMBOL_CHECK | SYMBOL_CHECKED);

	choice->flags |= (SYMBOL_CHECK | SYMBOL_CHECKED);
	sym2 = sym_check_sym_deps(choice);
	choice->flags &= ~SYMBOL_CHECK;
	if (sym2)
		goto out;

	expr_list_for_each_sym(prop->expr, e, sym) {
		sym2 = sym_check_sym_deps(sym);
		if (sym2) {
			fprintf(stderr, " -> %s", sym->name);
			break;
		}
	}
out:
	expr_list_for_each_sym(prop->expr, e, sym)
		sym->flags &= ~SYMBOL_CHECK;

	if (sym2 && sym_is_choice_value(sym2) &&
	    prop_get_symbol(sym_get_choice_prop(sym2)) == choice)
		sym2 = choice;

	return sym2;
}

struct symbol *sym_check_deps(struct symbol *sym)
{
	struct symbol *sym2;
	struct property *prop;

	if (sym->flags & SYMBOL_CHECK) {
		fprintf(stderr, "%s:%d:error: found recursive dependency: %s",
		        sym->prop->file->name, sym->prop->lineno,
			sym->name ? sym->name : "<choice>");
		return sym;
	}
	if (sym->flags & SYMBOL_CHECKED)
		return NULL;

	if (sym_is_choice_value(sym)) {
		/* for choice groups start the check with main choice symbol */
		prop = sym_get_choice_prop(sym);
		sym2 = sym_check_deps(prop_get_symbol(prop));
	} else if (sym_is_choice(sym)) {
		sym2 = sym_check_choice_deps(sym);
	} else {
		sym->flags |= (SYMBOL_CHECK | SYMBOL_CHECKED);
		sym2 = sym_check_sym_deps(sym);
		sym->flags &= ~SYMBOL_CHECK;
	}

	if (sym2) {
		fprintf(stderr, " -> %s", sym->name ? sym->name : "<choice>");
		if (sym2 == sym) {
			fprintf(stderr, "\n");
			zconfnerrs++;
			sym2 = NULL;
		}
	}

	return sym2;
}

struct property *prop_alloc(enum prop_type type, struct symbol *sym)
{
	struct property *prop;
	struct property **propp;

	prop = malloc(sizeof(*prop));
	memset(prop, 0, sizeof(*prop));
	prop->type = type;
	prop->sym = sym;
	prop->file = current_file;
	prop->lineno = zconf_lineno();

	/* append property to the prop list of symbol */
	if (sym) {
		for (propp = &sym->prop; *propp; propp = &(*propp)->next)
			;
		*propp = prop;
	}

	return prop;
}

struct symbol *prop_get_symbol(struct property *prop)
{
	if (prop->expr && (prop->expr->type == E_SYMBOL ||
			   prop->expr->type == E_LIST))
		return prop->expr->left.sym;
	return NULL;
}

const char *prop_get_type_name(enum prop_type type)
{
	switch (type) {
	case P_PROMPT:
		return "prompt";
	case P_ENV:
		return "env";
	case P_COMMENT:
		return "comment";
	case P_MENU:
		return "menu";
	case P_DEFAULT:
		return "default";
	case P_CHOICE:
		return "choice";
	case P_SELECT:
		return "select";
	case P_RANGE:
		return "range";
	case P_UNKNOWN:
		break;
	}
	return "unknown";
}

static void prop_add_env(const char *env)
{
	struct symbol *sym, *sym2;
	struct property *prop;
	char *p;

	sym = current_entry->sym;
	sym->flags |= SYMBOL_AUTO;
	for_all_properties(sym, prop, P_ENV) {
		sym2 = prop_get_symbol(prop);
		if (strcmp(sym2->name, env))
			menu_warn(current_entry, "redefining environment symbol from %s",
				  sym2->name);
		return;
	}

	prop = prop_alloc(P_ENV, sym);
	prop->expr = expr_alloc_symbol(sym_lookup(env, SYMBOL_CONST));

	sym_env_list = expr_alloc_one(E_LIST, sym_env_list);
	sym_env_list->right.sym = sym;

	p = getenv(env);
	if (p)
		sym_add_default(sym, p);
	else
		menu_warn(current_entry, "environment variable %s undefined", env);
}
