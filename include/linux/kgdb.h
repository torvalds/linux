/*
 * This provides the callbacks and functions that KGDB needs to share between
 * the core, I/O and arch-specific portions.
 *
 * Author: Amit Kale <amitkale@linsyssoft.com> and
 *         Tom Rini <trini@kernel.crashing.org>
 *
 * 2001-2004 (c) Amit S. Kale and 2003-2005 (c) MontaVista Software, Inc.
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */
#ifndef _KGDB_H_
#define _KGDB_H_

#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/atomic.h>
#ifdef CONFIG_HAVE_ARCH_KGDB
#include <asm/kgdb.h>
#endif

#ifdef CONFIG_KGDB
struct pt_regs;

/**
 *	kgdb_skipexception - (optional) exit kgdb_handle_exception early
 *	@exception: Exception vector number
 *	@regs: Current &struct pt_regs.
 *
 *	On some architectures it is required to skip a breakpoint
 *	exception when it occurs after a breakpoint has been removed.
 *	This can be implemented in the architecture specific portion of kgdb.
 */
extern int kgdb_skipexception(int exception, struct pt_regs *regs);

struct tasklet_struct;
struct task_struct;
struct uart_port;

/**
 *	kgdb_breakpoint - compiled in breakpoint
 *
 *	This will be implemented as a static inline per architecture.  This
 *	function is called by the kgdb core to execute an architecture
 *	specific trap to cause kgdb to enter the exception processing.
 *
 */
void kgdb_breakpoint(void);

extern int kgdb_connected;
extern int kgdb_io_module_registered;

extern atomic_t			kgdb_setting_breakpoint;
extern atomic_t			kgdb_cpu_doing_single_step;

extern struct task_struct	*kgdb_usethread;
extern struct task_struct	*kgdb_contthread;

enum kgdb_bptype {
	BP_BREAKPOINT = 0,
	BP_HARDWARE_BREAKPOINT,
	BP_WRITE_WATCHPOINT,
	BP_READ_WATCHPOINT,
	BP_ACCESS_WATCHPOINT,
	BP_POKE_BREAKPOINT,
};

enum kgdb_bpstate {
	BP_UNDEFINED = 0,
	BP_REMOVED,
	BP_SET,
	BP_ACTIVE
};

struct kgdb_bkpt {
	unsigned long		bpt_addr;
	unsigned char		saved_instr[BREAK_INSTR_SIZE];
	enum kgdb_bptype	type;
	enum kgdb_bpstate	state;
};

struct dbg_reg_def_t {
	char *name;
	int size;
	int offset;
};

#ifndef DBG_MAX_REG_NUM
#define DBG_MAX_REG_NUM 0
#else
extern struct dbg_reg_def_t dbg_reg_def[];
extern char *dbg_get_reg(int regno, void *mem, struct pt_regs *regs);
extern int dbg_set_reg(int regno, void *mem, struct pt_regs *regs);
#endif
#ifndef KGDB_MAX_BREAKPOINTS
# define KGDB_MAX_BREAKPOINTS	1000
#endif

#define KGDB_HW_BREAKPOINT	1

/*
 * Functions each KGDB-supporting architecture must provide:
 */

/**
 *	kgdb_arch_init - Perform any architecture specific initalization.
 *
 *	This function will handle the initalization of any architecture
 *	specific callbacks.
 */
extern int kgdb_arch_init(void);

/**
 *	kgdb_arch_exit - Perform any architecture specific uninitalization.
 *
 *	This function will handle the uninitalization of any architecture
 *	specific callbacks, for dynamic registration and unregistration.
 */
extern void kgdb_arch_exit(void);

/**
 *	pt_regs_to_gdb_regs - Convert ptrace regs to GDB regs
 *	@gdb_regs: A pointer to hold the registers in the order GDB wants.
 *	@regs: The &struct pt_regs of the current process.
 *
 *	Convert the pt_regs in @regs into the format for registers that
 *	GDB expects, stored in @gdb_regs.
 */
extern void pt_regs_to_gdb_regs(unsigned long *gdb_regs, struct pt_regs *regs);

/**
 *	sleeping_thread_to_gdb_regs - Convert ptrace regs to GDB regs
 *	@gdb_regs: A pointer to hold the registers in the order GDB wants.
 *	@p: The &struct task_struct of the desired process.
 *
 *	Convert the register values of the sleeping process in @p to
 *	the format that GDB expects.
 *	This function is called when kgdb does not have access to the
 *	&struct pt_regs and therefore it should fill the gdb registers
 *	@gdb_regs with what has	been saved in &struct thread_struct
 *	thread field during switch_to.
 */
extern void
sleeping_thread_to_gdb_regs(unsigned long *gdb_regs, struct task_struct *p);

/**
 *	gdb_regs_to_pt_regs - Convert GDB regs to ptrace regs.
 *	@gdb_regs: A pointer to hold the registers we've received from GDB.
 *	@regs: A pointer to a &struct pt_regs to hold these values in.
 *
 *	Convert the GDB regs in @gdb_regs into the pt_regs, and store them
 *	in @regs.
 */
extern void gdb_regs_to_pt_regs(unsigned long *gdb_regs, struct pt_regs *regs);

/**
 *	kgdb_arch_handle_exception - Handle architecture specific GDB packets.
 *	@vector: The error vector of the exception that happened.
 *	@signo: The signal number of the exception that happened.
 *	@err_code: The error code of the exception that happened.
 *	@remcom_in_buffer: The buffer of the packet we have read.
 *	@remcom_out_buffer: The buffer of %BUFMAX bytes to write a packet into.
 *	@regs: The &struct pt_regs of the current process.
 *
 *	This function MUST handle the 'c' and 's' command packets,
 *	as well packets to set / remove a hardware breakpoint, if used.
 *	If there are additional packets which the hardware needs to handle,
 *	they are handled here.  The code should return -1 if it wants to
 *	process more packets, and a %0 or %1 if it wants to exit from the
 *	kgdb callback.
 */
extern int
kgdb_arch_handle_exception(int vector, int signo, int err_code,
			   char *remcom_in_buffer,
			   char *remcom_out_buffer,
			   struct pt_regs *regs);

/**
 *	kgdb_roundup_cpus - Get other CPUs into a holding pattern
 *
 *	On SMP systems, we need to get the attention of the other CPUs
 *	and get them into a known state.  This should do what is needed
 *	to get the other CPUs to call kgdb_wait(). Note that on some arches,
 *	the NMI approach is not used for rounding up all the CPUs. For example,
 *	in case of MIPS, smp_call_function() is used to roundup CPUs.
 *
 *	On non-SMP systems, this is not called.
 */
extern void kgdb_roundup_cpus(void);

/**
 *	kgdb_arch_set_pc - Generic call back to the program counter
 *	@regs: Current &struct pt_regs.
 *  @pc: The new value for the program counter
 *
 *	This function handles updating the program counter and requires an
 *	architecture specific implementation.
 */
extern void kgdb_arch_set_pc(struct pt_regs *regs, unsigned long pc);


/* Optional functions. */
extern int kgdb_validate_break_address(unsigned long addr);
extern int kgdb_arch_set_breakpoint(struct kgdb_bkpt *bpt);
extern int kgdb_arch_remove_breakpoint(struct kgdb_bkpt *bpt);

/**
 *	kgdb_arch_late - Perform any architecture specific initalization.
 *
 *	This function will handle the late initalization of any
 *	architecture specific callbacks.  This is an optional function for
 *	handling things like late initialization of hw breakpoints.  The
 *	default implementation does nothing.
 */
extern void kgdb_arch_late(void);


/**
 * struct kgdb_arch - Describe architecture specific values.
 * @gdb_bpt_instr: The instruction to trigger a breakpoint.
 * @flags: Flags for the breakpoint, currently just %KGDB_HW_BREAKPOINT.
 * @set_breakpoint: Allow an architecture to specify how to set a software
 * breakpoint.
 * @remove_breakpoint: Allow an architecture to specify how to remove a
 * software breakpoint.
 * @set_hw_breakpoint: Allow an architecture to specify how to set a hardware
 * breakpoint.
 * @remove_hw_breakpoint: Allow an architecture to specify how to remove a
 * hardware breakpoint.
 * @disable_hw_break: Allow an architecture to specify how to disable
 * hardware breakpoints for a single cpu.
 * @remove_all_hw_break: Allow an architecture to specify how to remove all
 * hardware breakpoints.
 * @correct_hw_break: Allow an architecture to specify how to correct the
 * hardware debug registers.
 * @enable_nmi: Manage NMI-triggered entry to KGDB
 */
struct kgdb_arch {
	unsigned char		gdb_bpt_instr[BREAK_INSTR_SIZE];
	unsigned long		flags;

	int	(*set_breakpoint)(unsigned long, char *);
	int	(*remove_breakpoint)(unsigned long, char *);
	int	(*set_hw_breakpoint)(unsigned long, int, enum kgdb_bptype);
	int	(*remove_hw_breakpoint)(unsigned long, int, enum kgdb_bptype);
	void	(*disable_hw_break)(struct pt_regs *regs);
	void	(*remove_all_hw_break)(void);
	void	(*correct_hw_break)(void);

	void	(*enable_nmi)(bool on);
};

/**
 * struct kgdb_io - Describe the interface for an I/O driver to talk with KGDB.
 * @name: Name of the I/O driver.
 * @read_char: Pointer to a function that will return one char.
 * @write_char: Pointer to a function that will write one char.
 * @flush: Pointer to a function that will flush any pending writes.
 * @init: Pointer to a function that will initialize the device.
 * @pre_exception: Pointer to a function that will do any prep work for
 * the I/O driver.
 * @post_exception: Pointer to a function that will do any cleanup work
 * for the I/O driver.
 * @is_console: 1 if the end device is a console 0 if the I/O device is
 * not a console
 */
struct kgdb_io {
	const char		*name;
	int			(*read_char) (void);
	void			(*write_char) (u8);
	void			(*flush) (void);
	int			(*init) (void);
	void			(*pre_exception) (void);
	void			(*post_exception) (void);
	int			is_console;
};

extern struct kgdb_arch		arch_kgdb_ops;

extern unsigned long kgdb_arch_pc(int exception, struct pt_regs *regs);

#ifdef CONFIG_SERIAL_KGDB_NMI
extern int kgdb_register_nmi_console(void);
extern int kgdb_unregister_nmi_console(void);
extern bool kgdb_nmi_poll_knock(void);
#else
static inline int kgdb_register_nmi_console(void) { return 0; }
static inline int kgdb_unregister_nmi_console(void) { return 0; }
static inline bool kgdb_nmi_poll_knock(void) { return 1; }
#endif

extern int kgdb_register_io_module(struct kgdb_io *local_kgdb_io_ops);
extern void kgdb_unregister_io_module(struct kgdb_io *local_kgdb_io_ops);
extern struct kgdb_io *dbg_io_ops;

extern int kgdb_hex2long(char **ptr, unsigned long *long_val);
extern char *kgdb_mem2hex(char *mem, char *buf, int count);
extern int kgdb_hex2mem(char *buf, char *mem, int count);

extern int kgdb_isremovedbreak(unsigned long addr);
extern void kgdb_schedule_breakpoint(void);

extern int
kgdb_handle_exception(int ex_vector, int signo, int err_code,
		      struct pt_regs *regs);
extern int kgdb_nmicallback(int cpu, void *regs);
extern int kgdb_nmicallin(int cpu, int trapnr, void *regs, int err_code,
			  atomic_t *snd_rdy);
extern void gdbstub_exit(int status);

extern int			kgdb_single_step;
extern atomic_t			kgdb_active;
#define in_dbg_master() \
	(raw_smp_processor_id() == atomic_read(&kgdb_active))
extern bool dbg_is_early;
extern void __init dbg_late_init(void);
#else /* ! CONFIG_KGDB */
#define in_dbg_master() (0)
#define dbg_late_init()
#endif /* ! CONFIG_KGDB */
#endif /* _KGDB_H_ */
