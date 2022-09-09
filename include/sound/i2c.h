/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __SOUND_I2C_H
#define __SOUND_I2C_H

/*
 */

#define SND_I2C_DEVICE_ADDRTEN	(1<<0)	/* 10-bit I2C address */

struct snd_i2c_device {
	struct list_head list;
	struct snd_i2c_bus *bus;	/* I2C bus */
	char name[32];		/* some useful device name */
	unsigned short flags;	/* device flags */
	unsigned short addr;	/* device address (might be 10-bit) */
	unsigned long private_value;
	void *private_data;
	void (*private_free)(struct snd_i2c_device *device);
};

#define snd_i2c_device(n) list_entry(n, struct snd_i2c_device, list)

struct snd_i2c_bit_ops {
	void (*start)(struct snd_i2c_bus *bus);	/* transfer start */
	void (*stop)(struct snd_i2c_bus *bus);	/* transfer stop */
	void (*direction)(struct snd_i2c_bus *bus, int clock, int data);  /* set line direction (0 = write, 1 = read) */
	void (*setlines)(struct snd_i2c_bus *bus, int clock, int data);
	int (*getclock)(struct snd_i2c_bus *bus);
	int (*getdata)(struct snd_i2c_bus *bus, int ack);
};

struct snd_i2c_ops {
	int (*sendbytes)(struct snd_i2c_device *device, unsigned char *bytes, int count);
	int (*readbytes)(struct snd_i2c_device *device, unsigned char *bytes, int count);
	int (*probeaddr)(struct snd_i2c_bus *bus, unsigned short addr);
};

struct snd_i2c_bus {
	struct snd_card *card;	/* card which I2C belongs to */
	char name[32];		/* some useful label */

	struct mutex lock_mutex;

	struct snd_i2c_bus *master;	/* master bus when SCK/SCL is shared */
	struct list_head buses;	/* master: slave buses sharing SCK/SCL, slave: link list */

	struct list_head devices; /* attached devices to this bus */

	union {
		struct snd_i2c_bit_ops *bit;
		void *ops;
	} hw_ops;		/* lowlevel operations */
	const struct snd_i2c_ops *ops;	/* midlevel operations */

	unsigned long private_value;
	void *private_data;
	void (*private_free)(struct snd_i2c_bus *bus);
};

#define snd_i2c_slave_bus(n) list_entry(n, struct snd_i2c_bus, buses)

int snd_i2c_bus_create(struct snd_card *card, const char *name,
		       struct snd_i2c_bus *master, struct snd_i2c_bus **ri2c);
int snd_i2c_device_create(struct snd_i2c_bus *bus, const char *name,
			  unsigned char addr, struct snd_i2c_device **rdevice);
int snd_i2c_device_free(struct snd_i2c_device *device);

static inline void snd_i2c_lock(struct snd_i2c_bus *bus)
{
	if (bus->master)
		mutex_lock(&bus->master->lock_mutex);
	else
		mutex_lock(&bus->lock_mutex);
}

static inline void snd_i2c_unlock(struct snd_i2c_bus *bus)
{
	if (bus->master)
		mutex_unlock(&bus->master->lock_mutex);
	else
		mutex_unlock(&bus->lock_mutex);
}

int snd_i2c_sendbytes(struct snd_i2c_device *device, unsigned char *bytes, int count);
int snd_i2c_readbytes(struct snd_i2c_device *device, unsigned char *bytes, int count);
int snd_i2c_probeaddr(struct snd_i2c_bus *bus, unsigned short addr);

#endif /* __SOUND_I2C_H */
