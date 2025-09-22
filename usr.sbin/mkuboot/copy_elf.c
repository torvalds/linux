/*      $OpenBSD: copy_elf.c,v 1.7 2020/04/28 04:17:42 deraadt Exp $       */

/*
 * Copyright (c) 2013 Miodrag Vallat.
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

#include <stdio.h>
#include <err.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <elf.h>

#if defined(ELFSIZE) && (ELFSIZE == 32)
#define elfoff2h(x) letoh32(x)
#define h2elfoff(x) htole32(x)
#elif defined(ELFSIZE) && (ELFSIZE == 64)
#define elfoff2h(x) letoh64(x)
#define h2elfoff(x) htole64(x)
#else
#error "unknown elf size"
#endif

struct image_header;

#define        roundup(x, y)   ((((x)+((y)-1))/(y))*(y))

extern u_long copy_data(int, const char *, int, const char *, u_long,
	    struct image_header *, Elf_Word);
u_long	copy_mem(void *, int, const char *, u_long, struct image_header *,
	    Elf_Word);
extern u_long fill_zeroes(int, const char *, u_long, struct image_header *,
	    Elf_Word);

u_long
ELFNAME(copy_elf)(int ifd, const char *iname, int ofd, const char *oname,
    u_long crc, struct image_header *ih)
{
	ssize_t nbytes;
	Elf_Ehdr ehdr, elf;
	Elf_Phdr phdr;
	Elf_Addr vaddr, ovaddr, svaddr, off, ssym;
	Elf_Shdr *shp, *wshp;
	Elf_Addr esym = 0, esymval;
	int i, sz, havesyms;

	nbytes = read(ifd, &ehdr, sizeof ehdr);
	if (nbytes == -1)
		err(1, "%s", iname);
	if (nbytes != sizeof ehdr)
		return 0;

	elf = ehdr;

	if (lseek(ifd, (off_t)elfoff2h(elf.e_shoff), SEEK_SET) == -1)
		err(1, "%s unable to seek to section header", iname);

	sz = letoh16(elf.e_shnum) * sizeof(Elf_Shdr);
	shp = calloc(sz, 1);
	if (read(ifd, shp, sz) != sz)
		err(1, "%s: read section headers", iname);

	wshp = calloc(sz, 1);
	memcpy(wshp, shp, sz);

	/* first walk the load sections to find the kernel addresses */
	/* next we walk the sections to find the
	 * location of esym (first address of data space
	 */
	for (i = 0; i < letoh16(ehdr.e_phnum); i++) {
		if (lseek(ifd, elfoff2h(ehdr.e_phoff) + i *
		    letoh16(ehdr.e_phentsize), SEEK_SET) == (off_t)-1)
			err(1, "%s", iname);
		if (read(ifd, &phdr, sizeof phdr) != sizeof(phdr))
			err(1, "%s", iname);
		/* assumes it loads in incrementing address order */
		if (letoh32(phdr.p_type) == PT_LOAD)
			vaddr = elfoff2h(phdr.p_vaddr) +
			    elfoff2h(phdr.p_memsz);
	}

	/* ok, we need to write the elf header and section header
	 * which contains info about the not yet written section data
	 * however due to crc the data all has to be written in order
	 * which means walking the structures twice once to precompute
	 * the data, once to write the data.
	 */
	ssym = vaddr;
	vaddr += roundup((sizeof(Elf_Ehdr) + sz), sizeof(Elf_Addr));
	off = roundup((sizeof(Elf_Ehdr) + sz), sizeof(Elf_Addr));
	for (i = 0; i < letoh16(elf.e_shnum); i++) {
		if (esym == 0 && elfoff2h(shp[i].sh_flags) & SHF_WRITE &&
		    elfoff2h(shp[i].sh_flags) & SHF_ALLOC)
			esym = elfoff2h(shp[i].sh_addr);

		if (letoh32(shp[i].sh_type) == SHT_SYMTAB ||
		    letoh32(shp[i].sh_type) == SHT_STRTAB) {
#ifdef DEBUG
		fprintf(stderr, "shdr %d %d/%d off %lx\n", i,
		    letoh32(shp[i].sh_type), roundup(elfoff2h(shp[i].sh_size),
		    sizeof(Elf_Addr)), off);
#endif
			/* data is at shp[i].sh_offset of len shp[i].sh_size */
			wshp[i].sh_offset = h2elfoff(off);
			off += roundup(elfoff2h(shp[i].sh_size),
			    sizeof(Elf_Addr));
			vaddr += roundup(elfoff2h(shp[i].sh_size),
			    sizeof(Elf_Addr));
		}
	}
	esymval = vaddr;
#ifdef DEBUG
	fprintf(stderr, "esymval %lx size %ld\n", esymval, esymval - ssym);
#endif

	for (i = 0; i < letoh16(ehdr.e_phnum); i++) {
#ifdef DEBUG
		fprintf(stderr, "phdr %d/%d\n", i, letoh16(ehdr.e_phnum));
#endif
		if (lseek(ifd, elfoff2h(ehdr.e_phoff) + i *
		    letoh16(ehdr.e_phentsize), SEEK_SET) == (off_t)-1)
			err(1, "%s", iname);
		if (read(ifd, &phdr, sizeof phdr) != sizeof(phdr))
			err(1, "%s", iname);

#ifdef DEBUG
		fprintf(stderr,
		    "vaddr %p type %#x offset %p filesz %p memsz %p\n",
		    elfoff2h(phdr.p_vaddr), letoh32(phdr.p_type),
	            elfoff2h(phdr.p_offset), elfoff2h(phdr.p_filesz),
                    elfoff2h(phdr.p_memsz));
#endif

		switch (letoh32(phdr.p_type)) {
		case PT_LOAD:
			break;
		case PT_NULL:
		case PT_NOTE:
		case PT_OPENBSD_RANDOMIZE:
#ifdef DEBUG
			fprintf(stderr, "skipping segment type %#x\n",
			    letoh32(phdr.p_type));
#endif
			continue;
		default:
			errx(1, "unexpected segment type %#x",
			    letoh32(phdr.p_type));
		}

		if (i == 0) 
			vaddr = elfoff2h(phdr.p_vaddr);
		else if (vaddr != elfoff2h(phdr.p_vaddr)) {
#ifdef DEBUG
			fprintf(stderr, "gap %p->%p\n", vaddr,
			    elfoff2h(phdr.p_vaddr));
#endif
			/* fill the gap between the previous phdr if any */
			crc = fill_zeroes(ofd, oname, crc, ih,
			    elfoff2h(phdr.p_vaddr) - vaddr);
			vaddr = elfoff2h(phdr.p_vaddr);
		}

		if (elfoff2h(phdr.p_filesz) != 0) {
#ifdef DEBUG
			fprintf(stderr, "copying %p from infile %p\n",
			   elfoff2h(phdr.p_filesz), elfoff2h(phdr.p_offset));
#endif
			/* esym will be in the data portion of a region */
			if (esym >= elfoff2h(phdr.p_vaddr) &&
			    esym < elfoff2h(phdr.p_vaddr) +
			    elfoff2h(phdr.p_filesz)) {
				/* load the region up to the esym
				 * (may be empty)
				 */
				Elf_Addr loadlen = esym -
				    elfoff2h(phdr.p_vaddr);

				if (lseek(ifd, elfoff2h(phdr.p_offset),
				    SEEK_SET) == (off_t)-1)
					err(1, "%s", iname);
				crc = copy_data(ifd, iname, ofd, oname, crc,
				    ih, loadlen);

				crc = copy_mem(&esymval, ofd, oname, crc, ih,
				    sizeof(esymval));

				if (lseek(ifd, elfoff2h(phdr.p_offset) +
				    loadlen + sizeof(esymval), SEEK_SET) ==
				    (off_t)-1)
					err(1, "%s", iname);
				crc = copy_data(ifd, iname, ofd, oname, crc,
				    ih, elfoff2h(phdr.p_filesz) - loadlen -
				    sizeof(esymval));
			} else {

				if (lseek(ifd, elfoff2h(phdr.p_offset),
				    SEEK_SET) == (off_t)-1)
					err(1, "%s", iname);
				crc = copy_data(ifd, iname, ofd, oname, crc,
				    ih, elfoff2h(phdr.p_filesz));
			}
			if (elfoff2h(phdr.p_memsz) - elfoff2h(phdr.p_filesz)
			    != 0) {
#ifdef DEBUG
				fprintf(stderr, "zeroing %p\n",
				    elfoff2h(phdr.p_memsz) -
				    elfoff2h(phdr.p_filesz));
#endif
				crc = fill_zeroes(ofd, oname, crc, ih,
				    elfoff2h(phdr.p_memsz) -
				    elfoff2h(phdr.p_filesz));
			}
			ovaddr = vaddr + elfoff2h(phdr.p_memsz);
		} else {
			ovaddr = vaddr;
		}
		/*
		 * If p_filesz == 0, this is likely .bss, which we do not
		 * need to provide. If it's not the last phdr, the gap
		 * filling code will output the necessary zeroes anyway.
		 */
		vaddr += elfoff2h(phdr.p_memsz);
	}

	vaddr = roundup(vaddr, sizeof(Elf_Addr));
	if (vaddr != ovaddr) {
#ifdef DEBUG
		fprintf(stderr, "gap %p->%p\n", vaddr, elfoff2h(phdr.p_vaddr));
#endif
		/* fill the gap between the previous phdr if not aligned */
		crc = fill_zeroes(ofd, oname, crc, ih, vaddr - ovaddr);
	}

	for (havesyms = i = 0; i < letoh16(elf.e_shnum); i++)
		if (letoh32(shp[i].sh_type) == SHT_SYMTAB)
			havesyms = 1;

	if (havesyms == 0)
		return crc;

	elf.e_phoff = 0;
	elf.e_shoff = h2elfoff(sizeof(Elf_Ehdr));
	elf.e_phentsize = 0;
	elf.e_phnum = 0;
	crc = copy_mem(&elf, ofd, oname, crc, ih, sizeof(elf));
	crc = copy_mem(wshp, ofd, oname, crc, ih, sz);
	off = sizeof(elf) + sz;
	vaddr += sizeof(elf) + sz;

	off = roundup((sizeof(Elf_Ehdr) + sz), sizeof(Elf_Addr));
	for (i = 0; i < letoh16(elf.e_shnum); i++) {
		if (letoh32(shp[i].sh_type) == SHT_SYMTAB ||
		    letoh32(shp[i].sh_type) == SHT_STRTAB) {
			Elf_Addr align;
			/* data is at shp[i].sh_offset of len shp[i].sh_size */
			if (lseek(ifd, elfoff2h(shp[i].sh_offset), SEEK_SET)
			    == -1)
				err(1, "%s", iname);

			off += elfoff2h(shp[i].sh_size);
			vaddr += elfoff2h(shp[i].sh_size);
			crc = copy_data(ifd, iname, ofd, oname, crc, ih,
			    elfoff2h(shp[i].sh_size));
			align = roundup(elfoff2h(shp[i].sh_size),
			    sizeof(Elf_Addr)) - elfoff2h(shp[i].sh_size);
			if (align != 0) {
				vaddr += align;
				crc = fill_zeroes(ofd, oname, crc, ih, align);
			}
		}
	}

	if (vaddr != esymval)
		warnx("esymval and vaddr mismatch %llx %llx\n",
		    (long long)esymval, (long long)vaddr);

	return crc;
}
