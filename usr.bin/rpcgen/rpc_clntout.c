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
#ident	"@(#)rpc_clntout.c	1.15	94/04/25 SMI"
static char sccsid[] = "@(#)rpc_clntout.c 1.11 89/02/22 (C) 1987 SMI";
#endif
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * rpc_clntout.c, Client-stub outputter for the RPC protocol compiler
 * Copyright (C) 1987, Sun Microsytsems, Inc.
 */
#include <stdio.h>
#include <string.h>
#include <rpc/types.h>
#include "rpc_parse.h"
#include "rpc_scan.h"
#include "rpc_util.h"

static void write_program( definition * );
static void printbody( proc_list * );

static char RESULT[] = "clnt_res";


#define DEFAULT_TIMEOUT 25	/* in seconds */


void
write_stubs(void)
{
	list *l;
	definition *def;

	f_print(fout,
		"\n/* Default timeout can be changed using clnt_control() */\n");
	f_print(fout, "static struct timeval TIMEOUT = { %d, 0 };\n",
		DEFAULT_TIMEOUT);
	for (l = defined; l != NULL; l = l->next) {
		def = (definition *) l->val;
		if (def->def_kind == DEF_PROGRAM) {
			write_program(def);
		}
	}
}

static void
write_program(definition *def)
{
	version_list *vp;
	proc_list *proc;

	for (vp = def->def.pr.versions; vp != NULL; vp = vp->next) {
		for (proc = vp->procs; proc != NULL; proc = proc->next) {
			f_print(fout, "\n");
			if (mtflag == 0) {
				ptype(proc->res_prefix, proc->res_type, 1);
				f_print(fout, "*\n");
				pvname(proc->proc_name, vp->vers_num);
				printarglist(proc, RESULT, "clnt", "CLIENT *");
			} else {
				f_print(fout, "enum clnt_stat \n");
				pvname(proc->proc_name, vp->vers_num);
				printarglist(proc, RESULT,  "clnt", "CLIENT *");

			}
			f_print(fout, "{\n");
			printbody(proc);

			f_print(fout, "}\n");
		}
	}
}

/*
 * Writes out declarations of procedure's argument list.
 * In either ANSI C style, in one of old rpcgen style (pass by reference),
 * or new rpcgen style (multiple arguments, pass by value);
 */

/* sample addargname = "clnt"; sample addargtype = "CLIENT * " */

void
printarglist(proc_list *proc, const char *result, const char *addargname,
    const char *addargtype)
{

	decl_list *l;

	if (!newstyle) {
		/* old style: always pass argument by reference */
		f_print(fout, "(");
		ptype(proc->args.decls->decl.prefix,
		      proc->args.decls->decl.type, 1);

		if (mtflag) {/* Generate result field */
			f_print(fout, "*argp, ");
			ptype(proc->res_prefix, proc->res_type, 1);
			f_print(fout, "*%s, %s%s)\n",
				result, addargtype, addargname);
		} else
			f_print(fout, "*argp, %s%s)\n", addargtype, addargname);
	} else if (streq(proc->args.decls->decl.type, "void")) {
		/* newstyle, 0 argument */
		if (mtflag) {
			f_print(fout, "(");
			ptype(proc->res_prefix, proc->res_type, 1);
			f_print(fout, "*%s, %s%s)\n",
				result, addargtype, addargname);
		} else
			f_print(fout, "(%s%s)\n", addargtype, addargname);
	} else {
		/* new style, 1 or multiple arguments */
		f_print(fout, "(");
		for (l = proc->args.decls; l != NULL; l = l->next) {
			pdeclaration(proc->args.argname, &l->decl, 0, ", ");
		}
		if (mtflag) {
			ptype(proc->res_prefix, proc->res_type, 1);
			f_print(fout, "*%s, ", result);

		}
		f_print(fout, "%s%s)\n", addargtype, addargname);
	}
}



static const char *
ampr(const char *type)
{
	if (isvectordef(type, REL_ALIAS)) {
		return ("");
	} else {
		return ("&");
	}
}

static void
printbody(proc_list *proc)
{
	decl_list *l;
	bool_t args2 = (proc->arg_num > 1);

	/*
	 * For new style with multiple arguments, need a structure in which
	 *  to stuff the arguments.
	 */
	

	if (newstyle && args2) {
		f_print(fout, "\t%s", proc->args.argname);
		f_print(fout, " arg;\n");
	}
	if (!mtflag) {
		f_print(fout, "\tstatic ");
		if (streq(proc->res_type, "void")) {
			f_print(fout, "char ");
		} else {
			ptype(proc->res_prefix, proc->res_type, 0);
		}
		f_print(fout, "%s;\n", RESULT);
		f_print(fout, "\n");
		f_print(fout, "\tmemset((char *)%s%s, 0, sizeof (%s));\n",
			ampr(proc->res_type), RESULT, RESULT);

	}
	if (newstyle && !args2 &&
	    (streq(proc->args.decls->decl.type, "void"))) {
		/* newstyle, 0 arguments */

		if (mtflag)
			f_print(fout, "\t return ");
		else
			f_print(fout, "\t if ");

		f_print(fout,
			"(clnt_call(clnt, %s,\n\t\t(xdrproc_t) xdr_void, ",
			proc->proc_name);
		f_print(fout,
			"(caddr_t) NULL,\n\t\t(xdrproc_t) xdr_%s, (caddr_t) %s%s,",
			stringfix(proc->res_type), (mtflag)?"":ampr(proc->res_type),
			RESULT);

		if (mtflag)
			f_print(fout, "\n\t\tTIMEOUT));\n");
		else
			f_print(fout, "\n\t\tTIMEOUT) != RPC_SUCCESS) {\n");

	} else if (newstyle && args2) {
		/*
		 * Newstyle, multiple arguments
		 * stuff arguments into structure
		 */
		for (l = proc->args.decls;  l != NULL; l = l->next) {
			f_print(fout, "\targ.%s = %s;\n",
				l->decl.name, l->decl.name);
		}
		if (mtflag)
			f_print(fout, "\treturn ");
		else
			f_print(fout, "\tif ");			
		f_print(fout,
			"(clnt_call(clnt, %s,\n\t\t(xdrproc_t) xdr_%s",
			proc->proc_name,proc->args.argname);
		f_print(fout,
			", (caddr_t) &arg,\n\t\t(xdrproc_t) xdr_%s, (caddr_t) %s%s,",
			stringfix(proc->res_type), (mtflag)?"":ampr(proc->res_type),
			RESULT);
		if (mtflag)
			f_print(fout, "\n\t\tTIMEOUT));\n");
		else
			f_print(fout, "\n\t\tTIMEOUT) != RPC_SUCCESS) {\n");
	} else {		/* single argument, new or old style */
		if (!mtflag)
			f_print(fout,
			"\tif (clnt_call(clnt, %s,\n\t\t(xdrproc_t) xdr_%s, (caddr_t) %s%s,\n\t\t(xdrproc_t) xdr_%s, (caddr_t) %s%s,\n\t\tTIMEOUT) != RPC_SUCCESS) {\n",
			proc->proc_name,
			stringfix(proc->args.decls->decl.type),
			(newstyle ? "&" : ""),
			(newstyle ? proc->args.decls->decl.name : "argp"),
			stringfix(proc->res_type), ampr(proc->res_type),
			RESULT);
		else
			
		f_print(fout,
			"\treturn (clnt_call(clnt, %s,\n\t\t(xdrproc_t) xdr_%s, (caddr_t) %s%s,\n\t\t(xdrproc_t) xdr_%s, (caddr_t) %s%s,\n\t\tTIMEOUT));\n",
			proc->proc_name,
			stringfix(proc->args.decls->decl.type),
			(newstyle ? "&" : ""),
			(newstyle ? proc->args.decls->decl.name : "argp"),
			stringfix(proc->res_type), "",
			RESULT);
	}
	if (!mtflag) {
		f_print(fout, "\t\treturn (NULL);\n");
		f_print(fout, "\t}\n");

		if (streq(proc->res_type, "void")) {
			f_print(fout, "\treturn ((void *)%s%s);\n",
				ampr(proc->res_type), RESULT);
		} else {
			f_print(fout, "\treturn (%s%s);\n",
				ampr(proc->res_type), RESULT);
		}
	}
}
