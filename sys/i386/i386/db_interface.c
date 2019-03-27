/*-
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Interface to new debugger.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kdb.h>
#include <sys/pcpu.h>
#include <sys/proc.h>

#include <machine/psl.h>

#include <ddb/ddb.h>

/*
 * Read bytes from kernel address space for debugger.
 */
int
db_read_bytes(vm_offset_t addr, size_t size, char *data)
{
	jmp_buf jb;
	void *prev_jb;
	char *src;
	int ret;

	prev_jb = kdb_jmpbuf(jb);
	ret = setjmp(jb);
	if (ret == 0) {
		src = (char *)addr;
		while (size-- > 0)
			*data++ = *src++;
	}
	(void)kdb_jmpbuf(prev_jb);
	return (ret);
}

/*
 * Write bytes to kernel address space for debugger.
 */
int
db_write_bytes(vm_offset_t addr, size_t size, char *data)
{
	jmp_buf jb;
	void *prev_jb;
	char *dst;
	int ret;

	prev_jb = kdb_jmpbuf(jb);
	ret = setjmp(jb);
	if (ret == 0) {
		dst = (char *)addr;
		while (size-- > 0)
			*dst++ = *data++;
	}
	(void)kdb_jmpbuf(prev_jb);
	return (ret);
}

int
db_segsize(struct trapframe *tfp)
{
	struct proc_ldt *plp;
	struct segment_descriptor *sdp;
	int sel;

	if (tfp == NULL)
	    return (32);
	if (tfp->tf_eflags & PSL_VM)
	    return (16);
	sel = tfp->tf_cs & 0xffff;
	if (sel == GSEL(GCODE_SEL, SEL_KPL))
	    return (32);
	/* Rare cases follow.  User mode cases are currently unreachable. */
	if (ISLDT(sel)) {
	    plp = curthread->td_proc->p_md.md_ldt;
	    sdp = (plp != NULL) ? &plp->ldt_sd : &ldt[0].sd;
	} else {
	    sdp = &gdt[PCPU_GET(cpuid) * NGDT].sd;
	}
	return (sdp[IDXSEL(sel)].sd_def32 == 0 ? 16 : 32);
}

void
db_show_mdpcpu(struct pcpu *pc)
{

	db_printf("APIC ID      = %d\n", pc->pc_apic_id);
	db_printf("currentldt   = 0x%x\n", pc->pc_currentldt);
	db_printf("trampstk     = 0x%x\n", pc->pc_trampstk);
	db_printf("kesp0        = 0x%x\n", pc->pc_kesp0);
	db_printf("common_tssp  = 0x%x\n", (u_int)pc->pc_common_tssp);
	db_printf("tlb gen      = %u\n", pc->pc_smp_tlb_done);
}
