// SPDX-License-Identifier: GPL-2.0
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>
#include <linux/perf_event.h>
#include <sys/mman.h>
#include "trace_helpers.h"
#include <linux/limits.h>
#include <libelf.h>
#include <gelf.h>
#include "bpf/libbpf_internal.h"

#define TRACEFS_PIPE	"/sys/kernel/tracing/trace_pipe"
#define DEBUGFS_PIPE	"/sys/kernel/debug/tracing/trace_pipe"

struct ksyms {
	struct ksym *syms;
	size_t sym_cap;
	size_t sym_cnt;
};

static struct ksyms *ksyms;
static pthread_mutex_t ksyms_mutex = PTHREAD_MUTEX_INITIALIZER;

static int ksyms__add_symbol(struct ksyms *ksyms, const char *name,
			     unsigned long addr)
{
	void *tmp;

	tmp = strdup(name);
	if (!tmp)
		return -ENOMEM;
	ksyms->syms[ksyms->sym_cnt].addr = addr;
	ksyms->syms[ksyms->sym_cnt].name = tmp;
	ksyms->sym_cnt++;
	return 0;
}

void free_kallsyms_local(struct ksyms *ksyms)
{
	unsigned int i;

	if (!ksyms)
		return;

	if (!ksyms->syms) {
		free(ksyms);
		return;
	}

	for (i = 0; i < ksyms->sym_cnt; i++)
		free(ksyms->syms[i].name);
	free(ksyms->syms);
	free(ksyms);
}

static struct ksyms *load_kallsyms_local_common(ksym_cmp_t cmp_cb)
{
	FILE *f;
	char func[256], buf[256];
	char symbol;
	void *addr;
	int ret;
	struct ksyms *ksyms;

	f = fopen("/proc/kallsyms", "r");
	if (!f)
		return NULL;

	ksyms = calloc(1, sizeof(struct ksyms));
	if (!ksyms) {
		fclose(f);
		return NULL;
	}

	while (fgets(buf, sizeof(buf), f)) {
		if (sscanf(buf, "%p %c %s", &addr, &symbol, func) != 3)
			break;
		if (!addr)
			continue;

		ret = libbpf_ensure_mem((void **) &ksyms->syms, &ksyms->sym_cap,
					sizeof(struct ksym), ksyms->sym_cnt + 1);
		if (ret)
			goto error;
		ret = ksyms__add_symbol(ksyms, func, (unsigned long)addr);
		if (ret)
			goto error;
	}
	fclose(f);
	qsort(ksyms->syms, ksyms->sym_cnt, sizeof(struct ksym), cmp_cb);
	return ksyms;

error:
	fclose(f);
	free_kallsyms_local(ksyms);
	return NULL;
}

static int ksym_cmp(const void *p1, const void *p2)
{
	return ((struct ksym *)p1)->addr - ((struct ksym *)p2)->addr;
}

struct ksyms *load_kallsyms_local(void)
{
	return load_kallsyms_local_common(ksym_cmp);
}

struct ksyms *load_kallsyms_custom_local(ksym_cmp_t cmp_cb)
{
	return load_kallsyms_local_common(cmp_cb);
}

int load_kallsyms(void)
{
	pthread_mutex_lock(&ksyms_mutex);
	if (!ksyms)
		ksyms = load_kallsyms_local();
	pthread_mutex_unlock(&ksyms_mutex);
	return ksyms ? 0 : 1;
}

struct ksym *ksym_search_local(struct ksyms *ksyms, long key)
{
	int start = 0, end = ksyms->sym_cnt;
	int result;

	/* kallsyms not loaded. return NULL */
	if (ksyms->sym_cnt <= 0)
		return NULL;

	while (start < end) {
		size_t mid = start + (end - start) / 2;

		result = key - ksyms->syms[mid].addr;
		if (result < 0)
			end = mid;
		else if (result > 0)
			start = mid + 1;
		else
			return &ksyms->syms[mid];
	}

	if (start >= 1 && ksyms->syms[start - 1].addr < key &&
	    key < ksyms->syms[start].addr)
		/* valid ksym */
		return &ksyms->syms[start - 1];

	/* out of range. return _stext */
	return &ksyms->syms[0];
}

struct ksym *search_kallsyms_custom_local(struct ksyms *ksyms, const void *p,
					  ksym_search_cmp_t cmp_cb)
{
	int start = 0, mid, end = ksyms->sym_cnt;
	struct ksym *ks;
	int result;

	while (start < end) {
		mid = start + (end - start) / 2;
		ks = &ksyms->syms[mid];
		result = cmp_cb(p, ks);
		if (result < 0)
			end = mid;
		else if (result > 0)
			start = mid + 1;
		else
			return ks;
	}

	return NULL;
}

struct ksym *ksym_search(long key)
{
	if (!ksyms)
		return NULL;
	return ksym_search_local(ksyms, key);
}

long ksym_get_addr_local(struct ksyms *ksyms, const char *name)
{
	int i;

	for (i = 0; i < ksyms->sym_cnt; i++) {
		if (strcmp(ksyms->syms[i].name, name) == 0)
			return ksyms->syms[i].addr;
	}

	return 0;
}

long ksym_get_addr(const char *name)
{
	if (!ksyms)
		return 0;
	return ksym_get_addr_local(ksyms, name);
}

/* open kallsyms and read symbol addresses on the fly. Without caching all symbols,
 * this is faster than load + find.
 */
int kallsyms_find(const char *sym, unsigned long long *addr)
{
	char type, name[500], *match;
	unsigned long long value;
	int err = 0;
	FILE *f;

	f = fopen("/proc/kallsyms", "r");
	if (!f)
		return -EINVAL;

	while (fscanf(f, "%llx %c %499s%*[^\n]\n", &value, &type, name) > 0) {
		/* If CONFIG_LTO_CLANG_THIN is enabled, static variable/function
		 * symbols could be promoted to global due to cross-file inlining.
		 * For such cases, clang compiler will add .llvm.<hash> suffix
		 * to those symbols to avoid potential naming conflict.
		 * Let us ignore .llvm.<hash> suffix during symbol comparison.
		 */
		if (type == 'd') {
			match = strstr(name, ".llvm.");
			if (match)
				*match = '\0';
		}
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
		const __u32 *insn = (const __u32 *)(uintptr_t)addr;

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

int read_trace_pipe_iter(void (*cb)(const char *str, void *data), void *data, int iter)
{
	size_t buflen, n;
	char *buf = NULL;
	FILE *fp = NULL;

	if (access(TRACEFS_PIPE, F_OK) == 0)
		fp = fopen(TRACEFS_PIPE, "r");
	else
		fp = fopen(DEBUGFS_PIPE, "r");
	if (!fp)
		return -1;

	 /* We do not want to wait forever when iter is specified. */
	if (iter)
		fcntl(fileno(fp), F_SETFL, O_NONBLOCK);

	while ((n = getline(&buf, &buflen, fp) >= 0) || errno == EAGAIN) {
		if (n > 0)
			cb(buf, data);
		if (iter && !(--iter))
			break;
	}

	free(buf);
	if (fp)
		fclose(fp);
	return 0;
}

static void trace_pipe_cb(const char *str, void *data)
{
	printf("%s", str);
}

void read_trace_pipe(void)
{
	read_trace_pipe_iter(trace_pipe_cb, NULL, 0);
}
