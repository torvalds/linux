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
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <asm/unistd.h>
#include <linux/kernel.h>
#include <linux/bpf.h>
#include <linux/list.h>
#include <libelf.h>
#include <gelf.h>

#include "libbpf.h"
#include "bpf.h"

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

#define STRERR_BUFSIZE  128

#define ERRNO_OFFSET(e)		((e) - __LIBBPF_ERRNO__START)
#define ERRCODE_OFFSET(c)	ERRNO_OFFSET(LIBBPF_ERRNO__##c)
#define NR_ERRNO	(__LIBBPF_ERRNO__END - __LIBBPF_ERRNO__START)

static const char *libbpf_strerror_table[NR_ERRNO] = {
	[ERRCODE_OFFSET(LIBELF)]	= "Something wrong in libelf",
	[ERRCODE_OFFSET(FORMAT)]	= "BPF object format invalid",
	[ERRCODE_OFFSET(KVERSION)]	= "'version' section incorrect or lost",
	[ERRCODE_OFFSET(ENDIAN)]	= "Endian missmatch",
	[ERRCODE_OFFSET(INTERNAL)]	= "Internal error in libbpf",
	[ERRCODE_OFFSET(RELOC)]		= "Relocation failed",
	[ERRCODE_OFFSET(VERIFY)]	= "Kernel verifier blocks program loading",
	[ERRCODE_OFFSET(PROG2BIG)]	= "Program too big",
	[ERRCODE_OFFSET(KVER)]		= "Incorrect kernel version",
};

int libbpf_strerror(int err, char *buf, size_t size)
{
	if (!buf || !size)
		return -1;

	err = err > 0 ? err : -err;

	if (err < __LIBBPF_ERRNO__START) {
		int ret;

		ret = strerror_r(err, buf, size);
		buf[size - 1] = '\0';
		return ret;
	}

	if (err < __LIBBPF_ERRNO__END) {
		const char *msg;

		msg = libbpf_strerror_table[ERRNO_OFFSET(err)];
		snprintf(buf, size, "%s", msg);
		buf[size - 1] = '\0';
		return 0;
	}

	snprintf(buf, size, "Unknown libbpf error %d", err);
	buf[size - 1] = '\0';
	return -1;
}

#define CHECK_ERR(action, err, out) do {	\
	err = action;			\
	if (err)			\
		goto out;		\
} while(0)


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

/*
 * bpf_prog should be a better name but it has been used in
 * linux/filter.h.
 */
struct bpf_program {
	/* Index in elf obj file, for relocation use. */
	int idx;
	char *section_name;
	struct bpf_insn *insns;
	size_t insns_cnt;

	struct {
		int insn_idx;
		int map_idx;
	} *reloc_desc;
	int nr_reloc;

	int fd;

	struct bpf_object *obj;
	void *priv;
	bpf_program_clear_priv_t clear_priv;
};

static LIST_HEAD(bpf_objects_list);

struct bpf_object {
	char license[64];
	u32 kern_version;
	void *maps_buf;
	size_t maps_buf_sz;

	struct bpf_program *programs;
	size_t nr_programs;
	int *map_fds;
	/*
	 * This field is required because maps_buf will be freed and
	 * maps_buf_sz will be set to 0 after loaded.
	 */
	size_t nr_map_fds;
	bool loaded;

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
		struct {
			GElf_Shdr shdr;
			Elf_Data *data;
		} *reloc;
		int nr_reloc;
	} efile;
	/*
	 * All loaded bpf_object is linked in a list, which is
	 * hidden to caller. bpf_objects__<func> handlers deal with
	 * all objects.
	 */
	struct list_head list;
	char path[];
};
#define obj_elf_valid(o)	((o)->efile.elf)

static void bpf_program__unload(struct bpf_program *prog)
{
	if (!prog)
		return;

	zclose(prog->fd);
}

static void bpf_program__exit(struct bpf_program *prog)
{
	if (!prog)
		return;

	if (prog->clear_priv)
		prog->clear_priv(prog, prog->priv);

	prog->priv = NULL;
	prog->clear_priv = NULL;

	bpf_program__unload(prog);
	zfree(&prog->section_name);
	zfree(&prog->insns);
	zfree(&prog->reloc_desc);

	prog->nr_reloc = 0;
	prog->insns_cnt = 0;
	prog->idx = -1;
}

static int
bpf_program__init(void *data, size_t size, char *name, int idx,
		    struct bpf_program *prog)
{
	if (size < sizeof(struct bpf_insn)) {
		pr_warning("corrupted section '%s'\n", name);
		return -EINVAL;
	}

	bzero(prog, sizeof(*prog));

	prog->section_name = strdup(name);
	if (!prog->section_name) {
		pr_warning("failed to alloc name for prog %s\n",
			   name);
		goto errout;
	}

	prog->insns = malloc(size);
	if (!prog->insns) {
		pr_warning("failed to alloc insns for %s\n", name);
		goto errout;
	}
	prog->insns_cnt = size / sizeof(struct bpf_insn);
	memcpy(prog->insns, data,
	       prog->insns_cnt * sizeof(struct bpf_insn));
	prog->idx = idx;
	prog->fd = -1;

	return 0;
errout:
	bpf_program__exit(prog);
	return -ENOMEM;
}

static int
bpf_object__add_program(struct bpf_object *obj, void *data, size_t size,
			char *name, int idx)
{
	struct bpf_program prog, *progs;
	int nr_progs, err;

	err = bpf_program__init(data, size, name, idx, &prog);
	if (err)
		return err;

	progs = obj->programs;
	nr_progs = obj->nr_programs;

	progs = realloc(progs, sizeof(progs[0]) * (nr_progs + 1));
	if (!progs) {
		/*
		 * In this case the original obj->programs
		 * is still valid, so don't need special treat for
		 * bpf_close_object().
		 */
		pr_warning("failed to alloc a new program '%s'\n",
			   name);
		bpf_program__exit(&prog);
		return -ENOMEM;
	}

	pr_debug("found program %s\n", prog.section_name);
	obj->programs = progs;
	obj->nr_programs = nr_progs + 1;
	prog.obj = obj;
	progs[nr_progs] = prog;
	return 0;
}

static struct bpf_object *bpf_object__new(const char *path,
					  void *obj_buf,
					  size_t obj_buf_sz)
{
	struct bpf_object *obj;

	obj = calloc(1, sizeof(struct bpf_object) + strlen(path) + 1);
	if (!obj) {
		pr_warning("alloc memory failed for %s\n", path);
		return ERR_PTR(-ENOMEM);
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

	obj->loaded = false;

	INIT_LIST_HEAD(&obj->list);
	list_add(&obj->list, &bpf_objects_list);
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

	zfree(&obj->efile.reloc);
	obj->efile.nr_reloc = 0;
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
		return -LIBBPF_ERRNO__LIBELF;
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
		err = -LIBBPF_ERRNO__LIBELF;
		goto errout;
	}

	if (!gelf_getehdr(obj->efile.elf, &obj->efile.ehdr)) {
		pr_warning("failed to get EHDR from %s\n",
				obj->path);
		err = -LIBBPF_ERRNO__FORMAT;
		goto errout;
	}
	ep = &obj->efile.ehdr;

	if ((ep->e_type != ET_REL) || (ep->e_machine != 0)) {
		pr_warning("%s is not an eBPF object file\n",
			obj->path);
		err = -LIBBPF_ERRNO__FORMAT;
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
		return -LIBBPF_ERRNO__ENDIAN;
	}

	return 0;

mismatch:
	pr_warning("Error: endianness mismatch.\n");
	return -LIBBPF_ERRNO__ENDIAN;
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
		return -LIBBPF_ERRNO__FORMAT;
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
		return -LIBBPF_ERRNO__FORMAT;
	}

	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		char *name;
		GElf_Shdr sh;
		Elf_Data *data;

		idx++;
		if (gelf_getshdr(scn, &sh) != &sh) {
			pr_warning("failed to get section header from %s\n",
				   obj->path);
			err = -LIBBPF_ERRNO__FORMAT;
			goto out;
		}

		name = elf_strptr(elf, ep->e_shstrndx, sh.sh_name);
		if (!name) {
			pr_warning("failed to get section name from %s\n",
				   obj->path);
			err = -LIBBPF_ERRNO__FORMAT;
			goto out;
		}

		data = elf_getdata(scn, 0);
		if (!data) {
			pr_warning("failed to get section data from %s(%s)\n",
				   name, obj->path);
			err = -LIBBPF_ERRNO__FORMAT;
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
				err = -LIBBPF_ERRNO__FORMAT;
			} else
				obj->efile.symbols = data;
		} else if ((sh.sh_type == SHT_PROGBITS) &&
			   (sh.sh_flags & SHF_EXECINSTR) &&
			   (data->d_size > 0)) {
			err = bpf_object__add_program(obj, data->d_buf,
						      data->d_size, name, idx);
			if (err) {
				char errmsg[STRERR_BUFSIZE];

				strerror_r(-err, errmsg, sizeof(errmsg));
				pr_warning("failed to alloc program %s (%s): %s",
					   name, obj->path, errmsg);
			}
		} else if (sh.sh_type == SHT_REL) {
			void *reloc = obj->efile.reloc;
			int nr_reloc = obj->efile.nr_reloc + 1;

			reloc = realloc(reloc,
					sizeof(*obj->efile.reloc) * nr_reloc);
			if (!reloc) {
				pr_warning("realloc failed\n");
				err = -ENOMEM;
			} else {
				int n = nr_reloc - 1;

				obj->efile.reloc = reloc;
				obj->efile.nr_reloc = nr_reloc;

				obj->efile.reloc[n].shdr = sh;
				obj->efile.reloc[n].data = data;
			}
		}
		if (err)
			goto out;
	}
out:
	return err;
}

static struct bpf_program *
bpf_object__find_prog_by_idx(struct bpf_object *obj, int idx)
{
	struct bpf_program *prog;
	size_t i;

	for (i = 0; i < obj->nr_programs; i++) {
		prog = &obj->programs[i];
		if (prog->idx == idx)
			return prog;
	}
	return NULL;
}

static int
bpf_program__collect_reloc(struct bpf_program *prog,
			   size_t nr_maps, GElf_Shdr *shdr,
			   Elf_Data *data, Elf_Data *symbols)
{
	int i, nrels;

	pr_debug("collecting relocating info for: '%s'\n",
		 prog->section_name);
	nrels = shdr->sh_size / shdr->sh_entsize;

	prog->reloc_desc = malloc(sizeof(*prog->reloc_desc) * nrels);
	if (!prog->reloc_desc) {
		pr_warning("failed to alloc memory in relocation\n");
		return -ENOMEM;
	}
	prog->nr_reloc = nrels;

	for (i = 0; i < nrels; i++) {
		GElf_Sym sym;
		GElf_Rel rel;
		unsigned int insn_idx;
		struct bpf_insn *insns = prog->insns;
		size_t map_idx;

		if (!gelf_getrel(data, i, &rel)) {
			pr_warning("relocation: failed to get %d reloc\n", i);
			return -LIBBPF_ERRNO__FORMAT;
		}

		insn_idx = rel.r_offset / sizeof(struct bpf_insn);
		pr_debug("relocation: insn_idx=%u\n", insn_idx);

		if (!gelf_getsym(symbols,
				 GELF_R_SYM(rel.r_info),
				 &sym)) {
			pr_warning("relocation: symbol %"PRIx64" not found\n",
				   GELF_R_SYM(rel.r_info));
			return -LIBBPF_ERRNO__FORMAT;
		}

		if (insns[insn_idx].code != (BPF_LD | BPF_IMM | BPF_DW)) {
			pr_warning("bpf: relocation: invalid relo for insns[%d].code 0x%x\n",
				   insn_idx, insns[insn_idx].code);
			return -LIBBPF_ERRNO__RELOC;
		}

		map_idx = sym.st_value / sizeof(struct bpf_map_def);
		if (map_idx >= nr_maps) {
			pr_warning("bpf relocation: map_idx %d large than %d\n",
				   (int)map_idx, (int)nr_maps - 1);
			return -LIBBPF_ERRNO__RELOC;
		}

		prog->reloc_desc[i].insn_idx = insn_idx;
		prog->reloc_desc[i].map_idx = map_idx;
	}
	return 0;
}

static int
bpf_object__create_maps(struct bpf_object *obj)
{
	unsigned int i;
	size_t nr_maps;
	int *pfd;

	nr_maps = obj->maps_buf_sz / sizeof(struct bpf_map_def);
	if (!obj->maps_buf || !nr_maps) {
		pr_debug("don't need create maps for %s\n",
			 obj->path);
		return 0;
	}

	obj->map_fds = malloc(sizeof(int) * nr_maps);
	if (!obj->map_fds) {
		pr_warning("realloc perf_bpf_map_fds failed\n");
		return -ENOMEM;
	}
	obj->nr_map_fds = nr_maps;

	/* fill all fd with -1 */
	memset(obj->map_fds, -1, sizeof(int) * nr_maps);

	pfd = obj->map_fds;
	for (i = 0; i < nr_maps; i++) {
		struct bpf_map_def def;

		def = *(struct bpf_map_def *)(obj->maps_buf +
				i * sizeof(struct bpf_map_def));

		*pfd = bpf_create_map(def.type,
				      def.key_size,
				      def.value_size,
				      def.max_entries);
		if (*pfd < 0) {
			size_t j;
			int err = *pfd;

			pr_warning("failed to create map: %s\n",
				   strerror(errno));
			for (j = 0; j < i; j++)
				zclose(obj->map_fds[j]);
			obj->nr_map_fds = 0;
			zfree(&obj->map_fds);
			return err;
		}
		pr_debug("create map: fd=%d\n", *pfd);
		pfd++;
	}

	zfree(&obj->maps_buf);
	obj->maps_buf_sz = 0;
	return 0;
}

static int
bpf_program__relocate(struct bpf_program *prog, int *map_fds)
{
	int i;

	if (!prog || !prog->reloc_desc)
		return 0;

	for (i = 0; i < prog->nr_reloc; i++) {
		int insn_idx, map_idx;
		struct bpf_insn *insns = prog->insns;

		insn_idx = prog->reloc_desc[i].insn_idx;
		map_idx = prog->reloc_desc[i].map_idx;

		if (insn_idx >= (int)prog->insns_cnt) {
			pr_warning("relocation out of range: '%s'\n",
				   prog->section_name);
			return -LIBBPF_ERRNO__RELOC;
		}
		insns[insn_idx].src_reg = BPF_PSEUDO_MAP_FD;
		insns[insn_idx].imm = map_fds[map_idx];
	}

	zfree(&prog->reloc_desc);
	prog->nr_reloc = 0;
	return 0;
}


static int
bpf_object__relocate(struct bpf_object *obj)
{
	struct bpf_program *prog;
	size_t i;
	int err;

	for (i = 0; i < obj->nr_programs; i++) {
		prog = &obj->programs[i];

		err = bpf_program__relocate(prog, obj->map_fds);
		if (err) {
			pr_warning("failed to relocate '%s'\n",
				   prog->section_name);
			return err;
		}
	}
	return 0;
}

static int bpf_object__collect_reloc(struct bpf_object *obj)
{
	int i, err;

	if (!obj_elf_valid(obj)) {
		pr_warning("Internal error: elf object is closed\n");
		return -LIBBPF_ERRNO__INTERNAL;
	}

	for (i = 0; i < obj->efile.nr_reloc; i++) {
		GElf_Shdr *shdr = &obj->efile.reloc[i].shdr;
		Elf_Data *data = obj->efile.reloc[i].data;
		int idx = shdr->sh_info;
		struct bpf_program *prog;
		size_t nr_maps = obj->maps_buf_sz /
				 sizeof(struct bpf_map_def);

		if (shdr->sh_type != SHT_REL) {
			pr_warning("internal error at %d\n", __LINE__);
			return -LIBBPF_ERRNO__INTERNAL;
		}

		prog = bpf_object__find_prog_by_idx(obj, idx);
		if (!prog) {
			pr_warning("relocation failed: no %d section\n",
				   idx);
			return -LIBBPF_ERRNO__RELOC;
		}

		err = bpf_program__collect_reloc(prog, nr_maps,
						 shdr, data,
						 obj->efile.symbols);
		if (err)
			return err;
	}
	return 0;
}

static int
load_program(struct bpf_insn *insns, int insns_cnt,
	     char *license, u32 kern_version, int *pfd)
{
	int ret;
	char *log_buf;

	if (!insns || !insns_cnt)
		return -EINVAL;

	log_buf = malloc(BPF_LOG_BUF_SIZE);
	if (!log_buf)
		pr_warning("Alloc log buffer for bpf loader error, continue without log\n");

	ret = bpf_load_program(BPF_PROG_TYPE_KPROBE, insns,
			       insns_cnt, license, kern_version,
			       log_buf, BPF_LOG_BUF_SIZE);

	if (ret >= 0) {
		*pfd = ret;
		ret = 0;
		goto out;
	}

	ret = -LIBBPF_ERRNO__LOAD;
	pr_warning("load bpf program failed: %s\n", strerror(errno));

	if (log_buf && log_buf[0] != '\0') {
		ret = -LIBBPF_ERRNO__VERIFY;
		pr_warning("-- BEGIN DUMP LOG ---\n");
		pr_warning("\n%s\n", log_buf);
		pr_warning("-- END LOG --\n");
	} else {
		if (insns_cnt >= BPF_MAXINSNS) {
			pr_warning("Program too large (%d insns), at most %d insns\n",
				   insns_cnt, BPF_MAXINSNS);
			ret = -LIBBPF_ERRNO__PROG2BIG;
		} else if (log_buf) {
			pr_warning("log buffer is empty\n");
			ret = -LIBBPF_ERRNO__KVER;
		}
	}

out:
	free(log_buf);
	return ret;
}

static int
bpf_program__load(struct bpf_program *prog,
		  char *license, u32 kern_version)
{
	int err, fd;

	err = load_program(prog->insns, prog->insns_cnt,
			   license, kern_version, &fd);
	if (!err)
		prog->fd = fd;

	if (err)
		pr_warning("failed to load program '%s'\n",
			   prog->section_name);
	zfree(&prog->insns);
	prog->insns_cnt = 0;
	return err;
}

static int
bpf_object__load_progs(struct bpf_object *obj)
{
	size_t i;
	int err;

	for (i = 0; i < obj->nr_programs; i++) {
		err = bpf_program__load(&obj->programs[i],
					obj->license,
					obj->kern_version);
		if (err)
			return err;
	}
	return 0;
}

static int bpf_object__validate(struct bpf_object *obj)
{
	if (obj->kern_version == 0) {
		pr_warning("%s doesn't provide kernel version\n",
			   obj->path);
		return -LIBBPF_ERRNO__KVERSION;
	}
	return 0;
}

static struct bpf_object *
__bpf_object__open(const char *path, void *obj_buf, size_t obj_buf_sz)
{
	struct bpf_object *obj;
	int err;

	if (elf_version(EV_CURRENT) == EV_NONE) {
		pr_warning("failed to init libelf for %s\n", path);
		return ERR_PTR(-LIBBPF_ERRNO__LIBELF);
	}

	obj = bpf_object__new(path, obj_buf, obj_buf_sz);
	if (IS_ERR(obj))
		return obj;

	CHECK_ERR(bpf_object__elf_init(obj), err, out);
	CHECK_ERR(bpf_object__check_endianness(obj), err, out);
	CHECK_ERR(bpf_object__elf_collect(obj), err, out);
	CHECK_ERR(bpf_object__collect_reloc(obj), err, out);
	CHECK_ERR(bpf_object__validate(obj), err, out);

	bpf_object__elf_finish(obj);
	return obj;
out:
	bpf_object__close(obj);
	return ERR_PTR(err);
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
					   size_t obj_buf_sz,
					   const char *name)
{
	char tmp_name[64];

	/* param validation */
	if (!obj_buf || obj_buf_sz <= 0)
		return NULL;

	if (!name) {
		snprintf(tmp_name, sizeof(tmp_name), "%lx-%lx",
			 (unsigned long)obj_buf,
			 (unsigned long)obj_buf_sz);
		tmp_name[sizeof(tmp_name) - 1] = '\0';
		name = tmp_name;
	}
	pr_debug("loading object '%s' from buffer\n",
		 name);

	return __bpf_object__open(name, obj_buf, obj_buf_sz);
}

int bpf_object__unload(struct bpf_object *obj)
{
	size_t i;

	if (!obj)
		return -EINVAL;

	for (i = 0; i < obj->nr_map_fds; i++)
		zclose(obj->map_fds[i]);
	zfree(&obj->map_fds);
	obj->nr_map_fds = 0;

	for (i = 0; i < obj->nr_programs; i++)
		bpf_program__unload(&obj->programs[i]);

	return 0;
}

int bpf_object__load(struct bpf_object *obj)
{
	int err;

	if (!obj)
		return -EINVAL;

	if (obj->loaded) {
		pr_warning("object should not be loaded twice\n");
		return -EINVAL;
	}

	obj->loaded = true;

	CHECK_ERR(bpf_object__create_maps(obj), err, out);
	CHECK_ERR(bpf_object__relocate(obj), err, out);
	CHECK_ERR(bpf_object__load_progs(obj), err, out);

	return 0;
out:
	bpf_object__unload(obj);
	pr_warning("failed to load object '%s'\n", obj->path);
	return err;
}

void bpf_object__close(struct bpf_object *obj)
{
	size_t i;

	if (!obj)
		return;

	bpf_object__elf_finish(obj);
	bpf_object__unload(obj);

	zfree(&obj->maps_buf);

	if (obj->programs && obj->nr_programs) {
		for (i = 0; i < obj->nr_programs; i++)
			bpf_program__exit(&obj->programs[i]);
	}
	zfree(&obj->programs);

	list_del(&obj->list);
	free(obj);
}

struct bpf_object *
bpf_object__next(struct bpf_object *prev)
{
	struct bpf_object *next;

	if (!prev)
		next = list_first_entry(&bpf_objects_list,
					struct bpf_object,
					list);
	else
		next = list_next_entry(prev, list);

	/* Empty list is noticed here so don't need checking on entry. */
	if (&next->list == &bpf_objects_list)
		return NULL;

	return next;
}

const char *
bpf_object__get_name(struct bpf_object *obj)
{
	if (!obj)
		return ERR_PTR(-EINVAL);
	return obj->path;
}

unsigned int
bpf_object__get_kversion(struct bpf_object *obj)
{
	if (!obj)
		return 0;
	return obj->kern_version;
}

struct bpf_program *
bpf_program__next(struct bpf_program *prev, struct bpf_object *obj)
{
	size_t idx;

	if (!obj->programs)
		return NULL;
	/* First handler */
	if (prev == NULL)
		return &obj->programs[0];

	if (prev->obj != obj) {
		pr_warning("error: program handler doesn't match object\n");
		return NULL;
	}

	idx = (prev - obj->programs) + 1;
	if (idx >= obj->nr_programs)
		return NULL;
	return &obj->programs[idx];
}

int bpf_program__set_private(struct bpf_program *prog,
			     void *priv,
			     bpf_program_clear_priv_t clear_priv)
{
	if (prog->priv && prog->clear_priv)
		prog->clear_priv(prog, prog->priv);

	prog->priv = priv;
	prog->clear_priv = clear_priv;
	return 0;
}

int bpf_program__get_private(struct bpf_program *prog, void **ppriv)
{
	*ppriv = prog->priv;
	return 0;
}

const char *bpf_program__title(struct bpf_program *prog, bool needs_copy)
{
	const char *title;

	title = prog->section_name;
	if (needs_copy) {
		title = strdup(title);
		if (!title) {
			pr_warning("failed to strdup program title\n");
			return ERR_PTR(-ENOMEM);
		}
	}

	return title;
}

int bpf_program__fd(struct bpf_program *prog)
{
	return prog->fd;
}
