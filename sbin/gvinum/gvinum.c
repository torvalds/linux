/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 *  Copyright (c) 2004 Lukas Ertl
 *  Copyright (c) 2005 Chris Jones
 *  Copyright (c) 2007 Ulf Lilleengen
 *  All rights reserved.
 *
 * Portions of this software were developed for the FreeBSD Project
 * by Chris Jones thanks to the support of Google's Summer of Code
 * program and mentoring by Lukas Ertl.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/linker.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/utsname.h>

#include <geom/vinum/geom_vinum_var.h>
#include <geom/vinum/geom_vinum_share.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <libgeom.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <paths.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <unistd.h>

#include "gvinum.h"

static void gvinum_attach(int, char * const *);
static void gvinum_concat(int, char * const *);
static void gvinum_create(int, char * const *);
static void gvinum_detach(int, char * const *);
static void gvinum_grow(int, char * const *);
static void gvinum_help(void);
static void gvinum_list(int, char * const *);
static void gvinum_move(int, char * const *);
static void gvinum_mirror(int, char * const *);
static void gvinum_parityop(int, char * const * , int);
static void gvinum_printconfig(int, char * const *);
static void gvinum_raid5(int, char * const *);
static void gvinum_rename(int, char * const *);
static void gvinum_resetconfig(int, char * const *);
static void gvinum_rm(int, char * const *);
static void gvinum_saveconfig(void);
static void gvinum_setstate(int, char * const *);
static void gvinum_start(int, char * const *);
static void gvinum_stop(int, char * const *);
static void gvinum_stripe(int, char * const *);
static void parseline(int, char * const *);
static void printconfig(FILE *, const char *);

static char *create_drive(const char *);
static void create_volume(int, char * const * , const char *);
static char *find_name(const char *, int, int);
static const char *find_pattern(char *, const char *);
static void copy_device(struct gv_drive *, const char *);
#define	find_drive()							\
    find_name("gvinumdrive", GV_TYPE_DRIVE, GV_MAXDRIVENAME)

int
main(int argc, char **argv)
{
	int tokens;
	char buffer[BUFSIZ], *inputline, *token[GV_MAXARGS];

	/* Load the module if necessary. */
	if (modfind(GVINUMMOD) < 0) {
		if (kldload(GVINUMKLD) < 0 && modfind(GVINUMMOD) < 0)
			err(1, GVINUMKLD ": Kernel module not available");
	}

	/* Arguments given on the command line. */
	if (argc > 1) {
		argc--;
		argv++;
		parseline(argc, argv);

	/* Interactive mode. */
	} else {
		for (;;) {
			inputline = readline("gvinum -> ");
			if (inputline == NULL) {
				if (ferror(stdin)) {
					err(1, "can't read input");
				} else {
					printf("\n");
					exit(0);
				}
			} else if (*inputline) {
				add_history(inputline);
				strcpy(buffer, inputline);
				free(inputline);
				tokens = gv_tokenize(buffer, token, GV_MAXARGS);
				if (tokens)
					parseline(tokens, token);
			}
		}
	}
	exit(0);
}

/* Attach a plex to a volume or a subdisk to a plex. */
static void
gvinum_attach(int argc, char * const *argv)
{
	struct gctl_req *req;
	const char *errstr;
	int rename;
	off_t offset;

	rename = 0;
	offset = -1;
	if (argc < 3) {
		warnx("usage:\tattach <subdisk> <plex> [rename] "
		    "[<plexoffset>]\n"
		    "\tattach <plex> <volume> [rename]");
		return;
	}
	if (argc > 3) {
		if (!strcmp(argv[3], "rename")) {
			rename = 1;
			if (argc == 5)
				offset = strtol(argv[4], NULL, 0);
		} else
			offset = strtol(argv[3], NULL, 0);
	}
	req = gctl_get_handle();
	gctl_ro_param(req, "class", -1, "VINUM");
	gctl_ro_param(req, "verb", -1, "attach");
	gctl_ro_param(req, "child", -1, argv[1]);
	gctl_ro_param(req, "parent", -1, argv[2]);
	gctl_ro_param(req, "offset", sizeof(off_t), &offset);
	gctl_ro_param(req, "rename", sizeof(int), &rename);
	errstr = gctl_issue(req);
	if (errstr != NULL)
		warnx("attach failed: %s", errstr);
	gctl_free(req);
}

static void
gvinum_create(int argc, char * const *argv)
{
	struct gctl_req *req;
	struct gv_drive *d;
	struct gv_plex *p;
	struct gv_sd *s;
	struct gv_volume *v;
	FILE *tmp;
	int drives, errors, fd, flags, i, line, plexes, plex_in_volume;
	int sd_in_plex, status, subdisks, tokens, undeffd, volumes;
	const char *errstr;
	char buf[BUFSIZ], buf1[BUFSIZ], commandline[BUFSIZ], *sdname;
	const char *ed;
	char original[BUFSIZ], tmpfile[20], *token[GV_MAXARGS];
	char plex[GV_MAXPLEXNAME], volume[GV_MAXVOLNAME];

	tmp = NULL;
	flags = 0;
	for (i = 1; i < argc; i++) {
		/* Force flag used to ignore already created drives. */
		if (!strcmp(argv[i], "-f")) {
			flags |= GV_FLAG_F;
		/* Else it must be a file. */
		} else {
			if ((tmp = fopen(argv[i], "r")) == NULL) {
				warn("can't open '%s' for reading", argv[i]);
				return;
			}
		}	
	}

	/* We didn't get a file. */
	if (tmp == NULL) {
		snprintf(tmpfile, sizeof(tmpfile), "/tmp/gvinum.XXXXXX");
		
		if ((fd = mkstemp(tmpfile)) == -1) {
			warn("temporary file not accessible");
			return;
		}
		if ((tmp = fdopen(fd, "w")) == NULL) {
			warn("can't open '%s' for writing", tmpfile);
			return;
		}
		printconfig(tmp, "# ");
		fclose(tmp);
		
		ed = getenv("EDITOR");
		if (ed == NULL)
			ed = _PATH_VI;
		
		snprintf(commandline, sizeof(commandline), "%s %s", ed,
		    tmpfile);
		status = system(commandline);
		if (status != 0) {
			warn("couldn't exec %s; status: %d", ed, status);
			return;
		}
		
		if ((tmp = fopen(tmpfile, "r")) == NULL) {
			warn("can't open '%s' for reading", tmpfile);
			return;
		}
	}

	req = gctl_get_handle();
	gctl_ro_param(req, "class", -1, "VINUM");
	gctl_ro_param(req, "verb", -1, "create");
	gctl_ro_param(req, "flags", sizeof(int), &flags);

	drives = volumes = plexes = subdisks = 0;
	plex_in_volume = sd_in_plex = undeffd = 0;
	plex[0] = '\0';
	errors = 0;
	line = 1;
	while ((fgets(buf, BUFSIZ, tmp)) != NULL) {

		/* Skip empty lines and comments. */
		if (*buf == '\0' || *buf == '#') {
			line++;
			continue;
		}

		/* Kill off the newline. */
		buf[strlen(buf) - 1] = '\0';

		/*
		 * Copy the original input line in case we need it for error
		 * output.
		 */
		strlcpy(original, buf, sizeof(original));

		tokens = gv_tokenize(buf, token, GV_MAXARGS);
		if (tokens <= 0) {
			line++;
			continue;
		}

		/* Volume definition. */
		if (!strcmp(token[0], "volume")) {
			v = gv_new_volume(tokens, token);
			if (v == NULL) {
				warnx("line %d: invalid volume definition",
				    line);
				warnx("line %d: '%s'", line, original);
				errors++;
				line++;
				continue;
			}

			/* Reset plex count for this volume. */
			plex_in_volume = 0;

			/*
			 * Set default volume name for following plex
			 * definitions.
			 */
			strlcpy(volume, v->name, sizeof(volume));

			snprintf(buf1, sizeof(buf1), "volume%d", volumes);
			gctl_ro_param(req, buf1, sizeof(*v), v);
			volumes++;

		/* Plex definition. */
		} else if (!strcmp(token[0], "plex")) {
			p = gv_new_plex(tokens, token);
			if (p == NULL) {
				warnx("line %d: invalid plex definition", line);
				warnx("line %d: '%s'", line, original);
				errors++;
				line++;
				continue;
			}

			/* Reset subdisk count for this plex. */
			sd_in_plex = 0;

			/* Default name. */
			if (strlen(p->name) == 0) {
				snprintf(p->name, sizeof(p->name), "%s.p%d",
				    volume, plex_in_volume++);
			}

			/* Default volume. */
			if (strlen(p->volume) == 0) {
				snprintf(p->volume, sizeof(p->volume), "%s",
				    volume);
			}

			/*
			 * Set default plex name for following subdisk
			 * definitions.
			 */
			strlcpy(plex, p->name, sizeof(plex));

			snprintf(buf1, sizeof(buf1), "plex%d", plexes);
			gctl_ro_param(req, buf1, sizeof(*p), p);
			plexes++;

		/* Subdisk definition. */
		} else if (!strcmp(token[0], "sd")) {
			s = gv_new_sd(tokens, token);
			if (s == NULL) {
				warnx("line %d: invalid subdisk "
				    "definition:", line);
				warnx("line %d: '%s'", line, original);
				errors++;
				line++;
				continue;
			}

			/* Default name. */
			if (strlen(s->name) == 0) {
				if (strlen(plex) == 0) {
					sdname = find_name("gvinumsubdisk.p",
					    GV_TYPE_SD, GV_MAXSDNAME);
					snprintf(s->name, sizeof(s->name),
					    "%s.s%d", sdname, undeffd++);
					free(sdname);
				} else {
					snprintf(s->name, sizeof(s->name),
					    "%s.s%d",plex, sd_in_plex++);
				}
			}

			/* Default plex. */
			if (strlen(s->plex) == 0)
				snprintf(s->plex, sizeof(s->plex), "%s", plex);

			snprintf(buf1, sizeof(buf1), "sd%d", subdisks);
			gctl_ro_param(req, buf1, sizeof(*s), s);
			subdisks++;

		/* Subdisk definition. */
		} else if (!strcmp(token[0], "drive")) {
			d = gv_new_drive(tokens, token);
			if (d == NULL) {
				warnx("line %d: invalid drive definition:",
				    line);
				warnx("line %d: '%s'", line, original);
				errors++;
				line++;
				continue;
			}

			snprintf(buf1, sizeof(buf1), "drive%d", drives);
			gctl_ro_param(req, buf1, sizeof(*d), d);
			drives++;

		/* Everything else is bogus. */
		} else {
			warnx("line %d: invalid definition:", line);
			warnx("line %d: '%s'", line, original);
			errors++;
		}
		line++;
	}

	fclose(tmp);
	unlink(tmpfile);

	if (!errors && (volumes || plexes || subdisks || drives)) {
		gctl_ro_param(req, "volumes", sizeof(int), &volumes);
		gctl_ro_param(req, "plexes", sizeof(int), &plexes);
		gctl_ro_param(req, "subdisks", sizeof(int), &subdisks);
		gctl_ro_param(req, "drives", sizeof(int), &drives);
		errstr = gctl_issue(req);
		if (errstr != NULL)
			warnx("create failed: %s", errstr);
	}
	gctl_free(req);
}

/* Create a concatenated volume. */
static void
gvinum_concat(int argc, char * const *argv)
{

	if (argc < 2) {
		warnx("usage:\tconcat [-fv] [-n name] drives\n");
		return;
	}
	create_volume(argc, argv, "concat");
}

/* Create a drive quick and dirty. */
static char *
create_drive(const char *device)
{
	struct gv_drive *d;
	struct gctl_req *req;
	const char *errstr;
	char *drivename, *dname;
	int drives, i, flags, volumes, subdisks, plexes;
	int found = 0;

	flags = plexes = subdisks = volumes = 0;
	drives = 1;
	dname = NULL;

	drivename = find_drive();
	if (drivename == NULL)
		return (NULL);

	req = gctl_get_handle();
	gctl_ro_param(req, "class", -1, "VINUM");
	gctl_ro_param(req, "verb", -1, "create");
	d = gv_alloc_drive();
	if (d == NULL)
		err(1, "unable to allocate for gv_drive object");

	strlcpy(d->name, drivename, sizeof(d->name));
	copy_device(d, device);
	gctl_ro_param(req, "drive0", sizeof(*d), d);
	gctl_ro_param(req, "flags", sizeof(int), &flags);
	gctl_ro_param(req, "drives", sizeof(int), &drives);
	gctl_ro_param(req, "volumes", sizeof(int), &volumes);
	gctl_ro_param(req, "plexes", sizeof(int), &plexes);
	gctl_ro_param(req, "subdisks", sizeof(int), &subdisks);
	errstr = gctl_issue(req);
	if (errstr != NULL) {
		warnx("error creating drive: %s", errstr);
		drivename = NULL;
	} else {
		/* XXX: This is needed because we have to make sure the drives
		 * are created before we return. */
		/* Loop until it's in the config. */
		for (i = 0; i < 100000; i++) {
			dname = find_name("gvinumdrive", GV_TYPE_DRIVE,
			    GV_MAXDRIVENAME);
			/* If we got a different name, quit. */
			if (dname == NULL)
				continue;
			if (strcmp(dname, drivename))
				found = 1;
			free(dname);
			dname = NULL;
			if (found)
				break;
			usleep(100000); /* Sleep for 0.1s */
		}
		if (found == 0) {
			warnx("error creating drive");
			drivename = NULL;
		}
	}
	gctl_free(req);
	return (drivename);
}

/*
 * General routine for creating a volume. Mainly for use by concat, mirror,
 * raid5 and stripe commands.
 */
static void
create_volume(int argc, char * const *argv, const char *verb)
{
	struct gctl_req *req;
	const char *errstr;
	char buf[BUFSIZ], *drivename, *volname;
	int drives, flags, i;
	off_t stripesize;

	flags = 0;
	drives = 0;
	volname = NULL;
	stripesize = 262144;

	/* XXX: Should we check for argument length? */

	req = gctl_get_handle();
	gctl_ro_param(req, "class", -1, "VINUM");

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-f")) {
			flags |= GV_FLAG_F;
		} else if (!strcmp(argv[i], "-n")) {
			volname = argv[++i];
		} else if (!strcmp(argv[i], "-v")) {
			flags |= GV_FLAG_V;
		} else if (!strcmp(argv[i], "-s")) {
			flags |= GV_FLAG_S;
			if (!strcmp(verb, "raid5"))
				stripesize = gv_sizespec(argv[++i]);
		} else {
			/* Assume it's a drive. */
			snprintf(buf, sizeof(buf), "drive%d", drives++);

			/* First we create the drive. */
			drivename = create_drive(argv[i]);
			if (drivename == NULL)
				goto bad;
			/* Then we add it to the request. */
			gctl_ro_param(req, buf, -1, drivename);
		}
	}

	gctl_ro_param(req, "stripesize", sizeof(off_t), &stripesize);

	/* Find a free volume name. */
	if (volname == NULL)
		volname = find_name("gvinumvolume", GV_TYPE_VOL, GV_MAXVOLNAME);

	/* Then we send a request to actually create the volumes. */
	gctl_ro_param(req, "verb", -1, verb);
	gctl_ro_param(req, "flags", sizeof(int), &flags);
	gctl_ro_param(req, "drives", sizeof(int), &drives);
	gctl_ro_param(req, "name", -1, volname);
	errstr = gctl_issue(req);
	if (errstr != NULL)
		warnx("creating %s volume failed: %s", verb, errstr);
bad:
	gctl_free(req);
}

/* Parse a line of the config, return the word after <pattern>. */
static const char *
find_pattern(char *line, const char *pattern)
{
	char *ptr;

	ptr = strsep(&line, " ");
	while (ptr != NULL) {
		if (!strcmp(ptr, pattern)) {
			/* Return the next. */
			ptr = strsep(&line, " ");
			return (ptr);
		}
		ptr = strsep(&line, " ");
	}
	return (NULL);
}

/* Find a free name for an object given a prefix. */
static char *
find_name(const char *prefix, int type, int namelen)
{
	struct gctl_req *req;
	char comment[1], buf[GV_CFG_LEN - 1], *sname, *ptr;
	const char *errstr, *name;
	int i, n, begin, len, conflict;
	char line[1024];

	comment[0] = '\0';

	/* Find a name. Fetch out configuration first. */
	req = gctl_get_handle();
	gctl_ro_param(req, "class", -1, "VINUM");
	gctl_ro_param(req, "verb", -1, "getconfig");
	gctl_ro_param(req, "comment", -1, comment);
	gctl_rw_param(req, "config", sizeof(buf), buf);
	errstr = gctl_issue(req);
	if (errstr != NULL) {
		warnx("can't get configuration: %s", errstr);
		return (NULL);
	}
	gctl_free(req);

	begin = 0;
	len = strlen(buf);
	i = 0;
	sname = malloc(namelen + 1);

	/* XXX: Max object setting? */
	for (n = 0; n < 10000; n++) {
		snprintf(sname, namelen, "%s%d", prefix, n);
		conflict = 0;
		begin = 0;
		/* Loop through the configuration line by line. */
		for (i = 0; i < len; i++) {
			if (buf[i] == '\n' || buf[i] == '\0') {
				ptr = buf + begin;
				strlcpy(line, ptr, (i - begin) + 1);
				begin = i + 1;
				switch (type) {
				case GV_TYPE_DRIVE:
					name = find_pattern(line, "drive");
					break;
				case GV_TYPE_VOL:
					name = find_pattern(line, "volume");
					break;
				case GV_TYPE_PLEX:
				case GV_TYPE_SD:
					name = find_pattern(line, "name");
					break;
				default:
					printf("Invalid type given\n");
					continue;
				}
				if (name == NULL)
					continue;
				if (!strcmp(sname, name)) {
					conflict = 1;
					/* XXX: Could quit the loop earlier. */
				}
			}
		}
		if (!conflict)
			return (sname);
	}
	free(sname);
	return (NULL);
}

static void
copy_device(struct gv_drive *d, const char *device)
{

	if (strncmp(device, "/dev/", 5) == 0)
		strlcpy(d->device, (device + 5), sizeof(d->device));
	else
		strlcpy(d->device, device, sizeof(d->device));
}

/* Detach a plex or subdisk from its parent. */
static void
gvinum_detach(int argc, char * const *argv)
{
	const char *errstr;
	struct gctl_req *req;
	int flags, i;

	flags = 0;
	optreset = 1;
	optind = 1;
	while ((i = getopt(argc, argv, "f")) != -1) {
		switch (i) {
		case 'f':
			flags |= GV_FLAG_F;
			break;
		default:
			warn("invalid flag: %c", i);
			return;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1) {
		warnx("usage: detach [-f] <subdisk> | <plex>");
		return;
	}

	req = gctl_get_handle();
	gctl_ro_param(req, "class", -1, "VINUM");
	gctl_ro_param(req, "verb", -1, "detach");
	gctl_ro_param(req, "object", -1, argv[0]);
	gctl_ro_param(req, "flags", sizeof(int), &flags);

	errstr = gctl_issue(req);
	if (errstr != NULL)
		warnx("detach failed: %s", errstr);
	gctl_free(req);
}

static void
gvinum_help(void)
{

	printf("COMMANDS\n"
	    "checkparity [-f] plex\n"
	    "        Check the parity blocks of a RAID-5 plex.\n"
	    "create [-f] description-file\n"
	    "        Create as per description-file or open editor.\n"
	    "attach plex volume [rename]\n"
	    "attach subdisk plex [offset] [rename]\n"
	    "        Attach a plex to a volume, or a subdisk to a plex\n"
	    "concat [-fv] [-n name] drives\n"
	    "        Create a concatenated volume from the specified drives.\n"
	    "detach [-f] [plex | subdisk]\n"
	    "        Detach a plex or a subdisk from the volume or plex to\n"
	    "        which it is attached.\n"
	    "grow plex drive\n"
	    "        Grow plex by creating a properly sized subdisk on drive\n"
	    "l | list [-r] [-v] [-V] [volume | plex | subdisk]\n"
	    "        List information about specified objects.\n"
	    "ld [-r] [-v] [-V] [volume]\n"
	    "        List information about drives.\n"
	    "ls [-r] [-v] [-V] [subdisk]\n"
	    "        List information about subdisks.\n"
	    "lp [-r] [-v] [-V] [plex]\n"
	    "        List information about plexes.\n"
	    "lv [-r] [-v] [-V] [volume]\n"
	    "        List information about volumes.\n"
	    "mirror [-fsv] [-n name] drives\n"
	    "        Create a mirrored volume from the specified drives.\n"
	    "move | mv -f drive object ...\n"
	    "        Move the object(s) to the specified drive.\n"
	    "quit    Exit the vinum program when running in interactive mode."
	    "  Nor-\n"
	    "        mally this would be done by entering the EOF character.\n"
	    "raid5 [-fv] [-s stripesize] [-n name] drives\n"
	    "        Create a RAID-5 volume from the specified drives.\n"
	    "rename [-r] [drive | subdisk | plex | volume] newname\n"
	    "        Change the name of the specified object.\n"
	    "rebuildparity plex [-f]\n"
	    "        Rebuild the parity blocks of a RAID-5 plex.\n"
	    "resetconfig [-f]\n"
	    "        Reset the complete gvinum configuration\n"
	    "rm [-r] [-f] volume | plex | subdisk | drive\n"
	    "        Remove an object.\n"
	    "saveconfig\n"
	    "        Save vinum configuration to disk after configuration"
	    " failures.\n"
	    "setstate [-f] state [volume | plex | subdisk | drive]\n"
	    "        Set state without influencing other objects, for"
	    " diagnostic pur-\n"
	    "        poses only.\n"
	    "start [-S size] volume | plex | subdisk\n"
	    "        Allow the system to access the objects.\n"
	    "stripe [-fv] [-n name] drives\n"
	    "        Create a striped volume from the specified drives.\n"
	);
}

static void
gvinum_setstate(int argc, char * const *argv)
{
	struct gctl_req *req;
	int flags, i;
	const char *errstr;

	flags = 0;

	optreset = 1;
	optind = 1;

	while ((i = getopt(argc, argv, "f")) != -1) {
		switch (i) {
		case 'f':
			flags |= GV_FLAG_F;
			break;
		case '?':
		default:
			warn("invalid flag: %c", i);
			return;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 2) {
		warnx("usage: setstate [-f] <state> <obj>");
		return;
	}

	/*
	 * XXX: This hack is needed to avoid tripping over (now) invalid
	 * 'classic' vinum states and will go away later.
	 */
	if (strcmp(argv[0], "up") && strcmp(argv[0], "down") &&
	    strcmp(argv[0], "stale")) {
		warnx("invalid state '%s'", argv[0]);
		return;
	}

	req = gctl_get_handle();
	gctl_ro_param(req, "class", -1, "VINUM");
	gctl_ro_param(req, "verb", -1, "setstate");
	gctl_ro_param(req, "state", -1, argv[0]);
	gctl_ro_param(req, "object", -1, argv[1]);
	gctl_ro_param(req, "flags", sizeof(int), &flags);

	errstr = gctl_issue(req);
	if (errstr != NULL)
		warnx("%s", errstr);
	gctl_free(req);
}

static void
gvinum_list(int argc, char * const *argv)
{
	struct gctl_req *req;
	int flags, i, j;
	const char *errstr;
	char buf[20], config[GV_CFG_LEN + 1];
	const char *cmd;

	flags = 0;
	cmd = "list";

	if (argc) {
		optreset = 1;
		optind = 1;
		cmd = argv[0];
		while ((j = getopt(argc, argv, "rsvV")) != -1) {
			switch (j) {
			case 'r':
				flags |= GV_FLAG_R;
				break;
			case 's':
				flags |= GV_FLAG_S;
				break;
			case 'v':
				flags |= GV_FLAG_V;
				break;
			case 'V':
				flags |= GV_FLAG_V;
				flags |= GV_FLAG_VV;
				break;
			case '?':
			default:
				return;
			}
		}
		argc -= optind;
		argv += optind;

	}

	req = gctl_get_handle();
	gctl_ro_param(req, "class", -1, "VINUM");
	gctl_ro_param(req, "verb", -1, "list");
	gctl_ro_param(req, "cmd", -1, cmd);
	gctl_ro_param(req, "argc", sizeof(int), &argc);
	gctl_ro_param(req, "flags", sizeof(int), &flags);
	gctl_rw_param(req, "config", sizeof(config), config);
	if (argc) {
		for (i = 0; i < argc; i++) {
			snprintf(buf, sizeof(buf), "argv%d", i);
			gctl_ro_param(req, buf, -1, argv[i]);
		}
	}
	errstr = gctl_issue(req);
	if (errstr != NULL) {
		warnx("can't get configuration: %s", errstr);
		gctl_free(req);
		return;
	}

	printf("%s", config);
	gctl_free(req);
}

/* Create a mirrored volume. */
static void
gvinum_mirror(int argc, char * const *argv)
{

	if (argc < 2) {
		warnx("usage\tmirror [-fsv] [-n name] drives\n");
		return;
	}
	create_volume(argc, argv, "mirror");
}

/* Note that move is currently of form '[-r] target object [...]' */
static void
gvinum_move(int argc, char * const *argv)
{
	struct gctl_req *req;
	const char *errstr;
	char buf[20];
	int flags, i, j;

	flags = 0;
	if (argc) {
		optreset = 1;
		optind = 1;
		while ((j = getopt(argc, argv, "f")) != -1) {
			switch (j) {
			case 'f':
				flags |= GV_FLAG_F;
				break;
			case '?':
			default:
				return;
			}
		}
		argc -= optind;
		argv += optind;
	}

	switch (argc) {
		case 0:
			warnx("no destination or object(s) to move specified");
			return;
		case 1:
			warnx("no object(s) to move specified");
			return;
		default:
			break;
	}

	req = gctl_get_handle();
	gctl_ro_param(req, "class", -1, "VINUM");
	gctl_ro_param(req, "verb", -1, "move");
	gctl_ro_param(req, "argc", sizeof(int), &argc);
	gctl_ro_param(req, "flags", sizeof(int), &flags);
	gctl_ro_param(req, "destination", -1, argv[0]);
	for (i = 1; i < argc; i++) {
		snprintf(buf, sizeof(buf), "argv%d", i);
		gctl_ro_param(req, buf, -1, argv[i]);
	}
	errstr = gctl_issue(req);
	if (errstr != NULL)
		warnx("can't move object(s):  %s", errstr);
	gctl_free(req);
}

static void
gvinum_printconfig(int argc __unused, char * const *argv __unused)
{

	printconfig(stdout, "");
}

static void
gvinum_parityop(int argc, char * const *argv, int rebuild)
{
	struct gctl_req *req;
	int flags, i;
	const char *errstr;
	const char *op;

	if (rebuild) {
		op = "rebuildparity";
	} else {
		op = "checkparity";
	}

	optreset = 1;
	optind = 1;
	flags = 0;
	while ((i = getopt(argc, argv, "fv")) != -1) {
		switch (i) {
		case 'f':
			flags |= GV_FLAG_F;
			break;
		case 'v':
			flags |= GV_FLAG_V;
			break;
		default:
			warnx("invalid flag '%c'", i);
			return;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1) {
		warn("usage: %s [-f] [-v] <plex>", op);
		return;
	}

	req = gctl_get_handle();
	gctl_ro_param(req, "class", -1, "VINUM");
	gctl_ro_param(req, "verb", -1, op);
	gctl_ro_param(req, "rebuild", sizeof(int), &rebuild);
	gctl_ro_param(req, "flags", sizeof(int), &flags);
	gctl_ro_param(req, "plex", -1, argv[0]);

	errstr = gctl_issue(req);
	if (errstr)
		warnx("%s\n", errstr);
	gctl_free(req);
}

/* Create a RAID-5 volume. */
static void
gvinum_raid5(int argc, char * const *argv)
{

	if (argc < 2) {
		warnx("usage:\traid5 [-fv] [-s stripesize] [-n name] drives\n");
		return;
	}
	create_volume(argc, argv, "raid5");
}

static void
gvinum_rename(int argc, char * const *argv)
{
	struct gctl_req *req;
	const char *errstr;
	int flags, j;

	flags = 0;

	if (argc) {
		optreset = 1;
		optind = 1;
		while ((j = getopt(argc, argv, "r")) != -1) {
			switch (j) {
			case 'r':
				flags |= GV_FLAG_R;
				break;
			default:
				return;
			}
		}
		argc -= optind;
		argv += optind;
	}

	switch (argc) {
		case 0:
			warnx("no object to rename specified");
			return;
		case 1:
			warnx("no new name specified");
			return;
		case 2:
			break;
		default:
			warnx("more than one new name specified");
			return;
	}

	req = gctl_get_handle();
	gctl_ro_param(req, "class", -1, "VINUM");
	gctl_ro_param(req, "verb", -1, "rename");
	gctl_ro_param(req, "flags", sizeof(int), &flags);
	gctl_ro_param(req, "object", -1, argv[0]);
	gctl_ro_param(req, "newname", -1, argv[1]);
	errstr = gctl_issue(req);
	if (errstr != NULL)
		warnx("can't rename object:  %s", errstr);
	gctl_free(req);
}

static void
gvinum_rm(int argc, char * const *argv)
{
	struct gctl_req *req;
	int flags, i, j;
	const char *errstr;
	char buf[20];

	flags = 0;
	optreset = 1;
	optind = 1;
	while ((j = getopt(argc, argv, "rf")) != -1) {
		switch (j) {
		case 'f':
			flags |= GV_FLAG_F;
			break;
		case 'r':
			flags |= GV_FLAG_R;
			break;
		default:
			return;
		}
	}
	argc -= optind;
	argv += optind;

	req = gctl_get_handle();
	gctl_ro_param(req, "class", -1, "VINUM");
	gctl_ro_param(req, "verb", -1, "remove");
	gctl_ro_param(req, "argc", sizeof(int), &argc);
	gctl_ro_param(req, "flags", sizeof(int), &flags);
	if (argc) {
		for (i = 0; i < argc; i++) {
			snprintf(buf, sizeof(buf), "argv%d", i);
			gctl_ro_param(req, buf, -1, argv[i]);
		}
	}
	errstr = gctl_issue(req);
	if (errstr != NULL) {
		warnx("can't remove: %s", errstr);
		gctl_free(req);
		return;
	}
	gctl_free(req);
}

static void
gvinum_resetconfig(int argc, char * const *argv)
{
	struct gctl_req *req;
	const char *errstr;
	char reply[32];
	int flags, i;

	flags = 0;
	while ((i = getopt(argc, argv, "f")) != -1) {
		switch (i) {
		case 'f':
			flags |= GV_FLAG_F;
			break;
		default:
			warn("invalid flag: %c", i);
			return;
		}
	}
	if ((flags & GV_FLAG_F) == 0) {
		if (!isatty(STDIN_FILENO)) {
			warn("Please enter this command from a tty device\n");
			return;
		}
		printf(" WARNING!  This command will completely wipe out"
		    " your gvinum configuration.\n"
		    " All data will be lost.  If you really want to do this,"
		    " enter the text\n\n"
		    " NO FUTURE\n"
		    " Enter text -> ");
		fgets(reply, sizeof(reply), stdin);
		if (strcmp(reply, "NO FUTURE\n")) {
			printf("\n No change\n");
			return;
		}
	}
	req = gctl_get_handle();
	gctl_ro_param(req, "class", -1, "VINUM");
	gctl_ro_param(req, "verb", -1, "resetconfig");
	errstr = gctl_issue(req);
	if (errstr != NULL) {
		warnx("can't reset config: %s", errstr);
		gctl_free(req);
		return;
	}
	gctl_free(req);
	printf("gvinum configuration obliterated\n");
}

static void
gvinum_saveconfig(void)
{
	struct gctl_req *req;
	const char *errstr;

	req = gctl_get_handle();
	gctl_ro_param(req, "class", -1, "VINUM");
	gctl_ro_param(req, "verb", -1, "saveconfig");
	errstr = gctl_issue(req);
	if (errstr != NULL)
		warnx("can't save configuration: %s", errstr);
	gctl_free(req);
}

static void
gvinum_start(int argc, char * const *argv)
{
	struct gctl_req *req;
	int i, initsize, j;
	const char *errstr;
	char buf[20];

	/* 'start' with no arguments is a no-op. */
	if (argc == 1)
		return;

	initsize = 0;

	optreset = 1;
	optind = 1;
	while ((j = getopt(argc, argv, "S")) != -1) {
		switch (j) {
		case 'S':
			initsize = atoi(optarg);
			break;
		default:
			return;
		}
	}
	argc -= optind;
	argv += optind;

	if (!initsize)
		initsize = 512;

	req = gctl_get_handle();
	gctl_ro_param(req, "class", -1, "VINUM");
	gctl_ro_param(req, "verb", -1, "start");
	gctl_ro_param(req, "argc", sizeof(int), &argc);
	gctl_ro_param(req, "initsize", sizeof(int), &initsize);
	if (argc) {
		for (i = 0; i < argc; i++) {
			snprintf(buf, sizeof(buf), "argv%d", i);
			gctl_ro_param(req, buf, -1, argv[i]);
		}
	}
	errstr = gctl_issue(req);
	if (errstr != NULL) {
		warnx("can't start: %s", errstr);
		gctl_free(req);
		return;
	}

	gctl_free(req);
}

static void
gvinum_stop(int argc __unused, char * const *argv __unused)
{
	int err, fileid;

	fileid = kldfind(GVINUMKLD);
	if (fileid == -1) {
		if (modfind(GVINUMMOD) < 0)
			warn("cannot find " GVINUMKLD);
		return;
	}

	/*
	 * This little hack prevents that we end up in an infinite loop in
	 * g_unload_class().  gv_unload() will return EAGAIN so that the GEOM
	 * event thread will be free for the g_wither_geom() call from
	 * gv_unload().  It's silly, but it works.
	 */
	printf("unloading " GVINUMKLD " kernel module... ");
	fflush(stdout);
	if ((err = kldunload(fileid)) != 0 && (errno == EAGAIN)) {
		sleep(1);
		err = kldunload(fileid);
	}
	if (err != 0) {
		printf(" failed!\n");
		warn("cannot unload " GVINUMKLD);
		return;
	}

	printf("done\n");
	exit(0);
}

/* Create a striped volume. */
static void
gvinum_stripe(int argc, char * const *argv)
{

	if (argc < 2) {
		warnx("usage:\tstripe [-fv] [-n name] drives\n");
		return;
	}
	create_volume(argc, argv, "stripe");
}

/* Grow a subdisk by adding disk backed by provider. */
static void
gvinum_grow(int argc, char * const *argv)
{
	struct gctl_req *req;
	char *drive, *sdname;
	char sdprefix[GV_MAXSDNAME];
	struct gv_drive *d;
	struct gv_sd *s;
	const char *errstr;
	int drives, volumes, plexes, subdisks, flags;

	flags = 0;
	drives = volumes = plexes = subdisks = 0;
	if (argc < 3) {
		warnx("usage:\tgrow plex drive\n");
		return;
	}

	s = gv_alloc_sd();
	if (s == NULL) {
		warn("unable to create subdisk");
		return;
	}
	d = gv_alloc_drive();
	if (d == NULL) {
		warn("unable to create drive");
		free(s);
		return;
	}
	/* Lookup device and set an appropriate drive name. */
	drive = find_drive();
	if (drive == NULL) {
		warn("unable to find an appropriate drive name");
		free(s);
		free(d);
		return;
	}
	strlcpy(d->name, drive, sizeof(d->name));
	copy_device(d, argv[2]);

	drives = 1;

	/* We try to use the plex name as basis for the subdisk name. */
	snprintf(sdprefix, sizeof(sdprefix), "%s.s", argv[1]);
	sdname = find_name(sdprefix, GV_TYPE_SD, GV_MAXSDNAME);
	if (sdname == NULL) {
		warn("unable to find an appropriate subdisk name");
		free(s);
		free(d);
		free(drive);
		return;
	}
	strlcpy(s->name, sdname, sizeof(s->name));
	free(sdname);
	strlcpy(s->plex, argv[1], sizeof(s->plex));
	strlcpy(s->drive, d->name, sizeof(s->drive));
	subdisks = 1;

	req = gctl_get_handle();
	gctl_ro_param(req, "class", -1, "VINUM");
	gctl_ro_param(req, "verb", -1, "create");
	gctl_ro_param(req, "flags", sizeof(int), &flags);
	gctl_ro_param(req, "volumes", sizeof(int), &volumes);
	gctl_ro_param(req, "plexes", sizeof(int), &plexes);
	gctl_ro_param(req, "subdisks", sizeof(int), &subdisks);
	gctl_ro_param(req, "drives", sizeof(int), &drives);
	gctl_ro_param(req, "drive0", sizeof(*d), d);
	gctl_ro_param(req, "sd0", sizeof(*s), s);
	errstr = gctl_issue(req);
	free(drive);
	if (errstr != NULL) {
		warnx("unable to grow plex: %s", errstr);
		free(s);
		free(d);
		return;
	}
	gctl_free(req);
}

static void
parseline(int argc, char * const *argv)
{

	if (argc <= 0)
		return;

	if (!strcmp(argv[0], "create"))
		gvinum_create(argc, argv);
	else if (!strcmp(argv[0], "exit") || !strcmp(argv[0], "quit"))
		exit(0);
	else if (!strcmp(argv[0], "attach"))
		gvinum_attach(argc, argv);
	else if (!strcmp(argv[0], "detach"))
		gvinum_detach(argc, argv);
	else if (!strcmp(argv[0], "concat"))
		gvinum_concat(argc, argv);
	else if (!strcmp(argv[0], "grow"))
		gvinum_grow(argc, argv);
	else if (!strcmp(argv[0], "help"))
		gvinum_help();
	else if (!strcmp(argv[0], "list") || !strcmp(argv[0], "l"))
		gvinum_list(argc, argv);
	else if (!strcmp(argv[0], "ld"))
		gvinum_list(argc, argv);
	else if (!strcmp(argv[0], "lp"))
		gvinum_list(argc, argv);
	else if (!strcmp(argv[0], "ls"))
		gvinum_list(argc, argv);
	else if (!strcmp(argv[0], "lv"))
		gvinum_list(argc, argv);
	else if (!strcmp(argv[0], "mirror"))
		gvinum_mirror(argc, argv);
	else if (!strcmp(argv[0], "move"))
		gvinum_move(argc, argv);
	else if (!strcmp(argv[0], "mv"))
		gvinum_move(argc, argv);
	else if (!strcmp(argv[0], "printconfig"))
		gvinum_printconfig(argc, argv);
	else if (!strcmp(argv[0], "raid5"))
		gvinum_raid5(argc, argv);
	else if (!strcmp(argv[0], "rename"))
		gvinum_rename(argc, argv);
	else if (!strcmp(argv[0], "resetconfig"))
		gvinum_resetconfig(argc, argv);
	else if (!strcmp(argv[0], "rm"))
		gvinum_rm(argc, argv);
	else if (!strcmp(argv[0], "saveconfig"))
		gvinum_saveconfig();
	else if (!strcmp(argv[0], "setstate"))
		gvinum_setstate(argc, argv);
	else if (!strcmp(argv[0], "start"))
		gvinum_start(argc, argv);
	else if (!strcmp(argv[0], "stop"))
		gvinum_stop(argc, argv);
	else if (!strcmp(argv[0], "stripe"))
		gvinum_stripe(argc, argv);
	else if (!strcmp(argv[0], "checkparity"))
		gvinum_parityop(argc, argv, 0);
	else if (!strcmp(argv[0], "rebuildparity"))
		gvinum_parityop(argc, argv, 1);
	else
		printf("unknown command '%s'\n", argv[0]);
}

/*
 * The guts of printconfig.  This is called from gvinum_printconfig and from
 * gvinum_create when called without an argument, in order to give the user
 * something to edit.
 */
static void
printconfig(FILE *of, const char *comment)
{
	struct gctl_req *req;
	struct utsname uname_s;
	const char *errstr;
	time_t now;
	char buf[GV_CFG_LEN + 1];
	
	uname(&uname_s);
	time(&now);

	req = gctl_get_handle();
	gctl_ro_param(req, "class", -1, "VINUM");
	gctl_ro_param(req, "verb", -1, "getconfig");
	gctl_ro_param(req, "comment", -1, comment);
	gctl_rw_param(req, "config", sizeof(buf), buf);
	errstr = gctl_issue(req);
	if (errstr != NULL) {
		warnx("can't get configuration: %s", errstr);
		return;
	}
	gctl_free(req);

	fprintf(of, "# Vinum configuration of %s, saved at %s",
	    uname_s.nodename,
	    ctime(&now));
	
	if (*comment != '\0')
	    fprintf(of, "# Current configuration:\n");

	fprintf(of, "%s", buf);
}
