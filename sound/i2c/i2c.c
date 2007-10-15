/*
 *   Generic i2c interface for ALSA
 *
 *   (c) 1998 Gerd Knorr <kraxel@cs.tu-berlin.de>
 *   Modified for the ALSA driver by Jaroslav Kysela <perex@perex.cz>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <sound/core.h>
#include <sound/i2c.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("Generic i2c interface for ALSA");
MODULE_LICENSE("GPL");

static int snd_i2c_bit_sendbytes(struct snd_i2c_device *device,
				 unsigned char *bytes, int count);
static int snd_i2c_bit_readbytes(struct snd_i2c_device *device,
				 unsigned char *bytes, int count);
static int snd_i2c_bit_probeaddr(struct snd_i2c_bus *bus,
				 unsigned short addr);

static struct snd_i2c_ops snd_i2c_bit_ops = {
	.sendbytes = snd_i2c_bit_sendbytes,
	.readbytes = snd_i2c_bit_readbytes,
	.probeaddr = snd_i2c_bit_probeaddr,
};

static int snd_i2c_bus_free(struct snd_i2c_bus *bus)
{
	struct snd_i2c_bus *slave;
	struct snd_i2c_device *device;

	snd_assert(bus != NULL, return -EINVAL);
	while (!list_empty(&bus->devices)) {
		device = snd_i2c_device(bus->devices.next);
		snd_i2c_device_free(device);
	}
	if (bus->master)
		list_del(&bus->buses);
	else {
		while (!list_empty(&bus->buses)) {
			slave = snd_i2c_slave_bus(bus->buses.next);
			snd_device_free(bus->card, slave);
		}
	}
	if (bus->private_free)
		bus->private_free(bus);
	kfree(bus);
	return 0;
}

static int snd_i2c_bus_dev_free(struct snd_device *device)
{
	struct snd_i2c_bus *bus = device->device_data;
	return snd_i2c_bus_free(bus);
}

int snd_i2c_bus_create(struct snd_card *card, const char *name,
		       struct snd_i2c_bus *master, struct snd_i2c_bus **ri2c)
{
	struct snd_i2c_bus *bus;
	int err;
	static struct snd_device_ops ops = {
		.dev_free =	snd_i2c_bus_dev_free,
	};

	*ri2c = NULL;
	bus = kzalloc(sizeof(*bus), GFP_KERNEL);
	if (bus == NULL)
		return -ENOMEM;
	mutex_init(&bus->lock_mutex);
	INIT_LIST_HEAD(&bus->devices);
	INIT_LIST_HEAD(&bus->buses);
	bus->card = card;
	bus->ops = &snd_i2c_bit_ops;
	if (master) {
		list_add_tail(&bus->buses, &master->buses);
		bus->master = master;
	}
	strlcpy(bus->name, name, sizeof(bus->name));
	if ((err = snd_device_new(card, SNDRV_DEV_BUS, bus, &ops)) < 0) {
		snd_i2c_bus_free(bus);
		return err;
	}
	*ri2c = bus;
	return 0;
}

EXPORT_SYMBOL(snd_i2c_bus_create);

int snd_i2c_device_create(struct snd_i2c_bus *bus, const char *name,
			  unsigned char addr, struct snd_i2c_device **rdevice)
{
	struct snd_i2c_device *device;

	*rdevice = NULL;
	snd_assert(bus != NULL, return -EINVAL);
	device = kzalloc(sizeof(*device), GFP_KERNEL);
	if (device == NULL)
		return -ENOMEM;
	device->addr = addr;
	strlcpy(device->name, name, sizeof(device->name));
	list_add_tail(&device->list, &bus->devices);
	device->bus = bus;
	*rdevice = device;
	return 0;
}

EXPORT_SYMBOL(snd_i2c_device_create);

int snd_i2c_device_free(struct snd_i2c_device *device)
{
	if (device->bus)
		list_del(&device->list);
	if (device->private_free)
		device->private_free(device);
	kfree(device);
	return 0;
}

EXPORT_SYMBOL(snd_i2c_device_free);

int snd_i2c_sendbytes(struct snd_i2c_device *device, unsigned char *bytes, int count)
{
	return device->bus->ops->sendbytes(device, bytes, count);
}

EXPORT_SYMBOL(snd_i2c_sendbytes);

int snd_i2c_readbytes(struct snd_i2c_device *device, unsigned char *bytes, int count)
{
	return device->bus->ops->readbytes(device, bytes, count);
}

EXPORT_SYMBOL(snd_i2c_readbytes);

int snd_i2c_probeaddr(struct snd_i2c_bus *bus, unsigned short addr)
{
	return bus->ops->probeaddr(bus, addr);
}

EXPORT_SYMBOL(snd_i2c_probeaddr);

/*
 *  bit-operations
 */

static inline void snd_i2c_bit_hw_start(struct snd_i2c_bus *bus)
{
	if (bus->hw_ops.bit->start)
		bus->hw_ops.bit->start(bus);
}

static inline void snd_i2c_bit_hw_stop(struct snd_i2c_bus *bus)
{
	if (bus->hw_ops.bit->stop)
		bus->hw_ops.bit->stop(bus);
}

static void snd_i2c_bit_direction(struct snd_i2c_bus *bus, int clock, int data)
{
	if (bus->hw_ops.bit->direction)
		bus->hw_ops.bit->direction(bus, clock, data);
}

static void snd_i2c_bit_set(struct snd_i2c_bus *bus, int clock, int data)
{
	bus->hw_ops.bit->setlines(bus, clock, data);
}

#if 0
static int snd_i2c_bit_clock(struct snd_i2c_bus *bus)
{
	if (bus->hw_ops.bit->getclock)
		return bus->hw_ops.bit->getclock(bus);
	return -ENXIO;
}
#endif

static int snd_i2c_bit_data(struct snd_i2c_bus *bus, int ack)
{
	return bus->hw_ops.bit->getdata(bus, ack);
}

static void snd_i2c_bit_start(struct snd_i2c_bus *bus)
{
	snd_i2c_bit_hw_start(bus);
	snd_i2c_bit_direction(bus, 1, 1);	/* SCL - wr, SDA - wr */
	snd_i2c_bit_set(bus, 1, 1);
	snd_i2c_bit_set(bus, 1, 0);
	snd_i2c_bit_set(bus, 0, 0);
}

static void snd_i2c_bit_stop(struct snd_i2c_bus *bus)
{
	snd_i2c_bit_set(bus, 0, 0);
	snd_i2c_bit_set(bus, 1, 0);
	snd_i2c_bit_set(bus, 1, 1);
	snd_i2c_bit_hw_stop(bus);
}

static void snd_i2c_bit_send(struct snd_i2c_bus *bus, int data)
{
	snd_i2c_bit_set(bus, 0, data);
	snd_i2c_bit_set(bus, 1, data);
	snd_i2c_bit_set(bus, 0, data);
}

static int snd_i2c_bit_ack(struct snd_i2c_bus *bus)
{
	int ack;

	snd_i2c_bit_set(bus, 0, 1);
	snd_i2c_bit_set(bus, 1, 1);
	snd_i2c_bit_direction(bus, 1, 0);	/* SCL - wr, SDA - rd */
	ack = snd_i2c_bit_data(bus, 1);
	snd_i2c_bit_direction(bus, 1, 1);	/* SCL - wr, SDA - wr */
	snd_i2c_bit_set(bus, 0, 1);
	return ack ? -EIO : 0;
}

static int snd_i2c_bit_sendbyte(struct snd_i2c_bus *bus, unsigned char data)
{
	int i, err;

	for (i = 7; i >= 0; i--)
		snd_i2c_bit_send(bus, !!(data & (1 << i)));
	if ((err = snd_i2c_bit_ack(bus)) < 0)
		return err;
	return 0;
}

static int snd_i2c_bit_readbyte(struct snd_i2c_bus *bus, int last)
{
	int i;
	unsigned char data = 0;

	snd_i2c_bit_set(bus, 0, 1);
	snd_i2c_bit_direction(bus, 1, 0);	/* SCL - wr, SDA - rd */
	for (i = 7; i >= 0; i--) {
		snd_i2c_bit_set(bus, 1, 1);
		if (snd_i2c_bit_data(bus, 0))
			data |= (1 << i);
		snd_i2c_bit_set(bus, 0, 1);
	}
	snd_i2c_bit_direction(bus, 1, 1);	/* SCL - wr, SDA - wr */
	snd_i2c_bit_send(bus, !!last);
	return data;
}

static int snd_i2c_bit_sendbytes(struct snd_i2c_device *device,
				 unsigned char *bytes, int count)
{
	struct snd_i2c_bus *bus = device->bus;
	int err, res = 0;

	if (device->flags & SND_I2C_DEVICE_ADDRTEN)
		return -EIO;		/* not yet implemented */
	snd_i2c_bit_start(bus);
	if ((err = snd_i2c_bit_sendbyte(bus, device->addr << 1)) < 0) {
		snd_i2c_bit_hw_stop(bus);
		return err;
	}
	while (count-- > 0) {
		if ((err = snd_i2c_bit_sendbyte(bus, *bytes++)) < 0) {
			snd_i2c_bit_hw_stop(bus);
			return err;
		}
		res++;
	}
	snd_i2c_bit_stop(bus);
	return res;
}

static int snd_i2c_bit_readbytes(struct snd_i2c_device *device,
				 unsigned char *bytes, int count)
{
	struct snd_i2c_bus *bus = device->bus;
	int err, res = 0;

	if (device->flags & SND_I2C_DEVICE_ADDRTEN)
		return -EIO;		/* not yet implemented */
	snd_i2c_bit_start(bus);
	if ((err = snd_i2c_bit_sendbyte(bus, (device->addr << 1) | 1)) < 0) {
		snd_i2c_bit_hw_stop(bus);
		return err;
	}
	while (count-- > 0) {
		if ((err = snd_i2c_bit_readbyte(bus, count == 0)) < 0) {
			snd_i2c_bit_hw_stop(bus);
			return err;
		}
		*bytes++ = (unsigned char)err;
		res++;
	}
	snd_i2c_bit_stop(bus);
	return res;
}

static int snd_i2c_bit_probeaddr(struct snd_i2c_bus *bus, unsigned short addr)
{
	int err;

	if (addr & 0x8000)	/* 10-bit address */
		return -EIO;	/* not yet implemented */
	if (addr & 0x7f80)	/* invalid address */
		return -EINVAL;
	snd_i2c_bit_start(bus);
	err = snd_i2c_bit_sendbyte(bus, addr << 1);
	snd_i2c_bit_stop(bus);
	return err;
}


static int __init alsa_i2c_init(void)
{
	return 0;
}

static void __exit alsa_i2c_exit(void)
{
}

module_init(alsa_i2c_init)
module_exit(alsa_i2c_exit)
