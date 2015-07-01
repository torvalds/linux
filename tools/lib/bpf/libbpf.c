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
#include <linux/kernel.h>
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
	char license[64];
	u32 kern_version;
	void *maps_buf;
	size_t maps_buf_sz;

	/*
	 * Information when doing elf related work. Only valid if fd
	 * is valid.
	 */
	struct {
		int fd;
		void *obj_buf;
		size_t obj_buf_sz;
		Elf *elf;
		GElf_Ehdr ehdr;
		Elf_Data *symbols;
	} efile;
	char path[];
};
#define obj_elf_valid(o)	((o)->efile.elf)

static struct bpf_object *bpf_object__new(const char *path,
					  void *obj_buf,
					  size_t obj_buf_sz)
{
	struct bpf_object *obj;

	obj = calloc(1, sizeof(struct bpf_object) + strlen(path) + 1);
	if (!obj) {
		pr_warning("alloc memory failed for %s\n", path);
		return NULL;
	}

	strcpy(obj->path, path);
	obj->efile.fd = -1;

	/*
	 * Caller of this function should also calls
	 * bpf_object__elf_finish() after data collection to return
	 * obj_buf to user. If not, we should duplicate the buffer to
	 * avoid user freeing them before elf finish.
	 */
	obj->efile.obj_buf = obj_buf;
	obj->efile.obj_buf_sz = obj_buf_sz;

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
	obj->efile.symbols = NULL;
	zclose(obj->efile.fd);
	obj->efile.obj_buf = NULL;
	obj->efile.obj_buf_sz = 0;
}

static int bpf_object__elf_init(struct bpf_object *obj)
{
	int err = 0;
	GElf_Ehdr *ep;

	if (obj_elf_valid(obj)) {
		pr_warning("elf init: internal error\n");
		return -EEXIST;
	}

	if (obj->efile.obj_buf_sz > 0) {
		/*
		 * obj_buf should have been validated by
		 * bpf_object__open_buffer().
		 */
		obj->efile.elf = elf_memory(obj->efile.obj_buf,
					    obj->efile.obj_buf_sz);
	} else {
		obj->efile.fd = open(obj->path, O_RDONLY);
		if (obj->efile.fd < 0) {
			pr_warning("failed to open %s: %s\n", obj->path,
					strerror(errno));
			return -errno;
		}

		obj->efile.elf = elf_begin(obj->efile.fd,
				LIBBPF_ELF_C_READ_MMAP,
				NULL);
	}

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

static int
bpf_object__check_endianness(struct bpf_object *obj)
{
	static unsigned int const endian = 1;

	switch (obj->efile.ehdr.e_ident[EI_DATA]) {
	case ELFDATA2LSB:
		/* We are big endian, BPF obj is little endian. */
		if (*(unsigned char const *)&endian != 1)
			goto mismatch;
		break;

	case ELFDATA2MSB:
		/* We are little endian, BPF obj is big endian. */
		if (*(unsigned char const *)&endian != 0)
			goto mismatch;
		break;
	default:
		return -EINVAL;
	}

	return 0;

mismatch:
	pr_warning("Error: endianness mismatch.\n");
	return -EINVAL;
}

static int
bpf_object__init_license(struct bpf_object *obj,
			 void *data, size_t size)
{
	memcpy(obj->license, data,
	       min(size, sizeof(obj->license) - 1));
	pr_debug("license of %s is %s\n", obj->path, obj->license);
	return 0;
}

static int
bpf_object__init_kversion(struct bpf_object *obj,
			  void *data, size_t size)
{
	u32 kver;

	if (size != sizeof(kver)) {
		pr_warning("invalid kver section in %s\n", obj->path);
		return -EINVAL;
	}
	memcpy(&kver, data, sizeof(kver));
	obj->kern_version = kver;
	pr_debug("kernel version of %s is %x\n", obj->path,
		 obj->kern_version);
	return 0;
}

static int
bpf_object__init_maps(struct bpf_object *obj, void *data,
		      size_t size)
{
	if (size == 0) {
		pr_debug("%s doesn't need map definition\n",
			 obj->path);
		return 0;
	}

	obj->maps_buf = malloc(size);
	if (!obj->maps_buf) {
		pr_warning("malloc maps failed: %s\n", obj->path);
		return -ENOMEM;
	}

	obj->maps_buf_sz = size;
	memcpy(obj->maps_buf, data, size);
	pr_debug("maps in %s: %ld bytes\n", obj->path, (long)size);
	return 0;
}

static int bpf_object__elf_collect(struct bpf_object *obj)
{
	Elf *elf = obj->efile.elf;
	GElf_Ehdr *ep = &obj->efile.ehdr;
	Elf_Scn *scn = NULL;
	int idx = 0, err = 0;

	/* Elf is corrupted/truncated, avoid calling elf_strptr. */
	if (!elf_rawdata(elf_getscn(elf, ep->e_shstrndx), NULL)) {
		pr_warning("failed to get e_shstrndx from %s\n",
			   obj->path);
		return -EINVAL;
	}

	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		char *name;
		GElf_Shdr sh;
		Elf_Data *data;

		idx++;
		if (gelf_getshdr(scn, &sh) != &sh) {
			pr_warning("failed to get section header from %s\n",
				   obj->path);
			err = -EINVAL;
			goto out;
		}

		name = elf_strptr(elf, ep->e_shstrndx, sh.sh_name);
		if (!name) {
			pr_warning("failed to get section name from %s\n",
				   obj->path);
			err = -EINVAL;
			goto out;
		}

		data = elf_getdata(scn, 0);
		if (!data) {
			pr_warning("failed to get section data from %s(%s)\n",
				   name, obj->path);
			err = -EINVAL;
			goto out;
		}
		pr_debug("section %s, size %ld, link %d, flags %lx, type=%d\n",
			 name, (unsigned long)data->d_size,
			 (int)sh.sh_link, (unsigned long)sh.sh_flags,
			 (int)sh.sh_type);

		if (strcmp(name, "license") == 0)
			err = bpf_object__init_license(obj,
						       data->d_buf,
						       data->d_size);
		else if (strcmp(name, "version") == 0)
			err = bpf_object__init_kversion(obj,
							data->d_buf,
							data->d_size);
		else if (strcmp(name, "maps") == 0)
			err = bpf_object__init_maps(obj, data->d_buf,
						    data->d_size);
		else if (sh.sh_type == SHT_SYMTAB) {
			if (obj->efile.symbols) {
				pr_warning("bpf: multiple SYMTAB in %s\n",
					   obj->path);
				err = -EEXIST;
			} else
				obj->efile.symbols = data;
		}
		if (err)
			goto out;
	}
out:
	return err;
}

static int bpf_object__validate(struct bpf_object *obj)
{
	if (obj->kern_version == 0) {
		pr_warning("%s doesn't provide kernel version\n",
			   obj->path);
		return -EINVAL;
	}
	return 0;
}

static struct bpf_object *
__bpf_object__open(const char *path, void *obj_buf, size_t obj_buf_sz)
{
	struct bpf_object *obj;

	if (elf_version(EV_CURRENT) == EV_NONE) {
		pr_warning("failed to init libelf for %s\n", path);
		return NULL;
	}

	obj = bpf_object__new(path, obj_buf, obj_buf_sz);
	if (!obj)
		return NULL;

	if (bpf_object__elf_init(obj))
		goto out;
	if (bpf_object__check_endianness(obj))
		goto out;
	if (bpf_object__elf_collect(obj))
		goto out;
	if (bpf_object__validate(obj))
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

	return __bpf_object__open(path, NULL, 0);
}

struct bpf_object *bpf_object__open_buffer(void *obj_buf,
					   size_t obj_buf_sz)
{
	/* param validation */
	if (!obj_buf || obj_buf_sz <= 0)
		return NULL;

	pr_debug("loading object from buffer\n");

	return __bpf_object__open("[buffer]", obj_buf, obj_buf_sz);
}

void bpf_object__close(struct bpf_object *obj)
{
	if (!obj)
		return;

	bpf_object__elf_finish(obj);

	zfree(&obj->maps_buf);
	free(obj);
}
