// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <list.h>
#include "lkc.h"
#include "internal.h"

static const char nohelp_text[] = "There is no help available for this option.";

struct menu rootmenu;
static struct menu **last_entry_ptr;

/**
 * menu_next - return the next menu entry with depth-first traversal
 * @menu: pointer to the current menu
 * @root: root of the sub-tree to traverse. If NULL is given, the traveral
 *        continues until it reaches the end of the entire menu tree.
 * return: the menu to visit next, or NULL when it reaches the end.
 */
struct menu *menu_next(struct menu *menu, struct menu *root)
{
	if (menu->list)
		return menu->list;

	while (menu != root && !menu->next)
		menu = menu->parent;

	if (menu == root)
		return NULL;

	return menu->next;
}

void menu_warn(const struct menu *menu, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "%s:%d:warning: ", menu->filename, menu->lineno);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

static void prop_warn(const struct property *prop, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "%s:%d:warning: ", prop->filename, prop->lineno);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

void _menu_init(void)
{
	current_entry = current_menu = &rootmenu;
	last_entry_ptr = &rootmenu.list;
}

void menu_add_entry(struct symbol *sym)
{
	struct menu *menu;

	menu = xmalloc(sizeof(*menu));
	memset(menu, 0, sizeof(*menu));
	menu->sym = sym;
	menu->parent = current_menu;
	menu->filename = cur_filename;
	menu->lineno = cur_lineno;

	*last_entry_ptr = menu;
	last_entry_ptr = &menu->next;
	current_entry = menu;
	if (sym) {
		menu_add_symbol(P_SYMBOL, sym, NULL);
		list_add_tail(&menu->link, &sym->menus);
	}
}

struct menu *menu_add_menu(void)
{
	last_entry_ptr = &current_entry->list;
	current_menu = current_entry;
	return current_menu;
}

void menu_end_menu(void)
{
	last_entry_ptr = &current_menu->next;
	current_menu = current_menu->parent;
}

/*
 * Rewrites 'm' to 'm' && MODULES, so that it evaluates to 'n' when running
 * without modules
 */
static struct expr *rewrite_m(struct expr *e)
{
	if (!e)
		return e;

	switch (e->type) {
	case E_NOT:
		e->left.expr = rewrite_m(e->left.expr);
		break;
	case E_OR:
	case E_AND:
		e->left.expr = rewrite_m(e->left.expr);
		e->right.expr = rewrite_m(e->right.expr);
		break;
	case E_SYMBOL:
		/* change 'm' into 'm' && MODULES */
		if (e->left.sym == &symbol_mod)
			return expr_alloc_and(e, expr_alloc_symbol(modules_sym));
		break;
	default:
		break;
	}
	return e;
}

void menu_add_dep(struct expr *dep)
{
	current_entry->dep = expr_alloc_and(current_entry->dep, dep);
}

void menu_set_type(int type)
{
	struct symbol *sym = current_entry->sym;

	if (sym->type == type)
		return;
	if (sym->type == S_UNKNOWN) {
		sym->type = type;
		return;
	}
	menu_warn(current_entry,
		"ignoring type redefinition of '%s' from '%s' to '%s'",
		sym->name ? sym->name : "<choice>",
		sym_type_name(sym->type), sym_type_name(type));
}

static struct property *menu_add_prop(enum prop_type type, struct expr *expr,
				      struct expr *dep)
{
	struct property *prop;

	prop = xmalloc(sizeof(*prop));
	memset(prop, 0, sizeof(*prop));
	prop->type = type;
	prop->filename = cur_filename;
	prop->lineno = cur_lineno;
	prop->menu = current_entry;
	prop->expr = expr;
	prop->visible.expr = dep;

	/* append property to the prop list of symbol */
	if (current_entry->sym) {
		struct property **propp;

		for (propp = &current_entry->sym->prop;
		     *propp;
		     propp = &(*propp)->next)
			;
		*propp = prop;
	}

	return prop;
}

struct property *menu_add_prompt(enum prop_type type, const char *prompt,
				 struct expr *dep)
{
	struct property *prop = menu_add_prop(type, NULL, dep);

	if (isspace(*prompt)) {
		prop_warn(prop, "leading whitespace ignored");
		while (isspace(*prompt))
			prompt++;
	}
	if (current_entry->prompt)
		prop_warn(prop, "prompt redefined");

	/* Apply all upper menus' visibilities to actual prompts. */
	if (type == P_PROMPT) {
		struct menu *menu = current_entry;

		while ((menu = menu->parent) != NULL) {
			struct expr *dup_expr;

			if (!menu->visibility)
				continue;
			/*
			 * Do not add a reference to the menu's visibility
			 * expression but use a copy of it. Otherwise the
			 * expression reduction functions will modify
			 * expressions that have multiple references which
			 * can cause unwanted side effects.
			 */
			dup_expr = expr_copy(menu->visibility);

			prop->visible.expr = expr_alloc_and(prop->visible.expr,
							    dup_expr);
		}
	}

	current_entry->prompt = prop;
	prop->text = prompt;

	return prop;
}

void menu_add_visibility(struct expr *expr)
{
	current_entry->visibility = expr_alloc_and(current_entry->visibility,
	    expr);
}

void menu_add_expr(enum prop_type type, struct expr *expr, struct expr *dep)
{
	menu_add_prop(type, expr, dep);
}

void menu_add_symbol(enum prop_type type, struct symbol *sym, struct expr *dep)
{
	menu_add_prop(type, expr_alloc_symbol(sym), dep);
}

static int menu_validate_number(struct symbol *sym, struct symbol *sym2)
{
	return sym2->type == S_INT || sym2->type == S_HEX ||
	       (sym2->type == S_UNKNOWN && sym_string_valid(sym, sym2->name));
}

static void sym_check_prop(struct symbol *sym)
{
	struct property *prop;
	struct symbol *sym2;
	char *use;

	for (prop = sym->prop; prop; prop = prop->next) {
		switch (prop->type) {
		case P_DEFAULT:
			if ((sym->type == S_STRING || sym->type == S_INT || sym->type == S_HEX) &&
			    prop->expr->type != E_SYMBOL)
				prop_warn(prop,
				    "default for config symbol '%s'"
				    " must be a single symbol", sym->name);
			if (prop->expr->type != E_SYMBOL)
				break;
			sym2 = prop_get_symbol(prop);
			if (sym->type == S_HEX || sym->type == S_INT) {
				if (!menu_validate_number(sym, sym2))
					prop_warn(prop,
					    "'%s': number is invalid",
					    sym->name);
			}
			if (sym_is_choice(sym)) {
				struct menu *choice = sym_get_choice_menu(sym2);

				if (!choice || choice->sym != sym)
					prop_warn(prop,
						  "choice default symbol '%s' is not contained in the choice",
						  sym2->name);
			}
			break;
		case P_SELECT:
		case P_IMPLY:
			use = prop->type == P_SELECT ? "select" : "imply";
			sym2 = prop_get_symbol(prop);
			if (sym->type != S_BOOLEAN && sym->type != S_TRISTATE)
				prop_warn(prop,
				    "config symbol '%s' uses %s, but is "
				    "not bool or tristate", sym->name, use);
			else if (sym2->type != S_UNKNOWN &&
				 sym2->type != S_BOOLEAN &&
				 sym2->type != S_TRISTATE)
				prop_warn(prop,
				    "'%s' has wrong type. '%s' only "
				    "accept arguments of bool and "
				    "tristate type", sym2->name, use);
			break;
		case P_RANGE:
			if (sym->type != S_INT && sym->type != S_HEX)
				prop_warn(prop, "range is only allowed "
						"for int or hex symbols");
			if (!menu_validate_number(sym, prop->expr->left.sym) ||
			    !menu_validate_number(sym, prop->expr->right.sym))
				prop_warn(prop, "range is invalid");
			break;
		default:
			;
		}
	}
}

static void _menu_finalize(struct menu *parent, bool inside_choice)
{
	struct menu *menu, *last_menu;
	struct symbol *sym;
	struct property *prop;
	struct expr *basedep, *dep, *dep2;

	sym = parent->sym;
	if (parent->list) {
		/*
		 * This menu node has children. We (recursively) process them
		 * and propagate parent dependencies before moving on.
		 */

		/* For each child menu node... */
		for (menu = parent->list; menu; menu = menu->next) {
			/*
			 * Propagate parent dependencies to the child menu
			 * node, also rewriting and simplifying expressions
			 */
			basedep = rewrite_m(menu->dep);
			basedep = expr_transform(basedep);
			basedep = expr_alloc_and(expr_copy(parent->dep), basedep);
			basedep = expr_eliminate_dups(basedep);
			menu->dep = basedep;

			if (menu->sym)
				/*
				 * Note: For symbols, all prompts are included
				 * too in the symbol's own property list
				 */
				prop = menu->sym->prop;
			else
				/*
				 * For non-symbol menu nodes, we just need to
				 * handle the prompt
				 */
				prop = menu->prompt;

			/* For each property... */
			for (; prop; prop = prop->next) {
				if (prop->menu != menu)
					/*
					 * Two possibilities:
					 *
					 * 1. The property lacks dependencies
					 *    and so isn't location-specific,
					 *    e.g. an 'option'
					 *
					 * 2. The property belongs to a symbol
					 *    defined in multiple locations and
					 *    is from some other location. It
					 *    will be handled there in that
					 *    case.
					 *
					 * Skip the property.
					 */
					continue;

				/*
				 * Propagate parent dependencies to the
				 * property's condition, rewriting and
				 * simplifying expressions at the same time
				 */
				dep = rewrite_m(prop->visible.expr);
				dep = expr_transform(dep);
				dep = expr_alloc_and(expr_copy(basedep), dep);
				dep = expr_eliminate_dups(dep);
				prop->visible.expr = dep;

				/*
				 * Handle selects and implies, which modify the
				 * dependencies of the selected/implied symbol
				 */
				if (prop->type == P_SELECT) {
					struct symbol *es = prop_get_symbol(prop);
					es->rev_dep.expr = expr_alloc_or(es->rev_dep.expr,
							expr_alloc_and(expr_alloc_symbol(menu->sym), expr_copy(dep)));
				} else if (prop->type == P_IMPLY) {
					struct symbol *es = prop_get_symbol(prop);
					es->implied.expr = expr_alloc_or(es->implied.expr,
							expr_alloc_and(expr_alloc_symbol(menu->sym), expr_copy(dep)));
				}
			}
		}

		/*
		 * Recursively process children in the same fashion before
		 * moving on
		 */
		for (menu = parent->list; menu; menu = menu->next)
			_menu_finalize(menu, sym && sym_is_choice(sym));
	} else if (!inside_choice && sym) {
		/*
		 * Automatic submenu creation. If sym is a symbol and A, B, C,
		 * ... are consecutive items (symbols, menus, ifs, etc.) that
		 * all depend on sym, then the following menu structure is
		 * created:
		 *
		 *	sym
		 *	 +-A
		 *	 +-B
		 *	 +-C
		 *	 ...
		 *
		 * This also works recursively, giving the following structure
		 * if A is a symbol and B depends on A:
		 *
		 *	sym
		 *	 +-A
		 *	 | +-B
		 *	 +-C
		 *	 ...
		 */

		basedep = parent->prompt ? parent->prompt->visible.expr : NULL;
		basedep = expr_trans_compare(basedep, E_UNEQUAL, &symbol_no);
		basedep = expr_eliminate_dups(expr_transform(basedep));

		/* Examine consecutive elements after sym */
		last_menu = NULL;
		for (menu = parent->next; menu; menu = menu->next) {
			dep = menu->prompt ? menu->prompt->visible.expr : menu->dep;
			if (!expr_contains_symbol(dep, sym))
				/* No dependency, quit */
				break;
			if (expr_depends_symbol(dep, sym))
				/* Absolute dependency, put in submenu */
				goto next;

			/*
			 * Also consider it a dependency on sym if our
			 * dependencies contain sym and are a "superset" of
			 * sym's dependencies, e.g. '(sym || Q) && R' when sym
			 * depends on R.
			 *
			 * Note that 'R' might be from an enclosing menu or if,
			 * making this a more common case than it might seem.
			 */
			dep = expr_trans_compare(dep, E_UNEQUAL, &symbol_no);
			dep = expr_eliminate_dups(expr_transform(dep));
			dep2 = expr_copy(basedep);
			expr_eliminate_eq(&dep, &dep2);
			expr_free(dep);
			if (!expr_is_yes(dep2)) {
				/* Not superset, quit */
				expr_free(dep2);
				break;
			}
			/* Superset, put in submenu */
			expr_free(dep2);
		next:
			_menu_finalize(menu, false);
			menu->parent = parent;
			last_menu = menu;
		}
		expr_free(basedep);
		if (last_menu) {
			parent->list = parent->next;
			parent->next = last_menu->next;
			last_menu->next = NULL;
		}

		sym->dir_dep.expr = expr_alloc_or(sym->dir_dep.expr, parent->dep);
	}
	for (menu = parent->list; menu; menu = menu->next) {
		/*
		 * This code serves two purposes:
		 *
		 * (1) Flattening 'if' blocks, which do not specify a submenu
		 *     and only add dependencies.
		 *
		 *     (Automatic submenu creation might still create a submenu
		 *     from an 'if' before this code runs.)
		 *
		 * (2) "Undoing" any automatic submenus created earlier below
		 *     promptless symbols.
		 *
		 * Before:
		 *
		 *	A
		 *	if ... (or promptless symbol)
		 *	 +-B
		 *	 +-C
		 *	D
		 *
		 * After:
		 *
		 *	A
		 *	if ... (or promptless symbol)
		 *	B
		 *	C
		 *	D
		 */
		if (menu->list && (!menu->prompt || !menu->prompt->text)) {
			for (last_menu = menu->list; ; last_menu = last_menu->next) {
				last_menu->parent = parent;
				if (!last_menu->next)
					break;
			}
			last_menu->next = menu->next;
			menu->next = menu->list;
			menu->list = NULL;
		}
	}

	if (sym && !(sym->flags & SYMBOL_WARNED)) {
		if (sym->type == S_UNKNOWN)
			menu_warn(parent, "config symbol defined without type");

		/* Check properties connected to this symbol */
		sym_check_prop(sym);
		sym->flags |= SYMBOL_WARNED;
	}
}

void menu_finalize(void)
{
	_menu_finalize(&rootmenu, false);
}

bool menu_has_prompt(const struct menu *menu)
{
	if (!menu->prompt)
		return false;
	return true;
}

/*
 * Determine if a menu is empty.
 * A menu is considered empty if it contains no or only
 * invisible entries.
 */
bool menu_is_empty(struct menu *menu)
{
	struct menu *child;

	for (child = menu->list; child; child = child->next) {
		if (menu_is_visible(child))
			return(false);
	}
	return(true);
}

bool menu_is_visible(struct menu *menu)
{
	struct symbol *sym;
	tristate visible;

	if (!menu->prompt)
		return false;

	if (menu->visibility) {
		if (expr_calc_value(menu->visibility) == no)
			return false;
	}

	sym = menu->sym;
	if (sym) {
		sym_calc_value(sym);
		visible = menu->prompt->visible.tri;
	} else
		visible = menu->prompt->visible.tri = expr_calc_value(menu->prompt->visible.expr);

	return visible != no;
}

const char *menu_get_prompt(const struct menu *menu)
{
	if (menu->prompt)
		return menu->prompt->text;
	else if (menu->sym)
		return menu->sym->name;
	return NULL;
}

struct menu *menu_get_parent_menu(struct menu *menu)
{
	enum prop_type type;

	for (; menu != &rootmenu; menu = menu->parent) {
		type = menu->prompt ? menu->prompt->type : 0;
		if (type == P_MENU)
			break;
	}
	return menu;
}

static void get_def_str(struct gstr *r, const struct menu *menu)
{
	str_printf(r, "Defined at %s:%d\n",
		   menu->filename, menu->lineno);
}

static void get_dep_str(struct gstr *r, const struct expr *expr,
			const char *prefix)
{
	if (!expr_is_yes(expr)) {
		str_append(r, prefix);
		expr_gstr_print(expr, r);
		str_append(r, "\n");
	}
}

int __attribute__((weak)) get_jump_key_char(void)
{
	return -1;
}

static void get_prompt_str(struct gstr *r, struct property *prop,
			   struct list_head *head)
{
	int i, j;
	struct menu *submenu[8], *menu, *location = NULL;
	struct jump_key *jump = NULL;

	str_printf(r, "  Prompt: %s\n", prop->text);

	get_dep_str(r, prop->menu->dep, "  Depends on: ");
	/*
	 * Most prompts in Linux have visibility that exactly matches their
	 * dependencies. For these, we print only the dependencies to improve
	 * readability. However, prompts with inline "if" expressions and
	 * prompts with a parent that has a "visible if" expression have
	 * differing dependencies and visibility. In these rare cases, we
	 * print both.
	 */
	if (!expr_eq(prop->menu->dep, prop->visible.expr))
		get_dep_str(r, prop->visible.expr, "  Visible if: ");

	menu = prop->menu;
	for (i = 0; menu != &rootmenu && i < 8; menu = menu->parent) {
		submenu[i++] = menu;
		if (location == NULL && menu_is_visible(menu))
			location = menu;
	}
	if (head && location) {
		jump = xmalloc(sizeof(struct jump_key));
		jump->target = location;
		list_add_tail(&jump->entries, head);
	}

	str_printf(r, "  Location:\n");
	for (j = 0; --i >= 0; j++) {
		int jk = -1;
		int indent = 2 * j + 4;

		menu = submenu[i];
		if (jump && menu == location) {
			jump->offset = strlen(r->s);
			jk = get_jump_key_char();
		}

		if (jk >= 0) {
			str_printf(r, "(%c)", jk);
			indent -= 3;
		}

		str_printf(r, "%*c-> %s", indent, ' ', menu_get_prompt(menu));
		if (menu->sym) {
			str_printf(r, " (%s [=%s])", menu->sym->name ?
				menu->sym->name : "<choice>",
				sym_get_string_value(menu->sym));
		}
		str_append(r, "\n");
	}
}

static void get_symbol_props_str(struct gstr *r, struct symbol *sym,
				 enum prop_type tok, const char *prefix)
{
	bool hit = false;
	struct property *prop;

	for_all_properties(sym, prop, tok) {
		if (!hit) {
			str_append(r, prefix);
			hit = true;
		} else
			str_printf(r, " && ");
		expr_gstr_print(prop->expr, r);
	}
	if (hit)
		str_append(r, "\n");
}

/*
 * head is optional and may be NULL
 */
static void get_symbol_str(struct gstr *r, struct symbol *sym,
		    struct list_head *head)
{
	struct property *prop;
	struct menu *menu;

	if (sym && sym->name) {
		str_printf(r, "Symbol: %s [=%s]\n", sym->name,
			   sym_get_string_value(sym));
		str_printf(r, "Type  : %s\n", sym_type_name(sym->type));
		if (sym->type == S_INT || sym->type == S_HEX) {
			prop = sym_get_range_prop(sym);
			if (prop) {
				str_printf(r, "Range : ");
				expr_gstr_print(prop->expr, r);
				str_append(r, "\n");
			}
		}
	}

	/* Print the definitions with prompts before the ones without */
	list_for_each_entry(menu, &sym->menus, link) {
		if (menu->prompt) {
			get_def_str(r, menu);
			get_prompt_str(r, menu->prompt, head);
		}
	}

	list_for_each_entry(menu, &sym->menus, link) {
		if (!menu->prompt) {
			get_def_str(r, menu);
			get_dep_str(r, menu->dep, "  Depends on: ");
		}
	}

	get_symbol_props_str(r, sym, P_SELECT, "Selects: ");
	if (sym->rev_dep.expr) {
		expr_gstr_print_revdep(sym->rev_dep.expr, r, yes, "Selected by [y]:\n");
		expr_gstr_print_revdep(sym->rev_dep.expr, r, mod, "Selected by [m]:\n");
		expr_gstr_print_revdep(sym->rev_dep.expr, r, no, "Selected by [n]:\n");
	}

	get_symbol_props_str(r, sym, P_IMPLY, "Implies: ");
	if (sym->implied.expr) {
		expr_gstr_print_revdep(sym->implied.expr, r, yes, "Implied by [y]:\n");
		expr_gstr_print_revdep(sym->implied.expr, r, mod, "Implied by [m]:\n");
		expr_gstr_print_revdep(sym->implied.expr, r, no, "Implied by [n]:\n");
	}

	str_append(r, "\n\n");
}

struct gstr get_relations_str(struct symbol **sym_arr, struct list_head *head)
{
	struct symbol *sym;
	struct gstr res = str_new();
	int i;

	for (i = 0; sym_arr && (sym = sym_arr[i]); i++)
		get_symbol_str(&res, sym, head);
	if (!i)
		str_append(&res, "No matches found.\n");
	return res;
}


void menu_get_ext_help(struct menu *menu, struct gstr *help)
{
	struct symbol *sym = menu->sym;
	const char *help_text = nohelp_text;

	if (menu->help) {
		if (sym->name)
			str_printf(help, "%s%s:\n\n", CONFIG_, sym->name);
		help_text = menu->help;
	}
	str_printf(help, "%s\n", help_text);
	if (sym)
		get_symbol_str(help, sym, NULL);
}
