/*
 * Use DWARF Debug information to skip unnecessary callchain entries.
 *
 * Copyright (C) 2014 Sukadev Bhattiprolu, IBM Corporation.
 * Copyright (C) 2014 Ulrich Weigand, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <inttypes.h>
#include <dwarf.h>
#include <elfutils/libdwfl.h>

#include "util/thread.h"
#include "util/callchain.h"
#include "util/debug.h"

/*
 * When saving the callchain on Power, the kernel conservatively saves
 * excess entries in the callchain. A few of these entries are needed
 * in some cases but not others. If the unnecessary entries are not
 * ignored, we end up with duplicate arcs in the call-graphs. Use
 * DWARF debug information to skip over any unnecessary callchain
 * entries.
 *
 * See function header for arch_adjust_callchain() below for more details.
 *
 * The libdwfl code in this file is based on code from elfutils
 * (libdwfl/argp-std.c, libdwfl/tests/addrcfi.c, etc).
 */
static char *debuginfo_path;

static const Dwfl_Callbacks offline_callbacks = {
	.debuginfo_path = &debuginfo_path,
	.find_debuginfo = dwfl_standard_find_debuginfo,
	.section_address = dwfl_offline_section_address,
};


/*
 * Use the DWARF expression for the Call-frame-address and determine
 * if return address is in LR and if a new frame was allocated.
 */
static int check_return_reg(int ra_regno, Dwarf_Frame *frame)
{
	Dwarf_Op ops_mem[2];
	Dwarf_Op dummy;
	Dwarf_Op *ops = &dummy;
	size_t nops;
	int result;

	result = dwarf_frame_register(frame, ra_regno, ops_mem, &ops, &nops);
	if (result < 0) {
		pr_debug("dwarf_frame_register() %s\n", dwarf_errmsg(-1));
		return -1;
	}

	/*
	 * Check if return address is on the stack.
	 */
	if (nops != 0 || ops != NULL)
		return 0;

	/*
	 * Return address is in LR. Check if a frame was allocated
	 * but not-yet used.
	 */
	result = dwarf_frame_cfa(frame, &ops, &nops);
	if (result < 0) {
		pr_debug("dwarf_frame_cfa() returns %d, %s\n", result,
					dwarf_errmsg(-1));
		return -1;
	}

	/*
	 * If call frame address is in r1, no new frame was allocated.
	 */
	if (nops == 1 && ops[0].atom == DW_OP_bregx && ops[0].number == 1 &&
				ops[0].number2 == 0)
		return 1;

	/*
	 * A new frame was allocated but has not yet been used.
	 */
	return 2;
}

/*
 * Get the DWARF frame from the .eh_frame section.
 */
static Dwarf_Frame *get_eh_frame(Dwfl_Module *mod, Dwarf_Addr pc)
{
	int		result;
	Dwarf_Addr	bias;
	Dwarf_CFI	*cfi;
	Dwarf_Frame	*frame;

	cfi = dwfl_module_eh_cfi(mod, &bias);
	if (!cfi) {
		pr_debug("%s(): no CFI - %s\n", __func__, dwfl_errmsg(-1));
		return NULL;
	}

	result = dwarf_cfi_addrframe(cfi, pc-bias, &frame);
	if (result) {
		pr_debug("%s(): %s\n", __func__, dwfl_errmsg(-1));
		return NULL;
	}

	return frame;
}

/*
 * Get the DWARF frame from the .debug_frame section.
 */
static Dwarf_Frame *get_dwarf_frame(Dwfl_Module *mod, Dwarf_Addr pc)
{
	Dwarf_CFI       *cfi;
	Dwarf_Addr      bias;
	Dwarf_Frame     *frame;
	int             result;

	cfi = dwfl_module_dwarf_cfi(mod, &bias);
	if (!cfi) {
		pr_debug("%s(): no CFI - %s\n", __func__, dwfl_errmsg(-1));
		return NULL;
	}

	result = dwarf_cfi_addrframe(cfi, pc-bias, &frame);
	if (result) {
		pr_debug("%s(): %s\n", __func__, dwfl_errmsg(-1));
		return NULL;
	}

	return frame;
}

/*
 * Return:
 *	0 if return address for the program counter @pc is on stack
 *	1 if return address is in LR and no new stack frame was allocated
 *	2 if return address is in LR and a new frame was allocated (but not
 *		yet used)
 *	-1 in case of errors
 */
static int check_return_addr(struct dso *dso, u64 map_start, Dwarf_Addr pc)
{
	int		rc = -1;
	Dwfl		*dwfl;
	Dwfl_Module	*mod;
	Dwarf_Frame	*frame;
	int		ra_regno;
	Dwarf_Addr	start = pc;
	Dwarf_Addr	end = pc;
	bool		signalp;
	const char	*exec_file = dso->long_name;

	dwfl = dso->dwfl;

	if (!dwfl) {
		dwfl = dwfl_begin(&offline_callbacks);
		if (!dwfl) {
			pr_debug("dwfl_begin() failed: %s\n", dwarf_errmsg(-1));
			return -1;
		}

		mod = dwfl_report_elf(dwfl, exec_file, exec_file, -1,
						map_start, false);
		if (!mod) {
			pr_debug("dwfl_report_elf() failed %s\n",
						dwarf_errmsg(-1));
			/*
			 * We normally cache the DWARF debug info and never
			 * call dwfl_end(). But to prevent fd leak, free in
			 * case of error.
			 */
			dwfl_end(dwfl);
			goto out;
		}
		dso->dwfl = dwfl;
	}

	mod = dwfl_addrmodule(dwfl, pc);
	if (!mod) {
		pr_debug("dwfl_addrmodule() failed, %s\n", dwarf_errmsg(-1));
		goto out;
	}

	/*
	 * To work with split debug info files (eg: glibc), check both
	 * .eh_frame and .debug_frame sections of the ELF header.
	 */
	frame = get_eh_frame(mod, pc);
	if (!frame) {
		frame = get_dwarf_frame(mod, pc);
		if (!frame)
			goto out;
	}

	ra_regno = dwarf_frame_info(frame, &start, &end, &signalp);
	if (ra_regno < 0) {
		pr_debug("Return address register unavailable: %s\n",
				dwarf_errmsg(-1));
		goto out;
	}

	rc = check_return_reg(ra_regno, frame);

out:
	return rc;
}

/*
 * The callchain saved by the kernel always includes the link register (LR).
 *
 *	0:	PERF_CONTEXT_USER
 *	1:	Program counter (Next instruction pointer)
 *	2:	LR value
 *	3:	Caller's caller
 *	4:	...
 *
 * The value in LR is only needed when it holds a return address. If the
 * return address is on the stack, we should ignore the LR value.
 *
 * Further, when the return address is in the LR, if a new frame was just
 * allocated but the LR was not saved into it, then the LR contains the
 * caller, slot 4: contains the caller's caller and the contents of slot 3:
 * (chain->ips[3]) is undefined and must be ignored.
 *
 * Use DWARF debug information to determine if any entries need to be skipped.
 *
 * Return:
 *	index:	of callchain entry that needs to be ignored (if any)
 *	-1	if no entry needs to be ignored or in case of errors
 */
int arch_skip_callchain_idx(struct thread *thread, struct ip_callchain *chain)
{
	struct addr_location al;
	struct dso *dso = NULL;
	int rc;
	u64 ip;
	u64 skip_slot = -1;

	if (!chain || chain->nr < 3)
		return skip_slot;

	ip = chain->ips[2];

	thread__find_addr_location(thread, PERF_RECORD_MISC_USER,
			MAP__FUNCTION, ip, &al);

	if (al.map)
		dso = al.map->dso;

	if (!dso) {
		pr_debug("%" PRIx64 " dso is NULL\n", ip);
		return skip_slot;
	}

	rc = check_return_addr(dso, al.map->start, ip);

	pr_debug("[DSO %s, sym %s, ip 0x%" PRIx64 "] rc %d\n",
				dso->long_name, al.sym->name, ip, rc);

	if (rc == 0) {
		/*
		 * Return address on stack. Ignore LR value in callchain
		 */
		skip_slot = 2;
	} else if (rc == 2) {
		/*
		 * New frame allocated but return address still in LR.
		 * Ignore the caller's caller entry in callchain.
		 */
		skip_slot = 3;
	}
	return skip_slot;
}
