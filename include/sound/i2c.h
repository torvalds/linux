#ifndef __SOUND_I2C_H
#define __SOUND_I2C_H

/*
 *
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
 *
 */

typedef struct _snd_i2c_device snd_i2c_device_t;
typedef struct _snd_i2c_bus snd_i2c_bus_t;

#define SND_I2C_DEVICE_ADDRTEN	(1<<0)	/* 10-bit I2C address */

struct _snd_i2c_device {
	struct list_head list;
	snd_i2c_bus_t *bus;	/* I2C bus */
	char name[32];		/* some useful device name */
	unsigned short flags;	/* device flags */
	unsigned short addr;	/* device address (might be 10-bit) */
	unsigned long private_value;
	void *private_data;
	void (*private_free)(snd_i2c_device_t *device);
};

#define snd_i2c_device(n) list_entry(n, snd_i2c_device_t, list)

typedef struct _snd_i2c_bit_ops {
	void (*start)(snd_i2c_bus_t *bus);	/* transfer start */
	void (*stop)(snd_i2c_bus_t *bus);	/* transfer stop */
	void (*direction)(snd_i2c_bus_t *bus, int clock, int data);  /* set line direction (0 = write, 1 = read) */
	void (*setlines)(snd_i2c_bus_t *bus, int clock, int data);
	int (*getclock)(snd_i2c_bus_t *bus);
	int (*getdata)(snd_i2c_bus_t *bus, int ack);
} snd_i2c_bit_ops_t;

typedef struct _snd_i2c_ops {
	int (*sendbytes)(snd_i2c_device_t *device, unsigned char *bytes, int count);
	int (*readbytes)(snd_i2c_device_t *device, unsigned char *bytes, int count);
	int (*probeaddr)(snd_i2c_bus_t *bus, unsigned short addr);
} snd_i2c_ops_t;

struct _snd_i2c_bus {
	snd_card_t *card;	/* card which I2C belongs to */
	char name[32];		/* some useful label */

	struct semaphore lock_mutex;

	snd_i2c_bus_t *master;	/* master bus when SCK/SCL is shared */
	struct list_head buses;	/* master: slave buses sharing SCK/SCL, slave: link list */

	struct list_head devices; /* attached devices to this bus */

	union {
		snd_i2c_bit_ops_t *bit;
		void *ops;
	} hw_ops;		/* lowlevel operations */
	snd_i2c_ops_t *ops;	/* midlevel operations */

	unsigned long private_value;
	void *private_data;
	void (*private_free)(snd_i2c_bus_t *bus);
};

#define snd_i2c_slave_bus(n) list_entry(n, snd_i2c_bus_t, buses)

int snd_i2c_bus_create(snd_card_t *card, const char *name, snd_i2c_bus_t *master, snd_i2c_bus_t **ri2c);
int snd_i2c_device_create(snd_i2c_bus_t *bus, const char *name, unsigned char addr, snd_i2c_device_t **rdevice);
int snd_i2c_device_free(snd_i2c_device_t *device);

static inline void snd_i2c_lock(snd_i2c_bus_t *bus) {
	if (bus->master)
		down(&bus->master->lock_mutex);
	else
		down(&bus->lock_mutex);
}
static inline void snd_i2c_unlock(snd_i2c_bus_t *bus) {
	if (bus->master)
		up(&bus->master->lock_mutex);
	else
		up(&bus->lock_mutex);
}

int snd_i2c_sendbytes(snd_i2c_device_t *device, unsigned char *bytes, int count);
int snd_i2c_readbytes(snd_i2c_device_t *device, unsigned char *bytes, int count);
int snd_i2c_probeaddr(snd_i2c_bus_t *bus, unsigned short addr);

#endif /* __SOUND_I2C_H */
