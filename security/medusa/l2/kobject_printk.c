/* kobject_printk.c, (C) 2002 Milan Pikula */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/medusa/l3/registry.h>

struct printk_kobject {
	/* int loglevel; */ /* TODO: add loglevels some day */
	char message[512];
};

MED_ATTRS(printk_kobject) {
	MED_ATTR_KEY	(printk_kobject, message, "message", MED_STRING),
	MED_ATTR_END
};

static struct medusa_kobject_s * printk_fetch(struct medusa_kobject_s * key_obj)
{
	return NULL;
}
static medusa_answer_t printk_update(struct medusa_kobject_s * kobj)
{
	((struct printk_kobject *) kobj)->message[sizeof(((struct printk_kobject *) kobj)->message)-1] = '\0';
	MED_PRINTF("%s",((struct printk_kobject *) kobj)->message);
	return MED_OK;
}

MED_KCLASS(printk_kobject) {
	MEDUSA_KCLASS_HEADER(printk_kobject),
	"printk",
	NULL,		/* init kclass */
	NULL,		/* destroy kclass */
	printk_fetch,
	printk_update,
	NULL,		/* unmonitor */
};

#ifdef MODULE
static int printk_kobject_unload_check(void) __exit;
#endif

void printk_kobject_rmmod(void);

int __init printk_kobject_init(void) {
#ifdef MODULE
	THIS_MODULE->can_unload = printk_kobject_unload_check;
#endif
	MED_REGISTER_KCLASS(printk_kobject);
	return 0;
}

/* After this is called, and returns 0, printk_kobject_rmmod should be. */
static int __exit printk_kobject_unload_check(void)
{
	if (med_unlink_kclass(&MED_KCLASSOF(printk_kobject)) != MED_OK)
		return -EBUSY;
	return 0;
}

void __exit printk_kobject_rmmod(void)
{
	med_unregister_kclass(&MED_KCLASSOF(printk_kobject));
}

module_init(printk_kobject_init);
module_exit(printk_kobject_rmmod);
MODULE_LICENSE("GPL");
