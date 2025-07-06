/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Convert sample address to data type using DWARF debug info.
 *
 * Written by Namhyung Kim <namhyung@kernel.org>
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <linux/zalloc.h>

#include "annotate.h"
#include "annotate-data.h"
#include "debuginfo.h"
#include "debug.h"
#include "dso.h"
#include "dwarf-regs.h"
#include "evsel.h"
#include "evlist.h"
#include "map.h"
#include "map_symbol.h"
#include "sort.h"
#include "strbuf.h"
#include "symbol.h"
#include "symbol_conf.h"
#include "thread.h"

/* register number of the stack pointer */
#define X86_REG_SP 7

static void delete_var_types(struct die_var_type *var_types);

#define pr_debug_dtp(fmt, ...)					\
do {								\
	if (debug_type_profile)					\
		pr_info(fmt, ##__VA_ARGS__);			\
	else							\
		pr_debug3(fmt, ##__VA_ARGS__);			\
} while (0)

void pr_debug_type_name(Dwarf_Die *die, enum type_state_kind kind)
{
	struct strbuf sb;
	char *str;
	Dwarf_Word size = 0;

	if (!debug_type_profile && verbose < 3)
		return;

	switch (kind) {
	case TSR_KIND_INVALID:
		pr_info("\n");
		return;
	case TSR_KIND_PERCPU_BASE:
		pr_info(" percpu base\n");
		return;
	case TSR_KIND_CONST:
		pr_info(" constant\n");
		return;
	case TSR_KIND_POINTER:
		pr_info(" pointer");
		/* it also prints the type info */
		break;
	case TSR_KIND_CANARY:
		pr_info(" stack canary\n");
		return;
	case TSR_KIND_TYPE:
	default:
		break;
	}

	dwarf_aggregate_size(die, &size);

	strbuf_init(&sb, 32);
	die_get_typename_from_type(die, &sb);
	str = strbuf_detach(&sb, NULL);
	pr_info(" type='%s' size=%#lx (die:%#lx)\n",
		str, (long)size, (long)dwarf_dieoffset(die));
	free(str);
}

static void pr_debug_location(Dwarf_Die *die, u64 pc, int reg)
{
	ptrdiff_t off = 0;
	Dwarf_Attribute attr;
	Dwarf_Addr base, start, end;
	Dwarf_Op *ops;
	size_t nops;

	if (!debug_type_profile && verbose < 3)
		return;

	if (dwarf_attr(die, DW_AT_location, &attr) == NULL)
		return;

	while ((off = dwarf_getlocations(&attr, off, &base, &start, &end, &ops, &nops)) > 0) {
		if (reg != DWARF_REG_PC && end <= pc)
			continue;
		if (reg != DWARF_REG_PC && start > pc)
			break;

		pr_info(" variable location: ");
		switch (ops->atom) {
		case DW_OP_reg0 ...DW_OP_reg31:
			pr_info("reg%d\n", ops->atom - DW_OP_reg0);
			break;
		case DW_OP_breg0 ...DW_OP_breg31:
			pr_info("base=reg%d, offset=%#lx\n",
				ops->atom - DW_OP_breg0, (long)ops->number);
			break;
		case DW_OP_regx:
			pr_info("reg%ld\n", (long)ops->number);
			break;
		case DW_OP_bregx:
			pr_info("base=reg%ld, offset=%#lx\n",
				(long)ops->number, (long)ops->number2);
			break;
		case DW_OP_fbreg:
			pr_info("use frame base, offset=%#lx\n", (long)ops->number);
			break;
		case DW_OP_addr:
			pr_info("address=%#lx\n", (long)ops->number);
			break;
		default:
			pr_info("unknown: code=%#x, number=%#lx\n",
				ops->atom, (long)ops->number);
			break;
		}
		break;
	}
}

static void pr_debug_scope(Dwarf_Die *scope_die)
{
	int tag;

	if (!debug_type_profile && verbose < 3)
		return;

	pr_info("(die:%lx) ", (long)dwarf_dieoffset(scope_die));

	tag = dwarf_tag(scope_die);
	if (tag == DW_TAG_subprogram)
		pr_info("[function] %s\n", dwarf_diename(scope_die));
	else if (tag == DW_TAG_inlined_subroutine)
		pr_info("[inlined] %s\n", dwarf_diename(scope_die));
	else if (tag == DW_TAG_lexical_block)
		pr_info("[block]\n");
	else
		pr_info("[unknown] tag=%x\n", tag);
}

bool has_reg_type(struct type_state *state, int reg)
{
	return (unsigned)reg < ARRAY_SIZE(state->regs);
}

static void init_type_state(struct type_state *state, struct arch *arch)
{
	memset(state, 0, sizeof(*state));
	INIT_LIST_HEAD(&state->stack_vars);

	if (arch__is(arch, "x86")) {
		state->regs[0].caller_saved = true;
		state->regs[1].caller_saved = true;
		state->regs[2].caller_saved = true;
		state->regs[4].caller_saved = true;
		state->regs[5].caller_saved = true;
		state->regs[8].caller_saved = true;
		state->regs[9].caller_saved = true;
		state->regs[10].caller_saved = true;
		state->regs[11].caller_saved = true;
		state->ret_reg = 0;
		state->stack_reg = X86_REG_SP;
	}
}

static void exit_type_state(struct type_state *state)
{
	struct type_state_stack *stack, *tmp;

	list_for_each_entry_safe(stack, tmp, &state->stack_vars, list) {
		list_del(&stack->list);
		free(stack);
	}
}

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
	Dwarf_Word size, loc, bit_size = 0;
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

	__die_get_real_type(die, &member_type);
	if (dwarf_tag(&member_type) == DW_TAG_typedef)
		die_get_real_type(&member_type, &die_mem);
	else
		die_mem = member_type;

	if (dwarf_aggregate_size(&die_mem, &size) < 0)
		size = 0;

	if (dwarf_attr_integrate(die, DW_AT_data_member_location, &attr))
		dwarf_formudata(&attr, &loc);
	else {
		/* bitfield member */
		if (dwarf_attr_integrate(die, DW_AT_data_bit_offset, &attr) &&
		    dwarf_formudata(&attr, &loc) == 0)
			loc /= 8;
		else
			loc = 0;

		if (dwarf_attr_integrate(die, DW_AT_bit_size, &attr) &&
		    dwarf_formudata(&attr, &bit_size) == 0)
			size = (bit_size + 7) / 8;
	}

	member->type_name = strbuf_detach(&sb, NULL);
	/* member->var_name can be NULL */
	if (dwarf_diename(die)) {
		if (bit_size) {
			if (asprintf(&member->var_name, "%s:%ld",
				     dwarf_diename(die), (long)bit_size) < 0)
				member->var_name = NULL;
		} else {
			member->var_name = strdup(dwarf_diename(die));
		}

		if (member->var_name == NULL) {
			free(member);
			return DIE_FIND_CB_END;
		}
	}
	member->size = size;
	member->offset = loc + parent->offset;
	INIT_LIST_HEAD(&member->children);
	list_add_tail(&member->node, &parent->children);

	tag = dwarf_tag(&die_mem);
	switch (tag) {
	case DW_TAG_structure_type:
	case DW_TAG_union_type:
		die_find_child(&die_mem, __add_member_cb, member, &die_mem);
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
		zfree(&child->type_name);
		zfree(&child->var_name);
		free(child);
	}
}

static int fill_member_name(char *buf, size_t sz, struct annotated_member *m,
			    int offset, bool first)
{
	struct annotated_member *child;

	if (list_empty(&m->children))
		return 0;

	list_for_each_entry(child, &m->children, node) {
		int len;

		if (offset < child->offset || offset >= child->offset + child->size)
			continue;

		/* It can have anonymous struct/union members */
		if (child->var_name) {
			len = scnprintf(buf, sz, "%s%s",
					first ? "" : ".", child->var_name);
			first = false;
		} else {
			len = 0;
		}

		return fill_member_name(buf + len, sz - len, child, offset, first) + len;
	}
	return 0;
}

int annotated_data_type__get_member_name(struct annotated_data_type *adt,
					 char *buf, size_t sz, int member_offset)
{
	return fill_member_name(buf, sz, &adt->self, member_offset, /*first=*/true);
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

	if (dwarf_tag(type_die) == DW_TAG_typedef)
		die_get_real_type(type_die, type_die);

	dwarf_aggregate_size(type_die, &size);

	/* Check existing nodes in dso->data_types tree */
	key.self.type_name = type_name;
	key.self.size = size;
	node = rb_find(&key, dso__data_types(dso), data_type_cmp);
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

	rb_add(&result->node, dso__data_types(dso), data_type_less);
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

enum type_match_result {
	PERF_TMR_UNKNOWN = 0,
	PERF_TMR_OK,
	PERF_TMR_NO_TYPE,
	PERF_TMR_NO_POINTER,
	PERF_TMR_NO_SIZE,
	PERF_TMR_BAD_OFFSET,
	PERF_TMR_BAIL_OUT,
};

static const char *match_result_str(enum type_match_result tmr)
{
	switch (tmr) {
	case PERF_TMR_OK:
		return "Good!";
	case PERF_TMR_NO_TYPE:
		return "no type information";
	case PERF_TMR_NO_POINTER:
		return "no/void pointer";
	case PERF_TMR_NO_SIZE:
		return "type size is unknown";
	case PERF_TMR_BAD_OFFSET:
		return "offset bigger than size";
	case PERF_TMR_UNKNOWN:
	case PERF_TMR_BAIL_OUT:
	default:
		return "invalid state";
	}
}

static bool is_pointer_type(Dwarf_Die *type_die)
{
	int tag = dwarf_tag(type_die);

	return tag == DW_TAG_pointer_type || tag == DW_TAG_array_type;
}

static bool is_compound_type(Dwarf_Die *type_die)
{
	int tag = dwarf_tag(type_die);

	return tag == DW_TAG_structure_type || tag == DW_TAG_union_type;
}

/* returns if Type B has better information than Type A */
static bool is_better_type(Dwarf_Die *type_a, Dwarf_Die *type_b)
{
	Dwarf_Word size_a, size_b;
	Dwarf_Die die_a, die_b;

	/* pointer type is preferred */
	if (is_pointer_type(type_a) != is_pointer_type(type_b))
		return is_pointer_type(type_b);

	if (is_pointer_type(type_b)) {
		/*
		 * We want to compare the target type, but 'void *' can fail to
		 * get the target type.
		 */
		if (die_get_real_type(type_a, &die_a) == NULL)
			return true;
		if (die_get_real_type(type_b, &die_b) == NULL)
			return false;

		type_a = &die_a;
		type_b = &die_b;
	}

	/* bigger type is preferred */
	if (dwarf_aggregate_size(type_a, &size_a) < 0 ||
	    dwarf_aggregate_size(type_b, &size_b) < 0)
		return false;

	if (size_a != size_b)
		return size_a < size_b;

	/* struct or union is preferred */
	if (is_compound_type(type_a) != is_compound_type(type_b))
		return is_compound_type(type_b);

	/* typedef is preferred */
	if (dwarf_tag(type_b) == DW_TAG_typedef)
		return true;

	return false;
}

/* The type info will be saved in @type_die */
static enum type_match_result check_variable(struct data_loc_info *dloc,
					     Dwarf_Die *var_die,
					     Dwarf_Die *type_die, int reg,
					     int offset, bool is_fbreg)
{
	Dwarf_Word size;
	bool needs_pointer = true;
	Dwarf_Die sized_type;

	if (reg == DWARF_REG_PC)
		needs_pointer = false;
	else if (reg == dloc->fbreg || is_fbreg)
		needs_pointer = false;
	else if (arch__is(dloc->arch, "x86") && reg == X86_REG_SP)
		needs_pointer = false;

	/* Get the type of the variable */
	if (__die_get_real_type(var_die, type_die) == NULL)
		return PERF_TMR_NO_TYPE;

	/*
	 * Usually it expects a pointer type for a memory access.
	 * Convert to a real type it points to.  But global variables
	 * and local variables are accessed directly without a pointer.
	 */
	if (needs_pointer) {
		if (!is_pointer_type(type_die) ||
		    __die_get_real_type(type_die, type_die) == NULL)
			return PERF_TMR_NO_POINTER;
	}

	if (dwarf_tag(type_die) == DW_TAG_typedef)
		die_get_real_type(type_die, &sized_type);
	else
		sized_type = *type_die;

	/* Get the size of the actual type */
	if (dwarf_aggregate_size(&sized_type, &size) < 0)
		return PERF_TMR_NO_SIZE;

	/* Minimal sanity check */
	if ((unsigned)offset >= size)
		return PERF_TMR_BAD_OFFSET;

	return PERF_TMR_OK;
}

struct type_state_stack *find_stack_state(struct type_state *state,
						 int offset)
{
	struct type_state_stack *stack;

	list_for_each_entry(stack, &state->stack_vars, list) {
		if (offset == stack->offset)
			return stack;

		if (stack->compound && stack->offset < offset &&
		    offset < stack->offset + stack->size)
			return stack;
	}
	return NULL;
}

void set_stack_state(struct type_state_stack *stack, int offset, u8 kind,
			    Dwarf_Die *type_die)
{
	int tag;
	Dwarf_Word size;

	if (dwarf_aggregate_size(type_die, &size) < 0)
		size = 0;

	tag = dwarf_tag(type_die);

	stack->type = *type_die;
	stack->size = size;
	stack->offset = offset;
	stack->kind = kind;

	switch (tag) {
	case DW_TAG_structure_type:
	case DW_TAG_union_type:
		stack->compound = (kind != TSR_KIND_POINTER);
		break;
	default:
		stack->compound = false;
		break;
	}
}

struct type_state_stack *findnew_stack_state(struct type_state *state,
						    int offset, u8 kind,
						    Dwarf_Die *type_die)
{
	struct type_state_stack *stack = find_stack_state(state, offset);

	if (stack) {
		set_stack_state(stack, offset, kind, type_die);
		return stack;
	}

	stack = malloc(sizeof(*stack));
	if (stack) {
		set_stack_state(stack, offset, kind, type_die);
		list_add(&stack->list, &state->stack_vars);
	}
	return stack;
}

/* Maintain a cache for quick global variable lookup */
struct global_var_entry {
	struct rb_node node;
	char *name;
	u64 start;
	u64 end;
	u64 die_offset;
};

static int global_var_cmp(const void *_key, const struct rb_node *node)
{
	const u64 addr = (uintptr_t)_key;
	struct global_var_entry *gvar;

	gvar = rb_entry(node, struct global_var_entry, node);

	if (gvar->start <= addr && addr < gvar->end)
		return 0;
	return gvar->start > addr ? -1 : 1;
}

static bool global_var_less(struct rb_node *node_a, const struct rb_node *node_b)
{
	struct global_var_entry *gvar_a, *gvar_b;

	gvar_a = rb_entry(node_a, struct global_var_entry, node);
	gvar_b = rb_entry(node_b, struct global_var_entry, node);

	return gvar_a->start < gvar_b->start;
}

static struct global_var_entry *global_var__find(struct data_loc_info *dloc, u64 addr)
{
	struct dso *dso = map__dso(dloc->ms->map);
	struct rb_node *node;

	node = rb_find((void *)(uintptr_t)addr, dso__global_vars(dso), global_var_cmp);
	if (node == NULL)
		return NULL;

	return rb_entry(node, struct global_var_entry, node);
}

static bool global_var__add(struct data_loc_info *dloc, u64 addr,
			    const char *name, Dwarf_Die *type_die)
{
	struct dso *dso = map__dso(dloc->ms->map);
	struct global_var_entry *gvar;
	Dwarf_Word size;

	if (dwarf_aggregate_size(type_die, &size) < 0)
		return false;

	gvar = malloc(sizeof(*gvar));
	if (gvar == NULL)
		return false;

	gvar->name = name ? strdup(name) : NULL;
	if (name && gvar->name == NULL) {
		free(gvar);
		return false;
	}

	gvar->start = addr;
	gvar->end = addr + size;
	gvar->die_offset = dwarf_dieoffset(type_die);

	rb_add(&gvar->node, dso__global_vars(dso), global_var_less);
	return true;
}

void global_var_type__tree_delete(struct rb_root *root)
{
	struct global_var_entry *gvar;

	while (!RB_EMPTY_ROOT(root)) {
		struct rb_node *node = rb_first(root);

		rb_erase(node, root);
		gvar = rb_entry(node, struct global_var_entry, node);
		zfree(&gvar->name);
		free(gvar);
	}
}

bool get_global_var_info(struct data_loc_info *dloc, u64 addr,
				const char **var_name, int *var_offset)
{
	struct addr_location al;
	struct symbol *sym;
	u64 mem_addr;

	/* Kernel symbols might be relocated */
	mem_addr = addr + map__reloc(dloc->ms->map);

	addr_location__init(&al);
	sym = thread__find_symbol_fb(dloc->thread, dloc->cpumode,
				     mem_addr, &al);
	if (sym) {
		*var_name = sym->name;
		/* Calculate type offset from the start of variable */
		*var_offset = mem_addr - map__unmap_ip(al.map, sym->start);
	} else {
		*var_name = NULL;
	}
	addr_location__exit(&al);
	if (*var_name == NULL)
		return false;

	return true;
}

static void global_var__collect(struct data_loc_info *dloc)
{
	Dwarf *dwarf = dloc->di->dbg;
	Dwarf_Off off, next_off;
	Dwarf_Die cu_die, type_die;
	size_t header_size;

	/* Iterate all CU and collect global variables that have no location in a register. */
	off = 0;
	while (dwarf_nextcu(dwarf, off, &next_off, &header_size,
			    NULL, NULL, NULL) == 0) {
		struct die_var_type *var_types = NULL;
		struct die_var_type *pos;

		if (dwarf_offdie(dwarf, off + header_size, &cu_die) == NULL) {
			off = next_off;
			continue;
		}

		die_collect_global_vars(&cu_die, &var_types);

		for (pos = var_types; pos; pos = pos->next) {
			const char *var_name = NULL;
			int var_offset = 0;

			if (pos->reg != -1)
				continue;

			if (!dwarf_offdie(dwarf, pos->die_off, &type_die))
				continue;

			if (!get_global_var_info(dloc, pos->addr, &var_name,
						 &var_offset))
				continue;

			if (var_offset != 0)
				continue;

			global_var__add(dloc, pos->addr, var_name, &type_die);
		}

		delete_var_types(var_types);

		off = next_off;
	}
}

bool get_global_var_type(Dwarf_Die *cu_die, struct data_loc_info *dloc,
				u64 ip, u64 var_addr, int *var_offset,
				Dwarf_Die *type_die)
{
	u64 pc;
	int offset;
	const char *var_name = NULL;
	struct global_var_entry *gvar;
	struct dso *dso = map__dso(dloc->ms->map);
	Dwarf_Die var_die;

	if (RB_EMPTY_ROOT(dso__global_vars(dso)))
		global_var__collect(dloc);

	gvar = global_var__find(dloc, var_addr);
	if (gvar) {
		if (!dwarf_offdie(dloc->di->dbg, gvar->die_offset, type_die))
			return false;

		*var_offset = var_addr - gvar->start;
		return true;
	}

	/* Try to get the variable by address first */
	if (die_find_variable_by_addr(cu_die, var_addr, &var_die, &offset) &&
	    check_variable(dloc, &var_die, type_die, DWARF_REG_PC, offset,
			   /*is_fbreg=*/false) == PERF_TMR_OK) {
		var_name = dwarf_diename(&var_die);
		*var_offset = offset;
		goto ok;
	}

	if (!get_global_var_info(dloc, var_addr, &var_name, var_offset))
		return false;

	pc = map__rip_2objdump(dloc->ms->map, ip);

	/* Try to get the name of global variable */
	if (die_find_variable_at(cu_die, var_name, pc, &var_die) &&
	    check_variable(dloc, &var_die, type_die, DWARF_REG_PC, *var_offset,
			   /*is_fbreg=*/false) == PERF_TMR_OK)
		goto ok;

	return false;

ok:
	/* The address should point to the start of the variable */
	global_var__add(dloc, var_addr - *var_offset, var_name, type_die);
	return true;
}

static bool die_is_same(Dwarf_Die *die_a, Dwarf_Die *die_b)
{
	return (die_a->cu == die_b->cu) && (die_a->addr == die_b->addr);
}

/**
 * update_var_state - Update type state using given variables
 * @state: type state table
 * @dloc: data location info
 * @addr: instruction address to match with variable
 * @insn_offset: instruction offset (for debug)
 * @var_types: list of variables with type info
 *
 * This function fills the @state table using @var_types info.  Each variable
 * is used only at the given location and updates an entry in the table.
 */
static void update_var_state(struct type_state *state, struct data_loc_info *dloc,
			     u64 addr, u64 insn_offset, struct die_var_type *var_types)
{
	Dwarf_Die mem_die;
	struct die_var_type *var;
	int fbreg = dloc->fbreg;
	int fb_offset = 0;

	if (dloc->fb_cfa) {
		if (die_get_cfa(dloc->di->dbg, addr, &fbreg, &fb_offset) < 0)
			fbreg = -1;
	}

	for (var = var_types; var != NULL; var = var->next) {
		if (var->addr != addr)
			continue;
		/* Get the type DIE using the offset */
		if (!dwarf_offdie(dloc->di->dbg, var->die_off, &mem_die))
			continue;

		if (var->reg == DWARF_REG_FB || var->reg == fbreg || var->reg == state->stack_reg) {
			int offset = var->offset;
			struct type_state_stack *stack;

			if (var->reg != DWARF_REG_FB)
				offset -= fb_offset;

			stack = find_stack_state(state, offset);
			if (stack && stack->kind == TSR_KIND_TYPE &&
			    !is_better_type(&stack->type, &mem_die))
				continue;

			findnew_stack_state(state, offset, TSR_KIND_TYPE,
					    &mem_die);

			if (var->reg == state->stack_reg) {
				pr_debug_dtp("var [%"PRIx64"] %#x(reg%d)",
					     insn_offset, offset, state->stack_reg);
			} else {
				pr_debug_dtp("var [%"PRIx64"] -%#x(stack)",
					     insn_offset, -offset);
			}
			pr_debug_type_name(&mem_die, TSR_KIND_TYPE);
		} else if (has_reg_type(state, var->reg) && var->offset == 0) {
			struct type_state_reg *reg;
			Dwarf_Die orig_type;

			reg = &state->regs[var->reg];

			if (reg->ok && reg->kind == TSR_KIND_TYPE &&
			    !is_better_type(&reg->type, &mem_die))
				continue;

			orig_type = reg->type;

			reg->type = mem_die;
			reg->kind = TSR_KIND_TYPE;
			reg->ok = true;

			pr_debug_dtp("var [%"PRIx64"] reg%d",
				     insn_offset, var->reg);
			pr_debug_type_name(&mem_die, TSR_KIND_TYPE);

			/*
			 * If this register is directly copied from another and it gets a
			 * better type, also update the type of the source register.  This
			 * is usually the case of container_of() macro with offset of 0.
			 */
			if (has_reg_type(state, reg->copied_from)) {
				struct type_state_reg *copy_reg;

				copy_reg = &state->regs[reg->copied_from];

				/* TODO: check if type is compatible or embedded */
				if (!copy_reg->ok || (copy_reg->kind != TSR_KIND_TYPE) ||
				    !die_is_same(&copy_reg->type, &orig_type) ||
				    !is_better_type(&copy_reg->type, &mem_die))
					continue;

				copy_reg->type = mem_die;

				pr_debug_dtp("var [%"PRIx64"] copyback reg%d",
					     insn_offset, reg->copied_from);
				pr_debug_type_name(&mem_die, TSR_KIND_TYPE);
			}
		}
	}
}

/**
 * update_insn_state - Update type state for an instruction
 * @state: type state table
 * @dloc: data location info
 * @cu_die: compile unit debug entry
 * @dl: disasm line for the instruction
 *
 * This function updates the @state table for the target operand of the
 * instruction at @dl if it transfers the type like MOV on x86.  Since it
 * tracks the type, it won't care about the values like in arithmetic
 * instructions like ADD/SUB/MUL/DIV and INC/DEC.
 *
 * Note that ops->reg2 is only available when both mem_ref and multi_regs
 * are true.
 */
static void update_insn_state(struct type_state *state, struct data_loc_info *dloc,
			      Dwarf_Die *cu_die, struct disasm_line *dl)
{
	if (dloc->arch->update_insn_state)
		dloc->arch->update_insn_state(state, dloc, cu_die, dl);
}

/*
 * Prepend this_blocks (from the outer scope) to full_blocks, removing
 * duplicate disasm line.
 */
static void prepend_basic_blocks(struct list_head *this_blocks,
				 struct list_head *full_blocks)
{
	struct annotated_basic_block *first_bb, *last_bb;

	last_bb = list_last_entry(this_blocks, typeof(*last_bb), list);
	first_bb = list_first_entry(full_blocks, typeof(*first_bb), list);

	if (list_empty(full_blocks))
		goto out;

	/* Last insn in this_blocks should be same as first insn in full_blocks */
	if (last_bb->end != first_bb->begin) {
		pr_debug("prepend basic blocks: mismatched disasm line %"PRIx64" -> %"PRIx64"\n",
			 last_bb->end->al.offset, first_bb->begin->al.offset);
		goto out;
	}

	/* Is the basic block have only one disasm_line? */
	if (last_bb->begin == last_bb->end) {
		list_del(&last_bb->list);
		free(last_bb);
		goto out;
	}

	/* Point to the insn before the last when adding this block to full_blocks */
	last_bb->end = list_prev_entry(last_bb->end, al.node);

out:
	list_splice(this_blocks, full_blocks);
}

static void delete_basic_blocks(struct list_head *basic_blocks)
{
	struct annotated_basic_block *bb, *tmp;

	list_for_each_entry_safe(bb, tmp, basic_blocks, list) {
		list_del(&bb->list);
		free(bb);
	}
}

/* Make sure all variables have a valid start address */
static void fixup_var_address(struct die_var_type *var_types, u64 addr)
{
	while (var_types) {
		/*
		 * Some variables have no address range meaning it's always
		 * available in the whole scope.  Let's adjust the start
		 * address to the start of the scope.
		 */
		if (var_types->addr == 0)
			var_types->addr = addr;

		var_types = var_types->next;
	}
}

static void delete_var_types(struct die_var_type *var_types)
{
	while (var_types) {
		struct die_var_type *next = var_types->next;

		free(var_types);
		var_types = next;
	}
}

/* should match to is_stack_canary() in util/annotate.c */
static void setup_stack_canary(struct data_loc_info *dloc)
{
	if (arch__is(dloc->arch, "x86")) {
		dloc->op->segment = INSN_SEG_X86_GS;
		dloc->op->imm = true;
		dloc->op->offset = 40;
	}
}

/*
 * It's at the target address, check if it has a matching type.
 * It returns PERF_TMR_BAIL_OUT when it looks up per-cpu variables which
 * are similar to global variables and no additional info is needed.
 */
static enum type_match_result check_matching_type(struct type_state *state,
						  struct data_loc_info *dloc,
						  Dwarf_Die *cu_die,
						  struct disasm_line *dl,
						  Dwarf_Die *type_die)
{
	Dwarf_Word size;
	u32 insn_offset = dl->al.offset;
	int reg = dloc->op->reg1;
	int offset = dloc->op->offset;
	const char *offset_sign = "";
	bool retry = true;

	if (offset < 0) {
		offset = -offset;
		offset_sign = "-";
	}

again:
	pr_debug_dtp("chk [%x] reg%d offset=%s%#x ok=%d kind=%d ",
		     insn_offset, reg, offset_sign, offset,
		     state->regs[reg].ok, state->regs[reg].kind);

	if (!state->regs[reg].ok)
		goto check_non_register;

	if (state->regs[reg].kind == TSR_KIND_TYPE) {
		Dwarf_Die sized_type;
		struct strbuf sb;

		strbuf_init(&sb, 32);
		die_get_typename_from_type(&state->regs[reg].type, &sb);
		pr_debug_dtp("(%s)", sb.buf);
		strbuf_release(&sb);

		/*
		 * Normal registers should hold a pointer (or array) to
		 * dereference a memory location.
		 */
		if (!is_pointer_type(&state->regs[reg].type)) {
			if (dloc->op->offset < 0 && reg != state->stack_reg)
				goto check_kernel;

			return PERF_TMR_NO_POINTER;
		}

		/* Remove the pointer and get the target type */
		if (__die_get_real_type(&state->regs[reg].type, type_die) == NULL)
			return PERF_TMR_NO_POINTER;

		dloc->type_offset = dloc->op->offset;

		if (dwarf_tag(type_die) == DW_TAG_typedef)
			die_get_real_type(type_die, &sized_type);
		else
			sized_type = *type_die;

		/* Get the size of the actual type */
		if (dwarf_aggregate_size(&sized_type, &size) < 0 ||
		    (unsigned)dloc->type_offset >= size)
			return PERF_TMR_BAD_OFFSET;

		return PERF_TMR_OK;
	}

	if (state->regs[reg].kind == TSR_KIND_POINTER) {
		pr_debug_dtp("percpu ptr");

		/*
		 * It's actaully pointer but the address was calculated using
		 * some arithmetic.  So it points to the actual type already.
		 */
		*type_die = state->regs[reg].type;

		dloc->type_offset = dloc->op->offset;

		/* Get the size of the actual type */
		if (dwarf_aggregate_size(type_die, &size) < 0 ||
		    (unsigned)dloc->type_offset >= size)
			return PERF_TMR_BAIL_OUT;

		return PERF_TMR_OK;
	}

	if (state->regs[reg].kind == TSR_KIND_CANARY) {
		pr_debug_dtp("stack canary");

		/*
		 * This is a saved value of the stack canary which will be handled
		 * in the outer logic when it returns failure here.  Pretend it's
		 * from the stack canary directly.
		 */
		setup_stack_canary(dloc);

		return PERF_TMR_BAIL_OUT;
	}

	if (state->regs[reg].kind == TSR_KIND_PERCPU_BASE) {
		u64 var_addr = dloc->op->offset;
		int var_offset;

		pr_debug_dtp("percpu var");

		if (dloc->op->multi_regs) {
			int reg2 = dloc->op->reg2;

			if (dloc->op->reg2 == reg)
				reg2 = dloc->op->reg1;

			if (has_reg_type(state, reg2) && state->regs[reg2].ok &&
			    state->regs[reg2].kind == TSR_KIND_CONST)
				var_addr += state->regs[reg2].imm_value;
		}

		if (get_global_var_type(cu_die, dloc, dloc->ip, var_addr,
					&var_offset, type_die)) {
			dloc->type_offset = var_offset;
			return PERF_TMR_OK;
		}
		/* No need to retry per-cpu (global) variables */
		return PERF_TMR_BAIL_OUT;
	}

check_non_register:
	if (reg == dloc->fbreg || reg == state->stack_reg) {
		struct type_state_stack *stack;

		pr_debug_dtp("%s", reg == dloc->fbreg ? "fbreg" : "stack");

		stack = find_stack_state(state, dloc->type_offset);
		if (stack == NULL) {
			if (retry) {
				pr_debug_dtp(" : retry\n");
				retry = false;

				/* update type info it's the first store to the stack */
				update_insn_state(state, dloc, cu_die, dl);
				goto again;
			}
			return PERF_TMR_NO_TYPE;
		}

		if (stack->kind == TSR_KIND_CANARY) {
			setup_stack_canary(dloc);
			return PERF_TMR_BAIL_OUT;
		}

		if (stack->kind != TSR_KIND_TYPE)
			return PERF_TMR_NO_TYPE;

		*type_die = stack->type;
		/* Update the type offset from the start of slot */
		dloc->type_offset -= stack->offset;

		return PERF_TMR_OK;
	}

	if (dloc->fb_cfa) {
		struct type_state_stack *stack;
		u64 pc = map__rip_2objdump(dloc->ms->map, dloc->ip);
		int fbreg, fboff;

		pr_debug_dtp("cfa");

		if (die_get_cfa(dloc->di->dbg, pc, &fbreg, &fboff) < 0)
			fbreg = -1;

		if (reg != fbreg)
			return PERF_TMR_NO_TYPE;

		stack = find_stack_state(state, dloc->type_offset - fboff);
		if (stack == NULL) {
			if (retry) {
				pr_debug_dtp(" : retry\n");
				retry = false;

				/* update type info it's the first store to the stack */
				update_insn_state(state, dloc, cu_die, dl);
				goto again;
			}
			return PERF_TMR_NO_TYPE;
		}

		if (stack->kind == TSR_KIND_CANARY) {
			setup_stack_canary(dloc);
			return PERF_TMR_BAIL_OUT;
		}

		if (stack->kind != TSR_KIND_TYPE)
			return PERF_TMR_NO_TYPE;

		*type_die = stack->type;
		/* Update the type offset from the start of slot */
		dloc->type_offset -= fboff + stack->offset;

		return PERF_TMR_OK;
	}

check_kernel:
	if (dso__kernel(map__dso(dloc->ms->map))) {
		u64 addr;

		/* Direct this-cpu access like "%gs:0x34740" */
		if (dloc->op->segment == INSN_SEG_X86_GS && dloc->op->imm &&
		    arch__is(dloc->arch, "x86")) {
			pr_debug_dtp("this-cpu var");

			addr = dloc->op->offset;

			if (get_global_var_type(cu_die, dloc, dloc->ip, addr,
						&offset, type_die)) {
				dloc->type_offset = offset;
				return PERF_TMR_OK;
			}
			return PERF_TMR_BAIL_OUT;
		}

		/* Access to global variable like "-0x7dcf0500(,%rdx,8)" */
		if (dloc->op->offset < 0 && reg != state->stack_reg) {
			addr = (s64) dloc->op->offset;

			if (get_global_var_type(cu_die, dloc, dloc->ip, addr,
						&offset, type_die)) {
				pr_debug_dtp("global var");

				dloc->type_offset = offset;
				return PERF_TMR_OK;
			}
			return PERF_TMR_BAIL_OUT;
		}
	}

	return PERF_TMR_UNKNOWN;
}

/* Iterate instructions in basic blocks and update type table */
static enum type_match_result find_data_type_insn(struct data_loc_info *dloc,
						  struct list_head *basic_blocks,
						  struct die_var_type *var_types,
						  Dwarf_Die *cu_die,
						  Dwarf_Die *type_die)
{
	struct type_state state;
	struct symbol *sym = dloc->ms->sym;
	struct annotation *notes = symbol__annotation(sym);
	struct annotated_basic_block *bb;
	enum type_match_result ret = PERF_TMR_UNKNOWN;

	init_type_state(&state, dloc->arch);

	list_for_each_entry(bb, basic_blocks, list) {
		struct disasm_line *dl = bb->begin;

		BUG_ON(bb->begin->al.offset == -1 || bb->end->al.offset == -1);

		pr_debug_dtp("bb: [%"PRIx64" - %"PRIx64"]\n",
			     bb->begin->al.offset, bb->end->al.offset);

		list_for_each_entry_from(dl, &notes->src->source, al.node) {
			u64 this_ip = sym->start + dl->al.offset;
			u64 addr = map__rip_2objdump(dloc->ms->map, this_ip);

			/* Skip comment or debug info lines */
			if (dl->al.offset == -1)
				continue;

			/* Update variable type at this address */
			update_var_state(&state, dloc, addr, dl->al.offset, var_types);

			if (this_ip == dloc->ip) {
				ret = check_matching_type(&state, dloc,
							  cu_die, dl, type_die);
				pr_debug_dtp(" : %s\n", match_result_str(ret));
				goto out;
			}

			/* Update type table after processing the instruction */
			update_insn_state(&state, dloc, cu_die, dl);
			if (dl == bb->end)
				break;
		}
	}

out:
	exit_type_state(&state);
	return ret;
}

static int arch_supports_insn_tracking(struct data_loc_info *dloc)
{
	if ((arch__is(dloc->arch, "x86")) || (arch__is(dloc->arch, "powerpc")))
		return 1;
	return 0;
}

/*
 * Construct a list of basic blocks for each scope with variables and try to find
 * the data type by updating a type state table through instructions.
 */
static enum type_match_result find_data_type_block(struct data_loc_info *dloc,
						   Dwarf_Die *cu_die,
						   Dwarf_Die *scopes,
						   int nr_scopes,
						   Dwarf_Die *type_die)
{
	LIST_HEAD(basic_blocks);
	struct die_var_type *var_types = NULL;
	u64 src_ip, dst_ip, prev_dst_ip;
	enum type_match_result ret = PERF_TMR_UNKNOWN;

	/* TODO: other architecture support */
	if (!arch_supports_insn_tracking(dloc))
		return PERF_TMR_BAIL_OUT;

	prev_dst_ip = dst_ip = dloc->ip;
	for (int i = nr_scopes - 1; i >= 0; i--) {
		Dwarf_Addr base, start, end;
		LIST_HEAD(this_blocks);

		if (dwarf_ranges(&scopes[i], 0, &base, &start, &end) < 0)
			break;

		pr_debug_dtp("scope: [%d/%d] ", i + 1, nr_scopes);
		pr_debug_scope(&scopes[i]);

		src_ip = map__objdump_2rip(dloc->ms->map, start);

again:
		/* Get basic blocks for this scope */
		if (annotate_get_basic_blocks(dloc->ms->sym, src_ip, dst_ip,
					      &this_blocks) < 0) {
			/* Try previous block if they are not connected */
			if (prev_dst_ip != dst_ip) {
				dst_ip = prev_dst_ip;
				goto again;
			}

			pr_debug_dtp("cannot find a basic block from %"PRIx64" to %"PRIx64"\n",
				     src_ip - dloc->ms->sym->start,
				     dst_ip - dloc->ms->sym->start);
			continue;
		}
		prepend_basic_blocks(&this_blocks, &basic_blocks);

		/* Get variable info for this scope and add to var_types list */
		die_collect_vars(&scopes[i], &var_types);
		fixup_var_address(var_types, start);

		/* Find from start of this scope to the target instruction */
		ret = find_data_type_insn(dloc, &basic_blocks, var_types,
					    cu_die, type_die);
		if (ret == PERF_TMR_OK) {
			char buf[64];
			int offset = dloc->op->offset;
			const char *offset_sign = "";

			if (offset < 0) {
				offset = -offset;
				offset_sign = "-";
			}

			if (dloc->op->multi_regs)
				snprintf(buf, sizeof(buf), "reg%d, reg%d",
					 dloc->op->reg1, dloc->op->reg2);
			else
				snprintf(buf, sizeof(buf), "reg%d", dloc->op->reg1);

			pr_debug_dtp("found by insn track: %s%#x(%s) type-offset=%#x\n",
				     offset_sign, offset, buf, dloc->type_offset);
			break;
		}

		if (ret == PERF_TMR_BAIL_OUT)
			break;

		/* Go up to the next scope and find blocks to the start */
		prev_dst_ip = dst_ip;
		dst_ip = src_ip;
	}

	delete_basic_blocks(&basic_blocks);
	delete_var_types(var_types);
	return ret;
}

/* The result will be saved in @type_die */
static int find_data_type_die(struct data_loc_info *dloc, Dwarf_Die *type_die)
{
	struct annotated_op_loc *loc = dloc->op;
	Dwarf_Die cu_die, var_die;
	Dwarf_Die *scopes = NULL;
	int reg, offset = loc->offset;
	int ret = -1;
	int i, nr_scopes;
	int fbreg = -1;
	int fb_offset = 0;
	bool is_fbreg = false;
	bool found = false;
	u64 pc;
	char buf[64];
	enum type_match_result result = PERF_TMR_UNKNOWN;
	const char *offset_sign = "";

	if (dloc->op->multi_regs)
		snprintf(buf, sizeof(buf), "reg%d, reg%d", dloc->op->reg1, dloc->op->reg2);
	else if (dloc->op->reg1 == DWARF_REG_PC)
		snprintf(buf, sizeof(buf), "PC");
	else
		snprintf(buf, sizeof(buf), "reg%d", dloc->op->reg1);

	if (offset < 0) {
		offset = -offset;
		offset_sign = "-";
	}

	pr_debug_dtp("-----------------------------------------------------------\n");
	pr_debug_dtp("find data type for %s%#x(%s) at %s+%#"PRIx64"\n",
		     offset_sign, offset, buf,
		     dloc->ms->sym->name, dloc->ip - dloc->ms->sym->start);

	/*
	 * IP is a relative instruction address from the start of the map, as
	 * it can be randomized/relocated, it needs to translate to PC which is
	 * a file address for DWARF processing.
	 */
	pc = map__rip_2objdump(dloc->ms->map, dloc->ip);

	/* Get a compile_unit for this address */
	if (!find_cu_die(dloc->di, pc, &cu_die)) {
		pr_debug_dtp("cannot find CU for address %"PRIx64"\n", pc);
		ann_data_stat.no_cuinfo++;
		return -1;
	}

	reg = loc->reg1;
	offset = loc->offset;

	pr_debug_dtp("CU for %s (die:%#lx)\n",
		     dwarf_diename(&cu_die), (long)dwarf_dieoffset(&cu_die));

	if (reg == DWARF_REG_PC) {
		if (get_global_var_type(&cu_die, dloc, dloc->ip, dloc->var_addr,
					&offset, type_die)) {
			dloc->type_offset = offset;

			pr_debug_dtp("found by addr=%#"PRIx64" type_offset=%#x\n",
				     dloc->var_addr, offset);
			pr_debug_type_name(type_die, TSR_KIND_TYPE);
			found = true;
			goto out;
		}
	}

	/* Get a list of nested scopes - i.e. (inlined) functions and blocks. */
	nr_scopes = die_get_scopes(&cu_die, pc, &scopes);

	if (reg != DWARF_REG_PC && dwarf_hasattr(&scopes[0], DW_AT_frame_base)) {
		Dwarf_Attribute attr;
		Dwarf_Block block;

		/* Check if the 'reg' is assigned as frame base register */
		if (dwarf_attr(&scopes[0], DW_AT_frame_base, &attr) != NULL &&
		    dwarf_formblock(&attr, &block) == 0 && block.length == 1) {
			switch (*block.data) {
			case DW_OP_reg0 ... DW_OP_reg31:
				fbreg = dloc->fbreg = *block.data - DW_OP_reg0;
				break;
			case DW_OP_call_frame_cfa:
				dloc->fb_cfa = true;
				if (die_get_cfa(dloc->di->dbg, pc, &fbreg,
						&fb_offset) < 0)
					fbreg = -1;
				break;
			default:
				break;
			}

			pr_debug_dtp("frame base: cfa=%d fbreg=%d\n",
				     dloc->fb_cfa, fbreg);
		}
	}

retry:
	is_fbreg = (reg == fbreg);
	if (is_fbreg)
		offset = loc->offset - fb_offset;

	/* Search from the inner-most scope to the outer */
	for (i = nr_scopes - 1; i >= 0; i--) {
		Dwarf_Die mem_die;
		int type_offset = offset;

		if (reg == DWARF_REG_PC) {
			if (!die_find_variable_by_addr(&scopes[i], dloc->var_addr,
						       &var_die, &type_offset))
				continue;
		} else {
			/* Look up variables/parameters in this scope */
			if (!die_find_variable_by_reg(&scopes[i], pc, reg,
						      &type_offset, is_fbreg, &var_die))
				continue;
		}

		pr_debug_dtp("found \"%s\" (die: %#lx) in scope=%d/%d (die: %#lx) ",
			     dwarf_diename(&var_die), (long)dwarf_dieoffset(&var_die),
			     i+1, nr_scopes, (long)dwarf_dieoffset(&scopes[i]));

		/* Found a variable, see if it's correct */
		result = check_variable(dloc, &var_die, &mem_die, reg, type_offset, is_fbreg);
		if (result == PERF_TMR_OK) {
			if (reg == DWARF_REG_PC) {
				pr_debug_dtp("addr=%#"PRIx64" type_offset=%#x\n",
					     dloc->var_addr, type_offset);
			} else if (reg == DWARF_REG_FB || is_fbreg) {
				pr_debug_dtp("stack_offset=%#x type_offset=%#x\n",
					     fb_offset, type_offset);
			} else {
				pr_debug_dtp("type_offset=%#x\n", type_offset);
			}

			if (!found || is_better_type(type_die, &mem_die)) {
				*type_die = mem_die;
				dloc->type_offset = type_offset;
				found = true;
			}
		} else {
			pr_debug_dtp("failed: %s\n", match_result_str(result));
		}

		pr_debug_location(&var_die, pc, reg);
		pr_debug_type_name(&mem_die, TSR_KIND_TYPE);
	}

	if (!found && loc->multi_regs && reg == loc->reg1 && loc->reg1 != loc->reg2) {
		reg = loc->reg2;
		goto retry;
	}

	if (!found && reg != DWARF_REG_PC) {
		result = find_data_type_block(dloc, &cu_die, scopes,
					      nr_scopes, type_die);
		if (result == PERF_TMR_OK) {
			ann_data_stat.insn_track++;
			found = true;
		}
	}

out:
	pr_debug_dtp("final result: ");
	if (found) {
		pr_debug_type_name(type_die, TSR_KIND_TYPE);
		ret = 0;
	} else {
		switch (result) {
		case PERF_TMR_NO_TYPE:
		case PERF_TMR_NO_POINTER:
			pr_debug_dtp("%s\n", match_result_str(result));
			ann_data_stat.no_typeinfo++;
			break;
		case PERF_TMR_NO_SIZE:
			pr_debug_dtp("%s\n", match_result_str(result));
			ann_data_stat.invalid_size++;
			break;
		case PERF_TMR_BAD_OFFSET:
			pr_debug_dtp("%s\n", match_result_str(result));
			ann_data_stat.bad_offset++;
			break;
		case PERF_TMR_UNKNOWN:
		case PERF_TMR_BAIL_OUT:
		case PERF_TMR_OK:  /* should not reach here */
		default:
			pr_debug_dtp("no variable found\n");
			ann_data_stat.no_var++;
			break;
		}
		ret = -1;
	}

	free(scopes);
	return ret;
}

/**
 * find_data_type - Return a data type at the location
 * @dloc: data location
 *
 * This functions searches the debug information of the binary to get the data
 * type it accesses.  The exact location is expressed by (ip, reg, offset)
 * for pointer variables or (ip, addr) for global variables.  Note that global
 * variables might update the @dloc->type_offset after finding the start of the
 * variable.  If it cannot find a global variable by address, it tried to find
 * a declaration of the variable using var_name.  In that case, @dloc->offset
 * won't be updated.
 *
 * It return %NULL if not found.
 */
struct annotated_data_type *find_data_type(struct data_loc_info *dloc)
{
	struct dso *dso = map__dso(dloc->ms->map);
	Dwarf_Die type_die;

	/*
	 * The type offset is the same as instruction offset by default.
	 * But when finding a global variable, the offset won't be valid.
	 */
	dloc->type_offset = dloc->op->offset;

	dloc->fbreg = -1;

	if (find_data_type_die(dloc, &type_die) < 0)
		return NULL;

	return dso__findnew_data_type(dso, &type_die);
}

static int alloc_data_type_histograms(struct annotated_data_type *adt, int nr_entries)
{
	int i;
	size_t sz = sizeof(struct type_hist);

	sz += sizeof(struct type_hist_entry) * adt->self.size;

	/* Allocate a table of pointers for each event */
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

	adt->nr_histograms = nr_entries;
	return 0;

err:
	while (--i >= 0)
		zfree(&(adt->histograms[i]));
	zfree(&adt->histograms);
	return -ENOMEM;
}

static void delete_data_type_histograms(struct annotated_data_type *adt)
{
	for (int i = 0; i < adt->nr_histograms; i++)
		zfree(&(adt->histograms[i]));

	zfree(&adt->histograms);
	adt->nr_histograms = 0;
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
		zfree(&pos->self.type_name);
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

static void print_annotated_data_header(struct hist_entry *he, struct evsel *evsel)
{
	struct dso *dso = map__dso(he->ms.map);
	int nr_members = 1;
	int nr_samples = he->stat.nr_events;
	int width = 7;
	const char *val_hdr = "Percent";

	if (evsel__is_group_event(evsel)) {
		struct hist_entry *pair;

		list_for_each_entry(pair, &he->pairs.head, pairs.node)
			nr_samples += pair->stat.nr_events;
	}

	printf("Annotate type: '%s' in %s (%d samples):\n",
	       he->mem_type->self.type_name, dso__name(dso), nr_samples);

	if (evsel__is_group_event(evsel)) {
		struct evsel *pos;
		int i = 0;

		nr_members = 0;
		for_each_group_evsel(pos, evsel) {
			if (symbol_conf.skip_empty &&
			    evsel__hists(pos)->stats.nr_samples == 0)
				continue;

			printf(" event[%d] = %s\n", i++, pos->name);
			nr_members++;
		}
	}

	if (symbol_conf.show_total_period) {
		width = 11;
		val_hdr = "Period";
	} else if (symbol_conf.show_nr_samples) {
		width = 7;
		val_hdr = "Samples";
	}

	printf("============================================================================\n");
	printf("%*s %10s %10s  %s\n", (width + 1) * nr_members, val_hdr,
	       "offset", "size", "field");
}

static void print_annotated_data_value(struct type_hist *h, u64 period, int nr_samples)
{
	double percent = h->period ? (100.0 * period / h->period) : 0;
	const char *color = get_percent_color(percent);

	if (symbol_conf.show_total_period)
		color_fprintf(stdout, color, " %11" PRIu64, period);
	else if (symbol_conf.show_nr_samples)
		color_fprintf(stdout, color, " %7d", nr_samples);
	else
		color_fprintf(stdout, color, " %7.2f", percent);
}

static void print_annotated_data_type(struct annotated_data_type *mem_type,
				      struct annotated_member *member,
				      struct evsel *evsel, int indent)
{
	struct annotated_member *child;
	struct type_hist *h = mem_type->histograms[evsel->core.idx];
	int i, nr_events = 0, samples = 0;
	u64 period = 0;
	int width = symbol_conf.show_total_period ? 11 : 7;
	struct evsel *pos;

	for_each_group_evsel(pos, evsel) {
		h = mem_type->histograms[pos->core.idx];

		if (symbol_conf.skip_empty &&
		    evsel__hists(pos)->stats.nr_samples == 0)
			continue;

		samples = 0;
		period = 0;
		for (i = 0; i < member->size; i++) {
			samples += h->addr[member->offset + i].nr_samples;
			period += h->addr[member->offset + i].period;
		}
		print_annotated_data_value(h, period, samples);
		nr_events++;
	}

	printf(" %#10x %#10x  %*s%s\t%s",
	       member->offset, member->size, indent, "", member->type_name,
	       member->var_name ?: "");

	if (!list_empty(&member->children))
		printf(" {\n");

	list_for_each_entry(child, &member->children, node)
		print_annotated_data_type(mem_type, child, evsel, indent + 4);

	if (!list_empty(&member->children))
		printf("%*s}", (width + 1) * nr_events + 24 + indent, "");
	printf(";\n");
}

int hist_entry__annotate_data_tty(struct hist_entry *he, struct evsel *evsel)
{
	print_annotated_data_header(he, evsel);
	print_annotated_data_type(he->mem_type, &he->mem_type->self, evsel, 0);
	printf("\n");

	/* move to the next entry */
	return '>';
}
