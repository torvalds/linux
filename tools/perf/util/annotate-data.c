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
#include "evsel.h"
#include "evlist.h"
#include "map.h"
#include "map_symbol.h"
#include "strbuf.h"
#include "symbol.h"
#include "symbol_conf.h"

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

	if (key->self.size != type->self.size)
		return key->self.size - type->self.size;
	return strcmp(key->self.type_name, type->self.type_name);
}

static bool data_type_less(struct rb_node *node_a, const struct rb_node *node_b)
{
	struct annotated_data_type *a, *b;

	a = rb_entry(node_a, struct annotated_data_type, node);
	b = rb_entry(node_b, struct annotated_data_type, node);

	if (a->self.size != b->self.size)
		return a->self.size < b->self.size;
	return strcmp(a->self.type_name, b->self.type_name) < 0;
}

/* Recursively add new members for struct/union */
static int __add_member_cb(Dwarf_Die *die, void *arg)
{
	struct annotated_member *parent = arg;
	struct annotated_member *member;
	Dwarf_Die member_type, die_mem;
	Dwarf_Word size, loc;
	Dwarf_Attribute attr;
	struct strbuf sb;
	int tag;

	if (dwarf_tag(die) != DW_TAG_member)
		return DIE_FIND_CB_SIBLING;

	member = zalloc(sizeof(*member));
	if (member == NULL)
		return DIE_FIND_CB_END;

	strbuf_init(&sb, 32);
	die_get_typename(die, &sb);

	die_get_real_type(die, &member_type);
	if (dwarf_aggregate_size(&member_type, &size) < 0)
		size = 0;

	if (!dwarf_attr_integrate(die, DW_AT_data_member_location, &attr))
		loc = 0;
	else
		dwarf_formudata(&attr, &loc);

	member->type_name = strbuf_detach(&sb, NULL);
	/* member->var_name can be NULL */
	if (dwarf_diename(die))
		member->var_name = strdup(dwarf_diename(die));
	member->size = size;
	member->offset = loc + parent->offset;
	INIT_LIST_HEAD(&member->children);
	list_add_tail(&member->node, &parent->children);

	tag = dwarf_tag(&member_type);
	switch (tag) {
	case DW_TAG_structure_type:
	case DW_TAG_union_type:
		die_find_child(&member_type, __add_member_cb, member, &die_mem);
		break;
	default:
		break;
	}
	return DIE_FIND_CB_SIBLING;
}

static void add_member_types(struct annotated_data_type *parent, Dwarf_Die *type)
{
	Dwarf_Die die_mem;

	die_find_child(type, __add_member_cb, &parent->self, &die_mem);
}

static void delete_members(struct annotated_member *member)
{
	struct annotated_member *child, *tmp;

	list_for_each_entry_safe(child, tmp, &member->children, node) {
		list_del(&child->node);
		delete_members(child);
		free(child->type_name);
		free(child->var_name);
		free(child);
	}
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
	key.self.type_name = type_name;
	key.self.size = size;
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

	result->self.type_name = type_name;
	result->self.size = size;
	INIT_LIST_HEAD(&result->self.children);

	if (symbol_conf.annotate_data_member)
		add_member_types(result, type_die);

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
		ann_data_stat.no_typeinfo++;
		return -1;
	}

	/*
	 * It expects a pointer type for a memory access.
	 * Convert to a real type it points to.
	 */
	if (dwarf_tag(type_die) != DW_TAG_pointer_type ||
	    die_get_real_type(type_die, type_die) == NULL) {
		pr_debug("no pointer or no type\n");
		ann_data_stat.no_typeinfo++;
		return -1;
	}

	/* Get the size of the actual type */
	if (dwarf_aggregate_size(type_die, &size) < 0) {
		pr_debug("type size is unknown\n");
		ann_data_stat.invalid_size++;
		return -1;
	}

	/* Minimal sanity check */
	if ((unsigned)offset >= size) {
		pr_debug("offset: %d is bigger than size: %" PRIu64 "\n", offset, size);
		ann_data_stat.bad_offset++;
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
		ann_data_stat.no_cuinfo++;
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
		goto out;
	}
	if (ret < 0)
		ann_data_stat.no_var++;

out:
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

static int alloc_data_type_histograms(struct annotated_data_type *adt, int nr_entries)
{
	int i;
	size_t sz = sizeof(struct type_hist);

	sz += sizeof(struct type_hist_entry) * adt->self.size;

	/* Allocate a table of pointers for each event */
	adt->nr_histograms = nr_entries;
	adt->histograms = calloc(nr_entries, sizeof(*adt->histograms));
	if (adt->histograms == NULL)
		return -ENOMEM;

	/*
	 * Each histogram is allocated for the whole size of the type.
	 * TODO: Probably we can move the histogram to members.
	 */
	for (i = 0; i < nr_entries; i++) {
		adt->histograms[i] = zalloc(sz);
		if (adt->histograms[i] == NULL)
			goto err;
	}
	return 0;

err:
	while (--i >= 0)
		free(adt->histograms[i]);
	free(adt->histograms);
	return -ENOMEM;
}

static void delete_data_type_histograms(struct annotated_data_type *adt)
{
	for (int i = 0; i < adt->nr_histograms; i++)
		free(adt->histograms[i]);
	free(adt->histograms);
}

void annotated_data_type__tree_delete(struct rb_root *root)
{
	struct annotated_data_type *pos;

	while (!RB_EMPTY_ROOT(root)) {
		struct rb_node *node = rb_first(root);

		rb_erase(node, root);
		pos = rb_entry(node, struct annotated_data_type, node);
		delete_members(&pos->self);
		delete_data_type_histograms(pos);
		free(pos->self.type_name);
		free(pos);
	}
}

/**
 * annotated_data_type__update_samples - Update histogram
 * @adt: Data type to update
 * @evsel: Event to update
 * @offset: Offset in the type
 * @nr_samples: Number of samples at this offset
 * @period: Event count at this offset
 *
 * This function updates type histogram at @ofs for @evsel.  Samples are
 * aggregated before calling this function so it can be called with more
 * than one samples at a certain offset.
 */
int annotated_data_type__update_samples(struct annotated_data_type *adt,
					struct evsel *evsel, int offset,
					int nr_samples, u64 period)
{
	struct type_hist *h;

	if (adt == NULL)
		return 0;

	if (adt->histograms == NULL) {
		int nr = evsel->evlist->core.nr_entries;

		if (alloc_data_type_histograms(adt, nr) < 0)
			return -1;
	}

	if (offset < 0 || offset >= adt->self.size)
		return -1;

	h = adt->histograms[evsel->core.idx];

	h->nr_samples += nr_samples;
	h->addr[offset].nr_samples += nr_samples;
	h->period += period;
	h->addr[offset].period += period;
	return 0;
}
