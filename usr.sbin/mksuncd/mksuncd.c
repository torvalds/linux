/*	$OpenBSD: mksuncd.c,v 1.5 2021/12/23 09:17:19 jsg Exp $	*/

/*
 * Copyright (c) 2001 Jason L. Wright (jason@thought.net)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * WARNING! WARNING!
 * This program is not type safe (it assumes sparc type sizes) and not
 * endian safe (assumes sparc endianness).
 * WARNING! WARNING!
 */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *
 *	@(#)sun_disklabel.h	8.1 (Berkeley) 6/11/93
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <string.h>

/*
 * SunOS disk label layout (only relevant portions discovered here).
 * JLW XXX should get these definitions from elsewhere, oh well.
 */

#define	SUN_DKMAGIC	55998

/* partition info */
struct sun_dkpart {
	int	sdkp_cyloffset;		/* starting cylinder */
	int	sdkp_nsectors;		/* number of sectors */
};

struct sun_disklabel {			/* total size = 512 bytes */
	char	sl_text[128];
	char	sl_xxx1[292];
#define sl_bsdlabel	sl_xxx1		/* Embedded OpenBSD label */
	u_short sl_rpm;			/* rotational speed */
	u_short	sl_pcylinders;		/* number of physical cyls */
#define	sl_pcyl	 sl_pcylinders		/* XXX: old sun3 */
	u_short sl_sparespercyl;	/* spare sectors per cylinder */
	char	sl_xxx3[4];
	u_short sl_interleave;		/* interleave factor */
	u_short	sl_ncylinders;		/* data cylinders */
	u_short	sl_acylinders;		/* alternate cylinders */
	u_short	sl_ntracks;		/* tracks per cylinder */
	u_short	sl_nsectors;		/* sectors per track */
	char	sl_xxx4[4];
	struct sun_dkpart sl_part[8];	/* partition layout */
	u_short	sl_magic;		/* == SUN_DKMAGIC */
	u_short	sl_cksum;		/* xor checksum of all shorts */
};

int expand_file(int, off_t);
void usage(void);
int adjust_base(int, struct sun_disklabel *);
int get_label(int, struct sun_disklabel *);
int append_osfile(int, int);
off_t cylindersize(int, struct sun_disklabel *);
int adjust_label(int, struct sun_disklabel *, int, off_t, off_t);

int
expand_file(int f, off_t len)
{
	char buf[1024];
	off_t i;

	if (lseek(f, 0, SEEK_END) == -1)
		return (-1);
	bzero(buf, sizeof(buf));
	while (len) {
		i = (sizeof(buf) < len) ? sizeof(buf) : len;
		if (write(f, buf, i) != i)
			return (-1);
		len -= i;
	}
	return (0);
}

void
usage(void)
{
	fprintf(stderr, "usage: mksuncd partition isoimage bootimage\n");
}

/*
 * Adjust size of base to meet a cylinder boundary.
 */
int
adjust_base(int f, struct sun_disklabel *slp)
{
	struct stat st;
	off_t sz;

	if (lseek(f, 0, SEEK_END) == -1)
		err(1, "lseek");

	if (fstat(f, &st) == -1)
		err(1, "fstat");

	sz = ((off_t)slp->sl_nsectors) *
	    ((off_t)slp->sl_ntracks) * ((off_t)512);

	if ((st.st_size % sz) != 0) {
		if (expand_file(f, sz - (st.st_size % sz)))
			err(1, "expand_file");
	}

	return (0);
}

int
get_label(int f, struct sun_disklabel *slp)
{
	int r;

	if (lseek(f, 0, SEEK_SET) == -1)
		err(1, "lseek");

	r = read(f, slp, sizeof(*slp));
	if (r == -1)
		err(1, "read");
	if (r != sizeof(*slp))
		errx(1, "short read");

	if (slp->sl_ntracks == 0 || slp->sl_ncylinders == 0 ||
	    slp->sl_nsectors == 0)
		errx(1, "bogus disklabel");

	return (0);
}

int
main(int argc, char **argv)
{
	struct sun_disklabel sl;
	int part, bf, of;
	off_t cylstart, cylsize;

	if (argc != 4) {
		usage();
		return (1);
	}

	if (argv[1] == NULL || strlen(argv[1]) != 1 ||
	    (argv[1][0] < 'a' || argv[1][0] > 'h')) {
		usage();
		return (1);
	}
	part = argv[1][0] - 'a';

	if (argv[2] == NULL || argv[3] == NULL) {
		usage();
		return (1);
	}

	bf = open(argv[2], O_RDWR);
	if (bf == -1)
		err(1, "open");

	of = open(argv[3], O_RDONLY);
	if (of == -1)
		err(1, "open");

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if (get_label(bf, &sl))
		return (1);

	if (adjust_base(bf, &sl))
		return (1);

	cylstart = cylindersize(bf, &sl);
	cylsize = cylindersize(of, &sl);

	if (append_osfile(bf, of))
		return (1);

	if (adjust_base(bf, &sl))
		return (1);

	if (adjust_label(bf, &sl, part, cylstart, cylsize))
		return (1);

	close(bf);
	close(of);

	return (0);
}

/*
 * Put our entry into the disklabel, recompute label checksum, and
 * write it back to the disk.
 */
int
adjust_label(int f, struct sun_disklabel *slp, int part, off_t start, off_t size)
{
	u_short sum = 0, *sp;
	int i;

	if (start > 65535)
		errx(1, "start too large! %lld", (long long)start);
	if (part < 0 || part >= 8)
		errx(1, "invalid partition: %d", part);
	slp->sl_part[part].sdkp_cyloffset = start;
	slp->sl_part[part].sdkp_nsectors =
	    size * slp->sl_nsectors * slp->sl_ntracks;

	slp->sl_cksum = 0;
	sp = (u_short *)slp;
	for (i = 0; i < sizeof(*slp)/sizeof(u_short); i++) {
		sum ^= *sp;
		sp++;
	}
	slp->sl_cksum = sum;

	if (lseek(f, 0, SEEK_SET) == -1)
		return (-1);

	i = write(f, slp, sizeof(*slp));
	if (i == -1)
		err(1, "write modified label");
	if (i != sizeof(*slp))
		errx(1, "short write modified label");
	return (0);
}

int
append_osfile(int outf, int inf)
{
	char buf[512];
	int r, len;

	while (1) {
		len = read(inf, buf, sizeof(buf));
		if (len == -1)
			err(1, "read osfile");
		if (len == 0)
			return (0);

		r = write(outf, buf, len);
		if (r == -1)
			err(1, "write basefile");
		if (r != len)
			errx(1, "short write basefile");
	}
}

off_t
cylindersize(int f, struct sun_disklabel *slp)
{
	struct stat st;
	off_t sz, r;

	if (fstat(f, &st) == -1)
		err(1, "fstat");

	sz = ((off_t)slp->sl_nsectors) *
	    ((off_t)slp->sl_ntracks) * ((off_t)512);

	r = st.st_size / sz;

	if ((st.st_size % sz) == 0)
		return (r);
	return (r + 1);
}
