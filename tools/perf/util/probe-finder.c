/*
 * probe-finder.c : C expression to kprobe event converter
 *
 * Written by Masami Hiramatsu <mhiramat@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#include "event.h"
#include "debug.h"
#include "util.h"
#include "probe-finder.h"


/* Dwarf_Die Linkage to parent Die */
struct die_link {
	struct die_link *parent;	/* Parent die */
	Dwarf_Die die;			/* Current die */
};

static Dwarf_Debug __dw_debug;
static Dwarf_Error __dw_error;

/*
 * Generic dwarf analysis helpers
 */

#define X86_32_MAX_REGS 8
const char *x86_32_regs_table[X86_32_MAX_REGS] = {
	"%ax",
	"%cx",
	"%dx",
	"%bx",
	"$stack",	/* Stack address instead of %sp */
	"%bp",
	"%si",
	"%di",
};

#define X86_64_MAX_REGS 16
const char *x86_64_regs_table[X86_64_MAX_REGS] = {
	"%ax",
	"%dx",
	"%cx",
	"%bx",
	"%si",
	"%di",
	"%bp",
	"%sp",
	"%r8",
	"%r9",
	"%r10",
	"%r11",
	"%r12",
	"%r13",
	"%r14",
	"%r15",
};

/* TODO: switching by dwarf address size */
#ifdef __x86_64__
#define ARCH_MAX_REGS X86_64_MAX_REGS
#define arch_regs_table x86_64_regs_table
#else
#define ARCH_MAX_REGS X86_32_MAX_REGS
#define arch_regs_table x86_32_regs_table
#endif

/* Return architecture dependent register string (for kprobe-tracer) */
static const char *get_arch_regstr(unsigned int n)
{
	return (n <= ARCH_MAX_REGS) ? arch_regs_table[n] : NULL;
}

/*
 * Compare the tail of two strings.
 * Return 0 if whole of either string is same as another's tail part.
 */
static int strtailcmp(const char *s1, const char *s2)
{
	int i1 = strlen(s1);
	int i2 = strlen(s2);
	while (--i1 > 0 && --i2 > 0) {
		if (s1[i1] != s2[i2])
			return s1[i1] - s2[i2];
	}
	return 0;
}

/* Find the fileno of the target file. */
static Dwarf_Unsigned die_get_fileno(Dwarf_Die cu_die, const char *fname)
{
	Dwarf_Signed cnt, i;
	Dwarf_Unsigned found = 0;
	char **srcs;
	int ret;

	if (!fname)
		return 0;

	ret = dwarf_srcfiles(cu_die, &srcs, &cnt, &__dw_error);
	if (ret == DW_DLV_OK) {
		for (i = 0; i < cnt && !found; i++) {
			if (strtailcmp(srcs[i], fname) == 0)
				found = i + 1;
			dwarf_dealloc(__dw_debug, srcs[i], DW_DLA_STRING);
		}
		for (; i < cnt; i++)
			dwarf_dealloc(__dw_debug, srcs[i], DW_DLA_STRING);
		dwarf_dealloc(__dw_debug, srcs, DW_DLA_LIST);
	}
	if (found)
		pr_debug("found fno: %d\n", (int)found);
	return found;
}

/* Compare diename and tname */
static int die_compare_name(Dwarf_Die dw_die, const char *tname)
{
	char *name;
	int ret;
	ret = dwarf_diename(dw_die, &name, &__dw_error);
	DIE_IF(ret == DW_DLV_ERROR);
	if (ret == DW_DLV_OK) {
		ret = strcmp(tname, name);
		dwarf_dealloc(__dw_debug, name, DW_DLA_STRING);
	} else
		ret = -1;
	return ret;
}

/* Check the address is in the subprogram(function). */
static int die_within_subprogram(Dwarf_Die sp_die, Dwarf_Addr addr,
				 Dwarf_Signed *offs)
{
	Dwarf_Addr lopc, hipc;
	int ret;

	/* TODO: check ranges */
	ret = dwarf_lowpc(sp_die, &lopc, &__dw_error);
	DIE_IF(ret == DW_DLV_ERROR);
	if (ret == DW_DLV_NO_ENTRY)
		return 0;
	ret = dwarf_highpc(sp_die, &hipc, &__dw_error);
	DIE_IF(ret != DW_DLV_OK);
	if (lopc <= addr && addr < hipc) {
		*offs = addr - lopc;
		return 1;
	} else
		return 0;
}

/* Check the die is inlined function */
static Dwarf_Bool die_inlined_subprogram(Dwarf_Die dw_die)
{
	/* TODO: check strictly */
	Dwarf_Bool inl;
	int ret;

	ret = dwarf_hasattr(dw_die, DW_AT_inline, &inl, &__dw_error);
	DIE_IF(ret == DW_DLV_ERROR);
	return inl;
}

/* Get the offset of abstruct_origin */
static Dwarf_Off die_get_abstract_origin(Dwarf_Die dw_die)
{
	Dwarf_Attribute attr;
	Dwarf_Off cu_offs;
	int ret;

	ret = dwarf_attr(dw_die, DW_AT_abstract_origin, &attr, &__dw_error);
	DIE_IF(ret != DW_DLV_OK);
	ret = dwarf_formref(attr, &cu_offs, &__dw_error);
	DIE_IF(ret != DW_DLV_OK);
	dwarf_dealloc(__dw_debug, attr, DW_DLA_ATTR);
	return cu_offs;
}

/* Get entry pc(or low pc, 1st entry of ranges)  of the die */
static Dwarf_Addr die_get_entrypc(Dwarf_Die dw_die)
{
	Dwarf_Attribute attr;
	Dwarf_Addr addr;
	Dwarf_Off offs;
	Dwarf_Ranges *ranges;
	Dwarf_Signed cnt;
	int ret;

	/* Try to get entry pc */
	ret = dwarf_attr(dw_die, DW_AT_entry_pc, &attr, &__dw_error);
	DIE_IF(ret == DW_DLV_ERROR);
	if (ret == DW_DLV_OK) {
		ret = dwarf_formaddr(attr, &addr, &__dw_error);
		DIE_IF(ret != DW_DLV_OK);
		dwarf_dealloc(__dw_debug, attr, DW_DLA_ATTR);
		return addr;
	}

	/* Try to get low pc */
	ret = dwarf_lowpc(dw_die, &addr, &__dw_error);
	DIE_IF(ret == DW_DLV_ERROR);
	if (ret == DW_DLV_OK)
		return addr;

	/* Try to get ranges */
	ret = dwarf_attr(dw_die, DW_AT_ranges, &attr, &__dw_error);
	DIE_IF(ret != DW_DLV_OK);
	ret = dwarf_formref(attr, &offs, &__dw_error);
	DIE_IF(ret != DW_DLV_OK);
	ret = dwarf_get_ranges(__dw_debug, offs, &ranges, &cnt, NULL,
				&__dw_error);
	DIE_IF(ret != DW_DLV_OK);
	addr = ranges[0].dwr_addr1;
	dwarf_ranges_dealloc(__dw_debug, ranges, cnt);
	return addr;
}

/*
 * Search a Die from Die tree.
 * Note: cur_link->die should be deallocated in this function.
 */
static int __search_die_tree(struct die_link *cur_link,
			     int (*die_cb)(struct die_link *, void *),
			     void *data)
{
	Dwarf_Die new_die;
	struct die_link new_link;
	int ret;

	if (!die_cb)
		return 0;

	/* Check current die */
	while (!(ret = die_cb(cur_link, data))) {
		/* Check child die */
		ret = dwarf_child(cur_link->die, &new_die, &__dw_error);
		DIE_IF(ret == DW_DLV_ERROR);
		if (ret == DW_DLV_OK) {
			new_link.parent = cur_link;
			new_link.die = new_die;
			ret = __search_die_tree(&new_link, die_cb, data);
			if (ret)
				break;
		}

		/* Move to next sibling */
		ret = dwarf_siblingof(__dw_debug, cur_link->die, &new_die,
				      &__dw_error);
		DIE_IF(ret == DW_DLV_ERROR);
		dwarf_dealloc(__dw_debug, cur_link->die, DW_DLA_DIE);
		cur_link->die = new_die;
		if (ret == DW_DLV_NO_ENTRY)
			return 0;
	}
	dwarf_dealloc(__dw_debug, cur_link->die, DW_DLA_DIE);
	return ret;
}

/* Search a die in its children's die tree */
static int search_die_from_children(Dwarf_Die parent_die,
				    int (*die_cb)(struct die_link *, void *),
				    void *data)
{
	struct die_link new_link;
	int ret;

	new_link.parent = NULL;
	ret = dwarf_child(parent_die, &new_link.die, &__dw_error);
	DIE_IF(ret == DW_DLV_ERROR);
	if (ret == DW_DLV_OK)
		return __search_die_tree(&new_link, die_cb, data);
	else
		return 0;
}

/* Find a locdesc corresponding to the address */
static int attr_get_locdesc(Dwarf_Attribute attr, Dwarf_Locdesc *desc,
			    Dwarf_Addr addr)
{
	Dwarf_Signed lcnt;
	Dwarf_Locdesc **llbuf;
	int ret, i;

	ret = dwarf_loclist_n(attr, &llbuf, &lcnt, &__dw_error);
	DIE_IF(ret != DW_DLV_OK);
	ret = DW_DLV_NO_ENTRY;
	for (i = 0; i < lcnt; ++i) {
		if (llbuf[i]->ld_lopc <= addr &&
		    llbuf[i]->ld_hipc > addr) {
			memcpy(desc, llbuf[i], sizeof(Dwarf_Locdesc));
			desc->ld_s =
				malloc(sizeof(Dwarf_Loc) * llbuf[i]->ld_cents);
			DIE_IF(desc->ld_s == NULL);
			memcpy(desc->ld_s, llbuf[i]->ld_s,
				sizeof(Dwarf_Loc) * llbuf[i]->ld_cents);
			ret = DW_DLV_OK;
			break;
		}
		dwarf_dealloc(__dw_debug, llbuf[i]->ld_s, DW_DLA_LOC_BLOCK);
		dwarf_dealloc(__dw_debug, llbuf[i], DW_DLA_LOCDESC);
	}
	/* Releasing loop */
	for (; i < lcnt; ++i) {
		dwarf_dealloc(__dw_debug, llbuf[i]->ld_s, DW_DLA_LOC_BLOCK);
		dwarf_dealloc(__dw_debug, llbuf[i], DW_DLA_LOCDESC);
	}
	dwarf_dealloc(__dw_debug, llbuf, DW_DLA_LIST);
	return ret;
}

/*
 * Probe finder related functions
 */

/* Show a location */
static void show_location(Dwarf_Loc *loc, struct probe_finder *pf)
{
	Dwarf_Small op;
	Dwarf_Unsigned regn;
	Dwarf_Signed offs;
	int deref = 0, ret;
	const char *regs;

	op = loc->lr_atom;

	/* If this is based on frame buffer, set the offset */
	if (op == DW_OP_fbreg) {
		deref = 1;
		offs = (Dwarf_Signed)loc->lr_number;
		op = pf->fbloc.ld_s[0].lr_atom;
		loc = &pf->fbloc.ld_s[0];
	} else
		offs = 0;

	if (op >= DW_OP_breg0 && op <= DW_OP_breg31) {
		regn = op - DW_OP_breg0;
		offs += (Dwarf_Signed)loc->lr_number;
		deref = 1;
	} else if (op >= DW_OP_reg0 && op <= DW_OP_reg31) {
		regn = op - DW_OP_reg0;
	} else if (op == DW_OP_bregx) {
		regn = loc->lr_number;
		offs += (Dwarf_Signed)loc->lr_number2;
		deref = 1;
	} else if (op == DW_OP_regx) {
		regn = loc->lr_number;
	} else
		die("Dwarf_OP %d is not supported.\n", op);

	regs = get_arch_regstr(regn);
	if (!regs)
		die("%lld exceeds max register number.\n", regn);

	if (deref)
		ret = snprintf(pf->buf, pf->len,
				 " %s=%+lld(%s)", pf->var, offs, regs);
	else
		ret = snprintf(pf->buf, pf->len, " %s=%s", pf->var, regs);
	DIE_IF(ret < 0);
	DIE_IF(ret >= pf->len);
}

/* Show a variables in kprobe event format */
static void show_variable(Dwarf_Die vr_die, struct probe_finder *pf)
{
	Dwarf_Attribute attr;
	Dwarf_Locdesc ld;
	int ret;

	ret = dwarf_attr(vr_die, DW_AT_location, &attr, &__dw_error);
	if (ret != DW_DLV_OK)
		goto error;
	ret = attr_get_locdesc(attr, &ld, (pf->addr - pf->cu_base));
	if (ret != DW_DLV_OK)
		goto error;
	/* TODO? */
	DIE_IF(ld.ld_cents != 1);
	show_location(&ld.ld_s[0], pf);
	free(ld.ld_s);
	dwarf_dealloc(__dw_debug, attr, DW_DLA_ATTR);
	return ;
error:
	die("Failed to find the location of %s at this address.\n"
	    " Perhaps, it has been optimized out.\n", pf->var);
}

static int variable_callback(struct die_link *dlink, void *data)
{
	struct probe_finder *pf = (struct probe_finder *)data;
	Dwarf_Half tag;
	int ret;

	ret = dwarf_tag(dlink->die, &tag, &__dw_error);
	DIE_IF(ret == DW_DLV_ERROR);
	if ((tag == DW_TAG_formal_parameter ||
	     tag == DW_TAG_variable) &&
	    (die_compare_name(dlink->die, pf->var) == 0)) {
		show_variable(dlink->die, pf);
		return 1;
	}
	/* TODO: Support struct members and arrays */
	return 0;
}

/* Find a variable in a subprogram die */
static void find_variable(Dwarf_Die sp_die, struct probe_finder *pf)
{
	int ret;

	if (!is_c_varname(pf->var)) {
		/* Output raw parameters */
		ret = snprintf(pf->buf, pf->len, " %s", pf->var);
		DIE_IF(ret < 0);
		DIE_IF(ret >= pf->len);
		return ;
	}

	pr_debug("Searching '%s' variable in context.\n", pf->var);
	/* Search child die for local variables and parameters. */
	ret = search_die_from_children(sp_die, variable_callback, pf);
	if (!ret)
		die("Failed to find '%s' in this function.\n", pf->var);
}

/* Get a frame base on the address */
static void get_current_frame_base(Dwarf_Die sp_die, struct probe_finder *pf)
{
	Dwarf_Attribute attr;
	int ret;

	ret = dwarf_attr(sp_die, DW_AT_frame_base, &attr, &__dw_error);
	DIE_IF(ret != DW_DLV_OK);
	ret = attr_get_locdesc(attr, &pf->fbloc, (pf->addr - pf->cu_base));
	DIE_IF(ret != DW_DLV_OK);
	dwarf_dealloc(__dw_debug, attr, DW_DLA_ATTR);
}

static void free_current_frame_base(struct probe_finder *pf)
{
	free(pf->fbloc.ld_s);
	memset(&pf->fbloc, 0, sizeof(Dwarf_Locdesc));
}

/* Show a probe point to output buffer */
static void show_probepoint(Dwarf_Die sp_die, Dwarf_Signed offs,
			    struct probe_finder *pf)
{
	struct probe_point *pp = pf->pp;
	char *name;
	char tmp[MAX_PROBE_BUFFER];
	int ret, i, len;

	/* Output name of probe point */
	ret = dwarf_diename(sp_die, &name, &__dw_error);
	DIE_IF(ret == DW_DLV_ERROR);
	if (ret == DW_DLV_OK) {
		ret = snprintf(tmp, MAX_PROBE_BUFFER, "%s+%u", name,
				(unsigned int)offs);
		/* Copy the function name if possible */
		if (!pp->function) {
			pp->function = strdup(name);
			pp->offset = offs;
		}
		dwarf_dealloc(__dw_debug, name, DW_DLA_STRING);
	} else {
		/* This function has no name. */
		ret = snprintf(tmp, MAX_PROBE_BUFFER, "0x%llx", pf->addr);
		if (!pp->function) {
			/* TODO: Use _stext */
			pp->function = strdup("");
			pp->offset = (int)pf->addr;
		}
	}
	DIE_IF(ret < 0);
	DIE_IF(ret >= MAX_PROBE_BUFFER);
	len = ret;

	/* Find each argument */
	get_current_frame_base(sp_die, pf);
	for (i = 0; i < pp->nr_args; i++) {
		pf->var = pp->args[i];
		pf->buf = &tmp[len];
		pf->len = MAX_PROBE_BUFFER - len;
		find_variable(sp_die, pf);
		len += strlen(pf->buf);
	}
	free_current_frame_base(pf);

	pp->probes[pp->found] = strdup(tmp);
	pp->found++;
}

static int probeaddr_callback(struct die_link *dlink, void *data)
{
	struct probe_finder *pf = (struct probe_finder *)data;
	Dwarf_Half tag;
	Dwarf_Signed offs;
	int ret;

	ret = dwarf_tag(dlink->die, &tag, &__dw_error);
	DIE_IF(ret == DW_DLV_ERROR);
	/* Check the address is in this subprogram */
	if (tag == DW_TAG_subprogram &&
	    die_within_subprogram(dlink->die, pf->addr, &offs)) {
		show_probepoint(dlink->die, offs, pf);
		return 1;
	}
	return 0;
}

/* Find probe point from its line number */
static void find_by_line(Dwarf_Die cu_die, struct probe_finder *pf)
{
	struct probe_point *pp = pf->pp;
	Dwarf_Signed cnt, i;
	Dwarf_Line *lines;
	Dwarf_Unsigned lineno = 0;
	Dwarf_Addr addr;
	Dwarf_Unsigned fno;
	int ret;

	ret = dwarf_srclines(cu_die, &lines, &cnt, &__dw_error);
	DIE_IF(ret != DW_DLV_OK);

	for (i = 0; i < cnt; i++) {
		ret = dwarf_line_srcfileno(lines[i], &fno, &__dw_error);
		DIE_IF(ret != DW_DLV_OK);
		if (fno != pf->fno)
			continue;

		ret = dwarf_lineno(lines[i], &lineno, &__dw_error);
		DIE_IF(ret != DW_DLV_OK);
		if (lineno != (Dwarf_Unsigned)pp->line)
			continue;

		ret = dwarf_lineaddr(lines[i], &addr, &__dw_error);
		DIE_IF(ret != DW_DLV_OK);
		pr_debug("Probe point found: 0x%llx\n", addr);
		pf->addr = addr;
		/* Search a real subprogram including this line, */
		ret = search_die_from_children(cu_die, probeaddr_callback, pf);
		if (ret == 0)
			die("Probe point is not found in subprograms.\n");
		/* Continuing, because target line might be inlined. */
	}
	dwarf_srclines_dealloc(__dw_debug, lines, cnt);
}

/* Search function from function name */
static int probefunc_callback(struct die_link *dlink, void *data)
{
	struct probe_finder *pf = (struct probe_finder *)data;
	struct probe_point *pp = pf->pp;
	struct die_link *lk;
	Dwarf_Signed offs;
	Dwarf_Half tag;
	int ret;

	ret = dwarf_tag(dlink->die, &tag, &__dw_error);
	DIE_IF(ret == DW_DLV_ERROR);
	if (tag == DW_TAG_subprogram) {
		if (die_compare_name(dlink->die, pp->function) == 0) {
			if (die_inlined_subprogram(dlink->die)) {
				/* Inlined function, save it. */
				ret = dwarf_die_CU_offset(dlink->die,
							  &pf->inl_offs,
							  &__dw_error);
				DIE_IF(ret != DW_DLV_OK);
				pr_debug("inline definition offset %lld\n",
					 pf->inl_offs);
				return 0;	/* Continue to search */
			}
			/* Get probe address */
			pf->addr = die_get_entrypc(dlink->die);
			pf->addr += pp->offset;
			/* TODO: Check the address in this function */
			show_probepoint(dlink->die, pp->offset, pf);
			return 1; /* Exit; no same symbol in this CU. */
		}
	} else if (tag == DW_TAG_inlined_subroutine && pf->inl_offs) {
		if (die_get_abstract_origin(dlink->die) == pf->inl_offs) {
			/* Get probe address */
			pf->addr = die_get_entrypc(dlink->die);
			pf->addr += pp->offset;
			pr_debug("found inline addr: 0x%llx\n", pf->addr);
			/* Inlined function. Get a real subprogram */
			for (lk = dlink->parent; lk != NULL; lk = lk->parent) {
				tag = 0;
				dwarf_tag(lk->die, &tag, &__dw_error);
				DIE_IF(ret == DW_DLV_ERROR);
				if (tag == DW_TAG_subprogram &&
				    !die_inlined_subprogram(lk->die))
					goto found;
			}
			die("Failed to find real subprogram.\n");
found:
			/* Get offset from subprogram */
			ret = die_within_subprogram(lk->die, pf->addr, &offs);
			DIE_IF(!ret);
			show_probepoint(lk->die, offs, pf);
			/* Continue to search */
		}
	}
	return 0;
}

static void find_by_func(Dwarf_Die cu_die, struct probe_finder *pf)
{
	search_die_from_children(cu_die, probefunc_callback, pf);
}

/* Find a probe point */
int find_probepoint(int fd, struct probe_point *pp)
{
	Dwarf_Half addr_size = 0;
	Dwarf_Unsigned next_cuh = 0;
	Dwarf_Die cu_die = 0;
	int cu_number = 0, ret;
	struct probe_finder pf = {.pp = pp};

	ret = dwarf_init(fd, DW_DLC_READ, 0, 0, &__dw_debug, &__dw_error);
	if (ret != DW_DLV_OK)
		die("Failed to call dwarf_init(). Maybe, not a dwarf file.\n");

	pp->found = 0;
	while (++cu_number) {
		/* Search CU (Compilation Unit) */
		ret = dwarf_next_cu_header(__dw_debug, NULL, NULL, NULL,
			&addr_size, &next_cuh, &__dw_error);
		DIE_IF(ret == DW_DLV_ERROR);
		if (ret == DW_DLV_NO_ENTRY)
			break;

		/* Get the DIE(Debugging Information Entry) of this CU */
		ret = dwarf_siblingof(__dw_debug, 0, &cu_die, &__dw_error);
		DIE_IF(ret != DW_DLV_OK);

		/* Check if target file is included. */
		if (pp->file)
			pf.fno = die_get_fileno(cu_die, pp->file);

		if (!pp->file || pf.fno) {
			/* Save CU base address (for frame_base) */
			ret = dwarf_lowpc(cu_die, &pf.cu_base, &__dw_error);
			DIE_IF(ret == DW_DLV_ERROR);
			if (ret == DW_DLV_NO_ENTRY)
				pf.cu_base = 0;
			if (pp->line)
				find_by_line(cu_die, &pf);
			if (pp->function)
				find_by_func(cu_die, &pf);
		}
		dwarf_dealloc(__dw_debug, cu_die, DW_DLA_DIE);
	}
	ret = dwarf_finish(__dw_debug, &__dw_error);
	DIE_IF(ret != DW_DLV_OK);

	return pp->found;
}

