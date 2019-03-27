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
#ident	"@(#)rpc_main.c	1.21	94/04/25 SMI"
static char sccsid[] = "@(#)rpc_main.c 1.30 89/03/30 (C) 1987 SMI";
#endif
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * rpc_main.c, Top level of the RPC protocol compiler.
 * Copyright (C) 1987, Sun Microsystems, Inc.
 */

#include <err.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/file.h>
#include <sys/stat.h>
#include "rpc_parse.h"
#include "rpc_scan.h"
#include "rpc_util.h"

static void c_output(const char *, const char *, int, const char *);
static void h_output(const char *, const char *, int, const char *, int);
static void l_output(const char *, const char *, int, const char *);
static void t_output(const char *, const char *, int, const char *);
static void clnt_output(const char *, const char *, int, const char * );
static char *generate_guard(const char *);
static void c_initialize(void);

static void usage(void);
static void options_usage(void);
static int do_registers(int, const char **);
static int parseargs(int, const char **, struct commandline *);
static void svc_output(const char *, const char *, int, const char *);
static void mkfile_output(struct commandline *);
static void s_output(int, const char **, const char *, const char *, int, const char *, int, int);

#define	EXTEND	1		/* alias for TRUE */
#define	DONT_EXTEND	0		/* alias for FALSE */

static const char *svcclosetime = "120";
static const char *CPP = NULL;
static const char CPPFLAGS[] = "-C";
static char pathbuf[MAXPATHLEN + 1];
static const char *allv[] = {
	"rpcgen", "-s", "udp", "-s", "tcp",
};
static int allc = nitems(allv);
static const char *allnv[] = {
	"rpcgen", "-s", "netpath",
};
static int allnc = nitems(allnv);

/*
 * machinations for handling expanding argument list
 */
static void addarg(const char *);	/* add another argument to the list */
static void insarg(int, const char *);	/* insert arg at specified location */
static void clear_args(void);		/* clear argument list */
static void checkfiles(const char *, const char *);
					/* check if out file already exists */



#define	ARGLISTLEN	20
#define	FIXEDARGS	0

static char *arglist[ARGLISTLEN];
static int argcount = FIXEDARGS;


int nonfatalerrors;	/* errors */
int inetdflag = 0;	/* Support for inetd is disabled by default, use -I */
int pmflag = 0;		/* Support for port monitors is disabled by default */
int tirpc_socket = 1;	/* TI-RPC on socket, no TLI library */
int logflag;		/* Use syslog instead of fprintf for errors */
int tblflag;		/* Support for dispatch table file */
int mtflag = 0;		/* Support for MT */

#define INLINE 0
/* length at which to start doing an inline */

int inline_size = INLINE;
/*
 * Length at which to start doing an inline. INLINE = default
 * if 0, no xdr_inline code
 */

int indefinitewait;	/* If started by port monitors, hang till it wants */
int exitnow;		/* If started by port monitors, exit after the call */
int timerflag;		/* TRUE if !indefinite && !exitnow */
int newstyle;		/* newstyle of passing arguments (by value) */
int CCflag = 0;		/* C++ files */
static int allfiles;   /* generate all files */
int tirpcflag = 1;    /* generating code for tirpc, by default */
xdrfunc *xdrfunc_head = NULL; /* xdr function list */
xdrfunc *xdrfunc_tail = NULL; /* xdr function list */
pid_t childpid;


int
main(int argc, const char *argv[])
{
	struct commandline cmd;

	(void) memset((char *)&cmd, 0, sizeof (struct commandline));
	clear_args();
	if (!parseargs(argc, argv, &cmd))
		usage();
	/*
	 * Only the client and server side stubs are likely to be customized,
	 *  so in that case only, check if the outfile exists, and if so,
	 *  print an error message and exit.
	 */
	if (cmd.Ssflag || cmd.Scflag || cmd.makefileflag) {
		checkfiles(cmd.infile, cmd.outfile);
	}
	else
		checkfiles(cmd.infile, NULL);

	if (cmd.cflag) {
		c_output(cmd.infile, "-DRPC_XDR", DONT_EXTEND, cmd.outfile);
	} else if (cmd.hflag) {
		h_output(cmd.infile, "-DRPC_HDR", DONT_EXTEND, cmd.outfile,
		    cmd.hflag);
	} else if (cmd.lflag) {
		l_output(cmd.infile, "-DRPC_CLNT", DONT_EXTEND, cmd.outfile);
	} else if (cmd.sflag || cmd.mflag || (cmd.nflag)) {
		s_output(argc, argv, cmd.infile, "-DRPC_SVC", DONT_EXTEND,
			cmd.outfile, cmd.mflag, cmd.nflag);
	} else if (cmd.tflag) {
		t_output(cmd.infile, "-DRPC_TBL", DONT_EXTEND, cmd.outfile);
	} else if  (cmd.Ssflag) {
		svc_output(cmd.infile, "-DRPC_SERVER", DONT_EXTEND,
			cmd.outfile);
	} else if (cmd.Scflag) {
		clnt_output(cmd.infile, "-DRPC_CLIENT", DONT_EXTEND,
			    cmd.outfile);
	} else if (cmd.makefileflag) {
		mkfile_output(&cmd);
	} else {
		/* the rescans are required, since cpp may effect input */
		c_output(cmd.infile, "-DRPC_XDR", EXTEND, "_xdr.c");
		reinitialize();
		h_output(cmd.infile, "-DRPC_HDR", EXTEND, ".h", cmd.hflag);
		reinitialize();
		l_output(cmd.infile, "-DRPC_CLNT", EXTEND, "_clnt.c");
		reinitialize();
		if (inetdflag || !tirpcflag)
			s_output(allc, allv, cmd.infile, "-DRPC_SVC", EXTEND,
			"_svc.c", cmd.mflag, cmd.nflag);
		else
			s_output(allnc, allnv, cmd.infile, "-DRPC_SVC",
				EXTEND, "_svc.c", cmd.mflag, cmd.nflag);
		if (tblflag) {
			reinitialize();
			t_output(cmd.infile, "-DRPC_TBL", EXTEND, "_tbl.i");
		}

		if (allfiles) {
			reinitialize();
			svc_output(cmd.infile, "-DRPC_SERVER", EXTEND,
				"_server.c");
			reinitialize();
			clnt_output(cmd.infile, "-DRPC_CLIENT", EXTEND,
				"_client.c");

		}
		if (allfiles || (cmd.makefileflag == 1)){
			reinitialize();
			mkfile_output(&cmd);
		}

	}
	exit(nonfatalerrors);
	/* NOTREACHED */
}


/*
 * add extension to filename
 */
static char *
extendfile(const char *path, const char *ext)
{
	char *res;
	const char *p;
	const char *file;

	if ((file = strrchr(path, '/')) == NULL)
		file = path;
	else
		file++;
	res = xmalloc(strlen(file) + strlen(ext) + 1);
	p = strrchr(file, '.');
	if (p == NULL) {
		p = file + strlen(file);
	}
	(void) strcpy(res, file);
	(void) strcpy(res + (p - file), ext);
	return (res);
}

/*
 * Open output file with given extension
 */
static void
open_output(const char *infile, const char *outfile)
{

	if (outfile == NULL) {
		fout = stdout;
		return;
	}

	if (infile != NULL && streq(outfile, infile)) {
		warnx("%s already exists. No output generated", infile);
		crash();
	}
	fout = fopen(outfile, "w");
	if (fout == NULL) {
		warn("unable to open %s", outfile);
		crash();
	}
	record_open(outfile);

	return;
}

static void
add_warning(void)
{
	f_print(fout, "/*\n");
	f_print(fout, " * Please do not edit this file.\n");
	f_print(fout, " * It was generated using rpcgen.\n");
	f_print(fout, " */\n\n");
}

/* clear list of arguments */
static void
clear_args(void)
{
	int i;
	for (i = FIXEDARGS; i < ARGLISTLEN; i++)
		arglist[i] = NULL;
	argcount = FIXEDARGS;
}

/* prepend C-preprocessor and flags before arguments */
static void
prepend_cpp(void)
{
	int idx = 1;
	const char *var;
	char *dupvar, *s, *t;

	if (CPP != NULL)
		insarg(0, CPP);
	else if ((var = getenv("RPCGEN_CPP")) == NULL)
		insarg(0, "/usr/bin/cpp");
	else {
		/* Parse command line in a rudimentary way */
		dupvar = xstrdup(var);
		for (s = dupvar, idx = 0; (t = strsep(&s, " \t")) != NULL; ) {
			if (t[0])
				insarg(idx++, t);
		}
		free(dupvar);
	}

	insarg(idx, CPPFLAGS);
}

/*
 * Open input file with given define for C-preprocessor
 */
static void
open_input(const char *infile, const char *define)
{
	int pd[2];

	infilename = (infile == NULL) ? "<stdin>" : infile;
	(void) pipe(pd);
	switch (childpid = fork()) {
	case 0:
		prepend_cpp();
		addarg(define);
		if (infile)
			addarg(infile);
		addarg((char *)NULL);
		(void) close(1);
		(void) dup2(pd[1], 1);
		(void) close(pd[0]);
		execvp(arglist[0], arglist);
		err(1, "execvp %s", arglist[0]);
	case -1:
		err(1, "fork");
	}
	(void) close(pd[1]);
	fin = fdopen(pd[0], "r");
	if (fin == NULL) {
		warn("%s", infilename);
		crash();
	}
}

/* valid tirpc nettypes */
static const char *valid_ti_nettypes[] =
{
	"netpath",
	"visible",
	"circuit_v",
	"datagram_v",
	"circuit_n",
	"datagram_n",
	"udp",
	"tcp",
	"raw",
	NULL
	};

/* valid inetd nettypes */
static const char *valid_i_nettypes[] =
{
	"udp",
	"tcp",
	NULL
	};

static int
check_nettype(const char *name, const char *list_to_check[])
{
	int i;
	for (i = 0; list_to_check[i] != NULL; i++) {
		if (strcmp(name, list_to_check[i]) == 0) {
			return (1);
		}
	}
	warnx("illegal nettype :\'%s\'", name);
	return (0);
}

static const char *
file_name(const char *file, const char *ext)
{
	char *temp;
	temp = extendfile(file, ext);

	if (access(temp, F_OK) != -1)
		return (temp);
	else
		return (" ");

}


static void
c_output(const char *infile, const char *define, int extend, const char *outfile)
{
	definition *def;
	char *include;
	const char *outfilename;
	long tell;

	c_initialize();
	open_input(infile, define);
	outfilename = extend ? extendfile(infile, outfile) : outfile;
	open_output(infile, outfilename);
	add_warning();
	if (infile && (include = extendfile(infile, ".h"))) {
		f_print(fout, "#include \"%s\"\n", include);
		free(include);
		/* .h file already contains rpc/rpc.h */
	} else
		f_print(fout, "#include <rpc/rpc.h>\n");
	tell = ftell(fout);
	while ( (def = get_definition()) ) {
		emit(def);
	}
	if (extend && tell == ftell(fout)) {
		(void) unlink(outfilename);
	}
}


void
c_initialize(void)
{

	/* add all the starting basic types */
	add_type(1, "int");
	add_type(1, "long");
	add_type(1, "short");
	add_type(1, "bool");
	add_type(1, "u_int");
	add_type(1, "u_long");
	add_type(1, "u_short");

}

static const char rpcgen_table_dcl[] = "struct rpcgen_table {\n\
	char	*(*proc)(); \n\
	xdrproc_t	xdr_arg; \n\
	unsigned	len_arg; \n\
	xdrproc_t	xdr_res; \n\
	unsigned	len_res; \n\
}; \n";


char *
generate_guard(const char *pathname)
{
	const char *filename;
	char *guard, *tmp, *stopat;

	filename = strrchr(pathname, '/');  /* find last component */
	filename = ((filename == NULL) ? pathname : filename+1);
	guard = xstrdup(filename);
	stopat = strrchr(guard, '.');

	/*
	 * Convert to a valid C macro name and make it upper case.
	 * Map macro unfriendly characterss to '_'.
	 */
	for (tmp = guard; *tmp != '\000'; ++tmp) {
		if (islower(*tmp))
			*tmp = toupper(*tmp);
		else if (isupper(*tmp) || *tmp == '_')
			/* OK for C */;
		else if (tmp == guard)
			*tmp = '_';
		else if (isdigit(*tmp))
			/* OK for all but first character */;
		else if (tmp == stopat) {
			*tmp = '\0';
			break;
		} else
			*tmp = '_';
	}
	/*
	 * Can't have a '_' in front, because it'll end up being "__".
	 * "__" macros shoudln't be used. So, remove all of the
	 * '_' characters from the front.
	 */
	if (*guard == '_') {
		for (tmp = guard; *tmp == '_'; ++tmp)
			;
		strcpy(guard, tmp);
	}
	tmp = guard;
	guard = extendfile(guard, "_H_RPCGEN");
	free(tmp);
	return (guard);
}

/*
 * Compile into an XDR header file
 */


static void
h_output(const char *infile, const char *define, int extend, const char *outfile, int headeronly)
{
	definition *def;
	const char *outfilename;
	long tell;
	const char *guard;
	list *l;
	xdrfunc *xdrfuncp;
	void *tmp = NULL;

	open_input(infile, define);
	outfilename =  extend ? extendfile(infile, outfile) : outfile;
	open_output(infile, outfilename);
	add_warning();
	if (outfilename || infile){
		guard = tmp = generate_guard(outfilename ? outfilename: infile);
	} else
		guard = "STDIN_";

	f_print(fout, "#ifndef _%s\n#define	_%s\n\n", guard,
		guard);

	f_print(fout, "#include <rpc/rpc.h>\n");

	if (mtflag)
		f_print(fout, "#include <pthread.h>\n");

	/* put the C++ support */
	if (!CCflag) {
		f_print(fout, "\n#ifdef __cplusplus\n");
		f_print(fout, "extern \"C\" {\n");
		f_print(fout, "#endif\n\n");
	}

	/* put in a typedef for quadprecision. Only with Cflag */

	tell = ftell(fout);

	/* print data definitions */
	while ( (def = get_definition()) ) {
		print_datadef(def, headeronly);
	}

	/*
	 * print function declarations.
	 *  Do this after data definitions because they might be used as
	 *  arguments for functions
	 */
	for (l = defined; l != NULL; l = l->next) {
		print_funcdef(l->val, headeronly);
	}
	/* Now  print all xdr func declarations */
	if (xdrfunc_head != NULL){

		f_print(fout,
			"\n/* the xdr functions */\n");

		if (CCflag){
			f_print(fout, "\n#ifdef __cplusplus\n");
			f_print(fout, "extern \"C\" {\n");
			f_print(fout, "#endif\n");
		}

		xdrfuncp = xdrfunc_head;
		while (xdrfuncp != NULL){
			print_xdr_func_def(xdrfuncp->name, xdrfuncp->pointerp);
			xdrfuncp = xdrfuncp->next;
		}
	}

	if (extend && tell == ftell(fout)) {
		(void) unlink(outfilename);
	} else if (tblflag) {
		f_print(fout, rpcgen_table_dcl);
	}

	f_print(fout, "\n#ifdef __cplusplus\n");
	f_print(fout, "}\n");
	f_print(fout, "#endif\n");

	f_print(fout, "\n#endif /* !_%s */\n", guard);
	free(tmp);
}

/*
 * Compile into an RPC service
 */
static void
s_output(int argc, const char *argv[], const char *infile, const char *define,
    int extend, const char *outfile, int nomain, int netflag)
{
	char *include;
	definition *def;
	int foundprogram = 0;
	const char *outfilename;

	open_input(infile, define);
	outfilename = extend ? extendfile(infile, outfile) : outfile;
	open_output(infile, outfilename);
	add_warning();
	if (infile && (include = extendfile(infile, ".h"))) {
		f_print(fout, "#include \"%s\"\n", include);
		free(include);
	} else
		f_print(fout, "#include <rpc/rpc.h>\n");

	f_print(fout, "#include <stdio.h>\n");
	f_print(fout, "#include <stdlib.h> /* getenv, exit */\n");
	f_print (fout, "#include <rpc/pmap_clnt.h> /* for pmap_unset */\n");
	f_print (fout, "#include <string.h> /* strcmp */\n");
	if (tirpcflag)
		f_print(fout, "#include <rpc/rpc_com.h>\n");
	if (strcmp(svcclosetime, "-1") == 0)
		indefinitewait = 1;
	else if (strcmp(svcclosetime, "0") == 0)
		exitnow = 1;
	else if (inetdflag || pmflag) {
		f_print(fout, "#include <signal.h>\n");
		timerflag = 1;
	}

	if (!tirpcflag && inetdflag)
		f_print(fout, "#include <sys/ttycom.h> /* TIOCNOTTY */\n");
	if (inetdflag || pmflag) {
		f_print(fout, "#ifdef __cplusplus\n");
		f_print(fout,
			"#include <sys/sysent.h> /* getdtablesize, open */\n");
		f_print(fout, "#endif /* __cplusplus */\n");
	}
	if (tirpcflag) {
		f_print(fout, "#include <fcntl.h> /* open */\n");
		f_print(fout, "#include <unistd.h> /* fork / setsid */\n");
		f_print(fout, "#include <sys/types.h>\n");
	}

	f_print(fout, "#include <string.h>\n");
	if (inetdflag || !tirpcflag) {
		f_print(fout, "#include <sys/socket.h>\n");
		f_print(fout, "#include <netinet/in.h>\n");
	}

	if ((netflag || pmflag) && tirpcflag && !nomain) {
		f_print(fout, "#include <netconfig.h>\n");
	}
	if (tirpcflag)
		f_print(fout, "#include <sys/resource.h> /* rlimit */\n");
	if (logflag || inetdflag || pmflag || tirpcflag)
		f_print(fout, "#include <syslog.h>\n");

	f_print(fout, "\n#ifdef DEBUG\n#define	RPC_SVC_FG\n#endif\n");
	if (timerflag)
		f_print(fout, "\n#define	_RPCSVC_CLOSEDOWN %s\n",
			svcclosetime);
	while ( (def = get_definition()) ) {
		foundprogram |= (def->def_kind == DEF_PROGRAM);
	}
	if (extend && !foundprogram) {
		(void) unlink(outfilename);
		return;
	}
	write_most(infile, netflag, nomain);
	if (!nomain) {
		if (!do_registers(argc, argv)) {
			if (outfilename)
				(void) unlink(outfilename);
			usage();
		}
		write_rest();
	}
}

/*
 * generate client side stubs
 */
static void
l_output(const char *infile, const char *define, int extend, const char *outfile)
{
	char *include;
	definition *def;
	int foundprogram = 0;
	const char *outfilename;

	open_input(infile, define);
	outfilename = extend ? extendfile(infile, outfile) : outfile;
	open_output(infile, outfilename);
	add_warning();
	f_print (fout, "#include <string.h> /* for memset */\n");
	if (infile && (include = extendfile(infile, ".h"))) {
		f_print(fout, "#include \"%s\"\n", include);
		free(include);
	} else
		f_print(fout, "#include <rpc/rpc.h>\n");
	while ( (def = get_definition()) ) {
		foundprogram |= (def->def_kind == DEF_PROGRAM);
	}
	if (extend && !foundprogram) {
		(void) unlink(outfilename);
		return;
	}
	write_stubs();
}

/*
 * generate the dispatch table
 */
static void
t_output(const char *infile, const char *define, int extend, const char *outfile)
{
	definition *def;
	int foundprogram = 0;
	const char *outfilename;

	open_input(infile, define);
	outfilename = extend ? extendfile(infile, outfile) : outfile;
	open_output(infile, outfilename);
	add_warning();
	while ( (def = get_definition()) ) {
		foundprogram |= (def->def_kind == DEF_PROGRAM);
	}
	if (extend && !foundprogram) {
		(void) unlink(outfilename);
		return;
	}
	write_tables();
}

/* sample routine for the server template */
static void
svc_output(const char *infile, const char *define, int extend, const char *outfile)
{
	definition *def;
	char *include;
	const char *outfilename;
	long tell;
	open_input(infile, define);
	outfilename = extend ? extendfile(infile, outfile) : outfile;
	checkfiles(infile, outfilename);
	/*
	 * Check if outfile already exists.
	 * if so, print an error message and exit
	 */
	open_output(infile, outfilename);
	add_sample_msg();

	if (infile && (include = extendfile(infile, ".h"))) {
		f_print(fout, "#include \"%s\"\n", include);
		free(include);
	} else
		f_print(fout, "#include <rpc/rpc.h>\n");

	tell = ftell(fout);
	while ( (def = get_definition()) ) {
		write_sample_svc(def);
	}
	if (extend && tell == ftell(fout)) {
		(void) unlink(outfilename);
	}
}

/* sample main routine for client */
static void
clnt_output(const char *infile, const char *define, int extend, const char *outfile)
{
	definition *def;
	char *include;
	const char *outfilename;
	long tell;
	int has_program = 0;

	open_input(infile, define);
	outfilename = extend ? extendfile(infile, outfile) : outfile;
	checkfiles(infile, outfilename);
	/*
	 * Check if outfile already exists.
	 * if so, print an error message and exit
	 */

	open_output(infile, outfilename);
	add_sample_msg();
	if (infile && (include = extendfile(infile, ".h"))) {
		f_print(fout, "#include \"%s\"\n", include);
		free(include);
	} else
		f_print(fout, "#include <rpc/rpc.h>\n");
	f_print(fout, "#include <stdio.h>\n");
	f_print(fout, "#include <stdlib.h>\n");
	tell = ftell(fout);
	while ( (def = get_definition()) ) {
		has_program += write_sample_clnt(def);
	}

	if (has_program)
		write_sample_clnt_main();

	if (extend && tell == ftell(fout)) {
		(void) unlink(outfilename);
	}
}


static void mkfile_output(struct commandline *cmd)
{
	const char *mkfilename, *clientname, *clntname, *xdrname, *hdrname;
	const char *servername, *svcname, *servprogname, *clntprogname;
	char *temp, *mkftemp;

	svcname = file_name(cmd->infile, "_svc.c");
	clntname = file_name(cmd->infile, "_clnt.c");
	xdrname = file_name(cmd->infile, "_xdr.c");
	hdrname = file_name(cmd->infile, ".h");


	if (allfiles){
		servername = extendfile(cmd->infile, "_server.c");
		clientname = extendfile(cmd->infile, "_client.c");
	}else{
		servername = " ";
		clientname = " ";
	}
	servprogname = extendfile(cmd->infile, "_server");
	clntprogname = extendfile(cmd->infile, "_client");

	if (allfiles){
		mkftemp = xmalloc(strlen("makefile.") +
		                     strlen(cmd->infile) + 1);
		temp = strrchr(cmd->infile, '.');
		strcpy(mkftemp, "makefile.");
		(void) strncat(mkftemp, cmd->infile,
			(temp - cmd->infile));
		mkfilename = mkftemp;
	} else
		mkfilename = cmd->outfile;


	checkfiles(NULL, mkfilename);
	open_output(NULL, mkfilename);

	f_print(fout, "\n# This is a template makefile generated\
		by rpcgen \n");

	f_print(fout, "\n# Parameters \n\n");

	f_print(fout, "CLIENT = %s\nSERVER = %s\n\n",
		clntprogname, servprogname);
	f_print(fout, "SOURCES_CLNT.c = \nSOURCES_CLNT.h = \n");
	f_print(fout, "SOURCES_SVC.c = \nSOURCES_SVC.h = \n");
	f_print(fout, "SOURCES.x = %s\n\n", cmd->infile);
	f_print(fout, "TARGETS_SVC.c = %s %s %s \n",
		svcname, servername, xdrname);
	f_print(fout, "TARGETS_CLNT.c = %s %s %s \n",
		clntname, clientname, xdrname);
	f_print(fout, "TARGETS = %s %s %s %s %s %s\n\n",
		hdrname, xdrname, clntname,
		svcname, clientname, servername);

	f_print(fout, "OBJECTS_CLNT = $(SOURCES_CLNT.c:%%.c=%%.o) \
$(TARGETS_CLNT.c:%%.c=%%.o) ");

	f_print(fout, "\nOBJECTS_SVC = $(SOURCES_SVC.c:%%.c=%%.o) \
$(TARGETS_SVC.c:%%.c=%%.o) ");


	f_print(fout, "\n# Compiler flags \n");
	if (mtflag)
		f_print(fout, "\nCFLAGS += -D_REENTRANT -D_THEAD_SAFE \nLDLIBS += -pthread\n");

	f_print(fout, "RPCGENFLAGS = \n");

	f_print(fout, "\n# Targets \n\n");

	f_print(fout, "all : $(CLIENT) $(SERVER)\n\n");
	f_print(fout, "$(TARGETS) : $(SOURCES.x) \n");
	f_print(fout, "\trpcgen $(RPCGENFLAGS) $(SOURCES.x)\n\n");
	if (allfiles) {
		f_print(fout, "\trpcgen -Sc $(RPCGENFLAGS) $(SOURCES.x) -o %s\n\n", clientname);
		f_print(fout, "\trpcgen -Ss $(RPCGENFLAGS) $(SOURCES.x) -o %s\n\n", servername);
	}
	f_print(fout, "$(OBJECTS_CLNT) : $(SOURCES_CLNT.c) $(SOURCES_CLNT.h) \
$(TARGETS_CLNT.c) \n\n");

	f_print(fout, "$(OBJECTS_SVC) : $(SOURCES_SVC.c) $(SOURCES_SVC.h) \
$(TARGETS_SVC.c) \n\n");
	f_print(fout, "$(CLIENT) : $(OBJECTS_CLNT) \n");
	f_print(fout, "\t$(CC) -o $(CLIENT) $(OBJECTS_CLNT) \
$(LDLIBS) \n\n");
	f_print(fout, "$(SERVER) : $(OBJECTS_SVC) \n");
	f_print(fout, "\t$(CC) -o $(SERVER) $(OBJECTS_SVC) $(LDLIBS)\n\n");
	f_print(fout, "clean:\n\t rm -f core $(TARGETS) $(OBJECTS_CLNT) \
$(OBJECTS_SVC) $(CLIENT) $(SERVER)\n\n");
}



/*
 * Perform registrations for service output
 * Return 0 if failed; 1 otherwise.
 */
static int
do_registers(int argc, const char *argv[])
{
	int i;

	if (inetdflag || !tirpcflag) {
		for (i = 1; i < argc; i++) {
			if (streq(argv[i], "-s")) {
				if (!check_nettype(argv[i + 1],
						    valid_i_nettypes))
					return (0);
				write_inetd_register(argv[i + 1]);
				i++;
			}
		}
	} else {
		for (i = 1; i < argc; i++)
			if (streq(argv[i], "-s")) {
				if (!check_nettype(argv[i + 1],
						    valid_ti_nettypes))
					return (0);
				write_nettype_register(argv[i + 1]);
				i++;
			} else if (streq(argv[i], "-n")) {
				write_netid_register(argv[i + 1]);
				i++;
			}
	}
	return (1);
}

/*
 * Add another argument to the arg list
 */
static void
addarg(const char *cp)
{
	if (argcount >= ARGLISTLEN) {
		warnx("too many defines");
		crash();
		/*NOTREACHED*/
	}
	if (cp != NULL)
		arglist[argcount++] = xstrdup(cp);
	else
		arglist[argcount++] = NULL;

}

/*
 * Insert an argument at the specified location
 */
static void
insarg(int place, const char *cp)
{
	int i;

	if (argcount >= ARGLISTLEN) {
		warnx("too many defines");
		crash();
		/*NOTREACHED*/
	}

	/* Move up existing arguments */
	for (i = argcount - 1; i >= place; i--)
		arglist[i + 1] = arglist[i];

	arglist[place] = xstrdup(cp);
	argcount++;
}

/*
 * if input file is stdin and an output file is specified then complain
 * if the file already exists. Otherwise the file may get overwritten
 * If input file does not exist, exit with an error
 */

static void
checkfiles(const char *infile, const char *outfile)
{

	struct stat buf;

	if (infile)		/* infile ! = NULL */
		if (stat(infile, &buf) < 0)
		{
			warn("%s", infile);
			crash();
		}
	if (outfile) {
		if (stat(outfile, &buf) < 0)
			return;	/* file does not exist */
		else {
			warnx("file '%s' already exists and may be overwritten", outfile);
			crash();
		}
	}
}

/*
 * Parse command line arguments
 */
static int
parseargs(int argc, const char *argv[], struct commandline *cmd)
{
	int i;
	int j;
	char c, ch;
	char flag[(1 << 8 * sizeof (char))];
	int nflags;

	cmd->infile = cmd->outfile = NULL;
	if (argc < 2) {
		return (0);
	}
	allfiles = 0;
	flag['c'] = 0;
	flag['h'] = 0;
	flag['l'] = 0;
	flag['m'] = 0;
	flag['o'] = 0;
	flag['s'] = 0;
	flag['n'] = 0;
	flag['t'] = 0;
	flag['S'] = 0;
	flag['C'] = 0;
	flag['M'] = 0;

	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-') {
			if (cmd->infile) {
				warnx("cannot specify more than one input file");
				return (0);
			}
			cmd->infile = argv[i];
		} else {
			for (j = 1; argv[i][j] != 0; j++) {
				c = argv[i][j];
				switch (c) {
				case 'a':
					allfiles = 1;
					break;
				case 'c':
				case 'h':
				case 'l':
				case 'm':
				case 't':
					if (flag[(int)c]) {
						return (0);
					}
					flag[(int)c] = 1;
					break;
				case 'S':
					/*
					 * sample flag: Ss or Sc.
					 *  Ss means set flag['S'];
					 *  Sc means set flag['C'];
					 *  Sm means set flag['M'];
					 */
					ch = argv[i][++j]; /* get next char */
					if (ch == 's')
						ch = 'S';
					else if (ch == 'c')
						ch = 'C';
					else if (ch == 'm')
						ch = 'M';
					else
						return (0);

					if (flag[(int)ch]) {
						return (0);
					}
					flag[(int)ch] = 1;
					break;
				case 'C': /* ANSI C syntax */
					ch = argv[i][j+1]; /* get next char */

					if (ch != 'C')
						break;
					CCflag = 1;
					break;
				case 'b':
					/*
					 *  Turn TIRPC flag off for
					 *  generating backward compatible
					 *  code
					 */
					tirpcflag = 0;
					break;

				case 'I':
					inetdflag = 1;
					break;
				case 'N':
					newstyle = 1;
					break;
				case 'L':
					logflag = 1;
					break;
				case 'P':
					pmflag = 1;
					break;
				case 'K':
					if (++i == argc) {
						return (0);
					}
					svcclosetime = argv[i];
					goto nextarg;
				case 'T':
					tblflag = 1;
					break;
				case 'M':
					mtflag = 1;
					break;
				case 'i' :
					if (++i == argc) {
						return (0);
					}
					inline_size = atoi(argv[i]);
					goto nextarg;
				case 'n':
				case 'o':
				case 's':
					if (argv[i][j - 1] != '-' ||
					    argv[i][j + 1] != 0) {
						return (0);
					}
					flag[(int)c] = 1;
					if (++i == argc) {
						return (0);
					}
					if (c == 'o') {
						if (cmd->outfile) {
							return (0);
						}
						cmd->outfile = argv[i];
					}
					goto nextarg;
				case 'D':
					if (argv[i][j - 1] != '-') {
						return (0);
					}
					(void) addarg(argv[i]);
					goto nextarg;
				case 'Y':
					if (++i == argc) {
						return (0);
					}
					if (strlcpy(pathbuf, argv[i],
					    sizeof(pathbuf)) >= sizeof(pathbuf)
					    || strlcat(pathbuf, "/cpp",
					    sizeof(pathbuf)) >=
					    sizeof(pathbuf)) {
						warnx("argument too long");
						return (0);
					}
					CPP = pathbuf;
					goto nextarg;



				default:
					return (0);
				}
			}
		nextarg:
			;
		}
	}

	cmd->cflag = flag['c'];
	cmd->hflag = flag['h'];
	cmd->lflag = flag['l'];
	cmd->mflag = flag['m'];
	cmd->nflag = flag['n'];
	cmd->sflag = flag['s'];
	cmd->tflag = flag['t'];
	cmd->Ssflag = flag['S'];
	cmd->Scflag = flag['C'];
	cmd->makefileflag = flag['M'];

	if (tirpcflag) {
		if (inetdflag)
			pmflag = 0;
		if ((inetdflag && cmd->nflag)) {
			/* netid not allowed with inetdflag */
			warnx("cannot use netid flag with inetd flag");
			return (0);
		}
	} else {		/* 4.1 mode */
		pmflag = 0;	/* set pmflag only in tirpcmode */
		if (cmd->nflag) { /* netid needs TIRPC */
			warnx("cannot use netid flag without TIRPC");
			return (0);
		}
	}

	if (newstyle && (tblflag || cmd->tflag)) {
		warnx("cannot use table flags with newstyle");
		return (0);
	}

	/* check no conflicts with file generation flags */
	nflags = cmd->cflag + cmd->hflag + cmd->lflag + cmd->mflag +
		cmd->sflag + cmd->nflag + cmd->tflag + cmd->Ssflag +
			cmd->Scflag + cmd->makefileflag;

	if (nflags == 0) {
		if (cmd->outfile != NULL || cmd->infile == NULL) {
			return (0);
		}
	} else if (cmd->infile == NULL &&
	    (cmd->Ssflag || cmd->Scflag || cmd->makefileflag)) {
		warnx("\"infile\" is required for template generation flags");
		return (0);
	} if (nflags > 1) {
		warnx("cannot have more than one file generation flag");
		return (0);
	}
	return (1);
}

static void
usage(void)
{
	f_print(stderr, "%s\n%s\n%s\n%s\n%s\n",
		"usage: rpcgen infile",
		"       rpcgen [-abCLNTM] [-Dname[=value]] [-i size]\
[-I -P [-K seconds]] [-Y path] infile",
		"       rpcgen [-c | -h | -l | -m | -t | -Sc | -Ss | -Sm]\
[-o outfile] [infile]",
		"       rpcgen [-s nettype]* [-o outfile] [infile]",
		"       rpcgen [-n netid]* [-o outfile] [infile]");
	options_usage();
	exit(1);
}

static void
options_usage(void)
{
	f_print(stderr, "options:\n");
	f_print(stderr, "-a\t\tgenerate all files, including samples\n");
	f_print(stderr, "-b\t\tbackward compatibility mode (generates code \
for FreeBSD 4.X)\n");
	f_print(stderr, "-c\t\tgenerate XDR routines\n");
	f_print(stderr, "-C\t\tANSI C mode\n");
	f_print(stderr, "-Dname[=value]\tdefine a symbol (same as #define)\n");
	f_print(stderr, "-h\t\tgenerate header file\n");
	f_print(stderr, "-i size\t\tsize at which to start generating\
inline code\n");
	f_print(stderr, "-I\t\tgenerate code for inetd support in server\n");
	f_print(stderr, "-K seconds\tserver exits after K seconds of\
inactivity\n");
	f_print(stderr, "-l\t\tgenerate client side stubs\n");
	f_print(stderr, "-L\t\tserver errors will be printed to syslog\n");
	f_print(stderr, "-m\t\tgenerate server side stubs\n");
	f_print(stderr, "-M\t\tgenerate MT-safe code\n");
	f_print(stderr, "-n netid\tgenerate server code that supports\
named netid\n");
	f_print(stderr, "-N\t\tsupports multiple arguments and\
call-by-value\n");
	f_print(stderr, "-o outfile\tname of the output file\n");
	f_print(stderr, "-P\t\tgenerate code for port monitoring support in server\n");
	f_print(stderr, "-s nettype\tgenerate server code that supports named\
nettype\n");
	f_print(stderr, "-Sc\t\tgenerate sample client code that uses remote\
procedures\n");
	f_print(stderr, "-Ss\t\tgenerate sample server code that defines\
remote procedures\n");
	f_print(stderr, "-Sm \t\tgenerate makefile template \n");

	f_print(stderr, "-t\t\tgenerate RPC dispatch table\n");
	f_print(stderr, "-T\t\tgenerate code to support RPC dispatch tables\n");
	f_print(stderr, "-Y path\t\tpath where cpp is found\n");
	exit(1);
}
