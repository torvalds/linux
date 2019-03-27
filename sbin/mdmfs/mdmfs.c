/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Dima Dorfman.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * mdmfs (md/MFS) is a wrapper around mdconfig(8),
 * newfs(8), and mount(8) that mimics the command line option set of
 * the deprecated mount_mfs(8).
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/linker.h>
#include <sys/mdioctl.h>
#include <sys/module.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <inttypes.h>
#include <paths.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

typedef enum { false, true } bool;

struct mtpt_info {
	uid_t		 mi_uid;
	bool		 mi_have_uid;
	gid_t		 mi_gid;
	bool		 mi_have_gid;
	mode_t		 mi_mode;
	bool		 mi_have_mode;
	bool		 mi_forced_pw;
};

static	bool debug;		/* Emit debugging information? */
static	bool loudsubs;		/* Suppress output from helper programs? */
static	bool norun;		/* Actually run the helper programs? */
static	int unit;      		/* The unit we're working with. */
static	const char *mdname;	/* Name of memory disk device (e.g., "md"). */
static	const char *mdsuffix;	/* Suffix of memory disk device (e.g., ".uzip"). */
static	size_t mdnamelen;	/* Length of mdname. */
static	const char *path_mdconfig =_PATH_MDCONFIG;

static void	 argappend(char **, const char *, ...) __printflike(2, 3);
static void	 debugprintf(const char *, ...) __printflike(1, 2);
static void	 do_mdconfig_attach(const char *, const enum md_types);
static void	 do_mdconfig_attach_au(const char *, const enum md_types);
static void	 do_mdconfig_detach(void);
static void	 do_mount_md(const char *, const char *);
static void	 do_mount_tmpfs(const char *, const char *);
static void	 do_mtptsetup(const char *, struct mtpt_info *);
static void	 do_newfs(const char *);
static void	 extract_ugid(const char *, struct mtpt_info *);
static int	 run(int *, const char *, ...) __printflike(2, 3);
static const char *run_exitstr(int);
static int	 run_exitnumber(int);
static void	 usage(void);

int
main(int argc, char **argv)
{
	struct mtpt_info mi;		/* Mountpoint info. */
	intmax_t mdsize;
	char *mdconfig_arg, *newfs_arg,	/* Args to helper programs. */
	    *mount_arg;
	enum md_types mdtype;		/* The type of our memory disk. */
	bool have_mdtype, mlmac;
	bool detach, softdep, autounit, newfs;
	const char *mtpoint, *size_arg, *unitstr;
	char *p;
	int ch, idx;
	void *set;
	unsigned long ul;

	/* Misc. initialization. */
	(void)memset(&mi, '\0', sizeof(mi));
	detach = true;
	softdep = true;
	autounit = false;
	mlmac = false;
	newfs = true;
	have_mdtype = false;
	mdtype = MD_SWAP;
	mdname = MD_NAME;
	mdnamelen = strlen(mdname);
	mdsize = 0;
	/*
	 * Can't set these to NULL.  They may be passed to the
	 * respective programs without modification.  I.e., we may not
	 * receive any command-line options which will caused them to
	 * be modified.
	 */
	mdconfig_arg = strdup("");
	newfs_arg = strdup("");
	mount_arg = strdup("");
	size_arg = NULL;

	/* If we were started as mount_mfs or mfs, imply -C. */
	if (strcmp(getprogname(), "mount_mfs") == 0 ||
	    strcmp(getprogname(), "mfs") == 0) {
		/* Make compatibility assumptions. */
		mi.mi_mode = 01777;
		mi.mi_have_mode = true;
	}

	while ((ch = getopt(argc, argv,
	    "a:b:Cc:Dd:E:e:F:f:hi:LlMm:NnO:o:Pp:Ss:tT:Uv:w:X")) != -1)
		switch (ch) {
		case 'a':
			argappend(&newfs_arg, "-a %s", optarg);
			break;
		case 'b':
			argappend(&newfs_arg, "-b %s", optarg);
			break;
		case 'C':
			/* Ignored for compatibility. */
			break;
		case 'c':
			argappend(&newfs_arg, "-c %s", optarg);
			break;
		case 'D':
			detach = false;
			break;
		case 'd':
			argappend(&newfs_arg, "-d %s", optarg);
			break;
		case 'E':
			path_mdconfig = optarg;
			break;
		case 'e':
			argappend(&newfs_arg, "-e %s", optarg);
			break;
		case 'F':
			if (have_mdtype)
				usage();
			mdtype = MD_VNODE;
			have_mdtype = true;
			argappend(&mdconfig_arg, "-f %s", optarg);
			break;
		case 'f':
			argappend(&newfs_arg, "-f %s", optarg);
			break;
		case 'h':
			usage();
			break;
		case 'i':
			argappend(&newfs_arg, "-i %s", optarg);
			break;
		case 'L':
			loudsubs = true;
			break;
		case 'l':
			mlmac = true;
			argappend(&newfs_arg, "-l");
			break;
		case 'M':
			if (have_mdtype)
				usage();
			mdtype = MD_MALLOC;
			have_mdtype = true;
			argappend(&mdconfig_arg, "-o reserve");
			break;
		case 'm':
			argappend(&newfs_arg, "-m %s", optarg);
			break;
		case 'N':
			norun = true;
			break;
		case 'n':
			argappend(&newfs_arg, "-n");
			break;
		case 'O':
			argappend(&newfs_arg, "-o %s", optarg);
			break;
		case 'o':
			argappend(&mount_arg, "-o %s", optarg);
			break;
		case 'P':
			newfs = false;
			break;
		case 'p':
			if ((set = setmode(optarg)) == NULL)
				usage();
			mi.mi_mode = getmode(set, S_IRWXU | S_IRWXG | S_IRWXO);
			mi.mi_have_mode = true;
			mi.mi_forced_pw = true;
			free(set);
			break;
		case 'S':
			softdep = false;
			break;
		case 's':
			size_arg = optarg;
			break;
		case 't':
			argappend(&newfs_arg, "-t");
			break;
		case 'T':
			argappend(&mount_arg, "-t %s", optarg);
			break;
		case 'U':
			softdep = true;
			break;
		case 'v':
			argappend(&newfs_arg, "-O %s", optarg);
			break;
		case 'w':
			extract_ugid(optarg, &mi);
			mi.mi_forced_pw = true;
			break;
		case 'X':
			debug = true;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;
	if (argc < 2)
		usage();

	/*
	 * Historically our size arg was passed directly to mdconfig, which
	 * treats a number without a suffix as a count of 512-byte sectors;
	 * tmpfs would treat it as a count of bytes.  To get predictable
	 * behavior for 'auto' we document that the size always uses mdconfig
	 * rules.  To make that work, decode the size here so it can be passed
	 * to either tmpfs or mdconfig as a count of bytes.
	 */
	if (size_arg != NULL) {
		mdsize = (intmax_t)strtoumax(size_arg, &p, 0);
		if (p == size_arg || (p[0] != 0 && p[1] != 0) || mdsize < 0)
			errx(1, "invalid size '%s'", size_arg);
		switch (*p) {
		case 'p':
		case 'P':
			mdsize *= 1024;
		case 't':
		case 'T':
			mdsize *= 1024;
		case 'g':
		case 'G':
			mdsize *= 1024;
		case 'm':
		case 'M':
			mdsize *= 1024;
		case 'k':
		case 'K':
			mdsize *= 1024;
		case 'b':
		case 'B':
			break;
		case '\0':
			mdsize *= 512;
			break;
		default:
			errx(1, "invalid size suffix on '%s'", size_arg);
		}
	}

	/*
	 * Based on the command line 'md-device' either mount a tmpfs filesystem
	 * or configure the md device then format and mount a filesystem on it.
	 * If the device is 'auto' use tmpfs if it is available and there is no
	 * request for multilabel MAC (which tmpfs does not support).
	 */
	unitstr = argv[0];
	mtpoint = argv[1];

	if (strcmp(unitstr, "auto") == 0) {
		if (mlmac)
			idx = -1; /* Must use md for mlmac. */
		else if ((idx = modfind("tmpfs")) == -1)
			idx = kldload("tmpfs");
		if (idx == -1)
			unitstr = "md";
		else
			unitstr = "tmpfs";
	}

	if (strcmp(unitstr, "tmpfs") == 0) {
		if (size_arg != NULL && mdsize != 0)
			argappend(&mount_arg, "-o size=%jd", mdsize);
		do_mount_tmpfs(mount_arg, mtpoint); 
	} else {
		if (size_arg != NULL)
			argappend(&mdconfig_arg, "-s %jdB", mdsize);
		if (strncmp(unitstr, "/dev/", 5) == 0)
			unitstr += 5;
		if (strncmp(unitstr, mdname, mdnamelen) == 0)
			unitstr += mdnamelen;
		if (!isdigit(*unitstr)) {
			autounit = true;
			unit = -1;
			mdsuffix = unitstr;
		} else {
			ul = strtoul(unitstr, &p, 10);
			if (ul == ULONG_MAX)
				errx(1, "bad device unit: %s", unitstr);
			unit = ul;
			mdsuffix = p;	/* can be empty */
		}
	
		if (!have_mdtype)
			mdtype = MD_SWAP;
		if (softdep)
			argappend(&newfs_arg, "-U");
		if (mdtype != MD_VNODE && !newfs)
			errx(1, "-P requires a vnode-backed disk");
	
		/* Do the work. */
		if (detach && !autounit)
			do_mdconfig_detach();
		if (autounit)
			do_mdconfig_attach_au(mdconfig_arg, mdtype);
		else
			do_mdconfig_attach(mdconfig_arg, mdtype);
		if (newfs)
			do_newfs(newfs_arg);
		do_mount_md(mount_arg, mtpoint);
	}

	do_mtptsetup(mtpoint, &mi);

	return (0);
}

/*
 * Append the expansion of 'fmt' to the buffer pointed to by '*dstp';
 * reallocate as required.
 */
static void
argappend(char **dstp, const char *fmt, ...)
{
	char *old, *new;
	va_list ap;

	old = *dstp;
	assert(old != NULL);

	va_start(ap, fmt);
	if (vasprintf(&new, fmt,ap) == -1)
		errx(1, "vasprintf");
	va_end(ap);

	*dstp = new;
	if (asprintf(&new, "%s %s", old, new) == -1)
		errx(1, "asprintf");
	free(*dstp);
	free(old);

	*dstp = new;
}

/*
 * If run-time debugging is enabled, print the expansion of 'fmt'.
 * Otherwise, do nothing.
 */
static void
debugprintf(const char *fmt, ...)
{
	va_list ap;

	if (!debug)
		return;
	fprintf(stderr, "DEBUG: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	fflush(stderr);
}

/*
 * Attach a memory disk with a known unit.
 */
static void
do_mdconfig_attach(const char *args, const enum md_types mdtype)
{
	int rv;
	const char *ta;		/* Type arg. */

	switch (mdtype) {
	case MD_SWAP:
		ta = "-t swap";
		break;
	case MD_VNODE:
		ta = "-t vnode";
		break;
	case MD_MALLOC:
		ta = "-t malloc";
		break;
	default:
		abort();
	}
	rv = run(NULL, "%s -a %s%s -u %s%d", path_mdconfig, ta, args,
	    mdname, unit);
	if (rv)
		errx(1, "mdconfig (attach) exited %s %d", run_exitstr(rv),
		    run_exitnumber(rv));
}

/*
 * Attach a memory disk with an unknown unit; use autounit.
 */
static void
do_mdconfig_attach_au(const char *args, const enum md_types mdtype)
{
	const char *ta;		/* Type arg. */
	char *linep;
	char linebuf[12];	/* 32-bit unit (10) + '\n' (1) + '\0' (1) */
	int fd;			/* Standard output of mdconfig invocation. */
	FILE *sfd;
	int rv;
	char *p;
	size_t linelen;
	unsigned long ul;

	switch (mdtype) {
	case MD_SWAP:
		ta = "-t swap";
		break;
	case MD_VNODE:
		ta = "-t vnode";
		break;
	case MD_MALLOC:
		ta = "-t malloc";
		break;
	default:
		abort();
	}
	rv = run(&fd, "%s -a %s%s", path_mdconfig, ta, args);
	if (rv)
		errx(1, "mdconfig (attach) exited %s %d", run_exitstr(rv),
		    run_exitnumber(rv));

	/* Receive the unit number. */
	if (norun) {	/* Since we didn't run, we can't read.  Fake it. */
		unit = 0;
		return;
	}
	sfd = fdopen(fd, "r");
	if (sfd == NULL)
		err(1, "fdopen");
	linep = fgetln(sfd, &linelen);
	/* If the output format changes, we want to know about it. */
	if (linep == NULL || linelen <= mdnamelen + 1 ||
	    linelen - mdnamelen >= sizeof(linebuf) ||
	    strncmp(linep, mdname, mdnamelen) != 0)
		errx(1, "unexpected output from mdconfig (attach)");
	linep += mdnamelen;
	linelen -= mdnamelen;
	/* Can't use strlcpy because linep is not NULL-terminated. */
	strncpy(linebuf, linep, linelen);
	linebuf[linelen] = '\0';
	ul = strtoul(linebuf, &p, 10);
	if (ul == ULONG_MAX || *p != '\n')
		errx(1, "unexpected output from mdconfig (attach)");
	unit = ul;

	fclose(sfd);
}

/*
 * Detach a memory disk.
 */
static void
do_mdconfig_detach(void)
{
	int rv;

	rv = run(NULL, "%s -d -u %s%d", path_mdconfig, mdname, unit);
	if (rv && debug)	/* This is allowed to fail. */
		warnx("mdconfig (detach) exited %s %d (ignored)",
		    run_exitstr(rv), run_exitnumber(rv));
}

/*
 * Mount the configured memory disk.
 */
static void
do_mount_md(const char *args, const char *mtpoint)
{
	int rv;

	rv = run(NULL, "%s%s /dev/%s%d%s %s", _PATH_MOUNT, args,
	    mdname, unit, mdsuffix, mtpoint);
	if (rv)
		errx(1, "mount exited %s %d", run_exitstr(rv),
		    run_exitnumber(rv));
}

/*
 * Mount the configured tmpfs.
 */
static void
do_mount_tmpfs(const char *args, const char *mtpoint)
{
	int rv;

	rv = run(NULL, "%s -t tmpfs %s tmp %s", _PATH_MOUNT, args, mtpoint);
	if (rv)
		errx(1, "tmpfs mount exited %s %d", run_exitstr(rv),
		    run_exitnumber(rv));
}

/*
 * Various configuration of the mountpoint.  Mostly, enact 'mip'.
 */
static void
do_mtptsetup(const char *mtpoint, struct mtpt_info *mip)
{
	struct statfs sfs;

	if (!mip->mi_have_mode && !mip->mi_have_uid && !mip->mi_have_gid)
		return;

	if (!norun) {
		if (statfs(mtpoint, &sfs) == -1) {
			warn("statfs: %s", mtpoint);
			return;
		}
		if ((sfs.f_flags & MNT_RDONLY) != 0) {
			if (mip->mi_forced_pw) {
				warnx(
	"Not changing mode/owner of %s since it is read-only",
				    mtpoint);
			} else {
				debugprintf(
	"Not changing mode/owner of %s since it is read-only",
				    mtpoint);
			}
			return;
		}
	}

	if (mip->mi_have_mode) {
		debugprintf("changing mode of %s to %o.", mtpoint,
		    mip->mi_mode);
		if (!norun)
			if (chmod(mtpoint, mip->mi_mode) == -1)
				err(1, "chmod: %s", mtpoint);
	}
	/*
	 * We have to do these separately because the user may have
	 * only specified one of them.
	 */
	if (mip->mi_have_uid) {
		debugprintf("changing owner (user) or %s to %u.", mtpoint,
		    mip->mi_uid);
		if (!norun)
			if (chown(mtpoint, mip->mi_uid, -1) == -1)
				err(1, "chown %s to %u (user)", mtpoint,
				    mip->mi_uid);
	}
	if (mip->mi_have_gid) {
		debugprintf("changing owner (group) or %s to %u.", mtpoint,
		    mip->mi_gid);
		if (!norun)
			if (chown(mtpoint, -1, mip->mi_gid) == -1)
				err(1, "chown %s to %u (group)", mtpoint,
				    mip->mi_gid);
	}
}

/*
 * Put a file system on the memory disk.
 */
static void
do_newfs(const char *args)
{
	int rv;

	rv = run(NULL, "%s%s /dev/%s%d", _PATH_NEWFS, args, mdname, unit);
	if (rv)
		errx(1, "newfs exited %s %d", run_exitstr(rv),
		    run_exitnumber(rv));
}

/*
 * 'str' should be a user and group name similar to the last argument
 * to chown(1); i.e., a user, followed by a colon, followed by a
 * group.  The user and group in 'str' may be either a [ug]id or a
 * name.  Upon return, the uid and gid fields in 'mip' will contain
 * the uid and gid of the user and group name in 'str', respectively.
 *
 * In other words, this derives a user and group id from a string
 * formatted like the last argument to chown(1).
 *
 * Notice: At this point we don't support only a username or only a
 * group name. do_mtptsetup already does, so when this feature is
 * desired, this is the only routine that needs to be changed.
 */
static void
extract_ugid(const char *str, struct mtpt_info *mip)
{
	char *ug;			/* Writable 'str'. */
	char *user, *group;		/* Result of extracton. */
	struct passwd *pw;
	struct group *gr;
	char *p;
	uid_t *uid;
	gid_t *gid;

	uid = &mip->mi_uid;
	gid = &mip->mi_gid;
	mip->mi_have_uid = mip->mi_have_gid = false;

	/* Extract the user and group from 'str'.  Format above. */
	ug = strdup(str);
	assert(ug != NULL);
	group = ug;
	user = strsep(&group, ":");
	if (user == NULL || group == NULL || *user == '\0' || *group == '\0')
		usage();

	/* Derive uid. */
	*uid = strtoul(user, &p, 10);
	if (*uid == (uid_t)ULONG_MAX)
		usage();
	if (*p != '\0') {
		pw = getpwnam(user);
		if (pw == NULL)
			errx(1, "invalid user: %s", user);
		*uid = pw->pw_uid;
	}
	mip->mi_have_uid = true;

	/* Derive gid. */
	*gid = strtoul(group, &p, 10);
	if (*gid == (gid_t)ULONG_MAX)
		usage();
	if (*p != '\0') {
		gr = getgrnam(group);
		if (gr == NULL)
			errx(1, "invalid group: %s", group);
		*gid = gr->gr_gid;
	}
	mip->mi_have_gid = true;

	free(ug);
}

/*
 * Run a process with command name and arguments pointed to by the
 * formatted string 'cmdline'.  Since system(3) is not used, the first
 * space-delimited token of 'cmdline' must be the full pathname of the
 * program to run.
 *
 * The return value is the return code of the process spawned, or a negative
 * signal number if the process exited due to an uncaught signal.
 *
 * If 'ofd' is non-NULL, it is set to the standard output of
 * the program spawned (i.e., you can read from ofd and get the output
 * of the program).
 */
static int
run(int *ofd, const char *cmdline, ...)
{
	char **argv, **argvp;		/* Result of splitting 'cmd'. */
	int argc;
	char *cmd;			/* Expansion of 'cmdline'. */
	int pid, status;		/* Child info. */
	int pfd[2];			/* Pipe to the child. */
	int nfd;			/* Null (/dev/null) file descriptor. */
	bool dup2dn;			/* Dup /dev/null to stdout? */
	va_list ap;
	char *p;
	int rv, i;

	dup2dn = true;
	va_start(ap, cmdline);
	rv = vasprintf(&cmd, cmdline, ap);
	if (rv == -1)
		err(1, "vasprintf");
	va_end(ap);

	/* Split up 'cmd' into 'argv' for use with execve. */
	for (argc = 1, p = cmd; (p = strchr(p, ' ')) != NULL; p++)
		argc++;		/* 'argc' generation loop. */
	argv = (char **)malloc(sizeof(*argv) * (argc + 1));
	assert(argv != NULL);
	for (p = cmd, argvp = argv; (*argvp = strsep(&p, " ")) != NULL;)
		if (**argvp != '\0')
			if (++argvp >= &argv[argc]) {
				*argvp = NULL;
				break;
			}
	assert(*argv);
	/* The argv array ends up NULL-terminated here. */

	/* Make sure the above loop works as expected. */
	if (debug) {
		/*
		 * We can't, but should, use debugprintf here.  First,
		 * it appends a trailing newline to the output, and
		 * second it prepends "DEBUG: " to the output.  The
		 * former is a problem for this would-be first call,
		 * and the latter for the would-be call inside the
		 * loop.
		 */
		(void)fprintf(stderr, "DEBUG: running:");
		/* Should be equivalent to 'cmd' (before strsep, of course). */
		for (i = 0; argv[i] != NULL; i++)
			(void)fprintf(stderr, " %s", argv[i]);
		(void)fprintf(stderr, "\n");
	}

	/* Create a pipe if necessary and fork the helper program. */
	if (ofd != NULL) {
		if (pipe(&pfd[0]) == -1)
			err(1, "pipe");
		*ofd = pfd[0];
		dup2dn = false;
	}
	pid = fork();
	switch (pid) {
	case 0:
		/* XXX can we call err() in here? */
		if (norun)
			_exit(0);
		if (ofd != NULL)
			if (dup2(pfd[1], STDOUT_FILENO) < 0)
				err(1, "dup2");
		if (!loudsubs) {
			nfd = open(_PATH_DEVNULL, O_RDWR);
			if (nfd == -1)
				err(1, "open: %s", _PATH_DEVNULL);
			if (dup2(nfd, STDIN_FILENO) < 0)
				err(1, "dup2");
			if (dup2dn)
				if (dup2(nfd, STDOUT_FILENO) < 0)
				   err(1, "dup2");
			if (dup2(nfd, STDERR_FILENO) < 0)
				err(1, "dup2");
		}

		(void)execv(argv[0], argv);
		warn("exec: %s", argv[0]);
		_exit(-1);
	case -1:
		err(1, "fork");
	}

	free(cmd);
	free(argv);
	while (waitpid(pid, &status, 0) != pid)
		;
	if (WIFEXITED(status))
		return (WEXITSTATUS(status));
	if (WIFSIGNALED(status))
		return (-WTERMSIG(status));
	err(1, "unexpected waitpid status: 0x%x", status);
}

/*
 * If run() returns non-zero, provide a string explaining why.
 */
static const char *
run_exitstr(int rv)
{
	if (rv > 0)
		return ("with error code");
	if (rv < 0)
		return ("with signal");
	return (NULL);
}

/*
 * If run returns non-zero, provide a relevant number.
 */
static int
run_exitnumber(int rv)
{
	if (rv < 0)
		return (-rv);
	return (rv);
}

static void
usage(void)
{

	fprintf(stderr,
"usage: %s [-DLlMNnPStUX] [-a maxcontig] [-b block-size]\n"
"\t[-c blocks-per-cylinder-group][-d max-extent-size] [-E path-mdconfig]\n"
"\t[-e maxbpg] [-F file] [-f frag-size] [-i bytes] [-m percent-free]\n"
"\t[-O optimization] [-o mount-options]\n"
"\t[-p permissions] [-s size] [-v version] [-w user:group]\n"
"\tmd-device mount-point\n", getprogname());
	exit(1);
}
