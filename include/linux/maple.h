#ifndef __LINUX_MAPLE_H
#define __LINUX_MAPLE_H

#include <linux/device.h>

extern struct bus_type maple_bus_type;

/* Maple Bus command and response codes */
enum maple_code {
	MAPLE_RESPONSE_FILEERR = -5,
	MAPLE_RESPONSE_AGAIN = -4,	/* request should be retransmitted */
	MAPLE_RESPONSE_BADCMD = -3,
	MAPLE_RESPONSE_BADFUNC = -2,
	MAPLE_RESPONSE_NONE = -1,	/* unit didn't respond at all */
	MAPLE_COMMAND_DEVINFO = 1,
	MAPLE_COMMAND_ALLINFO = 2,
	MAPLE_COMMAND_RESET = 3,
	MAPLE_COMMAND_KILL = 4,
	MAPLE_RESPONSE_DEVINFO = 5,
	MAPLE_RESPONSE_ALLINFO = 6,
	MAPLE_RESPONSE_OK = 7,
	MAPLE_RESPONSE_DATATRF = 8,
	MAPLE_COMMAND_GETCOND = 9,
	MAPLE_COMMAND_GETMINFO = 10,
	MAPLE_COMMAND_BREAD = 11,
	MAPLE_COMMAND_BWRITE = 12,
	MAPLE_COMMAND_SETCOND = 14
};

struct mapleq {
	struct list_head list;
	struct maple_device *dev;
	void *sendbuf, *recvbuf, *recvbufdcsp;
	unsigned char length;
	enum maple_code command;
};

struct maple_devinfo {
	unsigned long function;
	unsigned long function_data[3];
	unsigned char area_code;
	unsigned char connector_direction;
	char product_name[31];
	char product_licence[61];
	unsigned short standby_power;
	unsigned short max_power;
};

struct maple_device {
	struct maple_driver *driver;
	struct mapleq *mq;
	void *private_data;
	void (*callback) (struct mapleq * mq);
	unsigned long when, interval, function;
	struct maple_devinfo devinfo;
	unsigned char port, unit;
	char product_name[32];
	char product_licence[64];
	struct device dev;
};

struct maple_driver {
	unsigned long function;
	int (*connect) (struct maple_device * dev);
	void (*disconnect) (struct maple_device * dev);
	struct device_driver drv;
	int registered;
};

void maple_getcond_callback(struct maple_device *dev,
			    void (*callback) (struct mapleq * mq),
			    unsigned long interval,
			    unsigned long function);
int maple_driver_register(struct device_driver *drv);
void maple_add_packet(struct mapleq *mq);

#define to_maple_dev(n) container_of(n, struct maple_device, dev)
#define to_maple_driver(n) container_of(n, struct maple_driver, drv)

#endif				/* __LINUX_MAPLE_H */
