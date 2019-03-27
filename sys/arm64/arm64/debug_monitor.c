/*-
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Semihalf under
 * the sponsorship of the FreeBSD Foundation.
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

#include "opt_ddb.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/kdb.h>
#include <sys/pcpu.h>
#include <sys/systm.h>

#include <machine/armreg.h>
#include <machine/cpu.h>
#include <machine/debug_monitor.h>
#include <machine/kdb.h>

#include <ddb/ddb.h>
#include <ddb/db_sym.h>

enum dbg_t {
	DBG_TYPE_BREAKPOINT = 0,
	DBG_TYPE_WATCHPOINT = 1,
};

static int dbg_watchpoint_num;
static int dbg_breakpoint_num;
static int dbg_ref_count_mde[MAXCPU];
static int dbg_ref_count_kde[MAXCPU];

/* Watchpoints/breakpoints control register bitfields */
#define DBG_WATCH_CTRL_LEN_1		(0x1 << 5)
#define DBG_WATCH_CTRL_LEN_2		(0x3 << 5)
#define DBG_WATCH_CTRL_LEN_4		(0xf << 5)
#define DBG_WATCH_CTRL_LEN_8		(0xff << 5)
#define DBG_WATCH_CTRL_LEN_MASK(x)	((x) & (0xff << 5))
#define DBG_WATCH_CTRL_EXEC		(0x0 << 3)
#define DBG_WATCH_CTRL_LOAD		(0x1 << 3)
#define DBG_WATCH_CTRL_STORE		(0x2 << 3)
#define DBG_WATCH_CTRL_ACCESS_MASK(x)	((x) & (0x3 << 3))

/* Common for breakpoint and watchpoint */
#define DBG_WB_CTRL_EL1		(0x1 << 1)
#define DBG_WB_CTRL_EL0		(0x2 << 1)
#define DBG_WB_CTRL_ELX_MASK(x)	((x) & (0x3 << 1))
#define DBG_WB_CTRL_E		(0x1 << 0)

#define DBG_REG_BASE_BVR	0
#define DBG_REG_BASE_BCR	(DBG_REG_BASE_BVR + 16)
#define DBG_REG_BASE_WVR	(DBG_REG_BASE_BCR + 16)
#define DBG_REG_BASE_WCR	(DBG_REG_BASE_WVR + 16)

/* Watchpoint/breakpoint helpers */
#define DBG_WB_WVR	"wvr"
#define DBG_WB_WCR	"wcr"
#define DBG_WB_BVR	"bvr"
#define DBG_WB_BCR	"bcr"

#define DBG_WB_READ(reg, num, val) do {					\
	__asm __volatile("mrs %0, dbg" reg #num "_el1" : "=r" (val));	\
} while (0)

#define DBG_WB_WRITE(reg, num, val) do {				\
	__asm __volatile("msr dbg" reg #num "_el1, %0" :: "r" (val));	\
} while (0)

#define READ_WB_REG_CASE(reg, num, offset, val)		\
	case (num + offset):				\
		DBG_WB_READ(reg, num, val);		\
		break

#define WRITE_WB_REG_CASE(reg, num, offset, val)	\
	case (num + offset):				\
		DBG_WB_WRITE(reg, num, val);		\
		break

#define SWITCH_CASES_READ_WB_REG(reg, offset, val)	\
	READ_WB_REG_CASE(reg,  0, offset, val);		\
	READ_WB_REG_CASE(reg,  1, offset, val);		\
	READ_WB_REG_CASE(reg,  2, offset, val);		\
	READ_WB_REG_CASE(reg,  3, offset, val);		\
	READ_WB_REG_CASE(reg,  4, offset, val);		\
	READ_WB_REG_CASE(reg,  5, offset, val);		\
	READ_WB_REG_CASE(reg,  6, offset, val);		\
	READ_WB_REG_CASE(reg,  7, offset, val);		\
	READ_WB_REG_CASE(reg,  8, offset, val);		\
	READ_WB_REG_CASE(reg,  9, offset, val);		\
	READ_WB_REG_CASE(reg, 10, offset, val);		\
	READ_WB_REG_CASE(reg, 11, offset, val);		\
	READ_WB_REG_CASE(reg, 12, offset, val);		\
	READ_WB_REG_CASE(reg, 13, offset, val);		\
	READ_WB_REG_CASE(reg, 14, offset, val);		\
	READ_WB_REG_CASE(reg, 15, offset, val)

#define SWITCH_CASES_WRITE_WB_REG(reg, offset, val)	\
	WRITE_WB_REG_CASE(reg,  0, offset, val);	\
	WRITE_WB_REG_CASE(reg,  1, offset, val);	\
	WRITE_WB_REG_CASE(reg,  2, offset, val);	\
	WRITE_WB_REG_CASE(reg,  3, offset, val);	\
	WRITE_WB_REG_CASE(reg,  4, offset, val);	\
	WRITE_WB_REG_CASE(reg,  5, offset, val);	\
	WRITE_WB_REG_CASE(reg,  6, offset, val);	\
	WRITE_WB_REG_CASE(reg,  7, offset, val);	\
	WRITE_WB_REG_CASE(reg,  8, offset, val);	\
	WRITE_WB_REG_CASE(reg,  9, offset, val);	\
	WRITE_WB_REG_CASE(reg, 10, offset, val);	\
	WRITE_WB_REG_CASE(reg, 11, offset, val);	\
	WRITE_WB_REG_CASE(reg, 12, offset, val);	\
	WRITE_WB_REG_CASE(reg, 13, offset, val);	\
	WRITE_WB_REG_CASE(reg, 14, offset, val);	\
	WRITE_WB_REG_CASE(reg, 15, offset, val)

static uint64_t
dbg_wb_read_reg(int reg, int n)
{
	uint64_t val = 0;

	switch (reg + n) {
	SWITCH_CASES_READ_WB_REG(DBG_WB_WVR, DBG_REG_BASE_WVR, val);
	SWITCH_CASES_READ_WB_REG(DBG_WB_WCR, DBG_REG_BASE_WCR, val);
	SWITCH_CASES_READ_WB_REG(DBG_WB_BVR, DBG_REG_BASE_BVR, val);
	SWITCH_CASES_READ_WB_REG(DBG_WB_BCR, DBG_REG_BASE_BCR, val);
	default:
		db_printf("trying to read from wrong debug register %d\n", n);
	}

	return val;
}

static void
dbg_wb_write_reg(int reg, int n, uint64_t val)
{
	switch (reg + n) {
	SWITCH_CASES_WRITE_WB_REG(DBG_WB_WVR, DBG_REG_BASE_WVR, val);
	SWITCH_CASES_WRITE_WB_REG(DBG_WB_WCR, DBG_REG_BASE_WCR, val);
	SWITCH_CASES_WRITE_WB_REG(DBG_WB_BVR, DBG_REG_BASE_BVR, val);
	SWITCH_CASES_WRITE_WB_REG(DBG_WB_BCR, DBG_REG_BASE_BCR, val);
	default:
		db_printf("trying to write to wrong debug register %d\n", n);
	}
	isb();
}

void
kdb_cpu_set_singlestep(void)
{

	kdb_frame->tf_spsr |= DBG_SPSR_SS;
	WRITE_SPECIALREG(MDSCR_EL1, READ_SPECIALREG(MDSCR_EL1) |
	    DBG_MDSCR_SS | DBG_MDSCR_KDE);

	/*
	 * Disable breakpoints and watchpoints, e.g. stepping
	 * over watched instruction will trigger break exception instead of
	 * single-step exception and locks CPU on that instruction for ever.
	 */
	if (dbg_ref_count_mde[PCPU_GET(cpuid)] > 0) {
		WRITE_SPECIALREG(MDSCR_EL1,
		    READ_SPECIALREG(MDSCR_EL1) & ~DBG_MDSCR_MDE);
	}
}

void
kdb_cpu_clear_singlestep(void)
{

	WRITE_SPECIALREG(MDSCR_EL1, READ_SPECIALREG(MDSCR_EL1) &
	    ~(DBG_MDSCR_SS | DBG_MDSCR_KDE));

	/* Restore breakpoints and watchpoints */
	if (dbg_ref_count_mde[PCPU_GET(cpuid)] > 0) {
		WRITE_SPECIALREG(MDSCR_EL1,
		    READ_SPECIALREG(MDSCR_EL1) | DBG_MDSCR_MDE);
	}

	if (dbg_ref_count_kde[PCPU_GET(cpuid)] > 0) {
		WRITE_SPECIALREG(MDSCR_EL1,
		    READ_SPECIALREG(MDSCR_EL1) | DBG_MDSCR_KDE);
	}
}

static const char *
dbg_watchtype_str(uint32_t type)
{
	switch (type) {
		case DBG_WATCH_CTRL_EXEC:
			return ("execute");
		case DBG_WATCH_CTRL_STORE:
			return ("write");
		case DBG_WATCH_CTRL_LOAD:
			return ("read");
		case DBG_WATCH_CTRL_LOAD | DBG_WATCH_CTRL_STORE:
			return ("read/write");
		default:
			return ("invalid");
	}
}

static int
dbg_watchtype_len(uint32_t len)
{
	switch (len) {
	case DBG_WATCH_CTRL_LEN_1:
		return (1);
	case DBG_WATCH_CTRL_LEN_2:
		return (2);
	case DBG_WATCH_CTRL_LEN_4:
		return (4);
	case DBG_WATCH_CTRL_LEN_8:
		return (8);
	default:
		return (0);
	}
}

void
dbg_show_watchpoint(void)
{
	uint32_t wcr, len, type;
	uint64_t addr;
	int i;

	db_printf("\nhardware watchpoints:\n");
	db_printf("  watch    status        type  len             address              symbol\n");
	db_printf("  -----  --------  ----------  ---  ------------------  ------------------\n");
	for (i = 0; i < dbg_watchpoint_num; i++) {
		wcr = dbg_wb_read_reg(DBG_REG_BASE_WCR, i);
		if ((wcr & DBG_WB_CTRL_E) != 0) {
			type = DBG_WATCH_CTRL_ACCESS_MASK(wcr);
			len = DBG_WATCH_CTRL_LEN_MASK(wcr);
			addr = dbg_wb_read_reg(DBG_REG_BASE_WVR, i);
			db_printf("  %-5d  %-8s  %10s  %3d  0x%16lx  ",
			    i, "enabled", dbg_watchtype_str(type),
			    dbg_watchtype_len(len), addr);
			db_printsym((db_addr_t)addr, DB_STGY_ANY);
			db_printf("\n");
		} else {
			db_printf("  %-5d  disabled\n", i);
		}
	}
}


static int
dbg_find_free_slot(enum dbg_t type)
{
	u_int max, reg, i;

	switch(type) {
	case DBG_TYPE_BREAKPOINT:
		max = dbg_breakpoint_num;
		reg = DBG_REG_BASE_BCR;

		break;
	case DBG_TYPE_WATCHPOINT:
		max = dbg_watchpoint_num;
		reg = DBG_REG_BASE_WCR;
		break;
	default:
		db_printf("Unsupported debug type\n");
		return (i);
	}

	for (i = 0; i < max; i++) {
		if ((dbg_wb_read_reg(reg, i) & DBG_WB_CTRL_E) == 0)
			return (i);
	}

	return (-1);
}

static int
dbg_find_slot(enum dbg_t type, db_expr_t addr)
{
	u_int max, reg_addr, reg_ctrl, i;

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
		return (i);
	}

	for (i = 0; i < max; i++) {
		if ((dbg_wb_read_reg(reg_addr, i) == addr) &&
		    ((dbg_wb_read_reg(reg_ctrl, i) & DBG_WB_CTRL_E) != 0))
			return (i);
	}

	return (-1);
}

static void
dbg_enable_monitor(enum dbg_el_t el)
{
	uint64_t reg_mdcr = 0;

	/*
	 * There is no need to have debug monitor on permanently, thus we are
	 * refcounting and turn it on only if any of CPU is going to use that.
	 */
	if (atomic_fetchadd_int(&dbg_ref_count_mde[PCPU_GET(cpuid)], 1) == 0)
		reg_mdcr = DBG_MDSCR_MDE;

	if ((el == DBG_FROM_EL1) &&
	    atomic_fetchadd_int(&dbg_ref_count_kde[PCPU_GET(cpuid)], 1) == 0)
		reg_mdcr |= DBG_MDSCR_KDE;

	if (reg_mdcr)
		WRITE_SPECIALREG(MDSCR_EL1, READ_SPECIALREG(MDSCR_EL1) | reg_mdcr);
}

static void
dbg_disable_monitor(enum dbg_el_t el)
{
	uint64_t reg_mdcr = 0;

	if (atomic_fetchadd_int(&dbg_ref_count_mde[PCPU_GET(cpuid)], -1) == 1)
		reg_mdcr = DBG_MDSCR_MDE;

	if ((el == DBG_FROM_EL1) &&
	    atomic_fetchadd_int(&dbg_ref_count_kde[PCPU_GET(cpuid)], -1) == 1)
		reg_mdcr |= DBG_MDSCR_KDE;

	if (reg_mdcr)
		WRITE_SPECIALREG(MDSCR_EL1, READ_SPECIALREG(MDSCR_EL1) & ~reg_mdcr);
}

int
dbg_setup_watchpoint(db_expr_t addr, db_expr_t size, enum dbg_el_t el,
    enum dbg_access_t access)
{
	uint64_t wcr_size, wcr_priv, wcr_access;
	u_int i;

	i = dbg_find_free_slot(DBG_TYPE_WATCHPOINT);
	if (i == -1) {
		db_printf("Can not find slot for watchpoint, max %d"
		    " watchpoints supported\n", dbg_watchpoint_num);
		return (i);
	}

	switch(size) {
	case 1:
		wcr_size = DBG_WATCH_CTRL_LEN_1;
		break;
	case 2:
		wcr_size = DBG_WATCH_CTRL_LEN_2;
		break;
	case 4:
		wcr_size = DBG_WATCH_CTRL_LEN_4;
		break;
	case 8:
		wcr_size = DBG_WATCH_CTRL_LEN_8;
		break;
	default:
		db_printf("Unsupported address size for watchpoint\n");
		return (-1);
	}

	switch(el) {
	case DBG_FROM_EL0:
		wcr_priv = DBG_WB_CTRL_EL0;
		break;
	case DBG_FROM_EL1:
		wcr_priv = DBG_WB_CTRL_EL1;
		break;
	default:
		db_printf("Unsupported exception level for watchpoint\n");
		return (-1);
	}

	switch(access) {
	case HW_BREAKPOINT_X:
		wcr_access = DBG_WATCH_CTRL_EXEC;
		break;
	case HW_BREAKPOINT_R:
		wcr_access = DBG_WATCH_CTRL_LOAD;
		break;
	case HW_BREAKPOINT_W:
		wcr_access = DBG_WATCH_CTRL_STORE;
		break;
	case HW_BREAKPOINT_RW:
		wcr_access = DBG_WATCH_CTRL_LOAD | DBG_WATCH_CTRL_STORE;
		break;
	default:
		db_printf("Unsupported exception level for watchpoint\n");
		return (-1);
	}

	dbg_wb_write_reg(DBG_REG_BASE_WVR, i, addr);
	dbg_wb_write_reg(DBG_REG_BASE_WCR, i, wcr_size | wcr_access | wcr_priv |
	    DBG_WB_CTRL_E);
	dbg_enable_monitor(el);
	return (0);
}

int
dbg_remove_watchpoint(db_expr_t addr, db_expr_t size, enum dbg_el_t el)
{
	u_int i;

	i = dbg_find_slot(DBG_TYPE_WATCHPOINT, addr);
	if (i == -1) {
		db_printf("Can not find watchpoint for address 0%lx\n", addr);
		return (i);
	}

	dbg_wb_write_reg(DBG_REG_BASE_WCR, i, 0);
	dbg_disable_monitor(el);
	return (0);
}

void
dbg_monitor_init(void)
{
	u_int i;

	/* Find out many breakpoints and watchpoints we can use */
	dbg_watchpoint_num = ((READ_SPECIALREG(ID_AA64DFR0_EL1) >> 20) & 0xf) + 1;
	dbg_breakpoint_num = ((READ_SPECIALREG(ID_AA64DFR0_EL1) >> 12) & 0xf) + 1;

	if (bootverbose && PCPU_GET(cpuid) == 0) {
		printf("%d watchpoints and %d breakpoints supported\n",
		    dbg_watchpoint_num, dbg_breakpoint_num);
	}

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

	dbg_enable();
}
