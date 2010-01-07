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

#include <linux/serial_8250.h>
#include <linux/linkage.h>
#include <linux/init.h>

#include <asm/atomic.h>
#include <asm/kgdb.h>

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

/**
 *	kgdb_post_primary_code - (optional) Save error vector/code numbers.
 *	@regs: Original pt_regs.
 *	@e_vector: Original error vector.
 *	@err_code: Original error code.
 *
 *	This is usually needed on architectures which support SMP and
 *	KGDB.  This function is called after all the secondary cpus have
 *	been put to a know spin state and the primary CPU has control over
 *	KGDB.
 */
extern void kgdb_post_primary_code(struct pt_regs *regs, int e_vector,
				  int err_code);

/**
 *	kgdb_disable_hw_debug - (optional) Disable hardware debugging hook
 *	@regs: Current &struct pt_regs.
 *
 *	This function will be called if the particular architecture must
 *	disable hardware debugging while it is processing gdb packets or
 *	handling exception.
 */
extern void kgdb_disable_hw_debug(struct pt_regs *regs);

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

extern atomic_t			kgdb_setting_breakpoint;
extern atomic_t			kgdb_cpu_doing_single_step;

extern struct task_struct	*kgdb_usethread;
extern struct task_struct	*kgdb_contthread;

enum kgdb_bptype {
	BP_BREAKPOINT = 0,
	BP_HARDWARE_BREAKPOINT,
	BP_WRITE_WATCHPOINT,
	BP_READ_WATCHPOINT,
	BP_ACCESS_WATCHPOINT
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
 *	@flags: Current IRQ state
 *
 *	On SMP systems, we need to get the attention of the other CPUs
 *	and get them into a known state.  This should do what is needed
 *	to get the other CPUs to call kgdb_wait(). Note that on some arches,
 *	the NMI approach is not used for rounding up all the CPUs. For example,
 *	in case of MIPS, smp_call_function() is used to roundup CPUs. In
 *	this case, we have to make sure that interrupts are enabled before
 *	calling smp_call_function(). The argument to this function is
 *	the flags that will be used when restoring the interrupts. There is
 *	local_irq_save() call before kgdb_roundup_cpus().
 *
 *	On non-SMP systems, this is not called.
 */
extern void kgdb_roundup_cpus(unsigned long flags);

/* Optional functions. */
extern int kgdb_validate_break_address(unsigned long addr);
extern int kgdb_arch_set_breakpoint(unsigned long addr, char *saved_instr);
extern int kgdb_arch_remove_breakpoint(unsigned long addr, char *bundle);

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
 * @remove_all_hw_break: Allow an architecture to specify how to remove all
 * hardware breakpoints.
 * @correct_hw_break: Allow an architecture to specify how to correct the
 * hardware debug registers.
 */
struct kgdb_arch {
	unsigned char		gdb_bpt_instr[BREAK_INSTR_SIZE];
	unsigned long		flags;

	int	(*set_breakpoint)(unsigned long, char *);
	int	(*remove_breakpoint)(unsigned long, char *);
	int	(*set_hw_breakpoint)(unsigned long, int, enum kgdb_bptype);
	int	(*remove_hw_breakpoint)(unsigned long, int, enum kgdb_bptype);
	void	(*remove_all_hw_break)(void);
	void	(*correct_hw_break)(void);
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
 */
struct kgdb_io {
	const char		*name;
	int			(*read_char) (void);
	void			(*write_char) (u8);
	void			(*flush) (void);
	int			(*init) (void);
	void			(*pre_exception) (void);
	void			(*post_exception) (void);
};

extern struct kgdb_arch		arch_kgdb_ops;

extern unsigned long __weak kgdb_arch_pc(int exception, struct pt_regs *regs);

extern int kgdb_register_io_module(struct kgdb_io *local_kgdb_io_ops);
extern void kgdb_unregister_io_module(struct kgdb_io *local_kgdb_io_ops);

extern int kgdb_hex2long(char **ptr, unsigned long *long_val);
extern int kgdb_mem2hex(char *mem, char *buf, int count);
extern int kgdb_hex2mem(char *buf, char *mem, int count);

extern int kgdb_isremovedbreak(unsigned long addr);

extern int
kgdb_handle_exception(int ex_vector, int signo, int err_code,
		      struct pt_regs *regs);
extern int kgdb_nmicallback(int cpu, void *regs);

extern int			kgdb_single_step;
extern atomic_t			kgdb_active;

#endif /* _KGDB_H_ */
