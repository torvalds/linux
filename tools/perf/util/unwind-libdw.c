// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <elfutils/libdw.h>
#include <elfutils/libdwfl.h>
#include <inttypes.h>
#include <errno.h>
#include "debug.h"
#include "dso.h"
#include "unwind.h"
#include "unwind-libdw.h"
#include "machine.h"
#include "map.h"
#include "symbol.h"
#include "thread.h"
#include <linux/types.h>
#include <linux/zalloc.h>
#include "event.h"
#include "perf_regs.h"
#include "callchain.h"

static char *debuginfo_path;

static int __find_debuginfo(Dwfl_Module *mod __maybe_unused, void **userdata,
			    const char *modname __maybe_unused, Dwarf_Addr base __maybe_unused,
			    const char *file_name, const char *debuglink_file __maybe_unused,
			    GElf_Word debuglink_crc __maybe_unused, char **debuginfo_file_name)
{
	const struct dso *dso = *userdata;

	assert(dso);
	if (dso->symsrc_filename && strcmp (file_name, dso->symsrc_filename))
		*debuginfo_file_name = strdup(dso->symsrc_filename);
	return -1;
}

static const Dwfl_Callbacks offline_callbacks = {
	.find_debuginfo		= __find_debuginfo,
	.debuginfo_path		= &debuginfo_path,
	.section_address	= dwfl_offline_section_address,
	// .find_elf is not set as we use dwfl_report_elf() instead.
};

static int __report_module(struct addr_location *al, u64 ip,
			    struct unwind_info *ui)
{
	Dwfl_Module *mod;
	struct dso *dso = NULL;
	/*
	 * Some callers will use al->sym, so we can't just use the
	 * cheaper thread__find_map() here.
	 */
	thread__find_symbol(ui->thread, PERF_RECORD_MISC_USER, ip, al);

	if (al->map)
		dso = map__dso(al->map);

	if (!dso)
		return 0;

	mod = dwfl_addrmodule(ui->dwfl, ip);
	if (mod) {
		Dwarf_Addr s;

		dwfl_module_info(mod, NULL, &s, NULL, NULL, NULL, NULL, NULL);
		if (s != map__start(al->map) - map__pgoff(al->map))
			mod = 0;
	}

	if (!mod)
		mod = dwfl_report_elf(ui->dwfl, dso->short_name, dso->long_name, -1,
				      map__start(al->map) - map__pgoff(al->map), false);
	if (!mod) {
		char filename[PATH_MAX];

		if (dso__build_id_filename(dso, filename, sizeof(filename), false))
			mod = dwfl_report_elf(ui->dwfl, dso->short_name, filename, -1,
					      map__start(al->map) - map__pgoff(al->map), false);
	}

	if (mod) {
		void **userdatap;

		dwfl_module_info(mod, &userdatap, NULL, NULL, NULL, NULL, NULL, NULL);
		*userdatap = dso;
	}

	return mod && dwfl_addrmodule(ui->dwfl, ip) == mod ? 0 : -1;
}

static int report_module(u64 ip, struct unwind_info *ui)
{
	struct addr_location al;

	return __report_module(&al, ip, ui);
}

/*
 * Store all entries within entries array,
 * we will process it after we finish unwind.
 */
static int entry(u64 ip, struct unwind_info *ui)

{
	struct unwind_entry *e = &ui->entries[ui->idx++];
	struct addr_location al;

	if (__report_module(&al, ip, ui))
		return -1;

	e->ip	  = ip;
	e->ms.maps = al.maps;
	e->ms.map = al.map;
	e->ms.sym = al.sym;

	pr_debug("unwind: %s:ip = 0x%" PRIx64 " (0x%" PRIx64 ")\n",
		 al.sym ? al.sym->name : "''",
		 ip,
		 al.map ? map__map_ip(al.map, ip) : (u64) 0);
	return 0;
}

static pid_t next_thread(Dwfl *dwfl, void *arg, void **thread_argp)
{
	/* We want only single thread to be processed. */
	if (*thread_argp != NULL)
		return 0;

	*thread_argp = arg;
	return dwfl_pid(dwfl);
}

static int access_dso_mem(struct unwind_info *ui, Dwarf_Addr addr,
			  Dwarf_Word *data)
{
	struct addr_location al;
	ssize_t size;
	struct dso *dso;

	if (!thread__find_map(ui->thread, PERF_RECORD_MISC_USER, addr, &al)) {
		pr_debug("unwind: no map for %lx\n", (unsigned long)addr);
		return -1;
	}
	dso = map__dso(al.map);
	if (!dso)
		return -1;

	size = dso__data_read_addr(dso, al.map, ui->machine, addr, (u8 *) data, sizeof(*data));

	return !(size == sizeof(*data));
}

static bool memory_read(Dwfl *dwfl __maybe_unused, Dwarf_Addr addr, Dwarf_Word *result,
			void *arg)
{
	struct unwind_info *ui = arg;
	struct stack_dump *stack = &ui->sample->user_stack;
	u64 start, end;
	int offset;
	int ret;

	ret = perf_reg_value(&start, &ui->sample->user_regs, PERF_REG_SP);
	if (ret)
		return false;

	end = start + stack->size;

	/* Check overflow. */
	if (addr + sizeof(Dwarf_Word) < addr)
		return false;

	if (addr < start || addr + sizeof(Dwarf_Word) > end) {
		ret = access_dso_mem(ui, addr, result);
		if (ret) {
			pr_debug("unwind: access_mem 0x%" PRIx64 " not inside range"
				 " 0x%" PRIx64 "-0x%" PRIx64 "\n",
				addr, start, end);
			return false;
		}
		return true;
	}

	offset  = addr - start;
	*result = *(Dwarf_Word *)&stack->data[offset];
	pr_debug("unwind: access_mem addr 0x%" PRIx64 ", val %lx, offset %d\n",
		 addr, (unsigned long)*result, offset);
	return true;
}

static const Dwfl_Thread_Callbacks callbacks = {
	.next_thread		= next_thread,
	.memory_read		= memory_read,
	.set_initial_registers	= libdw__arch_set_initial_registers,
};

static int
frame_callback(Dwfl_Frame *state, void *arg)
{
	struct unwind_info *ui = arg;
	Dwarf_Addr pc;
	bool isactivation;

	if (!dwfl_frame_pc(state, &pc, NULL)) {
		if (!ui->best_effort)
			pr_err("%s", dwfl_errmsg(-1));
		return DWARF_CB_ABORT;
	}

	// report the module before we query for isactivation
	report_module(pc, ui);

	if (!dwfl_frame_pc(state, &pc, &isactivation)) {
		if (!ui->best_effort)
			pr_err("%s", dwfl_errmsg(-1));
		return DWARF_CB_ABORT;
	}

	if (!isactivation)
		--pc;

	return entry(pc, ui) || !(--ui->max_stack) ?
	       DWARF_CB_ABORT : DWARF_CB_OK;
}

int unwind__get_entries(unwind_entry_cb_t cb, void *arg,
			struct thread *thread,
			struct perf_sample *data,
			int max_stack,
			bool best_effort)
{
	struct unwind_info *ui, ui_buf = {
		.sample		= data,
		.thread		= thread,
		.machine	= RC_CHK_ACCESS(thread->maps)->machine,
		.cb		= cb,
		.arg		= arg,
		.max_stack	= max_stack,
		.best_effort    = best_effort
	};
	Dwarf_Word ip;
	int err = -EINVAL, i;

	if (!data->user_regs.regs)
		return -EINVAL;

	ui = zalloc(sizeof(ui_buf) + sizeof(ui_buf.entries[0]) * max_stack);
	if (!ui)
		return -ENOMEM;

	*ui = ui_buf;

	ui->dwfl = dwfl_begin(&offline_callbacks);
	if (!ui->dwfl)
		goto out;

	err = perf_reg_value(&ip, &data->user_regs, PERF_REG_IP);
	if (err)
		goto out;

	err = report_module(ip, ui);
	if (err)
		goto out;

	err = !dwfl_attach_state(ui->dwfl, EM_NONE, thread->tid, &callbacks, ui);
	if (err)
		goto out;

	err = dwfl_getthread_frames(ui->dwfl, thread->tid, frame_callback, ui);

	if (err && ui->max_stack != max_stack)
		err = 0;

	/*
	 * Display what we got based on the order setup.
	 */
	for (i = 0; i < ui->idx && !err; i++) {
		int j = i;

		if (callchain_param.order == ORDER_CALLER)
			j = ui->idx - i - 1;

		err = ui->entries[j].ip ? ui->cb(&ui->entries[j], ui->arg) : 0;
	}

 out:
	if (err)
		pr_debug("unwind: failed with '%s'\n", dwfl_errmsg(-1));

	dwfl_end(ui->dwfl);
	free(ui);
	return 0;
}
