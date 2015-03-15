/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Released under the terms of the GNU GPL v2.0.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "lkc.h"

static const char nohelp_text[] = "There is no help available for this option.";

struct menu rootmenu;
static struct menu **last_entry_ptr;

struct file *file_list;
struct file *current_file;

void menu_warn(struct menu *menu, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "%s:%d:warning: ", menu->file->name, menu->lineno);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

static void prop_warn(struct property *prop, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "%s:%d:warning: ", prop->file->name, prop->lineno);
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
	menu->file = current_file;
	menu->lineno = zconf_lineno();

	*last_entry_ptr = menu;
	last_entry_ptr = &menu->next;
	current_entry = menu;
	if (sym)
		menu_add_symbol(P_SYMBOL, sym, NULL);
}

void menu_end_entry(void)
{
}

struct menu *menu_add_menu(void)
{
	menu_end_entry();
	last_entry_ptr = &current_entry->list;
	return current_menu = current_entry;
}

void menu_end_menu(void)
{
	last_entry_ptr = &current_menu->next;
	current_menu = current_menu->parent;
}

static struct expr *menu_check_dep(struct expr *e)
{
	if (!e)
		return e;

	switch (e->type) {
	case E_NOT:
		e->left.expr = menu_check_dep(e->left.expr);
		break;
	case E_OR:
	case E_AND:
		e->left.expr = menu_check_dep(e->left.expr);
		e->right.expr = menu_check_dep(e->right.expr);
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
	current_entry->dep = expr_alloc_and(current_entry->dep, menu_check_dep(dep));
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

struct property *menu_add_prop(enum prop_type type, char *prompt, struct expr *expr, struct expr *dep)
{
	struct property *prop = prop_alloc(type, current_entry->sym);

	prop->menu = current_entry;
	prop->expr = expr;
	prop->visible.expr = menu_check_dep(dep);

	if (prompt) {
		if (isspace(*prompt)) {
			prop_warn(prop, "leading whitespace ignored");
			while (isspace(*prompt))
				prompt++;
		}
		if (current_entry->prompt && current_entry != &rootmenu)
			prop_warn(prop, "prompt redefined");

		/* Apply all upper menus' visibilities to actual prompts. */
		if(type == P_PROMPT) {
			struct menu *menu = current_entry;

			while ((menu = menu->parent) != NULL) {
				struct expr *dup_expr;

				if (!menu->visibility)
					continue;
				/*
				 * Do not add a reference to the
				 * menu's visibility expression but
				 * use a copy of it.  Otherwise the
				 * expression reduction functions
				 * will modify expressions that have
				 * multiple references which can
				 * cause unwanted side effects.
				 */
				dup_expr = expr_copy(menu->visibility);

				prop->visible.expr
					= expr_alloc_and(prop->visible.expr,
							 dup_expr);
			}
		}

		current_entry->prompt = prop;
	}
	prop->text = prompt;

	return prop;
}

struct property *menu_add_prompt(enum prop_type type, char *prompt, struct expr *dep)
{
	return menu_add_prop(type, prompt, NULL, dep);
}

void menu_add_visibility(struct expr *expr)
{
	current_entry->visibility = expr_alloc_and(current_entry->visibility,
	    expr);
}

void menu_add_expr(enum prop_type type, struct expr *expr, struct expr *dep)
{
	menu_add_prop(type, NULL, expr, dep);
}

void menu_add_symbol(enum prop_type type, struct symbol *sym, struct expr *dep)
{
	menu_add_prop(type, NULL, expr_alloc_symbol(sym), dep);
}

void menu_add_option(int token, char *arg)
{
	switch (token) {
	case T_OPT_MODULES:
		if (modules_sym)
			zconf_error("symbol '%s' redefines option 'modules'"
				    " already defined by symbol '%s'",
				    current_entry->sym->name,
				    modules_sym->name
				    );
		modules_sym = current_entry->sym;
		break;
	case T_OPT_DEFCONFIG_LIST:
		if (!sym_defconfig_list)
			sym_defconfig_list = current_entry->sym;
		else if (sym_defconfig_list != current_entry->sym)
			zconf_error("trying to redefine defconfig symbol");
		break;
	case T_OPT_ENV:
		prop_add_env(arg);
		break;
	case T_OPT_ALLNOCONFIG_Y:
		current_entry->sym->flags |= SYMBOL_ALLNOCONFIG_Y;
		break;
	}
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
			break;
		case P_SELECT:
			sym2 = prop_get_symbol(prop);
			if (sym->type != S_BOOLEAN && sym->type != S_TRISTATE)
				prop_warn(prop,
				    "config symbol '%s' uses select, but is "
				    "not boolean or tristate", sym->name);
			else if (sym2->type != S_UNKNOWN &&
				 sym2->type != S_BOOLEAN &&
				 sym2->type != S_TRISTATE)
				prop_warn(prop,
				    "'%s' has wrong type. 'select' only "
				    "accept arguments of boolean and "
				    "tristate type", sym2->name);
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

void menu_finalize(struct menu *parent)
{
	struct menu *menu, *last_menu;
	struct symbol *sym;
	struct property *prop;
	struct expr *parentdep, *basedep, *dep, *dep2, **ep;

	sym = parent->sym;
	if (parent->list) {
		if (sym && sym_is_choice(sym)) {
			if (sym->type == S_UNKNOWN) {
				/* find the first choice value to find out choice type */
				current_entry = parent;
				for (menu = parent->list; menu; menu = menu->next) {
					if (menu->sym && menu->sym->type != S_UNKNOWN) {
						menu_set_type(menu->sym->type);
						break;
					}
				}
			}
			/* set the type of the remaining choice values */
			for (menu = parent->list; menu; menu = menu->next) {
				current_entry = menu;
				if (menu->sym && menu->sym->type == S_UNKNOWN)
					menu_set_type(sym->type);
			}
			parentdep = expr_alloc_symbol(sym);
		} else if (parent->prompt)
			parentdep = parent->prompt->visible.expr;
		else
			parentdep = parent->dep;

		for (menu = parent->list; menu; menu = menu->next) {
			basedep = expr_transform(menu->dep);
			basedep = expr_alloc_and(expr_copy(parentdep), basedep);
			basedep = expr_eliminate_dups(basedep);
			menu->dep = basedep;
			if (menu->sym)
				prop = menu->sym->prop;
			else
				prop = menu->prompt;
			for (; prop; prop = prop->next) {
				if (prop->menu != menu)
					continue;
				dep = expr_transform(prop->visible.expr);
				dep = expr_alloc_and(expr_copy(basedep), dep);
				dep = expr_eliminate_dups(dep);
				if (menu->sym && menu->sym->type != S_TRISTATE)
					dep = expr_trans_bool(dep);
				prop->visible.expr = dep;
				if (prop->type == P_SELECT) {
					struct symbol *es = prop_get_symbol(prop);
					es->rev_dep.expr = expr_alloc_or(es->rev_dep.expr,
							expr_alloc_and(expr_alloc_symbol(menu->sym), expr_copy(dep)));
				}
			}
		}
		for (menu = parent->list; menu; menu = menu->next)
			menu_finalize(menu);
	} else if (sym) {
		basedep = parent->prompt ? parent->prompt->visible.expr : NULL;
		basedep = expr_trans_compare(basedep, E_UNEQUAL, &symbol_no);
		basedep = expr_eliminate_dups(expr_transform(basedep));
		last_menu = NULL;
		for (menu = parent->next; menu; menu = menu->next) {
			dep = menu->prompt ? menu->prompt->visible.expr : menu->dep;
			if (!expr_contains_symbol(dep, sym))
				break;
			if (expr_depends_symbol(dep, sym))
				goto next;
			dep = expr_trans_compare(dep, E_UNEQUAL, &symbol_no);
			dep = expr_eliminate_dups(expr_transform(dep));
			dep2 = expr_copy(basedep);
			expr_eliminate_eq(&dep, &dep2);
			expr_free(dep);
			if (!expr_is_yes(dep2)) {
				expr_free(dep2);
				break;
			}
			expr_free(dep2);
		next:
			menu_finalize(menu);
			menu->parent = parent;
			last_menu = menu;
		}
		if (last_menu) {
			parent->list = parent->next;
			parent->next = last_menu->next;
			last_menu->next = NULL;
		}

		sym->dir_dep.expr = expr_alloc_or(sym->dir_dep.expr, parent->dep);
	}
	for (menu = parent->list; menu; menu = menu->next) {
		if (sym && sym_is_choice(sym) &&
		    menu->sym && !sym_is_choice_value(menu->sym)) {
			current_entry = menu;
			menu->sym->flags |= SYMBOL_CHOICEVAL;
			if (!menu->prompt)
				menu_warn(menu, "choice value must have a prompt");
			for (prop = menu->sym->prop; prop; prop = prop->next) {
				if (prop->type == P_DEFAULT)
					prop_warn(prop, "defaults for choice "
						  "values not supported");
				if (prop->menu == menu)
					continue;
				if (prop->type == P_PROMPT &&
				    prop->menu->parent->sym != sym)
					prop_warn(prop, "choice value used outside its choice group");
			}
			/* Non-tristate choice values of tristate choices must
			 * depend on the choice being set to Y. The choice
			 * values' dependencies were propagated to their
			 * properties above, so the change here must be re-
			 * propagated.
			 */
			if (sym->type == S_TRISTATE && menu->sym->type != S_TRISTATE) {
				basedep = expr_alloc_comp(E_EQUAL, sym, &symbol_yes);
				menu->dep = expr_alloc_and(basedep, menu->dep);
				for (prop = menu->sym->prop; prop; prop = prop->next) {
					if (prop->menu != menu)
						continue;
					prop->visible.expr = expr_alloc_and(expr_copy(basedep),
									    prop->visible.expr);
				}
			}
			menu_add_symbol(P_CHOICE, sym, NULL);
			prop = sym_get_choice_prop(sym);
			for (ep = &prop->expr; *ep; ep = &(*ep)->left.expr)
				;
			*ep = expr_alloc_one(E_LIST, NULL);
			(*ep)->right.sym = menu->sym;
		}
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

		if (sym_is_choice(sym) && !parent->prompt)
			menu_warn(parent, "choice must have a prompt");

		/* Check properties connected to this symbol */
		sym_check_prop(sym);
		sym->flags |= SYMBOL_WARNED;
	}

	if (sym && !sym_is_optional(sym) && parent->prompt) {
		sym->rev_dep.expr = expr_alloc_or(sym->rev_dep.expr,
				expr_alloc_and(parent->prompt->visible.expr,
					expr_alloc_symbol(&symbol_mod)));
	}
}

bool menu_has_prompt(struct menu *menu)
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
	struct menu *child;
	struct symbol *sym;
	tristate visible;

	if (!menu->prompt)
		return false;

	if (menu->visibility) {
		if (expr_calc_value(menu->visibility) == no)
			return no;
	}

	sym = menu->sym;
	if (sym) {
		sym_calc_value(sym);
		visible = menu->prompt->visible.tri;
	} else
		visible = menu->prompt->visible.tri = expr_calc_value(menu->prompt->visible.expr);

	if (visible != no)
		return true;

	if (!sym || sym_get_tristate_value(menu->sym) == no)
		return false;

	for (child = menu->list; child; child = child->next) {
		if (menu_is_visible(child)) {
			if (sym)
				sym->flags |= SYMBOL_DEF_USER;
			return true;
		}
	}

	return false;
}

const char *menu_get_prompt(struct menu *menu)
{
	if (menu->prompt)
		return menu->prompt->text;
	else if (menu->sym)
		return menu->sym->name;
	return NULL;
}

struct menu *menu_get_root_menu(struct menu *menu)
{
	return &rootmenu;
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

bool menu_has_help(struct menu *menu)
{
	return menu->help != NULL;
}

const char *menu_get_help(struct menu *menu)
{
	if (menu->help)
		return menu->help;
	else
		return "";
}

static void get_prompt_str(struct gstr *r, struct property *prop,
			   struct list_head *head)
{
	int i, j;
	struct menu *submenu[8], *menu, *location = NULL;
	struct jump_key *jump = NULL;

	str_printf(r, _("Prompt: %s\n"), _(prop->text));
	menu = prop->menu->parent;
	for (i = 0; menu != &rootmenu && i < 8; menu = menu->parent) {
		bool accessible = menu_is_visible(menu);

		submenu[i++] = menu;
		if (location == NULL && accessible)
			location = menu;
	}
	if (head && location) {
		jump = xmalloc(sizeof(struct jump_key));

		if (menu_is_visible(prop->menu)) {
			/*
			 * There is not enough room to put the hint at the
			 * beginning of the "Prompt" line. Put the hint on the
			 * last "Location" line even when it would belong on
			 * the former.
			 */
			jump->target = prop->menu;
		} else
			jump->target = location;

		if (list_empty(head))
			jump->index = 0;
		else
			jump->index = list_entry(head->prev, struct jump_key,
						 entries)->index + 1;

		list_add_tail(&jump->entries, head);
	}

	if (i > 0) {
		str_printf(r, _("  Location:\n"));
		for (j = 4; --i >= 0; j += 2) {
			menu = submenu[i];
			if (jump && menu == location)
				jump->offset = strlen(r->s);
			str_printf(r, "%*c-> %s", j, ' ',
				   _(menu_get_prompt(menu)));
			if (menu->sym) {
				str_printf(r, " (%s [=%s])", menu->sym->name ?
					menu->sym->name : _("<choice>"),
					sym_get_string_value(menu->sym));
			}
			str_append(r, "\n");
		}
	}
}

/*
 * get property of type P_SYMBOL
 */
static struct property *get_symbol_prop(struct symbol *sym)
{
	struct property *prop = NULL;

	for_all_properties(sym, prop, P_SYMBOL)
		break;
	return prop;
}

/*
 * head is optional and may be NULL
 */
void get_symbol_str(struct gstr *r, struct symbol *sym,
		    struct list_head *head)
{
	bool hit;
	struct property *prop;

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
	for_all_prompts(sym, prop)
		get_prompt_str(r, prop, head);

	prop = get_symbol_prop(sym);
	if (prop) {
		str_printf(r, _("  Defined at %s:%d\n"), prop->menu->file->name,
			prop->menu->lineno);
		if (!expr_is_yes(prop->visible.expr)) {
			str_append(r, _("  Depends on: "));
			expr_gstr_print(prop->visible.expr, r);
			str_append(r, "\n");
		}
	}

	hit = false;
	for_all_properties(sym, prop, P_SELECT) {
		if (!hit) {
			str_append(r, "  Selects: ");
			hit = true;
		} else
			str_printf(r, " && ");
		expr_gstr_print(prop->expr, r);
	}
	if (hit)
		str_append(r, "\n");
	if (sym->rev_dep.expr) {
		str_append(r, _("  Selected by: "));
		expr_gstr_print(sym->rev_dep.expr, r);
		str_append(r, "\n");
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
		str_append(&res, _("No matches found.\n"));
	return res;
}


void menu_get_ext_help(struct menu *menu, struct gstr *help)
{
	struct symbol *sym = menu->sym;
	const char *help_text = nohelp_text;

	if (menu_has_help(menu)) {
		if (sym->name)
			str_printf(help, "%s%s:\n\n", CONFIG_, sym->name);
		help_text = menu_get_help(menu);
	}
	str_printf(help, "%s\n", _(help_text));
	if (sym)
		get_symbol_str(help, sym, NULL);
}
