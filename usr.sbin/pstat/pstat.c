/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2002 Networks Associates Technologies, Inc.
 * All rights reserved.
 *
 * Portions of this software were developed for the FreeBSD Project by
 * ThinkSec AS and NAI Labs, the Security Research Division of Network
 * Associates, Inc.  under DARPA/SPAWAR contract N66001-01-C-8035
 * ("CBOSS"), as part of the DARPA CHATS research program.
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
"@(#) Copyright (c) 1980, 1991, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)pstat.c	8.16 (Berkeley) 5/9/95";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/stdint.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/blist.h>

#include <sys/sysctl.h>
#include <vm/vm_param.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <kvm.h>
#include <libutil.h>
#include <limits.h>
#include <nlist.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
	NL_CONSTTY,
	NL_MAXFILES,
	NL_NFILES,
	NL_TTY_LIST,
	NL_MARKER
};

static struct {
	int order;
	const char *name;
} namelist[] = {
	{ NL_CONSTTY, "_constty" },
	{ NL_MAXFILES, "_maxfiles" },
	{ NL_NFILES, "_openfiles" },
	{ NL_TTY_LIST, "_tty_list" },
	{ NL_MARKER, "" },
};
#define NNAMES	(sizeof(namelist) / sizeof(*namelist))
static struct nlist nl[NNAMES];

static int	humanflag;
static int	usenumflag;
static int	totalflag;
static int	swapflag;
static char	*nlistf;
static char	*memf;
static kvm_t	*kd;

static const char *usagestr;

static void	filemode(void);
static int	getfiles(struct xfile **, size_t *);
static void	swapmode(void);
static void	ttymode(void);
static void	ttyprt(struct xtty *);
static void	usage(void);

int
main(int argc, char *argv[])
{
	int ch, quit, ret;
	int fileflag, ttyflag;
	unsigned int i;
	char buf[_POSIX2_LINE_MAX];
	const char *opts;

	fileflag = swapflag = ttyflag = 0;

	/* We will behave like good old swapinfo if thus invoked */
	opts = strrchr(argv[0], '/');
	if (opts)
		opts++;
	else
		opts = argv[0];
	if (!strcmp(opts, "swapinfo")) {
		swapflag = 1;
		opts = "ghkmM:N:";
		usagestr = "swapinfo [-ghkm] [-M core [-N system]]";
	} else {
		opts = "TM:N:fghkmnst";
		usagestr = "pstat [-Tfghkmnst] [-M core [-N system]]";
	}

	while ((ch = getopt(argc, argv, opts)) != -1)
		switch (ch) {
		case 'f':
			fileflag = 1;
			break;
		case 'g':
			setenv("BLOCKSIZE", "1G", 1);
			break;
		case 'h':
			humanflag = 1;
			break;
		case 'k':
			setenv("BLOCKSIZE", "1K", 1);
			break;
		case 'm':
			setenv("BLOCKSIZE", "1M", 1);
			break;
		case 'M':
			memf = optarg;
			break;
		case 'N':
			nlistf = optarg;
			break;
		case 'n':
			usenumflag = 1;
			break;
		case 's':
			++swapflag;
			break;
		case 'T':
			totalflag = 1;
			break;
		case 't':
			ttyflag = 1;
			break;
		default:
			usage();
		}

	/*
	 * Initialize symbol names list.
	 */
	for (i = 0; i < NNAMES; i++)
		nl[namelist[i].order].n_name = strdup(namelist[i].name);

	if (memf != NULL) {
		kd = kvm_openfiles(nlistf, memf, NULL, O_RDONLY, buf);
		if (kd == NULL)
			errx(1, "kvm_openfiles: %s", buf);
		if ((ret = kvm_nlist(kd, nl)) != 0) {
			if (ret == -1)
				errx(1, "kvm_nlist: %s", kvm_geterr(kd));
			quit = 0;
			for (i = 0; nl[i].n_name[0] != '\0'; ++i)
				if (nl[i].n_value == 0) {
					quit = 1;
					warnx("undefined symbol: %s",
					    nl[i].n_name);
				}
			if (quit)
				exit(1);
		}
	}
	if (!(fileflag | ttyflag | swapflag | totalflag))
		usage();
	if (fileflag || totalflag)
		filemode();
	if (ttyflag)
		ttymode();
	if (swapflag || totalflag)
		swapmode();
	exit (0);
}

static void
usage(void)
{
	fprintf(stderr, "usage: %s\n", usagestr);
	exit (1);
}

static const char fhdr32[] =
  "   LOC   TYPE   FLG  CNT MSG   DATA        OFFSET\n";
/* c0000000 ------ RWAI 123 123 c0000000 1000000000000000 */

static const char fhdr64[] =
  "       LOC       TYPE   FLG  CNT MSG       DATA            OFFSET\n";
/* c000000000000000 ------ RWAI 123 123 c000000000000000 1000000000000000 */

static const char hdr[] =
"      LINE   INQ  CAN  LIN  LOW  OUTQ  USE  LOW   COL  SESS  PGID STATE\n";

static void
ttymode_kvm(void)
{
	TAILQ_HEAD(, tty) tl;
	struct tty *tp, tty;
	struct xtty xt;

	(void)printf("%s", hdr);
	bzero(&xt, sizeof xt);
	xt.xt_size = sizeof xt;
	if (kvm_read(kd, nl[NL_TTY_LIST].n_value, &tl, sizeof tl) != sizeof tl)
		errx(1, "kvm_read(): %s", kvm_geterr(kd));
	tp = TAILQ_FIRST(&tl);
	while (tp != NULL) {
		if (kvm_read(kd, (u_long)tp, &tty, sizeof tty) != sizeof tty)
			errx(1, "kvm_read(): %s", kvm_geterr(kd));
		xt.xt_insize = tty.t_inq.ti_nblocks * TTYINQ_DATASIZE;
		xt.xt_incc = tty.t_inq.ti_linestart - tty.t_inq.ti_begin;
		xt.xt_inlc = tty.t_inq.ti_end - tty.t_inq.ti_linestart;
		xt.xt_inlow = tty.t_inlow;
		xt.xt_outsize = tty.t_outq.to_nblocks * TTYOUTQ_DATASIZE;
		xt.xt_outcc = tty.t_outq.to_end - tty.t_outq.to_begin;
		xt.xt_outlow = tty.t_outlow;
		xt.xt_column = tty.t_column;
		/* xt.xt_pgid = ... */
		/* xt.xt_sid = ... */
		xt.xt_flags = tty.t_flags;
		xt.xt_dev = (uint32_t)NODEV;
		ttyprt(&xt);
		tp = TAILQ_NEXT(&tty, t_list);
	}
}

static void
ttymode_sysctl(void)
{
	struct xtty *xttys;
	size_t len;
	unsigned int i, n;

	(void)printf("%s", hdr);
	if ((xttys = malloc(len = sizeof(*xttys))) == NULL)
		err(1, "malloc()");
	while (sysctlbyname("kern.ttys", xttys, &len, 0, 0) == -1) {
		if (errno != ENOMEM)
			err(1, "sysctlbyname()");
		len *= 2;
		if ((xttys = realloc(xttys, len)) == NULL)
			err(1, "realloc()");
	}
	n = len / sizeof(*xttys);
	for (i = 0; i < n; i++)
		ttyprt(&xttys[i]);
}

static void
ttymode(void)
{

	if (kd != NULL)
		ttymode_kvm();
	else
		ttymode_sysctl();
}

static struct {
	int flag;
	char val;
} ttystates[] = {
#if 0
	{ TF_NOPREFIX,		'N' },
#endif
	{ TF_INITLOCK,		'I' },
	{ TF_CALLOUT,		'C' },

	/* Keep these together -> 'Oi' and 'Oo'. */
	{ TF_OPENED,		'O' },
	{ TF_OPENED_IN,		'i' },
	{ TF_OPENED_OUT,	'o' },
	{ TF_OPENED_CONS,	'c' },

	{ TF_GONE,		'G' },
	{ TF_OPENCLOSE,		'B' },
	{ TF_ASYNC,		'Y' },
	{ TF_LITERAL,		'L' },

	/* Keep these together -> 'Hi' and 'Ho'. */
	{ TF_HIWAT,		'H' },
	{ TF_HIWAT_IN,		'i' },
	{ TF_HIWAT_OUT,		'o' },

	{ TF_STOPPED,		'S' },
	{ TF_EXCLUDE,		'X' },
	{ TF_BYPASS,		'l' },
	{ TF_ZOMBIE,		'Z' },
	{ TF_HOOK,		's' },

	/* Keep these together -> 'bi' and 'bo'. */
	{ TF_BUSY,		'b' },
	{ TF_BUSY_IN,		'i' },
	{ TF_BUSY_OUT,		'o' },

	{ 0,			'\0'},
};

static void
ttyprt(struct xtty *xt)
{
	int i, j;
	const char *name;

	if (xt->xt_size != sizeof *xt)
		errx(1, "struct xtty size mismatch");
	if (usenumflag || xt->xt_dev == 0 ||
	   (name = devname(xt->xt_dev, S_IFCHR)) == NULL)
		printf("%#10jx ", (uintmax_t)xt->xt_dev);
	else
		printf("%10s ", name);
	printf("%5zu %4zu %4zu %4zu %5zu %4zu %4zu %5u %5d %5d ",
	    xt->xt_insize, xt->xt_incc, xt->xt_inlc,
	    (xt->xt_insize - xt->xt_inlow), xt->xt_outsize,
	    xt->xt_outcc, (xt->xt_outsize - xt->xt_outlow),
	    MIN(xt->xt_column, 99999), xt->xt_sid, xt->xt_pgid);
	for (i = j = 0; ttystates[i].flag; i++)
		if (xt->xt_flags & ttystates[i].flag) {
			putchar(ttystates[i].val);
			j++;
		}
	if (j == 0)
		putchar('-');
	putchar('\n');
}

static void
filemode(void)
{
	struct xfile *fp, *buf;
	char flagbuf[16], *fbp;
	int maxf, openf;
	size_t len;
	static char const * const dtypes[] = { "???", "inode", "socket",
	    "pipe", "fifo", "kqueue", "crypto" };
	int i;
	int wid;

	if (kd != NULL) {
		if (kvm_read(kd, nl[NL_MAXFILES].n_value,
			&maxf, sizeof maxf) != sizeof maxf ||
		    kvm_read(kd, nl[NL_NFILES].n_value,
			&openf, sizeof openf) != sizeof openf)
			errx(1, "kvm_read(): %s", kvm_geterr(kd));
	} else {
		len = sizeof(int);
		if (sysctlbyname("kern.maxfiles", &maxf, &len, 0, 0) == -1 ||
		    sysctlbyname("kern.openfiles", &openf, &len, 0, 0) == -1)
			err(1, "sysctlbyname()");
	}

	if (totalflag) {
		(void)printf("%3d/%3d files\n", openf, maxf);
		return;
	}
	if (getfiles(&buf, &len) == -1)
		return;
	openf = len / sizeof *fp;

	(void)printf("%d/%d open files\n", openf, maxf);
	printf(sizeof(uintptr_t) == 4 ? fhdr32 : fhdr64);
	wid = (int)sizeof(uintptr_t) * 2;
	for (fp = (struct xfile *)buf, i = 0; i < openf; ++fp, ++i) {
		if ((size_t)fp->xf_type >= sizeof(dtypes) / sizeof(dtypes[0]))
			continue;
		(void)printf("%*jx", wid, (uintmax_t)(uintptr_t)fp->xf_file);
		(void)printf(" %-6.6s", dtypes[fp->xf_type]);
		fbp = flagbuf;
		if (fp->xf_flag & FREAD)
			*fbp++ = 'R';
		if (fp->xf_flag & FWRITE)
			*fbp++ = 'W';
		if (fp->xf_flag & FAPPEND)
			*fbp++ = 'A';
		if (fp->xf_flag & FASYNC)
			*fbp++ = 'I';
		*fbp = '\0';
		(void)printf(" %4s %3d", flagbuf, fp->xf_count);
		(void)printf(" %3d", fp->xf_msgcount);
		(void)printf(" %*jx", wid, (uintmax_t)(uintptr_t)fp->xf_data);
		(void)printf(" %*jx\n", (int)sizeof(fp->xf_offset) * 2,
		    (uintmax_t)fp->xf_offset);
	}
	free(buf);
}

static int
getfiles(struct xfile **abuf, size_t *alen)
{
	struct xfile *buf;
	size_t len;
	int mib[2];

	/*
	 * XXX
	 * Add emulation of KINFO_FILE here.
	 */
	if (kd != NULL)
		errx(1, "files on dead kernel, not implemented");

	mib[0] = CTL_KERN;
	mib[1] = KERN_FILE;
	if (sysctl(mib, 2, NULL, &len, NULL, 0) == -1) {
		warn("sysctl: KERN_FILE");
		return (-1);
	}
	if ((buf = malloc(len)) == NULL)
		errx(1, "malloc");
	if (sysctl(mib, 2, buf, &len, NULL, 0) == -1) {
		warn("sysctl: KERN_FILE");
		return (-1);
	}
	*abuf = buf;
	*alen = len;
	return (0);
}

/*
 * swapmode is based on a program called swapinfo written
 * by Kevin Lahey <kml@rokkaku.atl.ga.us>.
 */

#define CONVERT(v)	((int64_t)(v) * pagesize / blocksize)
#define CONVERT_BLOCKS(v)	((int64_t)(v) * pagesize)
static struct kvm_swap swtot;
static int nswdev;

static void
print_swap_header(void)
{
	int hlen;
	long blocksize;
	const char *header;

	header = getbsize(&hlen, &blocksize);
	if (totalflag == 0)
		(void)printf("%-15s %*s %8s %8s %8s\n",
		    "Device", hlen, header,
		    "Used", "Avail", "Capacity");
}

static void
print_swap_line(const char *swdevname, intmax_t nblks, intmax_t bused,
    intmax_t bavail, float bpercent)
{
	char usedbuf[5];
	char availbuf[5];
	int hlen, pagesize;
	long blocksize;

	pagesize = getpagesize();
	getbsize(&hlen, &blocksize);

	printf("%-15s %*jd ", swdevname, hlen, CONVERT(nblks));
	if (humanflag) {
		humanize_number(usedbuf, sizeof(usedbuf),
		    CONVERT_BLOCKS(bused), "",
		    HN_AUTOSCALE, HN_B | HN_NOSPACE | HN_DECIMAL);
		humanize_number(availbuf, sizeof(availbuf),
		    CONVERT_BLOCKS(bavail), "",
		    HN_AUTOSCALE, HN_B | HN_NOSPACE | HN_DECIMAL);
		printf("%8s %8s %5.0f%%\n", usedbuf, availbuf, bpercent);
	} else {
		printf("%8jd %8jd %5.0f%%\n", (intmax_t)CONVERT(bused),
		    (intmax_t)CONVERT(bavail), bpercent);
	}
}

static void
print_swap(struct kvm_swap *ksw)
{

	swtot.ksw_total += ksw->ksw_total;
	swtot.ksw_used += ksw->ksw_used;
	++nswdev;
	if (totalflag == 0)
		print_swap_line(ksw->ksw_devname, ksw->ksw_total,
		    ksw->ksw_used, ksw->ksw_total - ksw->ksw_used,
		    (ksw->ksw_used * 100.0) / ksw->ksw_total);
}

static void
print_swap_total(void)
{
	int hlen, pagesize;
	long blocksize;

	pagesize = getpagesize();
	getbsize(&hlen, &blocksize);
	if (totalflag) {
		blocksize = 1024 * 1024;
		(void)printf("%jdM/%jdM swap space\n",
		    CONVERT(swtot.ksw_used), CONVERT(swtot.ksw_total));
	} else if (nswdev > 1) {
		print_swap_line("Total", swtot.ksw_total, swtot.ksw_used,
		    swtot.ksw_total - swtot.ksw_used,
		    (swtot.ksw_used * 100.0) / swtot.ksw_total);
	}
}

static void
swapmode_kvm(void)
{
	struct kvm_swap kswap[16];
	int i, n;

	n = kvm_getswapinfo(kd, kswap, sizeof kswap / sizeof kswap[0],
	    SWIF_DEV_PREFIX);

	print_swap_header();
	for (i = 0; i < n; ++i)
		print_swap(&kswap[i]);
	print_swap_total();
}

static void
swapmode_sysctl(void)
{
	struct kvm_swap ksw;
	struct xswdev xsw;
	size_t mibsize, size;
	int mib[16], n;

	print_swap_header();
	mibsize = sizeof mib / sizeof mib[0];
	if (sysctlnametomib("vm.swap_info", mib, &mibsize) == -1)
		err(1, "sysctlnametomib()");
	for (n = 0; ; ++n) {
		mib[mibsize] = n;
		size = sizeof xsw;
		if (sysctl(mib, mibsize + 1, &xsw, &size, NULL, 0) == -1)
			break;
		if (xsw.xsw_version != XSWDEV_VERSION)
			errx(1, "xswdev version mismatch");
		if (xsw.xsw_dev == NODEV)
			snprintf(ksw.ksw_devname, sizeof ksw.ksw_devname,
			    "<NFSfile>");
		else
			snprintf(ksw.ksw_devname, sizeof ksw.ksw_devname,
			    "/dev/%s", devname(xsw.xsw_dev, S_IFCHR));
		ksw.ksw_used = xsw.xsw_used;
		ksw.ksw_total = xsw.xsw_nblks;
		ksw.ksw_flags = xsw.xsw_flags;
		print_swap(&ksw);
	}
	if (errno != ENOENT)
		err(1, "sysctl()");
	print_swap_total();
}

static void
swapmode(void)
{
	if (kd != NULL)
		swapmode_kvm();
	else
		swapmode_sysctl();
}
