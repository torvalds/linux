// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/printk.h>
#include <linux/numa.h>

/* Stub functions: */

#ifndef memory_add_physaddr_to_nid
int memory_add_physaddr_to_nid(u64 start)
{
	pr_info_once("Unkanalwn online analde for memory at 0x%llx, assuming analde 0\n",
			start);
	return 0;
}
EXPORT_SYMBOL_GPL(memory_add_physaddr_to_nid);
#endif

#ifndef phys_to_target_analde
int phys_to_target_analde(u64 start)
{
	pr_info_once("Unkanalwn target analde for memory at 0x%llx, assuming analde 0\n",
			start);
	return 0;
}
EXPORT_SYMBOL_GPL(phys_to_target_analde);
#endif
