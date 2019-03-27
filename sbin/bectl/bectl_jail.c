/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Kyle Evans <kevans@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/jail.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <err.h>
#include <jail.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <be.h>
#include "bectl.h"

#define MNTTYPE_ZFS	222

static void jailparam_add(const char *name, const char *val);
static int jailparam_del(const char *name);
static bool jailparam_addarg(char *arg);
static int jailparam_delarg(char *arg);

static int bectl_search_jail_paths(const char *mnt);
static int bectl_locate_jail(const char *ident);
static int bectl_jail_cleanup(char *mountpoint, int jid);

static char mnt_loc[BE_MAXPATHLEN];
static nvlist_t *jailparams;

static const char *disabled_params[] = {
    "command", "exec.start", "nopersist", "persist", NULL
};


static void
jailparam_add(const char *name, const char *val)
{

	nvlist_add_string(jailparams, name, val);
}

static int
jailparam_del(const char *name)
{

	nvlist_remove_all(jailparams, name);
	return (0);
}

static bool
jailparam_addarg(char *arg)
{
	char *name, *val;
	size_t i, len;

	if (arg == NULL)
		return (false);
	name = arg;
	if ((val = strchr(arg, '=')) == NULL) {
		fprintf(stderr, "bectl jail: malformed jail option '%s'\n",
		    arg);
		return (false);
	}

	*val++ = '\0';
	if (strcmp(name, "path") == 0) {
		if (strlen(val) >= BE_MAXPATHLEN) {
			fprintf(stderr,
			    "bectl jail: skipping too long path assignment '%s' (max length = %d)\n",
			    val, BE_MAXPATHLEN);
			return (false);
		}
		strlcpy(mnt_loc, val, sizeof(mnt_loc));
	}

	for (i = 0; disabled_params[i] != NULL; i++) {
		len = strlen(disabled_params[i]);
		if (strncmp(disabled_params[i], name, len) == 0) {
			fprintf(stderr, "invalid jail parameter: %s\n", name);
			return (false);
		}
	}

	jailparam_add(name, val);
	return (true);
}

static int
jailparam_delarg(char *arg)
{
	char *name, *val;

	if (arg == NULL)
		return (EINVAL);
	name = arg;
	if ((val = strchr(name, '=')) != NULL)
		*val++ = '\0';

	if (strcmp(name, "path") == 0)
		*mnt_loc = '\0';
	return (jailparam_del(name));
}

static int
build_jailcmd(char ***argvp, bool interactive, int argc, char *argv[])
{
	char *cmd, **jargv, *name, *val;
	nvpair_t *nvp;
	size_t i, iarg, nargv;

	cmd = NULL;
	nvp = NULL;
	iarg = i = 0;
	if (nvlist_size(jailparams, &nargv, NV_ENCODE_NATIVE) != 0)
		return (1);

	/*
	 * Number of args + "/usr/sbin/jail", "-c", and ending NULL.
	 * If interactive also include command.
	 */
	nargv += 3;
	if (interactive) {
		if (argc == 0)
			nargv++;
		else
			nargv += argc;
	}

	jargv = *argvp = calloc(nargv, sizeof(jargv));
	if (jargv == NULL)
		err(2, "calloc");

	jargv[iarg++] = strdup("/usr/sbin/jail");
	jargv[iarg++] = strdup("-c");
	while ((nvp = nvlist_next_nvpair(jailparams, nvp)) != NULL) {
		name = nvpair_name(nvp);
		if (nvpair_value_string(nvp, &val) != 0)
			continue;

		if (asprintf(&jargv[iarg++], "%s=%s", name, val) < 0)
			goto error;
	}
	if (interactive) {
		if (argc < 1)
			cmd = strdup("/bin/sh");
		else {
			cmd = argv[0];
			argc--;
			argv++;
		}

		if (asprintf(&jargv[iarg++], "command=%s", cmd) < 0) {
			goto error;
		}
		if (argc < 1) {
			free(cmd);
			cmd = NULL;
		}

		for (; argc > 0; argc--) {
			if (asprintf(&jargv[iarg++], "%s", argv[0]) < 0)
				goto error;
			argv++;
		}
	}

	return (0);

error:
	if (interactive && argc < 1)
		free(cmd);
	for (; i < iarg - 1; i++) {
		free(jargv[i]);
	}
	free(jargv);
	return (1);
}

/* Remove jail and cleanup any non zfs mounts. */
static int
bectl_jail_cleanup(char *mountpoint, int jid)
{
	struct statfs *mntbuf;
	size_t i, searchlen, mntsize;

	if (jid >= 0 && jail_remove(jid) != 0) {
		fprintf(stderr, "unable to remove jail");
		return (1);
	}

	searchlen = strnlen(mountpoint, MAXPATHLEN);
	mntsize = getmntinfo(&mntbuf, MNT_NOWAIT);
	for (i = 0; i < mntsize; i++) {
		if (strncmp(mountpoint, mntbuf[i].f_mntonname, searchlen) == 0 &&
		    mntbuf[i].f_type != MNTTYPE_ZFS) {

			if (unmount(mntbuf[i].f_mntonname, 0) != 0) {
				fprintf(stderr, "bectl jail: unable to unmount filesystem %s",
				    mntbuf[i].f_mntonname);
				return (1);
			}
		}
	}

	return (0);
}

int
bectl_cmd_jail(int argc, char *argv[])
{
	char *bootenv, **jargv, *mountpoint;
	int i, jid, mntflags, opt, ret;
	bool default_hostname, interactive, unjail;
	pid_t pid;


	/* XXX TODO: Allow shallow */
	mntflags = BE_MNT_DEEP;
	default_hostname = interactive = unjail = true;

	if ((nvlist_alloc(&jailparams, NV_UNIQUE_NAME, 0)) != 0) {
		fprintf(stderr, "nvlist_alloc() failed\n");
		return (1);
	}

	jailparam_add("persist", "true");
	jailparam_add("allow.mount", "true");
	jailparam_add("allow.mount.devfs", "true");
	jailparam_add("enforce_statfs", "1");

	while ((opt = getopt(argc, argv, "bo:Uu:")) != -1) {
		switch (opt) {
		case 'b':
			interactive = false;
			break;
		case 'o':
			if (jailparam_addarg(optarg)) {
				/*
				 * optarg has been modified to null terminate
				 * at the assignment operator.
				 */
				if (strcmp(optarg, "host.hostname") == 0)
					default_hostname = false;
			} else {
				return (1);
			}
			break;
		case 'U':
			unjail = false;
			break;
		case 'u':
			if ((ret = jailparam_delarg(optarg)) == 0) {
				if (strcmp(optarg, "host.hostname") == 0)
					default_hostname = true;
			} else if (ret != ENOENT) {
				fprintf(stderr,
				    "bectl jail: error unsetting \"%s\"\n",
				    optarg);
				return (ret);
			}
			break;
		default:
			fprintf(stderr, "bectl jail: unknown option '-%c'\n",
			    optopt);
			return (usage(false));
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		fprintf(stderr, "bectl jail: missing boot environment name\n");
		return (usage(false));
	}

	bootenv = argv[0];
	argc--;
	argv++;

	/*
	 * XXX TODO: if its already mounted, perhaps there should be a flag to
	 * indicate its okay to proceed??
	 */
	if (*mnt_loc == '\0')
		mountpoint = NULL;
	else
		mountpoint = mnt_loc;
	if (be_mount(be, bootenv, mountpoint, mntflags, mnt_loc) != BE_ERR_SUCCESS) {
		fprintf(stderr, "could not mount bootenv\n");
		return (1);
	}

	if (default_hostname)
		jailparam_add("host.hostname", bootenv);

	/*
	 * This is our indicator that path was not set by the user, so we'll use
	 * the path that libbe generated for us.
	 */
	if (mountpoint == NULL) {
		jailparam_add("path", mnt_loc);
		mountpoint = mnt_loc;
	}

	if ((build_jailcmd(&jargv, interactive, argc, argv)) != 0) {
		fprintf(stderr, "unable to build argument list for jail command\n");
		return (1);
	}

	pid = fork();

	switch (pid) {
	case -1:
		perror("fork");
		return (1);
	case 0:
		execv("/usr/sbin/jail", jargv);
		fprintf(stderr, "bectl jail: failed to execute\n");
	default:
		waitpid(pid, NULL, 0);
	}

	for (i = 0; jargv[i] != NULL; i++) {
		free(jargv[i]);
	}
	free(jargv);

	if (!interactive)
		return (0);

	if (unjail) {
		/*
		 *  We're not checking the jail id result here because in the
		 *  case of invalid param, or last command in jail was an error
		 *  the jail will not exist upon exit. bectl_jail_cleanup will
		 *  only jail_remove if the jid is >= 0.
		 */
		jid = bectl_locate_jail(bootenv);
		bectl_jail_cleanup(mountpoint, jid);
		be_unmount(be, bootenv, 0);
	}

	return (0);
}

static int
bectl_search_jail_paths(const char *mnt)
{
	int jid;
	char lastjid[16];
	char jailpath[MAXPATHLEN];

	/* jail_getv expects name/value strings */
	snprintf(lastjid, sizeof(lastjid), "%d", 0);

	while ((jid = jail_getv(0, "lastjid", lastjid, "path", &jailpath,
	    NULL)) != -1) {

		/* the jail we've been looking for */
		if (strcmp(jailpath, mnt) == 0)
			return (jid);

		/* update lastjid and keep on looking */
		snprintf(lastjid, sizeof(lastjid), "%d", jid);
	}

	return (-1);
}

/*
 * Locate a jail based on an arbitrary identifier.  This may be either a name,
 * a jid, or a BE name.  Returns the jid or -1 on failure.
 */
static int
bectl_locate_jail(const char *ident)
{
	nvlist_t *belist, *props;
	char *mnt;
	int jid;

	/* Try the easy-match first */
	jid = jail_getid(ident);
	if (jid != -1)
		return (jid);

	/* Attempt to try it as a BE name, first */
	if (be_prop_list_alloc(&belist) != 0)
		return (-1);

	if (be_get_bootenv_props(be, belist) != 0)
		return (-1);

	if (nvlist_lookup_nvlist(belist, ident, &props) == 0) {

		/* path where a boot environment is mounted */
		if (nvlist_lookup_string(props, "mounted", &mnt) == 0) {

			/* looking for a jail that matches our bootenv path */
			jid = bectl_search_jail_paths(mnt);
			be_prop_list_free(belist);
			return (jid);
		}

		be_prop_list_free(belist);
	}

	return (-1);
}

int
bectl_cmd_unjail(int argc, char *argv[])
{
	char path[MAXPATHLEN];
	char *cmd, *name, *target;
	int jid;

	/* Store alias used */
	cmd = argv[0];

	if (argc != 2) {
		fprintf(stderr, "bectl %s: wrong number of arguments\n", cmd);
		return (usage(false));
	}

	target = argv[1];

	/* Locate the jail */
	if ((jid = bectl_locate_jail(target)) == -1) {
		fprintf(stderr, "bectl %s: failed to locate BE by '%s'\n", cmd,
		    target);
		return (1);
	}

	bzero(&path, MAXPATHLEN);
	name = jail_getname(jid);
	if (jail_getv(0, "name", name, "path", path, NULL) != jid) {
		free(name);
		fprintf(stderr,
		    "bectl %s: failed to get path for jail requested by '%s'\n",
		    cmd, target);
		return (1);
	}

	free(name);

	if (be_mounted_at(be, path, NULL) != 0) {
		fprintf(stderr, "bectl %s: jail requested by '%s' not a BE\n",
		    cmd, target);
		return (1);
	}

	bectl_jail_cleanup(path, jid);
	be_unmount(be, target, 0);

	return (0);
}
