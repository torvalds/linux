/*
 * Created by: Jason Wessel <jason.wessel@windriver.com>
 *
 * Copyright (c) 2010 Wind River Systems, Inc.  All Rights Reserved.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/kdb.h>

/*
 * All kdb shell command call backs receive argc and argv, where
 * argv[0] is the command the end user typed
 */
static int kdb_hello_cmd(int argc, const char **argv)
{
	if (argc > 1)
		return KDB_ARGCOUNT;

	if (argc)
		kdb_printf("Hello %s.\n", argv[1]);
	else
		kdb_printf("Hello world!\n");

	return 0;
}

static kdbtab_t hello_cmd = {
	.name = "hello",
	.func = kdb_hello_cmd,
	.usage = "[string]",
	.help = "Say Hello World or Hello [string]",
};

static int __init kdb_hello_cmd_init(void)
{
	/*
	 * Registration of a dynamically added kdb command is done with
	 * kdb_register().
	 */
	kdb_register(&hello_cmd);
	return 0;
}

static void __exit kdb_hello_cmd_exit(void)
{
	kdb_unregister(&hello_cmd);
}

module_init(kdb_hello_cmd_init);
module_exit(kdb_hello_cmd_exit);

MODULE_AUTHOR("WindRiver");
MODULE_DESCRIPTION("KDB example to add a hello command");
MODULE_LICENSE("GPL");
