/*
 * dwarf-aux.c : libdw auxiliary interfaces
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

#include <stdbool.h>
#include "util.h"
#include "debug.h"
#include "dwarf-aux.h"

/**
 * cu_find_realpath - Find the realpath of the target file
 * @cu_die: A DIE(dwarf information entry) of CU(compilation Unit)
 * @fname:  The tail filename of the target file
 *
 * Find the real(long) path of @fname in @cu_die.
 */
const char *cu_find_realpath(Dwarf_Die *cu_die, const char *fname)
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

/**
 * cu_get_comp_dir - Get the path of compilation directory
 * @cu_die: a CU DIE
 *
 * Get the path of compilation directory of given @cu_die.
 * Since this depends on DW_AT_comp_dir, older gcc will not
 * embedded it. In that case, this returns NULL.
 */
const char *cu_get_comp_dir(Dwarf_Die *cu_die)
{
	Dwarf_Attribute attr;
	if (dwarf_attr(cu_die, DW_AT_comp_dir, &attr) == NULL)
		return NULL;
	return dwarf_formstring(&attr);
}

/**
 * cu_find_lineinfo - Get a line number and file name for given address
 * @cu_die: a CU DIE
 * @addr: An address
 * @fname: a pointer which returns the file name string
 * @lineno: a pointer which returns the line number
 *
 * Find a line number and file name for @addr in @cu_die.
 */
int cu_find_lineinfo(Dwarf_Die *cu_die, unsigned long addr,
		    const char **fname, int *lineno)
{
	Dwarf_Line *line;
	Dwarf_Addr laddr;

	line = dwarf_getsrc_die(cu_die, (Dwarf_Addr)addr);
	if (line && dwarf_lineaddr(line, &laddr) == 0 &&
	    addr == (unsigned long)laddr && dwarf_lineno(line, lineno) == 0) {
		*fname = dwarf_linesrc(line, NULL, NULL);
		if (!*fname)
			/* line number is useless without filename */
			*lineno = 0;
	}

	return *lineno ?: -ENOENT;
}

/**
 * die_compare_name - Compare diename and tname
 * @dw_die: a DIE
 * @tname: a string of target name
 *
 * Compare the name of @dw_die and @tname. Return false if @dw_die has no name.
 */
bool die_compare_name(Dwarf_Die *dw_die, const char *tname)
{
	const char *name;
	name = dwarf_diename(dw_die);
	return name ? (strcmp(tname, name) == 0) : false;
}

/**
 * die_get_call_lineno - Get callsite line number of inline-function instance
 * @in_die: a DIE of an inlined function instance
 *
 * Get call-site line number of @in_die. This means from where the inline
 * function is called.
 */
int die_get_call_lineno(Dwarf_Die *in_die)
{
	Dwarf_Attribute attr;
	Dwarf_Word ret;

	if (!dwarf_attr(in_die, DW_AT_call_line, &attr))
		return -ENOENT;

	dwarf_formudata(&attr, &ret);
	return (int)ret;
}

/**
 * die_get_type - Get type DIE
 * @vr_die: a DIE of a variable
 * @die_mem: where to store a type DIE
 *
 * Get a DIE of the type of given variable (@vr_die), and store
 * it to die_mem. Return NULL if fails to get a type DIE.
 */
Dwarf_Die *die_get_type(Dwarf_Die *vr_die, Dwarf_Die *die_mem)
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

/**
 * die_get_real_type - Get a type die, but skip qualifiers and typedef
 * @vr_die: a DIE of a variable
 * @die_mem: where to store a type DIE
 *
 * Get a DIE of the type of given variable (@vr_die), and store
 * it to die_mem. Return NULL if fails to get a type DIE.
 * If the type is qualifiers (e.g. const) or typedef, this skips it
 * and tries to find real type (structure or basic types, e.g. int).
 */
Dwarf_Die *die_get_real_type(Dwarf_Die *vr_die, Dwarf_Die *die_mem)
{
	do {
		vr_die = __die_get_real_type(vr_die, die_mem);
	} while (vr_die && dwarf_tag(vr_die) == DW_TAG_typedef);

	return vr_die;
}

/* Get attribute and translate it as a udata */
static int die_get_attr_udata(Dwarf_Die *tp_die, unsigned int attr_name,
			      Dwarf_Word *result)
{
	Dwarf_Attribute attr;

	if (dwarf_attr(tp_die, attr_name, &attr) == NULL ||
	    dwarf_formudata(&attr, result) != 0)
		return -ENOENT;

	return 0;
}

/**
 * die_is_signed_type - Check whether a type DIE is signed or not
 * @tp_die: a DIE of a type
 *
 * Get the encoding of @tp_die and return true if the encoding
 * is signed.
 */
bool die_is_signed_type(Dwarf_Die *tp_die)
{
	Dwarf_Word ret;

	if (die_get_attr_udata(tp_die, DW_AT_encoding, &ret))
		return false;

	return (ret == DW_ATE_signed_char || ret == DW_ATE_signed ||
		ret == DW_ATE_signed_fixed);
}

/**
 * die_get_data_member_location - Get the data-member offset
 * @mb_die: a DIE of a member of a data structure
 * @offs: The offset of the member in the data structure
 *
 * Get the offset of @mb_die in the data structure including @mb_die, and
 * stores result offset to @offs. If any error occurs this returns errno.
 */
int die_get_data_member_location(Dwarf_Die *mb_die, Dwarf_Word *offs)
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

/**
 * die_find_child - Generic DIE search function in DIE tree
 * @rt_die: a root DIE
 * @callback: a callback function
 * @data: a user data passed to the callback function
 * @die_mem: a buffer for result DIE
 *
 * Trace DIE tree from @rt_die and call @callback for each child DIE.
 * If @callback returns DIE_FIND_CB_END, this stores the DIE into
 * @die_mem and returns it. If @callback returns DIE_FIND_CB_CONTINUE,
 * this continues to trace the tree. Optionally, @callback can return
 * DIE_FIND_CB_CHILD and DIE_FIND_CB_SIBLING, those means trace only
 * the children and trace only the siblings respectively.
 * Returns NULL if @callback can't find any appropriate DIE.
 */
Dwarf_Die *die_find_child(Dwarf_Die *rt_die,
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
		if (ret == DIE_FIND_CB_END)
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

/* die_find callback for non-inlined function search */
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

/**
 * die_find_realfunc - Search a non-inlined function at given address
 * @cu_die: a CU DIE which including @addr
 * @addr: target address
 * @die_mem: a buffer for result DIE
 *
 * Search a non-inlined function DIE which includes @addr. Stores the
 * DIE to @die_mem and returns it if found. Returns NULl if failed.
 */
Dwarf_Die *die_find_realfunc(Dwarf_Die *cu_die, Dwarf_Addr addr,
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
		return DIE_FIND_CB_END;

	return DIE_FIND_CB_CONTINUE;
}

/**
 * die_find_inlinefunc - Search an inlined function at given address
 * @cu_die: a CU DIE which including @addr
 * @addr: target address
 * @die_mem: a buffer for result DIE
 *
 * Search an inlined function DIE which includes @addr. Stores the
 * DIE to @die_mem and returns it if found. Returns NULl if failed.
 * If several inlined functions are expanded recursively, this trace
 * it and returns deepest one.
 */
Dwarf_Die *die_find_inlinefunc(Dwarf_Die *sp_die, Dwarf_Addr addr,
			       Dwarf_Die *die_mem)
{
	Dwarf_Die tmp_die;

	sp_die = die_find_child(sp_die, __die_find_inline_cb, &addr, &tmp_die);
	if (!sp_die)
		return NULL;

	/* Inlined function could be recursive. Trace it until fail */
	while (sp_die) {
		memcpy(die_mem, sp_die, sizeof(Dwarf_Die));
		sp_die = die_find_child(sp_die, __die_find_inline_cb, &addr,
					&tmp_die);
	}

	return die_mem;
}

/* Line walker internal parameters */
struct __line_walk_param {
	const char *fname;
	line_walk_callback_t callback;
	void *data;
	int retval;
};

static int __die_walk_funclines_cb(Dwarf_Die *in_die, void *data)
{
	struct __line_walk_param *lw = data;
	Dwarf_Addr addr;
	int lineno;

	if (dwarf_tag(in_die) == DW_TAG_inlined_subroutine) {
		lineno = die_get_call_lineno(in_die);
		if (lineno > 0 && dwarf_entrypc(in_die, &addr) == 0) {
			lw->retval = lw->callback(lw->fname, lineno, addr,
						  lw->data);
			if (lw->retval != 0)
				return DIE_FIND_CB_END;
		}
	}
	return DIE_FIND_CB_SIBLING;
}

/* Walk on lines of blocks included in given DIE */
static int __die_walk_funclines(Dwarf_Die *sp_die,
				line_walk_callback_t callback, void *data)
{
	struct __line_walk_param lw = {
		.callback = callback,
		.data = data,
		.retval = 0,
	};
	Dwarf_Die die_mem;
	Dwarf_Addr addr;
	int lineno;

	/* Handle function declaration line */
	lw.fname = dwarf_decl_file(sp_die);
	if (lw.fname && dwarf_decl_line(sp_die, &lineno) == 0 &&
	    dwarf_entrypc(sp_die, &addr) == 0) {
		lw.retval = callback(lw.fname, lineno, addr, data);
		if (lw.retval != 0)
			goto done;
	}
	die_find_child(sp_die, __die_walk_funclines_cb, &lw, &die_mem);
done:
	return lw.retval;
}

static int __die_walk_culines_cb(Dwarf_Die *sp_die, void *data)
{
	struct __line_walk_param *lw = data;

	lw->retval = __die_walk_funclines(sp_die, lw->callback, lw->data);
	if (lw->retval != 0)
		return DWARF_CB_ABORT;

	return DWARF_CB_OK;
}

/**
 * die_walk_lines - Walk on lines inside given DIE
 * @rt_die: a root DIE (CU or subprogram)
 * @callback: callback routine
 * @data: user data
 *
 * Walk on all lines inside given @rt_die and call @callback on each line.
 * If the @rt_die is a function, walk only on the lines inside the function,
 * otherwise @rt_die must be a CU DIE.
 * Note that this walks not only dwarf line list, but also function entries
 * and inline call-site.
 */
int die_walk_lines(Dwarf_Die *rt_die, line_walk_callback_t callback, void *data)
{
	Dwarf_Lines *lines;
	Dwarf_Line *line;
	Dwarf_Addr addr;
	const char *fname;
	int lineno, ret = 0;
	Dwarf_Die die_mem, *cu_die;
	size_t nlines, i;

	/* Get the CU die */
	if (dwarf_tag(rt_die) == DW_TAG_subprogram)
		cu_die = dwarf_diecu(rt_die, &die_mem, NULL, NULL);
	else
		cu_die = rt_die;
	if (!cu_die) {
		pr_debug2("Failed to get CU from subprogram\n");
		return -EINVAL;
	}

	/* Get lines list in the CU */
	if (dwarf_getsrclines(cu_die, &lines, &nlines) != 0) {
		pr_debug2("Failed to get source lines on this CU.\n");
		return -ENOENT;
	}
	pr_debug2("Get %zd lines from this CU\n", nlines);

	/* Walk on the lines on lines list */
	for (i = 0; i < nlines; i++) {
		line = dwarf_onesrcline(lines, i);
		if (line == NULL ||
		    dwarf_lineno(line, &lineno) != 0 ||
		    dwarf_lineaddr(line, &addr) != 0) {
			pr_debug2("Failed to get line info. "
				  "Possible error in debuginfo.\n");
			continue;
		}
		/* Filter lines based on address */
		if (rt_die != cu_die)
			/*
			 * Address filtering
			 * The line is included in given function, and
			 * no inline block includes it.
			 */
			if (!dwarf_haspc(rt_die, addr) ||
			    die_find_inlinefunc(rt_die, addr, &die_mem))
				continue;
		/* Get source line */
		fname = dwarf_linesrc(line, NULL, NULL);

		ret = callback(fname, lineno, addr, data);
		if (ret != 0)
			return ret;
	}

	/*
	 * Dwarf lines doesn't include function declarations and inlined
	 * subroutines. We have to check functions list or given function.
	 */
	if (rt_die != cu_die)
		ret = __die_walk_funclines(rt_die, callback, data);
	else {
		struct __line_walk_param param = {
			.callback = callback,
			.data = data,
			.retval = 0,
		};
		dwarf_getfuncs(cu_die, __die_walk_culines_cb, &param, 0);
		ret = param.retval;
	}

	return ret;
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
		return DIE_FIND_CB_END;

	if (dwarf_haspc(die_mem, fvp->addr))
		return DIE_FIND_CB_CONTINUE;
	else
		return DIE_FIND_CB_SIBLING;
}

/**
 * die_find_variable_at - Find a given name variable at given address
 * @sp_die: a function DIE
 * @name: variable name
 * @addr: address
 * @die_mem: a buffer for result DIE
 *
 * Find a variable DIE called @name at @addr in @sp_die.
 */
Dwarf_Die *die_find_variable_at(Dwarf_Die *sp_die, const char *name,
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
		return DIE_FIND_CB_END;

	return DIE_FIND_CB_SIBLING;
}

/**
 * die_find_member - Find a given name member in a data structure
 * @st_die: a data structure type DIE
 * @name: member name
 * @die_mem: a buffer for result DIE
 *
 * Find a member DIE called @name in @st_die.
 */
Dwarf_Die *die_find_member(Dwarf_Die *st_die, const char *name,
			   Dwarf_Die *die_mem)
{
	return die_find_child(st_die, __die_find_member_cb, (void *)name,
			      die_mem);
}

/**
 * die_get_typename - Get the name of given variable DIE
 * @vr_die: a variable DIE
 * @buf: a buffer for result type name
 * @len: a max-length of @buf
 *
 * Get the name of @vr_die and stores it to @buf. Return the actual length
 * of type name if succeeded. Return -E2BIG if @len is not enough long, and
 * Return -ENOENT if failed to find type name.
 * Note that the result will stores typedef name if possible, and stores
 * "*(function_type)" if the type is a function pointer.
 */
int die_get_typename(Dwarf_Die *vr_die, char *buf, int len)
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

/**
 * die_get_varname - Get the name and type of given variable DIE
 * @vr_die: a variable DIE
 * @buf: a buffer for type and variable name
 * @len: the max-length of @buf
 *
 * Get the name and type of @vr_die and stores it in @buf as "type\tname".
 */
int die_get_varname(Dwarf_Die *vr_die, char *buf, int len)
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

