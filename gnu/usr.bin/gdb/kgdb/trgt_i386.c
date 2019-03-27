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

#include <sys/param.h>
#include <sys/proc.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/pcb.h>
#include <machine/frame.h>
#include <machine/segments.h>
#include <machine/tss.h>
#include <err.h>
#include <kvm.h>
#include <string.h>

#include <defs.h>
#include <target.h>
#include <gdbthread.h>
#include <inferior.h>
#include <regcache.h>
#include <frame-unwind.h>
#include <i386-tdep.h>

#include "kgdb.h"

static int ofs_fix;

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
	supply_register(I386_EBX_REGNUM, (char *)&pcb.pcb_ebx);
	supply_register(I386_ESP_REGNUM, (char *)&pcb.pcb_esp);
	supply_register(I386_EBP_REGNUM, (char *)&pcb.pcb_ebp);
	supply_register(I386_ESI_REGNUM, (char *)&pcb.pcb_esi);
	supply_register(I386_EDI_REGNUM, (char *)&pcb.pcb_edi);
	supply_register(I386_EIP_REGNUM, (char *)&pcb.pcb_eip);
}

void
kgdb_trgt_store_registers(int regno __unused)
{
	fprintf_unfiltered(gdb_stderr, "XXX: %s\n", __func__);
}

void
kgdb_trgt_new_objfile(struct objfile *objfile)
{

	/*
	 * In revision 1.117 of i386/i386/exception.S trap handlers
	 * were changed to pass trapframes by reference rather than
	 * by value.  Detect this by seeing if the first instruction
	 * at the 'calltrap' label is a "push %esp" which has the
	 * opcode 0x54.
	 */
	if (kgdb_parse("((char *)calltrap)[0]") == 0x54)
		ofs_fix = 4;
	else
		ofs_fix = 0;
}

struct kgdb_tss_cache {
	CORE_ADDR	pc;
	CORE_ADDR	sp;
	CORE_ADDR	tss;
};

static int kgdb_trgt_tss_offset[15] = {
	offsetof(struct i386tss, tss_eax),
	offsetof(struct i386tss, tss_ecx),
	offsetof(struct i386tss, tss_edx),
	offsetof(struct i386tss, tss_ebx),
	offsetof(struct i386tss, tss_esp),
	offsetof(struct i386tss, tss_ebp),
	offsetof(struct i386tss, tss_esi),
	offsetof(struct i386tss, tss_edi),
	offsetof(struct i386tss, tss_eip),
	offsetof(struct i386tss, tss_eflags),
	offsetof(struct i386tss, tss_cs),
	offsetof(struct i386tss, tss_ss),
	offsetof(struct i386tss, tss_ds),
	offsetof(struct i386tss, tss_es),
	offsetof(struct i386tss, tss_fs)
};

/*
 * If the current thread is executing on a CPU, fetch the common_tss
 * for that CPU.
 *
 * This is painful because 'struct pcpu' is variant sized, so we can't
 * use it.  Instead, we lookup the GDT selector for this CPU and
 * extract the base of the TSS from there.
 */
static CORE_ADDR
kgdb_trgt_fetch_tss(void)
{
	struct kthr *kt;
	struct segment_descriptor sd;
	uintptr_t addr, cpu0prvpage, tss;

	kt = kgdb_thr_lookup_tid(ptid_get_pid(inferior_ptid));
	if (kt == NULL || kt->cpu == NOCPU || kt->cpu < 0)
		return (0);

	addr = kgdb_lookup("gdt");
	if (addr == 0)
		return (0);
	addr += (kt->cpu * NGDT + GPROC0_SEL) * sizeof(sd);
	if (kvm_read(kvm, addr, &sd, sizeof(sd)) != sizeof(sd)) {
		warnx("kvm_read: %s", kvm_geterr(kvm));
		return (0);
	}
	if (sd.sd_type != SDT_SYS386BSY) {
		warnx("descriptor is not a busy TSS");
		return (0);
	}
	tss = sd.sd_hibase << 24 | sd.sd_lobase;

	/*
	 * In SMP kernels, the TSS is stored as part of the per-CPU
	 * data.  On older kernels, the CPU0's private page
	 * is stored at an address that isn't mapped in minidumps.
	 * However, the data is mapped at the alternate cpu0prvpage
	 * address.  Thus, if the TSS is at the invalid address,
	 * change it to be relative to cpu0prvpage instead.
	 */ 
	if (trunc_page(tss) == 0xffc00000) {
		addr = kgdb_lookup("cpu0prvpage");
		if (addr == 0)
			return (0);
		if (kvm_read(kvm, addr, &cpu0prvpage, sizeof(cpu0prvpage)) !=
		    sizeof(cpu0prvpage)) {
			warnx("kvm_read: %s", kvm_geterr(kvm));
			return (0);
		}
		tss = cpu0prvpage + (tss & PAGE_MASK);
	}
	return ((CORE_ADDR)tss);
}

static struct kgdb_tss_cache *
kgdb_trgt_tss_cache(struct frame_info *next_frame, void **this_cache)
{
	char buf[MAX_REGISTER_SIZE];
	struct kgdb_tss_cache *cache;

	cache = *this_cache;
	if (cache == NULL) {
		cache = FRAME_OBSTACK_ZALLOC(struct kgdb_tss_cache);
		*this_cache = cache;
		cache->pc = frame_func_unwind(next_frame);
		frame_unwind_register(next_frame, SP_REGNUM, buf);
		cache->sp = extract_unsigned_integer(buf,
		    register_size(current_gdbarch, SP_REGNUM));
		cache->tss = kgdb_trgt_fetch_tss();
	}
	return (cache);
}

static void
kgdb_trgt_dblfault_this_id(struct frame_info *next_frame, void **this_cache,
    struct frame_id *this_id)
{
	struct kgdb_tss_cache *cache;

	cache = kgdb_trgt_tss_cache(next_frame, this_cache);
	*this_id = frame_id_build(cache->sp, cache->pc);
}

static void
kgdb_trgt_dblfault_prev_register(struct frame_info *next_frame,
    void **this_cache, int regnum, int *optimizedp, enum lval_type *lvalp,
    CORE_ADDR *addrp, int *realnump, void *valuep)
{
	char dummy_valuep[MAX_REGISTER_SIZE];
	struct kgdb_tss_cache *cache;
	int ofs, regsz;

	regsz = register_size(current_gdbarch, regnum);

	if (valuep == NULL)
		valuep = dummy_valuep;
	memset(valuep, 0, regsz);
	*optimizedp = 0;
	*addrp = 0;
	*lvalp = not_lval;
	*realnump = -1;

	ofs = (regnum >= I386_EAX_REGNUM && regnum <= I386_FS_REGNUM)
	    ? kgdb_trgt_tss_offset[regnum] : -1;
	if (ofs == -1)
		return;

	cache = kgdb_trgt_tss_cache(next_frame, this_cache);
	if (cache->tss == 0)
		return;
	*addrp = cache->tss + ofs;
	*lvalp = lval_memory;
	target_read_memory(*addrp, valuep, regsz);
}

static const struct frame_unwind kgdb_trgt_dblfault_unwind = {
        UNKNOWN_FRAME,
        &kgdb_trgt_dblfault_this_id,
        &kgdb_trgt_dblfault_prev_register
};

struct kgdb_frame_cache {
	int		frame_type;
	CORE_ADDR	pc;
	CORE_ADDR	sp;
};
#define	FT_NORMAL		1
#define	FT_INTRFRAME		2
#define	FT_INTRTRAPFRAME	3
#define	FT_TIMERFRAME		4

static int kgdb_trgt_frame_offset[15] = {
	offsetof(struct trapframe, tf_eax),
	offsetof(struct trapframe, tf_ecx),
	offsetof(struct trapframe, tf_edx),
	offsetof(struct trapframe, tf_ebx),
	offsetof(struct trapframe, tf_esp),
	offsetof(struct trapframe, tf_ebp),
	offsetof(struct trapframe, tf_esi),
	offsetof(struct trapframe, tf_edi),
	offsetof(struct trapframe, tf_eip),
	offsetof(struct trapframe, tf_eflags),
	offsetof(struct trapframe, tf_cs),
	offsetof(struct trapframe, tf_ss),
	offsetof(struct trapframe, tf_ds),
	offsetof(struct trapframe, tf_es),
	offsetof(struct trapframe, tf_fs)
};

static struct kgdb_frame_cache *
kgdb_trgt_frame_cache(struct frame_info *next_frame, void **this_cache)
{
	char buf[MAX_REGISTER_SIZE];
	struct kgdb_frame_cache *cache;
	char *pname;
	CORE_ADDR pcx;
	uintptr_t addr, setidt_disp;

	cache = *this_cache;
	if (cache == NULL) {
		cache = FRAME_OBSTACK_ZALLOC(struct kgdb_frame_cache);
		*this_cache = cache;
		pcx = frame_pc_unwind(next_frame);
		if (pcx >= PMAP_TRM_MIN_ADDRESS) {
			addr = kgdb_lookup("setidt_disp");
			if (addr != 0) {
				if (kvm_read(kvm, addr, &setidt_disp,
				    sizeof(setidt_disp)) !=
				    sizeof(setidt_disp))
					warnx("kvm_read: %s", kvm_geterr(kvm));
				else
					pcx -= setidt_disp;
			}
		}
		cache->pc = pcx;
		find_pc_partial_function(cache->pc, &pname, NULL, NULL);
		if (pname[0] != 'X')
			cache->frame_type = FT_NORMAL;
		else if (strcmp(pname, "Xtimerint") == 0)
			cache->frame_type = FT_TIMERFRAME;
		else if (strcmp(pname, "Xcpustop") == 0 ||
		    strcmp(pname, "Xrendezvous") == 0 ||
		    strcmp(pname, "Xipi_intr_bitmap_handler") == 0 ||
		    strcmp(pname, "Xlazypmap") == 0)
			cache->frame_type = FT_INTRTRAPFRAME;
		else
			cache->frame_type = FT_INTRFRAME;
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

	ofs = (regnum >= I386_EAX_REGNUM && regnum <= I386_FS_REGNUM)
	    ? kgdb_trgt_frame_offset[regnum] + ofs_fix : -1;
	if (ofs == -1)
		return;

	cache = kgdb_trgt_frame_cache(next_frame, this_cache);
	switch (cache->frame_type) {
	case FT_NORMAL:
		break;
	case FT_INTRFRAME:
		ofs += 4;
		break;
	case FT_TIMERFRAME:
		break;
	case FT_INTRTRAPFRAME:
		ofs -= ofs_fix;
		break;
	default:
		fprintf_unfiltered(gdb_stderr, "Correct FT_XXX frame offsets "
		   "for %d\n", cache->frame_type);
		break;
	}
	*addrp = cache->sp + ofs;
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

	pc = frame_pc_unwind(next_frame);
	if (pc >= PMAP_TRM_MIN_ADDRESS)
		return (&kgdb_trgt_trapframe_unwind);
	pname = NULL;
	find_pc_partial_function(pc, &pname, NULL, NULL);
	if (pname == NULL)
		return (NULL);
	if (strcmp(pname, "dblfault_handler") == 0)
		return (&kgdb_trgt_dblfault_unwind);
	if (strcmp(pname, "calltrap") == 0 ||
	    (pname[0] == 'X' && pname[1] != '_'))
		return (&kgdb_trgt_trapframe_unwind);
	/* printf("%s: %llx =%s\n", __func__, pc, pname); */
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
