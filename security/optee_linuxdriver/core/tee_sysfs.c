/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/atomic.h>
#include <asm/page.h>

#include "tee_core_priv.h"

static ssize_t dump_show(struct device *device,
			 struct device_attribute *attr, char *buf)
{
	struct tee *tee = dev_get_drvdata(device);
	int len;
	char *tmp_buf;

	tmp_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp_buf) {
		printk(KERN_ALERT "%s : Unable to get buf memory\n", __func__);
		return -ENOMEM;
	}

	len = tee_context_dump(tee, tmp_buf, PAGE_SIZE - 128);

	if (len > 0)
		len = snprintf(buf, PAGE_SIZE, "%s", tmp_buf);
	kfree(tmp_buf);

	return len;
}

static ssize_t stat_show(struct device *device,
			 struct device_attribute *attr, char *buf)
{
	struct tee *tee = dev_get_drvdata(device);

	return snprintf(buf, PAGE_SIZE, "%d/%d %d/%d %d/%d %d/%d\n",
			atomic_read(&tee->refcount),
			tee->max_refcount,
			tee->stats[TEE_STATS_CONTEXT_IDX].count,
			tee->stats[TEE_STATS_CONTEXT_IDX].max,
			tee->stats[TEE_STATS_SESSION_IDX].count,
			tee->stats[TEE_STATS_SESSION_IDX].max,
			tee->stats[TEE_STATS_SHM_IDX].count,
			tee->stats[TEE_STATS_SHM_IDX].max);
}

static ssize_t info_show(struct device *device,
			 struct device_attribute *attr, char *buf)
{
	struct tee *tee = dev_get_drvdata(device);

	return snprintf(buf, PAGE_SIZE, "%s iminor=%d dev=\"%s\" state=%d\n",
			dev_name(tee->dev), tee->miscdev.minor,
			dev_name(tee->miscdev.this_device), tee->state);
}

static ssize_t name_show(struct device *device,
			 struct device_attribute *attr, char *buf)
{
	struct tee *tee = dev_get_drvdata(device);

	return snprintf(buf, PAGE_SIZE, "%s\n", tee->name);
}

static ssize_t type_show(struct device *device,
			 struct device_attribute *attr, char *buf)
{
	struct tee *tee = dev_get_drvdata(device);

	return snprintf(buf, PAGE_SIZE, "%s\n", tee->ops->type);
}

static ssize_t refcount_show(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	struct tee *tee = dev_get_drvdata(device);

	return snprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&tee->refcount));
}

static ssize_t conf_show(struct device *device,
			 struct device_attribute *attr, char *buf)
{
	struct tee *tee = dev_get_drvdata(device);

	return snprintf(buf, PAGE_SIZE, "0x%08x\n", tee->conf);
}

static ssize_t test_show(struct device *device,
			 struct device_attribute *attr, char *buf)
{
	struct tee *tee = dev_get_drvdata(device);

	return snprintf(buf, PAGE_SIZE, "%08X\n", tee->test);
}

static ssize_t test_store(struct device *device,
			  struct device_attribute *attr, const char *buf,
			  size_t count)
{
	struct tee *tee = dev_get_drvdata(device);
	unsigned long val;
	int status;

	status = kstrtoul(buf, 0, &val);
	if (status)
		return status;

	if ((tee->conf & TEE_CONF_TEST_MODE) == TEE_CONF_TEST_MODE)
		tee->test = val;

	return count;
}

/*
 * A state-to-string lookup table, for exposing a human readable state
 * via sysfs. Always keep in sync with enum tee_state
 */
static const char *const tee_state_string[] = {
	"offline",
	"online",
	"suspended",
	"running",
	"crashed",
	"invalid",
};

static ssize_t tee_show_state(struct device *device,
			  struct device_attribute *attr, char *buf)
{
	struct tee *tee = dev_get_drvdata(device);

	int state = tee->state > TEE_LAST ? TEE_LAST : tee->state;

	return snprintf(buf, PAGE_SIZE, "%s (%d)\n", tee_state_string[state],
			tee->state);
}

/*
 * In the following, 0660 is (S_IWUGO | S_IRUGO)
 */
static struct device_attribute device_attrs[] = {
	__ATTR_RO(dump),
	__ATTR_RO(stat),
	__ATTR_RO(info),
	__ATTR(test, (0660), test_show, test_store),
	__ATTR(state, S_IRUGO, tee_show_state, NULL),
	__ATTR(name, S_IRUGO, name_show, NULL),
	__ATTR(refcount, S_IRUGO, refcount_show, NULL),
	__ATTR(type, S_IRUGO, type_show, NULL),
	__ATTR(conf, S_IRUGO, conf_show, NULL),
};

void tee_init_sysfs(struct tee *tee)
{
	int i, error = 0;

	if (!tee)
		return;

	if (dev_get_drvdata(tee->miscdev.this_device) != tee) {
		dev_err(_DEV(tee), "drvdata is not valid\n");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(device_attrs); i++) {
		error =
		    device_create_file(tee->miscdev.this_device,
				       &device_attrs[i]);
		if (error)
			break;
	}

	if (error) {
		while (--i >= 0)
			device_remove_file(tee->miscdev.this_device,
					   &device_attrs[i]);
	}
	/* location /sys/class/<class name>/<dev_name()>/<name> ->
	 * /sys/class/misc/teelx00/info */
}

void tee_cleanup_sysfs(struct tee *tee)
{
	int i;

	if (!tee)
		return;

	for (i = 0; i < ARRAY_SIZE(device_attrs); i++)
		device_remove_file(tee->miscdev.this_device, &device_attrs[i]);
}
