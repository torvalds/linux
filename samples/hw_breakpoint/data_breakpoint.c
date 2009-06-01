/*
 * data_breakpoint.c - Sample HW Breakpoint file to watch kernel data address
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * usage: insmod data_breakpoint.ko ksym=<ksym_name>
 *
 * This file is a kernel module that places a breakpoint over ksym_name kernel
 * variable using Hardware Breakpoint register. The corresponding handler which
 * prints a backtrace is invoked everytime a write operation is performed on
 * that variable.
 *
 * Copyright (C) IBM Corporation, 2009
 */
#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/init.h>		/* Needed for the macros */

#include <asm/hw_breakpoint.h>

struct hw_breakpoint sample_hbp;

static char ksym_name[KSYM_NAME_LEN] = "pid_max";
module_param_string(ksym, ksym_name, KSYM_NAME_LEN, S_IRUGO);
MODULE_PARM_DESC(ksym, "Kernel symbol to monitor; this module will report any"
			" write operations on the kernel symbol");

void sample_hbp_handler(struct hw_breakpoint *temp, struct pt_regs
								*temp_regs)
{
	printk(KERN_INFO "%s value is changed\n", ksym_name);
	dump_stack();
	printk(KERN_INFO "Dump stack from sample_hbp_handler\n");
}

static int __init hw_break_module_init(void)
{
	int ret;

#ifdef CONFIG_X86
	sample_hbp.info.name = ksym_name;
	sample_hbp.info.type = HW_BREAKPOINT_WRITE;
	sample_hbp.info.len = HW_BREAKPOINT_LEN_4;
#endif /* CONFIG_X86 */

	sample_hbp.triggered = (void *)sample_hbp_handler;

	ret = register_kernel_hw_breakpoint(&sample_hbp);

	if (ret < 0) {
		printk(KERN_INFO "Breakpoint registration failed\n");
		return ret;
	} else
		printk(KERN_INFO "HW Breakpoint for %s write installed\n",
								ksym_name);

	return 0;
}

static void __exit hw_break_module_exit(void)
{
	unregister_kernel_hw_breakpoint(&sample_hbp);
	printk(KERN_INFO "HW Breakpoint for %s write uninstalled\n", ksym_name);
}

module_init(hw_break_module_init);
module_exit(hw_break_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("K.Prasad");
MODULE_DESCRIPTION("ksym breakpoint");
