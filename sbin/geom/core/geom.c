/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2009 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <libutil.h>
#include <inttypes.h>
#include <dlfcn.h>
#include <assert.h>
#include <libgeom.h>
#include <geom.h>

#include "misc/subr.h"

#ifdef STATIC_GEOM_CLASSES
extern uint32_t gpart_version;
extern struct g_command gpart_class_commands[];
extern uint32_t glabel_version;
extern struct g_command glabel_class_commands[];
#endif

static char comm[MAXPATHLEN], *class_name = NULL, *gclass_name = NULL;
static uint32_t *version = NULL;
static int verbose = 0;
static struct g_command *class_commands = NULL;

#define	GEOM_CLASS_CMDS		0x01
#define	GEOM_STD_CMDS		0x02

#define	GEOM_CLASS_WIDTH	10

static struct g_command *find_command(const char *cmdstr, int flags);
static void list_one_geom_by_provider(const char *provider_name);
static int std_available(const char *name);

static void std_help(struct gctl_req *req, unsigned flags);
static void std_list(struct gctl_req *req, unsigned flags);
static void std_status(struct gctl_req *req, unsigned flags);
static void std_load(struct gctl_req *req, unsigned flags);
static void std_unload(struct gctl_req *req, unsigned flags);

static struct g_command std_commands[] = {
	{ "help", 0, std_help, G_NULL_OPTS, NULL },
	{ "list", 0, std_list,
	    {
		{ 'a', "all", NULL, G_TYPE_BOOL },
		G_OPT_SENTINEL
	    },
	    "[-a] [name ...]"
	},
	{ "status", 0, std_status,
	    {
		{ 'a', "all", NULL, G_TYPE_BOOL },
		{ 'g', "geoms", NULL, G_TYPE_BOOL },
		{ 's', "script", NULL, G_TYPE_BOOL },
		G_OPT_SENTINEL
	    },
	    "[-ags] [name ...]"
	},
	{ "load", G_FLAG_VERBOSE | G_FLAG_LOADKLD, std_load, G_NULL_OPTS,
	    NULL },
	{ "unload", G_FLAG_VERBOSE, std_unload, G_NULL_OPTS, NULL },
	G_CMD_SENTINEL
};

static void
usage_command(struct g_command *cmd, const char *prefix)
{
	struct g_option *opt;
	unsigned i;

	if (cmd->gc_usage != NULL) {
		char *pos, *ptr, *sptr;

		sptr = ptr = strdup(cmd->gc_usage);
		while ((pos = strsep(&ptr, "\n")) != NULL) {
			if (*pos == '\0')
				continue;
			fprintf(stderr, "%s %s %s %s\n", prefix, comm,
			    cmd->gc_name, pos);
		}
		free(sptr);
		return;
	}

	fprintf(stderr, "%s %s %s", prefix, comm, cmd->gc_name);
	if ((cmd->gc_flags & G_FLAG_VERBOSE) != 0)
		fprintf(stderr, " [-v]");
	for (i = 0; ; i++) {
		opt = &cmd->gc_options[i];
		if (opt->go_name == NULL)
			break;
		if (opt->go_val != NULL || G_OPT_TYPE(opt) == G_TYPE_BOOL)
			fprintf(stderr, " [");
		else
			fprintf(stderr, " ");
		fprintf(stderr, "-%c", opt->go_char);
		if (G_OPT_TYPE(opt) != G_TYPE_BOOL)
			fprintf(stderr, " %s", opt->go_name);
		if (opt->go_val != NULL || G_OPT_TYPE(opt) == G_TYPE_BOOL)
			fprintf(stderr, "]");
	}
	fprintf(stderr, "\n");
}

static void
usage(void)
{

	if (class_name == NULL) {
		fprintf(stderr, "usage: geom <class> <command> [options]\n");
		fprintf(stderr, "       geom -p <provider-name>\n");
		fprintf(stderr, "       geom -t\n");
		exit(EXIT_FAILURE);
	} else {
		struct g_command *cmd;
		const char *prefix;
		unsigned i;

		prefix = "usage:";
		if (class_commands != NULL) {
			for (i = 0; ; i++) {
				cmd = &class_commands[i];
				if (cmd->gc_name == NULL)
					break;
				usage_command(cmd, prefix);
				prefix = "      ";
			}
		}
		for (i = 0; ; i++) {
			cmd = &std_commands[i];
			if (cmd->gc_name == NULL)
				break;
			/*
			 * If class defines command, which has the same name as
			 * standard command, skip it, because it was already
			 * shown on usage().
			 */
			if (find_command(cmd->gc_name, GEOM_CLASS_CMDS) != NULL)
				continue;
			usage_command(cmd, prefix);
			prefix = "      ";
		}
		exit(EXIT_FAILURE);
	}
}

static void
load_module(void)
{
	char name1[64], name2[64];

	snprintf(name1, sizeof(name1), "g_%s", class_name);
	snprintf(name2, sizeof(name2), "geom_%s", class_name);
	if (modfind(name1) < 0) {
		/* Not present in kernel, try loading it. */
		if (kldload(name2) < 0 || modfind(name1) < 0) {
			if (errno != EEXIST) {
				err(EXIT_FAILURE, "cannot load %s", name2);
			}
		}
	}
}

static int
strlcatf(char *str, size_t size, const char *format, ...)
{
	size_t len;
	va_list ap;
	int ret;

	len = strlen(str);
	str += len;
	size -= len;

	va_start(ap, format);
	ret = vsnprintf(str, size, format, ap);
	va_end(ap);

	return (ret);
}

/*
 * Find given option in options available for given command.
 */
static struct g_option *
find_option(struct g_command *cmd, char ch)
{
	struct g_option *opt;
	unsigned i;

	for (i = 0; ; i++) {
		opt = &cmd->gc_options[i];
		if (opt->go_name == NULL)
			return (NULL);
		if (opt->go_char == ch)
			return (opt);
	}
	/* NOTREACHED */
	return (NULL);
}

/*
 * Add given option to gctl_req.
 */
static void
set_option(struct gctl_req *req, struct g_option *opt, const char *val)
{
	const char *optname;
	uint64_t number;
	void *ptr;

	if (G_OPT_ISMULTI(opt)) {
		size_t optnamesize;

		if (G_OPT_NUM(opt) == UCHAR_MAX)
			errx(EXIT_FAILURE, "Too many -%c options.", opt->go_char);

		/*
		 * Base option name length plus 3 bytes for option number
		 * (max. 255 options) plus 1 byte for terminating '\0'.
		 */
		optnamesize = strlen(opt->go_name) + 3 + 1;
		ptr = malloc(optnamesize);
		if (ptr == NULL)
			errx(EXIT_FAILURE, "No memory.");
		snprintf(ptr, optnamesize, "%s%u", opt->go_name, G_OPT_NUM(opt));
		G_OPT_NUMINC(opt);
		optname = ptr;
	} else {
		optname = opt->go_name;
	}

	if (G_OPT_TYPE(opt) == G_TYPE_NUMBER) {
		if (expand_number(val, &number) == -1) {
			err(EXIT_FAILURE, "Invalid value for '%c' argument",
			    opt->go_char);
		}
		ptr = malloc(sizeof(intmax_t));
		if (ptr == NULL)
			errx(EXIT_FAILURE, "No memory.");
		*(intmax_t *)ptr = number;
		opt->go_val = ptr;
		gctl_ro_param(req, optname, sizeof(intmax_t), opt->go_val);
	} else if (G_OPT_TYPE(opt) == G_TYPE_STRING) {
		gctl_ro_param(req, optname, -1, val);
	} else if (G_OPT_TYPE(opt) == G_TYPE_BOOL) {
		ptr = malloc(sizeof(int));
		if (ptr == NULL)
			errx(EXIT_FAILURE, "No memory.");
		*(int *)ptr = *val - '0';
		opt->go_val = ptr;
		gctl_ro_param(req, optname, sizeof(int), opt->go_val);
	} else {
		assert(!"Invalid type");
	}

	if (G_OPT_ISMULTI(opt))
		free(__DECONST(char *, optname));
}

/*
 * 1. Add given argument by caller.
 * 2. Add default values of not given arguments.
 * 3. Add the rest of arguments.
 */
static void
parse_arguments(struct g_command *cmd, struct gctl_req *req, int *argc,
    char ***argv)
{
	struct g_option *opt;
	char opts[64];
	unsigned i;
	int ch;

	*opts = '\0';
	if ((cmd->gc_flags & G_FLAG_VERBOSE) != 0)
		strlcat(opts, "v", sizeof(opts));
	for (i = 0; ; i++) {
		opt = &cmd->gc_options[i];
		if (opt->go_name == NULL)
			break;
		assert(G_OPT_TYPE(opt) != 0);
		assert((opt->go_type & ~(G_TYPE_MASK | G_TYPE_MULTI)) == 0);
		/* Multiple bool arguments makes no sense. */
		assert(G_OPT_TYPE(opt) != G_TYPE_BOOL ||
		    (opt->go_type & G_TYPE_MULTI) == 0);
		strlcatf(opts, sizeof(opts), "%c", opt->go_char);
		if (G_OPT_TYPE(opt) != G_TYPE_BOOL)
			strlcat(opts, ":", sizeof(opts));
	}

	/*
	 * Add specified arguments.
	 */
	while ((ch = getopt(*argc, *argv, opts)) != -1) {
		/* Standard (not passed to kernel) options. */
		switch (ch) {
		case 'v':
			verbose = 1;
			continue;
		}
		/* Options passed to kernel. */
		opt = find_option(cmd, ch);
		if (opt == NULL)
			usage();
		if (!G_OPT_ISMULTI(opt) && G_OPT_ISDONE(opt)) {
			warnx("Option '%c' specified twice.", opt->go_char);
			usage();
		}
		G_OPT_DONE(opt);

		if (G_OPT_TYPE(opt) == G_TYPE_BOOL)
			set_option(req, opt, "1");
		else
			set_option(req, opt, optarg);
	}
	*argc -= optind;
	*argv += optind;

	/*
	 * Add not specified arguments, but with default values.
	 */
	for (i = 0; ; i++) {
		opt = &cmd->gc_options[i];
		if (opt->go_name == NULL)
			break;
		if (G_OPT_ISDONE(opt))
			continue;

		if (G_OPT_TYPE(opt) == G_TYPE_BOOL) {
			assert(opt->go_val == NULL);
			set_option(req, opt, "0");
		} else {
			if (opt->go_val == NULL) {
				warnx("Option '%c' not specified.",
				    opt->go_char);
				usage();
			} else if (opt->go_val == G_VAL_OPTIONAL) {
				/* add nothing. */
			} else {
				set_option(req, opt, opt->go_val);
			}
		}
	}

	/*
	 * Add rest of given arguments.
	 */
	gctl_ro_param(req, "nargs", sizeof(int), argc);
	for (i = 0; i < (unsigned)*argc; i++) {
		char argname[16];

		snprintf(argname, sizeof(argname), "arg%u", i);
		gctl_ro_param(req, argname, -1, (*argv)[i]);
	}
}

/*
 * Find given command in commands available for given class.
 */
static struct g_command *
find_command(const char *cmdstr, int flags)
{
	struct g_command *cmd;
	unsigned i;

	/*
	 * First try to find command defined by loaded library.
	 */
	if ((flags & GEOM_CLASS_CMDS) != 0 && class_commands != NULL) {
		for (i = 0; ; i++) {
			cmd = &class_commands[i];
			if (cmd->gc_name == NULL)
				break;
			if (strcmp(cmd->gc_name, cmdstr) == 0)
				return (cmd);
		}
	}
	/*
	 * Now try to find in standard commands.
	 */
	if ((flags & GEOM_STD_CMDS) != 0) {
		for (i = 0; ; i++) {
			cmd = &std_commands[i];
			if (cmd->gc_name == NULL)
				break;
			if (strcmp(cmd->gc_name, cmdstr) == 0)
				return (cmd);
		}
	}
	return (NULL);
}

static unsigned
set_flags(struct g_command *cmd)
{
	unsigned flags = 0;

	if ((cmd->gc_flags & G_FLAG_VERBOSE) != 0 && verbose)
		flags |= G_FLAG_VERBOSE;

	return (flags);
}

/*
 * Run command.
 */
static void
run_command(int argc, char *argv[])
{
	struct g_command *cmd;
	struct gctl_req *req;
	const char *errstr;
	char buf[4096];

	/* First try to find a command defined by a class. */
	cmd = find_command(argv[0], GEOM_CLASS_CMDS);
	if (cmd == NULL) {
		/* Now, try to find a standard command. */
		cmd = find_command(argv[0], GEOM_STD_CMDS);
		if (cmd == NULL) {
			warnx("Unknown command: %s.", argv[0]);
			usage();
		}
		if (!std_available(cmd->gc_name)) {
			warnx("Command '%s' not available; "
			    "try 'load' first.", argv[0]);
			exit(EXIT_FAILURE);
		}
	}
	if ((cmd->gc_flags & G_FLAG_LOADKLD) != 0)
		load_module();

	req = gctl_get_handle();
	gctl_ro_param(req, "class", -1, gclass_name);
	gctl_ro_param(req, "verb", -1, argv[0]);
	if (version != NULL)
		gctl_ro_param(req, "version", sizeof(*version), version);
	parse_arguments(cmd, req, &argc, &argv);

	bzero(buf, sizeof(buf));
	if (cmd->gc_func != NULL) {
		unsigned flags;

		flags = set_flags(cmd);
		cmd->gc_func(req, flags);
		errstr = req->error;
	} else {
		gctl_rw_param(req, "output", sizeof(buf), buf);
		errstr = gctl_issue(req);
	}
	if (errstr != NULL && errstr[0] != '\0') {
		warnx("%s", errstr);
		if (strncmp(errstr, "warning: ", strlen("warning: ")) != 0) {
			gctl_free(req);
			exit(EXIT_FAILURE);
		}
	}
	if (buf[0] != '\0')
		printf("%s", buf);
	gctl_free(req);
	if (verbose)
		printf("Done.\n");
	exit(EXIT_SUCCESS);
}

#ifndef STATIC_GEOM_CLASSES
static const char *
library_path(void)
{
	const char *path;

	path = getenv("GEOM_LIBRARY_PATH");
	if (path == NULL)
		path = GEOM_CLASS_DIR;
	return (path);
}

static void
load_library(void)
{
	char *curpath, path[MAXPATHLEN], *tofree, *totalpath;
	uint32_t *lib_version;
	void *dlh;
	int ret;

	ret = 0;
	tofree = totalpath = strdup(library_path());
	if (totalpath == NULL)
		err(EXIT_FAILURE, "Not enough memory for library path");

	if (strchr(totalpath, ':') != NULL)
		curpath = strsep(&totalpath, ":");
	else
		curpath = totalpath;
	/* Traverse the paths to find one that contains the library we want. */
	while (curpath != NULL) {
		snprintf(path, sizeof(path), "%s/geom_%s.so", curpath,
		    class_name);
		ret = access(path, F_OK);
		if (ret == -1) {
			if (errno == ENOENT) {
				/*
				 * If we cannot find library, try the next
				 * path.
				 */
				curpath = strsep(&totalpath, ":");
				continue;
			}
			err(EXIT_FAILURE, "Cannot access library");
		}
		break;
	}
	free(tofree);
	/* No library was found, but standard commands can still be used */
	if (ret == -1)
		return;
	dlh = dlopen(path, RTLD_NOW);
	if (dlh == NULL)
		errx(EXIT_FAILURE, "Cannot open library: %s.", dlerror());
	lib_version = dlsym(dlh, "lib_version");
	if (lib_version == NULL) {
		warnx("Cannot find symbol %s: %s.", "lib_version", dlerror());
		dlclose(dlh);
		exit(EXIT_FAILURE);
	}
	if (*lib_version != G_LIB_VERSION) {
		dlclose(dlh);
		errx(EXIT_FAILURE, "%s and %s are not synchronized.",
		    getprogname(), path);
	}
	version = dlsym(dlh, "version");
	if (version == NULL) {
		warnx("Cannot find symbol %s: %s.", "version", dlerror());
		dlclose(dlh);
		exit(EXIT_FAILURE);
	}
	class_commands = dlsym(dlh, "class_commands");
	if (class_commands == NULL) {
		warnx("Cannot find symbol %s: %s.", "class_commands",
		    dlerror());
		dlclose(dlh);
		exit(EXIT_FAILURE);
	}
}
#endif	/* !STATIC_GEOM_CLASSES */

/*
 * Class name should be all capital letters.
 */
static void
set_class_name(void)
{
	char *s1, *s2;

	s1 = class_name;
	for (; *s1 != '\0'; s1++)
		*s1 = tolower(*s1);
	gclass_name = malloc(strlen(class_name) + 1);
	if (gclass_name == NULL)
		errx(EXIT_FAILURE, "No memory");
	s1 = gclass_name;
	s2 = class_name;
	for (; *s2 != '\0'; s2++)
		*s1++ = toupper(*s2);
	*s1 = '\0';
}

static void
get_class(int *argc, char ***argv)
{

	snprintf(comm, sizeof(comm), "%s", basename((*argv)[0]));
	if (strcmp(comm, "geom") == 0) {
		if (*argc < 2)
			usage();
		else if (*argc == 2) {
			if (strcmp((*argv)[1], "-h") == 0 ||
			    strcmp((*argv)[1], "help") == 0) {
				usage();
			}
		}
		strlcatf(comm, sizeof(comm), " %s", (*argv)[1]);
		class_name = (*argv)[1];
		*argc -= 2;
		*argv += 2;
	} else if (*comm == 'g') {
		class_name = comm + 1;
		*argc -= 1;
		*argv += 1;
	} else {
		errx(EXIT_FAILURE, "Invalid utility name.");
	}

#ifndef STATIC_GEOM_CLASSES
	load_library();
#else
	if (!strcasecmp(class_name, "part")) {
		version = &gpart_version;
		class_commands = gpart_class_commands;
	} else if (!strcasecmp(class_name, "label")) {
		version = &glabel_version;
		class_commands = glabel_class_commands;
	}
#endif /* !STATIC_GEOM_CLASSES */

	set_class_name();

	/* If we can't load or list, it's not a class. */
	if (!std_available("load") && !std_available("list"))
		errx(EXIT_FAILURE, "Invalid class name '%s'.", class_name);

	if (*argc < 1)
		usage();
}

static struct ggeom *
find_geom_by_provider(struct gmesh *mesh, const char *name)
{
	struct gclass *classp;
	struct ggeom *gp;
	struct gprovider *pp;

	LIST_FOREACH(classp, &mesh->lg_class, lg_class) {
		LIST_FOREACH(gp, &classp->lg_geom, lg_geom) {
			LIST_FOREACH(pp, &gp->lg_provider, lg_provider) {
				if (strcmp(pp->lg_name, name) == 0)
					return (gp);
			}
		}
	}

	return (NULL);
}

static int
compute_tree_width_geom(struct gmesh *mesh, struct ggeom *gp, int indent)
{
	struct gclass *classp2;
	struct ggeom *gp2;
	struct gconsumer *cp2;
	struct gprovider *pp;
	int max_width, width;

	max_width = width = indent + strlen(gp->lg_name);

	LIST_FOREACH(pp, &gp->lg_provider, lg_provider) {
		LIST_FOREACH(classp2, &mesh->lg_class, lg_class) {
			LIST_FOREACH(gp2, &classp2->lg_geom, lg_geom) {
				LIST_FOREACH(cp2,
				    &gp2->lg_consumer, lg_consumer) {
					if (pp != cp2->lg_provider)
						continue;
					width = compute_tree_width_geom(mesh,
					    gp2, indent + 2);
					if (width > max_width)
						max_width = width;
				}
			}
		}
	}

	return (max_width);
}

static int
compute_tree_width(struct gmesh *mesh)
{
	struct gclass *classp;
	struct ggeom *gp;
	int max_width, width;

	max_width = width = 0;

	LIST_FOREACH(classp, &mesh->lg_class, lg_class) {
		LIST_FOREACH(gp, &classp->lg_geom, lg_geom) {
			if (!LIST_EMPTY(&gp->lg_consumer))
				continue;
			width = compute_tree_width_geom(mesh, gp, 0);
			if (width > max_width)
				max_width = width;
		}
	}

	return (max_width);
}

static void
show_tree_geom(struct gmesh *mesh, struct ggeom *gp, int indent, int width)
{
	struct gclass *classp2;
	struct ggeom *gp2;
	struct gconsumer *cp2;
	struct gprovider *pp;

	if (LIST_EMPTY(&gp->lg_provider)) {
		printf("%*s%-*.*s %-*.*s\n", indent, "",
		    width - indent, width - indent, gp->lg_name,
		    GEOM_CLASS_WIDTH, GEOM_CLASS_WIDTH, gp->lg_class->lg_name);
		return;
	}

	LIST_FOREACH(pp, &gp->lg_provider, lg_provider) {
		printf("%*s%-*.*s %-*.*s %s\n", indent, "",
		    width - indent, width - indent, gp->lg_name,
		    GEOM_CLASS_WIDTH, GEOM_CLASS_WIDTH, gp->lg_class->lg_name,
		    pp->lg_name);

		LIST_FOREACH(classp2, &mesh->lg_class, lg_class) {
			LIST_FOREACH(gp2, &classp2->lg_geom, lg_geom) {
				LIST_FOREACH(cp2,
				    &gp2->lg_consumer, lg_consumer) {
					if (pp != cp2->lg_provider)
						continue;
					show_tree_geom(mesh, gp2,
					    indent + 2, width);
				}
			}
		}
	}
}

static void
show_tree(void)
{
	struct gmesh mesh;
	struct gclass *classp;
	struct ggeom *gp;
	int error, width;

	error = geom_gettree(&mesh);
	if (error != 0)
		errc(EXIT_FAILURE, error, "Cannot get GEOM tree");

	width = compute_tree_width(&mesh);

	printf("%-*.*s %-*.*s %s\n",
	    width, width, "Geom",
	    GEOM_CLASS_WIDTH, GEOM_CLASS_WIDTH, "Class",
	    "Provider");

	LIST_FOREACH(classp, &mesh.lg_class, lg_class) {
		LIST_FOREACH(gp, &classp->lg_geom, lg_geom) {
			if (!LIST_EMPTY(&gp->lg_consumer))
				continue;
			show_tree_geom(&mesh, gp, 0, width);
		}
	}
}

int
main(int argc, char *argv[])
{
	char *provider_name;
	bool tflag;
	int ch;

	provider_name = NULL;
	tflag = false;

	if (strcmp(getprogname(), "geom") == 0) {
		while ((ch = getopt(argc, argv, "hp:t")) != -1) {
			switch (ch) {
			case 'p':
				provider_name = strdup(optarg);
				if (provider_name == NULL)
					err(1, "strdup");
				break;
			case 't':
				tflag = true;
				break;
			case 'h':
			default:
				usage();
			}
		}

		/*
		 * Don't adjust argc and argv, it would break get_class().
		 */
	}

	if (tflag && provider_name != NULL) {
		errx(EXIT_FAILURE,
		    "At most one of -P and -t may be specified.");
	}

	if (provider_name != NULL) {
		list_one_geom_by_provider(provider_name);
		return (0);
	}

	if (tflag) {
		show_tree();
		return (0);
	}

	get_class(&argc, &argv);
	run_command(argc, argv);
	/* NOTREACHED */

	exit(EXIT_FAILURE);
}

static struct gclass *
find_class(struct gmesh *mesh, const char *name)
{
	struct gclass *classp;

	LIST_FOREACH(classp, &mesh->lg_class, lg_class) {
		if (strcmp(classp->lg_name, name) == 0)
			return (classp);
	}
	return (NULL);
}

static struct ggeom *
find_geom(struct gclass *classp, const char *name)
{
	struct ggeom *gp;

	LIST_FOREACH(gp, &classp->lg_geom, lg_geom) {
		if (strcmp(gp->lg_name, name) == 0)
			return (gp);
	}
	return (NULL);
}

static void
list_one_provider(struct gprovider *pp, const char *prefix)
{
	struct gconfig *conf;
	char buf[5];

	printf("Name: %s\n", pp->lg_name);
	humanize_number(buf, sizeof(buf), (int64_t)pp->lg_mediasize, "",
	    HN_AUTOSCALE, HN_B | HN_NOSPACE | HN_DECIMAL);
	printf("%sMediasize: %jd (%s)\n", prefix, (intmax_t)pp->lg_mediasize,
	    buf);
	printf("%sSectorsize: %u\n", prefix, pp->lg_sectorsize);
	if (pp->lg_stripesize > 0 || pp->lg_stripeoffset > 0) {
		printf("%sStripesize: %ju\n", prefix, pp->lg_stripesize);
		printf("%sStripeoffset: %ju\n", prefix, pp->lg_stripeoffset);
	}
	printf("%sMode: %s\n", prefix, pp->lg_mode);
	LIST_FOREACH(conf, &pp->lg_config, lg_config) {
		printf("%s%s: %s\n", prefix, conf->lg_name, conf->lg_val);
	}
}

static void
list_one_consumer(struct gconsumer *cp, const char *prefix)
{
	struct gprovider *pp;
	struct gconfig *conf;

	pp = cp->lg_provider;
	if (pp == NULL)
		printf("[no provider]\n");
	else {
		char buf[5];

		printf("Name: %s\n", pp->lg_name);
		humanize_number(buf, sizeof(buf), (int64_t)pp->lg_mediasize, "",
		    HN_AUTOSCALE, HN_B | HN_NOSPACE | HN_DECIMAL);
		printf("%sMediasize: %jd (%s)\n", prefix,
		    (intmax_t)pp->lg_mediasize, buf);
		printf("%sSectorsize: %u\n", prefix, pp->lg_sectorsize);
		if (pp->lg_stripesize > 0 || pp->lg_stripeoffset > 0) {
			printf("%sStripesize: %ju\n", prefix, pp->lg_stripesize);
			printf("%sStripeoffset: %ju\n", prefix, pp->lg_stripeoffset);
		}
		printf("%sMode: %s\n", prefix, cp->lg_mode);
	}
	LIST_FOREACH(conf, &cp->lg_config, lg_config) {
		printf("%s%s: %s\n", prefix, conf->lg_name, conf->lg_val);
	}
}

static void
list_one_geom(struct ggeom *gp)
{
	struct gprovider *pp;
	struct gconsumer *cp;
	struct gconfig *conf;
	unsigned n;

	printf("Geom name: %s\n", gp->lg_name);
	LIST_FOREACH(conf, &gp->lg_config, lg_config) {
		printf("%s: %s\n", conf->lg_name, conf->lg_val);
	}
	if (!LIST_EMPTY(&gp->lg_provider)) {
		printf("Providers:\n");
		n = 1;
		LIST_FOREACH(pp, &gp->lg_provider, lg_provider) {
			printf("%u. ", n++);
			list_one_provider(pp, "   ");
		}
	}
	if (!LIST_EMPTY(&gp->lg_consumer)) {
		printf("Consumers:\n");
		n = 1;
		LIST_FOREACH(cp, &gp->lg_consumer, lg_consumer) {
			printf("%u. ", n++);
			list_one_consumer(cp, "   ");
		}
	}
	printf("\n");
}

static void
list_one_geom_by_provider(const char *provider_name)
{
	struct gmesh mesh;
	struct ggeom *gp;
	int error;

	error = geom_gettree(&mesh);
	if (error != 0)
		errc(EXIT_FAILURE, error, "Cannot get GEOM tree");

	gp = find_geom_by_provider(&mesh, provider_name);
	if (gp == NULL)
		errx(EXIT_FAILURE, "Cannot find provider '%s'.", provider_name);

	printf("Geom class: %s\n", gp->lg_class->lg_name);
	list_one_geom(gp);
}

static void
std_help(struct gctl_req *req __unused, unsigned flags __unused)
{

	usage();
}

static int
std_list_available(void)
{
	struct gmesh mesh;
	struct gclass *classp;
	int error;

	error = geom_gettree(&mesh);
	if (error != 0)
		errc(EXIT_FAILURE, error, "Cannot get GEOM tree");
	classp = find_class(&mesh, gclass_name);
	geom_deletetree(&mesh);
	if (classp != NULL)
		return (1);
	return (0);
}

static void
std_list(struct gctl_req *req, unsigned flags __unused)
{
	struct gmesh mesh;
	struct gclass *classp;
	struct ggeom *gp;
	const char *name;
	int all, error, i, nargs;

	error = geom_gettree(&mesh);
	if (error != 0)
		errc(EXIT_FAILURE, error, "Cannot get GEOM tree");
	classp = find_class(&mesh, gclass_name);
	if (classp == NULL) {
		geom_deletetree(&mesh);
		errx(EXIT_FAILURE, "Class '%s' not found.", gclass_name);
	}
	nargs = gctl_get_int(req, "nargs");
	all = gctl_get_int(req, "all");
	if (nargs > 0) {
		for (i = 0; i < nargs; i++) {
			name = gctl_get_ascii(req, "arg%d", i);
			gp = find_geom(classp, name);
			if (gp == NULL) {
				errx(EXIT_FAILURE, "Class '%s' does not have "
				    "an instance named '%s'.",
				    gclass_name, name);
			}
			list_one_geom(gp);
		}
	} else {
		LIST_FOREACH(gp, &classp->lg_geom, lg_geom) {
			if (LIST_EMPTY(&gp->lg_provider) && !all)
				continue;
			list_one_geom(gp);
		}
	}
	geom_deletetree(&mesh);
}

static int
std_status_available(void)
{

	/* 'status' command is available when 'list' command is. */
	return (std_list_available());
}

static void
status_update_len(struct ggeom *gp, int *name_len, int *status_len)
{
	struct gconfig *conf;
	int len;

	assert(gp != NULL);
	assert(name_len != NULL);
	assert(status_len != NULL);

	len = strlen(gp->lg_name);
	if (*name_len < len)
		*name_len = len;
	LIST_FOREACH(conf, &gp->lg_config, lg_config) {
		if (strcasecmp(conf->lg_name, "state") == 0) {
			len = strlen(conf->lg_val);
			if (*status_len < len)
				*status_len = len;
		}
	}
}

static void
status_update_len_prs(struct ggeom *gp, int *name_len, int *status_len)
{
	struct gprovider *pp;
	struct gconfig *conf;
	int len, glen;

	assert(gp != NULL);
	assert(name_len != NULL);
	assert(status_len != NULL);

	glen = 0;
	LIST_FOREACH(conf, &gp->lg_config, lg_config) {
		if (strcasecmp(conf->lg_name, "state") == 0) {
			glen = strlen(conf->lg_val);
			break;
		}
	}
	LIST_FOREACH(pp, &gp->lg_provider, lg_provider) {
		len = strlen(pp->lg_name);
		if (*name_len < len)
			*name_len = len;
		len = glen;
		LIST_FOREACH(conf, &pp->lg_config, lg_config) {
			if (strcasecmp(conf->lg_name, "state") == 0) {
				len = strlen(conf->lg_val);
				break;
			}
		}
		if (*status_len < len)
			*status_len = len;
	}
}

static char *
status_one_consumer(struct gconsumer *cp)
{
	static char buf[256];
	struct gprovider *pp;
	struct gconfig *conf;
	const char *state, *syncr;

	pp = cp->lg_provider;
	if (pp == NULL)
		return (NULL);
	state = NULL;
	syncr = NULL;
	LIST_FOREACH(conf, &cp->lg_config, lg_config) {
		if (strcasecmp(conf->lg_name, "state") == 0)
			state = conf->lg_val;
		if (strcasecmp(conf->lg_name, "synchronized") == 0)
			syncr = conf->lg_val;
	}
	if (state == NULL && syncr == NULL)
		snprintf(buf, sizeof(buf), "%s", pp->lg_name);
	else if (state != NULL && syncr != NULL) {
		snprintf(buf, sizeof(buf), "%s (%s, %s)", pp->lg_name,
		    state, syncr);
	} else {
		snprintf(buf, sizeof(buf), "%s (%s)", pp->lg_name,
		    state ? state : syncr);
	}
	return (buf);
}

static void
status_one_geom(struct ggeom *gp, int script, int name_len, int status_len)
{
	struct gconsumer *cp;
	struct gconfig *conf;
	const char *name, *status, *component;
	int gotone;

	name = gp->lg_name;
	status = "N/A";
	LIST_FOREACH(conf, &gp->lg_config, lg_config) {
		if (strcasecmp(conf->lg_name, "state") == 0) {
			status = conf->lg_val;
			break;
		}
	}
	gotone = 0;
	LIST_FOREACH(cp, &gp->lg_consumer, lg_consumer) {
		component = status_one_consumer(cp);
		if (component == NULL)
			continue;
		gotone = 1;
		printf("%*s  %*s  %s\n", name_len, name, status_len, status,
		    component);
		if (!script)
			name = status = "";
	}
	if (!gotone) {
		printf("%*s  %*s  %s\n", name_len, name, status_len, status,
		    "N/A");
	}
}

static void
status_one_geom_prs(struct ggeom *gp, int script, int name_len, int status_len)
{
	struct gprovider *pp;
	struct gconsumer *cp;
	struct gconfig *conf;
	const char *name, *status, *component;
	int gotone;

	LIST_FOREACH(pp, &gp->lg_provider, lg_provider) {
		name = pp->lg_name;
		status = "N/A";
		LIST_FOREACH(conf, &gp->lg_config, lg_config) {
			if (strcasecmp(conf->lg_name, "state") == 0) {
				status = conf->lg_val;
				break;
			}
		}
		LIST_FOREACH(conf, &pp->lg_config, lg_config) {
			if (strcasecmp(conf->lg_name, "state") == 0) {
				status = conf->lg_val;
				break;
			}
		}
		gotone = 0;
		LIST_FOREACH(cp, &gp->lg_consumer, lg_consumer) {
			component = status_one_consumer(cp);
			if (component == NULL)
				continue;
			gotone = 1;
			printf("%*s  %*s  %s\n", name_len, name,
			    status_len, status, component);
			if (!script)
				name = status = "";
		}
		if (!gotone) {
			printf("%*s  %*s  %s\n", name_len, name,
			    status_len, status, "N/A");
		}
	}
}

static void
std_status(struct gctl_req *req, unsigned flags __unused)
{
	struct gmesh mesh;
	struct gclass *classp;
	struct ggeom *gp;
	const char *name;
	int name_len, status_len;
	int all, error, geoms, i, n, nargs, script;

	error = geom_gettree(&mesh);
	if (error != 0)
		errc(EXIT_FAILURE, error, "Cannot get GEOM tree");
	classp = find_class(&mesh, gclass_name);
	if (classp == NULL)
		errx(EXIT_FAILURE, "Class %s not found.", gclass_name);
	nargs = gctl_get_int(req, "nargs");
	all = gctl_get_int(req, "all");
	geoms = gctl_get_int(req, "geoms");
	script = gctl_get_int(req, "script");
	if (script) {
		name_len = 0;
		status_len = 0;
	} else {
		name_len = strlen("Name");
		status_len = strlen("Status");
	}
	if (nargs > 0) {
		for (i = 0, n = 0; i < nargs; i++) {
			name = gctl_get_ascii(req, "arg%d", i);
			gp = find_geom(classp, name);
			if (gp == NULL)
				errx(EXIT_FAILURE, "No such geom: %s.", name);
			if (geoms) {
				status_update_len(gp,
				    &name_len, &status_len);
			} else {
				status_update_len_prs(gp,
				    &name_len, &status_len);
			}
			n++;
		}
		if (n == 0)
			goto end;
	} else {
		n = 0;
		LIST_FOREACH(gp, &classp->lg_geom, lg_geom) {
			if (LIST_EMPTY(&gp->lg_provider) && !all)
				continue;
			if (geoms) {
				status_update_len(gp,
				    &name_len, &status_len);
			} else {
				status_update_len_prs(gp,
				    &name_len, &status_len);
			}
			n++;
		}
		if (n == 0)
			goto end;
	}
	if (!script) {
		printf("%*s  %*s  %s\n", name_len, "Name", status_len, "Status",
		    "Components");
	}
	if (nargs > 0) {
		for (i = 0; i < nargs; i++) {
			name = gctl_get_ascii(req, "arg%d", i);
			gp = find_geom(classp, name);
			if (gp == NULL)
				continue;
			if (geoms) {
				status_one_geom(gp, script, name_len,
				    status_len);
			} else {
				status_one_geom_prs(gp, script, name_len,
				    status_len);
			}
		}
	} else {
		LIST_FOREACH(gp, &classp->lg_geom, lg_geom) {
			if (LIST_EMPTY(&gp->lg_provider) && !all)
				continue;
			if (geoms) {
				status_one_geom(gp, script, name_len,
				    status_len);
			} else {
				status_one_geom_prs(gp, script, name_len,
				    status_len);
			}
		}
	}
end:
	geom_deletetree(&mesh);
}

static int
std_load_available(void)
{
	char name[MAXPATHLEN], paths[MAXPATHLEN * 8], *p;
	struct stat sb;
	size_t len;

	snprintf(name, sizeof(name), "g_%s", class_name);
	/*
	 * If already in kernel, "load" command is not available.
	 */
	if (modfind(name) >= 0)
		return (0);
	bzero(paths, sizeof(paths));
	len = sizeof(paths);
	if (sysctlbyname("kern.module_path", paths, &len, NULL, 0) < 0)
		err(EXIT_FAILURE, "sysctl(kern.module_path)");
	for (p = strtok(paths, ";"); p != NULL; p = strtok(NULL, ";")) {
		snprintf(name, sizeof(name), "%s/geom_%s.ko", p, class_name);
		/*
		 * If geom_<name>.ko file exists, "load" command is available.
		 */
		if (stat(name, &sb) == 0)
			return (1);
	}
	return (0);
}

static void
std_load(struct gctl_req *req __unused, unsigned flags)
{

	/*
	 * Do nothing special here, because of G_FLAG_LOADKLD flag,
	 * module is already loaded.
	 */
	if ((flags & G_FLAG_VERBOSE) != 0)
		printf("Module available.\n");
}

static int
std_unload_available(void)
{
	char name[64];
	int id;

	snprintf(name, sizeof(name), "geom_%s", class_name);
	id = kldfind(name);
	if (id >= 0)
		return (1);
	return (0);
}

static void
std_unload(struct gctl_req *req, unsigned flags __unused)
{
	char name[64];
	int id;

	snprintf(name, sizeof(name), "geom_%s", class_name);
	id = kldfind(name);
	if (id < 0) {
		gctl_error(req, "Could not find module: %s.", strerror(errno));
		return;
	}
	if (kldunload(id) < 0) {
		gctl_error(req, "Could not unload module: %s.",
		    strerror(errno));
		return;
	}
}

static int
std_available(const char *name)
{

	if (strcmp(name, "help") == 0)
		return (1);
	else if (strcmp(name, "list") == 0)
		return (std_list_available());
	else if (strcmp(name, "status") == 0)
		return (std_status_available());
	else if (strcmp(name, "load") == 0)
		return (std_load_available());
	else if (strcmp(name, "unload") == 0)
		return (std_unload_available());
	else
		assert(!"Unknown standard command.");
	return (0);
}
