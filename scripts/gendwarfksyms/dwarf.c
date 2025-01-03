// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Google LLC
 */

#include <inttypes.h>
#include <stdarg.h>
#include "gendwarfksyms.h"

#define DEFINE_GET_ATTR(attr, type)                                    \
	static bool get_##attr##_attr(Dwarf_Die *die, unsigned int id, \
				      type *value)                     \
	{                                                              \
		Dwarf_Attribute da;                                    \
		return dwarf_attr(die, id, &da) &&                     \
		       !dwarf_form##attr(&da, value);                  \
	}

DEFINE_GET_ATTR(udata, Dwarf_Word)

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
static void process(struct die *cache, const char *s)
{
	s = s ?: "<null>";

	if (dump_dies)
		fputs(s, stderr);

	die_map_add_string(cache, s);
}

#define MAX_FMT_BUFFER_SIZE 128

static void process_fmt(struct die *cache, const char *fmt, ...)
{
	char buf[MAX_FMT_BUFFER_SIZE];
	va_list args;

	va_start(args, fmt);

	if (checkp(vsnprintf(buf, sizeof(buf), fmt, args)) >= sizeof(buf))
		error("vsnprintf overflow: increase MAX_FMT_BUFFER_SIZE");

	process(cache, buf);
	va_end(args);
}

#define MAX_FQN_SIZE 64

/* Get a fully qualified name from DWARF scopes */
static char *get_fqn(Dwarf_Die *die)
{
	const char *list[MAX_FQN_SIZE];
	Dwarf_Die *scopes = NULL;
	bool has_name = false;
	char *fqn = NULL;
	char *p;
	int count = 0;
	int len = 0;
	int res;
	int i;

	res = checkp(dwarf_getscopes_die(die, &scopes));
	if (!res) {
		list[count] = get_name_attr(die);

		if (!list[count])
			return NULL;

		len += strlen(list[count]);
		count++;

		goto done;
	}

	for (i = res - 1; i >= 0 && count < MAX_FQN_SIZE; i--) {
		if (dwarf_tag(&scopes[i]) == DW_TAG_compile_unit)
			continue;

		list[count] = get_name_attr(&scopes[i]);

		if (list[count]) {
			has_name = true;
		} else {
			list[count] = "<anonymous>";
			has_name = false;
		}

		len += strlen(list[count]);
		count++;

		if (i > 0) {
			list[count++] = "::";
			len += 2;
		}
	}

	free(scopes);

	if (count == MAX_FQN_SIZE)
		warn("increase MAX_FQN_SIZE: reached the maximum");

	/* Consider the DIE unnamed if the last scope doesn't have a name */
	if (!has_name)
		return NULL;
done:
	fqn = xmalloc(len + 1);
	*fqn = '\0';

	p = fqn;
	for (i = 0; i < count; i++)
		p = stpcpy(p, list[i]);

	return fqn;
}

static void update_fqn(struct die *cache, Dwarf_Die *die)
{
	if (!cache->fqn)
		cache->fqn = get_fqn(die) ?: "";
}

static void process_fqn(struct die *cache, Dwarf_Die *die)
{
	update_fqn(cache, die);
	if (*cache->fqn)
		process(cache, " ");
	process(cache, cache->fqn);
}

#define DEFINE_PROCESS_UDATA_ATTRIBUTE(attribute)                          \
	static void process_##attribute##_attr(struct die *cache,          \
					       Dwarf_Die *die)             \
	{                                                                  \
		Dwarf_Word value;                                          \
		if (get_udata_attr(die, DW_AT_##attribute, &value))        \
			process_fmt(cache, " " #attribute "(%" PRIu64 ")", \
				    value);                                \
	}

DEFINE_PROCESS_UDATA_ATTRIBUTE(alignment)
DEFINE_PROCESS_UDATA_ATTRIBUTE(byte_size)
DEFINE_PROCESS_UDATA_ATTRIBUTE(encoding)

bool match_all(Dwarf_Die *die)
{
	return true;
}

int process_die_container(struct state *state, struct die *cache,
			  Dwarf_Die *die, die_callback_t func,
			  die_match_callback_t match)
{
	Dwarf_Die current;
	int res;

	res = checkp(dwarf_child(die, &current));
	while (!res) {
		if (match(&current)) {
			/* <0 = error, 0 = continue, >0 = stop */
			res = checkp(func(state, cache, &current));
			if (res)
				return res;
		}

		res = checkp(dwarf_siblingof(&current, &current));
	}

	return 0;
}

static int process_type(struct state *state, struct die *parent,
			Dwarf_Die *die);

static void process_type_attr(struct state *state, struct die *cache,
			      Dwarf_Die *die)
{
	Dwarf_Die type;

	if (get_ref_die_attr(die, DW_AT_type, &type)) {
		check(process_type(state, cache, &type));
		return;
	}

	/* Compilers can omit DW_AT_type -- print out 'void' to clarify */
	process(cache, "base_type void");
}

static void process_base_type(struct state *state, struct die *cache,
			      Dwarf_Die *die)
{
	process(cache, "base_type");
	process_fqn(cache, die);
	process_byte_size_attr(cache, die);
	process_encoding_attr(cache, die);
	process_alignment_attr(cache, die);
}

static void process_cached(struct state *state, struct die *cache,
			   Dwarf_Die *die)
{
	struct die_fragment *df;
	Dwarf_Die child;

	list_for_each_entry(df, &cache->fragments, list) {
		switch (df->type) {
		case FRAGMENT_STRING:
			process(NULL, df->data.str);
			break;
		case FRAGMENT_DIE:
			if (!dwarf_die_addr_die(dwarf_cu_getdwarf(die->cu),
						(void *)df->data.addr, &child))
				error("dwarf_die_addr_die failed");
			check(process_type(state, NULL, &child));
			break;
		default:
			error("empty die_fragment");
		}
	}
}

#define PROCESS_TYPE(type)                                \
	case DW_TAG_##type##_type:                        \
		process_##type##_type(state, cache, die); \
		break;

static int process_type(struct state *state, struct die *parent, Dwarf_Die *die)
{
	struct die *cache;
	int tag = dwarf_tag(die);

	/*
	 * If we have the DIE already cached, use it instead of walking
	 * through DWARF.
	 */
	cache = die_map_get(die, DIE_COMPLETE);

	if (cache->state == DIE_COMPLETE) {
		process_cached(state, cache, die);
		die_map_add_die(parent, cache);
		return 0;
	}

	switch (tag) {
	PROCESS_TYPE(base)
	default:
		debug("unimplemented type: %x", tag);
		break;
	}

	/* Update cache state and append to the parent (if any) */
	cache->tag = tag;
	cache->state = DIE_COMPLETE;
	die_map_add_die(parent, cache);

	return 0;
}

/*
 * Exported symbol processing
 */
static void process_symbol(struct state *state, Dwarf_Die *die,
			   die_callback_t process_func)
{
	debug("%s", state->sym->name);
	check(process_func(state, NULL, die));
	if (dump_dies)
		fputs("\n", stderr);
}

static int __process_subprogram(struct state *state, struct die *cache,
				Dwarf_Die *die)
{
	process(cache, "subprogram");
	return 0;
}

static void process_subprogram(struct state *state, Dwarf_Die *die)
{
	process_symbol(state, die, __process_subprogram);
}

static int __process_variable(struct state *state, struct die *cache,
			      Dwarf_Die *die)
{
	process(cache, "variable ");
	process_type_attr(state, cache, die);
	return 0;
}

static void process_variable(struct state *state, Dwarf_Die *die)
{
	process_symbol(state, die, __process_variable);
}

static int process_exported_symbols(struct state *unused, struct die *cache,
				    Dwarf_Die *die)
{
	int tag = dwarf_tag(die);

	switch (tag) {
	/* Possible containers of exported symbols */
	case DW_TAG_namespace:
	case DW_TAG_class_type:
	case DW_TAG_structure_type:
		return check(process_die_container(
			NULL, cache, die, process_exported_symbols, match_all));

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
	check(process_die_container(NULL, NULL, cudie, process_exported_symbols,
				    match_all));
}
