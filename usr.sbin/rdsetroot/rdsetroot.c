/* $OpenBSD: rdsetroot.c,v 1.5 2023/04/24 14:06:01 krw Exp $ */

/*
 * Copyright (c) 2019 Sunil Nimmagadda <sunil@openbsd.org>
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

#include <sys/mman.h>
#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int find_rd_root_image(uint64_t *, uint64_t *, off_t *, size_t *);
int symbol_get_u64(const char *, uint64_t *);
__dead void usage(void);

Elf	*e;
Elf_Scn	*symtab;
size_t	 nsymb, strtabndx, strtabsz;

int
main(int argc, char **argv)
{
	GElf_Shdr	 shdr;
	Elf_Scn		*scn;
	char		*dataseg, *kernel = NULL, *fs = NULL, *name;
	off_t		 mmap_off, rd_root_size_val;
	size_t		 shstrndx, mmap_size;
	uint64_t	 rd_root_size_off, rd_root_image_off;
	uint32_t	*ip;
	int		 ch, debug = 0, fsfd, kfd, n, sflag = 0, xflag = 0;

	while ((ch = getopt(argc, argv, "dsx")) != -1) {
		switch (ch) {
		case 'd':
			debug = 1;
			break;
		case 's':
			sflag = 1;
			break;
		case 'x':
			xflag = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (sflag && (debug || xflag || argc > 1))
		usage();

	if (argc == 1)
		kernel = argv[0];
	else if (argc == 2) {
		kernel = argv[0];
		fs = argv[1];
	} else
		usage();

	if ((kfd = open(kernel, xflag ? O_RDONLY : O_RDWR)) < 0)
		err(1, "%s", kernel);

	if (fs) {
		if (xflag)
			fsfd = open(fs, O_RDWR | O_CREAT | O_TRUNC, 0644);
		else
			fsfd = open(fs, O_RDONLY);
	} else {
		if (xflag)
			fsfd = dup(STDOUT_FILENO);
		else
			fsfd = dup(STDIN_FILENO);
	}
	if (fsfd < 0)
		err(1, "%s", fs);

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if (elf_version(EV_CURRENT) == EV_NONE)
		errx(1, "elf_version: %s", elf_errmsg(-1));

	if ((e = elf_begin(kfd, xflag ? ELF_C_READ : ELF_C_RDWR, NULL)) == NULL)
		errx(1, "elf_begin: %s", elf_errmsg(-1));

	if (elf_kind(e) != ELF_K_ELF)
		errx(1, "%s: not an elf", kernel);

	if (gelf_getclass(e) == ELFCLASSNONE)
		errx(1, "%s: invalid elf, not 32 or 64 bit", kernel);

	/* Retrieve index of section name string table. */
	if (elf_getshdrstrndx(e, &shstrndx) != 0)
		errx(1, "elf_getshdrstrndx: %s", elf_errmsg(-1));

	/* Find symbol table, string table. */
	scn = symtab = NULL;
	while ((scn = elf_nextscn(e, scn)) != NULL) {
		if (gelf_getshdr(scn, &shdr) != &shdr)
			errx(1, "elf_getshdr: %s", elf_errmsg(-1));

		if ((name = elf_strptr(e, shstrndx, shdr.sh_name)) == NULL)
			errx(1, "elf_strptr: %s", elf_errmsg(-1));

		if (strcmp(name, ELF_SYMTAB) == 0 &&
		    shdr.sh_type == SHT_SYMTAB && shdr.sh_entsize != 0) {
			symtab = scn;
			nsymb = shdr.sh_size / shdr.sh_entsize;
		}

		if (strcmp(name, ELF_STRTAB) == 0 &&
		    shdr.sh_type == SHT_STRTAB) {
			strtabndx = elf_ndxscn(scn);
			strtabsz = shdr.sh_size;
		}
	}

	if (symtab == NULL)
		errx(1, "symbol table not found");

	if (strtabndx == 0)
		errx(1, "string table not found");

	if (find_rd_root_image(&rd_root_size_off, &rd_root_image_off,
	    &mmap_off, &mmap_size) != 0)
		errx(1, "can't locate space for rd_root_image!");

	if (debug) {
		fprintf(stderr, "rd_root_size_off: 0x%llx\n", rd_root_size_off);
		fprintf(stderr, "rd_root_image_off: 0x%llx\n",
		    rd_root_image_off);
	}

	/*
	 * Map in the whole data segment.
	 * The file offset needs to be page aligned.
	 */
	dataseg = mmap(NULL, mmap_size,
	    xflag ? PROT_READ : PROT_READ | PROT_WRITE,
	    MAP_SHARED, kfd, mmap_off);
	if (dataseg == MAP_FAILED)
		err(1, "%s: cannot map data seg", kernel);

	/*
	 * Find value in the location: rd_root_size
	 */
	ip = (uint32_t *) (dataseg + rd_root_size_off);
	rd_root_size_val = *ip;
	if (sflag) {
		fprintf(stdout, "%llu\n", (unsigned long long)rd_root_size_val);
		goto done;
	}

	if (debug) {
		fprintf(stderr, "rd_root_size  val: 0x%llx (%lld blocks)\n",
		    (unsigned long long)rd_root_size_val,
		    (unsigned long long)rd_root_size_val >> 9);
		fprintf(stderr, "copying root image...\n");
	}

	if (xflag) {
		n = write(fsfd, dataseg + rd_root_image_off,
		    (size_t)rd_root_size_val);
		if (n != rd_root_size_val)
			err(1, "write");
	} else {
		struct stat sstat;

		if (fstat(fsfd, &sstat) == -1)
			err(1, "fstat");
		if (S_ISREG(sstat.st_mode) &&
		    sstat.st_size > rd_root_size_val) {
			fprintf(stderr, "ramdisk too small 0x%llx 0x%llx\n",
			    (unsigned long long)sstat.st_size,
			    (unsigned long long)rd_root_size_val);
			exit(1);
		}
		n = read(fsfd, dataseg + rd_root_image_off,
		    (size_t)rd_root_size_val);
		if (n < 0)
			err(1, "read");

		msync(dataseg, mmap_size, 0);
	}

	if (debug)
		fprintf(stderr, "...copied %d bytes\n", n);

 done:
	elf_end(e);
	return 0;
}

int
find_rd_root_image(uint64_t *rd_root_size_off, uint64_t *rd_root_image_off,
    off_t *pmmap_off, size_t *pmmap_size)
{
	GElf_Phdr	phdr;
	size_t		i, phdrnum;
	unsigned long	kernel_start, kernel_size;
	uint64_t	adiff, rd_root_size, rd_root_image, size_off, image_off;
	int		error = 1;

	if (symbol_get_u64("rd_root_size", &rd_root_size) != 0)
		errx(1, "no rd_root_image symbols?");

	if (symbol_get_u64("rd_root_image", &rd_root_image) != 0)
		errx(1, "no rd_root_image symbols?");

	/* Retrieve number of program headers. */
	if (elf_getphdrnum(e, &phdrnum) != 0)
		errx(1, "elf_getphdrnum: %s", elf_errmsg(-1));

	/* Locate the data segment. */
	for (i = 0; i < phdrnum; i++) {
		if (gelf_getphdr(e, i, &phdr) != &phdr)
			errx(1, "gelf_getphdr: %s", elf_errmsg(-1));

		if (phdr.p_type != PT_LOAD)
			continue;

		kernel_start = phdr.p_paddr;
		kernel_size = phdr.p_filesz;
		adiff = phdr.p_vaddr - phdr.p_paddr;

		size_off = rd_root_size - kernel_start;
		image_off = rd_root_image - kernel_start;
		if (size_off < adiff || image_off < adiff)
			continue;

		size_off -= adiff;
		image_off -= adiff;
		if (image_off >= kernel_size)
			continue;
		if (size_off >= kernel_size)
			errx(1, "rd_root_size not in data segment");

		*pmmap_off = phdr.p_offset;
		*pmmap_size = kernel_size;
		*rd_root_size_off = size_off;
		*rd_root_image_off = image_off;
		error = 0;
		break;
	}

	return error;
}

int
symbol_get_u64(const char *symbol, uint64_t *result)
{
	GElf_Sym	 sym;
	Elf_Data	*data;
	const char	*name;
	size_t		 i;
	int		 error = 1;

	data = NULL;
	while ((data = elf_rawdata(symtab, data)) != NULL) {
		for (i = 0; i < nsymb; i++) {
			if (gelf_getsym(data, i, &sym) != &sym)
				continue;

			if (sym.st_name >= strtabsz)
				break;

			if ((name = elf_strptr(e, strtabndx,
			    sym.st_name)) == NULL)
				continue;

			if (strcmp(name, symbol) == 0) {
				if (result)
					*result = sym.st_value;
				error = 0;
				break;
			}
		}
	}

	return error;
}

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s -s kernel\n", __progname);
	fprintf(stderr, "       %s [-dx] kernel [disk.fs]\n", __progname);
	exit(1);
}
