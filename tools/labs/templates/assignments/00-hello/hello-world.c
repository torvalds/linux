#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>

MODULE_DESCRIPTION("Hello World");
MODULE_AUTHOR("Psoru Lesfo Rever");
MODULE_LICENSE("GPL");


static int hello_init(void)
{
	/* TODO: Print "Hello, World!" */
	pr_info("Hello, World!\n");

	return 0;
}

static void hello_exit(void)
{
}

module_init(hello_init);
module_exit(hello_exit);
