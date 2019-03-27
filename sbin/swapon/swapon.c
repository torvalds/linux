/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 */

#if 0
#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1980, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)swapon.c	8.1 (Berkeley) 6/5/93";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/mdioctl.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <vm/vm_param.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <fstab.h>
#include <libgen.h>
#include <libutil.h>
#include <limits.h>
#include <paths.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(void);
static const char *swap_on_off(const char *, int, char *);
static const char *swap_on_off_gbde(const char *, int);
static const char *swap_on_off_geli(const char *, char *, int);
static const char *swap_on_off_md(const char *, char *, int);
static const char *swap_on_off_sfile(const char *, int);
static void swaplist(int, int, int);
static int run_cmd(int *, const char *, ...) __printflike(2, 3);

static enum { SWAPON, SWAPOFF, SWAPCTL } orig_prog, which_prog = SWAPCTL;

static int qflag;

int
main(int argc, char **argv)
{
	struct fstab *fsp;
	const char *swfile;
	char *ptr;
	int ret, ch, doall;
	int sflag, lflag, late, hflag;
	const char *etc_fstab;

	sflag = lflag = late = hflag = 0;
	if ((ptr = strrchr(argv[0], '/')) == NULL)
		ptr = argv[0];
	if (strstr(ptr, "swapon") != NULL)
		which_prog = SWAPON;
	else if (strstr(ptr, "swapoff") != NULL)
		which_prog = SWAPOFF;
	orig_prog = which_prog;
	
	doall = 0;
	etc_fstab = NULL;
	while ((ch = getopt(argc, argv, "AadghklLmqsUF:")) != -1) {
		switch(ch) {
		case 'A':
			if (which_prog == SWAPCTL) {
				doall = 1;
				which_prog = SWAPON;
			} else
				usage();
			break;
		case 'a':
			if (which_prog == SWAPON || which_prog == SWAPOFF)
				doall = 1;
			else
				which_prog = SWAPON;
			break;
		case 'd':
			if (which_prog == SWAPCTL)
				which_prog = SWAPOFF;
			else
				usage();
			break;
		case 'g':
			hflag = 'G';
			break;
		case 'h':
			hflag = 'H';
			break;
		case 'k':
			hflag = 'K';
			break;
		case 'l':
			lflag = 1;
			break;
		case 'L':
			late = 1;
			break;
		case 'm':
			hflag = 'M';
			break;
		case 'q':
			if (which_prog == SWAPON || which_prog == SWAPOFF)
				qflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		case 'U':
			if (which_prog == SWAPCTL) {
				doall = 1;
				which_prog = SWAPOFF;
			} else
				usage();
			break;
		case 'F':
			etc_fstab = optarg;
			break;
		case '?':
		default:
			usage();
		}
	}
	argv += optind;

	ret = 0;
	swfile = NULL;
	if (etc_fstab != NULL)
		setfstab(etc_fstab);
	if (which_prog == SWAPON || which_prog == SWAPOFF) {
		if (doall) {
			while ((fsp = getfsent()) != NULL) {
				if (strcmp(fsp->fs_type, FSTAB_SW) != 0)
					continue;
				if (strstr(fsp->fs_mntops, "noauto") != NULL)
					continue;
				if (which_prog != SWAPOFF &&
				    strstr(fsp->fs_mntops, "late") &&
				    late == 0)
					continue;
				if (which_prog == SWAPOFF &&
				    strstr(fsp->fs_mntops, "late") == NULL &&
				    late != 0)
					continue;
				swfile = swap_on_off(fsp->fs_spec, 1,
				    fsp->fs_mntops);
				if (swfile == NULL) {
					ret = 1;
					continue;
				}
				if (qflag == 0) {
					printf("%s: %sing %s as swap device\n",
					    getprogname(),
					    (which_prog == SWAPOFF) ?
					    "remov" : "add", swfile);
				}
			}
		} else if (*argv == NULL)
			usage();
		for (; *argv; ++argv) {
			swfile = swap_on_off(*argv, 0, NULL);
			if (swfile == NULL) {
				ret = 1;
				continue;
			}
			if (orig_prog == SWAPCTL) {
				printf("%s: %sing %s as swap device\n",
				    getprogname(),
				    (which_prog == SWAPOFF) ? "remov" : "add",
				    swfile);
			}
		}
	} else {
		if (lflag || sflag)
			swaplist(lflag, sflag, hflag);
		else 
			usage();
	}
	exit(ret);
}

static const char *
swap_on_off(const char *name, int doingall, char *mntops)
{
	char *base, *basebuf;

	/* Swap on vnode-backed md(4) device. */
	if (mntops != NULL &&
	    (fnmatch(_PATH_DEV MD_NAME "[0-9]*", name, 0) == 0 ||
	     fnmatch(MD_NAME "[0-9]*", name, 0) == 0 ||
	     strncmp(_PATH_DEV MD_NAME, name,
		sizeof(_PATH_DEV) + sizeof(MD_NAME)) == 0 ||
	     strncmp(MD_NAME, name, sizeof(MD_NAME)) == 0))
		return (swap_on_off_md(name, mntops, doingall));

	basebuf = strdup(name);
	base = basename(basebuf);

	/* Swap on encrypted device by GEOM_BDE. */
	if (fnmatch("*.bde", base, 0) == 0) {
		free(basebuf);
		return (swap_on_off_gbde(name, doingall));
	}

	/* Swap on encrypted device by GEOM_ELI. */
	if (fnmatch("*.eli", base, 0) == 0) {
		free(basebuf);
		return (swap_on_off_geli(name, mntops, doingall));
	}

	/* Swap on special file. */
	free(basebuf);
	return (swap_on_off_sfile(name, doingall));
}

/* Strip off .bde or .eli suffix from swap device name */
static char *
swap_basename(const char *name)
{
	char *dname, *p;

	dname = strdup(name);
	p = strrchr(dname, '.');
	/* assert(p != NULL); */
	*p = '\0';

	return (dname);
}

static const char *
swap_on_off_gbde(const char *name, int doingall)
{
	const char *ret;
	char pass[64 * 2 + 1];
	unsigned char bpass[64];
	char *dname;
	int i, error;

	dname = swap_basename(name);
	if (dname == NULL)
		return (NULL);

	if (which_prog == SWAPON) {
		arc4random_buf(bpass, sizeof(bpass));
		for (i = 0; i < (int)sizeof(bpass); i++)
			sprintf(&pass[2 * i], "%02x", bpass[i]);
		pass[sizeof(pass) - 1] = '\0';

		error = run_cmd(NULL, "%s init %s -P %s", _PATH_GBDE,
		    dname, pass);
		if (error) {
			/* bde device found.  Ignore it. */
			free(dname);
			if (qflag == 0)
				warnx("%s: Device already in use", name);
			return (NULL);
		}
		error = run_cmd(NULL, "%s attach %s -p %s", _PATH_GBDE,
		    dname, pass);
		free(dname);
		if (error) {
			warnx("gbde (attach) error: %s", name);
			return (NULL);
		}
	}

	ret = swap_on_off_sfile(name, doingall);

	if (which_prog == SWAPOFF) {
		error = run_cmd(NULL, "%s detach %s", _PATH_GBDE, dname);
		free(dname);
		if (error) {
			/* bde device not found.  Ignore it. */
			if (qflag == 0)
				warnx("%s: Device not found", name);
			return (NULL);
		}
	}

	return (ret);
}

/* Build geli(8) arguments from mntops */
static char *
swap_on_geli_args(const char *mntops)
{
	const char *aalgo, *ealgo, *keylen_str, *sectorsize_str;
	const char *aflag, *eflag, *lflag, *Tflag, *sflag;
	char *p, *args, *token, *string, *ops;
	int pagesize;
	size_t pagesize_len;
	u_long ul;

	/* Use built-in defaults for geli(8). */
	aalgo = ealgo = keylen_str = "";
	aflag = eflag = lflag = Tflag = "";

	/* We will always specify sectorsize. */
	sflag = " -s ";
	sectorsize_str = NULL;

	if (mntops != NULL) {
		string = ops = strdup(mntops);

		while ((token = strsep(&string, ",")) != NULL) {
			if ((p = strstr(token, "aalgo=")) == token) {
				aalgo = p + sizeof("aalgo=") - 1;
				aflag = " -a ";
			} else if ((p = strstr(token, "ealgo=")) == token) {
				ealgo = p + sizeof("ealgo=") - 1;
				eflag = " -e ";
			} else if ((p = strstr(token, "keylen=")) == token) {
				keylen_str = p + sizeof("keylen=") - 1;
				errno = 0;
				ul = strtoul(keylen_str, &p, 10);
				if (errno == 0) {
					if (*p != '\0' || ul > INT_MAX)
						errno = EINVAL;
				}
				if (errno) {
					warn("Invalid keylen: %s", keylen_str);
					free(ops);
					return (NULL);
				}
				lflag = " -l ";
			} else if ((p = strstr(token, "sectorsize=")) == token) {
				sectorsize_str = p + sizeof("sectorsize=") - 1;
				errno = 0;
				ul = strtoul(sectorsize_str, &p, 10);
				if (errno == 0) {
					if (*p != '\0' || ul > INT_MAX)
						errno = EINVAL;
				}
				if (errno) {
					warn("Invalid sectorsize: %s",
					    sectorsize_str);
					free(ops);
					return (NULL);
				}
			} else if (strcmp(token, "notrim") == 0) {
				Tflag = " -T ";
			} else if (strcmp(token, "late") == 0) {
				/* ignore known option */
			} else if (strcmp(token, "noauto") == 0) {
				/* ignore known option */
			} else if (strcmp(token, "sw") != 0) {
				warnx("Invalid option: %s", token);
				free(ops);
				return (NULL);
			}
		}
	} else
		ops = NULL;

	/*
	 * If we do not have a sector size at this point, fill in
	 * pagesize as sector size.
	 */
	if (sectorsize_str == NULL) {
		/* Use pagesize as default sectorsize. */
		pagesize = getpagesize();
		pagesize_len = snprintf(NULL, 0, "%d", pagesize) + 1;
		p = alloca(pagesize_len);
		snprintf(p, pagesize_len, "%d", pagesize);
		sectorsize_str = p;
	}

	(void)asprintf(&args, "%s%s%s%s%s%s%s%s%s -d",
	    aflag, aalgo, eflag, ealgo, lflag, keylen_str, Tflag,
	    sflag, sectorsize_str);

	free(ops);
	return (args);
}

static const char *
swap_on_off_geli(const char *name, char *mntops, int doingall)
{
	struct stat sb;
	char *dname, *args;
	int error;

	error = stat(name, &sb);

	if (which_prog == SWAPON) do {
		/* Skip if the .eli device already exists. */
		if (error == 0)
			break;

		args = swap_on_geli_args(mntops);
		if (args == NULL)
			return (NULL);

		dname = swap_basename(name);
		if (dname == NULL) {
			free(args);
			return (NULL);
		}

		error = run_cmd(NULL, "%s onetime%s %s", _PATH_GELI, args,
		    dname);

		free(dname);
		free(args);

		if (error) {
			/* error occurred during creation. */
			if (qflag == 0)
				warnx("%s: Invalid parameters", name);
			return (NULL);
		}
	} while (0);

	return (swap_on_off_sfile(name, doingall));
}

static const char *
swap_on_off_md(const char *name, char *mntops, int doingall)
{
	FILE *sfd;
	int fd, mdunit, error;
	const char *ret;
	static char mdpath[PATH_MAX], linebuf[PATH_MAX];
	char *p, *vnodefile;
	size_t linelen;
	u_long ul;

	fd = -1;
	sfd = NULL;
	if (strlen(name) == (sizeof(MD_NAME) - 1))
		mdunit = -1;
	else {
		errno = 0;
		ul = strtoul(name + 2, &p, 10);
		if (errno == 0) {
			if (*p != '\0' || ul > INT_MAX)
				errno = EINVAL;
		}
		if (errno) {
			warn("Bad device unit: %s", name);
			return (NULL);
		}
		mdunit = (int)ul;
	}

	vnodefile = NULL;
	if ((p = strstr(mntops, "file=")) != NULL) {
		vnodefile = strdup(p + sizeof("file=") - 1);
		p = strchr(vnodefile, ',');
		if (p != NULL)
			*p = '\0';
	}
	if (vnodefile == NULL) {
		warnx("file option not found for %s", name);
		return (NULL);
	}

	if (which_prog == SWAPON) {
		if (mdunit == -1) {
			error = run_cmd(&fd, "%s -l -n -f %s",
			    _PATH_MDCONFIG, vnodefile);
			if (error == 0) {
				/* md device found.  Ignore it. */
				close(fd);
				if (!qflag)
					warnx("%s: Device already in use",
					    vnodefile);
				free(vnodefile);
				return (NULL);
			}
			error = run_cmd(&fd, "%s -a -t vnode -n -f %s",
			    _PATH_MDCONFIG, vnodefile);
			if (error) {
				warnx("mdconfig (attach) error: file=%s",
				    vnodefile);
				free(vnodefile);
				return (NULL);
			}
			sfd = fdopen(fd, "r");
			if (sfd == NULL) {
				warn("mdconfig (attach) fdopen error");
				ret = NULL;
				goto err;
			}
			p = fgetln(sfd, &linelen);
			if (p == NULL &&
			    (linelen < 2 || linelen > sizeof(linebuf))) {
				warn("mdconfig (attach) unexpected output");
				ret = NULL;
				goto err;
			}
			strncpy(linebuf, p, linelen);
			linebuf[linelen - 1] = '\0';
			errno = 0;
			ul = strtoul(linebuf, &p, 10);
			if (errno == 0) {
				if (*p != '\0' || ul > INT_MAX)
					errno = EINVAL;
			}
			if (errno) {
				warn("mdconfig (attach) unexpected output: %s",
				    linebuf);
				ret = NULL;
				goto err;
			}
			mdunit = (int)ul;
		} else {
			error = run_cmd(&fd, "%s -l -n -f %s -u %d",
			    _PATH_MDCONFIG, vnodefile, mdunit);
			if (error == 0) {
				/* md device found.  Ignore it. */
				close(fd);
				if (qflag == 0)
					warnx("md%d on %s: Device already "
					    "in use", mdunit, vnodefile);
				free(vnodefile);
				return (NULL);
			}
			error = run_cmd(NULL, "%s -a -t vnode -u %d -f %s",
			    _PATH_MDCONFIG, mdunit, vnodefile);
			if (error) {
				warnx("mdconfig (attach) error: "
				    "md%d on file=%s", mdunit, vnodefile);
				free(vnodefile);
				return (NULL);
			}
		}
	} else /* SWAPOFF */ {
		if (mdunit == -1) {
			error = run_cmd(&fd, "%s -l -n -f %s",
			    _PATH_MDCONFIG, vnodefile);
			if (error) {
				/* md device not found.  Ignore it. */
				close(fd);
				if (!qflag)
					warnx("md on %s: Device not found",
					    vnodefile);
				free(vnodefile);
				return (NULL);
			}
			sfd = fdopen(fd, "r");
			if (sfd == NULL) {
				warn("mdconfig (list) fdopen error");
				ret = NULL;
				goto err;
			}
			p = fgetln(sfd, &linelen);
			if (p == NULL &&
			    (linelen < 2 || linelen > sizeof(linebuf) - 1)) {
				warn("mdconfig (list) unexpected output");
				ret = NULL;
				goto err;
			}
			strncpy(linebuf, p, linelen);
			linebuf[linelen - 1] = '\0';
			p = strchr(linebuf, ' ');
			if (p != NULL)
				*p = '\0';
			errno = 0;
			ul = strtoul(linebuf, &p, 10);
			if (errno == 0) {
				if (*p != '\0' || ul > INT_MAX)
					errno = EINVAL;
			}
			if (errno) {
				warn("mdconfig (list) unexpected output: %s",
				    linebuf);
				ret = NULL;
				goto err;
			}
			mdunit = (int)ul;
		} else {
			error = run_cmd(&fd, "%s -l -n -f %s -u %d",
			    _PATH_MDCONFIG, vnodefile, mdunit);
			if (error) {
				/* md device not found.  Ignore it. */
				close(fd);
				if (!qflag)
					warnx("md%d on %s: Device not found",
					    mdunit, vnodefile);
				free(vnodefile);
				return (NULL);
			}
		}
	}
	snprintf(mdpath, sizeof(mdpath), "%s%s%d", _PATH_DEV,
	    MD_NAME, mdunit);
	mdpath[sizeof(mdpath) - 1] = '\0';
	ret = swap_on_off_sfile(mdpath, doingall);

	if (which_prog == SWAPOFF) {
		if (ret != NULL) {
			error = run_cmd(NULL, "%s -d -u %d",
			    _PATH_MDCONFIG, mdunit);
			if (error)
				warn("mdconfig (detach) detach failed: %s%s%d",
				    _PATH_DEV, MD_NAME, mdunit);
		}
	}
err:
	if (sfd != NULL)
		fclose(sfd);
	if (fd != -1)
		close(fd);
	free(vnodefile);
	return (ret);
}

static int
run_cmd(int *ofd, const char *cmdline, ...)
{
	va_list ap;
	char **argv, **argvp, *cmd, *p;
	int argc, pid, status, rv;
	int pfd[2], nfd, dup2dn;

	va_start(ap, cmdline);
	rv = vasprintf(&cmd, cmdline, ap);
	if (rv == -1) {
		warn("%s", __func__);
		va_end(ap);
		return (rv);
	}
	va_end(ap);

	for (argc = 1, p = cmd; (p = strchr(p, ' ')) != NULL; p++)
		argc++;
	argv = (char **)malloc(sizeof(*argv) * (argc + 1));
	for (p = cmd, argvp = argv; (*argvp = strsep(&p, " ")) != NULL;)
		if (**argvp != '\0' && (++argvp > &argv[argc])) {
			*argvp = NULL;
			break;
		}
	/* The argv array ends up NULL-terminated here. */
#if 0
	{
		int i;

		fprintf(stderr, "DEBUG: running:");
		/* Should be equivalent to 'cmd' (before strsep, of course). */
		for (i = 0; argv[i] != NULL; i++)
			fprintf(stderr, " %s", argv[i]);
		fprintf(stderr, "\n");
	}
#endif
	dup2dn = 1;
	if (ofd != NULL) {
		if (pipe(&pfd[0]) == -1) {
			warn("%s: pipe", __func__);
			return (-1);
		}
		*ofd = pfd[0];
		dup2dn = 0;
	}
	pid = fork();
	switch (pid) {
	case 0:
		/* Child process. */
		if (ofd != NULL)
			if (dup2(pfd[1], STDOUT_FILENO) < 0)
				err(1, "dup2 in %s", __func__);
		nfd = open(_PATH_DEVNULL, O_RDWR);
		if (nfd == -1)
			err(1, "%s: open %s", __func__, _PATH_DEVNULL);
		if (dup2(nfd, STDIN_FILENO) < 0)
			err(1, "%s: dup2", __func__);
		if (dup2dn && dup2(nfd, STDOUT_FILENO) < 0)
			err(1, "%s: dup2", __func__);
		if (dup2(nfd, STDERR_FILENO) < 0)
			err(1, "%s: dup2", __func__);
		execv(argv[0], argv);
		warn("exec: %s", argv[0]);
		_exit(-1);
	case -1:
		err(1, "%s: fork", __func__);
	}
	free(cmd);
	free(argv);
	while (waitpid(pid, &status, 0) != pid)
		;
	return (WEXITSTATUS(status));
}

static const char *
swap_on_off_sfile(const char *name, int doingall)
{
	int error;

	if (which_prog == SWAPON)
		error = swapon(name);
	else /* SWAPOFF */
		error = swapoff(name);

	if (error == -1) {
		switch (errno) {
		case EBUSY:
			if (doingall == 0)
				warnx("%s: Device already in use", name);
			break;
		case EINVAL:
			if (which_prog == SWAPON)
				warnx("%s: NSWAPDEV limit reached", name);
			else if (doingall == 0)
				warn("%s", name);
			break;
		default:
			warn("%s", name);
			break;
		}
		return (NULL);
	}
	return (name);
}

static void
usage(void)
{

	fprintf(stderr, "usage: %s ", getprogname());
	switch(orig_prog) {
	case SWAPON:
	case SWAPOFF:
	    fprintf(stderr, "[-F fstab] -aLq | file ...\n");
	    break;
	case SWAPCTL:
	    fprintf(stderr, "[-AghklmsU] [-a file ... | -d file ...]\n");
	    break;
	}
	exit(1);
}

static void
sizetobuf(char *buf, size_t bufsize, int hflag, long long val, int hlen,
    long blocksize)
{
	char tmp[16];

	if (hflag == 'H') {
		humanize_number(tmp, 5, (int64_t)val, "", HN_AUTOSCALE,
		    HN_B | HN_NOSPACE | HN_DECIMAL);
		snprintf(buf, bufsize, "%*s", hlen, tmp);
	} else
		snprintf(buf, bufsize, "%*lld", hlen, val / blocksize);
}

static void
swaplist(int lflag, int sflag, int hflag)
{
	size_t mibsize, size;
	struct xswdev xsw;
	int hlen, mib[16], n, pagesize;
	long blocksize;
	long long total = 0;
	long long used = 0;
	long long tmp_total;
	long long tmp_used;
	char buf[32];
	
	pagesize = getpagesize();
	switch(hflag) {
	case 'G':
		blocksize = 1024 * 1024 * 1024;
		strlcpy(buf, "1GB-blocks", sizeof(buf));
		hlen = 10;
		break;
	case 'H':
		blocksize = -1;
		strlcpy(buf, "Bytes", sizeof(buf));
		hlen = 10;
		break;
	case 'K':
		blocksize = 1024;
		strlcpy(buf, "1kB-blocks", sizeof(buf));
		hlen = 10;
		break;
	case 'M':
		blocksize = 1024 * 1024;
		strlcpy(buf, "1MB-blocks", sizeof(buf));
		hlen = 10;
		break;
	default:
		getbsize(&hlen, &blocksize);
		snprintf(buf, sizeof(buf), "%ld-blocks", blocksize);
		break;
	}
	
	mibsize = nitems(mib);
	if (sysctlnametomib("vm.swap_info", mib, &mibsize) == -1)
		err(1, "sysctlnametomib()");
	
	if (lflag) {
		printf("%-13s %*s %*s\n",
		    "Device:", 
		    hlen, buf,
		    hlen, "Used:");
	}
	
	for (n = 0; ; ++n) {
		mib[mibsize] = n;
		size = sizeof xsw;
		if (sysctl(mib, mibsize + 1, &xsw, &size, NULL, 0) == -1)
			break;
		if (xsw.xsw_version != XSWDEV_VERSION)
			errx(1, "xswdev version mismatch");
		
		tmp_total = (long long)xsw.xsw_nblks * pagesize;
		tmp_used  = (long long)xsw.xsw_used * pagesize;
		total += tmp_total;
		used  += tmp_used;
		if (lflag) {
			sizetobuf(buf, sizeof(buf), hflag, tmp_total, hlen,
			    blocksize);
			printf("/dev/%-8s %s ", devname(xsw.xsw_dev, S_IFCHR),
			    buf);
			sizetobuf(buf, sizeof(buf), hflag, tmp_used, hlen,
			    blocksize);
			printf("%s\n", buf);
		}
	}
	if (errno != ENOENT)
		err(1, "sysctl()");
	
	if (sflag) {
		sizetobuf(buf, sizeof(buf), hflag, total, hlen, blocksize);
		printf("Total:        %s ", buf);
		sizetobuf(buf, sizeof(buf), hflag, used, hlen, blocksize);
		printf("%s\n", buf);
	}
}

