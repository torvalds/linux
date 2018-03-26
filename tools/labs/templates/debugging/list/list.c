#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/slab.h>

static char *op = "ubi";

module_param(op, charp, 0000);
MODULE_PARM_DESC(op, "List error type");

struct list_m {
	int a;
	struct list_head list;
};

LIST_HEAD(head);

static noinline void use_before_init(void)
{
	struct list_m *m = kmalloc(sizeof(*m), GFP_KERNEL);
	
	pr_info("use before init\n");

	list_del(&m->list);
}

static noinline void use_after_free(void)
{
	struct list_m *m = kmalloc(sizeof(*m), GFP_KERNEL);
	
	pr_info("use after free\n");

	kfree(m);
	list_del(&m->list);
}

static noinline void crush(void)
{
	struct list_m e1, e2;
	struct list_head *i;

	e1.a = 2;
	e2.a = 3;

	list_add(&e1.list, &head);
	list_add(&e2.list, &head);

	list_for_each(i, &head) {
		struct list_m *x = list_entry(i, struct list_m, list);
		
		pr_info("list_for each %p\n", &x->a);
		list_del(&x->list);
	}
}

static int so2_list_init(void)
{
	pr_info("list_init with op %s\n", op);

	/* use before init */
	if (strncmp(op, "ubi", 3) == 0)
		use_before_init();
	if (strncmp(op, "uaf", 3) == 0)
		use_after_free();
	if (strncmp(op, "crush", 5) == 0)
		crush();

	return 0;
}

static void so2_list_exit(void)
{
}

MODULE_LICENSE("GPL v2");
module_init(so2_list_init);
module_exit(so2_list_exit);
