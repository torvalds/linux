/*
 *  linux/init/main.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  GK 2/5/95  -  Changed to support mounting root fs via NFS
 *  Added initrd & change_root: Werner Almesberger & Hans Lermen, Feb '96
 *  Moan early if gcc is old, avoiding bogus kernels - Paul Gortmaker, May '96
 *  Simplified starting of init:  Michael A. Griffith <grif@acm.org> 
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/utsname.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/initrd.h>
#include <linux/hdreg.h>
#include <linux/bootmem.h>
#include <linux/tty.h>
#include <linux/gfp.h>
#include <linux/percpu.h>
#include <linux/kmod.h>
#include <linux/kernel_stat.h>
#include <linux/start_kernel.h>
#include <linux/security.h>
#include <linux/workqueue.h>
#include <linux/profile.h>
#include <linux/rcupdate.h>
#include <linux/moduleparam.h>
#include <linux/kallsyms.h>
#include <linux/writeback.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/cgroup.h>
#include <linux/efi.h>
#include <linux/tick.h>
#include <linux/interrupt.h>
#include <linux/taskstats_kern.h>
#include <linux/delayacct.h>
#include <linux/unistd.h>
#include <linux/rmap.h>
#include <linux/mempolicy.h>
#include <linux/key.h>
#include <linux/unwind.h>
#include <linux/buffer_head.h>
#include <linux/debug_locks.h>
#include <linux/lockdep.h>
#include <linux/pid_namespace.h>
#include <linux/device.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/signal.h>

#include <asm/io.h>
#include <asm/bugs.h>
#include <asm/setup.h>
#include <asm/sections.h>
#include <asm/cacheflush.h>

#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/smp.h>
#endif

/*
 * This is one of the first .c files built. Error out early if we have compiler
 * trouble.
 */

#if __GNUC__ == 4 && __GNUC_MINOR__ == 1 && __GNUC_PATCHLEVEL__ == 0
#warning gcc-4.1.0 is known to miscompile the kernel.  A different compiler version is recommended.
#endif

static int kernel_init(void *);

extern void init_IRQ(void);
extern void fork_init(unsigned long);
extern void mca_init(void);
extern void sbus_init(void);
extern void pidhash_init(void);
extern void pidmap_init(void);
extern void prio_tree_init(void);
extern void radix_tree_init(void);
extern void free_initmem(void);
#ifdef	CONFIG_ACPI
extern void acpi_early_init(void);
#else
static inline void acpi_early_init(void) { }
#endif
#ifndef CONFIG_DEBUG_RODATA
static inline void mark_rodata_ro(void) { }
#endif

#ifdef CONFIG_TC
extern void tc_init(void);
#endif

enum system_states system_state;
EXPORT_SYMBOL(system_state);

/*
 * Boot command-line arguments
 */
#define MAX_INIT_ARGS CONFIG_INIT_ENV_ARG_LIMIT
#define MAX_INIT_ENVS CONFIG_INIT_ENV_ARG_LIMIT

extern void time_init(void);
/* Default late time init is NULL. archs can override this later. */
void (*late_time_init)(void);
extern void softirq_init(void);

/* Untouched command line saved by arch-specific code. */
char __initdata boot_command_line[COMMAND_LINE_SIZE];
/* Untouched saved command line (eg. for /proc) */
char *saved_command_line;
/* Command line for parameter parsing */
static char *static_command_line;

static char *execute_command;
static char *ramdisk_execute_command;

#ifdef CONFIG_SMP
/* Setup configured maximum number of CPUs to activate */
unsigned int __initdata setup_max_cpus = NR_CPUS;

/*
 * Setup routine for controlling SMP activation
 *
 * Command-line option of "nosmp" or "maxcpus=0" will disable SMP
 * activation entirely (the MPS table probe still happens, though).
 *
 * Command-line option of "maxcpus=<NUM>", where <NUM> is an integer
 * greater than 0, limits the maximum number of CPUs activated in
 * SMP mode to <NUM>.
 */
#ifndef CONFIG_X86_IO_APIC
static inline void disable_ioapic_setup(void) {};
#endif

static int __init nosmp(char *str)
{
	setup_max_cpus = 0;
	disable_ioapic_setup();
	return 0;
}

early_param("nosmp", nosmp);

static int __init maxcpus(char *str)
{
	get_option(&str, &setup_max_cpus);
	if (setup_max_cpus == 0)
		disable_ioapic_setup();

	return 0;
}

early_param("maxcpus", maxcpus);
#else
#define setup_max_cpus NR_CPUS
#endif

/*
 * If set, this is an indication to the drivers that reset the underlying
 * device before going ahead with the initialization otherwise driver might
 * rely on the BIOS and skip the reset operation.
 *
 * This is useful if kernel is booting in an unreliable environment.
 * For ex. kdump situaiton where previous kernel has crashed, BIOS has been
 * skipped and devices will be in unknown state.
 */
unsigned int reset_devices;
EXPORT_SYMBOL(reset_devices);

static int __init set_reset_devices(char *str)
{
	reset_devices = 1;
	return 1;
}

__setup("reset_devices", set_reset_devices);

static char * argv_init[MAX_INIT_ARGS+2] = { "init", NULL, };
char * envp_init[MAX_INIT_ENVS+2] = { "HOME=/", "TERM=linux", NULL, };
static const char *panic_later, *panic_param;

extern struct obs_kernel_param __setup_start[], __setup_end[];

static int __init obsolete_checksetup(char *line)
{
	struct obs_kernel_param *p;
	int had_early_param = 0;

	p = __setup_start;
	do {
		int n = strlen(p->str);
		if (!strncmp(line, p->str, n)) {
			if (p->early) {
				/* Already done in parse_early_param?
				 * (Needs exact match on param part).
				 * Keep iterating, as we can have early
				 * params and __setups of same names 8( */
				if (line[n] == '\0' || line[n] == '=')
					had_early_param = 1;
			} else if (!p->setup_func) {
				printk(KERN_WARNING "Parameter %s is obsolete,"
				       " ignored\n", p->str);
				return 1;
			} else if (p->setup_func(line + n))
				return 1;
		}
		p++;
	} while (p < __setup_end);

	return had_early_param;
}

/*
 * This should be approx 2 Bo*oMips to start (note initial shift), and will
 * still work even if initially too large, it will just take slightly longer
 */
unsigned long loops_per_jiffy = (1<<12);

EXPORT_SYMBOL(loops_per_jiffy);

static int __init debug_kernel(char *str)
{
	if (*str)
		return 0;
	console_loglevel = 10;
	return 1;
}

static int __init quiet_kernel(char *str)
{
	if (*str)
		return 0;
	console_loglevel = 4;
	return 1;
}

__setup("debug", debug_kernel);
__setup("quiet", quiet_kernel);

static int __init loglevel(char *str)
{
	get_option(&str, &console_loglevel);
	return 1;
}

__setup("loglevel=", loglevel);

/*
 * Unknown boot options get handed to init, unless they look like
 * failed parameters
 */
static int __init unknown_bootoption(char *param, char *val)
{
	/* Change NUL term back to "=", to make "param" the whole string. */
	if (val) {
		/* param=val or param="val"? */
		if (val == param+strlen(param)+1)
			val[-1] = '=';
		else if (val == param+strlen(param)+2) {
			val[-2] = '=';
			memmove(val-1, val, strlen(val)+1);
			val--;
		} else
			BUG();
	}

	/* Handle obsolete-style parameters */
	if (obsolete_checksetup(param))
		return 0;

	/*
	 * Preemptive maintenance for "why didn't my misspelled command
	 * line work?"
	 */
	if (strchr(param, '.') && (!val || strchr(param, '.') < val)) {
		printk(KERN_ERR "Unknown boot option `%s': ignoring\n", param);
		return 0;
	}

	if (panic_later)
		return 0;

	if (val) {
		/* Environment option */
		unsigned int i;
		for (i = 0; envp_init[i]; i++) {
			if (i == MAX_INIT_ENVS) {
				panic_later = "Too many boot env vars at `%s'";
				panic_param = param;
			}
			if (!strncmp(param, envp_init[i], val - param))
				break;
		}
		envp_init[i] = param;
	} else {
		/* Command line option */
		unsigned int i;
		for (i = 0; argv_init[i]; i++) {
			if (i == MAX_INIT_ARGS) {
				panic_later = "Too many boot init vars at `%s'";
				panic_param = param;
			}
		}
		argv_init[i] = param;
	}
	return 0;
}

#ifdef CONFIG_DEBUG_PAGEALLOC
int __read_mostly debug_pagealloc_enabled = 0;
#endif

static int __init init_setup(char *str)
{
	unsigned int i;

	execute_command = str;
	/*
	 * In case LILO is going to boot us with default command line,
	 * it prepends "auto" before the whole cmdline which makes
	 * the shell think it should execute a script with such name.
	 * So we ignore all arguments entered _before_ init=... [MJ]
	 */
	for (i = 1; i < MAX_INIT_ARGS; i++)
		argv_init[i] = NULL;
	return 1;
}
__setup("init=", init_setup);

static int __init rdinit_setup(char *str)
{
	unsigned int i;

	ramdisk_execute_command = str;
	/* See "auto" comment in init_setup */
	for (i = 1; i < MAX_INIT_ARGS; i++)
		argv_init[i] = NULL;
	return 1;
}
__setup("rdinit=", rdinit_setup);

#ifndef CONFIG_SMP

#ifdef CONFIG_X86_LOCAL_APIC
static void __init smp_init(void)
{
	APIC_init_uniprocessor();
}
#else
#define smp_init()	do { } while (0)
#endif

static inline void setup_per_cpu_areas(void) { }
static inline void smp_prepare_cpus(unsigned int maxcpus) { }

#else

#ifndef CONFIG_HAVE_SETUP_PER_CPU_AREA
unsigned long __per_cpu_offset[NR_CPUS] __read_mostly;

EXPORT_SYMBOL(__per_cpu_offset);

static void __init setup_per_cpu_areas(void)
{
	unsigned long size, i;
	char *ptr;
	unsigned long nr_possible_cpus = num_possible_cpus();

	/* Copy section for each CPU (we discard the original) */
	size = ALIGN(PERCPU_ENOUGH_ROOM, PAGE_SIZE);
	ptr = alloc_bootmem_pages(size * nr_possible_cpus);

	for_each_possible_cpu(i) {
		__per_cpu_offset[i] = ptr - __per_cpu_start;
		memcpy(ptr, __per_cpu_start, __per_cpu_end - __per_cpu_start);
		ptr += size;
	}
}
#endif /* CONFIG_HAVE_SETUP_PER_CPU_AREA */

/* Called by boot processor to activate the rest. */
static void __init smp_init(void)
{
	unsigned int cpu;

	/* FIXME: This should be done in userspace --RR */
	for_each_present_cpu(cpu) {
		if (num_online_cpus() >= setup_max_cpus)
			break;
		if (!cpu_online(cpu))
			cpu_up(cpu);
	}

	/* Any cleanup work */
	printk(KERN_INFO "Brought up %ld CPUs\n", (long)num_online_cpus());
	smp_cpus_done(setup_max_cpus);
}

#endif

/*
 * We need to store the untouched command line for future reference.
 * We also need to store the touched command line since the parameter
 * parsing is performed in place, and we should allow a component to
 * store reference of name/value for future reference.
 */
static void __init setup_command_line(char *command_line)
{
	saved_command_line = alloc_bootmem(strlen (boot_command_line)+1);
	static_command_line = alloc_bootmem(strlen (command_line)+1);
	strcpy (saved_command_line, boot_command_line);
	strcpy (static_command_line, command_line);
}

/*
 * We need to finalize in a non-__init function or else race conditions
 * between the root thread and the init thread may cause start_kernel to
 * be reaped by free_initmem before the root thread has proceeded to
 * cpu_idle.
 *
 * gcc-3.4 accidentally inlines this function, so use noinline.
 */

static void noinline __init_refok rest_init(void)
	__releases(kernel_lock)
{
	int pid;

	kernel_thread(kernel_init, NULL, CLONE_FS | CLONE_SIGHAND);
	numa_default_policy();
	pid = kernel_thread(kthreadd, NULL, CLONE_FS | CLONE_FILES);
	kthreadd_task = find_task_by_pid(pid);
	unlock_kernel();

	/*
	 * The boot idle thread must execute schedule()
	 * at least once to get things moving:
	 */
	init_idle_bootup_task(current);
	preempt_enable_no_resched();
	schedule();
	preempt_disable();

	/* Call into cpu_idle with preempt disabled */
	cpu_idle();
}

/* Check for early params. */
static int __init do_early_param(char *param, char *val)
{
	struct obs_kernel_param *p;

	for (p = __setup_start; p < __setup_end; p++) {
		if ((p->early && strcmp(param, p->str) == 0) ||
		    (strcmp(param, "console") == 0 &&
		     strcmp(p->str, "earlycon") == 0)
		) {
			if (p->setup_func(val) != 0)
				printk(KERN_WARNING
				       "Malformed early option '%s'\n", param);
		}
	}
	/* We accept everything at this stage. */
	return 0;
}

/* Arch code calls this early on, or if not, just before other parsing. */
void __init parse_early_param(void)
{
	static __initdata int done = 0;
	static __initdata char tmp_cmdline[COMMAND_LINE_SIZE];

	if (done)
		return;

	/* All fall through to do_early_param. */
	strlcpy(tmp_cmdline, boot_command_line, COMMAND_LINE_SIZE);
	parse_args("early options", tmp_cmdline, NULL, 0, do_early_param);
	done = 1;
}

/*
 *	Activate the first processor.
 */

static void __init boot_cpu_init(void)
{
	int cpu = smp_processor_id();
	/* Mark the boot cpu "present", "online" etc for SMP and UP case */
	cpu_set(cpu, cpu_online_map);
	cpu_set(cpu, cpu_present_map);
	cpu_set(cpu, cpu_possible_map);
}

void __init __attribute__((weak)) smp_setup_processor_id(void)
{
}

asmlinkage void __init start_kernel(void)
{
	char * command_line;
	extern struct kernel_param __start___param[], __stop___param[];

	smp_setup_processor_id();

	/*
	 * Need to run as early as possible, to initialize the
	 * lockdep hash:
	 */
	unwind_init();
	lockdep_init();
	cgroup_init_early();

	local_irq_disable();
	early_boot_irqs_off();
	early_init_irq_lock_class();

/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */
	lock_kernel();
	tick_init();
	boot_cpu_init();
	page_address_init();
	printk(KERN_NOTICE);
	printk(linux_banner);
	setup_arch(&command_line);
	setup_command_line(command_line);
	unwind_setup();
	setup_per_cpu_areas();
	smp_prepare_boot_cpu();	/* arch-specific boot-cpu hooks */

	/*
	 * Set up the scheduler prior starting any interrupts (such as the
	 * timer interrupt). Full topology setup happens at smp_init()
	 * time - but meanwhile we still have a functioning scheduler.
	 */
	sched_init();
	/*
	 * Disable preemption - early bootup scheduling is extremely
	 * fragile until we cpu_idle() for the first time.
	 */
	preempt_disable();
	build_all_zonelists();
	page_alloc_init();
	enable_debug_pagealloc();
	printk(KERN_NOTICE "Kernel command line: %s\n", boot_command_line);
	parse_early_param();
	parse_args("Booting kernel", static_command_line, __start___param,
		   __stop___param - __start___param,
		   &unknown_bootoption);
	if (!irqs_disabled()) {
		printk(KERN_WARNING "start_kernel(): bug: interrupts were "
				"enabled *very* early, fixing it\n");
		local_irq_disable();
	}
	sort_main_extable();
	trap_init();
	rcu_init();
	init_IRQ();
	pidhash_init();
	init_timers();
	hrtimers_init();
	softirq_init();
	timekeeping_init();
	time_init();
	profile_init();
	if (!irqs_disabled())
		printk("start_kernel(): bug: interrupts were enabled early\n");
	early_boot_irqs_on();
	local_irq_enable();

	/*
	 * HACK ALERT! This is early. We're enabling the console before
	 * we've done PCI setups etc, and console_init() must be aware of
	 * this. But we do want output early, in case something goes wrong.
	 */
	console_init();
	if (panic_later)
		panic(panic_later, panic_param);

	lockdep_info();

	/*
	 * Need to run this when irqs are enabled, because it wants
	 * to self-test [hard/soft]-irqs on/off lock inversion bugs
	 * too:
	 */
	locking_selftest();

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start && !initrd_below_start_ok &&
			initrd_start < min_low_pfn << PAGE_SHIFT) {
		printk(KERN_CRIT "initrd overwritten (0x%08lx < 0x%08lx) - "
		    "disabling it.\n",initrd_start,min_low_pfn << PAGE_SHIFT);
		initrd_start = 0;
	}
#endif
	vfs_caches_init_early();
	cpuset_init_early();
	mem_init();
	cpu_hotplug_init();
	kmem_cache_init();
	setup_per_cpu_pageset();
	numa_policy_init();
	if (late_time_init)
		late_time_init();
	calibrate_delay();
	pidmap_init();
	pgtable_cache_init();
	prio_tree_init();
	anon_vma_init();
#ifdef CONFIG_X86
	if (efi_enabled)
		efi_enter_virtual_mode();
#endif
	fork_init(num_physpages);
	proc_caches_init();
	buffer_init();
	unnamed_dev_init();
	key_init();
	security_init();
	vfs_caches_init(num_physpages);
	radix_tree_init();
	signals_init();
	/* rootfs populating might need page-writeback */
	page_writeback_init();
#ifdef CONFIG_PROC_FS
	proc_root_init();
#endif
	cgroup_init();
	cpuset_init();
	taskstats_init_early();
	delayacct_init();

	check_bugs();

	acpi_early_init(); /* before LAPIC and SMP init */

	/* Do the rest non-__init'ed, we're now alive */
	rest_init();
}

static int __initdata initcall_debug;

static int __init initcall_debug_setup(char *str)
{
	initcall_debug = 1;
	return 1;
}
__setup("initcall_debug", initcall_debug_setup);

extern initcall_t __initcall_start[], __initcall_end[];

static void __init do_initcalls(void)
{
	initcall_t *call;
	int count = preempt_count();

	for (call = __initcall_start; call < __initcall_end; call++) {
		ktime_t t0, t1, delta;
		char *msg = NULL;
		char msgbuf[40];
		int result;

		if (initcall_debug) {
			printk("Calling initcall 0x%p", *call);
			print_fn_descriptor_symbol(": %s()",
					(unsigned long) *call);
			printk("\n");
			t0 = ktime_get();
		}

		result = (*call)();

		if (initcall_debug) {
			t1 = ktime_get();
			delta = ktime_sub(t1, t0);

			printk("initcall 0x%p", *call);
			print_fn_descriptor_symbol(": %s()",
					(unsigned long) *call);
			printk(" returned %d.\n", result);

			printk("initcall 0x%p ran for %Ld msecs: ",
				*call, (unsigned long long)delta.tv64 >> 20);
			print_fn_descriptor_symbol("%s()\n",
				(unsigned long) *call);
		}

		if (result && result != -ENODEV && initcall_debug) {
			sprintf(msgbuf, "error code %d", result);
			msg = msgbuf;
		}
		if (preempt_count() != count) {
			msg = "preemption imbalance";
			preempt_count() = count;
		}
		if (irqs_disabled()) {
			msg = "disabled interrupts";
			local_irq_enable();
		}
		if (msg) {
			printk(KERN_WARNING "initcall at 0x%p", *call);
			print_fn_descriptor_symbol(": %s()",
					(unsigned long) *call);
			printk(": returned with %s\n", msg);
		}
	}

	/* Make sure there is no pending stuff from the initcall sequence */
	flush_scheduled_work();
}

/*
 * Ok, the machine is now initialized. None of the devices
 * have been touched yet, but the CPU subsystem is up and
 * running, and memory and process management works.
 *
 * Now we can finally start doing some real work..
 */
static void __init do_basic_setup(void)
{
	/* drivers will send hotplug events */
	init_workqueues();
	usermodehelper_init();
	driver_init();
	init_irq_proc();
	do_initcalls();
}

static int __initdata nosoftlockup;

static int __init nosoftlockup_setup(char *str)
{
	nosoftlockup = 1;
	return 1;
}
__setup("nosoftlockup", nosoftlockup_setup);

static void __init do_pre_smp_initcalls(void)
{
	extern int spawn_ksoftirqd(void);

	migration_init();
	spawn_ksoftirqd();
	if (!nosoftlockup)
		spawn_softlockup_task();
}

static void run_init_process(char *init_filename)
{
	argv_init[0] = init_filename;
	kernel_execve(init_filename, argv_init, envp_init);
}

/* This is a non __init function. Force it to be noinline otherwise gcc
 * makes it inline to init() and it becomes part of init.text section
 */
static int noinline init_post(void)
{
	free_initmem();
	unlock_kernel();
	mark_rodata_ro();
	system_state = SYSTEM_RUNNING;
	numa_default_policy();

	if (sys_open((const char __user *) "/dev/console", O_RDWR, 0) < 0)
		printk(KERN_WARNING "Warning: unable to open an initial console.\n");

	(void) sys_dup(0);
	(void) sys_dup(0);

	if (ramdisk_execute_command) {
		run_init_process(ramdisk_execute_command);
		printk(KERN_WARNING "Failed to execute %s\n",
				ramdisk_execute_command);
	}

	/*
	 * We try each of these until one succeeds.
	 *
	 * The Bourne shell can be used instead of init if we are
	 * trying to recover a really broken machine.
	 */
	if (execute_command) {
		run_init_process(execute_command);
		printk(KERN_WARNING "Failed to execute %s.  Attempting "
					"defaults...\n", execute_command);
	}
	run_init_process("/sbin/init");
	run_init_process("/etc/init");
	run_init_process("/bin/init");
	run_init_process("/bin/sh");

	panic("No init found.  Try passing init= option to kernel.");
}

static int __init kernel_init(void * unused)
{
	lock_kernel();
	/*
	 * init can run on any cpu.
	 */
	set_cpus_allowed(current, CPU_MASK_ALL);
	/*
	 * Tell the world that we're going to be the grim
	 * reaper of innocent orphaned children.
	 *
	 * We don't want people to have to make incorrect
	 * assumptions about where in the task array this
	 * can be found.
	 */
	init_pid_ns.child_reaper = current;

	__set_special_pids(1, 1);
	cad_pid = task_pid(current);

	smp_prepare_cpus(setup_max_cpus);

	do_pre_smp_initcalls();

	smp_init();
	sched_init_smp();

	cpuset_init_smp();

	do_basic_setup();

	/*
	 * check if there is an early userspace init.  If yes, let it do all
	 * the work
	 */

	if (!ramdisk_execute_command)
		ramdisk_execute_command = "/init";

	if (sys_access((const char __user *) ramdisk_execute_command, 0) != 0) {
		ramdisk_execute_command = NULL;
		prepare_namespace();
	}

	/*
	 * Ok, we have completed the initial bootup, and
	 * we're essentially up and running. Get rid of the
	 * initmem segments and start the user-mode stuff..
	 */
	init_post();
	return 0;
}
