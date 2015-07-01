/*
 * Common eBPF ELF object loading operations.
 *
 * Copyright (C) 2013-2015 Alexei Starovoitov <ast@kernel.org>
 * Copyright (C) 2015 Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2015 Huawei Inc.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <asm/unistd.h>
#include <linux/bpf.h>
#include <libelf.h>
#include <gelf.h>

#include "libbpf.h"

#define __printf(a, b)	__attribute__((format(printf, a, b)))

__printf(1, 2)
static int __base_pr(const char *format, ...)
{
	va_list args;
	int err;

	va_start(args, format);
	err = vfprintf(stderr, format, args);
	va_end(args);
	return err;
}

static __printf(1, 2) libbpf_print_fn_t __pr_warning = __base_pr;
static __printf(1, 2) libbpf_print_fn_t __pr_info = __base_pr;
static __printf(1, 2) libbpf_print_fn_t __pr_debug;

#define __pr(func, fmt, ...)	\
do {				\
	if ((func))		\
		(func)("libbpf: " fmt, ##__VA_ARGS__); \
} while (0)

#define pr_warning(fmt, ...)	__pr(__pr_warning, fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...)	__pr(__pr_info, fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...)	__pr(__pr_debug, fmt, ##__VA_ARGS__)

void libbpf_set_print(libbpf_print_fn_t warn,
		      libbpf_print_fn_t info,
		      libbpf_print_fn_t debug)
{
	__pr_warning = warn;
	__pr_info = info;
	__pr_debug = debug;
}

/* Copied from tools/perf/util/util.h */
#ifndef zfree
# define zfree(ptr) ({ free(*ptr); *ptr = NULL; })
#endif

#ifndef zclose
# define zclose(fd) ({			\
	int ___err = 0;			\
	if ((fd) >= 0)			\
		___err = close((fd));	\
	fd = -1;			\
	___err; })
#endif

#ifdef HAVE_LIBELF_MMAP_SUPPORT
# define LIBBPF_ELF_C_READ_MMAP ELF_C_READ_MMAP
#else
# define LIBBPF_ELF_C_READ_MMAP ELF_C_READ
#endif

struct bpf_object {
	/*
	 * Information when doing elf related work. Only valid if fd
	 * is valid.
	 */
	struct {
		int fd;
		Elf *elf;
		GElf_Ehdr ehdr;
	} efile;
	char path[];
};
#define obj_elf_valid(o)	((o)->efile.elf)

static struct bpf_object *bpf_object__new(const char *path)
{
	struct bpf_object *obj;

	obj = calloc(1, sizeof(struct bpf_object) + strlen(path) + 1);
	if (!obj) {
		pr_warning("alloc memory failed for %s\n", path);
		return NULL;
	}

	strcpy(obj->path, path);
	obj->efile.fd = -1;
	return obj;
}

static void bpf_object__elf_finish(struct bpf_object *obj)
{
	if (!obj_elf_valid(obj))
		return;

	if (obj->efile.elf) {
		elf_end(obj->efile.elf);
		obj->efile.elf = NULL;
	}
	zclose(obj->efile.fd);
}

static int bpf_object__elf_init(struct bpf_object *obj)
{
	int err = 0;
	GElf_Ehdr *ep;

	if (obj_elf_valid(obj)) {
		pr_warning("elf init: internal error\n");
		return -EEXIST;
	}

	obj->efile.fd = open(obj->path, O_RDONLY);
	if (obj->efile.fd < 0) {
		pr_warning("failed to open %s: %s\n", obj->path,
				strerror(errno));
		return -errno;
	}

	obj->efile.elf = elf_begin(obj->efile.fd,
				 LIBBPF_ELF_C_READ_MMAP,
				 NULL);
	if (!obj->efile.elf) {
		pr_warning("failed to open %s as ELF file\n",
				obj->path);
		err = -EINVAL;
		goto errout;
	}

	if (!gelf_getehdr(obj->efile.elf, &obj->efile.ehdr)) {
		pr_warning("failed to get EHDR from %s\n",
				obj->path);
		err = -EINVAL;
		goto errout;
	}
	ep = &obj->efile.ehdr;

	if ((ep->e_type != ET_REL) || (ep->e_machine != 0)) {
		pr_warning("%s is not an eBPF object file\n",
			obj->path);
		err = -EINVAL;
		goto errout;
	}

	return 0;
errout:
	bpf_object__elf_finish(obj);
	return err;
}

static struct bpf_object *
__bpf_object__open(const char *path)
{
	struct bpf_object *obj;

	if (elf_version(EV_CURRENT) == EV_NONE) {
		pr_warning("failed to init libelf for %s\n", path);
		return NULL;
	}

	obj = bpf_object__new(path);
	if (!obj)
		return NULL;

	if (bpf_object__elf_init(obj))
		goto out;

	bpf_object__elf_finish(obj);
	return obj;
out:
	bpf_object__close(obj);
	return NULL;
}

struct bpf_object *bpf_object__open(const char *path)
{
	/* param validation */
	if (!path)
		return NULL;

	pr_debug("loading %s\n", path);

	return __bpf_object__open(path);
}

void bpf_object__close(struct bpf_object *obj)
{
	if (!obj)
		return;

	bpf_object__elf_finish(obj);

	free(obj);
}
