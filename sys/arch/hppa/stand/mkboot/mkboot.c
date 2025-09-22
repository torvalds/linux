/*	$OpenBSD: mkboot.c,v 1.22 2024/10/16 18:47:48 miod Exp $	*/

/*
 * Copyright (c) 1990, 1993
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
 *
 *	@(#)mkboot.c	8.1 (Berkeley) 7/15/93
 */

#include <sys/param.h>
#include <sys/stat.h>

#include <a.out.h>
#include <ctype.h>
#include <elf.h>
#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef hppa
/* hack for cross compile XXX */
#include "../../include/disklabel.h"
#else
#include <sys/disklabel.h>
#endif

int putfile(char *, int);
void __dead usage(void);
void bcddate(char *, char *);
char *lifname(char *);
int cksum(int, int *, int);

char *to_file;
int loadpoint, verbose;
u_long entry;

/*
 * Old Format:
 *	sector 0:	LIF volume header (40 bytes)
 *	sector 1:	<unused>
 *	sector 2:	LIF directory (8 x 32 == 256 bytes)
 *	sector 3-:	LIF file 0, LIF file 1, etc.
 * where sectors are 256 bytes.
 *
 * New Format:
 *	sector 0:	LIF volume header (40 bytes)
 *	sector 1:	<unused>
 *	sector 2:	LIF directory (8 x 32 == 256 bytes)
 *	sector 3:	<unused>
 *	sector 4-31:	disklabel (~300 bytes right now)
 *	sector 32-:	LIF file 0, LIF file 1, etc.
 */
int
main(int argc, char **argv)
{
	int to;
	register int n, pos, c;
	char buf[LIF_FILESTART];
	struct lifvol *lifv = (struct lifvol *)buf;
	struct lifdir *lifd = (struct lifdir *)(buf + LIF_DIRSTART);

	while ((c = getopt(argc, argv, "vl:")) != -1) {
		switch (c) {
		case 'v':
			verbose++;
			break;
		case 'l':
			sscanf(optarg, "0x%x", &loadpoint);
			break;
		default:
			usage();
		}
	}
	if (argc - optind < 2)
		usage();
	else if (argc - optind > 8)
		errx(1, "too many boot programs (max 8 supported)");

	to_file = argv[--argc];
	if ((to = open(to_file, O_RDWR | O_TRUNC | O_CREAT, 0644)) < 0)
		err(1, "%s: open", to_file);

	bzero(buf, sizeof(buf));
	/* clear possibly unused directory entries */
	memset(lifd[1].dir_name, ' ', sizeof lifd[1].dir_name);
	lifd[1].dir_type = -1;
	lifd[1].dir_addr = 0;
	lifd[1].dir_length = 0;
	lifd[1].dir_flag = 0xFF;
	lifd[1].dir_implement = 0;
	lifd[7] = lifd[6] = lifd[5] = lifd[4] = lifd[3] = lifd[2] = lifd[1];

	/* record volume info */
	lifv->vol_id = htobe16(LIF_VOL_ID);
	strncpy(lifv->vol_label, "BOOT44", 6);
	lifv->vol_addr = htobe32(btolifs(LIF_DIRSTART));
	lifv->vol_oct = htobe16(LIF_VOL_OCT);
	lifv->vol_dirsize = htobe32(btolifs(LIF_DIRSIZE));
	lifv->vol_version = htobe16(1);
	lifv->vol_lastvol = lifv->vol_number =  htobe16(1);
	lifv->vol_length = LIF_FILESTART;
	bcddate(to_file, lifv->vol_toc);
	lifv->ipl_addr = htobe32(LIF_FILESTART);
	lifv->ipl_size = 0;
	lifv->ipl_entry = 0;

	argv += optind;
	argc -= optind;
	optind = 0;
	for (pos = LIF_FILESTART; optind < argc; optind++) {

		/* output bootfile */
		lseek(to, pos, 0);
		lifd[optind].dir_addr = htobe32(btolifs(pos));
		n = btolifs(putfile(argv[optind], to));
		if (lifv->ipl_entry == 0) {
			lifv->ipl_entry = htobe32(loadpoint + entry);
			lifv->ipl_size = htobe32(lifstob(n));
			lifd[optind].dir_type = htobe16(LIF_DIR_ISL);
			lifd[optind].dir_implement = 0;
		} else {
			lifd[optind].dir_type = htobe16(LIF_DIR_TYPE);
			lifd[1].dir_implement = htobe32(loadpoint + entry);
		}

		strlcpy(lifd[optind].dir_name, lifname(argv[optind]),
		    sizeof lifd[optind].dir_name);
		lifd[optind].dir_length = htobe32(n);
		bcddate(argv[optind], lifd[optind].dir_toc);
		lifd[optind].dir_flag = htobe16(LIF_DIR_FLAG);

		lifv->vol_length += n;
		pos += lifstob(n);
	}

	lifv->vol_length = htobe32(lifv->vol_length);

	/* output volume/directory header info */
	lseek(to, LIF_VOLSTART, 0);
	if (write(to, buf, sizeof(buf)) != sizeof(buf))
		err(1, "%s: write LIF volume", to_file);
	lseek(to, 0, SEEK_END);

	if (close(to) < 0)
		err(1, "%s", to_file);

	return (0);
}

int
putfile(char *from_file, int to)
{
	struct exec ex;
	register int n, total;
	char buf[2048];
	int from, check_sum = 0;
	struct lif_load load;

	if ((from = open(from_file, O_RDONLY)) < 0)
		err(1, "%s", from_file);

	n = read(from, &ex, sizeof(ex));
	if (n != sizeof(ex))
		err(1, "%s: reading file header", from_file);

	entry = ex.a_entry;
	if (N_GETMAGIC(ex) == OMAGIC || N_GETMAGIC(ex) == NMAGIC)
		entry += sizeof(ex);
	else if (IS_ELF(*(Elf32_Ehdr *)&ex)) {
		Elf32_Ehdr elf_header;
		Elf32_Phdr *elf_segments;
		int i, header_count, memory_needed, elf_load_image_segment;

		(void) lseek(from, 0, SEEK_SET);
		n = read(from, &elf_header, sizeof(elf_header));
		if (n != sizeof (elf_header))
			err(1, "%s: reading ELF header", from_file);
		header_count = ntohs(elf_header.e_phnum);
		elf_segments = reallocarray(NULL, header_count,
		    sizeof(*elf_segments));
		if (elf_segments == NULL)
			err(1, "malloc");
		memory_needed = header_count * sizeof(*elf_segments);
		(void) lseek(from, ntohl(elf_header.e_phoff), SEEK_SET);
		n = read(from, elf_segments, memory_needed);
		if (n != memory_needed)
			err(1, "%s: reading ELF segments", from_file);
		elf_load_image_segment = -1;
		for (i = 0; i < header_count; i++) {
			if (elf_segments[i].p_filesz &&
			    ntohl(elf_segments[i].p_flags) & PF_X) {
				if (elf_load_image_segment != -1)
					errx(1, "%s: more than one ELF program segment", from_file);
				elf_load_image_segment = i;
			}
		}
		if (elf_load_image_segment == -1)
			errx(1, "%s: no suitable ELF program segment", from_file);
		entry = ntohl(elf_header.e_entry) +
			ntohl(elf_segments[elf_load_image_segment].p_offset) -
			ntohl(elf_segments[elf_load_image_segment].p_vaddr);
		free(elf_segments);
	} else if (*(u_char *)&ex == 0x1f && ((u_char *)&ex)[1] == 0x8b) {
		entry = 0;
	} else
		errx(1, "%s: bad magic number", from_file);

	entry += sizeof(load);
	lseek(to, sizeof(load), SEEK_CUR);
	total = 0;
	n = sizeof(buf) - sizeof(load);
	/* copy the whole file */
	for (lseek(from, 0, 0); ; n = sizeof(buf)) {
		bzero(buf, sizeof(buf));
		if ((n = read(from, buf, n)) < 0)
			err(1, "%s", from_file);
		else if (n == 0)
			break;

		if (write(to, buf, n) != n)
			err(1, "%s", to_file);

		total += n;
		check_sum = cksum(check_sum, (int *)buf, n);
	}

	/* load header */
	load.address = htobe32(loadpoint + sizeof(load));
	load.count = htobe32(4 + total);
	check_sum = cksum(check_sum, (int *)&load, sizeof(load));

	if (verbose)
		warnx("wrote %d bytes of file \'%s\'", total, from_file);

	total += sizeof(load);
	/* insert the header */
	lseek(to, -total, SEEK_CUR);
	if (write(to, &load, sizeof(load)) != sizeof(load))
		err(1, "%s", to_file);
	lseek(to, total - sizeof(load), SEEK_CUR);

	bzero(buf, sizeof(buf));
	/* pad to int */
	n = sizeof(int) - total % sizeof(int);
	if (total % sizeof(int)) {
		if (write(to, buf, n) != n)
			err(1, "%s", to_file);
		else
			total += n;
	}

	/* pad to the blocksize */
	n = sizeof(buf) - total % sizeof(buf);

	if (n < sizeof(int)) {
		n += sizeof(buf);
		total += sizeof(buf);
	} else
		total += n;

	/* TODO should pad here to the 65k boundary for tape boot */

	if (verbose)
		warnx("checksum is 0x%08x", -check_sum);

	check_sum = htobe32(-check_sum);
	if (write(to, &check_sum, sizeof(int)) != sizeof(int))
		err(1, "%s", to_file);

	n -= sizeof(int);

	if (write(to, buf, n) != n)
		err(1, "%s", to_file);

	if (close(from) < 0 )
		err(1, "%s", from_file);

	return total;
}

int
cksum(int ck, int *p, int size)
{
	/* we assume size is int-aligned */
	for (size = (size + sizeof(int) - 1) / sizeof(int); size--; p++ )
		ck += betoh32(*p);

	return ck;
}

void __dead
usage(void)
{
	extern char *__progname;
	fprintf(stderr,
		"usage: %s [-v] [-l loadpoint] prog1 {progN} outfile\n",
		__progname);
	exit(1);
}

char *
lifname(char *str)
{
	static char lname[10] = "XXXXXXXXXX";
	register int i;

	for (i = 0; i < 9; i++) {
		if (islower(*str))
			lname[i] = toupper(*str);
		else if (isalnum(*str) || *str == '_')
			lname[i] = *str;
		else
			break;
		str++;
	}
	for ( ; i < 10; i++)
		lname[i] = ' ';
	return(lname);
}


void
bcddate(char *file, char *toc)
{
	struct stat statb;
	struct tm *tm;

	stat(file, &statb);
	tm = localtime(&statb.st_ctime);
	*toc = (tm->tm_year / 10) << 4;
	*toc++ |= tm->tm_year % 10;
	*toc = ((tm->tm_mon+1) / 10) << 4;
	*toc++ |= (tm->tm_mon+1) % 10;
	*toc = (tm->tm_mday / 10) << 4;
	*toc++ |= tm->tm_mday % 10;
	*toc = (tm->tm_hour / 10) << 4;
	*toc++ |= tm->tm_hour % 10;
	*toc = (tm->tm_min / 10) << 4;
	*toc++ |= tm->tm_min % 10;
	*toc = (tm->tm_sec / 10) << 4;
	*toc |= tm->tm_sec % 10;
}
