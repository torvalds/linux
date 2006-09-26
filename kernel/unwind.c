/*
 * Copyright (C) 2002-2006 Novell, Inc.
 *	Jan Beulich <jbeulich@novell.com>
 * This code is released under version 2 of the GNU GPL.
 *
 * A simple API for unwinding kernel stacks.  This is used for
 * debugging and error reporting purposes.  The kernel doesn't need
 * full-blown stack unwinding with all the bells and whistles, so there
 * is not much point in implementing the full Dwarf2 unwind API.
 */

#include <linux/unwind.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/stop_machine.h>
#include <asm/sections.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>

extern char __start_unwind[], __end_unwind[];

#define MAX_STACK_DEPTH 8

#define EXTRA_INFO(f) { \
		BUILD_BUG_ON_ZERO(offsetof(struct unwind_frame_info, f) \
		                  % FIELD_SIZEOF(struct unwind_frame_info, f)) \
		+ offsetof(struct unwind_frame_info, f) \
		  / FIELD_SIZEOF(struct unwind_frame_info, f), \
		FIELD_SIZEOF(struct unwind_frame_info, f) \
	}
#define PTREGS_INFO(f) EXTRA_INFO(regs.f)

static const struct {
	unsigned offs:BITS_PER_LONG / 2;
	unsigned width:BITS_PER_LONG / 2;
} reg_info[] = {
	UNW_REGISTER_INFO
};

#undef PTREGS_INFO
#undef EXTRA_INFO

#ifndef REG_INVALID
#define REG_INVALID(r) (reg_info[r].width == 0)
#endif

#define DW_CFA_nop                          0x00
#define DW_CFA_set_loc                      0x01
#define DW_CFA_advance_loc1                 0x02
#define DW_CFA_advance_loc2                 0x03
#define DW_CFA_advance_loc4                 0x04
#define DW_CFA_offset_extended              0x05
#define DW_CFA_restore_extended             0x06
#define DW_CFA_undefined                    0x07
#define DW_CFA_same_value                   0x08
#define DW_CFA_register                     0x09
#define DW_CFA_remember_state               0x0a
#define DW_CFA_restore_state                0x0b
#define DW_CFA_def_cfa                      0x0c
#define DW_CFA_def_cfa_register             0x0d
#define DW_CFA_def_cfa_offset               0x0e
#define DW_CFA_def_cfa_expression           0x0f
#define DW_CFA_expression                   0x10
#define DW_CFA_offset_extended_sf           0x11
#define DW_CFA_def_cfa_sf                   0x12
#define DW_CFA_def_cfa_offset_sf            0x13
#define DW_CFA_val_offset                   0x14
#define DW_CFA_val_offset_sf                0x15
#define DW_CFA_val_expression               0x16
#define DW_CFA_lo_user                      0x1c
#define DW_CFA_GNU_window_save              0x2d
#define DW_CFA_GNU_args_size                0x2e
#define DW_CFA_GNU_negative_offset_extended 0x2f
#define DW_CFA_hi_user                      0x3f

#define DW_EH_PE_FORM     0x07
#define DW_EH_PE_native   0x00
#define DW_EH_PE_leb128   0x01
#define DW_EH_PE_data2    0x02
#define DW_EH_PE_data4    0x03
#define DW_EH_PE_data8    0x04
#define DW_EH_PE_signed   0x08
#define DW_EH_PE_ADJUST   0x70
#define DW_EH_PE_abs      0x00
#define DW_EH_PE_pcrel    0x10
#define DW_EH_PE_textrel  0x20
#define DW_EH_PE_datarel  0x30
#define DW_EH_PE_funcrel  0x40
#define DW_EH_PE_aligned  0x50
#define DW_EH_PE_indirect 0x80
#define DW_EH_PE_omit     0xff

typedef unsigned long uleb128_t;
typedef   signed long sleb128_t;

static struct unwind_table {
	struct {
		unsigned long pc;
		unsigned long range;
	} core, init;
	const void *address;
	unsigned long size;
	struct unwind_table *link;
	const char *name;
} root_table, *last_table;

struct unwind_item {
	enum item_location {
		Nowhere,
		Memory,
		Register,
		Value
	} where;
	uleb128_t value;
};

struct unwind_state {
	uleb128_t loc, org;
	const u8 *cieStart, *cieEnd;
	uleb128_t codeAlign;
	sleb128_t dataAlign;
	struct cfa {
		uleb128_t reg, offs;
	} cfa;
	struct unwind_item regs[ARRAY_SIZE(reg_info)];
	unsigned stackDepth:8;
	unsigned version:8;
	const u8 *label;
	const u8 *stack[MAX_STACK_DEPTH];
};

static const struct cfa badCFA = { ARRAY_SIZE(reg_info), 1 };

static struct unwind_table *find_table(unsigned long pc)
{
	struct unwind_table *table;

	for (table = &root_table; table; table = table->link)
		if ((pc >= table->core.pc
		     && pc < table->core.pc + table->core.range)
		    || (pc >= table->init.pc
		        && pc < table->init.pc + table->init.range))
			break;

	return table;
}

static void init_unwind_table(struct unwind_table *table,
                              const char *name,
                              const void *core_start,
                              unsigned long core_size,
                              const void *init_start,
                              unsigned long init_size,
                              const void *table_start,
                              unsigned long table_size)
{
	table->core.pc = (unsigned long)core_start;
	table->core.range = core_size;
	table->init.pc = (unsigned long)init_start;
	table->init.range = init_size;
	table->address = table_start;
	table->size = table_size;
	table->link = NULL;
	table->name = name;
}

void __init unwind_init(void)
{
	init_unwind_table(&root_table, "kernel",
	                  _text, _end - _text,
	                  NULL, 0,
	                  __start_unwind, __end_unwind - __start_unwind);
}

#ifdef CONFIG_MODULES

/* Must be called with module_mutex held. */
void *unwind_add_table(struct module *module,
                       const void *table_start,
                       unsigned long table_size)
{
	struct unwind_table *table;

	if (table_size <= 0)
		return NULL;

	table = kmalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return NULL;

	init_unwind_table(table, module->name,
	                  module->module_core, module->core_size,
	                  module->module_init, module->init_size,
	                  table_start, table_size);

	if (last_table)
		last_table->link = table;
	else
		root_table.link = table;
	last_table = table;

	return table;
}

struct unlink_table_info
{
	struct unwind_table *table;
	int init_only;
};

static int unlink_table(void *arg)
{
	struct unlink_table_info *info = arg;
	struct unwind_table *table = info->table, *prev;

	for (prev = &root_table; prev->link && prev->link != table; prev = prev->link)
		;

	if (prev->link) {
		if (info->init_only) {
			table->init.pc = 0;
			table->init.range = 0;
			info->table = NULL;
		} else {
			prev->link = table->link;
			if (!prev->link)
				last_table = prev;
		}
	} else
		info->table = NULL;

	return 0;
}

/* Must be called with module_mutex held. */
void unwind_remove_table(void *handle, int init_only)
{
	struct unwind_table *table = handle;
	struct unlink_table_info info;

	if (!table || table == &root_table)
		return;

	if (init_only && table == last_table) {
		table->init.pc = 0;
		table->init.range = 0;
		return;
	}

	info.table = table;
	info.init_only = init_only;
	stop_machine_run(unlink_table, &info, NR_CPUS);

	if (info.table)
		kfree(table);
}

#endif /* CONFIG_MODULES */

static uleb128_t get_uleb128(const u8 **pcur, const u8 *end)
{
	const u8 *cur = *pcur;
	uleb128_t value;
	unsigned shift;

	for (shift = 0, value = 0; cur < end; shift += 7) {
		if (shift + 7 > 8 * sizeof(value)
		    && (*cur & 0x7fU) >= (1U << (8 * sizeof(value) - shift))) {
			cur = end + 1;
			break;
		}
		value |= (uleb128_t)(*cur & 0x7f) << shift;
		if (!(*cur++ & 0x80))
			break;
	}
	*pcur = cur;

	return value;
}

static sleb128_t get_sleb128(const u8 **pcur, const u8 *end)
{
	const u8 *cur = *pcur;
	sleb128_t value;
	unsigned shift;

	for (shift = 0, value = 0; cur < end; shift += 7) {
		if (shift + 7 > 8 * sizeof(value)
		    && (*cur & 0x7fU) >= (1U << (8 * sizeof(value) - shift))) {
			cur = end + 1;
			break;
		}
		value |= (sleb128_t)(*cur & 0x7f) << shift;
		if (!(*cur & 0x80)) {
			value |= -(*cur++ & 0x40) << shift;
			break;
		}
	}
	*pcur = cur;

	return value;
}

static unsigned long read_pointer(const u8 **pLoc,
                                  const void *end,
                                  signed ptrType)
{
	unsigned long value = 0;
	union {
		const u8 *p8;
		const u16 *p16u;
		const s16 *p16s;
		const u32 *p32u;
		const s32 *p32s;
		const unsigned long *pul;
	} ptr;

	if (ptrType < 0 || ptrType == DW_EH_PE_omit)
		return 0;
	ptr.p8 = *pLoc;
	switch(ptrType & DW_EH_PE_FORM) {
	case DW_EH_PE_data2:
		if (end < (const void *)(ptr.p16u + 1))
			return 0;
		if(ptrType & DW_EH_PE_signed)
			value = get_unaligned(ptr.p16s++);
		else
			value = get_unaligned(ptr.p16u++);
		break;
	case DW_EH_PE_data4:
#ifdef CONFIG_64BIT
		if (end < (const void *)(ptr.p32u + 1))
			return 0;
		if(ptrType & DW_EH_PE_signed)
			value = get_unaligned(ptr.p32s++);
		else
			value = get_unaligned(ptr.p32u++);
		break;
	case DW_EH_PE_data8:
		BUILD_BUG_ON(sizeof(u64) != sizeof(value));
#else
		BUILD_BUG_ON(sizeof(u32) != sizeof(value));
#endif
	case DW_EH_PE_native:
		if (end < (const void *)(ptr.pul + 1))
			return 0;
		value = get_unaligned(ptr.pul++);
		break;
	case DW_EH_PE_leb128:
		BUILD_BUG_ON(sizeof(uleb128_t) > sizeof(value));
		value = ptrType & DW_EH_PE_signed
		        ? get_sleb128(&ptr.p8, end)
		        : get_uleb128(&ptr.p8, end);
		if ((const void *)ptr.p8 > end)
			return 0;
		break;
	default:
		return 0;
	}
	switch(ptrType & DW_EH_PE_ADJUST) {
	case DW_EH_PE_abs:
		break;
	case DW_EH_PE_pcrel:
		value += (unsigned long)*pLoc;
		break;
	default:
		return 0;
	}
	if ((ptrType & DW_EH_PE_indirect)
	    && __get_user(value, (unsigned long *)value))
		return 0;
	*pLoc = ptr.p8;

	return value;
}

static signed fde_pointer_type(const u32 *cie)
{
	const u8 *ptr = (const u8 *)(cie + 2);
	unsigned version = *ptr;

	if (version != 1)
		return -1; /* unsupported */
	if (*++ptr) {
		const char *aug;
		const u8 *end = (const u8 *)(cie + 1) + *cie;
		uleb128_t len;

		/* check if augmentation size is first (and thus present) */
		if (*ptr != 'z')
			return -1;
		/* check if augmentation string is nul-terminated */
		if ((ptr = memchr(aug = (const void *)ptr, 0, end - ptr)) == NULL)
			return -1;
		++ptr; /* skip terminator */
		get_uleb128(&ptr, end); /* skip code alignment */
		get_sleb128(&ptr, end); /* skip data alignment */
		/* skip return address column */
		version <= 1 ? (void)++ptr : (void)get_uleb128(&ptr, end);
		len = get_uleb128(&ptr, end); /* augmentation length */
		if (ptr + len < ptr || ptr + len > end)
			return -1;
		end = ptr + len;
		while (*++aug) {
			if (ptr >= end)
				return -1;
			switch(*aug) {
			case 'L':
				++ptr;
				break;
			case 'P': {
					signed ptrType = *ptr++;

					if (!read_pointer(&ptr, end, ptrType) || ptr > end)
						return -1;
				}
				break;
			case 'R':
				return *ptr;
			default:
				return -1;
			}
		}
	}
	return DW_EH_PE_native|DW_EH_PE_abs;
}

static int advance_loc(unsigned long delta, struct unwind_state *state)
{
	state->loc += delta * state->codeAlign;

	return delta > 0;
}

static void set_rule(uleb128_t reg,
                     enum item_location where,
                     uleb128_t value,
                     struct unwind_state *state)
{
	if (reg < ARRAY_SIZE(state->regs)) {
		state->regs[reg].where = where;
		state->regs[reg].value = value;
	}
}

static int processCFI(const u8 *start,
                      const u8 *end,
                      unsigned long targetLoc,
                      signed ptrType,
                      struct unwind_state *state)
{
	union {
		const u8 *p8;
		const u16 *p16;
		const u32 *p32;
	} ptr;
	int result = 1;

	if (start != state->cieStart) {
		state->loc = state->org;
		result = processCFI(state->cieStart, state->cieEnd, 0, ptrType, state);
		if (targetLoc == 0 && state->label == NULL)
			return result;
	}
	for (ptr.p8 = start; result && ptr.p8 < end; ) {
		switch(*ptr.p8 >> 6) {
			uleb128_t value;

		case 0:
			switch(*ptr.p8++) {
			case DW_CFA_nop:
				break;
			case DW_CFA_set_loc:
				if ((state->loc = read_pointer(&ptr.p8, end, ptrType)) == 0)
					result = 0;
				break;
			case DW_CFA_advance_loc1:
				result = ptr.p8 < end && advance_loc(*ptr.p8++, state);
				break;
			case DW_CFA_advance_loc2:
				result = ptr.p8 <= end + 2
				         && advance_loc(*ptr.p16++, state);
				break;
			case DW_CFA_advance_loc4:
				result = ptr.p8 <= end + 4
				         && advance_loc(*ptr.p32++, state);
				break;
			case DW_CFA_offset_extended:
				value = get_uleb128(&ptr.p8, end);
				set_rule(value, Memory, get_uleb128(&ptr.p8, end), state);
				break;
			case DW_CFA_val_offset:
				value = get_uleb128(&ptr.p8, end);
				set_rule(value, Value, get_uleb128(&ptr.p8, end), state);
				break;
			case DW_CFA_offset_extended_sf:
				value = get_uleb128(&ptr.p8, end);
				set_rule(value, Memory, get_sleb128(&ptr.p8, end), state);
				break;
			case DW_CFA_val_offset_sf:
				value = get_uleb128(&ptr.p8, end);
				set_rule(value, Value, get_sleb128(&ptr.p8, end), state);
				break;
			case DW_CFA_restore_extended:
			case DW_CFA_undefined:
			case DW_CFA_same_value:
				set_rule(get_uleb128(&ptr.p8, end), Nowhere, 0, state);
				break;
			case DW_CFA_register:
				value = get_uleb128(&ptr.p8, end);
				set_rule(value,
				         Register,
				         get_uleb128(&ptr.p8, end), state);
				break;
			case DW_CFA_remember_state:
				if (ptr.p8 == state->label) {
					state->label = NULL;
					return 1;
				}
				if (state->stackDepth >= MAX_STACK_DEPTH)
					return 0;
				state->stack[state->stackDepth++] = ptr.p8;
				break;
			case DW_CFA_restore_state:
				if (state->stackDepth) {
					const uleb128_t loc = state->loc;
					const u8 *label = state->label;

					state->label = state->stack[state->stackDepth - 1];
					memcpy(&state->cfa, &badCFA, sizeof(state->cfa));
					memset(state->regs, 0, sizeof(state->regs));
					state->stackDepth = 0;
					result = processCFI(start, end, 0, ptrType, state);
					state->loc = loc;
					state->label = label;
				} else
					return 0;
				break;
			case DW_CFA_def_cfa:
				state->cfa.reg = get_uleb128(&ptr.p8, end);
				/*nobreak*/
			case DW_CFA_def_cfa_offset:
				state->cfa.offs = get_uleb128(&ptr.p8, end);
				break;
			case DW_CFA_def_cfa_sf:
				state->cfa.reg = get_uleb128(&ptr.p8, end);
				/*nobreak*/
			case DW_CFA_def_cfa_offset_sf:
				state->cfa.offs = get_sleb128(&ptr.p8, end)
				                  * state->dataAlign;
				break;
			case DW_CFA_def_cfa_register:
				state->cfa.reg = get_uleb128(&ptr.p8, end);
				break;
			/*todo case DW_CFA_def_cfa_expression: */
			/*todo case DW_CFA_expression: */
			/*todo case DW_CFA_val_expression: */
			case DW_CFA_GNU_args_size:
				get_uleb128(&ptr.p8, end);
				break;
			case DW_CFA_GNU_negative_offset_extended:
				value = get_uleb128(&ptr.p8, end);
				set_rule(value,
				         Memory,
				         (uleb128_t)0 - get_uleb128(&ptr.p8, end), state);
				break;
			case DW_CFA_GNU_window_save:
			default:
				result = 0;
				break;
			}
			break;
		case 1:
			result = advance_loc(*ptr.p8++ & 0x3f, state);
			break;
		case 2:
			value = *ptr.p8++ & 0x3f;
			set_rule(value, Memory, get_uleb128(&ptr.p8, end), state);
			break;
		case 3:
			set_rule(*ptr.p8++ & 0x3f, Nowhere, 0, state);
			break;
		}
		if (ptr.p8 > end)
			result = 0;
		if (result && targetLoc != 0 && targetLoc < state->loc)
			return 1;
	}

	return result
	   && ptr.p8 == end
	   && (targetLoc == 0
	    || (/*todo While in theory this should apply, gcc in practice omits
	          everything past the function prolog, and hence the location
	          never reaches the end of the function.
	        targetLoc < state->loc &&*/ state->label == NULL));
}

/* Unwind to previous to frame.  Returns 0 if successful, negative
 * number in case of an error. */
int unwind(struct unwind_frame_info *frame)
{
#define FRAME_REG(r, t) (((t *)frame)[reg_info[r].offs])
	const u32 *fde = NULL, *cie = NULL;
	const u8 *ptr = NULL, *end = NULL;
	unsigned long pc = UNW_PC(frame) - frame->call_frame;
	unsigned long startLoc = 0, endLoc = 0, cfa;
	unsigned i;
	signed ptrType = -1;
	uleb128_t retAddrReg = 0;
	struct unwind_table *table;
	struct unwind_state state;

	if (UNW_PC(frame) == 0)
		return -EINVAL;
	if ((table = find_table(pc)) != NULL
	    && !(table->size & (sizeof(*fde) - 1))) {
		unsigned long tableSize = table->size;

		for (fde = table->address;
		     tableSize > sizeof(*fde) && tableSize - sizeof(*fde) >= *fde;
		     tableSize -= sizeof(*fde) + *fde,
		     fde += 1 + *fde / sizeof(*fde)) {
			if (!*fde || (*fde & (sizeof(*fde) - 1)))
				break;
			if (!fde[1])
				continue; /* this is a CIE */
			if ((fde[1] & (sizeof(*fde) - 1))
			    || fde[1] > (unsigned long)(fde + 1)
			                - (unsigned long)table->address)
				continue; /* this is not a valid FDE */
			cie = fde + 1 - fde[1] / sizeof(*fde);
			if (*cie <= sizeof(*cie) + 4
			    || *cie >= fde[1] - sizeof(*fde)
			    || (*cie & (sizeof(*cie) - 1))
			    || cie[1]
			    || (ptrType = fde_pointer_type(cie)) < 0) {
				cie = NULL; /* this is not a (valid) CIE */
				continue;
			}
			ptr = (const u8 *)(fde + 2);
			startLoc = read_pointer(&ptr,
			                        (const u8 *)(fde + 1) + *fde,
			                        ptrType);
			endLoc = startLoc
			         + read_pointer(&ptr,
			                        (const u8 *)(fde + 1) + *fde,
			                        ptrType & DW_EH_PE_indirect
			                        ? ptrType
			                        : ptrType & (DW_EH_PE_FORM|DW_EH_PE_signed));
			if (pc >= startLoc && pc < endLoc)
				break;
			cie = NULL;
		}
	}
	if (cie != NULL) {
		memset(&state, 0, sizeof(state));
		state.cieEnd = ptr; /* keep here temporarily */
		ptr = (const u8 *)(cie + 2);
		end = (const u8 *)(cie + 1) + *cie;
		frame->call_frame = 1;
		if ((state.version = *ptr) != 1)
			cie = NULL; /* unsupported version */
		else if (*++ptr) {
			/* check if augmentation size is first (and thus present) */
			if (*ptr == 'z') {
				while (++ptr < end && *ptr) {
					switch(*ptr) {
					/* check for ignorable (or already handled)
					 * nul-terminated augmentation string */
					case 'L':
					case 'P':
					case 'R':
						continue;
					case 'S':
						frame->call_frame = 0;
						continue;
					default:
						break;
					}
					break;
				}
			}
			if (ptr >= end || *ptr)
				cie = NULL;
		}
		++ptr;
	}
	if (cie != NULL) {
		/* get code aligment factor */
		state.codeAlign = get_uleb128(&ptr, end);
		/* get data aligment factor */
		state.dataAlign = get_sleb128(&ptr, end);
		if (state.codeAlign == 0 || state.dataAlign == 0 || ptr >= end)
			cie = NULL;
		else {
			retAddrReg = state.version <= 1 ? *ptr++ : get_uleb128(&ptr, end);
			/* skip augmentation */
			if (((const char *)(cie + 2))[1] == 'z')
				ptr += get_uleb128(&ptr, end);
			if (ptr > end
			   || retAddrReg >= ARRAY_SIZE(reg_info)
			   || REG_INVALID(retAddrReg)
			   || reg_info[retAddrReg].width != sizeof(unsigned long))
				cie = NULL;
		}
	}
	if (cie != NULL) {
		state.cieStart = ptr;
		ptr = state.cieEnd;
		state.cieEnd = end;
		end = (const u8 *)(fde + 1) + *fde;
		/* skip augmentation */
		if (((const char *)(cie + 2))[1] == 'z') {
			uleb128_t augSize = get_uleb128(&ptr, end);

			if ((ptr += augSize) > end)
				fde = NULL;
		}
	}
	if (cie == NULL || fde == NULL) {
#ifdef CONFIG_FRAME_POINTER
		unsigned long top, bottom;
#endif

#ifdef CONFIG_FRAME_POINTER
		top = STACK_TOP(frame->task);
		bottom = STACK_BOTTOM(frame->task);
# if FRAME_RETADDR_OFFSET < 0
		if (UNW_SP(frame) < top
		    && UNW_FP(frame) <= UNW_SP(frame)
		    && bottom < UNW_FP(frame)
# else
		if (UNW_SP(frame) > top
		    && UNW_FP(frame) >= UNW_SP(frame)
		    && bottom > UNW_FP(frame)
# endif
		   && !((UNW_SP(frame) | UNW_FP(frame))
		        & (sizeof(unsigned long) - 1))) {
			unsigned long link;

			if (!__get_user(link,
			                (unsigned long *)(UNW_FP(frame)
			                                  + FRAME_LINK_OFFSET))
# if FRAME_RETADDR_OFFSET < 0
			   && link > bottom && link < UNW_FP(frame)
# else
			   && link > UNW_FP(frame) && link < bottom
# endif
			   && !(link & (sizeof(link) - 1))
			   && !__get_user(UNW_PC(frame),
			                  (unsigned long *)(UNW_FP(frame)
			                                    + FRAME_RETADDR_OFFSET))) {
				UNW_SP(frame) = UNW_FP(frame) + FRAME_RETADDR_OFFSET
# if FRAME_RETADDR_OFFSET < 0
					-
# else
					+
# endif
					  sizeof(UNW_PC(frame));
				UNW_FP(frame) = link;
				return 0;
			}
		}
#endif
		return -ENXIO;
	}
	state.org = startLoc;
	memcpy(&state.cfa, &badCFA, sizeof(state.cfa));
	/* process instructions */
	if (!processCFI(ptr, end, pc, ptrType, &state)
	   || state.loc > endLoc
	   || state.regs[retAddrReg].where == Nowhere
	   || state.cfa.reg >= ARRAY_SIZE(reg_info)
	   || reg_info[state.cfa.reg].width != sizeof(unsigned long)
	   || state.cfa.offs % sizeof(unsigned long))
		return -EIO;
	/* update frame */
#ifndef CONFIG_AS_CFI_SIGNAL_FRAME
	if(frame->call_frame
	   && !UNW_DEFAULT_RA(state.regs[retAddrReg], state.dataAlign))
		frame->call_frame = 0;
#endif
	cfa = FRAME_REG(state.cfa.reg, unsigned long) + state.cfa.offs;
	startLoc = min((unsigned long)UNW_SP(frame), cfa);
	endLoc = max((unsigned long)UNW_SP(frame), cfa);
	if (STACK_LIMIT(startLoc) != STACK_LIMIT(endLoc)) {
		startLoc = min(STACK_LIMIT(cfa), cfa);
		endLoc = max(STACK_LIMIT(cfa), cfa);
	}
#ifndef CONFIG_64BIT
# define CASES CASE(8); CASE(16); CASE(32)
#else
# define CASES CASE(8); CASE(16); CASE(32); CASE(64)
#endif
	for (i = 0; i < ARRAY_SIZE(state.regs); ++i) {
		if (REG_INVALID(i)) {
			if (state.regs[i].where == Nowhere)
				continue;
			return -EIO;
		}
		switch(state.regs[i].where) {
		default:
			break;
		case Register:
			if (state.regs[i].value >= ARRAY_SIZE(reg_info)
			   || REG_INVALID(state.regs[i].value)
			   || reg_info[i].width > reg_info[state.regs[i].value].width)
				return -EIO;
			switch(reg_info[state.regs[i].value].width) {
#define CASE(n) \
			case sizeof(u##n): \
				state.regs[i].value = FRAME_REG(state.regs[i].value, \
				                                const u##n); \
				break
			CASES;
#undef CASE
			default:
				return -EIO;
			}
			break;
		}
	}
	for (i = 0; i < ARRAY_SIZE(state.regs); ++i) {
		if (REG_INVALID(i))
			continue;
		switch(state.regs[i].where) {
		case Nowhere:
			if (reg_info[i].width != sizeof(UNW_SP(frame))
			   || &FRAME_REG(i, __typeof__(UNW_SP(frame)))
			      != &UNW_SP(frame))
				continue;
			UNW_SP(frame) = cfa;
			break;
		case Register:
			switch(reg_info[i].width) {
#define CASE(n) case sizeof(u##n): \
				FRAME_REG(i, u##n) = state.regs[i].value; \
				break
			CASES;
#undef CASE
			default:
				return -EIO;
			}
			break;
		case Value:
			if (reg_info[i].width != sizeof(unsigned long))
				return -EIO;
			FRAME_REG(i, unsigned long) = cfa + state.regs[i].value
			                                    * state.dataAlign;
			break;
		case Memory: {
				unsigned long addr = cfa + state.regs[i].value
				                           * state.dataAlign;

				if ((state.regs[i].value * state.dataAlign)
				    % sizeof(unsigned long)
				    || addr < startLoc
				    || addr + sizeof(unsigned long) < addr
				    || addr + sizeof(unsigned long) > endLoc)
					return -EIO;
				switch(reg_info[i].width) {
#define CASE(n)     case sizeof(u##n): \
					__get_user(FRAME_REG(i, u##n), (u##n *)addr); \
					break
				CASES;
#undef CASE
				default:
					return -EIO;
				}
			}
			break;
		}
	}

	return 0;
#undef CASES
#undef FRAME_REG
}
EXPORT_SYMBOL(unwind);

int unwind_init_frame_info(struct unwind_frame_info *info,
                           struct task_struct *tsk,
                           /*const*/ struct pt_regs *regs)
{
	info->task = tsk;
	info->call_frame = 0;
	arch_unw_init_frame_info(info, regs);

	return 0;
}
EXPORT_SYMBOL(unwind_init_frame_info);

/*
 * Prepare to unwind a blocked task.
 */
int unwind_init_blocked(struct unwind_frame_info *info,
                        struct task_struct *tsk)
{
	info->task = tsk;
	info->call_frame = 0;
	arch_unw_init_blocked(info);

	return 0;
}
EXPORT_SYMBOL(unwind_init_blocked);

/*
 * Prepare to unwind the currently running thread.
 */
int unwind_init_running(struct unwind_frame_info *info,
                        asmlinkage int (*callback)(struct unwind_frame_info *,
                                                   void *arg),
                        void *arg)
{
	info->task = current;
	info->call_frame = 0;

	return arch_unwind_init_running(info, callback, arg);
}
EXPORT_SYMBOL(unwind_init_running);

/*
 * Unwind until the return pointer is in user-land (or until an error
 * occurs).  Returns 0 if successful, negative number in case of
 * error.
 */
int unwind_to_user(struct unwind_frame_info *info)
{
	while (!arch_unw_user_mode(info)) {
		int err = unwind(info);

		if (err < 0)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL(unwind_to_user);
