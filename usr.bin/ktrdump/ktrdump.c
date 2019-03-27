/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Jake Burkholder
 * Copyright (c) 2004 Robert Watson
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/capsicum.h>
#include <sys/ktr.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <capsicum_helpers.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <nlist.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	SBUFLEN	128
#define	USAGE \
	"usage: ktrdump [-cflqrtH] [-i ktrfile] [-M core] [-N system] [-o outfile]\n"

static void usage(void);

static struct nlist nl[] = {
	{ "_ktr_version" },
	{ "_ktr_entries" },
	{ "_ktr_idx" },
	{ "_ktr_buf" },
	{ NULL }
};

static int cflag;
static int fflag;
static int lflag;
static int Mflag;
static int Nflag;
static int qflag;
static int rflag;
static int tflag;
static int iflag;
static int hflag;

static char corefile[PATH_MAX];
static char execfile[PATH_MAX];
static char outfile[PATH_MAX] = "stdout";

static char desc[SBUFLEN];
static char errbuf[_POSIX2_LINE_MAX];
static char fbuf[PATH_MAX];
static char obuf[PATH_MAX];
static char sbuf[KTR_PARMS][SBUFLEN];

/*
 * Reads the ktr trace buffer from kernel memory and prints the trace entries.
 */
int
main(int ac, char **av)
{
	u_long parms[KTR_PARMS];
	struct ktr_entry *buf;
	uintmax_t tlast, tnow;
	unsigned long bufptr;
	cap_rights_t rights;
	struct stat sb;
	kvm_t *kd;
	FILE *out;
	char *p;
	int version;
	int entries;
	int count;
	int index, index2;
	int parm;
	int in;
	int c;
	int i = 0;

	/*
	 * Parse commandline arguments.
	 */
	out = stdout;
	while ((c = getopt(ac, av, "cflqrtHe:i:m:M:N:o:")) != -1)
		switch (c) {
		case 'c':
			cflag = 1;
			break;
		case 'N':
		case 'e':
			if (strlcpy(execfile, optarg, sizeof(execfile))
			    >= sizeof(execfile))
				errx(1, "%s: File name too long", optarg);
			Nflag = 1;
			break;
		case 'f':
			fflag = 1;
			break;
		case 'i':
			iflag = 1;
			if ((in = open(optarg, O_RDONLY)) == -1)
				err(1, "%s", optarg);
			cap_rights_init(&rights, CAP_FSTAT, CAP_MMAP_R);
			if (caph_rights_limit(in, &rights) < 0)
				err(1, "unable to limit rights for %s",
				    optarg);
			break;
		case 'l':
			lflag = 1;
			break;
		case 'M':
		case 'm':
			if (strlcpy(corefile, optarg, sizeof(corefile))
			    >= sizeof(corefile))
				errx(1, "%s: File name too long", optarg);
			Mflag = 1;
			break;
		case 'o':
			if ((out = fopen(optarg, "w")) == NULL)
				err(1, "%s", optarg);
			strlcpy(outfile, optarg, sizeof(outfile));
			break;
		case 'q':
			qflag++;
			break;
		case 'r':
			rflag = 1;
			break;
		case 't':
			tflag = 1;
			break;
		case 'H':
			hflag = 1;
			break;
		case '?':
		default:
			usage();
		}
	ac -= optind;
	av += optind;
	if (ac != 0)
		usage();

	if (caph_limit_stream(fileno(out), CAPH_WRITE) < 0)
		err(1, "unable to limit rights for %s", outfile);
	if (caph_limit_stderr() < 0)
		err(1, "unable to limit rights for stderr");

	/*
	 * Open our execfile and corefile, resolve needed symbols and read in
	 * the trace buffer.
	 */
	if ((kd = kvm_openfiles(Nflag ? execfile : NULL,
	    Mflag ? corefile : NULL, NULL, O_RDONLY, errbuf)) == NULL)
		errx(1, "%s", errbuf);

	/*
	 * Cache NLS data, for strerror, for err(3), before entering capability
	 * mode.
	 */
	caph_cache_catpages();

	count = kvm_nlist(kd, nl);
	if (count == -1)
		errx(1, "%s", kvm_geterr(kd));
	if (count > 0)
		errx(1, "failed to resolve ktr symbols");
	if (kvm_read(kd, nl[0].n_value, &version, sizeof(version)) == -1)
		errx(1, "%s", kvm_geterr(kd));
	if (version != KTR_VERSION)
		errx(1, "ktr version mismatch");

	/*
	 * Enter Capsicum sandbox.
	 *
	 * kvm_nlist() above uses kldsym(2) for native kernels, and that isn't
	 * allowed in the sandbox.
	 */
	if (caph_enter() < 0)
		err(1, "unable to enter capability mode");

	if (iflag) {
		if (fstat(in, &sb) == -1)
			errx(1, "stat");
		entries = sb.st_size / sizeof(*buf);
		index = 0;
		buf = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, in, 0);
		if (buf == MAP_FAILED)
			errx(1, "mmap");
	} else {
		if (kvm_read(kd, nl[1].n_value, &entries, sizeof(entries))
		    == -1)
			errx(1, "%s", kvm_geterr(kd));
		if ((buf = malloc(sizeof(*buf) * entries)) == NULL)
			err(1, NULL);
		if (kvm_read(kd, nl[2].n_value, &index, sizeof(index)) == -1 ||
		    kvm_read(kd, nl[3].n_value, &bufptr,
		    sizeof(bufptr)) == -1 ||
		    kvm_read(kd, bufptr, buf, sizeof(*buf) * entries) == -1 ||
		    kvm_read(kd, nl[2].n_value, &index2, sizeof(index2)) == -1)
			errx(1, "%s", kvm_geterr(kd));
	}

	/*
	 * Print a nice header.
	 */
	if (!qflag) {
		fprintf(out, "%-6s ", "index");
		if (cflag)
			fprintf(out, "%-3s ", "cpu");
		if (tflag)
			fprintf(out, "%-16s ", "timestamp");
		if (fflag)
			fprintf(out, "%-40s ", "file and line");
		if (hflag)
			fprintf(out, "%-18s ", "tid");
		fprintf(out, "%s", "trace");
		fprintf(out, "\n");

		fprintf(out, "------ ");
		if (cflag)
			fprintf(out, "--- ");
		if (tflag)
			fprintf(out, "---------------- ");
		if (fflag)
			fprintf(out,
			    "---------------------------------------- ");
		if (hflag)
			fprintf(out, "------------------ ");
		fprintf(out, "----- ");
		fprintf(out, "\n");
	}

	tlast = -1;
	/*
	 * Now tear through the trace buffer.
	 *
	 * In "live" mode, find the oldest entry (first non-NULL entry
	 * after index2) and walk forward.  Otherwise, start with the
	 * most recent entry and walk backwards.
	 */
	if (!iflag) {
		if (lflag) {
			i = index2 + 1 % entries;
			while (buf[i].ktr_desc == NULL && i != index) {
				i++;
				if (i == entries)
					i = 0;
			}
		} else {
			i = index - 1;
			if (i < 0)
				i = entries - 1;
		}
	}
dump_entries:
	for (;;) {
		if (buf[i].ktr_desc == NULL)
			break;
		if (kvm_read(kd, (u_long)buf[i].ktr_desc, desc,
		    sizeof(desc)) == -1)
			errx(1, "%s", kvm_geterr(kd));
		desc[sizeof(desc) - 1] = '\0';
		parm = 0;
		for (p = desc; (c = *p++) != '\0';) {
			if (c != '%')
				continue;
next:			if ((c = *p++) == '\0')
				break;
			if (parm == KTR_PARMS)
				errx(1, "too many parameters in \"%s\"", desc);
			switch (c) {
			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
			case '#': case '-': case ' ': case '+': case '\'':
			case 'h': case 'l': case 'j': case 't': case 'z':
			case 'q': case 'L': case '.':
				goto next;
			case 's':
				if (kvm_read(kd, (u_long)buf[i].ktr_parms[parm],
				    sbuf[parm], sizeof(sbuf[parm])) == -1)
					strcpy(sbuf[parm], "(null)");
				sbuf[parm][sizeof(sbuf[0]) - 1] = '\0';
				parms[parm] = (u_long)sbuf[parm];
				parm++;
				break;
			default:
				parms[parm] = buf[i].ktr_parms[parm];
				parm++;
				break;
			}
		}
		fprintf(out, "%6d ", i);
		if (cflag)
			fprintf(out, "%3d ", buf[i].ktr_cpu);
		if (tflag) {
			tnow = (uintmax_t)buf[i].ktr_timestamp;
			if (rflag) {
				if (tlast == -1)
					tlast = tnow;
				fprintf(out, "%16ju ", !iflag ? tlast - tnow :
				    tnow - tlast);
				tlast = tnow;
			} else
				fprintf(out, "%16ju ", tnow);
		}
		if (fflag) {
			if (kvm_read(kd, (u_long)buf[i].ktr_file, fbuf,
			    sizeof(fbuf)) == -1)
				strcpy(fbuf, "(null)");
			snprintf(obuf, sizeof(obuf), "%s:%d", fbuf,
			    buf[i].ktr_line);
			fprintf(out, "%-40s ", obuf);
		}
		if (hflag)
			fprintf(out, "%p ", buf[i].ktr_thread);
		fprintf(out, desc, parms[0], parms[1], parms[2], parms[3],
		    parms[4], parms[5]);
		fprintf(out, "\n");
		if (!iflag) {
			/*
			 * 'index' and 'index2' are the values of 'ktr_idx'
			 * before and after the KTR buffer was copied into
			 * 'buf'. Since the KTR entries between 'index' and
			 * 'index2' were in flux while the KTR buffer was
			 * being copied to userspace we don't dump them.
			 */
			if (lflag) {
				if (++i == entries)
					i = 0;
				if (i == index)
					break;
			} else {
				if (i == index2)
					break;
				if (--i < 0)
					i = entries - 1;
			}
		} else {
			if (++i == entries)
				break;
		}
	}

	/*
	 * In "live" mode, poll 'ktr_idx' periodically and dump any
	 * new entries since our last pass through the ring.
	 */
	if (lflag && !iflag) {
		while (index == index2) {
			usleep(50 * 1000);
			if (kvm_read(kd, nl[2].n_value, &index2,
			    sizeof(index2)) == -1)
				errx(1, "%s", kvm_geterr(kd));
		}
		i = index;
		index = index2;
		if (kvm_read(kd, bufptr, buf, sizeof(*buf) * entries) == -1 ||
		    kvm_read(kd, nl[2].n_value, &index2, sizeof(index2)) == -1)
			errx(1, "%s", kvm_geterr(kd));
		goto dump_entries;
	}

	return (0);
}

static void
usage(void)
{

	fprintf(stderr, USAGE);
	exit(1);
}
