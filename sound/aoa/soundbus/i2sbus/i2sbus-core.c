/*
 * i2sbus driver
 *
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 *
 * GPL v2, can be found in COPYING.
 */

#include <linux/module.h>
#include <asm/macio.h>
#include <asm/dbdma.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <sound/driver.h>
#include <sound/core.h>
#include <linux/dma-mapping.h>
#include "../soundbus.h"
#include "i2sbus.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Johannes Berg <johannes@sipsolutions.net>");
MODULE_DESCRIPTION("Apple Soundbus: I2S support");
/* for auto-loading, declare that we handle this weird
 * string that macio puts into the relevant device */
MODULE_ALIAS("of:Ni2sTi2sC");

static struct of_device_id i2sbus_match[] = {
	{ .name = "i2s" },
	{ }
};

static int alloc_dbdma_descriptor_ring(struct i2sbus_dev *i2sdev,
				       struct dbdma_command_mem *r,
				       int numcmds)
{
	/* one more for rounding */
	r->size = (numcmds+1) * sizeof(struct dbdma_cmd);
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
	for (i=0;i<3;i++)
		if (i2sdev->allocated_resource[i])
			release_and_free_resource(i2sdev->allocated_resource[i]);
	free_dbdma_descriptor_ring(i2sdev, &i2sdev->out.dbdma_ring);
	free_dbdma_descriptor_ring(i2sdev, &i2sdev->in.dbdma_ring);
	for (i=0;i<3;i++)
		free_irq(i2sdev->interrupts[i], i2sdev);
	i2sbus_control_remove_dev(i2sdev->control, i2sdev);
	mutex_destroy(&i2sdev->lock);
	kfree(i2sdev);
}

static irqreturn_t i2sbus_bus_intr(int irq, void *devid, struct pt_regs *regs)
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

static int force;
module_param(force, int, 0444);
MODULE_PARM_DESC(force, "Force loading i2sbus even when"
			" no layout-id property is present");

/* FIXME: look at device node refcounting */
static int i2sbus_add_dev(struct macio_dev *macio,
			  struct i2sbus_control *control,
			  struct device_node *np)
{
	struct i2sbus_dev *dev;
	struct device_node *child = NULL, *sound = NULL;
	int i;
	static const char *rnames[] = { "i2sbus: %s (control)",
					"i2sbus: %s (tx)",
					"i2sbus: %s (rx)" };
	static irqreturn_t (*ints[])(int irq, void *devid,
				     struct pt_regs *regs) = {
		i2sbus_bus_intr,
		i2sbus_tx_intr,
		i2sbus_rx_intr
	};

	if (strlen(np->name) != 5)
		return 0;
	if (strncmp(np->name, "i2s-", 4))
		return 0;

	if (np->n_intrs != 3)
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
		u32 *layout_id;
		layout_id = (u32*) get_property(sound, "layout-id", NULL);
		if (layout_id) {
			snprintf(dev->sound.modalias, 32,
				 "sound-layout-%d", *layout_id);
			force = 1;
		}
	}
	/* for the time being, until we can handle non-layout-id
	 * things in some fabric, refuse to attach if there is no
	 * layout-id property or we haven't been forced to attach.
	 * When there are two i2s busses and only one has a layout-id,
	 * then this depends on the order, but that isn't important
	 * either as the second one in that case is just a modem. */
	if (!force) {
		kfree(dev);
		return -ENODEV;
	}

	mutex_init(&dev->lock);
	spin_lock_init(&dev->low_lock);
	dev->sound.ofdev.node = np;
	dev->sound.ofdev.dma_mask = macio->ofdev.dma_mask;
	dev->sound.ofdev.dev.dma_mask = &dev->sound.ofdev.dma_mask;
	dev->sound.ofdev.dev.parent = &macio->ofdev.dev;
	dev->sound.ofdev.dev.release = i2sbus_release_dev;
	dev->sound.attach_codec = i2sbus_attach_codec;
	dev->sound.detach_codec = i2sbus_detach_codec;
	dev->sound.pcmid = -1;
	dev->macio = macio;
	dev->control = control;
	dev->bus_number = np->name[4] - 'a';
	INIT_LIST_HEAD(&dev->sound.codec_list);

	for (i=0;i<3;i++) {
		dev->interrupts[i] = -1;
		snprintf(dev->rnames[i], sizeof(dev->rnames[i]), rnames[i], np->name);
	}
	for (i=0;i<3;i++) {
		if (request_irq(np->intrs[i].line, ints[i], 0, dev->rnames[i], dev))
			goto err;
		dev->interrupts[i] = np->intrs[i].line;
	}

	for (i=0;i<3;i++) {
		if (of_address_to_resource(np, i, &dev->resources[i]))
			goto err;
		/* if only we could use our resource dev->resources[i]...
		 * but request_resource doesn't know about parents and
		 * contained resources... */
		dev->allocated_resource[i] = 
			request_mem_region(dev->resources[i].start,
					   dev->resources[i].end -
					   dev->resources[i].start + 1,
					   dev->rnames[i]);
		if (!dev->allocated_resource[i]) {
			printk(KERN_ERR "i2sbus: failed to claim resource %d!\n", i);
			goto err;
		}
	}
	/* should do sanity checking here about length of them */
	dev->intfregs = ioremap(dev->resources[0].start,
				dev->resources[0].end-dev->resources[0].start+1);
	dev->out.dbdma = ioremap(dev->resources[1].start,
			 	 dev->resources[1].end-dev->resources[1].start+1);
	dev->in.dbdma = ioremap(dev->resources[2].start,
				dev->resources[2].end-dev->resources[2].start+1);
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

	while ((np = of_get_next_child(dev->ofdev.node, np))) {
		if (device_is_compatible(np, "i2sbus") ||
		    device_is_compatible(np, "i2s-modem")) {
			got += i2sbus_add_dev(dev, control, np);
		}
	}

	if (!got) {
		/* found none, clean up */
		i2sbus_control_destroy(control);
		return -ENODEV;
	}

	dev->ofdev.dev.driver_data = control;

	return 0;
}

static int i2sbus_remove(struct macio_dev* dev)
{
	struct i2sbus_control *control = dev->ofdev.dev.driver_data;
	struct i2sbus_dev *i2sdev, *tmp;

	list_for_each_entry_safe(i2sdev, tmp, &control->list, item)
		soundbus_remove_one(&i2sdev->sound);

	return 0;
}

#ifdef CONFIG_PM
static int i2sbus_suspend(struct macio_dev* dev, pm_message_t state)
{
	struct i2sbus_control *control = dev->ofdev.dev.driver_data;
	struct codec_info_item *cii;
	struct i2sbus_dev* i2sdev;
	int err, ret = 0;

	list_for_each_entry(i2sdev, &control->list, item) {
		/* Notify Alsa */
		if (i2sdev->sound.pcm) {
			/* Suspend PCM streams */
			snd_pcm_suspend_all(i2sdev->sound.pcm);
			/* Probably useless as we handle
			 * power transitions ourselves */
			snd_power_change_state(i2sdev->sound.pcm->card,
					       SNDRV_CTL_POWER_D3hot);
		}
		/* Notify codecs */
		list_for_each_entry(cii, &i2sdev->sound.codec_list, list) {
			err = 0;
			if (cii->codec->suspend)
				err = cii->codec->suspend(cii, state);
			if (err)
				ret = err;
		}
	}
	return ret;
}

static int i2sbus_resume(struct macio_dev* dev)
{
	struct i2sbus_control *control = dev->ofdev.dev.driver_data;
	struct codec_info_item *cii;
	struct i2sbus_dev* i2sdev;
	int err, ret = 0;

	list_for_each_entry(i2sdev, &control->list, item) {
		/* Notify codecs so they can re-initialize */
		list_for_each_entry(cii, &i2sdev->sound.codec_list, list) {
			err = 0;
			if (cii->codec->resume)
				err = cii->codec->resume(cii);
			if (err)
				ret = err;
		}
		/* Notify Alsa */
		if (i2sdev->sound.pcm) {
			/* Same comment as above, probably useless */
			snd_power_change_state(i2sdev->sound.pcm->card,
					       SNDRV_CTL_POWER_D0);
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
	.name = "soundbus-i2s",
	.owner = THIS_MODULE,
	.match_table = i2sbus_match,
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
