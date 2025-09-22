/*	$OpenBSD: db_machdep.h,v 1.23 2022/10/21 18:55:42 miod Exp $	*/
/*	$NetBSD: db_machdep.h,v 1.12 2001/07/07 15:16:13 eeh Exp $ */

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
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

#ifndef	_MACHINE_DB_MACHDEP_H_
#define	_MACHINE_DB_MACHDEP_H_

/*
 * Machine-dependent defines for new kernel debugger.
 */

#include <uvm/uvm_extern.h>

#include <machine/frame.h>
#include <machine/psl.h>
#include <machine/trap.h>
#include <machine/reg.h>

/* end of mangling */

typedef	long		db_expr_t;	/* expression - signed */

struct trapstate {
	int64_t tstate;
	int64_t tpc;
	int64_t	tnpc;
	int64_t	tt;
};
#if 1
typedef struct {
	struct trapframe	ddb_tf;
	struct frame		ddb_fr;
	struct trapstate	ddb_ts[5];
	int			ddb_tl;
	struct fpstate		ddb_fpstate;
} db_regs_t;
#else
typedef struct db_regs {
	struct trapregs dbr_traps[4];
	int		dbr_y;
	char		dbr_tl;
	char		dbr_canrestore;
	char		dbr_cansave;
	char		dbr_cleanwin;
	char		dbr_cwp;
	char		dbr_wstate;
	int64_t		dbr_g[8];
	int64_t		dbr_ag[8];
	int64_t		dbr_ig[8];
	int64_t		dbr_mg[8];
	int64_t		dbr_out[8];
	int64_t		dbr_local[8];
	int64_t		dbr_in[8];
} db_regs_t;
#endif

extern	db_regs_t ddb_regs;	/* register state */
#define	DDB_TF		(&ddb_regs.ddb_tf)
#define	DDB_FR		(&ddb_regs.ddb_fr)
#define	DDB_FP		(&ddb_regs.ddb_fpstate)

#define	PC_REGS(regs)	((vaddr_t)(regs)->ddb_tf.tf_pc)
#define	SET_PC_REGS(regs, value)	(regs)->ddb_tf.tf_pc = (int32_t)(value)
#define	PC_ADVANCE(regs) do {				\
	vaddr_t n = (regs)->ddb_tf.tf_npc;		\
	(regs)->ddb_tf.tf_pc = n;			\
	(regs)->ddb_tf.tf_npc = n + 4;			\
} while(0)

#define	BKPT_INST	0x91d02001	/* breakpoint instruction */
#define	BKPT_SIZE	(4)		/* size of breakpoint inst */
#define	BKPT_SET(inst)	(BKPT_INST)

#define	IS_BREAKPOINT_TRAP(type, code)	\
	((type) == T_BREAKPOINT || (type) == T_KGDB_EXEC)
#define IS_WATCHPOINT_TRAP(type, code)	\
	((type) ==T_PA_WATCHPT || (type) == T_VA_WATCHPT)

/*
 * Sparc cpus have no hardware single-step.
 */
#define SOFTWARE_SSTEP

int		db_inst_trap_return(int inst);
int		db_inst_return(int inst);
int		db_inst_call(int inst);
int		db_inst_branch(int inst);
int		db_inst_unconditional_flow_transfer(int inst);
vaddr_t		db_branch_taken(int inst, vaddr_t pc, db_regs_t *regs);

#define inst_trap_return(ins)	db_inst_trap_return(ins)
#define inst_return(ins)	db_inst_return(ins)
#define inst_call(ins)		db_inst_call(ins)
#define inst_branch(ins)	db_inst_branch(ins)
#define	inst_unconditional_flow_transfer(ins) \
				db_inst_unconditional_flow_transfer(ins)
#define branch_taken(ins, pc, fun, regs) \
				db_branch_taken((ins), (pc), (regs))

/* see note in db_interface.c about reversed breakpoint addrs */
#define next_instr_address(pc, bd) \
	((bd) ? (pc) : ddb_regs.ddb_tf.tf_npc)

#define DB_MACHINE_COMMANDS

void db_machine_init(void);
int db_ktrap(int, struct trapframe *);

int db_enter_ddb(void);
void db_startcpu(struct cpu_info *);
void db_stopcpu(struct cpu_info *);

#define DDB_STATE_NOT_RUNNING	0
#define DDB_STATE_RUNNING	1
#define DDB_STATE_EXITING	2

/* Register device-specific method for triggering XIRs. */
void db_register_xir(void (*)(void *, int), void *);

#endif	/* _MACHINE_DB_MACHDEP_H_ */
