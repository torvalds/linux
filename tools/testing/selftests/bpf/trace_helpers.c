// SPDX-License-Identifier: GPL-2.0
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <linux/perf_event.h>
#include <sys/mman.h>
#include "trace_helpers.h"
#include <linux/limits.h>
#include <libelf.h>
#include <gelf.h>

#define TRACEFS_PIPE	"/sys/kernel/tracing/trace_pipe"
#define DEBUGFS_PIPE	"/sys/kernel/debug/tracing/trace_pipe"

#define MAX_SYMS 400000
static struct ksym syms[MAX_SYMS];
static int sym_cnt;

static int ksym_cmp(const void *p1, const void *p2)
{
	return ((struct ksym *)p1)->addr - ((struct ksym *)p2)->addr;
}

int load_kallsyms_refresh(void)
{
	FILE *f;
	char func[256], buf[256];
	char symbol;
	void *addr;
	int i = 0;

	sym_cnt = 0;

	f = fopen("/proc/kallsyms", "r");
	if (!f)
		return -ENOENT;

	while (fgets(buf, sizeof(buf), f)) {
		if (sscanf(buf, "%p %c %s", &addr, &symbol, func) != 3)
			break;
		if (!addr)
			continue;
		if (i >= MAX_SYMS)
			return -EFBIG;

		syms[i].addr = (long) addr;
		syms[i].name = strdup(func);
		i++;
	}
	fclose(f);
	sym_cnt = i;
	qsort(syms, sym_cnt, sizeof(struct ksym), ksym_cmp);
	return 0;
}

int load_kallsyms(void)
{
	/*
	 * This is called/used from multiplace places,
	 * load symbols just once.
	 */
	if (sym_cnt)
		return 0;
	return load_kallsyms_refresh();
}

struct ksym *ksym_search(long key)
{
	int start = 0, end = sym_cnt;
	int result;

	/* kallsyms not loaded. return NULL */
	if (sym_cnt <= 0)
		return NULL;

	while (start < end) {
		size_t mid = start + (end - start) / 2;

		result = key - syms[mid].addr;
		if (result < 0)
			end = mid;
		else if (result > 0)
			start = mid + 1;
		else
			return &syms[mid];
	}

	if (start >= 1 && syms[start - 1].addr < key &&
	    key < syms[start].addr)
		/* valid ksym */
		return &syms[start - 1];

	/* out of range. return _stext */
	return &syms[0];
}

long ksym_get_addr(const char *name)
{
	int i;

	for (i = 0; i < sym_cnt; i++) {
		if (strcmp(syms[i].name, name) == 0)
			return syms[i].addr;
	}

	return 0;
}

/* open kallsyms and read symbol addresses on the fly. Without caching all symbols,
 * this is faster than load + find.
 */
int kallsyms_find(const char *sym, unsigned long long *addr)
{
	char type, name[500];
	unsigned long long value;
	int err = 0;
	FILE *f;

	f = fopen("/proc/kallsyms", "r");
	if (!f)
		return -EINVAL;

	while (fscanf(f, "%llx %c %499s%*[^\n]\n", &value, &type, name) > 0) {
		if (strcmp(name, sym) == 0) {
			*addr = value;
			goto out;
		}
	}
	err = -ENOENT;

out:
	fclose(f);
	return err;
}

void read_trace_pipe(void)
{
	int trace_fd;

	if (access(TRACEFS_PIPE, F_OK) == 0)
		trace_fd = open(TRACEFS_PIPE, O_RDONLY, 0);
	else
		trace_fd = open(DEBUGFS_PIPE, O_RDONLY, 0);
	if (trace_fd < 0)
		return;

	while (1) {
		static char buf[4096];
		ssize_t sz;

		sz = read(trace_fd, buf, sizeof(buf) - 1);
		if (sz > 0) {
			buf[sz] = 0;
			puts(buf);
		}
	}
}

ssize_t get_uprobe_offset(const void *addr)
{
	size_t start, end, base;
	char buf[256];
	bool found = false;
	FILE *f;

	f = fopen("/proc/self/maps", "r");
	if (!f)
		return -errno;

	while (fscanf(f, "%zx-%zx %s %zx %*[^\n]\n", &start, &end, buf, &base) == 4) {
		if (buf[2] == 'x' && (uintptr_t)addr >= start && (uintptr_t)addr < end) {
			found = true;
			break;
		}
	}

	fclose(f);

	if (!found)
		return -ESRCH;

#if defined(__powerpc64__) && defined(_CALL_ELF) && _CALL_ELF == 2

#define OP_RT_RA_MASK   0xffff0000UL
#define LIS_R2          0x3c400000UL
#define ADDIS_R2_R12    0x3c4c0000UL
#define ADDI_R2_R2      0x38420000UL

	/*
	 * A PPC64 ABIv2 function may have a local and a global entry
	 * point. We need to use the local entry point when patching
	 * functions, so identify and step over the global entry point
	 * sequence.
	 *
	 * The global entry point sequence is always of the form:
	 *
	 * addis r2,r12,XXXX
	 * addi  r2,r2,XXXX
	 *
	 * A linker optimisation may convert the addis to lis:
	 *
	 * lis   r2,XXXX
	 * addi  r2,r2,XXXX
	 */
	{
		const u32 *insn = (const u32 *)(uintptr_t)addr;

		if ((((*insn & OP_RT_RA_MASK) == ADDIS_R2_R12) ||
		     ((*insn & OP_RT_RA_MASK) == LIS_R2)) &&
		    ((*(insn + 1) & OP_RT_RA_MASK) == ADDI_R2_R2))
			return (uintptr_t)(insn + 2) - start + base;
	}
#endif
	return (uintptr_t)addr - start + base;
}

ssize_t get_rel_offset(uintptr_t addr)
{
	size_t start, end, offset;
	char buf[256];
	FILE *f;

	f = fopen("/proc/self/maps", "r");
	if (!f)
		return -errno;

	while (fscanf(f, "%zx-%zx %s %zx %*[^\n]\n", &start, &end, buf, &offset) == 4) {
		if (addr >= start && addr < end) {
			fclose(f);
			return (size_t)addr - start + offset;
		}
	}

	fclose(f);
	return -EINVAL;
}

static int
parse_build_id_buf(const void *note_start, Elf32_Word note_size, char *build_id)
{
	Elf32_Word note_offs = 0;

	while (note_offs + sizeof(Elf32_Nhdr) < note_size) {
		Elf32_Nhdr *nhdr = (Elf32_Nhdr *)(note_start + note_offs);

		if (nhdr->n_type == 3 && nhdr->n_namesz == sizeof("GNU") &&
		    !strcmp((char *)(nhdr + 1), "GNU") && nhdr->n_descsz > 0 &&
		    nhdr->n_descsz <= BPF_BUILD_ID_SIZE) {
			memcpy(build_id, note_start + note_offs +
			       ALIGN(sizeof("GNU"), 4) + sizeof(Elf32_Nhdr), nhdr->n_descsz);
			memset(build_id + nhdr->n_descsz, 0, BPF_BUILD_ID_SIZE - nhdr->n_descsz);
			return (int) nhdr->n_descsz;
		}

		note_offs = note_offs + sizeof(Elf32_Nhdr) +
			   ALIGN(nhdr->n_namesz, 4) + ALIGN(nhdr->n_descsz, 4);
	}

	return -ENOENT;
}

/* Reads binary from *path* file and returns it in the *build_id* buffer
 * with *size* which is expected to be at least BPF_BUILD_ID_SIZE bytes.
 * Returns size of build id on success. On error the error value is
 * returned.
 */
int read_build_id(const char *path, char *build_id, size_t size)
{
	int fd, err = -EINVAL;
	Elf *elf = NULL;
	GElf_Ehdr ehdr;
	size_t max, i;

	if (size < BPF_BUILD_ID_SIZE)
		return -EINVAL;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;

	(void)elf_version(EV_CURRENT);

	elf = elf_begin(fd, ELF_C_READ_MMAP, NULL);
	if (!elf)
		goto out;
	if (elf_kind(elf) != ELF_K_ELF)
		goto out;
	if (!gelf_getehdr(elf, &ehdr))
		goto out;

	for (i = 0; i < ehdr.e_phnum; i++) {
		GElf_Phdr mem, *phdr;
		char *data;

		phdr = gelf_getphdr(elf, i, &mem);
		if (!phdr)
			goto out;
		if (phdr->p_type != PT_NOTE)
			continue;
		data = elf_rawfile(elf, &max);
		if (!data)
			goto out;
		if (phdr->p_offset + phdr->p_memsz > max)
			goto out;
		err = parse_build_id_buf(data + phdr->p_offset, phdr->p_memsz, build_id);
		if (err > 0)
			break;
	}

out:
	if (elf)
		elf_end(elf);
	close(fd);
	return err;
}
