/*
 * Post mortem Dwarf CFI based unwinding on top of regs and stack dumps.
 *
 * Lots of this code have been borrowed or heavily inspired from parts of
 * the libunwind 0.99 code which are (amongst other contributors I may have
 * forgotten):
 *
 * Copyright (C) 2002-2007 Hewlett-Packard Co
 *	Contributed by David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * And the bugs have been added by:
 *
 * Copyright (C) 2010, Frederic Weisbecker <fweisbec@gmail.com>
 * Copyright (C) 2012, Jiri Olsa <jolsa@redhat.com>
 *
 */

#include <elf.h>
#include <gelf.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <linux/list.h>
#include <libunwind.h>
#include <libunwind-ptrace.h>
#include "callchain.h"
#include "thread.h"
#include "session.h"
#include "perf_regs.h"
#include "unwind.h"
#include "symbol.h"
#include "util.h"
#include "debug.h"

extern int
UNW_OBJ(dwarf_search_unwind_table) (unw_addr_space_t as,
				    unw_word_t ip,
				    unw_dyn_info_t *di,
				    unw_proc_info_t *pi,
				    int need_unwind_info, void *arg);

#define dwarf_search_unwind_table UNW_OBJ(dwarf_search_unwind_table)

extern int
UNW_OBJ(dwarf_find_debug_frame) (int found, unw_dyn_info_t *di_debug,
				 unw_word_t ip,
				 unw_word_t segbase,
				 const char *obj_name, unw_word_t start,
				 unw_word_t end);

#define dwarf_find_debug_frame UNW_OBJ(dwarf_find_debug_frame)

#define DW_EH_PE_FORMAT_MASK	0x0f	/* format of the encoded value */
#define DW_EH_PE_APPL_MASK	0x70	/* how the value is to be applied */

/* Pointer-encoding formats: */
#define DW_EH_PE_omit		0xff
#define DW_EH_PE_ptr		0x00	/* pointer-sized unsigned value */
#define DW_EH_PE_udata4		0x03	/* unsigned 32-bit value */
#define DW_EH_PE_udata8		0x04	/* unsigned 64-bit value */
#define DW_EH_PE_sdata4		0x0b	/* signed 32-bit value */
#define DW_EH_PE_sdata8		0x0c	/* signed 64-bit value */

/* Pointer-encoding application: */
#define DW_EH_PE_absptr		0x00	/* absolute value */
#define DW_EH_PE_pcrel		0x10	/* rel. to addr. of encoded value */

/*
 * The following are not documented by LSB v1.3, yet they are used by
 * GCC, presumably they aren't documented by LSB since they aren't
 * used on Linux:
 */
#define DW_EH_PE_funcrel	0x40	/* start-of-procedure-relative */
#define DW_EH_PE_aligned	0x50	/* aligned pointer */

/* Flags intentionaly not handled, since they're not needed:
 * #define DW_EH_PE_indirect      0x80
 * #define DW_EH_PE_uleb128       0x01
 * #define DW_EH_PE_udata2        0x02
 * #define DW_EH_PE_sleb128       0x09
 * #define DW_EH_PE_sdata2        0x0a
 * #define DW_EH_PE_textrel       0x20
 * #define DW_EH_PE_datarel       0x30
 */

struct unwind_info {
	struct perf_sample	*sample;
	struct machine		*machine;
	struct thread		*thread;
};

#define dw_read(ptr, type, end) ({	\
	type *__p = (type *) ptr;	\
	type  __v;			\
	if ((__p + 1) > (type *) end)	\
		return -EINVAL;		\
	__v = *__p++;			\
	ptr = (typeof(ptr)) __p;	\
	__v;				\
	})

static int __dw_read_encoded_value(u8 **p, u8 *end, u64 *val,
				   u8 encoding)
{
	u8 *cur = *p;
	*val = 0;

	switch (encoding) {
	case DW_EH_PE_omit:
		*val = 0;
		goto out;
	case DW_EH_PE_ptr:
		*val = dw_read(cur, unsigned long, end);
		goto out;
	default:
		break;
	}

	switch (encoding & DW_EH_PE_APPL_MASK) {
	case DW_EH_PE_absptr:
		break;
	case DW_EH_PE_pcrel:
		*val = (unsigned long) cur;
		break;
	default:
		return -EINVAL;
	}

	if ((encoding & 0x07) == 0x00)
		encoding |= DW_EH_PE_udata4;

	switch (encoding & DW_EH_PE_FORMAT_MASK) {
	case DW_EH_PE_sdata4:
		*val += dw_read(cur, s32, end);
		break;
	case DW_EH_PE_udata4:
		*val += dw_read(cur, u32, end);
		break;
	case DW_EH_PE_sdata8:
		*val += dw_read(cur, s64, end);
		break;
	case DW_EH_PE_udata8:
		*val += dw_read(cur, u64, end);
		break;
	default:
		return -EINVAL;
	}

 out:
	*p = cur;
	return 0;
}

#define dw_read_encoded_value(ptr, end, enc) ({			\
	u64 __v;						\
	if (__dw_read_encoded_value(&ptr, end, &__v, enc)) {	\
		return -EINVAL;                                 \
	}                                                       \
	__v;                                                    \
	})

static u64 elf_section_offset(int fd, const char *name)
{
	Elf *elf;
	GElf_Ehdr ehdr;
	GElf_Shdr shdr;
	u64 offset = 0;

	elf = elf_begin(fd, PERF_ELF_C_READ_MMAP, NULL);
	if (elf == NULL)
		return 0;

	do {
		if (gelf_getehdr(elf, &ehdr) == NULL)
			break;

		if (!elf_section_by_name(elf, &ehdr, &shdr, name, NULL))
			break;

		offset = shdr.sh_offset;
	} while (0);

	elf_end(elf);
	return offset;
}

struct table_entry {
	u32 start_ip_offset;
	u32 fde_offset;
};

struct eh_frame_hdr {
	unsigned char version;
	unsigned char eh_frame_ptr_enc;
	unsigned char fde_count_enc;
	unsigned char table_enc;

	/*
	 * The rest of the header is variable-length and consists of the
	 * following members:
	 *
	 *	encoded_t eh_frame_ptr;
	 *	encoded_t fde_count;
	 */

	/* A single encoded pointer should not be more than 8 bytes. */
	u64 enc[2];

	/*
	 * struct {
	 *    encoded_t start_ip;
	 *    encoded_t fde_addr;
	 * } binary_search_table[fde_count];
	 */
	char data[0];
} __packed;

static int unwind_spec_ehframe(struct dso *dso, struct machine *machine,
			       u64 offset, u64 *table_data, u64 *segbase,
			       u64 *fde_count)
{
	struct eh_frame_hdr hdr;
	u8 *enc = (u8 *) &hdr.enc;
	u8 *end = (u8 *) &hdr.data;
	ssize_t r;

	r = dso__data_read_offset(dso, machine, offset,
				  (u8 *) &hdr, sizeof(hdr));
	if (r != sizeof(hdr))
		return -EINVAL;

	/* We dont need eh_frame_ptr, just skip it. */
	dw_read_encoded_value(enc, end, hdr.eh_frame_ptr_enc);

	*fde_count  = dw_read_encoded_value(enc, end, hdr.fde_count_enc);
	*segbase    = offset;
	*table_data = (enc - (u8 *) &hdr) + offset;
	return 0;
}

static int read_unwind_spec_eh_frame(struct dso *dso, struct machine *machine,
				     u64 *table_data, u64 *segbase,
				     u64 *fde_count)
{
	int ret = -EINVAL, fd;
	u64 offset;

	fd = dso__data_fd(dso, machine);
	if (fd < 0)
		return -EINVAL;

	/* Check the .eh_frame section for unwinding info */
	offset = elf_section_offset(fd, ".eh_frame_hdr");

	if (offset)
		ret = unwind_spec_ehframe(dso, machine, offset,
					  table_data, segbase,
					  fde_count);

	return ret;
}

#ifndef NO_LIBUNWIND_DEBUG_FRAME
static int read_unwind_spec_debug_frame(struct dso *dso,
					struct machine *machine, u64 *offset)
{
	int fd = dso__data_fd(dso, machine);

	if (fd < 0)
		return -EINVAL;

	/* Check the .debug_frame section for unwinding info */
	*offset = elf_section_offset(fd, ".debug_frame");

	if (*offset)
		return 0;

	return -EINVAL;
}
#endif

static struct map *find_map(unw_word_t ip, struct unwind_info *ui)
{
	struct addr_location al;

	thread__find_addr_map(ui->thread, ui->machine, PERF_RECORD_MISC_USER,
			      MAP__FUNCTION, ip, &al);
	return al.map;
}

static int
find_proc_info(unw_addr_space_t as, unw_word_t ip, unw_proc_info_t *pi,
	       int need_unwind_info, void *arg)
{
	struct unwind_info *ui = arg;
	struct map *map;
	unw_dyn_info_t di;
	u64 table_data, segbase, fde_count;

	map = find_map(ip, ui);
	if (!map || !map->dso)
		return -EINVAL;

	pr_debug("unwind: find_proc_info dso %s\n", map->dso->name);

	/* Check the .eh_frame section for unwinding info */
	if (!read_unwind_spec_eh_frame(map->dso, ui->machine,
				       &table_data, &segbase, &fde_count)) {
		memset(&di, 0, sizeof(di));
		di.format   = UNW_INFO_FORMAT_REMOTE_TABLE;
		di.start_ip = map->start;
		di.end_ip   = map->end;
		di.u.rti.segbase    = map->start + segbase;
		di.u.rti.table_data = map->start + table_data;
		di.u.rti.table_len  = fde_count * sizeof(struct table_entry)
				      / sizeof(unw_word_t);
		return dwarf_search_unwind_table(as, ip, &di, pi,
						 need_unwind_info, arg);
	}

#ifndef NO_LIBUNWIND_DEBUG_FRAME
	/* Check the .debug_frame section for unwinding info */
	if (!read_unwind_spec_debug_frame(map->dso, ui->machine, &segbase)) {
		memset(&di, 0, sizeof(di));
		if (dwarf_find_debug_frame(0, &di, ip, 0, map->dso->name,
					   map->start, map->end))
			return dwarf_search_unwind_table(as, ip, &di, pi,
							 need_unwind_info, arg);
	}
#endif

	return -EINVAL;
}

static int access_fpreg(unw_addr_space_t __maybe_unused as,
			unw_regnum_t __maybe_unused num,
			unw_fpreg_t __maybe_unused *val,
			int __maybe_unused __write,
			void __maybe_unused *arg)
{
	pr_err("unwind: access_fpreg unsupported\n");
	return -UNW_EINVAL;
}

static int get_dyn_info_list_addr(unw_addr_space_t __maybe_unused as,
				  unw_word_t __maybe_unused *dil_addr,
				  void __maybe_unused *arg)
{
	return -UNW_ENOINFO;
}

static int resume(unw_addr_space_t __maybe_unused as,
		  unw_cursor_t __maybe_unused *cu,
		  void __maybe_unused *arg)
{
	pr_err("unwind: resume unsupported\n");
	return -UNW_EINVAL;
}

static int
get_proc_name(unw_addr_space_t __maybe_unused as,
	      unw_word_t __maybe_unused addr,
		char __maybe_unused *bufp, size_t __maybe_unused buf_len,
		unw_word_t __maybe_unused *offp, void __maybe_unused *arg)
{
	pr_err("unwind: get_proc_name unsupported\n");
	return -UNW_EINVAL;
}

static int access_dso_mem(struct unwind_info *ui, unw_word_t addr,
			  unw_word_t *data)
{
	struct addr_location al;
	ssize_t size;

	thread__find_addr_map(ui->thread, ui->machine, PERF_RECORD_MISC_USER,
			      MAP__FUNCTION, addr, &al);
	if (!al.map) {
		pr_debug("unwind: no map for %lx\n", (unsigned long)addr);
		return -1;
	}

	if (!al.map->dso)
		return -1;

	size = dso__data_read_addr(al.map->dso, al.map, ui->machine,
				   addr, (u8 *) data, sizeof(*data));

	return !(size == sizeof(*data));
}

static int access_mem(unw_addr_space_t __maybe_unused as,
		      unw_word_t addr, unw_word_t *valp,
		      int __write, void *arg)
{
	struct unwind_info *ui = arg;
	struct stack_dump *stack = &ui->sample->user_stack;
	u64 start, end;
	int offset;
	int ret;

	/* Don't support write, probably not needed. */
	if (__write || !stack || !ui->sample->user_regs.regs) {
		*valp = 0;
		return 0;
	}

	ret = perf_reg_value(&start, &ui->sample->user_regs, PERF_REG_SP);
	if (ret)
		return ret;

	end = start + stack->size;

	/* Check overflow. */
	if (addr + sizeof(unw_word_t) < addr)
		return -EINVAL;

	if (addr < start || addr + sizeof(unw_word_t) >= end) {
		ret = access_dso_mem(ui, addr, valp);
		if (ret) {
			pr_debug("unwind: access_mem %p not inside range"
				 " 0x%" PRIx64 "-0x%" PRIx64 "\n",
				 (void *) addr, start, end);
			*valp = 0;
			return ret;
		}
		return 0;
	}

	offset = addr - start;
	*valp  = *(unw_word_t *)&stack->data[offset];
	pr_debug("unwind: access_mem addr %p val %lx, offset %d\n",
		 (void *) addr, (unsigned long)*valp, offset);
	return 0;
}

static int access_reg(unw_addr_space_t __maybe_unused as,
		      unw_regnum_t regnum, unw_word_t *valp,
		      int __write, void *arg)
{
	struct unwind_info *ui = arg;
	int id, ret;
	u64 val;

	/* Don't support write, I suspect we don't need it. */
	if (__write) {
		pr_err("unwind: access_reg w %d\n", regnum);
		return 0;
	}

	if (!ui->sample->user_regs.regs) {
		*valp = 0;
		return 0;
	}

	id = libunwind__arch_reg_id(regnum);
	if (id < 0)
		return -EINVAL;

	ret = perf_reg_value(&val, &ui->sample->user_regs, id);
	if (ret) {
		pr_err("unwind: can't read reg %d\n", regnum);
		return ret;
	}

	*valp = (unw_word_t) val;
	pr_debug("unwind: reg %d, val %lx\n", regnum, (unsigned long)*valp);
	return 0;
}

static void put_unwind_info(unw_addr_space_t __maybe_unused as,
			    unw_proc_info_t *pi __maybe_unused,
			    void *arg __maybe_unused)
{
	pr_debug("unwind: put_unwind_info called\n");
}

static int entry(u64 ip, struct thread *thread, struct machine *machine,
		 unwind_entry_cb_t cb, void *arg)
{
	struct unwind_entry e;
	struct addr_location al;

	thread__find_addr_location(thread, machine,
				   PERF_RECORD_MISC_USER,
				   MAP__FUNCTION, ip, &al);

	e.ip = ip;
	e.map = al.map;
	e.sym = al.sym;

	pr_debug("unwind: %s:ip = 0x%" PRIx64 " (0x%" PRIx64 ")\n",
		 al.sym ? al.sym->name : "''",
		 ip,
		 al.map ? al.map->map_ip(al.map, ip) : (u64) 0);

	return cb(&e, arg);
}

static void display_error(int err)
{
	switch (err) {
	case UNW_EINVAL:
		pr_err("unwind: Only supports local.\n");
		break;
	case UNW_EUNSPEC:
		pr_err("unwind: Unspecified error.\n");
		break;
	case UNW_EBADREG:
		pr_err("unwind: Register unavailable.\n");
		break;
	default:
		break;
	}
}

static unw_accessors_t accessors = {
	.find_proc_info		= find_proc_info,
	.put_unwind_info	= put_unwind_info,
	.get_dyn_info_list_addr	= get_dyn_info_list_addr,
	.access_mem		= access_mem,
	.access_reg		= access_reg,
	.access_fpreg		= access_fpreg,
	.resume			= resume,
	.get_proc_name		= get_proc_name,
};

int unwind__prepare_access(struct thread *thread)
{
	unw_addr_space_t addr_space;

	if (callchain_param.record_mode != CALLCHAIN_DWARF)
		return 0;

	addr_space = unw_create_addr_space(&accessors, 0);
	if (!addr_space) {
		pr_err("unwind: Can't create unwind address space.\n");
		return -ENOMEM;
	}

	unw_set_caching_policy(addr_space, UNW_CACHE_GLOBAL);
	thread__set_priv(thread, addr_space);

	return 0;
}

void unwind__flush_access(struct thread *thread)
{
	unw_addr_space_t addr_space;

	if (callchain_param.record_mode != CALLCHAIN_DWARF)
		return;

	addr_space = thread__priv(thread);
	unw_flush_cache(addr_space, 0, 0);
}

void unwind__finish_access(struct thread *thread)
{
	unw_addr_space_t addr_space;

	if (callchain_param.record_mode != CALLCHAIN_DWARF)
		return;

	addr_space = thread__priv(thread);
	unw_destroy_addr_space(addr_space);
}

static int get_entries(struct unwind_info *ui, unwind_entry_cb_t cb,
		       void *arg, int max_stack)
{
	unw_addr_space_t addr_space;
	unw_cursor_t c;
	int ret;

	addr_space = thread__priv(ui->thread);
	if (addr_space == NULL)
		return -1;

	ret = unw_init_remote(&c, addr_space, ui);
	if (ret)
		display_error(ret);

	while (!ret && (unw_step(&c) > 0) && max_stack--) {
		unw_word_t ip;

		unw_get_reg(&c, UNW_REG_IP, &ip);
		ret = ip ? entry(ip, ui->thread, ui->machine, cb, arg) : 0;
	}

	return ret;
}

int unwind__get_entries(unwind_entry_cb_t cb, void *arg,
			struct machine *machine, struct thread *thread,
			struct perf_sample *data, int max_stack)
{
	u64 ip;
	struct unwind_info ui = {
		.sample       = data,
		.thread       = thread,
		.machine      = machine,
	};
	int ret;

	if (!data->user_regs.regs)
		return -EINVAL;

	ret = perf_reg_value(&ip, &data->user_regs, PERF_REG_IP);
	if (ret)
		return ret;

	ret = entry(ip, thread, machine, cb, arg);
	if (ret)
		return -ENOMEM;

	return --max_stack > 0 ? get_entries(&ui, cb, arg, max_stack) : 0;
}
