/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Kyle J. Kneitinger <kyle@kneit.in>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mount.h>
#include <errno.h>
#include <libutil.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#include <be.h>

#include "bectl.h"

static int bectl_cmd_activate(int argc, char *argv[]);
static int bectl_cmd_create(int argc, char *argv[]);
static int bectl_cmd_destroy(int argc, char *argv[]);
static int bectl_cmd_export(int argc, char *argv[]);
static int bectl_cmd_import(int argc, char *argv[]);
#if SOON
static int bectl_cmd_add(int argc, char *argv[]);
#endif
static int bectl_cmd_mount(int argc, char *argv[]);
static int bectl_cmd_rename(int argc, char *argv[]);
static int bectl_cmd_unmount(int argc, char *argv[]);

libbe_handle_t *be;

int
usage(bool explicit)
{
	FILE *fp;

	fp =  explicit ? stdout : stderr;
	fprintf(fp, "%s",
	    "usage:\tbectl {-h | -? | subcommand [args...]}\n"
#if SOON
	    "\tbectl add (path)*\n"
#endif
	    "\tbectl activate [-t] beName\n"
	    "\tbectl create [-r] [-e {nonActiveBe | beName@snapshot}] beName\n"
	    "\tbectl create [-r] beName@snapshot\n"
	    "\tbectl destroy [-F] {beName | beName@snapshot}\n"
	    "\tbectl export sourceBe\n"
	    "\tbectl import targetBe\n"
	    "\tbectl jail {-b | -U} [{-o key=value | -u key}]... "
	    "{jailID | jailName}\n"
	    "\t      bootenv [utility [argument ...]]\n"
	    "\tbectl list [-DHas]\n"
	    "\tbectl mount beName [mountpoint]\n"
	    "\tbectl rename origBeName newBeName\n"
	    "\tbectl {ujail | unjail} {jailID | jailName} bootenv\n"
	    "\tbectl {umount | unmount} [-f] beName\n");

	return (explicit ? 0 : EX_USAGE);
}


/*
 * Represents a relationship between the command name and the parser action
 * that handles it.
 */
struct command_map_entry {
	const char *command;
	int (*fn)(int argc, char *argv[]);
};

static struct command_map_entry command_map[] =
{
	{ "activate", bectl_cmd_activate },
	{ "create",   bectl_cmd_create   },
	{ "destroy",  bectl_cmd_destroy  },
	{ "export",   bectl_cmd_export   },
	{ "import",   bectl_cmd_import   },
#if SOON
	{ "add",      bectl_cmd_add      },
#endif
	{ "jail",     bectl_cmd_jail     },
	{ "list",     bectl_cmd_list     },
	{ "mount",    bectl_cmd_mount    },
	{ "rename",   bectl_cmd_rename   },
	{ "unjail",   bectl_cmd_unjail   },
	{ "unmount",  bectl_cmd_unmount  },
};

static int
get_cmd_index(const char *cmd, int *idx)
{
	int map_size;

	map_size = nitems(command_map);
	for (int i = 0; i < map_size; ++i) {
		if (strcmp(cmd, command_map[i].command) == 0) {
			*idx = i;
			return (0);
		}
	}

	return (1);
}


static int
bectl_cmd_activate(int argc, char *argv[])
{
	int err, opt;
	bool temp;

	temp = false;
	while ((opt = getopt(argc, argv, "t")) != -1) {
		switch (opt) {
		case 't':
			temp = true;
			break;
		default:
			fprintf(stderr, "bectl activate: unknown option '-%c'\n",
			    optopt);
			return (usage(false));
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1) {
		fprintf(stderr, "bectl activate: wrong number of arguments\n");
		return (usage(false));
	}


	/* activate logic goes here */
	if ((err = be_activate(be, argv[0], temp)) != 0)
		/* XXX TODO: more specific error msg based on err */
		printf("did not successfully activate boot environment %s\n",
		    argv[0]);
	else
		printf("successfully activated boot environment %s\n", argv[0]);

	if (temp)
		printf("for next boot\n");

	return (err);
}


/*
 * TODO: when only one arg is given, and it contains an "@" the this should
 * create that snapshot
 */
static int
bectl_cmd_create(int argc, char *argv[])
{
	char *atpos, *bootenv, *snapname, *source;
	int err, opt;
	bool recursive;

	snapname = NULL;
	recursive = false;
	while ((opt = getopt(argc, argv, "e:r")) != -1) {
		switch (opt) {
		case 'e':
			snapname = optarg;
			break;
		case 'r':
			recursive = true;
			break;
		default:
			fprintf(stderr, "bectl create: unknown option '-%c'\n",
			    optopt);
			return (usage(false));
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1) {
		fprintf(stderr, "bectl create: wrong number of arguments\n");
		return (usage(false));
	}

	bootenv = *argv;
	if ((atpos = strchr(bootenv, '@')) != NULL) {
		/*
		 * This is the "create a snapshot variant". No new boot
		 * environment is to be created here.
		 */
		*atpos++ = '\0';
		err = be_snapshot(be, bootenv, atpos, recursive, NULL);
	} else if (snapname != NULL) {
		if (strchr(snapname, '@') != NULL)
			err = be_create_from_existing_snap(be, bootenv,
			    snapname);
		else
			err = be_create_from_existing(be, bootenv, snapname);
	} else {
		if ((snapname = strchr(bootenv, '@')) != NULL) {
			*(snapname++) = '\0';
			if ((err = be_snapshot(be, be_active_path(be),
			    snapname, true, NULL)) != BE_ERR_SUCCESS)
				fprintf(stderr, "failed to create snapshot\n");
			asprintf(&source, "%s@%s", be_active_path(be), snapname);
			err = be_create_from_existing_snap(be, bootenv,
			    source);
			return (err);
		} else
			err = be_create(be, bootenv);
	}

	switch (err) {
	case BE_ERR_SUCCESS:
		break;
	default:
		if (atpos != NULL)
			fprintf(stderr,
			    "failed to create a snapshot '%s' of '%s'\n",
			    atpos, bootenv);
		else if (snapname == NULL)
			fprintf(stderr,
			    "failed to create bootenv %s\n", bootenv);
		else
			fprintf(stderr,
			    "failed to create bootenv %s from snapshot %s\n",
			    bootenv, snapname);
	}

	return (err);
}


static int
bectl_cmd_export(int argc, char *argv[])
{
	char *bootenv;

	if (argc == 1) {
		fprintf(stderr, "bectl export: missing boot environment name\n");
		return (usage(false));
	}

	if (argc > 2) {
		fprintf(stderr, "bectl export: extra arguments provided\n");
		return (usage(false));
	}

	bootenv = argv[1];

	if (isatty(STDOUT_FILENO)) {
		fprintf(stderr, "bectl export: must redirect output\n");
		return (EX_USAGE);
	}

	be_export(be, bootenv, STDOUT_FILENO);

	return (0);
}


static int
bectl_cmd_import(int argc, char *argv[])
{
	char *bootenv;
	int err;

	if (argc == 1) {
		fprintf(stderr, "bectl import: missing boot environment name\n");
		return (usage(false));
	}

	if (argc > 2) {
		fprintf(stderr, "bectl import: extra arguments provided\n");
		return (usage(false));
	}

	bootenv = argv[1];

	if (isatty(STDIN_FILENO)) {
		fprintf(stderr, "bectl import: input can not be from terminal\n");
		return (EX_USAGE);
	}

	err = be_import(be, bootenv, STDIN_FILENO);

	return (err);
}

#if SOON
static int
bectl_cmd_add(int argc, char *argv[])
{

	if (argc < 2) {
		fprintf(stderr, "bectl add: must provide at least one path\n");
		return (usage(false));
	}

	for (int i = 1; i < argc; ++i) {
		printf("arg %d: %s\n", i, argv[i]);
		/* XXX TODO catch err */
		be_add_child(be, argv[i], true);
	}

	return (0);
}
#endif

static int
bectl_cmd_destroy(int argc, char *argv[])
{
	nvlist_t *props;
	char *origin, *target, targetds[BE_MAXPATHLEN];
	int err, flags, opt;

	flags = 0;
	while ((opt = getopt(argc, argv, "Fo")) != -1) {
		switch (opt) {
		case 'F':
			flags |= BE_DESTROY_FORCE;
			break;
		case 'o':
			flags |= BE_DESTROY_ORIGIN;
			break;
		default:
			fprintf(stderr, "bectl destroy: unknown option '-%c'\n",
			    optopt);
			return (usage(false));
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1) {
		fprintf(stderr, "bectl destroy: wrong number of arguments\n");
		return (usage(false));
	}

	target = argv[0];

	/* We'll emit a notice if there's an origin to be cleaned up */
	if ((flags & BE_DESTROY_ORIGIN) == 0 && strchr(target, '@') == NULL) {
		if (be_root_concat(be, target, targetds) != 0)
			goto destroy;
		if (be_prop_list_alloc(&props) != 0)
			goto destroy;
		if (be_get_dataset_props(be, targetds, props) != 0) {
			be_prop_list_free(props);
			goto destroy;
		}
		if (nvlist_lookup_string(props, "origin", &origin) == 0)
			fprintf(stderr, "bectl destroy: leaving origin '%s' intact\n",
			    origin);
		be_prop_list_free(props);
	}

destroy:
	err = be_destroy(be, target, flags);

	return (err);
}

static int
bectl_cmd_mount(int argc, char *argv[])
{
	char result_loc[BE_MAXPATHLEN];
	char *bootenv, *mountpoint;
	int err, mntflags;

	/* XXX TODO: Allow shallow */
	mntflags = BE_MNT_DEEP;
	if (argc < 2) {
		fprintf(stderr, "bectl mount: missing argument(s)\n");
		return (usage(false));
	}

	if (argc > 3) {
		fprintf(stderr, "bectl mount: too many arguments\n");
		return (usage(false));
	}

	bootenv = argv[1];
	mountpoint = ((argc == 3) ? argv[2] : NULL);

	err = be_mount(be, bootenv, mountpoint, mntflags, result_loc);

	switch (err) {
	case BE_ERR_SUCCESS:
		printf("successfully mounted %s at %s\n", bootenv, result_loc);
		break;
	default:
		fprintf(stderr,
		    (argc == 3) ? "failed to mount bootenv %s at %s\n" :
		    "failed to mount bootenv %s at temporary path %s\n",
		    bootenv, mountpoint);
	}

	return (err);
}


static int
bectl_cmd_rename(int argc, char *argv[])
{
	char *dest, *src;
	int err;

	if (argc < 3) {
		fprintf(stderr, "bectl rename: missing argument\n");
		return (usage(false));
	}

	if (argc > 3) {
		fprintf(stderr, "bectl rename: too many arguments\n");
		return (usage(false));
	}

	src = argv[1];
	dest = argv[2];

	err = be_rename(be, src, dest);

	switch (err) {
	case BE_ERR_SUCCESS:
		break;
	default:
		fprintf(stderr, "failed to rename bootenv %s to %s\n",
		    src, dest);
	}

	return (0);
}

static int
bectl_cmd_unmount(int argc, char *argv[])
{
	char *bootenv, *cmd;
	int err, flags, opt;

	/* Store alias used */
	cmd = argv[0];

	flags = 0;
	while ((opt = getopt(argc, argv, "f")) != -1) {
		switch (opt) {
		case 'f':
			flags |= BE_MNT_FORCE;
			break;
		default:
			fprintf(stderr, "bectl %s: unknown option '-%c'\n",
			    cmd, optopt);
			return (usage(false));
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1) {
		fprintf(stderr, "bectl %s: wrong number of arguments\n", cmd);
		return (usage(false));
	}

	bootenv = argv[0];

	err = be_unmount(be, bootenv, flags);

	switch (err) {
	case BE_ERR_SUCCESS:
		break;
	default:
		fprintf(stderr, "failed to unmount bootenv %s\n", bootenv);
	}

	return (err);
}


int
main(int argc, char *argv[])
{
	const char *command;
	char *root;
	int command_index, rc;

	root = NULL;
	if (argc < 2)
		return (usage(false));

	if (strcmp(argv[1], "-r") == 0) {
		if (argc < 4)
			return (usage(false));
		root = strdup(argv[2]);
		command = argv[3];
		argc -= 3;
		argv += 3;
	} else {
		command = argv[1];
		argc -= 1;
		argv += 1;
	}

	/* Handle command aliases */
	if (strcmp(command, "umount") == 0)
		command = "unmount";

	if (strcmp(command, "ujail") == 0)
		command = "unjail";

	if ((strcmp(command, "-?") == 0) || (strcmp(command, "-h") == 0))
		return (usage(true));

	if (get_cmd_index(command, &command_index)) {
		fprintf(stderr, "unknown command: %s\n", command);
		return (usage(false));
	}


	if ((be = libbe_init(root)) == NULL)
		return (-1);

	libbe_print_on_error(be, true);

	rc = command_map[command_index].fn(argc, argv);

	libbe_close(be);
	return (rc);
}
