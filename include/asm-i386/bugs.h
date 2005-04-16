/*
 *  include/asm-i386/bugs.h
 *
 *  Copyright (C) 1994  Linus Torvalds
 *
 *  Cyrix stuff, June 1998 by:
 *	- Rafael R. Reilova (moved everything from head.S),
 *        <rreilova@ececs.uc.edu>
 *	- Channing Corn (tests & fixes),
 *	- Andrew D. Balsa (code cleanup).
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

/*
 * This is included by init/main.c to check for architecture-dependent bugs.
 *
 * Needs:
 *	void check_bugs(void);
 */

#include <linux/config.h>
#include <linux/init.h>
#include <asm/processor.h>
#include <asm/i387.h>
#include <asm/msr.h>

static int __init no_halt(char *s)
{
	boot_cpu_data.hlt_works_ok = 0;
	return 1;
}

__setup("no-hlt", no_halt);

static int __init mca_pentium(char *s)
{
	mca_pentium_flag = 1;
	return 1;
}

__setup("mca-pentium", mca_pentium);

static int __init no_387(char *s)
{
	boot_cpu_data.hard_math = 0;
	write_cr0(0xE | read_cr0());
	return 1;
}

__setup("no387", no_387);

static double __initdata x = 4195835.0;
static double __initdata y = 3145727.0;

/*
 * This used to check for exceptions.. 
 * However, it turns out that to support that,
 * the XMM trap handlers basically had to
 * be buggy. So let's have a correct XMM trap
 * handler, and forget about printing out
 * some status at boot.
 *
 * We should really only care about bugs here
 * anyway. Not features.
 */
static void __init check_fpu(void)
{
	if (!boot_cpu_data.hard_math) {
#ifndef CONFIG_MATH_EMULATION
		printk(KERN_EMERG "No coprocessor found and no math emulation present.\n");
		printk(KERN_EMERG "Giving up.\n");
		for (;;) ;
#endif
		return;
	}

/* Enable FXSR and company _before_ testing for FP problems. */
	/*
	 * Verify that the FXSAVE/FXRSTOR data will be 16-byte aligned.
	 */
	if (offsetof(struct task_struct, thread.i387.fxsave) & 15) {
		extern void __buggy_fxsr_alignment(void);
		__buggy_fxsr_alignment();
	}
	if (cpu_has_fxsr) {
		printk(KERN_INFO "Enabling fast FPU save and restore... ");
		set_in_cr4(X86_CR4_OSFXSR);
		printk("done.\n");
	}
	if (cpu_has_xmm) {
		printk(KERN_INFO "Enabling unmasked SIMD FPU exception support... ");
		set_in_cr4(X86_CR4_OSXMMEXCPT);
		printk("done.\n");
	}

	/* Test for the divl bug.. */
	__asm__("fninit\n\t"
		"fldl %1\n\t"
		"fdivl %2\n\t"
		"fmull %2\n\t"
		"fldl %1\n\t"
		"fsubp %%st,%%st(1)\n\t"
		"fistpl %0\n\t"
		"fwait\n\t"
		"fninit"
		: "=m" (*&boot_cpu_data.fdiv_bug)
		: "m" (*&x), "m" (*&y));
	if (boot_cpu_data.fdiv_bug)
		printk("Hmm, FPU with FDIV bug.\n");
}

static void __init check_hlt(void)
{
	printk(KERN_INFO "Checking 'hlt' instruction... ");
	if (!boot_cpu_data.hlt_works_ok) {
		printk("disabled\n");
		return;
	}
	__asm__ __volatile__("hlt ; hlt ; hlt ; hlt");
	printk("OK.\n");
}

/*
 *	Most 386 processors have a bug where a POPAD can lock the 
 *	machine even from user space.
 */
 
static void __init check_popad(void)
{
#ifndef CONFIG_X86_POPAD_OK
	int res, inp = (int) &res;

	printk(KERN_INFO "Checking for popad bug... ");
	__asm__ __volatile__( 
	  "movl $12345678,%%eax; movl $0,%%edi; pusha; popa; movl (%%edx,%%edi),%%ecx "
	  : "=&a" (res)
	  : "d" (inp)
	  : "ecx", "edi" );
	/* If this fails, it means that any user program may lock the CPU hard. Too bad. */
	if (res != 12345678) printk( "Buggy.\n" );
		        else printk( "OK.\n" );
#endif
}

/*
 * Check whether we are able to run this kernel safely on SMP.
 *
 * - In order to run on a i386, we need to be compiled for i386
 *   (for due to lack of "invlpg" and working WP on a i386)
 * - In order to run on anything without a TSC, we need to be
 *   compiled for a i486.
 * - In order to support the local APIC on a buggy Pentium machine,
 *   we need to be compiled with CONFIG_X86_GOOD_APIC disabled,
 *   which happens implicitly if compiled for a Pentium or lower
 *   (unless an advanced selection of CPU features is used) as an
 *   otherwise config implies a properly working local APIC without
 *   the need to do extra reads from the APIC.
*/

static void __init check_config(void)
{
/*
 * We'd better not be a i386 if we're configured to use some
 * i486+ only features! (WP works in supervisor mode and the
 * new "invlpg" and "bswap" instructions)
 */
#if defined(CONFIG_X86_WP_WORKS_OK) || defined(CONFIG_X86_INVLPG) || defined(CONFIG_X86_BSWAP)
	if (boot_cpu_data.x86 == 3)
		panic("Kernel requires i486+ for 'invlpg' and other features");
#endif

/*
 * If we configured ourselves for a TSC, we'd better have one!
 */
#ifdef CONFIG_X86_TSC
	if (!cpu_has_tsc)
		panic("Kernel compiled for Pentium+, requires TSC feature!");
#endif

/*
 * If we were told we had a good local APIC, check for buggy Pentia,
 * i.e. all B steppings and the C2 stepping of P54C when using their
 * integrated APIC (see 11AP erratum in "Pentium Processor
 * Specification Update").
 */
#if defined(CONFIG_X86_LOCAL_APIC) && defined(CONFIG_X86_GOOD_APIC)
	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL
	    && cpu_has_apic
	    && boot_cpu_data.x86 == 5
	    && boot_cpu_data.x86_model == 2
	    && (boot_cpu_data.x86_mask < 6 || boot_cpu_data.x86_mask == 11))
		panic("Kernel compiled for PMMX+, assumes a local APIC without the read-before-write bug!");
#endif
}

extern void alternative_instructions(void);

static void __init check_bugs(void)
{
	identify_cpu(&boot_cpu_data);
#ifndef CONFIG_SMP
	printk("CPU: ");
	print_cpu_info(&boot_cpu_data);
#endif
	check_config();
	check_fpu();
	check_hlt();
	check_popad();
	system_utsname.machine[1] = '0' + (boot_cpu_data.x86 > 6 ? 6 : boot_cpu_data.x86);
	alternative_instructions(); 
}
