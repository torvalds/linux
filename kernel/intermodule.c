/* Deprecated, do not use.  Moved from module.c to here. --RR */

/* Written by Keith Owens <kaos@ocs.com.au> Oct 2000 */
#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/slab.h>

/* inter_module functions are always available, even when the kernel is
 * compiled without modules.  Consumers of inter_module_xxx routines
 * will always work, even when both are built into the kernel, this
 * approach removes lots of #ifdefs in mainline code.
 */

static struct list_head ime_list = LIST_HEAD_INIT(ime_list);
static DEFINE_SPINLOCK(ime_lock);
static int kmalloc_failed;

struct inter_module_entry {
	struct list_head list;
	const char *im_name;
	struct module *owner;
	const void *userdata;
};

/**
 * inter_module_register - register a new set of inter module data.
 * @im_name: an arbitrary string to identify the data, must be unique
 * @owner: module that is registering the data, always use THIS_MODULE
 * @userdata: pointer to arbitrary userdata to be registered
 *
 * Description: Check that the im_name has not already been registered,
 * complain if it has.  For new data, add it to the inter_module_entry
 * list.
 */
void inter_module_register(const char *im_name, struct module *owner, const void *userdata)
{
	struct list_head *tmp;
	struct inter_module_entry *ime, *ime_new;

	if (!(ime_new = kzalloc(sizeof(*ime), GFP_KERNEL))) {
		/* Overloaded kernel, not fatal */
		printk(KERN_ERR
			"Aiee, inter_module_register: cannot kmalloc entry for '%s'\n",
			im_name);
		kmalloc_failed = 1;
		return;
	}
	ime_new->im_name = im_name;
	ime_new->owner = owner;
	ime_new->userdata = userdata;

	spin_lock(&ime_lock);
	list_for_each(tmp, &ime_list) {
		ime = list_entry(tmp, struct inter_module_entry, list);
		if (strcmp(ime->im_name, im_name) == 0) {
			spin_unlock(&ime_lock);
			kfree(ime_new);
			/* Program logic error, fatal */
			printk(KERN_ERR "inter_module_register: duplicate im_name '%s'", im_name);
			BUG();
		}
	}
	list_add(&(ime_new->list), &ime_list);
	spin_unlock(&ime_lock);
}

/**
 * inter_module_unregister - unregister a set of inter module data.
 * @im_name: an arbitrary string to identify the data, must be unique
 *
 * Description: Check that the im_name has been registered, complain if
 * it has not.  For existing data, remove it from the
 * inter_module_entry list.
 */
void inter_module_unregister(const char *im_name)
{
	struct list_head *tmp;
	struct inter_module_entry *ime;

	spin_lock(&ime_lock);
	list_for_each(tmp, &ime_list) {
		ime = list_entry(tmp, struct inter_module_entry, list);
		if (strcmp(ime->im_name, im_name) == 0) {
			list_del(&(ime->list));
			spin_unlock(&ime_lock);
			kfree(ime);
			return;
		}
	}
	spin_unlock(&ime_lock);
	if (kmalloc_failed) {
		printk(KERN_ERR
			"inter_module_unregister: no entry for '%s', "
			"probably caused by previous kmalloc failure\n",
			im_name);
		return;
	}
	else {
		/* Program logic error, fatal */
		printk(KERN_ERR "inter_module_unregister: no entry for '%s'", im_name);
		BUG();
	}
}

/**
 * inter_module_get - return arbitrary userdata from another module.
 * @im_name: an arbitrary string to identify the data, must be unique
 *
 * Description: If the im_name has not been registered, return NULL.
 * Try to increment the use count on the owning module, if that fails
 * then return NULL.  Otherwise return the userdata.
 */
static const void *inter_module_get(const char *im_name)
{
	struct list_head *tmp;
	struct inter_module_entry *ime;
	const void *result = NULL;

	spin_lock(&ime_lock);
	list_for_each(tmp, &ime_list) {
		ime = list_entry(tmp, struct inter_module_entry, list);
		if (strcmp(ime->im_name, im_name) == 0) {
			if (try_module_get(ime->owner))
				result = ime->userdata;
			break;
		}
	}
	spin_unlock(&ime_lock);
	return(result);
}

/**
 * inter_module_get_request - im get with automatic request_module.
 * @im_name: an arbitrary string to identify the data, must be unique
 * @modname: module that is expected to register im_name
 *
 * Description: If inter_module_get fails, do request_module then retry.
 */
const void *inter_module_get_request(const char *im_name, const char *modname)
{
	const void *result = inter_module_get(im_name);
	if (!result) {
		request_module("%s", modname);
		result = inter_module_get(im_name);
	}
	return(result);
}

/**
 * inter_module_put - release use of data from another module.
 * @im_name: an arbitrary string to identify the data, must be unique
 *
 * Description: If the im_name has not been registered, complain,
 * otherwise decrement the use count on the owning module.
 */
void inter_module_put(const char *im_name)
{
	struct list_head *tmp;
	struct inter_module_entry *ime;

	spin_lock(&ime_lock);
	list_for_each(tmp, &ime_list) {
		ime = list_entry(tmp, struct inter_module_entry, list);
		if (strcmp(ime->im_name, im_name) == 0) {
			if (ime->owner)
				module_put(ime->owner);
			spin_unlock(&ime_lock);
			return;
		}
	}
	spin_unlock(&ime_lock);
	printk(KERN_ERR "inter_module_put: no entry for '%s'", im_name);
	BUG();
}

EXPORT_SYMBOL(inter_module_register);
EXPORT_SYMBOL(inter_module_unregister);
EXPORT_SYMBOL(inter_module_get_request);
EXPORT_SYMBOL(inter_module_put);
