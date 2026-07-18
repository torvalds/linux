// SPDX-License-Identifier: GPL-2.0
#include "libunwind-arch.h"
#include "../debug.h"
#include "../maps.h"
#include "../thread.h"
#include "../../../arch/s390/include/uapi/asm/perf_regs.h"
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>
#include <elf.h>
#include <errno.h>

#ifdef HAVE_LIBUNWIND_S390X_SUPPORT
#include <libunwind-s390x.h>
#endif

int __get_perf_regnum_for_unw_regnum_s390(int unw_regnum __maybe_unused)
{
#ifndef HAVE_LIBUNWIND_S390X_SUPPORT
	return -EINVAL;
#else
	switch (unw_regnum) {
	case UNW_S390X_R0 ... UNW_S390X_R15:
		return unw_regnum - UNW_S390X_R0 + PERF_REG_S390_R0;
	case UNW_S390X_F0 ... UNW_S390X_F15:
		return unw_regnum - UNW_S390X_F0 + PERF_REG_S390_FP0;
	case UNW_S390X_IP:
		return PERF_REG_S390_PC;
	default:
		pr_err("unwind: invalid reg id %d\n", unw_regnum);
		return -EINVAL;
	}
#endif // HAVE_LIBUNWIND_S390X_SUPPORT
}

void __libunwind_arch__flush_access_s390(struct maps *maps __maybe_unused)
{
#ifdef HAVE_LIBUNWIND_S390X_SUPPORT
	unw_flush_cache(maps__addr_space(maps), 0, 0);
#endif
}

void __libunwind_arch__finish_access_s390(struct maps *maps __maybe_unused)
{
#ifdef HAVE_LIBUNWIND_S390X_SUPPORT
	unw_destroy_addr_space(maps__addr_space(maps));
#endif
}

#ifdef HAVE_LIBUNWIND_S390X_SUPPORT
static int find_proc_info(unw_addr_space_t as, unw_word_t ip, unw_proc_info_t *pi,
			  int need_unwind_info, void *arg)
{
	return __libunwind__find_proc_info(as, ip, pi, need_unwind_info, arg);
}

static void put_unwind_info(unw_addr_space_t __maybe_unused as,
			    unw_proc_info_t *pi __maybe_unused,
			    void *arg __maybe_unused)
{
	pr_debug("unwind: put_unwind_info called\n");
}

static int get_dyn_info_list_addr(unw_addr_space_t __maybe_unused as,
				  unw_word_t __maybe_unused *dil_addr,
				  void __maybe_unused *arg)
{
	return -UNW_ENOINFO;
}

static int access_mem(unw_addr_space_t as, unw_word_t addr, unw_word_t *valp,
		      int __write, void *arg)
{
	return __libunwind__access_mem(as, addr, valp, __write, arg);
}

static int access_reg(unw_addr_space_t as, unw_regnum_t regnum, unw_word_t *valp,
		      int __write, void *arg)
{
	return __libunwind__access_reg(as, regnum, valp, __write, arg);
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

static int resume(unw_addr_space_t __maybe_unused as,
		  unw_cursor_t __maybe_unused *cu,
		  void __maybe_unused *arg)
{
	pr_err("unwind: resume unsupported\n");
	return -UNW_EINVAL;
}

static int get_proc_name(unw_addr_space_t __maybe_unused as,
			 unw_word_t __maybe_unused addr,
			 char __maybe_unused *bufp, size_t __maybe_unused buf_len,
			 unw_word_t __maybe_unused *offp, void __maybe_unused *arg)
{
	pr_err("unwind: get_proc_name unsupported\n");
	return -UNW_EINVAL;
}
#endif

void *__libunwind_arch__create_addr_space_s390(void)
{
#ifdef HAVE_LIBUNWIND_S390X_SUPPORT
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
	unw_addr_space_t addr_space;

	addr_space = unw_create_addr_space(&accessors, /*byte_order=*/0);
	unw_set_caching_policy(addr_space, UNW_CACHE_GLOBAL);
	return addr_space;
#else
	return NULL;
#endif
}

#ifdef HAVE_LIBUNWIND_S390X_SUPPORT
extern int UNW_OBJ(dwarf_search_unwind_table) (unw_addr_space_t as,
					       unw_word_t ip,
					       unw_dyn_info_t *di,
					       unw_proc_info_t *pi,
					       int need_unwind_info, void *arg);
#define dwarf_search_unwind_table UNW_OBJ(dwarf_search_unwind_table)
#endif

int __libunwind_arch__dwarf_search_unwind_table_s390(void *as __maybe_unused,
						       uint64_t ip __maybe_unused,
						       struct libarch_unwind__dyn_info *_di __maybe_unused,
						       void *pi __maybe_unused,
						       int need_unwind_info __maybe_unused,
						       void *arg __maybe_unused)
{
#ifdef HAVE_LIBUNWIND_S390X_SUPPORT
	unw_dyn_info_t di = {
		.format     = UNW_INFO_FORMAT_REMOTE_TABLE,
		.start_ip   = _di->start_ip,
		.end_ip     = _di->end_ip,
		.u = {
			.rti = {
				.segbase    = _di->segbase,
				.table_data = _di->table_data,
				.table_len  = _di->table_len,
			},
		},
	};
	int ret = dwarf_search_unwind_table(as, ip, &di, pi, need_unwind_info, arg);

	_di->start_ip = di.start_ip;
	_di->end_ip = di.end_ip;
	_di->segbase = di.u.rti.segbase;
	_di->table_data = di.u.rti.table_data;
	_di->table_len = di.u.rti.table_len;
	return ret;
#else
	return -EINVAL;
#endif
}

#if defined(HAVE_LIBUNWIND_S390X_SUPPORT) && !defined(NO_LIBUNWIND_DEBUG_FRAME_S390X)
extern int UNW_OBJ(dwarf_find_debug_frame) (int found, unw_dyn_info_t *di_debug,
					    unw_word_t ip,
					    unw_word_t segbase,
					    const char *obj_name, unw_word_t start,
					    unw_word_t end);
#define dwarf_find_debug_frame UNW_OBJ(dwarf_find_debug_frame)
#endif

int __libunwind_arch__dwarf_find_debug_frame_s390(int found __maybe_unused,
						 struct libarch_unwind__dyn_info *_di __maybe_unused,
						 uint64_t ip __maybe_unused,
						 uint64_t segbase __maybe_unused,
						 const char *obj_name __maybe_unused,
						 uint64_t start __maybe_unused,
						 uint64_t end __maybe_unused)
{
#if defined(HAVE_LIBUNWIND_S390X_SUPPORT) && !defined(NO_LIBUNWIND_DEBUG_FRAME_S390X)
	unw_dyn_info_t di = {
		.format     = UNW_INFO_FORMAT_REMOTE_TABLE,
		.start_ip   = _di->start_ip,
		.end_ip     = _di->end_ip,
		.u = {
			.rti = {
				.segbase    = _di->segbase,
				.table_data = _di->table_data,
				.table_len  = _di->table_len,
			},
		},
	};
	int ret = dwarf_find_debug_frame(found, &di, ip, segbase, obj_name, start, end);

	_di->start_ip = di.start_ip;
	_di->end_ip = di.end_ip;
	_di->segbase = di.u.ti.segbase;
	_di->table_data = di.u.ti.table_data;
	_di->table_len = di.u.ti.table_len;
	return ret;
#else
	return -EINVAL;
#endif
}

struct unwind_info *__libunwind_arch_unwind_info__new_s390(struct thread *thread __maybe_unused,
							     struct perf_sample *sample  __maybe_unused,
							     int max_stack __maybe_unused,
							     bool best_effort  __maybe_unused,
							     uint64_t first_ip  __maybe_unused)
{
#ifdef HAVE_LIBUNWIND_S390X_SUPPORT
	struct arch_unwind_info {
		struct unwind_info ui;
		unw_cursor_t _cursor;
		uint64_t _ips[];
	};

	struct maps *maps = thread__maps(thread);
	void *addr_space = maps__addr_space(maps);
	struct arch_unwind_info *ui;
	int ret;

	if (addr_space == NULL)
		return NULL;

	ui = zalloc(sizeof(*ui) + sizeof(ui->_ips[0]) * max_stack);
	if (!ui)
		return NULL;

	ui->ui.machine = maps__machine(maps);
	ui->ui.thread = thread;
	ui->ui.sample = sample;
	ui->ui.cursor = &ui->_cursor;
	ui->ui.ips = &ui->_ips[0];
	ui->ui.ips[0] = first_ip;
	ui->ui.cur_ip = 1;
	ui->ui.max_ips = max_stack;
	ui->ui.unw_word_t_size = sizeof(unw_word_t);
	ui->ui.e_machine = EM_S390;
	ui->ui.best_effort = best_effort;

	ret = unw_init_remote(&ui->_cursor, addr_space, &ui->ui);
	if (ret) {
		if (!best_effort)
			pr_err("libunwind: %s\n", unw_strerror(ret));
		free(ui);
		return NULL;
	}

	return &ui->ui;
#else
	return NULL;
#endif
}

int __libunwind_arch__unwind_step_s390(struct unwind_info *ui __maybe_unused)
{
#ifdef HAVE_LIBUNWIND_S390X_SUPPORT
	int ret;

	if (ui->cur_ip >= ui->max_ips)
		return 0;

	ret = unw_step(ui->cursor);
	if (ret > 0) {
		uint64_t ip;

		unw_get_reg(ui->cursor, UNW_REG_IP, &ip);

		if (unw_is_signal_frame(ui->cursor) <= 0) {
			/*
			 * Decrement the IP for any non-activation frames. This
			 * is required to properly find the srcline for caller
			 * frames.  See also the documentation for
			 * dwfl_frame_pc(), which this code tries to replicate.
			 */
			--ip;
		}
		ui->ips[ui->cur_ip++] = ip;
	}
	return ret;
#else
	return -EINVAL;
#endif
}
