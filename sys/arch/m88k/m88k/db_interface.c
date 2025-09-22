/*	$OpenBSD: db_interface.c,v 1.33 2025/06/26 20:28:07 miod Exp $	*/
/*
 * Mach Operating System
 * Copyright (c) 1993-1991 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON AND OMRON ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON AND OMRON DISCLAIM ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
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

/*
 * m88k interface to ddb debugger
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/reboot.h>

#include <uvm/uvm_extern.h>

#include <machine/asm_macro.h>
#include <machine/cmmu.h>
#include <machine/trap.h>
#include <machine/db_machdep.h>
#include <machine/cpu.h>
#ifdef M88100
#include <machine/m88100.h>
#include <machine/m8820x.h>
#endif

#include <ddb/db_access.h>
#include <ddb/db_command.h>
#include <ddb/db_extern.h>
#include <ddb/db_interface.h>
#include <ddb/db_output.h>
#include <ddb/db_run.h>
#include <ddb/db_sym.h>

extern label_t *db_recover;
extern int frame_is_sane(db_regs_t *, int);	/* db_trace */
extern void cnpollc(int);

void	kdbprinttrap(int);

int	m88k_dmx_print(u_int, u_int, u_int, u_int);

void	m88k_db_trap(int, struct trapframe *);
void	m88k_db_print_frame(db_expr_t, int, db_expr_t, char *);
void	m88k_db_registers(db_expr_t, int, db_expr_t, char *);
void	m88k_db_where(db_expr_t, int, db_expr_t, char *);
void	m88k_db_frame_search(db_expr_t, int, db_expr_t, char *);

db_regs_t ddb_regs;

#ifdef MULTIPROCESSOR
#include <sys/mplock.h>
struct __mp_lock ddb_mp_lock;
cpuid_t	ddb_mp_nextcpu = (cpuid_t)-1;

void	m88k_db_cpu_cmd(db_expr_t, int, db_expr_t, char *);
#endif

/*
 * If you really feel like understanding the following procedure and
 * macros, see pages 6-22 to 6-30 (Section 6.7.3) of
 *
 * MC88100 RISC Microprocessor User's Manual Second Edition
 * (Motorola Order: MC88100UM/AD REV 1)
 *
 * and ERRATA-5 (6-23, 6-24, 6-24) of
 *
 * Errata to MC88100 User's Manual Second Edition MC88100UM/AD Rev 1
 * (Oct 2, 1990)
 * (Motorola Order: MC88100UMAD/AD)
 */

#ifdef M88100
/* macros for decoding dmt registers */

/*
 * return 1 if the printing of the next stage should be suppressed
 */
int
m88k_dmx_print(u_int t, u_int d, u_int a, u_int no)
{
	static const u_int addr_mod[16] = {
		0, 3, 2, 2, 1, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0
	};
	static const char *mode[16]  = {
		"?", ".b", ".b", ".h", ".b", "?", "?", "?",
		".b", "?", "?" , "?" , ".h" , "?", "?", ""
	};
	static const u_int mask[16] = {
		0, 0xff, 0xff00, 0xffff,
		0xff0000, 0, 0, 0,
		0xff000000, 0, 0, 0,
		0xffff0000, 0, 0, 0xffffffff
	};
	static const u_int shift[16] = {
		0,  0, 8, 0, 16, 0, 0, 0,
		24, 0, 0, 0, 16, 0, 0, 0
	};
	int reg = DMT_DREGBITS(t);

	if (ISSET(t, DMT_LOCKBAR)) {
		db_printf("xmem%s%s r%d(0x%x) <-> mem(0x%x),",
		    DMT_ENBITS(t) == 0x0f ? "" : ".bu",
		    ISSET(t, DMT_DAS) ? "" : ".usr", reg,
		    ((t >> 2 & 0xf) == 0xf) ? d : (d & 0xff), a);
		return 1;
	} else if (DMT_ENBITS(t) == 0xf) {
		/* full or double word */
		if (ISSET(t, DMT_WRITE)) {
			if (ISSET(t, DMT_DOUB1) && no == 2)
				db_printf("st.d%s -> mem(0x%x) (** restart sxip **)",
				    ISSET(t, DMT_DAS) ? "" : ".usr", a);
			else
				db_printf("st%s (0x%x) -> mem(0x%x)",
				    ISSET(t, DMT_DAS) ? "" : ".usr", d, a);
		} else {
			/* load */
			if (ISSET(t, DMT_DOUB1) && no == 2)
				db_printf("ld.d%s r%d <- mem(0x%x), r%d <- mem(0x%x)",
				    ISSET(t, DMT_DAS) ? "" : ".usr", reg, a, reg+1, a+4);
			else
				db_printf("ld%s r%d <- mem(0x%x)",
				    ISSET(t, DMT_DAS) ? "" : ".usr", reg, a);
		}
	} else {
		/* fractional word - check if load or store */
		a += addr_mod[DMT_ENBITS(t)];
		if (ISSET(t, DMT_WRITE))
			db_printf("st%s%s (0x%x) -> mem(0x%x)",
			    mode[DMT_ENBITS(t)],
			    ISSET(t, DMT_DAS) ? "" : ".usr",
			    (d & mask[DMT_ENBITS(t)]) >> shift[DMT_ENBITS(t)],
			    a);
		else
			db_printf("ld%s%s%s r%d <- mem(0x%x)",
			    mode[DMT_ENBITS(t)],
			    ISSET(t, DMT_SIGNED) ? "" : "u",
			    ISSET(t, DMT_DAS) ? "" : ".usr", reg, a);
	}
	return (0);
}
#endif	/* M88100 */

void
m88k_db_print_frame(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	struct trapframe *s = (struct trapframe *)addr;
	const char *name;
	db_expr_t offset;
#ifdef M88100
	int suppress1 = 0, suppress2 = 0;
#endif
	int c, force = 0, help = 0;

	if (!have_addr) {
		db_printf("requires address of frame\n");
		help = 1;
	}

	while (modif && *modif) {
		switch (c = *modif++, c) {
		case 'f':
			force = 1;
			break;
		case 'h':
			help = 1;
			break;
		default:
			db_printf("unknown modifier [%c]\n", c);
			help = 1;
			break;
		}
	}

	if (help) {
		db_printf("usage: mach frame/[f] ADDRESS\n");
		db_printf("  /f force printing of insane frames.\n");
		return;
	}

	if (badaddr((vaddr_t)s, 4) ||
	    badaddr((vaddr_t)(&((db_regs_t*)s)->fpit), 4)) {
		db_printf("frame at %8p is unreadable\n", s);
		return;
	}

	if (frame_is_sane((db_regs_t *)s, 0) == 0) {
		if (force == 0)
			return;
	}

#define R(i) s->tf_r[i]
#define IPMASK(x) ((x) &  ~(3))
	db_printf("R00-05: 0x%08lx  0x%08lx  0x%08lx  0x%08lx  0x%08lx  0x%08lx\n",
	    R(0), R(1), R(2), R(3), R(4), R(5));
	db_printf("R06-11: 0x%08lx  0x%08lx  0x%08lx  0x%08lx  0x%08lx  0x%08lx\n",
	    R(6), R(7), R(8), R(9), R(10), R(11));
	db_printf("R12-17: 0x%08lx  0x%08lx  0x%08lx  0x%08lx  0x%08lx  0x%08lx\n",
	    R(12), R(13), R(14), R(15), R(16), R(17));
	db_printf("R18-23: 0x%08lx  0x%08lx  0x%08lx  0x%08lx  0x%08lx  0x%08lx\n",
	    R(18), R(19), R(20), R(21), R(22), R(23));
	db_printf("R24-29: 0x%08lx  0x%08lx  0x%08lx  0x%08lx  0x%08lx  0x%08lx\n",
	    R(24), R(25), R(26), R(27), R(28), R(29));
	db_printf("R30-31: 0x%08lx  0x%08lx\n", R(30), R(31));

	db_printf("%cxip: 0x%08lx ",
	    CPU_IS88110 ? 'e' : 's', s->tf_sxip & XIP_ADDR);
	db_find_xtrn_sym_and_offset((vaddr_t)IPMASK(s->tf_sxip),
	    &name, &offset);
	if (name != NULL && (u_int)offset <= db_maxoff)
		db_printf("%s+0x%08x", name, (u_int)offset);
	db_printf("\n");

	if (s->tf_snip != s->tf_sxip + 4) {
		db_printf("%cnip: 0x%08lx ",
		    CPU_IS88110 ? 'e' : 's', s->tf_snip);
		db_find_xtrn_sym_and_offset((vaddr_t)IPMASK(s->tf_snip),
		    &name, &offset);
		if (name != NULL && (u_int)offset <= db_maxoff)
			db_printf("%s+0x%08x", name, (u_int)offset);
		db_printf("\n");
	}

#ifdef M88100
	if (CPU_IS88100) {
		if (s->tf_sfip != s->tf_snip + 4) {
			db_printf("sfip: 0x%08lx ", s->tf_sfip);
			db_find_xtrn_sym_and_offset((vaddr_t)IPMASK(s->tf_sfip),
			    &name, &offset);
			if (name != NULL && (u_int)offset <= db_maxoff)
				db_printf("%s+0x%08x", name, (u_int)offset);
			db_printf("\n");
		}
	}
#endif
#ifdef M88110
	if (CPU_IS88110) {
		db_printf("fpsr: 0x%08lx fpcr: 0x%08lx fpecr: 0x%08lx\n",
			  s->tf_fpsr, s->tf_fpcr, s->tf_fpecr);
		db_printf("dsap 0x%08lx duap 0x%08lx dsr 0x%08lx dlar 0x%08lx dpar 0x%08lx\n",
			  s->tf_dsap, s->tf_duap, s->tf_dsr, s->tf_dlar, s->tf_dpar);
		db_printf("isap 0x%08lx iuap 0x%08lx isr 0x%08lx ilar 0x%08lx ipar 0x%08lx\n",
			  s->tf_isap, s->tf_iuap, s->tf_isr, s->tf_ilar, s->tf_ipar);
	}
#endif

	db_printf("epsr: 0x%08lx                current process: %p\n",
		  s->tf_epsr, curproc);
	db_printf("vector: 0x%02lx                    interrupt mask: 0x%08lx\n",
		  s->tf_vector, s->tf_mask);

	/*
	 * If the vector indicates trap, instead of an exception or
	 * interrupt, skip the check of dmt and fp regs.
	 *
	 * Interrupt and exceptions are vectored at 0-10 and 114-127.
	 */
	if (!(s->tf_vector <= 10 ||
	    (114 <= s->tf_vector && s->tf_vector <= 127))) {
		db_printf("\n");
		return;
	}

#ifdef M88100
	if (CPU_IS88100) {
		if (s->tf_vector == /*data*/3 || s->tf_dmt0 & DMT_VALID) {
			db_printf("dmt,d,a0: 0x%08lx  0x%08lx  0x%08lx ",
			    s->tf_dmt0, s->tf_dmd0, s->tf_dma0);
			db_find_xtrn_sym_and_offset((vaddr_t)s->tf_dma0,
			    &name, &offset);
			if (name != NULL && (u_int)offset <= db_maxoff)
				db_printf("%s+0x%08x", name, (u_int)offset);
			db_printf("\n          ");

			suppress1 = m88k_dmx_print(s->tf_dmt0, s->tf_dmd0,
			    s->tf_dma0, 0);
			db_printf("\n");

			if ((s->tf_dmt1 & DMT_VALID) && (!suppress1)) {
				db_printf("dmt,d,a1: 0x%08lx  0x%08lx  0x%08lx ",
				    s->tf_dmt1, s->tf_dmd1, s->tf_dma1);
				db_find_xtrn_sym_and_offset((vaddr_t)s->tf_dma1,
				    &name, &offset);
				if (name != NULL && (u_int)offset <= db_maxoff)
					db_printf("%s+0x%08x", name,
					    (u_int)offset);
				db_printf("\n          ");
				suppress2 = m88k_dmx_print(s->tf_dmt1,
				    s->tf_dmd1, s->tf_dma1, 1);
				db_printf("\n");

				if ((s->tf_dmt2 & DMT_VALID) && (!suppress2)) {
					db_printf("dmt,d,a2: 0x%08lx  0x%08lx  0x%08lx ",
						  s->tf_dmt2, s->tf_dmd2, s->tf_dma2);
					db_find_xtrn_sym_and_offset((vaddr_t)s->tf_dma2,
					    &name, &offset);
					if (name != 0 &&
					    (u_int)offset <= db_maxoff)
						db_printf("%s+0x%08x", name,
						    (u_int)offset);
					db_printf("\n          ");
					m88k_dmx_print(s->tf_dmt2, s->tf_dmd2,
					    s->tf_dma2, 2);
					db_printf("\n");
				}
			}

			db_printf("fault code %ld\n",
			    CMMU_PFSR_FAULT(s->tf_dpfsr));
		}
	}
#endif	/* M88100 */

	if (s->tf_fpecr & 255) { /* floating point error occurred */
		db_printf("fpecr: 0x%08lx fpsr: 0x%08lx fpcr: 0x%08lx\n",
		    s->tf_fpecr, s->tf_fpsr, s->tf_fpcr);
#ifdef M88100
		if (CPU_IS88100) {
			db_printf("fcr1-4: 0x%08lx  0x%08lx  0x%08lx  0x%08lx\n",
			    s->tf_fphs1, s->tf_fpls1, s->tf_fphs2, s->tf_fpls2);
			db_printf("fcr5-8: 0x%08lx  0x%08lx  0x%08lx  0x%08lx\n",
			    s->tf_fppt, s->tf_fprh, s->tf_fprl, s->tf_fpit);
		}
#endif
	}
	db_printf("\n");
}

void
m88k_db_registers(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	m88k_db_print_frame((db_expr_t)&ddb_regs, TRUE, 0, modif);
}

/*
 * m88k_db_trap - field a TRACE or BPT trap
 * Note that only the tf_regs part of the frame is valid - some ddb routines
 * invoke this function with a promoted struct reg!
 */
void
m88k_db_trap(int type, struct trapframe *frame)
{
#ifdef MULTIPROCESSOR
	struct cpu_info *ci = curcpu();
#endif

	switch(type) {
	case T_KDB_BREAK:
	case T_KDB_TRACE:
	case T_KDB_ENTRY:
		break;
	case -1:
		break;
	default:
		kdbprinttrap(type);
		if (db_recover != 0) {
			db_error("Caught exception in ddb.\n");
			/*NOTREACHED*/
		}
	}

#ifdef MULTIPROCESSOR
	ci->ci_ddb_state = CI_DDB_ENTERDDB;
	__mp_lock(&ddb_mp_lock);
	ci->ci_ddb_state = CI_DDB_INDDB;
	ddb_mp_nextcpu = (cpuid_t)-1;
	m88k_broadcast_ipi(CI_IPI_DDB);		/* pause other processors */
#endif

	ddb_regs = frame->tf_regs;

	db_active++;
	cnpollc(1);
	db_trap(type, 0);
	cnpollc(0);
	db_active--;

	frame->tf_regs = ddb_regs;

#ifdef MULTIPROCESSOR
	ci->ci_ddb_state = CI_DDB_RUNNING;
	__mp_release_all(&ddb_mp_lock);
#endif
}

extern const char *trap_type[];
extern const int trap_types;

/*
 * Print trap reason.
 */
void
kdbprinttrap(int type)
{
	printf("kernel: ");
	if (type >= trap_types || type < 0)
		printf("type %d", type);
	else
		printf("%s", trap_type[type]);
	printf(" trap\n");
}

void
db_enter(void)
{
	asm (ENTRY_ASM); /* entry trap */
	/* ends up at ddb_entry_trap below */
	return;
}

/*
 * When the below routine is entered interrupts should be on
 * but spl should be high
 *
 * The following routine is for breakpoint and watchpoint entry.
 */

/* breakpoint/watchpoint entry */
int
ddb_break_trap(int type, db_regs_t *eframe)
{
	m88k_db_trap(type, (struct trapframe *)eframe);

	if (type == T_KDB_BREAK) {
		/*
		 * back up an instruction and retry the instruction
		 * at the breakpoint address.  mc88110's exip reg
		 * already has the address of the exception instruction.
		 */
		if (CPU_IS88100)
			m88100_rewind_insn(eframe);
	}

	return 0;
}

/* enter at splhigh */
int
ddb_entry_trap(int level, db_regs_t *eframe)
{
	m88k_db_trap(T_KDB_ENTRY, (struct trapframe *)eframe);

	return 0;
}

/*
 * Read bytes from kernel address space for debugger.
 */
void
db_read_bytes(vaddr_t addr, size_t size, void *datap)
{
	char *data = datap, *src;

	src = (char *)addr;

	while (size-- > 0) {
		*data++ = *src++;
	}
}

/*
 * Write bytes to kernel address space for debugger.
 */
void
db_write_bytes(vaddr_t addr, size_t size, void *datap)
{
	extern pt_entry_t *pmap_pte(pmap_t, vaddr_t);
	char *data = datap, *dst = (char *)addr;
	vaddr_t va;
	paddr_t pa;
	pt_entry_t *pte, opte, npte;
	size_t len, olen;
	int cpu = cpu_number();

	while (size != 0) {
		va = trunc_page((vaddr_t)dst);
#ifdef M88100
		if (CPU_IS88100 && va >= BATC8_VA)
			pte = NULL;
		else
#endif
			pte = pmap_pte(pmap_kernel(), va);
		if (pte != NULL) {
			opte = *pte;
			pa = (opte & PG_FRAME) | ((vaddr_t)dst & PAGE_MASK);
		}
		len = PAGE_SIZE - ((vaddr_t)dst & PAGE_MASK);
		if (len > size)
			len = size;
		size -= olen = len;

		if (pte != NULL && (opte & PG_RO)) {
			npte = opte & ~PG_RO;
			*pte = npte;
			cmmu_tlbis(cpu, va, npte);
		}
		while (len-- != 0)
			*dst++ = *data++;
		if (pte != NULL && (opte & PG_RO)) {
			*pte = opte;
			cmmu_tlbis(cpu, va, opte);
		}
		if (pte != NULL && (opte & (CACHE_INH | CACHE_WT)) == 0) {
			cmmu_dcache_wb(cpu, pa, olen);
			cmmu_icache_inv(cpu, pa, olen);
		}
	}
}

/* display where all the cpus are stopped at */
void
m88k_db_where(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	const char *name;
	db_expr_t offset;
	vaddr_t l;

	l = PC_REGS(&ddb_regs); /* clear low bits */

	db_find_xtrn_sym_and_offset(l, &name, &offset);
	if (name && (u_int)offset <= db_maxoff)
		db_printf("stopped at 0x%lx  (%s+0x%lx)\n", l, name, offset);
	else
		db_printf("stopped at 0x%lx\n", l);
}

/*
 * Walk back a stack, looking for exception frames.
 * These frames are recognized by the routine frame_is_sane. Frames
 * only start with zero, so we only call frame_is_sane if the
 * current address contains zero.
 *
 * If addr is given, it is assumed to an address on the stack to be
 * searched. Otherwise, r31 of the current cpu is used.
 */
void
m88k_db_frame_search(db_expr_t addr, int have_addr, db_expr_t count,
    char *modif)
{
	if (have_addr)
		addr &= ~3; /* round to word */
	else
		addr = (ddb_regs.r[31]);

	/* walk back up stack until 8k boundary, looking for 0 */
	while (addr & ((8 * 1024) - 1)) {
		if (frame_is_sane((db_regs_t *)addr, 1) != 0)
			db_printf("frame found at 0x%lx\n", addr);
		addr += 4;
	}

	db_printf("(Walked back until 0x%lx)\n",addr);
}

#ifdef MULTIPROCESSOR

void
m88k_db_cpu_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	cpuid_t cpu;
	struct cpu_info *ci;
	char state[15];

	/* switch to another processor if requested */
	if (have_addr) {
		cpu = (cpuid_t)addr;
		if (cpu >= 0 && cpu < MAX_CPUS &&
		    ISSET(m88k_cpus[cpu].ci_flags, CIF_ALIVE)) {
			ddb_mp_nextcpu = cpu;
			db_cmd_loop_done = 1;
		} else {
			db_printf("cpu%ld is not active\n", cpu);
		}
		return;
	}

	db_printf(" cpu  flags state          curproc  curpcb   depth    ipi\n");
	CPU_INFO_FOREACH(cpu, ci) {
		switch (ci->ci_ddb_state) {
		case CI_DDB_RUNNING:
			strlcpy(state, "running", sizeof state);
			break;
		case CI_DDB_ENTERDDB:
			strlcpy(state, "entering ddb", sizeof state);
			break;
		case CI_DDB_INDDB:
			strlcpy(state, "in ddb", sizeof state);
			break;
		case CI_DDB_PAUSE:
			strlcpy(state, "paused", sizeof state);
			break;
		default:
			snprintf(state, sizeof state, "unknown (%d)",
			    ci->ci_ddb_state);
			break;
		}
		db_printf("%ccpu%d   %02x  %-14s %08lx %08lx %3u %08x\n",
		    (cpu == cpu_number()) ? '*' : ' ', CPU_INFO_UNIT(ci),
		    ci->ci_flags, state, (register_t)ci->ci_curproc,
		    (register_t)ci->ci_curpcb, ci->ci_idepth, ci->ci_ipi);
	}
}

#endif	/* MULTIPROCESSOR */

/************************/
/* COMMAND TABLE / INIT */
/************************/

const struct db_command db_machine_command_table[] = {
#ifdef MULTIPROCESSOR
	{ "ddbcpu",	m88k_db_cpu_cmd,	0,	NULL },
#endif
	{ "frame",	m88k_db_print_frame,	0,	NULL },
	{ "regs",	m88k_db_registers,	0,	NULL },
	{ "searchframe",m88k_db_frame_search,	0,	NULL },
	{ "where",	m88k_db_where,		0,	NULL },
#if defined(EXTRA_MACHDEP_COMMANDS)
	EXTRA_MACHDEP_COMMANDS
#endif
	{ NULL,		NULL,			0,	NULL }
};

void
db_machine_init()
{
#ifdef MULTIPROCESSOR
	__mp_lock_init(&ddb_mp_lock);
#endif
}
