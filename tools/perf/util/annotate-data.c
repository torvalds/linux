/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Convert sample address to data type using DWARF debug info.
 *
 * Written by Namhyung Kim <namhyung@kernel.org>
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "annotate-data.h"
#include "debuginfo.h"
#include "debug.h"
#include "dso.h"
#include "map.h"
#include "map_symbol.h"
#include "strbuf.h"
#include "symbol.h"

/*
 * Compare type name and size to maintain them in a tree.
 * I'm not sure if DWARF would have information of a single type in many
 * different places (compilation units).  If not, it could compare the
 * offset of the type entry in the .debug_info section.
 */
static int data_type_cmp(const void *_key, const struct rb_node *node)
{
	const struct annotated_data_type *key = _key;
	struct annotated_data_type *type;

	type = rb_entry(node, struct annotated_data_type, node);

	if (key->type_size != type->type_size)
		return key->type_size - type->type_size;
	return strcmp(key->type_name, type->type_name);
}

static bool data_type_less(struct rb_node *node_a, const struct rb_node *node_b)
{
	struct annotated_data_type *a, *b;

	a = rb_entry(node_a, struct annotated_data_type, node);
	b = rb_entry(node_b, struct annotated_data_type, node);

	if (a->type_size != b->type_size)
		return a->type_size < b->type_size;
	return strcmp(a->type_name, b->type_name) < 0;
}

static struct annotated_data_type *dso__findnew_data_type(struct dso *dso,
							  Dwarf_Die *type_die)
{
	struct annotated_data_type *result = NULL;
	struct annotated_data_type key;
	struct rb_node *node;
	struct strbuf sb;
	char *type_name;
	Dwarf_Word size;

	strbuf_init(&sb, 32);
	if (die_get_typename_from_type(type_die, &sb) < 0)
		strbuf_add(&sb, "(unknown type)", 14);
	type_name = strbuf_detach(&sb, NULL);
	dwarf_aggregate_size(type_die, &size);

	/* Check existing nodes in dso->data_types tree */
	key.type_name = type_name;
	key.type_size = size;
	node = rb_find(&key, &dso->data_types, data_type_cmp);
	if (node) {
		result = rb_entry(node, struct annotated_data_type, node);
		free(type_name);
		return result;
	}

	/* If not, add a new one */
	result = zalloc(sizeof(*result));
	if (result == NULL) {
		free(type_name);
		return NULL;
	}

	result->type_name = type_name;
	result->type_size = size;

	rb_add(&result->node, &dso->data_types, data_type_less);
	return result;
}

static bool find_cu_die(struct debuginfo *di, u64 pc, Dwarf_Die *cu_die)
{
	Dwarf_Off off, next_off;
	size_t header_size;

	if (dwarf_addrdie(di->dbg, pc, cu_die) != NULL)
		return cu_die;

	/*
	 * There are some kernels don't have full aranges and contain only a few
	 * aranges entries.  Fallback to iterate all CU entries in .debug_info
	 * in case it's missing.
	 */
	off = 0;
	while (dwarf_nextcu(di->dbg, off, &next_off, &header_size,
			    NULL, NULL, NULL) == 0) {
		if (dwarf_offdie(di->dbg, off + header_size, cu_die) &&
		    dwarf_haspc(cu_die, pc))
			return true;

		off = next_off;
	}
	return false;
}

/* The type info will be saved in @type_die */
static int check_variable(Dwarf_Die *var_die, Dwarf_Die *type_die, int offset)
{
	Dwarf_Word size;

	/* Get the type of the variable */
	if (die_get_real_type(var_die, type_die) == NULL) {
		pr_debug("variable has no type\n");
		return -1;
	}

	/*
	 * It expects a pointer type for a memory access.
	 * Convert to a real type it points to.
	 */
	if (dwarf_tag(type_die) != DW_TAG_pointer_type ||
	    die_get_real_type(type_die, type_die) == NULL) {
		pr_debug("no pointer or no type\n");
		return -1;
	}

	/* Get the size of the actual type */
	if (dwarf_aggregate_size(type_die, &size) < 0) {
		pr_debug("type size is unknown\n");
		return -1;
	}

	/* Minimal sanity check */
	if ((unsigned)offset >= size) {
		pr_debug("offset: %d is bigger than size: %" PRIu64 "\n", offset, size);
		return -1;
	}

	return 0;
}

/* The result will be saved in @type_die */
static int find_data_type_die(struct debuginfo *di, u64 pc,
			      int reg, int offset, Dwarf_Die *type_die)
{
	Dwarf_Die cu_die, var_die;
	Dwarf_Die *scopes = NULL;
	int ret = -1;
	int i, nr_scopes;

	/* Get a compile_unit for this address */
	if (!find_cu_die(di, pc, &cu_die)) {
		pr_debug("cannot find CU for address %" PRIx64 "\n", pc);
		return -1;
	}

	/* Get a list of nested scopes - i.e. (inlined) functions and blocks. */
	nr_scopes = die_get_scopes(&cu_die, pc, &scopes);

	/* Search from the inner-most scope to the outer */
	for (i = nr_scopes - 1; i >= 0; i--) {
		/* Look up variables/parameters in this scope */
		if (!die_find_variable_by_reg(&scopes[i], pc, reg, &var_die))
			continue;

		/* Found a variable, see if it's correct */
		ret = check_variable(&var_die, type_die, offset);
		break;
	}

	free(scopes);
	return ret;
}

/**
 * find_data_type - Return a data type at the location
 * @ms: map and symbol at the location
 * @ip: instruction address of the memory access
 * @reg: register that holds the base address
 * @offset: offset from the base address
 *
 * This functions searches the debug information of the binary to get the data
 * type it accesses.  The exact location is expressed by (ip, reg, offset).
 * It return %NULL if not found.
 */
struct annotated_data_type *find_data_type(struct map_symbol *ms, u64 ip,
					   int reg, int offset)
{
	struct annotated_data_type *result = NULL;
	struct dso *dso = map__dso(ms->map);
	struct debuginfo *di;
	Dwarf_Die type_die;
	u64 pc;

	di = debuginfo__new(dso->long_name);
	if (di == NULL) {
		pr_debug("cannot get the debug info\n");
		return NULL;
	}

	/*
	 * IP is a relative instruction address from the start of the map, as
	 * it can be randomized/relocated, it needs to translate to PC which is
	 * a file address for DWARF processing.
	 */
	pc = map__rip_2objdump(ms->map, ip);
	if (find_data_type_die(di, pc, reg, offset, &type_die) < 0)
		goto out;

	result = dso__findnew_data_type(dso, &type_die);

out:
	debuginfo__delete(di);
	return result;
}

void annotated_data_type__tree_delete(struct rb_root *root)
{
	struct annotated_data_type *pos;

	while (!RB_EMPTY_ROOT(root)) {
		struct rb_node *node = rb_first(root);

		rb_erase(node, root);
		pos = rb_entry(node, struct annotated_data_type, node);
		free(pos->type_name);
		free(pos);
	}
}
