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
#include <dwarf-regs.h>

#include "event.h"
#include "debug.h"
#include "util.h"
#include "symbol.h"
#include "probe-finder.h"

/* Kprobe tracer basic type is up to u64 */
#define MAX_BASIC_TYPE_BITS	64

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

/* Line number list operations */

/* Add a line to line number list */
static int line_list__add_line(struct list_head *head, int line)
{
	struct line_node *ln;
	struct list_head *p;

	/* Reverse search, because new line will be the last one */
	list_for_each_entry_reverse(ln, head, list) {
		if (ln->line < line) {
			p = &ln->list;
			goto found;
		} else if (ln->line == line)	/* Already exist */
			return 1;
	}
	/* List is empty, or the smallest entry */
	p = head;
found:
	pr_debug("line list: add a line %u\n", line);
	ln = zalloc(sizeof(struct line_node));
	if (ln == NULL)
		return -ENOMEM;
	ln->line = line;
	INIT_LIST_HEAD(&ln->list);
	list_add(&ln->list, p);
	return 0;
}

/* Check if the line in line number list */
static int line_list__has_line(struct list_head *head, int line)
{
	struct line_node *ln;

	/* Reverse search, because new line will be the last one */
	list_for_each_entry(ln, head, list)
		if (ln->line == line)
			return 1;

	return 0;
}

/* Init line number list */
static void line_list__init(struct list_head *head)
{
	INIT_LIST_HEAD(head);
}

/* Free line number list */
static void line_list__free(struct list_head *head)
{
	struct line_node *ln;
	while (!list_empty(head)) {
		ln = list_first_entry(head, struct line_node, list);
		list_del(&ln->list);
		free(ln);
	}
}

/* Dwarf FL wrappers */
static char *debuginfo_path;	/* Currently dummy */

static const Dwfl_Callbacks offline_callbacks = {
	.find_debuginfo = dwfl_standard_find_debuginfo,
	.debuginfo_path = &debuginfo_path,

	.section_address = dwfl_offline_section_address,

	/* We use this table for core files too.  */
	.find_elf = dwfl_build_id_find_elf,
};

/* Get a Dwarf from offline image */
static Dwarf *dwfl_init_offline_dwarf(int fd, Dwfl **dwflp, Dwarf_Addr *bias)
{
	Dwfl_Module *mod;
	Dwarf *dbg = NULL;

	if (!dwflp)
		return NULL;

	*dwflp = dwfl_begin(&offline_callbacks);
	if (!*dwflp)
		return NULL;

	mod = dwfl_report_offline(*dwflp, "", "", fd);
	if (!mod)
		goto error;

	dbg = dwfl_module_getdwarf(mod, bias);
	if (!dbg) {
error:
		dwfl_end(*dwflp);
		*dwflp = NULL;
	}
	return dbg;
}

#if _ELFUTILS_PREREQ(0, 148)
/* This method is buggy if elfutils is older than 0.148 */
static int __linux_kernel_find_elf(Dwfl_Module *mod,
				   void **userdata,
				   const char *module_name,
				   Dwarf_Addr base,
				   char **file_name, Elf **elfp)
{
	int fd;
	const char *path = kernel_get_module_path(module_name);

	pr_debug2("Use file %s for %s\n", path, module_name);
	if (path) {
		fd = open(path, O_RDONLY);
		if (fd >= 0) {
			*file_name = strdup(path);
			return fd;
		}
	}
	/* If failed, try to call standard method */
	return dwfl_linux_kernel_find_elf(mod, userdata, module_name, base,
					  file_name, elfp);
}

static const Dwfl_Callbacks kernel_callbacks = {
	.find_debuginfo = dwfl_standard_find_debuginfo,
	.debuginfo_path = &debuginfo_path,

	.find_elf = __linux_kernel_find_elf,
	.section_address = dwfl_linux_kernel_module_section_address,
};

/* Get a Dwarf from live kernel image */
static Dwarf *dwfl_init_live_kernel_dwarf(Dwarf_Addr addr, Dwfl **dwflp,
					  Dwarf_Addr *bias)
{
	Dwarf *dbg;

	if (!dwflp)
		return NULL;

	*dwflp = dwfl_begin(&kernel_callbacks);
	if (!*dwflp)
		return NULL;

	/* Load the kernel dwarves: Don't care the result here */
	dwfl_linux_kernel_report_kernel(*dwflp);
	dwfl_linux_kernel_report_modules(*dwflp);

	dbg = dwfl_addrdwarf(*dwflp, addr, bias);
	/* Here, check whether we could get a real dwarf */
	if (!dbg) {
		pr_debug("Failed to find kernel dwarf at %lx\n",
			 (unsigned long)addr);
		dwfl_end(*dwflp);
		*dwflp = NULL;
	}
	return dbg;
}
#else
/* With older elfutils, this just support kernel module... */
static Dwarf *dwfl_init_live_kernel_dwarf(Dwarf_Addr addr __used, Dwfl **dwflp,
					  Dwarf_Addr *bias)
{
	int fd;
	const char *path = kernel_get_module_path("kernel");

	if (!path) {
		pr_err("Failed to find vmlinux path\n");
		return NULL;
	}

	pr_debug2("Use file %s for debuginfo\n", path);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return NULL;

	return dwfl_init_offline_dwarf(fd, dwflp, bias);
}
#endif

/* Dwarf wrappers */

/* Find the realpath of the target file. */
static const char *cu_find_realpath(Dwarf_Die *cu_die, const char *fname)
{
	Dwarf_Files *files;
	size_t nfiles, i;
	const char *src = NULL;
	int ret;

	if (!fname)
		return NULL;

	ret = dwarf_getsrcfiles(cu_die, &files, &nfiles);
	if (ret != 0)
		return NULL;

	for (i = 0; i < nfiles; i++) {
		src = dwarf_filesrc(files, i, NULL, NULL);
		if (strtailcmp(src, fname) == 0)
			break;
	}
	if (i == nfiles)
		return NULL;
	return src;
}

/* Get DW_AT_comp_dir (should be NULL with older gcc) */
static const char *cu_get_comp_dir(Dwarf_Die *cu_die)
{
	Dwarf_Attribute attr;
	if (dwarf_attr(cu_die, DW_AT_comp_dir, &attr) == NULL)
		return NULL;
	return dwarf_formstring(&attr);
}

/* Compare diename and tname */
static bool die_compare_name(Dwarf_Die *dw_die, const char *tname)
{
	const char *name;
	name = dwarf_diename(dw_die);
	return name ? (strcmp(tname, name) == 0) : false;
}

/* Get type die */
static Dwarf_Die *die_get_type(Dwarf_Die *vr_die, Dwarf_Die *die_mem)
{
	Dwarf_Attribute attr;

	if (dwarf_attr_integrate(vr_die, DW_AT_type, &attr) &&
	    dwarf_formref_die(&attr, die_mem))
		return die_mem;
	else
		return NULL;
}

/* Get a type die, but skip qualifiers */
static Dwarf_Die *__die_get_real_type(Dwarf_Die *vr_die, Dwarf_Die *die_mem)
{
	int tag;

	do {
		vr_die = die_get_type(vr_die, die_mem);
		if (!vr_die)
			break;
		tag = dwarf_tag(vr_die);
	} while (tag == DW_TAG_const_type ||
		 tag == DW_TAG_restrict_type ||
		 tag == DW_TAG_volatile_type ||
		 tag == DW_TAG_shared_type);

	return vr_die;
}

/* Get a type die, but skip qualifiers and typedef */
static Dwarf_Die *die_get_real_type(Dwarf_Die *vr_die, Dwarf_Die *die_mem)
{
	do {
		vr_die = __die_get_real_type(vr_die, die_mem);
	} while (vr_die && dwarf_tag(vr_die) == DW_TAG_typedef);

	return vr_die;
}

static bool die_is_signed_type(Dwarf_Die *tp_die)
{
	Dwarf_Attribute attr;
	Dwarf_Word ret;

	if (dwarf_attr(tp_die, DW_AT_encoding, &attr) == NULL ||
	    dwarf_formudata(&attr, &ret) != 0)
		return false;

	return (ret == DW_ATE_signed_char || ret == DW_ATE_signed ||
		ret == DW_ATE_signed_fixed);
}

static int die_get_byte_size(Dwarf_Die *tp_die)
{
	Dwarf_Attribute attr;
	Dwarf_Word ret;

	if (dwarf_attr(tp_die, DW_AT_byte_size, &attr) == NULL ||
	    dwarf_formudata(&attr, &ret) != 0)
		return 0;

	return (int)ret;
}

/* Get data_member_location offset */
static int die_get_data_member_location(Dwarf_Die *mb_die, Dwarf_Word *offs)
{
	Dwarf_Attribute attr;
	Dwarf_Op *expr;
	size_t nexpr;
	int ret;

	if (dwarf_attr(mb_die, DW_AT_data_member_location, &attr) == NULL)
		return -ENOENT;

	if (dwarf_formudata(&attr, offs) != 0) {
		/* DW_AT_data_member_location should be DW_OP_plus_uconst */
		ret = dwarf_getlocation(&attr, &expr, &nexpr);
		if (ret < 0 || nexpr == 0)
			return -ENOENT;

		if (expr[0].atom != DW_OP_plus_uconst || nexpr != 1) {
			pr_debug("Unable to get offset:Unexpected OP %x (%zd)\n",
				 expr[0].atom, nexpr);
			return -ENOTSUP;
		}
		*offs = (Dwarf_Word)expr[0].number;
	}
	return 0;
}

/* Return values for die_find callbacks */
enum {
	DIE_FIND_CB_FOUND = 0,		/* End of Search */
	DIE_FIND_CB_CHILD = 1,		/* Search only children */
	DIE_FIND_CB_SIBLING = 2,	/* Search only siblings */
	DIE_FIND_CB_CONTINUE = 3,	/* Search children and siblings */
};

/* Search a child die */
static Dwarf_Die *die_find_child(Dwarf_Die *rt_die,
				 int (*callback)(Dwarf_Die *, void *),
				 void *data, Dwarf_Die *die_mem)
{
	Dwarf_Die child_die;
	int ret;

	ret = dwarf_child(rt_die, die_mem);
	if (ret != 0)
		return NULL;

	do {
		ret = callback(die_mem, data);
		if (ret == DIE_FIND_CB_FOUND)
			return die_mem;

		if ((ret & DIE_FIND_CB_CHILD) &&
		    die_find_child(die_mem, callback, data, &child_die)) {
			memcpy(die_mem, &child_die, sizeof(Dwarf_Die));
			return die_mem;
		}
	} while ((ret & DIE_FIND_CB_SIBLING) &&
		 dwarf_siblingof(die_mem, die_mem) == 0);

	return NULL;
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
static Dwarf_Die *die_find_real_subprogram(Dwarf_Die *cu_die, Dwarf_Addr addr,
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

/* die_find callback for inline function search */
static int __die_find_inline_cb(Dwarf_Die *die_mem, void *data)
{
	Dwarf_Addr *addr = data;

	if (dwarf_tag(die_mem) == DW_TAG_inlined_subroutine &&
	    dwarf_haspc(die_mem, *addr))
		return DIE_FIND_CB_FOUND;

	return DIE_FIND_CB_CONTINUE;
}

/* Similar to dwarf_getfuncs, but returns inlined_subroutine if exists. */
static Dwarf_Die *die_find_inlinefunc(Dwarf_Die *sp_die, Dwarf_Addr addr,
				      Dwarf_Die *die_mem)
{
	return die_find_child(sp_die, __die_find_inline_cb, &addr, die_mem);
}

struct __find_variable_param {
	const char *name;
	Dwarf_Addr addr;
};

static int __die_find_variable_cb(Dwarf_Die *die_mem, void *data)
{
	struct __find_variable_param *fvp = data;
	int tag;

	tag = dwarf_tag(die_mem);
	if ((tag == DW_TAG_formal_parameter ||
	     tag == DW_TAG_variable) &&
	    die_compare_name(die_mem, fvp->name))
		return DIE_FIND_CB_FOUND;

	if (dwarf_haspc(die_mem, fvp->addr))
		return DIE_FIND_CB_CONTINUE;
	else
		return DIE_FIND_CB_SIBLING;
}

/* Find a variable called 'name' at given address */
static Dwarf_Die *die_find_variable_at(Dwarf_Die *sp_die, const char *name,
				       Dwarf_Addr addr, Dwarf_Die *die_mem)
{
	struct __find_variable_param fvp = { .name = name, .addr = addr};

	return die_find_child(sp_die, __die_find_variable_cb, (void *)&fvp,
			      die_mem);
}

static int __die_find_member_cb(Dwarf_Die *die_mem, void *data)
{
	const char *name = data;

	if ((dwarf_tag(die_mem) == DW_TAG_member) &&
	    die_compare_name(die_mem, name))
		return DIE_FIND_CB_FOUND;

	return DIE_FIND_CB_SIBLING;
}

/* Find a member called 'name' */
static Dwarf_Die *die_find_member(Dwarf_Die *st_die, const char *name,
				  Dwarf_Die *die_mem)
{
	return die_find_child(st_die, __die_find_member_cb, (void *)name,
			      die_mem);
}

/* Get the name of given variable DIE */
static int die_get_typename(Dwarf_Die *vr_die, char *buf, int len)
{
	Dwarf_Die type;
	int tag, ret, ret2;
	const char *tmp = "";

	if (__die_get_real_type(vr_die, &type) == NULL)
		return -ENOENT;

	tag = dwarf_tag(&type);
	if (tag == DW_TAG_array_type || tag == DW_TAG_pointer_type)
		tmp = "*";
	else if (tag == DW_TAG_subroutine_type) {
		/* Function pointer */
		ret = snprintf(buf, len, "(function_type)");
		return (ret >= len) ? -E2BIG : ret;
	} else {
		if (!dwarf_diename(&type))
			return -ENOENT;
		if (tag == DW_TAG_union_type)
			tmp = "union ";
		else if (tag == DW_TAG_structure_type)
			tmp = "struct ";
		/* Write a base name */
		ret = snprintf(buf, len, "%s%s", tmp, dwarf_diename(&type));
		return (ret >= len) ? -E2BIG : ret;
	}
	ret = die_get_typename(&type, buf, len);
	if (ret > 0) {
		ret2 = snprintf(buf + ret, len - ret, "%s", tmp);
		ret = (ret2 >= len - ret) ? -E2BIG : ret2 + ret;
	}
	return ret;
}

/* Get the name and type of given variable DIE, stored as "type\tname" */
static int die_get_varname(Dwarf_Die *vr_die, char *buf, int len)
{
	int ret, ret2;

	ret = die_get_typename(vr_die, buf, len);
	if (ret < 0) {
		pr_debug("Failed to get type, make it unknown.\n");
		ret = snprintf(buf, len, "(unknown_type)");
	}
	if (ret > 0) {
		ret2 = snprintf(buf + ret, len - ret, "\t%s",
				dwarf_diename(vr_die));
		ret = (ret2 >= len - ret) ? -E2BIG : ret2 + ret;
	}
	return ret;
}

/*
 * Probe finder related functions
 */

static struct probe_trace_arg_ref *alloc_trace_arg_ref(long offs)
{
	struct probe_trace_arg_ref *ref;
	ref = zalloc(sizeof(struct probe_trace_arg_ref));
	if (ref != NULL)
		ref->offset = offs;
	return ref;
}

/*
 * Convert a location into trace_arg.
 * If tvar == NULL, this just checks variable can be converted.
 */
static int convert_variable_location(Dwarf_Die *vr_die, Dwarf_Addr addr,
				     Dwarf_Op *fb_ops,
				     struct probe_trace_arg *tvar)
{
	Dwarf_Attribute attr;
	Dwarf_Op *op;
	size_t nops;
	unsigned int regn;
	Dwarf_Word offs = 0;
	bool ref = false;
	const char *regs;
	int ret;

	if (dwarf_attr(vr_die, DW_AT_external, &attr) != NULL)
		goto static_var;

	/* TODO: handle more than 1 exprs */
	if (dwarf_attr(vr_die, DW_AT_location, &attr) == NULL ||
	    dwarf_getlocation_addr(&attr, addr, &op, &nops, 1) <= 0 ||
	    nops == 0) {
		/* TODO: Support const_value */
		return -ENOENT;
	}

	if (op->atom == DW_OP_addr) {
static_var:
		if (!tvar)
			return 0;
		/* Static variables on memory (not stack), make @varname */
		ret = strlen(dwarf_diename(vr_die));
		tvar->value = zalloc(ret + 2);
		if (tvar->value == NULL)
			return -ENOMEM;
		snprintf(tvar->value, ret + 2, "@%s", dwarf_diename(vr_die));
		tvar->ref = alloc_trace_arg_ref((long)offs);
		if (tvar->ref == NULL)
			return -ENOMEM;
		return 0;
	}

	/* If this is based on frame buffer, set the offset */
	if (op->atom == DW_OP_fbreg) {
		if (fb_ops == NULL)
			return -ENOTSUP;
		ref = true;
		offs = op->number;
		op = &fb_ops[0];
	}

	if (op->atom >= DW_OP_breg0 && op->atom <= DW_OP_breg31) {
		regn = op->atom - DW_OP_breg0;
		offs += op->number;
		ref = true;
	} else if (op->atom >= DW_OP_reg0 && op->atom <= DW_OP_reg31) {
		regn = op->atom - DW_OP_reg0;
	} else if (op->atom == DW_OP_bregx) {
		regn = op->number;
		offs += op->number2;
		ref = true;
	} else if (op->atom == DW_OP_regx) {
		regn = op->number;
	} else {
		pr_debug("DW_OP %x is not supported.\n", op->atom);
		return -ENOTSUP;
	}

	if (!tvar)
		return 0;

	regs = get_arch_regstr(regn);
	if (!regs) {
		/* This should be a bug in DWARF or this tool */
		pr_warning("Mapping for the register number %u "
			   "missing on this architecture.\n", regn);
		return -ERANGE;
	}

	tvar->value = strdup(regs);
	if (tvar->value == NULL)
		return -ENOMEM;

	if (ref) {
		tvar->ref = alloc_trace_arg_ref((long)offs);
		if (tvar->ref == NULL)
			return -ENOMEM;
	}
	return 0;
}

static int convert_variable_type(Dwarf_Die *vr_die,
				 struct probe_trace_arg *tvar,
				 const char *cast)
{
	struct probe_trace_arg_ref **ref_ptr = &tvar->ref;
	Dwarf_Die type;
	char buf[16];
	int ret;

	/* TODO: check all types */
	if (cast && strcmp(cast, "string") != 0) {
		/* Non string type is OK */
		tvar->type = strdup(cast);
		return (tvar->type == NULL) ? -ENOMEM : 0;
	}

	if (die_get_real_type(vr_die, &type) == NULL) {
		pr_warning("Failed to get a type information of %s.\n",
			   dwarf_diename(vr_die));
		return -ENOENT;
	}

	pr_debug("%s type is %s.\n",
		 dwarf_diename(vr_die), dwarf_diename(&type));

	if (cast && strcmp(cast, "string") == 0) {	/* String type */
		ret = dwarf_tag(&type);
		if (ret != DW_TAG_pointer_type &&
		    ret != DW_TAG_array_type) {
			pr_warning("Failed to cast into string: "
				   "%s(%s) is not a pointer nor array.\n",
				   dwarf_diename(vr_die), dwarf_diename(&type));
			return -EINVAL;
		}
		if (ret == DW_TAG_pointer_type) {
			if (die_get_real_type(&type, &type) == NULL) {
				pr_warning("Failed to get a type"
					   " information.\n");
				return -ENOENT;
			}
			while (*ref_ptr)
				ref_ptr = &(*ref_ptr)->next;
			/* Add new reference with offset +0 */
			*ref_ptr = zalloc(sizeof(struct probe_trace_arg_ref));
			if (*ref_ptr == NULL) {
				pr_warning("Out of memory error\n");
				return -ENOMEM;
			}
		}
		if (!die_compare_name(&type, "char") &&
		    !die_compare_name(&type, "unsigned char")) {
			pr_warning("Failed to cast into string: "
				   "%s is not (unsigned) char *.\n",
				   dwarf_diename(vr_die));
			return -EINVAL;
		}
		tvar->type = strdup(cast);
		return (tvar->type == NULL) ? -ENOMEM : 0;
	}

	ret = die_get_byte_size(&type) * 8;
	if (ret) {
		/* Check the bitwidth */
		if (ret > MAX_BASIC_TYPE_BITS) {
			pr_info("%s exceeds max-bitwidth."
				" Cut down to %d bits.\n",
				dwarf_diename(&type), MAX_BASIC_TYPE_BITS);
			ret = MAX_BASIC_TYPE_BITS;
		}

		ret = snprintf(buf, 16, "%c%d",
			       die_is_signed_type(&type) ? 's' : 'u', ret);
		if (ret < 0 || ret >= 16) {
			if (ret >= 16)
				ret = -E2BIG;
			pr_warning("Failed to convert variable type: %s\n",
				   strerror(-ret));
			return ret;
		}
		tvar->type = strdup(buf);
		if (tvar->type == NULL)
			return -ENOMEM;
	}
	return 0;
}

static int convert_variable_fields(Dwarf_Die *vr_die, const char *varname,
				    struct perf_probe_arg_field *field,
				    struct probe_trace_arg_ref **ref_ptr,
				    Dwarf_Die *die_mem)
{
	struct probe_trace_arg_ref *ref = *ref_ptr;
	Dwarf_Die type;
	Dwarf_Word offs;
	int ret, tag;

	pr_debug("converting %s in %s\n", field->name, varname);
	if (die_get_real_type(vr_die, &type) == NULL) {
		pr_warning("Failed to get the type of %s.\n", varname);
		return -ENOENT;
	}
	pr_debug2("Var real type: (%x)\n", (unsigned)dwarf_dieoffset(&type));
	tag = dwarf_tag(&type);

	if (field->name[0] == '[' &&
	    (tag == DW_TAG_array_type || tag == DW_TAG_pointer_type)) {
		if (field->next)
			/* Save original type for next field */
			memcpy(die_mem, &type, sizeof(*die_mem));
		/* Get the type of this array */
		if (die_get_real_type(&type, &type) == NULL) {
			pr_warning("Failed to get the type of %s.\n", varname);
			return -ENOENT;
		}
		pr_debug2("Array real type: (%x)\n",
			 (unsigned)dwarf_dieoffset(&type));
		if (tag == DW_TAG_pointer_type) {
			ref = zalloc(sizeof(struct probe_trace_arg_ref));
			if (ref == NULL)
				return -ENOMEM;
			if (*ref_ptr)
				(*ref_ptr)->next = ref;
			else
				*ref_ptr = ref;
		}
		ref->offset += die_get_byte_size(&type) * field->index;
		if (!field->next)
			/* Save vr_die for converting types */
			memcpy(die_mem, vr_die, sizeof(*die_mem));
		goto next;
	} else if (tag == DW_TAG_pointer_type) {
		/* Check the pointer and dereference */
		if (!field->ref) {
			pr_err("Semantic error: %s must be referred by '->'\n",
			       field->name);
			return -EINVAL;
		}
		/* Get the type pointed by this pointer */
		if (die_get_real_type(&type, &type) == NULL) {
			pr_warning("Failed to get the type of %s.\n", varname);
			return -ENOENT;
		}
		/* Verify it is a data structure  */
		if (dwarf_tag(&type) != DW_TAG_structure_type) {
			pr_warning("%s is not a data structure.\n", varname);
			return -EINVAL;
		}

		ref = zalloc(sizeof(struct probe_trace_arg_ref));
		if (ref == NULL)
			return -ENOMEM;
		if (*ref_ptr)
			(*ref_ptr)->next = ref;
		else
			*ref_ptr = ref;
	} else {
		/* Verify it is a data structure  */
		if (tag != DW_TAG_structure_type) {
			pr_warning("%s is not a data structure.\n", varname);
			return -EINVAL;
		}
		if (field->name[0] == '[') {
			pr_err("Semantic error: %s is not a pointor"
			       " nor array.\n", varname);
			return -EINVAL;
		}
		if (field->ref) {
			pr_err("Semantic error: %s must be referred by '.'\n",
			       field->name);
			return -EINVAL;
		}
		if (!ref) {
			pr_warning("Structure on a register is not "
				   "supported yet.\n");
			return -ENOTSUP;
		}
	}

	if (die_find_member(&type, field->name, die_mem) == NULL) {
		pr_warning("%s(tyep:%s) has no member %s.\n", varname,
			   dwarf_diename(&type), field->name);
		return -EINVAL;
	}

	/* Get the offset of the field */
	ret = die_get_data_member_location(die_mem, &offs);
	if (ret < 0) {
		pr_warning("Failed to get the offset of %s.\n", field->name);
		return ret;
	}
	ref->offset += (long)offs;

next:
	/* Converting next field */
	if (field->next)
		return convert_variable_fields(die_mem, field->name,
					field->next, &ref, die_mem);
	else
		return 0;
}

/* Show a variables in kprobe event format */
static int convert_variable(Dwarf_Die *vr_die, struct probe_finder *pf)
{
	Dwarf_Die die_mem;
	int ret;

	pr_debug("Converting variable %s into trace event.\n",
		 dwarf_diename(vr_die));

	ret = convert_variable_location(vr_die, pf->addr, pf->fb_ops,
					pf->tvar);
	if (ret == -ENOENT)
		pr_err("Failed to find the location of %s at this address.\n"
		       " Perhaps, it has been optimized out.\n", pf->pvar->var);
	else if (ret == -ENOTSUP)
		pr_err("Sorry, we don't support this variable location yet.\n");
	else if (pf->pvar->field) {
		ret = convert_variable_fields(vr_die, pf->pvar->var,
					      pf->pvar->field, &pf->tvar->ref,
					      &die_mem);
		vr_die = &die_mem;
	}
	if (ret == 0)
		ret = convert_variable_type(vr_die, pf->tvar, pf->pvar->type);
	/* *expr will be cached in libdw. Don't free it. */
	return ret;
}

/* Find a variable in a subprogram die */
static int find_variable(Dwarf_Die *sp_die, struct probe_finder *pf)
{
	Dwarf_Die vr_die, *scopes;
	char buf[32], *ptr;
	int ret, nscopes;

	if (!is_c_varname(pf->pvar->var)) {
		/* Copy raw parameters */
		pf->tvar->value = strdup(pf->pvar->var);
		if (pf->tvar->value == NULL)
			return -ENOMEM;
		if (pf->pvar->type) {
			pf->tvar->type = strdup(pf->pvar->type);
			if (pf->tvar->type == NULL)
				return -ENOMEM;
		}
		if (pf->pvar->name) {
			pf->tvar->name = strdup(pf->pvar->name);
			if (pf->tvar->name == NULL)
				return -ENOMEM;
		} else
			pf->tvar->name = NULL;
		return 0;
	}

	if (pf->pvar->name)
		pf->tvar->name = strdup(pf->pvar->name);
	else {
		ret = synthesize_perf_probe_arg(pf->pvar, buf, 32);
		if (ret < 0)
			return ret;
		ptr = strchr(buf, ':');	/* Change type separator to _ */
		if (ptr)
			*ptr = '_';
		pf->tvar->name = strdup(buf);
	}
	if (pf->tvar->name == NULL)
		return -ENOMEM;

	pr_debug("Searching '%s' variable in context.\n",
		 pf->pvar->var);
	/* Search child die for local variables and parameters. */
	if (die_find_variable_at(sp_die, pf->pvar->var, pf->addr, &vr_die))
		ret = convert_variable(&vr_die, pf);
	else {
		/* Search upper class */
		nscopes = dwarf_getscopes_die(sp_die, &scopes);
		while (nscopes-- > 1) {
			pr_debug("Searching variables in %s\n",
				 dwarf_diename(&scopes[nscopes]));
			/* We should check this scope, so give dummy address */
			if (die_find_variable_at(&scopes[nscopes],
						 pf->pvar->var, 0,
						 &vr_die)) {
				ret = convert_variable(&vr_die, pf);
				goto found;
			}
		}
		if (scopes)
			free(scopes);
		ret = -ENOENT;
	}
found:
	if (ret < 0)
		pr_warning("Failed to find '%s' in this function.\n",
			   pf->pvar->var);
	return ret;
}

/* Convert subprogram DIE to trace point */
static int convert_to_trace_point(Dwarf_Die *sp_die, Dwarf_Addr paddr,
				  bool retprobe, struct probe_trace_point *tp)
{
	Dwarf_Addr eaddr;
	const char *name;

	/* Copy the name of probe point */
	name = dwarf_diename(sp_die);
	if (name) {
		if (dwarf_entrypc(sp_die, &eaddr) != 0) {
			pr_warning("Failed to get entry address of %s\n",
				   dwarf_diename(sp_die));
			return -ENOENT;
		}
		tp->symbol = strdup(name);
		if (tp->symbol == NULL)
			return -ENOMEM;
		tp->offset = (unsigned long)(paddr - eaddr);
	} else
		/* This function has no name. */
		tp->offset = (unsigned long)paddr;

	/* Return probe must be on the head of a subprogram */
	if (retprobe) {
		if (eaddr != paddr) {
			pr_warning("Return probe must be on the head of"
				   " a real function.\n");
			return -EINVAL;
		}
		tp->retprobe = true;
	}

	return 0;
}

/* Call probe_finder callback with real subprogram DIE */
static int call_probe_finder(Dwarf_Die *sp_die, struct probe_finder *pf)
{
	Dwarf_Die die_mem;
	Dwarf_Attribute fb_attr;
	size_t nops;
	int ret;

	/* If no real subprogram, find a real one */
	if (!sp_die || dwarf_tag(sp_die) != DW_TAG_subprogram) {
		sp_die = die_find_real_subprogram(&pf->cu_die,
						  pf->addr, &die_mem);
		if (!sp_die) {
			pr_warning("Failed to find probe point in any "
				   "functions.\n");
			return -ENOENT;
		}
	}

	/* Get the frame base attribute/ops */
	dwarf_attr(sp_die, DW_AT_frame_base, &fb_attr);
	ret = dwarf_getlocation_addr(&fb_attr, pf->addr, &pf->fb_ops, &nops, 1);
	if (ret <= 0 || nops == 0) {
		pf->fb_ops = NULL;
#if _ELFUTILS_PREREQ(0, 142)
	} else if (nops == 1 && pf->fb_ops[0].atom == DW_OP_call_frame_cfa &&
		   pf->cfi != NULL) {
		Dwarf_Frame *frame;
		if (dwarf_cfi_addrframe(pf->cfi, pf->addr, &frame) != 0 ||
		    dwarf_frame_cfa(frame, &pf->fb_ops, &nops) != 0) {
			pr_warning("Failed to get call frame on 0x%jx\n",
				   (uintmax_t)pf->addr);
			return -ENOENT;
		}
#endif
	}

	/* Call finder's callback handler */
	ret = pf->callback(sp_die, pf);

	/* *pf->fb_ops will be cached in libdw. Don't free it. */
	pf->fb_ops = NULL;

	return ret;
}

/* Find probe point from its line number */
static int find_probe_point_by_line(struct probe_finder *pf)
{
	Dwarf_Lines *lines;
	Dwarf_Line *line;
	size_t nlines, i;
	Dwarf_Addr addr;
	int lineno;
	int ret = 0;

	if (dwarf_getsrclines(&pf->cu_die, &lines, &nlines) != 0) {
		pr_warning("No source lines found.\n");
		return -ENOENT;
	}

	for (i = 0; i < nlines && ret == 0; i++) {
		line = dwarf_onesrcline(lines, i);
		if (dwarf_lineno(line, &lineno) != 0 ||
		    lineno != pf->lno)
			continue;

		/* TODO: Get fileno from line, but how? */
		if (strtailcmp(dwarf_linesrc(line, NULL, NULL), pf->fname) != 0)
			continue;

		if (dwarf_lineaddr(line, &addr) != 0) {
			pr_warning("Failed to get the address of the line.\n");
			return -ENOENT;
		}
		pr_debug("Probe line found: line[%d]:%d addr:0x%jx\n",
			 (int)i, lineno, (uintmax_t)addr);
		pf->addr = addr;

		ret = call_probe_finder(NULL, pf);
		/* Continuing, because target line might be inlined. */
	}
	return ret;
}

/* Find lines which match lazy pattern */
static int find_lazy_match_lines(struct list_head *head,
				 const char *fname, const char *pat)
{
	char *fbuf, *p1, *p2;
	int fd, line, nlines = -1;
	struct stat st;

	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		pr_warning("Failed to open %s: %s\n", fname, strerror(-fd));
		return -errno;
	}

	if (fstat(fd, &st) < 0) {
		pr_warning("Failed to get the size of %s: %s\n",
			   fname, strerror(errno));
		nlines = -errno;
		goto out_close;
	}

	nlines = -ENOMEM;
	fbuf = malloc(st.st_size + 2);
	if (fbuf == NULL)
		goto out_close;
	if (read(fd, fbuf, st.st_size) < 0) {
		pr_warning("Failed to read %s: %s\n", fname, strerror(errno));
		nlines = -errno;
		goto out_free_fbuf;
	}
	fbuf[st.st_size] = '\n';	/* Dummy line */
	fbuf[st.st_size + 1] = '\0';
	p1 = fbuf;
	line = 1;
	nlines = 0;
	while ((p2 = strchr(p1, '\n')) != NULL) {
		*p2 = '\0';
		if (strlazymatch(p1, pat)) {
			line_list__add_line(head, line);
			nlines++;
		}
		line++;
		p1 = p2 + 1;
	}
out_free_fbuf:
	free(fbuf);
out_close:
	close(fd);
	return nlines;
}

/* Find probe points from lazy pattern  */
static int find_probe_point_lazy(Dwarf_Die *sp_die, struct probe_finder *pf)
{
	Dwarf_Lines *lines;
	Dwarf_Line *line;
	size_t nlines, i;
	Dwarf_Addr addr;
	Dwarf_Die die_mem;
	int lineno;
	int ret = 0;

	if (list_empty(&pf->lcache)) {
		/* Matching lazy line pattern */
		ret = find_lazy_match_lines(&pf->lcache, pf->fname,
					    pf->pev->point.lazy_line);
		if (ret == 0) {
			pr_debug("No matched lines found in %s.\n", pf->fname);
			return 0;
		} else if (ret < 0)
			return ret;
	}

	if (dwarf_getsrclines(&pf->cu_die, &lines, &nlines) != 0) {
		pr_warning("No source lines found.\n");
		return -ENOENT;
	}

	for (i = 0; i < nlines && ret >= 0; i++) {
		line = dwarf_onesrcline(lines, i);

		if (dwarf_lineno(line, &lineno) != 0 ||
		    !line_list__has_line(&pf->lcache, lineno))
			continue;

		/* TODO: Get fileno from line, but how? */
		if (strtailcmp(dwarf_linesrc(line, NULL, NULL), pf->fname) != 0)
			continue;

		if (dwarf_lineaddr(line, &addr) != 0) {
			pr_debug("Failed to get the address of line %d.\n",
				 lineno);
			continue;
		}
		if (sp_die) {
			/* Address filtering 1: does sp_die include addr? */
			if (!dwarf_haspc(sp_die, addr))
				continue;
			/* Address filtering 2: No child include addr? */
			if (die_find_inlinefunc(sp_die, addr, &die_mem))
				continue;
		}

		pr_debug("Probe line found: line[%d]:%d addr:0x%llx\n",
			 (int)i, lineno, (unsigned long long)addr);
		pf->addr = addr;

		ret = call_probe_finder(sp_die, pf);
		/* Continuing, because target line might be inlined. */
	}
	/* TODO: deallocate lines, but how? */
	return ret;
}

/* Callback parameter with return value */
struct dwarf_callback_param {
	void *data;
	int retval;
};

static int probe_point_inline_cb(Dwarf_Die *in_die, void *data)
{
	struct dwarf_callback_param *param = data;
	struct probe_finder *pf = param->data;
	struct perf_probe_point *pp = &pf->pev->point;
	Dwarf_Addr addr;

	if (pp->lazy_line)
		param->retval = find_probe_point_lazy(in_die, pf);
	else {
		/* Get probe address */
		if (dwarf_entrypc(in_die, &addr) != 0) {
			pr_warning("Failed to get entry address of %s.\n",
				   dwarf_diename(in_die));
			param->retval = -ENOENT;
			return DWARF_CB_ABORT;
		}
		pf->addr = addr;
		pf->addr += pp->offset;
		pr_debug("found inline addr: 0x%jx\n",
			 (uintmax_t)pf->addr);

		param->retval = call_probe_finder(in_die, pf);
		if (param->retval < 0)
			return DWARF_CB_ABORT;
	}

	return DWARF_CB_OK;
}

/* Search function from function name */
static int probe_point_search_cb(Dwarf_Die *sp_die, void *data)
{
	struct dwarf_callback_param *param = data;
	struct probe_finder *pf = param->data;
	struct perf_probe_point *pp = &pf->pev->point;

	/* Check tag and diename */
	if (dwarf_tag(sp_die) != DW_TAG_subprogram ||
	    !die_compare_name(sp_die, pp->function))
		return DWARF_CB_OK;

	pf->fname = dwarf_decl_file(sp_die);
	if (pp->line) { /* Function relative line */
		dwarf_decl_line(sp_die, &pf->lno);
		pf->lno += pp->line;
		param->retval = find_probe_point_by_line(pf);
	} else if (!dwarf_func_inline(sp_die)) {
		/* Real function */
		if (pp->lazy_line)
			param->retval = find_probe_point_lazy(sp_die, pf);
		else {
			if (dwarf_entrypc(sp_die, &pf->addr) != 0) {
				pr_warning("Failed to get entry address of "
					   "%s.\n", dwarf_diename(sp_die));
				param->retval = -ENOENT;
				return DWARF_CB_ABORT;
			}
			pf->addr += pp->offset;
			/* TODO: Check the address in this function */
			param->retval = call_probe_finder(sp_die, pf);
		}
	} else {
		struct dwarf_callback_param _param = {.data = (void *)pf,
						      .retval = 0};
		/* Inlined function: search instances */
		dwarf_func_inline_instances(sp_die, probe_point_inline_cb,
					    &_param);
		param->retval = _param.retval;
	}

	return DWARF_CB_ABORT; /* Exit; no same symbol in this CU. */
}

static int find_probe_point_by_func(struct probe_finder *pf)
{
	struct dwarf_callback_param _param = {.data = (void *)pf,
					      .retval = 0};
	dwarf_getfuncs(&pf->cu_die, probe_point_search_cb, &_param, 0);
	return _param.retval;
}

/* Find probe points from debuginfo */
static int find_probes(int fd, struct probe_finder *pf)
{
	struct perf_probe_point *pp = &pf->pev->point;
	Dwarf_Off off, noff;
	size_t cuhl;
	Dwarf_Die *diep;
	Dwarf *dbg = NULL;
	Dwfl *dwfl;
	Dwarf_Addr bias;	/* Currently ignored */
	int ret = 0;

	dbg = dwfl_init_offline_dwarf(fd, &dwfl, &bias);
	if (!dbg) {
		pr_warning("No debug information found in the vmlinux - "
			"please rebuild with CONFIG_DEBUG_INFO=y.\n");
		return -EBADF;
	}

#if _ELFUTILS_PREREQ(0, 142)
	/* Get the call frame information from this dwarf */
	pf->cfi = dwarf_getcfi(dbg);
#endif

	off = 0;
	line_list__init(&pf->lcache);
	/* Loop on CUs (Compilation Unit) */
	while (!dwarf_nextcu(dbg, off, &noff, &cuhl, NULL, NULL, NULL) &&
	       ret >= 0) {
		/* Get the DIE(Debugging Information Entry) of this CU */
		diep = dwarf_offdie(dbg, off + cuhl, &pf->cu_die);
		if (!diep)
			continue;

		/* Check if target file is included. */
		if (pp->file)
			pf->fname = cu_find_realpath(&pf->cu_die, pp->file);
		else
			pf->fname = NULL;

		if (!pp->file || pf->fname) {
			if (pp->function)
				ret = find_probe_point_by_func(pf);
			else if (pp->lazy_line)
				ret = find_probe_point_lazy(NULL, pf);
			else {
				pf->lno = pp->line;
				ret = find_probe_point_by_line(pf);
			}
		}
		off = noff;
	}
	line_list__free(&pf->lcache);
	if (dwfl)
		dwfl_end(dwfl);

	return ret;
}

/* Add a found probe point into trace event list */
static int add_probe_trace_event(Dwarf_Die *sp_die, struct probe_finder *pf)
{
	struct trace_event_finder *tf =
			container_of(pf, struct trace_event_finder, pf);
	struct probe_trace_event *tev;
	int ret, i;

	/* Check number of tevs */
	if (tf->ntevs == tf->max_tevs) {
		pr_warning("Too many( > %d) probe point found.\n",
			   tf->max_tevs);
		return -ERANGE;
	}
	tev = &tf->tevs[tf->ntevs++];

	ret = convert_to_trace_point(sp_die, pf->addr, pf->pev->point.retprobe,
				     &tev->point);
	if (ret < 0)
		return ret;

	pr_debug("Probe point found: %s+%lu\n", tev->point.symbol,
		 tev->point.offset);

	/* Find each argument */
	tev->nargs = pf->pev->nargs;
	tev->args = zalloc(sizeof(struct probe_trace_arg) * tev->nargs);
	if (tev->args == NULL)
		return -ENOMEM;
	for (i = 0; i < pf->pev->nargs; i++) {
		pf->pvar = &pf->pev->args[i];
		pf->tvar = &tev->args[i];
		ret = find_variable(sp_die, pf);
		if (ret != 0)
			return ret;
	}

	return 0;
}

/* Find probe_trace_events specified by perf_probe_event from debuginfo */
int find_probe_trace_events(int fd, struct perf_probe_event *pev,
			    struct probe_trace_event **tevs, int max_tevs)
{
	struct trace_event_finder tf = {
			.pf = {.pev = pev, .callback = add_probe_trace_event},
			.max_tevs = max_tevs};
	int ret;

	/* Allocate result tevs array */
	*tevs = zalloc(sizeof(struct probe_trace_event) * max_tevs);
	if (*tevs == NULL)
		return -ENOMEM;

	tf.tevs = *tevs;
	tf.ntevs = 0;

	ret = find_probes(fd, &tf.pf);
	if (ret < 0) {
		free(*tevs);
		*tevs = NULL;
		return ret;
	}

	return (ret < 0) ? ret : tf.ntevs;
}

#define MAX_VAR_LEN 64

/* Collect available variables in this scope */
static int collect_variables_cb(Dwarf_Die *die_mem, void *data)
{
	struct available_var_finder *af = data;
	struct variable_list *vl;
	char buf[MAX_VAR_LEN];
	int tag, ret;

	vl = &af->vls[af->nvls - 1];

	tag = dwarf_tag(die_mem);
	if (tag == DW_TAG_formal_parameter ||
	    tag == DW_TAG_variable) {
		ret = convert_variable_location(die_mem, af->pf.addr,
						af->pf.fb_ops, NULL);
		if (ret == 0) {
			ret = die_get_varname(die_mem, buf, MAX_VAR_LEN);
			pr_debug2("Add new var: %s\n", buf);
			if (ret > 0)
				strlist__add(vl->vars, buf);
		}
	}

	if (af->child && dwarf_haspc(die_mem, af->pf.addr))
		return DIE_FIND_CB_CONTINUE;
	else
		return DIE_FIND_CB_SIBLING;
}

/* Add a found vars into available variables list */
static int add_available_vars(Dwarf_Die *sp_die, struct probe_finder *pf)
{
	struct available_var_finder *af =
			container_of(pf, struct available_var_finder, pf);
	struct variable_list *vl;
	Dwarf_Die die_mem, *scopes = NULL;
	int ret, nscopes;

	/* Check number of tevs */
	if (af->nvls == af->max_vls) {
		pr_warning("Too many( > %d) probe point found.\n", af->max_vls);
		return -ERANGE;
	}
	vl = &af->vls[af->nvls++];

	ret = convert_to_trace_point(sp_die, pf->addr, pf->pev->point.retprobe,
				     &vl->point);
	if (ret < 0)
		return ret;

	pr_debug("Probe point found: %s+%lu\n", vl->point.symbol,
		 vl->point.offset);

	/* Find local variables */
	vl->vars = strlist__new(true, NULL);
	if (vl->vars == NULL)
		return -ENOMEM;
	af->child = true;
	die_find_child(sp_die, collect_variables_cb, (void *)af, &die_mem);

	/* Find external variables */
	if (!af->externs)
		goto out;
	/* Don't need to search child DIE for externs. */
	af->child = false;
	nscopes = dwarf_getscopes_die(sp_die, &scopes);
	while (nscopes-- > 1)
		die_find_child(&scopes[nscopes], collect_variables_cb,
			       (void *)af, &die_mem);
	if (scopes)
		free(scopes);

out:
	if (strlist__empty(vl->vars)) {
		strlist__delete(vl->vars);
		vl->vars = NULL;
	}

	return ret;
}

/* Find available variables at given probe point */
int find_available_vars_at(int fd, struct perf_probe_event *pev,
			   struct variable_list **vls, int max_vls,
			   bool externs)
{
	struct available_var_finder af = {
			.pf = {.pev = pev, .callback = add_available_vars},
			.max_vls = max_vls, .externs = externs};
	int ret;

	/* Allocate result vls array */
	*vls = zalloc(sizeof(struct variable_list) * max_vls);
	if (*vls == NULL)
		return -ENOMEM;

	af.vls = *vls;
	af.nvls = 0;

	ret = find_probes(fd, &af.pf);
	if (ret < 0) {
		/* Free vlist for error */
		while (af.nvls--) {
			if (af.vls[af.nvls].point.symbol)
				free(af.vls[af.nvls].point.symbol);
			if (af.vls[af.nvls].vars)
				strlist__delete(af.vls[af.nvls].vars);
		}
		free(af.vls);
		*vls = NULL;
		return ret;
	}

	return (ret < 0) ? ret : af.nvls;
}

/* Reverse search */
int find_perf_probe_point(unsigned long addr, struct perf_probe_point *ppt)
{
	Dwarf_Die cudie, spdie, indie;
	Dwarf *dbg = NULL;
	Dwfl *dwfl = NULL;
	Dwarf_Line *line;
	Dwarf_Addr laddr, eaddr, bias = 0;
	const char *tmp;
	int lineno, ret = 0;
	bool found = false;

	/* Open the live linux kernel */
	dbg = dwfl_init_live_kernel_dwarf(addr, &dwfl, &bias);
	if (!dbg) {
		pr_warning("No debug information found in the vmlinux - "
			"please rebuild with CONFIG_DEBUG_INFO=y.\n");
		ret = -EINVAL;
		goto end;
	}

	/* Adjust address with bias */
	addr += bias;
	/* Find cu die */
	if (!dwarf_addrdie(dbg, (Dwarf_Addr)addr - bias, &cudie)) {
		pr_warning("Failed to find debug information for address %lx\n",
			   addr);
		ret = -EINVAL;
		goto end;
	}

	/* Find a corresponding line */
	line = dwarf_getsrc_die(&cudie, (Dwarf_Addr)addr);
	if (line) {
		if (dwarf_lineaddr(line, &laddr) == 0 &&
		    (Dwarf_Addr)addr == laddr &&
		    dwarf_lineno(line, &lineno) == 0) {
			tmp = dwarf_linesrc(line, NULL, NULL);
			if (tmp) {
				ppt->line = lineno;
				ppt->file = strdup(tmp);
				if (ppt->file == NULL) {
					ret = -ENOMEM;
					goto end;
				}
				found = true;
			}
		}
	}

	/* Find a corresponding function */
	if (die_find_real_subprogram(&cudie, (Dwarf_Addr)addr, &spdie)) {
		tmp = dwarf_diename(&spdie);
		if (!tmp || dwarf_entrypc(&spdie, &eaddr) != 0)
			goto end;

		if (ppt->line) {
			if (die_find_inlinefunc(&spdie, (Dwarf_Addr)addr,
						&indie)) {
				/* addr in an inline function */
				tmp = dwarf_diename(&indie);
				if (!tmp)
					goto end;
				ret = dwarf_decl_line(&indie, &lineno);
			} else {
				if (eaddr == addr) {	/* Function entry */
					lineno = ppt->line;
					ret = 0;
				} else
					ret = dwarf_decl_line(&spdie, &lineno);
			}
			if (ret == 0) {
				/* Make a relative line number */
				ppt->line -= lineno;
				goto found;
			}
		}
		/* We don't have a line number, let's use offset */
		ppt->offset = addr - (unsigned long)eaddr;
found:
		ppt->function = strdup(tmp);
		if (ppt->function == NULL) {
			ret = -ENOMEM;
			goto end;
		}
		found = true;
	}

end:
	if (dwfl)
		dwfl_end(dwfl);
	if (ret >= 0)
		ret = found ? 1 : 0;
	return ret;
}

/* Add a line and store the src path */
static int line_range_add_line(const char *src, unsigned int lineno,
			       struct line_range *lr)
{
	/* Copy source path */
	if (!lr->path) {
		lr->path = strdup(src);
		if (lr->path == NULL)
			return -ENOMEM;
	}
	return line_list__add_line(&lr->line_list, lineno);
}

/* Search function declaration lines */
static int line_range_funcdecl_cb(Dwarf_Die *sp_die, void *data)
{
	struct dwarf_callback_param *param = data;
	struct line_finder *lf = param->data;
	const char *src;
	int lineno;

	src = dwarf_decl_file(sp_die);
	if (src && strtailcmp(src, lf->fname) != 0)
		return DWARF_CB_OK;

	if (dwarf_decl_line(sp_die, &lineno) != 0 ||
	    (lf->lno_s > lineno || lf->lno_e < lineno))
		return DWARF_CB_OK;

	param->retval = line_range_add_line(src, lineno, lf->lr);
	if (param->retval < 0)
		return DWARF_CB_ABORT;
	return DWARF_CB_OK;
}

static int find_line_range_func_decl_lines(struct line_finder *lf)
{
	struct dwarf_callback_param param = {.data = (void *)lf, .retval = 0};
	dwarf_getfuncs(&lf->cu_die, line_range_funcdecl_cb, &param, 0);
	return param.retval;
}

/* Find line range from its line number */
static int find_line_range_by_line(Dwarf_Die *sp_die, struct line_finder *lf)
{
	Dwarf_Lines *lines;
	Dwarf_Line *line;
	size_t nlines, i;
	Dwarf_Addr addr;
	int lineno, ret = 0;
	const char *src;
	Dwarf_Die die_mem;

	line_list__init(&lf->lr->line_list);
	if (dwarf_getsrclines(&lf->cu_die, &lines, &nlines) != 0) {
		pr_warning("No source lines found.\n");
		return -ENOENT;
	}

	/* Search probable lines on lines list */
	for (i = 0; i < nlines; i++) {
		line = dwarf_onesrcline(lines, i);
		if (dwarf_lineno(line, &lineno) != 0 ||
		    (lf->lno_s > lineno || lf->lno_e < lineno))
			continue;

		if (sp_die) {
			/* Address filtering 1: does sp_die include addr? */
			if (dwarf_lineaddr(line, &addr) != 0 ||
			    !dwarf_haspc(sp_die, addr))
				continue;

			/* Address filtering 2: No child include addr? */
			if (die_find_inlinefunc(sp_die, addr, &die_mem))
				continue;
		}

		/* TODO: Get fileno from line, but how? */
		src = dwarf_linesrc(line, NULL, NULL);
		if (strtailcmp(src, lf->fname) != 0)
			continue;

		ret = line_range_add_line(src, lineno, lf->lr);
		if (ret < 0)
			return ret;
	}

	/*
	 * Dwarf lines doesn't include function declarations. We have to
	 * check functions list or given function.
	 */
	if (sp_die) {
		src = dwarf_decl_file(sp_die);
		if (src && dwarf_decl_line(sp_die, &lineno) == 0 &&
		    (lf->lno_s <= lineno && lf->lno_e >= lineno))
			ret = line_range_add_line(src, lineno, lf->lr);
	} else
		ret = find_line_range_func_decl_lines(lf);

	/* Update status */
	if (ret >= 0)
		if (!list_empty(&lf->lr->line_list))
			ret = lf->found = 1;
		else
			ret = 0;	/* Lines are not found */
	else {
		free(lf->lr->path);
		lf->lr->path = NULL;
	}
	return ret;
}

static int line_range_inline_cb(Dwarf_Die *in_die, void *data)
{
	struct dwarf_callback_param *param = data;

	param->retval = find_line_range_by_line(in_die, param->data);
	return DWARF_CB_ABORT;	/* No need to find other instances */
}

/* Search function from function name */
static int line_range_search_cb(Dwarf_Die *sp_die, void *data)
{
	struct dwarf_callback_param *param = data;
	struct line_finder *lf = param->data;
	struct line_range *lr = lf->lr;

	pr_debug("find (%llx) %s\n",
		 (unsigned long long)dwarf_dieoffset(sp_die),
		 dwarf_diename(sp_die));
	if (dwarf_tag(sp_die) == DW_TAG_subprogram &&
	    die_compare_name(sp_die, lr->function)) {
		lf->fname = dwarf_decl_file(sp_die);
		dwarf_decl_line(sp_die, &lr->offset);
		pr_debug("fname: %s, lineno:%d\n", lf->fname, lr->offset);
		lf->lno_s = lr->offset + lr->start;
		if (lf->lno_s < 0)	/* Overflow */
			lf->lno_s = INT_MAX;
		lf->lno_e = lr->offset + lr->end;
		if (lf->lno_e < 0)	/* Overflow */
			lf->lno_e = INT_MAX;
		pr_debug("New line range: %d to %d\n", lf->lno_s, lf->lno_e);
		lr->start = lf->lno_s;
		lr->end = lf->lno_e;
		if (dwarf_func_inline(sp_die)) {
			struct dwarf_callback_param _param;
			_param.data = (void *)lf;
			_param.retval = 0;
			dwarf_func_inline_instances(sp_die,
						    line_range_inline_cb,
						    &_param);
			param->retval = _param.retval;
		} else
			param->retval = find_line_range_by_line(sp_die, lf);
		return DWARF_CB_ABORT;
	}
	return DWARF_CB_OK;
}

static int find_line_range_by_func(struct line_finder *lf)
{
	struct dwarf_callback_param param = {.data = (void *)lf, .retval = 0};
	dwarf_getfuncs(&lf->cu_die, line_range_search_cb, &param, 0);
	return param.retval;
}

int find_line_range(int fd, struct line_range *lr)
{
	struct line_finder lf = {.lr = lr, .found = 0};
	int ret = 0;
	Dwarf_Off off = 0, noff;
	size_t cuhl;
	Dwarf_Die *diep;
	Dwarf *dbg = NULL;
	Dwfl *dwfl;
	Dwarf_Addr bias;	/* Currently ignored */
	const char *comp_dir;

	dbg = dwfl_init_offline_dwarf(fd, &dwfl, &bias);
	if (!dbg) {
		pr_warning("No debug information found in the vmlinux - "
			"please rebuild with CONFIG_DEBUG_INFO=y.\n");
		return -EBADF;
	}

	/* Loop on CUs (Compilation Unit) */
	while (!lf.found && ret >= 0) {
		if (dwarf_nextcu(dbg, off, &noff, &cuhl, NULL, NULL, NULL) != 0)
			break;

		/* Get the DIE(Debugging Information Entry) of this CU */
		diep = dwarf_offdie(dbg, off + cuhl, &lf.cu_die);
		if (!diep)
			continue;

		/* Check if target file is included. */
		if (lr->file)
			lf.fname = cu_find_realpath(&lf.cu_die, lr->file);
		else
			lf.fname = 0;

		if (!lr->file || lf.fname) {
			if (lr->function)
				ret = find_line_range_by_func(&lf);
			else {
				lf.lno_s = lr->start;
				lf.lno_e = lr->end;
				ret = find_line_range_by_line(NULL, &lf);
			}
		}
		off = noff;
	}

	/* Store comp_dir */
	if (lf.found) {
		comp_dir = cu_get_comp_dir(&lf.cu_die);
		if (comp_dir) {
			lr->comp_dir = strdup(comp_dir);
			if (!lr->comp_dir)
				ret = -ENOMEM;
		}
	}

	pr_debug("path: %s\n", lr->path);
	dwfl_end(dwfl);
	return (ret < 0) ? ret : lf.found;
}

