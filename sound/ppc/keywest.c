// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * common keywest i2c layer
 *
 * Copyright (c) by Takashi Iwai <tiwai@suse.de>
 */


#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <sound/core.h>
#include "pmac.h"

static struct pmac_keywest *keywest_ctx;
static bool keywest_probed;

static int keywest_probe(struct i2c_client *client)
{
	keywest_probed = true;
	/* If instantiated via i2c-powermac, we still need to set the client */
	if (!keywest_ctx->client)
		keywest_ctx->client = client;
	i2c_set_clientdata(client, keywest_ctx);
	return 0;
}

/*
 * This is kind of a hack, best would be to turn powermac to fixed i2c
 * bus numbers and declare the sound device as part of platform
 * initialization
 */
static int keywest_attach_adapter(struct i2c_adapter *adapter)
{
	struct i2c_board_info info;
	struct i2c_client *client;

	if (! keywest_ctx)
		return -EINVAL;

	if (strncmp(adapter->name, "mac-io", 6))
		return -EINVAL; /* ignored */

	memset(&info, 0, sizeof(struct i2c_board_info));
	strscpy(info.type, "keywest", I2C_NAME_SIZE);
	info.addr = keywest_ctx->addr;
	client = i2c_new_client_device(adapter, &info);
	if (IS_ERR(client))
		return PTR_ERR(client);
	keywest_ctx->client = client;

	/*
	 * We know the driver is already loaded, so the device should be
	 * already bound. If not it means binding failed, and then there
	 * is no point in keeping the device instantiated.
	 */
	if (!keywest_ctx->client->dev.driver) {
		i2c_unregister_device(keywest_ctx->client);
		keywest_ctx->client = NULL;
		return -ENODEV;
	}
	
	/*
	 * Let i2c-core delete that device on driver removal.
	 * This is safe because i2c-core holds the core_lock mutex for us.
	 */
	list_add_tail(&keywest_ctx->client->detected,
		      &to_i2c_driver(keywest_ctx->client->dev.driver)->clients);
	return 0;
}

static void keywest_remove(struct i2c_client *client)
{
	if (! keywest_ctx)
		return;
	if (client == keywest_ctx->client)
		keywest_ctx->client = NULL;
}


static const struct i2c_device_id keywest_i2c_id[] = {
	{ "MAC,tas3004" },	/* instantiated by i2c-powermac */
	{ "keywest" },		/* instantiated by us if needed */
	{ }
};
MODULE_DEVICE_TABLE(i2c, keywest_i2c_id);

static struct i2c_driver keywest_driver = {
	.driver = {
		.name = "PMac Keywest Audio",
	},
	.probe = keywest_probe,
	.remove = keywest_remove,
	.id_table = keywest_i2c_id,
};

/* exported */
void snd_pmac_keywest_cleanup(struct pmac_keywest *i2c)
{
	if (keywest_ctx && keywest_ctx == i2c) {
		i2c_del_driver(&keywest_driver);
		keywest_ctx = NULL;
	}
}

int snd_pmac_tumbler_post_init(void)
{
	int err;
	
	if (!keywest_ctx || !keywest_ctx->client)
		return -ENXIO;

	err = keywest_ctx->init_client(keywest_ctx);
	if (err < 0) {
		snd_printk(KERN_ERR "tumbler: %i :cannot initialize the MCS\n", err);
		return err;
	}
	return 0;
}

/* exported */
int snd_pmac_keywest_init(struct pmac_keywest *i2c)
{
	struct i2c_adapter *adap;
	int err, i = 0;

	if (keywest_ctx)
		return -EBUSY;

	adap = i2c_get_adapter(0);
	if (!adap)
		return -EPROBE_DEFER;

	keywest_ctx = i2c;

	err = i2c_add_driver(&keywest_driver);
	if (err) {
		snd_printk(KERN_ERR "cannot register keywest i2c driver\n");
		i2c_put_adapter(adap);
		return err;
	}

	/* There was already a device from i2c-powermac. Great, let's return */
	if (keywest_probed)
		return 0;

	/* We assume Macs have consecutive I2C bus numbers starting at 0 */
	while (adap) {
		/* Scan for devices to be bound to */
		err = keywest_attach_adapter(adap);
		if (!err)
			return 0;
		i2c_put_adapter(adap);
		adap = i2c_get_adapter(++i);
	}

	return -ENODEV;
}
