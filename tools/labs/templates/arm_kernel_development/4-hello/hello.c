/*
 * ARM Kernel Development
 * 
 * hello.c - Simple hello world module
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>

MODULE_DESCRIPTION("Hello World");
MODULE_AUTHOR("SO2");
MODULE_LICENSE("GPL");

static int hello_world_init(void)
{
	pr_info("Hello ARM World!\n");

	return 0;
}

static void hello_world_exit(void)
{
	pr_info("Going out ARM world!\n");
}

module_init(hello_world_init);
module_exit(hello_world_exit);
