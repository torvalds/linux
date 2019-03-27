/*
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2003
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <net/if.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <libgen.h>
#include <err.h>
#include <ctype.h>

#include <compat/ndis/pe_var.h>

#include "inf.h"

static int insert_padding(void **, int *);
extern const char *__progname;

/*
 * Sections within Windows PE files are defined using virtual
 * and physical address offsets and virtual and physical sizes.
 * The physical values define how the section data is stored in
 * the executable file while the virtual values describe how the
 * sections will look once loaded into memory. It happens that
 * the linker in the Microsoft(r) DDK will tend to generate
 * binaries where the virtual and physical values are identical,
 * which means in most cases we can just transfer the file
 * directly to memory without any fixups. This is not always
 * the case though, so we have to be prepared to handle files
 * where the in-memory section layout differs from the disk file
 * section layout.
 *
 * There are two kinds of variations that can occur: the relative
 * virtual address of the section might be different from the
 * physical file offset, and the virtual section size might be
 * different from the physical size (for example, the physical
 * size of the .data section might be 1024 bytes, but the virtual
 * size might be 1384 bytes, indicating that the data section should
 * actually use up 1384 bytes in RAM and be padded with zeros). What we
 * do is read the original file into memory and then make an in-memory
 * copy with all of the sections relocated, re-sized and zero padded
 * according to the virtual values specified in the section headers.
 * We then emit the fixed up image file for use by the if_ndis driver.
 * This way, we don't have to do the fixups inside the kernel.
 */

#define ROUND_DOWN(n, align)    (((uintptr_t)n) & ~((align) - 1l))
#define ROUND_UP(n, align)      ROUND_DOWN(((uintptr_t)n) + (align) - 1l, \
                                (align))

#define SET_HDRS(x)	\
	dos_hdr = (image_dos_header *)x;				\
	nt_hdr = (image_nt_header *)(x + dos_hdr->idh_lfanew);		\
	sect_hdr = IMAGE_FIRST_SECTION(nt_hdr);

static int
insert_padding(void **imgbase, int *imglen)
{
        image_section_header	*sect_hdr;
        image_dos_header	*dos_hdr;
        image_nt_header		*nt_hdr;
	image_optional_header	opt_hdr;
        int			i = 0, sections, curlen = 0;
	int			offaccum = 0, oldraddr, oldrlen;
	uint8_t			*newimg, *tmp;

	newimg = malloc(*imglen);

	if (newimg == NULL)
		return(ENOMEM);

	bcopy(*imgbase, newimg, *imglen);
	curlen = *imglen;

	if (pe_get_optional_header((vm_offset_t)newimg, &opt_hdr))
		return(0);

        sections = pe_numsections((vm_offset_t)newimg);

	SET_HDRS(newimg);

	for (i = 0; i < sections; i++) {
		oldraddr = sect_hdr->ish_rawdataaddr;
		oldrlen = sect_hdr->ish_rawdatasize;
		sect_hdr->ish_rawdataaddr = sect_hdr->ish_vaddr;
		offaccum += ROUND_UP(sect_hdr->ish_vaddr - oldraddr,
		    opt_hdr.ioh_filealign);
		offaccum +=
		    ROUND_UP(sect_hdr->ish_misc.ish_vsize,
			     opt_hdr.ioh_filealign) -
		    ROUND_UP(sect_hdr->ish_rawdatasize,
			     opt_hdr.ioh_filealign);
		tmp = realloc(newimg, *imglen + offaccum);
		if (tmp == NULL) {
			free(newimg);
			return(ENOMEM);
		}
		newimg = tmp;
		SET_HDRS(newimg);
		sect_hdr += i;
		bzero(newimg + sect_hdr->ish_rawdataaddr,
		    ROUND_UP(sect_hdr->ish_misc.ish_vsize,
		    opt_hdr.ioh_filealign));
		bcopy((uint8_t *)(*imgbase) + oldraddr,
		    newimg + sect_hdr->ish_rawdataaddr, oldrlen);
		sect_hdr++;
	}

	free(*imgbase);

	*imgbase = newimg;
	*imglen += offaccum;

	return(0);
}

static void
usage(void)
{
	fprintf(stderr, "Usage: %s [-O] [-i <inffile>] -s <sysfile> "
	    "[-n devname] [-o outfile]\n", __progname);
	fprintf(stderr, "       %s -f <firmfile>\n", __progname);

	exit(1);
}

static void
bincvt(char *sysfile, char *outfile, void *img, int fsize)
{
	char			*ptr;
	char			tname[] = "/tmp/ndiscvt.XXXXXX";
	char			sysbuf[1024];
	FILE			*binfp;

	mkstemp(tname);

	binfp = fopen(tname, "a+");
	if (binfp == NULL)
		err(1, "opening %s failed", tname);

	if (fwrite(img, fsize, 1, binfp) != 1)
		err(1, "failed to output binary image");

	fclose(binfp);

	outfile = strdup(basename(outfile));
	if (strchr(outfile, '.'))
		*strchr(outfile, '.') = '\0';

	snprintf(sysbuf, sizeof(sysbuf),
#ifdef __i386__
	    "objcopy -I binary -O elf32-i386-freebsd -B i386 %s %s.o\n",
#endif
#ifdef __amd64__
	    "objcopy -I binary -O elf64-x86-64-freebsd -B i386 %s %s.o\n",
#endif
	    tname, outfile);
	printf("%s", sysbuf);
	system(sysbuf);
	unlink(tname);

	ptr = tname;
	while (*ptr) {
		if (*ptr == '/' || *ptr == '.')
			*ptr = '_';
		ptr++;
	}

	snprintf(sysbuf, sizeof(sysbuf),
	    "objcopy --redefine-sym _binary_%s_start=ndis_%s_drv_data_start "
	    "--strip-symbol _binary_%s_size "
	    "--redefine-sym _binary_%s_end=ndis_%s_drv_data_end %s.o %s.o\n",
	    tname, sysfile, tname, tname, sysfile, outfile, outfile);
	printf("%s", sysbuf);
	system(sysbuf);
	free(outfile);

	return;
}
   
static void
firmcvt(char *firmfile)
{
	char			*basefile, *outfile, *ptr;
	char			sysbuf[1024];

	outfile = strdup(basename(firmfile));
	basefile = strdup(outfile);

	snprintf(sysbuf, sizeof(sysbuf),
#ifdef __i386__
	    "objcopy -I binary -O elf32-i386-freebsd -B i386 %s %s.o\n",
#endif
#ifdef __amd64__
	    "objcopy -I binary -O elf64-x86-64-freebsd -B i386 %s %s.o\n",
#endif
	    firmfile, outfile);
	printf("%s", sysbuf);
	system(sysbuf);

	ptr = firmfile;
	while (*ptr) {
		if (*ptr == '/' || *ptr == '.')
			*ptr = '_';
		ptr++;
	}
	ptr = basefile;
	while (*ptr) {
		if (*ptr == '/' || *ptr == '.')
			*ptr = '_';
		else
			*ptr = tolower(*ptr);
		ptr++;
	}

	snprintf(sysbuf, sizeof(sysbuf),
	    "objcopy --redefine-sym _binary_%s_start=%s_start "
	    "--strip-symbol _binary_%s_size "
	    "--redefine-sym _binary_%s_end=%s_end %s.o %s.o\n",
	    firmfile, basefile, firmfile, firmfile,
	    basefile, outfile, outfile);
	ptr = sysbuf;
	printf("%s", sysbuf);
	system(sysbuf);

	snprintf(sysbuf, sizeof(sysbuf),
	    "ld -Bshareable -d -warn-common -o %s.ko %s.o\n",
	    outfile, outfile);
	printf("%s", sysbuf);
	system(sysbuf);

	free(basefile);

	exit(0);
}

int
main(int argc, char *argv[])
{
	FILE			*fp, *outfp;
	int			i, bin = 0;
	void			*img;
	int			n, fsize, cnt;
	unsigned char		*ptr;
	char			*inffile = NULL, *sysfile = NULL;
	char			*outfile = NULL, *firmfile = NULL;
	char			*dname = NULL;
	int			ch;

	while((ch = getopt(argc, argv, "i:s:o:n:f:O")) != -1) {
		switch(ch) {
		case 'f':
			firmfile = optarg;
			break;
		case 'i':
			inffile = optarg;
			break;
		case 's':
			sysfile = optarg;
			break;
		case 'o':
			outfile = optarg;
			break;
		case 'n':
			dname = optarg;
			break;
		case 'O':
			bin = 1;
			break;
		default:
			usage();
			break;
		}
	}

	if (firmfile != NULL)
		firmcvt(firmfile);

	if (sysfile == NULL)
		usage();

	/* Open the .SYS file and load it into memory */
	fp = fopen(sysfile, "r");
	if (fp == NULL)
		err(1, "opening .SYS file '%s' failed", sysfile);
	fseek (fp, 0L, SEEK_END);
	fsize = ftell (fp);
	rewind (fp);
	img = calloc(fsize, 1);
	n = fread (img, fsize, 1, fp);
	if (n == 0)
		err(1, "reading .SYS file '%s' failed", sysfile);

	fclose(fp);

	if (insert_padding(&img, &fsize)) {
		fprintf(stderr, "section relocation failed\n");
		exit(1);
	}

	if (outfile == NULL || strcmp(outfile, "-") == 0)
		outfp = stdout;
	else {
		outfp = fopen(outfile, "w");
		if (outfp == NULL)
			err(1, "opening output file '%s' failed", outfile);
	}

	fprintf(outfp, "\n/*\n");
	fprintf(outfp, " * Generated from %s and %s (%d bytes)\n",
	    inffile == NULL ? "<notused>" : inffile, sysfile, fsize);
	fprintf(outfp, " */\n\n");

	if (dname != NULL) {
		if (strlen(dname) > IFNAMSIZ)
			err(1, "selected device name '%s' is "
			    "too long (max chars: %d)", dname, IFNAMSIZ);
		fprintf (outfp, "#define NDIS_DEVNAME \"%s\"\n", dname);
		fprintf (outfp, "#define NDIS_MODNAME %s\n\n", dname);
	}

	if (inffile == NULL) {
		fprintf (outfp, "#ifdef NDIS_REGVALS\n");
		fprintf (outfp, "ndis_cfg ndis_regvals[] = {\n");
        	fprintf (outfp, "\t{ NULL, NULL, { 0 }, 0 }\n");
		fprintf (outfp, "#endif /* NDIS_REGVALS */\n");

		fprintf (outfp, "};\n\n");
	} else {
		fp = fopen(inffile, "r");
		if (fp == NULL)
			err(1, "opening .INF file '%s' failed", inffile);


		if (inf_parse(fp, outfp) != 0)
			errx(1, "creating .INF file - no entries created, are you using the correct files?");
		fclose(fp);
	}

	fprintf(outfp, "\n#ifdef NDIS_IMAGE\n");

	if (bin) {
		sysfile = strdup(basename(sysfile));
		ptr = (unsigned char *)sysfile;
		while (*ptr) {
			if (*ptr == '.')
				*ptr = '_';
			ptr++;
		}
		fprintf(outfp,
		    "\nextern unsigned char ndis_%s_drv_data_start[];\n",
		    sysfile);
		fprintf(outfp, "static unsigned char *drv_data = "
		    "ndis_%s_drv_data_start;\n\n", sysfile);
		bincvt(sysfile, outfile, img, fsize);
		goto done;
	}


	fprintf(outfp, "\nextern unsigned char drv_data[];\n\n");

	fprintf(outfp, "__asm__(\".data\");\n");
	fprintf(outfp, "__asm__(\".globl  drv_data\");\n");
	fprintf(outfp, "__asm__(\".type   drv_data, @object\");\n");
	fprintf(outfp, "__asm__(\".size   drv_data, %d\");\n", fsize);
	fprintf(outfp, "__asm__(\"drv_data:\");\n");

	ptr = img;
	cnt = 0;
	while(cnt < fsize) {
		fprintf (outfp, "__asm__(\".byte ");
		for (i = 0; i < 10; i++) {
			cnt++;
			if (cnt == fsize) {
				fprintf(outfp, "0x%.2X\");\n", ptr[i]);
				goto done;
			} else {
				if (i == 9)
					fprintf(outfp, "0x%.2X\");\n", ptr[i]);
				else
					fprintf(outfp, "0x%.2X, ", ptr[i]);
			}
		}
		ptr += 10;
	}

done:

	fprintf(outfp, "#endif /* NDIS_IMAGE */\n");

	if (fp != NULL)
		fclose(fp);
	fclose(outfp);
	free(img);
	exit(0);
}
