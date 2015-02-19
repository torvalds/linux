/*
 * Created by: Jason Wessel <jason.wessel@windriver.com>
 *
 * Copyright (c) 2009 Wind River Systems, Inc.  All Rights Reserved.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef _DEBUG_CORE_H_
#define _DEBUG_CORE_H_
/*
 * These are the private implementation headers between the kernel
 * debugger core and the debugger front end code.
 */

/* kernel debug core data structures */
struct kgdb_state {
	int			ex_vector;
	int			signo;
	int			err_code;
	int			cpu;
	int			pass_exception;
	unsigned long		thr_query;
	unsigned long		threadid;
	long			kgdb_usethreadid;
	struct pt_regs		*linux_regs;
	atomic_t		*send_ready;
};

/* Exception state values */
#define DCPU_WANT_MASTER 0x1 /* Waiting to become a master kgdb cpu */
#define DCPU_NEXT_MASTER 0x2 /* Transition from one master cpu to another */
#define DCPU_IS_SLAVE    0x4 /* Slave cpu enter exception */
#define DCPU_SSTEP       0x8 /* CPU is single stepping */

struct debuggerinfo_struct {
	void			*debuggerinfo;
	struct task_struct	*task;
	int			exception_state;
	int			ret_state;
	int			irq_depth;
	int			enter_kgdb;
};

extern struct debuggerinfo_struct kgdb_info[];

/* kernel debug core break point routines */
extern int dbg_remove_all_break(void);
extern int dbg_set_sw_break(unsigned long addr);
extern int dbg_remove_sw_break(unsigned long addr);
extern int dbg_activate_sw_breakpoints(void);
extern int dbg_deactivate_sw_breakpoints(void);

/* polled character access to i/o module */
extern int dbg_io_get_char(void);

/* stub return value for switching between the gdbstub and kdb */
#define DBG_PASS_EVENT -12345
/* Switch from one cpu to another */
#define DBG_SWITCH_CPU_EVENT -123456
extern int dbg_switch_cpu;

/* gdbstub interface functions */
extern int gdb_serial_stub(struct kgdb_state *ks);
extern void gdbstub_msg_write(const char *s, int len);

/* gdbstub functions used for kdb <-> gdbstub transition */
extern int gdbstub_state(struct kgdb_state *ks, char *cmd);
extern int dbg_kdb_mode;

#ifdef CONFIG_KGDB_KDB
extern int kdb_stub(struct kgdb_state *ks);
extern int kdb_parse(const char *cmdstr);
extern int kdb_common_init_state(struct kgdb_state *ks);
extern int kdb_common_deinit_state(void);
#else /* ! CONFIG_KGDB_KDB */
static inline int kdb_stub(struct kgdb_state *ks)
{
	return DBG_PASS_EVENT;
}
#endif /* CONFIG_KGDB_KDB */

#endif /* _DEBUG_CORE_H_ */
