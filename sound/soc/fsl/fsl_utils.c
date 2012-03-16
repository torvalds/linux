/**
 * Freescale ALSA SoC Machine driver utility
 *
 * Author: Timur Tabi <timur@freescale.com>
 *
 * Copyright 2010 Freescale Semiconductor, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_i2c.h>
#include <sound/soc.h>

#include "fsl_utils.h"

/**
 * fsl_asoc_get_codec_dev_name - determine the dev_name for a codec node
 *
 * @np: pointer to the I2C device tree node
 * @buf: buffer to be filled with the dev_name of the I2C device
 * @len: the length of the buffer
 *
 * This function determines the dev_name for an I2C node.  This is the name
 * that would be returned by dev_name() if this device_node were part of a
 * 'struct device'  It's ugly and hackish, but it works.
 *
 * The dev_name for such devices include the bus number and I2C address. For
 * example, "cs4270.0-004f".
 */
int fsl_asoc_get_codec_dev_name(struct device_node *np, char *buf, size_t len)
{
	const u32 *iprop;
	u32 addr;
	char temp[DAI_NAME_SIZE];
	struct i2c_client *i2c;

	of_modalias_node(np, temp, DAI_NAME_SIZE);

	iprop = of_get_property(np, "reg", NULL);
	if (!iprop)
		return -EINVAL;

	addr = be32_to_cpup(iprop);

	/* We need the adapter number */
	i2c = of_find_i2c_device_by_node(np);
	if (!i2c) {
		put_device(&i2c->dev);
		return -ENODEV;
	}

	snprintf(buf, len, "%s.%u-%04x", temp, i2c->adapter->nr, addr);
	put_device(&i2c->dev);

	return 0;
}
EXPORT_SYMBOL(fsl_asoc_get_codec_dev_name);

/**
 * fsl_asoc_get_dma_channel - determine the dma channel for a SSI node
 *
 * @ssi_np: pointer to the SSI device tree node
 * @name: name of the phandle pointing to the dma channel
 * @dai: ASoC DAI link pointer to be filled with platform_name
 * @dma_channel_id: dma channel id to be returned
 * @dma_id: dma id to be returned
 *
 * This function determines the dma and channel id for given SSI node.  It
 * also discovers the platform_name for the ASoC DAI link.
 */
int fsl_asoc_get_dma_channel(struct device_node *ssi_np,
			     const char *name,
			     struct snd_soc_dai_link *dai,
			     unsigned int *dma_channel_id,
			     unsigned int *dma_id)
{
	struct resource res;
	struct device_node *dma_channel_np, *dma_np;
	const u32 *iprop;
	int ret;

	dma_channel_np = of_parse_phandle(ssi_np, name, 0);
	if (!dma_channel_np)
		return -EINVAL;

	if (!of_device_is_compatible(dma_channel_np, "fsl,ssi-dma-channel")) {
		of_node_put(dma_channel_np);
		return -EINVAL;
	}

	/* Determine the dev_name for the device_node.  This code mimics the
	 * behavior of of_device_make_bus_id(). We need this because ASoC uses
	 * the dev_name() of the device to match the platform (DMA) device with
	 * the CPU (SSI) device.  It's all ugly and hackish, but it works (for
	 * now).
	 *
	 * dai->platform name should already point to an allocated buffer.
	 */
	ret = of_address_to_resource(dma_channel_np, 0, &res);
	if (ret) {
		of_node_put(dma_channel_np);
		return ret;
	}
	snprintf((char *)dai->platform_name, DAI_NAME_SIZE, "%llx.%s",
		 (unsigned long long) res.start, dma_channel_np->name);

	iprop = of_get_property(dma_channel_np, "cell-index", NULL);
	if (!iprop) {
		of_node_put(dma_channel_np);
		return -EINVAL;
	}
	*dma_channel_id = be32_to_cpup(iprop);

	dma_np = of_get_parent(dma_channel_np);
	iprop = of_get_property(dma_np, "cell-index", NULL);
	if (!iprop) {
		of_node_put(dma_np);
		return -EINVAL;
	}
	*dma_id = be32_to_cpup(iprop);

	of_node_put(dma_np);
	of_node_put(dma_channel_np);

	return 0;
}
EXPORT_SYMBOL(fsl_asoc_get_dma_channel);

MODULE_AUTHOR("Timur Tabi <timur@freescale.com>");
MODULE_DESCRIPTION("Freescale ASoC utility code");
MODULE_LICENSE("GPL v2");
