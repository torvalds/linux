/*
 * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
 * unrestricted use provided that this legend is included on all tape
 * media and as a part of the software program in whole or part.  Users
 * may copy or modify Sun RPC without charge, but are not authorized
 * to license or distribute it to anyone else except as part of a product or
 * program developed by the user.
 *
 * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
 * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 *
 * Sun RPC is provided with no support and without any obligation on the
 * part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 *
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
 * OR ANY PART THEREOF.
 *
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 *
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 */

#if 0
#ifndef lint
#ident	"@(#)rpc_cout.c	1.14	93/07/05 SMI"
static char sccsid[] = "@(#)rpc_cout.c 1.13 89/02/22 (C) 1987 SMI";
#endif
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * rpc_cout.c, XDR routine outputter for the RPC protocol compiler
 * Copyright (C) 1987, Sun Microsystems, Inc.
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "rpc_parse.h"
#include "rpc_scan.h"
#include "rpc_util.h"

static void print_header( definition * );
static void print_trailer( void );
static void print_stat( int , declaration * );
static void emit_enum( definition * );
static void emit_program( definition * );
static void emit_union( definition * );
static void emit_struct( definition * );
static void emit_typedef( definition * );
static void emit_inline( int, declaration *, int );
static void emit_single_in_line( int, declaration *, int, relation );

/*
 * Emit the C-routine for the given definition
 */
void
emit(definition *def)
{
	if (def->def_kind == DEF_CONST) {
		return;
	}
	if (def->def_kind == DEF_PROGRAM) {
		emit_program(def);
		return;
	}
	if (def->def_kind == DEF_TYPEDEF) {
		/*
		 * now we need to handle declarations like
		 * struct typedef foo foo;
		 * since we dont want this to be expanded into 2 calls to xdr_foo
		 */

		if (strcmp(def->def.ty.old_type, def->def_name) == 0)
			return;
	}
	print_header(def);
	switch (def->def_kind) {
	case DEF_UNION:
		emit_union(def);
		break;
	case DEF_ENUM:
		emit_enum(def);
		break;
	case DEF_STRUCT:
		emit_struct(def);
		break;
	case DEF_TYPEDEF:
		emit_typedef(def);
		break;
		/* DEF_CONST and DEF_PROGRAM have already been handled */
	default:
		break;
	}
	print_trailer();
}

static int
findtype(definition *def, const char *type)
{

	if (def->def_kind == DEF_PROGRAM || def->def_kind == DEF_CONST) {
		return (0);
	} else {
		return (streq(def->def_name, type));
	}
}

static int
undefined(const char *type)
{
	definition *def;

	def = (definition *) FINDVAL(defined, type, findtype);
	return (def == NULL);
}


static void
print_generic_header(const char *procname, int pointerp)
{
	f_print(fout, "\n");
	f_print(fout, "bool_t\n");
	f_print(fout, "xdr_%s(", procname);
	f_print(fout, "XDR *xdrs, ");
	f_print(fout, "%s ", procname);
	if (pointerp)
		f_print(fout, "*");
	f_print(fout, "objp)\n{\n\n");
}

static void
print_header(definition *def)
{
	print_generic_header(def->def_name,
			    def->def_kind != DEF_TYPEDEF ||
			    !isvectordef(def->def.ty.old_type,
					 def->def.ty.rel));
	/* Now add Inline support */

	if (inline_size == 0)
		return;
	/* May cause lint to complain. but  ... */
	f_print(fout, "\tregister long *buf;\n\n");
}

static void
print_prog_header(proc_list *plist)
{
	print_generic_header(plist->args.argname, 1);
}

static void
print_trailer(void)
{
	f_print(fout, "\treturn (TRUE);\n");
	f_print(fout, "}\n");
}


static void
print_ifopen(int indent, const char *name)
{
	tabify(fout, indent);
	f_print(fout, "if (!xdr_%s(xdrs", name);
}

static void
print_ifarg(const char *arg)
{
	f_print(fout, ", %s", arg);
}

static void
print_ifsizeof(int indent, const char *prefix, const char *type)
{
	if (indent) {
		f_print(fout, ",\n");
		tabify(fout, indent);
	} else  {
		f_print(fout, ", ");
	}
	if (streq(type, "bool")) {
		f_print(fout, "sizeof (bool_t), (xdrproc_t) xdr_bool");
	} else {
		f_print(fout, "sizeof (");
		if (undefined(type) && prefix) {
			f_print(fout, "%s ", prefix);
		}
		f_print(fout, "%s), (xdrproc_t) xdr_%s", type, type);
	}
}

static void
print_ifclose(int indent, int brace)
{
	f_print(fout, "))\n");
	tabify(fout, indent);
	f_print(fout, "\treturn (FALSE);\n");
	if (brace)
		f_print(fout, "\t}\n");
}

static void
print_ifstat(int indent, const char *prefix, const char *type, relation rel,
    const char *amax, const char *objname, const char *name)
{
	const char *alt = NULL;
	int brace = 0;

	switch (rel) {
	case REL_POINTER:
		brace = 1;
		f_print(fout, "\t{\n");
		f_print(fout, "\t%s **pp = %s;\n", type, objname);
		print_ifopen(indent, "pointer");
		print_ifarg("(char **)");
		f_print(fout, "pp");
		print_ifsizeof(0, prefix, type);
		break;
	case REL_VECTOR:
		if (streq(type, "string")) {
			alt = "string";
		} else if (streq(type, "opaque")) {
			alt = "opaque";
		}
		if (alt) {
			print_ifopen(indent, alt);
			print_ifarg(objname);
		} else {
			print_ifopen(indent, "vector");
			print_ifarg("(char *)");
			f_print(fout, "%s", objname);
		}
		print_ifarg(amax);
		if (!alt) {
			print_ifsizeof(indent + 1, prefix, type);
		}
		break;
	case REL_ARRAY:
		if (streq(type, "string")) {
			alt = "string";
		} else if (streq(type, "opaque")) {
			alt = "bytes";
		}
		if (streq(type, "string")) {
			print_ifopen(indent, alt);
			print_ifarg(objname);
		} else {
			if (alt) {
				print_ifopen(indent, alt);
			} else {
				print_ifopen(indent, "array");
			}
			print_ifarg("(char **)");
			if (*objname == '&') {
				f_print(fout, "%s.%s_val, (u_int *) %s.%s_len",
					objname, name, objname, name);
			} else {
				f_print(fout,
					"&%s->%s_val, (u_int *) &%s->%s_len",
					objname, name, objname, name);
			}
		}
		print_ifarg(amax);
		if (!alt) {
			print_ifsizeof(indent + 1, prefix, type);
		}
		break;
	case REL_ALIAS:
		print_ifopen(indent, type);
		print_ifarg(objname);
		break;
	}
	print_ifclose(indent, brace);
}

/* ARGSUSED */
static void
emit_enum(definition *def __unused)
{
	print_ifopen(1, "enum");
	print_ifarg("(enum_t *)objp");
	print_ifclose(1, 0);
}

static void
emit_program(definition *def)
{
	decl_list *dl;
	version_list *vlist;
	proc_list *plist;

	for (vlist = def->def.pr.versions; vlist != NULL; vlist = vlist->next)
		for (plist = vlist->procs; plist != NULL; plist = plist->next) {
			if (!newstyle || plist->arg_num < 2)
				continue; /* old style, or single argument */
			print_prog_header(plist);
			for (dl = plist->args.decls; dl != NULL;
			     dl = dl->next)
				print_stat(1, &dl->decl);
			print_trailer();
		}
}


static void
emit_union(definition *def)
{
	declaration *dflt;
	case_list *cl;
	declaration *cs;
	char *object;
	const char *vecformat = "objp->%s_u.%s";
	const char *format = "&objp->%s_u.%s";

	print_stat(1, &def->def.un.enum_decl);
	f_print(fout, "\tswitch (objp->%s) {\n", def->def.un.enum_decl.name);
	for (cl = def->def.un.cases; cl != NULL; cl = cl->next) {

		f_print(fout, "\tcase %s:\n", cl->case_name);
		if (cl->contflag == 1) /* a continued case statement */
			continue;
		cs = &cl->case_decl;
		if (!streq(cs->type, "void")) {
			object = xmalloc(strlen(def->def_name) +
			                 strlen(format) + strlen(cs->name) + 1);
			if (isvectordef (cs->type, cs->rel)) {
				s_print(object, vecformat, def->def_name,
					cs->name);
			} else {
				s_print(object, format, def->def_name,
					cs->name);
			}
			print_ifstat(2, cs->prefix, cs->type, cs->rel,
				     cs->array_max, object, cs->name);
			free(object);
		}
		f_print(fout, "\t\tbreak;\n");
	}
	dflt = def->def.un.default_decl;
	if (dflt != NULL) {
		if (!streq(dflt->type, "void")) {
			f_print(fout, "\tdefault:\n");
			object = xmalloc(strlen(def->def_name) +
			                 strlen(format) + strlen(dflt->name) + 1);
			if (isvectordef (dflt->type, dflt->rel)) {
				s_print(object, vecformat, def->def_name,
					dflt->name);
			} else {
				s_print(object, format, def->def_name,
					dflt->name);
			}

			print_ifstat(2, dflt->prefix, dflt->type, dflt->rel,
				    dflt->array_max, object, dflt->name);
			free(object);
			f_print(fout, "\t\tbreak;\n");
		} else {
			f_print(fout, "\tdefault:\n");
			f_print(fout, "\t\tbreak;\n");
		}
	} else {
		f_print(fout, "\tdefault:\n");
		f_print(fout, "\t\treturn (FALSE);\n");
	}

	f_print(fout, "\t}\n");
}

static void
inline_struct(definition *def, int flag)
{
	decl_list *dl;
	int i, size;
	decl_list *cur, *psav;
	bas_type *ptr;
	char *sizestr;
	const char *plus;
	char ptemp[256];
	int indent = 1;

	cur = NULL;
	if (flag == PUT)
		f_print(fout, "\n\tif (xdrs->x_op == XDR_ENCODE) {\n");
	else
		f_print(fout, "\t\treturn (TRUE);\n\t} else if (xdrs->x_op == XDR_DECODE) {\n");

	i = 0;
	size = 0;
	sizestr = NULL;
	for (dl = def->def.st.decls; dl != NULL; dl = dl->next) { /* xxx */
		/* now walk down the list and check for basic types */
		if ((dl->decl.prefix == NULL) &&
		    ((ptr = find_type(dl->decl.type)) != NULL) &&
		    ((dl->decl.rel == REL_ALIAS) ||
		     (dl->decl.rel == REL_VECTOR))){
			if (i == 0)
				cur = dl;
			i++;

			if (dl->decl.rel == REL_ALIAS)
				size += ptr->length;
			else {
				/* this code is required to handle arrays */
				if (sizestr == NULL)
					plus = "";
				else
					plus = " + ";

				if (ptr->length != 1)
					s_print(ptemp, "%s%s * %d",
						plus, dl->decl.array_max,
						ptr->length);
				else
					s_print(ptemp, "%s%s", plus,
						dl->decl.array_max);

				/* now concatenate to sizestr !!!! */
				if (sizestr == NULL) {
					sizestr = xstrdup(ptemp);
				}
				else{
					sizestr = xrealloc(sizestr,
							  strlen(sizestr)
							  +strlen(ptemp)+1);
					sizestr = strcat(sizestr, ptemp);
					/* build up length of array */
				}
			}
		} else {
			if (i > 0) {
				if (sizestr == NULL && size < inline_size){
					/*
					 * don't expand into inline code
					 * if size < inline_size
					 */
					while (cur != dl){
						print_stat(indent + 1, &cur->decl);
						cur = cur->next;
					}
				} else {
					/* were already looking at a xdr_inlineable structure */
					tabify(fout, indent + 1);
					if (sizestr == NULL)
						f_print(fout, "buf = XDR_INLINE(xdrs, %d * BYTES_PER_XDR_UNIT);",
							size);
					else {
						if (size == 0)
							f_print(fout,
								"buf = XDR_INLINE(xdrs, (%s) * BYTES_PER_XDR_UNIT);",
								sizestr);
						else
							f_print(fout,
								"buf = XDR_INLINE(xdrs, (%d + (%s)) * BYTES_PER_XDR_UNIT);",
								size, sizestr);

					}
					f_print(fout, "\n");
					tabify(fout, indent + 1);
					f_print(fout,
						"if (buf == NULL) {\n");

					psav = cur;
					while (cur != dl){
						print_stat(indent + 2, &cur->decl);
						cur = cur->next;
					}

					f_print(fout, "\n\t\t} else {\n");

					cur = psav;
					while (cur != dl){
						emit_inline(indent + 2, &cur->decl, flag);
						cur = cur->next;
					}

					tabify(fout, indent + 1);
					f_print(fout, "}\n");
				}
			}
			size = 0;
			i = 0;
			free(sizestr);
			sizestr = NULL;
			print_stat(indent + 1, &dl->decl);
		}
	}

	if (i > 0) {
		if (sizestr == NULL && size < inline_size){
			/* don't expand into inline code if size < inline_size */
			while (cur != dl){
				print_stat(indent + 1, &cur->decl);
				cur = cur->next;
			}
		} else {
			/* were already looking at a xdr_inlineable structure */
			if (sizestr == NULL)
				f_print(fout, "\t\tbuf = XDR_INLINE(xdrs, %d * BYTES_PER_XDR_UNIT);",
					size);
			else
				if (size == 0)
					f_print(fout,
						"\t\tbuf = XDR_INLINE(xdrs, (%s) * BYTES_PER_XDR_UNIT);",
						sizestr);
				else
					f_print(fout,
						"\t\tbuf = XDR_INLINE(xdrs, (%d + (%s)) * BYTES_PER_XDR_UNIT);",
						size, sizestr);

			f_print(fout, "\n\t\tif (buf == NULL) {\n");
			psav = cur;
			while (cur != NULL){
				print_stat(indent + 2, &cur->decl);
				cur = cur->next;
			}
			f_print(fout, "\t\t} else {\n");

			cur = psav;
			while (cur != dl){
				emit_inline(indent + 2, &cur->decl, flag);
				cur = cur->next;
			}
			f_print(fout, "\t\t}\n");
		}
	}
}

static void
emit_struct(definition *def)
{
	decl_list *dl;
	int j, size, flag;
	bas_type *ptr;
	int can_inline;

	if (inline_size == 0) {
		/* No xdr_inlining at all */
		for (dl = def->def.st.decls; dl != NULL; dl = dl->next)
			print_stat(1, &dl->decl);
		return;
	}

	for (dl = def->def.st.decls; dl != NULL; dl = dl->next)
		if (dl->decl.rel == REL_VECTOR &&
		    strcmp(dl->decl.type, "opaque") != 0){
			f_print(fout, "\tint i;\n");
			break;
		}

	size = 0;
	can_inline = 0;
	/*
	 * Make a first pass and see if inling is possible.
	 */
	for (dl = def->def.st.decls; dl != NULL; dl = dl->next)
		if ((dl->decl.prefix == NULL) &&
		    ((ptr = find_type(dl->decl.type)) != NULL) &&
		    ((dl->decl.rel == REL_ALIAS)||
		     (dl->decl.rel == REL_VECTOR))){
			if (dl->decl.rel == REL_ALIAS)
				size += ptr->length;
			else {
				can_inline = 1;
				break; /* can be inlined */
			}
		} else {
			if (size >= inline_size){
				can_inline = 1;
				break; /* can be inlined */
			}
			size = 0;
		}
	if (size >= inline_size)
		can_inline = 1;

	if (can_inline == 0){	/* can not inline, drop back to old mode */
		for (dl = def->def.st.decls; dl != NULL; dl = dl->next)
			print_stat(1, &dl->decl);
		return;
	}

	flag = PUT;
	for (j = 0; j < 2; j++){
		inline_struct(def, flag);
		if (flag == PUT)
			flag = GET;
	}

	f_print(fout, "\t\treturn (TRUE);\n\t}\n\n");

	/* now take care of XDR_FREE case */

	for (dl = def->def.st.decls; dl != NULL; dl = dl->next)
		print_stat(1, &dl->decl);

}

static void
emit_typedef(definition *def)
{
	const char *prefix = def->def.ty.old_prefix;
	const char *type = def->def.ty.old_type;
	const char *amax = def->def.ty.array_max;
	relation rel = def->def.ty.rel;

	print_ifstat(1, prefix, type, rel, amax, "objp", def->def_name);
}

static void
print_stat(int indent, declaration *dec)
{
	const char *prefix = dec->prefix;
	const char *type = dec->type;
	const char *amax = dec->array_max;
	relation rel = dec->rel;
	char name[256];

	if (isvectordef(type, rel)) {
		s_print(name, "objp->%s", dec->name);
	} else {
		s_print(name, "&objp->%s", dec->name);
	}
	print_ifstat(indent, prefix, type, rel, amax, name, dec->name);
}


char *upcase(const char *);

static void
emit_inline(int indent, declaration *decl, int flag)
{
	switch (decl->rel) {
	case  REL_ALIAS :
		emit_single_in_line(indent, decl, flag, REL_ALIAS);
		break;
	case REL_VECTOR :
		tabify(fout, indent);
		f_print(fout, "{\n");
		tabify(fout, indent + 1);
		f_print(fout, "%s *genp;\n\n", decl->type);
		tabify(fout, indent + 1);
		f_print(fout,
			"for (i = 0, genp = objp->%s;\n", decl->name);
		tabify(fout, indent + 2);
		f_print(fout, "i < %s; i++) {\n", decl->array_max);
		emit_single_in_line(indent + 2, decl, flag, REL_VECTOR);
		tabify(fout, indent + 1);
		f_print(fout, "}\n");
		tabify(fout, indent);
		f_print(fout, "}\n");
		break;
	default:
		break;
	}
}

static void
emit_single_in_line(int indent, declaration *decl, int flag, relation rel)
{
	char *upp_case;

	tabify(fout, indent);
	if (flag == PUT)
		f_print(fout, "IXDR_PUT_");
	else
		if (rel == REL_ALIAS)
			f_print(fout, "objp->%s = IXDR_GET_", decl->name);
		else
			f_print(fout, "*genp++ = IXDR_GET_");

	upp_case = upcase(decl->type);

	/* hack	 - XX */
	if (strcmp(upp_case, "INT") == 0)
	{
		free(upp_case);
		upp_case = strdup("LONG");
	}

	if (strcmp(upp_case, "U_INT") == 0)
	{
		free(upp_case);
		upp_case = strdup("U_LONG");
	}
	if (flag == PUT)
		if (rel == REL_ALIAS)
			f_print(fout,
				"%s(buf, objp->%s);\n", upp_case, decl->name);
		else
			f_print(fout, "%s(buf, *genp++);\n", upp_case);

	else
		f_print(fout, "%s(buf);\n", upp_case);
	free(upp_case);
}

char *
upcase(const char *str)
{
	char *ptr, *hptr;

	ptr = (char *)xmalloc(strlen(str)+1);

	hptr = ptr;
	while (*str != '\0')
		*ptr++ = toupper(*str++);

	*ptr = '\0';
	return (hptr);
}
