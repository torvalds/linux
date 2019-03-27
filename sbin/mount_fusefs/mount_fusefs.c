/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Jean-Sebastien Pedron
 * Copyright (c) 2005 Csaba Henk 
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/sysctl.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <getopt.h>
#include <limits.h>
#include <osreldate.h>
#include <paths.h>

#include "mntopts.h"

#ifndef FUSE4BSD_VERSION
#define	FUSE4BSD_VERSION	"0.3.9-pre1"
#endif

void	__usage_short(void);
void	usage(void);
void	helpmsg(void);
void	showversion(void);
int	init_backgrounded(void);

static struct mntopt mopts[] = {
	#define ALTF_PRIVATE 0x01
	{ "private",             0, ALTF_PRIVATE, 1 },
	{ "neglect_shares",      0, 0x02, 1 },
	{ "push_symlinks_in",    0, 0x04, 1 },
	{ "allow_other",         0, 0x08, 1 },
	{ "default_permissions", 0, 0x10, 1 },
	#define ALTF_MAXREAD 0x20
	{ "max_read=",           0, ALTF_MAXREAD, 1 },
	#define ALTF_SUBTYPE 0x40
	{ "subtype=",            0, ALTF_SUBTYPE, 1 },
	#define ALTF_SYNC_UNMOUNT 0x80
	{ "sync_unmount",        0, ALTF_SYNC_UNMOUNT, 1 },
	/*
	 * MOPT_AUTOMOUNTED, included by MOPT_STDOPTS, does not fit into
	 * the 'flags' argument to nmount(2).  We have to abuse altflags
	 * to pass it, as string, via iovec.
	 */
	#define ALTF_AUTOMOUNTED 0x100
	{ "automounted",	0, ALTF_AUTOMOUNTED, 1 },
	/* Linux specific options, we silently ignore them */
	{ "fsname=",             0, 0x00, 1 },
	{ "fd=",                 0, 0x00, 1 },
	{ "rootmode=",           0, 0x00, 1 },
	{ "user_id=",            0, 0x00, 1 },
	{ "group_id=",           0, 0x00, 1 },
	{ "large_read",          0, 0x00, 1 },
	/* "nonempty", just the first two chars are stripped off during parsing */
	{ "nempty",              0, 0x00, 1 },
	MOPT_STDOPTS,
	MOPT_END
};

struct mntval {
	int mv_flag;
	void *mv_value;
	int mv_len;
};

static struct mntval mvals[] = {
	{ ALTF_MAXREAD, NULL, 0 },
	{ ALTF_SUBTYPE, NULL, 0 },
	{ 0, NULL, 0 }
};

#define DEFAULT_MOUNT_FLAGS ALTF_PRIVATE | ALTF_SYNC_UNMOUNT

int
main(int argc, char *argv[])
{
	struct iovec *iov;
	int mntflags, iovlen, verbose = 0;
	char *dev = NULL, *dir = NULL, mntpath[MAXPATHLEN];
	char *devo = NULL, *diro = NULL;
	char ndev[128], fdstr[15];
	int i, done = 0, reject_allow_other = 0, safe_level = 0;
	int altflags = DEFAULT_MOUNT_FLAGS;
	int __altflags = DEFAULT_MOUNT_FLAGS;
	int ch = 0;
	struct mntopt *mo;
	struct mntval *mv;
	static struct option longopts[] = {
		{"reject-allow_other", no_argument, NULL, 'A'},
		{"safe", no_argument, NULL, 'S'},
		{"daemon", required_argument, NULL, 'D'},
		{"daemon_opts", required_argument, NULL, 'O'},
		{"special", required_argument, NULL, 's'},
		{"mountpath", required_argument, NULL, 'm'},
		{"version", no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{0,0,0,0}
	};
	int pid = 0;
	int fd = -1, fdx;
	char *ep;
	char *daemon_str = NULL, *daemon_opts = NULL;

	/*
	 * We want a parsing routine which is not sensitive to
	 * the position of args/opts; it should extract the
	 * first two args and stop at the beginning of the rest.
	 * (This makes it easier to call mount_fusefs from external
	 * utils than it is with a strict "util flags args" syntax.)
	 */

	iov = NULL;
	iovlen = 0;
	mntflags = 0;
	/* All in all, I feel it more robust this way... */
	unsetenv("POSIXLY_CORRECT");
	if (getenv("MOUNT_FUSEFS_IGNORE_UNKNOWN"))
		getmnt_silent = 1;
	if (getenv("MOUNT_FUSEFS_VERBOSE"))
		verbose = 1;

	do {
		for (i = 0; i < 3; i++) {
			if (optind < argc && argv[optind][0] != '-') {
				if (dir) {
					done = 1;
					break;
				}
				if (dev)
					dir = argv[optind];
				else
					dev = argv[optind];
				optind++;
			}
		}
		switch(ch) {
		case 'A':
			reject_allow_other = 1;
			break;
		case 'S':
			safe_level = 1;
			break;
		case 'D':
			if (daemon_str)
				errx(1, "daemon specified inconsistently");
			daemon_str = optarg;
			break;
		case 'O':
			if (daemon_opts)
				errx(1, "daemon opts specified inconsistently");
			daemon_opts = optarg;
			break;
		case 'o':
			getmntopts(optarg, mopts, &mntflags, &altflags);
			for (mv = mvals; mv->mv_flag; ++mv) {
				if (! (altflags & mv->mv_flag))
					continue;
				for (mo = mopts; mo->m_flag; ++mo) {
					char *p, *q;

					if (mo->m_flag != mv->mv_flag)
						continue;
					p = strstr(optarg, mo->m_option);
					if (p) {
						p += strlen(mo->m_option);
						q = p;
						while (*q != '\0' && *q != ',')
							q++;
						mv->mv_len = q - p + 1;
						mv->mv_value = malloc(mv->mv_len);
						memcpy(mv->mv_value, p, mv->mv_len - 1);
						((char *)mv->mv_value)[mv->mv_len - 1] = '\0';
						break;
					}
				}
			}
			break;
		case 's':
			if (devo)
				errx(1, "special specified inconsistently");
			devo = optarg;
			break;
		case 'm':
			if (diro)
				errx(1, "mount path specified inconsistently");
			diro = optarg;
			break;
		case 'v': 
			verbose = 1;
			break;
		case 'h':
			helpmsg();
			break;
		case 'V':
			showversion();
			break;
		case '\0':
			break;
		case '?':
		default:
			usage();
		}
		if (done)
			break;
	} while ((ch = getopt_long(argc, argv, "AvVho:SD:O:s:m:", longopts, NULL)) != -1);

	argc -= optind;
	argv += optind;

	if (devo) {
		if (dev)
			errx(1, "special specified inconsistently");
		dev = devo;
	} else if (diro)
		errx(1, "if mountpoint is given via an option, special should also be given via an option"); 

	if (diro) {
		if (dir)
			errx(1, "mount path specified inconsistently");
		dir = diro;
	}

	if ((! dev) && argc > 0) {
		dev = *argv++;
		argc--;
	}

	if ((! dir) && argc > 0) {
		dir = *argv++;
		argc--;
	}

	if (! (dev && dir))
		errx(1, "missing special and/or mountpoint");

	for (mo = mopts; mo->m_flag; ++mo) {
		if (altflags & mo->m_flag) {
			int iov_done = 0;

			if (reject_allow_other &&
			    strcmp(mo->m_option, "allow_other") == 0)
				/*
				 * reject_allow_other is stronger than a
				 * negative of allow_other: if this is set,
				 * allow_other is blocked, period.
				 */
				errx(1, "\"allow_other\" usage is banned by respective option");

			for (mv = mvals; mv->mv_flag; ++mv) {
				if (mo->m_flag != mv->mv_flag)
					continue;
				if (mv->mv_value) {
					build_iovec(&iov, &iovlen, mo->m_option, mv->mv_value, mv->mv_len);
					iov_done = 1;
					break;
				}
			}
			if (! iov_done)
				build_iovec(&iov, &iovlen, mo->m_option,
				    __DECONST(void *, ""), -1);
		}
		if (__altflags & mo->m_flag) {
			char *uscore_opt;

			if (asprintf(&uscore_opt, "__%s", mo->m_option) == -1)
				err(1, "failed to allocate memory");
			build_iovec(&iov, &iovlen, uscore_opt,
			    __DECONST(void *, ""), -1);
			free(uscore_opt);
		}
	}

	if (getenv("MOUNT_FUSEFS_SAFE"))
		safe_level = 1;

	if (safe_level > 0 && (argc > 0 || daemon_str || daemon_opts))
		errx(1, "safe mode, spawning daemon not allowed");

	if ((argc > 0 && (daemon_str || daemon_opts)) ||
	    (daemon_opts && ! daemon_str))
		errx(1, "daemon specified inconsistently");

	/*
	 * Resolve the mountpoint with realpath(3) and remove unnecessary
	 * slashes from the devicename if there are any.
	 */
	if (checkpath(dir, mntpath) != 0)
		err(1, "%s", mntpath);
	(void)rmslashes(dev, dev);

	if (strcmp(dev, "auto") == 0)
		dev = __DECONST(char *, "/dev/fuse");

	if (strcmp(dev, "/dev/fuse") == 0) {
		if (! (argc > 0 || daemon_str)) {
			fprintf(stderr, "Please also specify the fuse daemon to run when mounting via the multiplexer!\n");
			usage();
		}
		if ((fd = open(dev, O_RDWR)) < 0)
			err(1, "failed to open fuse device");
	} else {
		fdx = strtol(dev, &ep, 10);
		if (*ep == '\0')
			fd = fdx;
	}

	/* Identifying device */
	if (fd >= 0) {
		struct stat sbuf;
		char *ndevbas, *lep;

		if (fstat(fd, &sbuf) == -1)
			err(1, "cannot stat device file descriptor");

		strcpy(ndev, _PATH_DEV);
		ndevbas = ndev + strlen(_PATH_DEV);
		devname_r(sbuf.st_rdev, S_IFCHR, ndevbas,
		          sizeof(ndev) - strlen(_PATH_DEV));

		if (strncmp(ndevbas, "fuse", 4))
			errx(1, "mounting inappropriate device");

		strtol(ndevbas + 4, &lep, 10);
		if (*lep != '\0')
			errx(1, "mounting inappropriate device");

		dev = ndev;
	}

	if (argc > 0 || daemon_str) {
		char *fds;

		if (fd < 0 && (fd = open(dev, O_RDWR)) < 0)
			err(1, "failed to open fuse device");
	
		if (asprintf(&fds, "%d", fd) == -1)
			err(1, "failed to allocate memory");
		setenv("FUSE_DEV_FD", fds, 1);
		free(fds);
		setenv("FUSE_NO_MOUNT", "1", 1);

		if (daemon_str) {
			char *bgdaemon;
			int len;

			if (! daemon_opts)
				daemon_opts = __DECONST(char *, "");

			len =  strlen(daemon_str) + 1 + strlen(daemon_opts) +
			    2 + 1;
			bgdaemon = calloc(1, len);

			if (! bgdaemon)
				err(1, "failed to allocate memory");

			strlcpy(bgdaemon, daemon_str, len);
			strlcat(bgdaemon, " ", len);
			strlcat(bgdaemon, daemon_opts, len);
			strlcat(bgdaemon, " &", len);

			if (system(bgdaemon))
				err(1, "failed to call fuse daemon");
		} else {
			if ((pid = fork()) < 0)
				err(1, "failed to fork for fuse daemon");

			if (pid == 0) {
				execvp(argv[0], argv);
				err(1, "failed to exec fuse daemon");
			}
		}
	}

	if (fd >= 0 && ! init_backgrounded() && close(fd) < 0) {
		if (pid)
			kill(pid, SIGKILL);
		err(1, "failed to close fuse device");
	}

	/* Prepare the options vector for nmount(). build_iovec() is declared
	 * in mntopts.h. */
	sprintf(fdstr, "%d", fd);
	build_iovec(&iov, &iovlen, "fstype", __DECONST(void *, "fusefs"), -1);
	build_iovec(&iov, &iovlen, "fspath", mntpath, -1);
	build_iovec(&iov, &iovlen, "from", dev, -1);
	build_iovec(&iov, &iovlen, "fd", fdstr, -1);

	if (verbose)
		fprintf(stderr, "mounting fuse daemon on device %s\n", dev);

	if (nmount(iov, iovlen, mntflags) < 0)
		err(EX_OSERR, "%s on %s", dev, mntpath);

	exit(0);
}

void
__usage_short(void) {
	fprintf(stderr,
	    "usage:\n%s [-A|-S|-v|-V|-h|-D daemon|-O args|-s special|-m node|-o option...] special node [daemon args...]\n\n",
	    getprogname());
}

void
usage(void)
{
	struct mntopt *mo;

	__usage_short();

	fprintf(stderr, "known options:\n");
	for (mo = mopts; mo->m_flag; ++mo)
		fprintf(stderr, "\t%s\n", mo->m_option);

	fprintf(stderr, "\n(use -h for a detailed description of these options)\n");
	exit(EX_USAGE);
}

void
helpmsg(void)
{
	if (! getenv("MOUNT_FUSEFS_CALL_BY_LIB")) {
		__usage_short();
		fprintf(stderr, "description of options:\n");
	}

	/*
	 * The main use case of this function is giving info embedded in general
	 * FUSE lib help output. Therefore the style and the content of the output
	 * tries to fit there as much as possible.
	 */
	fprintf(stderr,
	        "    -o allow_other         allow access to other users\n"
	        /* "    -o nonempty            allow mounts over non-empty file/dir\n" */
	        "    -o default_permissions enable permission checking by kernel\n"
		/*
	        "    -o fsname=NAME         set filesystem name\n"
	        "    -o large_read          issue large read requests (2.4 only)\n"
		 */
	        "    -o subtype=NAME        set filesystem type\n"
	        "    -o max_read=N          set maximum size of read requests\n"
	        "    -o noprivate           allow secondary mounting of the filesystem\n"
	        "    -o neglect_shares      don't report EBUSY when unmount attempted\n"
	        "                           in presence of secondary mounts\n" 
	        "    -o push_symlinks_in    prefix absolute symlinks with mountpoint\n"
	        "    -o sync_unmount        do unmount synchronously\n"
	        );
	exit(EX_USAGE);
}

void
showversion(void)
{
	puts("mount_fusefs [fuse4bsd] version: " FUSE4BSD_VERSION);
	exit(EX_USAGE);
}

int
init_backgrounded(void)
{
	int ibg;
	size_t len;

	len = sizeof(ibg);

	if (sysctlbyname("vfs.fusefs.init_backgrounded", &ibg, &len, NULL, 0))
		return (0);

	return (ibg);
}
