// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 */

#include <sys/types.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include <hash.h>
#include <xalloc.h>
#include "internal.h"
#include "lkc.h"

struct symbol symbol_yes = {
	.name = "y",
	.type = S_TRISTATE,
	.curr = { "y", yes },
	.menus = LIST_HEAD_INIT(symbol_yes.menus),
	.flags = SYMBOL_CONST|SYMBOL_VALID,
};

struct symbol symbol_mod = {
	.name = "m",
	.type = S_TRISTATE,
	.curr = { "m", mod },
	.menus = LIST_HEAD_INIT(symbol_mod.menus),
	.flags = SYMBOL_CONST|SYMBOL_VALID,
};

struct symbol symbol_no = {
	.name = "n",
	.type = S_TRISTATE,
	.curr = { "n", no },
	.menus = LIST_HEAD_INIT(symbol_no.menus),
	.flags = SYMBOL_CONST|SYMBOL_VALID,
};

struct symbol *modules_sym;
static tristate modules_val;
static int sym_warnings;

enum symbol_type sym_get_type(const struct symbol *sym)
{
	enum symbol_type type = sym->type;

	if (type == S_TRISTATE && modules_val == no)
		type = S_BOOLEAN;
	return type;
}

const char *sym_type_name(enum symbol_type type)
{
	switch (type) {
	case S_BOOLEAN:
		return "bool";
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
	}
	return "???";
}

/**
 * sym_get_prompt_menu - get the menu entry with a prompt
 *
 * @sym: a symbol pointer
 *
 * Return: the menu entry with a prompt.
 */
struct menu *sym_get_prompt_menu(const struct symbol *sym)
{
	struct menu *m;

	list_for_each_entry(m, &sym->menus, link)
		if (m->prompt)
			return m;

	return NULL;
}

/**
 * sym_get_choice_menu - get the parent choice menu if present
 *
 * @sym: a symbol pointer
 *
 * Return: a choice menu if this function is called against a choice member.
 */
struct menu *sym_get_choice_menu(const struct symbol *sym)
{
	struct menu *menu = NULL;

	/*
	 * Choice members must have a prompt. Find a menu entry with a prompt,
	 * and assume it resides inside a choice block.
	 */
	menu = sym_get_prompt_menu(sym);
	if (!menu)
		return NULL;

	do {
		menu = menu->parent;
	} while (menu && !menu->sym);

	if (menu && menu->sym && sym_is_choice(menu->sym))
		return menu;

	return NULL;
}

static struct property *sym_get_default_prop(struct symbol *sym)
{
	struct property *prop;

	for_all_defaults(sym, prop) {
		prop->visible.tri = expr_calc_value(prop->visible.expr);
		if (prop->visible.tri != no)
			return prop;
	}
	return NULL;
}

struct property *sym_get_range_prop(struct symbol *sym)
{
	struct property *prop;

	for_all_properties(sym, prop, P_RANGE) {
		prop->visible.tri = expr_calc_value(prop->visible.expr);
		if (prop->visible.tri != no)
			return prop;
	}
	return NULL;
}

static long long sym_get_range_val(struct symbol *sym, int base)
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
	return strtoll(sym->curr.val, NULL, base);
}

static void sym_validate_range(struct symbol *sym)
{
	struct property *prop;
	struct symbol *range_sym;
	int base;
	long long val, val2;

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
	val = strtoll(sym->curr.val, NULL, base);
	range_sym = prop->expr->left.sym;
	val2 = sym_get_range_val(range_sym, base);
	if (val >= val2) {
		range_sym = prop->expr->right.sym;
		val2 = sym_get_range_val(range_sym, base);
		if (val <= val2)
			return;
	}
	sym->curr.val = range_sym->curr.val;
}

static void sym_set_changed(struct symbol *sym)
{
	struct menu *menu;

	list_for_each_entry(menu, &sym->menus, link)
		menu->flags |= MENU_CHANGED;

	menu = sym_get_choice_menu(sym);
	if (menu)
		menu->flags |= MENU_CHANGED;
}

static void sym_set_all_changed(void)
{
	struct symbol *sym;

	for_all_symbols(sym)
		sym_set_changed(sym);
}

static void sym_calc_visibility(struct symbol *sym)
{
	struct property *prop;
	tristate tri;

	if (sym->flags & SYMBOL_TRANS) {
		sym->visible = yes;
		return;
	}

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
	if (tri == mod && sym_get_type(sym) == S_BOOLEAN)
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
	tri = no;
	if (sym->implied.expr)
		tri = expr_calc_value(sym->implied.expr);
	if (tri == mod && sym_get_type(sym) == S_BOOLEAN)
		tri = yes;
	if (sym->implied.tri != tri) {
		sym->implied.tri = tri;
		sym_set_changed(sym);
	}
}

/*
 * Find the default symbol for a choice.
 * First try the default values for the choice symbol
 * Next locate the first visible choice value
 * Return NULL if none was found
 */
struct symbol *sym_choice_default(struct menu *choice)
{
	struct menu *menu;
	struct symbol *def_sym;
	struct property *prop;

	/* any of the defaults visible? */
	for_all_defaults(choice->sym, prop) {
		prop->visible.tri = expr_calc_value(prop->visible.expr);
		if (prop->visible.tri == no)
			continue;
		def_sym = prop_get_symbol(prop);
		if (def_sym->visible != no)
			return def_sym;
	}

	/* just get the first visible value */
	menu_for_each_sub_entry(menu, choice)
		if (menu->sym && menu->sym->visible != no)
			return menu->sym;

	/* failed to locate any defaults */
	return NULL;
}

/*
 * sym_calc_choice - calculate symbol values in a choice
 *
 * @choice: a menu of the choice
 *
 * Return: a chosen symbol
 */
struct symbol *sym_calc_choice(struct menu *choice)
{
	struct symbol *res = NULL;
	struct symbol *sym;
	struct menu *menu;

	/* Traverse the list of choice members in the priority order. */
	list_for_each_entry(sym, &choice->choice_members, choice_link) {
		sym_calc_visibility(sym);
		if (sym->visible == no)
			continue;

		/* The first visible symble with the user value 'y'. */
		if (sym_has_value(sym) && sym->def[S_DEF_USER].tri == yes) {
			res = sym;
			break;
		}
	}

	/*
	 * If 'y' is not found in the user input, use the default, unless it is
	 * explicitly set to 'n'.
	 */
	if (!res) {
		res = sym_choice_default(choice);
		if (res && sym_has_value(res) && res->def[S_DEF_USER].tri == no)
			res = NULL;
	}

	/* Still not found. Pick up the first visible, user-unspecified symbol. */
	if (!res) {
		menu_for_each_sub_entry(menu, choice) {
			sym = menu->sym;

			if (!sym || sym->visible == no || sym_has_value(sym))
				continue;

			res = sym;
			break;
		}
	}

	/*
	 * Still not found. Traverse the linked list in the _reverse_ order to
	 * pick up the least prioritized 'n'.
	 */
	if (!res) {
		list_for_each_entry_reverse(sym, &choice->choice_members,
					    choice_link) {
			if (sym->visible == no)
				continue;

			res = sym;
			break;
		}
	}

	menu_for_each_sub_entry(menu, choice) {
		tristate val;

		sym = menu->sym;

		if (!sym || sym->visible == no)
			continue;

		val = sym == res ? yes : no;

		if (sym->curr.tri != val)
			sym_set_changed(sym);

		sym->curr.tri = val;
		sym->flags |= SYMBOL_VALID | SYMBOL_WRITE;
	}

	return res;
}

static void sym_warn_unmet_dep(const struct symbol *sym)
{
	struct gstr gs = str_new();

	str_printf(&gs,
		   "\nWARNING: unmet direct dependencies detected for %s\n",
		   sym->name);
	str_printf(&gs,
		   "  Depends on [%c]: ",
		   sym->dir_dep.tri == mod ? 'm' : 'n');
	expr_gstr_print(sym->dir_dep.expr, &gs);
	str_printf(&gs, "\n");

	expr_gstr_print_revdep(sym->rev_dep.expr, &gs, yes,
			       "  Selected by [y]:\n");
	expr_gstr_print_revdep(sym->rev_dep.expr, &gs, mod,
			       "  Selected by [m]:\n");

	fputs(str_get(&gs), stderr);
	str_free(&gs);
	sym_warnings++;
}

bool sym_dep_errors(void)
{
	if (sym_warnings)
		return getenv("KCONFIG_WERROR");
	return false;
}

void sym_calc_value(struct symbol *sym)
{
	struct symbol_value newval, oldval;
	struct property *prop = NULL;
	struct menu *choice_menu;

	if (!sym)
		return;

	if (sym->flags & SYMBOL_VALID)
		return;

	sym->flags |= SYMBOL_VALID;

	oldval = sym->curr;

	newval.tri = no;

	switch (sym->type) {
	case S_INT:
		newval.val = "0";
		break;
	case S_HEX:
		newval.val = "0x0";
		break;
	case S_STRING:
		newval.val = "";
		break;
	case S_BOOLEAN:
	case S_TRISTATE:
		newval.val = "n";
		break;
	default:
		sym->curr.val = sym->name;
		sym->curr.tri = no;
		return;
	}
	sym->flags &= ~SYMBOL_WRITE;

	sym_calc_visibility(sym);

	if (sym->visible != no)
		sym->flags |= SYMBOL_WRITE;

	/* set default if recursively called */
	sym->curr = newval;

	switch (sym_get_type(sym)) {
	case S_BOOLEAN:
	case S_TRISTATE:
		choice_menu = sym_get_choice_menu(sym);

		if (choice_menu) {
			sym_calc_choice(choice_menu);
			newval.tri = sym->curr.tri;
		} else {
			if (sym->visible != no) {
				/* if the symbol is visible use the user value
				 * if available, otherwise try the default value
				 */
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
					newval.tri = EXPR_AND(expr_calc_value(prop->expr),
							      prop->visible.tri);
					if (newval.tri != no)
						sym->flags |= SYMBOL_WRITE;
				}
				if (sym->implied.tri != no) {
					sym->flags |= SYMBOL_WRITE;
					newval.tri = EXPR_OR(newval.tri, sym->implied.tri);
					newval.tri = EXPR_AND(newval.tri,
							      sym->dir_dep.tri);
				}
			}
		calc_newval:
			if (sym->dir_dep.tri < sym->rev_dep.tri)
				sym_warn_unmet_dep(sym);
			newval.tri = EXPR_OR(newval.tri, sym->rev_dep.tri);
		}
		if (newval.tri == mod && sym_get_type(sym) == S_BOOLEAN)
			newval.tri = yes;
		break;
	case S_STRING:
	case S_HEX:
	case S_INT:
		if (sym->visible != no && sym_has_value(sym)) {
			newval.val = sym->def[S_DEF_USER].val;
			break;
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

	/*
	 * If the symbol lacks a user value but its value comes from a
	 * single transitional symbol with an existing user value, mark
	 * this symbol as having a user value to avoid prompting.
	 */
	if (prop && !sym_has_value(sym)) {
		struct symbol *ds = prop_get_symbol(prop);
		if (ds && (ds->flags & SYMBOL_TRANS) && sym_has_value(ds)) {
			sym->def[S_DEF_USER] = newval;
			sym->flags |= SYMBOL_DEF_USER;
		}
	}

	sym->curr = newval;
	sym_validate_range(sym);

	if (memcmp(&oldval, &sym->curr, sizeof(oldval))) {
		sym_set_changed(sym);
		if (modules_sym == sym) {
			sym_set_all_changed();
			modules_val = modules_sym->curr.tri;
		}
	}

	if (sym_is_choice(sym) || sym->flags & SYMBOL_TRANS)
		sym->flags &= ~SYMBOL_WRITE;
}

void sym_clear_all_valid(void)
{
	struct symbol *sym;

	for_all_symbols(sym)
		sym->flags &= ~SYMBOL_VALID;
	expr_invalidate_all();
	conf_set_changed(true);
	sym_calc_value(modules_sym);
}

bool sym_tristate_within_range(const struct symbol *sym, tristate val)
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
	return val >= sym->rev_dep.tri && val <= sym->visible;
}

bool sym_set_tristate_value(struct symbol *sym, tristate val)
{
	tristate oldval = sym_get_tristate_value(sym);

	if (!sym_tristate_within_range(sym, val))
		return false;

	if (!(sym->flags & SYMBOL_DEF_USER) || sym->def[S_DEF_USER].tri != val) {
		sym->def[S_DEF_USER].tri = val;
		sym->flags |= SYMBOL_DEF_USER;
		sym_set_changed(sym);
	}

	if (oldval != val)
		sym_clear_all_valid();

	return true;
}

/**
 * choice_set_value - set the user input to a choice
 *
 * @choice: menu entry for the choice
 * @sym: selected symbol
 */
void choice_set_value(struct menu *choice, struct symbol *sym)
{
	struct menu *menu;
	bool changed = false;

	menu_for_each_sub_entry(menu, choice) {
		tristate val;

		if (!menu->sym)
			continue;

		if (menu->sym->visible == no)
			continue;

		val = menu->sym == sym ? yes : no;

		if (menu->sym->curr.tri != val)
			changed = true;

		menu->sym->def[S_DEF_USER].tri = val;
		menu->sym->flags |= SYMBOL_DEF_USER;

		/*
		 * Now, the user has explicitly enabled or disabled this symbol,
		 * it should be given the highest priority. We are possibly
		 * setting multiple symbols to 'n', where the first symbol is
		 * given the least prioritized 'n'. This works well when the
		 * choice block ends up with selecting 'n' symbol.
		 * (see sym_calc_choice())
		 */
		list_move(&menu->sym->choice_link, &choice->choice_members);
	}

	if (changed)
		sym_clear_all_valid();
}

tristate sym_toggle_tristate_value(struct symbol *sym)
{
	struct menu *choice;
	tristate oldval, newval;

	choice = sym_get_choice_menu(sym);
	if (choice) {
		choice_set_value(choice, sym);
		return yes;
	}

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
	long long val;

	switch (sym->type) {
	case S_STRING:
		return sym_string_valid(sym, str);
	case S_INT:
		if (!sym_string_valid(sym, str))
			return false;
		prop = sym_get_range_prop(sym);
		if (!prop)
			return true;
		val = strtoll(str, NULL, 10);
		return val >= sym_get_range_val(prop->expr->left.sym, 10) &&
		       val <= sym_get_range_val(prop->expr->right.sym, 10);
	case S_HEX:
		if (!sym_string_valid(sym, str))
			return false;
		prop = sym_get_range_prop(sym);
		if (!prop)
			return true;
		val = strtoll(str, NULL, 16);
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
		sym->def[S_DEF_USER].val = val = xmalloc(size);
		*val++ = '0';
		*val++ = 'x';
	} else if (!oldval || strcmp(oldval, newval))
		sym->def[S_DEF_USER].val = val = xmalloc(size);
	else
		return true;

	strcpy(val, newval);
	free((void *)oldval);
	sym_clear_all_valid();

	return true;
}

/*
 * Find the default value associated to a symbol.
 * For tristate symbol handle the modules=n case
 * in which case "m" becomes "y".
 * If the symbol does not have any default then fallback
 * to the fixed default values.
 */
const char *sym_get_string_default(struct symbol *sym)
{
	struct property *prop;
	struct symbol *ds;
	const char *str = "";
	tristate val;

	sym_calc_visibility(sym);
	sym_calc_value(modules_sym);
	val = symbol_no.curr.tri;

	/* If symbol has a default value look it up */
	prop = sym_get_default_prop(sym);
	if (prop != NULL) {
		switch (sym->type) {
		case S_BOOLEAN:
		case S_TRISTATE:
			/* The visibility may limit the value from yes => mod */
			val = EXPR_AND(expr_calc_value(prop->expr), prop->visible.tri);
			break;
		default:
			/*
			 * The following fails to handle the situation
			 * where a default value is further limited by
			 * the valid range.
			 */
			ds = prop_get_symbol(prop);
			if (ds != NULL) {
				sym_calc_value(ds);
				str = (const char *)ds->curr.val;
			}
		}
	}

	/* Handle select statements */
	val = EXPR_OR(val, sym->rev_dep.tri);

	/* transpose mod to yes if modules are not enabled */
	if (val == mod)
		if (!sym_is_choice_value(sym) && modules_sym->curr.tri == no)
			val = yes;

	/* transpose mod to yes if type is bool */
	if (sym->type == S_BOOLEAN && val == mod)
		val = yes;

	/* adjust the default value if this symbol is implied by another */
	if (val < sym->implied.tri)
		val = sym->implied.tri;

	switch (sym->type) {
	case S_BOOLEAN:
	case S_TRISTATE:
		switch (val) {
		case no: return "n";
		case mod: return "m";
		case yes: return "y";
		}
	case S_INT:
		if (!str[0])
			str = "0";
		break;
	case S_HEX:
		if (!str[0])
			str = "0x0";
		break;
	default:
		break;
	}
	return str;
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
	return sym->curr.val;
}

bool sym_is_changeable(const struct symbol *sym)
{
	return !sym_is_choice(sym) && sym->visible > sym->rev_dep.tri;
}

bool sym_is_choice_value(const struct symbol *sym)
{
	return !list_empty(&sym->choice_link);
}

HASHTABLE_DEFINE(sym_hashtable, SYMBOL_HASHSIZE);

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
		hash = hash_str(name);

		hash_for_each_possible(sym_hashtable, symbol, node, hash) {
			if (symbol->name &&
			    !strcmp(symbol->name, name) &&
			    (flags ? symbol->flags & flags
				   : !(symbol->flags & SYMBOL_CONST)))
				return symbol;
		}
		new_name = xstrdup(name);
	} else {
		new_name = NULL;
		hash = 0;
	}

	symbol = xmalloc(sizeof(*symbol));
	memset(symbol, 0, sizeof(*symbol));
	symbol->name = new_name;
	symbol->type = S_UNKNOWN;
	symbol->flags = flags;
	INIT_LIST_HEAD(&symbol->menus);
	INIT_LIST_HEAD(&symbol->choice_link);

	hash_add(sym_hashtable, &symbol->node, hash);

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
	hash = hash_str(name);

	hash_for_each_possible(sym_hashtable, symbol, node, hash) {
		if (symbol->name &&
		    !strcmp(symbol->name, name) &&
		    !(symbol->flags & SYMBOL_CONST))
				break;
	}

	return symbol;
}

struct sym_match {
	struct symbol	*sym;
	off_t		so, eo;
};

/* Compare matched symbols as thus:
 * - first, symbols that match exactly
 * - then, alphabetical sort
 */
static int sym_rel_comp(const void *sym1, const void *sym2)
{
	const struct sym_match *s1 = sym1;
	const struct sym_match *s2 = sym2;
	int exact1, exact2;

	/* Exact match:
	 * - if matched length on symbol s1 is the length of that symbol,
	 *   then this symbol should come first;
	 * - if matched length on symbol s2 is the length of that symbol,
	 *   then this symbol should come first.
	 * Note: since the search can be a regexp, both symbols may match
	 * exactly; if this is the case, we can't decide which comes first,
	 * and we fallback to sorting alphabetically.
	 */
	exact1 = (s1->eo - s1->so) == strlen(s1->sym->name);
	exact2 = (s2->eo - s2->so) == strlen(s2->sym->name);
	if (exact1 && !exact2)
		return -1;
	if (!exact1 && exact2)
		return 1;

	/* As a fallback, sort symbols alphabetically */
	return strcmp(s1->sym->name, s2->sym->name);
}

struct symbol **sym_re_search(const char *pattern)
{
	struct symbol *sym, **sym_arr = NULL;
	struct sym_match *sym_match_arr = NULL;
	int i, cnt, size;
	regex_t re;
	regmatch_t match[1];

	cnt = size = 0;
	/* Skip if empty */
	if (strlen(pattern) == 0)
		return NULL;
	if (regcomp(&re, pattern, REG_EXTENDED|REG_ICASE))
		return NULL;

	for_all_symbols(sym) {
		if (sym->flags & SYMBOL_CONST || !sym->name)
			continue;
		if (regexec(&re, sym->name, 1, match, 0))
			continue;
		if (cnt >= size) {
			void *tmp;
			size += 16;
			tmp = realloc(sym_match_arr, size * sizeof(struct sym_match));
			if (!tmp)
				goto sym_re_search_free;
			sym_match_arr = tmp;
		}
		sym_calc_value(sym);
		/* As regexec returned 0, we know we have a match, so
		 * we can use match[0].rm_[se]o without further checks
		 */
		sym_match_arr[cnt].so = match[0].rm_so;
		sym_match_arr[cnt].eo = match[0].rm_eo;
		sym_match_arr[cnt++].sym = sym;
	}
	if (sym_match_arr) {
		qsort(sym_match_arr, cnt, sizeof(struct sym_match), sym_rel_comp);
		sym_arr = malloc((cnt+1) * sizeof(struct symbol *));
		if (!sym_arr)
			goto sym_re_search_free;
		for (i = 0; i < cnt; i++)
			sym_arr[i] = sym_match_arr[i].sym;
		sym_arr[cnt] = NULL;
	}
sym_re_search_free:
	/* sym_match_arr can be NULL if no match, but free(NULL) is OK */
	free(sym_match_arr);
	regfree(&re);

	return sym_arr;
}

/*
 * When we check for recursive dependencies we use a stack to save
 * current state so we can print out relevant info to user.
 * The entries are located on the call stack so no need to free memory.
 * Note insert() remove() must always match to properly clear the stack.
 */
static struct dep_stack {
	struct dep_stack *prev, *next;
	struct symbol *sym;
	struct property *prop;
	struct expr **expr;
} *check_top;

static void dep_stack_insert(struct dep_stack *stack, struct symbol *sym)
{
	memset(stack, 0, sizeof(*stack));
	if (check_top)
		check_top->next = stack;
	stack->prev = check_top;
	stack->sym = sym;
	check_top = stack;
}

static void dep_stack_remove(void)
{
	check_top = check_top->prev;
	if (check_top)
		check_top->next = NULL;
}

/*
 * Called when we have detected a recursive dependency.
 * check_top point to the top of the stact so we use
 * the ->prev pointer to locate the bottom of the stack.
 */
static void sym_check_print_recursive(struct symbol *last_sym)
{
	struct dep_stack *stack;
	struct symbol *sym, *next_sym;
	struct menu *choice;
	struct dep_stack cv_stack;
	enum prop_type type;

	choice = sym_get_choice_menu(last_sym);
	if (choice) {
		dep_stack_insert(&cv_stack, last_sym);
		last_sym = choice->sym;
	}

	for (stack = check_top; stack != NULL; stack = stack->prev)
		if (stack->sym == last_sym)
			break;
	if (!stack) {
		fprintf(stderr, "unexpected recursive dependency error\n");
		return;
	}

	for (; stack; stack = stack->next) {
		sym = stack->sym;
		next_sym = stack->next ? stack->next->sym : last_sym;
		type = stack->prop ? stack->prop->type : P_UNKNOWN;

		if (stack->sym == last_sym)
			fprintf(stderr, "error: recursive dependency detected!\n");

		if (sym_is_choice(next_sym)) {
			choice = list_first_entry(&next_sym->menus, struct menu, link);

			fprintf(stderr, "\tsymbol %s is part of choice block at %s:%d\n",
				sym->name ? sym->name : "<choice>",
				choice->filename, choice->lineno);
		} else if (stack->expr == &sym->dir_dep.expr) {
			fprintf(stderr, "\tsymbol %s depends on %s\n",
				sym->name ? sym->name : "<choice>",
				next_sym->name);
		} else if (stack->expr == &sym->rev_dep.expr) {
			fprintf(stderr, "\tsymbol %s is selected by %s\n",
				sym->name, next_sym->name);
		} else if (stack->expr == &sym->implied.expr) {
			fprintf(stderr, "\tsymbol %s is implied by %s\n",
				sym->name, next_sym->name);
		} else if (stack->expr) {
			fprintf(stderr, "\tsymbol %s %s value contains %s\n",
				sym->name ? sym->name : "<choice>",
				prop_get_type_name(type),
				next_sym->name);
		} else {
			fprintf(stderr, "\tsymbol %s %s is visible depending on %s\n",
				sym->name ? sym->name : "<choice>",
				prop_get_type_name(type),
				next_sym->name);
		}
	}

	fprintf(stderr,
		"For a resolution refer to Documentation/kbuild/kconfig-language.rst\n"
		"subsection \"Kconfig recursive dependency limitations\"\n"
		"\n");

	if (check_top == &cv_stack)
		dep_stack_remove();
}

static struct symbol *sym_check_expr_deps(const struct expr *e)
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
	case E_GEQ:
	case E_GTH:
	case E_LEQ:
	case E_LTH:
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
	fprintf(stderr, "Oops! How to check %d?\n", e->type);
	return NULL;
}

/* return NULL when dependencies are OK */
static struct symbol *sym_check_sym_deps(struct symbol *sym)
{
	struct symbol *sym2;
	struct property *prop;
	struct dep_stack stack;

	dep_stack_insert(&stack, sym);

	stack.expr = &sym->dir_dep.expr;
	sym2 = sym_check_expr_deps(sym->dir_dep.expr);
	if (sym2)
		goto out;

	stack.expr = &sym->rev_dep.expr;
	sym2 = sym_check_expr_deps(sym->rev_dep.expr);
	if (sym2)
		goto out;

	stack.expr = &sym->implied.expr;
	sym2 = sym_check_expr_deps(sym->implied.expr);
	if (sym2)
		goto out;

	stack.expr = NULL;

	for (prop = sym->prop; prop; prop = prop->next) {
		if (prop->type == P_SELECT || prop->type == P_IMPLY)
			continue;
		stack.prop = prop;
		sym2 = sym_check_expr_deps(prop->visible.expr);
		if (sym2)
			break;
		if (prop->type != P_DEFAULT || sym_is_choice(sym))
			continue;
		stack.expr = &prop->expr;
		sym2 = sym_check_expr_deps(prop->expr);
		if (sym2)
			break;
		stack.expr = NULL;
	}

out:
	dep_stack_remove();

	return sym2;
}

static struct symbol *sym_check_choice_deps(struct symbol *choice)
{
	struct menu *choice_menu, *menu;
	struct symbol *sym2;
	struct dep_stack stack;

	dep_stack_insert(&stack, choice);

	choice_menu = list_first_entry(&choice->menus, struct menu, link);

	menu_for_each_sub_entry(menu, choice_menu) {
		if (menu->sym)
			menu->sym->flags |= SYMBOL_CHECK | SYMBOL_CHECKED;
	}

	choice->flags |= (SYMBOL_CHECK | SYMBOL_CHECKED);
	sym2 = sym_check_sym_deps(choice);
	choice->flags &= ~SYMBOL_CHECK;
	if (sym2)
		goto out;

	menu_for_each_sub_entry(menu, choice_menu) {
		if (!menu->sym)
			continue;
		sym2 = sym_check_sym_deps(menu->sym);
		if (sym2)
			break;
	}
out:
	menu_for_each_sub_entry(menu, choice_menu)
		if (menu->sym)
			menu->sym->flags &= ~SYMBOL_CHECK;

	if (sym2) {
		struct menu *choice_menu2;

		choice_menu2 = sym_get_choice_menu(sym2);
		if (choice_menu2 == choice_menu)
			sym2 = choice;
	}

	dep_stack_remove();

	return sym2;
}

struct symbol *sym_check_deps(struct symbol *sym)
{
	struct menu *choice;
	struct symbol *sym2;

	if (sym->flags & SYMBOL_CHECK) {
		sym_check_print_recursive(sym);
		return sym;
	}
	if (sym->flags & SYMBOL_CHECKED)
		return NULL;

	choice = sym_get_choice_menu(sym);
	if (choice) {
		struct dep_stack stack;

		/* for choice groups start the check with main choice symbol */
		dep_stack_insert(&stack, sym);
		sym2 = sym_check_deps(choice->sym);
		dep_stack_remove();
	} else if (sym_is_choice(sym)) {
		sym2 = sym_check_choice_deps(sym);
	} else {
		sym->flags |= (SYMBOL_CHECK | SYMBOL_CHECKED);
		sym2 = sym_check_sym_deps(sym);
		sym->flags &= ~SYMBOL_CHECK;
	}

	return sym2;
}

struct symbol *prop_get_symbol(const struct property *prop)
{
	if (prop->expr && prop->expr->type == E_SYMBOL)
		return prop->expr->left.sym;
	return NULL;
}

const char *prop_get_type_name(enum prop_type type)
{
	switch (type) {
	case P_PROMPT:
		return "prompt";
	case P_COMMENT:
		return "comment";
	case P_MENU:
		return "menu";
	case P_DEFAULT:
		return "default";
	case P_SELECT:
		return "select";
	case P_IMPLY:
		return "imply";
	case P_RANGE:
		return "range";
	case P_UNKNOWN:
		break;
	}
	return "unknown";
}
