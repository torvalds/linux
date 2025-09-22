/*	$OpenBSD: fxproc0.c,v 1.2 2021/12/13 16:56:49 deraadt Exp $	*/
/*
 * Copyright (c) 2018 Alexander Bluhm <bluhm@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <machine/fpu.h>
#include <machine/pcb.h>

#include <err.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define nitems(_a)     (sizeof((_a)) / sizeof((_a)[0]))

void __dead usage(void);
void fenv_proc(kvm_t *, unsigned long);

void __dead
usage(void)
{
	fprintf(stderr, "usage: %s [-M core] [-N system]\n", getprogname());
	exit(1);
}

int
main(int argc, char *argv[])
{
	char errbuf[_POSIX2_LINE_MAX];
	char *memf, *nlistf;
	kvm_t *kd;
	int ch;
	struct nlist nl[] = { { .n_name = "_proc0" }, { .n_name = NULL } };

	memf = nlistf = NULL;
	while ((ch = getopt(argc, argv, "M:N:")) != -1) {
		switch(ch) {
		case 'M':
			memf = optarg;
			break;
		case 'N':
			nlistf = optarg;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc)
		usage();

	kd = kvm_openfiles(nlistf, memf, NULL, O_RDONLY, errbuf);
	if (kd == NULL)
		errx(1, "kvm_openfiles: %s", errbuf);
	if (kvm_nlist(kd, nl) == -1)
		errx(1, "kvm_nlist: %s", kvm_geterr(kd));
	if (nl[0].n_type == 0)
		errx(1, "name '%s' has type %d", nl[0].n_name, nl[0].n_type);
	fenv_proc(kd, nl[0].n_value);

	if (kvm_close(kd) == -1)
		errx(1, "kvm_close: %s", kvm_geterr(kd));
	return 0;
}

void
fenv_proc(kvm_t *kd, unsigned long p)
{
	struct proc proc;
	struct user user;
	struct fxsave64 *fxs = &user.u_pcb.pcb_savefpu.fp_fxsave;
	size_t i;

	if (kvm_read(kd, p, &proc, sizeof(proc)) == -1)
		errx(1, "kvm_read proc: %s", kvm_geterr(kd));
	if (kvm_read(kd, (u_long)proc.p_addr, &user, sizeof(user)) == -1)
		errx(1, "kvm_read user: %s", kvm_geterr(kd));

	if (fxs != &fxs->fx_fcw)
		errx(1, "fxsave start %p, fx_fcw start %p",
		    &fxs, &fxs->fx_fcw);
	printf("fcw\t%04x\n", fxs->fx_fcw);
	printf("fsw\t%04x\n", fxs->fx_fsw);
	printf("ftw\t%02x\n", fxs->fx_ftw);
	printf("unused1\t%02x\n", fxs->fx_unused1);
	printf("fop\t%04x\n", fxs->fx_fop);
	printf("rip\t%016llx\n", fxs->fx_rip);
	printf("rdp\t%016llx\n", fxs->fx_rdp);
	printf("mxcsr\t%08x\n", fxs->fx_mxcsr);
	printf("mxcsr_mask\t%08x\n", fxs->fx_mxcsr_mask);
	if (&fxs->fx_mxcsr_mask + 1 != fxs->fx_st)
		errx(1, "fx_mxcsr_mask end %p, fx_st start %p",
		    &fxs->fx_mxcsr_mask + 1, fxs->fx_st);
	for (i = 0; i < nitems(fxs->fx_st); i++)
		printf("st[%zu]\t%016llx:%016llx\n", i,
		    fxs->fx_st[i][1], fxs->fx_st[i][0]);
	if (&fxs->fx_st[i] != fxs->fx_xmm)
		errx(1, "fx_st end %p, fx_xmm start %p",
		    &fxs->fx_st[i], fxs->fx_xmm);
	for (i = 0; i < nitems(fxs->fx_xmm); i++)
		printf("xmm[%zu]\t%016llx:%016llx\n", i,
		    fxs->fx_xmm[i][1], fxs->fx_xmm[i][0]);
	if (&fxs->fx_xmm[i] != fxs->fx_unused3)
		errx(1, "fx_xmm end %p, fx_unused3 start %p",
		    &fxs->fx_xmm[i], fxs->fx_unused3);
	for (i = 0; i < nitems(fxs->fx_unused3); i++)
		printf("unused3[%zu]\t%02x\n", i, fxs->fx_unused3[i]);
	if (&fxs->fx_unused3[i] != fxs + 1)
		errx(1, "fx_unused3 end %p, fxsave end %p",
		    &fxs->fx_unused3[i], fxs + 1);
}
