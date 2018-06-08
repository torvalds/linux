/*
 * tools/testing/selftests/kvm/lib/elf.c
 *
 * Copyright (C) 2018, Google LLC.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 */

#include "test_util.h"

#include <bits/endian.h>
#include <linux/elf.h>

#include "kvm_util.h"
#include "kvm_util_internal.h"

static void elfhdr_get(const char *filename, Elf64_Ehdr *hdrp)
{
	off_t offset_rv;

	/* Open the ELF file. */
	int fd;
	fd = open(filename, O_RDONLY);
	TEST_ASSERT(fd >= 0, "Failed to open ELF file,\n"
		"  filename: %s\n"
		"  rv: %i errno: %i", filename, fd, errno);

	/* Read in and validate ELF Identification Record.
	 * The ELF Identification record is the first 16 (EI_NIDENT) bytes
	 * of the ELF header, which is at the beginning of the ELF file.
	 * For now it is only safe to read the first EI_NIDENT bytes.  Once
	 * read and validated, the value of e_ehsize can be used to determine
	 * the real size of the ELF header.
	 */
	unsigned char ident[EI_NIDENT];
	test_read(fd, ident, sizeof(ident));
	TEST_ASSERT((ident[EI_MAG0] == ELFMAG0) && (ident[EI_MAG1] == ELFMAG1)
		&& (ident[EI_MAG2] == ELFMAG2) && (ident[EI_MAG3] == ELFMAG3),
		"ELF MAGIC Mismatch,\n"
		"  filename: %s\n"
		"  ident[EI_MAG0 - EI_MAG3]: %02x %02x %02x %02x\n"
		"  Expected: %02x %02x %02x %02x",
		filename,
		ident[EI_MAG0], ident[EI_MAG1], ident[EI_MAG2], ident[EI_MAG3],
		ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3);
	TEST_ASSERT(ident[EI_CLASS] == ELFCLASS64,
		"Current implementation only able to handle ELFCLASS64,\n"
		"  filename: %s\n"
		"  ident[EI_CLASS]: %02x\n"
		"  expected: %02x",
		filename,
		ident[EI_CLASS], ELFCLASS64);
	TEST_ASSERT(((BYTE_ORDER == LITTLE_ENDIAN)
			&& (ident[EI_DATA] == ELFDATA2LSB))
		|| ((BYTE_ORDER == BIG_ENDIAN)
			&& (ident[EI_DATA] == ELFDATA2MSB)), "Current "
		"implementation only able to handle\n"
		"cases where the host and ELF file endianness\n"
		"is the same:\n"
		"  host BYTE_ORDER: %u\n"
		"  host LITTLE_ENDIAN: %u\n"
		"  host BIG_ENDIAN: %u\n"
		"  ident[EI_DATA]: %u\n"
		"  ELFDATA2LSB: %u\n"
		"  ELFDATA2MSB: %u",
		BYTE_ORDER, LITTLE_ENDIAN, BIG_ENDIAN,
		ident[EI_DATA], ELFDATA2LSB, ELFDATA2MSB);
	TEST_ASSERT(ident[EI_VERSION] == EV_CURRENT,
		"Current implementation only able to handle current "
		"ELF version,\n"
		"  filename: %s\n"
		"  ident[EI_VERSION]: %02x\n"
		"  expected: %02x",
		filename, ident[EI_VERSION], EV_CURRENT);

	/* Read in the ELF header.
	 * With the ELF Identification portion of the ELF header
	 * validated, especially that the value at EI_VERSION is
	 * as expected, it is now safe to read the entire ELF header.
	 */
	offset_rv = lseek(fd, 0, SEEK_SET);
	TEST_ASSERT(offset_rv == 0, "Seek to ELF header failed,\n"
		"  rv: %zi expected: %i", offset_rv, 0);
	test_read(fd, hdrp, sizeof(*hdrp));
	TEST_ASSERT(hdrp->e_phentsize == sizeof(Elf64_Phdr),
		"Unexpected physical header size,\n"
		"  hdrp->e_phentsize: %x\n"
		"  expected: %zx",
		hdrp->e_phentsize, sizeof(Elf64_Phdr));
	TEST_ASSERT(hdrp->e_shentsize == sizeof(Elf64_Shdr),
		"Unexpected section header size,\n"
		"  hdrp->e_shentsize: %x\n"
		"  expected: %zx",
		hdrp->e_shentsize, sizeof(Elf64_Shdr));
}

/* VM ELF Load
 *
 * Input Args:
 *   filename - Path to ELF file
 *
 * Output Args: None
 *
 * Input/Output Args:
 *   vm - Pointer to opaque type that describes the VM.
 *
 * Return: None, TEST_ASSERT failures for all error conditions
 *
 * Loads the program image of the ELF file specified by filename,
 * into the virtual address space of the VM pointed to by vm.  On entry
 * the VM needs to not be using any of the virtual address space used
 * by the image and it needs to have sufficient available physical pages, to
 * back the virtual pages used to load the image.
 */
void kvm_vm_elf_load(struct kvm_vm *vm, const char *filename,
	uint32_t data_memslot, uint32_t pgd_memslot)
{
	off_t offset, offset_rv;
	Elf64_Ehdr hdr;

	/* Open the ELF file. */
	int fd;
	fd = open(filename, O_RDONLY);
	TEST_ASSERT(fd >= 0, "Failed to open ELF file,\n"
		"  filename: %s\n"
		"  rv: %i errno: %i", filename, fd, errno);

	/* Read in the ELF header. */
	elfhdr_get(filename, &hdr);

	/* For each program header.
	 * The following ELF header members specify the location
	 * and size of the program headers:
	 *
	 *   e_phoff - File offset to start of program headers
	 *   e_phentsize - Size of each program header
	 *   e_phnum - Number of program header entries
	 */
	for (unsigned int n1 = 0; n1 < hdr.e_phnum; n1++) {
		/* Seek to the beginning of the program header. */
		offset = hdr.e_phoff + (n1 * hdr.e_phentsize);
		offset_rv = lseek(fd, offset, SEEK_SET);
		TEST_ASSERT(offset_rv == offset,
			"Failed to seek to begining of program header %u,\n"
			"  filename: %s\n"
			"  rv: %jd errno: %i",
			n1, filename, (intmax_t) offset_rv, errno);

		/* Read in the program header. */
		Elf64_Phdr phdr;
		test_read(fd, &phdr, sizeof(phdr));

		/* Skip if this header doesn't describe a loadable segment. */
		if (phdr.p_type != PT_LOAD)
			continue;

		/* Allocate memory for this segment within the VM. */
		TEST_ASSERT(phdr.p_memsz > 0, "Unexpected loadable segment "
			"memsize of 0,\n"
			"  phdr index: %u p_memsz: 0x%" PRIx64,
			n1, (uint64_t) phdr.p_memsz);
		vm_vaddr_t seg_vstart = phdr.p_vaddr;
		seg_vstart &= ~(vm_vaddr_t)(vm->page_size - 1);
		vm_vaddr_t seg_vend = phdr.p_vaddr + phdr.p_memsz - 1;
		seg_vend |= vm->page_size - 1;
		size_t seg_size = seg_vend - seg_vstart + 1;

		vm_vaddr_t vaddr = vm_vaddr_alloc(vm, seg_size, seg_vstart,
			data_memslot, pgd_memslot);
		TEST_ASSERT(vaddr == seg_vstart, "Unable to allocate "
			"virtual memory for segment at requested min addr,\n"
			"  segment idx: %u\n"
			"  seg_vstart: 0x%lx\n"
			"  vaddr: 0x%lx",
			n1, seg_vstart, vaddr);
		memset(addr_gva2hva(vm, vaddr), 0, seg_size);
		/* TODO(lhuemill): Set permissions of each memory segment
		 * based on the least-significant 3 bits of phdr.p_flags.
		 */

		/* Load portion of initial state that is contained within
		 * the ELF file.
		 */
		if (phdr.p_filesz) {
			offset_rv = lseek(fd, phdr.p_offset, SEEK_SET);
			TEST_ASSERT(offset_rv == phdr.p_offset,
				"Seek to program segment offset failed,\n"
				"  program header idx: %u errno: %i\n"
				"  offset_rv: 0x%jx\n"
				"  expected: 0x%jx\n",
				n1, errno, (intmax_t) offset_rv,
				(intmax_t) phdr.p_offset);
			test_read(fd, addr_gva2hva(vm, phdr.p_vaddr),
				phdr.p_filesz);
		}
	}
}
