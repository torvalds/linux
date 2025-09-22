/*	$OpenBSD: main.c,v 1.63 2021/11/20 03:13:37 jcs Exp $	*/
/*	$NetBSD: main.c,v 1.22 1997/02/02 21:12:33 thorpej Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratories.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)main.c	8.1 (Berkeley) 6/6/93
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "config.h"

int	firstfile(const char *);
int	yyparse(void);

static struct hashtab *mkopttab;
static struct nvlist **nextopt;
static struct nvlist **nextdefopt;
static struct nvlist **nextmkopt;

static __dead void stop(void);
static int do_option(struct hashtab *, struct nvlist ***,
    const char *, const char *, const char *);
static int crosscheck(void);
static int badstar(void);
static int mksymlinks(void);
static int hasparent(struct devi *);
static int cfcrosscheck(struct config *, const char *, struct nvlist *);
static void optiondelta(void);

int	verbose;

const char *conffile;		/* source file, e.g., "GENERIC.sparc" */
const char *last_component;
const char *machine;		/* machine type, e.g., "sparc" or "sun3" */
const char *machinearch;	/* machine arch, e.g., "sparc" or "m68k" */
const char *srcdir;		/* path to source directory (rel. to build) */
const char *builddir;		/* path to build directory */
const char *defbuilddir;	/* default build directory */
int errors;			/* counts calls to error() */
int minmaxusers;		/* minimum "maxusers" parameter */
int defmaxusers;		/* default "maxusers" parameter */
int maxmaxusers;		/* default "maxusers" parameter */
int maxusers;		/* configuration's "maxusers" parameter */
int maxpartitions;		/* configuration's "maxpartitions" parameter */
struct nvlist *options;	/* options */
struct nvlist *defoptions;	/* "defopt"'d options */
struct nvlist *mkoptions;	/* makeoptions */
struct hashtab *devbasetab;	/* devbase lookup */
struct hashtab *devatab;	/* devbase attachment lookup */
struct hashtab *selecttab;	/* selects things that are "optional foo" */
struct hashtab *needcnttab;	/* retains names marked "needs-count" */
struct hashtab *opttab;	/* table of configured options */
struct hashtab *defopttab;	/* options that have been "defopt"'d */
struct devbase *allbases;	/* list of all devbase structures */
struct deva *alldevas;		/* list of all devbase attachment structures */
struct config *allcf;		/* list of configured kernels */
struct devi *alldevi;		/* list of all instances */
struct devi *allpseudo;	/* list of all pseudo-devices */
int ndevi;			/* number of devi's (before packing) */
int npseudo;		/* number of pseudo's */

struct files *allfiles;	/* list of all kernel source files */
struct objects *allobjects;	/* list of all kernel object and library files */

struct devi **packed;		/* arrayified table for packed devi's */
int npacked;		/* size of packed table, <= ndevi */

struct parents parents;
struct locators locators;

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr,
		"usage: %s [-p] [-b builddir] [-s srcdir] [config-file]\n"
		"       %s -e [-u] [-c cmdfile] [-f | -o outfile] infile\n",
		__progname, __progname);

	exit(1);
}

int pflag = 0;
char *sflag = NULL;
char *bflag = NULL;
char *startdir;
char *cmdfile = NULL;
FILE *cmdfp = NULL;

int
main(int argc, char *argv[])
{
	char *p;
	char *outfile = NULL;
	int ch, eflag, uflag, fflag;
	char dirbuffer[PATH_MAX];

	if (pledge("stdio rpath wpath cpath flock proc exec", NULL) == -1)
		err(1, "pledge");

	pflag = eflag = uflag = fflag = 0;
	while ((ch = getopt(argc, argv, "c:epfb:s:o:u")) != -1) {
		switch (ch) {
		case 'c':
			cmdfile = optarg;
			break;
		case 'o':
			outfile = optarg;
			break;
		case 'u':
			uflag = 1;
			break;
		case 'f':
			fflag = 1;
			break;

		case 'e':
			eflag = 1;
			if (!isatty(STDIN_FILENO))
				verbose = 1;
			break;

		case 'p':
			/*
			 * Essentially the same as makeoptions PROF="-pg",
			 * but also changes the path from ../../compile/FOO
			 * to ../../compile/FOO.PROF; i.e., compile a
			 * profiling kernel based on a typical "regular"
			 * kernel.
			 *
			 * Note that if you always want profiling, you
			 * can (and should) use a "makeoptions" line.
			 */
			pflag = 1;
			break;

		case 'b':
			bflag = optarg;
			builddir = optarg;
			break;

		case 's':
			sflag = optarg;
			srcdir = optarg;
			break;

		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;
	if (argc > 1 || (eflag && argv[0] == NULL))
		usage();
	if (bflag) {
		startdir = getcwd(dirbuffer, sizeof dirbuffer);
		if (startdir == NULL)
			warn("Use of -b and can't getcwd, no make config");
	} else {
		startdir = "../../conf";
	}

	if (eflag) {
#ifdef MAKE_BOOTSTRAP
		errx(1, "UKC not available in this binary");
#else
		if (cmdfile != NULL) {
			cmdfp = fopen(cmdfile, "r");
			if (cmdfp == NULL)
				err(1, "open %s", cmdfile);
		}
		return (ukc(argv[0], outfile, uflag, fflag));
#endif
	}

	conffile = (argc == 1) ? argv[0] : "CONFIG";
	if (firstfile(conffile))
		err(2, "cannot read %s", conffile);

	/*
	 * Init variables.
	 */
	minmaxusers = 1;
	maxmaxusers = 10000;
	initintern();
	initfiles();
	initsem();
	devbasetab = ht_new();
	devatab = ht_new();
	selecttab = ht_new();
	needcnttab = ht_new();
	opttab = ht_new();
	mkopttab = ht_new();
	defopttab = ht_new();
	nextopt = &options;
	nextmkopt = &mkoptions;
	nextdefopt = &defoptions;

	/*
	 * Handle profiling (must do this before we try to create any
	 * files).
	 */
	last_component = strrchr(conffile, '/');
	last_component = (last_component) ? last_component + 1 : conffile;
	if (pflag) {
		if (asprintf(&p, "../compile/%s.PROF", last_component) == -1)
			err(1, NULL);
		(void)addmkoption(intern("PROF"), "-pg");
		(void)addoption(intern("GPROF"), NULL);
	} else {
		if (asprintf(&p, "../compile/%s", last_component) == -1)
			err(1, NULL);
	}
	defbuilddir = (argc == 0) ? "." : p;

	/*
	 * Parse config file (including machine definitions).
	 */
	if (yyparse())
		stop();

	/*
	 * Fix (as in `set firmly in place') files.
	 */
	if (fixfiles())
		stop();

	/*
	 * Fix objects and libraries.
	 */
	if (fixobjects())
		stop();

	/*
	 * Perform cross-checking.
	 */
	if (maxusers == 0) {
		if (defmaxusers) {
			(void)printf("maxusers not specified; %d assumed\n",
			    defmaxusers);
			maxusers = defmaxusers;
		} else {
			warnx("need \"maxusers\" line");
			errors++;
		}
	}
	if (crosscheck() || errors)
		stop();

	/*
	 * Squeeze things down and finish cross-checks (STAR checks must
	 * run after packing).
	 */
	pack();
	if (badstar())
		stop();

	/*
	 * Ready to go.  Build all the various files.
	 */
	if (mksymlinks() || mkmakefile() || mkheaders() || mkswap() ||
	    mkioconf())
		stop();
	optiondelta();
	return (0);
}

static int
mksymlink(const char *value, const char *path)
{
	int ret = 0;

	if (remove(path) && errno != ENOENT) {
		warn("remove(%s)", path);
		ret = 1;
	}
	if (symlink(value, path)) {
		warn("symlink(%s -> %s)", path, value);
		ret = 1;
	}
	return (ret);
}


/*
 * Make a symlink for "machine" so that "#include <machine/foo.h>" works,
 * and for the machine's CPU architecture, so that works as well.
 */
static int
mksymlinks(void)
{
	int ret;
	char *p, buf[PATH_MAX];
	const char *q;

	snprintf(buf, sizeof buf, "arch/%s/include", machine);
	p = sourcepath(buf);
	ret = mksymlink(p, "machine");
	if (machinearch != NULL) {
		snprintf(buf, sizeof buf, "arch/%s/include", machinearch);
		p = sourcepath(buf);
		q = machinearch;
	} else {
		p = strdup("machine");
		if (!p)
			errx(1, "out of memory");
		q = machine;
	}
	ret |= mksymlink(p, q);
	free(p);

	return (ret);
}

static __dead void
stop(void)
{
	(void)fprintf(stderr, "*** Stop.\n");
	exit(1);
}

/*
 * Define a standard option, for which a header file will be generated.
 */
void
defoption(const char *name)
{
	char *p, *low, c;
	const char *n;

	/*
	 * Convert to lower case.  The header file name will be
	 * in lower case, so we store the lower case version in
	 * the hash table to detect option name collisions.  The
	 * original string will be stored in the nvlist for use
	 * in the header file.
	 */
	low = emalloc(strlen(name) + 1);
	for (n = name, p = low; (c = *n) != '\0'; n++)
		*p++ = isupper((unsigned char)c) ?
		    tolower((unsigned char)c) : c;
	*p = 0;

	n = intern(low);
	free(low);
	(void)do_option(defopttab, &nextdefopt, n, name, "defopt");

	/*
	 * Insert a verbatim copy of the option name, as well,
	 * to speed lookups when creating the Makefile.
	 */
	(void)ht_insert(defopttab, name, (void *)name);
}

/*
 * Remove an option.
 */
void
removeoption(const char *name)
{
	struct nvlist *nv, *nvt;
	char *p, *low, c;
	const char *n;

	if ((nv = ht_lookup(opttab, name)) != NULL) {
		if (options == nv) {
			options = nv->nv_next;
			nvfree(nv);
		} else {
			nvt = options;
			while (nvt->nv_next != NULL) {
				if (nvt->nv_next == nv) {
					nvt->nv_next = nvt->nv_next->nv_next;
					nvfree(nv);
					break;
				} else
					nvt = nvt->nv_next;
			}
		}
	}

	(void)ht_remove(opttab, name);

	low = emalloc(strlen(name) + 1);
	/* make lowercase, then remove from select table */
	for (n = name, p = low; (c = *n) != '\0'; n++)
		*p++ = isupper((unsigned char)c) ?
		    tolower((unsigned char)c) : c;
	*p = 0;
	n = intern(low);
	free(low);
	(void)ht_remove(selecttab, n);
}

/*
 * Add an option from "options FOO".  Note that this selects things that
 * are "optional foo".
 */
void
addoption(const char *name, const char *value)
{
	char *p, *low, c;
	const char *n;

	if (do_option(opttab, &nextopt, name, value, "options"))
		return;

	low = emalloc(strlen(name) + 1);
	/* make lowercase, then add to select table */
	for (n = name, p = low; (c = *n) != '\0'; n++)
		*p++ = isupper((unsigned char)c) ?
		    tolower((unsigned char)c) : c;
	*p = 0;
	n = intern(low);
	free(low);
	(void)ht_insert(selecttab, n, (void *)n);
}

/*
 * Add a "make" option.
 */
void
addmkoption(const char *name, const char *value)
{

	(void)do_option(mkopttab, &nextmkopt, name, value, "mkoptions");
}

/*
 * Add a name=value pair to an option list.  The value may be NULL.
 */
static int
do_option(struct hashtab *ht, struct nvlist ***nppp, const char *name,
    const char *value, const char *type)
{
	struct nvlist *nv;

	/* assume it will work */
	nv = newnv(name, value, NULL, 0, NULL);
	if (ht_insert(ht, name, nv) == 0) {
		**nppp = nv;
		*nppp = &nv->nv_next;
		return (0);
	}

	/* oops, already got that option */
	nvfree(nv);
	if ((nv = ht_lookup(ht, name)) == NULL)
		panic("do_option");
	if (nv->nv_str != NULL)
		error("already have %s `%s=%s'", type, name, nv->nv_str);
	else
		error("already have %s `%s'", type, name);
	return (1);
}

/*
 * Return true if there is at least one instance of the given unit
 * on the given device attachment (or any units, if unit == WILD).
 */
int
deva_has_instances(struct deva *deva, int unit)
{
	struct devi *i;

	if (unit == WILD)
		return (deva->d_ihead != NULL);
	for (i = deva->d_ihead; i != NULL; i = i->i_asame)
		if (unit == i->i_unit)
			return (1);
	return (0);
}

/*
 * Return true if there is at least one instance of the given unit
 * on the given base (or any units, if unit == WILD).
 */
int
devbase_has_instances(struct devbase *dev, int unit)
{
	struct deva *da;

	for (da = dev->d_ahead; da != NULL; da = da->d_bsame)
		if (deva_has_instances(da, unit))
			return (1);
	return (0);
}

static int
hasparent(struct devi *i)
{
	struct nvlist *nv;
	int atunit = i->i_atunit;

	/*
	 * We determine whether or not a device has a parent in in one
	 * of two ways:
	 *	(1) If a parent device was named in the config file,
	 *	    i.e. cases (2) and (3) in sem.c:adddev(), then
	 *	    we search its devbase for a matching unit number.
	 *	(2) If the device was attach to an attribute, then we
	 *	    search all attributes the device can be attached to
	 *	    for parents (with appropriate unit numbers) that
	 *	    may be able to attach the device.
	 */

	/*
	 * Case (1): A parent was named.  Either it's configured, or not.
	 */
	if (i->i_atdev != NULL)
		return (devbase_has_instances(i->i_atdev, atunit));

	/*
	 * Case (2): No parent was named.  Look for devs that provide the attr.
	 */
	if (i->i_atattr != NULL)
		for (nv = i->i_atattr->a_refs; nv != NULL; nv = nv->nv_next)
			if (devbase_has_instances(nv->nv_ptr, atunit))
				return (1);
	return (0);
}

static int
cfcrosscheck(struct config *cf, const char *what, struct nvlist *nv)
{
	struct devbase *dev;
	struct devi *pd;
	int errs, devminor;

	if (maxpartitions <= 0)
		panic("cfcrosscheck");

	for (errs = 0; nv != NULL; nv = nv->nv_next) {
		if (nv->nv_name == NULL)
			continue;
		dev = ht_lookup(devbasetab, nv->nv_name);
		if (dev == NULL)
			panic("cfcrosscheck(%s)", nv->nv_name);
		devminor = minor(nv->nv_int) / maxpartitions;
		if (devbase_has_instances(dev, devminor))
			continue;
		if (devbase_has_instances(dev, STAR) &&
		    devminor >= dev->d_umax)
			continue;
		for (pd = allpseudo; pd != NULL; pd = pd->i_next)
			if (pd->i_base == dev && devminor < dev->d_umax &&
			    devminor >= 0)
				goto loop;
		(void)fprintf(stderr,
		    "%s:%d: %s says %s on %s, but there's no %s\n",
		    conffile, cf->cf_lineno,
		    cf->cf_name, what, nv->nv_str, nv->nv_str);
		errs++;
loop:
		;
	}
	return (errs);
}

/*
 * Cross-check the configuration: make sure that each target device
 * or attribute (`at foo[0*?]') names at least one real device.  Also
 * see that the root, swap, and dump devices for all configurations
 * are there.
 */
int
crosscheck(void)
{
	struct devi *i;
	struct config *cf;
	int errs;

	errs = 0;
	for (i = alldevi; i != NULL; i = i->i_next) {
		if (i->i_at == NULL || hasparent(i))
			continue;
		xerror(conffile, i->i_lineno,
		    "%s at %s is orphaned", i->i_name, i->i_at);
		(void)fprintf(stderr, " (%s %s declared)\n",
		    i->i_atunit == WILD ? "nothing matching" : "no",
		    i->i_at);
		errs++;
	}
	if (allcf == NULL) {
		(void)fprintf(stderr, "%s has no configurations!\n",
		    conffile);
		errs++;
	}
	for (cf = allcf; cf != NULL; cf = cf->cf_next) {
		if (cf->cf_root != NULL) {	/* i.e., not swap generic */
			errs += cfcrosscheck(cf, "root", cf->cf_root);
			errs += cfcrosscheck(cf, "swap", cf->cf_swap);
			errs += cfcrosscheck(cf, "dumps", cf->cf_dump);
		}
	}
	return (errs);
}

/*
 * Check to see if there is a *'d unit with a needs-count file.
 */
int
badstar(void)
{
	struct devbase *d;
	struct deva *da;
	struct devi *i;
	int errs, n;

	errs = 0;
	for (d = allbases; d != NULL; d = d->d_next) {
		for (da = d->d_ahead; da != NULL; da = da->d_bsame)
			for (i = da->d_ihead; i != NULL; i = i->i_asame) {
				if (i->i_unit == STAR)
					goto foundstar;
			}
		continue;
	foundstar:
		if (ht_lookup(needcnttab, d->d_name)) {
			warnx("%s's cannot be *'d until its driver is fixed",
			    d->d_name);
			errs++;
			continue;
		}
		for (n = 0; i != NULL; i = i->i_alias)
			if (!i->i_collapsed)
				n++;
		if (n < 1)
			panic("badstar() n<1");
	}
	return (errs);
}

/*
 * Verify/create builddir if necessary, change to it, and verify srcdir.
 * This will be called when we see the first include.
 */
void
setupdirs(void)
{
	struct stat st;
	FILE *fp;

	/* srcdir must be specified if builddir is not specified or if
	 * no configuration filename was specified. */
	if ((builddir || strcmp(defbuilddir, ".") == 0) && !srcdir) {
		error("source directory must be specified");
		exit(1);
	}

	if (srcdir == NULL)
		srcdir = "../../../..";
	if (builddir == NULL)
		builddir = defbuilddir;

	if (stat(builddir, &st) != 0) {
		if (mkdir(builddir, 0777))
			err(2, "cannot create %s", builddir);
	} else if (!S_ISDIR(st.st_mode))
		errc(2, ENOTDIR, "%s", builddir);
	if (chdir(builddir) != 0)
		errx(2, "cannot change to %s", builddir);
	if (stat(srcdir, &st) != 0 || !S_ISDIR(st.st_mode))
		errc(2, ENOTDIR, "%s", srcdir);

	if (bflag) {
		if (pledge("stdio rpath wpath cpath flock", NULL) == -1)
			err(1, "pledge");
		return;
	}

	if (stat("obj", &st) == 0)
		goto reconfig;

	fp = fopen("Makefile", "w");
	if (!fp)
		errx(2, "cannot create Makefile");
	if (fprintf(fp, ".include \"../Makefile.inc\"\n") < 0 ||
	    fclose(fp) == EOF)
		errx(2, "cannot write Makefile");

reconfig:
	if (system("make obj") != 0)
		exit(2);
	if (system("make config") != 0)
		exit(2);
	exit(0);
}

struct opt {
	const char *name;
	const char *val;
};

int
optcmp(const void *v1, const void *v2)
{
	const struct opt *sp1 = v1, *sp2 = v2;
	int r;

	r = strcmp(sp1->name, sp2->name);
	if (r == 0) {
		if (!sp1->val && !sp2->val)
			r = 0;
		else if (sp1->val && !sp2->val)
			r = -1;
		else if (sp2->val && !sp1->val)
			r = 1;
		else r = strcmp(sp1->val, sp2->val);
	}
	return (r);
}

void
optiondelta(void)
{
	struct nvlist *nv;
	char nbuf[BUFSIZ], obuf[BUFSIZ];	/* XXX size */
	int nnewopts, ret = 0, i;
	struct opt *newopts;
	FILE *fp;

	for (nnewopts = 0, nv = options; nv != NULL; nv = nv->nv_next)
		nnewopts++;
	newopts = ereallocarray(NULL, nnewopts, sizeof(struct opt));
	if (newopts == NULL)
		ret = 0;
	for (i = 0, nv = options; nv != NULL; nv = nv->nv_next, i++) {
		newopts[i].name = nv->nv_name;
		newopts[i].val = nv->nv_str;
	}
	qsort(newopts, nnewopts, sizeof (struct opt), optcmp);

	/* compare options against previous config */
	if ((fp = fopen("options", "r"))) {
		for (i = 0; !feof(fp) && i < nnewopts && ret == 0; i++) {
			if (newopts[i].val)
				snprintf(nbuf, sizeof nbuf, "%s=%s\n",
				    newopts[i].name, newopts[i].val);
			else
				snprintf(nbuf, sizeof nbuf, "%s\n",
				    newopts[i].name);
			if (fgets(obuf, sizeof obuf, fp) == NULL ||
			    strcmp(nbuf, obuf))
				ret = 1;
		}
		fclose(fp);
		fp = NULL;
	} else if (access("options", F_OK) == 0)
		ret = 1;

	/* replace with the new list of options */
	if ((fp = fopen("options", "w+"))) {
		rewind(fp);
		for (i = 0; i < nnewopts; i++) {
			if (newopts[i].val)
				fprintf(fp, "%s=%s\n", newopts[i].name,
				    newopts[i].val);
			else
				fprintf(fp, "%s\n", newopts[i].name);
		}
		fclose(fp);
	}
	free(newopts);
	if (ret == 0)
		return;
	(void)printf("Kernel options have changed -- you must run \"make clean\"\n");
}
