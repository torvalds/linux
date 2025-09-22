/*	$OpenBSD: mopa.out.c,v 1.19 2024/10/16 18:47:48 miod Exp $ */

/*
 * mopa.out - Convert a Unix format kernel into something that
 * can be transferred via MOP.
 *
 * This code was written while referring to the NetBSD/vax boot
 * loader. Therefore anything that can be booted by the Vax
 * should be convertible with this program.
 *
 * If necessary, the a.out header is stripped, and the program
 * segments are padded out. The BSS segment is zero filled.
 * A header is prepended that looks like an IHD header. In
 * particular the Unix machine ID is placed where mopd expects
 * the image type to be (offset is IHD_W_ALIAS). If the machine
 * ID could be mistaken for a DEC image type, then the conversion
 * is aborted. The original a.out header is copied into the front
 * of the header so that once we have detected the Unix machine
 * ID we can haul the load address and the xfer address out.
 */

/*
 * Copyright (c) 1996 Lloyd Parkes.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Lloyd Parkes.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "os.h"
#include "common/common.h"
#include "common/mopdef.h"
#include "common/file.h"
#if defined(__OpenBSD__)
#include <a.out.h>
#endif
#if defined(__FreeBSD__)
#include <sys/imgact_aout.h>
#endif
#if defined(__bsdi__)
#include <a.out.h>
#define NOAOUT
#endif
#if !defined(MID_VAX)
#define MID_VAX 140
#endif

#ifndef NOELF
#if defined(__NetBSD__) || defined(__OpenBSD__)
#include <elf.h>
#else
#define NOELF
#endif
#endif

#ifndef NOELF
#if !defined(_LP64)
#define NOELF64
#endif
#endif

u_char header[512];		/* The MOP header we generate is 1 block. */
#if !defined(NOAOUT)
struct exec ex, ex_swap;
#endif

int
main (int argc, char **argv)
{
	FILE   *out;		/* A FILE because that is easier. */
	int	i, j;
	struct dllist dl;
	short image_type;

#ifdef NOAOUT
	fprintf(stderr, "%s: has no function in OS/BSD\n", argv[0]);
	return(1);
#endif

	if (argc != 3) {
		fprintf (stderr, "usage: %s infile outfile\n", argv[0]);
		return (1);
	}

	dl.ldfd = open (argv[1], O_RDONLY);
	if (dl.ldfd == -1)
		err(2, "open `%s'", argv[1]);

	if (GetFileInfo(&dl, 0) == -1)
		errx(3, "`%s' is an unknown file type", argv[1]);

	switch (dl.image_type) {
	case IMAGE_TYPE_MOP:
		errx(3, "`%s' is already a MOP image", argv[1]);
		break;

#ifndef NOELF
	case IMAGE_TYPE_ELF32:
		if (dl.e_machine != EM_VAX)
			printf("WARNING: `%s' is not a VAX image "
			    "(machine=%d)\n", argv[1], dl.e_machine);
		for (i = 0, j = 0; j < dl.e_nsec; j++)
			i += dl.e_sections[j].s_fsize + dl.e_sections[j].s_pad;
		image_type = IHD_C_NATIVE;
		break;
#endif

#if !defined(NOELF) && !defined(NOELF64)
	case IMAGE_TYPE_ELF64:
		if (dl.e_machine != EM_ALPHA && dl.e_machine != EM_ALPHA_EXP)
			printf("WARNING: `%s' is not an ALPHA image "
			    "(machine=%d)\n", argv[1], dl.e_machine);
		for (i = 0, j = 0; j < dl.e_nsec; j++)
			i += dl.e_sections[j].s_fsize + dl.e_sections[j].s_pad;
		image_type = IHD_C_ALPHA;
		break;
#endif

#ifndef NOAOUT
	case IMAGE_TYPE_AOUT:
		if (dl.a_mid != MID_VAX)
			printf("WARNING: `%s' is not a VAX image (mid=%d)\n",
			    argv[1], dl.a_mid);
		i = dl.a_text + dl.a_text_fill + dl.a_data + dl.a_data_fill +
		    dl.a_bss  + dl.a_bss_fill;
		image_type = IHD_C_NATIVE;
		break;
#endif

	default:
		errx(3, "Image type `%s' not supported",
		    FileTypeName(dl.image_type));
	}

	dl.nloadaddr = dl.loadaddr;
	dl.lseek     = lseek(dl.ldfd,0L,SEEK_CUR);
	dl.a_lseek   = 0;
	dl.count     = 0;
	dl.dl_bsz    = 512;

	switch (image_type) {
	default:
	case IHD_C_NATIVE:
		/* Offset to ISD section. */
		mopFilePutLX(header, IHD_W_SIZE, 0xd4, 2);
		/* Offset to 1st section.*/
		mopFilePutLX(header, IHD_W_ACTIVOFF, 0x30, 2);
		/* It's a VAX image.*/
		mopFilePutLX(header, IHD_W_ALIAS, IHD_C_NATIVE, 2);
		/* Only one header block. */
		mopFilePutLX(header, IHD_B_HDRBLKCNT, 1, 1);

		/* Xfer Addr */
		mopFilePutLX(header, 0x30 + IHA_L_TFRADR1, dl.xferaddr, 4);

		/* load Addr */
		mopFilePutLX(header, 0xd4 + ISD_V_VPN, dl.loadaddr / 512, 2);
		/* Imagesize in blks.*/
		i = (i + 1) / 512;
		mopFilePutLX(header, 0xd4 + ISD_W_PAGCNT, i, 2);
		break;
	case IHD_C_ALPHA:
		/* Offset to ISD section. */
		mopFilePutLX(header, EIHD_L_ISDOFF, 0xd4, 4);
		/* It's an alpha image.*/
		mopFilePutLX(header, IHD_W_ALIAS, IHD_C_ALPHA, 2);
		/* Only one header block. */
		mopFilePutLX(header, EIHD_L_HDRBLKCNT, 1, 4);

		/* Imagesize in bytes.*/
		mopFilePutLX(header, 0xd4 + EISD_L_SECSIZE, i, 4);
		break;
	}

	out = fopen (argv[2], "w");
	if (!out)
		err(2, "writing `%s'", argv[2]);

	/* Now we do the actual work. Write MOP-image header */

	fwrite (header, sizeof (header), 1, out);

	switch (dl.image_type) {
	case IMAGE_TYPE_MOP:
		abort();

	case IMAGE_TYPE_ELF32:
#ifdef NOELF
		abort();
#else
		fprintf(stderr, "copying ");
		for (j = 0; j < dl.e_nsec; j++)
			fprintf(stderr, "%s%u+%u", j == 0 ? "" : "+",
			    dl.e_sections[j].s_fsize,
			    dl.e_sections[j].s_pad);
		fprintf(stderr, "->0x%x\n", dl.xferaddr);
#endif
		break;

	case IMAGE_TYPE_AOUT:
#ifdef NOAOUT
		abort();
#else
		fprintf(stderr, "copying %u+%u+%u->0x%x\n", dl.a_text,
		    dl.a_data, dl.a_bss, dl.xferaddr);
#endif
		break;
	default:
		break;
	}

	while ((i = mopFileRead(&dl,header)) > 0) {
		(void)fwrite(header, i, 1, out);
	}

	fclose (out);
	exit(0);
}
