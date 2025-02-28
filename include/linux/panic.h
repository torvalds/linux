/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PANIC_H
#define _LINUX_PANIC_H

#include <linux/compiler_attributes.h>
#include <linux/types.h>

struct pt_regs;

extern long (*panic_blink)(int state);
__printf(1, 2)
void panic(const char *fmt, ...) __noreturn __cold;
void nmi_panic(struct pt_regs *regs, const char *msg);
void check_panic_on_warn(const char *origin);
extern void oops_enter(void);
extern void oops_exit(void);
extern bool oops_may_print(void);

extern bool panic_triggering_all_cpu_backtrace;
extern int panic_timeout;
extern unsigned long panic_print;
extern int panic_on_oops;
extern int panic_on_unrecovered_nmi;
extern int panic_on_io_nmi;
extern int panic_on_warn;

extern unsigned long panic_on_taint;
extern bool panic_on_taint_nousertaint;

extern int sysctl_panic_on_rcu_stall;
extern int sysctl_max_rcu_stall_to_panic;
extern int sysctl_panic_on_stackoverflow;

extern bool crash_kexec_post_notifiers;

extern void __stack_chk_fail(void);
void abort(void);

/*
 * panic_cpu is used for synchronizing panic() and crash_kexec() execution. It
 * holds a CPU number which is executing panic() currently. A value of
 * PANIC_CPU_INVALID means no CPU has entered panic() or crash_kexec().
 */
extern atomic_t panic_cpu;
#define PANIC_CPU_INVALID	-1

/*
 * Only to be used by arch init code. If the user over-wrote the default
 * CONFIG_PANIC_TIMEOUT, honor it.
 */
static inline void set_arch_panic_timeout(int timeout, int arch_default_timeout)
{
	if (panic_timeout == arch_default_timeout)
		panic_timeout = timeout;
}

/* This cannot be an enum because some may be used in assembly source. */
#define TAINT_PROPRIETARY_MODULE	0
#define TAINT_FORCED_MODULE		1
#define TAINT_CPU_OUT_OF_SPEC		2
#define TAINT_FORCED_RMMOD		3
#define TAINT_MACHINE_CHECK		4
#define TAINT_BAD_PAGE			5
#define TAINT_USER			6
#define TAINT_DIE			7
#define TAINT_OVERRIDDEN_ACPI_TABLE	8
#define TAINT_WARN			9
#define TAINT_CRAP			10
#define TAINT_FIRMWARE_WORKAROUND	11
#define TAINT_OOT_MODULE		12
#define TAINT_UNSIGNED_MODULE		13
#define TAINT_SOFTLOCKUP		14
#define TAINT_LIVEPATCH			15
#define TAINT_AUX			16
#define TAINT_RANDSTRUCT		17
#define TAINT_TEST			18
#define TAINT_FWCTL			19
#define TAINT_FLAGS_COUNT		20
#define TAINT_FLAGS_MAX			((1UL << TAINT_FLAGS_COUNT) - 1)

struct taint_flag {
	char c_true;		/* character printed when tainted */
	char c_false;		/* character printed when not tainted */
	bool module;		/* also show as a per-module taint flag */
	const char *desc;	/* verbose description of the set taint flag */
};

extern const struct taint_flag taint_flags[TAINT_FLAGS_COUNT];

enum lockdep_ok {
	LOCKDEP_STILL_OK,
	LOCKDEP_NOW_UNRELIABLE,
};

extern const char *print_tainted(void);
extern const char *print_tainted_verbose(void);
extern void add_taint(unsigned flag, enum lockdep_ok);
extern int test_taint(unsigned flag);
extern unsigned long get_taint(void);

#endif	/* _LINUX_PANIC_H */
