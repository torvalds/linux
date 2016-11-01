#include <linux/compiler.h>
#include <elfutils/libdw.h>
#include <elfutils/libdwfl.h>
#include <inttypes.h>
#include <errno.h>
#include "debug.h"
#include "unwind.h"
#include "unwind-libdw.h"
#include "machine.h"
#include "thread.h"
#include <linux/types.h>
#include "event.h"
#include "perf_regs.h"
#include "callchain.h"

static char *debuginfo_path;

static const Dwfl_Callbacks offline_callbacks = {
	.find_debuginfo		= dwfl_standard_find_debuginfo,
	.debuginfo_path		= &debuginfo_path,
	.section_address	= dwfl_offline_section_address,
};

static int __report_module(struct addr_location *al, u64 ip,
			    struct unwind_info *ui)
{
	Dwfl_Module *mod;
	struct dso *dso = NULL;

	thread__find_addr_location(ui->thread,
				   PERF_RECORD_MISC_USER,
				   MAP__FUNCTION, ip, al);

	if (al->map)
		dso = al->map->dso;

	if (!dso)
		return 0;

	mod = dwfl_addrmodule(ui->dwfl, ip);
	if (!mod)
		mod = dwfl_report_elf(ui->dwfl, dso->short_name,
				      dso->long_name, -1, al->map->start,
				      false);

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

	e->ip  = al.addr;
	e->map = al.map;
	e->sym = al.sym;

	pr_debug("unwind: %s:ip = 0x%" PRIx64 " (0x%" PRIx64 ")\n",
		 al.sym ? al.sym->name : "''",
		 ip,
		 al.map ? al.map->map_ip(al.map, ip) : (u64) 0);
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

	thread__find_addr_map(ui->thread, PERF_RECORD_MISC_USER,
			      MAP__FUNCTION, addr, &al);
	if (!al.map) {
		/*
		 * We've seen cases (softice) where DWARF unwinder went
		 * through non executable mmaps, which we need to lookup
		 * in MAP__VARIABLE tree.
		 */
		thread__find_addr_map(ui->thread, PERF_RECORD_MISC_USER,
				      MAP__VARIABLE, addr, &al);
	}

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

	if (!dwfl_frame_pc(state, &pc, NULL)) {
		pr_err("%s", dwfl_errmsg(-1));
		return DWARF_CB_ABORT;
	}

	return entry(pc, ui) || !(--ui->max_stack) ?
	       DWARF_CB_ABORT : DWARF_CB_OK;
}

int unwind__get_entries(unwind_entry_cb_t cb, void *arg,
			struct thread *thread,
			struct perf_sample *data,
			int max_stack)
{
	struct unwind_info *ui, ui_buf = {
		.sample		= data,
		.thread		= thread,
		.machine	= thread->mg->machine,
		.cb		= cb,
		.arg		= arg,
		.max_stack	= max_stack,
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

	if (!dwfl_attach_state(ui->dwfl, EM_NONE, thread->tid, &callbacks, ui))
		goto out;

	err = dwfl_getthread_frames(ui->dwfl, thread->tid, frame_callback, ui);

	if (err && !ui->max_stack)
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
