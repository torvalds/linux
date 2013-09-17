/*
 * i2sbus driver
 *
 * Copyright 2006-2008 Johannes Berg <johannes@sipsolutions.net>
 *
 * GPL v2, can be found in COPYING.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <sound/core.h>

#include <asm/macio.h>
#include <asm/dbdma.h>

#include "../soundbus.h"
#include "i2sbus.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Johannes Berg <johannes@sipsolutions.net>");
MODULE_DESCRIPTION("Apple Soundbus: I2S support");

static int force;
module_param(force, int, 0444);
MODULE_PARM_DESC(force, "Force loading i2sbus even when"
			" no layout-id property is present");

static struct of_device_id i2sbus_match[] = {
	{ .name = "i2s" },
	{ }
};

MODULE_DEVICE_TABLE(of, i2sbus_match);

static int alloc_dbdma_descriptor_ring(struct i2sbus_dev *i2sdev,
				       struct dbdma_command_mem *r,
				       int numcmds)
{
	/* one more for rounding, one for branch back, one for stop command */
	r->size = (numcmds + 3) * sizeof(struct dbdma_cmd);
	/* We use the PCI APIs for now until the generic one gets fixed
	 * enough or until we get some macio-specific versions
	 */
	r->space = dma_alloc_coherent(
			&macio_get_pci_dev(i2sdev->macio)->dev,
			r->size,
			&r->bus_addr,
			GFP_KERNEL);

	if (!r->space) return -ENOMEM;

	memset(r->space, 0, r->size);
	r->cmds = (void*)DBDMA_ALIGN(r->space);
	r->bus_cmd_start = r->bus_addr +
			   (dma_addr_t)((char*)r->cmds - (char*)r->space);

	return 0;
}

static void free_dbdma_descriptor_ring(struct i2sbus_dev *i2sdev,
				       struct dbdma_command_mem *r)
{
	if (!r->space) return;

	dma_free_coherent(&macio_get_pci_dev(i2sdev->macio)->dev,
			    r->size, r->space, r->bus_addr);
}

static void i2sbus_release_dev(struct device *dev)
{
	struct i2sbus_dev *i2sdev;
	int i;

	i2sdev = container_of(dev, struct i2sbus_dev, sound.ofdev.dev);

 	if (i2sdev->intfregs) iounmap(i2sdev->intfregs);
 	if (i2sdev->out.dbdma) iounmap(i2sdev->out.dbdma);
 	if (i2sdev->in.dbdma) iounmap(i2sdev->in.dbdma);
	for (i = aoa_resource_i2smmio; i <= aoa_resource_rxdbdma; i++)
		if (i2sdev->allocated_resource[i])
			release_and_free_resource(i2sdev->allocated_resource[i]);
	free_dbdma_descriptor_ring(i2sdev, &i2sdev->out.dbdma_ring);
	free_dbdma_descriptor_ring(i2sdev, &i2sdev->in.dbdma_ring);
	for (i = aoa_resource_i2smmio; i <= aoa_resource_rxdbdma; i++)
		free_irq(i2sdev->interrupts[i], i2sdev);
	i2sbus_control_remove_dev(i2sdev->control, i2sdev);
	mutex_destroy(&i2sdev->lock);
	kfree(i2sdev);
}

static irqreturn_t i2sbus_bus_intr(int irq, void *devid)
{
	struct i2sbus_dev *dev = devid;
	u32 intreg;

	spin_lock(&dev->low_lock);
	intreg = in_le32(&dev->intfregs->intr_ctl);

	/* acknowledge interrupt reasons */
	out_le32(&dev->intfregs->intr_ctl, intreg);

	spin_unlock(&dev->low_lock);

	return IRQ_HANDLED;
}


/*
 * XXX FIXME: We test the layout_id's here to get the proper way of
 * mapping in various registers, thanks to bugs in Apple device-trees.
 * We could instead key off the machine model and the name of the i2s
 * node (i2s-a). This we'll do when we move it all to macio_asic.c
 * and have that export items for each sub-node too.
 */
static int i2sbus_get_and_fixup_rsrc(struct device_node *np, int index,
				     int layout, struct resource *res)
{
	struct device_node *parent;
	int pindex, rc = -ENXIO;
	const u32 *reg;

	/* Machines with layout 76 and 36 (K2 based) have a weird device
	 * tree what we need to special case.
	 * Normal machines just fetch the resource from the i2s-X node.
	 * Darwin further divides normal machines into old and new layouts
	 * with a subtely different code path but that doesn't seem necessary
	 * in practice, they just bloated it. In addition, even on our K2
	 * case the i2s-modem node, if we ever want to handle it, uses the
	 * normal layout
	 */
	if (layout != 76 && layout != 36)
		return of_address_to_resource(np, index, res);

	parent = of_get_parent(np);
	pindex = (index == aoa_resource_i2smmio) ? 0 : 1;
	rc = of_address_to_resource(parent, pindex, res);
	if (rc)
		goto bail;
	reg = of_get_property(np, "reg", NULL);
	if (reg == NULL) {
		rc = -ENXIO;
		goto bail;
	}
	res->start += reg[index * 2];
	res->end = res->start + reg[index * 2 + 1] - 1;
 bail:
	of_node_put(parent);
	return rc;
}

/* FIXME: look at device node refcounting */
static int i2sbus_add_dev(struct macio_dev *macio,
			  struct i2sbus_control *control,
			  struct device_node *np)
{
	struct i2sbus_dev *dev;
	struct device_node *child = NULL, *sound = NULL;
	struct resource *r;
	int i, layout = 0, rlen, ok = force;
	static const char *rnames[] = { "i2sbus: %s (control)",
					"i2sbus: %s (tx)",
					"i2sbus: %s (rx)" };
	static irq_handler_t ints[] = {
		i2sbus_bus_intr,
		i2sbus_tx_intr,
		i2sbus_rx_intr
	};

	if (strlen(np->name) != 5)
		return 0;
	if (strncmp(np->name, "i2s-", 4))
		return 0;

	dev = kzalloc(sizeof(struct i2sbus_dev), GFP_KERNEL);
	if (!dev)
		return 0;

	i = 0;
	while ((child = of_get_next_child(np, child))) {
		if (strcmp(child->name, "sound") == 0) {
			i++;
			sound = child;
		}
	}
	if (i == 1) {
		const u32 *id = of_get_property(sound, "layout-id", NULL);

		if (id) {
			layout = *id;
			snprintf(dev->sound.modalias, 32,
				 "sound-layout-%d", layout);
			ok = 1;
		} else {
			id = of_get_property(sound, "device-id", NULL);
			/*
			 * We probably cannot handle all device-id machines,
			 * so restrict to those we do handle for now.
			 */
			if (id && (*id == 22 || *id == 14 || *id == 35 ||
				   *id == 44)) {
				snprintf(dev->sound.modalias, 32,
					 "aoa-device-id-%d", *id);
				ok = 1;
				layout = -1;
			}
		}
	}
	/* for the time being, until we can handle non-layout-id
	 * things in some fabric, refuse to attach if there is no
	 * layout-id property or we haven't been forced to attach.
	 * When there are two i2s busses and only one has a layout-id,
	 * then this depends on the order, but that isn't important
	 * either as the second one in that case is just a modem. */
	if (!ok) {
		kfree(dev);
		return -ENODEV;
	}

	mutex_init(&dev->lock);
	spin_lock_init(&dev->low_lock);
	dev->sound.ofdev.archdata.dma_mask = macio->ofdev.archdata.dma_mask;
	dev->sound.ofdev.dev.of_node = np;
	dev->sound.ofdev.dev.dma_mask = &dev->sound.ofdev.archdata.dma_mask;
	dev->sound.ofdev.dev.parent = &macio->ofdev.dev;
	dev->sound.ofdev.dev.release = i2sbus_release_dev;
	dev->sound.attach_codec = i2sbus_attach_codec;
	dev->sound.detach_codec = i2sbus_detach_codec;
	dev->sound.pcmid = -1;
	dev->macio = macio;
	dev->control = control;
	dev->bus_number = np->name[4] - 'a';
	INIT_LIST_HEAD(&dev->sound.codec_list);

	for (i = aoa_resource_i2smmio; i <= aoa_resource_rxdbdma; i++) {
		dev->interrupts[i] = -1;
		snprintf(dev->rnames[i], sizeof(dev->rnames[i]),
			 rnames[i], np->name);
	}
	for (i = aoa_resource_i2smmio; i <= aoa_resource_rxdbdma; i++) {
		int irq = irq_of_parse_and_map(np, i);
		if (request_irq(irq, ints[i], 0, dev->rnames[i], dev))
			goto err;
		dev->interrupts[i] = irq;
	}


	/* Resource handling is problematic as some device-trees contain
	 * useless crap (ugh ugh ugh). We work around that here by calling
	 * specific functions for calculating the appropriate resources.
	 *
	 * This will all be moved to macio_asic.c at one point
	 */
	for (i = aoa_resource_i2smmio; i <= aoa_resource_rxdbdma; i++) {
		if (i2sbus_get_and_fixup_rsrc(np,i,layout,&dev->resources[i]))
			goto err;
		/* If only we could use our resource dev->resources[i]...
		 * but request_resource doesn't know about parents and
		 * contained resources...
		 */
		dev->allocated_resource[i] =
			request_mem_region(dev->resources[i].start,
					   resource_size(&dev->resources[i]),
					   dev->rnames[i]);
		if (!dev->allocated_resource[i]) {
			printk(KERN_ERR "i2sbus: failed to claim resource %d!\n", i);
			goto err;
		}
	}

	r = &dev->resources[aoa_resource_i2smmio];
	rlen = resource_size(r);
	if (rlen < sizeof(struct i2s_interface_regs))
		goto err;
	dev->intfregs = ioremap(r->start, rlen);

	r = &dev->resources[aoa_resource_txdbdma];
	rlen = resource_size(r);
	if (rlen < sizeof(struct dbdma_regs))
		goto err;
	dev->out.dbdma = ioremap(r->start, rlen);

	r = &dev->resources[aoa_resource_rxdbdma];
	rlen = resource_size(r);
	if (rlen < sizeof(struct dbdma_regs))
		goto err;
	dev->in.dbdma = ioremap(r->start, rlen);

	if (!dev->intfregs || !dev->out.dbdma || !dev->in.dbdma)
		goto err;

	if (alloc_dbdma_descriptor_ring(dev, &dev->out.dbdma_ring,
					MAX_DBDMA_COMMANDS))
		goto err;
	if (alloc_dbdma_descriptor_ring(dev, &dev->in.dbdma_ring,
					MAX_DBDMA_COMMANDS))
		goto err;

	if (i2sbus_control_add_dev(dev->control, dev)) {
		printk(KERN_ERR "i2sbus: control layer didn't like bus\n");
		goto err;
	}

	if (soundbus_add_one(&dev->sound)) {
		printk(KERN_DEBUG "i2sbus: device registration error!\n");
		goto err;
	}

	/* enable this cell */
	i2sbus_control_cell(dev->control, dev, 1);
	i2sbus_control_enable(dev->control, dev);
	i2sbus_control_clock(dev->control, dev, 1);

	return 1;
 err:
	for (i=0;i<3;i++)
		if (dev->interrupts[i] != -1)
			free_irq(dev->interrupts[i], dev);
	free_dbdma_descriptor_ring(dev, &dev->out.dbdma_ring);
	free_dbdma_descriptor_ring(dev, &dev->in.dbdma_ring);
	if (dev->intfregs) iounmap(dev->intfregs);
	if (dev->out.dbdma) iounmap(dev->out.dbdma);
	if (dev->in.dbdma) iounmap(dev->in.dbdma);
	for (i=0;i<3;i++)
		if (dev->allocated_resource[i])
			release_and_free_resource(dev->allocated_resource[i]);
	mutex_destroy(&dev->lock);
	kfree(dev);
	return 0;
}

static int i2sbus_probe(struct macio_dev* dev, const struct of_device_id *match)
{
	struct device_node *np = NULL;
	int got = 0, err;
	struct i2sbus_control *control = NULL;

	err = i2sbus_control_init(dev, &control);
	if (err)
		return err;
	if (!control) {
		printk(KERN_ERR "i2sbus_control_init API breakage\n");
		return -ENODEV;
	}

	while ((np = of_get_next_child(dev->ofdev.dev.of_node, np))) {
		if (of_device_is_compatible(np, "i2sbus") ||
		    of_device_is_compatible(np, "i2s-modem")) {
			got += i2sbus_add_dev(dev, control, np);
		}
	}

	if (!got) {
		/* found none, clean up */
		i2sbus_control_destroy(control);
		return -ENODEV;
	}

	dev_set_drvdata(&dev->ofdev.dev, control);

	return 0;
}

static int i2sbus_remove(struct macio_dev* dev)
{
	struct i2sbus_control *control = dev_get_drvdata(&dev->ofdev.dev);
	struct i2sbus_dev *i2sdev, *tmp;

	list_for_each_entry_safe(i2sdev, tmp, &control->list, item)
		soundbus_remove_one(&i2sdev->sound);

	return 0;
}

#ifdef CONFIG_PM
static int i2sbus_suspend(struct macio_dev* dev, pm_message_t state)
{
	struct i2sbus_control *control = dev_get_drvdata(&dev->ofdev.dev);
	struct codec_info_item *cii;
	struct i2sbus_dev* i2sdev;
	int err, ret = 0;

	list_for_each_entry(i2sdev, &control->list, item) {
		/* Notify Alsa */
		if (i2sdev->sound.pcm) {
			/* Suspend PCM streams */
			snd_pcm_suspend_all(i2sdev->sound.pcm);
		}

		/* Notify codecs */
		list_for_each_entry(cii, &i2sdev->sound.codec_list, list) {
			err = 0;
			if (cii->codec->suspend)
				err = cii->codec->suspend(cii, state);
			if (err)
				ret = err;
		}

		/* wait until streams are stopped */
		i2sbus_wait_for_stop_both(i2sdev);
	}

	return ret;
}

static int i2sbus_resume(struct macio_dev* dev)
{
	struct i2sbus_control *control = dev_get_drvdata(&dev->ofdev.dev);
	struct codec_info_item *cii;
	struct i2sbus_dev* i2sdev;
	int err, ret = 0;

	list_for_each_entry(i2sdev, &control->list, item) {
		/* reset i2s bus format etc. */
		i2sbus_pcm_prepare_both(i2sdev);

		/* Notify codecs so they can re-initialize */
		list_for_each_entry(cii, &i2sdev->sound.codec_list, list) {
			err = 0;
			if (cii->codec->resume)
				err = cii->codec->resume(cii);
			if (err)
				ret = err;
		}
	}

	return ret;
}
#endif /* CONFIG_PM */

static int i2sbus_shutdown(struct macio_dev* dev)
{
	return 0;
}

static struct macio_driver i2sbus_drv = {
	.driver = {
		.name = "soundbus-i2s",
		.owner = THIS_MODULE,
		.of_match_table = i2sbus_match,
	},
	.probe = i2sbus_probe,
	.remove = i2sbus_remove,
#ifdef CONFIG_PM
	.suspend = i2sbus_suspend,
	.resume = i2sbus_resume,
#endif
	.shutdown = i2sbus_shutdown,
};

static int __init soundbus_i2sbus_init(void)
{
	return macio_register_driver(&i2sbus_drv);
}

static void __exit soundbus_i2sbus_exit(void)
{
	macio_unregister_driver(&i2sbus_drv);
}

module_init(soundbus_i2sbus_init);
module_exit(soundbus_i2sbus_exit);
