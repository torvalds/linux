/*
 * Kernel Debugger Architecture Independent Breakpoint Handler
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2004 Silicon Graphics, Inc.  All Rights Reserved.
 * Copyright (c) 2009 Wind River Systems, Inc.  All Rights Reserved.
 */

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kdb.h>
#include <linux/kgdb.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include "kdb_private.h"

/*
 * Table of kdb_breakpoints
 */
kdb_bp_t kdb_breakpoints[KDB_MAXBPT];

static void kdb_setsinglestep(struct pt_regs *regs)
{
	KDB_STATE_SET(DOING_SS);
}

static char *kdb_rwtypes[] = {
	"Instruction(i)",
	"Instruction(Register)",
	"Data Write",
	"I/O",
	"Data Access"
};

static char *kdb_bptype(kdb_bp_t *bp)
{
	if (bp->bp_type < 0 || bp->bp_type > 4)
		return "";

	return kdb_rwtypes[bp->bp_type];
}

static int kdb_parsebp(int argc, const char **argv, int *nextargp, kdb_bp_t *bp)
{
	int nextarg = *nextargp;
	int diag;

	bp->bph_length = 1;
	if ((argc + 1) != nextarg) {
		if (strnicmp(argv[nextarg], "datar", sizeof("datar")) == 0)
			bp->bp_type = BP_ACCESS_WATCHPOINT;
		else if (strnicmp(argv[nextarg], "dataw", sizeof("dataw")) == 0)
			bp->bp_type = BP_WRITE_WATCHPOINT;
		else if (strnicmp(argv[nextarg], "inst", sizeof("inst")) == 0)
			bp->bp_type = BP_HARDWARE_BREAKPOINT;
		else
			return KDB_ARGCOUNT;

		bp->bph_length = 1;

		nextarg++;

		if ((argc + 1) != nextarg) {
			unsigned long len;

			diag = kdbgetularg((char *)argv[nextarg],
					   &len);
			if (diag)
				return diag;


			if (len > 8)
				return KDB_BADLENGTH;

			bp->bph_length = len;
			nextarg++;
		}

		if ((argc + 1) != nextarg)
			return KDB_ARGCOUNT;
	}

	*nextargp = nextarg;
	return 0;
}

static int _kdb_bp_remove(kdb_bp_t *bp)
{
	int ret = 1;
	if (!bp->bp_installed)
		return ret;
	if (!bp->bp_type)
		ret = dbg_remove_sw_break(bp->bp_addr);
	else
		ret = arch_kgdb_ops.remove_hw_breakpoint(bp->bp_addr,
			 bp->bph_length,
			 bp->bp_type);
	if (ret == 0)
		bp->bp_installed = 0;
	return ret;
}

static void kdb_handle_bp(struct pt_regs *regs, kdb_bp_t *bp)
{
	if (KDB_DEBUG(BP))
		kdb_printf("regs->ip = 0x%lx\n", instruction_pointer(regs));

	/*
	 * Setup single step
	 */
	kdb_setsinglestep(regs);

	/*
	 * Reset delay attribute
	 */
	bp->bp_delay = 0;
	bp->bp_delayed = 1;
}

static int _kdb_bp_install(struct pt_regs *regs, kdb_bp_t *bp)
{
	int ret;
	/*
	 * Install the breakpoint, if it is not already installed.
	 */

	if (KDB_DEBUG(BP))
		kdb_printf("%s: bp_installed %d\n",
			   __func__, bp->bp_installed);
	if (!KDB_STATE(SSBPT))
		bp->bp_delay = 0;
	if (bp->bp_installed)
		return 1;
	if (bp->bp_delay || (bp->bp_delayed && KDB_STATE(DOING_SS))) {
		if (KDB_DEBUG(BP))
			kdb_printf("%s: delayed bp\n", __func__);
		kdb_handle_bp(regs, bp);
		return 0;
	}
	if (!bp->bp_type)
		ret = dbg_set_sw_break(bp->bp_addr);
	else
		ret = arch_kgdb_ops.set_hw_breakpoint(bp->bp_addr,
			 bp->bph_length,
			 bp->bp_type);
	if (ret == 0) {
		bp->bp_installed = 1;
	} else {
		kdb_printf("%s: failed to set breakpoint at 0x%lx\n",
			   __func__, bp->bp_addr);
#ifdef CONFIG_DEBUG_RODATA
		if (!bp->bp_type) {
			kdb_printf("Software breakpoints are unavailable.\n"
				   "  Change the kernel CONFIG_DEBUG_RODATA=n\n"
				   "  OR use hw breaks: help bph\n");
		}
#endif
		return 1;
	}
	return 0;
}

/*
 * kdb_bp_install
 *
 *	Install kdb_breakpoints prior to returning from the
 *	kernel debugger.  This allows the kdb_breakpoints to be set
 *	upon functions that are used internally by kdb, such as
 *	printk().  This function is only called once per kdb session.
 */
void kdb_bp_install(struct pt_regs *regs)
{
	int i;

	for (i = 0; i < KDB_MAXBPT; i++) {
		kdb_bp_t *bp = &kdb_breakpoints[i];

		if (KDB_DEBUG(BP)) {
			kdb_printf("%s: bp %d bp_enabled %d\n",
				   __func__, i, bp->bp_enabled);
		}
		if (bp->bp_enabled)
			_kdb_bp_install(regs, bp);
	}
}

/*
 * kdb_bp_remove
 *
 *	Remove kdb_breakpoints upon entry to the kernel debugger.
 *
 * Parameters:
 *	None.
 * Outputs:
 *	None.
 * Returns:
 *	None.
 * Locking:
 *	None.
 * Remarks:
 */
void kdb_bp_remove(void)
{
	int i;

	for (i = KDB_MAXBPT - 1; i >= 0; i--) {
		kdb_bp_t *bp = &kdb_breakpoints[i];

		if (KDB_DEBUG(BP)) {
			kdb_printf("%s: bp %d bp_enabled %d\n",
				   __func__, i, bp->bp_enabled);
		}
		if (bp->bp_enabled)
			_kdb_bp_remove(bp);
	}
}


/*
 * kdb_printbp
 *
 *	Internal function to format and print a breakpoint entry.
 *
 * Parameters:
 *	None.
 * Outputs:
 *	None.
 * Returns:
 *	None.
 * Locking:
 *	None.
 * Remarks:
 */

static void kdb_printbp(kdb_bp_t *bp, int i)
{
	kdb_printf("%s ", kdb_bptype(bp));
	kdb_printf("BP #%d at ", i);
	kdb_symbol_print(bp->bp_addr, NULL, KDB_SP_DEFAULT);

	if (bp->bp_enabled)
		kdb_printf("\n    is enabled");
	else
		kdb_printf("\n    is disabled");

	kdb_printf("\taddr at %016lx, hardtype=%d installed=%d\n",
		   bp->bp_addr, bp->bp_type, bp->bp_installed);

	kdb_printf("\n");
}

/*
 * kdb_bp
 *
 *	Handle the bp commands.
 *
 *	[bp|bph] <addr-expression> [DATAR|DATAW]
 *
 * Parameters:
 *	argc	Count of arguments in argv
 *	argv	Space delimited command line arguments
 * Outputs:
 *	None.
 * Returns:
 *	Zero for success, a kdb diagnostic if failure.
 * Locking:
 *	None.
 * Remarks:
 *
 *	bp	Set breakpoint on all cpus.  Only use hardware assist if need.
 *	bph	Set breakpoint on all cpus.  Force hardware register
 */

static int kdb_bp(int argc, const char **argv)
{
	int i, bpno;
	kdb_bp_t *bp, *bp_check;
	int diag;
	char *symname = NULL;
	long offset = 0ul;
	int nextarg;
	kdb_bp_t template = {0};

	if (argc == 0) {
		/*
		 * Display breakpoint table
		 */
		for (bpno = 0, bp = kdb_breakpoints; bpno < KDB_MAXBPT;
		     bpno++, bp++) {
			if (bp->bp_free)
				continue;
			kdb_printbp(bp, bpno);
		}

		return 0;
	}

	nextarg = 1;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &template.bp_addr,
			     &offset, &symname);
	if (diag)
		return diag;
	if (!template.bp_addr)
		return KDB_BADINT;

	/*
	 * Find an empty bp structure to allocate
	 */
	for (bpno = 0, bp = kdb_breakpoints; bpno < KDB_MAXBPT; bpno++, bp++) {
		if (bp->bp_free)
			break;
	}

	if (bpno == KDB_MAXBPT)
		return KDB_TOOMANYBPT;

	if (strcmp(argv[0], "bph") == 0) {
		template.bp_type = BP_HARDWARE_BREAKPOINT;
		diag = kdb_parsebp(argc, argv, &nextarg, &template);
		if (diag)
			return diag;
	} else {
		template.bp_type = BP_BREAKPOINT;
	}

	/*
	 * Check for clashing breakpoints.
	 *
	 * Note, in this design we can't have hardware breakpoints
	 * enabled for both read and write on the same address.
	 */
	for (i = 0, bp_check = kdb_breakpoints; i < KDB_MAXBPT;
	     i++, bp_check++) {
		if (!bp_check->bp_free &&
		    bp_check->bp_addr == template.bp_addr) {
			kdb_printf("You already have a breakpoint at "
				   kdb_bfd_vma_fmt0 "\n", template.bp_addr);
			return KDB_DUPBPT;
		}
	}

	template.bp_enabled = 1;

	/*
	 * Actually allocate the breakpoint found earlier
	 */
	*bp = template;
	bp->bp_free = 0;

	kdb_printbp(bp, bpno);

	return 0;
}

/*
 * kdb_bc
 *
 *	Handles the 'bc', 'be', and 'bd' commands
 *
 *	[bd|bc|be] <breakpoint-number>
 *	[bd|bc|be] *
 *
 * Parameters:
 *	argc	Count of arguments in argv
 *	argv	Space delimited command line arguments
 * Outputs:
 *	None.
 * Returns:
 *	Zero for success, a kdb diagnostic for failure
 * Locking:
 *	None.
 * Remarks:
 */
static int kdb_bc(int argc, const char **argv)
{
	unsigned long addr;
	kdb_bp_t *bp = NULL;
	int lowbp = KDB_MAXBPT;
	int highbp = 0;
	int done = 0;
	int i;
	int diag = 0;

	int cmd;			/* KDBCMD_B? */
#define KDBCMD_BC	0
#define KDBCMD_BE	1
#define KDBCMD_BD	2

	if (strcmp(argv[0], "be") == 0)
		cmd = KDBCMD_BE;
	else if (strcmp(argv[0], "bd") == 0)
		cmd = KDBCMD_BD;
	else
		cmd = KDBCMD_BC;

	if (argc != 1)
		return KDB_ARGCOUNT;

	if (strcmp(argv[1], "*") == 0) {
		lowbp = 0;
		highbp = KDB_MAXBPT;
	} else {
		diag = kdbgetularg(argv[1], &addr);
		if (diag)
			return diag;

		/*
		 * For addresses less than the maximum breakpoint number,
		 * assume that the breakpoint number is desired.
		 */
		if (addr < KDB_MAXBPT) {
			bp = &kdb_breakpoints[addr];
			lowbp = highbp = addr;
			highbp++;
		} else {
			for (i = 0, bp = kdb_breakpoints; i < KDB_MAXBPT;
			    i++, bp++) {
				if (bp->bp_addr == addr) {
					lowbp = highbp = i;
					highbp++;
					break;
				}
			}
		}
	}

	/*
	 * Now operate on the set of breakpoints matching the input
	 * criteria (either '*' for all, or an individual breakpoint).
	 */
	for (bp = &kdb_breakpoints[lowbp], i = lowbp;
	    i < highbp;
	    i++, bp++) {
		if (bp->bp_free)
			continue;

		done++;

		switch (cmd) {
		case KDBCMD_BC:
			bp->bp_enabled = 0;

			kdb_printf("Breakpoint %d at "
				   kdb_bfd_vma_fmt " cleared\n",
				   i, bp->bp_addr);

			bp->bp_addr = 0;
			bp->bp_free = 1;

			break;
		case KDBCMD_BE:
			bp->bp_enabled = 1;

			kdb_printf("Breakpoint %d at "
				   kdb_bfd_vma_fmt " enabled",
				   i, bp->bp_addr);

			kdb_printf("\n");
			break;
		case KDBCMD_BD:
			if (!bp->bp_enabled)
				break;

			bp->bp_enabled = 0;

			kdb_printf("Breakpoint %d at "
				   kdb_bfd_vma_fmt " disabled\n",
				   i, bp->bp_addr);

			break;
		}
		if (bp->bp_delay && (cmd == KDBCMD_BC || cmd == KDBCMD_BD)) {
			bp->bp_delay = 0;
			KDB_STATE_CLEAR(SSBPT);
		}
	}

	return (!done) ? KDB_BPTNOTFOUND : 0;
}

/*
 * kdb_ss
 *
 *	Process the 'ss' (Single Step) command.
 *
 *	ss
 *
 * Parameters:
 *	argc	Argument count
 *	argv	Argument vector
 * Outputs:
 *	None.
 * Returns:
 *	KDB_CMD_SS for success, a kdb error if failure.
 * Locking:
 *	None.
 * Remarks:
 *
 *	Set the arch specific option to trigger a debug trap after the next
 *	instruction.
 */

static int kdb_ss(int argc, const char **argv)
{
	if (argc != 0)
		return KDB_ARGCOUNT;
	/*
	 * Set trace flag and go.
	 */
	KDB_STATE_SET(DOING_SS);
	return KDB_CMD_SS;
}

/* Initialize the breakpoint table and register	breakpoint commands. */

void __init kdb_initbptab(void)
{
	int i;
	kdb_bp_t *bp;

	/*
	 * First time initialization.
	 */
	memset(&kdb_breakpoints, '\0', sizeof(kdb_breakpoints));

	for (i = 0, bp = kdb_breakpoints; i < KDB_MAXBPT; i++, bp++)
		bp->bp_free = 1;

	kdb_register_repeat("bp", kdb_bp, "[<vaddr>]",
		"Set/Display breakpoints", 0, KDB_REPEAT_NO_ARGS);
	kdb_register_repeat("bl", kdb_bp, "[<vaddr>]",
		"Display breakpoints", 0, KDB_REPEAT_NO_ARGS);
	if (arch_kgdb_ops.flags & KGDB_HW_BREAKPOINT)
		kdb_register_repeat("bph", kdb_bp, "[<vaddr>]",
		"[datar [length]|dataw [length]]   Set hw brk", 0, KDB_REPEAT_NO_ARGS);
	kdb_register_repeat("bc", kdb_bc, "<bpnum>",
		"Clear Breakpoint", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("be", kdb_bc, "<bpnum>",
		"Enable Breakpoint", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("bd", kdb_bc, "<bpnum>",
		"Disable Breakpoint", 0, KDB_REPEAT_NONE);

	kdb_register_repeat("ss", kdb_ss, "",
		"Single Step", 1, KDB_REPEAT_NO_ARGS);
	/*
	 * Architecture dependent initialization.
	 */
}
