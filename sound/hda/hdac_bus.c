/*
 * HD-audio core bus driver
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/export.h>
#include <sound/hdaudio.h>

static void process_unsol_events(struct work_struct *work);

/**
 * snd_hdac_bus_init - initialize a HD-audio bas bus
 * @bus: the pointer to bus object
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hdac_bus_init(struct hdac_bus *bus, struct device *dev,
		      const struct hdac_bus_ops *ops)
{
	memset(bus, 0, sizeof(*bus));
	bus->dev = dev;
	bus->ops = ops;
	INIT_LIST_HEAD(&bus->codec_list);
	INIT_WORK(&bus->unsol_work, process_unsol_events);
	mutex_init(&bus->cmd_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_bus_init);

/**
 * snd_hdac_bus_exit - clean up a HD-audio bas bus
 * @bus: the pointer to bus object
 */
void snd_hdac_bus_exit(struct hdac_bus *bus)
{
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
		err = bus->ops->command(bus, cmd);
		if (err != -EAGAIN)
			break;
		/* process pending verbs */
		err = bus->ops->get_response(bus, addr, &tmp);
		if (err)
			break;
	}
	if (!err && res)
		err = bus->ops->get_response(bus, addr, res);
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
static void process_unsol_events(struct work_struct *work)
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
EXPORT_SYMBOL_GPL(snd_hdac_bus_add_device);

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
}
EXPORT_SYMBOL_GPL(snd_hdac_bus_remove_device);
