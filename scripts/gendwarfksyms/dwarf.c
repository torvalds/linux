// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Google LLC
 */

#define _GNU_SOURCE
#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include "gendwarfksyms.h"

/* See get_union_kabi_status */
#define KABI_PREFIX "__kabi_"
#define KABI_PREFIX_LEN (sizeof(KABI_PREFIX) - 1)
#define KABI_RESERVED_PREFIX "reserved"
#define KABI_RESERVED_PREFIX_LEN (sizeof(KABI_RESERVED_PREFIX) - 1)
#define KABI_RENAMED_PREFIX "renamed"
#define KABI_RENAMED_PREFIX_LEN (sizeof(KABI_RENAMED_PREFIX) - 1)
#define KABI_IGNORED_PREFIX "ignored"
#define KABI_IGNORED_PREFIX_LEN (sizeof(KABI_IGNORED_PREFIX) - 1)

static inline bool is_kabi_prefix(const char *name)
{
	return name && !strncmp(name, KABI_PREFIX, KABI_PREFIX_LEN);
}

enum kabi_status {
	/* >0 to stop DIE processing */
	KABI_NORMAL = 1,
	KABI_RESERVED,
	KABI_IGNORED,
};

static bool do_linebreak;
static int indentation_level;

/* Line breaks and indentation for pretty-printing */
static void process_linebreak(struct die *cache, int n)
{
	indentation_level += n;
	do_linebreak = true;
	die_map_add_linebreak(cache, n);
}

#define DEFINE_GET_ATTR(attr, type)                                    \
	static bool get_##attr##_attr(Dwarf_Die *die, unsigned int id, \
				      type *value)                     \
	{                                                              \
		Dwarf_Attribute da;                                    \
		return dwarf_attr(die, id, &da) &&                     \
		       !dwarf_form##attr(&da, value);                  \
	}

DEFINE_GET_ATTR(flag, bool)
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

/* DW_AT_decl_file -> struct srcfile */
static struct cache srcfile_cache;

static bool is_definition_private(Dwarf_Die *die)
{
	Dwarf_Word filenum;
	Dwarf_Files *files;
	Dwarf_Die cudie;
	const char *s;
	int res;

	/*
	 * Definitions in .c files cannot change the public ABI,
	 * so consider them private.
	 */
	if (!get_udata_attr(die, DW_AT_decl_file, &filenum))
		return false;

	res = cache_get(&srcfile_cache, filenum);
	if (res >= 0)
		return !!res;

	if (!dwarf_cu_die(die->cu, &cudie, NULL, NULL, NULL, NULL, NULL, NULL))
		error("dwarf_cu_die failed: '%s'", dwarf_errmsg(-1));

	if (dwarf_getsrcfiles(&cudie, &files, NULL))
		error("dwarf_getsrcfiles failed: '%s'", dwarf_errmsg(-1));

	s = dwarf_filesrc(files, filenum, NULL, NULL);
	if (!s)
		error("dwarf_filesrc failed: '%s'", dwarf_errmsg(-1));

	s = strrchr(s, '.');
	res = s && !strcmp(s, ".c");
	cache_set(&srcfile_cache, filenum, res);

	return !!res;
}

static bool is_kabi_definition(struct die *cache, Dwarf_Die *die)
{
	bool value;

	if (get_flag_attr(die, DW_AT_declaration, &value) && value)
		return false;

	if (kabi_is_declonly(cache->fqn))
		return false;

	return !is_definition_private(die);
}

/*
 * Type string processing
 */
static void process(struct die *cache, const char *s)
{
	s = s ?: "<null>";

	if (dump_dies && do_linebreak) {
		fputs("\n", stderr);
		for (int i = 0; i < indentation_level; i++)
			fputs("  ", stderr);
		do_linebreak = false;
	}
	if (dump_dies)
		fputs(s, stderr);

	if (cache)
		die_debug_r("cache %p string '%s'", cache, s);
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

static void update_fqn(struct die *cache, Dwarf_Die *die)
{
	struct die *fqn;

	if (!cache->fqn) {
		if (!__die_map_get((uintptr_t)die->addr, DIE_FQN, &fqn) &&
		    *fqn->fqn)
			cache->fqn = xstrdup(fqn->fqn);
		else
			cache->fqn = "";
	}
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

DEFINE_PROCESS_UDATA_ATTRIBUTE(accessibility)
DEFINE_PROCESS_UDATA_ATTRIBUTE(alignment)
DEFINE_PROCESS_UDATA_ATTRIBUTE(bit_size)
DEFINE_PROCESS_UDATA_ATTRIBUTE(encoding)
DEFINE_PROCESS_UDATA_ATTRIBUTE(data_bit_offset)
DEFINE_PROCESS_UDATA_ATTRIBUTE(data_member_location)
DEFINE_PROCESS_UDATA_ATTRIBUTE(discr_value)

static void process_byte_size_attr(struct die *cache, Dwarf_Die *die)
{
	Dwarf_Word value;
	unsigned long override;

	if (get_udata_attr(die, DW_AT_byte_size, &value)) {
		if (stable && kabi_get_byte_size(cache->fqn, &override))
			value = override;

		process_fmt(cache, " byte_size(%" PRIu64 ")", value);
	}
}

/* Match functions -- die_match_callback_t */
#define DEFINE_MATCH(type)                                     \
	static bool match_##type##_type(Dwarf_Die *die)        \
	{                                                      \
		return dwarf_tag(die) == DW_TAG_##type##_type; \
	}

DEFINE_MATCH(enumerator)
DEFINE_MATCH(formal_parameter)
DEFINE_MATCH(member)
DEFINE_MATCH(subrange)

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

	/* Track the first item in lists. */
	if (state)
		state->first_list_item = true;

	res = checkp(dwarf_child(die, &current));
	while (!res) {
		if (match(&current)) {
			/* <0 = error, 0 = continue, >0 = stop */
			res = checkp(func(state, cache, &current));
			if (res)
				goto out;
		}

		res = checkp(dwarf_siblingof(&current, &current));
	}

	res = 0;
out:
	if (state)
		state->first_list_item = false;

	return res;
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

static void process_list_comma(struct state *state, struct die *cache)
{
	if (state->first_list_item) {
		state->first_list_item = false;
	} else {
		process(cache, " ,");
		process_linebreak(cache, 0);
	}
}

/* Comma-separated with DW_AT_type */
static void __process_list_type(struct state *state, struct die *cache,
				Dwarf_Die *die, const char *type)
{
	const char *name = get_name_attr(die);

	if (stable) {
		if (is_kabi_prefix(name))
			name = NULL;
		state->kabi.orig_name = NULL;
	}

	process_list_comma(state, cache);
	process(cache, type);
	process_type_attr(state, cache, die);

	if (stable && state->kabi.orig_name)
		name = state->kabi.orig_name;
	if (name) {
		process(cache, " ");
		process(cache, name);
	}

	process_accessibility_attr(cache, die);
	process_bit_size_attr(cache, die);
	process_data_bit_offset_attr(cache, die);
	process_data_member_location_attr(cache, die);
}

#define DEFINE_PROCESS_LIST_TYPE(type)                                       \
	static void process_##type##_type(struct state *state,               \
					  struct die *cache, Dwarf_Die *die) \
	{                                                                    \
		__process_list_type(state, cache, die, #type " ");           \
	}

DEFINE_PROCESS_LIST_TYPE(formal_parameter)
DEFINE_PROCESS_LIST_TYPE(member)

/* Container types with DW_AT_type */
static void __process_type(struct state *state, struct die *cache,
			   Dwarf_Die *die, const char *type)
{
	process(cache, type);
	process_fqn(cache, die);
	process(cache, " {");
	process_linebreak(cache, 1);
	process_type_attr(state, cache, die);
	process_linebreak(cache, -1);
	process(cache, "}");
	process_byte_size_attr(cache, die);
	process_alignment_attr(cache, die);
}

#define DEFINE_PROCESS_TYPE(type)                                            \
	static void process_##type##_type(struct state *state,               \
					  struct die *cache, Dwarf_Die *die) \
	{                                                                    \
		__process_type(state, cache, die, #type "_type");            \
	}

DEFINE_PROCESS_TYPE(atomic)
DEFINE_PROCESS_TYPE(const)
DEFINE_PROCESS_TYPE(immutable)
DEFINE_PROCESS_TYPE(packed)
DEFINE_PROCESS_TYPE(pointer)
DEFINE_PROCESS_TYPE(reference)
DEFINE_PROCESS_TYPE(restrict)
DEFINE_PROCESS_TYPE(rvalue_reference)
DEFINE_PROCESS_TYPE(shared)
DEFINE_PROCESS_TYPE(template_type_parameter)
DEFINE_PROCESS_TYPE(volatile)
DEFINE_PROCESS_TYPE(typedef)

static void process_subrange_type(struct state *state, struct die *cache,
				  Dwarf_Die *die)
{
	Dwarf_Word count = 0;

	if (get_udata_attr(die, DW_AT_count, &count))
		process_fmt(cache, "[%" PRIu64 "]", count);
	else if (get_udata_attr(die, DW_AT_upper_bound, &count))
		process_fmt(cache, "[%" PRIu64 "]", count + 1);
	else
		process(cache, "[]");
}

static void process_array_type(struct state *state, struct die *cache,
			       Dwarf_Die *die)
{
	process(cache, "array_type");
	/* Array size */
	check(process_die_container(state, cache, die, process_type,
				    match_subrange_type));
	process(cache, " {");
	process_linebreak(cache, 1);
	process_type_attr(state, cache, die);
	process_linebreak(cache, -1);
	process(cache, "}");
}

static void __process_subroutine_type(struct state *state, struct die *cache,
				      Dwarf_Die *die, const char *type)
{
	process(cache, type);
	process(cache, " (");
	process_linebreak(cache, 1);
	/* Parameters */
	check(process_die_container(state, cache, die, process_type,
				    match_formal_parameter_type));
	process_linebreak(cache, -1);
	process(cache, ")");
	process_linebreak(cache, 0);
	/* Return type */
	process(cache, "-> ");
	process_type_attr(state, cache, die);
}

static void process_subroutine_type(struct state *state, struct die *cache,
				    Dwarf_Die *die)
{
	__process_subroutine_type(state, cache, die, "subroutine_type");
}

static void process_variant_type(struct state *state, struct die *cache,
				 Dwarf_Die *die)
{
	process_list_comma(state, cache);
	process(cache, "variant {");
	process_linebreak(cache, 1);
	check(process_die_container(state, cache, die, process_type,
				    match_member_type));
	process_linebreak(cache, -1);
	process(cache, "}");
	process_discr_value_attr(cache, die);
}

static void process_variant_part_type(struct state *state, struct die *cache,
				      Dwarf_Die *die)
{
	process_list_comma(state, cache);
	process(cache, "variant_part {");
	process_linebreak(cache, 1);
	check(process_die_container(state, cache, die, process_type,
				    match_all));
	process_linebreak(cache, -1);
	process(cache, "}");
}

static int get_kabi_status(Dwarf_Die *die, const char **suffix)
{
	const char *name = get_name_attr(die);

	if (suffix)
		*suffix = NULL;

	if (is_kabi_prefix(name)) {
		name += KABI_PREFIX_LEN;

		if (!strncmp(name, KABI_RESERVED_PREFIX,
			     KABI_RESERVED_PREFIX_LEN))
			return KABI_RESERVED;
		if (!strncmp(name, KABI_IGNORED_PREFIX,
			     KABI_IGNORED_PREFIX_LEN))
			return KABI_IGNORED;

		if (!strncmp(name, KABI_RENAMED_PREFIX,
			     KABI_RENAMED_PREFIX_LEN)) {
			if (suffix) {
				name += KABI_RENAMED_PREFIX_LEN;
				*suffix = name;
			}
			return KABI_RESERVED;
		}
	}

	return KABI_NORMAL;
}

static int check_struct_member_kabi_status(struct state *state,
					   struct die *__unused, Dwarf_Die *die)
{
	int res;

	assert(dwarf_tag(die) == DW_TAG_member_type);

	/*
	 * If the union member is a struct, expect the __kabi field to
	 * be the first member of the structure, i.e..:
	 *
	 * union {
	 * 	type new_member;
	 * 	struct {
	 * 		type __kabi_field;
	 * 	}
	 * };
	 */
	res = get_kabi_status(die, &state->kabi.orig_name);

	if (res == KABI_RESERVED &&
	    !get_ref_die_attr(die, DW_AT_type, &state->kabi.placeholder))
		error("structure member missing a type?");

	return res;
}

static int check_union_member_kabi_status(struct state *state,
					  struct die *__unused, Dwarf_Die *die)
{
	Dwarf_Die type;
	int res;

	assert(dwarf_tag(die) == DW_TAG_member_type);

	if (!get_ref_die_attr(die, DW_AT_type, &type))
		error("union member missing a type?");

	/*
	 * We expect a union with two members. Check if either of them
	 * has a __kabi name prefix, i.e.:
	 *
	 * union {
	 * 	...
	 * 	type memberN; // <- type, N = {0,1}
	 *	...
	 * };
	 *
	 * The member can also be a structure type, in which case we'll
	 * check the first structure member.
	 *
	 * In any case, stop processing after we've seen two members.
	 */
	res = get_kabi_status(die, &state->kabi.orig_name);

	if (res == KABI_RESERVED)
		state->kabi.placeholder = type;
	if (res != KABI_NORMAL)
		return res;

	if (dwarf_tag(&type) == DW_TAG_structure_type)
		res = checkp(process_die_container(
			state, NULL, &type, check_struct_member_kabi_status,
			match_member_type));

	if (res <= KABI_NORMAL && ++state->kabi.members < 2)
		return 0; /* Continue */

	return res;
}

static int get_union_kabi_status(Dwarf_Die *die, Dwarf_Die *placeholder,
				 const char **orig_name)
{
	struct state state;
	int res;

	if (!stable)
		return KABI_NORMAL;

	/*
	 * To maintain a stable kABI, distributions may choose to reserve
	 * space in structs for later use by adding placeholder members,
	 * for example:
	 *
	 * struct s {
	 * 	u32 a;
	 *	// an 8-byte placeholder for future use
	 * 	u64 __kabi_reserved_0;
	 * };
	 *
	 * When the reserved member is taken into use, the type change
	 * would normally cause the symbol version to change as well, but
	 * if the replacement uses the following convention, gendwarfksyms
	 * continues to use the placeholder type for versioning instead,
	 * thus maintaining the same symbol version:
	 *
	 * struct s {
	 * 	u32 a;
	 *	union {
	 * 		// placeholder replaced with a new member `b`
	 * 		struct t b;
	 * 		struct {
	 * 			// the placeholder type that is still
	 *			// used for versioning
	 * 			u64 __kabi_reserved_0;
	 * 		};
	 * 	};
	 * };
	 *
	 * I.e., as long as the replaced member is in a union, and the
	 * placeholder has a __kabi_reserved name prefix, we'll continue
	 * to use the placeholder type (here u64) for version calculation
	 * instead of the union type.
	 *
	 * It's also possible to ignore new members from versioning if
	 * they've been added to alignment holes, for example, by
	 * including them in a union with another member that uses the
	 * __kabi_ignored name prefix:
	 *
	 * struct s {
	 * 	u32 a;
	 *	// an alignment hole is used to add `n`
	 * 	union {
	 * 		u32 n;
	 *		// hide the entire union member from versioning
	 * 		u8 __kabi_ignored_0;
	 * 	};
	 * 	u64 b;
	 * };
	 *
	 * Note that the user of this feature is responsible for ensuring
	 * that the structure actually remains ABI compatible.
	 */
	memset(&state.kabi, 0, sizeof(state.kabi));

	res = checkp(process_die_container(&state, NULL, die,
					   check_union_member_kabi_status,
					   match_member_type));

	if (res == KABI_RESERVED) {
		if (placeholder)
			*placeholder = state.kabi.placeholder;
		if (orig_name)
			*orig_name = state.kabi.orig_name;
	}

	return res;
}

static bool is_kabi_ignored(Dwarf_Die *die)
{
	Dwarf_Die type;

	if (!stable)
		return false;

	if (!get_ref_die_attr(die, DW_AT_type, &type))
		error("member missing a type?");

	return dwarf_tag(&type) == DW_TAG_union_type &&
	       checkp(get_union_kabi_status(&type, NULL, NULL)) == KABI_IGNORED;
}

static int ___process_structure_type(struct state *state, struct die *cache,
				     Dwarf_Die *die)
{
	switch (dwarf_tag(die)) {
	case DW_TAG_member:
		if (is_kabi_ignored(die))
			return 0;
		return check(process_type(state, cache, die));
	case DW_TAG_variant_part:
		return check(process_type(state, cache, die));
	case DW_TAG_class_type:
	case DW_TAG_enumeration_type:
	case DW_TAG_structure_type:
	case DW_TAG_template_type_parameter:
	case DW_TAG_union_type:
	case DW_TAG_subprogram:
		/* Skip non-member types, including member functions */
		return 0;
	default:
		error("unexpected structure_type child: %x", dwarf_tag(die));
	}
}

static void __process_structure_type(struct state *state, struct die *cache,
				     Dwarf_Die *die, const char *type,
				     die_callback_t process_func,
				     die_match_callback_t match_func)
{
	bool expand;

	process(cache, type);
	process_fqn(cache, die);
	process(cache, " {");
	process_linebreak(cache, 1);

	expand = state->expand.expand && is_kabi_definition(cache, die);

	if (expand) {
		state->expand.current_fqn = cache->fqn;
		check(process_die_container(state, cache, die, process_func,
					    match_func));
	}

	process_linebreak(cache, -1);
	process(cache, "}");

	if (expand) {
		process_byte_size_attr(cache, die);
		process_alignment_attr(cache, die);
	}
}

#define DEFINE_PROCESS_STRUCTURE_TYPE(structure)                        \
	static void process_##structure##_type(                         \
		struct state *state, struct die *cache, Dwarf_Die *die) \
	{                                                               \
		__process_structure_type(state, cache, die,             \
					 #structure "_type",            \
					 ___process_structure_type,     \
					 match_all);                    \
	}

DEFINE_PROCESS_STRUCTURE_TYPE(class)
DEFINE_PROCESS_STRUCTURE_TYPE(structure)

static void process_union_type(struct state *state, struct die *cache,
			       Dwarf_Die *die)
{
	Dwarf_Die placeholder;

	int res = checkp(get_union_kabi_status(die, &placeholder,
					       &state->kabi.orig_name));

	if (res == KABI_RESERVED)
		check(process_type(state, cache, &placeholder));
	if (res > KABI_NORMAL)
		return;

	__process_structure_type(state, cache, die, "union_type",
				 ___process_structure_type, match_all);
}

static void process_enumerator_type(struct state *state, struct die *cache,
				    Dwarf_Die *die)
{
	bool overridden = false;
	Dwarf_Word value;

	if (stable) {
		/* Get the fqn before we process anything */
		update_fqn(cache, die);

		if (kabi_is_enumerator_ignored(state->expand.current_fqn,
					       cache->fqn))
			return;

		overridden = kabi_get_enumerator_value(
			state->expand.current_fqn, cache->fqn, &value);
	}

	process_list_comma(state, cache);
	process(cache, "enumerator");
	process_fqn(cache, die);

	if (overridden || get_udata_attr(die, DW_AT_const_value, &value)) {
		process(cache, " = ");
		process_fmt(cache, "%" PRIu64, value);
	}
}

static void process_enumeration_type(struct state *state, struct die *cache,
				     Dwarf_Die *die)
{
	__process_structure_type(state, cache, die, "enumeration_type",
				 process_type, match_enumerator_type);
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

static void process_unspecified_type(struct state *state, struct die *cache,
				     Dwarf_Die *die)
{
	/*
	 * These can be emitted for stand-alone assembly code, which means we
	 * might run into them in vmlinux.o.
	 */
	process(cache, "unspecified_type");
}

static void process_cached(struct state *state, struct die *cache,
			   Dwarf_Die *die)
{
	struct die_fragment *df;
	Dwarf_Die child;

	list_for_each_entry(df, &cache->fragments, list) {
		switch (df->type) {
		case FRAGMENT_STRING:
			die_debug_b("cache %p STRING '%s'", cache,
				    df->data.str);
			process(NULL, df->data.str);
			break;
		case FRAGMENT_LINEBREAK:
			process_linebreak(NULL, df->data.linebreak);
			break;
		case FRAGMENT_DIE:
			if (!dwarf_die_addr_die(dwarf_cu_getdwarf(die->cu),
						(void *)df->data.addr, &child))
				error("dwarf_die_addr_die failed");
			die_debug_b("cache %p DIE addr %" PRIxPTR " tag %x",
				    cache, df->data.addr, dwarf_tag(&child));
			check(process_type(state, NULL, &child));
			break;
		default:
			error("empty die_fragment");
		}
	}
}

static void state_init(struct state *state)
{
	state->expand.expand = true;
	state->expand.current_fqn = NULL;
	cache_init(&state->expansion_cache);
}

static void expansion_state_restore(struct expansion_state *state,
				    struct expansion_state *saved)
{
	state->expand = saved->expand;
	state->current_fqn = saved->current_fqn;
}

static void expansion_state_save(struct expansion_state *state,
				 struct expansion_state *saved)
{
	expansion_state_restore(saved, state);
}

static bool is_expanded_type(int tag)
{
	return tag == DW_TAG_class_type || tag == DW_TAG_structure_type ||
	       tag == DW_TAG_union_type || tag == DW_TAG_enumeration_type;
}

#define PROCESS_TYPE(type)                                \
	case DW_TAG_##type##_type:                        \
		process_##type##_type(state, cache, die); \
		break;

static int process_type(struct state *state, struct die *parent, Dwarf_Die *die)
{
	enum die_state want_state = DIE_COMPLETE;
	struct die *cache;
	struct expansion_state saved;
	int tag = dwarf_tag(die);

	expansion_state_save(&state->expand, &saved);

	/*
	 * Structures and enumeration types are expanded only once per
	 * exported symbol. This is sufficient for detecting ABI changes
	 * within the structure.
	 */
	if (is_expanded_type(tag)) {
		if (cache_was_expanded(&state->expansion_cache, die->addr))
			state->expand.expand = false;

		if (state->expand.expand)
			cache_mark_expanded(&state->expansion_cache, die->addr);
		else
			want_state = DIE_UNEXPANDED;
	}

	/*
	 * If we have want_state already cached, use it instead of walking
	 * through DWARF.
	 */
	cache = die_map_get(die, want_state);

	if (cache->state == want_state) {
		die_debug_g("cached addr %p tag %x -- %s", die->addr, tag,
			    die_state_name(cache->state));

		process_cached(state, cache, die);
		die_map_add_die(parent, cache);

		expansion_state_restore(&state->expand, &saved);
		return 0;
	}

	die_debug_g("addr %p tag %x -- %s -> %s", die->addr, tag,
		    die_state_name(cache->state), die_state_name(want_state));

	switch (tag) {
	/* Type modifiers */
	PROCESS_TYPE(atomic)
	PROCESS_TYPE(const)
	PROCESS_TYPE(immutable)
	PROCESS_TYPE(packed)
	PROCESS_TYPE(pointer)
	PROCESS_TYPE(reference)
	PROCESS_TYPE(restrict)
	PROCESS_TYPE(rvalue_reference)
	PROCESS_TYPE(shared)
	PROCESS_TYPE(volatile)
	/* Container types */
	PROCESS_TYPE(class)
	PROCESS_TYPE(structure)
	PROCESS_TYPE(union)
	PROCESS_TYPE(enumeration)
	/* Subtypes */
	PROCESS_TYPE(enumerator)
	PROCESS_TYPE(formal_parameter)
	PROCESS_TYPE(member)
	PROCESS_TYPE(subrange)
	PROCESS_TYPE(template_type_parameter)
	PROCESS_TYPE(variant)
	PROCESS_TYPE(variant_part)
	/* Other types */
	PROCESS_TYPE(array)
	PROCESS_TYPE(base)
	PROCESS_TYPE(subroutine)
	PROCESS_TYPE(typedef)
	PROCESS_TYPE(unspecified)
	default:
		error("unexpected type: %x", tag);
	}

	die_debug_r("parent %p cache %p die addr %p tag %x", parent, cache,
		    die->addr, tag);

	/* Update cache state and append to the parent (if any) */
	cache->tag = tag;
	cache->state = want_state;
	die_map_add_die(parent, cache);

	expansion_state_restore(&state->expand, &saved);
	return 0;
}

/*
 * Exported symbol processing
 */
static struct die *get_symbol_cache(struct state *state, Dwarf_Die *die)
{
	struct die *cache;

	cache = die_map_get(die, DIE_SYMBOL);

	if (cache->state != DIE_INCOMPLETE)
		return NULL; /* We already processed a symbol for this DIE */

	cache->tag = dwarf_tag(die);
	return cache;
}

static void process_symbol(struct state *state, Dwarf_Die *die,
			   die_callback_t process_func)
{
	struct die *cache;

	symbol_set_die(state->sym, die);

	cache = get_symbol_cache(state, die);
	if (!cache)
		return;

	debug("%s", state->sym->name);
	check(process_func(state, cache, die));
	cache->state = DIE_SYMBOL;
	if (dump_dies)
		fputs("\n", stderr);
}

static int __process_subprogram(struct state *state, struct die *cache,
				Dwarf_Die *die)
{
	__process_subroutine_type(state, cache, die, "subprogram");
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

static void save_symbol_ptr(struct state *state)
{
	Dwarf_Die ptr_type;
	Dwarf_Die type;

	if (!get_ref_die_attr(&state->die, DW_AT_type, &ptr_type) ||
	    dwarf_tag(&ptr_type) != DW_TAG_pointer_type)
		error("%s must be a pointer type!",
		      get_symbol_name(&state->die));

	if (!get_ref_die_attr(&ptr_type, DW_AT_type, &type))
		error("%s pointer missing a type attribute?",
		      get_symbol_name(&state->die));

	/*
	 * Save the symbol pointer DIE in case the actual symbol is
	 * missing from the DWARF. Clang, for example, intentionally
	 * omits external symbols from the debugging information.
	 */
	if (dwarf_tag(&type) == DW_TAG_subroutine_type)
		symbol_set_ptr(state->sym, &type);
	else
		symbol_set_ptr(state->sym, &ptr_type);
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

		state_init(&state);

		if (is_symbol_ptr(get_symbol_name(&state.die)))
			save_symbol_ptr(&state);
		else if (tag == DW_TAG_subprogram)
			process_subprogram(&state, &state.die);
		else
			process_variable(&state, &state.die);

		cache_free(&state.expansion_cache);
		return 0;
	}
	default:
		return 0;
	}
}

static void process_symbol_ptr(struct symbol *sym, void *arg)
{
	struct state state;
	Dwarf *dwarf = arg;

	if (sym->state != SYMBOL_UNPROCESSED || !sym->ptr_die_addr)
		return;

	debug("%s", sym->name);
	state_init(&state);
	state.sym = sym;

	if (!dwarf_die_addr_die(dwarf, (void *)sym->ptr_die_addr, &state.die))
		error("dwarf_die_addr_die failed for symbol ptr: '%s'",
		      sym->name);

	if (dwarf_tag(&state.die) == DW_TAG_subroutine_type)
		process_subprogram(&state, &state.die);
	else
		process_variable(&state, &state.die);

	cache_free(&state.expansion_cache);
}

static int resolve_fqns(struct state *parent, struct die *unused,
			Dwarf_Die *die)
{
	struct state state;
	struct die *cache;
	const char *name;
	bool use_prefix;
	char *prefix = NULL;
	char *fqn = "";
	int tag;

	if (!__die_map_get((uintptr_t)die->addr, DIE_FQN, &cache))
		return 0;

	tag = dwarf_tag(die);

	/*
	 * Only namespaces and structures need to pass a prefix to the next
	 * scope.
	 */
	use_prefix = tag == DW_TAG_namespace || tag == DW_TAG_class_type ||
		     tag == DW_TAG_structure_type;

	state.expand.current_fqn = NULL;
	name = get_name_attr(die);

	if (parent && parent->expand.current_fqn && (use_prefix || name)) {
		/*
		 * The fqn for the current DIE, and if needed, a prefix for the
		 * next scope.
		 */
		if (asprintf(&prefix, "%s::%s", parent->expand.current_fqn,
			     name ? name : "<anonymous>") < 0)
			error("asprintf failed");

		if (use_prefix)
			state.expand.current_fqn = prefix;

		/*
		 * Use fqn only if the DIE has a name. Otherwise fqn will
		 * remain empty.
		 */
		if (name) {
			fqn = prefix;
			/* prefix will be freed by die_map. */
			prefix = NULL;
		}
	} else if (name) {
		/* No prefix from the previous scope. Use only the name. */
		fqn = xstrdup(name);

		if (use_prefix)
			state.expand.current_fqn = fqn;
	}

	/* If the DIE has a non-empty name, cache it. */
	if (*fqn) {
		cache = die_map_get(die, DIE_FQN);
		/* Move ownership of fqn to die_map. */
		cache->fqn = fqn;
		cache->state = DIE_FQN;
	}

	check(process_die_container(&state, NULL, die, resolve_fqns,
				    match_all));

	free(prefix);
	return 0;
}

void process_cu(Dwarf_Die *cudie)
{
	check(process_die_container(NULL, NULL, cudie, resolve_fqns,
				    match_all));

	check(process_die_container(NULL, NULL, cudie, process_exported_symbols,
				    match_all));

	symbol_for_each(process_symbol_ptr, dwarf_cu_getdwarf(cudie->cu));

	cache_free(&srcfile_cache);
}
