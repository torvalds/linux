/*
 *  net/9p/9p.c
 *
 *  9P entry point
 *
 *  Copyright (C) 2007 by Latchesar Ionkov <lucho@ionkov.net>
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <net/9p/9p.h>

#ifdef CONFIG_NET_9P_DEBUG
unsigned int p9_debug_level = 0;	/* feature-rific global debug level  */
EXPORT_SYMBOL(p9_debug_level);
module_param_named(debug, p9_debug_level, uint, 0);
MODULE_PARM_DESC(debug, "9P debugging level");
#endif

extern int p9_mux_global_init(void);
extern void p9_mux_global_exit(void);
extern int p9_sysctl_register(void);
extern void p9_sysctl_unregister(void);

/**
 * v9fs_init - Initialize module
 *
 */
static int __init init_p9(void)
{
	int ret;

	p9_error_init();
	printk(KERN_INFO "Installing 9P2000 support\n");
	ret = p9_mux_global_init();
	if (ret) {
		printk(KERN_WARNING "9p: starting mux failed\n");
		return ret;
	}

	ret = p9_sysctl_register();
	if (ret) {
		printk(KERN_WARNING "9p: registering sysctl failed\n");
		return ret;
	}

	return ret;
}

/**
 * v9fs_init - shutdown module
 *
 */

static void __exit exit_p9(void)
{
	p9_sysctl_unregister();
	p9_mux_global_exit();
}

module_init(init_p9)
module_exit(exit_p9)

MODULE_AUTHOR("Latchesar Ionkov <lucho@ionkov.net>");
MODULE_AUTHOR("Eric Van Hensbergen <ericvh@gmail.com>");
MODULE_AUTHOR("Ron Minnich <rminnich@lanl.gov>");
MODULE_LICENSE("GPL");
