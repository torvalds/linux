/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1992, 1993
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1983, 1992, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)kgmon.c	8.1 (Berkeley) 6/6/93";
#endif
#endif /* not lint */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/gmon.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <kvm.h>
#include <limits.h>
#include <nlist.h>
#include <paths.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct nlist nl[] = {
#define	N_GMONPARAM	0
	{ "__gmonparam" },
#define	N_PROFHZ	1
	{ "_profhz" },
	{ NULL },
};

struct kvmvars {
	kvm_t	*kd;
	struct gmonparam gpm;
};

int	Bflag, bflag, hflag, kflag, rflag, pflag;
int	debug = 0;
int	getprof(struct kvmvars *);
int	getprofhz(struct kvmvars *);
void	kern_readonly(int);
int	openfiles(const char *, char *, struct kvmvars *);
void	setprof(struct kvmvars *kvp, int state);
void	dumpstate(struct kvmvars *kvp);
void	reset(struct kvmvars *kvp);
static void usage(void);

int
main(int argc, char **argv)
{
	int ch, mode, disp, accessmode;
	struct kvmvars kvmvars;
	const char *systemname;
	char *kmemf;

	if (seteuid(getuid()) != 0) {
		err(1, "seteuid failed\n");
	}
	kmemf = NULL;
	systemname = NULL;
	while ((ch = getopt(argc, argv, "M:N:Bbhpr")) != -1) {
		switch((char)ch) {

		case 'M':
			kmemf = optarg;
			kflag = 1;
			break;

		case 'N':
			systemname = optarg;
			break;

		case 'B':
			Bflag = 1;
			break;

		case 'b':
			bflag = 1;
			break;

		case 'h':
			hflag = 1;
			break;

		case 'p':
			pflag = 1;
			break;

		case 'r':
			rflag = 1;
			break;

		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

#define BACKWARD_COMPATIBILITY
#ifdef	BACKWARD_COMPATIBILITY
	if (*argv) {
		systemname = *argv;
		if (*++argv) {
			kmemf = *argv;
			++kflag;
		}
	}
#endif
	if (systemname == NULL)
		systemname = getbootfile();
	accessmode = openfiles(systemname, kmemf, &kvmvars);
	mode = getprof(&kvmvars);
	if (hflag)
		disp = GMON_PROF_OFF;
	else if (Bflag)
		disp = GMON_PROF_HIRES;
	else if (bflag)
		disp = GMON_PROF_ON;
	else
		disp = mode;
	if (pflag)
		dumpstate(&kvmvars);
	if (rflag)
		reset(&kvmvars);
	if (accessmode == O_RDWR)
		setprof(&kvmvars, disp);
	(void)fprintf(stdout, "kgmon: kernel profiling is %s.\n",
		      disp == GMON_PROF_OFF ? "off" :
		      disp == GMON_PROF_HIRES ? "running (high resolution)" :
		      disp == GMON_PROF_ON ? "running" :
		      disp == GMON_PROF_BUSY ? "busy" :
		      disp == GMON_PROF_ERROR ? "off (error)" :
		      "in an unknown state");
	return (0);
}

static void
usage(void)
{
	fprintf(stderr, "usage: kgmon [-Bbhrp] [-M core] [-N system]\n");
	exit(1);
}

/*
 * Check that profiling is enabled and open any necessary files.
 */
int
openfiles(const char *systemname, char *kmemf, struct kvmvars *kvp)
{
	size_t size;
	int mib[3], state, openmode;
	char errbuf[_POSIX2_LINE_MAX];

	if (!kflag) {
		mib[0] = CTL_KERN;
		mib[1] = KERN_PROF;
		mib[2] = GPROF_STATE;
		size = sizeof state;
		if (sysctl(mib, 3, &state, &size, NULL, 0) < 0)
			errx(20, "profiling not defined in kernel");
		if (!(Bflag || bflag || hflag || rflag ||
		    (pflag &&
		     (state == GMON_PROF_HIRES || state == GMON_PROF_ON))))
			return (O_RDONLY);
		(void)seteuid(0);
		if (sysctl(mib, 3, NULL, NULL, &state, size) >= 0)
			return (O_RDWR);
		(void)seteuid(getuid());
		kern_readonly(state);
		return (O_RDONLY);
	}
	openmode = (Bflag || bflag || hflag || pflag || rflag)
		   ? O_RDWR : O_RDONLY;
	kvp->kd = kvm_openfiles(systemname, kmemf, NULL, openmode, errbuf);
	if (kvp->kd == NULL) {
		if (openmode == O_RDWR) {
			openmode = O_RDONLY;
			kvp->kd = kvm_openfiles(systemname, kmemf, NULL, O_RDONLY,
			    errbuf);
		}
		if (kvp->kd == NULL)
			errx(2, "kvm_openfiles: %s", errbuf);
		kern_readonly(GMON_PROF_ON);
	}
	if (kvm_nlist(kvp->kd, nl) < 0)
		errx(3, "%s: no namelist", systemname);
	if (!nl[N_GMONPARAM].n_value)
		errx(20, "profiling not defined in kernel");
	return (openmode);
}

/*
 * Suppress options that require a writable kernel.
 */
void
kern_readonly(int mode)
{

	(void)fprintf(stderr, "kgmon: kernel read-only: ");
	if (pflag && (mode == GMON_PROF_HIRES || mode == GMON_PROF_ON))
		(void)fprintf(stderr, "data may be inconsistent\n");
	if (rflag)
		(void)fprintf(stderr, "-r suppressed\n");
	if (Bflag)
		(void)fprintf(stderr, "-B suppressed\n");
	if (bflag)
		(void)fprintf(stderr, "-b suppressed\n");
	if (hflag)
		(void)fprintf(stderr, "-h suppressed\n");
	rflag = Bflag = bflag = hflag = 0;
}

/*
 * Get the state of kernel profiling.
 */
int
getprof(struct kvmvars *kvp)
{
	size_t size;
	int mib[3];

	if (kflag) {
		size = kvm_read(kvp->kd, nl[N_GMONPARAM].n_value, &kvp->gpm,
		    sizeof kvp->gpm);
	} else {
		mib[0] = CTL_KERN;
		mib[1] = KERN_PROF;
		mib[2] = GPROF_GMONPARAM;
		size = sizeof kvp->gpm;
		if (sysctl(mib, 3, &kvp->gpm, &size, NULL, 0) < 0)
			size = 0;
	}

	/*
	 * Accept certain undersized "structs" from old kernels.  We need
	 * everything up to hashfraction, and want profrate and
	 * histcounter_type.  Assume that the kernel doesn't put garbage
	 * in any padding that is returned instead of profrate and
	 * histcounter_type.  This is a bad assumption for dead kernels,
	 * since kvm_read() will normally return garbage for bytes beyond
	 * the end of the actual kernel struct, if any.
	 */
	if (size < offsetof(struct gmonparam, hashfraction) +
	    sizeof(kvp->gpm.hashfraction) || size > sizeof(kvp->gpm))
		errx(4, "cannot get gmonparam: %s",
		    kflag ? kvm_geterr(kvp->kd) : strerror(errno));
	bzero((char *)&kvp->gpm + size, sizeof(kvp->gpm) - size);
	if (kvp->gpm.profrate == 0)
		kvp->gpm.profrate = getprofhz(kvp);
#ifdef __i386__
	if (kvp->gpm.histcounter_type == 0) {
		/*
		 * This fixup only works for not-so-old i386 kernels.  The
		 * magic 16 is the kernel FUNCTION_ALIGNMENT.  64-bit
		 * counters are signed; smaller counters are unsigned.
		 */
		kvp->gpm.histcounter_type = 16 /
		    (kvp->gpm.textsize / kvp->gpm.kcountsize) * CHAR_BIT;
		if (kvp->gpm.histcounter_type == 64)
			kvp->gpm.histcounter_type = -64;
	}
#endif

	return (kvp->gpm.state);
}

/*
 * Enable or disable kernel profiling according to the state variable.
 */
void
setprof(struct kvmvars *kvp, int state)
{
	struct gmonparam *p = (struct gmonparam *)nl[N_GMONPARAM].n_value;
	size_t sz;
	int mib[3], oldstate;

	sz = sizeof(state);
	if (!kflag) {
		mib[0] = CTL_KERN;
		mib[1] = KERN_PROF;
		mib[2] = GPROF_STATE;
		if (sysctl(mib, 3, &oldstate, &sz, NULL, 0) < 0)
			goto bad;
		if (oldstate == state)
			return;
		(void)seteuid(0);
		if (sysctl(mib, 3, NULL, NULL, &state, sz) >= 0) {
			(void)seteuid(getuid());
			return;
		}
		(void)seteuid(getuid());
	} else if (kvm_write(kvp->kd, (u_long)&p->state, (void *)&state, sz)
	    == (ssize_t)sz)
		return;
bad:
	warnx("warning: cannot turn profiling %s",
	    state == GMON_PROF_OFF ? "off" : "on");
}

/*
 * Build the gmon.out file.
 */
void
dumpstate(struct kvmvars *kvp)
{
	register FILE *fp;
	struct rawarc rawarc;
	struct tostruct *tos;
	u_long frompc;
	u_short *froms, *tickbuf;
	size_t i;
	int mib[3];
	struct gmonhdr h;
	int fromindex, endfrom, toindex;

	setprof(kvp, GMON_PROF_OFF);
	fp = fopen("gmon.out", "w");
	if (fp == NULL) {
		warn("gmon.out");
		return;
	}

	/*
	 * Build the gmon header and write it to a file.
	 */
	bzero(&h, sizeof(h));
	h.lpc = kvp->gpm.lowpc;
	h.hpc = kvp->gpm.highpc;
	h.ncnt = kvp->gpm.kcountsize + sizeof(h);
	h.version = GMONVERSION;
	h.profrate = kvp->gpm.profrate;
	h.histcounter_type = kvp->gpm.histcounter_type;
	fwrite((char *)&h, sizeof(h), 1, fp);

	/*
	 * Write out the tick buffer.
	 */
	mib[0] = CTL_KERN;
	mib[1] = KERN_PROF;
	if ((tickbuf = (u_short *)malloc(kvp->gpm.kcountsize)) == NULL)
		errx(5, "cannot allocate kcount space");
	if (kflag) {
		i = kvm_read(kvp->kd, (u_long)kvp->gpm.kcount, (void *)tickbuf,
		    kvp->gpm.kcountsize);
	} else {
		mib[2] = GPROF_COUNT;
		i = kvp->gpm.kcountsize;
		if (sysctl(mib, 3, tickbuf, &i, NULL, 0) < 0)
			i = 0;
	}
	if (i != kvp->gpm.kcountsize)
		errx(6, "read ticks: read %lu, got %ld: %s",
		    kvp->gpm.kcountsize, (long)i,
		    kflag ? kvm_geterr(kvp->kd) : strerror(errno));
	if ((fwrite(tickbuf, kvp->gpm.kcountsize, 1, fp)) != 1)
		err(7, "writing tocks to gmon.out");
	free(tickbuf);

	/*
	 * Write out the arc info.
	 */
	if ((froms = (u_short *)malloc(kvp->gpm.fromssize)) == NULL)
		errx(8, "cannot allocate froms space");
	if (kflag) {
		i = kvm_read(kvp->kd, (u_long)kvp->gpm.froms, (void *)froms,
		    kvp->gpm.fromssize);
	} else {
		mib[2] = GPROF_FROMS;
		i = kvp->gpm.fromssize;
		if (sysctl(mib, 3, froms, &i, NULL, 0) < 0)
			i = 0;
	}
	if (i != kvp->gpm.fromssize)
		errx(9, "read froms: read %lu, got %ld: %s",
		    kvp->gpm.fromssize, (long)i,
		    kflag ? kvm_geterr(kvp->kd) : strerror(errno));
	if ((tos = (struct tostruct *)malloc(kvp->gpm.tossize)) == NULL)
		errx(10, "cannot allocate tos space");
	if (kflag) {
		i = kvm_read(kvp->kd, (u_long)kvp->gpm.tos, (void *)tos,
		    kvp->gpm.tossize);
	} else {
		mib[2] = GPROF_TOS;
		i = kvp->gpm.tossize;
		if (sysctl(mib, 3, tos, &i, NULL, 0) < 0)
			i = 0;
	}
	if (i != kvp->gpm.tossize)
		errx(11, "read tos: read %lu, got %ld: %s",
		    kvp->gpm.tossize, (long)i,
		    kflag ? kvm_geterr(kvp->kd) : strerror(errno));
	if (debug)
		warnx("lowpc 0x%lx, textsize 0x%lx",
		    (unsigned long)kvp->gpm.lowpc, kvp->gpm.textsize);
	endfrom = kvp->gpm.fromssize / sizeof(*froms);
	for (fromindex = 0; fromindex < endfrom; ++fromindex) {
		if (froms[fromindex] == 0)
			continue;
		frompc = (u_long)kvp->gpm.lowpc +
		    (fromindex * kvp->gpm.hashfraction * sizeof(*froms));
		for (toindex = froms[fromindex]; toindex != 0;
		     toindex = tos[toindex].link) {
			if (debug)
				warnx("[mcleanup] frompc 0x%lx selfpc 0x%lx "
				    "count %ld", frompc, tos[toindex].selfpc,
				    tos[toindex].count);
			rawarc.raw_frompc = frompc;
			rawarc.raw_selfpc = (u_long)tos[toindex].selfpc;
			rawarc.raw_count = tos[toindex].count;
			fwrite((char *)&rawarc, sizeof(rawarc), 1, fp);
		}
	}
	fclose(fp);
}

/*
 * Get the profiling rate.
 */
int
getprofhz(struct kvmvars *kvp)
{
	size_t size;
	int mib[2], profrate;
	struct clockinfo clockrate;

	if (kflag) {
		profrate = 1;
		if (kvm_read(kvp->kd, nl[N_PROFHZ].n_value, &profrate,
		    sizeof profrate) != sizeof profrate)
			warnx("get clockrate: %s", kvm_geterr(kvp->kd));
		return (profrate);
	}
	mib[0] = CTL_KERN;
	mib[1] = KERN_CLOCKRATE;
	clockrate.profhz = 1;
	size = sizeof clockrate;
	if (sysctl(mib, 2, &clockrate, &size, NULL, 0) < 0)
		warn("get clockrate");
	return (clockrate.profhz);
}

/*
 * Reset the kernel profiling date structures.
 */
void
reset(struct kvmvars *kvp)
{
	char *zbuf;
	u_long biggest;
	int mib[3];

	setprof(kvp, GMON_PROF_OFF);

	biggest = kvp->gpm.kcountsize;
	if (kvp->gpm.fromssize > biggest)
		biggest = kvp->gpm.fromssize;
	if (kvp->gpm.tossize > biggest)
		biggest = kvp->gpm.tossize;
	if ((zbuf = (char *)malloc(biggest)) == NULL)
		errx(12, "cannot allocate zbuf space");
	bzero(zbuf, biggest);
	if (kflag) {
		if (kvm_write(kvp->kd, (u_long)kvp->gpm.kcount, zbuf,
		    kvp->gpm.kcountsize) != (ssize_t)kvp->gpm.kcountsize)
			errx(13, "tickbuf zero: %s", kvm_geterr(kvp->kd));
		if (kvm_write(kvp->kd, (u_long)kvp->gpm.froms, zbuf,
		    kvp->gpm.fromssize) != (ssize_t)kvp->gpm.fromssize)
			errx(14, "froms zero: %s", kvm_geterr(kvp->kd));
		if (kvm_write(kvp->kd, (u_long)kvp->gpm.tos, zbuf,
		    kvp->gpm.tossize) != (ssize_t)kvp->gpm.tossize)
			errx(15, "tos zero: %s", kvm_geterr(kvp->kd));
		free(zbuf);
		return;
	}
	(void)seteuid(0);
	mib[0] = CTL_KERN;
	mib[1] = KERN_PROF;
	mib[2] = GPROF_COUNT;
	if (sysctl(mib, 3, NULL, NULL, zbuf, kvp->gpm.kcountsize) < 0)
		err(13, "tickbuf zero");
	mib[2] = GPROF_FROMS;
	if (sysctl(mib, 3, NULL, NULL, zbuf, kvp->gpm.fromssize) < 0)
		err(14, "froms zero");
	mib[2] = GPROF_TOS;
	if (sysctl(mib, 3, NULL, NULL, zbuf, kvp->gpm.tossize) < 0)
		err(15, "tos zero");
	(void)seteuid(getuid());
	free(zbuf);
}
