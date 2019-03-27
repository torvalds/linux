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

#include <machine/cpufunc.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>

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
 * We need to disable write protection temporarily so we can write
 * things (such as break points) that might be in write-protected
 * memory.
 */
int
db_write_bytes(vm_offset_t addr, size_t size, char *data)
{
	jmp_buf jb;
	void *prev_jb;
	char *dst;
	bool old_wp;
	int ret;

	old_wp = false;
	prev_jb = kdb_jmpbuf(jb);
	ret = setjmp(jb);
	if (ret == 0) {
		old_wp = disable_wp();
		dst = (char *)addr;
		while (size-- > 0)
			*dst++ = *data++;
	}
	restore_wp(old_wp);
	(void)kdb_jmpbuf(prev_jb);
	return (ret);
}

void
db_show_mdpcpu(struct pcpu *pc)
{

	db_printf("curpmap      = %p\n", pc->pc_curpmap);
	db_printf("tssp         = %p\n", pc->pc_tssp);
	db_printf("commontssp   = %p\n", pc->pc_commontssp);
	db_printf("rsp0         = 0x%lx\n", pc->pc_rsp0);
	db_printf("gs32p        = %p\n", pc->pc_gs32p);
	db_printf("ldt          = %p\n", pc->pc_ldt);
	db_printf("tss          = %p\n", pc->pc_tss);
	db_printf("tlb gen      = %u\n", pc->pc_smp_tlb_done);
}
