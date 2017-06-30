#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "bex.h"

MODULE_AUTHOR ("Kernel Hacker");
MODULE_LICENSE ("GPL");
MODULE_DESCRIPTION ("BEX bus module");

static int bex_match(struct device *dev, struct device_driver *driver)
{
	/* TODO 5/5: implement the bus match function */
	struct bex_device *bex_dev = to_bex_device(dev);
	struct bex_driver *bex_drv = to_bex_driver(driver);

	if (!strcmp(bex_dev->type, bex_drv->type))
		return 1;

	return 0;
}

static int bex_probe(struct device *dev)
{
	struct bex_device *bex_dev = to_bex_device(dev);
	struct bex_driver *bex_drv = to_bex_driver(dev->driver);

	return bex_drv->probe(bex_dev);
}

static int bex_remove(struct device *dev)
{
	struct bex_device *bex_dev = to_bex_device(dev);
	struct bex_driver *bex_drv = to_bex_driver(dev->driver);

	bex_drv->remove(bex_dev);
	return 0;
}

static int bex_add_dev(const char *name, const char *type, int version);

/* TODO 3/14: implement write only add attribute */
static ssize_t
add_store(struct bus_type *bt, const char *buf, size_t count)
{
	char type[32], name[32];
	int version;
	int ret;

	ret = sscanf(buf, "%31s %31s %d", name, type, &version);
	if (ret != 3)
		return -EINVAL;

	return bex_add_dev(name, type, version) ? : count;
}
BUS_ATTR(add, S_IWUSR, NULL, add_store);

static int bex_del_dev(const char *name);

/* TODO 3/13: implement write only del attribute */
static ssize_t
del_store(struct bus_type *bt, const char *buf, size_t count)
{
	char name[32];
	int version;

	if (sscanf(buf, "%s", name) != 1)
		return -EINVAL;

	return bex_del_dev(name) ? 0 : count;

}
BUS_ATTR(del, S_IWUSR, NULL, del_store);

static struct attribute *bex_bus_attrs[] = {
	/* TODO 3/2: add del and add attributes */
	&bus_attr_add.attr,
	&bus_attr_del.attr,
	NULL
};
ATTRIBUTE_GROUPS(bex_bus);

struct bus_type bex_bus_type = {
	.name	= "bex",
	.match	= bex_match,
	.probe  = bex_probe,
	.remove  = bex_remove,
	/* TODO 3: add bus groups attributes */
	.bus_groups = bex_bus_groups,
};

/*TODO 2/8: add read-only device attribute to show the type */
static ssize_t
type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bex_device *bex_dev = to_bex_device(dev);

	return sprintf(buf, "%s\n", bex_dev->type);
}
DEVICE_ATTR_RO(type);

/*TODO 2/8: add read-only device attribute to show the version */
static ssize_t
version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bex_device *bex_dev = to_bex_device(dev);

	return sprintf(buf, "%d\n", bex_dev->version);
}
DEVICE_ATTR_RO(version);

static struct attribute *bex_dev_attrs[] = {
	/* TODO 2/2: add type and version attributes */
	&dev_attr_type.attr,
	&dev_attr_version.attr,
	NULL
};
ATTRIBUTE_GROUPS(bex_dev);

static int bex_dev_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	return add_uevent_var(env, "MODALIAS=bex:%s", dev_name(dev));
}

static void bex_dev_release(struct device *dev)
{
	struct bex_device *bex_dev = to_bex_device(dev);

	kfree(bex_dev->type);
	kfree(bex_dev);
}

struct device_type bex_device_type = {
	/* TODO 2: set the device groups attributes */
	.groups = bex_dev_groups,
	.uevent	= bex_dev_uevent,
	.release = bex_dev_release,
};

static int bex_add_dev(const char *name, const char *type, int version)
{
	struct bex_device *bex_dev;

	bex_dev = kzalloc(sizeof(*bex_dev), GFP_KERNEL);
	if (!bex_dev)
		return -ENOMEM;

	bex_dev->type = kstrdup(type, GFP_KERNEL);
	bex_dev->version = version;

	bex_dev->dev.bus = &bex_bus_type;
	bex_dev->dev.type = &bex_device_type;
	bex_dev->dev.parent = NULL;

	dev_set_name(&bex_dev->dev, "%s", name);

	return device_register(&bex_dev->dev);
}

static int bex_del_dev(const char *name)
{
	struct device *dev;

	dev = bus_find_device_by_name(&bex_bus_type, NULL, name);
	if (!dev)
		return -EINVAL;

	device_unregister(dev);
	put_device(dev);

	return 0;
}

int bex_register_driver(struct bex_driver *drv)
{
	int ret;

	drv->driver.bus = &bex_bus_type;
	ret = driver_register(&drv->driver);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL(bex_register_driver);

void bex_unregister_driver(struct bex_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL(bex_unregister_driver);

static int __init my_bus_init (void)
{
	int ret;

	/* TODO 1/5: register the bus driver */
	ret = bus_register(&bex_bus_type);
	if (ret < 0) {
		pr_err("Unable to register bus\n");
		return ret;
	}

	/* TODO 1: add a device */
	bex_add_dev("root", "none", 1);

	return 0;
}

static void my_bus_exit (void)
{
	/* TODO 1: unregister the bus driver */
	bus_unregister(&bex_bus_type);
}

module_init (my_bus_init);
module_exit (my_bus_exit);
