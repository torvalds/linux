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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kdb.h>
#include <sys/proc.h>
#include <sys/sysent.h>

#include <machine/cpu.h>
#include <machine/frame.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/reg.h>
#include <machine/stack.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>

#include <ddb/ddb.h>
#include <ddb/db_access.h>
#include <ddb/db_sym.h>
#include <ddb/db_variables.h>

static db_varfcn_t db_esp;
static db_varfcn_t db_frame;
static db_varfcn_t db_frame_seg;
static db_varfcn_t db_gs;
static db_varfcn_t db_ss;

/*
 * Machine register set.
 */
#define	DB_OFFSET(x)	(db_expr_t *)offsetof(struct trapframe, x)
struct db_variable db_regs[] = {
	{ "cs",		DB_OFFSET(tf_cs),	db_frame_seg },
	{ "ds",		DB_OFFSET(tf_ds),	db_frame_seg },
	{ "es",		DB_OFFSET(tf_es),	db_frame_seg },
	{ "fs",		DB_OFFSET(tf_fs),	db_frame_seg },
	{ "gs",		NULL,			db_gs },
	{ "ss",		NULL,			db_ss },
	{ "eax",	DB_OFFSET(tf_eax),	db_frame },
	{ "ecx",	DB_OFFSET(tf_ecx),	db_frame },
	{ "edx",	DB_OFFSET(tf_edx),	db_frame },
	{ "ebx",	DB_OFFSET(tf_ebx),	db_frame },
	{ "esp",	NULL,			db_esp },
	{ "ebp",	DB_OFFSET(tf_ebp),	db_frame },
	{ "esi",	DB_OFFSET(tf_esi),	db_frame },
	{ "edi",	DB_OFFSET(tf_edi),	db_frame },
	{ "eip",	DB_OFFSET(tf_eip),	db_frame },
	{ "efl",	DB_OFFSET(tf_eflags),	db_frame },
};
struct db_variable *db_eregs = db_regs + nitems(db_regs);

static __inline int
get_esp(struct trapframe *tf)
{
	return (TF_HAS_STACKREGS(tf) ? tf->tf_esp : (intptr_t)&tf->tf_esp);
}

static int
db_frame(struct db_variable *vp, db_expr_t *valuep, int op)
{
	int *reg;

	if (kdb_frame == NULL)
		return (0);

	reg = (int *)((uintptr_t)kdb_frame + (db_expr_t)vp->valuep);
	if (op == DB_VAR_GET)
		*valuep = *reg;
	else
		*reg = *valuep;
	return (1);
}

static int
db_frame_seg(struct db_variable *vp, db_expr_t *valuep, int op)
{
	struct trapframe_vm86 *tfp;
	int off;
	uint16_t *reg;

	if (kdb_frame == NULL)
		return (0);

	off = (intptr_t)vp->valuep;
	if (kdb_frame->tf_eflags & PSL_VM) {
		tfp = (void *)kdb_frame;
		switch ((intptr_t)vp->valuep) {
		case (intptr_t)DB_OFFSET(tf_cs):
			reg = (uint16_t *)&tfp->tf_cs;
			break;
		case (intptr_t)DB_OFFSET(tf_ds):
			reg = (uint16_t *)&tfp->tf_vm86_ds;
			break;
		case (intptr_t)DB_OFFSET(tf_es):
			reg = (uint16_t *)&tfp->tf_vm86_es;
			break;
		case (intptr_t)DB_OFFSET(tf_fs):
			reg = (uint16_t *)&tfp->tf_vm86_fs;
			break;
		}
	} else
		reg = (uint16_t *)((uintptr_t)kdb_frame + off);
	if (op == DB_VAR_GET)
		*valuep = *reg;
	else
		*reg = *valuep;
	return (1);
}

static int
db_esp(struct db_variable *vp, db_expr_t *valuep, int op)
{

	if (kdb_frame == NULL)
		return (0);

	if (op == DB_VAR_GET)
		*valuep = get_esp(kdb_frame);
	else if (TF_HAS_STACKREGS(kdb_frame))
		kdb_frame->tf_esp = *valuep;
	return (1);
}

static int
db_gs(struct db_variable *vp, db_expr_t *valuep, int op)
{
	struct trapframe_vm86 *tfp;

	if (kdb_frame != NULL && kdb_frame->tf_eflags & PSL_VM) {
		tfp = (void *)kdb_frame;
		if (op == DB_VAR_GET)
			*valuep = tfp->tf_vm86_gs;
		else
			tfp->tf_vm86_gs = *valuep;
		return (1);
	}
	if (op == DB_VAR_GET)
		*valuep = rgs();
	else
		load_gs(*valuep);
	return (1);
}

static int
db_ss(struct db_variable *vp, db_expr_t *valuep, int op)
{

	if (kdb_frame == NULL)
		return (0);

	if (op == DB_VAR_GET)
		*valuep = TF_HAS_STACKREGS(kdb_frame) ? kdb_frame->tf_ss :
		    rss();
	else if (TF_HAS_STACKREGS(kdb_frame))
		kdb_frame->tf_ss = *valuep;
	return (1);
}

#define NORMAL		0
#define	TRAP		1
#define	INTERRUPT	2
#define	SYSCALL		3
#define	DOUBLE_FAULT	4
#define	TRAP_INTERRUPT	5
#define	TRAP_TIMERINT	6

static void db_nextframe(struct i386_frame **, db_addr_t *, struct thread *);
static int db_numargs(struct i386_frame *);
static void db_print_stack_entry(const char *, int, char **, int *, db_addr_t,
    void *);
static void decode_syscall(int, struct thread *);

static const char * watchtype_str(int type);
int  i386_set_watch(int watchnum, unsigned int watchaddr, int size, int access,
		    struct dbreg *d);
int  i386_clr_watch(int watchnum, struct dbreg *d);

/*
 * Figure out how many arguments were passed into the frame at "fp".
 */
static int
db_numargs(fp)
	struct i386_frame *fp;
{
	char   *argp;
	int	inst;
	int	args;

	argp = (char *)db_get_value((int)&fp->f_retaddr, 4, false);
	/*
	 * XXX etext is wrong for LKMs.  We should attempt to interpret
	 * the instruction at the return address in all cases.  This
	 * may require better fault handling.
	 */
	if (argp < btext || argp >= etext) {
		args = -1;
	} else {
retry:
		inst = db_get_value((int)argp, 4, false);
		if ((inst & 0xff) == 0x59)	/* popl %ecx */
			args = 1;
		else if ((inst & 0xffff) == 0xc483)	/* addl $Ibs, %esp */
			args = ((inst >> 16) & 0xff) / 4;
		else if ((inst & 0xf8ff) == 0xc089) {	/* movl %eax, %Reg */
			argp += 2;
			goto retry;
		} else
			args = -1;
	}
	return (args);
}

static void
db_print_stack_entry(name, narg, argnp, argp, callpc, frame)
	const char *name;
	int narg;
	char **argnp;
	int *argp;
	db_addr_t callpc;
	void *frame;
{
	int n = narg >= 0 ? narg : 5;

	db_printf("%s(", name);
	while (n) {
		if (argnp)
			db_printf("%s=", *argnp++);
		db_printf("%r", db_get_value((int)argp, 4, false));
		argp++;
		if (--n != 0)
			db_printf(",");
	}
	if (narg < 0)
		db_printf(",...");
	db_printf(") at ");
	db_printsym(callpc, DB_STGY_PROC);
	if (frame != NULL)
		db_printf("/frame 0x%r", (register_t)frame);
	db_printf("\n");
}

static void
decode_syscall(int number, struct thread *td)
{
	struct proc *p;
	c_db_sym_t sym;
	db_expr_t diff;
	sy_call_t *f;
	const char *symname;

	db_printf(" (%d", number);
	p = (td != NULL) ? td->td_proc : NULL;
	if (p != NULL && 0 <= number && number < p->p_sysent->sv_size) {
		f = p->p_sysent->sv_table[number].sy_call;
		sym = db_search_symbol((db_addr_t)f, DB_STGY_ANY, &diff);
		if (sym != DB_SYM_NULL && diff == 0) {
			db_symbol_values(sym, &symname, NULL);
			db_printf(", %s, %s", p->p_sysent->sv_name, symname);
		}
	}
	db_printf(")");
}

/*
 * Figure out the next frame up in the call stack.
 */
static void
db_nextframe(struct i386_frame **fp, db_addr_t *ip, struct thread *td)
{
	struct trapframe *tf;
	int frame_type;
	int eip, esp, ebp;
	db_expr_t offset;
	c_db_sym_t sym;
	const char *name;

	eip = db_get_value((int) &(*fp)->f_retaddr, 4, false);
	ebp = db_get_value((int) &(*fp)->f_frame, 4, false);

	/*
	 * Figure out frame type.  We look at the address just before
	 * the saved instruction pointer as the saved EIP is after the
	 * call function, and if the function being called is marked as
	 * dead (such as panic() at the end of dblfault_handler()), then
	 * the instruction at the saved EIP will be part of a different
	 * function (syscall() in this example) rather than the one that
	 * actually made the call.
	 */
	frame_type = NORMAL;
	if (eip >= PMAP_TRM_MIN_ADDRESS) {
		sym = db_search_symbol(eip - 1 - setidt_disp, DB_STGY_ANY,
		    &offset);
	} else {
		sym = db_search_symbol(eip - 1, DB_STGY_ANY, &offset);
	}
	db_symbol_values(sym, &name, NULL);
	if (name != NULL) {
		if (strcmp(name, "calltrap") == 0 ||
		    strcmp(name, "fork_trampoline") == 0)
			frame_type = TRAP;
		else if (strncmp(name, "Xatpic_intr", 11) == 0 ||
		    strncmp(name, "Xapic_isr", 9) == 0)
			frame_type = INTERRUPT;
		else if (strcmp(name, "Xlcall_syscall") == 0 ||
		    strcmp(name, "Xint0x80_syscall") == 0)
			frame_type = SYSCALL;
		else if (strcmp(name, "dblfault_handler") == 0)
			frame_type = DOUBLE_FAULT;
		/* XXX: These are interrupts with trap frames. */
		else if (strcmp(name, "Xtimerint") == 0)
			frame_type = TRAP_TIMERINT;
		else if (strcmp(name, "Xcpustop") == 0 ||
		    strcmp(name, "Xrendezvous") == 0 ||
		    strcmp(name, "Xipi_intr_bitmap_handler") == 0)
			frame_type = TRAP_INTERRUPT;
	}

	/*
	 * Normal frames need no special processing.
	 */
	if (frame_type == NORMAL) {
		*ip = (db_addr_t) eip;
		*fp = (struct i386_frame *) ebp;
		return;
	}

	db_print_stack_entry(name, 0, 0, 0, eip, &(*fp)->f_frame);

	/*
	 * For a double fault, we have to snag the values from the
	 * previous TSS since a double fault uses a task gate to
	 * switch to a known good state.
	 */
	if (frame_type == DOUBLE_FAULT) {
		esp = PCPU_GET(common_tssp)->tss_esp;
		eip = PCPU_GET(common_tssp)->tss_eip;
		ebp = PCPU_GET(common_tssp)->tss_ebp;
		db_printf(
		    "--- trap 0x17, eip = %#r, esp = %#r, ebp = %#r ---\n",
		    eip, esp, ebp);
		*ip = (db_addr_t) eip;
		*fp = (struct i386_frame *) ebp;
		return;
	}

	/*
	 * Point to base of trapframe which is just above the
	 * current frame.
	 */
	if (frame_type == INTERRUPT)
		tf = (struct trapframe *)((int)*fp + 16);
	else if (frame_type == TRAP_INTERRUPT)
		tf = (struct trapframe *)((int)*fp + 8);
	else
		tf = (struct trapframe *)((int)*fp + 12);

	esp = get_esp(tf);
	eip = tf->tf_eip;
	ebp = tf->tf_ebp;
	switch (frame_type) {
	case TRAP:
		db_printf("--- trap %#r", tf->tf_trapno);
		break;
	case SYSCALL:
		db_printf("--- syscall");
		decode_syscall(tf->tf_eax, td);
		break;
	case TRAP_TIMERINT:
	case TRAP_INTERRUPT:
	case INTERRUPT:
		db_printf("--- interrupt");
		break;
	default:
		panic("The moon has moved again.");
	}
	db_printf(", eip = %#r, esp = %#r, ebp = %#r ---\n", eip, esp, ebp);

	switch (frame_type) {
	case TRAP:
	case TRAP_TIMERINT:
	case TRAP_INTERRUPT:
	case INTERRUPT:
		if ((tf->tf_eflags & PSL_VM) != 0 ||
		    (tf->tf_cs & SEL_RPL_MASK) != 0)
			ebp = 0;
		break;
	case SYSCALL:
		ebp = 0;
		break;
	}
	
	*ip = (db_addr_t) eip;
	*fp = (struct i386_frame *) ebp;
}

static int
db_backtrace(struct thread *td, struct trapframe *tf, struct i386_frame *frame,
    db_addr_t pc, register_t sp, int count)
{
	struct i386_frame *actframe;
#define MAXNARG	16
	char *argnames[MAXNARG], **argnp = NULL;
	const char *name;
	int *argp;
	db_expr_t offset;
	c_db_sym_t sym;
	int instr, narg;
	bool first;

	if (db_segsize(tf) == 16) {
		db_printf(
"--- 16-bit%s, cs:eip = %#x:%#x, ss:esp = %#x:%#x, ebp = %#x, tf = %p ---\n",
		    (tf->tf_eflags & PSL_VM) ? " (vm86)" : "",
		    tf->tf_cs, tf->tf_eip,
		    TF_HAS_STACKREGS(tf) ? tf->tf_ss : rss(),
		    TF_HAS_STACKREGS(tf) ? tf->tf_esp : (intptr_t)&tf->tf_esp,
		    tf->tf_ebp, tf);
		return (0);
	}

	/* 'frame' can be null initially.  Just print the pc then. */
	if (frame == NULL)
		goto out;

	/*
	 * If an indirect call via an invalid pointer caused a trap,
	 * %pc contains the invalid address while the return address
	 * of the unlucky caller has been saved by CPU on the stack
	 * just before the trap frame.  In this case, try to recover
	 * the caller's address so that the first frame is assigned
	 * to the right spot in the right function, for that is where
	 * the failure actually happened.
	 *
	 * This trick depends on the fault address stashed in tf_err
	 * by trap_fatal() before entering KDB.
	 */
	if (kdb_frame && pc == kdb_frame->tf_err) {
		/*
		 * Find where the trap frame actually ends.
		 * It won't contain tf_esp or tf_ss unless crossing rings.
		 */
		if (TF_HAS_STACKREGS(kdb_frame))
			instr = (int)(kdb_frame + 1);
		else
			instr = (int)&kdb_frame->tf_esp;
		pc = db_get_value(instr, 4, false);
	}

	if (count == -1)
		count = 1024;

	first = true;
	while (count-- && !db_pager_quit) {
		sym = db_search_symbol(pc, DB_STGY_ANY, &offset);
		db_symbol_values(sym, &name, NULL);

		/*
		 * Attempt to determine a (possibly fake) frame that gives
		 * the caller's pc.  It may differ from `frame' if the
		 * current function never sets up a standard frame or hasn't
		 * set one up yet or has just discarded one.  The last two
		 * cases can be guessed fairly reliably for code generated
		 * by gcc.  The first case is too much trouble to handle in
		 * general because the amount of junk on the stack depends
		 * on the pc (the special handling of "calltrap", etc. in
		 * db_nextframe() works because the `next' pc is special).
		 */
		actframe = frame;
		if (first) {
			first = false;
			if (sym == C_DB_SYM_NULL && sp != 0) {
				/*
				 * If a symbol couldn't be found, we've probably
				 * jumped to a bogus location, so try and use
				 * the return address to find our caller.
				 */
				db_print_stack_entry(name, 0, 0, 0, pc,
				    NULL);
				pc = db_get_value(sp, 4, false);
				if (db_search_symbol(pc, DB_STGY_PROC,
				    &offset) == C_DB_SYM_NULL)
					break;
				continue;
			} else if (tf != NULL) {
				instr = db_get_value(pc, 4, false);
				if ((instr & 0xffffff) == 0x00e58955) {
					/* pushl %ebp; movl %esp, %ebp */
					actframe = (void *)(get_esp(tf) - 4);
				} else if ((instr & 0xffff) == 0x0000e589) {
					/* movl %esp, %ebp */
					actframe = (void *)get_esp(tf);
					if (tf->tf_ebp == 0) {
						/* Fake frame better. */
						frame = actframe;
					}
				} else if ((instr & 0xff) == 0x000000c3) {
					/* ret */
					actframe = (void *)(get_esp(tf) - 4);
				} else if (offset == 0) {
					/* Probably an assembler symbol. */
					actframe = (void *)(get_esp(tf) - 4);
				}
			} else if (strcmp(name, "fork_trampoline") == 0) {
				/*
				 * Don't try to walk back on a stack for a
				 * process that hasn't actually been run yet.
				 */
				db_print_stack_entry(name, 0, 0, 0, pc,
				    actframe);
				break;
			}
		}

		argp = &actframe->f_arg0;
		narg = MAXNARG;
		if (sym != NULL && db_sym_numargs(sym, &narg, argnames)) {
			argnp = argnames;
		} else {
			narg = db_numargs(frame);
		}

		db_print_stack_entry(name, narg, argnp, argp, pc, actframe);

		if (actframe != frame) {
			/* `frame' belongs to caller. */
			pc = (db_addr_t)
			    db_get_value((int)&actframe->f_retaddr, 4, false);
			continue;
		}

		db_nextframe(&frame, &pc, td);

out:
		/*
		 * 'frame' can be null here, either because it was initially
		 * null or because db_nextframe() found no frame.
		 * db_nextframe() may also have found a non-kernel frame.
		 * !INKERNEL() classifies both.  Stop tracing if either,
		 * after printing the pc if it is the kernel.
		 */
		if (frame == NULL || frame <= actframe) {
			sym = db_search_symbol(pc, DB_STGY_ANY, &offset);
			db_symbol_values(sym, &name, NULL);
			db_print_stack_entry(name, 0, 0, 0, pc, frame);
			break;
		}
	}

	return (0);
}

void
db_trace_self(void)
{
	struct i386_frame *frame;
	db_addr_t callpc;
	register_t ebp;

	__asm __volatile("movl %%ebp,%0" : "=r" (ebp));
	frame = (struct i386_frame *)ebp;
	callpc = (db_addr_t)db_get_value((int)&frame->f_retaddr, 4, false);
	frame = frame->f_frame;
	db_backtrace(curthread, NULL, frame, callpc, 0, -1);
}

int
db_trace_thread(struct thread *thr, int count)
{
	struct pcb *ctx;
	struct trapframe *tf;

	ctx = kdb_thr_ctx(thr);
	tf = thr == kdb_thread ? kdb_frame : NULL;
	return (db_backtrace(thr, tf, (struct i386_frame *)ctx->pcb_ebp,
	    ctx->pcb_eip, ctx->pcb_esp, count));
}

int
i386_set_watch(watchnum, watchaddr, size, access, d)
	int watchnum;
	unsigned int watchaddr;
	int size;
	int access;
	struct dbreg *d;
{
	int i, len;

	if (watchnum == -1) {
		for (i = 0; i < 4; i++)
			if (!DBREG_DR7_ENABLED(d->dr[7], i))
				break;
		if (i < 4)
			watchnum = i;
		else
			return (-1);
	}

	switch (access) {
	case DBREG_DR7_EXEC:
		size = 1; /* size must be 1 for an execution breakpoint */
		/* fall through */
	case DBREG_DR7_WRONLY:
	case DBREG_DR7_RDWR:
		break;
	default:
		return (-1);
	}

	/*
	 * we can watch a 1, 2, or 4 byte sized location
	 */
	switch (size) {
	case 1:
		len = DBREG_DR7_LEN_1;
		break;
	case 2:
		len = DBREG_DR7_LEN_2;
		break;
	case 4:
		len = DBREG_DR7_LEN_4;
		break;
	default:
		return (-1);
	}

	/* clear the bits we are about to affect */
	d->dr[7] &= ~DBREG_DR7_MASK(watchnum);

	/* set drN register to the address, N=watchnum */
	DBREG_DRX(d, watchnum) = watchaddr;

	/* enable the watchpoint */
	d->dr[7] |= DBREG_DR7_SET(watchnum, len, access,
	    DBREG_DR7_GLOBAL_ENABLE);

	return (watchnum);
}


int
i386_clr_watch(watchnum, d)
	int watchnum;
	struct dbreg *d;
{

	if (watchnum < 0 || watchnum >= 4)
		return (-1);

	d->dr[7] &= ~DBREG_DR7_MASK(watchnum);
	DBREG_DRX(d, watchnum) = 0;

	return (0);
}


int
db_md_set_watchpoint(addr, size)
	db_expr_t addr;
	db_expr_t size;
{
	struct dbreg d;
	int avail, i, wsize;

	fill_dbregs(NULL, &d);

	avail = 0;
	for(i = 0; i < 4; i++) {
		if (!DBREG_DR7_ENABLED(d.dr[7], i))
			avail++;
	}

	if (avail * 4 < size)
		return (-1);

	for (i = 0; i < 4 && (size > 0); i++) {
		if (!DBREG_DR7_ENABLED(d.dr[7], i)) {
			if (size > 2)
				wsize = 4;
			else
				wsize = size;
			i386_set_watch(i, addr, wsize,
				       DBREG_DR7_WRONLY, &d);
			addr += wsize;
			size -= wsize;
		}
	}

	set_dbregs(NULL, &d);

	return(0);
}


int
db_md_clr_watchpoint(addr, size)
	db_expr_t addr;
	db_expr_t size;
{
	struct dbreg d;
	int i;

	fill_dbregs(NULL, &d);

	for(i = 0; i < 4; i++) {
		if (DBREG_DR7_ENABLED(d.dr[7], i)) {
			if ((DBREG_DRX((&d), i) >= addr) &&
			    (DBREG_DRX((&d), i) < addr+size))
				i386_clr_watch(i, &d);

		}
	}

	set_dbregs(NULL, &d);

	return(0);
}


static const char *
watchtype_str(type)
	int type;
{
	switch (type) {
		case DBREG_DR7_EXEC   : return "execute";    break;
		case DBREG_DR7_RDWR   : return "read/write"; break;
		case DBREG_DR7_WRONLY : return "write";	     break;
		default		      : return "invalid";    break;
	}
}


void
db_md_list_watchpoints(void)
{
	struct dbreg d;
	int i, len, type;

	fill_dbregs(NULL, &d);

	db_printf("\nhardware watchpoints:\n");
	db_printf("  watch    status        type  len     address\n");
	db_printf("  -----  --------  ----------  ---  ----------\n");
	for (i = 0; i < 4; i++) {
		if (DBREG_DR7_ENABLED(d.dr[7], i)) {
			type = DBREG_DR7_ACCESS(d.dr[7], i);
			len = DBREG_DR7_LEN(d.dr[7], i);
			db_printf("  %-5d  %-8s  %10s  %3d  ",
			    i, "enabled", watchtype_str(type), len + 1);
			db_printsym((db_addr_t)DBREG_DRX(&d, i), DB_STGY_ANY);
			db_printf("\n");
		} else {
			db_printf("  %-5d  disabled\n", i);
		}
	}

	db_printf("\ndebug register values:\n");
	for (i = 0; i < 8; i++)
		if (i != 4 && i != 5)
			db_printf("  dr%d 0x%08x\n", i, DBREG_DRX(&d, i));
	db_printf("\n");
}
