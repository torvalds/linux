/*	$OpenBSD: config.h,v 1.32 2021/11/28 19:26:03 deraadt Exp $	*/
/*	$NetBSD: config.h,v 1.30 1997/02/02 21:12:30 thorpej Exp $	*/

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
 *	from: @(#)config.h	8.1 (Berkeley) 6/6/93
 */

/*
 * config.h:  Global definitions for "config"
 */

#include <sys/types.h>

#include <paths.h>
#include <stdlib.h>
#include <unistd.h>

/* These are really for MAKE_BOOTSTRAP but harmless. */
#ifndef __dead
#define __dead
#endif
#ifndef _PATH_DEVNULL
#define _PATH_DEVNULL "/dev/null"
#endif


/*
 * Name/value lists.  Values can be strings or pointers and/or can carry
 * integers.  The names can be NULL, resulting in simple value lists.
 */
struct nvlist {
	struct	nvlist *nv_next;
	const char *nv_name;
	union {
		const char *un_str;
		void *un_ptr;
	} nv_un;
#define	nv_str	nv_un.un_str
#define	nv_ptr	nv_un.un_ptr
	int	nv_int;
};

/*
 * Kernel configurations.
 */
struct config {
	struct	config *cf_next;	/* linked list */
	const char *cf_name;		/* "vmunix" */
	int	cf_lineno;		/* source line */
	struct	nvlist *cf_root;	/* "root on ra0a" */
	struct	nvlist *cf_swap;	/* "swap on ra0b and ra1b" */
	struct	nvlist *cf_dump;	/* "dumps on ra0b" */
};

/*
 * Attributes.  These come in two flavors: "plain" and "interface".
 * Plain attributes (e.g., "ether") simply serve to pull in files.
 * Interface attributes (e.g., "scsi") carry three lists: locators,
 * child devices, and references.  The locators are those things
 * that must be specified in order to configure a device instance
 * using this attribute (e.g., "tg0 at scsi0").  The a_devs field
 * lists child devices that can connect here (e.g., "tg"s), while
 * the a_refs are parents that carry the attribute (e.g., actual
 * SCSI host adapter drivers such as the SPARC "esp").
 */
struct attr {
	const char *a_name;		/* name of this attribute */
	int	a_iattr;		/* true => allows children */
	struct	nvlist *a_locs;		/* locators required */
	int	a_loclen;		/* length of above list */
	struct	nvlist *a_devs;		/* children */
	struct	nvlist *a_refs;		/* parents */
};

/*
 * The "base" part (struct devbase) of a device ("uba", "sd"; but not
 * "uba2" or "sd0").  It may be found "at" one or more attributes,
 * including "at root" (this is represented by a NULL attribute), as
 * specified by the device attachments (struct deva).
 *
 * Each device may also export attributes.  If any provide an output
 * interface (e.g., "esp" provides "scsi"), other devices (e.g.,
 * "tg"s) can be found at instances of this one (e.g., "esp"s).
 * Such a connection must provide locators as specified by that
 * interface attribute (e.g., "target").  The base device can
 * export both output (aka `interface') attributes, as well as
 * import input (`plain') attributes.  Device attachments may
 * only import input attributes; it makes no sense to have a
 * specific attachment export a new interface to other devices.
 *
 * Each base carries a list of instances (via d_ihead).  Note that this
 * list "skips over" aliases; those must be found through the instances
 * themselves.  Each base also carries a list of possible attachments,
 * each of which specify a set of devices that the device can attach
 * to, as well as the device instances that are actually using that
 * attachment.
 */
struct devbase {
	const char *d_name;		/* e.g., "sd" */
	struct	devbase *d_next;	/* linked list */
	int	d_isdef;		/* set once properly defined */
	int	d_ispseudo;		/* is a pseudo-device */
	int	d_major;		/* used for "root on sd0", e.g. */
	struct	nvlist *d_attrs;	/* attributes, if any */
	int	d_umax;			/* highest unit number + 1 */
	struct	devi *d_ihead;		/* first instance, if any */
	struct	devi **d_ipp;		/* used for tacking on more instances */
	struct	deva *d_ahead;		/* first attachment, if any */
	struct	deva **d_app;		/* used for tacking on attachments */
};

struct deva {
	const char *d_name;		/* name of attachment, e.g. "com_isa" */
	struct	deva *d_next;		/* linked list */
	struct	deva *d_bsame;		/* list on same base */
	int	d_isdef;		/* set once properly defined */
	struct	devbase *d_devbase;	/* the base device */
	struct	nvlist *d_atlist;	/* e.g., "at tg" (attr list) */
	struct	nvlist *d_attrs;	/* attributes, if any */
	struct	devi *d_ihead;		/* first instance, if any */
	struct	devi **d_ipp;		/* used for tacking on more instances */
};

/*
 * An "instance" of a device.  The same instance may be listed more
 * than once, e.g., "xx0 at isa? port FOO" + "xx0 at isa? port BAR".
 *
 * After everything has been read in and verified, the devi's are
 * "packed" to collect all the information needed to generate ioconf.c.
 * In particular, we try to collapse multiple aliases into a single entry.
 * We then assign each "primary" (non-collapsed) instance a cfdata index.
 * Note that there may still be aliases among these.
 */
struct devi {
	/* created while parsing config file */
	const char *i_name;	/* e.g., "sd0" */
	int	i_unit;		/* unit from name, e.g., 0 */
	int	i_disable;	/* device is disabled */
	struct	devbase *i_base;/* e.g., pointer to "sd" base */
	struct	devi *i_next;	/* list of all instances */
	struct	devi *i_bsame;	/* list on same base */
	struct	devi *i_asame;	/* list on same base attachment */
	struct	devi *i_alias;	/* other aliases of this instance */
	const char *i_at;	/* where this is "at" (NULL if at root) */
	struct	attr *i_atattr;	/* attr that allowed attach */
	struct	devbase *i_atdev;/* if "at <devname><unit>", else NULL */
	struct	deva *i_atdeva;
	const char **i_locs;	/* locators (as given by i_atattr) */
	int	i_atunit;	/* unit from "at" */
	int	i_cfflags;	/* flags from config line */
	int	i_lineno;	/* line # in config, for later errors */

	/* created during packing or ioconf.c generation */
/*		i_loclen	   via i_atattr->a_loclen */
	short	i_collapsed;	/* set => this alias no longer needed */
	short	i_cfindex;	/* our index in cfdata */
	short	i_pvlen;	/* number of parents */
	short	i_pvoff;	/* offset in parents.vec */
	short	i_locoff;	/* offset in locators.vec */
	struct	devi **i_parents;/* the parents themselves */
	int	i_locnami;	/* my index into locnami[] */
	int	i_plocnami;	/* parent's locnami[] index */
};
/* special units */
#define	STAR	(-1)		/* unit number for, e.g., "sd*" */
#define	WILD	(-2)		/* unit number for, e.g., "sd?" */

/*
 * Files.  Each file is either standard (always included) or optional,
 * depending on whether it has names on which to *be* optional.  The
 * options field (fi_optx) is actually an expression tree, with nodes
 * for OR, AND, and NOT, as well as atoms (words) representing some
 * particular option.  The node type is stored in the nv_int field.
 * Subexpressions appear in the `next' field; for the binary operators
 * AND and OR, the left subexpression is first stored in the nv_ptr field.
 *
 * For any file marked as needs-count or needs-flag, fixfiles() will
 * build fi_optf, a `flat list' of the options with nv_int fields that
 * contain counts or `need' flags; this is used in mkheaders().
 */
struct files {
	struct	files *fi_next;	/* linked list */
	const char *fi_srcfile;	/* the name of the "files" file that got us */
	u_short	fi_srcline;	/* and the line number */
	u_char	fi_flags;	/* as below */
	char	fi_lastc;	/* last char from path */
	struct nvlist *fi_nvpath; /* list of paths */
	const char *fi_base;	/* tail minus ".c" (or whatever) */
	struct  nvlist *fi_optx;/* options expression */
	struct  nvlist *fi_optf;/* flattened version of above, if needed */
	const char *fi_mkrule;/* special make rules, if any */
};

/*
 * Objects and libraries.  This allows precompiled object and library
 * files (e.g. binary-only device drivers) to be linked in.
 */
struct objects {
	struct  objects *oi_next;/* linked list */
	const char *oi_srcfile; /* the name of the "objects" file that got us */
	u_short oi_srcline;	/* and the line number */
	u_char  oi_flags;	/* as below */
	char    oi_lastc;	/* last char from path */
	const char *oi_path;    /* full object path */
	struct  nvlist *oi_optx;/* options expression */
	struct  nvlist *oi_optf;/* flattened version of above, if needed */
};

#define	OI_SEL		0x01	/* selected */
#define	OI_NEEDSFLAG	0x02	/* needs-flag */

#define	FX_ATOM		0	/* atom (in nv_name) */
#define	FX_NOT		1	/* NOT expr (subexpression in nv_next) */
#define	FX_AND		2	/* AND expr (lhs in nv_ptr, rhs in nv_next) */
#define	FX_OR		3	/* OR expr (lhs in nv_ptr, rhs in nv_next) */

/* flags */
#define	FI_SEL		0x01	/* selected */
#define	FI_NEEDSCOUNT	0x02	/* needs-count */
#define	FI_NEEDSFLAG	0x04	/* needs-flag */
#define	FI_HIDDEN	0x08	/* obscured by other(s), base names overlap */

/*
 * Hash tables look up name=value pairs.  The pointer value of the name
 * is assumed to be constant forever; this can be arranged by interning
 * the name.  (This is fairly convenient since our lexer does this for
 * all identifier-like strings---it has to save them anyway, lest yacc's
 * look-ahead wipe out the current one.)
 */
struct hashtab;

extern const char *conffile;		/* source file, e.g., "GENERIC.sparc" */
extern const char *last_component;
extern const char *machine;		/* machine type, e.g., "sparc" or "sun3" */
extern const char *machinearch;	/* machine arch, e.g., "sparc" or "m68k" */
extern const char *srcdir;		/* path to source directory (rel. to build) */
extern const char *builddir;		/* path to build directory */
extern const char *defbuilddir;	/* default build directory */
extern int errors;			/* counts calls to error() */
extern int minmaxusers;		/* minimum "maxusers" parameter */
extern int defmaxusers;		/* default "maxusers" parameter */
extern int maxmaxusers;		/* default "maxusers" parameter */
extern int maxusers;		/* configuration's "maxusers" parameter */
extern int maxpartitions;		/* configuration's "maxpartitions" parameter */
extern struct nvlist *options;	/* options */
extern struct nvlist *defoptions;	/* "defopt"'d options */
extern struct nvlist *mkoptions;	/* makeoptions */
extern struct hashtab *devbasetab;	/* devbase lookup */
extern struct hashtab *devatab;	/* devbase attachment lookup */
extern struct hashtab *selecttab;	/* selects things that are "optional foo" */
extern struct hashtab *needcnttab;	/* retains names marked "needs-count" */
extern struct hashtab *opttab;	/* table of configured options */
extern struct hashtab *defopttab;	/* options that have been "defopt"'d */
extern struct devbase *allbases;	/* list of all devbase structures */
extern struct deva *alldevas;		/* list of all devbase attachment structures */
extern struct config *allcf;		/* list of configured kernels */
extern struct devi *alldevi;		/* list of all instances */
extern struct devi *allpseudo;	/* list of all pseudo-devices */
extern int ndevi;			/* number of devi's (before packing) */
extern int npseudo;		/* number of pseudo's */

extern struct files *allfiles;	/* list of all kernel source files */
extern struct objects *allobjects;	/* list of all kernel object and library files */

extern struct devi **packed;		/* arrayified table for packed devi's */
extern int npacked;		/* size of packed table, <= ndevi */

struct parents {			/* pv[] table for config */
	short	*vec;
	int	used;
};
extern struct parents parents;
struct locators {			/* loc[] table for config */
	const char **vec;
	int	used;
};
extern struct locators locators;

/* files.c */
void	initfiles(void);
void	checkfiles(void);
int	fixfiles(void);		/* finalize */
int	fixobjects(void);
void	addfile(struct nvlist *, struct nvlist *, int, const char *);
void	addobject(const char *, struct nvlist *, int);

/* hash.c */
struct	hashtab *ht_new(void);
int	ht_insrep(struct hashtab *, const char *, void *, int);
int	ht_remove(struct hashtab *, const char *);
#define	ht_insert(ht, nam, val) ht_insrep(ht, nam, val, 0)
#define	ht_replace(ht, nam, val) ht_insrep(ht, nam, val, 1)
void	*ht_lookup(struct hashtab *, const char *);
void	initintern(void);
const char *intern(const char *);

/* main.c */
void	addoption(const char *name, const char *value);
void	removeoption(const char *name);
void	addmkoption(const char *name, const char *value);
void	defoption(const char *name);
int	devbase_has_instances(struct devbase *, int);
int	deva_has_instances(struct deva *, int);
void	setupdirs(void);
extern int	pflag;
extern char 	*sflag;
extern char	*bflag;
extern char	*startdir;

/* mkheaders.c */
int	mkheaders(void);

/* mkioconf.c */
int	mkioconf(void);

/* mkmakefile.c */
int	mkmakefile(void);

/* mkswap.c */
extern dev_t nodev;
int	mkswap(void);

/* pack.c */
void	pack(void);

/* scan.l */
int	currentline(void);
int	firstfile(const char *);
int	include(const char *, int);

/* sem.c, other than for yacc actions */
void	initsem(void);

/* util.c */
void	*emalloc(size_t);
void	*ereallocarray(void *, size_t, size_t);
void	*ecalloc(size_t, size_t);
char	*sourcepath(const char *);
void	error(const char *, ...)			/* immediate errs */
		__attribute__((__format__ (printf, 1, 2)));
void	xerror(const char *, int, const char *, ...)	/* delayed errs */
		__attribute__((__format__ (printf, 3, 4)));
__dead void panic(const char *, ...)
		__attribute__((__format__ (printf, 1, 2)));
struct nvlist *newnv(const char *, const char *, void *, int, struct nvlist *);
void	nvfree(struct nvlist *);
void	nvfreel(struct nvlist *);

int	ukc(char *, char *, int, int);
