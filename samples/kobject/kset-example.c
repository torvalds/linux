/*
 * Sample kset and ktype implementation
 *
 * Copyright (C) 2004-2007 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2007 Novell Inc.
 *
 * Released under the GPL version 2 only.
 *
 */
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>

/*
 * This module shows how to create a kset in sysfs called
 * /sys/kernel/kset-example
 * Then tree kobjects are created and assigned to this kset, "foo", "baz",
 * and "bar".  In those kobjects, attributes of the same name are also
 * created and if an integer is written to these files, it can be later
 * read out of it.
 */


/*
 * This is our "object" that we will create a few of and register them with
 * sysfs.
 */
struct foo_obj {
	struct kobject kobj;
	int foo;
	int baz;
	int bar;
};
#define to_foo_obj(x) container_of(x, struct foo_obj, kobj)

/* a custom attribute that works just for a struct foo_obj. */
struct foo_attribute {
	struct attribute attr;
	ssize_t (*show)(struct foo_obj *foo, struct foo_attribute *attr, char *buf);
	ssize_t (*store)(struct foo_obj *foo, struct foo_attribute *attr, const char *buf, size_t count);
};
#define to_foo_attr(x) container_of(x, struct foo_attribute, attr)

/*
 * The default show function that must be passed to sysfs.  This will be
 * called by sysfs for whenever a show function is called by the user on a
 * sysfs file associated with the kobjects we have registered.  We need to
 * transpose back from a "default" kobject to our custom struct foo_obj and
 * then call the show function for that specific object.
 */
static ssize_t foo_attr_show(struct kobject *kobj,
			     struct attribute *attr,
			     char *buf)
{
	struct foo_attribute *attribute;
	struct foo_obj *foo;

	attribute = to_foo_attr(attr);
	foo = to_foo_obj(kobj);

	if (!attribute->show)
		return -EIO;

	return attribute->show(foo, attribute, buf);
}

/*
 * Just like the default show function above, but this one is for when the
 * sysfs "store" is requested (when a value is written to a file.)
 */
static ssize_t foo_attr_store(struct kobject *kobj,
			      struct attribute *attr,
			      const char *buf, size_t len)
{
	struct foo_attribute *attribute;
	struct foo_obj *foo;

	attribute = to_foo_attr(attr);
	foo = to_foo_obj(kobj);

	if (!attribute->store)
		return -EIO;

	return attribute->store(foo, attribute, buf, len);
}

/* Our custom sysfs_ops that we will associate with our ktype later on */
static const struct sysfs_ops foo_sysfs_ops = {
	.show = foo_attr_show,
	.store = foo_attr_store,
};

/*
 * The release function for our object.  This is REQUIRED by the kernel to
 * have.  We free the memory held in our object here.
 *
 * NEVER try to get away with just a "blank" release function to try to be
 * smarter than the kernel.  Turns out, no one ever is...
 */
static void foo_release(struct kobject *kobj)
{
	struct foo_obj *foo;

	foo = to_foo_obj(kobj);
	kfree(foo);
}

/*
 * The "foo" file where the .foo variable is read from and written to.
 */
static ssize_t foo_show(struct foo_obj *foo_obj, struct foo_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%d\n", foo_obj->foo);
}

static ssize_t foo_store(struct foo_obj *foo_obj, struct foo_attribute *attr,
			 const char *buf, size_t count)
{
	sscanf(buf, "%du", &foo_obj->foo);
	return count;
}

static struct foo_attribute foo_attribute =
	__ATTR(foo, 0666, foo_show, foo_store);

/*
 * More complex function where we determine which variable is being accessed by
 * looking at the attribute for the "baz" and "bar" files.
 */
static ssize_t b_show(struct foo_obj *foo_obj, struct foo_attribute *attr,
		      char *buf)
{
	int var;

	if (strcmp(attr->attr.name, "baz") == 0)
		var = foo_obj->baz;
	else
		var = foo_obj->bar;
	return sprintf(buf, "%d\n", var);
}

static ssize_t b_store(struct foo_obj *foo_obj, struct foo_attribute *attr,
		       const char *buf, size_t count)
{
	int var;

	sscanf(buf, "%du", &var);
	if (strcmp(attr->attr.name, "baz") == 0)
		foo_obj->baz = var;
	else
		foo_obj->bar = var;
	return count;
}

static struct foo_attribute baz_attribute =
	__ATTR(baz, 0666, b_show, b_store);
static struct foo_attribute bar_attribute =
	__ATTR(bar, 0666, b_show, b_store);

/*
 * Create a group of attributes so that we can create and destroy them all
 * at once.
 */
static struct attribute *foo_default_attrs[] = {
	&foo_attribute.attr,
	&baz_attribute.attr,
	&bar_attribute.attr,
	NULL,	/* need to NULL terminate the list of attributes */
};

/*
 * Our own ktype for our kobjects.  Here we specify our sysfs ops, the
 * release function, and the set of default attributes we want created
 * whenever a kobject of this type is registered with the kernel.
 */
static struct kobj_type foo_ktype = {
	.sysfs_ops = &foo_sysfs_ops,
	.release = foo_release,
	.default_attrs = foo_default_attrs,
};

static struct kset *example_kset;
static struct foo_obj *foo_obj;
static struct foo_obj *bar_obj;
static struct foo_obj *baz_obj;

static struct foo_obj *create_foo_obj(const char *name)
{
	struct foo_obj *foo;
	int retval;

	/* allocate the memory for the whole object */
	foo = kzalloc(sizeof(*foo), GFP_KERNEL);
	if (!foo)
		return NULL;

	/*
	 * As we have a kset for this kobject, we need to set it before calling
	 * the kobject core.
	 */
	foo->kobj.kset = example_kset;

	/*
	 * Initialize and add the kobject to the kernel.  All the default files
	 * will be created here.  As we have already specified a kset for this
	 * kobject, we don't have to set a parent for the kobject, the kobject
	 * will be placed beneath that kset automatically.
	 */
	retval = kobject_init_and_add(&foo->kobj, &foo_ktype, NULL, "%s", name);
	if (retval) {
		kobject_put(&foo->kobj);
		return NULL;
	}

	/*
	 * We are always responsible for sending the uevent that the kobject
	 * was added to the system.
	 */
	kobject_uevent(&foo->kobj, KOBJ_ADD);

	return foo;
}

static void destroy_foo_obj(struct foo_obj *foo)
{
	kobject_put(&foo->kobj);
}

static int __init example_init(void)
{
	/*
	 * Create a kset with the name of "kset_example",
	 * located under /sys/kernel/
	 */
	example_kset = kset_create_and_add("kset_example", NULL, kernel_kobj);
	if (!example_kset)
		return -ENOMEM;

	/*
	 * Create three objects and register them with our kset
	 */
	foo_obj = create_foo_obj("foo");
	if (!foo_obj)
		goto foo_error;

	bar_obj = create_foo_obj("bar");
	if (!bar_obj)
		goto bar_error;

	baz_obj = create_foo_obj("baz");
	if (!baz_obj)
		goto baz_error;

	return 0;

baz_error:
	destroy_foo_obj(bar_obj);
bar_error:
	destroy_foo_obj(foo_obj);
foo_error:
	kset_unregister(example_kset);
	return -EINVAL;
}

static void __exit example_exit(void)
{
	destroy_foo_obj(baz_obj);
	destroy_foo_obj(bar_obj);
	destroy_foo_obj(foo_obj);
	kset_unregister(example_kset);
}

module_init(example_init);
module_exit(example_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Greg Kroah-Hartman <greg@kroah.com>");
