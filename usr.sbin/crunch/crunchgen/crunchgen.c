/*
 * Copyright (c) 1994 University of Maryland
 * All Rights Reserved.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of U.M. not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  U.M. makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * U.M. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL U.M.
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: James da Silva, Systems Design and Analysis Group
 *			   Computer Science Department
 *			   University of Maryland at College Park
 */
/*
 * ========================================================================
 * crunchgen.c
 *
 * Generates a Makefile and main C file for a crunched executable,
 * from specs given in a .conf file.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CRUNCH_VERSION	"0.2"

#define MAXLINELEN	16384
#define MAXFIELDS 	 2048


/* internal representation of conf file: */

/* simple lists of strings suffice for most parms */

typedef struct strlst {
	struct strlst *next;
	char *str;
} strlst_t;

/* progs have structure, each field can be set with "special" or calculated */

typedef struct prog {
	struct prog *next;	/* link field */
	char *name;		/* program name */
	char *ident;		/* C identifier for the program name */
	char *srcdir;
	char *realsrcdir;
	char *objdir;
	char *objvar;		/* Makefile variable to replace OBJS */
	strlst_t *objs, *objpaths;
	strlst_t *buildopts;
	strlst_t *keeplist;
	strlst_t *links;
	strlst_t *libs;
	strlst_t *libs_so;
	int goterror;
} prog_t;


/* global state */

strlst_t *buildopts = NULL;
strlst_t *srcdirs   = NULL;
strlst_t *libs      = NULL;
strlst_t *libs_so   = NULL;
prog_t   *progs     = NULL;

char confname[MAXPATHLEN], infilename[MAXPATHLEN];
char outmkname[MAXPATHLEN], outcfname[MAXPATHLEN], execfname[MAXPATHLEN];
char tempfname[MAXPATHLEN], cachename[MAXPATHLEN], curfilename[MAXPATHLEN];
char outhdrname[MAXPATHLEN] ;	/* user-supplied header for *.mk */
char *objprefix;		/* where are the objects ? */
char *path_make;
int linenum = -1;
int goterror = 0;

int verbose, readcache;		/* options */
int reading_cache;
int makeobj = 0;		/* add 'make obj' rules to the makefile */

int list_mode;

/* general library routines */

void status(const char *str);
void out_of_memory(void);
void add_string(strlst_t **listp, char *str);
int is_dir(const char *pathname);
int is_nonempty_file(const char *pathname);
int subtract_strlst(strlst_t **lista, strlst_t **listb);
int in_list(strlst_t **listp, char *str);

/* helper routines for main() */

void usage(void);
void parse_conf_file(void);
void gen_outputs(void);

extern char *crunched_skel[];


int
main(int argc, char **argv)
{
	char *p;
	int optc;

	verbose = 1;
	readcache = 1;
	*outmkname = *outcfname = *execfname = '\0';

	path_make = getenv("MAKE");
	if (path_make == NULL || *path_make == '\0')
		path_make = "make";

	p = getenv("MAKEOBJDIRPREFIX");
	if (p == NULL || *p == '\0')
		objprefix = "/usr/obj"; /* default */
	else
		if ((objprefix = strdup(p)) == NULL)
			out_of_memory();

	while((optc = getopt(argc, argv, "lh:m:c:e:p:foq")) != -1) {
		switch(optc) {
		case 'f':
			readcache = 0;
			break;
		case 'o':
			makeobj = 1;
			break;
		case 'q':
			verbose = 0;
			break;

		case 'm':
			strlcpy(outmkname, optarg, sizeof(outmkname));
			break;
		case 'p':
			if ((objprefix = strdup(optarg)) == NULL)
				out_of_memory();
			break;

		case 'h':
			strlcpy(outhdrname, optarg, sizeof(outhdrname));
			break;
		case 'c':
			strlcpy(outcfname, optarg, sizeof(outcfname));
			break;
		case 'e':
			strlcpy(execfname, optarg, sizeof(execfname));
			break;

		case 'l':
			list_mode++;
			verbose = 0;
			break;

		case '?':
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	/*
	 * generate filenames
	 */

	strlcpy(infilename, argv[0], sizeof(infilename));

	/* confname = `basename infilename .conf` */

	if ((p=strrchr(infilename, '/')) != NULL)
		strlcpy(confname, p + 1, sizeof(confname));
	else
		strlcpy(confname, infilename, sizeof(confname));

	if ((p=strrchr(confname, '.')) != NULL && !strcmp(p, ".conf"))
		*p = '\0';

	if (!*outmkname)
		snprintf(outmkname, sizeof(outmkname), "%s.mk", confname);
	if (!*outcfname)
		snprintf(outcfname, sizeof(outcfname), "%s.c", confname);
	if (!*execfname)
		snprintf(execfname, sizeof(execfname), "%s", confname);

	snprintf(cachename, sizeof(cachename), "%s.cache", confname);
	snprintf(tempfname, sizeof(tempfname), "%s/crunchgen_%sXXXXXX",
	getenv("TMPDIR") ? getenv("TMPDIR") : _PATH_TMP, confname);

	parse_conf_file();
	if (list_mode)
		exit(goterror);

	gen_outputs();

	exit(goterror);
}


void
usage(void)
{
	fprintf(stderr, "%s%s\n\t%s%s\n", "usage: crunchgen [-foq] ",
	    "[-h <makefile-header-name>] [-m <makefile>]",
	    "[-p <obj-prefix>] [-c <c-file-name>] [-e <exec-file>] ",
	    "<conffile>");
	exit(1);
}


/*
 * ========================================================================
 * parse_conf_file subsystem
 *
 */

/* helper routines for parse_conf_file */

void parse_one_file(char *filename);
void parse_line(char *pline, int *fc, char **fv, int nf);
void add_srcdirs(int argc, char **argv);
void add_progs(int argc, char **argv);
void add_link(int argc, char **argv);
void add_libs(int argc, char **argv);
void add_libs_so(int argc, char **argv);
void add_buildopts(int argc, char **argv);
void add_special(int argc, char **argv);

prog_t *find_prog(char *str);
void add_prog(char *progname);


void
parse_conf_file(void)
{
	if (!is_nonempty_file(infilename))
		errx(1, "fatal: input file \"%s\" not found", infilename);

	parse_one_file(infilename);
	if (readcache && is_nonempty_file(cachename)) {
		reading_cache = 1;
		parse_one_file(cachename);
	}
}


void
parse_one_file(char *filename)
{
	char *fieldv[MAXFIELDS];
	int fieldc;
	void (*f)(int c, char **v);
	FILE *cf;
	char line[MAXLINELEN];

	snprintf(line, sizeof(line), "reading %s", filename);
	status(line);
	strlcpy(curfilename, filename, sizeof(curfilename));

	if ((cf = fopen(curfilename, "r")) == NULL) {
		warn("%s", curfilename);
		goterror = 1;
		return;
	}

	linenum = 0;
	while (fgets(line, MAXLINELEN, cf) != NULL) {
		linenum++;
		parse_line(line, &fieldc, fieldv, MAXFIELDS);

		if (fieldc < 1)
			continue;

		if (!strcmp(fieldv[0], "srcdirs"))
			f = add_srcdirs;
		else if(!strcmp(fieldv[0], "progs"))
			f = add_progs;
		else if(!strcmp(fieldv[0], "ln"))
			f = add_link;
		else if(!strcmp(fieldv[0], "libs"))
			f = add_libs;
		else if(!strcmp(fieldv[0], "libs_so"))
			f = add_libs_so;
		else if(!strcmp(fieldv[0], "buildopts"))
			f = add_buildopts;
		else if(!strcmp(fieldv[0], "special"))
			f = add_special;
		else {
			warnx("%s:%d: skipping unknown command `%s'",
			    curfilename, linenum, fieldv[0]);
			goterror = 1;
			continue;
		}

		if (fieldc < 2) {
			warnx("%s:%d: %s %s",
			    curfilename, linenum, fieldv[0],
			    "command needs at least 1 argument, skipping");
			goterror = 1;
			continue;
		}

		f(fieldc, fieldv);
	}

	if (ferror(cf)) {
		warn("%s", curfilename);
		goterror = 1;
	}
	fclose(cf);
}


void
parse_line(char *pline, int *fc, char **fv, int nf)
{
	char *p;

	p = pline;
	*fc = 0;

	while (1) {
		while (isspace((unsigned char)*p))
			p++;

		if (*p == '\0' || *p == '#')
			break;

		if (*fc < nf)
			fv[(*fc)++] = p;

		while (*p && !isspace((unsigned char)*p) && *p != '#')
			p++;

		if (*p == '\0' || *p == '#')
			break;

		*p++ = '\0';
	}

	if (*p)
		*p = '\0';		/* needed for '#' case */
}


void
add_srcdirs(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		if (is_dir(argv[i]))
			add_string(&srcdirs, argv[i]);
		else {
			warnx("%s:%d: `%s' is not a directory, skipping it",
			    curfilename, linenum, argv[i]);
			goterror = 1;
		}
	}
}


void
add_progs(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++)
		add_prog(argv[i]);
}


void
add_prog(char *progname)
{
	prog_t *p1, *p2;

	/* add to end, but be smart about dups */

	for (p1 = NULL, p2 = progs; p2 != NULL; p1 = p2, p2 = p2->next)
		if (!strcmp(p2->name, progname))
			return;

	p2 = malloc(sizeof(prog_t));
	if(p2) {
		memset(p2, 0, sizeof(prog_t));
		p2->name = strdup(progname);
	}
	if (!p2 || !p2->name)
		out_of_memory();

	p2->next = NULL;
	if (p1 == NULL)
		progs = p2;
	else
		p1->next = p2;

	p2->ident = NULL;
	p2->srcdir = NULL;
	p2->realsrcdir = NULL;
	p2->objdir = NULL;
	p2->links = NULL;
	p2->libs = NULL;
	p2->libs_so = NULL;
	p2->objs = NULL;
	p2->keeplist = NULL;
	p2->buildopts = NULL;
	p2->goterror = 0;

	if (list_mode)
		printf("%s\n",progname);
}


void
add_link(int argc, char **argv)
{
	int i;
	prog_t *p = find_prog(argv[1]);

	if (p == NULL) {
		warnx("%s:%d: no prog %s previously declared, skipping link",
		    curfilename, linenum, argv[1]);
		goterror = 1;
		return;
	}

	for (i = 2; i < argc; i++) {
		if (list_mode)
			printf("%s\n",argv[i]);

		add_string(&p->links, argv[i]);
	}
}


void
add_libs(int argc, char **argv)
{
	int i;

	for(i = 1; i < argc; i++) {
		add_string(&libs, argv[i]);
		if ( in_list(&libs_so, argv[i]) )
			warnx("%s:%d: "
				"library `%s' specified as dynamic earlier",
				curfilename, linenum, argv[i]);
	}
}


void
add_libs_so(int argc, char **argv)
{
	int i;

	for(i = 1; i < argc; i++) {
		add_string(&libs_so, argv[i]);
		if ( in_list(&libs, argv[i]) )
			warnx("%s:%d: "
				"library `%s' specified as static earlier",
				curfilename, linenum, argv[i]);
	}
}


void
add_buildopts(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++)
		add_string(&buildopts, argv[i]);
}


void
add_special(int argc, char **argv)
{
	int i;
	prog_t *p = find_prog(argv[1]);

	if (p == NULL) {
		if (reading_cache)
			return;

		warnx("%s:%d: no prog %s previously declared, skipping special",
		    curfilename, linenum, argv[1]);
		goterror = 1;
		return;
	}

	if (!strcmp(argv[2], "ident")) {
		if (argc != 4)
			goto argcount;
		if ((p->ident = strdup(argv[3])) == NULL)
			out_of_memory();
	} else if (!strcmp(argv[2], "srcdir")) {
		if (argc != 4)
			goto argcount;
		if ((p->srcdir = strdup(argv[3])) == NULL)
			out_of_memory();
	} else if (!strcmp(argv[2], "objdir")) {
		if(argc != 4)
			goto argcount;
		if((p->objdir = strdup(argv[3])) == NULL)
			out_of_memory();
	} else if (!strcmp(argv[2], "objs")) {
		p->objs = NULL;
		for (i = 3; i < argc; i++)
			add_string(&p->objs, argv[i]);
	} else if (!strcmp(argv[2], "objpaths")) {
		p->objpaths = NULL;
		for (i = 3; i < argc; i++)
			add_string(&p->objpaths, argv[i]);
	} else if (!strcmp(argv[2], "keep")) {
		p->keeplist = NULL;
		for(i = 3; i < argc; i++)
			add_string(&p->keeplist, argv[i]);
	} else if (!strcmp(argv[2], "objvar")) {
		if(argc != 4)
			goto argcount;
		if ((p->objvar = strdup(argv[3])) == NULL)
			out_of_memory();
	} else if (!strcmp(argv[2], "buildopts")) {
		p->buildopts = NULL;
		for (i = 3; i < argc; i++)
			add_string(&p->buildopts, argv[i]);
	} else if (!strcmp(argv[2], "lib")) {
		for (i = 3; i < argc; i++)
			add_string(&p->libs, argv[i]);
	} else {
		warnx("%s:%d: bad parameter name `%s', skipping line",
		    curfilename, linenum, argv[2]);
		goterror = 1;
	}
	return;

 argcount:
	warnx("%s:%d: too %s arguments, expected \"special %s %s <string>\"",
	    curfilename, linenum, argc < 4? "few" : "many", argv[1], argv[2]);
	goterror = 1;
}


prog_t *find_prog(char *str)
{
	prog_t *p;

	for (p = progs; p != NULL; p = p->next)
		if (!strcmp(p->name, str))
			return p;

	return NULL;
}


/*
 * ========================================================================
 * gen_outputs subsystem
 *
 */

/* helper subroutines */

void remove_error_progs(void);
void fillin_program(prog_t *p);
void gen_specials_cache(void);
void gen_output_makefile(void);
void gen_output_cfile(void);

void fillin_program_objs(prog_t *p, char *path);
void top_makefile_rules(FILE *outmk);
void prog_makefile_rules(FILE *outmk, prog_t *p);
void output_strlst(FILE *outf, strlst_t *lst);
char *genident(char *str);
char *dir_search(char *progname);


void
gen_outputs(void)
{
	prog_t *p;

	for (p = progs; p != NULL; p = p->next)
		fillin_program(p);

	remove_error_progs();
	gen_specials_cache();
	gen_output_cfile();
	gen_output_makefile();
	status("");
	fprintf(stderr,
	    "Run \"%s -f %s\" to build crunched binary.\n",
	    path_make, outmkname);
}

/*
 * run the makefile for the program to find which objects are necessary
 */
void
fillin_program(prog_t *p)
{
	char path[MAXPATHLEN];
	char line[MAXLINELEN];
	FILE *f;

	snprintf(line, MAXLINELEN, "filling in parms for %s", p->name);
	status(line);

	if (!p->ident)
		p->ident = genident(p->name);

	/* look for the source directory if one wasn't specified by a special */
	if (!p->srcdir) {
		p->srcdir = dir_search(p->name);
	}

	/* Determine the actual srcdir (maybe symlinked). */
	if (p->srcdir) {
		snprintf(line, MAXLINELEN, "cd %s && echo -n `/bin/pwd`",
		    p->srcdir);
		f = popen(line,"r");
		if (!f)
			errx(1, "Can't execute: %s\n", line);

		path[0] = '\0';
		fgets(path, sizeof path, f);
		if (pclose(f))
			errx(1, "Can't execute: %s\n", line);

		if (!*path)
			errx(1, "Can't perform pwd on: %s\n", p->srcdir);

		p->realsrcdir = strdup(path);
	}

	/* Unless the option to make object files was specified the
	* the objects will be built in the source directory unless
	* an object directory already exists.
	*/
	if (!makeobj && !p->objdir && p->srcdir) {
		char *auto_obj;

		auto_obj = NULL;
		snprintf(line, sizeof line, "%s/%s", objprefix, p->realsrcdir);
		if (is_dir(line) ||
		    ((auto_obj = getenv("MK_AUTO_OBJ")) != NULL &&
		    strcmp(auto_obj, "yes") == 0)) {
			if ((p->objdir = strdup(line)) == NULL)
			out_of_memory();
		} else
			p->objdir = p->realsrcdir;
	}

	/*
	* XXX look for a Makefile.{name} in local directory first.
	* This lets us override the original Makefile.
	*/
	snprintf(path, sizeof(path), "Makefile.%s", p->name);
	if (is_nonempty_file(path)) {
		snprintf(line, MAXLINELEN, "Using %s for %s", path, p->name);
		status(line);
	} else
		if (p->srcdir)
			snprintf(path, sizeof(path), "%s/Makefile", p->srcdir);
	if (!p->objs && p->srcdir && is_nonempty_file(path))
		fillin_program_objs(p, path);

	if (!p->srcdir && !p->objdir && verbose)
		warnx("%s: %s: %s",
		    "warning: could not find source directory",
		    infilename, p->name);
	if (!p->objs && verbose)
		warnx("%s: %s: warning: could not find any .o files",
		    infilename, p->name);

	if ((!p->srcdir || !p->objdir) && !p->objs)
		p->goterror = 1;
}

void
fillin_program_objs(prog_t *p, char *path)
{
	char *obj, *cp;
	int fd, rc;
	FILE *f;
	char *objvar="OBJS";
	strlst_t *s;
	char line[MAXLINELEN];

	/* discover the objs from the srcdir Makefile */

	if ((fd = mkstemp(tempfname)) == -1) {
		perror(tempfname);
		exit(1);
	}
	if ((f = fdopen(fd, "w")) == NULL) {
		warn("%s", tempfname);
		goterror = 1;
		return;
	}
	if (p->objvar)
		objvar = p->objvar;

	/*
	* XXX include outhdrname (e.g. to contain Make variables)
	*/
	if (outhdrname[0] != '\0')
		fprintf(f, ".include \"%s\"\n", outhdrname);
	fprintf(f, ".include \"%s\"\n", path);
	fprintf(f, ".POSIX:\n");
	if (buildopts) {
		fprintf(f, "BUILDOPTS+=");
		output_strlst(f, buildopts);
	}
	fprintf(f, ".if defined(PROG)\n");
	fprintf(f, "%s?=${PROG}.o\n", objvar);
	fprintf(f, ".endif\n");
	fprintf(f, "loop:\n\t@echo 'OBJS= '${%s}\n", objvar);

	fprintf(f, "crunchgen_objs:\n"
	    "\t@cd %s && %s -f %s $(BUILDOPTS) $(%s_OPTS)",
	    p->srcdir, path_make, tempfname, p->ident);
	for (s = p->buildopts; s != NULL; s = s->next)
		fprintf(f, " %s", s->str);
	fprintf(f, " loop\n");

	fclose(f);

	snprintf(line, MAXLINELEN, "cd %s && %s -f %s -B crunchgen_objs",
	     p->srcdir, path_make, tempfname);
	if ((f = popen(line, "r")) == NULL) {
		warn("submake pipe");
		goterror = 1;
		return;
	}

	while(fgets(line, MAXLINELEN, f)) {
		if (strncmp(line, "OBJS= ", 6)) {
			warnx("make error: %s", line);
			goterror = 1;
			continue;
		}

		cp = line + 6;
		while (isspace((unsigned char)*cp))
			cp++;

		while(*cp) {
			obj = cp;
			while (*cp && !isspace((unsigned char)*cp))
				cp++;
			if (*cp)
				*cp++ = '\0';
			add_string(&p->objs, obj);
			while (isspace((unsigned char)*cp))
				cp++;
		}
	}

	if ((rc=pclose(f)) != 0) {
		warnx("make error: make returned %d", rc);
		goterror = 1;
	}

	unlink(tempfname);
}

void
remove_error_progs(void)
{
	prog_t *p1, *p2;

	p1 = NULL; p2 = progs;
	while (p2 != NULL) {
		if (!p2->goterror)
			p1 = p2, p2 = p2->next;
		else {
			/* delete it from linked list */
			warnx("%s: %s: ignoring program because of errors",
			    infilename, p2->name);
			if (p1)
				p1->next = p2->next;
			else
				progs = p2->next;
			p2 = p2->next;
		}
	}
}

void
gen_specials_cache(void)
{
	FILE *cachef;
	prog_t *p;
	char line[MAXLINELEN];

	snprintf(line, MAXLINELEN, "generating %s", cachename);
	status(line);

	if ((cachef = fopen(cachename, "w")) == NULL) {
		warn("%s", cachename);
		goterror = 1;
		return;
	}

	fprintf(cachef, "# %s - parm cache generated from %s by crunchgen "
	    " %s\n\n",
	    cachename, infilename, CRUNCH_VERSION);

	for (p = progs; p != NULL; p = p->next) {
		fprintf(cachef, "\n");
		if (p->srcdir)
			fprintf(cachef, "special %s srcdir %s\n",
			    p->name, p->srcdir);
		if (p->objdir)
			fprintf(cachef, "special %s objdir %s\n",
			    p->name, p->objdir);
		if (p->objs) {
			fprintf(cachef, "special %s objs", p->name);
			output_strlst(cachef, p->objs);
		}
		if (p->objpaths) {
			fprintf(cachef, "special %s objpaths", p->name);
			output_strlst(cachef, p->objpaths);
		}
	}
	fclose(cachef);
}


void
gen_output_makefile(void)
{
	prog_t *p;
	FILE *outmk;
	char line[MAXLINELEN];

	snprintf(line, MAXLINELEN, "generating %s", outmkname);
	status(line);

	if ((outmk = fopen(outmkname, "w")) == NULL) {
		warn("%s", outmkname);
		goterror = 1;
		return;
	}

	fprintf(outmk, "# %s - generated from %s by crunchgen %s\n\n",
	    outmkname, infilename, CRUNCH_VERSION);

	if (outhdrname[0] != '\0')
		fprintf(outmk, ".include \"%s\"\n", outhdrname);

	top_makefile_rules(outmk);
	for (p = progs; p != NULL; p = p->next)
		prog_makefile_rules(outmk, p);

	fprintf(outmk, "\n# ========\n");
	fclose(outmk);
}


void
gen_output_cfile(void)
{
	char **cp;
	FILE *outcf;
	prog_t *p;
	strlst_t *s;
	char line[MAXLINELEN];

	snprintf(line, MAXLINELEN, "generating %s", outcfname);
	status(line);

	if((outcf = fopen(outcfname, "w")) == NULL) {
		warn("%s", outcfname);
		goterror = 1;
		return;
	}

	fprintf(outcf,
	    "/* %s - generated from %s by crunchgen %s */\n",
	    outcfname, infilename, CRUNCH_VERSION);

	fprintf(outcf, "#define EXECNAME \"%s\"\n", execfname);
	for (cp = crunched_skel; *cp != NULL; cp++)
		fprintf(outcf, "%s\n", *cp);

	for (p = progs; p != NULL; p = p->next)
		fprintf(outcf, "extern int _crunched_%s_stub();\n", p->ident);

	fprintf(outcf, "\nstruct stub entry_points[] = {\n");
	for (p = progs; p != NULL; p = p->next) {
		fprintf(outcf, "\t{ \"%s\", _crunched_%s_stub },\n",
		    p->name, p->ident);
		for (s = p->links; s != NULL; s = s->next)
			fprintf(outcf, "\t{ \"%s\", _crunched_%s_stub },\n",
			    s->str, p->ident);
	}

	fprintf(outcf, "\t{ EXECNAME, crunched_main },\n");
	fprintf(outcf, "\t{ NULL, NULL }\n};\n");
	fclose(outcf);
}


char *genident(char *str)
{
	char *n, *s, *d;

	/*
	 * generates a Makefile/C identifier from a program name,
	 * mapping '-' to '_' and ignoring all other non-identifier
	 * characters.  This leads to programs named "foo.bar" and
	 * "foobar" to map to the same identifier.
	 */

	if ((n = strdup(str)) == NULL)
		return NULL;
	for (d = s = n; *s != '\0'; s++) {
		if (*s == '-')
			*d++ = '_';
		else if (*s == '_' || isalnum((unsigned char)*s))
			*d++ = *s;
	}
	*d = '\0';
	return n;
}


char *dir_search(char *progname)
{
	char path[MAXPATHLEN];
	strlst_t *dir;
	char *srcdir;

	for (dir = srcdirs; dir != NULL; dir = dir->next) {
		snprintf(path, MAXPATHLEN, "%s/%s", dir->str, progname);
		if (!is_dir(path))
			continue;

		if ((srcdir = strdup(path)) == NULL)
			out_of_memory();

		return srcdir;
	}
	return NULL;
}


void
top_makefile_rules(FILE *outmk)
{
	prog_t *p;

	fprintf(outmk, "LD?= ld\n");
	if ( subtract_strlst(&libs, &libs_so) )
		fprintf(outmk, "# NOTE: Some LIBS declarations below overridden by LIBS_SO\n");

	fprintf(outmk, "LIBS+=");
	output_strlst(outmk, libs);

	fprintf(outmk, "LIBS_SO+=");
	output_strlst(outmk, libs_so);

	if (makeobj) {
		fprintf(outmk, "MAKEOBJDIRPREFIX?=%s\n", objprefix);
		fprintf(outmk, "MAKEENV=env MAKEOBJDIRPREFIX=$(MAKEOBJDIRPREFIX)\n");
		fprintf(outmk, "CRUNCHMAKE=$(MAKEENV) $(MAKE)\n");
	} else {
		fprintf(outmk, "CRUNCHMAKE=$(MAKE)\n");
	}

	if (buildopts) {
		fprintf(outmk, "BUILDOPTS+=");
		output_strlst(outmk, buildopts);
	}

	fprintf(outmk, "CRUNCHED_OBJS=");
	for (p = progs; p != NULL; p = p->next)
		fprintf(outmk, " %s.lo", p->name);
	fprintf(outmk, "\n");

	fprintf(outmk, "SUBMAKE_TARGETS=");
	for (p = progs; p != NULL; p = p->next)
		fprintf(outmk, " %s_make", p->ident);
	fprintf(outmk, "\nSUBCLEAN_TARGETS=");
	for (p = progs; p != NULL; p = p->next)
		fprintf(outmk, " %s_clean", p->ident);
	fprintf(outmk, "\n\n");

	fprintf(outmk, "all: objs exe\nobjs: $(SUBMAKE_TARGETS)\n");
	fprintf(outmk, "exe: %s\n", execfname);
	fprintf(outmk, "%s: %s.o $(CRUNCHED_OBJS) $(SUBMAKE_TARGETS)\n", execfname, execfname);
	fprintf(outmk, ".if defined(LIBS_SO) && !empty(LIBS_SO)\n");
	fprintf(outmk, "\t$(CC) -o %s %s.o $(CRUNCHED_OBJS) \\\n",
	    execfname, execfname);
	fprintf(outmk, "\t\t-Xlinker -Bstatic $(LIBS) \\\n");
	fprintf(outmk, "\t\t-Xlinker -Bdynamic $(LIBS_SO)\n");
	fprintf(outmk, ".else\n");
	fprintf(outmk, "\t$(CC) -static -o %s %s.o $(CRUNCHED_OBJS) $(LIBS)\n",
	    execfname, execfname);
	fprintf(outmk, ".endif\n");
	fprintf(outmk, "realclean: clean subclean\n");
	fprintf(outmk, "clean:\n\trm -f %s *.lo *.o *_stub.c\n", execfname);
	fprintf(outmk, "subclean: $(SUBCLEAN_TARGETS)\n");
}


void
prog_makefile_rules(FILE *outmk, prog_t *p)
{
	strlst_t *lst;

	fprintf(outmk, "\n# -------- %s\n\n", p->name);

	fprintf(outmk, "%s_OBJDIR=", p->ident);
	if (p->objdir)
		fprintf(outmk, "%s", p->objdir);
	else
		fprintf(outmk, "$(MAKEOBJDIRPREFIX)/$(%s_REALSRCDIR)\n",
		    p->ident);
	fprintf(outmk, "\n");

	fprintf(outmk, "%s_OBJPATHS=", p->ident);
	if (p->objpaths)
		output_strlst(outmk, p->objpaths);
	else {
		for (lst = p->objs; lst != NULL; lst = lst->next) {
			fprintf(outmk, " $(%s_OBJDIR)/%s", p->ident, lst->str);
		}
		fprintf(outmk, "\n");
	}
	fprintf(outmk, "$(%s_OBJPATHS): .NOMETA\n", p->ident);

	if (p->srcdir && p->objs) {
		fprintf(outmk, "%s_SRCDIR=%s\n", p->ident, p->srcdir);
		fprintf(outmk, "%s_REALSRCDIR=%s\n", p->ident, p->realsrcdir);

		fprintf(outmk, "%s_OBJS=", p->ident);
		output_strlst(outmk, p->objs);
		if (p->buildopts != NULL) {
			fprintf(outmk, "%s_OPTS+=", p->ident);
			output_strlst(outmk, p->buildopts);
		}
#if 0
		fprintf(outmk, "$(%s_OBJPATHS): %s_make\n\n", p->ident, p->ident);
#endif
		fprintf(outmk, "%s_make:\n", p->ident);
		fprintf(outmk, "\t(cd $(%s_SRCDIR) && ", p->ident);
		if (makeobj)
			fprintf(outmk, "$(CRUNCHMAKE) obj && ");
		fprintf(outmk, "\\\n");
		fprintf(outmk, "\t\t$(CRUNCHMAKE) $(BUILDOPTS) $(%s_OPTS) depend &&",
		    p->ident);
		fprintf(outmk, "\\\n");
		fprintf(outmk, "\t\t$(CRUNCHMAKE) $(BUILDOPTS) $(%s_OPTS) "
		    "$(%s_OBJS))",
		    p->ident, p->ident);
		fprintf(outmk, "\n");
		fprintf(outmk, "%s_clean:\n", p->ident);
		fprintf(outmk, "\t(cd $(%s_SRCDIR) && $(CRUNCHMAKE) $(BUILDOPTS) clean cleandepend)\n\n",
		    p->ident);
	} else {
		fprintf(outmk, "%s_make:\n", p->ident);
		fprintf(outmk, "\t@echo \"** cannot make objs for %s\"\n\n",
		    p->name);
	}

	if (p->libs) {
		fprintf(outmk, "%s_LIBS=", p->ident);
		output_strlst(outmk, p->libs);
	}

	fprintf(outmk, "%s_stub.c:\n", p->name);
	fprintf(outmk, "\techo \""
	    "extern int main(int argc, char **argv, char **envp); "
	    "int _crunched_%s_stub(int argc, char **argv, char **envp)"
	    "{return main(argc,argv,envp);}\" >%s_stub.c\n",
	    p->ident, p->name);
	fprintf(outmk, "%s.lo: %s_stub.o $(%s_OBJPATHS)",
	    p->name, p->name, p->ident);
	if (p->libs)
		fprintf(outmk, " $(%s_LIBS)", p->ident);

	fprintf(outmk, "\n");
	fprintf(outmk, "\t$(CC) -nostdlib -Wl,-dc -r -o %s.lo %s_stub.o $(%s_OBJPATHS)",
	    p->name, p->name, p->ident);
	if (p->libs)
		fprintf(outmk, " $(%s_LIBS)", p->ident);
	fprintf(outmk, "\n");
	fprintf(outmk, "\tcrunchide -k _crunched_%s_stub ", p->ident);
	for (lst = p->keeplist; lst != NULL; lst = lst->next)
		fprintf(outmk, "-k _%s ", lst->str);
	fprintf(outmk, "%s.lo\n", p->name);
}

void
output_strlst(FILE *outf, strlst_t *lst)
{
	for (; lst != NULL; lst = lst->next)
		if ( strlen(lst->str) )
			fprintf(outf, " %s", lst->str);
	fprintf(outf, "\n");
}


/*
 * ========================================================================
 * general library routines
 *
 */

void
status(const char *str)
{
	static int lastlen = 0;
	int len, spaces;

	if (!verbose)
		return;

	len = strlen(str);
	spaces = lastlen - len;
	if (spaces < 1)
		spaces = 1;

	fprintf(stderr, " [%s]%*.*s\r", str, spaces, spaces, " ");
	fflush(stderr);
	lastlen = len;
}


void
out_of_memory(void)
{
	err(1, "%s: %d: out of memory, stopping", infilename, linenum);
}


void
add_string(strlst_t **listp, char *str)
{
	strlst_t *p1, *p2;

	/* add to end, but be smart about dups */

	for (p1 = NULL, p2 = *listp; p2 != NULL; p1 = p2, p2 = p2->next)
		if (!strcmp(p2->str, str))
			return;

	p2 = malloc(sizeof(strlst_t));
	if (p2) {
		p2->next = NULL;
		p2->str = strdup(str);
    	}
	if (!p2 || !p2->str)
		out_of_memory();

	if (p1 == NULL)
		*listp = p2;
	else
		p1->next = p2;
}

int
subtract_strlst(strlst_t **lista, strlst_t **listb)
{
	int subtract_count = 0;
	strlst_t *p1;
	for (p1 = *listb; p1 != NULL; p1 = p1->next)
		if ( in_list(lista, p1->str) ) {
			warnx("Will compile library `%s' dynamically", p1->str);
			strcat(p1->str, "");
			subtract_count++;
		}
	return subtract_count;
}

int
in_list(strlst_t **listp, char *str)
{
	strlst_t *p1;
	for (p1 = *listp; p1 != NULL; p1 = p1->next)
		if (!strcmp(p1->str, str))
			return 1;
	return 0;
}

int
is_dir(const char *pathname)
{
	struct stat buf;

	if (stat(pathname, &buf) == -1)
		return 0;

	return S_ISDIR(buf.st_mode);
}

int
is_nonempty_file(const char *pathname)
{
	struct stat buf;

	if (stat(pathname, &buf) == -1)
		return 0;

	return S_ISREG(buf.st_mode) && buf.st_size > 0;
}
