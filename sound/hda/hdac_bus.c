// SPDX-License-Identifier: GPL-2.0-only
/*
 * HD-audio core bus driver
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/export.h>
#include <sound/hdaudio.h>
#include "local.h"
#include "trace.h"

static void snd_hdac_bus_process_unsol_events(struct work_struct *work);

static const struct hdac_bus_ops default_ops = {
	.command = snd_hdac_bus_send_cmd,
	.get_response = snd_hdac_bus_get_response,
};

/**
 * snd_hdac_bus_init - initialize a HD-audio bas bus
 * @bus: the pointer to bus object
 * @ops: bus verb operators
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hdac_bus_init(struct hdac_bus *bus, struct device *dev,
		      const struct hdac_bus_ops *ops)
{
	memset(bus, 0, sizeof(*bus));
	bus->dev = dev;
	if (ops)
		bus->ops = ops;
	else
		bus->ops = &default_ops;
	bus->dma_type = SNDRV_DMA_TYPE_DEV;
	INIT_LIST_HEAD(&bus->stream_list);
	INIT_LIST_HEAD(&bus->codec_list);
	INIT_WORK(&bus->unsol_work, snd_hdac_bus_process_unsol_events);
	spin_lock_init(&bus->reg_lock);
	mutex_init(&bus->cmd_mutex);
	mutex_init(&bus->lock);
	INIT_LIST_HEAD(&bus->hlink_list);
	bus->irq = -1;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_bus_init);

/**
 * snd_hdac_bus_exit - clean up a HD-audio bas bus
 * @bus: the pointer to bus object
 */
void snd_hdac_bus_exit(struct hdac_bus *bus)
{
	WARN_ON(!list_empty(&bus->stream_list));
	WARN_ON(!list_empty(&bus->codec_list));
	cancel_work_sync(&bus->unsol_work);
}
EXPORT_SYMBOL_GPL(snd_hdac_bus_exit);

/**
 * snd_hdac_bus_exec_verb - execute a HD-audio verb on the given bus
 * @bus: bus object
 * @cmd: HD-audio encoded verb
 * @res: pointer to store the response, NULL if performing asynchronously
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hdac_bus_exec_verb(struct hdac_bus *bus, unsigned int addr,
			   unsigned int cmd, unsigned int *res)
{
	int err;

	mutex_lock(&bus->cmd_mutex);
	err = snd_hdac_bus_exec_verb_unlocked(bus, addr, cmd, res);
	mutex_unlock(&bus->cmd_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(snd_hdac_bus_exec_verb);

/**
 * snd_hdac_bus_exec_verb_unlocked - unlocked version
 * @bus: bus object
 * @cmd: HD-audio encoded verb
 * @res: pointer to store the response, NULL if performing asynchronously
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hdac_bus_exec_verb_unlocked(struct hdac_bus *bus, unsigned int addr,
				    unsigned int cmd, unsigned int *res)
{
	unsigned int tmp;
	int err;

	if (cmd == ~0)
		return -EINVAL;

	if (res)
		*res = -1;
	else if (bus->sync_write)
		res = &tmp;
	for (;;) {
		trace_hda_send_cmd(bus, cmd);
		err = bus->ops->command(bus, cmd);
		if (err != -EAGAIN)
			break;
		/* process pending verbs */
		err = bus->ops->get_response(bus, addr, &tmp);
		if (err)
			break;
	}
	if (!err && res) {
		err = bus->ops->get_response(bus, addr, res);
		trace_hda_get_response(bus, addr, *res);
	}
	return err;
}
EXPORT_SYMBOL_GPL(snd_hdac_bus_exec_verb_unlocked);

/**
 * snd_hdac_bus_queue_event - add an unsolicited event to queue
 * @bus: the BUS
 * @res: unsolicited event (lower 32bit of RIRB entry)
 * @res_ex: codec addr and flags (upper 32bit or RIRB entry)
 *
 * Adds the given event to the queue.  The events are processed in
 * the workqueue asynchronously.  Call this function in the interrupt
 * hanlder when RIRB receives an unsolicited event.
 */
void snd_hdac_bus_queue_event(struct hdac_bus *bus, u32 res, u32 res_ex)
{
	unsigned int wp;

	if (!bus)
		return;

	trace_hda_unsol_event(bus, res, res_ex);
	wp = (bus->unsol_wp + 1) % HDA_UNSOL_QUEUE_SIZE;
	bus->unsol_wp = wp;

	wp <<= 1;
	bus->unsol_queue[wp] = res;
	bus->unsol_queue[wp + 1] = res_ex;

	schedule_work(&bus->unsol_work);
}
EXPORT_SYMBOL_GPL(snd_hdac_bus_queue_event);

/*
 * process queued unsolicited events
 */
static void snd_hdac_bus_process_unsol_events(struct work_struct *work)
{
	struct hdac_bus *bus = container_of(work, struct hdac_bus, unsol_work);
	struct hdac_device *codec;
	struct hdac_driver *drv;
	unsigned int rp, caddr, res;

	while (bus->unsol_rp != bus->unsol_wp) {
		rp = (bus->unsol_rp + 1) % HDA_UNSOL_QUEUE_SIZE;
		bus->unsol_rp = rp;
		rp <<= 1;
		res = bus->unsol_queue[rp];
		caddr = bus->unsol_queue[rp + 1];
		if (!(caddr & (1 << 4))) /* no unsolicited event? */
			continue;
		codec = bus->caddr_tbl[caddr & 0x0f];
		if (!codec || !codec->dev.driver)
			continue;
		drv = drv_to_hdac_driver(codec->dev.driver);
		if (drv->unsol_event)
			drv->unsol_event(codec, res);
	}
}

/**
 * snd_hdac_bus_add_device - Add a codec to bus
 * @bus: HDA core bus
 * @codec: HDA core device to add
 *
 * Adds the given codec to the list in the bus.  The caddr_tbl array
 * and codec_powered bits are updated, as well.
 * Returns zero if success, or a negative error code.
 */
int snd_hdac_bus_add_device(struct hdac_bus *bus, struct hdac_device *codec)
{
	if (bus->caddr_tbl[codec->addr]) {
		dev_err(bus->dev, "address 0x%x is already occupied\n",
			codec->addr);
		return -EBUSY;
	}

	list_add_tail(&codec->list, &bus->codec_list);
	bus->caddr_tbl[codec->addr] = codec;
	set_bit(codec->addr, &bus->codec_powered);
	bus->num_codecs++;
	return 0;
}

/**
 * snd_hdac_bus_remove_device - Remove a codec from bus
 * @bus: HDA core bus
 * @codec: HDA core device to remove
 */
void snd_hdac_bus_remove_device(struct hdac_bus *bus,
				struct hdac_device *codec)
{
	WARN_ON(bus != codec->bus);
	if (list_empty(&codec->list))
		return;
	list_del_init(&codec->list);
	bus->caddr_tbl[codec->addr] = NULL;
	clear_bit(codec->addr, &bus->codec_powered);
	bus->num_codecs--;
	flush_work(&bus->unsol_work);
}

#ifdef CONFIG_SND_HDA_ALIGNED_MMIO
/* Helpers for aligned read/write of mmio space, for Tegra */
unsigned int snd_hdac_aligned_read(void __iomem *addr, unsigned int mask)
{
	void __iomem *aligned_addr =
		(void __iomem *)((unsigned long)(addr) & ~0x3);
	unsigned int shift = ((unsigned long)(addr) & 0x3) << 3;
	unsigned int v;

	v = readl(aligned_addr);
	return (v >> shift) & mask;
}
EXPORT_SYMBOL_GPL(snd_hdac_aligned_read);

void snd_hdac_aligned_write(unsigned int val, void __iomem *addr,
			    unsigned int mask)
{
	void __iomem *aligned_addr =
		(void __iomem *)((unsigned long)(addr) & ~0x3);
	unsigned int shift = ((unsigned long)(addr) & 0x3) << 3;
	unsigned int v;

	v = readl(aligned_addr);
	v &= ~(mask << shift);
	v |= val << shift;
	writel(v, aligned_addr);
}
EXPORT_SYMBOL_GPL(snd_hdac_aligned_write);
#endif /* CONFIG_SND_HDA_ALIGNED_MMIO */
