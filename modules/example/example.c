#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>

static int __init example_init(void)
{
    pr_info("example init\n");
    return 0;
}
module_init(example_init);

static void __exit example_exit(void)
{
    pr_info("example exit\n");
}
module_exit(example_exit);

MODULE_LICENSE("GPL");

