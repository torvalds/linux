// SPDX-License-Identifier: GPL-2.0-only
/*
 * Apple Onboard Audio driver core
 *
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/list.h>
#include "../aoa.h"
#include "alsa.h"

MODULE_DESCRIPTION("Apple Onboard Audio Sound Driver");
MODULE_AUTHOR("Johannes Berg <johannes@sipsolutions.net>");
MODULE_LICENSE("GPL");

/* We allow only one fabric. This simplifies things,
 * and more don't really make that much sense */
static struct aoa_fabric *fabric;
static LIST_HEAD(codec_list);

static int attach_codec_to_fabric(struct aoa_codec *c)
{
	int err;

	if (!try_module_get(c->owner))
		return -EBUSY;
	/* found_codec has to be assigned */
	err = -ENOENT;
	if (fabric->found_codec)
		err = fabric->found_codec(c);
	if (err) {
		module_put(c->owner);
		printk(KERN_ERR "snd-aoa: fabric didn't like codec %s\n",
				c->name);
		return err;
	}
	c->fabric = fabric;

	err = 0;
	if (c->init)
		err = c->init(c);
	if (err) {
		printk(KERN_ERR "snd-aoa: codec %s didn't init\n", c->name);
		c->fabric = NULL;
		if (fabric->remove_codec)
			fabric->remove_codec(c);
		module_put(c->owner);
		return err;
	}
	if (fabric->attached_codec)
		fabric->attached_codec(c);
	return 0;
}

int aoa_codec_register(struct aoa_codec *codec)
{
	int err = 0;

	/* if there's a fabric already, we can tell if we
	 * will want to have this codec, so propagate error
	 * through. Otherwise, this will happen later... */
	if (fabric)
		err = attach_codec_to_fabric(codec);
	if (!err)
		list_add(&codec->list, &codec_list);
	return err;
}
EXPORT_SYMBOL_GPL(aoa_codec_register);

void aoa_codec_unregister(struct aoa_codec *codec)
{
	list_del(&codec->list);
	if (codec->fabric && codec->exit)
		codec->exit(codec);
	if (fabric && fabric->remove_codec)
		fabric->remove_codec(codec);
	codec->fabric = NULL;
	module_put(codec->owner);
}
EXPORT_SYMBOL_GPL(aoa_codec_unregister);

int aoa_fabric_register(struct aoa_fabric *new_fabric, struct device *dev)
{
	struct aoa_codec *c;
	int err;

	/* allow querying for presence of fabric
	 * (i.e. do this test first!) */
	if (new_fabric == fabric) {
		err = -EALREADY;
		goto attach;
	}
	if (fabric)
		return -EEXIST;
	if (!new_fabric)
		return -EINVAL;

	err = aoa_alsa_init(new_fabric->name, new_fabric->owner, dev);
	if (err)
		return err;

	fabric = new_fabric;

 attach:
	list_for_each_entry(c, &codec_list, list) {
		if (c->fabric != fabric)
			attach_codec_to_fabric(c);
	}
	return err;
}
EXPORT_SYMBOL_GPL(aoa_fabric_register);

void aoa_fabric_unregister(struct aoa_fabric *old_fabric)
{
	struct aoa_codec *c;

	if (fabric != old_fabric)
		return;

	list_for_each_entry(c, &codec_list, list) {
		if (c->fabric)
			aoa_fabric_unlink_codec(c);
	}

	aoa_alsa_cleanup();

	fabric = NULL;
}
EXPORT_SYMBOL_GPL(aoa_fabric_unregister);

void aoa_fabric_unlink_codec(struct aoa_codec *codec)
{
	if (!codec->fabric) {
		printk(KERN_ERR "snd-aoa: fabric unassigned "
				"in aoa_fabric_unlink_codec\n");
		dump_stack();
		return;
	}
	if (codec->exit)
		codec->exit(codec);
	if (codec->fabric->remove_codec)
		codec->fabric->remove_codec(codec);
	codec->fabric = NULL;
	module_put(codec->owner);
}
EXPORT_SYMBOL_GPL(aoa_fabric_unlink_codec);

static int __init aoa_init(void)
{
	return 0;
}

static void __exit aoa_exit(void)
{
	aoa_alsa_cleanup();
}

module_init(aoa_init);
module_exit(aoa_exit);
