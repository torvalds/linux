/*
 * Copyright (c) 2015 Juniper Networks Inc.
 * All rights reserved.
 *
 * Developed by Semihalf.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/kdb.h>
#include <sys/pcpu.h>
#include <sys/smp.h>
#include <sys/systm.h>

#include <machine/atomic.h>
#include <machine/armreg.h>
#include <machine/cpu.h>
#include <machine/debug_monitor.h>
#include <machine/kdb.h>
#include <machine/pcb.h>
#include <machine/reg.h>

#include <ddb/ddb.h>
#include <ddb/db_access.h>
#include <ddb/db_sym.h>

enum dbg_t {
	DBG_TYPE_BREAKPOINT = 0,
	DBG_TYPE_WATCHPOINT = 1,
};

struct dbg_wb_conf {
	enum dbg_t		type;
	enum dbg_access_t	access;
	db_addr_t		address;
	db_expr_t		size;
	u_int			slot;
};

static int dbg_reset_state(void);
static int dbg_setup_breakpoint(db_expr_t, db_expr_t, u_int);
static int dbg_remove_breakpoint(u_int);
static u_int dbg_find_slot(enum dbg_t, db_expr_t);
static boolean_t dbg_check_slot_free(enum dbg_t, u_int);

static int dbg_remove_xpoint(struct dbg_wb_conf *);
static int dbg_setup_xpoint(struct dbg_wb_conf *);

static int dbg_capable_var;	/* Indicates that machine is capable of using
				   HW watchpoints/breakpoints */

static uint32_t dbg_model;	/* Debug Arch. Model */
static boolean_t dbg_ossr;	/* OS Save and Restore implemented */

static uint32_t dbg_watchpoint_num;
static uint32_t dbg_breakpoint_num;

/* ID_DFR0 - Debug Feature Register 0 */
#define	ID_DFR0_CP_DEBUG_M_SHIFT	0
#define	ID_DFR0_CP_DEBUG_M_MASK		(0xF << ID_DFR0_CP_DEBUG_M_SHIFT)
#define	ID_DFR0_CP_DEBUG_M_NS		(0x0) /* Not supported */
#define	ID_DFR0_CP_DEBUG_M_V6		(0x2) /* v6 Debug arch. CP14 access */
#define	ID_DFR0_CP_DEBUG_M_V6_1		(0x3) /* v6.1 Debug arch. CP14 access */
#define	ID_DFR0_CP_DEBUG_M_V7		(0x4) /* v7 Debug arch. CP14 access */
#define	ID_DFR0_CP_DEBUG_M_V7_1		(0x5) /* v7.1 Debug arch. CP14 access */

/* DBGDIDR - Debug ID Register */
#define	DBGDIDR_WRPS_SHIFT		28
#define	DBGDIDR_WRPS_MASK		(0xF << DBGDIDR_WRPS_SHIFT)
#define	DBGDIDR_WRPS_NUM(reg)		\
    ((((reg) & DBGDIDR_WRPS_MASK) >> DBGDIDR_WRPS_SHIFT) + 1)

#define	DBGDIDR_BRPS_SHIFT		24
#define	DBGDIDR_BRPS_MASK		(0xF << DBGDIDR_BRPS_SHIFT)
#define	DBGDIDR_BRPS_NUM(reg)		\
    ((((reg) & DBGDIDR_BRPS_MASK) >> DBGDIDR_BRPS_SHIFT) + 1)

/* DBGPRSR - Device Powerdown and Reset Status Register */
#define	DBGPRSR_PU			(1 << 0) /* Powerup status */

/* DBGOSLSR - OS Lock Status Register */
#define	DBGOSLSR_OSLM0			(1 << 0)

/* DBGOSDLR - OS Double Lock Register */
#define	DBGPRSR_DLK			(1 << 0) /* OS Double Lock set */

/* DBGDSCR - Debug Status and Control Register */
#define	DBGSCR_MDBG_EN			(1 << 15) /* Monitor debug-mode enable */

/* DBGWVR - Watchpoint Value Register */
#define	DBGWVR_ADDR_MASK		(~0x3U)

/* Watchpoints/breakpoints control register bitfields */
#define	DBG_WB_CTRL_LEN_1		(0x1 << 5)
#define	DBG_WB_CTRL_LEN_2		(0x3 << 5)
#define	DBG_WB_CTRL_LEN_4		(0xf << 5)
#define	DBG_WB_CTRL_LEN_8		(0xff << 5)
#define	DBG_WB_CTRL_LEN_MASK(x)	((x) & (0xff << 5))
#define	DBG_WB_CTRL_EXEC		(0x0 << 3)
#define	DBG_WB_CTRL_LOAD		(0x1 << 3)
#define	DBG_WB_CTRL_STORE		(0x2 << 3)
#define	DBG_WB_CTRL_ACCESS_MASK(x)	((x) & (0x3 << 3))

/* Common for breakpoint and watchpoint */
#define	DBG_WB_CTRL_PL1		(0x1 << 1)
#define	DBG_WB_CTRL_PL0		(0x2 << 1)
#define	DBG_WB_CTRL_PLX_MASK(x)	((x) & (0x3 << 1))
#define	DBG_WB_CTRL_E		(0x1 << 0)

/*
 * Watchpoint/breakpoint helpers
 */
#define	DBG_BKPT_BT_SLOT	0	/* Slot for branch taken */
#define	DBG_BKPT_BNT_SLOT	1	/* Slot for branch not taken */

#define	OP2_SHIFT		4

/* Opc2 numbers for coprocessor instructions */
#define	DBG_WB_BVR	4
#define	DBG_WB_BCR	5
#define	DBG_WB_WVR	6
#define	DBG_WB_WCR	7

#define	DBG_REG_BASE_BVR	(DBG_WB_BVR << OP2_SHIFT)
#define	DBG_REG_BASE_BCR	(DBG_WB_BCR << OP2_SHIFT)
#define	DBG_REG_BASE_WVR	(DBG_WB_WVR << OP2_SHIFT)
#define	DBG_REG_BASE_WCR	(DBG_WB_WCR << OP2_SHIFT)

#define	DBG_WB_READ(cn, cm, op2, val) do {					\
	__asm __volatile("mrc p14, 0, %0, " #cn "," #cm "," #op2 : "=r" (val));	\
} while (0)

#define	DBG_WB_WRITE(cn, cm, op2, val) do {					\
	__asm __volatile("mcr p14, 0, %0, " #cn "," #cm "," #op2 :: "r" (val));	\
} while (0)

#define	READ_WB_REG_CASE(op2, m, val)			\
	case (((op2) << OP2_SHIFT) + m):		\
		DBG_WB_READ(c0, c ## m, op2, val);	\
		break

#define	WRITE_WB_REG_CASE(op2, m, val)			\
	case (((op2) << OP2_SHIFT) + m):		\
		DBG_WB_WRITE(c0, c ## m, op2, val);	\
		break

#define	SWITCH_CASES_READ_WB_REG(op2, val)	\
	READ_WB_REG_CASE(op2,  0, val);		\
	READ_WB_REG_CASE(op2,  1, val);		\
	READ_WB_REG_CASE(op2,  2, val);		\
	READ_WB_REG_CASE(op2,  3, val);		\
	READ_WB_REG_CASE(op2,  4, val);		\
	READ_WB_REG_CASE(op2,  5, val);		\
	READ_WB_REG_CASE(op2,  6, val);		\
	READ_WB_REG_CASE(op2,  7, val);		\
	READ_WB_REG_CASE(op2,  8, val);		\
	READ_WB_REG_CASE(op2,  9, val);		\
	READ_WB_REG_CASE(op2, 10, val);		\
	READ_WB_REG_CASE(op2, 11, val);		\
	READ_WB_REG_CASE(op2, 12, val);		\
	READ_WB_REG_CASE(op2, 13, val);		\
	READ_WB_REG_CASE(op2, 14, val);		\
	READ_WB_REG_CASE(op2, 15, val)

#define	SWITCH_CASES_WRITE_WB_REG(op2, val)	\
	WRITE_WB_REG_CASE(op2,  0, val);	\
	WRITE_WB_REG_CASE(op2,  1, val);	\
	WRITE_WB_REG_CASE(op2,  2, val);	\
	WRITE_WB_REG_CASE(op2,  3, val);	\
	WRITE_WB_REG_CASE(op2,  4, val);	\
	WRITE_WB_REG_CASE(op2,  5, val);	\
	WRITE_WB_REG_CASE(op2,  6, val);	\
	WRITE_WB_REG_CASE(op2,  7, val);	\
	WRITE_WB_REG_CASE(op2,  8, val);	\
	WRITE_WB_REG_CASE(op2,  9, val);	\
	WRITE_WB_REG_CASE(op2, 10, val);	\
	WRITE_WB_REG_CASE(op2, 11, val);	\
	WRITE_WB_REG_CASE(op2, 12, val);	\
	WRITE_WB_REG_CASE(op2, 13, val);	\
	WRITE_WB_REG_CASE(op2, 14, val);	\
	WRITE_WB_REG_CASE(op2, 15, val)

static uint32_t
dbg_wb_read_reg(int reg, int n)
{
	uint32_t val;

	val = 0;

	switch (reg + n) {
	SWITCH_CASES_READ_WB_REG(DBG_WB_WVR, val);
	SWITCH_CASES_READ_WB_REG(DBG_WB_WCR, val);
	SWITCH_CASES_READ_WB_REG(DBG_WB_BVR, val);
	SWITCH_CASES_READ_WB_REG(DBG_WB_BCR, val);
	default:
		db_printf(
		    "trying to read from CP14 reg. using wrong opc2 %d\n",
		    reg >> OP2_SHIFT);
	}

	return (val);
}

static void
dbg_wb_write_reg(int reg, int n, uint32_t val)
{

	switch (reg + n) {
	SWITCH_CASES_WRITE_WB_REG(DBG_WB_WVR, val);
	SWITCH_CASES_WRITE_WB_REG(DBG_WB_WCR, val);
	SWITCH_CASES_WRITE_WB_REG(DBG_WB_BVR, val);
	SWITCH_CASES_WRITE_WB_REG(DBG_WB_BCR, val);
	default:
		db_printf(
		    "trying to write to CP14 reg. using wrong opc2 %d\n",
		    reg >> OP2_SHIFT);
	}
	isb();
}

static __inline boolean_t
dbg_capable(void)
{

	return (atomic_cmpset_int(&dbg_capable_var, 0, 0) == 0);
}

boolean_t
kdb_cpu_pc_is_singlestep(db_addr_t pc)
{
	/*
	 * XXX: If the platform fails to enable its debug arch.
	 *      there will be no stepping capabilities
	 *      (SOFTWARE_SSTEP is not defined for __ARM_ARCH >= 6).
	 */
	if (!dbg_capable())
		return (FALSE);

	if (dbg_find_slot(DBG_TYPE_BREAKPOINT, pc) != ~0U)
		return (TRUE);

	return (FALSE);
}

void
kdb_cpu_set_singlestep(void)
{
	db_expr_t inst;
	db_addr_t pc, brpc;
	uint32_t wcr;
	u_int i;

	if (!dbg_capable())
		return;

	/*
	 * Disable watchpoints, e.g. stepping over watched instruction will
	 * trigger break exception instead of single-step exception and locks
	 * CPU on that instruction for ever.
	 */
	for (i = 0; i < dbg_watchpoint_num; i++) {
		wcr = dbg_wb_read_reg(DBG_REG_BASE_WCR, i);
		if ((wcr & DBG_WB_CTRL_E) != 0) {
			dbg_wb_write_reg(DBG_REG_BASE_WCR, i,
			    (wcr & ~DBG_WB_CTRL_E));
		}
	}

	pc = PC_REGS();

	inst = db_get_value(pc, sizeof(pc), FALSE);
	if (inst_branch(inst) || inst_call(inst) || inst_return(inst)) {
		brpc = branch_taken(inst, pc);
		dbg_setup_breakpoint(brpc, INSN_SIZE, DBG_BKPT_BT_SLOT);
	}
	pc = next_instr_address(pc, 0);
	dbg_setup_breakpoint(pc, INSN_SIZE, DBG_BKPT_BNT_SLOT);
}

void
kdb_cpu_clear_singlestep(void)
{
	uint32_t wvr, wcr;
	u_int i;

	if (!dbg_capable())
		return;

	dbg_remove_breakpoint(DBG_BKPT_BT_SLOT);
	dbg_remove_breakpoint(DBG_BKPT_BNT_SLOT);

	/* Restore all watchpoints */
	for (i = 0; i < dbg_watchpoint_num; i++) {
		wcr = dbg_wb_read_reg(DBG_REG_BASE_WCR, i);
		wvr = dbg_wb_read_reg(DBG_REG_BASE_WVR, i);
		/* Watchpoint considered not empty if address value is not 0 */
		if ((wvr & DBGWVR_ADDR_MASK) != 0) {
			dbg_wb_write_reg(DBG_REG_BASE_WCR, i,
			    (wcr | DBG_WB_CTRL_E));
		}
	}
}

int
dbg_setup_watchpoint(db_expr_t addr, db_expr_t size, enum dbg_access_t access)
{
	struct dbg_wb_conf conf;

	if (access == HW_BREAKPOINT_X) {
		db_printf("Invalid access type for watchpoint: %d\n", access);
		return (EINVAL);
	}

	conf.address = addr;
	conf.size = size;
	conf.access = access;
	conf.type = DBG_TYPE_WATCHPOINT;

	return (dbg_setup_xpoint(&conf));
}

int
dbg_remove_watchpoint(db_expr_t addr, db_expr_t size __unused)
{
	struct dbg_wb_conf conf;

	conf.address = addr;
	conf.type = DBG_TYPE_WATCHPOINT;

	return (dbg_remove_xpoint(&conf));
}

static int
dbg_setup_breakpoint(db_expr_t addr, db_expr_t size, u_int slot)
{
	struct dbg_wb_conf conf;

	conf.address = addr;
	conf.size = size;
	conf.access = HW_BREAKPOINT_X;
	conf.type = DBG_TYPE_BREAKPOINT;
	conf.slot = slot;

	return (dbg_setup_xpoint(&conf));
}

static int
dbg_remove_breakpoint(u_int slot)
{
	struct dbg_wb_conf conf;

	/* Slot already cleared. Don't recurse */
	if (dbg_check_slot_free(DBG_TYPE_BREAKPOINT, slot))
		return (0);

	conf.slot = slot;
	conf.type = DBG_TYPE_BREAKPOINT;

	return (dbg_remove_xpoint(&conf));
}

static const char *
dbg_watchtype_str(uint32_t type)
{

	switch (type) {
		case DBG_WB_CTRL_EXEC:
			return ("execute");
		case DBG_WB_CTRL_STORE:
			return ("write");
		case DBG_WB_CTRL_LOAD:
			return ("read");
		case DBG_WB_CTRL_LOAD | DBG_WB_CTRL_STORE:
			return ("read/write");
		default:
			return ("invalid");
	}
}

static int
dbg_watchtype_len(uint32_t len)
{

	switch (len) {
	case DBG_WB_CTRL_LEN_1:
		return (1);
	case DBG_WB_CTRL_LEN_2:
		return (2);
	case DBG_WB_CTRL_LEN_4:
		return (4);
	case DBG_WB_CTRL_LEN_8:
		return (8);
	default:
		return (0);
	}
}

void
dbg_show_watchpoint(void)
{
	uint32_t wcr, len, type;
	uint32_t addr;
	boolean_t is_enabled;
	int i;

	if (!dbg_capable()) {
		db_printf("Architecture does not support HW "
		    "breakpoints/watchpoints\n");
		return;
	}

	db_printf("\nhardware watchpoints:\n");
	db_printf("  watch    status        type  len     address              symbol\n");
	db_printf("  -----  --------  ----------  ---  ----------  ------------------\n");
	for (i = 0; i < dbg_watchpoint_num; i++) {
		wcr = dbg_wb_read_reg(DBG_REG_BASE_WCR, i);
		if ((wcr & DBG_WB_CTRL_E) != 0)
			is_enabled = TRUE;
		else
			is_enabled = FALSE;

		type = DBG_WB_CTRL_ACCESS_MASK(wcr);
		len = DBG_WB_CTRL_LEN_MASK(wcr);
		addr = dbg_wb_read_reg(DBG_REG_BASE_WVR, i) & DBGWVR_ADDR_MASK;
		db_printf("  %-5d  %-8s  %10s  %3d  0x%08x  ", i,
		    is_enabled ? "enabled" : "disabled",
		    is_enabled ? dbg_watchtype_str(type) : "",
		    is_enabled ? dbg_watchtype_len(len) : 0,
		    addr);
		db_printsym((db_addr_t)addr, DB_STGY_ANY);
		db_printf("\n");
	}
}

static boolean_t
dbg_check_slot_free(enum dbg_t type, u_int slot)
{
	uint32_t cr, vr;
	uint32_t max;

	switch(type) {
	case DBG_TYPE_BREAKPOINT:
		max = dbg_breakpoint_num;
		cr = DBG_REG_BASE_BCR;
		vr = DBG_REG_BASE_BVR;
		break;
	case DBG_TYPE_WATCHPOINT:
		max = dbg_watchpoint_num;
		cr = DBG_REG_BASE_WCR;
		vr = DBG_REG_BASE_WVR;
		break;
	default:
		db_printf("%s: Unsupported event type %d\n", __func__, type);
		return (FALSE);
	}

	if (slot >= max) {
		db_printf("%s: Invalid slot number %d, max %d\n",
		    __func__, slot, max - 1);
		return (FALSE);
	}

	if ((dbg_wb_read_reg(cr, slot) & DBG_WB_CTRL_E) == 0 &&
	    (dbg_wb_read_reg(vr, slot) & DBGWVR_ADDR_MASK) == 0)
		return (TRUE);

	return (FALSE);
}

static u_int
dbg_find_free_slot(enum dbg_t type)
{
	u_int max, i;

	switch(type) {
	case DBG_TYPE_BREAKPOINT:
		max = dbg_breakpoint_num;
		break;
	case DBG_TYPE_WATCHPOINT:
		max = dbg_watchpoint_num;
		break;
	default:
		db_printf("Unsupported debug type\n");
		return (~0U);
	}

	for (i = 0; i < max; i++) {
		if (dbg_check_slot_free(type, i))
			return (i);
	}

	return (~0U);
}

static u_int
dbg_find_slot(enum dbg_t type, db_expr_t addr)
{
	uint32_t reg_addr, reg_ctrl;
	u_int max, i;

	switch(type) {
	case DBG_TYPE_BREAKPOINT:
		max = dbg_breakpoint_num;
		reg_addr = DBG_REG_BASE_BVR;
		reg_ctrl = DBG_REG_BASE_BCR;
		break;
	case DBG_TYPE_WATCHPOINT:
		max = dbg_watchpoint_num;
		reg_addr = DBG_REG_BASE_WVR;
		reg_ctrl = DBG_REG_BASE_WCR;
		break;
	default:
		db_printf("Unsupported debug type\n");
		return (~0U);
	}

	for (i = 0; i < max; i++) {
		if ((dbg_wb_read_reg(reg_addr, i) == addr) &&
		    ((dbg_wb_read_reg(reg_ctrl, i) & DBG_WB_CTRL_E) != 0))
			return (i);
	}

	return (~0U);
}

static __inline boolean_t
dbg_monitor_is_enabled(void)
{

	return ((cp14_dbgdscrint_get() & DBGSCR_MDBG_EN) != 0);
}

static int
dbg_enable_monitor(void)
{
	uint32_t dbg_dscr;

	/* Already enabled? Just return */
	if (dbg_monitor_is_enabled())
		return (0);

	dbg_dscr = cp14_dbgdscrint_get();

	switch (dbg_model) {
	case ID_DFR0_CP_DEBUG_M_V6:
	case ID_DFR0_CP_DEBUG_M_V6_1: /* fall through */
		cp14_dbgdscr_v6_set(dbg_dscr | DBGSCR_MDBG_EN);
		break;
	case ID_DFR0_CP_DEBUG_M_V7: /* fall through */
	case ID_DFR0_CP_DEBUG_M_V7_1:
		cp14_dbgdscr_v7_set(dbg_dscr | DBGSCR_MDBG_EN);
		break;
	default:
		break;
	}
	isb();

	/* Verify that Monitor mode is set */
	if (dbg_monitor_is_enabled())
		return (0);

	return (ENXIO);
}

static int
dbg_setup_xpoint(struct dbg_wb_conf *conf)
{
	struct pcpu *pcpu;
	struct dbreg *d;
	const char *typestr;
	uint32_t cr_size, cr_priv, cr_access;
	uint32_t reg_ctrl, reg_addr, ctrl, addr;
	boolean_t is_bkpt;
	u_int cpu;
	u_int i;

	if (!dbg_capable())
		return (ENXIO);

	is_bkpt = (conf->type == DBG_TYPE_BREAKPOINT);
	typestr = is_bkpt ? "breakpoint" : "watchpoint";

	if (is_bkpt) {
		if (dbg_breakpoint_num == 0) {
			db_printf("Breakpoints not supported on this architecture\n");
			return (ENXIO);
		}
		i = conf->slot;
		if (!dbg_check_slot_free(DBG_TYPE_BREAKPOINT, i)) {
			/*
			 * This should never happen. If it does it means that
			 * there is an erroneus scenario somewhere. Still, it can
			 * be done but let's inform the user.
			 */
			db_printf("ERROR: Breakpoint already set. Replacing...\n");
		}
	} else {
		i = dbg_find_free_slot(DBG_TYPE_WATCHPOINT);
		if (i == ~0U) {
			db_printf("Can not find slot for %s, max %d slots supported\n",
			    typestr, dbg_watchpoint_num);
			return (ENXIO);
		}
	}

	/* Kernel access only */
	cr_priv = DBG_WB_CTRL_PL1;

	switch(conf->size) {
	case 1:
		cr_size = DBG_WB_CTRL_LEN_1;
		break;
	case 2:
		cr_size = DBG_WB_CTRL_LEN_2;
		break;
	case 4:
		cr_size = DBG_WB_CTRL_LEN_4;
		break;
	case 8:
		cr_size = DBG_WB_CTRL_LEN_8;
		break;
	default:
		db_printf("Unsupported address size for %s\n", typestr);
		return (EINVAL);
	}

	if (is_bkpt) {
		cr_access = DBG_WB_CTRL_EXEC;
		reg_ctrl = DBG_REG_BASE_BCR;
		reg_addr = DBG_REG_BASE_BVR;
		/* Always unlinked BKPT */
		ctrl = (cr_size | cr_access | cr_priv | DBG_WB_CTRL_E);
	} else {
		switch(conf->access) {
		case HW_WATCHPOINT_R:
			cr_access = DBG_WB_CTRL_LOAD;
			break;
		case HW_WATCHPOINT_W:
			cr_access = DBG_WB_CTRL_STORE;
			break;
		case HW_WATCHPOINT_RW:
			cr_access = DBG_WB_CTRL_LOAD | DBG_WB_CTRL_STORE;
			break;
		default:
			db_printf("Unsupported exception level for %s\n", typestr);
			return (EINVAL);
		}

		reg_ctrl = DBG_REG_BASE_WCR;
		reg_addr = DBG_REG_BASE_WVR;
		ctrl = (cr_size | cr_access | cr_priv | DBG_WB_CTRL_E);
	}

	addr = conf->address;

	dbg_wb_write_reg(reg_addr, i, addr);
	dbg_wb_write_reg(reg_ctrl, i, ctrl);

	/*
	 * Save watchpoint settings for all CPUs.
	 * We don't need to do the same with breakpoints since HW breakpoints
	 * are only used to perform single stepping.
	 */
	if (!is_bkpt) {
		CPU_FOREACH(cpu) {
			pcpu = pcpu_find(cpu);
			/* Fill out the settings for watchpoint */
			d = (struct dbreg *)pcpu->pc_dbreg;
			d->dbg_wvr[i] = addr;
			d->dbg_wcr[i] = ctrl;
			/* Skip update command for the current CPU */
			if (cpu != PCPU_GET(cpuid))
				pcpu->pc_dbreg_cmd = PC_DBREG_CMD_LOAD;
		}
	}
	/* Ensure all data is written before waking other CPUs */
	atomic_thread_fence_rel();

	return (0);
}

static int
dbg_remove_xpoint(struct dbg_wb_conf *conf)
{
	struct pcpu *pcpu;
	struct dbreg *d;
	uint32_t reg_ctrl, reg_addr, addr;
	boolean_t is_bkpt;
	u_int cpu;
	u_int i;

	if (!dbg_capable())
		return (ENXIO);

	is_bkpt = (conf->type == DBG_TYPE_BREAKPOINT);
	addr = conf->address;

	if (is_bkpt) {
		i = conf->slot;
		reg_ctrl = DBG_REG_BASE_BCR;
		reg_addr = DBG_REG_BASE_BVR;
	} else {
		i = dbg_find_slot(DBG_TYPE_WATCHPOINT, addr);
		if (i == ~0U) {
			db_printf("Can not find watchpoint for address 0%x\n", addr);
			return (EINVAL);
		}
		reg_ctrl = DBG_REG_BASE_WCR;
		reg_addr = DBG_REG_BASE_WVR;
	}

	dbg_wb_write_reg(reg_ctrl, i, 0);
	dbg_wb_write_reg(reg_addr, i, 0);

	/*
	 * Save watchpoint settings for all CPUs.
	 * We don't need to do the same with breakpoints since HW breakpoints
	 * are only used to perform single stepping.
	 */
	if (!is_bkpt) {
		CPU_FOREACH(cpu) {
			pcpu = pcpu_find(cpu);
			/* Fill out the settings for watchpoint */
			d = (struct dbreg *)pcpu->pc_dbreg;
			d->dbg_wvr[i] = 0;
			d->dbg_wcr[i] = 0;
			/* Skip update command for the current CPU */
			if (cpu != PCPU_GET(cpuid))
				pcpu->pc_dbreg_cmd = PC_DBREG_CMD_LOAD;
		}
		/* Ensure all data is written before waking other CPUs */
		atomic_thread_fence_rel();
	}

	return (0);
}

static __inline uint32_t
dbg_get_debug_model(void)
{
	uint32_t dbg_m;

	dbg_m = ((cpuinfo.id_dfr0 & ID_DFR0_CP_DEBUG_M_MASK) >>
	    ID_DFR0_CP_DEBUG_M_SHIFT);

	return (dbg_m);
}

static __inline boolean_t
dbg_get_ossr(void)
{

	switch (dbg_model) {
	case ID_DFR0_CP_DEBUG_M_V7:
		if ((cp14_dbgoslsr_get() & DBGOSLSR_OSLM0) != 0)
			return (TRUE);

		return (FALSE);
	case ID_DFR0_CP_DEBUG_M_V7_1:
		return (TRUE);
	default:
		return (FALSE);
	}
}

static __inline boolean_t
dbg_arch_supported(void)
{
	uint32_t dbg_didr;

	switch (dbg_model) {
	case ID_DFR0_CP_DEBUG_M_V6:
	case ID_DFR0_CP_DEBUG_M_V6_1:
		dbg_didr = cp14_dbgdidr_get();
		/*
		 * read-all-zeroes is used by QEMU
		 * to indicate that ARMv6 debug support
		 * is not implemented. Real hardware has at
		 * least version bits set
		 */
		if (dbg_didr == 0)
			return (FALSE);
		return (TRUE);
	case ID_DFR0_CP_DEBUG_M_V7:
	case ID_DFR0_CP_DEBUG_M_V7_1:	/* fall through */
		return (TRUE);
	default:
		/* We only support valid v6.x/v7.x modes through CP14 */
		return (FALSE);
	}
}

static __inline uint32_t
dbg_get_wrp_num(void)
{
	uint32_t dbg_didr;

	dbg_didr = cp14_dbgdidr_get();

	return (DBGDIDR_WRPS_NUM(dbg_didr));
}

static __inline uint32_t
dgb_get_brp_num(void)
{
	uint32_t dbg_didr;

	dbg_didr = cp14_dbgdidr_get();

	return (DBGDIDR_BRPS_NUM(dbg_didr));
}

static int
dbg_reset_state(void)
{
	u_int cpuid;
	size_t i;
	int err;

	cpuid = PCPU_GET(cpuid);
	err = 0;

	switch (dbg_model) {
	case ID_DFR0_CP_DEBUG_M_V6:
	case ID_DFR0_CP_DEBUG_M_V6_1: /* fall through */
		/*
		 * Arch needs monitor mode selected and enabled
		 * to be able to access breakpoint/watchpoint registers.
		 */
		err = dbg_enable_monitor();
		if (err != 0)
			return (err);
		goto vectr_clr;
	case ID_DFR0_CP_DEBUG_M_V7:
		/* Is core power domain powered up? */
		if ((cp14_dbgprsr_get() & DBGPRSR_PU) == 0)
			err = ENXIO;

		if (err != 0)
			break;

		if (dbg_ossr)
			goto vectr_clr;
		break;
	case ID_DFR0_CP_DEBUG_M_V7_1:
		/* Is double lock set? */
		if ((cp14_dbgosdlr_get() & DBGPRSR_DLK) != 0)
			err = ENXIO;

		break;
	default:
		break;
	}

	if (err != 0) {
		db_printf("Debug facility locked (CPU%d)\n", cpuid);
		return (err);
	}

	/*
	 * DBGOSLAR is always implemented for v7.1 Debug Arch. however is
	 * optional for v7 (depends on OS save and restore support).
	 */
	if (((dbg_model & ID_DFR0_CP_DEBUG_M_V7_1) != 0) || dbg_ossr) {
		/*
		 * Clear OS lock.
		 * Writing any other value than 0xC5ACCESS will unlock.
		 */
		cp14_dbgoslar_set(0);
		isb();
	}

vectr_clr:
	/*
	 * After reset we must ensure that DBGVCR has a defined value.
	 * Disable all vector catch events. Safe to use - required in all
	 * implementations.
	 */
	cp14_dbgvcr_set(0);
	isb();

	/*
	 * We have limited number of {watch,break}points, each consists of
	 * two registers:
	 * - wcr/bcr regsiter configurates corresponding {watch,break}point
	 *   behaviour
	 * - wvr/bvr register keeps address we are hunting for
	 *
	 * Reset all breakpoints and watchpoints.
	 */
	for (i = 0; i < dbg_watchpoint_num; ++i) {
		dbg_wb_write_reg(DBG_REG_BASE_WCR, i, 0);
		dbg_wb_write_reg(DBG_REG_BASE_WVR, i, 0);
	}

	for (i = 0; i < dbg_breakpoint_num; ++i) {
		dbg_wb_write_reg(DBG_REG_BASE_BCR, i, 0);
		dbg_wb_write_reg(DBG_REG_BASE_BVR, i, 0);
	}

	return (0);
}

void
dbg_monitor_init(void)
{
	int err;

	/* Fetch ARM Debug Architecture model */
	dbg_model = dbg_get_debug_model();

	if (!dbg_arch_supported()) {
		db_printf("ARM Debug Architecture not supported\n");
		return;
	}

	if (bootverbose) {
		db_printf("ARM Debug Architecture %s\n",
		    (dbg_model == ID_DFR0_CP_DEBUG_M_V6) ? "v6" :
		    (dbg_model == ID_DFR0_CP_DEBUG_M_V6_1) ? "v6.1" :
		    (dbg_model == ID_DFR0_CP_DEBUG_M_V7) ? "v7" :
		    (dbg_model == ID_DFR0_CP_DEBUG_M_V7_1) ? "v7.1" : "unknown");
	}

	/* Do we have OS Save and Restore mechanism? */
	dbg_ossr = dbg_get_ossr();

	/* Find out many breakpoints and watchpoints we can use */
	dbg_watchpoint_num = dbg_get_wrp_num();
	dbg_breakpoint_num = dgb_get_brp_num();

	if (bootverbose) {
		db_printf("%d watchpoints and %d breakpoints supported\n",
		    dbg_watchpoint_num, dbg_breakpoint_num);
	}

	err = dbg_reset_state();
	if (err == 0) {
		err = dbg_enable_monitor();
		if (err == 0) {
			atomic_set_int(&dbg_capable_var, 1);
			return;
		}
	}

	db_printf("HW Breakpoints/Watchpoints not enabled on CPU%d\n",
	    PCPU_GET(cpuid));
}

CTASSERT(sizeof(struct dbreg) == sizeof(((struct pcpu *)NULL)->pc_dbreg));

void
dbg_monitor_init_secondary(void)
{
	u_int cpuid;
	int err;
	/*
	 * This flag is set on the primary CPU
	 * and its meaning is valid for other CPUs too.
	 */
	if (!dbg_capable())
		return;

	cpuid = PCPU_GET(cpuid);

	err = dbg_reset_state();
	if (err != 0) {
		/*
		 * Something is very wrong.
		 * WPs/BPs will not work correctly on this CPU.
		 */
		KASSERT(0, ("%s: Failed to reset Debug Architecture "
		    "state on CPU%d", __func__, cpuid));
		/* Disable HW debug capabilities for all CPUs */
		atomic_set_int(&dbg_capable_var, 0);
		return;
	}
	err = dbg_enable_monitor();
	if (err != 0) {
		KASSERT(0, ("%s: Failed to enable Debug Monitor"
		    " on CPU%d", __func__, cpuid));
		atomic_set_int(&dbg_capable_var, 0);
	}
}

void
dbg_resume_dbreg(void)
{
	struct dbreg *d;
	u_int i;

	/*
	 * This flag is set on the primary CPU
	 * and its meaning is valid for other CPUs too.
	 */
	if (!dbg_capable())
		return;

	atomic_thread_fence_acq();

	switch (PCPU_GET(dbreg_cmd)) {
	case PC_DBREG_CMD_LOAD:
		d = (struct dbreg *)PCPU_PTR(dbreg);

		/* Restore watchpoints */
		for (i = 0; i < dbg_watchpoint_num; i++) {
			dbg_wb_write_reg(DBG_REG_BASE_WVR, i, d->dbg_wvr[i]);
			dbg_wb_write_reg(DBG_REG_BASE_WCR, i, d->dbg_wcr[i]);
		}

		PCPU_SET(dbreg_cmd, PC_DBREG_CMD_NONE);
		break;
	}
}
