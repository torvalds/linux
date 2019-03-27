/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2002 Poul-Henning Kamp
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Poul-Henning Kamp
 * and NAI Labs, the Security Research Division of Network Associates, Inc.
 * under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the
 * DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/disk.h>
#include <sys/kerneldump.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <capsicum_helpers.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <fstab.h>
#include <paths.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <libcasper.h>
#include <casper/cap_fileargs.h>
#include <casper/cap_syslog.h>

#include <libxo/xo.h>

/* The size of the buffer used for I/O. */
#define	BUFFERSIZE	(1024*1024)

#define	STATUS_BAD	0
#define	STATUS_GOOD	1
#define	STATUS_UNKNOWN	2

static cap_channel_t *capsyslog;
static fileargs_t *capfa;
static int checkfor, compress, clear, force, keep, verbose;	/* flags */
static int nfound, nsaved, nerr;			/* statistics */
static int maxdumps;

extern FILE *zdopen(int, const char *);

static sig_atomic_t got_siginfo;
static void infohandler(int);

static void
logmsg(int pri, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (capsyslog != NULL)
		cap_vsyslog(capsyslog, pri, fmt, ap);
	else
		vsyslog(pri, fmt, ap);
	va_end(ap);
}

static FILE *
xfopenat(int dirfd, const char *path, int flags, const char *modestr, ...)
{
	va_list ap;
	FILE *fp;
	mode_t mode;
	int error, fd;

	if ((flags & O_CREAT) == O_CREAT) {
		va_start(ap, modestr);
		mode = (mode_t)va_arg(ap, int);
		va_end(ap);
	} else
		mode = 0;

	fd = openat(dirfd, path, flags, mode);
	if (fd < 0)
		return (NULL);
	fp = fdopen(fd, modestr);
	if (fp == NULL) {
		error = errno;
		(void)close(fd);
		errno = error;
	}
	return (fp);
}

static void
printheader(xo_handle_t *xo, const struct kerneldumpheader *h,
    const char *device, int bounds, const int status)
{
	uint64_t dumplen;
	time_t t;
	const char *stat_str;
	const char *comp_str;

	xo_flush_h(xo);
	xo_emit_h(xo, "{Lwc:Dump header from device}{:dump_device/%s}\n",
	    device);
	xo_emit_h(xo, "{P:  }{Lwc:Architecture}{:architecture/%s}\n",
	    h->architecture);
	xo_emit_h(xo,
	    "{P:  }{Lwc:Architecture Version}{:architecture_version/%u}\n",
	    dtoh32(h->architectureversion));
	dumplen = dtoh64(h->dumplength);
	xo_emit_h(xo, "{P:  }{Lwc:Dump Length}{:dump_length_bytes/%lld}\n",
	    (long long)dumplen);
	xo_emit_h(xo, "{P:  }{Lwc:Blocksize}{:blocksize/%d}\n",
	    dtoh32(h->blocksize));
	switch (h->compression) {
	case KERNELDUMP_COMP_NONE:
		comp_str = "none";
		break;
	case KERNELDUMP_COMP_GZIP:
		comp_str = "gzip";
		break;
	case KERNELDUMP_COMP_ZSTD:
		comp_str = "zstd";
		break;
	default:
		comp_str = "???";
		break;
	}
	xo_emit_h(xo, "{P:  }{Lwc:Compression}{:compression/%s}\n", comp_str);
	t = dtoh64(h->dumptime);
	xo_emit_h(xo, "{P:  }{Lwc:Dumptime}{:dumptime/%s}", ctime(&t));
	xo_emit_h(xo, "{P:  }{Lwc:Hostname}{:hostname/%s}\n", h->hostname);
	xo_emit_h(xo, "{P:  }{Lwc:Magic}{:magic/%s}\n", h->magic);
	xo_emit_h(xo, "{P:  }{Lwc:Version String}{:version_string/%s}",
	    h->versionstring);
	xo_emit_h(xo, "{P:  }{Lwc:Panic String}{:panic_string/%s}\n",
	    h->panicstring);
	xo_emit_h(xo, "{P:  }{Lwc:Dump Parity}{:dump_parity/%u}\n", h->parity);
	xo_emit_h(xo, "{P:  }{Lwc:Bounds}{:bounds/%d}\n", bounds);

	switch (status) {
	case STATUS_BAD:
		stat_str = "bad";
		break;
	case STATUS_GOOD:
		stat_str = "good";
		break;
	default:
		stat_str = "unknown";
		break;
	}
	xo_emit_h(xo, "{P:  }{Lwc:Dump Status}{:dump_status/%s}\n", stat_str);
	xo_flush_h(xo);
}

static int
getbounds(int savedirfd)
{
	FILE *fp;
	char buf[6];
	int ret;

	/*
	 * If we are just checking, then we haven't done a chdir to the dump
	 * directory and we should not try to read a bounds file.
	 */
	if (checkfor)
		return (0);

	ret = 0;

	if ((fp = xfopenat(savedirfd, "bounds", O_RDONLY, "r")) == NULL) {
		if (verbose)
			printf("unable to open bounds file, using 0\n");
		return (ret);
	}
	if (fgets(buf, sizeof(buf), fp) == NULL) {
		if (feof(fp))
			logmsg(LOG_WARNING, "bounds file is empty, using 0");
		else
			logmsg(LOG_WARNING, "bounds file: %s", strerror(errno));
		fclose(fp);
		return (ret);
	}

	errno = 0;
	ret = (int)strtol(buf, NULL, 10);
	if (ret == 0 && (errno == EINVAL || errno == ERANGE))
		logmsg(LOG_WARNING, "invalid value found in bounds, using 0");
	fclose(fp);
	return (ret);
}

static void
writebounds(int savedirfd, int bounds)
{
	FILE *fp;

	if ((fp = xfopenat(savedirfd, "bounds", O_WRONLY | O_CREAT | O_TRUNC,
	    "w", 0644)) == NULL) {
		logmsg(LOG_WARNING, "unable to write to bounds file: %m");
		return;
	}

	if (verbose)
		printf("bounds number: %d\n", bounds);

	fprintf(fp, "%d\n", bounds);
	fclose(fp);
}

static bool
writekey(int savedirfd, const char *keyname, uint8_t *dumpkey,
    uint32_t dumpkeysize)
{
	int fd;

	fd = openat(savedirfd, keyname, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd == -1) {
		logmsg(LOG_ERR, "Unable to open %s to write the key: %m.",
		    keyname);
		return (false);
	}

	if (write(fd, dumpkey, dumpkeysize) != (ssize_t)dumpkeysize) {
		logmsg(LOG_ERR, "Unable to write the key to %s: %m.", keyname);
		close(fd);
		return (false);
	}

	close(fd);
	return (true);
}

static off_t
file_size(int savedirfd, const char *path)
{
	struct stat sb;

	/* Ignore all errors, this file may not exist. */
	if (fstatat(savedirfd, path, &sb, 0) == -1)
		return (0);
	return (sb.st_size);
}

static off_t
saved_dump_size(int savedirfd, int bounds)
{
	static char path[PATH_MAX];
	off_t dumpsize;

	dumpsize = 0;

	(void)snprintf(path, sizeof(path), "info.%d", bounds);
	dumpsize += file_size(savedirfd, path);
	(void)snprintf(path, sizeof(path), "vmcore.%d", bounds);
	dumpsize += file_size(savedirfd, path);
	(void)snprintf(path, sizeof(path), "vmcore.%d.gz", bounds);
	dumpsize += file_size(savedirfd, path);
	(void)snprintf(path, sizeof(path), "vmcore.%d.zst", bounds);
	dumpsize += file_size(savedirfd, path);
	(void)snprintf(path, sizeof(path), "textdump.tar.%d", bounds);
	dumpsize += file_size(savedirfd, path);
	(void)snprintf(path, sizeof(path), "textdump.tar.%d.gz", bounds);
	dumpsize += file_size(savedirfd, path);

	return (dumpsize);
}

static void
saved_dump_remove(int savedirfd, int bounds)
{
	static char path[PATH_MAX];

	(void)snprintf(path, sizeof(path), "info.%d", bounds);
	(void)unlinkat(savedirfd, path, 0);
	(void)snprintf(path, sizeof(path), "vmcore.%d", bounds);
	(void)unlinkat(savedirfd, path, 0);
	(void)snprintf(path, sizeof(path), "vmcore.%d.gz", bounds);
	(void)unlinkat(savedirfd, path, 0);
	(void)snprintf(path, sizeof(path), "vmcore.%d.zst", bounds);
	(void)unlinkat(savedirfd, path, 0);
	(void)snprintf(path, sizeof(path), "textdump.tar.%d", bounds);
	(void)unlinkat(savedirfd, path, 0);
	(void)snprintf(path, sizeof(path), "textdump.tar.%d.gz", bounds);
	(void)unlinkat(savedirfd, path, 0);
}

static void
symlinks_remove(int savedirfd)
{

	(void)unlinkat(savedirfd, "info.last", 0);
	(void)unlinkat(savedirfd, "key.last", 0);
	(void)unlinkat(savedirfd, "vmcore.last", 0);
	(void)unlinkat(savedirfd, "vmcore.last.gz", 0);
	(void)unlinkat(savedirfd, "vmcore.last.zst", 0);
	(void)unlinkat(savedirfd, "vmcore_encrypted.last", 0);
	(void)unlinkat(savedirfd, "vmcore_encrypted.last.gz", 0);
	(void)unlinkat(savedirfd, "textdump.tar.last", 0);
	(void)unlinkat(savedirfd, "textdump.tar.last.gz", 0);
}

/*
 * Check that sufficient space is available on the disk that holds the
 * save directory.
 */
static int
check_space(const char *savedir, int savedirfd, off_t dumpsize, int bounds)
{
	char buf[100];
	struct statfs fsbuf;
	FILE *fp;
	off_t available, minfree, spacefree, totfree, needed;

	if (fstatfs(savedirfd, &fsbuf) < 0) {
		logmsg(LOG_ERR, "%s: %m", savedir);
		exit(1);
	}
	spacefree = ((off_t) fsbuf.f_bavail * fsbuf.f_bsize) / 1024;
	totfree = ((off_t) fsbuf.f_bfree * fsbuf.f_bsize) / 1024;

	if ((fp = xfopenat(savedirfd, "minfree", O_RDONLY, "r")) == NULL)
		minfree = 0;
	else {
		if (fgets(buf, sizeof(buf), fp) == NULL)
			minfree = 0;
		else {
			char *endp;

			errno = 0;
			minfree = strtoll(buf, &endp, 10);
			if (minfree == 0 && errno != 0)
				minfree = -1;
			else {
				while (*endp != '\0' && isspace(*endp))
					endp++;
				if (*endp != '\0' || minfree < 0)
					minfree = -1;
			}
			if (minfree < 0)
				logmsg(LOG_WARNING,
				    "`minfree` didn't contain a valid size "
				    "(`%s`). Defaulting to 0", buf);
		}
		(void)fclose(fp);
	}

	available = minfree > 0 ? spacefree - minfree : totfree;
	needed = dumpsize / 1024 + 2;	/* 2 for info file */
	needed -= saved_dump_size(savedirfd, bounds);
	if (available < needed) {
		logmsg(LOG_WARNING,
		    "no dump: not enough free space on device (need at least "
		    "%jdkB for dump; %jdkB available; %jdkB reserved)",
		    (intmax_t)needed,
		    (intmax_t)available + minfree,
		    (intmax_t)minfree);
		return (0);
	}
	if (spacefree - needed < 0)
		logmsg(LOG_WARNING,
		    "dump performed, but free space threshold crossed");
	return (1);
}

static bool
compare_magic(const struct kerneldumpheader *kdh, const char *magic)
{

	return (strncmp(kdh->magic, magic, sizeof(kdh->magic)) == 0);
}

#define BLOCKSIZE (1<<12)
#define BLOCKMASK (~(BLOCKSIZE-1))

static int
DoRegularFile(int fd, off_t dumpsize, u_int sectorsize, bool sparse, char *buf,
    const char *device, const char *filename, FILE *fp)
{
	int he, hs, nr, nw, wl;
	off_t dmpcnt, origsize;

	dmpcnt = 0;
	origsize = dumpsize;
	he = 0;
	while (dumpsize > 0) {
		wl = BUFFERSIZE;
		if (wl > dumpsize)
			wl = dumpsize;
		nr = read(fd, buf, roundup(wl, sectorsize));
		if (nr != (int)roundup(wl, sectorsize)) {
			if (nr == 0)
				logmsg(LOG_WARNING,
				    "WARNING: EOF on dump device");
			else
				logmsg(LOG_ERR, "read error on %s: %m", device);
			nerr++;
			return (-1);
		}
		if (!sparse) {
			nw = fwrite(buf, 1, wl, fp);
		} else {
			for (nw = 0; nw < nr; nw = he) {
				/* find a contiguous block of zeroes */
				for (hs = nw; hs < nr; hs += BLOCKSIZE) {
					for (he = hs; he < nr && buf[he] == 0;
					    ++he)
						/* nothing */ ;
					/* is the hole long enough to matter? */
					if (he >= hs + BLOCKSIZE)
						break;
				}

				/* back down to a block boundary */
				he &= BLOCKMASK;

				/*
				 * 1) Don't go beyond the end of the buffer.
				 * 2) If the end of the buffer is less than
				 *    BLOCKSIZE bytes away, we're at the end
				 *    of the file, so just grab what's left.
				 */
				if (hs + BLOCKSIZE > nr)
					hs = he = nr;

				/*
				 * At this point, we have a partial ordering:
				 *     nw <= hs <= he <= nr
				 * If hs > nw, buf[nw..hs] contains non-zero
				 * data. If he > hs, buf[hs..he] is all zeroes.
				 */
				if (hs > nw)
					if (fwrite(buf + nw, hs - nw, 1, fp)
					    != 1)
					break;
				if (he > hs)
					if (fseeko(fp, he - hs, SEEK_CUR) == -1)
						break;
			}
		}
		if (nw != wl) {
			logmsg(LOG_ERR,
			    "write error on %s file: %m", filename);
			logmsg(LOG_WARNING,
			    "WARNING: vmcore may be incomplete");
			nerr++;
			return (-1);
		}
		if (verbose) {
			dmpcnt += wl;
			printf("%llu\r", (unsigned long long)dmpcnt);
			fflush(stdout);
		}
		dumpsize -= wl;
		if (got_siginfo) {
			printf("%s %.1lf%%\n", filename, (100.0 - (100.0 *
			    (double)dumpsize / (double)origsize)));
			got_siginfo = 0;
		}
	}
	return (0);
}

/*
 * Specialized version of dump-reading logic for use with textdumps, which
 * are written backwards from the end of the partition, and must be reversed
 * before being written to the file.  Textdumps are small, so do a bit less
 * work to optimize/sparsify.
 */
static int
DoTextdumpFile(int fd, off_t dumpsize, off_t lasthd, char *buf,
    const char *device, const char *filename, FILE *fp)
{
	int nr, nw, wl;
	off_t dmpcnt, totsize;

	totsize = dumpsize;
	dmpcnt = 0;
	wl = 512;
	if ((dumpsize % wl) != 0) {
		logmsg(LOG_ERR, "textdump uneven multiple of 512 on %s",
		    device);
		nerr++;
		return (-1);
	}
	while (dumpsize > 0) {
		nr = pread(fd, buf, wl, lasthd - (totsize - dumpsize) - wl);
		if (nr != wl) {
			if (nr == 0)
				logmsg(LOG_WARNING,
				    "WARNING: EOF on dump device");
			else
				logmsg(LOG_ERR, "read error on %s: %m", device);
			nerr++;
			return (-1);
		}
		nw = fwrite(buf, 1, wl, fp);
		if (nw != wl) {
			logmsg(LOG_ERR,
			    "write error on %s file: %m", filename);
			logmsg(LOG_WARNING,
			    "WARNING: textdump may be incomplete");
			nerr++;
			return (-1);
		}
		if (verbose) {
			dmpcnt += wl;
			printf("%llu\r", (unsigned long long)dmpcnt);
			fflush(stdout);
		}
		dumpsize -= wl;
	}
	return (0);
}

static void
DoFile(const char *savedir, int savedirfd, const char *device)
{
	xo_handle_t *xostdout, *xoinfo;
	static char infoname[PATH_MAX], corename[PATH_MAX], linkname[PATH_MAX];
	static char keyname[PATH_MAX];
	static char *buf = NULL;
	char *temp = NULL;
	struct kerneldumpheader kdhf, kdhl;
	uint8_t *dumpkey;
	off_t mediasize, dumpextent, dumplength, firsthd, lasthd;
	FILE *core, *info;
	int fdcore, fddev, error;
	int bounds, status;
	u_int sectorsize, xostyle;
	uint32_t dumpkeysize;
	bool iscompressed, isencrypted, istextdump, ret;

	bounds = getbounds(savedirfd);
	dumpkey = NULL;
	mediasize = 0;
	status = STATUS_UNKNOWN;

	xostdout = xo_create_to_file(stdout, XO_STYLE_TEXT, 0);
	if (xostdout == NULL) {
		logmsg(LOG_ERR, "%s: %m", infoname);
		return;
	}

	if (maxdumps > 0 && bounds == maxdumps)
		bounds = 0;

	if (buf == NULL) {
		buf = malloc(BUFFERSIZE);
		if (buf == NULL) {
			logmsg(LOG_ERR, "%m");
			return;
		}
	}

	if (verbose)
		printf("checking for kernel dump on device %s\n", device);

	fddev = fileargs_open(capfa, device);
	if (fddev < 0) {
		logmsg(LOG_ERR, "%s: %m", device);
		return;
	}

	error = ioctl(fddev, DIOCGMEDIASIZE, &mediasize);
	if (!error)
		error = ioctl(fddev, DIOCGSECTORSIZE, &sectorsize);
	if (error) {
		logmsg(LOG_ERR,
		    "couldn't find media and/or sector size of %s: %m", device);
		goto closefd;
	}

	if (verbose) {
		printf("mediasize = %lld bytes\n", (long long)mediasize);
		printf("sectorsize = %u bytes\n", sectorsize);
	}

	if (sectorsize < sizeof(kdhl)) {
		logmsg(LOG_ERR,
		    "Sector size is less the kernel dump header %zu",
		    sizeof(kdhl));
		goto closefd;
	}

	lasthd = mediasize - sectorsize;
	temp = malloc(sectorsize);
	if (temp == NULL) {
		logmsg(LOG_ERR, "%m");
		goto closefd;
	}
	if (lseek(fddev, lasthd, SEEK_SET) != lasthd ||
	    read(fddev, temp, sectorsize) != (ssize_t)sectorsize) {
		logmsg(LOG_ERR,
		    "error reading last dump header at offset %lld in %s: %m",
		    (long long)lasthd, device);
		goto closefd;
	}
	memcpy(&kdhl, temp, sizeof(kdhl));
	iscompressed = istextdump = false;
	if (compare_magic(&kdhl, TEXTDUMPMAGIC)) {
		if (verbose)
			printf("textdump magic on last dump header on %s\n",
			    device);
		istextdump = true;
		if (dtoh32(kdhl.version) != KERNELDUMP_TEXT_VERSION) {
			logmsg(LOG_ERR,
			    "unknown version (%d) in last dump header on %s",
			    dtoh32(kdhl.version), device);

			status = STATUS_BAD;
			if (force == 0)
				goto closefd;
		}
	} else if (compare_magic(&kdhl, KERNELDUMPMAGIC)) {
		if (dtoh32(kdhl.version) != KERNELDUMPVERSION) {
			logmsg(LOG_ERR,
			    "unknown version (%d) in last dump header on %s",
			    dtoh32(kdhl.version), device);

			status = STATUS_BAD;
			if (force == 0)
				goto closefd;
		}
		switch (kdhl.compression) {
		case KERNELDUMP_COMP_NONE:
			break;
		case KERNELDUMP_COMP_GZIP:
		case KERNELDUMP_COMP_ZSTD:
			if (compress && verbose)
				printf("dump is already compressed\n");
			compress = false;
			iscompressed = true;
			break;
		default:
			logmsg(LOG_ERR, "unknown compression type %d on %s",
			    kdhl.compression, device);
			break;
		}
	} else {
		if (verbose)
			printf("magic mismatch on last dump header on %s\n",
			    device);

		status = STATUS_BAD;
		if (force == 0)
			goto closefd;

		if (compare_magic(&kdhl, KERNELDUMPMAGIC_CLEARED)) {
			if (verbose)
				printf("forcing magic on %s\n", device);
			memcpy(kdhl.magic, KERNELDUMPMAGIC, sizeof(kdhl.magic));
		} else {
			logmsg(LOG_ERR, "unable to force dump - bad magic");
			goto closefd;
		}
		if (dtoh32(kdhl.version) != KERNELDUMPVERSION) {
			logmsg(LOG_ERR,
			    "unknown version (%d) in last dump header on %s",
			    dtoh32(kdhl.version), device);

			status = STATUS_BAD;
			if (force == 0)
				goto closefd;
		}
	}

	nfound++;
	if (clear)
		goto nuke;

	if (kerneldump_parity(&kdhl)) {
		logmsg(LOG_ERR,
		    "parity error on last dump header on %s", device);
		nerr++;
		status = STATUS_BAD;
		if (force == 0)
			goto closefd;
	}
	dumpextent = dtoh64(kdhl.dumpextent);
	dumplength = dtoh64(kdhl.dumplength);
	dumpkeysize = dtoh32(kdhl.dumpkeysize);
	firsthd = lasthd - dumpextent - sectorsize - dumpkeysize;
	if (lseek(fddev, firsthd, SEEK_SET) != firsthd ||
	    read(fddev, temp, sectorsize) != (ssize_t)sectorsize) {
		logmsg(LOG_ERR,
		    "error reading first dump header at offset %lld in %s: %m",
		    (long long)firsthd, device);
		nerr++;
		goto closefd;
	}
	memcpy(&kdhf, temp, sizeof(kdhf));

	if (verbose >= 2) {
		printf("First dump headers:\n");
		printheader(xostdout, &kdhf, device, bounds, -1);

		printf("\nLast dump headers:\n");
		printheader(xostdout, &kdhl, device, bounds, -1);
		printf("\n");
	}

	if (memcmp(&kdhl, &kdhf, sizeof(kdhl))) {
		logmsg(LOG_ERR,
		    "first and last dump headers disagree on %s", device);
		nerr++;
		status = STATUS_BAD;
		if (force == 0)
			goto closefd;
	} else {
		status = STATUS_GOOD;
	}

	if (checkfor) {
		printf("A dump exists on %s\n", device);
		close(fddev);
		exit(0);
	}

	if (kdhl.panicstring[0] != '\0')
		logmsg(LOG_ALERT, "reboot after panic: %.*s",
		    (int)sizeof(kdhl.panicstring), kdhl.panicstring);
	else
		logmsg(LOG_ALERT, "reboot");

	if (verbose)
		printf("Checking for available free space\n");

	if (!check_space(savedir, savedirfd, dumplength, bounds)) {
		nerr++;
		goto closefd;
	}

	writebounds(savedirfd, bounds + 1);

	saved_dump_remove(savedirfd, bounds);

	snprintf(infoname, sizeof(infoname), "info.%d", bounds);

	/*
	 * Create or overwrite any existing dump header files.
	 */
	if ((info = xfopenat(savedirfd, infoname,
	    O_WRONLY | O_CREAT | O_TRUNC, "w", 0600)) == NULL) {
		logmsg(LOG_ERR, "open(%s): %m", infoname);
		nerr++;
		goto closefd;
	}

	isencrypted = (dumpkeysize > 0);
	if (compress)
		snprintf(corename, sizeof(corename), "%s.%d.gz",
		    istextdump ? "textdump.tar" :
		    (isencrypted ? "vmcore_encrypted" : "vmcore"), bounds);
	else if (iscompressed && !isencrypted)
		snprintf(corename, sizeof(corename), "vmcore.%d.%s", bounds,
		    (kdhl.compression == KERNELDUMP_COMP_GZIP) ? "gz" : "zst");
	else
		snprintf(corename, sizeof(corename), "%s.%d",
		    istextdump ? "textdump.tar" :
		    (isencrypted ? "vmcore_encrypted" : "vmcore"), bounds);
	fdcore = openat(savedirfd, corename, O_WRONLY | O_CREAT | O_TRUNC,
	    0600);
	if (fdcore < 0) {
		logmsg(LOG_ERR, "open(%s): %m", corename);
		fclose(info);
		nerr++;
		goto closefd;
	}

	if (compress)
		core = zdopen(fdcore, "w");
	else
		core = fdopen(fdcore, "w");
	if (core == NULL) {
		logmsg(LOG_ERR, "%s: %m", corename);
		(void)close(fdcore);
		(void)fclose(info);
		nerr++;
		goto closefd;
	}
	fdcore = -1;

	xostyle = xo_get_style(NULL);
	xoinfo = xo_create_to_file(info, xostyle, 0);
	if (xoinfo == NULL) {
		logmsg(LOG_ERR, "%s: %m", infoname);
		fclose(info);
		nerr++;
		goto closeall;
	}
	xo_open_container_h(xoinfo, "crashdump");

	if (verbose)
		printheader(xostdout, &kdhl, device, bounds, status);

	printheader(xoinfo, &kdhl, device, bounds, status);
	xo_close_container_h(xoinfo, "crashdump");
	xo_flush_h(xoinfo);
	xo_finish_h(xoinfo);
	fclose(info);

	if (isencrypted) {
		dumpkey = calloc(1, dumpkeysize);
		if (dumpkey == NULL) {
			logmsg(LOG_ERR, "Unable to allocate kernel dump key.");
			nerr++;
			goto closeall;
		}

		if (read(fddev, dumpkey, dumpkeysize) != (ssize_t)dumpkeysize) {
			logmsg(LOG_ERR, "Unable to read kernel dump key: %m.");
			nerr++;
			goto closeall;
		}

		snprintf(keyname, sizeof(keyname), "key.%d", bounds);
		ret = writekey(savedirfd, keyname, dumpkey, dumpkeysize);
		explicit_bzero(dumpkey, dumpkeysize);
		if (!ret) {
			nerr++;
			goto closeall;
		}
	}

	logmsg(LOG_NOTICE, "writing %s%score to %s/%s",
	    isencrypted ? "encrypted " : "", compress ? "compressed " : "",
	    savedir, corename);

	if (istextdump) {
		if (DoTextdumpFile(fddev, dumplength, lasthd, buf, device,
		    corename, core) < 0)
			goto closeall;
	} else {
		if (DoRegularFile(fddev, dumplength, sectorsize,
		    !(compress || iscompressed || isencrypted), buf, device,
		    corename, core) < 0) {
			goto closeall;
		}
	}
	if (verbose)
		printf("\n");

	if (fclose(core) < 0) {
		logmsg(LOG_ERR, "error on %s: %m", corename);
		nerr++;
		goto closefd;
	}

	symlinks_remove(savedirfd);
	if (symlinkat(infoname, savedirfd, "info.last") == -1) {
		logmsg(LOG_WARNING, "unable to create symlink %s/%s: %m",
		    savedir, "info.last");
	}
	if (isencrypted) {
		if (symlinkat(keyname, savedirfd, "key.last") == -1) {
			logmsg(LOG_WARNING,
			    "unable to create symlink %s/%s: %m", savedir,
			    "key.last");
		}
	}
	if (compress || iscompressed) {
		snprintf(linkname, sizeof(linkname), "%s.last.%s",
		    istextdump ? "textdump.tar" :
		    (isencrypted ? "vmcore_encrypted" : "vmcore"),
		    (kdhl.compression == KERNELDUMP_COMP_ZSTD) ? "zst" : "gz");
	} else {
		snprintf(linkname, sizeof(linkname), "%s.last",
		    istextdump ? "textdump.tar" :
		    (isencrypted ? "vmcore_encrypted" : "vmcore"));
	}
	if (symlinkat(corename, savedirfd, linkname) == -1) {
		logmsg(LOG_WARNING, "unable to create symlink %s/%s: %m",
		    savedir, linkname);
	}

	nsaved++;

	if (verbose)
		printf("dump saved\n");

nuke:
	if (!keep) {
		if (verbose)
			printf("clearing dump header\n");
		memcpy(kdhl.magic, KERNELDUMPMAGIC_CLEARED, sizeof(kdhl.magic));
		memcpy(temp, &kdhl, sizeof(kdhl));
		if (lseek(fddev, lasthd, SEEK_SET) != lasthd ||
		    write(fddev, temp, sectorsize) != (ssize_t)sectorsize)
			logmsg(LOG_ERR,
			    "error while clearing the dump header: %m");
	}
	xo_close_container_h(xostdout, "crashdump");
	xo_finish_h(xostdout);
	free(dumpkey);
	free(temp);
	close(fddev);
	return;

closeall:
	fclose(core);

closefd:
	free(dumpkey);
	free(temp);
	close(fddev);
}

static char **
enum_dumpdevs(int *argcp)
{
	struct fstab *fsp;
	char **argv;
	int argc, n;

	/*
	 * We cannot use getfsent(3) in capability mode, so we must
	 * scan /etc/fstab and build up a list of candidate devices
	 * before proceeding.
	 */
	argc = 0;
	n = 8;
	argv = malloc(n * sizeof(*argv));
	if (argv == NULL) {
		logmsg(LOG_ERR, "malloc(): %m");
		exit(1);
	}
	for (;;) {
		fsp = getfsent();
		if (fsp == NULL)
			break;
		if (strcmp(fsp->fs_vfstype, "swap") != 0 &&
		    strcmp(fsp->fs_vfstype, "dump") != 0)
			continue;
		if (argc >= n) {
			n *= 2;
			argv = realloc(argv, n * sizeof(*argv));
			if (argv == NULL) {
				logmsg(LOG_ERR, "realloc(): %m");
				exit(1);
			}
		}
		argv[argc] = strdup(fsp->fs_spec);
		if (argv[argc] == NULL) {
			logmsg(LOG_ERR, "strdup(): %m");
			exit(1);
		}
		argc++;
	}
	*argcp = argc;
	return (argv);
}

static void
init_caps(int argc, char **argv)
{
	cap_rights_t rights;
	cap_channel_t *capcas;

	capcas = cap_init();
	if (capcas == NULL) {
		logmsg(LOG_ERR, "cap_init(): %m");
		exit(1);
	}
	/*
	 * The fileargs capability does not currently provide a way to limit
	 * ioctls.
	 */
	(void)cap_rights_init(&rights, CAP_PREAD, CAP_WRITE, CAP_IOCTL);
	capfa = fileargs_init(argc, argv, checkfor || keep ? O_RDONLY : O_RDWR,
	    0, &rights);
	if (capfa == NULL) {
		logmsg(LOG_ERR, "fileargs_init(): %m");
		exit(1);
	}
	caph_cache_catpages();
	caph_cache_tzdata();
	if (caph_enter_casper() != 0) {
		logmsg(LOG_ERR, "caph_enter_casper(): %m");
		exit(1);
	}
	capsyslog = cap_service_open(capcas, "system.syslog");
	if (capsyslog == NULL) {
		logmsg(LOG_ERR, "cap_service_open(system.syslog): %m");
		exit(1);
	}
	cap_close(capcas);
}

static void
usage(void)
{
	xo_error("%s\n%s\n%s\n",
	    "usage: savecore -c [-v] [device ...]",
	    "       savecore -C [-v] [device ...]",
	    "       savecore [-fkvz] [-m maxdumps] [directory [device ...]]");
	exit(1);
}

int
main(int argc, char **argv)
{
	cap_rights_t rights;
	const char *savedir;
	int i, ch, error, savedirfd;

	checkfor = compress = clear = force = keep = verbose = 0;
	nfound = nsaved = nerr = 0;
	savedir = ".";

	openlog("savecore", LOG_PERROR, LOG_DAEMON);
	signal(SIGINFO, infohandler);

	argc = xo_parse_args(argc, argv);
	if (argc < 0)
		exit(1);

	while ((ch = getopt(argc, argv, "Ccfkm:vz")) != -1)
		switch(ch) {
		case 'C':
			checkfor = 1;
			break;
		case 'c':
			clear = 1;
			break;
		case 'f':
			force = 1;
			break;
		case 'k':
			keep = 1;
			break;
		case 'm':
			maxdumps = atoi(optarg);
			if (maxdumps <= 0) {
				logmsg(LOG_ERR, "Invalid maxdump value");
				exit(1);
			}
			break;
		case 'v':
			verbose++;
			break;
		case 'z':
			compress = 1;
			break;
		case '?':
		default:
			usage();
		}
	if (checkfor && (clear || force || keep))
		usage();
	if (clear && (compress || keep))
		usage();
	if (maxdumps > 0 && (checkfor || clear))
		usage();
	argc -= optind;
	argv += optind;
	if (argc >= 1 && !checkfor && !clear) {
		error = chdir(argv[0]);
		if (error) {
			logmsg(LOG_ERR, "chdir(%s): %m", argv[0]);
			exit(1);
		}
		savedir = argv[0];
		argc--;
		argv++;
	}
	if (argc == 0)
		argv = enum_dumpdevs(&argc);

	savedirfd = open(savedir, O_RDONLY | O_DIRECTORY);
	if (savedirfd < 0) {
		logmsg(LOG_ERR, "open(%s): %m", savedir);
		exit(1);
	}
	(void)cap_rights_init(&rights, CAP_CREATE, CAP_FCNTL, CAP_FSTATAT,
	    CAP_FSTATFS, CAP_PREAD, CAP_SYMLINKAT, CAP_FTRUNCATE, CAP_UNLINKAT,
	    CAP_WRITE);
	if (caph_rights_limit(savedirfd, &rights) < 0) {
		logmsg(LOG_ERR, "cap_rights_limit(): %m");
		exit(1);
	}

	/* Enter capability mode. */
	init_caps(argc, argv);

	for (i = 0; i < argc; i++)
		DoFile(savedir, savedirfd, argv[i]);

	/* Emit minimal output. */
	if (nfound == 0) {
		if (checkfor) {
			if (verbose)
				printf("No dump exists\n");
			exit(1);
		}
		if (verbose)
			logmsg(LOG_WARNING, "no dumps found");
	} else if (nsaved == 0) {
		if (nerr != 0) {
			if (verbose)
				logmsg(LOG_WARNING,
				    "unsaved dumps found but not saved");
			exit(1);
		} else if (verbose)
			logmsg(LOG_WARNING, "no unsaved dumps found");
	}

	return (0);
}

static void
infohandler(int sig __unused)
{
	got_siginfo = 1;
}
