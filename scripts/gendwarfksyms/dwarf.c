// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Google LLC
 */

#include "gendwarfksyms.h"

static bool get_ref_die_attr(Dwarf_Die *die, unsigned int id, Dwarf_Die *value)
{
	Dwarf_Attribute da;

	/* dwarf_formref_die returns a pointer instead of an error value. */
	return dwarf_attr(die, id, &da) && dwarf_formref_die(&da, value);
}

#define DEFINE_GET_STRING_ATTR(attr)                         \
	static const char *get_##attr##_attr(Dwarf_Die *die) \
	{                                                    \
		Dwarf_Attribute da;                          \
		if (dwarf_attr(die, DW_AT_##attr, &da))      \
			return dwarf_formstring(&da);        \
		return NULL;                                 \
	}

DEFINE_GET_STRING_ATTR(name)
DEFINE_GET_STRING_ATTR(linkage_name)

static const char *get_symbol_name(Dwarf_Die *die)
{
	const char *name;

	/* rustc uses DW_AT_linkage_name for exported symbols */
	name = get_linkage_name_attr(die);
	if (!name)
		name = get_name_attr(die);

	return name;
}

static bool match_export_symbol(struct state *state, Dwarf_Die *die)
{
	Dwarf_Die *source = die;
	Dwarf_Die origin;

	/* If the DIE has an abstract origin, use it for type information. */
	if (get_ref_die_attr(die, DW_AT_abstract_origin, &origin))
		source = &origin;

	state->sym = symbol_get(get_symbol_name(die));

	/* Look up using the origin name if there are no matches. */
	if (!state->sym && source != die)
		state->sym = symbol_get(get_symbol_name(source));

	state->die = *source;
	return !!state->sym;
}

/*
 * Type string processing
 */
static void process(const char *s)
{
	s = s ?: "<null>";

	if (dump_dies)
		fputs(s, stderr);
}

bool match_all(Dwarf_Die *die)
{
	return true;
}

int process_die_container(struct state *state, Dwarf_Die *die,
			  die_callback_t func, die_match_callback_t match)
{
	Dwarf_Die current;
	int res;

	res = checkp(dwarf_child(die, &current));
	while (!res) {
		if (match(&current)) {
			/* <0 = error, 0 = continue, >0 = stop */
			res = checkp(func(state, &current));
			if (res)
				return res;
		}

		res = checkp(dwarf_siblingof(&current, &current));
	}

	return 0;
}

/*
 * Exported symbol processing
 */
static void process_symbol(struct state *state, Dwarf_Die *die,
			   die_callback_t process_func)
{
	debug("%s", state->sym->name);
	check(process_func(state, die));
	if (dump_dies)
		fputs("\n", stderr);
}

static int __process_subprogram(struct state *state, Dwarf_Die *die)
{
	process("subprogram");
	return 0;
}

static void process_subprogram(struct state *state, Dwarf_Die *die)
{
	process_symbol(state, die, __process_subprogram);
}

static int __process_variable(struct state *state, Dwarf_Die *die)
{
	process("variable ");
	return 0;
}

static void process_variable(struct state *state, Dwarf_Die *die)
{
	process_symbol(state, die, __process_variable);
}

static int process_exported_symbols(struct state *unused, Dwarf_Die *die)
{
	int tag = dwarf_tag(die);

	switch (tag) {
	/* Possible containers of exported symbols */
	case DW_TAG_namespace:
	case DW_TAG_class_type:
	case DW_TAG_structure_type:
		return check(process_die_container(
			NULL, die, process_exported_symbols, match_all));

	/* Possible exported symbols */
	case DW_TAG_subprogram:
	case DW_TAG_variable: {
		struct state state;

		if (!match_export_symbol(&state, die))
			return 0;

		if (tag == DW_TAG_subprogram)
			process_subprogram(&state, &state.die);
		else
			process_variable(&state, &state.die);

		return 0;
	}
	default:
		return 0;
	}
}

void process_cu(Dwarf_Die *cudie)
{
	check(process_die_container(NULL, cudie, process_exported_symbols,
				    match_all));
}
