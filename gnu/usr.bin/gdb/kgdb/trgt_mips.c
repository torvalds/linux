/*
 * Copyright (c) 2007 Juniper Networks, Inc.
 * Copyright (c) 2004 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * from: src/gnu/usr.bin/gdb/kgdb/trgt_alpha.c,v 1.2.2.1 2005/09/15 05:32:10 marcel
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <machine/asm.h>
#include <machine/pcb.h>
#include <machine/frame.h>
#include <err.h>
#include <kvm.h>
#include <string.h>

#include <defs.h>
#include <target.h>
#include <gdbthread.h>
#include <inferior.h>
#include <regcache.h>
#include <frame-unwind.h>
#include <mips-tdep.h>

#ifndef	CROSS_DEBUGGER
#include <machine/pcb.h>
#endif

#include "kgdb.h"

CORE_ADDR
kgdb_trgt_core_pcb(u_int cpuid)
{
	return (kgdb_trgt_stop_pcb(cpuid, sizeof(struct pcb)));
}

void
kgdb_trgt_fetch_registers(int regno __unused)
{
#ifndef	CROSS_DEBUGGER
	struct kthr *kt;
	struct pcb pcb;

	kt = kgdb_thr_lookup_tid(ptid_get_pid(inferior_ptid));
	if (kt == NULL)
		return;
	if (kvm_read(kvm, kt->pcb, &pcb, sizeof(pcb)) != sizeof(pcb)) {
		warnx("kvm_read: %s", kvm_geterr(kvm));
		memset(&pcb, 0, sizeof(pcb));
	}

	supply_register(MIPS_S0_REGNUM, (char *)&pcb.pcb_context[PCB_REG_S0]);
	supply_register(MIPS_S1_REGNUM, (char *)&pcb.pcb_context[PCB_REG_S1]);
	supply_register(MIPS_S2_REGNUM, (char *)&pcb.pcb_context[PCB_REG_S2]);
	supply_register(MIPS_S3_REGNUM, (char *)&pcb.pcb_context[PCB_REG_S3]);
	supply_register(MIPS_S4_REGNUM, (char *)&pcb.pcb_context[PCB_REG_S4]);
	supply_register(MIPS_S5_REGNUM, (char *)&pcb.pcb_context[PCB_REG_S5]);
	supply_register(MIPS_S6_REGNUM, (char *)&pcb.pcb_context[PCB_REG_S6]);
	supply_register(MIPS_S7_REGNUM, (char *)&pcb.pcb_context[PCB_REG_S7]);
	supply_register(MIPS_SP_REGNUM, (char *)&pcb.pcb_context[PCB_REG_SP]);
	supply_register(MIPS_FP_REGNUM, (char *)&pcb.pcb_context[PCB_REG_GP]);
	supply_register(MIPS_RA_REGNUM, (char *)&pcb.pcb_context[PCB_REG_RA]);
	supply_register(MIPS_EMBED_PC_REGNUM, (char *)&pcb.pcb_context[PCB_REG_PC]);
#endif
}

void
kgdb_trgt_store_registers(int regno __unused)
{

	fprintf_unfiltered(gdb_stderr, "Unimplemented function: %s\n", __func__);
}

void
kgdb_trgt_new_objfile(struct objfile *objfile)
{
}

#ifndef CROSS_DEBUGGER
struct kgdb_frame_cache {
	CORE_ADDR	pc;
	CORE_ADDR	sp;
};

static int kgdb_trgt_frame_offset[] = {
	offsetof(struct trapframe, zero),
	offsetof(struct trapframe, ast),
	offsetof(struct trapframe, v0),
	offsetof(struct trapframe, v1),
	offsetof(struct trapframe, a0),
	offsetof(struct trapframe, a1),
	offsetof(struct trapframe, a2),
	offsetof(struct trapframe, a3),
#if defined(__mips_n32) || defined(__mips_n64)
	offsetof(struct trapframe, a4),
	offsetof(struct trapframe, a5),
	offsetof(struct trapframe, a6),
	offsetof(struct trapframe, a7),
	offsetof(struct trapframe, t0),
	offsetof(struct trapframe, t1),
	offsetof(struct trapframe, t2),
	offsetof(struct trapframe, t3),
#else
	offsetof(struct trapframe, t0),
	offsetof(struct trapframe, t1),
	offsetof(struct trapframe, t2),
	offsetof(struct trapframe, t3),
	offsetof(struct trapframe, t4),
	offsetof(struct trapframe, t5),
	offsetof(struct trapframe, t6),
	offsetof(struct trapframe, t7),
#endif
	offsetof(struct trapframe, s0),
	offsetof(struct trapframe, s1),
	offsetof(struct trapframe, s2),
	offsetof(struct trapframe, s3),
	offsetof(struct trapframe, s4),
	offsetof(struct trapframe, s5),
	offsetof(struct trapframe, s6),
	offsetof(struct trapframe, s7),
	offsetof(struct trapframe, t8),
	offsetof(struct trapframe, t9),
	offsetof(struct trapframe, k0),
	offsetof(struct trapframe, k1),
	offsetof(struct trapframe, gp),
	offsetof(struct trapframe, sp),
	offsetof(struct trapframe, s8),
	offsetof(struct trapframe, ra),
};

static struct kgdb_frame_cache *
kgdb_trgt_frame_cache(struct frame_info *next_frame, void **this_cache)
{
	char buf[MAX_REGISTER_SIZE];
	struct kgdb_frame_cache *cache;

	cache = *this_cache;
	if (cache == NULL) {
		cache = FRAME_OBSTACK_ZALLOC(struct kgdb_frame_cache);
		*this_cache = cache;
		cache->pc = frame_func_unwind(next_frame);
		frame_unwind_register(next_frame, SP_REGNUM, buf);
		cache->sp = extract_unsigned_integer(buf,
		    register_size(current_gdbarch, SP_REGNUM));
	}
	return (cache);
}

static void
kgdb_trgt_trapframe_this_id(struct frame_info *next_frame, void **this_cache,
    struct frame_id *this_id)
{
	struct kgdb_frame_cache *cache;

	cache = kgdb_trgt_frame_cache(next_frame, this_cache);
	*this_id = frame_id_build(cache->sp, cache->pc);
}

static void
kgdb_trgt_trapframe_prev_register(struct frame_info *next_frame __unused,
    void **this_cache __unused, int regnum __unused, int *optimizedp __unused,
    enum lval_type *lvalp __unused, CORE_ADDR *addrp __unused,
    int *realnump __unused, void *valuep __unused)
{
	char dummy_valuep[MAX_REGISTER_SIZE];
	struct kgdb_frame_cache *cache;
	int ofs, regsz;

	regsz = register_size(current_gdbarch, regnum);

	if (valuep == NULL)
		valuep = dummy_valuep;
	memset(valuep, 0, regsz);
	*optimizedp = 0;
	*addrp = 0;
	*lvalp = not_lval;
	*realnump = -1;

	ofs = (regnum >= 0 && regnum <= MIPS_RA_REGNUM) ?
	    kgdb_trgt_frame_offset[regnum] : -1;
	if (ofs == -1)
		return;

	cache = kgdb_trgt_frame_cache(next_frame, this_cache);
	*addrp = cache->sp + ofs * 8;
	*lvalp = lval_memory;
	target_read_memory(*addrp, valuep, regsz);
}

static const struct frame_unwind kgdb_trgt_trapframe_unwind = {
	UNKNOWN_FRAME,
	&kgdb_trgt_trapframe_this_id,
	&kgdb_trgt_trapframe_prev_register
};
#endif

const struct frame_unwind *
kgdb_trgt_trapframe_sniffer(struct frame_info *next_frame)
{
#ifndef CROSS_DEBUGGER
	char *pname;
	CORE_ADDR pc;

	pc = frame_pc_unwind(next_frame);
	pname = NULL;
	find_pc_partial_function(pc, &pname, NULL, NULL);
	if (pname == NULL)
		return (NULL);
	if ((strcmp(pname, "MipsKernIntr") == 0) ||
	    (strcmp(pname, "MipsKernGenException") == 0) ||
	    (strcmp(pname, "MipsUserIntr") == 0) ||
	    (strcmp(pname, "MipsUserGenException") == 0))
		return (&kgdb_trgt_trapframe_unwind);
#endif
	return (NULL);
}

/*
 * This function ensures, that the PC is inside the
 * function section which is understood by GDB.
 *
 * Return 0 when fixup is necessary, -1 otherwise.
 */
int
kgdb_trgt_pc_fixup(CORE_ADDR *pc __unused)
{

	return (-1);
}
