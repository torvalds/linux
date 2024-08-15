// SPDX-License-Identifier: GPL-2.0-only
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
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <bfd.h>
#include <dis-asm.h>
#include <regex.h>
#include <fcntl.h>
#include <sys/klog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <tools/dis-asm-compat.h>

#define CMD_ACTION_SIZE_BUFFER		10
#define CMD_ACTION_READ_ALL		3

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
	char tpath[PATH_MAX];
	struct disassemble_info info;
	disassembler_ftype disassemble;
	bfd *bfdf;

	memset(tpath, 0, sizeof(tpath));
	get_exec_path(tpath, sizeof(tpath));

	bfdf = bfd_openr(tpath, NULL);
	assert(bfdf);
	assert(bfd_check_format(bfdf, bfd_object));

	init_disassemble_info_compat(&info, stdout,
				     (fprintf_ftype) fprintf,
				     fprintf_styled);
	info.arch = bfd_get_arch(bfdf);
	info.mach = bfd_get_mach(bfdf);
	info.buffer = image;
	info.buffer_length = len;

	disassemble_init_for_target(&info);

#ifdef DISASM_FOUR_ARGS_SIGNATURE
	disassemble = disassembler(info.arch,
				   bfd_big_endian(bfdf),
				   info.mach,
				   bfdf);
#else
	disassemble = disassembler(bfdf);
#endif
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

static char *get_klog_buff(unsigned int *klen)
{
	int ret, len;
	char *buff;

	len = klogctl(CMD_ACTION_SIZE_BUFFER, NULL, 0);
	if (len < 0)
		return NULL;

	buff = malloc(len);
	if (!buff)
		return NULL;

	ret = klogctl(CMD_ACTION_READ_ALL, buff, len);
	if (ret < 0) {
		free(buff);
		return NULL;
	}

	*klen = ret;
	return buff;
}

static char *get_flog_buff(const char *file, unsigned int *klen)
{
	int fd, ret, len;
	struct stat fi;
	char *buff;

	fd = open(file, O_RDONLY);
	if (fd < 0)
		return NULL;

	ret = fstat(fd, &fi);
	if (ret < 0 || !S_ISREG(fi.st_mode))
		goto out;

	len = fi.st_size + 1;
	buff = malloc(len);
	if (!buff)
		goto out;

	memset(buff, 0, len);
	ret = read(fd, buff, len - 1);
	if (ret <= 0)
		goto out_free;

	close(fd);
	*klen = ret;
	return buff;
out_free:
	free(buff);
out:
	close(fd);
	return NULL;
}

static char *get_log_buff(const char *file, unsigned int *klen)
{
	return file ? get_flog_buff(file, klen) : get_klog_buff(klen);
}

static void put_log_buff(char *buff)
{
	free(buff);
}

static uint8_t *get_last_jit_image(char *haystack, size_t hlen,
				   unsigned int *ilen)
{
	char *ptr, *pptr, *tmp;
	off_t off = 0;
	unsigned int proglen;
	int ret, flen, pass, ulen = 0;
	regmatch_t pmatch[1];
	unsigned long base;
	regex_t regex;
	uint8_t *image;

	if (hlen == 0)
		return NULL;

	ret = regcomp(&regex, "flen=[[:alnum:]]+ proglen=[[:digit:]]+ "
		      "pass=[[:digit:]]+ image=[[:xdigit:]]+", REG_EXTENDED);
	assert(ret == 0);

	ptr = haystack;
	memset(pmatch, 0, sizeof(pmatch));

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
	ret = sscanf(ptr, "flen=%d proglen=%u pass=%d image=%lx",
		     &flen, &proglen, &pass, &base);
	if (ret != 4) {
		regfree(&regex);
		return NULL;
	}
	if (proglen > 1000000) {
		printf("proglen of %d too big, stopping\n", proglen);
		return NULL;
	}

	image = malloc(proglen);
	if (!image) {
		printf("Out of memory\n");
		return NULL;
	}
	memset(image, 0, proglen);

	tmp = ptr = haystack + off;
	while ((ptr = strtok(tmp, "\n")) != NULL && ulen < proglen) {
		tmp = NULL;
		if (!strstr(ptr, "JIT code"))
			continue;
		pptr = ptr;
		while ((ptr = strstr(pptr, ":")))
			pptr = ptr + 1;
		ptr = pptr;
		do {
			image[ulen++] = (uint8_t) strtoul(pptr, &pptr, 16);
			if (ptr == pptr) {
				ulen--;
				break;
			}
			if (ulen >= proglen)
				break;
			ptr = pptr;
		} while (1);
	}

	assert(ulen == proglen);
	printf("%u bytes emitted from JIT compiler (pass:%d, flen:%d)\n",
	       proglen, pass, flen);
	printf("%lx + <x>:\n", base);

	regfree(&regex);
	*ilen = ulen;
	return image;
}

static void usage(void)
{
	printf("Usage: bpf_jit_disasm [...]\n");
	printf("       -o          Also display related opcodes (default: off).\n");
	printf("       -O <file>   Write binary image of code to file, don't disassemble to stdout.\n");
	printf("       -f <file>   Read last image dump from file or stdin (default: klog).\n");
	printf("       -h          Display this help.\n");
}

int main(int argc, char **argv)
{
	unsigned int len, klen, opt, opcodes = 0;
	char *kbuff, *file = NULL;
	char *ofile = NULL;
	int ofd;
	ssize_t nr;
	uint8_t *pos;
	uint8_t *image = NULL;

	while ((opt = getopt(argc, argv, "of:O:")) != -1) {
		switch (opt) {
		case 'o':
			opcodes = 1;
			break;
		case 'O':
			ofile = optarg;
			break;
		case 'f':
			file = optarg;
			break;
		default:
			usage();
			return -1;
		}
	}

	bfd_init();

	kbuff = get_log_buff(file, &klen);
	if (!kbuff) {
		fprintf(stderr, "Could not retrieve log buffer!\n");
		return -1;
	}

	image = get_last_jit_image(kbuff, klen, &len);
	if (!image) {
		fprintf(stderr, "No JIT image found!\n");
		goto done;
	}
	if (!ofile) {
		get_asm_insns(image, len, opcodes);
		goto done;
	}

	ofd = open(ofile, O_WRONLY | O_CREAT | O_TRUNC, DEFFILEMODE);
	if (ofd < 0) {
		fprintf(stderr, "Could not open file %s for writing: ", ofile);
		perror(NULL);
		goto done;
	}
	pos = image;
	do {
		nr = write(ofd, pos, len);
		if (nr < 0) {
			fprintf(stderr, "Could not write data to %s: ", ofile);
			perror(NULL);
			goto done;
		}
		len -= nr;
		pos += nr;
	} while (len);
	close(ofd);

done:
	put_log_buff(kbuff);
	free(image);
	return 0;
}
