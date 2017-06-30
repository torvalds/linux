/*
 * SO2 lab3 - task 7
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>

MODULE_DESCRIPTION("Test list processing");
MODULE_AUTHOR("SO2");
MODULE_LICENSE("GPL");

extern void task_info_add_for_current(void);
extern void task_info_remove_expired(void);
extern void task_info_print_list(const char *msg);

static int list_test_init(void)
{
	task_info_add_for_current();
	task_info_print_list("after new addition");

	return 0;
}

static void list_test_exit(void)
{
	task_info_remove_expired();
	task_info_print_list("after removing expired");
}

module_init(list_test_init);
module_exit(list_test_exit);
