/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Released under the terms of the GNU GPL v2.0.
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <sys/utsname.h>

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
	int base;
	long long val, val2;
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
	val = strtoll(sym->curr.val, NULL, base);
	val2 = sym_get_range_val(prop->expr->left.sym, base);
	if (val >= val2) {
		val2 = sym_get_range_val(prop->expr->right.sym, base);
		if (val <= val2)
			return;
	}
	if (sym->type == S_INT)
		sprintf(str, "%lld", val2);
	else
		sprintf(str, "0x%llx", val2);
	sym->curr.val = strdup(str);
}

static void sym_set_changed(struct symbol *sym)
{
	struct property *prop;

	sym->flags |= SYMBOL_CHANGED;
	for (prop = sym->prop; prop; prop = prop->next) {
		if (prop->menu)
			prop->menu->flags |= MENU_CHANGED;
	}
}

static void sym_set_all_changed(void)
{
	struct symbol *sym;
	int i;

	for_all_symbols(i, sym)
		sym_set_changed(sym);
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

/*
 * Find the default symbol for a choice.
 * First try the default values for the choice symbol
 * Next locate the first visible choice value
 * Return NULL if none was found
 */
struct symbol *sym_choice_default(struct symbol *sym)
{
	struct symbol *def_sym;
	struct property *prop;
	struct expr *e;

	/* any of the defaults visible? */
	for_all_defaults(sym, prop) {
		prop->visible.tri = expr_calc_value(prop->visible.expr);
		if (prop->visible.tri == no)
			continue;
		def_sym = prop_get_symbol(prop);
		if (def_sym->visible != no)
			return def_sym;
	}

	/* just get the first visible value */
	prop = sym_get_choice_prop(sym);
	expr_list_for_each_sym(prop->expr, e, def_sym)
		if (def_sym->visible != no)
			return def_sym;

	/* failed to locate any defaults */
	return NULL;
}

static struct symbol *sym_calc_choice(struct symbol *sym)
{
	struct symbol *def_sym;
	struct property *prop;
	struct expr *e;
	int flags;

	/* first calculate all choice values' visibilities */
	flags = sym->flags;
	prop = sym_get_choice_prop(sym);
	expr_list_for_each_sym(prop->expr, e, def_sym) {
		sym_calc_visibility(def_sym);
		if (def_sym->visible != no)
			flags &= def_sym->flags;
	}

	sym->flags &= flags | ~SYMBOL_DEF_USER;

	/* is the user choice visible? */
	def_sym = sym->def[S_DEF_USER].val;
	if (def_sym && def_sym->visible != no)
		return def_sym;

	def_sym = sym_choice_default(sym);

	if (def_sym == NULL)
		/* no choice? reset tristate value */
		sym->curr.tri = no;

	return def_sym;
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

	if (sym_is_choice_value(sym) &&
	    sym->flags & SYMBOL_NEED_SET_CHOICE_VALUES) {
		sym->flags &= ~SYMBOL_NEED_SET_CHOICE_VALUES;
		prop = sym_get_choice_prop(sym);
		sym_calc_value(prop_get_symbol(prop));
	}

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
				struct expr *e;
				e = expr_simplify_unmet_dep(sym->rev_dep.expr,
				    sym->dir_dep.expr);
				fprintf(stderr, "warning: (");
				expr_fprint(e, stderr);
				fprintf(stderr, ") selects %s which has unmet direct dependencies (",
					sym->name);
				expr_fprint(sym->dir_dep.expr, stderr);
				fprintf(stderr, ")\n");
				expr_free(e);
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

		prop = sym_get_choice_prop(sym);
		expr_list_for_each_sym(prop->expr, e, choice_sym) {
			if ((sym->flags & SYMBOL_WRITE) &&
			    choice_sym->visible != no)
				choice_sym->flags |= SYMBOL_WRITE;
			if (sym->flags & SYMBOL_CHANGED)
				sym_set_changed(choice_sym);
		}
	}

	if (sym->flags & SYMBOL_AUTO)
		sym->flags &= ~SYMBOL_WRITE;

	if (sym->flags & SYMBOL_NEED_SET_CHOICE_VALUES)
		set_all_choice_values(sym);
}

void sym_clear_all_valid(void)
{
	struct symbol *sym;
	int i;

	for_all_symbols(i, sym)
		sym->flags &= ~SYMBOL_VALID;
	sym_add_change_count(1);
	sym_calc_value(modules_sym);
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
	const char *str;
	tristate val;

	sym_calc_visibility(sym);
	sym_calc_value(modules_sym);
	val = symbol_no.curr.tri;
	str = symbol_empty.curr.val;

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

	switch (sym->type) {
	case S_BOOLEAN:
	case S_TRISTATE:
		switch (val) {
		case no: return "n";
		case mod: return "m";
		case yes: return "y";
		}
	case S_INT:
	case S_HEX:
		return str;
	case S_STRING:
		return str;
	case S_OTHER:
	case S_UNKNOWN:
		break;
	}
	return "";
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
			sym_calc_value(modules_sym);
			return (modules_sym->curr.tri == no) ? "n" : "m";
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

	symbol = xmalloc(sizeof(*symbol));
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

/*
 * Expand symbol's names embedded in the string given in argument. Symbols'
 * name to be expanded shall be prefixed by a '$'. Unknown symbol expands to
 * the empty string.
 */
const char *sym_expand_string_value(const char *in)
{
	const char *src;
	char *res;
	size_t reslen;

	reslen = strlen(in) + 1;
	res = xmalloc(reslen);
	res[0] = '\0';

	while ((src = strchr(in, '$'))) {
		char *p, name[SYMBOL_MAXLENGTH];
		const char *symval = "";
		struct symbol *sym;
		size_t newlen;

		strncat(res, in, src - in);
		src++;

		p = name;
		while (isalnum(*src) || *src == '_')
			*p++ = *src++;
		*p = '\0';

		sym = sym_find(name);
		if (sym != NULL) {
			sym_calc_value(sym);
			symval = sym_get_string_value(sym);
		}

		newlen = strlen(res) + strlen(symval) + strlen(src) + 1;
		if (newlen > reslen) {
			reslen = newlen;
			res = realloc(res, reslen);
		}

		strcat(res, symval);
		in = src;
	}
	strcat(res, in);

	return res;
}

const char *sym_escape_string_value(const char *in)
{
	const char *p;
	size_t reslen;
	char *res;
	size_t l;

	reslen = strlen(in) + strlen("\"\"") + 1;

	p = in;
	for (;;) {
		l = strcspn(p, "\"\\");
		p += l;

		if (p[0] == '\0')
			break;

		reslen++;
		p++;
	}

	res = xmalloc(reslen);
	res[0] = '\0';

	strcat(res, "\"");

	p = in;
	for (;;) {
		l = strcspn(p, "\"\\");
		strncat(res, p, l);
		p += l;

		if (p[0] == '\0')
			break;

		strcat(res, "\\");
		strncat(res, p++, 1);
	}

	strcat(res, "\"");
	return res;
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

	for_all_symbols(i, sym) {
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
		sym_arr = malloc((cnt+1) * sizeof(struct symbol));
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
	struct expr *expr;
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
	struct menu *menu = NULL;
	struct property *prop;
	struct dep_stack cv_stack;

	if (sym_is_choice_value(last_sym)) {
		dep_stack_insert(&cv_stack, last_sym);
		last_sym = prop_get_symbol(sym_get_choice_prop(last_sym));
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
		prop = stack->prop;
		if (prop == NULL)
			prop = stack->sym->prop;

		/* for choice values find the menu entry (used below) */
		if (sym_is_choice(sym) || sym_is_choice_value(sym)) {
			for (prop = sym->prop; prop; prop = prop->next) {
				menu = prop->menu;
				if (prop->menu)
					break;
			}
		}
		if (stack->sym == last_sym)
			fprintf(stderr, "%s:%d:error: recursive dependency detected!\n",
				prop->file->name, prop->lineno);
			fprintf(stderr, "For a resolution refer to Documentation/kbuild/kconfig-language.txt\n");
			fprintf(stderr, "subsection \"Kconfig recursive dependency limitations\"\n");
		if (stack->expr) {
			fprintf(stderr, "%s:%d:\tsymbol %s %s value contains %s\n",
				prop->file->name, prop->lineno,
				sym->name ? sym->name : "<choice>",
				prop_get_type_name(prop->type),
				next_sym->name ? next_sym->name : "<choice>");
		} else if (stack->prop) {
			fprintf(stderr, "%s:%d:\tsymbol %s depends on %s\n",
				prop->file->name, prop->lineno,
				sym->name ? sym->name : "<choice>",
				next_sym->name ? next_sym->name : "<choice>");
		} else if (sym_is_choice(sym)) {
			fprintf(stderr, "%s:%d:\tchoice %s contains symbol %s\n",
				menu->file->name, menu->lineno,
				sym->name ? sym->name : "<choice>",
				next_sym->name ? next_sym->name : "<choice>");
		} else if (sym_is_choice_value(sym)) {
			fprintf(stderr, "%s:%d:\tsymbol %s is part of choice %s\n",
				menu->file->name, menu->lineno,
				sym->name ? sym->name : "<choice>",
				next_sym->name ? next_sym->name : "<choice>");
		} else {
			fprintf(stderr, "%s:%d:\tsymbol %s is selected by %s\n",
				prop->file->name, prop->lineno,
				sym->name ? sym->name : "<choice>",
				next_sym->name ? next_sym->name : "<choice>");
		}
	}

	if (check_top == &cv_stack)
		dep_stack_remove();
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
	printf("Oops! How to check %d?\n", e->type);
	return NULL;
}

/* return NULL when dependencies are OK */
static struct symbol *sym_check_sym_deps(struct symbol *sym)
{
	struct symbol *sym2;
	struct property *prop;
	struct dep_stack stack;

	dep_stack_insert(&stack, sym);

	sym2 = sym_check_expr_deps(sym->rev_dep.expr);
	if (sym2)
		goto out;

	for (prop = sym->prop; prop; prop = prop->next) {
		if (prop->type == P_CHOICE || prop->type == P_SELECT)
			continue;
		stack.prop = prop;
		sym2 = sym_check_expr_deps(prop->visible.expr);
		if (sym2)
			break;
		if (prop->type != P_DEFAULT || sym_is_choice(sym))
			continue;
		stack.expr = prop->expr;
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
	struct symbol *sym, *sym2;
	struct property *prop;
	struct expr *e;
	struct dep_stack stack;

	dep_stack_insert(&stack, choice);

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
		if (sym2)
			break;
	}
out:
	expr_list_for_each_sym(prop->expr, e, sym)
		sym->flags &= ~SYMBOL_CHECK;

	if (sym2 && sym_is_choice_value(sym2) &&
	    prop_get_symbol(sym_get_choice_prop(sym2)) == choice)
		sym2 = choice;

	dep_stack_remove();

	return sym2;
}

struct symbol *sym_check_deps(struct symbol *sym)
{
	struct symbol *sym2;
	struct property *prop;

	if (sym->flags & SYMBOL_CHECK) {
		sym_check_print_recursive(sym);
		return sym;
	}
	if (sym->flags & SYMBOL_CHECKED)
		return NULL;

	if (sym_is_choice_value(sym)) {
		struct dep_stack stack;

		/* for choice groups start the check with main choice symbol */
		dep_stack_insert(&stack, sym);
		prop = sym_get_choice_prop(sym);
		sym2 = sym_check_deps(prop_get_symbol(prop));
		dep_stack_remove();
	} else if (sym_is_choice(sym)) {
		sym2 = sym_check_choice_deps(sym);
	} else {
		sym->flags |= (SYMBOL_CHECK | SYMBOL_CHECKED);
		sym2 = sym_check_sym_deps(sym);
		sym->flags &= ~SYMBOL_CHECK;
	}

	if (sym2 && sym2 == sym)
		sym2 = NULL;

	return sym2;
}

struct property *prop_alloc(enum prop_type type, struct symbol *sym)
{
	struct property *prop;
	struct property **propp;

	prop = xmalloc(sizeof(*prop));
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
	case P_SYMBOL:
		return "symbol";
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
