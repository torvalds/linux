/*
 * KGDB stub.
 *
 * Maintainer: Jason Wessel <jason.wessel@windriver.com>
 *
 * Copyright (C) 2000-2001 VERITAS Software Corporation.
 * Copyright (C) 2002-2004 Timesys Corporation
 * Copyright (C) 2003-2004 Amit S. Kale <amitkale@linsyssoft.com>
 * Copyright (C) 2004 Pavel Machek <pavel@suse.cz>
 * Copyright (C) 2004-2006 Tom Rini <trini@kernel.crashing.org>
 * Copyright (C) 2004-2006 LinSysSoft Technologies Pvt. Ltd.
 * Copyright (C) 2005-2008 Wind River Systems, Inc.
 * Copyright (C) 2007 MontaVista Software, Inc.
 * Copyright (C) 2008 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 * Contributors at various stages not listed above:
 *  Jason Wessel ( jason.wessel@windriver.com )
 *  George Anzinger <george@mvista.com>
 *  Anurekh Saxena (anurekh.saxena@timesys.com)
 *  Lake Stevens Instrument Division (Glenn Engel)
 *  Jim Kingdon, Cygnus Support.
 *
 * Original KGDB stub: David Grothe <dave@gcom.com>,
 * Tigran Aivazian <tigran@sco.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */
#include <linux/pid_namespace.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/console.h>
#include <linux/threads.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/reboot.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/sysrq.h>
#include <linux/init.h>
#include <linux/kgdb.h>
#include <linux/pid.h>
#include <linux/smp.h>
#include <linux/mm.h>

#include <asm/cacheflush.h>
#include <asm/byteorder.h>
#include <asm/atomic.h>
#include <asm/system.h>
#include <asm/unaligned.h>

static int kgdb_break_asap;

#define KGDB_MAX_THREAD_QUERY 17
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
};

/* Exception state values */
#define DCPU_WANT_MASTER 0x1 /* Waiting to become a master kgdb cpu */
#define DCPU_NEXT_MASTER 0x2 /* Transition from one master cpu to another */
#define DCPU_IS_SLAVE    0x4 /* Slave cpu enter exception */
#define DCPU_SSTEP       0x8 /* CPU is single stepping */

static struct debuggerinfo_struct {
	void			*debuggerinfo;
	struct task_struct	*task;
	int 			exception_state;
} kgdb_info[NR_CPUS];

/**
 * kgdb_connected - Is a host GDB connected to us?
 */
int				kgdb_connected;
EXPORT_SYMBOL_GPL(kgdb_connected);

/* All the KGDB handlers are installed */
static int			kgdb_io_module_registered;

/* Guard for recursive entry */
static int			exception_level;

static struct kgdb_io		*kgdb_io_ops;
static DEFINE_SPINLOCK(kgdb_registration_lock);

/* kgdb console driver is loaded */
static int kgdb_con_registered;
/* determine if kgdb console output should be used */
static int kgdb_use_con;

static int __init opt_kgdb_con(char *str)
{
	kgdb_use_con = 1;
	return 0;
}

early_param("kgdbcon", opt_kgdb_con);

module_param(kgdb_use_con, int, 0644);

/*
 * Holds information about breakpoints in a kernel. These breakpoints are
 * added and removed by gdb.
 */
static struct kgdb_bkpt		kgdb_break[KGDB_MAX_BREAKPOINTS] = {
	[0 ... KGDB_MAX_BREAKPOINTS-1] = { .state = BP_UNDEFINED }
};

/*
 * The CPU# of the active CPU, or -1 if none:
 */
atomic_t			kgdb_active = ATOMIC_INIT(-1);

/*
 * We use NR_CPUs not PERCPU, in case kgdb is used to debug early
 * bootup code (which might not have percpu set up yet):
 */
static atomic_t			passive_cpu_wait[NR_CPUS];
static atomic_t			cpu_in_kgdb[NR_CPUS];
atomic_t			kgdb_setting_breakpoint;

struct task_struct		*kgdb_usethread;
struct task_struct		*kgdb_contthread;

int				kgdb_single_step;
pid_t				kgdb_sstep_pid;

/* Our I/O buffers. */
static char			remcom_in_buffer[BUFMAX];
static char			remcom_out_buffer[BUFMAX];

/* Storage for the registers, in GDB format. */
static unsigned long		gdb_regs[(NUMREGBYTES +
					sizeof(unsigned long) - 1) /
					sizeof(unsigned long)];

/* to keep track of the CPU which is doing the single stepping*/
atomic_t			kgdb_cpu_doing_single_step = ATOMIC_INIT(-1);

/*
 * If you are debugging a problem where roundup (the collection of
 * all other CPUs) is a problem [this should be extremely rare],
 * then use the nokgdbroundup option to avoid roundup. In that case
 * the other CPUs might interfere with your debugging context, so
 * use this with care:
 */
static int kgdb_do_roundup = 1;

static int __init opt_nokgdbroundup(char *str)
{
	kgdb_do_roundup = 0;

	return 0;
}

early_param("nokgdbroundup", opt_nokgdbroundup);

/*
 * Finally, some KGDB code :-)
 */

/*
 * Weak aliases for breakpoint management,
 * can be overriden by architectures when needed:
 */
int __weak kgdb_arch_set_breakpoint(unsigned long addr, char *saved_instr)
{
	int err;

	err = probe_kernel_read(saved_instr, (char *)addr, BREAK_INSTR_SIZE);
	if (err)
		return err;

	return probe_kernel_write((char *)addr, arch_kgdb_ops.gdb_bpt_instr,
				  BREAK_INSTR_SIZE);
}

int __weak kgdb_arch_remove_breakpoint(unsigned long addr, char *bundle)
{
	return probe_kernel_write((char *)addr,
				  (char *)bundle, BREAK_INSTR_SIZE);
}

int __weak kgdb_validate_break_address(unsigned long addr)
{
	char tmp_variable[BREAK_INSTR_SIZE];
	int err;
	/* Validate setting the breakpoint and then removing it.  In the
	 * remove fails, the kernel needs to emit a bad message because we
	 * are deep trouble not being able to put things back the way we
	 * found them.
	 */
	err = kgdb_arch_set_breakpoint(addr, tmp_variable);
	if (err)
		return err;
	err = kgdb_arch_remove_breakpoint(addr, tmp_variable);
	if (err)
		printk(KERN_ERR "KGDB: Critical breakpoint error, kernel "
		   "memory destroyed at: %lx", addr);
	return err;
}

unsigned long __weak kgdb_arch_pc(int exception, struct pt_regs *regs)
{
	return instruction_pointer(regs);
}

int __weak kgdb_arch_init(void)
{
	return 0;
}

int __weak kgdb_skipexception(int exception, struct pt_regs *regs)
{
	return 0;
}

void __weak
kgdb_post_primary_code(struct pt_regs *regs, int e_vector, int err_code)
{
	return;
}

/**
 *	kgdb_disable_hw_debug - Disable hardware debugging while we in kgdb.
 *	@regs: Current &struct pt_regs.
 *
 *	This function will be called if the particular architecture must
 *	disable hardware debugging while it is processing gdb packets or
 *	handling exception.
 */
void __weak kgdb_disable_hw_debug(struct pt_regs *regs)
{
}

/*
 * GDB remote protocol parser:
 */

static int hex(char ch)
{
	if ((ch >= 'a') && (ch <= 'f'))
		return ch - 'a' + 10;
	if ((ch >= '0') && (ch <= '9'))
		return ch - '0';
	if ((ch >= 'A') && (ch <= 'F'))
		return ch - 'A' + 10;
	return -1;
}

/* scan for the sequence $<data>#<checksum> */
static void get_packet(char *buffer)
{
	unsigned char checksum;
	unsigned char xmitcsum;
	int count;
	char ch;

	do {
		/*
		 * Spin and wait around for the start character, ignore all
		 * other characters:
		 */
		while ((ch = (kgdb_io_ops->read_char())) != '$')
			/* nothing */;

		kgdb_connected = 1;
		checksum = 0;
		xmitcsum = -1;

		count = 0;

		/*
		 * now, read until a # or end of buffer is found:
		 */
		while (count < (BUFMAX - 1)) {
			ch = kgdb_io_ops->read_char();
			if (ch == '#')
				break;
			checksum = checksum + ch;
			buffer[count] = ch;
			count = count + 1;
		}
		buffer[count] = 0;

		if (ch == '#') {
			xmitcsum = hex(kgdb_io_ops->read_char()) << 4;
			xmitcsum += hex(kgdb_io_ops->read_char());

			if (checksum != xmitcsum)
				/* failed checksum */
				kgdb_io_ops->write_char('-');
			else
				/* successful transfer */
				kgdb_io_ops->write_char('+');
			if (kgdb_io_ops->flush)
				kgdb_io_ops->flush();
		}
	} while (checksum != xmitcsum);
}

/*
 * Send the packet in buffer.
 * Check for gdb connection if asked for.
 */
static void put_packet(char *buffer)
{
	unsigned char checksum;
	int count;
	char ch;

	/*
	 * $<packet info>#<checksum>.
	 */
	while (1) {
		kgdb_io_ops->write_char('$');
		checksum = 0;
		count = 0;

		while ((ch = buffer[count])) {
			kgdb_io_ops->write_char(ch);
			checksum += ch;
			count++;
		}

		kgdb_io_ops->write_char('#');
		kgdb_io_ops->write_char(hex_asc_hi(checksum));
		kgdb_io_ops->write_char(hex_asc_lo(checksum));
		if (kgdb_io_ops->flush)
			kgdb_io_ops->flush();

		/* Now see what we get in reply. */
		ch = kgdb_io_ops->read_char();

		if (ch == 3)
			ch = kgdb_io_ops->read_char();

		/* If we get an ACK, we are done. */
		if (ch == '+')
			return;

		/*
		 * If we get the start of another packet, this means
		 * that GDB is attempting to reconnect.  We will NAK
		 * the packet being sent, and stop trying to send this
		 * packet.
		 */
		if (ch == '$') {
			kgdb_io_ops->write_char('-');
			if (kgdb_io_ops->flush)
				kgdb_io_ops->flush();
			return;
		}
	}
}

/*
 * Convert the memory pointed to by mem into hex, placing result in buf.
 * Return a pointer to the last char put in buf (null). May return an error.
 */
int kgdb_mem2hex(char *mem, char *buf, int count)
{
	char *tmp;
	int err;

	/*
	 * We use the upper half of buf as an intermediate buffer for the
	 * raw memory copy.  Hex conversion will work against this one.
	 */
	tmp = buf + count;

	err = probe_kernel_read(tmp, mem, count);
	if (!err) {
		while (count > 0) {
			buf = pack_hex_byte(buf, *tmp);
			tmp++;
			count--;
		}

		*buf = 0;
	}

	return err;
}

/*
 * Copy the binary array pointed to by buf into mem.  Fix $, #, and
 * 0x7d escaped with 0x7d. Return -EFAULT on failure or 0 on success.
 * The input buf is overwitten with the result to write to mem.
 */
static int kgdb_ebin2mem(char *buf, char *mem, int count)
{
	int size = 0;
	char *c = buf;

	while (count-- > 0) {
		c[size] = *buf++;
		if (c[size] == 0x7d)
			c[size] = *buf++ ^ 0x20;
		size++;
	}

	return probe_kernel_write(mem, c, size);
}

/*
 * Convert the hex array pointed to by buf into binary to be placed in mem.
 * Return a pointer to the character AFTER the last byte written.
 * May return an error.
 */
int kgdb_hex2mem(char *buf, char *mem, int count)
{
	char *tmp_raw;
	char *tmp_hex;

	/*
	 * We use the upper half of buf as an intermediate buffer for the
	 * raw memory that is converted from hex.
	 */
	tmp_raw = buf + count * 2;

	tmp_hex = tmp_raw - 1;
	while (tmp_hex >= buf) {
		tmp_raw--;
		*tmp_raw = hex(*tmp_hex--);
		*tmp_raw |= hex(*tmp_hex--) << 4;
	}

	return probe_kernel_write(mem, tmp_raw, count);
}

/*
 * While we find nice hex chars, build a long_val.
 * Return number of chars processed.
 */
int kgdb_hex2long(char **ptr, unsigned long *long_val)
{
	int hex_val;
	int num = 0;
	int negate = 0;

	*long_val = 0;

	if (**ptr == '-') {
		negate = 1;
		(*ptr)++;
	}
	while (**ptr) {
		hex_val = hex(**ptr);
		if (hex_val < 0)
			break;

		*long_val = (*long_val << 4) | hex_val;
		num++;
		(*ptr)++;
	}

	if (negate)
		*long_val = -*long_val;

	return num;
}

/* Write memory due to an 'M' or 'X' packet. */
static int write_mem_msg(int binary)
{
	char *ptr = &remcom_in_buffer[1];
	unsigned long addr;
	unsigned long length;
	int err;

	if (kgdb_hex2long(&ptr, &addr) > 0 && *(ptr++) == ',' &&
	    kgdb_hex2long(&ptr, &length) > 0 && *(ptr++) == ':') {
		if (binary)
			err = kgdb_ebin2mem(ptr, (char *)addr, length);
		else
			err = kgdb_hex2mem(ptr, (char *)addr, length);
		if (err)
			return err;
		if (CACHE_FLUSH_IS_SAFE)
			flush_icache_range(addr, addr + length);
		return 0;
	}

	return -EINVAL;
}

static void error_packet(char *pkt, int error)
{
	error = -error;
	pkt[0] = 'E';
	pkt[1] = hex_asc[(error / 10)];
	pkt[2] = hex_asc[(error % 10)];
	pkt[3] = '\0';
}

/*
 * Thread ID accessors. We represent a flat TID space to GDB, where
 * the per CPU idle threads (which under Linux all have PID 0) are
 * remapped to negative TIDs.
 */

#define BUF_THREAD_ID_SIZE	16

static char *pack_threadid(char *pkt, unsigned char *id)
{
	char *limit;

	limit = pkt + BUF_THREAD_ID_SIZE;
	while (pkt < limit)
		pkt = pack_hex_byte(pkt, *id++);

	return pkt;
}

static void int_to_threadref(unsigned char *id, int value)
{
	unsigned char *scan;
	int i = 4;

	scan = (unsigned char *)id;
	while (i--)
		*scan++ = 0;
	put_unaligned_be32(value, scan);
}

static struct task_struct *getthread(struct pt_regs *regs, int tid)
{
	/*
	 * Non-positive TIDs are remapped to the cpu shadow information
	 */
	if (tid == 0 || tid == -1)
		tid = -atomic_read(&kgdb_active) - 2;
	if (tid < -1 && tid > -NR_CPUS - 2) {
		if (kgdb_info[-tid - 2].task)
			return kgdb_info[-tid - 2].task;
		else
			return idle_task(-tid - 2);
	}
	if (tid <= 0) {
		printk(KERN_ERR "KGDB: Internal thread select error\n");
		dump_stack();
		return NULL;
	}

	/*
	 * find_task_by_pid_ns() does not take the tasklist lock anymore
	 * but is nicely RCU locked - hence is a pretty resilient
	 * thing to use:
	 */
	return find_task_by_pid_ns(tid, &init_pid_ns);
}

/*
 * Some architectures need cache flushes when we set/clear a
 * breakpoint:
 */
static void kgdb_flush_swbreak_addr(unsigned long addr)
{
	if (!CACHE_FLUSH_IS_SAFE)
		return;

	if (current->mm && current->mm->mmap_cache) {
		flush_cache_range(current->mm->mmap_cache,
				  addr, addr + BREAK_INSTR_SIZE);
	}
	/* Force flush instruction cache if it was outside the mm */
	flush_icache_range(addr, addr + BREAK_INSTR_SIZE);
}

/*
 * SW breakpoint management:
 */
static int kgdb_activate_sw_breakpoints(void)
{
	unsigned long addr;
	int error;
	int ret = 0;
	int i;

	for (i = 0; i < KGDB_MAX_BREAKPOINTS; i++) {
		if (kgdb_break[i].state != BP_SET)
			continue;

		addr = kgdb_break[i].bpt_addr;
		error = kgdb_arch_set_breakpoint(addr,
				kgdb_break[i].saved_instr);
		if (error) {
			ret = error;
			printk(KERN_INFO "KGDB: BP install failed: %lx", addr);
			continue;
		}

		kgdb_flush_swbreak_addr(addr);
		kgdb_break[i].state = BP_ACTIVE;
	}
	return ret;
}

static int kgdb_set_sw_break(unsigned long addr)
{
	int err = kgdb_validate_break_address(addr);
	int breakno = -1;
	int i;

	if (err)
		return err;

	for (i = 0; i < KGDB_MAX_BREAKPOINTS; i++) {
		if ((kgdb_break[i].state == BP_SET) &&
					(kgdb_break[i].bpt_addr == addr))
			return -EEXIST;
	}
	for (i = 0; i < KGDB_MAX_BREAKPOINTS; i++) {
		if (kgdb_break[i].state == BP_REMOVED &&
					kgdb_break[i].bpt_addr == addr) {
			breakno = i;
			break;
		}
	}

	if (breakno == -1) {
		for (i = 0; i < KGDB_MAX_BREAKPOINTS; i++) {
			if (kgdb_break[i].state == BP_UNDEFINED) {
				breakno = i;
				break;
			}
		}
	}

	if (breakno == -1)
		return -E2BIG;

	kgdb_break[breakno].state = BP_SET;
	kgdb_break[breakno].type = BP_BREAKPOINT;
	kgdb_break[breakno].bpt_addr = addr;

	return 0;
}

static int kgdb_deactivate_sw_breakpoints(void)
{
	unsigned long addr;
	int error;
	int ret = 0;
	int i;

	for (i = 0; i < KGDB_MAX_BREAKPOINTS; i++) {
		if (kgdb_break[i].state != BP_ACTIVE)
			continue;
		addr = kgdb_break[i].bpt_addr;
		error = kgdb_arch_remove_breakpoint(addr,
					kgdb_break[i].saved_instr);
		if (error) {
			printk(KERN_INFO "KGDB: BP remove failed: %lx\n", addr);
			ret = error;
		}

		kgdb_flush_swbreak_addr(addr);
		kgdb_break[i].state = BP_SET;
	}
	return ret;
}

static int kgdb_remove_sw_break(unsigned long addr)
{
	int i;

	for (i = 0; i < KGDB_MAX_BREAKPOINTS; i++) {
		if ((kgdb_break[i].state == BP_SET) &&
				(kgdb_break[i].bpt_addr == addr)) {
			kgdb_break[i].state = BP_REMOVED;
			return 0;
		}
	}
	return -ENOENT;
}

int kgdb_isremovedbreak(unsigned long addr)
{
	int i;

	for (i = 0; i < KGDB_MAX_BREAKPOINTS; i++) {
		if ((kgdb_break[i].state == BP_REMOVED) &&
					(kgdb_break[i].bpt_addr == addr))
			return 1;
	}
	return 0;
}

static int remove_all_break(void)
{
	unsigned long addr;
	int error;
	int i;

	/* Clear memory breakpoints. */
	for (i = 0; i < KGDB_MAX_BREAKPOINTS; i++) {
		if (kgdb_break[i].state != BP_ACTIVE)
			goto setundefined;
		addr = kgdb_break[i].bpt_addr;
		error = kgdb_arch_remove_breakpoint(addr,
				kgdb_break[i].saved_instr);
		if (error)
			printk(KERN_ERR "KGDB: breakpoint remove failed: %lx\n",
			   addr);
setundefined:
		kgdb_break[i].state = BP_UNDEFINED;
	}

	/* Clear hardware breakpoints. */
	if (arch_kgdb_ops.remove_all_hw_break)
		arch_kgdb_ops.remove_all_hw_break();

	return 0;
}

/*
 * Remap normal tasks to their real PID,
 * CPU shadow threads are mapped to -CPU - 2
 */
static inline int shadow_pid(int realpid)
{
	if (realpid)
		return realpid;

	return -raw_smp_processor_id() - 2;
}

static char gdbmsgbuf[BUFMAX + 1];

static void kgdb_msg_write(const char *s, int len)
{
	char *bufptr;
	int wcount;
	int i;

	/* 'O'utput */
	gdbmsgbuf[0] = 'O';

	/* Fill and send buffers... */
	while (len > 0) {
		bufptr = gdbmsgbuf + 1;

		/* Calculate how many this time */
		if ((len << 1) > (BUFMAX - 2))
			wcount = (BUFMAX - 2) >> 1;
		else
			wcount = len;

		/* Pack in hex chars */
		for (i = 0; i < wcount; i++)
			bufptr = pack_hex_byte(bufptr, s[i]);
		*bufptr = '\0';

		/* Move up */
		s += wcount;
		len -= wcount;

		/* Write packet */
		put_packet(gdbmsgbuf);
	}
}

/*
 * Return true if there is a valid kgdb I/O module.  Also if no
 * debugger is attached a message can be printed to the console about
 * waiting for the debugger to attach.
 *
 * The print_wait argument is only to be true when called from inside
 * the core kgdb_handle_exception, because it will wait for the
 * debugger to attach.
 */
static int kgdb_io_ready(int print_wait)
{
	if (!kgdb_io_ops)
		return 0;
	if (kgdb_connected)
		return 1;
	if (atomic_read(&kgdb_setting_breakpoint))
		return 1;
	if (print_wait)
		printk(KERN_CRIT "KGDB: Waiting for remote debugger\n");
	return 1;
}

/*
 * All the functions that start with gdb_cmd are the various
 * operations to implement the handlers for the gdbserial protocol
 * where KGDB is communicating with an external debugger
 */

/* Handle the '?' status packets */
static void gdb_cmd_status(struct kgdb_state *ks)
{
	/*
	 * We know that this packet is only sent
	 * during initial connect.  So to be safe,
	 * we clear out our breakpoints now in case
	 * GDB is reconnecting.
	 */
	remove_all_break();

	remcom_out_buffer[0] = 'S';
	pack_hex_byte(&remcom_out_buffer[1], ks->signo);
}

/* Handle the 'g' get registers request */
static void gdb_cmd_getregs(struct kgdb_state *ks)
{
	struct task_struct *thread;
	void *local_debuggerinfo;
	int i;

	thread = kgdb_usethread;
	if (!thread) {
		thread = kgdb_info[ks->cpu].task;
		local_debuggerinfo = kgdb_info[ks->cpu].debuggerinfo;
	} else {
		local_debuggerinfo = NULL;
		for_each_online_cpu(i) {
			/*
			 * Try to find the task on some other
			 * or possibly this node if we do not
			 * find the matching task then we try
			 * to approximate the results.
			 */
			if (thread == kgdb_info[i].task)
				local_debuggerinfo = kgdb_info[i].debuggerinfo;
		}
	}

	/*
	 * All threads that don't have debuggerinfo should be
	 * in schedule() sleeping, since all other CPUs
	 * are in kgdb_wait, and thus have debuggerinfo.
	 */
	if (local_debuggerinfo) {
		pt_regs_to_gdb_regs(gdb_regs, local_debuggerinfo);
	} else {
		/*
		 * Pull stuff saved during switch_to; nothing
		 * else is accessible (or even particularly
		 * relevant).
		 *
		 * This should be enough for a stack trace.
		 */
		sleeping_thread_to_gdb_regs(gdb_regs, thread);
	}
	kgdb_mem2hex((char *)gdb_regs, remcom_out_buffer, NUMREGBYTES);
}

/* Handle the 'G' set registers request */
static void gdb_cmd_setregs(struct kgdb_state *ks)
{
	kgdb_hex2mem(&remcom_in_buffer[1], (char *)gdb_regs, NUMREGBYTES);

	if (kgdb_usethread && kgdb_usethread != current) {
		error_packet(remcom_out_buffer, -EINVAL);
	} else {
		gdb_regs_to_pt_regs(gdb_regs, ks->linux_regs);
		strcpy(remcom_out_buffer, "OK");
	}
}

/* Handle the 'm' memory read bytes */
static void gdb_cmd_memread(struct kgdb_state *ks)
{
	char *ptr = &remcom_in_buffer[1];
	unsigned long length;
	unsigned long addr;
	int err;

	if (kgdb_hex2long(&ptr, &addr) > 0 && *ptr++ == ',' &&
					kgdb_hex2long(&ptr, &length) > 0) {
		err = kgdb_mem2hex((char *)addr, remcom_out_buffer, length);
		if (err)
			error_packet(remcom_out_buffer, err);
	} else {
		error_packet(remcom_out_buffer, -EINVAL);
	}
}

/* Handle the 'M' memory write bytes */
static void gdb_cmd_memwrite(struct kgdb_state *ks)
{
	int err = write_mem_msg(0);

	if (err)
		error_packet(remcom_out_buffer, err);
	else
		strcpy(remcom_out_buffer, "OK");
}

/* Handle the 'X' memory binary write bytes */
static void gdb_cmd_binwrite(struct kgdb_state *ks)
{
	int err = write_mem_msg(1);

	if (err)
		error_packet(remcom_out_buffer, err);
	else
		strcpy(remcom_out_buffer, "OK");
}

/* Handle the 'D' or 'k', detach or kill packets */
static void gdb_cmd_detachkill(struct kgdb_state *ks)
{
	int error;

	/* The detach case */
	if (remcom_in_buffer[0] == 'D') {
		error = remove_all_break();
		if (error < 0) {
			error_packet(remcom_out_buffer, error);
		} else {
			strcpy(remcom_out_buffer, "OK");
			kgdb_connected = 0;
		}
		put_packet(remcom_out_buffer);
	} else {
		/*
		 * Assume the kill case, with no exit code checking,
		 * trying to force detach the debugger:
		 */
		remove_all_break();
		kgdb_connected = 0;
	}
}

/* Handle the 'R' reboot packets */
static int gdb_cmd_reboot(struct kgdb_state *ks)
{
	/* For now, only honor R0 */
	if (strcmp(remcom_in_buffer, "R0") == 0) {
		printk(KERN_CRIT "Executing emergency reboot\n");
		strcpy(remcom_out_buffer, "OK");
		put_packet(remcom_out_buffer);

		/*
		 * Execution should not return from
		 * machine_emergency_restart()
		 */
		machine_emergency_restart();
		kgdb_connected = 0;

		return 1;
	}
	return 0;
}

/* Handle the 'q' query packets */
static void gdb_cmd_query(struct kgdb_state *ks)
{
	struct task_struct *g;
	struct task_struct *p;
	unsigned char thref[8];
	char *ptr;
	int i;
	int cpu;
	int finished = 0;

	switch (remcom_in_buffer[1]) {
	case 's':
	case 'f':
		if (memcmp(remcom_in_buffer + 2, "ThreadInfo", 10)) {
			error_packet(remcom_out_buffer, -EINVAL);
			break;
		}

		i = 0;
		remcom_out_buffer[0] = 'm';
		ptr = remcom_out_buffer + 1;
		if (remcom_in_buffer[1] == 'f') {
			/* Each cpu is a shadow thread */
			for_each_online_cpu(cpu) {
				ks->thr_query = 0;
				int_to_threadref(thref, -cpu - 2);
				pack_threadid(ptr, thref);
				ptr += BUF_THREAD_ID_SIZE;
				*(ptr++) = ',';
				i++;
			}
		}

		do_each_thread(g, p) {
			if (i >= ks->thr_query && !finished) {
				int_to_threadref(thref, p->pid);
				pack_threadid(ptr, thref);
				ptr += BUF_THREAD_ID_SIZE;
				*(ptr++) = ',';
				ks->thr_query++;
				if (ks->thr_query % KGDB_MAX_THREAD_QUERY == 0)
					finished = 1;
			}
			i++;
		} while_each_thread(g, p);

		*(--ptr) = '\0';
		break;

	case 'C':
		/* Current thread id */
		strcpy(remcom_out_buffer, "QC");
		ks->threadid = shadow_pid(current->pid);
		int_to_threadref(thref, ks->threadid);
		pack_threadid(remcom_out_buffer + 2, thref);
		break;
	case 'T':
		if (memcmp(remcom_in_buffer + 1, "ThreadExtraInfo,", 16)) {
			error_packet(remcom_out_buffer, -EINVAL);
			break;
		}
		ks->threadid = 0;
		ptr = remcom_in_buffer + 17;
		kgdb_hex2long(&ptr, &ks->threadid);
		if (!getthread(ks->linux_regs, ks->threadid)) {
			error_packet(remcom_out_buffer, -EINVAL);
			break;
		}
		if ((int)ks->threadid > 0) {
			kgdb_mem2hex(getthread(ks->linux_regs,
					ks->threadid)->comm,
					remcom_out_buffer, 16);
		} else {
			static char tmpstr[23 + BUF_THREAD_ID_SIZE];

			sprintf(tmpstr, "shadowCPU%d",
					(int)(-ks->threadid - 2));
			kgdb_mem2hex(tmpstr, remcom_out_buffer, strlen(tmpstr));
		}
		break;
	}
}

/* Handle the 'H' task query packets */
static void gdb_cmd_task(struct kgdb_state *ks)
{
	struct task_struct *thread;
	char *ptr;

	switch (remcom_in_buffer[1]) {
	case 'g':
		ptr = &remcom_in_buffer[2];
		kgdb_hex2long(&ptr, &ks->threadid);
		thread = getthread(ks->linux_regs, ks->threadid);
		if (!thread && ks->threadid > 0) {
			error_packet(remcom_out_buffer, -EINVAL);
			break;
		}
		kgdb_usethread = thread;
		ks->kgdb_usethreadid = ks->threadid;
		strcpy(remcom_out_buffer, "OK");
		break;
	case 'c':
		ptr = &remcom_in_buffer[2];
		kgdb_hex2long(&ptr, &ks->threadid);
		if (!ks->threadid) {
			kgdb_contthread = NULL;
		} else {
			thread = getthread(ks->linux_regs, ks->threadid);
			if (!thread && ks->threadid > 0) {
				error_packet(remcom_out_buffer, -EINVAL);
				break;
			}
			kgdb_contthread = thread;
		}
		strcpy(remcom_out_buffer, "OK");
		break;
	}
}

/* Handle the 'T' thread query packets */
static void gdb_cmd_thread(struct kgdb_state *ks)
{
	char *ptr = &remcom_in_buffer[1];
	struct task_struct *thread;

	kgdb_hex2long(&ptr, &ks->threadid);
	thread = getthread(ks->linux_regs, ks->threadid);
	if (thread)
		strcpy(remcom_out_buffer, "OK");
	else
		error_packet(remcom_out_buffer, -EINVAL);
}

/* Handle the 'z' or 'Z' breakpoint remove or set packets */
static void gdb_cmd_break(struct kgdb_state *ks)
{
	/*
	 * Since GDB-5.3, it's been drafted that '0' is a software
	 * breakpoint, '1' is a hardware breakpoint, so let's do that.
	 */
	char *bpt_type = &remcom_in_buffer[1];
	char *ptr = &remcom_in_buffer[2];
	unsigned long addr;
	unsigned long length;
	int error = 0;

	if (arch_kgdb_ops.set_hw_breakpoint && *bpt_type >= '1') {
		/* Unsupported */
		if (*bpt_type > '4')
			return;
	} else {
		if (*bpt_type != '0' && *bpt_type != '1')
			/* Unsupported. */
			return;
	}

	/*
	 * Test if this is a hardware breakpoint, and
	 * if we support it:
	 */
	if (*bpt_type == '1' && !(arch_kgdb_ops.flags & KGDB_HW_BREAKPOINT))
		/* Unsupported. */
		return;

	if (*(ptr++) != ',') {
		error_packet(remcom_out_buffer, -EINVAL);
		return;
	}
	if (!kgdb_hex2long(&ptr, &addr)) {
		error_packet(remcom_out_buffer, -EINVAL);
		return;
	}
	if (*(ptr++) != ',' ||
		!kgdb_hex2long(&ptr, &length)) {
		error_packet(remcom_out_buffer, -EINVAL);
		return;
	}

	if (remcom_in_buffer[0] == 'Z' && *bpt_type == '0')
		error = kgdb_set_sw_break(addr);
	else if (remcom_in_buffer[0] == 'z' && *bpt_type == '0')
		error = kgdb_remove_sw_break(addr);
	else if (remcom_in_buffer[0] == 'Z')
		error = arch_kgdb_ops.set_hw_breakpoint(addr,
			(int)length, *bpt_type - '0');
	else if (remcom_in_buffer[0] == 'z')
		error = arch_kgdb_ops.remove_hw_breakpoint(addr,
			(int) length, *bpt_type - '0');

	if (error == 0)
		strcpy(remcom_out_buffer, "OK");
	else
		error_packet(remcom_out_buffer, error);
}

/* Handle the 'C' signal / exception passing packets */
static int gdb_cmd_exception_pass(struct kgdb_state *ks)
{
	/* C09 == pass exception
	 * C15 == detach kgdb, pass exception
	 */
	if (remcom_in_buffer[1] == '0' && remcom_in_buffer[2] == '9') {

		ks->pass_exception = 1;
		remcom_in_buffer[0] = 'c';

	} else if (remcom_in_buffer[1] == '1' && remcom_in_buffer[2] == '5') {

		ks->pass_exception = 1;
		remcom_in_buffer[0] = 'D';
		remove_all_break();
		kgdb_connected = 0;
		return 1;

	} else {
		kgdb_msg_write("KGDB only knows signal 9 (pass)"
			" and 15 (pass and disconnect)\n"
			"Executing a continue without signal passing\n", 0);
		remcom_in_buffer[0] = 'c';
	}

	/* Indicate fall through */
	return -1;
}

/*
 * This function performs all gdbserial command procesing
 */
static int gdb_serial_stub(struct kgdb_state *ks)
{
	int error = 0;
	int tmp;

	/* Clear the out buffer. */
	memset(remcom_out_buffer, 0, sizeof(remcom_out_buffer));

	if (kgdb_connected) {
		unsigned char thref[8];
		char *ptr;

		/* Reply to host that an exception has occurred */
		ptr = remcom_out_buffer;
		*ptr++ = 'T';
		ptr = pack_hex_byte(ptr, ks->signo);
		ptr += strlen(strcpy(ptr, "thread:"));
		int_to_threadref(thref, shadow_pid(current->pid));
		ptr = pack_threadid(ptr, thref);
		*ptr++ = ';';
		put_packet(remcom_out_buffer);
	}

	kgdb_usethread = kgdb_info[ks->cpu].task;
	ks->kgdb_usethreadid = shadow_pid(kgdb_info[ks->cpu].task->pid);
	ks->pass_exception = 0;

	while (1) {
		error = 0;

		/* Clear the out buffer. */
		memset(remcom_out_buffer, 0, sizeof(remcom_out_buffer));

		get_packet(remcom_in_buffer);

		switch (remcom_in_buffer[0]) {
		case '?': /* gdbserial status */
			gdb_cmd_status(ks);
			break;
		case 'g': /* return the value of the CPU registers */
			gdb_cmd_getregs(ks);
			break;
		case 'G': /* set the value of the CPU registers - return OK */
			gdb_cmd_setregs(ks);
			break;
		case 'm': /* mAA..AA,LLLL  Read LLLL bytes at address AA..AA */
			gdb_cmd_memread(ks);
			break;
		case 'M': /* MAA..AA,LLLL: Write LLLL bytes at address AA..AA */
			gdb_cmd_memwrite(ks);
			break;
		case 'X': /* XAA..AA,LLLL: Write LLLL bytes at address AA..AA */
			gdb_cmd_binwrite(ks);
			break;
			/* kill or detach. KGDB should treat this like a
			 * continue.
			 */
		case 'D': /* Debugger detach */
		case 'k': /* Debugger detach via kill */
			gdb_cmd_detachkill(ks);
			goto default_handle;
		case 'R': /* Reboot */
			if (gdb_cmd_reboot(ks))
				goto default_handle;
			break;
		case 'q': /* query command */
			gdb_cmd_query(ks);
			break;
		case 'H': /* task related */
			gdb_cmd_task(ks);
			break;
		case 'T': /* Query thread status */
			gdb_cmd_thread(ks);
			break;
		case 'z': /* Break point remove */
		case 'Z': /* Break point set */
			gdb_cmd_break(ks);
			break;
		case 'C': /* Exception passing */
			tmp = gdb_cmd_exception_pass(ks);
			if (tmp > 0)
				goto default_handle;
			if (tmp == 0)
				break;
			/* Fall through on tmp < 0 */
		case 'c': /* Continue packet */
		case 's': /* Single step packet */
			if (kgdb_contthread && kgdb_contthread != current) {
				/* Can't switch threads in kgdb */
				error_packet(remcom_out_buffer, -EINVAL);
				break;
			}
			kgdb_activate_sw_breakpoints();
			/* Fall through to default processing */
		default:
default_handle:
			error = kgdb_arch_handle_exception(ks->ex_vector,
						ks->signo,
						ks->err_code,
						remcom_in_buffer,
						remcom_out_buffer,
						ks->linux_regs);
			/*
			 * Leave cmd processing on error, detach,
			 * kill, continue, or single step.
			 */
			if (error >= 0 || remcom_in_buffer[0] == 'D' ||
			    remcom_in_buffer[0] == 'k') {
				error = 0;
				goto kgdb_exit;
			}

		}

		/* reply to the request */
		put_packet(remcom_out_buffer);
	}

kgdb_exit:
	if (ks->pass_exception)
		error = 1;
	return error;
}

static int kgdb_reenter_check(struct kgdb_state *ks)
{
	unsigned long addr;

	if (atomic_read(&kgdb_active) != raw_smp_processor_id())
		return 0;

	/* Panic on recursive debugger calls: */
	exception_level++;
	addr = kgdb_arch_pc(ks->ex_vector, ks->linux_regs);
	kgdb_deactivate_sw_breakpoints();

	/*
	 * If the break point removed ok at the place exception
	 * occurred, try to recover and print a warning to the end
	 * user because the user planted a breakpoint in a place that
	 * KGDB needs in order to function.
	 */
	if (kgdb_remove_sw_break(addr) == 0) {
		exception_level = 0;
		kgdb_skipexception(ks->ex_vector, ks->linux_regs);
		kgdb_activate_sw_breakpoints();
		printk(KERN_CRIT "KGDB: re-enter error: breakpoint removed %lx\n",
			addr);
		WARN_ON_ONCE(1);

		return 1;
	}
	remove_all_break();
	kgdb_skipexception(ks->ex_vector, ks->linux_regs);

	if (exception_level > 1) {
		dump_stack();
		panic("Recursive entry to debugger");
	}

	printk(KERN_CRIT "KGDB: re-enter exception: ALL breakpoints killed\n");
	dump_stack();
	panic("Recursive entry to debugger");

	return 1;
}

static int kgdb_cpu_enter(struct kgdb_state *ks, struct pt_regs *regs)
{
	unsigned long flags;
	int sstep_tries = 100;
	int error = 0;
	int i, cpu;
	int trace_on = 0;
acquirelock:
	/*
	 * Interrupts will be restored by the 'trap return' code, except when
	 * single stepping.
	 */
	local_irq_save(flags);

	cpu = ks->cpu;
	kgdb_info[cpu].debuggerinfo = regs;
	kgdb_info[cpu].task = current;
	/*
	 * Make sure the above info reaches the primary CPU before
	 * our cpu_in_kgdb[] flag setting does:
	 */
	atomic_inc(&cpu_in_kgdb[cpu]);

	/*
	 * CPU will loop if it is a slave or request to become a kgdb
	 * master cpu and acquire the kgdb_active lock:
	 */
	while (1) {
		if (kgdb_info[cpu].exception_state & DCPU_WANT_MASTER) {
			if (atomic_cmpxchg(&kgdb_active, -1, cpu) == cpu)
				break;
		} else if (kgdb_info[cpu].exception_state & DCPU_IS_SLAVE) {
			if (!atomic_read(&passive_cpu_wait[cpu]))
				goto return_normal;
		} else {
return_normal:
			/* Return to normal operation by executing any
			 * hw breakpoint fixup.
			 */
			if (arch_kgdb_ops.correct_hw_break)
				arch_kgdb_ops.correct_hw_break();
			if (trace_on)
				tracing_on();
			atomic_dec(&cpu_in_kgdb[cpu]);
			touch_softlockup_watchdog_sync();
			clocksource_touch_watchdog();
			local_irq_restore(flags);
			return 0;
		}
		cpu_relax();
	}

	/*
	 * For single stepping, try to only enter on the processor
	 * that was single stepping.  To gaurd against a deadlock, the
	 * kernel will only try for the value of sstep_tries before
	 * giving up and continuing on.
	 */
	if (atomic_read(&kgdb_cpu_doing_single_step) != -1 &&
	    (kgdb_info[cpu].task &&
	     kgdb_info[cpu].task->pid != kgdb_sstep_pid) && --sstep_tries) {
		atomic_set(&kgdb_active, -1);
		touch_softlockup_watchdog_sync();
		clocksource_touch_watchdog();
		local_irq_restore(flags);

		goto acquirelock;
	}

	if (!kgdb_io_ready(1)) {
		error = 1;
		goto kgdb_restore; /* No I/O connection, so resume the system */
	}

	/*
	 * Don't enter if we have hit a removed breakpoint.
	 */
	if (kgdb_skipexception(ks->ex_vector, ks->linux_regs))
		goto kgdb_restore;

	/* Call the I/O driver's pre_exception routine */
	if (kgdb_io_ops->pre_exception)
		kgdb_io_ops->pre_exception();

	kgdb_disable_hw_debug(ks->linux_regs);

	/*
	 * Get the passive CPU lock which will hold all the non-primary
	 * CPU in a spin state while the debugger is active
	 */
	if (!kgdb_single_step) {
		for (i = 0; i < NR_CPUS; i++)
			atomic_inc(&passive_cpu_wait[i]);
	}

#ifdef CONFIG_SMP
	/* Signal the other CPUs to enter kgdb_wait() */
	if ((!kgdb_single_step) && kgdb_do_roundup)
		kgdb_roundup_cpus(flags);
#endif

	/*
	 * Wait for the other CPUs to be notified and be waiting for us:
	 */
	for_each_online_cpu(i) {
		while (!atomic_read(&cpu_in_kgdb[i]))
			cpu_relax();
	}

	/*
	 * At this point the primary processor is completely
	 * in the debugger and all secondary CPUs are quiescent
	 */
	kgdb_post_primary_code(ks->linux_regs, ks->ex_vector, ks->err_code);
	kgdb_deactivate_sw_breakpoints();
	kgdb_single_step = 0;
	kgdb_contthread = current;
	exception_level = 0;
	trace_on = tracing_is_on();
	if (trace_on)
		tracing_off();

	/* Talk to debugger with gdbserial protocol */
	error = gdb_serial_stub(ks);

	/* Call the I/O driver's post_exception routine */
	if (kgdb_io_ops->post_exception)
		kgdb_io_ops->post_exception();

	atomic_dec(&cpu_in_kgdb[ks->cpu]);

	if (!kgdb_single_step) {
		for (i = NR_CPUS-1; i >= 0; i--)
			atomic_dec(&passive_cpu_wait[i]);
		/*
		 * Wait till all the CPUs have quit
		 * from the debugger.
		 */
		for_each_online_cpu(i) {
			while (atomic_read(&cpu_in_kgdb[i]))
				cpu_relax();
		}
	}

kgdb_restore:
	if (atomic_read(&kgdb_cpu_doing_single_step) != -1) {
		int sstep_cpu = atomic_read(&kgdb_cpu_doing_single_step);
		if (kgdb_info[sstep_cpu].task)
			kgdb_sstep_pid = kgdb_info[sstep_cpu].task->pid;
		else
			kgdb_sstep_pid = 0;
	}
	if (trace_on)
		tracing_on();
	/* Free kgdb_active */
	atomic_set(&kgdb_active, -1);
	touch_softlockup_watchdog_sync();
	clocksource_touch_watchdog();
	local_irq_restore(flags);

	return error;
}

/*
 * kgdb_handle_exception() - main entry point from a kernel exception
 *
 * Locking hierarchy:
 *	interface locks, if any (begin_session)
 *	kgdb lock (kgdb_active)
 */
int
kgdb_handle_exception(int evector, int signo, int ecode, struct pt_regs *regs)
{
	struct kgdb_state kgdb_var;
	struct kgdb_state *ks = &kgdb_var;
	int ret;

	ks->cpu			= raw_smp_processor_id();
	ks->ex_vector		= evector;
	ks->signo		= signo;
	ks->ex_vector		= evector;
	ks->err_code		= ecode;
	ks->kgdb_usethreadid	= 0;
	ks->linux_regs		= regs;

	if (kgdb_reenter_check(ks))
		return 0; /* Ouch, double exception ! */
	kgdb_info[ks->cpu].exception_state |= DCPU_WANT_MASTER;
	ret = kgdb_cpu_enter(ks, regs);
	kgdb_info[ks->cpu].exception_state &= ~DCPU_WANT_MASTER;
	return ret;
}

int kgdb_nmicallback(int cpu, void *regs)
{
#ifdef CONFIG_SMP
	struct kgdb_state kgdb_var;
	struct kgdb_state *ks = &kgdb_var;

	memset(ks, 0, sizeof(struct kgdb_state));
	ks->cpu			= cpu;
	ks->linux_regs		= regs;

	if (!atomic_read(&cpu_in_kgdb[cpu]) &&
	    atomic_read(&kgdb_active) != -1 &&
	    atomic_read(&kgdb_active) != cpu) {
		kgdb_info[cpu].exception_state |= DCPU_IS_SLAVE;
		kgdb_cpu_enter(ks, regs);
		kgdb_info[cpu].exception_state &= ~DCPU_IS_SLAVE;
		return 0;
	}
#endif
	return 1;
}

static void kgdb_console_write(struct console *co, const char *s,
   unsigned count)
{
	unsigned long flags;

	/* If we're debugging, or KGDB has not connected, don't try
	 * and print. */
	if (!kgdb_connected || atomic_read(&kgdb_active) != -1)
		return;

	local_irq_save(flags);
	kgdb_msg_write(s, count);
	local_irq_restore(flags);
}

static struct console kgdbcons = {
	.name		= "kgdb",
	.write		= kgdb_console_write,
	.flags		= CON_PRINTBUFFER | CON_ENABLED,
	.index		= -1,
};

#ifdef CONFIG_MAGIC_SYSRQ
static void sysrq_handle_gdb(int key, struct tty_struct *tty)
{
	if (!kgdb_io_ops) {
		printk(KERN_CRIT "ERROR: No KGDB I/O module available\n");
		return;
	}
	if (!kgdb_connected)
		printk(KERN_CRIT "Entering KGDB\n");

	kgdb_breakpoint();
}

static struct sysrq_key_op sysrq_gdb_op = {
	.handler	= sysrq_handle_gdb,
	.help_msg	= "debug(G)",
	.action_msg	= "DEBUG",
};
#endif

static void kgdb_register_callbacks(void)
{
	if (!kgdb_io_module_registered) {
		kgdb_io_module_registered = 1;
		kgdb_arch_init();
#ifdef CONFIG_MAGIC_SYSRQ
		register_sysrq_key('g', &sysrq_gdb_op);
#endif
		if (kgdb_use_con && !kgdb_con_registered) {
			register_console(&kgdbcons);
			kgdb_con_registered = 1;
		}
	}
}

static void kgdb_unregister_callbacks(void)
{
	/*
	 * When this routine is called KGDB should unregister from the
	 * panic handler and clean up, making sure it is not handling any
	 * break exceptions at the time.
	 */
	if (kgdb_io_module_registered) {
		kgdb_io_module_registered = 0;
		kgdb_arch_exit();
#ifdef CONFIG_MAGIC_SYSRQ
		unregister_sysrq_key('g', &sysrq_gdb_op);
#endif
		if (kgdb_con_registered) {
			unregister_console(&kgdbcons);
			kgdb_con_registered = 0;
		}
	}
}

static void kgdb_initial_breakpoint(void)
{
	kgdb_break_asap = 0;

	printk(KERN_CRIT "kgdb: Waiting for connection from remote gdb...\n");
	kgdb_breakpoint();
}

/**
 *	kgdb_register_io_module - register KGDB IO module
 *	@new_kgdb_io_ops: the io ops vector
 *
 *	Register it with the KGDB core.
 */
int kgdb_register_io_module(struct kgdb_io *new_kgdb_io_ops)
{
	int err;

	spin_lock(&kgdb_registration_lock);

	if (kgdb_io_ops) {
		spin_unlock(&kgdb_registration_lock);

		printk(KERN_ERR "kgdb: Another I/O driver is already "
				"registered with KGDB.\n");
		return -EBUSY;
	}

	if (new_kgdb_io_ops->init) {
		err = new_kgdb_io_ops->init();
		if (err) {
			spin_unlock(&kgdb_registration_lock);
			return err;
		}
	}

	kgdb_io_ops = new_kgdb_io_ops;

	spin_unlock(&kgdb_registration_lock);

	printk(KERN_INFO "kgdb: Registered I/O driver %s.\n",
	       new_kgdb_io_ops->name);

	/* Arm KGDB now. */
	kgdb_register_callbacks();

	if (kgdb_break_asap)
		kgdb_initial_breakpoint();

	return 0;
}
EXPORT_SYMBOL_GPL(kgdb_register_io_module);

/**
 *	kkgdb_unregister_io_module - unregister KGDB IO module
 *	@old_kgdb_io_ops: the io ops vector
 *
 *	Unregister it with the KGDB core.
 */
void kgdb_unregister_io_module(struct kgdb_io *old_kgdb_io_ops)
{
	BUG_ON(kgdb_connected);

	/*
	 * KGDB is no longer able to communicate out, so
	 * unregister our callbacks and reset state.
	 */
	kgdb_unregister_callbacks();

	spin_lock(&kgdb_registration_lock);

	WARN_ON_ONCE(kgdb_io_ops != old_kgdb_io_ops);
	kgdb_io_ops = NULL;

	spin_unlock(&kgdb_registration_lock);

	printk(KERN_INFO
		"kgdb: Unregistered I/O driver %s, debugger disabled.\n",
		old_kgdb_io_ops->name);
}
EXPORT_SYMBOL_GPL(kgdb_unregister_io_module);

/**
 * kgdb_breakpoint - generate breakpoint exception
 *
 * This function will generate a breakpoint exception.  It is used at the
 * beginning of a program to sync up with a debugger and can be used
 * otherwise as a quick means to stop program execution and "break" into
 * the debugger.
 */
void kgdb_breakpoint(void)
{
	atomic_inc(&kgdb_setting_breakpoint);
	wmb(); /* Sync point before breakpoint */
	arch_kgdb_breakpoint();
	wmb(); /* Sync point after breakpoint */
	atomic_dec(&kgdb_setting_breakpoint);
}
EXPORT_SYMBOL_GPL(kgdb_breakpoint);

static int __init opt_kgdb_wait(char *str)
{
	kgdb_break_asap = 1;

	if (kgdb_io_module_registered)
		kgdb_initial_breakpoint();

	return 0;
}

early_param("kgdbwait", opt_kgdb_wait);
