/*
 * ds.h -- 16-bit PCMCIA core support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * (C) 1999		David A. Hinds
 * (C) 2003 - 2004	Dominik Brodowski
 */

#ifndef _LINUX_DS_H
#define _LINUX_DS_H

#ifdef __KERNEL__
#include <linux/mod_devicetable.h>
#endif

#include <pcmcia/bulkmem.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/device_id.h>

typedef struct tuple_parse_t {
    tuple_t		tuple;
    cisdata_t		data[255];
    cisparse_t		parse;
} tuple_parse_t;

typedef struct win_info_t {
    window_handle_t	handle;
    win_req_t		window;
    memreq_t		map;
} win_info_t;
    
typedef struct bind_info_t {
    dev_info_t		dev_info;
    u_char		function;
    struct dev_link_t	*instance;
    char		name[DEV_NAME_LEN];
    u_short		major, minor;
    void		*next;
} bind_info_t;

typedef struct mtd_info_t {
    dev_info_t		dev_info;
    u_int		Attributes;
    u_int		CardOffset;
} mtd_info_t;

typedef union ds_ioctl_arg_t {
    adjust_t		adjust;
    config_info_t	config;
    tuple_t		tuple;
    tuple_parse_t	tuple_parse;
    client_req_t	client_req;
    cs_status_t		status;
    conf_reg_t		conf_reg;
    cisinfo_t		cisinfo;
    region_info_t	region;
    bind_info_t		bind_info;
    mtd_info_t		mtd_info;
    win_info_t		win_info;
    cisdump_t		cisdump;
} ds_ioctl_arg_t;

#define DS_ADJUST_RESOURCE_INFO		_IOWR('d', 2, adjust_t)
#define DS_GET_CONFIGURATION_INFO	_IOWR('d', 3, config_info_t)
#define DS_GET_FIRST_TUPLE		_IOWR('d', 4, tuple_t)
#define DS_GET_NEXT_TUPLE		_IOWR('d', 5, tuple_t)
#define DS_GET_TUPLE_DATA		_IOWR('d', 6, tuple_parse_t)
#define DS_PARSE_TUPLE			_IOWR('d', 7, tuple_parse_t)
#define DS_RESET_CARD			_IO  ('d', 8)
#define DS_GET_STATUS			_IOWR('d', 9, cs_status_t)
#define DS_ACCESS_CONFIGURATION_REGISTER _IOWR('d', 10, conf_reg_t)
#define DS_VALIDATE_CIS			_IOR ('d', 11, cisinfo_t)
#define DS_SUSPEND_CARD			_IO  ('d', 12)
#define DS_RESUME_CARD			_IO  ('d', 13)
#define DS_EJECT_CARD			_IO  ('d', 14)
#define DS_INSERT_CARD			_IO  ('d', 15)
#define DS_GET_FIRST_REGION		_IOWR('d', 16, region_info_t)
#define DS_GET_NEXT_REGION		_IOWR('d', 17, region_info_t)
#define DS_REPLACE_CIS			_IOWR('d', 18, cisdump_t)
#define DS_GET_FIRST_WINDOW		_IOR ('d', 19, win_info_t)
#define DS_GET_NEXT_WINDOW		_IOWR('d', 20, win_info_t)
#define DS_GET_MEM_PAGE			_IOWR('d', 21, win_info_t)

#define DS_BIND_REQUEST			_IOWR('d', 60, bind_info_t)
#define DS_GET_DEVICE_INFO		_IOWR('d', 61, bind_info_t) 
#define DS_GET_NEXT_DEVICE		_IOWR('d', 62, bind_info_t) 
#define DS_UNBIND_REQUEST		_IOW ('d', 63, bind_info_t)
#define DS_BIND_MTD			_IOWR('d', 64, mtd_info_t)

#ifdef __KERNEL__
#include <linux/device.h>

typedef struct dev_node_t {
    char		dev_name[DEV_NAME_LEN];
    u_short		major, minor;
    struct dev_node_t	*next;
} dev_node_t;

typedef struct dev_link_t {
    dev_node_t		*dev;
    u_int		state, open;
    wait_queue_head_t	pending;
    client_handle_t	handle;
    io_req_t		io;
    irq_req_t		irq;
    config_req_t	conf;
    window_handle_t	win;
    void		*priv;
    struct dev_link_t	*next;
} dev_link_t;

/* Flags for device state */
#define DEV_PRESENT		0x01
#define DEV_CONFIG		0x02
#define DEV_STALE_CONFIG	0x04	/* release on close */
#define DEV_STALE_LINK		0x08	/* detach on release */
#define DEV_CONFIG_PENDING	0x10
#define DEV_RELEASE_PENDING	0x20
#define DEV_SUSPEND		0x40
#define DEV_BUSY		0x80

#define DEV_OK(l) \
    ((l) && ((l->state & ~DEV_BUSY) == (DEV_CONFIG|DEV_PRESENT)))


struct pcmcia_socket;

struct pcmcia_driver {
	int (*probe)		(struct pcmcia_device *dev);
	void (*remove)		(struct pcmcia_device *dev);

	int (*suspend)		(struct pcmcia_device *dev);
	int (*resume)		(struct pcmcia_device *dev);

	struct module		*owner;
	struct pcmcia_device_id	*id_table;
	struct device_driver	drv;
};

/* driver registration */
int pcmcia_register_driver(struct pcmcia_driver *driver);
void pcmcia_unregister_driver(struct pcmcia_driver *driver);

struct pcmcia_device {
	/* the socket and the device_no [for multifunction devices]
	   uniquely define a pcmcia_device */
	struct pcmcia_socket	*socket;

	char			*devname;

	u8			device_no;

	/* the hardware "function" device; certain subdevices can
	 * share one hardware "function" device. */
	u8			func;

	struct list_head	socket_device_list;

	/* deprecated, a cleaned up version will be moved into this
	   struct soon */
	dev_link_t		*instance;
	u_int			state;

	/* information about this device */
	u8			has_manf_id:1;
	u8			has_card_id:1;
	u8			has_func_id:1;

	u8			allow_func_id_match:1;
	u8			reserved:4;

	u8			func_id;
	u16			manf_id;
	u16			card_id;

	char *			prod_id[4];

	/* device driver wanted by cardmgr */
	struct pcmcia_driver *	cardmgr;

	struct device		dev;
};

#define to_pcmcia_dev(n) container_of(n, struct pcmcia_device, dev)
#define to_pcmcia_drv(n) container_of(n, struct pcmcia_driver, drv)

#define handle_to_pdev(handle) (handle)
#define handle_to_dev(handle) (handle->dev)

#define dev_to_instance(dev) (dev->instance)

/* error reporting */
void cs_error(client_handle_t handle, int func, int ret);

#endif /* __KERNEL__ */
#endif /* _LINUX_DS_H */
