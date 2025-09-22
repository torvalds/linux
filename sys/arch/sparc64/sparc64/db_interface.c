/*	$OpenBSD: db_interface.c,v 1.67 2025/06/02 18:49:04 claudio Exp $	*/
/*	$NetBSD: db_interface.c,v 1.61 2001/07/31 06:55:47 eeh Exp $ */

/*
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
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
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
 *
 *	From: db_interface.c,v 2.4 1991/02/05 17:11:13 mrt (CMU)
 */

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/reboot.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mutex.h>

#include <dev/cons.h>

#include <machine/db_machdep.h>
#include <ddb/db_command.h>
#include <ddb/db_sym.h>
#include <ddb/db_variables.h>
#include <ddb/db_extern.h>
#include <ddb/db_access.h>
#include <ddb/db_output.h>
#include <ddb/db_interface.h>
#include <ddb/db_run.h>

#include <machine/instr.h>
#include <machine/cpu.h>
#include <machine/openfirm.h>
#include <machine/ctlreg.h>
#include <machine/pte.h>

#ifdef notyet
#include "fb.h"
#include "esp_sbus.h"
#endif

#include "tda.h"

#ifdef MULTIPROCESSOR
struct db_mutex ddb_mp_mutex = DB_MUTEX_INITIALIZER;
volatile int ddb_state = DDB_STATE_NOT_RUNNING;
volatile cpuid_t ddb_active_cpu;
int		 db_switch_cpu;
struct cpu_info *db_switch_to_cpu;
#endif

db_regs_t	ddb_regs;	/* register state */

extern void OF_enter(void);

static long nil;

static int
db__char_value(struct db_variable *var, db_expr_t *expr, int mode)
{

	switch (mode) {
	case DB_VAR_SET:
		*var->valuep = *(char *)expr;
		break;
	case DB_VAR_GET:
		*expr = *(char *)var->valuep;
		break;
#ifdef DIAGNOSTIC
	default:
		printf("db__char_value: mode %d\n", mode);
		break;
#endif
	}

	return 0;
}

#ifdef notdef_yet
static int
db__short_value(struct db_variable *var, db_expr_t *expr, int mode)
{

	switch (mode) {
	case DB_VAR_SET:
		*var->valuep = *(short *)expr;
		break;
	case DB_VAR_GET:
		*expr = *(short *)var->valuep;
		break;
#ifdef DIAGNOSTIC
	default:
		printf("db__short_value: mode %d\n", mode);
		break;
#endif
	}

	return 0;
}
#endif

struct db_variable db_regs[] = {
	{ "tstate", (long *)&DDB_TF->tf_tstate, FCN_NULL, },
	{ "pc", (long *)&DDB_TF->tf_pc, FCN_NULL, },
	{ "npc", (long *)&DDB_TF->tf_npc, FCN_NULL, },
	{ "ipl", (long *)&DDB_TF->tf_oldpil, db__char_value, },
	{ "y", (long *)&DDB_TF->tf_y, db_var_rw_int, },
	{ "g0", (long *)&nil, FCN_NULL, },
	{ "g1", (long *)&DDB_TF->tf_global[1], FCN_NULL, },
	{ "g2", (long *)&DDB_TF->tf_global[2], FCN_NULL, },
	{ "g3", (long *)&DDB_TF->tf_global[3], FCN_NULL, },
	{ "g4", (long *)&DDB_TF->tf_global[4], FCN_NULL, },
	{ "g5", (long *)&DDB_TF->tf_global[5], FCN_NULL, },
	{ "g6", (long *)&DDB_TF->tf_global[6], FCN_NULL, },
	{ "g7", (long *)&DDB_TF->tf_global[7], FCN_NULL, },
	{ "o0", (long *)&DDB_TF->tf_out[0], FCN_NULL, },
	{ "o1", (long *)&DDB_TF->tf_out[1], FCN_NULL, },
	{ "o2", (long *)&DDB_TF->tf_out[2], FCN_NULL, },
	{ "o3", (long *)&DDB_TF->tf_out[3], FCN_NULL, },
	{ "o4", (long *)&DDB_TF->tf_out[4], FCN_NULL, },
	{ "o5", (long *)&DDB_TF->tf_out[5], FCN_NULL, },
	{ "o6", (long *)&DDB_TF->tf_out[6], FCN_NULL, },
	{ "o7", (long *)&DDB_TF->tf_out[7], FCN_NULL, },
	{ "l0", (long *)&DDB_TF->tf_local[0], FCN_NULL, },
	{ "l1", (long *)&DDB_TF->tf_local[1], FCN_NULL, },
	{ "l2", (long *)&DDB_TF->tf_local[2], FCN_NULL, },
	{ "l3", (long *)&DDB_TF->tf_local[3], FCN_NULL, },
	{ "l4", (long *)&DDB_TF->tf_local[4], FCN_NULL, },
	{ "l5", (long *)&DDB_TF->tf_local[5], FCN_NULL, },
	{ "l6", (long *)&DDB_TF->tf_local[6], FCN_NULL, },
	{ "l7", (long *)&DDB_TF->tf_local[7], FCN_NULL, },
	{ "i0", (long *)&DDB_FR->fr_arg[0], FCN_NULL, },
	{ "i1", (long *)&DDB_FR->fr_arg[1], FCN_NULL, },
	{ "i2", (long *)&DDB_FR->fr_arg[2], FCN_NULL, },
	{ "i3", (long *)&DDB_FR->fr_arg[3], FCN_NULL, },
	{ "i4", (long *)&DDB_FR->fr_arg[4], FCN_NULL, },
	{ "i5", (long *)&DDB_FR->fr_arg[5], FCN_NULL, },
	{ "i6", (long *)&DDB_FR->fr_arg[6], FCN_NULL, },
	{ "i7", (long *)&DDB_FR->fr_arg[7], FCN_NULL, },
	{ "f0", (long *)&DDB_FP->fs_regs[0], FCN_NULL, },
	{ "f2", (long *)&DDB_FP->fs_regs[2], FCN_NULL, },
	{ "f4", (long *)&DDB_FP->fs_regs[4], FCN_NULL, },
	{ "f6", (long *)&DDB_FP->fs_regs[6], FCN_NULL, },
	{ "f8", (long *)&DDB_FP->fs_regs[8], FCN_NULL, },
	{ "f10", (long *)&DDB_FP->fs_regs[10], FCN_NULL, },
	{ "f12", (long *)&DDB_FP->fs_regs[12], FCN_NULL, },
	{ "f14", (long *)&DDB_FP->fs_regs[14], FCN_NULL, },
	{ "f16", (long *)&DDB_FP->fs_regs[16], FCN_NULL, },
	{ "f18", (long *)&DDB_FP->fs_regs[18], FCN_NULL, },
	{ "f20", (long *)&DDB_FP->fs_regs[20], FCN_NULL, },
	{ "f22", (long *)&DDB_FP->fs_regs[22], FCN_NULL, },
	{ "f24", (long *)&DDB_FP->fs_regs[24], FCN_NULL, },
	{ "f26", (long *)&DDB_FP->fs_regs[26], FCN_NULL, },
	{ "f28", (long *)&DDB_FP->fs_regs[28], FCN_NULL, },
	{ "f30", (long *)&DDB_FP->fs_regs[30], FCN_NULL, },
	{ "f32", (long *)&DDB_FP->fs_regs[32], FCN_NULL, },
	{ "f34", (long *)&DDB_FP->fs_regs[34], FCN_NULL, },
	{ "f36", (long *)&DDB_FP->fs_regs[36], FCN_NULL, },
	{ "f38", (long *)&DDB_FP->fs_regs[38], FCN_NULL, },
	{ "f40", (long *)&DDB_FP->fs_regs[40], FCN_NULL, },
	{ "f42", (long *)&DDB_FP->fs_regs[42], FCN_NULL, },
	{ "f44", (long *)&DDB_FP->fs_regs[44], FCN_NULL, },
	{ "f46", (long *)&DDB_FP->fs_regs[46], FCN_NULL, },
	{ "f48", (long *)&DDB_FP->fs_regs[48], FCN_NULL, },
	{ "f50", (long *)&DDB_FP->fs_regs[50], FCN_NULL, },
	{ "f52", (long *)&DDB_FP->fs_regs[52], FCN_NULL, },
	{ "f54", (long *)&DDB_FP->fs_regs[54], FCN_NULL, },
	{ "f56", (long *)&DDB_FP->fs_regs[56], FCN_NULL, },
	{ "f58", (long *)&DDB_FP->fs_regs[58], FCN_NULL, },
	{ "f60", (long *)&DDB_FP->fs_regs[60], FCN_NULL, },
	{ "f62", (long *)&DDB_FP->fs_regs[62], FCN_NULL, },
	{ "fsr", (long *)&DDB_FP->fs_fsr, FCN_NULL, },
	{ "gsr", (long *)&DDB_FP->fs_gsr, FCN_NULL, },

};
struct db_variable *db_eregs = db_regs + nitems(db_regs);

extern label_t	*db_recover;

extern char *trap_type[];

void kdb_kbd_trap(struct trapframe *);
void db_prom_cmd(db_expr_t, int, db_expr_t, char *);
void db_proc_cmd(db_expr_t, int, db_expr_t, char *);
void db_ctx_cmd(db_expr_t, int, db_expr_t, char *);
void db_dump_window(db_expr_t, int, db_expr_t, char *);
void db_dump_stack(db_expr_t, int, db_expr_t, char *);
void db_dump_trap(db_expr_t, int, db_expr_t, char *);
void db_dump_fpstate(db_expr_t, int, db_expr_t, char *);
void db_dump_ts(db_expr_t, int, db_expr_t, char *);
void db_dump_pcb(db_expr_t, int, db_expr_t, char *);
void db_dump_pv(db_expr_t, int, db_expr_t, char *);
void db_setpcb(db_expr_t, int, db_expr_t, char *);
void db_dump_dtlb(db_expr_t, int, db_expr_t, char *);
void db_dump_itlb(db_expr_t, int, db_expr_t, char *);
void db_dump_dtsb(db_expr_t, int, db_expr_t, char *);
void db_pmap_kernel(db_expr_t, int, db_expr_t, char *);
void db_pload_cmd(db_expr_t, int, db_expr_t, char *);
void db_pmap_cmd(db_expr_t, int, db_expr_t, char *);
void db_lock(db_expr_t, int, db_expr_t, char *);
void db_watch(db_expr_t, int, db_expr_t, char *);
void db_xir(db_expr_t, int, db_expr_t, char *);

static void db_dump_pmap(struct pmap*);

#ifdef MULTIPROCESSOR
void db_cpuinfo_cmd(db_expr_t, int, db_expr_t, char *);
void db_startproc_cmd(db_expr_t, int, db_expr_t, char *);
void db_stopproc_cmd(db_expr_t, int, db_expr_t, char *);
void db_ddbproc_cmd(db_expr_t, int, db_expr_t, char *);
#endif

/*
 * Received keyboard interrupt sequence.
 */
void
kdb_kbd_trap(struct trapframe *tf)
{
	if (db_active == 0 /* && (boothowto & RB_KDB) */) {
		printf("\n\nkernel: keyboard interrupt tf=%p\n", tf);
		db_ktrap(-1, tf);
	}
}

/*
 *  db_ktrap - field a TRACE or BPT trap
 */
int
db_ktrap(int type, register struct trapframe *tf)
{
	int s, tl;
	struct trapstate *ts = &ddb_regs.ddb_ts[0];
	extern int savetstate(struct trapstate *ts);
	extern void restoretstate(int tl, struct trapstate *ts);

#if NTDA > 0
	tda_full_blast();
#endif

	fb_unblank();

	switch (type) {
	case T_BREAKPOINT:	/* breakpoint */
	case -1:		/* keyboard interrupt */
		break;
	default:
		printf("kernel trap %x: %s\n", type, trap_type[type & 0x1ff]);
		if (db_recover != 0) {
			OF_enter();
			db_error("Faulted in DDB; continuing...\n");
			OF_enter();
			/*NOTREACHED*/
		}
		db_recover = (label_t *)1;
	}

#ifdef MULTIPROCESSOR
	db_mtx_enter(&ddb_mp_mutex);
	if (ddb_state == DDB_STATE_EXITING)
		ddb_state = DDB_STATE_NOT_RUNNING;
	db_mtx_leave(&ddb_mp_mutex);
	while (db_enter_ddb()) {
#endif

	/* Should switch to kdb`s own stack here. */
	write_all_windows();

	ddb_regs.ddb_tf = *tf;
	if (fpproc) {
		savefpstate(fpproc->p_md.md_fpstate);
		ddb_regs.ddb_fpstate = *fpproc->p_md.md_fpstate;
		loadfpstate(fpproc->p_md.md_fpstate);
	}

	s = splhigh();
	db_active++;
	cnpollc(1);
	/* Need to do spl stuff till cnpollc works */
	tl = ddb_regs.ddb_tl = savetstate(ts);
	db_dump_ts(0, 0, 0, 0);
	db_trap(type, 0/*code*/);
	restoretstate(tl,ts);
	cnpollc(0);
	db_active--;
	splx(s);

	if (fpproc) {
		*fpproc->p_md.md_fpstate = ddb_regs.ddb_fpstate;
		loadfpstate(fpproc->p_md.md_fpstate);
	}
#if 0
	/* We will not alter the machine's running state until we get everything else working */
	*(struct frame *)tf->tf_out[6] = ddb_regs.ddb_fr;
#endif
	*tf = ddb_regs.ddb_tf;

#ifdef MULTIPROCESSOR
		if (!db_switch_cpu)
			ddb_state = DDB_STATE_EXITING;
	}
#endif

	return (1);
}

#ifdef MULTIPROCESSOR

void ipi_db(void);

void
db_cpuinfo_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	struct cpu_info *ci;

	for (ci = cpus; ci != NULL; ci = ci->ci_next) {
		db_printf("%c%4d: ", (ci == curcpu()) ? '*' : ' ',
		    ci->ci_cpuid);
		switch(ci->ci_ddb_paused) {
		case CI_DDB_RUNNING:
			db_printf("running\n");
			break;
		case CI_DDB_SHOULDSTOP:
			db_printf("stopping\n");
			break;
		case CI_DDB_STOPPED:
			db_printf("stopped\n");
			break;
		case CI_DDB_ENTERDDB:
			db_printf("entering ddb\n");
			break;
		case CI_DDB_INDDB:
			db_printf("ddb\n");
			break;
		default:
			db_printf("? (%d)\n",
			    ci->ci_ddb_paused);
			break;
		}
	}
}

void
db_startproc_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	struct cpu_info *ci;

	if (have_addr) {
		for (ci = cpus; ci != NULL; ci = ci->ci_next) {
			if (addr == ci->ci_cpuid) {
				db_startcpu(ci);
				break;
			}
		}
		if (ci == NULL)
			db_printf("Invalid cpu %d\n", (int)addr);
	} else {
		for (ci = cpus; ci != NULL; ci = ci->ci_next) {
			if (ci != curcpu())
				db_startcpu(ci);
		}
	}
}

void
db_stopproc_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	struct cpu_info *ci;

	if (have_addr) {
		for (ci = cpus; ci != NULL; ci = ci->ci_next) {
			if (addr == ci->ci_cpuid) {
				db_stopcpu(ci);
				break;
			}
		}
		if (ci == NULL)
			db_printf("Invalid cpu %d\n", (int)addr);
	} else {
		for (ci = cpus; ci != NULL; ci = ci->ci_next) {
			if (ci != curcpu())
				db_stopcpu(ci);
		}
	}
}

void
db_ddbproc_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	struct cpu_info *ci;

	if (have_addr) {
		for (ci = cpus; ci != NULL; ci = ci->ci_next) {
			if (addr == ci->ci_cpuid && ci != curcpu()) {
				db_stopcpu(ci);
				db_switch_to_cpu = ci;
				db_switch_cpu = 1;
				db_cmd_loop_done = 1;
				break;
			}
		}
		if (ci == NULL)
			db_printf("Invalid cpu %d\n", (int)addr);
	} else {
		db_printf("CPU not specified\n");
	}
}

int
db_enter_ddb(void)
{
	struct cpu_info *ci;

	db_mtx_enter(&ddb_mp_mutex);

	/* If we are first in, grab ddb and stop all other CPUs */
	if (ddb_state == DDB_STATE_NOT_RUNNING) {
		ddb_active_cpu = cpu_number();
		ddb_state = DDB_STATE_RUNNING;
		curcpu()->ci_ddb_paused = CI_DDB_INDDB;
		db_mtx_leave(&ddb_mp_mutex);
		for (ci = cpus; ci != NULL; ci = ci->ci_next) {
			if (ci != curcpu() &&
			    ci->ci_ddb_paused != CI_DDB_STOPPED) {
				ci->ci_ddb_paused = CI_DDB_SHOULDSTOP;
				sparc64_send_ipi(ci->ci_itid, ipi_db, 0, 0);
			}
		}
		return (1);
	}

	/* Leaving ddb completely.  Start all other CPUs and return 0 */
	if (ddb_active_cpu == cpu_number() && ddb_state == DDB_STATE_EXITING) {
		for (ci = cpus; ci != NULL; ci = ci->ci_next)
			ci->ci_ddb_paused = CI_DDB_RUNNING;
		db_mtx_leave(&ddb_mp_mutex);
		return (0);
	}

	/* We're switching to another CPU.  db_ddbproc_cmd() has made sure
	 * it is waiting for ddb, we just have to set ddb_active_cpu. */
	if (ddb_active_cpu == cpu_number() && db_switch_cpu) {
		curcpu()->ci_ddb_paused = CI_DDB_SHOULDSTOP;
		db_switch_cpu = 0;
		ddb_active_cpu = db_switch_to_cpu->ci_cpuid;
		db_switch_to_cpu->ci_ddb_paused = CI_DDB_ENTERDDB;
	}

	/* Wait until we should enter ddb or resume */
	while (ddb_active_cpu != cpu_number() &&
	    curcpu()->ci_ddb_paused != CI_DDB_RUNNING) {
		if (curcpu()->ci_ddb_paused == CI_DDB_SHOULDSTOP)
			curcpu()->ci_ddb_paused = CI_DDB_STOPPED;
		db_mtx_leave(&ddb_mp_mutex);

		/* Busy wait without locking, we'll confirm with lock later */
		while (ddb_active_cpu != cpu_number() &&
		    curcpu()->ci_ddb_paused != CI_DDB_RUNNING)
			CPU_BUSY_CYCLE();

		db_mtx_enter(&ddb_mp_mutex);
	}

	/* Either enter ddb or exit */
	if (ddb_active_cpu == cpu_number() && ddb_state == DDB_STATE_RUNNING) {
		curcpu()->ci_ddb_paused = CI_DDB_INDDB;
		db_mtx_leave(&ddb_mp_mutex);
		return (1);
	} else {
		db_mtx_leave(&ddb_mp_mutex);
		return (0);
	}
}

void
db_startcpu(struct cpu_info *ci)
{
	if (ci != curcpu()) {
		db_mtx_enter(&ddb_mp_mutex);
		ci->ci_ddb_paused = CI_DDB_RUNNING;
		db_mtx_leave(&ddb_mp_mutex);
	}
}

void
db_stopcpu(struct cpu_info *ci)
{
	db_mtx_enter(&ddb_mp_mutex);
	if (ci != curcpu() && ci->ci_ddb_paused != CI_DDB_STOPPED) {
		ci->ci_ddb_paused = CI_DDB_SHOULDSTOP;
		db_mtx_leave(&ddb_mp_mutex);
		sparc64_send_ipi(ci->ci_itid, ipi_db, 0, 0);
	} else {
		db_mtx_leave(&ddb_mp_mutex);
	}
}

#endif

/*
 * Read bytes from kernel address space for debugger.
 */
void
db_read_bytes(vaddr_t addr, size_t size, void *datap)
{
	char *data = datap;
	register char	*src;

	src = (char *)addr;
	while (size-- > 0) {
		if (src >= (char *)VM_MIN_KERNEL_ADDRESS)
			*data++ = probeget((paddr_t)(u_long)src++, ASI_P, 1);
		else
			_copyin(src++, data++, sizeof(u_char));
	}
}


/*
 * Write bytes to kernel address space for debugger.
 */
void
db_write_bytes(vaddr_t addr, size_t size, void *datap)
{
	char *data = datap;
	register char	*dst;
	extern vaddr_t ktext;
	extern paddr_t ktextp;

	dst = (char *)addr;
	while (size-- > 0) {
		if ((dst >= (char *)VM_MIN_KERNEL_ADDRESS+0x800000))
			*dst = *data;
		else if ((dst >= (char *)VM_MIN_KERNEL_ADDRESS) &&
			 (dst < (char *)VM_MIN_KERNEL_ADDRESS+0x800000))
			/* Read Only mapping -- need to do a bypass access */
			stba((u_long)dst - ktext + ktextp, ASI_PHYS_CACHED, *data);
		else
			copyout(data, dst, sizeof(char));
		dst++, data++;
	}

}

void
db_enter(void)
{
	/* We use the breakpoint to trap into DDB */
	asm("ta 1; nop");
}

void
db_prom_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	OF_enter();
}

#define CHEETAHP (((getver()>>32) & 0x1ff) >= 0x14)
unsigned long db_get_dtlb_data(int entry), db_get_dtlb_tag(int entry),
db_get_itlb_data(int entry), db_get_itlb_tag(int entry);
void db_print_itlb_entry(int entry, int i, int endc);
void db_print_dtlb_entry(int entry, int i, int endc);

extern __inline__ unsigned long
db_get_dtlb_data(int entry)
{
	unsigned long r;
	__asm__ volatile("ldxa [%1] %2,%0"
		: "=r" (r)
		: "r" (entry <<3), "i" (ASI_DMMU_TLB_DATA));
	return r;
}

extern __inline__ unsigned long
db_get_dtlb_tag(int entry)
{
	unsigned long r;
	__asm__ volatile("ldxa [%1] %2,%0"
		: "=r" (r)
		: "r" (entry <<3), "i" (ASI_DMMU_TLB_TAG));
	return r;
}

extern __inline__ unsigned long
db_get_itlb_data(int entry)
{
	unsigned long r;
	__asm__ volatile("ldxa [%1] %2,%0"
		: "=r" (r)
		: "r" (entry <<3), "i" (ASI_IMMU_TLB_DATA));
	return r;
}

extern __inline__ unsigned long
db_get_itlb_tag(int entry)
{
	unsigned long r;
	__asm__ volatile("ldxa [%1] %2,%0"
		: "=r" (r)
		: "r" (entry <<3), "i" (ASI_IMMU_TLB_TAG));
	return r;
}

void
db_print_dtlb_entry(int entry, int i, int endc)
{
	unsigned long tag, data;
	tag = db_get_dtlb_tag(entry);
	data = db_get_dtlb_data(entry);
	db_printf("%2d:%16.16lx %16.16lx%c", i, tag, data, endc);
}

void
db_print_itlb_entry(int entry, int i, int endc)
{
	unsigned long tag, data;
	tag = db_get_itlb_tag(entry);
	data = db_get_itlb_data(entry);
	db_printf("%2d:%16.16lx %16.16lx%c", i, tag, data, endc);
}

void
db_dump_dtlb(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	/* extern void print_dtlb(void); -- locore.s; no longer used here */

	if (have_addr) {
		int i;
		int64_t* p = (int64_t*)addr;
		static int64_t buf[128];
		extern void dump_dtlb(int64_t *);

	if (CHEETAHP) {
		db_printf("DTLB %ld\n", addr);
		switch(addr)
		{
		case 0:
			for (i = 0; i < 16; ++i)
				db_print_dtlb_entry(i, i, (i&1)?'\n':' ');
			break;
		case 2:
			for (i = 0; i < 512; ++i)
				db_print_dtlb_entry(i+16384, i, (i&1)?'\n':' ');
			break;
		}
	} else {
		dump_dtlb(buf);
		p = buf;
		for (i=0; i<64;) {
			db_printf("%2d:%16.16llx %16.16llx ", i++, p[0], p[1]);
			p += 2;
			db_printf("%2d:%16.16llx %16.16llx\n", i++, p[0], p[1]);
			p += 2;
		}
	}
	} else {
printf ("Usage: mach dtlb 0,2\n");
	}
}

void
db_dump_itlb(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int i;
	if (!have_addr) {
		db_printf("Usage: mach itlb 0,1,2\n");
		return;
	}
	if (CHEETAHP) {
		db_printf("ITLB %ld\n", addr);
		switch(addr)
		{
		case 0:
			for (i = 0; i < 16; ++i)
				db_print_itlb_entry(i, i, (i&1)?'\n':' ');
			break;
		case 2:
			for (i = 0; i < 128; ++i)
				db_print_itlb_entry(i+16384, i, (i&1)?'\n':' ');
			break;
		}
	} else {
		for (i = 0; i < 63; ++i)
			db_print_itlb_entry(i, i, (i&1)?'\n':' ');
	}
}

void
db_pload_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	static paddr_t oldaddr = -1;
	int asi = ASI_PHYS_CACHED;

	if (!have_addr) {
		addr = oldaddr;
	}
	if (addr == -1) {
		db_printf("no address\n");
		return;
	}
	addr &= ~0x7; /* align */
	{
		register char c, *cp = modif;
		while ((c = *cp++) != 0)
			if (c == 'u')
				asi = ASI_AIUS;
	}
	while (count--) {
		if (db_print_position() == 0) {
			/* Always print the address. */
			db_printf("%16.16lx:\t", addr);
		}
		oldaddr=addr;
		db_printf("%8.8lx\n", (long)ldxa(addr, asi));
		addr += 8;
		if (db_print_position() != 0)
			db_end_line(0);
	}
}

int64_t pseg_get(struct pmap *, vaddr_t);

void
db_dump_pmap(struct pmap* pm)
{
	/* print all valid pages in the kernel pmap */
	long i, j, k, n;
	paddr_t *pdir, *ptbl;

	n = 0;
	for (i=0; i<STSZ; i++) {
		if((pdir = (paddr_t *)(u_long)ldxa((vaddr_t)&pm->pm_segs[i], ASI_PHYS_CACHED))) {
			db_printf("pdir %ld at %lx:\n", i, (long)pdir);
			for (k=0; k<PDSZ; k++) {
				if ((ptbl = (paddr_t *)(u_long)ldxa((vaddr_t)&pdir[k], ASI_PHYS_CACHED))) {
					db_printf("\tptable %ld:%ld at %lx:\n", i, k, (long)ptbl);
					for (j=0; j<PTSZ; j++) {
						int64_t data0, data1;
						data0 = ldxa((vaddr_t)&ptbl[j], ASI_PHYS_CACHED);
						j++;
						data1 = ldxa((vaddr_t)&ptbl[j], ASI_PHYS_CACHED);
						if (data0 || data1) {
							db_printf("%llx: %llx\t",
								  (unsigned long long)(((u_int64_t)i<<STSHIFT)|(k<<PDSHIFT)|((j-1)<<PTSHIFT)),
								  (unsigned long long)(data0));
							db_printf("%llx: %llx\n",
								  (unsigned long long)(((u_int64_t)i<<STSHIFT)|(k<<PDSHIFT)|(j<<PTSHIFT)),
								  (unsigned long long)(data1));
						}
					}
				}
			}
		}
	}
}

void
db_pmap_kernel(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	extern struct pmap kernel_pmap_;
	int i, j, full = 0;
	u_int64_t data;

	{
		register char c, *cp = modif;
		while ((c = *cp++) != 0)
			if (c == 'f')
				full = 1;
	}
	if (have_addr) {
		/* lookup an entry for this VA */

		if ((data = pseg_get(&kernel_pmap_, (vaddr_t)addr))) {
			db_printf("pmap_kernel(%p)->pm_segs[%lx][%lx][%lx]=>%llx\n",
				  (void *)addr, (u_long)va_to_seg(addr),
				  (u_long)va_to_dir(addr), (u_long)va_to_pte(addr),
				  (unsigned long long)data);
		} else {
			db_printf("No mapping for %p\n", (void *)addr);
		}
		return;
	}

	db_printf("pmap_kernel(%p) psegs %p phys %llx\n",
		  &kernel_pmap_, kernel_pmap_.pm_segs,
		  (unsigned long long)kernel_pmap_.pm_physaddr);
	if (full) {
		db_dump_pmap(&kernel_pmap_);
	} else {
		for (j=i=0; i<STSZ; i++) {
			long seg = (long)ldxa((vaddr_t)&kernel_pmap_.pm_segs[i], ASI_PHYS_CACHED);
			if (seg)
				db_printf("seg %d => %lx%c", i, seg, (j++%4)?'\t':'\n');
		}
	}
}


void
db_pmap_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	struct pmap* pm=NULL;
	int i, j=0, full = 0;

	{
		register char c, *cp = modif;
		if (modif)
			while ((c = *cp++) != 0)
				if (c == 'f')
					full = 1;
	}
	if (curproc && curproc->p_vmspace)
		pm = curproc->p_vmspace->vm_map.pmap;
	if (have_addr) {
		pm = (struct pmap*)addr;
	}

	db_printf("pmap %p: ctx %x refs %d physaddr %llx psegs %p\n",
		pm, pm->pm_ctx, pm->pm_refs,
		(unsigned long long)pm->pm_physaddr, pm->pm_segs);

	if (full) {
		db_dump_pmap(pm);
	} else {
		for (i=0; i<STSZ; i++) {
			long seg = (long)ldxa((vaddr_t)&kernel_pmap_.pm_segs[i], ASI_PHYS_CACHED);
			if (seg)
				db_printf("seg %d => %lx%c", i, seg, (j++%4)?'\t':'\n');
		}
	}
}


void
db_lock(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
#if 0
	struct lock *l;

	if (!have_addr) {
		db_printf("What lock address?\n");
		return;
	}

	l = (struct lock *)addr;
	db_printf("flags=%x\n waitcount=%x sharecount=%x "
	    "exclusivecount=%x\n wmesg=%s recurselevel=%x\n",
	    l->lk_flags, l->lk_waitcount,
	    l->lk_sharecount, l->lk_exclusivecount, l->lk_wmesg,
	    l->lk_recurselevel);
#else
	db_printf("locks unsupported\n");
#endif
}

void
db_dump_dtsb(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	extern pte_t *tsb_dmmu;
	extern int tsbsize;
#define TSBENTS (512<<tsbsize)
	int i;

	db_printf("TSB:\n");
	for (i=0; i<TSBENTS; i++) {
		db_printf("%4d:%4d:%08x %08x:%08x ", i,
			  (int)((tsb_dmmu[i].tag&TSB_TAG_G)?-1:TSB_TAG_CTX(tsb_dmmu[i].tag)),
			  (int)((i<<13)|TSB_TAG_VA(tsb_dmmu[i].tag)),
			  (int)(tsb_dmmu[i].data>>32), (int)tsb_dmmu[i].data);
		i++;
		db_printf("%4d:%4d:%08x %08x:%08x\n", i,
			  (int)((tsb_dmmu[i].tag&TSB_TAG_G)?-1:TSB_TAG_CTX(tsb_dmmu[i].tag)),
			  (int)((i<<13)|TSB_TAG_VA(tsb_dmmu[i].tag)),
			  (int)(tsb_dmmu[i].data>>32), (int)tsb_dmmu[i].data);
	}
}

void db_page_cmd(db_expr_t, int, db_expr_t, char *);
void
db_page_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{

	if (!have_addr) {
		db_printf("Need paddr for page\n");
		return;
	}

	db_printf("pa %llx pg %p\n", (unsigned long long)addr,
	    PHYS_TO_VM_PAGE(addr));
}


void
db_proc_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	struct proc *p;

	p = curproc;
	if (have_addr)
		p = (struct proc*) addr;
	if (p == NULL) {
		db_printf("no current process\n");
		return;
	}
	db_printf("process %p:", p);
	db_printf("pid:%d vmspace:%p pmap:%p ctx:%x wchan:%p rpri:%d upri:%d\n",
	    p->p_p->ps_pid, p->p_vmspace, p->p_vmspace->vm_map.pmap,
	    p->p_vmspace->vm_map.pmap->pm_ctx,
	    p->p_wchan, p->p_runpri, p->p_usrpri);
	db_printf("maxsaddr:%p ssiz:%dpg or %llxB\n",
	    p->p_vmspace->vm_maxsaddr, p->p_vmspace->vm_ssize,
	    (unsigned long long)ptoa(p->p_vmspace->vm_ssize));
	db_printf("profile timer: %lld sec %ld usec\n",
	    (long long)p->p_p->ps_timer[ITIMER_PROF].it_value.tv_sec,
	    p->p_p->ps_timer[ITIMER_PROF].it_value.tv_nsec / 1000);
	db_printf("pcb: %p tf: %p fpstate: %p\n", &p->p_addr->u_pcb,
	    p->p_md.md_tf, p->p_md.md_fpstate);
	return;
}

void
db_ctx_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	struct proc *p;

	/* XXX LOCKING XXX */
	LIST_FOREACH(p, &allproc, p_list) {
		if (p->p_stat) {
			db_printf("process %p:", p);
			db_printf("pid:%d pmap:%p ctx:%x tf:%p fpstate %p "
			    "lastcall:%s\n",
			    p->p_p->ps_pid, p->p_vmspace->vm_map.pmap,
			    p->p_vmspace->vm_map.pmap->pm_ctx,
			    p->p_md.md_tf, p->p_md.md_fpstate,
			    (p->p_addr->u_pcb.lastcall)?
			    p->p_addr->u_pcb.lastcall : "Null");
		}
	}
	return;
}

void
db_dump_pcb(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	struct pcb *pcb;
	int i;

	pcb = curpcb;
	if (have_addr)
		pcb = (struct pcb*) addr;

	db_printf("pcb@%p sp:%p pc:%p cwp:%d pil:%d nsaved:%x onfault:%p\nlastcall:%s\nfull windows:\n",
		  pcb, (void *)(long)pcb->pcb_sp, (void *)(long)pcb->pcb_pc, pcb->pcb_cwp,
		  pcb->pcb_pil, pcb->pcb_nsaved, (void *)pcb->pcb_onfault,
		  (pcb->lastcall)?pcb->lastcall:"Null");

	for (i=0; i<pcb->pcb_nsaved; i++) {
		db_printf("win %d: at %llx local, in\n", i,
			  (unsigned long long)pcb->pcb_rwsp[i]);
		db_printf("%16llx %16llx %16llx %16llx\n",
			  (unsigned long long)pcb->pcb_rw[i].rw_local[0],
			  (unsigned long long)pcb->pcb_rw[i].rw_local[1],
			  (unsigned long long)pcb->pcb_rw[i].rw_local[2],
			  (unsigned long long)pcb->pcb_rw[i].rw_local[3]);
		db_printf("%16llx %16llx %16llx %16llx\n",
			  (unsigned long long)pcb->pcb_rw[i].rw_local[4],
			  (unsigned long long)pcb->pcb_rw[i].rw_local[5],
			  (unsigned long long)pcb->pcb_rw[i].rw_local[6],
			  (unsigned long long)pcb->pcb_rw[i].rw_local[7]);
		db_printf("%16llx %16llx %16llx %16llx\n",
			  (unsigned long long)pcb->pcb_rw[i].rw_in[0],
			  (unsigned long long)pcb->pcb_rw[i].rw_in[1],
			  (unsigned long long)pcb->pcb_rw[i].rw_in[2],
			  (unsigned long long)pcb->pcb_rw[i].rw_in[3]);
		db_printf("%16llx %16llx %16llx %16llx\n",
			  (unsigned long long)pcb->pcb_rw[i].rw_in[4],
			  (unsigned long long)pcb->pcb_rw[i].rw_in[5],
			  (unsigned long long)pcb->pcb_rw[i].rw_in[6],
			  (unsigned long long)pcb->pcb_rw[i].rw_in[7]);
	}
}


void
db_setpcb(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	struct proc *p;

	if (!have_addr) {
		db_printf("What TID do you want to map in?\n");
		return;
	}

	LIST_FOREACH(p, &allproc, p_list) {
		if (p->p_stat && p->p_tid == addr) {
			curproc = p;
			curpcb = (struct pcb*)p->p_addr;
			curcpu()->ci_cpcbpaddr = p->p_md.md_pcbpaddr;
			if (p->p_vmspace->vm_map.pmap->pm_ctx) {
				switchtoctx(p->p_vmspace->vm_map.pmap->pm_ctx);
				return;
			}
			db_printf("TID %ld has a null context.\n", addr);
			return;
		}
	}
	db_printf("TID %ld not found.\n", addr);
}


/*
 * Use physical or virtual watchpoint registers -- ugh
 */
void
db_watch(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int phys = 0;

#define WATCH_VR	(1L<<22)
#define WATCH_VW	(1L<<21)
#define WATCH_PR	(1L<<24)
#define WATCH_PW	(1L<<23)
#define WATCH_PM	(((u_int64_t)0xffffL)<<33)
#define WATCH_VM	(((u_int64_t)0xffffL)<<25)

	{
		register char c, *cp = modif;
		if (modif)
			while ((c = *cp++) != 0)
				if (c == 'p')
					phys = 1;
	}
	if (have_addr) {
		/* turn on the watchpoint */
		int64_t tmp = ldxa(0, ASI_MCCR);

		if (phys) {
			tmp &= ~(WATCH_PM|WATCH_PR|WATCH_PW);
			stxa(PHYSICAL_WATCHPOINT, ASI_DMMU, addr);
		} else {
			tmp &= ~(WATCH_VM|WATCH_VR|WATCH_VW);
			stxa(VIRTUAL_WATCHPOINT, ASI_DMMU, addr);
		}
		stxa(0, ASI_MCCR, tmp);
	} else {
		/* turn off the watchpoint */
		int64_t tmp = ldxa(0, ASI_MCCR);
		if (phys) tmp &= ~(WATCH_PM);
		else tmp &= ~(WATCH_VM);
		stxa(0, ASI_MCCR, tmp);
	}
}

/*
 * Provide a way to trigger an External Initiated Reset (XIR).  Some
 * systems can target individual processors, others can only target
 * all processors at once.
 */

struct xirhand {
	void (*xh_fun)(void *, int);
	void *xh_arg;
	SIMPLEQ_ENTRY(xirhand) xh_list;
};

SIMPLEQ_HEAD(, xirhand) db_xh = SIMPLEQ_HEAD_INITIALIZER(db_xh);

void
db_xir(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	struct xirhand *xh;

	if (!have_addr)
		addr = -1;

	SIMPLEQ_FOREACH(xh, &db_xh, xh_list) {
		xh->xh_fun(xh->xh_arg, addr);
	}
}

void
db_register_xir(void (*fun)(void *, int), void *arg)
{
	struct xirhand *xh;

	xh = malloc(sizeof(*xh), M_DEVBUF, M_NOWAIT);
	if (xh == NULL)
		panic("db_register_xir");
	xh->xh_fun = fun;
	xh->xh_arg = arg;
	SIMPLEQ_INSERT_TAIL(&db_xh, xh, xh_list);
}


#if NESP_SBUS
extern void db_esp(db_expr_t, int, db_expr_t, char *);
#endif

const struct db_command db_machine_command_table[] = {
	{ "ctx",	db_ctx_cmd,	0,	0 },
	{ "dtlb",	db_dump_dtlb,	0,	0 },
	{ "dtsb",	db_dump_dtsb,	0,	0 },
#if NESP_SBUS
	{ "esp",	db_esp,		0,	0 },
#endif
	{ "fpstate",	db_dump_fpstate,0,	0 },
	{ "itlb",	db_dump_itlb,	0,	0 },
	{ "kmap",	db_pmap_kernel,	0,	0 },
	{ "lock",	db_lock,	0,	0 },
	{ "pcb",	db_dump_pcb,	0,	0 },
	{ "pctx",	db_setpcb,	0,	0 },
	{ "page",	db_page_cmd,	0,	0 },
	{ "phys",	db_pload_cmd,	0,	0 },
	{ "pmap",	db_pmap_cmd,	0,	0 },
	{ "proc",	db_proc_cmd,	0,	0 },
	{ "prom",	db_prom_cmd,	0,	0 },
	{ "pv",		db_dump_pv,	0,	0 },
	{ "stack",	db_dump_stack,	0,	0 },
	{ "tf",		db_dump_trap,	0,	0 },
	{ "ts",		db_dump_ts,	0,	0 },
	{ "watch",	db_watch,	0,	0 },
	{ "window",	db_dump_window,	0,	0 },
	{ "xir",	db_xir,		0,	0 },
#ifdef MULTIPROCESSOR
	{ "cpuinfo",	db_cpuinfo_cmd,		0,	0 },
	{ "startcpu",	db_startproc_cmd,	0,	0 },
	{ "stopcpu",	db_stopproc_cmd,	0,	0 },
	{ "ddbcpu",	db_ddbproc_cmd,		0,	0 },
#endif
	{ NULL, }
};

/*
 * support for SOFTWARE_SSTEP:
 * return the next pc if the given branch is taken.
 *
 * note: in the case of conditional branches with annul,
 * this actually returns the next pc in the "not taken" path,
 * but in that case next_instr_address() will return the
 * next pc in the "taken" path.  so even tho the breakpoints
 * are backwards, everything will still work, and the logic is
 * much simpler this way.
 */
vaddr_t
db_branch_taken(int inst, vaddr_t pc, db_regs_t *regs)
{
    union instr insn;
    vaddr_t npc = ddb_regs.ddb_tf.tf_npc;

    insn.i_int = inst;

    /* the fancy union just gets in the way of this: */
    switch(inst & 0xffc00000) {
    case 0x30400000:	/* branch always, annul, with prediction */
	return pc + ((inst<<(32-19))>>((32-19)-2));
    case 0x30800000:	/* branch always, annul */
	return pc + ((inst<<(32-22))>>((32-22)-2));
    }

    /*
     * if this is not an annulled conditional branch, the next pc is "npc".
     */

    if (insn.i_any.i_op != IOP_OP2 || insn.i_branch.i_annul != 1)
	return npc;

    switch (insn.i_op2.i_op2) {
      case IOP2_Bicc:
      case IOP2_FBfcc:
      case IOP2_BPcc:
      case IOP2_FBPfcc:
      case IOP2_CBccc:
	/* branch on some condition-code */
	switch (insn.i_branch.i_cond)
	{
	  case Icc_A: /* always */
	    return pc + ((inst << 10) >> 8);

	  default: /* all other conditions */
	    return npc + 4;
	}

      case IOP2_BPr:
	/* branch on register, always conditional */
	return npc + 4;

      default:
	/* not a branch */
	panic("branch_taken() on non-branch");
    }
}

int
db_inst_branch(int inst)
{
    union instr insn;

    insn.i_int = inst;

    /* the fancy union just gets in the way of this: */
    switch(inst & 0xffc00000) {
    case 0x30400000:	/* branch always, annul, with prediction */
	return 1;
    case 0x30800000:	/* branch always, annul */
	return 1;
    }

    if (insn.i_any.i_op != IOP_OP2)
	return 0;

    switch (insn.i_op2.i_op2) {
      case IOP2_BPcc:
      case IOP2_Bicc:
      case IOP2_BPr:
      case IOP2_FBPfcc:
      case IOP2_FBfcc:
      case IOP2_CBccc:
	return 1;

      default:
	return 0;
    }
}


int
db_inst_call(int inst)
{
    union instr insn;

    insn.i_int = inst;

    switch (insn.i_any.i_op) {
      case IOP_CALL:
	return 1;

      case IOP_reg:
	return (insn.i_op3.i_op3 == IOP3_JMPL) && !db_inst_return(inst);

      default:
	return 0;
    }
}


int
db_inst_unconditional_flow_transfer(int inst)
{
    union instr insn;

    insn.i_int = inst;

    if (db_inst_call(inst))
	return 1;

    if (insn.i_any.i_op != IOP_OP2)
	return 0;

    switch (insn.i_op2.i_op2)
    {
      case IOP2_BPcc:
      case IOP2_Bicc:
      case IOP2_FBPfcc:
      case IOP2_FBfcc:
      case IOP2_CBccc:
	return insn.i_branch.i_cond == Icc_A;

      default:
	return 0;
    }
}


int
db_inst_return(int inst)
{
    return (inst == I_JMPLri(I_G0, I_O7, 8) ||		/* ret */
	    inst == I_JMPLri(I_G0, I_I7, 8));		/* retl */
}

int
db_inst_trap_return(int inst)
{
    union instr insn;

    insn.i_int = inst;

    return (insn.i_any.i_op == IOP_reg &&
	    insn.i_op3.i_op3 == IOP3_RETT);
}

void
db_machine_init(void)
{
}
