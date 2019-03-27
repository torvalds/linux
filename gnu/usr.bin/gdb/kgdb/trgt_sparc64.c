/*
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
#include <sparc-tdep.h>
#include <sparc64-tdep.h>

#include "kgdb.h"

CORE_ADDR
kgdb_trgt_core_pcb(u_int cpuid)
{
	return (kgdb_trgt_stop_pcb(cpuid, sizeof(struct pcb)));
}

void
kgdb_trgt_fetch_registers(int regno __unused)
{
	struct kthr *kt;
	struct pcb pcb;

	kt = kgdb_thr_lookup_tid(ptid_get_pid(inferior_ptid));
	if (kt == NULL)
		return;
	if (kvm_read(kvm, kt->pcb, &pcb, sizeof(pcb)) != sizeof(pcb)) {
		warnx("kvm_read: %s", kvm_geterr(kvm));
		memset(&pcb, 0, sizeof(pcb));
	}

	supply_register(SPARC_SP_REGNUM, (char *)&pcb.pcb_sp);
	sparc_supply_rwindow(current_regcache, pcb.pcb_sp, -1);
	supply_register(SPARC64_PC_REGNUM, (char *)&pcb.pcb_pc);
	pcb.pcb_pc += 4;
	supply_register(SPARC64_NPC_REGNUM, (char *)&pcb.pcb_pc);
}

void
kgdb_trgt_store_registers(int regno __unused)
{
	fprintf_unfiltered(gdb_stderr, "XXX: %s\n", __func__);
}

void
kgdb_trgt_new_objfile(struct objfile *objfile)
{
}

struct kgdb_frame_cache {
	CORE_ADDR	pc;
	CORE_ADDR	sp;
	CORE_ADDR	fp;
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
		frame_unwind_register(next_frame, SPARC_SP_REGNUM, buf);
		cache->sp = extract_unsigned_integer(buf,
		    register_size(current_gdbarch, SPARC_SP_REGNUM));
		frame_unwind_register(next_frame, SPARC_FP_REGNUM, buf);
		cache->fp = extract_unsigned_integer(buf,
		    register_size(current_gdbarch, SPARC_FP_REGNUM));
		cache->fp += BIAS - sizeof(struct trapframe);
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
kgdb_trgt_trapframe_prev_register(struct frame_info *next_frame,
    void **this_cache, int regnum, int *optimizedp, enum lval_type *lvalp,
    CORE_ADDR *addrp, int *realnump, void *valuep)
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

	cache = kgdb_trgt_frame_cache(next_frame, this_cache);

	switch (regnum) {
	case SPARC_SP_REGNUM:
		ofs = offsetof(struct trapframe, tf_sp);
		break;
	case SPARC64_PC_REGNUM:
		ofs = offsetof(struct trapframe, tf_tpc);
		break;
	case SPARC64_NPC_REGNUM:
		ofs = offsetof(struct trapframe, tf_tnpc);
		break;
	case SPARC_O0_REGNUM:
	case SPARC_O1_REGNUM:
	case SPARC_O2_REGNUM:
	case SPARC_O3_REGNUM:
	case SPARC_O4_REGNUM:
	case SPARC_O5_REGNUM:
	case SPARC_O7_REGNUM:
		ofs = offsetof(struct trapframe, tf_out) +
		    (regnum - SPARC_O0_REGNUM) * 8;
		break;
	default:
		if (regnum >= SPARC_L0_REGNUM && regnum <= SPARC_I7_REGNUM) {
			ofs = (regnum - SPARC_L0_REGNUM) * 8;
			*addrp = cache->sp + BIAS + ofs;
			*lvalp = lval_memory;
			target_read_memory(*addrp, valuep, regsz);
		}
		return;
	}

	*addrp = cache->fp + ofs;
	*lvalp = lval_memory;
	target_read_memory(*addrp, valuep, regsz);
}

static const struct frame_unwind kgdb_trgt_trapframe_unwind = {
        UNKNOWN_FRAME,
        &kgdb_trgt_trapframe_this_id,
        &kgdb_trgt_trapframe_prev_register
};

const struct frame_unwind *
kgdb_trgt_trapframe_sniffer(struct frame_info *next_frame)
{
	char *pname;
	CORE_ADDR pc;

	pc = frame_func_unwind(next_frame);
	pname = NULL;
	find_pc_partial_function(pc, &pname, NULL, NULL);
	if (pname == NULL)
		return (NULL);
	if (strcmp(pname, "tl0_intr") == 0 ||
	    strcmp(pname, "tl0_trap") == 0 ||
	    strcmp(pname, "tl1_intr") == 0 ||
	    strcmp(pname, "tl1_trap") == 0)
		return (&kgdb_trgt_trapframe_unwind);
	/* printf("%s: %lx =%s\n", __func__, pc, pname); */
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
