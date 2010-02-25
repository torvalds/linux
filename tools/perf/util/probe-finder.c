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
	while (--i1 >= 0 && --i2 >= 0) {
		if (s1[i1] != s2[i2])
			return s1[i1] - s2[i2];
	}
	return 0;
}

/* Find the fileno of the target file. */
static int cu_find_fileno(Dwarf_Die *cu_die, const char *fname)
{
	Dwarf_Files *files;
	size_t nfiles, i;
	const char *src;
	int ret;

	if (!fname)
		return -EINVAL;

	ret = dwarf_getsrcfiles(cu_die, &files, &nfiles);
	if (ret == 0) {
		for (i = 0; i < nfiles; i++) {
			src = dwarf_filesrc(files, i, NULL, NULL);
			if (strtailcmp(src, fname) == 0) {
				ret = (int)i;	/*???: +1 or not?*/
				break;
			}
		}
		if (ret)
			pr_debug("found fno: %d\n", ret);
	}
	return ret;
}

struct __addr_die_search_param {
	Dwarf_Addr	addr;
	Dwarf_Die	*die_mem;
};

static int __die_search_func_cb(Dwarf_Die *fn_die, void *data)
{
	struct __addr_die_search_param *ad = data;

	if (dwarf_tag(fn_die) == DW_TAG_subprogram &&
	    dwarf_haspc(fn_die, ad->addr)) {
		memcpy(ad->die_mem, fn_die, sizeof(Dwarf_Die));
		return DWARF_CB_ABORT;
	}
	return DWARF_CB_OK;
}

/* Search a real subprogram including this line, */
static Dwarf_Die *die_get_real_subprogram(Dwarf_Die *cu_die, Dwarf_Addr addr,
					  Dwarf_Die *die_mem)
{
	struct __addr_die_search_param ad;
	ad.addr = addr;
	ad.die_mem = die_mem;
	/* dwarf_getscopes can't find subprogram. */
	if (!dwarf_getfuncs(cu_die, __die_search_func_cb, &ad, 0))
		return NULL;
	else
		return die_mem;
}

/* Similar to dwarf_getfuncs, but returns inlined_subroutine if exists. */
static Dwarf_Die *die_get_inlinefunc(Dwarf_Die *sp_die, Dwarf_Addr addr,
				     Dwarf_Die *die_mem)
{
	Dwarf_Die child_die;
	int ret;

	ret = dwarf_child(sp_die, die_mem);
	if (ret != 0)
		return NULL;

	do {
		if (dwarf_tag(die_mem) == DW_TAG_inlined_subroutine &&
		    dwarf_haspc(die_mem, addr))
			return die_mem;

		if (die_get_inlinefunc(die_mem, addr, &child_die)) {
			memcpy(die_mem, &child_die, sizeof(Dwarf_Die));
			return die_mem;
		}
	} while (dwarf_siblingof(die_mem, die_mem) == 0);

	return NULL;
}

/* Compare diename and tname */
static bool die_compare_name(Dwarf_Die *dw_die, const char *tname)
{
	const char *name;
	name = dwarf_diename(dw_die);
	DIE_IF(name == NULL);
	return strcmp(tname, name);
}

/* Get entry pc(or low pc, 1st entry of ranges)  of the die */
static Dwarf_Addr die_get_entrypc(Dwarf_Die *dw_die)
{
	Dwarf_Addr epc;
	int ret;

	ret = dwarf_entrypc(dw_die, &epc);
	DIE_IF(ret == -1);
	return epc;
}

/* Get a variable die */
static Dwarf_Die *die_find_variable(Dwarf_Die *sp_die, const char *name,
				    Dwarf_Die *die_mem)
{
	Dwarf_Die child_die;
	int tag;
	int ret;

	ret = dwarf_child(sp_die, die_mem);
	if (ret != 0)
		return NULL;

	do {
		tag = dwarf_tag(die_mem);
		if ((tag == DW_TAG_formal_parameter ||
		     tag == DW_TAG_variable) &&
		    (die_compare_name(die_mem, name) == 0))
			return die_mem;

		if (die_find_variable(die_mem, name, &child_die)) {
			memcpy(die_mem, &child_die, sizeof(Dwarf_Die));
			return die_mem;
		}
	} while (dwarf_siblingof(die_mem, die_mem) == 0);

	return NULL;
}

/*
 * Probe finder related functions
 */

/* Show a location */
static void show_location(Dwarf_Op *op, struct probe_finder *pf)
{
	unsigned int regn;
	Dwarf_Word offs = 0;
	int deref = 0, ret;
	const char *regs;

	/* TODO: support CFA */
	/* If this is based on frame buffer, set the offset */
	if (op->atom == DW_OP_fbreg) {
		if (pf->fb_ops == NULL)
			die("The attribute of frame base is not supported.\n");
		deref = 1;
		offs = op->number;
		op = &pf->fb_ops[0];
	}

	if (op->atom >= DW_OP_breg0 && op->atom <= DW_OP_breg31) {
		regn = op->atom - DW_OP_breg0;
		offs += op->number;
		deref = 1;
	} else if (op->atom >= DW_OP_reg0 && op->atom <= DW_OP_reg31) {
		regn = op->atom - DW_OP_reg0;
	} else if (op->atom == DW_OP_bregx) {
		regn = op->number;
		offs += op->number2;
		deref = 1;
	} else if (op->atom == DW_OP_regx) {
		regn = op->number;
	} else
		die("DW_OP %d is not supported.", op->atom);

	regs = get_arch_regstr(regn);
	if (!regs)
		die("%u exceeds max register number.", regn);

	if (deref)
		ret = snprintf(pf->buf, pf->len, " %s=+%ju(%s)",
			       pf->var, (uintmax_t)offs, regs);
	else
		ret = snprintf(pf->buf, pf->len, " %s=%s", pf->var, regs);
	DIE_IF(ret < 0);
	DIE_IF(ret >= pf->len);
}

/* Show a variables in kprobe event format */
static void show_variable(Dwarf_Die *vr_die, struct probe_finder *pf)
{
	Dwarf_Attribute attr;
	Dwarf_Op *expr;
	size_t nexpr;
	int ret;

	if (dwarf_attr(vr_die, DW_AT_location, &attr) == NULL)
		goto error;
	/* TODO: handle more than 1 exprs */
	ret = dwarf_getlocation_addr(&attr, (pf->addr - pf->cu_base),
				     &expr, &nexpr, 1);
	if (ret <= 0 || nexpr == 0)
		goto error;

	show_location(expr, pf);
	/* *expr will be cached in libdw. Don't free it. */
	return ;
error:
	/* TODO: Support const_value */
	die("Failed to find the location of %s at this address.\n"
	    " Perhaps, it has been optimized out.", pf->var);
}

/* Find a variable in a subprogram die */
static void find_variable(Dwarf_Die *sp_die, struct probe_finder *pf)
{
	int ret;
	Dwarf_Die vr_die;

	/* TODO: Support struct members and arrays */
	if (!is_c_varname(pf->var)) {
		/* Output raw parameters */
		ret = snprintf(pf->buf, pf->len, " %s", pf->var);
		DIE_IF(ret < 0);
		DIE_IF(ret >= pf->len);
		return ;
	}

	pr_debug("Searching '%s' variable in context.\n", pf->var);
	/* Search child die for local variables and parameters. */
	if (!die_find_variable(sp_die, pf->var, &vr_die))
		die("Failed to find '%s' in this function.", pf->var);

	show_variable(&vr_die, pf);
}

/* Show a probe point to output buffer */
static void show_probe_point(Dwarf_Die *sp_die, struct probe_finder *pf)
{
	struct probe_point *pp = pf->pp;
	Dwarf_Addr eaddr;
	Dwarf_Die die_mem;
	const char *name;
	char tmp[MAX_PROBE_BUFFER];
	int ret, i, len;
	Dwarf_Attribute fb_attr;
	size_t nops;

	/* If no real subprogram, find a real one */
	if (!sp_die || dwarf_tag(sp_die) != DW_TAG_subprogram) {
		sp_die = die_get_real_subprogram(&pf->cu_die,
						 pf->addr, &die_mem);
		if (!sp_die)
			die("Probe point is not found in subprograms.");
	}

	/* Output name of probe point */
	name = dwarf_diename(sp_die);
	if (name) {
		dwarf_entrypc(sp_die, &eaddr);
		ret = snprintf(tmp, MAX_PROBE_BUFFER, "%s+%lu", name,
				(unsigned long)(pf->addr - eaddr));
		/* Copy the function name if possible */
		if (!pp->function) {
			pp->function = strdup(name);
			pp->offset = (size_t)(pf->addr - eaddr);
		}
	} else {
		/* This function has no name. */
		ret = snprintf(tmp, MAX_PROBE_BUFFER, "0x%jx",
			       (uintmax_t)pf->addr);
		if (!pp->function) {
			/* TODO: Use _stext */
			pp->function = strdup("");
			pp->offset = (size_t)pf->addr;
		}
	}
	DIE_IF(ret < 0);
	DIE_IF(ret >= MAX_PROBE_BUFFER);
	len = ret;
	pr_debug("Probe point found: %s\n", tmp);

	/* Get the frame base attribute/ops */
	dwarf_attr(sp_die, DW_AT_frame_base, &fb_attr);
	ret = dwarf_getlocation_addr(&fb_attr, (pf->addr - pf->cu_base),
				     &pf->fb_ops, &nops, 1);
	if (ret <= 0 || nops == 0)
		pf->fb_ops = NULL;

	/* Find each argument */
	/* TODO: use dwarf_cfi_addrframe */
	for (i = 0; i < pp->nr_args; i++) {
		pf->var = pp->args[i];
		pf->buf = &tmp[len];
		pf->len = MAX_PROBE_BUFFER - len;
		find_variable(sp_die, pf);
		len += strlen(pf->buf);
	}

	/* *pf->fb_ops will be cached in libdw. Don't free it. */
	pf->fb_ops = NULL;

	pp->probes[pp->found] = strdup(tmp);
	pp->found++;
}

/* Find probe point from its line number */
static void find_probe_point_by_line(struct probe_finder *pf)
{
	Dwarf_Lines *lines;
	Dwarf_Line *line;
	size_t nlines, i;
	Dwarf_Addr addr;
	int lineno;
	int ret;

	ret = dwarf_getsrclines(&pf->cu_die, &lines, &nlines);
	DIE_IF(ret != 0);

	for (i = 0; i < nlines; i++) {
		line = dwarf_onesrcline(lines, i);
		dwarf_lineno(line, &lineno);
		if (lineno != pf->lno)
			continue;

		/* TODO: Get fileno from line, but how? */
		if (strtailcmp(dwarf_linesrc(line, NULL, NULL), pf->fname) != 0)
			continue;

		ret = dwarf_lineaddr(line, &addr);
		DIE_IF(ret != 0);
		pr_debug("Probe line found: line[%d]:%d addr:0x%jx\n",
			 (int)i, lineno, (uintmax_t)addr);
		pf->addr = addr;

		show_probe_point(NULL, pf);
		/* Continuing, because target line might be inlined. */
	}
}

static int probe_point_inline_cb(Dwarf_Die *in_die, void *data)
{
	struct probe_finder *pf = (struct probe_finder *)data;
	struct probe_point *pp = pf->pp;

	/* Get probe address */
	pf->addr = die_get_entrypc(in_die);
	pf->addr += pp->offset;
	pr_debug("found inline addr: 0x%jx\n", (uintmax_t)pf->addr);

	show_probe_point(in_die, pf);
	return DWARF_CB_OK;
}

/* Search function from function name */
static int probe_point_search_cb(Dwarf_Die *sp_die, void *data)
{
	struct probe_finder *pf = (struct probe_finder *)data;
	struct probe_point *pp = pf->pp;

	/* Check tag and diename */
	if (dwarf_tag(sp_die) != DW_TAG_subprogram ||
	    die_compare_name(sp_die, pp->function) != 0)
		return 0;

	if (pp->line) { /* Function relative line */
		pf->fname = dwarf_decl_file(sp_die);
		dwarf_decl_line(sp_die, &pf->lno);
		pf->lno += pp->line;
		find_probe_point_by_line(pf);
	} else if (!dwarf_func_inline(sp_die)) {
		/* Real function */
		pf->addr = die_get_entrypc(sp_die);
		pf->addr += pp->offset;
		/* TODO: Check the address in this function */
		show_probe_point(sp_die, pf);
	} else
		/* Inlined function: search instances */
		dwarf_func_inline_instances(sp_die, probe_point_inline_cb, pf);

	return 1; /* Exit; no same symbol in this CU. */
}

static void find_probe_point_by_func(struct probe_finder *pf)
{
	dwarf_getfuncs(&pf->cu_die, probe_point_search_cb, pf, 0);
}

/* Find a probe point */
int find_probe_point(int fd, struct probe_point *pp)
{
	struct probe_finder pf = {.pp = pp};
	int ret;
	Dwarf_Off off, noff;
	size_t cuhl;
	Dwarf_Die *diep;
	Dwarf *dbg;
	int fno = 0;

	dbg = dwarf_begin(fd, DWARF_C_READ);
	if (!dbg)
		return -ENOENT;

	pp->found = 0;
	off = 0;
	/* Loop on CUs (Compilation Unit) */
	while (!dwarf_nextcu(dbg, off, &noff, &cuhl, NULL, NULL, NULL)) {
		/* Get the DIE(Debugging Information Entry) of this CU */
		diep = dwarf_offdie(dbg, off + cuhl, &pf.cu_die);
		if (!diep)
			continue;

		/* Check if target file is included. */
		if (pp->file)
			fno = cu_find_fileno(&pf.cu_die, pp->file);
		else
			fno = 0;

		if (!pp->file || fno) {
			/* Save CU base address (for frame_base) */
			ret = dwarf_lowpc(&pf.cu_die, &pf.cu_base);
			if (ret != 0)
				pf.cu_base = 0;
			if (pp->function)
				find_probe_point_by_func(&pf);
			else {
				pf.lno = pp->line;
				find_probe_point_by_line(&pf);
			}
		}
		off = noff;
	}
	dwarf_end(dbg);

	return pp->found;
}


static void line_range_add_line(struct line_range *lr, unsigned int line)
{
	struct line_node *ln;
	struct list_head *p;

	/* Reverse search, because new line will be the last one */
	list_for_each_entry_reverse(ln, &lr->line_list, list) {
		if (ln->line < line) {
			p = &ln->list;
			goto found;
		} else if (ln->line == line)	/* Already exist */
			return ;
	}
	/* List is empty, or the smallest entry */
	p = &lr->line_list;
found:
	pr_debug("Debug: add a line %u\n", line);
	ln = zalloc(sizeof(struct line_node));
	DIE_IF(ln == NULL);
	ln->line = line;
	INIT_LIST_HEAD(&ln->list);
	list_add(&ln->list, p);
}

/* Find line range from its line number */
static void find_line_range_by_line(Dwarf_Die *sp_die, struct line_finder *lf)
{
	Dwarf_Lines *lines;
	Dwarf_Line *line;
	size_t nlines, i;
	Dwarf_Addr addr;
	int lineno;
	int ret;
	const char *src;
	Dwarf_Die die_mem;

	INIT_LIST_HEAD(&lf->lr->line_list);
	ret = dwarf_getsrclines(&lf->cu_die, &lines, &nlines);
	DIE_IF(ret != 0);

	for (i = 0; i < nlines; i++) {
		line = dwarf_onesrcline(lines, i);
		ret = dwarf_lineno(line, &lineno);
		DIE_IF(ret != 0);
		if (lf->lno_s > lineno || lf->lno_e < lineno)
			continue;

		if (sp_die) {
			/* Address filtering 1: does sp_die include addr? */
			ret = dwarf_lineaddr(line, &addr);
			DIE_IF(ret != 0);
			if (!dwarf_haspc(sp_die, addr))
				continue;

			/* Address filtering 2: No child include addr? */
			if (die_get_inlinefunc(sp_die, addr, &die_mem))
				continue;
		}

		/* TODO: Get fileno from line, but how? */
		src = dwarf_linesrc(line, NULL, NULL);
		if (strtailcmp(src, lf->fname) != 0)
			continue;

		/* Copy real path */
		if (!lf->lr->path)
			lf->lr->path = strdup(src);
		line_range_add_line(lf->lr, (unsigned int)lineno);
	}
	/* Update status */
	if (!list_empty(&lf->lr->line_list))
		lf->found = 1;
	else {
		free(lf->lr->path);
		lf->lr->path = NULL;
	}
}

static int line_range_inline_cb(Dwarf_Die *in_die, void *data)
{
	find_line_range_by_line(in_die, (struct line_finder *)data);
	return DWARF_CB_ABORT;	/* No need to find other instances */
}

/* Search function from function name */
static int line_range_search_cb(Dwarf_Die *sp_die, void *data)
{
	struct line_finder *lf = (struct line_finder *)data;
	struct line_range *lr = lf->lr;

	if (dwarf_tag(sp_die) == DW_TAG_subprogram &&
	    die_compare_name(sp_die, lr->function) == 0) {
		lf->fname = dwarf_decl_file(sp_die);
		dwarf_decl_line(sp_die, &lr->offset);
		pr_debug("fname: %s, lineno:%d\n", lf->fname, lr->offset);
		lf->lno_s = lr->offset + lr->start;
		if (!lr->end)
			lf->lno_e = INT_MAX;
		else
			lf->lno_e = lr->offset + lr->end;
		lr->start = lf->lno_s;
		lr->end = lf->lno_e;
		if (dwarf_func_inline(sp_die))
			dwarf_func_inline_instances(sp_die,
						    line_range_inline_cb, lf);
		else
			find_line_range_by_line(sp_die, lf);
		return 1;
	}
	return 0;
}

static void find_line_range_by_func(struct line_finder *lf)
{
	dwarf_getfuncs(&lf->cu_die, line_range_search_cb, lf, 0);
}

int find_line_range(int fd, struct line_range *lr)
{
	struct line_finder lf = {.lr = lr, .found = 0};
	int ret;
	Dwarf_Off off = 0, noff;
	size_t cuhl;
	Dwarf_Die *diep;
	Dwarf *dbg;
	int fno;

	dbg = dwarf_begin(fd, DWARF_C_READ);
	if (!dbg)
		return -ENOENT;

	/* Loop on CUs (Compilation Unit) */
	while (!lf.found) {
		ret = dwarf_nextcu(dbg, off, &noff, &cuhl, NULL, NULL, NULL);
		if (ret != 0)
			break;

		/* Get the DIE(Debugging Information Entry) of this CU */
		diep = dwarf_offdie(dbg, off + cuhl, &lf.cu_die);
		if (!diep)
			continue;

		/* Check if target file is included. */
		if (lr->file)
			fno = cu_find_fileno(&lf.cu_die, lr->file);
		else
			fno = 0;

		if (!lr->file || fno) {
			if (lr->function)
				find_line_range_by_func(&lf);
			else {
				lf.fname = lr->file;
				lf.lno_s = lr->start;
				if (!lr->end)
					lf.lno_e = INT_MAX;
				else
					lf.lno_e = lr->end;
				find_line_range_by_line(NULL, &lf);
			}
		}
		off = noff;
	}
	pr_debug("path: %lx\n", (unsigned long)lr->path);
	dwarf_end(dbg);
	return lf.found;
}

