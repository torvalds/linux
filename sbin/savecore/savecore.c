/*	$OpenBSD: savecore.c,v 1.67 2025/05/31 20:25:33 millert Exp $	*/
/*	$NetBSD: savecore.c,v 1.26 1996/03/18 21:16:05 leo Exp $	*/

/*-
 * Copyright (c) 1986, 1992, 1993
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

#include <sys/param.h>	/* NODEV DEV_BSIZE */
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/syslog.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <nlist.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <kvm.h>
#include <vis.h>

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))

extern FILE *zopen(const char *fname, const char *mode, int bits);

#define KREAD(kd, addr, p)\
	(kvm_read(kd, addr, (char *)(p), sizeof(*(p))) != sizeof(*(p)))

struct nlist current_nl[] = {	/* Namelist for currently running system. */
#define X_DUMPDEV	0
	{ "_dumpdev" },
#define X_DUMPLO	1
	{ "_dumplo" },
#define X_TIME		2
	{ "_time_second" },
#define	X_DUMPSIZE	3
	{ "_dumpsize" },
#define X_VERSION	4
	{ "_version" },
#define X_PANICSTR	5
	{ "_panicstr" },
#define	X_DUMPMAG	6
	{ "_dumpmag" },
	{ NULL },
};
int cursyms[] = { X_DUMPDEV, X_DUMPLO, X_VERSION, X_DUMPMAG, -1 };
int dumpsyms[] = { X_TIME, X_DUMPSIZE, X_VERSION, X_PANICSTR, X_DUMPMAG, -1 };

struct nlist dump_nl[] = {	/* Name list for dumped system. */
	{ "_dumpdev" },		/* Entries MUST be the same as */
	{ "_dumplo" },		/*	those in current_nl[].  */
	{ "_time_second" },
	{ "_dumpsize" },
	{ "_version" },
	{ "_panicstr" },
	{ "_dumpmag" },
	{ NULL },
};

#define VERSIONSIZE 512

/* Types match kernel declarations. */
long	dumplo;			/* where dump starts on dumpdev (in blocks) */
off_t	dumpoff;		/* where dump starts on dumpdev (in bytes) */
u_long	dumpmag;		/* magic number in dump */
int	dumppages;		/* amount of memory dumped (in pages) */
u_long	dumpsize;		/* amount of memory dumped */

char	*kernel;
char	*dirn;			/* directory to save dumps in */
char	*ddname;		/* name of dump device */
dev_t	dumpdev;		/* dump device */
int	dumpfd;			/* read/write descriptor on block dev */
kvm_t	*kd_dump;		/* kvm descriptor on block dev	*/
time_t	now;			/* current date */
char	panic_mesg[1024];
int	panicstr;
char	vers[VERSIONSIZE];

int	clear, zcompress, force, verbose;	/* flags */

void	 check_kmem(void);
int	 check_space(void);
void	 clear_dump(void);
int	 dump_exists(void);
char	*find_dev(dev_t, int);
int	 get_crashtime(void);
void	 kmem_setup(void);
char	*rawname(char *s);
void	 save_core(void);
void	 usage(void);

int
main(int argc, char *argv[])
{
	struct rlimit rl;
	int ch;

	openlog("savecore", LOG_PERROR, LOG_DAEMON);

	/* Increase our data size to the max if we can. */
	if (getrlimit(RLIMIT_DATA, &rl) == 0) {
		rl.rlim_cur = rl.rlim_max;
		if (setrlimit(RLIMIT_DATA, &rl) == -1)
			syslog(LOG_WARNING, "can't set rlimit data size: %m");
	}

	while ((ch = getopt(argc, argv, "cdfN:vz")) != -1)
		switch(ch) {
		case 'c':
			clear = 1;
			break;
		case 'd':		/* Not documented. */
		case 'v':
			verbose = 1;
			break;
		case 'f':
			force = 1;
			break;
		case 'N':
			kernel = optarg;
			break;
		case 'z':
			zcompress = 1;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (!clear) {
		if (argc != 1)
			usage();
		dirn = argv[0];
	}

	(void)time(&now);
	kmem_setup();

	if (!clear) {
		if (unveil(dirn, "rwc") == -1) {
			syslog(LOG_ERR, "unveil: %m");
			exit(1);
		}
		if (unveil(kernel ? kernel : _PATH_UNIX, "r") == -1) {
			syslog(LOG_ERR, "unveil: %m");
			exit(1);
		}
		if (unveil(rawname(ddname), "r") == -1) {
			syslog(LOG_ERR, "unveil: %m");
			exit(1);
		}
		if (pledge("stdio rpath wpath cpath", NULL) == -1) {
			syslog(LOG_ERR, "pledge: %m");
			exit(1);
		}
	} else {
		clear_dump();
		return (0);
	}

	if (!dump_exists() && !force)
		return (1);

	check_kmem();

	if (panicstr)
		syslog(LOG_ALERT, "reboot after panic: %s", panic_mesg);
	else
		syslog(LOG_ALERT, "reboot");

	if ((!get_crashtime() || !check_space()) && !force)
		return (1);

	save_core();

	clear_dump();
	return (0);
}

char	*dump_sys;

void
kmem_setup(void)
{
	kvm_t	*kd_kern;
	char	errbuf[_POSIX2_LINE_MAX];
	int	i, hdrsz;

	/*
	 * Some names we need for the currently running system, others for
	 * the system that was running when the dump was made.  The values
	 * obtained from the current system are used to look for things in
	 * /dev/kmem that cannot be found in the dump_sys namelist, but are
	 * presumed to be the same (since the disk partitions are probably
	 * the same!)
	 */
	kd_kern = kvm_openfiles(NULL, NULL, NULL, O_RDONLY, errbuf);
	if (kd_kern == NULL) {
		syslog(LOG_ERR, "%s: kvm_openfiles: %s", _PATH_UNIX, errbuf);
		exit(1);
	}
	if (kvm_nlist(kd_kern, current_nl) == -1)
		syslog(LOG_ERR, "%s: kvm_nlist: %s", _PATH_UNIX,
			kvm_geterr(kd_kern));

	for (i = 0; cursyms[i] != -1; i++)
		if (current_nl[cursyms[i]].n_value == 0) {
			syslog(LOG_ERR, "%s: %s not in namelist",
			    _PATH_UNIX, current_nl[cursyms[i]].n_name);
			exit(1);
		}

	(void)KREAD(kd_kern, current_nl[X_DUMPDEV].n_value, &dumpdev);
	if (dumpdev == NODEV) {
		syslog(LOG_WARNING, "no core dump (no dumpdev)");
		exit(1);
	}
	(void)KREAD(kd_kern, current_nl[X_DUMPLO].n_value, &dumplo);
	dumpoff = (off_t)dumplo * DEV_BSIZE;
	if (verbose)
		(void)printf("dumpoff = %lld (%ld * %d)\n",
		    (long long)dumpoff, dumplo, DEV_BSIZE);
	(void) KREAD(kd_kern, current_nl[X_DUMPMAG].n_value, &dumpmag);

	if (kernel == NULL) {
		if (kvm_read(kd_kern, current_nl[X_VERSION].n_value,
		    vers, sizeof(vers)) == -1) {
			syslog(LOG_ERR, "%s: kvm_read: version misread", _PATH_UNIX);
			exit(1);
		}
		vers[sizeof(vers) - 1] = '\0';
	}

	ddname = find_dev(dumpdev, S_IFBLK);
	dumpfd = open(ddname, O_RDWR);
	if (dumpfd == -1) {
		syslog(LOG_ERR, "%s: %m", ddname);
		exit(1);
	}


	dump_sys = kernel ? kernel : _PATH_UNIX;
	kd_dump = kvm_openfiles(kernel, ddname, NULL, O_RDWR, errbuf);
	if (kd_dump == NULL) {
		syslog(LOG_ERR, "%s: kvm_openfiles: %s", dump_sys, errbuf);
		exit(1);
	}

	if (kvm_nlist(kd_dump, dump_nl) == -1)
		syslog(LOG_ERR, "%s: kvm_nlist: %s", dump_sys,
			kvm_geterr(kd_dump));

	for (i = 0; dumpsyms[i] != -1; i++)
		if (dump_nl[dumpsyms[i]].n_value == 0) {
			syslog(LOG_ERR, "%s: %s not in namelist",
			    dump_sys, dump_nl[dumpsyms[i]].n_name);
			exit(1);
		}
	hdrsz = kvm_dump_mkheader(kd_dump, dumpoff);
	if (hdrsz == -1) {
		if(verbose)
			syslog(LOG_ERR, "%s: kvm_dump_mkheader: %s", dump_sys,
				kvm_geterr(kd_dump));
		syslog(LOG_WARNING, "no core dump");
		exit(1);
	}
	dumpoff += hdrsz;
	kvm_close(kd_kern);
}

void
check_kmem(void)
{
	char	*cp;
	int	panicloc;
	char core_vers[VERSIONSIZE];

	if (kvm_read(kd_dump, dump_nl[X_VERSION].n_value, core_vers,
	    sizeof(core_vers)) != sizeof(core_vers)) {
		syslog(LOG_ERR, "%s: kvm_read: version misread", dump_sys);
		exit(1);
	}
	core_vers[sizeof(core_vers) - 1] = '\0';

	if (strcmp(vers, core_vers) && kernel == 0) {
		vers[strcspn(vers, "\n")] = '\0';
		core_vers[strcspn(core_vers, "\n")] = '\0';

		syslog(LOG_WARNING,
		    "warning: %s version mismatch:\n\t%s\nand\t%s\n",
		    _PATH_UNIX, vers, core_vers);
	}

	(void)KREAD(kd_dump, dump_nl[X_PANICSTR].n_value, &panicstr);
	if (panicstr) {
		char	c, visout[5];
		size_t	vislen;

		cp       = panic_mesg;
		panicloc = panicstr;
		for (;;) {
			if (KREAD(kd_dump, panicloc, &c) != 0 || c == '\0')
				break;
			panicloc++;

			vis(visout, c, VIS_SAFE|VIS_NOSLASH, 0);
			vislen = strlen(visout);
			if (cp - panic_mesg + vislen >= sizeof(panic_mesg))
				break;
			strlcat(cp, visout,
			    panic_mesg + sizeof panic_mesg - cp);
			cp += strlen(cp);
		}
	}
}

int
dump_exists(void)
{
	u_long newdumpmag;

	(void)KREAD(kd_dump, dump_nl[X_DUMPMAG].n_value, &newdumpmag);

	/* Read the dump size. */
	(void)KREAD(kd_dump, dump_nl[X_DUMPSIZE].n_value, &dumppages);
	dumpsize = (u_long)dumppages * getpagesize();

	/*
	 * Return zero if core dump doesn't seem to be there and note
	 * it for syslog.  This check and return happens after the dump size
	 * is read, so dumpsize is whether or not the core is valid (for -f).
	 */
	if (newdumpmag != dumpmag) {
		if (verbose)
			syslog(LOG_WARNING,
			    "magic number mismatch (%lx != %lx)",
			    newdumpmag, dumpmag);
		syslog(LOG_WARNING, "no core dump");
		return (0);
	}
	return (1);
}

void
clear_dump(void)
{
	if (pledge("stdio", NULL) == -1) {
		syslog(LOG_ERR, "pledge: %m");
		exit(1);
	}

	if (kvm_dump_inval(kd_dump) == -1)
		syslog(LOG_ERR, "%s: kvm_clear_dump: %s", ddname,
			kvm_geterr(kd_dump));

}

char buf[1024 * 1024];

void
save_core(void)
{
	FILE *fp;
	int bounds, ifd, nr, nw, ofd = -1;
	char *rawp, path[PATH_MAX];
	mode_t um;

	um = umask(S_IRWXG|S_IRWXO);

	/*
	 * Get the current number and update the bounds file.  Do the update
	 * now, because we may fail later and don't want to overwrite anything.
	 */
	(void)snprintf(path, sizeof(path), "%s/bounds", dirn);
	if ((fp = fopen(path, "r")) == NULL)
		goto err1;
	if (fgets(buf, sizeof(buf), fp) == NULL) {
		if (ferror(fp))
err1:			syslog(LOG_WARNING, "%s: %s", path, strerror(errno));
		bounds = 0;
	} else {
		const char *errstr = NULL;
		char *p;

		if ((p = strchr(buf, '\n')) != NULL)
			*p = '\0';
		bounds = strtonum(buf, 0, INT_MAX, &errstr);
		if (errstr)
			syslog(LOG_WARNING, "bounds was corrupt: %s", errstr);
	}
	if (fp != NULL)
		(void)fclose(fp);
	if ((fp = fopen(path, "w")) == NULL)
		syslog(LOG_ERR, "%s: %m", path);
	else {
		(void)fprintf(fp, "%d\n", bounds + 1);
		(void)fclose(fp);
	}

	/* Create the core file. */
	(void)snprintf(path, sizeof(path), "%s%s.%d.core%s",
	    dirn, _PATH_UNIX, bounds, zcompress ? ".Z" : "");
	if (zcompress) {
		if ((fp = zopen(path, "w", 0)) == NULL) {
			syslog(LOG_ERR, "%s: %s", path, strerror(errno));
			exit(1);
		}
	} else {
		ofd = open(path, O_WRONLY | O_CREAT | O_TRUNC,
		    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		if (ofd == -1) {
			syslog(LOG_ERR, "%s: %m", path);
			exit(1);
		}
		
		fp  = fdopen(ofd, "w");
		if (fp == NULL) {
			syslog(LOG_ERR, "%s: fdopen: %s", path, strerror(errno));
			exit(1);
		}
	}

	/* Open the raw device. */
	rawp = rawname(ddname);
	if ((ifd = open(rawp, O_RDONLY)) == -1) {
		syslog(LOG_WARNING, "%s: %m; using block device", rawp);
		ifd = dumpfd;
	}

	/* Seek to the start of the core. */
	if (lseek(ifd, dumpoff, SEEK_SET) == -1) {
		syslog(LOG_ERR, "lseek: %m");
		exit(1);
	}

	if (kvm_dump_wrtheader(kd_dump, fp, dumpsize) == -1) {
		syslog(LOG_ERR, "kvm_dump_wrtheader: %s : %s", path,
			kvm_geterr(kd_dump));
		exit(1);
	}

	/* Copy the core file. */
	syslog(LOG_NOTICE, "writing %score to %s",
	    zcompress ? "compressed " : "", path);
	for (; dumpsize != 0; dumpsize -= nr) {
		(void)printf("%8luK\r", dumpsize / 1024);
		(void)fflush(stdout);
		nr = read(ifd, buf, MINIMUM(dumpsize, sizeof(buf)));
		if (nr <= 0) {
			if (nr == 0)
				syslog(LOG_WARNING,
				    "WARNING: EOF on dump device");
			else
				syslog(LOG_ERR, "%s: %m", rawp);
			goto err2;
		}
		nw = fwrite(buf, 1, nr, fp);
		if (nw != nr) {
			syslog(LOG_ERR, "%s: %s",
			    path, strerror(nw == 0 ? EIO : errno));
err2:			syslog(LOG_WARNING,
			    "WARNING: core may be incomplete");
			(void)printf("\n");
			exit(1);
		}
	}
	(void)close(ifd);
	(void)fclose(fp);

	/* Copy the kernel. */
	ifd = open(kernel ? kernel : _PATH_UNIX, O_RDONLY);
	if (ifd == -1) {
		syslog(LOG_ERR, "%s: %m", kernel ? kernel : _PATH_UNIX);
		exit(1);
	}
	(void)snprintf(path, sizeof(path), "%s%s.%d%s",
	    dirn, _PATH_UNIX, bounds, zcompress ? ".Z" : "");
	if (zcompress) {
		if ((fp = zopen(path, "w", 0)) == NULL) {
			syslog(LOG_ERR, "%s: %s", path, strerror(errno));
			exit(1);
		}
	} else {
		ofd = open(path, O_WRONLY | O_CREAT | O_TRUNC,
		    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		if (ofd == -1) {
			syslog(LOG_ERR, "%s: %m", path);
			exit(1);
		}
	}
	syslog(LOG_NOTICE, "writing %skernel to %s",
	    zcompress ? "compressed " : "", path);
	while ((nr = read(ifd, buf, sizeof(buf))) > 0) {
		if (zcompress)
			nw = fwrite(buf, 1, nr, fp);
		else
			nw = write(ofd, buf, nr);
		if (nw != nr) {
			syslog(LOG_ERR, "%s: %s",
			    path, strerror(nw == 0 ? EIO : errno));
			syslog(LOG_WARNING,
			    "WARNING: kernel may be incomplete");
			exit(1);
		}
	}
	if (nr == -1) {
		syslog(LOG_ERR, "%s: %s",
		    kernel ? kernel : _PATH_UNIX, strerror(errno));
		syslog(LOG_WARNING,
		    "WARNING: kernel may be incomplete");
		exit(1);
	}
	if (zcompress)
		(void)fclose(fp);
	else
		(void)close(ofd);
	(void)umask(um);
}

char *
find_dev(dev_t dev, int type)
{
	char *devpath, *name;

	name = devname(dev, type);
	if (strcmp(name, "??") == 0) {
		syslog(LOG_ERR, "can't find device %u/%u",
		    major(dev), minor(dev));
		exit(1);
	}
	if (asprintf(&devpath, "%s%s", _PATH_DEV, name) == -1) {
		syslog(LOG_ERR, "%s", strerror(errno));
		exit(1);
	}
	return (devpath);
}

char *
rawname(char *s)
{
	char *sl, name[PATH_MAX];

	if ((sl = strrchr(s, '/')) == NULL || sl[1] == '0') {
		syslog(LOG_ERR,
		    "can't make raw dump device name from %s", s);
		return (s);
	}
	(void)snprintf(name, sizeof(name), "%.*s/r%s", (int)(sl - s), s, sl + 1);
	if ((sl = strdup(name)) == NULL) {
		syslog(LOG_ERR, "%s", strerror(errno));
		exit(1);
	}
	return (sl);
}

int
get_crashtime(void)
{
	time_t dumptime;			/* Time the dump was taken. */
	char *ct;

	(void)KREAD(kd_dump, dump_nl[X_TIME].n_value, &dumptime);
	if (dumptime == 0) {
		if (verbose)
			syslog(LOG_ERR, "dump time is zero");
		return (0);
	}
	ct = ctime(&dumptime);
	if (ct)
		printf("savecore: system went down at %s", ct);
	else
		printf("savecore: system went down %lld seconds after the"
		    " epoch\n", dumptime);
#define	SECSPERDAY	(24 * 60 * 60)
#define	LEEWAY		(7 * SECSPERDAY)
	if (dumptime < now - LEEWAY || dumptime > now + LEEWAY) {
		(void)printf("dump time is unreasonable\n");
		return (0);
	}
	return (1);
}

int
check_space(void)
{
	FILE *fp;
	char *tkernel;
	off_t minfree, spacefree, kernelsize, needed;
	struct stat st;
	struct statfs fsbuf;
	char buf[100], path[PATH_MAX];
	int fd;

	tkernel = kernel ? kernel : _PATH_UNIX;
	if (stat(tkernel, &st) == -1) {
		syslog(LOG_ERR, "%s: %m", tkernel);
		exit(1);
	}
	kernelsize = st.st_blocks * S_BLKSIZE;
	if ((fd = open(dirn, O_RDONLY)) == -1 || fstatfs(fd, &fsbuf) == -1) {
		syslog(LOG_ERR, "%s: %m", dirn);
		exit(1);
	}
	close(fd);
	spacefree = ((off_t)fsbuf.f_bavail * fsbuf.f_bsize) / 1024;

	(void)snprintf(path, sizeof(path), "%s/minfree", dirn);
	if ((fp = fopen(path, "r")) == NULL)
		minfree = 0;
	else {
		if (fgets(buf, sizeof(buf), fp) == NULL)
			minfree = 0;
		else {
			const char *errstr;
			char *p;

			if ((p = strchr(buf, '\n')) != NULL)
				*p = '\0';
			minfree = strtonum(buf, 0, LLONG_MAX, &errstr);
			if (errstr)
				syslog(LOG_WARNING,
				    "minfree was corrupt: %s", errstr);
		}
		(void)fclose(fp);
	}

	needed = (dumpsize + kernelsize) / 1024;
	if (minfree > 0 && spacefree - needed < minfree) {
		syslog(LOG_WARNING,
		    "no dump, not enough free space on device");
		return (0);
	}
	if (spacefree - needed < minfree)
		syslog(LOG_WARNING,
		    "dump performed, but free space threshold crossed");
	return (1);
}

void
usage(void)
{
	extern char *__progname;
	fprintf(stderr, "usage: %s [-cfvz] [-N system] directory\n",
		__progname);
	exit(1);
}
