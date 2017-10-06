/*
 * Based on:
 *
 * Minimal BPF JIT image disassembler
 *
 * Disassembles BPF JIT compiler emitted opcodes back to asm insn's for
 * debugging or verification purposes.
 *
 * Copyright 2013 Daniel Borkmann <daniel@iogearbox.net>
 * Licensed under the GNU General Public License, version 2.0 (GPLv2)
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <bfd.h>
#include <dis-asm.h>
#include <sys/types.h>
#include <sys/stat.h>

static void get_exec_path(char *tpath, size_t size)
{
	ssize_t len;
	char *path;

	snprintf(tpath, size, "/proc/%d/exe", (int) getpid());
	tpath[size - 1] = 0;

	path = strdup(tpath);
	assert(path);

	len = readlink(path, tpath, size - 1);
	assert(len > 0);
	tpath[len] = 0;

	free(path);
}

void disasm_print_insn(unsigned char *image, ssize_t len, int opcodes)
{
	disassembler_ftype disassemble;
	struct disassemble_info info;
	int count, i, pc = 0;
	char tpath[256];
	bfd *bfdf;

	if (!len)
		return;

	memset(tpath, 0, sizeof(tpath));
	get_exec_path(tpath, sizeof(tpath));

	bfdf = bfd_openr(tpath, NULL);
	assert(bfdf);
	assert(bfd_check_format(bfdf, bfd_object));

	init_disassemble_info(&info, stdout, (fprintf_ftype) fprintf);
	info.arch = bfd_get_arch(bfdf);
	info.mach = bfd_get_mach(bfdf);
	info.buffer = image;
	info.buffer_length = len;

	disassemble_init_for_target(&info);

	disassemble = disassembler(bfdf);
	assert(disassemble);

	do {
		printf("%4x:\t", pc);

		count = disassemble(pc, &info);

		if (opcodes) {
			printf("\n\t");
			for (i = 0; i < count; ++i)
				printf("%02x ", (uint8_t) image[pc + i]);
		}
		printf("\n");

		pc += count;
	} while (count > 0 && pc < len);

	bfd_close(bfdf);
}
