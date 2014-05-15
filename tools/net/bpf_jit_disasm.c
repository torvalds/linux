/*
 * Minimal BPF JIT image disassembler
 *
 * Disassembles BPF JIT compiler emitted opcodes back to asm insn's for
 * debugging or verification purposes.
 *
 * To get the disassembly of the JIT code, do the following:
 *
 *  1) `echo 2 > /proc/sys/net/core/bpf_jit_enable`
 *  2) Load a BPF filter (e.g. `tcpdump -p -n -s 0 -i eth1 host 192.168.20.0/24`)
 *  3) Run e.g. `bpf_jit_disasm -o` to read out the last JIT code
 *
 * Copyright 2013 Daniel Borkmann <borkmann@redhat.com>
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
#include <sys/klog.h>
#include <sys/types.h>
#include <regex.h>

static void get_exec_path(char *tpath, size_t size)
{
	char *path;
	ssize_t len;

	snprintf(tpath, size, "/proc/%d/exe", (int) getpid());
	tpath[size - 1] = 0;

	path = strdup(tpath);
	assert(path);

	len = readlink(path, tpath, size);
	tpath[len] = 0;

	free(path);
}

static void get_asm_insns(uint8_t *image, size_t len, int opcodes)
{
	int count, i, pc = 0;
	char tpath[256];
	struct disassemble_info info;
	disassembler_ftype disassemble;
	bfd *bfdf;

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
	} while(count > 0 && pc < len);

	bfd_close(bfdf);
}

static char *get_klog_buff(int *klen)
{
	int ret, len = klogctl(10, NULL, 0);
	char *buff = malloc(len);

	assert(buff && klen);
	ret = klogctl(3, buff, len);
	assert(ret >= 0);
	*klen = ret;

	return buff;
}

static void put_klog_buff(char *buff)
{
	free(buff);
}

static int get_last_jit_image(char *haystack, size_t hlen,
			      uint8_t *image, size_t ilen)
{
	char *ptr, *pptr, *tmp;
	off_t off = 0;
	int ret, flen, proglen, pass, ulen = 0;
	regmatch_t pmatch[1];
	unsigned long base;
	regex_t regex;

	if (hlen == 0)
		return 0;

	ret = regcomp(&regex, "flen=[[:alnum:]]+ proglen=[[:digit:]]+ "
		      "pass=[[:digit:]]+ image=[[:xdigit:]]+", REG_EXTENDED);
	assert(ret == 0);

	ptr = haystack;
	while (1) {
		ret = regexec(&regex, ptr, 1, pmatch, 0);
		if (ret == 0) {
			ptr += pmatch[0].rm_eo;
			off += pmatch[0].rm_eo;
			assert(off < hlen);
		} else
			break;
	}

	ptr = haystack + off - (pmatch[0].rm_eo - pmatch[0].rm_so);
	ret = sscanf(ptr, "flen=%d proglen=%d pass=%d image=%lx",
		     &flen, &proglen, &pass, &base);
	if (ret != 4)
		return 0;

	tmp = ptr = haystack + off;
	while ((ptr = strtok(tmp, "\n")) != NULL && ulen < ilen) {
		tmp = NULL;
		if (!strstr(ptr, "JIT code"))
			continue;
		pptr = ptr;
		while ((ptr = strstr(pptr, ":")))
			pptr = ptr + 1;
		ptr = pptr;
		do {
			image[ulen++] = (uint8_t) strtoul(pptr, &pptr, 16);
			if (ptr == pptr || ulen >= ilen) {
				ulen--;
				break;
			}
			ptr = pptr;
		} while (1);
	}

	assert(ulen == proglen);
	printf("%d bytes emitted from JIT compiler (pass:%d, flen:%d)\n",
	       proglen, pass, flen);
	printf("%lx + <x>:\n", base);

	regfree(&regex);
	return ulen;
}

int main(int argc, char **argv)
{
	int len, klen, opcodes = 0;
	char *kbuff;
	static uint8_t image[32768];

	if (argc > 1) {
		if (!strncmp("-o", argv[argc - 1], 2)) {
			opcodes = 1;
		} else {
			printf("usage: bpf_jit_disasm [-o: show opcodes]\n");
			exit(0);
		}
	}

	bfd_init();
	memset(image, 0, sizeof(image));

	kbuff = get_klog_buff(&klen);

	len = get_last_jit_image(kbuff, klen, image, sizeof(image));
	if (len > 0)
		get_asm_insns(image, len, opcodes);

	put_klog_buff(kbuff);

	return 0;
}
